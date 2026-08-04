#include "LivingTest.h"

#include "playerbot/living/policy/OrganicPolicy.h"

#include <cstring>
#include <vector>

// Decision tests for the pure Organic policy evaluator: always-on, fail-closed,
// one expectation per inventory row, and stable reason codes (0002 sections 2-4).

using living::AllOrganicActionMetadata;
using living::Evaluate;
using living::OrganicActionKind;
using living::OrganicActionMetadata;
using living::OrganicClassification;
using living::OrganicDecision;
using living::OrganicEvaluation;
using living::OrganicReasonCode;
using living::OrganicRequest;
using living::OrganicSourceKind;

namespace
{
    // A request that is valid in every generic respect: bound identity, known
    // source, no protected commitment. Tests override single fields.
    OrganicRequest BoundRequest(OrganicActionKind kind)
    {
        OrganicRequest request;
        request.characterGuid = 1234;
        request.identityNonce = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
        request.kind = kind;
        request.source = OrganicSourceKind::AiUpdate;
        return request;
    }

    bool Is(OrganicEvaluation result, OrganicDecision decision, OrganicReasonCode reason)
    {
        return result.decision == decision && result.reason == reason;
    }
}

LIVING_TEST(PolicyDefaultRequestDenies)
{
    // A default-constructed request carries the fail-closed defaults: unknown
    // action, unknown source, no identity.
    LIVING_CHECK(Is(Evaluate(OrganicRequest{}), OrganicDecision::Deny, OrganicReasonCode::UnknownAction));
}

LIVING_TEST(PolicyUnknownActionsFailClosed)
{
    OrganicRequest request = BoundRequest(OrganicActionKind::Count);
    LIVING_CHECK(Is(Evaluate(request), OrganicDecision::Deny, OrganicReasonCode::UnknownAction));

    request.kind = static_cast<OrganicActionKind>(static_cast<uint16_t>(OrganicActionKind::Count) + 1);
    LIVING_CHECK(Is(Evaluate(request), OrganicDecision::Deny, OrganicReasonCode::UnknownAction));

    request.kind = static_cast<OrganicActionKind>(0xFFFF);
    LIVING_CHECK(Is(Evaluate(request), OrganicDecision::Deny, OrganicReasonCode::UnknownAction));
}

LIVING_TEST(PolicyInvalidSourceFailsClosed)
{
    OrganicRequest request = BoundRequest(OrganicActionKind::GAMEPLAY_LOOT);
    request.source = OrganicSourceKind::Count;
    LIVING_CHECK(Is(Evaluate(request), OrganicDecision::Deny, OrganicReasonCode::InvalidSource));

    request.source = static_cast<OrganicSourceKind>(0xFF);
    LIVING_CHECK(Is(Evaluate(request), OrganicDecision::Deny, OrganicReasonCode::InvalidSource));
}

LIVING_TEST(PolicyUnboundIdentityDenies)
{
    // Both halves of (guid, nonce) are required for every non-creation action.
    OrganicRequest noGuid = BoundRequest(OrganicActionKind::GAMEPLAY_LOOT);
    noGuid.characterGuid = 0;
    LIVING_CHECK(Is(Evaluate(noGuid), OrganicDecision::Deny, OrganicReasonCode::IdentityRequired));

    OrganicRequest noNonce = BoundRequest(OrganicActionKind::GAMEPLAY_LOOT);
    noNonce.identityNonce = {};
    LIVING_CHECK(Is(Evaluate(noNonce), OrganicDecision::Deny, OrganicReasonCode::IdentityRequired));

    LIVING_CHECK(Is(Evaluate(BoundRequest(OrganicActionKind::GAMEPLAY_LOOT)),
        OrganicDecision::AllowGameplay, OrganicReasonCode::AllowedGameplay));
}

