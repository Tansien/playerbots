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
        request.mode = LivingRealmMode::Organic;
        return request;
    }

    OrganicRequest DisabledRequestFor(OrganicActionKind kind)
    {
        OrganicRequest request = OrganicRequestFor(kind);
        request.mode = LivingRealmMode::Disabled;
        request.provenance = BotProvenance::LEGACY_UNMANAGED;
        return request;
    }

    // A request with every 0002C gate for `kind` satisfied: the single eligible
    // shape from which each negative test removes exactly one gate.
    OrganicRequest EligibleAuditedRequest(OrganicActionKind kind)
    {
        OrganicRequest request = OrganicRequestFor(kind);
        request.source = OrganicSourceKind::TransportService;
        request.safeForCompatibilityAction = true;
        request.rateLimitOk = true;

        switch (kind)
        {
            case OrganicActionKind::PUBLIC_TRANSPORT_TRANSFER:
                request.canonicalRouteAllowlisted = true;
                request.typedGoalRouteBound = true;
                request.atExactOriginNode = true;
                request.originWaitSatisfied = true;
                break;
            case OrganicActionKind::CORE_CHARACTER_CREATE:
                break;
            case OrganicActionKind::TRANSPORT_GROUP_SYNC:
                request.protectedRealPlayerCommitment = true;
                request.ownerOnVerifiedTransport = true;
                request.nearBoardingContext = true;
                request.destinationMapSupported = true;
                break;
            case OrganicActionKind::STUCK_EMERGENCY_TELEPORT:
                request.source = OrganicSourceKind::RecoveryService;
                request.recoveryLadderExhausted = true;
                request.safeDestinationSelected = true;
                break;
            default:
                break;
        }

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
    request.mode = LivingRealmMode::UnknownProfile;
    OrganicPolicyResult const result = EvaluateOrganicPolicy(request);
    LIVING_CHECK(result.decision == OrganicDecision::Deny);
    LIVING_CHECK(result.reason == OrganicReasonCode::UnsupportedProfile);

    // An out-of-range mode value is corrupted input and also fails closed.
    request.mode = static_cast<LivingRealmMode>(0xFF);
    LIVING_CHECK(EvaluateOrganicPolicy(request).decision == OrganicDecision::Deny);
}

