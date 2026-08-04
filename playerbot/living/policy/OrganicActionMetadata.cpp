#include "OrganicActionMetadata.h"

#include <cstring>

namespace living
{
    namespace
    {
        // One row per OrganicActionKind, in enum order. Classifications implement
        // the normative compatibility matrix (0002A); source-audit rows cite the
        // legacy module path they inventory.
        constexpr OrganicActionMetadataTable METADATA = { {
            { OrganicActionKind::CORE_CHARACTER_CREATE, "CORE_CHARACTER_CREATE", OrganicActionCategory::Bootstrap,
              "Player::Create via RandomPlayerbotFactory::CreateRandomBots", OrganicClassification::BootstrapOnly,
              nullptr, "0002A 'Core character creation'" },
            { OrganicActionKind::COMMAND_CHARACTER_PROVISION, "COMMAND_CHARACTER_PROVISION", OrganicActionCategory::Bootstrap,
              "PlayerbotHolder::CreateBot via HandleCreate: creates and then grants level/spells/state before the first save, an indivisible create-plus-grants compound", OrganicClassification::Deny,
              nullptr, "0002A A.2 source-audit row" },

            { OrganicActionKind::RANDOMIZE_INSTANT, "RANDOMIZE_INSTANT", OrganicActionCategory::Randomization,
              "AiPlayerbot.InstantRandomize / RandomPlayerbotMgr::RandomizeFirst", OrganicClassification::Deny,
              nullptr, "0002A 'Instant/full/incremental randomization'" },
            { OrganicActionKind::RANDOMIZE_FULL, "RANDOMIZE_FULL", OrganicActionCategory::Randomization,
              "RandomPlayerbotMgr::Randomize / PlayerbotFactory::Randomize full pass", OrganicClassification::Deny,
              nullptr, "0002A 'Instant/full/incremental randomization'" },
            { OrganicActionKind::RANDOMIZE_INCREMENTAL, "RANDOMIZE_INCREMENTAL", OrganicActionCategory::Randomization,
              "PlayerbotFactory::Randomize incremental pass on the periodic randomize timer", OrganicClassification::Deny,
              nullptr, "0002A 'Instant/full/incremental randomization'" },
            { OrganicActionKind::RANDOMIZE_HOTFIX, "RANDOMIZE_HOTFIX", OrganicActionCategory::Randomization,
              "RandomPlayerbotMgr::Hotfix version migration", OrganicClassification::Deny,
              nullptr, "0002A 'Randomizer hotfix migration'" },

            { OrganicActionKind::LEVEL_ASSIGN, "LEVEL_ASSIGN", OrganicActionCategory::Progression,
              "Player::SetLevel/GiveLevel via factory or login random level", OrganicClassification::Deny,
              nullptr, "0002A 'Level/XP assignment'" },
            { OrganicActionKind::XP_ASSIGN, "XP_ASSIGN", OrganicActionCategory::Progression,
              "factory/login direct XP assignment", OrganicClassification::Deny,
              nullptr, "0002A 'Level/XP assignment'" },
            { OrganicActionKind::XP_MULTIPLIER, "XP_MULTIPLIER", OrganicActionCategory::Progression,
              "AiPlayerbot.XPRate scaled XP-gain action", OrganicClassification::Deny,
              nullptr, "0002A 'XP multiplier' (realm-normal 1.0 only)" },
            { OrganicActionKind::LEVEL_SYNC, "LEVEL_SYNC", OrganicActionCategory::Progression,
              "AiPlayerbot.SyncLevelWithPlayers / SyncAltLevelToMaster", OrganicClassification::Deny,
              nullptr, "0002A 'Player-level synchronization'" },

            { OrganicActionKind::GEAR_INIT, "GEAR_INIT", OrganicActionCategory::Gear,
              "PlayerbotFactory::InitEquipment family", OrganicClassification::Deny,
              nullptr, "0002A 'Random gear initialization'" },
            { OrganicActionKind::GEAR_UPGRADE, "GEAR_UPGRADE", OrganicActionCategory::Gear,
              "periodic gear upgrade / autogear-style commands and manager refresh", OrganicClassification::Deny,
              nullptr, "0002A 'Periodic gear upgrade/autogear'" },
            { OrganicActionKind::SYNTHETIC_ENCHANT_INIT, "SYNTHETIC_ENCHANT_INIT", OrganicActionCategory::Gear,
              "PlayerbotFactory::EnchantItem / AddGems / ApplyEnchantTemplate / InitGems", OrganicClassification::Deny,
              nullptr, "0002A 'Enchant/gem/template generation'" },
            { OrganicActionKind::AUTO_ENCHANT_ON_UPGRADE, "AUTO_ENCHANT_ON_UPGRADE", OrganicActionCategory::Gear,
              "AiPlayerbot.AutoEnchantUpgradeLoot equip/upgrade hooks", OrganicClassification::Deny,
              nullptr, "0002A 'Auto-enchant on upgrade'" },

            { OrganicActionKind::MONEY_INIT, "MONEY_INIT", OrganicActionCategory::Wealth,
              "PlayerbotFactory starting-money initialization", OrganicClassification::Deny,
              nullptr, "0002A 'Starting/periodic money'" },
            { OrganicActionKind::MONEY_PERIODIC, "MONEY_PERIODIC", OrganicActionCategory::Wealth,
              "RandomPlayerbotMgr::Refresh money grant and related periodic refresh paths", OrganicClassification::Deny,
              nullptr, "0002A 'Starting/periodic money'" },
            { OrganicActionKind::TEMP_MONEY_TRICK, "TEMP_MONEY_TRICK", OrganicActionCategory::Wealth,
              "temporary-SetMoney helpers (RPG/world-buff flight fare, gold-cheat fare/sell, buy/repair helpers) that grant, spend, and restore balance", OrganicClassification::Deny,
              nullptr, "0002A 'Temporary-money flight/purchase tricks'" },
            { OrganicActionKind::QUEST_MONEY_FABRICATION, "QUEST_MONEY_FABRICATION", OrganicActionCategory::Wealth,
              "QuestAction::CompleteQuest required-money grant", OrganicClassification::Deny,
              nullptr, "0002A 'Required-money fabrication during quest completion'" },

            { OrganicActionKind::BAGS_INVENTORY_INIT, "BAGS_INVENTORY_INIT", OrganicActionCategory::Inventory,
              "PlayerbotFactory::InitBags / InitInventory family", OrganicClassification::Deny,
              nullptr, "0002A 'Bags and generated inventory'" },
            { OrganicActionKind::CONSUMABLES_INIT, "CONSUMABLES_INIT", OrganicActionCategory::Inventory,
              "PlayerbotFactory food/reagent/potion init and refresh paths", OrganicClassification::Deny,
              nullptr, "0002A 'Food/ammo/reagents/potions/consumables'" },
            { OrganicActionKind::AMMO_REPLENISH, "AMMO_REPLENISH", OrganicActionCategory::Inventory,
              "PlayerbotFactory::InitAmmo / PlayerbotAI runtime ammo stack refill", OrganicClassification::Deny,
              nullptr, "0002A 'Runtime ammo replenishment'" },
            { OrganicActionKind::DIRECT_ITEM_SPLIT_TRANSFER, "DIRECT_ITEM_SPLIT_TRANSFER", OrganicActionCategory::Inventory,
              "GuildShareItemAction: Item::CreateItem split-stack construction outside trade/mail handlers", OrganicClassification::Deny,
              nullptr, "0002A A.2 source-audit row (normal trade/mail allowed instead)" },
            { OrganicActionKind::DIRECT_ITEM_OWNERSHIP_TRANSFER, "DIRECT_ITEM_OWNERSHIP_TRANSFER", OrganicActionCategory::Inventory,
              "GiveItemAction / GuildShareItemAction: MoveItemFromInventory + SetOwnerGuid outside trade handlers", OrganicClassification::Deny,
              nullptr, "0002A A.2 source-audit row (normal trade/mail allowed instead)" },
            { OrganicActionKind::DIRECT_ITEM_DESTRUCTION, "DIRECT_ITEM_DESTRUCTION", OrganicActionCategory::Inventory,
              "DestroyItemAction / SmartDestroyItemAction direct discard from maintenance and bag-pressure paths", OrganicClassification::Deny,
              nullptr, "0002A A.2 source-audit row (core destroy-item handler for any allowed automation)" },
            { OrganicActionKind::LOGIN_ITEM_FABRICATION, "LOGIN_ITEM_FABRICATION", OrganicActionCategory::Inventory,
              "PlayerbotHolder::OnBotLogin StoreNewItemInBestSlots(6948/40582) when hearthstone/death gate is missing", OrganicClassification::Deny,
              nullptr, "0002A A.2 source-audit row (starter items only at managed bootstrap)" },

            { OrganicActionKind::SKILLS_INIT, "SKILLS_INIT", OrganicActionCategory::Learning,
              "PlayerbotFactory::InitSkills / InitAllSkills / UpdateTradeSkills", OrganicClassification::Deny,
              nullptr, "0002A 'Skills/professions'" },
            { OrganicActionKind::SPELLS_INIT, "SPELLS_INIT", OrganicActionCategory::Learning,
              "PlayerbotFactory::InitAvailableSpells / InitSpecialSpells", OrganicClassification::Deny,
              nullptr, "0002A 'Spell initialization'" },
            { OrganicActionKind::FREE_TRAINER_MODE, "FREE_TRAINER_MODE", OrganicActionCategory::Learning,
              "AiPlayerbot.AutoTrainSpells=free trainer learning without cost", OrganicClassification::Deny,
              nullptr, "0002A 'Free trainer mode' (normal-cost trainer automation stays allowed)" },
            { OrganicActionKind::AUTO_LEARN_TRAINER_SPELLS, "AUTO_LEARN_TRAINER_SPELLS", OrganicActionCategory::Learning,
              "AiPlayerbot.AutoLearnTrainerSpells", OrganicClassification::Deny,
              nullptr, "0002A 'Auto trainer/quest/drop learning'" },
            { OrganicActionKind::AUTO_LEARN_QUEST_SPELLS, "AUTO_LEARN_QUEST_SPELLS", OrganicActionCategory::Learning,
              "AiPlayerbot.AutoLearnQuestSpells", OrganicClassification::Deny,
              nullptr, "0002A 'Auto trainer/quest/drop learning'" },
            { OrganicActionKind::AUTO_LEARN_DROPPED_SPELLS, "AUTO_LEARN_DROPPED_SPELLS", OrganicActionCategory::Learning,
              "AiPlayerbot.AutoLearnDroppedSpells", OrganicClassification::Deny,
              nullptr, "0002A 'Auto trainer/quest/drop learning'" },
            { OrganicActionKind::AUTO_LEARN_ITEM_FABRICATION, "AUTO_LEARN_ITEM_FABRICATION", OrganicActionCategory::Learning,
              "AutoLearnSpellAction quest reward-item creation/mail", OrganicClassification::Deny,
              nullptr, "0002A 'Auto-learn reward-item fabrication'" },
            { OrganicActionKind::RTSC_SPELL_GRANT, "RTSC_SPELL_GRANT", OrganicActionCategory::Learning,
              "RtscAction: learnSpell/removeSpell(RTSC_MOVE_SPELL) on the requesting real player", OrganicClassification::Deny,
              nullptr, "0002A A.2 source-audit row (mutates a real player's spell book)" },

            { OrganicActionKind::TALENT_SPEND_EARNED, "TALENT_SPEND_EARNED", OrganicActionCategory::Progression,
              "auto-talent action spending earned talent points", OrganicClassification::AllowAutomation,
              nullptr, "0002A 'Earned talent spending'" },
            { OrganicActionKind::TALENT_INIT_SYNTHETIC, "TALENT_INIT_SYNTHETIC", OrganicActionCategory::Progression,
              "PlayerbotFactory::InitTalentsTree / InitTalents synthetic spec assignment", OrganicClassification::Deny,
              nullptr, "0002A A.2 source-audit row (factory talent branch of the randomization family)" },
            { OrganicActionKind::FREE_TALENT_RESPEC, "FREE_TALENT_RESPEC", OrganicActionCategory::Progression,
              "ChangeTalentsAction / TalentSpec::ApplyTalents respec without cost or trainer", OrganicClassification::Deny,
              nullptr, "0002A A.2 source-audit row (core respec rules only)" },

            { OrganicActionKind::PREQUEST_INIT, "PREQUEST_INIT", OrganicActionCategory::Quests,
              "AiPlayerbot.PreQuests bootstrap quest completion", OrganicClassification::Deny,
              nullptr, "0002A 'Prequests/global bot quest rewards'" },
            { OrganicActionKind::CONFIGURED_QUEST_REWARD, "CONFIGURED_QUEST_REWARD", OrganicActionCategory::Quests,
              "AiPlayerbot.RandomBotQuestIds configured quest rewarding", OrganicClassification::Deny,
              nullptr, "0002A 'Prequests/global bot quest rewards'" },
            { OrganicActionKind::QUEST_COMPLETE_GENERIC, "QUEST_COMPLETE_GENERIC", OrganicActionCategory::Quests,
              "QuestAction::CompleteQuest / autocomplete actions", OrganicClassification::Deny,
              nullptr, "0002A 'Generic quest completion commands'" },
            { OrganicActionKind::QUEST_SYNC_TO_BOT, "QUEST_SYNC_TO_BOT", OrganicActionCategory::Quests,
              "AiPlayerbot.SyncQuestWithPlayer quest-giver sync from player state", OrganicClassification::Deny,
              nullptr, "0002A 'Quest sync to bot'" },
            { OrganicActionKind::QUEST_SYNC_TO_PLAYER, "QUEST_SYNC_TO_PLAYER", OrganicActionCategory::Quests,
              "AiPlayerbot.SyncQuestForPlayer real-player quest-log mutation", OrganicClassification::Deny,
              nullptr, "0002A 'Quest sync to real player'" },
            { OrganicActionKind::GAMEPLAY_QUEST_ACCEPT, "GAMEPLAY_QUEST_ACCEPT", OrganicActionCategory::Quests,
              "core-eligible AddQuest through normal quest-giver/shared/item handlers", OrganicClassification::AllowGameplay,
              nullptr, "0002A 'Normal quest reward/loot/equip' family" },
            { OrganicActionKind::REMOTE_QUEST_ACCEPT, "REMOTE_QUEST_ACCEPT", OrganicActionCategory::Quests,
              "TravelAction hardcoded Dark Portal AddQuest(10119/9407) without quest-giver interaction", OrganicClassification::Deny,
              nullptr, "0002A A.2 source-audit row (quests accepted through normal handlers only)" },
            { OrganicActionKind::DIRECT_QUEST_ABANDON, "DIRECT_QUEST_ABANDON", OrganicActionCategory::Quests,
              "PlayerbotAI::DropQuest callers (DropQuestAction, quest-log pruning, failed-timer cleanup)", OrganicClassification::Deny,
              nullptr, "0002A A.2 source-audit row (core abandon semantics only)" },

            { OrganicActionKind::WORLD_BUFF_APPLY, "WORLD_BUFF_APPLY", OrganicActionCategory::CharacterInit,
              "AiPlayerbot.WorldBuff* / WorldBuffAction direct aura application", OrganicClassification::Deny,
              nullptr, "0002A 'World buffs'" },
            { OrganicActionKind::SHARED_RESPAWN_ACCELERATION, "SHARED_RESPAWN_ACCELERATION", OrganicActionCategory::CharacterInit,
              "PlayerbotAI::AccelerateRespawn (kill-credit and loot-release callers): shortens shared creature respawns", OrganicClassification::Deny,
              nullptr, "0002A A.2 source-audit row (shared-world spawn state; affects real players)" },
            { OrganicActionKind::DIRECT_AURA_REMOVAL, "DIRECT_AURA_REMOVAL", OrganicActionCategory::CharacterInit,
              "RemoveAuraAction and world-buff strategy removal: direct RemoveAurasDueToSpell outside core cancellation rules", OrganicClassification::Deny,
              nullptr, "0002A A.2 source-audit row (core-validated cancellation only)" },
            { OrganicActionKind::ENCOUNTER_AURA_MUTATION, "ENCOUNTER_AURA_MUTATION", OrganicActionCategory::CharacterInit,
              "KarazhanDungeonActions: RemoveAurasDueToSpell on boss beams and AddAura on the bot", OrganicClassification::Deny,
              nullptr, "0002A A.2 source-audit row (encounter mechanics through core rules only)" },
            { OrganicActionKind::DIRECT_GLYPH_MUTATION, "DIRECT_GLYPH_MUTATION", OrganicActionCategory::CharacterInit,
              "GlyphAction direct glyph-slot application outside the core glyph handler (WotLK)", OrganicClassification::Deny,
              nullptr, "0002A A.2 source-audit row (core glyph handler only)" },
            { OrganicActionKind::REPUTATION_INIT, "REPUTATION_INIT", OrganicActionCategory::CharacterInit,
              "PlayerbotFactory::InitReputations", OrganicClassification::Deny,
              nullptr, "0002A 'Reputation initialization'" },
            { OrganicActionKind::TAXI_NODES_INIT, "TAXI_NODES_INIT", OrganicActionCategory::CharacterInit,
              "PlayerbotFactory::InitTaxiNodes", OrganicClassification::Deny,
              nullptr, "0002A 'Taxi-node initialization' (nodes learned normally instead)" },
            { OrganicActionKind::MOUNT_INIT, "MOUNT_INIT", OrganicActionCategory::CharacterInit,
              "PlayerbotFactory::InitMounts", OrganicClassification::Deny,
              nullptr, "0002A 'Mount initialization' (normal purchase/quest/trainer instead)" },
            { OrganicActionKind::PET_INIT, "PET_INIT", OrganicActionCategory::CharacterInit,
              "PlayerbotFactory::InitPet / InitPetSpells", OrganicClassification::Deny,
              nullptr, "0002A 'Pet initialization' (normal class/tame/summon path instead)" },
            { OrganicActionKind::FREE_PET_HAPPINESS, "FREE_PET_HAPPINESS", OrganicActionCategory::CharacterInit,
              "FeedPetAction and factory happiness fill: SetPower(POWER_HAPPINESS) without consuming food", OrganicClassification::Deny,
              nullptr, "0002A A.2 source-audit row (normal feed item/spell path only)" },

            { OrganicActionKind::GUILD_BOOTSTRAP, "GUILD_BOOTSTRAP", OrganicActionCategory::Social,
              "RandomPlayerbotFactory::CreateRandomGuilds / PlayerbotFactory::InitGuild", OrganicClassification::Deny,
              nullptr, "0002A 'Guild bootstrap/free tabard' (normal charter workflow allowed)" },
            { OrganicActionKind::ARENA_TEAM_BOOTSTRAP, "ARENA_TEAM_BOOTSTRAP", OrganicActionCategory::Social,
              "RandomPlayerbotFactory::CreateRandomArenaTeams / PlayerbotFactory::InitArenaTeam (TBC/WotLK)", OrganicClassification::Deny,
              nullptr, "0002A 'Arena-team bootstrap'" },
            { OrganicActionKind::GUILD_BANK_TAB_MUTATION, "GUILD_BANK_TAB_MUTATION", OrganicActionCategory::Social,
              "BuyGuildBankTabAction: buys tabs and sets rights without a guild-bank interaction", OrganicClassification::Deny,
              nullptr, "0002A A.2 source-audit row" },

            { OrganicActionKind::CHEAT_TAXI, "CHEAT_TAXI", OrganicActionCategory::Cheat,
              "BotCheatMask bit 'taxi' via AiPlayerbot.BotCheats/RndBotCheats", OrganicClassification::Deny,
              nullptr, "0002A 'All 13 configured cheat bits'" },
            { OrganicActionKind::CHEAT_GOLD, "CHEAT_GOLD", OrganicActionCategory::Cheat,
              "BotCheatMask bit 'gold' via AiPlayerbot.BotCheats/RndBotCheats", OrganicClassification::Deny,
              nullptr, "0002A 'All 13 configured cheat bits'" },
            { OrganicActionKind::CHEAT_HEALTH, "CHEAT_HEALTH", OrganicActionCategory::Cheat,
              "BotCheatMask bit 'health' via AiPlayerbot.BotCheats/RndBotCheats", OrganicClassification::Deny,
              nullptr, "0002A 'All 13 configured cheat bits'" },
            { OrganicActionKind::CHEAT_MANA, "CHEAT_MANA", OrganicActionCategory::Cheat,
              "BotCheatMask bit 'mana' via AiPlayerbot.BotCheats/RndBotCheats", OrganicClassification::Deny,
              nullptr, "0002A 'All 13 configured cheat bits'" },
            { OrganicActionKind::CHEAT_POWER, "CHEAT_POWER", OrganicActionCategory::Cheat,
              "BotCheatMask bit 'power' via AiPlayerbot.BotCheats/RndBotCheats", OrganicClassification::Deny,
              nullptr, "0002A 'All 13 configured cheat bits'" },
            { OrganicActionKind::CHEAT_ITEM, "CHEAT_ITEM", OrganicActionCategory::Cheat,
              "BotCheatMask bit 'item' via AiPlayerbot.BotCheats/RndBotCheats", OrganicClassification::Deny,
              nullptr, "0002A 'All 13 configured cheat bits'" },
            { OrganicActionKind::CHEAT_COOLDOWN, "CHEAT_COOLDOWN", OrganicActionCategory::Cheat,
              "BotCheatMask bit 'cooldown' via AiPlayerbot.BotCheats/RndBotCheats", OrganicClassification::Deny,
              nullptr, "0002A 'All 13 configured cheat bits'" },
            { OrganicActionKind::CHEAT_REPAIR, "CHEAT_REPAIR", OrganicActionCategory::Cheat,
              "BotCheatMask bit 'repair' via AiPlayerbot.BotCheats/RndBotCheats", OrganicClassification::Deny,
              nullptr, "0002A 'All 13 configured cheat bits'" },
            { OrganicActionKind::CHEAT_MOVESPEED, "CHEAT_MOVESPEED", OrganicActionCategory::Cheat,
              "BotCheatMask bit 'movespeed' via AiPlayerbot.BotCheats/RndBotCheats", OrganicClassification::Deny,
              nullptr, "0002A 'All 13 configured cheat bits'" },
            { OrganicActionKind::CHEAT_ATTACKSPEED, "CHEAT_ATTACKSPEED", OrganicActionCategory::Cheat,
              "BotCheatMask bit 'attackspeed' via AiPlayerbot.BotCheats/RndBotCheats", OrganicClassification::Deny,
              nullptr, "0002A 'All 13 configured cheat bits'" },
            { OrganicActionKind::CHEAT_BREATH, "CHEAT_BREATH", OrganicActionCategory::Cheat,
              "BotCheatMask bit 'breath' via AiPlayerbot.BotCheats/RndBotCheats", OrganicClassification::Deny,
              nullptr, "0002A 'All 13 configured cheat bits'" },
            { OrganicActionKind::CHEAT_GLYPH, "CHEAT_GLYPH", OrganicActionCategory::Cheat,
              "BotCheatMask bit 'glyph' via AiPlayerbot.BotCheats/RndBotCheats (WotLK)", OrganicClassification::Deny,
              nullptr, "0002A 'All 13 configured cheat bits'" },
            { OrganicActionKind::CHEAT_QUEST, "CHEAT_QUEST", OrganicActionCategory::Cheat,
              "BotCheatMask bit 'quest' via AiPlayerbot.BotCheats/RndBotCheats", OrganicClassification::Deny,
              nullptr, "0002A 'All 13 configured cheat bits'" },
            { OrganicActionKind::CHEAT_RUNTIME_OVERRIDE, "CHEAT_RUNTIME_OVERRIDE", OrganicActionCategory::Cheat,
              "PlayerbotAI::SetCheat / cheat chat command", OrganicClassification::FixtureOnly,
              nullptr, "0002A 'Per-bot runtime cheat override' (fixture profile only)" },

            { OrganicActionKind::RANDOM_TELEPORT, "RANDOM_TELEPORT", OrganicActionCategory::Relocation,
              "RandomPlayerbotMgr 'teleport' timer random relocation", OrganicClassification::Deny,
              nullptr, "0002A 'Random teleport'" },
            { OrganicActionKind::TELEPORT_NEAR_PLAYER, "TELEPORT_NEAR_PLAYER", OrganicActionCategory::Relocation,
              "teleport-near-player config/manager relocation", OrganicClassification::Deny,
              nullptr, "0002A 'Teleport near player'" },
            { OrganicActionKind::RPG_CAMP_TELEPORT, "RPG_CAMP_TELEPORT", OrganicActionCategory::Relocation,
              "random RPG camp/grind teleport selection", OrganicClassification::Deny,
              nullptr, "0002A 'RPG camp/grind teleport'" },
            { OrganicActionKind::BG_REGROUP_TELEPORT, "BG_REGROUP_TELEPORT", OrganicActionCategory::Relocation,
              "BattleGroundJoinAction: group-member TeleportTo leader on BG/arena join", OrganicClassification::Deny,
              nullptr, "0002A A.2 source-audit row (core BG entry rules remain gameplay)" },
            { OrganicActionKind::MINIMAL_MOVE_TELEPORT, "MINIMAL_MOVE_TELEPORT", OrganicActionCategory::Relocation,
              "AiPlayerbot.EnableMinimalMove: MovementActions direct TeleportTo along the path when unobserved", OrganicClassification::Deny,
              nullptr, "0002A A.2 source-audit row (normal movement only for managed bots)" },
            { OrganicActionKind::UNOBSERVED_MOVE_TELEPORT, "UNOBSERVED_MOVE_TELEPORT", OrganicActionCategory::Relocation,
              "MovementActions activity-throttled TeleportTo paths independent of EnableMinimalMove", OrganicClassification::Deny,
              nullptr, "0002A A.2 source-audit row (normal movement only for managed bots)" },
            { OrganicActionKind::FREE_SUMMON, "FREE_SUMMON", OrganicActionCategory::Relocation,
              "free/innkeeper summon settings without a normal summon source", OrganicClassification::Deny,
              nullptr, "0002A 'Free/innkeeper summon'" },
            { OrganicActionKind::DIRECT_SUMMON_TELEPORT, "DIRECT_SUMMON_TELEPORT", OrganicActionCategory::Relocation,
              "SetSummonPoint/TeleportTo helpers (custom-spell summon, world-buff travel, meeting stone) outside core summon rules", OrganicClassification::Deny,
              nullptr, "0002A A.2 source-audit row (core-rule summons remain gameplay)" },
            { OrganicActionKind::RANDOM_MANAGER_REVIVE, "RANDOM_MANAGER_REVIVE", OrganicActionCategory::Recovery,
              "RandomPlayerbotMgr revive timer / Refresh direct resurrection", OrganicClassification::Deny,
              nullptr, "0002A 'Automatic manager revive'" },

            { OrganicActionKind::LEGACY_TRANSPORT_SHORTCUT, "LEGACY_TRANSPORT_SHORTCUT", OrganicActionCategory::Transport,
              "AiPlayerbot.TransportTeleportType modes 1/2 teleport-past-transport", OrganicClassification::Deny,
              nullptr, "0002A 'Legacy transport mode 1/2' (replaced by canonical modeled transfer)" },
            { OrganicActionKind::PUBLIC_TRANSPORT_TRANSFER, "PUBLIC_TRANSPORT_TRANSFER", OrganicActionCategory::Transport,
              "proposed Living transport service over the canonical route registry", OrganicClassification::RequireAudit,
              "0002C route-registry arrival reconciler", "0002A 'Canonical public transport transfer'; 0002C" },
            { OrganicActionKind::TRANSPORT_GROUP_SYNC, "TRANSPORT_GROUP_SYNC", OrganicActionCategory::Transport,
              "proposed group/transport service near-transport master sync", OrganicClassification::RequireAudit,
              "0002C transport-relative postcondition reconciler", "0002A 'Protected transport group sync'; 0002C" },

            { OrganicActionKind::STUCK_EMERGENCY_TELEPORT, "STUCK_EMERGENCY_TELEPORT", OrganicActionCategory::Recovery,
              "proposed recovery service, last rung of the 0002C recovery ladder", OrganicClassification::RequireAudit,
              "0002C safe-node destination reconciler", "0002A 'Stuck emergency relocation'; 0002C" },
            { OrganicActionKind::GAMEPLAY_DEATH_RECOVERY, "GAMEPLAY_DEATH_RECOVERY", OrganicActionCategory::Recovery,
              "corpse release and resurrection through core handlers", OrganicClassification::AllowGameplay,
              nullptr, "0002A 'Corpse release/spirit healer/resurrection spell'" },
            { OrganicActionKind::INSTANT_REPOP_RELOCATE, "INSTANT_REPOP_RELOCATE", OrganicActionCategory::Recovery,
              "RepopAction (ReleaseSpiritAction.h): ResurrectPlayer(1.0) + relocate to spawn/homebind", OrganicClassification::Deny,
              nullptr, "0002A A.2 source-audit row (normal death recovery remains gameplay)" },
            { OrganicActionKind::DEATH_RECOVERY_TELEPORT, "DEATH_RECOVERY_TELEPORT", OrganicActionCategory::Recovery,
              "ReviveFromCorpseAction: direct TeleportTo to corpse/graveyard when unobserved or delayed", OrganicClassification::Deny,
              nullptr, "0002A A.2 source-audit row (normal corpse run remains gameplay)" },
            { OrganicActionKind::DIRECT_SPIRIT_HEALER_RESURRECTION, "DIRECT_SPIRIT_HEALER_RESURRECTION", OrganicActionCategory::Recovery,
              "SpiritHealerAction: ResurrectPlayer/DurabilityLossAll/SpawnCorpseBones without an interactable healer", OrganicClassification::Deny,
              nullptr, "0002A A.2 source-audit row (core spirit-healer handler only)" },

            { OrganicActionKind::GAMEPLAY_LOOT, "GAMEPLAY_LOOT", OrganicActionCategory::Gameplay,
              "core loot handlers", OrganicClassification::AllowGameplay,
              nullptr, "0002A 'Normal quest reward/loot/equip'" },
            { OrganicActionKind::GAMEPLAY_QUEST_REWARD, "GAMEPLAY_QUEST_REWARD", OrganicActionCategory::Gameplay,
              "core quest reward handlers", OrganicClassification::AllowGameplay,
              nullptr, "0002A 'Normal quest reward/loot/equip'" },
            { OrganicActionKind::GAMEPLAY_EQUIP, "GAMEPLAY_EQUIP", OrganicActionCategory::Gameplay,
              "core equip handlers", OrganicClassification::AllowGameplay,
              nullptr, "0002A 'Normal quest reward/loot/equip'" },
            { OrganicActionKind::GAMEPLAY_TAXI_FLIGHT, "GAMEPLAY_TAXI_FLIGHT", OrganicActionCategory::Gameplay,
              "core taxi eligibility and handlers with real cost", OrganicClassification::AllowGameplay,
              nullptr, "0002A 'Taxi/hearth/portal/area trigger'" },
            { OrganicActionKind::GAMEPLAY_HEARTHSTONE, "GAMEPLAY_HEARTHSTONE", OrganicActionCategory::Gameplay,
              "hearthstone cast via core rules", OrganicClassification::AllowGameplay,
              nullptr, "0002A 'Taxi/hearth/portal/area trigger'" },
            { OrganicActionKind::GAMEPLAY_ELIGIBLE_PORTAL, "GAMEPLAY_ELIGIBLE_PORTAL", OrganicActionCategory::Gameplay,
              "eligible portal / area trigger via core rules", OrganicClassification::AllowGameplay,
              nullptr, "0002A 'Taxi/hearth/portal/area trigger'" },
            { OrganicActionKind::GAMEPLAY_ELIGIBLE_SUMMON, "GAMEPLAY_ELIGIBLE_SUMMON", OrganicActionCategory::Gameplay,
              "eligible player/spell summon via core rules", OrganicClassification::AllowGameplay,
              nullptr, "0002A 'Eligible player/spell summon'" },

            { OrganicActionKind::TRAINER_PURCHASE, "TRAINER_PURCHASE", OrganicActionCategory::Economy,
              "trainer purchase automation with real cost", OrganicClassification::AllowAutomation,
              nullptr, "0002A 'Vendor/repair/train'" },
            { OrganicActionKind::VENDOR_REPAIR_TRANSACTION, "VENDOR_REPAIR_TRANSACTION", OrganicActionCategory::Economy,
              "buy/sell/repair automation with real cost", OrganicClassification::AllowAutomation,
              nullptr, "0002A 'Vendor/repair/train'" },
            { OrganicActionKind::AUCTION_TRANSACTION, "AUCTION_TRANSACTION", OrganicActionCategory::Economy,
              "auctions through core auction session handlers", OrganicClassification::AllowAutomation,
              nullptr, "0002A 'AH/mail/trade'" },
            { OrganicActionKind::DIRECT_AUCTION_MUTATION, "DIRECT_AUCTION_MUTATION", OrganicActionCategory::Economy,
              "GuildShareAhBuyAction: AuctionEntry::UpdateBid called directly, bypassing the auction handler", OrganicClassification::Deny,
              nullptr, "0002A A.2 source-audit row (core auction handlers only)" },
            { OrganicActionKind::MAIL_TRANSACTION, "MAIL_TRANSACTION", OrganicActionCategory::Economy,
              "mail through core mail session handlers", OrganicClassification::AllowAutomation,
              nullptr, "0002A 'AH/mail/trade'" },
            { OrganicActionKind::LEGACY_DIRECT_MAIL_MUTATION, "LEGACY_DIRECT_MAIL_MUTATION", OrganicActionCategory::Economy,
              "CheckMailAction: unconditional DELETE FROM mail/mail_items after partial processing; direct mail state changes", OrganicClassification::Deny,
              nullptr, "0002A A.2 source-audit row (can discard attachments; core handlers only)" },
            { OrganicActionKind::TRADE_TRANSACTION, "TRADE_TRANSACTION", OrganicActionCategory::Economy,
              "trade via normal session handlers", OrganicClassification::AllowAutomation,
              nullptr, "0002A 'AH/mail/trade'" },

            { OrganicActionKind::LEGACY_LOGIN_ROTATION, "LEGACY_LOGIN_ROTATION", OrganicActionCategory::Lifecycle,
              "RandomPlayerbotMgr::AddRandomBots / legacy ProcessBot selection", OrganicClassification::Deny,
              nullptr, "0002A 'Legacy login path'; LR-010" },
            { OrganicActionKind::LEGACY_TIMED_ROTATION, "LEGACY_TIMED_ROTATION", OrganicActionCategory::Lifecycle,
              "ai_playerbot_random_bots add/logout events, RandomBotTimedLogout/Offline", OrganicClassification::Deny,
              nullptr, "0002A 'Legacy timed online/offline state'; LR-010" },
            { OrganicActionKind::ACCOUNT_TRANSFER_HIRE, "ACCOUNT_TRANSFER_HIRE", OrganicActionCategory::Lifecycle,
              "HireAction: SetMoney + direct characters.account UPDATE", OrganicClassification::Deny,
              nullptr, "0002A A.2 source-audit row (identity/account moves need a managed operation)" },
            { OrganicActionKind::POPULATION_RESET_RECREATE, "POPULATION_RESET_RECREATE", OrganicActionCategory::Lifecycle,
              "population delete/reset/recreate paths (DeleteRandomBotAccounts and console flows)", OrganicClassification::Deny,
              nullptr, "0002A 'Delete/reset population' (managed global operation only)" },
            { OrganicActionKind::RAW_POPULATION_SQL_RESET, "RAW_POPULATION_SQL_RESET", OrganicActionCategory::Lifecycle,
              "sql/other delete_randombots.sql / delete_all_randombots.sql / reset_randombots.sql", OrganicClassification::Deny,
              nullptr, "0002A 'Raw delete/reset SQL'" },
            { OrganicActionKind::OFFLINE_PROGRESSION, "OFFLINE_PROGRESSION", OrganicActionCategory::Lifecycle,
              "future offline planners (prohibited in Organic Realm)", OrganicClassification::Deny,
              nullptr, "0002A 'Offline material progression'" },

            { OrganicActionKind::BROAD_MAINTENANCE_COMMAND, "BROAD_MAINTENANCE_COMMAND", OrganicActionCategory::Maintenance,
              "broad init/refresh/upgrade/maintenance console and chat commands", OrganicClassification::FixtureOnly,
              nullptr, "0002A 'Broad init/upgrade/refresh/maintenance' (fixture profile only)" },
            { OrganicActionKind::DEBUG_MUTATION_COMMAND, "DEBUG_MUTATION_COMMAND", OrganicActionCategory::Maintenance,
              "DebugAction mutation subcommands (do/quest/...)", OrganicClassification::FixtureOnly,
              nullptr, "0002A A.2 source-audit row (read-only diagnostics excluded)" },
            { OrganicActionKind::ADMIN_BYPASS_MUTATION, "ADMIN_BYPASS_MUTATION", OrganicActionCategory::Maintenance,
              "future authenticated admin progression bypass", OrganicClassification::Deny,
              nullptr, "0002A 'Explicit bounded admin mutation' (no approved reconciler in 0.1)" },

            { OrganicActionKind::FIXTURE_CHARACTER_CREATE, "FIXTURE_CHARACTER_CREATE", OrganicActionCategory::Fixture,
              "fixture-bot character creation under the test profile", OrganicClassification::FixtureOnly,
              nullptr, "0001A A.4 fixture boundary; invariant 12" },
            { OrganicActionKind::FIXTURE_PROVISION, "FIXTURE_PROVISION", OrganicActionCategory::Fixture,
              "fixture-bot deterministic provisioning under the test profile", OrganicClassification::FixtureOnly,
              nullptr, "0001A A.4 fixture boundary; invariant 12" },
        } };

