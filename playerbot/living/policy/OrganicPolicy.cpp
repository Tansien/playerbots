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
        // RequireAudit only when its required pure-data context is present; every
        // missing precondition is a Deny, never a silent downgrade to gameplay.
        OrganicPolicyResult EvaluateAuditedAction(OrganicRequest const& request)
        {
            switch (request.kind)
            {
                case OrganicActionKind::STUCK_EMERGENCY_TELEPORT:
                    if (!request.recoveryLadderExhausted)
                        return Result(OrganicDecision::Deny, OrganicReasonCode::RecoveryLadderNotExhausted, true);
                    if (request.protectedRealPlayerCommitment && !request.ownerAuthorizedRecovery)
                        return Result(OrganicDecision::Deny, OrganicReasonCode::ProtectedCommitmentBlocksRecovery, true);
                    return Result(OrganicDecision::RequireAudit, OrganicReasonCode::AuditRequired, true);

                case OrganicActionKind::TRANSPORT_GROUP_SYNC:
                    if (!request.protectedRealPlayerCommitment)
                        return Result(OrganicDecision::Deny, OrganicReasonCode::MissingProtectedCommitment, true);
                    return Result(OrganicDecision::RequireAudit, OrganicReasonCode::AuditRequired, true);

                case OrganicActionKind::PUBLIC_TRANSPORT_TRANSFER:
                    if (!request.canonicalRouteAllowlisted)
                        return Result(OrganicDecision::Deny, OrganicReasonCode::RouteNotAllowlisted, true);
                    return Result(OrganicDecision::RequireAudit, OrganicReasonCode::AuditRequired, true);

                default:
                    // Only the three named actions may be classified RequireAudit; the
                    // metadata completeness test enforces it. Fail closed regardless.
                    return Result(OrganicDecision::Deny, OrganicReasonCode::DeniedByClassification, true);
            }
        }
    }

    OrganicPolicyResult EvaluateOrganicPolicy(OrganicRequest const& request)
    {
        OrganicActionMetadata const* metadata = TryGetOrganicActionMetadata(request.kind);
        bool const knownAction = metadata != nullptr;

        // Disabled Living Realm is a passthrough: the guard must not alter legacy
        // behavior in any way (LR-001), including for actions it does not know.
        if (!request.livingRealmEnabled)
        {
            bool const gameplay = knownAction && metadata->classification == OrganicClassification::AllowGameplay;
            return Result(gameplay ? OrganicDecision::AllowGameplay : OrganicDecision::AllowAutomation,
                OrganicReasonCode::LegacyPassthrough, knownAction);
        }

        // Enabled Organic mode fails closed: unknown actions have no side effects.
        if (!knownAction)
            return Result(OrganicDecision::Deny, OrganicReasonCode::UnknownAction, false);

        // An enabled realm with an unsupported profile is a configuration error; no
        // action may proceed on guesswork.
        if (!request.organicProfile)
            return Result(OrganicDecision::Deny, OrganicReasonCode::UnsupportedProfile, true);

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

        // Fixture identities never act in production semantics (0001 invariant 13).
        if (request.provenance == BotProvenance::FIXTURE && !request.fixtureTestProfile)
            return Result(OrganicDecision::Deny, OrganicReasonCode::FixtureOutsideTestProfile, true);

        // Managed Organic semantics require ORGANIC_CREATED provenance (a FIXTURE
        // identity inside the test profile was vetted above). Everything else fails
        // closed: LEGACY_UNMANAGED is a mixed population (0002 section 3, 0003
        // section 2), and an out-of-range value is corrupted input, never an allow.
        if (!SatisfiesOrganicProvenance(request.provenance) && request.provenance != BotProvenance::FIXTURE)
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
            case OrganicReasonCode::UnsupportedProfile: return "UNSUPPORTED_PROFILE";
            case OrganicReasonCode::BootstrapNotActive: return "BOOTSTRAP_NOT_ACTIVE";
            case OrganicReasonCode::FixtureOutsideTestProfile: return "FIXTURE_OUTSIDE_TEST_PROFILE";
            case OrganicReasonCode::FixtureProvenanceRequired: return "FIXTURE_PROVENANCE_REQUIRED";
            case OrganicReasonCode::OrganicProvenanceRequired: return "ORGANIC_PROVENANCE_REQUIRED";
            case OrganicReasonCode::InvalidProvenance: return "INVALID_PROVENANCE";
            case OrganicReasonCode::LegacyLifecycleExcluded: return "LEGACY_LIFECYCLE_EXCLUDED";
            case OrganicReasonCode::ManagedOperationRequired: return "MANAGED_OPERATION_REQUIRED";
            case OrganicReasonCode::RawResetUnsupported: return "RAW_RESET_UNSUPPORTED";
            case OrganicReasonCode::NoApprovedReconciler: return "NO_APPROVED_RECONCILER";
            case OrganicReasonCode::MissingProtectedCommitment: return "MISSING_PROTECTED_COMMITMENT";
            case OrganicReasonCode::RecoveryLadderNotExhausted: return "RECOVERY_LADDER_NOT_EXHAUSTED";
            case OrganicReasonCode::ProtectedCommitmentBlocksRecovery: return "PROTECTED_COMMITMENT_BLOCKS_RECOVERY";
            case OrganicReasonCode::RouteNotAllowlisted: return "ROUTE_NOT_ALLOWLISTED";
        }

        return "INVALID_REASON";
    }
}