LIVING_TEST(policy_unspecified_mode_fails_closed)
{
    // A default-constructed request denies: legacy passthrough is a privilege of
    // an explicitly Disabled realm, so a caller that forgets to resolve the mode
    // cannot fail open.
    OrganicRequest defaulted;
    OrganicPolicyResult const result = EvaluateOrganicPolicy(defaulted);
    LIVING_CHECK(result.decision == OrganicDecision::Deny);

    OrganicRequest request = OrganicRequestFor(OrganicActionKind::GAMEPLAY_LOOT);
    request.mode = LivingRealmMode::Unspecified;
    request.managedBootstrapActive = true;
    request.protectedRealPlayerCommitment = true;
    request.recoveryLadderExhausted = true;
    request.ownerAuthorizedRecovery = true;
    request.canonicalRouteAllowlisted = true;
    OrganicPolicyResult const unspecified = EvaluateOrganicPolicy(request);
    LIVING_CHECK(unspecified.decision == OrganicDecision::Deny);
    LIVING_CHECK(unspecified.reason == OrganicReasonCode::ModeUnspecified);
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
    LIVING_CHECK(EvaluateOrganicPolicy(OrganicRequestFor(OrganicActionKind::GAMEPLAY_QUEST_ACCEPT)).decision
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
        OrganicActionKind::RPG_CAMP_TELEPORT, OrganicActionKind::BG_REGROUP_TELEPORT,
        OrganicActionKind::MINIMAL_MOVE_TELEPORT, OrganicActionKind::LEGACY_TRANSPORT_SHORTCUT,
        OrganicActionKind::FREE_SUMMON, OrganicActionKind::DIRECT_SUMMON_TELEPORT,
        OrganicActionKind::RANDOM_MANAGER_REVIVE,
        OrganicActionKind::INSTANT_REPOP_RELOCATE, OrganicActionKind::ACCOUNT_TRANSFER_HIRE,
        OrganicActionKind::DIRECT_ITEM_SPLIT_TRANSFER, OrganicActionKind::DIRECT_ITEM_OWNERSHIP_TRANSFER,
        OrganicActionKind::DIRECT_ITEM_DESTRUCTION,
        OrganicActionKind::LEGACY_DIRECT_MAIL_MUTATION, OrganicActionKind::DIRECT_AUCTION_MUTATION,
        OrganicActionKind::RTSC_SPELL_GRANT,
        OrganicActionKind::GUILD_BANK_TAB_MUTATION, OrganicActionKind::REMOTE_QUEST_ACCEPT,
        OrganicActionKind::DIRECT_QUEST_ABANDON, OrganicActionKind::DEATH_RECOVERY_TELEPORT,
        OrganicActionKind::UNOBSERVED_MOVE_TELEPORT, OrganicActionKind::FREE_PET_HAPPINESS,
        OrganicActionKind::DIRECT_AURA_REMOVAL, OrganicActionKind::ENCOUNTER_AURA_MUTATION,
        OrganicActionKind::LOGIN_ITEM_FABRICATION, OrganicActionKind::FREE_TALENT_RESPEC,
        OrganicActionKind::DIRECT_GLYPH_MUTATION, OrganicActionKind::DIRECT_SPIRIT_HEALER_RESURRECTION,
        OrganicActionKind::SHARED_RESPAWN_ACCELERATION,
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

LIVING_TEST(policy_bootstrap_creation_is_pre_identity_and_factory_bound)
{
    // The core generates the low GUID inside Player::Create and the managed root
    // is committed afterwards (0003 section 2), so creation must be authorized
    // with NO identity yet. Requiring an existing ORGANIC_CREATED root here would
    // be circular and could only be satisfied by a fake identity.
    OrganicRequest create;
    create.kind = OrganicActionKind::CORE_CHARACTER_CREATE;
    create.mode = LivingRealmMode::Organic;
    create.source = OrganicSourceKind::FactoryBootstrap;
    create.managedBootstrapActive = true;
    create.characterGuid = 0;            // no identity exists yet
    create.identityNonce = {};           // ...
    create.provenance = BotProvenance::LEGACY_UNMANAGED; // ...and no provenance either

    OrganicPolicyResult const allowed = EvaluateOrganicPolicy(create);
    LIVING_CHECK(allowed.decision == OrganicDecision::AllowGameplay);
    LIVING_CHECK(allowed.reason == OrganicReasonCode::BootstrapCreation);

    // Only the managed factory may create, and only during an active operation.
    OrganicRequest wrongSource = create;
    wrongSource.source = OrganicSourceKind::AiUpdate;
    LIVING_CHECK(EvaluateOrganicPolicy(wrongSource).decision == OrganicDecision::Deny);
    LIVING_CHECK(EvaluateOrganicPolicy(wrongSource).reason == OrganicReasonCode::BootstrapWrongSource);

    for (OrganicSourceKind source : { OrganicSourceKind::RandomManager, OrganicSourceKind::PlayerChatCommand,
        OrganicSourceKind::ConsoleCommand, OrganicSourceKind::AdminSurface, OrganicSourceKind::TestFixture })
    {
        OrganicRequest other = create;
        other.source = source;
        LIVING_CHECK(EvaluateOrganicPolicy(other).decision == OrganicDecision::Deny);
    }

    OrganicRequest notBootstrapping = create;
    notBootstrapping.managedBootstrapActive = false;
    LIVING_CHECK(EvaluateOrganicPolicy(notBootstrapping).decision == OrganicDecision::Deny);
    LIVING_CHECK(EvaluateOrganicPolicy(notBootstrapping).reason == OrganicReasonCode::BootstrapNotActive);

    // A request that already carries a root is not a creation.
    OrganicRequest alreadyCreated = create;
    alreadyCreated.characterGuid = 1000;
    alreadyCreated.identityNonce = { 1, 2, 3 };
    alreadyCreated.provenance = BotProvenance::ORGANIC_CREATED;
    LIVING_CHECK(EvaluateOrganicPolicy(alreadyCreated).decision == OrganicDecision::Deny);
    LIVING_CHECK(EvaluateOrganicPolicy(alreadyCreated).reason == OrganicReasonCode::BootstrapIdentityPresent);

    // Post-create, every other action still requires the real root: the
    // pre-identity exemption is creation-only and does not leak.
    OrganicRequest postCreate;
    postCreate.kind = OrganicActionKind::GAMEPLAY_LOOT;
    postCreate.mode = LivingRealmMode::Organic;
    postCreate.source = OrganicSourceKind::FactoryBootstrap;
    postCreate.managedBootstrapActive = true;
    postCreate.provenance = BotProvenance::ORGANIC_CREATED;
    LIVING_CHECK(EvaluateOrganicPolicy(postCreate).reason == OrganicReasonCode::InvalidIdentity);

    // Disabled mode remains a passthrough (LR-001).
    OrganicRequest disabled = create;
    disabled.mode = LivingRealmMode::Disabled;
    LIVING_CHECK(EvaluateOrganicPolicy(disabled).reason == OrganicReasonCode::LegacyPassthrough);
}

LIVING_TEST(policy_stuck_teleport_requires_every_recovery_gate)
{
    // The eligible shape is audited; each gate removed individually denies with its
    // own reason (0002C C.6/C.7).
    LIVING_CHECK(EvaluateOrganicPolicy(
        EligibleAuditedRequest(OrganicActionKind::STUCK_EMERGENCY_TELEPORT)).decision
        == OrganicDecision::RequireAudit);

    OrganicRequest ladder = EligibleAuditedRequest(OrganicActionKind::STUCK_EMERGENCY_TELEPORT);
    ladder.recoveryLadderExhausted = false;
    LIVING_CHECK(EvaluateOrganicPolicy(ladder).reason == OrganicReasonCode::RecoveryLadderNotExhausted);

    OrganicRequest unsafe = EligibleAuditedRequest(OrganicActionKind::STUCK_EMERGENCY_TELEPORT);
    unsafe.safeForCompatibilityAction = false;
    LIVING_CHECK(EvaluateOrganicPolicy(unsafe).reason == OrganicReasonCode::UnsafeStateForCompatibilityAction);

    OrganicRequest rateLimited = EligibleAuditedRequest(OrganicActionKind::STUCK_EMERGENCY_TELEPORT);
    rateLimited.rateLimitOk = false;
    LIVING_CHECK(EvaluateOrganicPolicy(rateLimited).reason == OrganicReasonCode::RateLimitExceeded);

    OrganicRequest noDestination = EligibleAuditedRequest(OrganicActionKind::STUCK_EMERGENCY_TELEPORT);
    noDestination.safeDestinationSelected = false;
    LIVING_CHECK(EvaluateOrganicPolicy(noDestination).reason == OrganicReasonCode::NoSafeDestination);

    // A protected commitment blocks recovery unless the owner authorized it.
    OrganicRequest committed = EligibleAuditedRequest(OrganicActionKind::STUCK_EMERGENCY_TELEPORT);
    committed.protectedRealPlayerCommitment = true;
    LIVING_CHECK(EvaluateOrganicPolicy(committed).reason == OrganicReasonCode::ProtectedCommitmentBlocksRecovery);
    committed.ownerAuthorizedRecovery = true;
    LIVING_CHECK(EvaluateOrganicPolicy(committed).decision == OrganicDecision::RequireAudit);

    for (OrganicPolicyResult const& denied : { EvaluateOrganicPolicy(ladder), EvaluateOrganicPolicy(unsafe),
        EvaluateOrganicPolicy(rateLimited), EvaluateOrganicPolicy(noDestination) })
        LIVING_CHECK(denied.decision == OrganicDecision::Deny);
}

LIVING_TEST(policy_transport_group_sync_requires_every_gate)
{
    LIVING_CHECK(EvaluateOrganicPolicy(
        EligibleAuditedRequest(OrganicActionKind::TRANSPORT_GROUP_SYNC)).decision
        == OrganicDecision::RequireAudit);

    OrganicRequest noCommitment = EligibleAuditedRequest(OrganicActionKind::TRANSPORT_GROUP_SYNC);
    noCommitment.protectedRealPlayerCommitment = false;
    LIVING_CHECK(EvaluateOrganicPolicy(noCommitment).reason == OrganicReasonCode::MissingProtectedCommitment);

    OrganicRequest ownerOff = EligibleAuditedRequest(OrganicActionKind::TRANSPORT_GROUP_SYNC);
    ownerOff.ownerOnVerifiedTransport = false;
    LIVING_CHECK(EvaluateOrganicPolicy(ownerOff).reason == OrganicReasonCode::OwnerNotOnVerifiedTransport);

    OrganicRequest farAway = EligibleAuditedRequest(OrganicActionKind::TRANSPORT_GROUP_SYNC);
    farAway.nearBoardingContext = false;
    LIVING_CHECK(EvaluateOrganicPolicy(farAway).reason == OrganicReasonCode::NotNearBoardingContext);

    OrganicRequest badMap = EligibleAuditedRequest(OrganicActionKind::TRANSPORT_GROUP_SYNC);
    badMap.destinationMapSupported = false;
    LIVING_CHECK(EvaluateOrganicPolicy(badMap).reason == OrganicReasonCode::DestinationMapUnsupported);

    OrganicRequest unsafe = EligibleAuditedRequest(OrganicActionKind::TRANSPORT_GROUP_SYNC);
    unsafe.safeForCompatibilityAction = false;
    LIVING_CHECK(EvaluateOrganicPolicy(unsafe).reason == OrganicReasonCode::UnsafeStateForCompatibilityAction);

    OrganicRequest rateLimited = EligibleAuditedRequest(OrganicActionKind::TRANSPORT_GROUP_SYNC);
    rateLimited.rateLimitOk = false;
    LIVING_CHECK(EvaluateOrganicPolicy(rateLimited).reason == OrganicReasonCode::RateLimitExceeded);

    for (OrganicPolicyResult const& denied : { EvaluateOrganicPolicy(noCommitment), EvaluateOrganicPolicy(ownerOff),
        EvaluateOrganicPolicy(farAway), EvaluateOrganicPolicy(badMap), EvaluateOrganicPolicy(unsafe),
        EvaluateOrganicPolicy(rateLimited) })
        LIVING_CHECK(denied.decision == OrganicDecision::Deny);
}

LIVING_TEST(policy_public_transport_requires_every_gate)
{
    LIVING_CHECK(EvaluateOrganicPolicy(
        EligibleAuditedRequest(OrganicActionKind::PUBLIC_TRANSPORT_TRANSFER)).decision
        == OrganicDecision::RequireAudit);

    OrganicRequest badRoute = EligibleAuditedRequest(OrganicActionKind::PUBLIC_TRANSPORT_TRANSFER);
    badRoute.canonicalRouteAllowlisted = false;
    LIVING_CHECK(EvaluateOrganicPolicy(badRoute).reason == OrganicReasonCode::RouteNotAllowlisted);

    OrganicRequest unboundGoal = EligibleAuditedRequest(OrganicActionKind::PUBLIC_TRANSPORT_TRANSFER);
    unboundGoal.typedGoalRouteBound = false;
    LIVING_CHECK(EvaluateOrganicPolicy(unboundGoal).reason == OrganicReasonCode::GoalRouteNotBound);

    OrganicRequest offOrigin = EligibleAuditedRequest(OrganicActionKind::PUBLIC_TRANSPORT_TRANSFER);
    offOrigin.atExactOriginNode = false;
    LIVING_CHECK(EvaluateOrganicPolicy(offOrigin).reason == OrganicReasonCode::NotAtOriginNode);

    OrganicRequest early = EligibleAuditedRequest(OrganicActionKind::PUBLIC_TRANSPORT_TRANSFER);
    early.originWaitSatisfied = false;
    LIVING_CHECK(EvaluateOrganicPolicy(early).reason == OrganicReasonCode::OriginWaitNotSatisfied);

    OrganicRequest unsafe = EligibleAuditedRequest(OrganicActionKind::PUBLIC_TRANSPORT_TRANSFER);
    unsafe.safeForCompatibilityAction = false;
    LIVING_CHECK(EvaluateOrganicPolicy(unsafe).reason == OrganicReasonCode::UnsafeStateForCompatibilityAction);

    OrganicRequest rateLimited = EligibleAuditedRequest(OrganicActionKind::PUBLIC_TRANSPORT_TRANSFER);
    rateLimited.rateLimitOk = false;
    LIVING_CHECK(EvaluateOrganicPolicy(rateLimited).reason == OrganicReasonCode::RateLimitExceeded);

    // 0002C C.3.3: a protected commitment must be explicitly certified compatible.
    // The gate is positive and default-false, so an uncertified commitment denies
    // rather than relocating the bot away from a real player's obligation.
    OrganicRequest committed = EligibleAuditedRequest(OrganicActionKind::PUBLIC_TRANSPORT_TRANSFER);
    committed.protectedRealPlayerCommitment = true;
    LIVING_CHECK(EvaluateOrganicPolicy(committed).decision == OrganicDecision::Deny);
    LIVING_CHECK(EvaluateOrganicPolicy(committed).reason == OrganicReasonCode::IncompatibleProtectedCommitment);

    committed.protectedCommitmentCompatible = true;
    LIVING_CHECK(EvaluateOrganicPolicy(committed).decision == OrganicDecision::RequireAudit);

    // Certifying compatibility without a commitment changes nothing.
    OrganicRequest uncommitted = EligibleAuditedRequest(OrganicActionKind::PUBLIC_TRANSPORT_TRANSFER);
    uncommitted.protectedCommitmentCompatible = true;
    LIVING_CHECK(EvaluateOrganicPolicy(uncommitted).decision == OrganicDecision::RequireAudit);

    for (OrganicPolicyResult const& denied : { EvaluateOrganicPolicy(badRoute), EvaluateOrganicPolicy(unboundGoal),
        EvaluateOrganicPolicy(offOrigin), EvaluateOrganicPolicy(early), EvaluateOrganicPolicy(unsafe),
        EvaluateOrganicPolicy(rateLimited) })
        LIVING_CHECK(denied.decision == OrganicDecision::Deny);
}

LIVING_TEST(policy_bootstrap_requires_exactly_absent_root)
{
    // Only an EXACTLY absent root (guid 0 AND every nonce byte 0) is pre-create
    // eligible. A half-populated root is malformed input, not "no identity yet",
    // and must not be waved through.
    OrganicRequest create;
    create.kind = OrganicActionKind::CORE_CHARACTER_CREATE;
    create.source = OrganicSourceKind::FactoryBootstrap;
    create.mode = LivingRealmMode::Organic;
    create.managedBootstrapActive = true;
    create.provenance = BotProvenance::ORGANIC_CREATED;

    // Exact absence: authorized.
    LIVING_CHECK(EvaluateOrganicPolicy(create).decision == OrganicDecision::AllowGameplay);
    LIVING_CHECK(EvaluateOrganicPolicy(create).reason == OrganicReasonCode::BootstrapCreation);

    // (nonzero guid, zero nonce): malformed partial root.
    OrganicRequest guidOnly = create;
    guidOnly.characterGuid = 1234;
    LIVING_CHECK(EvaluateOrganicPolicy(guidOnly).decision == OrganicDecision::Deny);
    LIVING_CHECK(EvaluateOrganicPolicy(guidOnly).reason == OrganicReasonCode::InvalidIdentity);

    // (zero guid, nonzero nonce): malformed partial root.
    OrganicRequest nonceOnly = create;
    nonceOnly.identityNonce = { 9 };
    LIVING_CHECK(EvaluateOrganicPolicy(nonceOnly).decision == OrganicDecision::Deny);
    LIVING_CHECK(EvaluateOrganicPolicy(nonceOnly).reason == OrganicReasonCode::InvalidIdentity);

    // A complete root means this is not a creation.
    OrganicRequest complete = create;
    complete.characterGuid = 1234;
    complete.identityNonce = { 9 };
    LIVING_CHECK(EvaluateOrganicPolicy(complete).reason == OrganicReasonCode::BootstrapIdentityPresent);
}

LIVING_TEST(policy_fixture_creation_is_pre_identity_and_provisioning_is_post_identity)
{
    // Fixture creation is the fixture twin of CORE_CHARACTER_CREATE: pre-identity,
    // so it cannot require the FIXTURE root that only exists after creation.
    OrganicRequest create;
    create.kind = OrganicActionKind::FIXTURE_CHARACTER_CREATE;
    create.source = OrganicSourceKind::TestFixture;
    create.mode = LivingRealmMode::Organic;
    create.fixtureTestProfile = true;
    LIVING_CHECK(EvaluateOrganicPolicy(create).decision == OrganicDecision::AllowAutomation);

    // Outside the test profile, and from a non-fixture source, it is denied.
    OrganicRequest production = create;
    production.fixtureTestProfile = false;
    LIVING_CHECK(EvaluateOrganicPolicy(production).decision == OrganicDecision::Deny);

    OrganicRequest wrongSource = create;
    wrongSource.source = OrganicSourceKind::AiUpdate;
    LIVING_CHECK(EvaluateOrganicPolicy(wrongSource).reason == OrganicReasonCode::BootstrapWrongSource);

    // Post-create provisioning requires the committed (guid, nonce, FIXTURE) root.
    OrganicRequest provision;
    provision.kind = OrganicActionKind::FIXTURE_PROVISION;
    provision.source = OrganicSourceKind::TestFixture;
    provision.mode = LivingRealmMode::Organic;
    provision.fixtureTestProfile = true;
    provision.provenance = BotProvenance::FIXTURE;
    LIVING_CHECK(EvaluateOrganicPolicy(provision).reason == OrganicReasonCode::InvalidIdentity); // no root yet

    provision.characterGuid = 4321;
    provision.identityNonce = { 7 };
    LIVING_CHECK(EvaluateOrganicPolicy(provision).decision == OrganicDecision::AllowAutomation);
    LIVING_CHECK(EvaluateOrganicPolicy(provision).reason == OrganicReasonCode::FixtureAuthorized);
}

LIVING_TEST(policy_shared_respawn_acceleration_is_denied)
{
    // A shared-world mutation: it shortens a creature's respawn delay and can
    // remove its corpse, which real players share.
    OrganicPolicyResult const result =
        EvaluateOrganicPolicy(OrganicRequestFor(OrganicActionKind::SHARED_RESPAWN_ACCELERATION));
    LIVING_CHECK(result.decision == OrganicDecision::Deny);
    LIVING_CHECK(result.knownAction);
}

LIVING_TEST(policy_identity_root_must_be_bindable)
{
    // A decision that cannot be bound to (guid, nonce) cannot be attached to a
    // durable audit row or distinguished from a reused GUID (0003 section 2).
    // Creation is deliberately excluded: it is the one pre-identity decision and
    // has its own test. Everything post-create must bind to the real root.
    OrganicActionKind const kinds[] = {
        OrganicActionKind::GAMEPLAY_LOOT,    // gameplay
        OrganicActionKind::TRAINER_PURCHASE  // automation
    };

    for (OrganicActionKind kind : kinds)
    {
        OrganicRequest zeroGuid = OrganicRequestFor(kind);
        zeroGuid.characterGuid = 0;
        LIVING_CHECK(EvaluateOrganicPolicy(zeroGuid).decision == OrganicDecision::Deny);
        LIVING_CHECK(EvaluateOrganicPolicy(zeroGuid).reason == OrganicReasonCode::InvalidIdentity);

        OrganicRequest zeroNonce = OrganicRequestFor(kind);
        zeroNonce.identityNonce = {};
        LIVING_CHECK(EvaluateOrganicPolicy(zeroNonce).decision == OrganicDecision::Deny);
        LIVING_CHECK(EvaluateOrganicPolicy(zeroNonce).reason == OrganicReasonCode::InvalidIdentity);
    }

    // Audited actions and the fixture profile are bound the same way.
    OrganicRequest audited = EligibleAuditedRequest(OrganicActionKind::PUBLIC_TRANSPORT_TRANSFER);
    audited.identityNonce = {};
    LIVING_CHECK(EvaluateOrganicPolicy(audited).reason == OrganicReasonCode::InvalidIdentity);

    OrganicRequest fixture = OrganicRequestFor(OrganicActionKind::FIXTURE_PROVISION);
    fixture.provenance = BotProvenance::FIXTURE;
    fixture.fixtureTestProfile = true;
    fixture.characterGuid = 0;
    LIVING_CHECK(EvaluateOrganicPolicy(fixture).reason == OrganicReasonCode::InvalidIdentity);

    // Explicit disabled-mode parity is preserved (LR-001).
    OrganicRequest disabled = DisabledRequestFor(OrganicActionKind::GAMEPLAY_LOOT);
    disabled.characterGuid = 0;
    disabled.identityNonce = {};
    LIVING_CHECK(EvaluateOrganicPolicy(disabled).decision != OrganicDecision::Deny);
    LIVING_CHECK(EvaluateOrganicPolicy(disabled).reason == OrganicReasonCode::LegacyPassthrough);
}

LIVING_TEST(policy_only_three_actions_can_ever_require_audit)
{
    for (size_t i = 0; i < static_cast<size_t>(OrganicActionKind::Count); ++i)
    {
        OrganicActionKind const kind = static_cast<OrganicActionKind>(i);

        // Grant every context flag; only the three named 0002C actions may reach
        // RequireAudit even then.
        OrganicRequest request = EligibleAuditedRequest(kind);
        request.protectedRealPlayerCommitment = true;
        request.managedBootstrapActive = true;
        request.recoveryLadderExhausted = true;
        request.ownerAuthorizedRecovery = true;
        request.safeDestinationSelected = true;
        request.canonicalRouteAllowlisted = true;
        request.typedGoalRouteBound = true;
        request.atExactOriginNode = true;
        request.originWaitSatisfied = true;
        request.ownerOnVerifiedTransport = true;
        request.nearBoardingContext = true;
        request.destinationMapSupported = true;
        request.protectedCommitmentCompatible = true;

        bool const isApproved = kind == OrganicActionKind::STUCK_EMERGENCY_TELEPORT
            || kind == OrganicActionKind::TRANSPORT_GROUP_SYNC
            || kind == OrganicActionKind::PUBLIC_TRANSPORT_TRANSFER;

        OrganicPolicyResult const result = EvaluateOrganicPolicy(request);
        LIVING_CHECK((result.decision == OrganicDecision::RequireAudit) == isApproved);
    }
}

LIVING_TEST(policy_every_classification_maps_to_its_decision)
{
    // Table-driven conformance over the whole inventory: every kind, present and
    // future, must map to exactly the decision its classification promises, so a
    // newly added kind cannot ship without its intended production behavior.
    for (OrganicActionMetadata const& row : AllOrganicActionMetadata())
    {
        OrganicRequest production = OrganicRequestFor(row.kind);
        OrganicPolicyResult const result = EvaluateOrganicPolicy(production);

        switch (row.classification)
        {
            case OrganicClassification::AllowGameplay:
                LIVING_CHECK(result.decision == OrganicDecision::AllowGameplay);
                break;
            case OrganicClassification::AllowAutomation:
                LIVING_CHECK(result.decision == OrganicDecision::AllowAutomation);
                break;
            case OrganicClassification::Deny:
                LIVING_CHECK(result.decision == OrganicDecision::Deny);
                break;
            case OrganicClassification::BootstrapOnly:
            {
                // An identity-carrying request is never a creation, and a non-factory
                // source cannot create even during an active bootstrap.
                LIVING_CHECK(result.decision == OrganicDecision::Deny);
                OrganicRequest identityBearing = production;
                identityBearing.source = OrganicSourceKind::FactoryBootstrap;
                identityBearing.managedBootstrapActive = true;
                LIVING_CHECK(EvaluateOrganicPolicy(identityBearing).decision == OrganicDecision::Deny);

                OrganicRequest aiUpdate = production;
                aiUpdate.source = OrganicSourceKind::AiUpdate;
                aiUpdate.managedBootstrapActive = true;
                LIVING_CHECK(EvaluateOrganicPolicy(aiUpdate).decision == OrganicDecision::Deny);

                OrganicRequest preIdentity = production;
                preIdentity.source = OrganicSourceKind::FactoryBootstrap;
                preIdentity.managedBootstrapActive = true;
                preIdentity.characterGuid = 0;
                preIdentity.identityNonce = {};
                LIVING_CHECK(EvaluateOrganicPolicy(preIdentity).decision == OrganicDecision::AllowGameplay);
                break;
            }
            case OrganicClassification::RequireAudit:
            {
                // Without action-specific context the audited kinds deny; with every
                // 0002C gate satisfied they reach RequireAudit.
                LIVING_CHECK(result.decision == OrganicDecision::Deny);
                LIVING_CHECK(EvaluateOrganicPolicy(EligibleAuditedRequest(row.kind)).decision
                    == OrganicDecision::RequireAudit);
                break;
            }
            case OrganicClassification::FixtureOnly:
            {
                LIVING_CHECK(result.decision == OrganicDecision::Deny);
                OrganicRequest fixture = production;
                fixture.provenance = BotProvenance::FIXTURE;
                fixture.fixtureTestProfile = true;
                LIVING_CHECK(EvaluateOrganicPolicy(fixture).decision == OrganicDecision::AllowAutomation);
                break;
            }

            case OrganicClassification::FixtureBootstrap:
            {
                // Pre-identity: denied in production, authorized from the fixture
                // source inside the test profile with no root yet.
                LIVING_CHECK(result.decision == OrganicDecision::Deny);
                OrganicRequest fixtureCreate;
                fixtureCreate.kind = row.kind;
                fixtureCreate.source = OrganicSourceKind::TestFixture;
                fixtureCreate.mode = LivingRealmMode::Organic;
                fixtureCreate.fixtureTestProfile = true;
                LIVING_CHECK(EvaluateOrganicPolicy(fixtureCreate).decision == OrganicDecision::AllowAutomation);
                break;
            }
        }
    }
}

LIVING_TEST(policy_invalid_source_fails_closed)
{
    OrganicRequest request = OrganicRequestFor(OrganicActionKind::GAMEPLAY_LOOT);
    request.source = static_cast<OrganicSourceKind>(0xFF);
    OrganicPolicyResult const garbage = EvaluateOrganicPolicy(request);
    LIVING_CHECK(garbage.decision == OrganicDecision::Deny);
    LIVING_CHECK(garbage.reason == OrganicReasonCode::InvalidSource);

    request.source = OrganicSourceKind::Count;
    LIVING_CHECK(EvaluateOrganicPolicy(request).decision == OrganicDecision::Deny);

    // Disabled mode stays a passthrough even with a corrupted source (LR-001).
    request.mode = LivingRealmMode::Disabled;
    LIVING_CHECK(EvaluateOrganicPolicy(request).decision != OrganicDecision::Deny);
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
    LIVING_CHECK(std::strcmp(ToString(LivingRealmMode::Unspecified), "Unspecified") == 0);
    LIVING_CHECK(std::strcmp(ToString(OrganicReasonCode::ModeUnspecified), "MODE_UNSPECIFIED") == 0);
    LIVING_CHECK(std::strcmp(ToString(OrganicReasonCode::InvalidIdentity), "INVALID_IDENTITY") == 0);
    LIVING_CHECK(std::strcmp(ToString(OrganicReasonCode::UnsafeStateForCompatibilityAction),
        "UNSAFE_STATE_FOR_COMPATIBILITY_ACTION") == 0);
    LIVING_CHECK(std::strcmp(ToString(OrganicReasonCode::OriginWaitNotSatisfied), "ORIGIN_WAIT_NOT_SATISFIED") == 0);
    LIVING_CHECK(std::strcmp(ToString(OrganicReasonCode::BootstrapWrongSource), "BOOTSTRAP_WRONG_SOURCE") == 0);
    LIVING_CHECK(std::strcmp(ToString(OrganicReasonCode::IncompatibleProtectedCommitment),
        "INCOMPATIBLE_PROTECTED_COMMITMENT") == 0);
    LIVING_CHECK(std::strcmp(ToString(OrganicClassification::RequireAudit), "RequireAudit") == 0);
}
