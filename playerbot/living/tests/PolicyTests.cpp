#include "LivingTest.h"

#include "../policy/OrganicActionMetadata.h"
#include "../policy/OrganicPolicy.h"

#include <cstring>
#include <string>

using namespace living;

namespace
{
    // Production Organic-realm request for an ORGANIC_CREATED managed bot.
    OrganicRequest OrganicRequestFor(OrganicActionKind kind)
    {
        OrganicRequest request;
        request.kind = kind;
        request.characterGuid = 1000;
        request.identityNonce = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
        request.provenance = BotProvenance::ORGANIC_CREATED;
        request.livingRealmEnabled = true;
        request.organicProfile = true;
        return request;
    }

    OrganicRequest DisabledRequestFor(OrganicActionKind kind)
    {
        OrganicRequest request = OrganicRequestFor(kind);
        request.livingRealmEnabled = false;
        request.provenance = BotProvenance::LEGACY_UNMANAGED;
        return request;
    }

    bool SameResult(OrganicPolicyResult const& a, OrganicPolicyResult const& b)
    {
        return a.decision == b.decision && a.reason == b.reason && a.knownAction == b.knownAction;
    }
}

LIVING_TEST(policy_disabled_realm_preserves_legacy_behavior)
{
    // LR-001: with Living Realm disabled the guard never denies anything, known or
    // unknown, so legacy paths behave exactly as before.
    for (size_t i = 0; i <= static_cast<size_t>(OrganicActionKind::Count); ++i)
    {
        OrganicRequest request = DisabledRequestFor(static_cast<OrganicActionKind>(i));
        OrganicPolicyResult const result = EvaluateOrganicPolicy(request);
        LIVING_CHECK(result.decision != OrganicDecision::Deny);
        LIVING_CHECK(result.decision != OrganicDecision::RequireAudit);
        LIVING_CHECK(result.reason == OrganicReasonCode::LegacyPassthrough);
        LIVING_CHECK(result.knownAction == (i < static_cast<size_t>(OrganicActionKind::Count)));
    }
}

LIVING_TEST(policy_unknown_actions_fail_closed_when_enabled)
{
    OrganicRequest unknown = OrganicRequestFor(OrganicActionKind::Count);
    OrganicPolicyResult const result = EvaluateOrganicPolicy(unknown);
    LIVING_CHECK(result.decision == OrganicDecision::Deny);
    LIVING_CHECK(result.reason == OrganicReasonCode::UnknownAction);
    LIVING_CHECK(!result.knownAction);

    // Even wildly out-of-range values fail closed.
    OrganicRequest garbage = OrganicRequestFor(static_cast<OrganicActionKind>(0xFFFF));
    LIVING_CHECK(EvaluateOrganicPolicy(garbage).decision == OrganicDecision::Deny);
}

LIVING_TEST(policy_unsupported_profile_fails_closed)
{
    OrganicRequest request = OrganicRequestFor(OrganicActionKind::GAMEPLAY_LOOT);
    request.organicProfile = false;
    OrganicPolicyResult const result = EvaluateOrganicPolicy(request);
    LIVING_CHECK(result.decision == OrganicDecision::Deny);
    LIVING_CHECK(result.reason == OrganicReasonCode::UnsupportedProfile);
}

