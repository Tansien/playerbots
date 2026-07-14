#pragma once

#include <cstdint>

namespace living
{
    // Typed inventory of the synthetic-mutation, shortcut, and gameplay action
    // families guarded by the Organic policy. Seeded from the normative
    // compatibility matrix in docs/design/0002-organic-policy-compatibility-matrix.md
    // (0002A) plus the cheat-mask categories in PlayerbotAIConfig.h, and extended by
    // Phase 0 source audits. Per the 0002A A.2 completion rule the inventory is a
    // growing seed, not a completeness claim: proving no state-changing shortcut
    // exists outside it is a 0.1 acceptance gate (repository search, call-graph
    // review, and runtime instrumentation), and every newly found path must get its
    // own enumerator and matrix row.
    //
    // Safety invariant (0002A A.2): every enumerator has exactly one metadata row in
    // OrganicActionMetadata.cpp; a missing row fails a static_assert there. A generic
    // "misc synthetic action" or "unknown shortcut" escape value is prohibited:
    // an action that has no enumerator fails closed as unknown.
    enum class OrganicActionKind : uint16_t
    {
        // managed bootstrap
        CORE_CHARACTER_CREATE = 0,
        COMMAND_CHARACTER_PROVISION,

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
        DIRECT_ITEM_SPLIT_TRANSFER,
        DIRECT_ITEM_OWNERSHIP_TRANSFER,
        DIRECT_ITEM_DESTRUCTION,
        LOGIN_ITEM_FABRICATION,

        // skills, spells, and free learning
        SKILLS_INIT,
        SPELLS_INIT,
        FREE_TRAINER_MODE,
        AUTO_LEARN_TRAINER_SPELLS,
        AUTO_LEARN_QUEST_SPELLS,
        AUTO_LEARN_DROPPED_SPELLS,
        AUTO_LEARN_ITEM_FABRICATION,
        RTSC_SPELL_GRANT,

        // talents
        TALENT_SPEND_EARNED,
        TALENT_INIT_SYNTHETIC,
        FREE_TALENT_RESPEC,

        // quests
        PREQUEST_INIT,
        CONFIGURED_QUEST_REWARD,
        QUEST_COMPLETE_GENERIC,
        QUEST_SYNC_TO_BOT,
        QUEST_SYNC_TO_PLAYER,
        REMOTE_QUEST_ACCEPT,
        DIRECT_QUEST_ABANDON,

        // character-state initialization
        WORLD_BUFF_APPLY,
        DIRECT_AURA_REMOVAL,
        ENCOUNTER_AURA_MUTATION,
        DIRECT_GLYPH_MUTATION,
        REPUTATION_INIT,
        TAXI_NODES_INIT,
        MOUNT_INIT,
        PET_INIT,
        FREE_PET_HAPPINESS,

        // social bootstrap
        GUILD_BOOTSTRAP,
        ARENA_TEAM_BOOTSTRAP,
        GUILD_BANK_TAB_MUTATION,

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
        BG_REGROUP_TELEPORT,
        MINIMAL_MOVE_TELEPORT,
        UNOBSERVED_MOVE_TELEPORT,
        FREE_SUMMON,
        DIRECT_SUMMON_TELEPORT,
        RANDOM_MANAGER_REVIVE,

        // transport
        LEGACY_TRANSPORT_SHORTCUT,
        PUBLIC_TRANSPORT_TRANSFER,
        TRANSPORT_GROUP_SYNC,

        // recovery
        STUCK_EMERGENCY_TELEPORT,
        GAMEPLAY_DEATH_RECOVERY,
        INSTANT_REPOP_RELOCATE,
        DEATH_RECOVERY_TELEPORT,

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
        DIRECT_AUCTION_MUTATION,
        MAIL_TRANSACTION,
        LEGACY_DIRECT_MAIL_MUTATION,
        TRADE_TRANSACTION,

        // lifecycle and population operations
        LEGACY_LOGIN_ROTATION,
        LEGACY_TIMED_ROTATION,
        ACCOUNT_TRANSFER_HIRE,
        POPULATION_RESET_RECREATE,
        RAW_POPULATION_SQL_RESET,
        OFFLINE_PROGRESSION,

        // broad maintenance and administration
        BROAD_MAINTENANCE_COMMAND,
        DEBUG_MUTATION_COMMAND,
        ADMIN_BYPASS_MUTATION,

        // test fixtures
        FIXTURE_PROVISION,

        Count
    };
}
