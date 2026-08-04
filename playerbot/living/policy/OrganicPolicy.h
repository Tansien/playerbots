#pragma once

#include "OrganicActionMetadata.h"

#include <array>
#include <cstdint>
#include <string>

namespace living
{
    // Decision vocabulary from design 0002 section 2. Living Realm is always on
    // in this fork: there is no disabled mode, no legacy passthrough, and no
    // profile selection anywhere in this interface.
    enum class OrganicDecision : uint8_t
    {
        AllowGameplay,   // proceed through ordinary core handlers and rules
        AllowAutomation, // ordinary gameplay chosen automatically; never bypasses eligibility or cost
        RequireAudit,    // named bounded action; the caller must run the 0002B request/apply/reconcile protocol
        Deny,            // no side effect

        Count            // iteration sentinel; never a decision
    };

    // Where the guarded request originated. Informational except where a rule
    // explicitly binds to it (managed bootstrap creation).
    enum class OrganicSourceKind : uint8_t
    {
        ManagedBootstrap = 0, // managed creation wrapper
        RandomManager,        // RandomPlayerbotMgr timers and refresh paths
        AiUpdate,             // PlayerbotAI update and strategy actions
        PlayerChatCommand,
        ConsoleCommand,
        RecoveryService,      // 0002C stuck-recovery ladder
        TransportService,     // 0002C transport compatibility service
        TestFixture,          // host-side or in-world test tooling

        Count
    };

    // Stable reason codes for logs, reports, and audit rows (0002 section 2:
    // "Deny has no side effect and returns a stable reason code").
    enum class OrganicReasonCode : uint8_t
    {
        AllowedGameplay,
        AllowedAutomation,
        AuditRequired,
        BootstrapCreation,

        UnknownAction,
        InvalidSource,
        IdentityRequired,
        BootstrapSourceRequired,
        BootstrapIdentityPresent,
        DeniedByClassification,
        FixtureDeniedInProduction,
        RecoveryNotOwnerAuthorized,
        CommitmentNotCertifiedCompatible,
        ProtectedCommitmentRequired,
        AuditPolicyUndefined,

        Count // iteration sentinel; never a reason
    };

    // Action-specific, versioned canonical field set (0002 section 2). Storage
    // and equality rules belong to the 0002B audit protocol; the evaluator
    // carries the fingerprint for that layer and never interprets it.
    struct StateFingerprint
    {
        uint16_t version = 0;
        OrganicActionKind kind = OrganicActionKind::Count;
        std::string canonicalPayload;
    };

    // Immutable pure-data request context (0002 section 2). Every input the
    // evaluator may consult is explicit here; it never reads configuration,
    // databases, world objects, or singletons. Defaults fail closed.
    struct OrganicRequest
    {
        uint32_t characterGuid = 0;
        std::array<uint8_t, 16> identityNonce{};
        OrganicActionKind kind = OrganicActionKind::Count;
        OrganicSourceKind source = OrganicSourceKind::Count;
        bool protectedRealPlayerCommitment = false;

        // 0002C gate context for the audited actions; both default fail closed
        // and are only consulted when a protected commitment exists.
        // ownerAuthorizedRecovery: the committed owner explicitly authorized
        // stuck recovery and remains notified. commitmentCertifiedCompatible:
        // the commitment is explicitly certified compatible with the modeled
        // public-transport transfer.
        bool ownerAuthorizedRecovery = false;
        bool commitmentCertifiedCompatible = false;

        StateFingerprint before;
    };

    struct OrganicEvaluation
    {
        OrganicDecision decision = OrganicDecision::Deny;
        OrganicReasonCode reason = OrganicReasonCode::UnknownAction;
    };

    // Pure, deterministic, side-effect-free policy decision over the action
    // inventory. Unknown actions, unknown sources, and unbound identities fail
    // closed to Deny; only managed bootstrap creation is legal pre-identity.
    OrganicEvaluation Evaluate(OrganicRequest const& request);

    char const* ToString(OrganicDecision value);
    char const* ToString(OrganicSourceKind value);
    char const* ToString(OrganicReasonCode value);
}
