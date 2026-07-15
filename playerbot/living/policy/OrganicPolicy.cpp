#include "OrganicPolicy.h"

#include "OrganicActionMetadata.h"

namespace living
{
    namespace
    {
        OrganicPolicyResult Result(OrganicDecision decision, OrganicReasonCode reason, bool knownAction)
        {
            OrganicPolicyResult result;
            result.decision = decision;
            result.reason = reason;
            result.knownAction = knownAction;
            return result;
        }

        // Kind-specific stable deny reasons; everything else uses DeniedByClassification.
        OrganicReasonCode DenyReasonFor(OrganicActionKind kind)
        {
            switch (kind)
            {
                case OrganicActionKind::LEGACY_LOGIN_ROTATION:
                case OrganicActionKind::LEGACY_TIMED_ROTATION:
                    return OrganicReasonCode::LegacyLifecycleExcluded;
                case OrganicActionKind::POPULATION_RESET_RECREATE:
                    return OrganicReasonCode::ManagedOperationRequired;
                case OrganicActionKind::RAW_POPULATION_SQL_RESET:
                    return OrganicReasonCode::RawResetUnsupported;
                case OrganicActionKind::ADMIN_BYPASS_MUTATION:
                    return OrganicReasonCode::NoApprovedReconciler;
                default:
                    return OrganicReasonCode::DeniedByClassification;
            }
        }

        // The three named 0.1 compatibility/recovery actions (0002C). Each returns
        // RequireAudit only when EVERY mandatory eligibility gate for that action is
        // satisfied; any missing gate is a Deny with its own reason code, never a
        // silent downgrade to gameplay. Admitting an ineligible request into the
        // durable 0002B protocol is the failure mode these gates exist to prevent.
        OrganicPolicyResult EvaluateAuditedAction(OrganicRequest const& request)
        {
            // Shared safety envelope: no compatibility action may run in combat,
            // trade, a taxi/transport transition, a BG/arena, or an instance
            // transition, and all are rate limited.
            if (!request.safeForCompatibilityAction)
                return Result(OrganicDecision::Deny, OrganicReasonCode::UnsafeStateForCompatibilityAction, true);
            if (!request.rateLimitOk)
                return Result(OrganicDecision::Deny, OrganicReasonCode::RateLimitExceeded, true);

            switch (request.kind)
            {
                case OrganicActionKind::STUCK_EMERGENCY_TELEPORT: // 0002C C.6/C.7
                    if (!request.recoveryLadderExhausted)
                        return Result(OrganicDecision::Deny, OrganicReasonCode::RecoveryLadderNotExhausted, true);
                    if (request.protectedRealPlayerCommitment && !request.ownerAuthorizedRecovery)
                        return Result(OrganicDecision::Deny, OrganicReasonCode::ProtectedCommitmentBlocksRecovery, true);
                    if (!request.safeDestinationSelected)
                        return Result(OrganicDecision::Deny, OrganicReasonCode::NoSafeDestination, true);
                    return Result(OrganicDecision::RequireAudit, OrganicReasonCode::AuditRequired, true);

                case OrganicActionKind::TRANSPORT_GROUP_SYNC: // 0002C C.5
                    if (!request.protectedRealPlayerCommitment)
                        return Result(OrganicDecision::Deny, OrganicReasonCode::MissingProtectedCommitment, true);
                    if (!request.ownerOnVerifiedTransport)
                        return Result(OrganicDecision::Deny, OrganicReasonCode::OwnerNotOnVerifiedTransport, true);
                    if (!request.nearBoardingContext)
                        return Result(OrganicDecision::Deny, OrganicReasonCode::NotNearBoardingContext, true);
                    if (!request.destinationMapSupported)
                        return Result(OrganicDecision::Deny, OrganicReasonCode::DestinationMapUnsupported, true);
                    return Result(OrganicDecision::RequireAudit, OrganicReasonCode::AuditRequired, true);

                case OrganicActionKind::PUBLIC_TRANSPORT_TRANSFER: // 0002C C.3
                    if (!request.canonicalRouteAllowlisted)
                        return Result(OrganicDecision::Deny, OrganicReasonCode::RouteNotAllowlisted, true);
                    if (!request.typedGoalRouteBound)
                        return Result(OrganicDecision::Deny, OrganicReasonCode::GoalRouteNotBound, true);
                    if (!request.atExactOriginNode)
                        return Result(OrganicDecision::Deny, OrganicReasonCode::NotAtOriginNode, true);
                    if (!request.originWaitSatisfied)
                        return Result(OrganicDecision::Deny, OrganicReasonCode::OriginWaitNotSatisfied, true);
                    return Result(OrganicDecision::RequireAudit, OrganicReasonCode::AuditRequired, true);

                default:
                    // Only the three named actions may be classified RequireAudit; the
                    // metadata completeness test enforces it. Fail closed regardless.
                    return Result(OrganicDecision::Deny, OrganicReasonCode::DeniedByClassification, true);
            }
        }