LIVING_TEST(PolicyBootstrapCreationIsPreIdentityAndSourceBound)
{
    // The one legal pre-identity decision: managed bootstrap creation with an
    // exactly absent identity.
    OrganicRequest creation;
    creation.kind = OrganicActionKind::CORE_CHARACTER_CREATE;
    creation.source = OrganicSourceKind::ManagedBootstrap;
    LIVING_CHECK(Is(Evaluate(creation), OrganicDecision::AllowGameplay, OrganicReasonCode::BootstrapCreation));

    // Any other source cannot create.
    for (OrganicSourceKind source : { OrganicSourceKind::RandomManager, OrganicSourceKind::AiUpdate,
        OrganicSourceKind::PlayerChatCommand, OrganicSourceKind::ConsoleCommand,
        OrganicSourceKind::RecoveryService, OrganicSourceKind::TransportService,
        OrganicSourceKind::TestFixture })
    {
        OrganicRequest request = creation;
        request.source = source;
        LIVING_CHECK(Is(Evaluate(request), OrganicDecision::Deny, OrganicReasonCode::BootstrapSourceRequired));
    }

    // A request that already carries any identity half is not a creation.
    OrganicRequest guidPresent = creation;
    guidPresent.characterGuid = 55;
    LIVING_CHECK(Is(Evaluate(guidPresent), OrganicDecision::Deny, OrganicReasonCode::BootstrapIdentityPresent));

    OrganicRequest noncePresent = creation;
    noncePresent.identityNonce[15] = 1;
    LIVING_CHECK(Is(Evaluate(noncePresent), OrganicDecision::Deny, OrganicReasonCode::BootstrapIdentityPresent));

    OrganicRequest bothPresent = BoundRequest(OrganicActionKind::CORE_CHARACTER_CREATE);
    bothPresent.source = OrganicSourceKind::ManagedBootstrap;
    LIVING_CHECK(Is(Evaluate(bothPresent), OrganicDecision::Deny, OrganicReasonCode::BootstrapIdentityPresent));
}

LIVING_TEST(PolicyEveryRowMapsToItsDecision)
{
    // One expectation per inventory row, from a bound-identity request with no
    // protected commitment. This is the per-row deny/allow conformance sweep.
    for (OrganicActionMetadata const& row : AllOrganicActionMetadata())
    {
        OrganicEvaluation const result = Evaluate(BoundRequest(row.kind));

        switch (row.classification)
        {
            case OrganicClassification::AllowGameplay:
                LIVING_CHECK(Is(result, OrganicDecision::AllowGameplay, OrganicReasonCode::AllowedGameplay));
                break;

            case OrganicClassification::AllowAutomation:
                LIVING_CHECK(Is(result, OrganicDecision::AllowAutomation, OrganicReasonCode::AllowedAutomation));
                break;

            case OrganicClassification::BootstrapOnly:
                // Not a creation context: the sweep's AiUpdate source fails the
                // bootstrap source gate (checked before the identity gate).
                LIVING_CHECK(Is(result, OrganicDecision::Deny, OrganicReasonCode::BootstrapSourceRequired));
                break;

            case OrganicClassification::Deny:
                LIVING_CHECK(Is(result, OrganicDecision::Deny, OrganicReasonCode::DeniedByClassification));
                break;

            case OrganicClassification::RequireAudit:
                if (row.kind == OrganicActionKind::TRANSPORT_GROUP_SYNC)
                    LIVING_CHECK(Is(result, OrganicDecision::Deny, OrganicReasonCode::ProtectedCommitmentRequired));
                else
                    LIVING_CHECK(Is(result, OrganicDecision::RequireAudit, OrganicReasonCode::AuditRequired));
                break;

            case OrganicClassification::FixtureOnly:
                LIVING_CHECK(Is(result, OrganicDecision::Deny, OrganicReasonCode::FixtureDeniedInProduction));
                break;
        }
    }
}

LIVING_TEST(PolicyFabricationFamiliesAreDenied)
{
    // Named spot checks over the fabrication families (readable core of the
    // 0002 section 3 mandatory outcomes; the full sweep is the row loop).
    for (OrganicActionKind kind : {
        OrganicActionKind::RANDOMIZE_INSTANT, OrganicActionKind::RANDOMIZE_FULL,
        OrganicActionKind::RANDOMIZE_INCREMENTAL, OrganicActionKind::RANDOMIZE_HOTFIX,
        OrganicActionKind::LEVEL_ASSIGN, OrganicActionKind::XP_ASSIGN,
        OrganicActionKind::XP_MULTIPLIER, OrganicActionKind::LEVEL_SYNC,
        OrganicActionKind::GEAR_INIT, OrganicActionKind::MONEY_INIT,
        OrganicActionKind::TEMP_MONEY_TRICK, OrganicActionKind::BAGS_INVENTORY_INIT,
        OrganicActionKind::AMMO_REPLENISH, OrganicActionKind::SKILLS_INIT,
        OrganicActionKind::SPELLS_INIT, OrganicActionKind::FREE_TRAINER_MODE,
        OrganicActionKind::PREQUEST_INIT, OrganicActionKind::QUEST_COMPLETE_GENERIC,
        OrganicActionKind::QUEST_SYNC_TO_BOT, OrganicActionKind::QUEST_SYNC_TO_PLAYER,
        OrganicActionKind::WORLD_BUFF_APPLY, OrganicActionKind::REPUTATION_INIT,
        OrganicActionKind::TAXI_NODES_INIT, OrganicActionKind::MOUNT_INIT,
        OrganicActionKind::PET_INIT, OrganicActionKind::GUILD_BOOTSTRAP,
        OrganicActionKind::RANDOM_TELEPORT, OrganicActionKind::TELEPORT_NEAR_PLAYER,
        OrganicActionKind::FREE_SUMMON, OrganicActionKind::RANDOM_MANAGER_REVIVE,
        OrganicActionKind::LEGACY_TRANSPORT_SHORTCUT, OrganicActionKind::LEGACY_LOGIN_ROTATION,
        OrganicActionKind::LEGACY_TIMED_ROTATION, OrganicActionKind::RAW_POPULATION_SQL_RESET,
        OrganicActionKind::OFFLINE_PROGRESSION, OrganicActionKind::ADMIN_BYPASS_MUTATION })
    {
        LIVING_CHECK(Evaluate(BoundRequest(kind)).decision == OrganicDecision::Deny);
    }
}