LIVING_TEST(policy_legal_automation_is_distinguishable_from_gameplay)
{
    LIVING_CHECK(EvaluateOrganicPolicy(OrganicRequestFor(OrganicActionKind::TRAINER_PURCHASE)).decision
        == OrganicDecision::AllowAutomation);
    LIVING_CHECK(EvaluateOrganicPolicy(OrganicRequestFor(OrganicActionKind::TALENT_SPEND_EARNED)).decision
        == OrganicDecision::AllowAutomation);
    LIVING_CHECK(EvaluateOrganicPolicy(OrganicRequestFor(OrganicActionKind::VENDOR_REPAIR_TRANSACTION)).decision
        == OrganicDecision::AllowAutomation);
    LIVING_CHECK(EvaluateOrganicPolicy(OrganicRequestFor(OrganicActionKind::AUCTION_TRANSACTION)).decision
        == OrganicDecision::AllowAutomation);
    LIVING_CHECK(EvaluateOrganicPolicy(OrganicRequestFor(OrganicActionKind::MAIL_TRANSACTION)).decision
        == OrganicDecision::AllowAutomation);
    LIVING_CHECK(EvaluateOrganicPolicy(OrganicRequestFor(OrganicActionKind::TRADE_TRANSACTION)).decision
        == OrganicDecision::AllowAutomation);

    LIVING_CHECK(EvaluateOrganicPolicy(OrganicRequestFor(OrganicActionKind::GAMEPLAY_LOOT)).decision
        == OrganicDecision::AllowGameplay);
    LIVING_CHECK(EvaluateOrganicPolicy(OrganicRequestFor(OrganicActionKind::GAMEPLAY_QUEST_REWARD)).decision
        == OrganicDecision::AllowGameplay);
    LIVING_CHECK(EvaluateOrganicPolicy(OrganicRequestFor(OrganicActionKind::GAMEPLAY_EQUIP)).decision
        == OrganicDecision::AllowGameplay);
    LIVING_CHECK(EvaluateOrganicPolicy(OrganicRequestFor(OrganicActionKind::GAMEPLAY_TAXI_FLIGHT)).decision
        == OrganicDecision::AllowGameplay);
    LIVING_CHECK(EvaluateOrganicPolicy(OrganicRequestFor(OrganicActionKind::GAMEPLAY_HEARTHSTONE)).decision
        == OrganicDecision::AllowGameplay);
    LIVING_CHECK(EvaluateOrganicPolicy(OrganicRequestFor(OrganicActionKind::GAMEPLAY_ELIGIBLE_PORTAL)).decision
        == OrganicDecision::AllowGameplay);
    LIVING_CHECK(EvaluateOrganicPolicy(OrganicRequestFor(OrganicActionKind::GAMEPLAY_ELIGIBLE_SUMMON)).decision
        == OrganicDecision::AllowGameplay);
    LIVING_CHECK(EvaluateOrganicPolicy(OrganicRequestFor(OrganicActionKind::GAMEPLAY_DEATH_RECOVERY)).decision
        == OrganicDecision::AllowGameplay);
}