        // A decision must be bindable to the (guid, nonce) identity root: a zero low
        // GUID or an all-zero nonce cannot be attached to a durable audit row or
        // distinguished from a reused GUID (0003 section 2).
        bool HasValidIdentityRoot(OrganicRequest const& request)
        {
            if (request.characterGuid == 0)
                return false;

            for (uint8_t byte : request.identityNonce)
                if (byte != 0)
                    return true;

            return false;
        }
    }

    OrganicPolicyResult EvaluateOrganicPolicy(OrganicRequest const& request)
    {
        OrganicActionMetadata const* metadata = TryGetOrganicActionMetadata(request.kind);
        bool const knownAction = metadata != nullptr;

        // Explicitly disabled Living Realm is a passthrough: the guard must not
        // alter legacy behavior in any way (LR-001), including for actions it does
        // not know. Only the explicit Disabled mode earns this; every other mode
        // value falls through to fail-closed handling below.
        if (request.mode == LivingRealmMode::Disabled)
        {
            bool const gameplay = knownAction && metadata->classification == OrganicClassification::AllowGameplay;
            return Result(gameplay ? OrganicDecision::AllowGameplay : OrganicDecision::AllowAutomation,
                OrganicReasonCode::LegacyPassthrough, knownAction);
        }

        // Fail closed: unknown actions have no side effects.
        if (!knownAction)
            return Result(OrganicDecision::Deny, OrganicReasonCode::UnknownAction, false);

        // A caller that did not resolve the mode gets a deny, never a passthrough.
        if (request.mode == LivingRealmMode::Unspecified)
            return Result(OrganicDecision::Deny, OrganicReasonCode::ModeUnspecified, true);

        // The source is informational for decisions, but an out-of-range value is
        // corrupted input and fails closed like every other malformed field.
        if (request.source >= OrganicSourceKind::Count)
            return Result(OrganicDecision::Deny, OrganicReasonCode::InvalidSource, true);

        // An enabled realm with an unsupported profile is a configuration error, and
        // an out-of-range mode value is corrupted input; neither may proceed.
        if (request.mode != LivingRealmMode::Organic)
            return Result(OrganicDecision::Deny, OrganicReasonCode::UnsupportedProfile, true);

        // Every enabled-mode decision must be bindable to an identity root.
        if (!HasValidIdentityRoot(request))
            return Result(OrganicDecision::Deny, OrganicReasonCode::InvalidIdentity, true);

        // Fixture-only actions exist solely for FIXTURE identities inside an explicit
        // test profile; production Organic identities can never be authorized.
        if (metadata->classification == OrganicClassification::FixtureOnly)
        {
            if (!request.fixtureTestProfile)
                return Result(OrganicDecision::Deny, OrganicReasonCode::FixtureOutsideTestProfile, true);
            if (request.provenance != BotProvenance::FIXTURE)
                return Result(OrganicDecision::Deny, OrganicReasonCode::FixtureProvenanceRequired, true);
            return Result(OrganicDecision::AllowAutomation, OrganicReasonCode::FixtureAuthorized, true);
        }

        // Fixture identities are fully isolated (0001 invariant 13, 0006 section 7):
        // beyond the fixture-only actions handled above they receive no gameplay,
        // automation, bootstrap, or audited authorization, in any profile. This is
        // what keeps fixtures out of production economy, schedules, fairness, and
        // audit accounting even if a test profile flag is misconfigured on.
        if (request.provenance == BotProvenance::FIXTURE)
            return Result(OrganicDecision::Deny, OrganicReasonCode::FixtureIsolated, true);

        // Managed Organic semantics require ORGANIC_CREATED provenance. Everything
        // else fails closed: LEGACY_UNMANAGED is a mixed population (0002 section 3,
        // 0003 section 2), and an out-of-range value is corrupted input, never an
        // allow.
        if (!SatisfiesOrganicProvenance(request.provenance))
            return Result(OrganicDecision::Deny,
                request.provenance == BotProvenance::LEGACY_UNMANAGED
                    ? OrganicReasonCode::OrganicProvenanceRequired
                    : OrganicReasonCode::InvalidProvenance,
                true);

        switch (metadata->classification)
        {
            case OrganicClassification::AllowGameplay:
                return Result(OrganicDecision::AllowGameplay, OrganicReasonCode::AllowedGameplay, true);

            case OrganicClassification::AllowAutomation:
                return Result(OrganicDecision::AllowAutomation, OrganicReasonCode::AllowedAutomation, true);

            case OrganicClassification::BootstrapOnly:
                if (!request.managedBootstrapActive)
                    return Result(OrganicDecision::Deny, OrganicReasonCode::BootstrapNotActive, true);
                return Result(OrganicDecision::AllowGameplay, OrganicReasonCode::BootstrapCreation, true);

            case OrganicClassification::Deny:
                return Result(OrganicDecision::Deny, DenyReasonFor(request.kind), true);

            case OrganicClassification::RequireAudit:
                return EvaluateAuditedAction(request);

            case OrganicClassification::FixtureOnly:
                break; // handled above; unreachable
        }

        return Result(OrganicDecision::Deny, OrganicReasonCode::DeniedByClassification, true);
    }