LIVING_TEST(PolicyAllCheatBitsAreDenied)
{
    // The 13 configured BotCheatMask categories deny by classification; the
    // runtime override is fixture-only and also denies in production.
    OrganicActionKind const first = OrganicActionKind::CHEAT_TAXI;
    OrganicActionKind const last = OrganicActionKind::CHEAT_QUEST;
    int count = 0;

    for (uint16_t value = static_cast<uint16_t>(first); value <= static_cast<uint16_t>(last); ++value)
    {
        ++count;
        LIVING_CHECK(Is(Evaluate(BoundRequest(static_cast<OrganicActionKind>(value))),
            OrganicDecision::Deny, OrganicReasonCode::DeniedByClassification));
    }

    LIVING_CHECK(count == 13);
    LIVING_CHECK(Is(Evaluate(BoundRequest(OrganicActionKind::CHEAT_RUNTIME_OVERRIDE)),
        OrganicDecision::Deny, OrganicReasonCode::FixtureDeniedInProduction));
}

LIVING_TEST(PolicyCommitmentGatesTheAuditedActions)
{
    // 0002C: stuck recovery under a protected real-player commitment requires
    // the owner's explicit recovery authorization ("no protected real-player
    // commitment unless the owner explicitly authorizes recovery").
    {
        OrganicRequest request = BoundRequest(OrganicActionKind::STUCK_EMERGENCY_TELEPORT);
        LIVING_CHECK(Is(Evaluate(request), OrganicDecision::RequireAudit, OrganicReasonCode::AuditRequired));

        request.protectedRealPlayerCommitment = true;
        LIVING_CHECK(Is(Evaluate(request), OrganicDecision::Deny, OrganicReasonCode::RecoveryNotOwnerAuthorized));

        request.ownerAuthorizedRecovery = true;
        LIVING_CHECK(Is(Evaluate(request), OrganicDecision::RequireAudit, OrganicReasonCode::AuditRequired));

        // The transfer-certification flag is not recovery authorization.
        request.ownerAuthorizedRecovery = false;
        request.commitmentCertifiedCompatible = true;
        LIVING_CHECK(Is(Evaluate(request), OrganicDecision::Deny, OrganicReasonCode::RecoveryNotOwnerAuthorized));
    }

    // 0002C: a modeled public-transport transfer must not run under an
    // incompatible protected commitment; compatibility is a positive,
    // default-false certification.
    {
        OrganicRequest request = BoundRequest(OrganicActionKind::PUBLIC_TRANSPORT_TRANSFER);
        LIVING_CHECK(Is(Evaluate(request), OrganicDecision::RequireAudit, OrganicReasonCode::AuditRequired));

        request.protectedRealPlayerCommitment = true;
        LIVING_CHECK(Is(Evaluate(request), OrganicDecision::Deny, OrganicReasonCode::CommitmentNotCertifiedCompatible));

        request.commitmentCertifiedCompatible = true;
        LIVING_CHECK(Is(Evaluate(request), OrganicDecision::RequireAudit, OrganicReasonCode::AuditRequired));

        // Recovery authorization is not transfer certification.
        request.commitmentCertifiedCompatible = false;
        request.ownerAuthorizedRecovery = true;
        LIVING_CHECK(Is(Evaluate(request), OrganicDecision::Deny, OrganicReasonCode::CommitmentNotCertifiedCompatible));
    }

    // 0002C: group transport sync exists only FOR a protected commitment.
    {
        OrganicRequest request = BoundRequest(OrganicActionKind::TRANSPORT_GROUP_SYNC);
        LIVING_CHECK(Is(Evaluate(request), OrganicDecision::Deny, OrganicReasonCode::ProtectedCommitmentRequired));

        // Neither gate flag substitutes for the commitment itself.
        request.ownerAuthorizedRecovery = true;
        request.commitmentCertifiedCompatible = true;
        LIVING_CHECK(Is(Evaluate(request), OrganicDecision::Deny, OrganicReasonCode::ProtectedCommitmentRequired));

        request.protectedRealPlayerCommitment = true;
        LIVING_CHECK(Is(Evaluate(request), OrganicDecision::RequireAudit, OrganicReasonCode::AuditRequired));
    }
}