LIVING_TEST(policy_fabrication_families_are_denied)
{
    OrganicActionKind const denied[] = {
        OrganicActionKind::RANDOMIZE_INSTANT, OrganicActionKind::RANDOMIZE_FULL,
        OrganicActionKind::RANDOMIZE_INCREMENTAL, OrganicActionKind::RANDOMIZE_HOTFIX,
        OrganicActionKind::LEVEL_ASSIGN, OrganicActionKind::XP_ASSIGN,
        OrganicActionKind::XP_MULTIPLIER, OrganicActionKind::LEVEL_SYNC,
        OrganicActionKind::GEAR_INIT, OrganicActionKind::GEAR_UPGRADE,
        OrganicActionKind::SYNTHETIC_ENCHANT_INIT, OrganicActionKind::AUTO_ENCHANT_ON_UPGRADE,
        OrganicActionKind::MONEY_INIT, OrganicActionKind::MONEY_PERIODIC,
        OrganicActionKind::TEMP_MONEY_TRICK, OrganicActionKind::QUEST_MONEY_FABRICATION,
        OrganicActionKind::BAGS_INVENTORY_INIT, OrganicActionKind::CONSUMABLES_INIT,
        OrganicActionKind::AMMO_REPLENISH,
        OrganicActionKind::SKILLS_INIT, OrganicActionKind::SPELLS_INIT,
        OrganicActionKind::FREE_TRAINER_MODE, OrganicActionKind::AUTO_LEARN_TRAINER_SPELLS,
        OrganicActionKind::AUTO_LEARN_QUEST_SPELLS, OrganicActionKind::AUTO_LEARN_DROPPED_SPELLS,
        OrganicActionKind::AUTO_LEARN_ITEM_FABRICATION, OrganicActionKind::TALENT_INIT_SYNTHETIC,
        OrganicActionKind::PREQUEST_INIT, OrganicActionKind::CONFIGURED_QUEST_REWARD,
        OrganicActionKind::QUEST_COMPLETE_GENERIC, OrganicActionKind::QUEST_SYNC_TO_BOT,
        OrganicActionKind::QUEST_SYNC_TO_PLAYER,
        OrganicActionKind::WORLD_BUFF_APPLY, OrganicActionKind::REPUTATION_INIT,
        OrganicActionKind::TAXI_NODES_INIT, OrganicActionKind::MOUNT_INIT, OrganicActionKind::PET_INIT,
        OrganicActionKind::GUILD_BOOTSTRAP, OrganicActionKind::ARENA_TEAM_BOOTSTRAP,
        OrganicActionKind::RANDOM_TELEPORT, OrganicActionKind::TELEPORT_NEAR_PLAYER,
        OrganicActionKind::RPG_CAMP_TELEPORT, OrganicActionKind::LEGACY_TRANSPORT_SHORTCUT,
        OrganicActionKind::FREE_SUMMON, OrganicActionKind::RANDOM_MANAGER_REVIVE,
        OrganicActionKind::OFFLINE_PROGRESSION
    };

    for (OrganicActionKind kind : denied)
    {
        OrganicPolicyResult const result = EvaluateOrganicPolicy(OrganicRequestFor(kind));
        LIVING_CHECK(result.decision == OrganicDecision::Deny);
        LIVING_CHECK(result.knownAction);
    }
}

LIVING_TEST(policy_all_13_cheat_bits_are_denied)
{
    for (OrganicActionMetadata const& row : AllOrganicActionMetadata())
    {
        if (row.category != OrganicActionCategory::Cheat)
            continue;

        OrganicPolicyResult const result = EvaluateOrganicPolicy(OrganicRequestFor(row.kind));
        LIVING_CHECK(result.decision == OrganicDecision::Deny);
    }
}

LIVING_TEST(policy_broad_maintenance_and_lifecycle_shortcuts_are_denied)
{
    LIVING_CHECK(EvaluateOrganicPolicy(OrganicRequestFor(OrganicActionKind::BROAD_MAINTENANCE_COMMAND)).decision
        == OrganicDecision::Deny);
    LIVING_CHECK(EvaluateOrganicPolicy(OrganicRequestFor(OrganicActionKind::RANDOMIZE_HOTFIX)).decision
        == OrganicDecision::Deny);

    OrganicPolicyResult const legacyLogin =
        EvaluateOrganicPolicy(OrganicRequestFor(OrganicActionKind::LEGACY_LOGIN_ROTATION));
    LIVING_CHECK(legacyLogin.decision == OrganicDecision::Deny);
    LIVING_CHECK(legacyLogin.reason == OrganicReasonCode::LegacyLifecycleExcluded);

    OrganicPolicyResult const legacyTimed =
        EvaluateOrganicPolicy(OrganicRequestFor(OrganicActionKind::LEGACY_TIMED_ROTATION));
    LIVING_CHECK(legacyTimed.decision == OrganicDecision::Deny);
    LIVING_CHECK(legacyTimed.reason == OrganicReasonCode::LegacyLifecycleExcluded);

    OrganicPolicyResult const reset =
        EvaluateOrganicPolicy(OrganicRequestFor(OrganicActionKind::POPULATION_RESET_RECREATE));
    LIVING_CHECK(reset.decision == OrganicDecision::Deny);
    LIVING_CHECK(reset.reason == OrganicReasonCode::ManagedOperationRequired);

    OrganicPolicyResult const rawReset =
        EvaluateOrganicPolicy(OrganicRequestFor(OrganicActionKind::RAW_POPULATION_SQL_RESET));
    LIVING_CHECK(rawReset.decision == OrganicDecision::Deny);
    LIVING_CHECK(rawReset.reason == OrganicReasonCode::RawResetUnsupported);

    OrganicPolicyResult const admin =
        EvaluateOrganicPolicy(OrganicRequestFor(OrganicActionKind::ADMIN_BYPASS_MUTATION));
    LIVING_CHECK(admin.decision == OrganicDecision::Deny);
    LIVING_CHECK(admin.reason == OrganicReasonCode::NoApprovedReconciler);
}