    char const* ToString(LivingRealmMode value)
    {
        switch (value)
        {
            case LivingRealmMode::Unspecified: return "Unspecified";
            case LivingRealmMode::Disabled: return "Disabled";
            case LivingRealmMode::Organic: return "Organic";
            case LivingRealmMode::UnknownProfile: return "UnknownProfile";
        }

        return "INVALID_MODE";
    }

    char const* ToString(OrganicDecision value)
    {
        switch (value)
        {
            case OrganicDecision::AllowGameplay: return "AllowGameplay";
            case OrganicDecision::AllowAutomation: return "AllowAutomation";
            case OrganicDecision::RequireAudit: return "RequireAudit";
            case OrganicDecision::Deny: return "Deny";
        }

        return "INVALID_DECISION";
    }

    char const* ToString(OrganicSourceKind value)
    {
        switch (value)
        {
            case OrganicSourceKind::FactoryBootstrap: return "FactoryBootstrap";
            case OrganicSourceKind::RandomManager: return "RandomManager";
            case OrganicSourceKind::AiUpdate: return "AiUpdate";
            case OrganicSourceKind::PlayerChatCommand: return "PlayerChatCommand";
            case OrganicSourceKind::ConsoleCommand: return "ConsoleCommand";
            case OrganicSourceKind::ConfigResolution: return "ConfigResolution";
            case OrganicSourceKind::RecoveryService: return "RecoveryService";
            case OrganicSourceKind::TransportService: return "TransportService";
            case OrganicSourceKind::AdminSurface: return "AdminSurface";
            case OrganicSourceKind::TestFixture: return "TestFixture";
            case OrganicSourceKind::Count: break;
        }

        return "INVALID_SOURCE";
    }