        // Compile-time completeness: adding an OrganicActionKind without a
        // consistent metadata row (or out of enum order) fails the build.
        // Name uniqueness is deliberately NOT a constexpr check: pairwise
        // constexpr string comparison is quadratic in rows and already costs
        // most of MSVC's default 100k constexpr-step budget at this size; the
        // host suite enforces uniqueness instead (InventoryNameLookupIsExact).
        constexpr bool MetadataTableIsComplete()
        {
            for (size_t i = 0; i < METADATA.size(); ++i)
            {
                OrganicActionMetadata const& row = METADATA[i];
                if (row.kind != static_cast<OrganicActionKind>(i))
                    return false;

                if (row.name == nullptr || row.legacySource == nullptr || row.designReference == nullptr)
                    return false;

                // An audited action names its reconciler; nothing else may carry one.
                bool const audited = row.classification == OrganicClassification::RequireAudit;
                if (audited != (row.auditReconciler != nullptr))
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
        if (name == nullptr)
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
            case OrganicActionCategory::Gameplay: return "Gameplay";
            case OrganicActionCategory::Economy: return "Economy";
            case OrganicActionCategory::Lifecycle: return "Lifecycle";
            case OrganicActionCategory::Maintenance: return "Maintenance";
            case OrganicActionCategory::Fixture: return "Fixture";
        }

        return "INVALID_CATEGORY";
    }
}