LIVING_TEST(policy_bootstrap_creation_requires_active_managed_bootstrap)
{
    OrganicRequest request = OrganicRequestFor(OrganicActionKind::CORE_CHARACTER_CREATE);
    request.source = OrganicSourceKind::FactoryBootstrap;

    OrganicPolicyResult const denied = EvaluateOrganicPolicy(request);
    LIVING_CHECK(denied.decision == OrganicDecision::Deny);
    LIVING_CHECK(denied.reason == OrganicReasonCode::BootstrapNotActive);

    request.managedBootstrapActive = true;
    OrganicPolicyResult const allowed = EvaluateOrganicPolicy(request);
    LIVING_CHECK(allowed.decision == OrganicDecision::AllowGameplay);
    LIVING_CHECK(allowed.reason == OrganicReasonCode::BootstrapCreation);
}

LIVING_TEST(policy_stuck_teleport_requires_exhausted_ladder_and_owner_consent)
{
    OrganicRequest request = OrganicRequestFor(OrganicActionKind::STUCK_EMERGENCY_TELEPORT);
    request.source = OrganicSourceKind::RecoveryService;

    OrganicPolicyResult const ladderNotDone = EvaluateOrganicPolicy(request);
    LIVING_CHECK(ladderNotDone.decision == OrganicDecision::Deny);
    LIVING_CHECK(ladderNotDone.reason == OrganicReasonCode::RecoveryLadderNotExhausted);

    request.recoveryLadderExhausted = true;
    OrganicPolicyResult const audit = EvaluateOrganicPolicy(request);
    LIVING_CHECK(audit.decision == OrganicDecision::RequireAudit);
    LIVING_CHECK(audit.reason == OrganicReasonCode::AuditRequired);

    request.protectedRealPlayerCommitment = true;
    OrganicPolicyResult const blockedByCommitment = EvaluateOrganicPolicy(request);
    LIVING_CHECK(blockedByCommitment.decision == OrganicDecision::Deny);
    LIVING_CHECK(blockedByCommitment.reason == OrganicReasonCode::ProtectedCommitmentBlocksRecovery);

    request.ownerAuthorizedRecovery = true;
    LIVING_CHECK(EvaluateOrganicPolicy(request).decision == OrganicDecision::RequireAudit);
}

LIVING_TEST(policy_transport_group_sync_requires_protected_commitment)
{
    OrganicRequest request = OrganicRequestFor(OrganicActionKind::TRANSPORT_GROUP_SYNC);
    request.source = OrganicSourceKind::TransportService;

    OrganicPolicyResult const denied = EvaluateOrganicPolicy(request);
    LIVING_CHECK(denied.decision == OrganicDecision::Deny);
    LIVING_CHECK(denied.reason == OrganicReasonCode::MissingProtectedCommitment);

    request.protectedRealPlayerCommitment = true;
    OrganicPolicyResult const audit = EvaluateOrganicPolicy(request);
    LIVING_CHECK(audit.decision == OrganicDecision::RequireAudit);
    LIVING_CHECK(audit.reason == OrganicReasonCode::AuditRequired);
}

