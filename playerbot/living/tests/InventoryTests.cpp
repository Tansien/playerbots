#include "LivingTest.h"

#include "../policy/OrganicActionMetadata.h"

#include <cstring>
#include <set>
#include <string>

using namespace living;

LIVING_TEST(inventory_every_kind_has_metadata)
{
    for (size_t i = 0; i < static_cast<size_t>(OrganicActionKind::Count); ++i)
    {
        OrganicActionMetadata const* row = TryGetOrganicActionMetadata(static_cast<OrganicActionKind>(i));
        LIVING_CHECK(row != nullptr);
        LIVING_CHECK(row->kind == static_cast<OrganicActionKind>(i));
        LIVING_CHECK(row->name != nullptr && row->name[0] != '\0');
        LIVING_CHECK(row->legacySource != nullptr && row->legacySource[0] != '\0');
        LIVING_CHECK(row->designReference != nullptr && row->designReference[0] != '\0');
    }

    LIVING_CHECK(TryGetOrganicActionMetadata(OrganicActionKind::Count) == nullptr);
}

LIVING_TEST(inventory_stable_names_unique_and_resolvable)
{
    std::set<std::string> names;
    for (OrganicActionMetadata const& row : AllOrganicActionMetadata())
    {
        LIVING_CHECK(names.insert(row.name).second);
        LIVING_CHECK(FindOrganicActionByName(row.name) == &row);
    }

    LIVING_CHECK(names.size() == static_cast<size_t>(OrganicActionKind::Count));
    LIVING_CHECK(FindOrganicActionByName("NO_SUCH_ACTION") == nullptr);
    LIVING_CHECK(FindOrganicActionByName(nullptr) == nullptr);
}

LIVING_TEST(inventory_only_approved_actions_require_audit_and_name_reconcilers)
{
    std::set<std::string> audited;
    for (OrganicActionMetadata const& row : AllOrganicActionMetadata())
    {
        if (row.classification == OrganicClassification::RequireAudit)
        {
            LIVING_CHECK(row.auditReconciler != nullptr && row.auditReconciler[0] != '\0');
            audited.insert(row.name);
        }
        else
            LIVING_CHECK(row.auditReconciler == nullptr);
    }

    // The exact 0.1 set from 0002C: nothing else may require audit.
    LIVING_CHECK(audited.size() == 3);
    LIVING_CHECK(audited.count("STUCK_EMERGENCY_TELEPORT") == 1);
    LIVING_CHECK(audited.count("TRANSPORT_GROUP_SYNC") == 1);
    LIVING_CHECK(audited.count("PUBLIC_TRANSPORT_TRANSFER") == 1);
}

LIVING_TEST(inventory_represents_all_13_cheat_categories)
{
    // Mirrors BotCheatMask in PlayerbotAIConfig.h (taxi..quest) plus the runtime
    // override; every category must be an explicit denied kind.
    char const* const expected[] = {
        "CHEAT_TAXI", "CHEAT_GOLD", "CHEAT_HEALTH", "CHEAT_MANA", "CHEAT_POWER",
        "CHEAT_ITEM", "CHEAT_COOLDOWN", "CHEAT_REPAIR", "CHEAT_MOVESPEED",
        "CHEAT_ATTACKSPEED", "CHEAT_BREATH", "CHEAT_GLYPH", "CHEAT_QUEST"
    };

    size_t cheatRows = 0;
    for (OrganicActionMetadata const& row : AllOrganicActionMetadata())
        if (row.category == OrganicActionCategory::Cheat)
            ++cheatRows;

    LIVING_CHECK(cheatRows == 14); // 13 mask bits + CHEAT_RUNTIME_OVERRIDE

    for (char const* name : expected)
    {
        OrganicActionMetadata const* row = FindOrganicActionByName(name);
        LIVING_CHECK(row != nullptr);
        LIVING_CHECK(row->category == OrganicActionCategory::Cheat);
        LIVING_CHECK(row->classification == OrganicClassification::Deny);
        LIVING_CHECK(!row->productionEligible);
    }

    OrganicActionMetadata const* runtimeOverride = FindOrganicActionByName("CHEAT_RUNTIME_OVERRIDE");
    LIVING_CHECK(runtimeOverride != nullptr);
    LIVING_CHECK(runtimeOverride->classification == OrganicClassification::FixtureOnly);
}