    char const* ToString(OrganicReasonCode value)
    {
        switch (value)
        {
            case OrganicReasonCode::LegacyPassthrough: return "LEGACY_PASSTHROUGH";
            case OrganicReasonCode::AllowedGameplay: return "ALLOWED_GAMEPLAY";
            case OrganicReasonCode::AllowedAutomation: return "ALLOWED_AUTOMATION";
            case OrganicReasonCode::AuditRequired: return "AUDIT_REQUIRED";
            case OrganicReasonCode::BootstrapCreation: return "BOOTSTRAP_CREATION";
            case OrganicReasonCode::FixtureAuthorized: return "FIXTURE_AUTHORIZED";
            case OrganicReasonCode::DeniedByClassification: return "DENIED_BY_CLASSIFICATION";
            case OrganicReasonCode::UnknownAction: return "UNKNOWN_ACTION";
            case OrganicReasonCode::ModeUnspecified: return "MODE_UNSPECIFIED";
            case OrganicReasonCode::UnsupportedProfile: return "UNSUPPORTED_PROFILE";
            case OrganicReasonCode::InvalidSource: return "INVALID_SOURCE";
            case OrganicReasonCode::InvalidIdentity: return "INVALID_IDENTITY";
            case OrganicReasonCode::BootstrapNotActive: return "BOOTSTRAP_NOT_ACTIVE";
            case OrganicReasonCode::FixtureOutsideTestProfile: return "FIXTURE_OUTSIDE_TEST_PROFILE";
            case OrganicReasonCode::FixtureProvenanceRequired: return "FIXTURE_PROVENANCE_REQUIRED";
            case OrganicReasonCode::FixtureIsolated: return "FIXTURE_ISOLATED";
            case OrganicReasonCode::OrganicProvenanceRequired: return "ORGANIC_PROVENANCE_REQUIRED";
            case OrganicReasonCode::InvalidProvenance: return "INVALID_PROVENANCE";
            case OrganicReasonCode::LegacyLifecycleExcluded: return "LEGACY_LIFECYCLE_EXCLUDED";
            case OrganicReasonCode::ManagedOperationRequired: return "MANAGED_OPERATION_REQUIRED";
            case OrganicReasonCode::RawResetUnsupported: return "RAW_RESET_UNSUPPORTED";
            case OrganicReasonCode::NoApprovedReconciler: return "NO_APPROVED_RECONCILER";
            case OrganicReasonCode::UnsafeStateForCompatibilityAction: return "UNSAFE_STATE_FOR_COMPATIBILITY_ACTION";
            case OrganicReasonCode::RateLimitExceeded: return "RATE_LIMIT_EXCEEDED";
            case OrganicReasonCode::MissingProtectedCommitment: return "MISSING_PROTECTED_COMMITMENT";
            case OrganicReasonCode::RecoveryLadderNotExhausted: return "RECOVERY_LADDER_NOT_EXHAUSTED";
            case OrganicReasonCode::ProtectedCommitmentBlocksRecovery: return "PROTECTED_COMMITMENT_BLOCKS_RECOVERY";
            case OrganicReasonCode::NoSafeDestination: return "NO_SAFE_DESTINATION";
            case OrganicReasonCode::RouteNotAllowlisted: return "ROUTE_NOT_ALLOWLISTED";
            case OrganicReasonCode::GoalRouteNotBound: return "GOAL_ROUTE_NOT_BOUND";
            case OrganicReasonCode::NotAtOriginNode: return "NOT_AT_ORIGIN_NODE";
            case OrganicReasonCode::OriginWaitNotSatisfied: return "ORIGIN_WAIT_NOT_SATISFIED";
            case OrganicReasonCode::OwnerNotOnVerifiedTransport: return "OWNER_NOT_ON_VERIFIED_TRANSPORT";
            case OrganicReasonCode::NotNearBoardingContext: return "NOT_NEAR_BOARDING_CONTEXT";
            case OrganicReasonCode::DestinationMapUnsupported: return "DESTINATION_MAP_UNSUPPORTED";
        }

        return "INVALID_REASON";
    }
}