LIVING_TEST(PolicyFixtureKindsAreDeniedInProduction)
{
    // Invariant 12: no fixture behavior in the production population, from any
    // source, including test tooling itself.
    for (OrganicActionKind kind : { OrganicActionKind::FIXTURE_CHARACTER_CREATE,
        OrganicActionKind::FIXTURE_PROVISION, OrganicActionKind::BROAD_MAINTENANCE_COMMAND,
        OrganicActionKind::DEBUG_MUTATION_COMMAND })
    {
        for (uint8_t source = 0; source < static_cast<uint8_t>(OrganicSourceKind::Count); ++source)
        {
            OrganicRequest request = BoundRequest(kind);
            request.source = static_cast<OrganicSourceKind>(source);
            LIVING_CHECK(Is(Evaluate(request), OrganicDecision::Deny, OrganicReasonCode::FixtureDeniedInProduction));
        }
    }
}

LIVING_TEST(PolicyEvaluationIsAPureFunctionOfTheRequest)
{
    // Sweep the whole decidable domain (every kind including out-of-range,
    // every source including invalid, both identity states, and all commitment
    // and gate contexts) twice -- the second pass in REVERSED order -- and
    // require identical per-request results. Order-dependent mutable state
    // inside Evaluate whose effect varies with call position (a counter, a
    // rolling rate-limit window, a bounded or evicting cache) makes some
    // request's answer diverge across the ~30k-call passes. Out of this
    // test's reach and excluded by contract instead: an UNBOUNDED memo --
    // correctly or wrongly keyed -- is fully warm after the forward pass and
    // serves the reversed pass identically (shared mutable state of any kind
    // is forbidden by the invariant-8 threading contract; a wrong-KEY defect
    // is still caught by PolicyCommitmentGatesTheAuditedActions, which pins
    // conflicting results for one kind under different flags), and externals
    // that stay constant within one run (wall clock, configuration) are
    // unobservable from inside the run -- the evaluator's explicit argument
    // list is the enforcement there.
    std::vector<OrganicRequest> requests;

    for (uint16_t kind = 0; kind <= static_cast<uint16_t>(OrganicActionKind::Count); ++kind)
        for (uint8_t source = 0; source <= static_cast<uint8_t>(OrganicSourceKind::Count); ++source)
            for (int bound = 0; bound < 2; ++bound)
                for (int commitment = 0; commitment < 2; ++commitment)
                    for (int authorized = 0; authorized < 2; ++authorized)
                        for (int certified = 0; certified < 2; ++certified)
                        {
                            OrganicRequest request;
                            if (bound == 1)
                                request = BoundRequest(static_cast<OrganicActionKind>(kind));
                            else
                                request.kind = static_cast<OrganicActionKind>(kind);
                            request.source = static_cast<OrganicSourceKind>(source);
                            request.protectedRealPlayerCommitment = commitment == 1;
                            request.ownerAuthorizedRecovery = authorized == 1;
                            request.commitmentCertifiedCompatible = certified == 1;

                            requests.push_back(request);
                        }

    std::vector<OrganicEvaluation> forward(requests.size());
    for (size_t i = 0; i < requests.size(); ++i)
        forward[i] = Evaluate(requests[i]);

    std::vector<OrganicEvaluation> reversed(requests.size());
    for (size_t i = requests.size(); i > 0; --i)
        reversed[i - 1] = Evaluate(requests[i - 1]);

    for (size_t i = 0; i < requests.size(); ++i)
    {
        LIVING_CHECK(forward[i].decision == reversed[i].decision);
        LIVING_CHECK(forward[i].reason == reversed[i].reason);
    }
}