LIVING_TEST(inventory_fixture_only_rows_are_never_production_eligible)
{
    size_t fixtureRows = 0;
    for (OrganicActionMetadata const& row : AllOrganicActionMetadata())
    {
        if (row.fixtureOnly)
        {
            ++fixtureRows;
            LIVING_CHECK(row.classification == OrganicClassification::FixtureOnly);
            LIVING_CHECK(!row.productionEligible);
        }

        // Denied rows can never be production eligible either.
        if (row.classification == OrganicClassification::Deny)
            LIVING_CHECK(!row.productionEligible);
    }

    LIVING_CHECK(fixtureRows == 3); // CHEAT_RUNTIME_OVERRIDE, BROAD_MAINTENANCE_COMMAND, FIXTURE_PROVISION
}

LIVING_TEST(inventory_covers_required_shortcut_families)
{
    // Spot-check the families the Phase 0 contract calls out by name; each must be
    // present and classified. The completeness static_assert covers structure; this
    // covers presence of the required rows.
    char const* const required[] = {
        "RANDOMIZE_INSTANT", "RANDOMIZE_FULL", "RANDOMIZE_INCREMENTAL", "RANDOMIZE_HOTFIX",
        "LEVEL_ASSIGN", "XP_ASSIGN", "XP_MULTIPLIER", "LEVEL_SYNC",
        "GEAR_INIT", "GEAR_UPGRADE", "SYNTHETIC_ENCHANT_INIT", "AUTO_ENCHANT_ON_UPGRADE",
        "MONEY_INIT", "MONEY_PERIODIC", "TEMP_MONEY_TRICK", "QUEST_MONEY_FABRICATION",
        "BAGS_INVENTORY_INIT", "CONSUMABLES_INIT", "AMMO_REPLENISH",
        "SKILLS_INIT", "SPELLS_INIT", "FREE_TRAINER_MODE",
        "AUTO_LEARN_TRAINER_SPELLS", "AUTO_LEARN_QUEST_SPELLS", "AUTO_LEARN_DROPPED_SPELLS",
        "AUTO_LEARN_ITEM_FABRICATION",
        "TALENT_SPEND_EARNED", "TALENT_INIT_SYNTHETIC",
        "PREQUEST_INIT", "CONFIGURED_QUEST_REWARD", "QUEST_COMPLETE_GENERIC",
        "QUEST_SYNC_TO_BOT", "QUEST_SYNC_TO_PLAYER",
        "WORLD_BUFF_APPLY", "REPUTATION_INIT", "TAXI_NODES_INIT", "MOUNT_INIT", "PET_INIT",
        "GUILD_BOOTSTRAP", "ARENA_TEAM_BOOTSTRAP",
        "RANDOM_TELEPORT", "TELEPORT_NEAR_PLAYER", "RPG_CAMP_TELEPORT", "BG_REGROUP_TELEPORT",
        "MINIMAL_MOVE_TELEPORT", "LEGACY_TRANSPORT_SHORTCUT", "FREE_SUMMON",
        "DIRECT_SUMMON_TELEPORT", "RANDOM_MANAGER_REVIVE",
        "PUBLIC_TRANSPORT_TRANSFER", "TRANSPORT_GROUP_SYNC", "STUCK_EMERGENCY_TELEPORT",
        "GAMEPLAY_DEATH_RECOVERY", "INSTANT_REPOP_RELOCATE",
        "ACCOUNT_TRANSFER_HIRE", "DIRECT_ITEM_SPLIT_TRANSFER", "DIRECT_ITEM_OWNERSHIP_TRANSFER",
        "LEGACY_DIRECT_MAIL_MUTATION", "RTSC_SPELL_GRANT",
        "LEGACY_LOGIN_ROTATION", "LEGACY_TIMED_ROTATION",
        "BROAD_MAINTENANCE_COMMAND", "POPULATION_RESET_RECREATE", "RAW_POPULATION_SQL_RESET",
        "OFFLINE_PROGRESSION", "CHEAT_RUNTIME_OVERRIDE", "CORE_CHARACTER_CREATE",
        "FIXTURE_PROVISION", "ADMIN_BYPASS_MUTATION"
    };

    for (char const* name : required)
        LIVING_CHECK(FindOrganicActionByName(name) != nullptr);
}
