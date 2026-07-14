#pragma once

#include "BotProvenance.h"
#include "OrganicActionKind.h"

#include <array>
#include <cstdint>

namespace living
{
    // Decision vocabulary from design 0002 section 2.
    enum class OrganicDecision : uint8_t
    {
        AllowGameplay,   // proceed through ordinary core handlers and rules
        AllowAutomation, // ordinary gameplay chosen automatically; never bypasses eligibility or cost
        RequireAudit,    // named bounded action; caller must run the 0002B request/apply/reconcile protocol
        Deny             // no side effect
    };

    // Where the guarded request originated. Informational for reports and telemetry;
    // decisions depend on the explicit boolean context, not on trusting the caller.
    enum class OrganicSourceKind : uint8_t
    {
        FactoryBootstrap, // managed creation/factory wrapper
        RandomManager,    // RandomPlayerbotMgr timers and refresh paths
        AiUpdate,         // PlayerbotAI update and strategy actions
        PlayerChatCommand,
        ConsoleCommand,
        ConfigResolution, // effective-configuration computation at startup
        RecoveryService,  // 0002C stuck-recovery ladder
        TransportService, // 0002C transport compatibility service
        AdminSurface,     // future authenticated administration surface
        TestFixture       // host-side or in-world test tooling
    };

    // Stable reason codes for logs, reports, and audit rows.
    enum class OrganicReasonCode : uint8_t
    {
        LegacyPassthrough,
        AllowedGameplay,
        AllowedAutomation,
        AuditRequired,
        BootstrapCreation,
        FixtureAuthorized,
        DeniedByClassification,
        UnknownAction,
        UnsupportedProfile,
        BootstrapNotActive,
        FixtureOutsideTestProfile,
        FixtureProvenanceRequired,
        OrganicProvenanceRequired,
        LegacyLifecycleExcluded,
        ManagedOperationRequired,
        RawResetUnsupported,
        NoApprovedReconciler,
        MissingProtectedCommitment,
        RecoveryLadderNotExhausted,
        ProtectedCommitmentBlocksRecovery,
        RouteNotAllowlisted
    };

    // Immutable pure-data request context (design 0002 section 2). Every input the
    // evaluator may consult is explicit here; it never reads configuration,
    // databases, world objects, or singletons. The 0002B StateFingerprint joins this
    // structure with the audit protocol implementation, not in Phase 0.
    struct OrganicRequest
    {
        // Defaults fail closed: unknown action, unmanaged provenance, disabled realm.
        OrganicActionKind kind = OrganicActionKind::Count;
        OrganicSourceKind source = OrganicSourceKind::AiUpdate;

        uint32_t characterGuid = 0;
        std::array<uint8_t, 16> identityNonce{};
        BotProvenance provenance = BotProvenance::LEGACY_UNMANAGED;

        // effective configuration context
        bool livingRealmEnabled = false;
        bool organicProfile = false;
        bool fixtureTestProfile = false;

        // situational context
        bool protectedRealPlayerCommitment = false;
        bool managedBootstrapActive = false;

        // action-specific pure-data context for the three audited 0.1 actions (0002C)
        bool recoveryLadderExhausted = false;   // STUCK_EMERGENCY_TELEPORT: C.6 ladder completed
        bool ownerAuthorizedRecovery = false;   // STUCK_EMERGENCY_TELEPORT: owner consent under commitment
        bool canonicalRouteAllowlisted = false; // PUBLIC_TRANSPORT_TRANSFER: registry route on typed goal path
    };

    struct OrganicPolicyResult
    {
        OrganicDecision decision = OrganicDecision::Deny;
        OrganicReasonCode reason = OrganicReasonCode::UnknownAction;
        bool knownAction = false;
    };

    // Pure, deterministic, and side-effect free. Disabled Living Realm preserves
    // legacy behavior; enabled Organic mode fails closed for unknown or unclassified
    // actions and never falls back to legacy fabrication (0001 invariant 5).
    OrganicPolicyResult EvaluateOrganicPolicy(OrganicRequest const& request);

    char const* ToString(OrganicDecision value);
    char const* ToString(OrganicSourceKind value);
    char const* ToString(OrganicReasonCode value);
}