LIVING_TEST(PolicyStringConversionsAreStable)
{
    // These literals are the stable audit-row and log identifiers (0002
    // section 2): every enumerator is pinned exactly, so an edited or
    // misrouted case in a ToString switch cannot slip through as a warning.
    struct DecisionName { OrganicDecision value; char const* name; };
    static DecisionName const DECISIONS[] = {
        { OrganicDecision::AllowGameplay, "AllowGameplay" },
        { OrganicDecision::AllowAutomation, "AllowAutomation" },
        { OrganicDecision::RequireAudit, "RequireAudit" },
        { OrganicDecision::Deny, "Deny" },
    };
    static_assert(sizeof(DECISIONS) / sizeof(DECISIONS[0])
        == static_cast<size_t>(OrganicDecision::Count), "pin every decision");
    for (DecisionName const& expected : DECISIONS)
        LIVING_CHECK(std::strcmp(ToString(expected.value), expected.name) == 0);

    struct SourceName { OrganicSourceKind value; char const* name; };
    static SourceName const SOURCES[] = {
        { OrganicSourceKind::ManagedBootstrap, "ManagedBootstrap" },
        { OrganicSourceKind::RandomManager, "RandomManager" },
        { OrganicSourceKind::AiUpdate, "AiUpdate" },
        { OrganicSourceKind::PlayerChatCommand, "PlayerChatCommand" },
        { OrganicSourceKind::ConsoleCommand, "ConsoleCommand" },
        { OrganicSourceKind::RecoveryService, "RecoveryService" },
        { OrganicSourceKind::TransportService, "TransportService" },
        { OrganicSourceKind::TestFixture, "TestFixture" },
    };
    static_assert(sizeof(SOURCES) / sizeof(SOURCES[0])
        == static_cast<size_t>(OrganicSourceKind::Count), "pin every source");
    for (SourceName const& expected : SOURCES)
        LIVING_CHECK(std::strcmp(ToString(expected.value), expected.name) == 0);

    struct ReasonName { OrganicReasonCode value; char const* name; };
    static ReasonName const REASONS[] = {
        { OrganicReasonCode::AllowedGameplay, "AllowedGameplay" },
        { OrganicReasonCode::AllowedAutomation, "AllowedAutomation" },
        { OrganicReasonCode::AuditRequired, "AuditRequired" },
        { OrganicReasonCode::BootstrapCreation, "BootstrapCreation" },
        { OrganicReasonCode::UnknownAction, "UnknownAction" },
        { OrganicReasonCode::InvalidSource, "InvalidSource" },
        { OrganicReasonCode::IdentityRequired, "IdentityRequired" },
        { OrganicReasonCode::BootstrapSourceRequired, "BootstrapSourceRequired" },
        { OrganicReasonCode::BootstrapIdentityPresent, "BootstrapIdentityPresent" },
        { OrganicReasonCode::DeniedByClassification, "DeniedByClassification" },
        { OrganicReasonCode::FixtureDeniedInProduction, "FixtureDeniedInProduction" },
        { OrganicReasonCode::RecoveryNotOwnerAuthorized, "RecoveryNotOwnerAuthorized" },
        { OrganicReasonCode::CommitmentNotCertifiedCompatible, "CommitmentNotCertifiedCompatible" },
        { OrganicReasonCode::ProtectedCommitmentRequired, "ProtectedCommitmentRequired" },
        { OrganicReasonCode::AuditPolicyUndefined, "AuditPolicyUndefined" },
    };
    static_assert(sizeof(REASONS) / sizeof(REASONS[0])
        == static_cast<size_t>(OrganicReasonCode::Count), "pin every reason");
    for (ReasonName const& expected : REASONS)
        LIVING_CHECK(std::strcmp(ToString(expected.value), expected.name) == 0);

    LIVING_CHECK(std::strcmp(ToString(OrganicDecision::Count), "INVALID_DECISION") == 0);
    LIVING_CHECK(std::strcmp(ToString(OrganicSourceKind::Count), "INVALID_SOURCE") == 0);
    LIVING_CHECK(std::strcmp(ToString(OrganicReasonCode::Count), "INVALID_REASON") == 0);
    LIVING_CHECK(std::strcmp(ToString(static_cast<OrganicDecision>(0xFF)), "INVALID_DECISION") == 0);
    LIVING_CHECK(std::strcmp(ToString(static_cast<OrganicSourceKind>(0xFF)), "INVALID_SOURCE") == 0);
    LIVING_CHECK(std::strcmp(ToString(static_cast<OrganicReasonCode>(0xFF)), "INVALID_REASON") == 0);
}