LIVING_TEST(policy_public_transport_requires_allowlisted_route)
{
    OrganicRequest request = OrganicRequestFor(OrganicActionKind::PUBLIC_TRANSPORT_TRANSFER);
    request.source = OrganicSourceKind::TransportService;

    OrganicPolicyResult const denied = EvaluateOrganicPolicy(request);
    LIVING_CHECK(denied.decision == OrganicDecision::Deny);
    LIVING_CHECK(denied.reason == OrganicReasonCode::RouteNotAllowlisted);

    request.canonicalRouteAllowlisted = true;
    OrganicPolicyResult const audit = EvaluateOrganicPolicy(request);
    LIVING_CHECK(audit.decision == OrganicDecision::RequireAudit);
    LIVING_CHECK(audit.reason == OrganicReasonCode::AuditRequired);
}

LIVING_TEST(policy_only_three_actions_can_ever_require_audit)
{
    for (size_t i = 0; i < static_cast<size_t>(OrganicActionKind::Count); ++i)
    {
        OrganicActionKind const kind = static_cast<OrganicActionKind>(i);

        // Grant every context flag; only the three named 0002C actions may reach
        // RequireAudit even then.
        OrganicRequest request = OrganicRequestFor(kind);
        request.protectedRealPlayerCommitment = true;
        request.managedBootstrapActive = true;
        request.recoveryLadderExhausted = true;
        request.ownerAuthorizedRecovery = true;
        request.canonicalRouteAllowlisted = true;

        bool const isApproved = kind == OrganicActionKind::STUCK_EMERGENCY_TELEPORT
            || kind == OrganicActionKind::TRANSPORT_GROUP_SYNC
            || kind == OrganicActionKind::PUBLIC_TRANSPORT_TRANSFER;

        OrganicPolicyResult const result = EvaluateOrganicPolicy(request);
        LIVING_CHECK((result.decision == OrganicDecision::RequireAudit) == isApproved);
    }
}

LIVING_TEST(policy_evaluation_is_deterministic)
{
    for (size_t i = 0; i <= static_cast<size_t>(OrganicActionKind::Count); ++i)
    {
        OrganicRequest const request = OrganicRequestFor(static_cast<OrganicActionKind>(i));
        LIVING_CHECK(SameResult(EvaluateOrganicPolicy(request), EvaluateOrganicPolicy(request)));

        OrganicRequest const disabled = DisabledRequestFor(static_cast<OrganicActionKind>(i));
        LIVING_CHECK(SameResult(EvaluateOrganicPolicy(disabled), EvaluateOrganicPolicy(disabled)));
    }
}

LIVING_TEST(policy_string_conversions_are_stable)
{
    LIVING_CHECK(std::strcmp(ToString(OrganicDecision::AllowGameplay), "AllowGameplay") == 0);
    LIVING_CHECK(std::strcmp(ToString(OrganicDecision::AllowAutomation), "AllowAutomation") == 0);
    LIVING_CHECK(std::strcmp(ToString(OrganicDecision::RequireAudit), "RequireAudit") == 0);
    LIVING_CHECK(std::strcmp(ToString(OrganicDecision::Deny), "Deny") == 0);

    LIVING_CHECK(std::strcmp(ToString(OrganicReasonCode::UnknownAction), "UNKNOWN_ACTION") == 0);
    LIVING_CHECK(std::strcmp(ToString(OrganicReasonCode::LegacyPassthrough), "LEGACY_PASSTHROUGH") == 0);
    LIVING_CHECK(std::strcmp(ToString(OrganicReasonCode::AuditRequired), "AUDIT_REQUIRED") == 0);
    LIVING_CHECK(std::strcmp(ToString(OrganicReasonCode::RouteNotAllowlisted), "ROUTE_NOT_ALLOWLISTED") == 0);

    LIVING_CHECK(std::strcmp(ToString(OrganicSourceKind::TestFixture), "TestFixture") == 0);
    LIVING_CHECK(std::strcmp(ToString(OrganicClassification::RequireAudit), "RequireAudit") == 0);
}
