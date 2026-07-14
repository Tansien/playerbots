#pragma once

#include <cstdint>

namespace living
{
    // Complete typed inventory of the synthetic-mutation, shortcut, and gameplay
    // action families guarded by the Organic policy. Seeded from the normative
    // compatibility matrix in docs/design/0002-organic-policy-compatibility-matrix.md
    // (0002A) plus the cheat-mask categories in PlayerbotAIConfig.h.
    //
    // Safety invariant (0002A A.2): every enumerator has exactly one metadata row in
    // OrganicActionMetadata.cpp; a missing row fails a static_assert there. A generic
    // "misc synthetic action" or "unknown shortcut" escape value is prohibited:
    // an action that has no enumerator fails closed as unknown.
    enum class OrganicActionKind : uint16_t
    {
        // managed bootstrap
        CORE_CHARACTER_CREATE = 0,

        // randomization families
        RANDOMIZE_INSTANT,
        RANDOMIZE_FULL,
        RANDOMIZE_INCREMENTAL,
        RANDOMIZE_HOTFIX,

        // level and experience
        LEVEL_ASSIGN,
        XP_ASSIGN,
        XP_MULTIPLIER,
        LEVEL_SYNC,

        // gear and enchantments
        GEAR_INIT,
        GEAR_UPGRADE,
        SYNTHETIC_ENCHANT_INIT,
        AUTO_ENCHANT_ON_UPGRADE,

        // money
        MONEY_INIT,
        MONEY_PERIODIC,
        TEMP_MONEY_TRICK,
        QUEST_MONEY_FABRICATION,

        // bags, inventory, and consumables
        BAGS_INVENTORY_INIT,
        CONSUMABLES_INIT,
        AMMO_REPLENISH,

        // skills, spells, and free learning
        SKILLS_INIT,
        SPELLS_INIT,
        FREE_TRAINER_MODE,
        AUTO_LEARN_TRAINER_SPELLS,
        AUTO_LEARN_QUEST_SPELLS,
        AUTO_LEARN_DROPPED_SPELLS,
        AUTO_LEARN_ITEM_FABRICATION,

        // talents
        TALENT_SPEND_EARNED,
        TALENT_INIT_SYNTHETIC,

        // quests
        PREQUEST_INIT,
        CONFIGURED_QUEST_REWARD,
        QUEST_COMPLETE_GENERIC,
        QUEST_SYNC_TO_BOT,
        QUEST_SYNC_TO_PLAYER,

        // character-state initialization
        WORLD_BUFF_APPLY,
        REPUTATION_INIT,
        TAXI_NODES_INIT,
        MOUNT_INIT,
        PET_INIT,

        // social bootstrap
        GUILD_BOOTSTRAP,
        ARENA_TEAM_BOOTSTRAP,

        // the 13 configured cheat-mask categories (BotCheatMask)
        CHEAT_TAXI,
        CHEAT_GOLD,
        CHEAT_HEALTH,
        CHEAT_MANA,
        CHEAT_POWER,
        CHEAT_ITEM,
        CHEAT_COOLDOWN,
        CHEAT_REPAIR,
        CHEAT_MOVESPEED,
        CHEAT_ATTACKSPEED,
        CHEAT_BREATH,
        CHEAT_GLYPH,
        CHEAT_QUEST,
        CHEAT_RUNTIME_OVERRIDE,

        // relocation shortcuts
        RANDOM_TELEPORT,
        TELEPORT_NEAR_PLAYER,
        RPG_CAMP_TELEPORT,
        FREE_SUMMON,
        RANDOM_MANAGER_REVIVE,

        // transport
        LEGACY_TRANSPORT_SHORTCUT,
        PUBLIC_TRANSPORT_TRANSFER,
        TRANSPORT_GROUP_SYNC,

        // recovery
        STUCK_EMERGENCY_TELEPORT,
        GAMEPLAY_DEATH_RECOVERY,

        // ordinary gameplay through core handlers
        GAMEPLAY_LOOT,
        GAMEPLAY_QUEST_REWARD,
        GAMEPLAY_EQUIP,
        GAMEPLAY_TAXI_FLIGHT,
        GAMEPLAY_HEARTHSTONE,
        GAMEPLAY_ELIGIBLE_PORTAL,
        GAMEPLAY_ELIGIBLE_SUMMON,

        // ordinary transactions automated by existing actions
        TRAINER_PURCHASE,
        VENDOR_REPAIR_TRANSACTION,
        AUCTION_TRANSACTION,
        MAIL_TRANSACTION,
        TRADE_TRANSACTION,

        // lifecycle and population operations
        LEGACY_LOGIN_ROTATION,
        LEGACY_TIMED_ROTATION,
        POPULATION_RESET_RECREATE,
        RAW_POPULATION_SQL_RESET,
        OFFLINE_PROGRESSION,

        // broad maintenance and administration
        BROAD_MAINTENANCE_COMMAND,
        ADMIN_BYPASS_MUTATION,

        // test fixtures
        FIXTURE_PROVISION,

        Count
    };
}
