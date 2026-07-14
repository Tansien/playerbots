#include "OrganicActionMetadata.h"

#include <cstring>

namespace living
{
    namespace
    {
        constexpr OrganicActionKind K(size_t index) { return static_cast<OrganicActionKind>(index); }

        // One row per OrganicActionKind, in enum order. Classifications implement the
        // normative compatibility matrix (0002A); rows must not be weakened without a
        // design revision.
        constexpr OrganicActionMetadataTable METADATA = { {
            { OrganicActionKind::CORE_CHARACTER_CREATE, "CORE_CHARACTER_CREATE", OrganicActionCategory::Bootstrap,
              "Player::Create via RandomPlayerbotFactory::CreateRandomBots", OrganicClassification::BootstrapOnly,
              nullptr, true, false, "0002A 'Core character creation'" },

            { OrganicActionKind::RANDOMIZE_INSTANT, "RANDOMIZE_INSTANT", OrganicActionCategory::Randomization,
              "AiPlayerbot.InstantRandomize / RandomPlayerbotMgr::RandomizeFirst", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Instant/full/incremental randomization'" },
            { OrganicActionKind::RANDOMIZE_FULL, "RANDOMIZE_FULL", OrganicActionCategory::Randomization,
              "RandomPlayerbotMgr::Randomize / PlayerbotFactory::Randomize(false, ...)", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Instant/full/incremental randomization'" },
            { OrganicActionKind::RANDOMIZE_INCREMENTAL, "RANDOMIZE_INCREMENTAL", OrganicActionCategory::Randomization,
              "PlayerbotFactory::Randomize(true, ...) periodic randomize timer", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Instant/full/incremental randomization'" },
            { OrganicActionKind::RANDOMIZE_HOTFIX, "RANDOMIZE_HOTFIX", OrganicActionCategory::Randomization,
              "RandomPlayerbotMgr::Hotfix version migration", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Randomizer hotfix migration'" },

            { OrganicActionKind::LEVEL_ASSIGN, "LEVEL_ASSIGN", OrganicActionCategory::Progression,
              "Player::GiveLevel/SetLevel via factory or login random level", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Level/XP assignment'" },
            { OrganicActionKind::XP_ASSIGN, "XP_ASSIGN", OrganicActionCategory::Progression,
              "factory/login direct XP assignment", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Level/XP assignment'" },
            { OrganicActionKind::XP_MULTIPLIER, "XP_MULTIPLIER", OrganicActionCategory::Progression,
              "AiPlayerbot.XPRate scaled XP-gain action", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'XP multiplier'" },
            { OrganicActionKind::LEVEL_SYNC, "LEVEL_SYNC", OrganicActionCategory::Progression,
              "AiPlayerbot.SyncLevelWithPlayers / AiPlayerbot.SyncAltLevelToMaster", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Player-level synchronization'" },

            { OrganicActionKind::GEAR_INIT, "GEAR_INIT", OrganicActionCategory::Gear,
              "PlayerbotFactory::InitEquipment / InitEquipmentNew / InitSecondEquipmentSet", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Random gear initialization'" },
            { OrganicActionKind::GEAR_UPGRADE, "GEAR_UPGRADE", OrganicActionCategory::Gear,
              "RandomPlayerbotMgr::UpdateGearSpells / periodic gear upgrade commands", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Periodic gear upgrade/autogear'" },
            { OrganicActionKind::SYNTHETIC_ENCHANT_INIT, "SYNTHETIC_ENCHANT_INIT", OrganicActionCategory::Gear,
              "PlayerbotFactory::EnchantItem / AddGems / ApplyEnchantTemplate / InitGems", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Enchant/gem/template generation'" },
            { OrganicActionKind::AUTO_ENCHANT_ON_UPGRADE, "AUTO_ENCHANT_ON_UPGRADE", OrganicActionCategory::Gear,
              "AiPlayerbot.AutoEnchantUpgradeLoot equip/upgrade hooks", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Auto-enchant on upgrade'" },

            { OrganicActionKind::MONEY_INIT, "MONEY_INIT", OrganicActionCategory::Wealth,
              "PlayerbotFactory starting-money initialization", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Starting/periodic money'" },
            { OrganicActionKind::MONEY_PERIODIC, "MONEY_PERIODIC", OrganicActionCategory::Wealth,
              "RandomPlayerbotMgr::Refresh periodic money grant", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Starting/periodic money'" },
            { OrganicActionKind::TEMP_MONEY_TRICK, "TEMP_MONEY_TRICK", OrganicActionCategory::Wealth,
              "RpgSubActions/purchase helpers: temporary SetMoney around taxi or purchase", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Temporary-money flight/purchase tricks'" },
            { OrganicActionKind::QUEST_MONEY_FABRICATION, "QUEST_MONEY_FABRICATION", OrganicActionCategory::Wealth,
              "QuestAction::CompleteQuest required-money grant", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Required-money fabrication during quest completion'" },

            { OrganicActionKind::BAGS_INVENTORY_INIT, "BAGS_INVENTORY_INIT", OrganicActionCategory::Inventory,
              "PlayerbotFactory::InitBags / InitInventory*", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Bags and generated inventory'" },
            { OrganicActionKind::CONSUMABLES_INIT, "CONSUMABLES_INIT", OrganicActionCategory::Inventory,
              "PlayerbotFactory::InitFood / InitReagents / InitPotions and refresh paths", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Food/ammo/reagents/potions/consumables'" },
            { OrganicActionKind::AMMO_REPLENISH, "AMMO_REPLENISH", OrganicActionCategory::Inventory,
              "PlayerbotFactory::InitAmmo / PlayerbotAI runtime ammo stack refill", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Runtime ammo replenishment'" },
            { OrganicActionKind::DIRECT_ITEM_SPLIT_TRANSFER, "DIRECT_ITEM_SPLIT_TRANSFER", OrganicActionCategory::Inventory,
              "GuildShareItemAction: Item::CreateItem split-stack construction outside trade/mail handlers", OrganicClassification::Deny,
              nullptr, false, false, "0002A A.2 Phase 0 source-audit row (normal trade/mail allowed instead)" },
            { OrganicActionKind::DIRECT_ITEM_OWNERSHIP_TRANSFER, "DIRECT_ITEM_OWNERSHIP_TRANSFER", OrganicActionCategory::Inventory,
              "GiveItemAction / GuildShareItemAction full stack: MoveItemFromInventory + SetOwnerGuid outside trade handlers", OrganicClassification::Deny,
              nullptr, false, false, "0002A A.2 Phase 0 source-audit row (normal trade/mail allowed instead)" },

            { OrganicActionKind::SKILLS_INIT, "SKILLS_INIT", OrganicActionCategory::Learning,
              "PlayerbotFactory::InitSkills / InitAllSkills / InitTradeSkills", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Skills/professions'" },
            { OrganicActionKind::SPELLS_INIT, "SPELLS_INIT", OrganicActionCategory::Learning,
              "PlayerbotFactory::InitSpells / InitAvailableSpells / InitSpecialSpells", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Spell initialization'" },
            { OrganicActionKind::FREE_TRAINER_MODE, "FREE_TRAINER_MODE", OrganicActionCategory::Learning,
              "AiPlayerbot.AutoTrainSpells=free trainer learning without cost", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Free trainer mode'" },
            { OrganicActionKind::AUTO_LEARN_TRAINER_SPELLS, "AUTO_LEARN_TRAINER_SPELLS", OrganicActionCategory::Learning,
              "AiPlayerbot.AutoLearnTrainerSpells", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Auto trainer/quest/drop learning'" },
            { OrganicActionKind::AUTO_LEARN_QUEST_SPELLS, "AUTO_LEARN_QUEST_SPELLS", OrganicActionCategory::Learning,
              "AiPlayerbot.AutoLearnQuestSpells", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Auto trainer/quest/drop learning'" },
            { OrganicActionKind::AUTO_LEARN_DROPPED_SPELLS, "AUTO_LEARN_DROPPED_SPELLS", OrganicActionCategory::Learning,
              "AiPlayerbot.AutoLearnDroppedSpells", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Auto trainer/quest/drop learning'" },
            { OrganicActionKind::AUTO_LEARN_ITEM_FABRICATION, "AUTO_LEARN_ITEM_FABRICATION", OrganicActionCategory::Learning,
              "AutoLearnSpellAction quest reward-item creation/mail", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Auto-learn reward-item fabrication'" },
            { OrganicActionKind::RTSC_SPELL_GRANT, "RTSC_SPELL_GRANT", OrganicActionCategory::Learning,
              "RtscAction: learnSpell/removeSpell(RTSC_MOVE_SPELL) on the requesting real player", OrganicClassification::Deny,
              nullptr, false, false, "0002A A.2 Phase 0 source-audit row (mutates a real player's spell book)" },

            { OrganicActionKind::TALENT_SPEND_EARNED, "TALENT_SPEND_EARNED", OrganicActionCategory::Progression,
              "auto-talent action spending earned talent points", OrganicClassification::AllowAutomation,
              nullptr, true, false, "0002A 'Earned talent spending'" },
            { OrganicActionKind::TALENT_INIT_SYNTHETIC, "TALENT_INIT_SYNTHETIC", OrganicActionCategory::Progression,
              "PlayerbotFactory::InitTalentsTree / InitTalents synthetic spec assignment", OrganicClassification::Deny,
              nullptr, false, false, "0002A randomization family (factory talent initialization)" },

            { OrganicActionKind::PREQUEST_INIT, "PREQUEST_INIT", OrganicActionCategory::Quests,
              "AiPlayerbot.PreQuests bootstrap quest completion", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Prequests/global bot quest rewards'" },
            { OrganicActionKind::CONFIGURED_QUEST_REWARD, "CONFIGURED_QUEST_REWARD", OrganicActionCategory::Quests,
              "AiPlayerbot.RandomBotQuestIds / PlayerbotFactory::InitQuests", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Prequests/global bot quest rewards'" },
            { OrganicActionKind::QUEST_COMPLETE_GENERIC, "QUEST_COMPLETE_GENERIC", OrganicActionCategory::Quests,
              "QuestAction::CompleteQuest / AutoCompleteQuestAction", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Generic quest completion commands'" },
            { OrganicActionKind::QUEST_SYNC_TO_BOT, "QUEST_SYNC_TO_BOT", OrganicActionCategory::Quests,
              "AiPlayerbot.SyncQuestWithPlayer quest-giver sync from player state", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Quest sync to bot'" },
            { OrganicActionKind::QUEST_SYNC_TO_PLAYER, "QUEST_SYNC_TO_PLAYER", OrganicActionCategory::Quests,
              "AiPlayerbot.SyncQuestForPlayer real-player quest-log mutation", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Quest sync to real player'" },

            { OrganicActionKind::WORLD_BUFF_APPLY, "WORLD_BUFF_APPLY", OrganicActionCategory::CharacterInit,
              "AiPlayerbot.WorldBuff* / WorldBuffAction direct aura application", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'World buffs'" },
            { OrganicActionKind::REPUTATION_INIT, "REPUTATION_INIT", OrganicActionCategory::CharacterInit,
              "PlayerbotFactory::InitReputations", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Reputation initialization'" },
            { OrganicActionKind::TAXI_NODES_INIT, "TAXI_NODES_INIT", OrganicActionCategory::CharacterInit,
              "PlayerbotFactory::InitTaxiNodes", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Taxi-node initialization'" },
            { OrganicActionKind::MOUNT_INIT, "MOUNT_INIT", OrganicActionCategory::CharacterInit,
              "PlayerbotFactory::InitMounts", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Mount initialization'" },
            { OrganicActionKind::PET_INIT, "PET_INIT", OrganicActionCategory::CharacterInit,
              "PlayerbotFactory::InitPet / InitPetSpells", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Pet initialization'" },

            { OrganicActionKind::GUILD_BOOTSTRAP, "GUILD_BOOTSTRAP", OrganicActionCategory::Social,
              "RandomPlayerbotFactory::CreateRandomGuilds / PlayerbotFactory::InitGuild", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Guild bootstrap/free tabard'" },
            { OrganicActionKind::ARENA_TEAM_BOOTSTRAP, "ARENA_TEAM_BOOTSTRAP", OrganicActionCategory::Social,
              "RandomPlayerbotFactory::CreateRandomArenaTeams / PlayerbotFactory::InitArenaTeam (TBC/WotLK only)", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Arena-team bootstrap'" },

            { OrganicActionKind::CHEAT_TAXI, "CHEAT_TAXI", OrganicActionCategory::Cheat,
              "BotCheatMask bit 'taxi' via AiPlayerbot.BotCheats/RndBotCheats", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'All 13 configured cheat bits'" },
            { OrganicActionKind::CHEAT_GOLD, "CHEAT_GOLD", OrganicActionCategory::Cheat,
              "BotCheatMask bit 'gold' via AiPlayerbot.BotCheats/RndBotCheats", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'All 13 configured cheat bits'" },
            { OrganicActionKind::CHEAT_HEALTH, "CHEAT_HEALTH", OrganicActionCategory::Cheat,
              "BotCheatMask bit 'health' via AiPlayerbot.BotCheats/RndBotCheats", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'All 13 configured cheat bits'" },
            { OrganicActionKind::CHEAT_MANA, "CHEAT_MANA", OrganicActionCategory::Cheat,
              "BotCheatMask bit 'mana' via AiPlayerbot.BotCheats/RndBotCheats", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'All 13 configured cheat bits'" },
            { OrganicActionKind::CHEAT_POWER, "CHEAT_POWER", OrganicActionCategory::Cheat,
              "BotCheatMask bit 'power' via AiPlayerbot.BotCheats/RndBotCheats", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'All 13 configured cheat bits'" },
            { OrganicActionKind::CHEAT_ITEM, "CHEAT_ITEM", OrganicActionCategory::Cheat,
              "BotCheatMask bit 'item' via AiPlayerbot.BotCheats/RndBotCheats", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'All 13 configured cheat bits'" },
            { OrganicActionKind::CHEAT_COOLDOWN, "CHEAT_COOLDOWN", OrganicActionCategory::Cheat,
              "BotCheatMask bit 'cooldown' via AiPlayerbot.BotCheats/RndBotCheats", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'All 13 configured cheat bits'" },
            { OrganicActionKind::CHEAT_REPAIR, "CHEAT_REPAIR", OrganicActionCategory::Cheat,
              "BotCheatMask bit 'repair' via AiPlayerbot.BotCheats/RndBotCheats", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'All 13 configured cheat bits'" },
            { OrganicActionKind::CHEAT_MOVESPEED, "CHEAT_MOVESPEED", OrganicActionCategory::Cheat,
              "BotCheatMask bit 'movespeed' via AiPlayerbot.BotCheats/RndBotCheats", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'All 13 configured cheat bits'" },
            { OrganicActionKind::CHEAT_ATTACKSPEED, "CHEAT_ATTACKSPEED", OrganicActionCategory::Cheat,
              "BotCheatMask bit 'attackspeed' via AiPlayerbot.BotCheats/RndBotCheats", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'All 13 configured cheat bits'" },
            { OrganicActionKind::CHEAT_BREATH, "CHEAT_BREATH", OrganicActionCategory::Cheat,
              "BotCheatMask bit 'breath' via AiPlayerbot.BotCheats/RndBotCheats", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'All 13 configured cheat bits'" },
            { OrganicActionKind::CHEAT_GLYPH, "CHEAT_GLYPH", OrganicActionCategory::Cheat,
              "BotCheatMask bit 'glyph' via AiPlayerbot.BotCheats/RndBotCheats (WotLK)", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'All 13 configured cheat bits'" },
            { OrganicActionKind::CHEAT_QUEST, "CHEAT_QUEST", OrganicActionCategory::Cheat,
              "BotCheatMask bit 'quest' via AiPlayerbot.BotCheats/RndBotCheats", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'All 13 configured cheat bits'" },
            { OrganicActionKind::CHEAT_RUNTIME_OVERRIDE, "CHEAT_RUNTIME_OVERRIDE", OrganicActionCategory::Cheat,
              "PlayerbotAI::SetCheat / cheat chat command", OrganicClassification::FixtureOnly,
              nullptr, false, true, "0002A 'Per-bot runtime cheat override'" },

            { OrganicActionKind::RANDOM_TELEPORT, "RANDOM_TELEPORT", OrganicActionCategory::Relocation,
              "RandomPlayerbotMgr 'teleport' timer random relocation", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Random teleport'" },
            { OrganicActionKind::TELEPORT_NEAR_PLAYER, "TELEPORT_NEAR_PLAYER", OrganicActionCategory::Relocation,
              "AiPlayerbot.RandomBotTeleportNearPlayer", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Teleport near player'" },
            { OrganicActionKind::RPG_CAMP_TELEPORT, "RPG_CAMP_TELEPORT", OrganicActionCategory::Relocation,
              "random RPG camp/grind teleport selection", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'RPG camp/grind teleport'" },
            { OrganicActionKind::BG_REGROUP_TELEPORT, "BG_REGROUP_TELEPORT", OrganicActionCategory::Relocation,
              "BattleGroundJoinAction: group-member teleport to leader on BG/arena join", OrganicClassification::Deny,
              nullptr, false, false, "0002A A.2 Phase 0 source-audit row (core BG entry rules remain gameplay)" },
            { OrganicActionKind::MINIMAL_MOVE_TELEPORT, "MINIMAL_MOVE_TELEPORT", OrganicActionCategory::Relocation,
              "AiPlayerbot.EnableMinimalMove: MovementActions direct TeleportTo along the path when unobserved", OrganicClassification::Deny,
              nullptr, false, false, "0002A A.2 Phase 0 source-audit row (normal movement only for managed bots)" },
            { OrganicActionKind::FREE_SUMMON, "FREE_SUMMON", OrganicActionCategory::Relocation,
              "AiPlayerbot.NonGmFreeSummon / SummonAtInnkeepersEnabled without a normal source", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Free/innkeeper summon'" },
            { OrganicActionKind::DIRECT_SUMMON_TELEPORT, "DIRECT_SUMMON_TELEPORT", OrganicActionCategory::Relocation,
              "SummonAction/UseMeetingStoneAction helper: direct ResurrectPlayer(1.0) + TeleportTo without core summon rules", OrganicClassification::Deny,
              nullptr, false, false, "0002A A.2 Phase 0 source-audit row (core-rule summons remain gameplay)" },
            { OrganicActionKind::RANDOM_MANAGER_REVIVE, "RANDOM_MANAGER_REVIVE", OrganicActionCategory::Recovery,
              "RandomPlayerbotMgr revive timer / Refresh direct resurrection", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Automatic manager revive'" },

            { OrganicActionKind::LEGACY_TRANSPORT_SHORTCUT, "LEGACY_TRANSPORT_SHORTCUT", OrganicActionCategory::Transport,
              "AiPlayerbot.TransportTeleportType modes 1/2 teleport-past-transport", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Legacy transport mode 1/2'" },
            { OrganicActionKind::PUBLIC_TRANSPORT_TRANSFER, "PUBLIC_TRANSPORT_TRANSFER", OrganicActionCategory::Transport,
              "proposed Living transport service (canonical route registry)", OrganicClassification::RequireAudit,
              "0002C C.4 route-registry arrival-region reconciler", true, false,
              "0002A 'Canonical public transport transfer'; 0002C C.3-C.4" },
            { OrganicActionKind::TRANSPORT_GROUP_SYNC, "TRANSPORT_GROUP_SYNC", OrganicActionCategory::Transport,
              "proposed group/transport service near-transport master sync", OrganicClassification::RequireAudit,
              "0002C C.5 transport-relative postcondition reconciler", true, false,
              "0002A 'Protected transport group sync'; 0002C C.5" },

            { OrganicActionKind::STUCK_EMERGENCY_TELEPORT, "STUCK_EMERGENCY_TELEPORT", OrganicActionCategory::Recovery,
              "proposed recovery service, last rung of the 0002C C.6 ladder", OrganicClassification::RequireAudit,
              "0002C C.7 safe-node destination reconciler", true, false,
              "0002A 'Stuck emergency relocation'; 0002C C.6-C.7" },
            { OrganicActionKind::GAMEPLAY_DEATH_RECOVERY, "GAMEPLAY_DEATH_RECOVERY", OrganicActionCategory::Recovery,
              "corpse release / spirit healer / resurrection via core rules", OrganicClassification::AllowGameplay,
              nullptr, true, false, "0002A 'Corpse release/spirit healer/resurrection spell'" },
            { OrganicActionKind::INSTANT_REPOP_RELOCATE, "INSTANT_REPOP_RELOCATE", OrganicActionCategory::Recovery,
              "RepopAction (ReleaseSpiritAction.h): ResurrectPlayer(1.0) + teleport to spawn/homebind", OrganicClassification::Deny,
              nullptr, false, false, "0002A A.2 Phase 0 source-audit row (normal death recovery remains gameplay)" },

            { OrganicActionKind::GAMEPLAY_LOOT, "GAMEPLAY_LOOT", OrganicActionCategory::Gameplay,
              "core loot handlers", OrganicClassification::AllowGameplay,
              nullptr, true, false, "0002A 'Normal quest reward/loot/equip'" },
            { OrganicActionKind::GAMEPLAY_QUEST_REWARD, "GAMEPLAY_QUEST_REWARD", OrganicActionCategory::Gameplay,
              "core quest reward handlers (TalkToQuestGiverAction)", OrganicClassification::AllowGameplay,
              nullptr, true, false, "0002A 'Normal quest reward/loot/equip'" },
            { OrganicActionKind::GAMEPLAY_EQUIP, "GAMEPLAY_EQUIP", OrganicActionCategory::Gameplay,
              "core equip handlers", OrganicClassification::AllowGameplay,
              nullptr, true, false, "0002A 'Normal quest reward/loot/equip'" },
            { OrganicActionKind::GAMEPLAY_TAXI_FLIGHT, "GAMEPLAY_TAXI_FLIGHT", OrganicActionCategory::Gameplay,
              "core taxi eligibility and handlers with real cost", OrganicClassification::AllowGameplay,
              nullptr, true, false, "0002A 'Taxi/hearth/portal/area trigger'" },
            { OrganicActionKind::GAMEPLAY_HEARTHSTONE, "GAMEPLAY_HEARTHSTONE", OrganicActionCategory::Gameplay,
              "hearthstone cast via core rules", OrganicClassification::AllowGameplay,
              nullptr, true, false, "0002A 'Taxi/hearth/portal/area trigger'" },
            { OrganicActionKind::GAMEPLAY_ELIGIBLE_PORTAL, "GAMEPLAY_ELIGIBLE_PORTAL", OrganicActionCategory::Gameplay,
              "eligible portal / area trigger via core rules", OrganicClassification::AllowGameplay,
              nullptr, true, false, "0002A 'Taxi/hearth/portal/area trigger'" },
            { OrganicActionKind::GAMEPLAY_ELIGIBLE_SUMMON, "GAMEPLAY_ELIGIBLE_SUMMON", OrganicActionCategory::Gameplay,
              "eligible player/spell summon via core rules", OrganicClassification::AllowGameplay,
              nullptr, true, false, "0002A 'Eligible player/spell summon'" },

            { OrganicActionKind::TRAINER_PURCHASE, "TRAINER_PURCHASE", OrganicActionCategory::Economy,
              "TrainerAction purchase with real cost", OrganicClassification::AllowAutomation,
              nullptr, true, false, "0002A 'Vendor/repair/train'" },
            { OrganicActionKind::VENDOR_REPAIR_TRANSACTION, "VENDOR_REPAIR_TRANSACTION", OrganicActionCategory::Economy,
              "BuyAction / SellAction / RepairAllAction with real cost", OrganicClassification::AllowAutomation,
              nullptr, true, false, "0002A 'Vendor/repair/train'" },
            { OrganicActionKind::AUCTION_TRANSACTION, "AUCTION_TRANSACTION", OrganicActionCategory::Economy,
              "AhAction via normal session handlers", OrganicClassification::AllowAutomation,
              nullptr, true, false, "0002A 'AH/mail/trade'" },
            { OrganicActionKind::MAIL_TRANSACTION, "MAIL_TRANSACTION", OrganicActionCategory::Economy,
              "mail through the core mail session handlers only (send/take money/take item)", OrganicClassification::AllowAutomation,
              nullptr, true, false, "0002A 'AH/mail/trade' (direct-mutation mail path is LEGACY_DIRECT_MAIL_MUTATION)" },
            { OrganicActionKind::LEGACY_DIRECT_MAIL_MUTATION, "LEGACY_DIRECT_MAIL_MUTATION", OrganicActionCategory::Economy,
              "CheckMailAction: unconditional DELETE FROM mail/mail_items after partial ProcessMail; SendMailAction direct state changes", OrganicClassification::Deny,
              nullptr, false, false, "0002A A.2 Phase 0 source-audit row (can discard money/attachments; core handlers only)" },
            { OrganicActionKind::TRADE_TRANSACTION, "TRADE_TRANSACTION", OrganicActionCategory::Economy,
              "trade actions via normal session handlers", OrganicClassification::AllowAutomation,
              nullptr, true, false, "0002A 'AH/mail/trade'" },

            { OrganicActionKind::LEGACY_LOGIN_ROTATION, "LEGACY_LOGIN_ROTATION", OrganicActionCategory::Lifecycle,
              "RandomPlayerbotMgr::AddRandomBots / legacy ProcessBot selection", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Legacy login path'; LR-010" },
            { OrganicActionKind::LEGACY_TIMED_ROTATION, "LEGACY_TIMED_ROTATION", OrganicActionCategory::Lifecycle,
              "ai_playerbot_random_bots add/logout events, RandomBotTimedLogout/Offline", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Legacy timed online/offline state'; LR-010" },
            { OrganicActionKind::ACCOUNT_TRANSFER_HIRE, "ACCOUNT_TRANSFER_HIRE", OrganicActionCategory::Lifecycle,
              "HireAction: SetMoney + direct characters.account UPDATE", OrganicClassification::Deny,
              nullptr, false, false, "0002A A.2 Phase 0 source-audit row (identity/account moves need a managed operation)" },
            { OrganicActionKind::POPULATION_RESET_RECREATE, "POPULATION_RESET_RECREATE", OrganicActionCategory::Lifecycle,
              "population delete/reset/recreate paths (DeleteRandomBotAccounts and console flows)", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Delete/reset population'; 0003 section 7 managed operation" },
            { OrganicActionKind::RAW_POPULATION_SQL_RESET, "RAW_POPULATION_SQL_RESET", OrganicActionCategory::Lifecycle,
              "sql/other/delete_randombots.sql and reset_randombots.sql", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Raw delete/reset SQL'" },
            { OrganicActionKind::OFFLINE_PROGRESSION, "OFFLINE_PROGRESSION", OrganicActionCategory::Lifecycle,
              "future offline planners (prohibited in Organic Realm)", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Offline material progression'" },

            { OrganicActionKind::BROAD_MAINTENANCE_COMMAND, "BROAD_MAINTENANCE_COMMAND", OrganicActionCategory::Maintenance,
              "broad init/refresh/upgrade/maintenance console and chat commands", OrganicClassification::FixtureOnly,
              nullptr, false, true, "0002A 'Broad init/upgrade/refresh/maintenance'" },
            { OrganicActionKind::ADMIN_BYPASS_MUTATION, "ADMIN_BYPASS_MUTATION", OrganicActionCategory::Maintenance,
              "future authenticated admin progression bypass", OrganicClassification::Deny,
              nullptr, false, false, "0002A 'Explicit bounded admin mutation' (no approved reconciler in 0.1)" },

            { OrganicActionKind::FIXTURE_PROVISION, "FIXTURE_PROVISION", OrganicActionCategory::Fixture,
              "fixture-bot deterministic provisioning under BUILD_PLAYERBOTS_LIVING_TESTS", OrganicClassification::FixtureOnly,
              nullptr, false, true, "0006 section 7; 0001A A.4 fixture boundary" },
        } };

        // Compile-time completeness: adding an OrganicActionKind without a metadata
        // row (or out of order) fails here instead of at runtime.
        constexpr bool MetadataTableIsComplete()
        {
            for (size_t i = 0; i < METADATA.size(); ++i)
            {
                OrganicActionMetadata const& row = METADATA[i];
                if (row.kind != K(i) || row.name == nullptr || row.legacySource == nullptr || row.designReference == nullptr)
                    return false;

                // An audited action names its reconciler; nothing else may carry one.
                bool const audited = row.classification == OrganicClassification::RequireAudit;
                if (audited != (row.auditReconciler != nullptr))
                    return false;

                // Production eligibility and fixture-only status follow classification.
                bool const eligible = row.classification == OrganicClassification::AllowGameplay
                    || row.classification == OrganicClassification::AllowAutomation
                    || row.classification == OrganicClassification::BootstrapOnly
                    || row.classification == OrganicClassification::RequireAudit;
                if (row.productionEligible != eligible)
                    return false;
                if (row.fixtureOnly != (row.classification == OrganicClassification::FixtureOnly))
                    return false;
            }

            return true;
        }

        static_assert(MetadataTableIsComplete(),
            "every OrganicActionKind needs one consistent metadata row, in enum order");
    }

    OrganicActionMetadataTable const& AllOrganicActionMetadata()
    {
        return METADATA;
    }

    OrganicActionMetadata const* TryGetOrganicActionMetadata(OrganicActionKind kind)
    {
        size_t const index = static_cast<size_t>(kind);
        if (index >= METADATA.size())
            return nullptr;

        return &METADATA[index];
    }

    OrganicActionMetadata const* FindOrganicActionByName(char const* name)
    {
        if (!name)
            return nullptr;

        for (OrganicActionMetadata const& row : METADATA)
            if (std::strcmp(row.name, name) == 0)
                return &row;

        return nullptr;
    }

    char const* ToString(OrganicClassification value)
    {
        switch (value)
        {
            case OrganicClassification::AllowGameplay: return "AllowGameplay";
            case OrganicClassification::AllowAutomation: return "AllowAutomation";
            case OrganicClassification::BootstrapOnly: return "BootstrapOnly";
            case OrganicClassification::Deny: return "Deny";
            case OrganicClassification::RequireAudit: return "RequireAudit";
            case OrganicClassification::FixtureOnly: return "FixtureOnly";
        }

        return "INVALID_CLASSIFICATION";
    }

    char const* ToString(OrganicActionCategory value)
    {
        switch (value)
        {
            case OrganicActionCategory::Bootstrap: return "Bootstrap";
            case OrganicActionCategory::Randomization: return "Randomization";
            case OrganicActionCategory::Progression: return "Progression";
            case OrganicActionCategory::Gear: return "Gear";
            case OrganicActionCategory::Wealth: return "Wealth";
            case OrganicActionCategory::Inventory: return "Inventory";
            case OrganicActionCategory::Learning: return "Learning";
            case OrganicActionCategory::Quests: return "Quests";
            case OrganicActionCategory::CharacterInit: return "CharacterInit";
            case OrganicActionCategory::Social: return "Social";
            case OrganicActionCategory::Cheat: return "Cheat";
            case OrganicActionCategory::Relocation: return "Relocation";
            case OrganicActionCategory::Transport: return "Transport";
            case OrganicActionCategory::Recovery: return "Recovery";
            case OrganicActionCategory::Lifecycle: return "Lifecycle";
            case OrganicActionCategory::Maintenance: return "Maintenance";
            case OrganicActionCategory::Gameplay: return "Gameplay";
            case OrganicActionCategory::Economy: return "Economy";
            case OrganicActionCategory::Fixture: return "Fixture";
        }

        return "INVALID_CATEGORY";
    }
}
