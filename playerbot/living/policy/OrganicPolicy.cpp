#include "OrganicPolicy.h"

namespace living
{
    namespace
    {
        bool HasBoundIdentity(OrganicRequest const& request)
        {
            return request.characterGuid != 0
                && request.identityNonce != std::array<uint8_t, 16>{};
        }

        // The three named 0.1 audited actions carry action-specific commitment
        // rules (0002C): stuck recovery under a protected real-player
        // commitment requires the owner's explicit recovery authorization; a
        // modeled public-transport transfer under one requires the commitment
        // to be explicitly certified compatible; group transport sync exists
        // only FOR a protected commitment. The gates admit a request into the
        // 0002B audit protocol, which re-validates every action-specific
        // precondition before any mutation.
        OrganicEvaluation EvaluateAudited(OrganicRequest const& request)
        {
            switch (request.kind)
            {
                case OrganicActionKind::STUCK_EMERGENCY_TELEPORT:
                    if (request.protectedRealPlayerCommitment && !request.ownerAuthorizedRecovery)
                        return { OrganicDecision::Deny, OrganicReasonCode::RecoveryNotOwnerAuthorized };
                    return { OrganicDecision::RequireAudit, OrganicReasonCode::AuditRequired };

                case OrganicActionKind::PUBLIC_TRANSPORT_TRANSFER:
                    if (request.protectedRealPlayerCommitment && !request.commitmentCertifiedCompatible)
                        return { OrganicDecision::Deny, OrganicReasonCode::CommitmentNotCertifiedCompatible };
                    return { OrganicDecision::RequireAudit, OrganicReasonCode::AuditRequired };

                case OrganicActionKind::TRANSPORT_GROUP_SYNC:
                    if (!request.protectedRealPlayerCommitment)
                        return { OrganicDecision::Deny, OrganicReasonCode::ProtectedCommitmentRequired };
                    return { OrganicDecision::RequireAudit, OrganicReasonCode::AuditRequired };

                default:
                    // A RequireAudit row without an explicit commitment rule here
                    // has no defined audit policy yet: fail closed.
                    return { OrganicDecision::Deny, OrganicReasonCode::AuditPolicyUndefined };
            }
        }
    }

    OrganicEvaluation Evaluate(OrganicRequest const& request)
    {
        // Unknown actions fail closed before anything else (0002 section 2).
        OrganicActionMetadata const* row = TryGetOrganicActionMetadata(request.kind);
        if (row == nullptr)
            return { OrganicDecision::Deny, OrganicReasonCode::UnknownAction };

        if (static_cast<uint8_t>(request.source) >= static_cast<uint8_t>(OrganicSourceKind::Count))
            return { OrganicDecision::Deny, OrganicReasonCode::InvalidSource };

        if (row->classification == OrganicClassification::BootstrapOnly)
        {
            // Managed bootstrap creation is the one pre-identity decision: the
            // core generates the low GUID inside creation, so an already-bound
            // identity means this is not a creation.
            if (request.source != OrganicSourceKind::ManagedBootstrap)
                return { OrganicDecision::Deny, OrganicReasonCode::BootstrapSourceRequired };
            if (request.characterGuid != 0 || request.identityNonce != std::array<uint8_t, 16>{})
                return { OrganicDecision::Deny, OrganicReasonCode::BootstrapIdentityPresent };

            return { OrganicDecision::AllowGameplay, OrganicReasonCode::BootstrapCreation };
        }

        // Every other decision must be attachable to one managed identity
        // (guid + nonce), or audit rows and telemetry could not name their
        // subject (0001 section 4).
        if (!HasBoundIdentity(request))
            return { OrganicDecision::Deny, OrganicReasonCode::IdentityRequired };

        switch (row->classification)
        {
            case OrganicClassification::AllowGameplay:
                return { OrganicDecision::AllowGameplay, OrganicReasonCode::AllowedGameplay };

            case OrganicClassification::AllowAutomation:
                return { OrganicDecision::AllowAutomation, OrganicReasonCode::AllowedAutomation };

            case OrganicClassification::RequireAudit:
                return EvaluateAudited(request);

            case OrganicClassification::FixtureOnly:
                // Invariant 12: fixture behavior never reaches the production
                // population. A fixture profile, when it exists, gets its own
                // explicit surface; the production evaluator always denies.
                return { OrganicDecision::Deny, OrganicReasonCode::FixtureDeniedInProduction };

            case OrganicClassification::BootstrapOnly:
            case OrganicClassification::Deny:
                break;
        }

        return { OrganicDecision::Deny, OrganicReasonCode::DeniedByClassification };
    }

    char const* ToString(OrganicDecision value)
    {
        switch (value)
        {
            case OrganicDecision::AllowGameplay: return "AllowGameplay";
            case OrganicDecision::AllowAutomation: return "AllowAutomation";
            case OrganicDecision::RequireAudit: return "RequireAudit";
            case OrganicDecision::Deny: return "Deny";
            case OrganicDecision::Count: break;
        }

        return "INVALID_DECISION";
    }

    char const* ToString(OrganicSourceKind value)
    {
        switch (value)
        {
            case OrganicSourceKind::ManagedBootstrap: return "ManagedBootstrap";
            case OrganicSourceKind::RandomManager: return "RandomManager";
            case OrganicSourceKind::AiUpdate: return "AiUpdate";
            case OrganicSourceKind::PlayerChatCommand: return "PlayerChatCommand";
            case OrganicSourceKind::ConsoleCommand: return "ConsoleCommand";
            case OrganicSourceKind::RecoveryService: return "RecoveryService";
            case OrganicSourceKind::TransportService: return "TransportService";
            case OrganicSourceKind::TestFixture: return "TestFixture";
            case OrganicSourceKind::Count: break;
        }

        return "INVALID_SOURCE";
    }

    char const* ToString(OrganicReasonCode value)
    {
        switch (value)
        {
            case OrganicReasonCode::AllowedGameplay: return "AllowedGameplay";
            case OrganicReasonCode::AllowedAutomation: return "AllowedAutomation";
            case OrganicReasonCode::AuditRequired: return "AuditRequired";
            case OrganicReasonCode::BootstrapCreation: return "BootstrapCreation";
            case OrganicReasonCode::UnknownAction: return "UnknownAction";
            case OrganicReasonCode::InvalidSource: return "InvalidSource";
            case OrganicReasonCode::IdentityRequired: return "IdentityRequired";
            case OrganicReasonCode::BootstrapSourceRequired: return "BootstrapSourceRequired";
            case OrganicReasonCode::BootstrapIdentityPresent: return "BootstrapIdentityPresent";
            case OrganicReasonCode::DeniedByClassification: return "DeniedByClassification";
            case OrganicReasonCode::FixtureDeniedInProduction: return "FixtureDeniedInProduction";
            case OrganicReasonCode::RecoveryNotOwnerAuthorized: return "RecoveryNotOwnerAuthorized";
            case OrganicReasonCode::CommitmentNotCertifiedCompatible: return "CommitmentNotCertifiedCompatible";
            case OrganicReasonCode::ProtectedCommitmentRequired: return "ProtectedCommitmentRequired";
            case OrganicReasonCode::AuditPolicyUndefined: return "AuditPolicyUndefined";
            case OrganicReasonCode::Count: break;
        }

        return "INVALID_REASON";
    }
}
