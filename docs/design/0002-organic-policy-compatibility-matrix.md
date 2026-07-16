# Living Realm 0002A: Organic compatibility matrix

[Back to design 0002](0002-organic-policy-and-audit.md)

## A.1 Classification rules

Each production path MUST have an `ActionKind`, classification, guarded
enforcement point, and test. **Denied** means no mutation. **Audit** means the
protocol in 0002B and an action-specific reconciler in 0002C or a future approved
design.

| Mechanism | Current key/path | Legacy effect | Organic policy | Enforcement point | Audit/exception |
|---|---|---|---|---|---|
| Core character creation | `Player::Create` through managed factory | Creates starter character/state | Allowed gameplay only during managed bootstrap | managed creation wrapper | Global bootstrap operation |
| Instant/full/incremental randomization | `InstantRandomize`, `Randomize*`, `PlayerbotFactory::Randomize` | Generates/resets broad state | Denied after core creation | manager/factory entry | None |
| Randomizer hotfix migration | `RandomPlayerbotMgr::Hotfix` | Retroactively rewards quests/sets level | Denied | hotfix dispatch | Future versioned migration only |
| Level/XP assignment | `SetLevel`, `GiveLevel`, factory/login random level | Sets level/XP | Denied | factory/login/AI sync | None |
| Player-level synchronization | `SyncLevelWithPlayers`, `SyncAltLevelToMaster` | Raises/changes level | Denied | login/AI update | None |
| XP multiplier | `AiPlayerbot.XPRate`, XP-gain action | Multiplies earned XP | Realm-normal (`1.0`) only | config + XP action | None |
| Random gear initialization | `InitEquipment`, gear templates | Creates/replaces gear | Denied | factory | None |
| Periodic gear upgrade/autogear | `RandomGear*`, update gear, autogear-style commands | Creates/replaces gear | Denied | manager/actions/commands | None |
| Starting/periodic money | factory full/incremental money, manager refresh | Creates currency | Denied | factory/manager | None |
| Temporary-money flight/purchase tricks | RPG flight, world-buff flight, `taxi`/`gold` cheat fare in `MovementActions`, buy/repair helpers | Grants money, performs action, restores balance; `SetMoney` runs `MoneyChanged` synchronously, so a crossed threshold can permanently auto-reward a money-required quest (and advance WotLK highest-gold achievements) — restoring the balance cannot undo it | Denied | shared temporary-`SetMoney` boundary | None |
| Bags and generated inventory | `InitBags`, `InitInventory` | Creates items | Only core starter inventory at creation | factory/bootstrap | None |
| Runtime ammo replenishment | `PlayerbotAI` ammo stack refill/`InitAmmo` | Creates or refills ammo | Denied | AI update/ammo action | Buy/craft/loot normally |
| Food/ammo/reagents/potions/consumables | init/refresh/maintenance paths | Creates items | Denied | factory/manager/actions | None |
| Skills/professions | `InitAllSkills`, `UpdateTradeSkills` | Grants skills | Normal trainers/recipes/use only | factory/trainer/profession action | None |
| Spell initialization | `InitAvailableSpells`, special/random spell IDs | Grants spells | Denied except core starter spells | factory/bootstrap | None |
| Free trainer mode | `AutoTrainSpells=free`, gold cheat | Learns without cost | Denied | trainer action/config | Normal-cost trainer automation allowed |
| Auto trainer/quest/drop learning | `AutoLearnTrainerSpells`, `AutoLearnQuestSpells`, `AutoLearnDroppedSpells` | Learns without normal path | Denied | auto-learn actions | None |
| Auto-learn reward-item fabrication | `AutoLearnSpellAction` quest-item creation/mail | Creates/mails items | Denied | action-level guard | None |
| Earned talent spending | auto-talent action | Spends earned points | Allowed automation | talent action | No |
| Enchant/gem/template generation | `EnchantItem`, `AddGems`, `ApplyEnchantTemplate`, `InitGems` | Creates enchant/gem state | Denied | factory/enchant actions | Future normal-material enchanting |
| Auto-enchant on upgrade | `AutoEnchantUpgradeLoot`, equip hooks | Adds fabricated enchant | Denied | equip/repair/buy/RPG hooks | None |
| Prequests/global bot quest rewards | `PreQuests`, `RandomBotQuestIds` | Completes/rewards quests | Denied | bootstrap/factory | None |
| Generic quest completion commands | `QuestAction::CompleteQuest`, autocomplete actions | Completes/rewards quest, may create money/items | Denied | quest action | None |
| Required-money fabrication during quest completion | quest completion helper paths | Creates required money | Denied | quest action | None |
| Quest sync to bot | `SyncQuestWithPlayer` | Completes bot quest from player state | Denied | quest-giver action | None |
| Quest sync to real player | `SyncQuestForPlayer` | Mutates real player's quest log | Denied globally in Organic mode | quest-giver action | None |
| World buffs | `AiPlayerbot.WorldBuff*`, `WorldBuffAction` | Applies aura directly | Denied | world-buff action | None |
| Reputation initialization | `InitReputations` | Grants reputation | Denied | factory | None |
| Taxi-node initialization | `InitTaxiNodes` | Grants flight nodes | Denied | factory | Nodes learned normally |
| Mount initialization | `InitMounts` | Grants mounts/spells | Denied | factory | Normal purchase/quest/trainer |
| Pet initialization | `InitPet`, `InitPetSpells` | Creates/teaches pet state | Denied | factory | Normal class/tame/summon path; viability tracked |
| Guild bootstrap/free tabard | `CreateRandomGuilds`, `InitGuild` | Creates/joins guild, may give tabard | Denied | factory/guild manager | Normal charter workflow allowed |
| Arena-team bootstrap | `CreateRandomArenaTeams`, `InitArenaTeam` | Creates/joins arena teams | Denied | factory/arena manager | Not applicable to Classic runtime |
| All 13 configured cheat bits | `BotCheatMask`: taxi, gold, health, mana, power, item, cooldown, repair, movespeed, attackspeed, breath, glyph, quest | Bypasses game rules | Managed effective mask forced to zero | AI init/config resolution | None |
| Per-bot runtime cheat override | `PlayerbotAI::SetCheat`, cheat command | Enables bypass dynamically | Denied | command/AI setter | Fixture profile only |
| Normal quest reward/loot/equip | core quest/loot/equip handlers | Legitimate progression | Allowed gameplay/automation | existing handlers/actions | No |
| Vendor/repair/train | normal session/actions with real cost | Legitimate transaction | Allowed automation | existing action + core handler | No |
| AH/mail/trade | normal session handlers | Real transactions | Allowed gameplay/automation | existing actions | No |
| Random teleport | manager `teleport` timer | Relocates | Denied | manager/teleport | None |
| Teleport near player | config/manager | Relocates | Denied | manager | None |
| RPG camp/grind teleport | random/RPG selection | Relocates | Denied | manager/RPG action | None |
| Legacy transport mode 1/2 | `TransportTeleportType` | Boards/bypasses transport by teleport | Denied | transport action | Replaced by canonical modeled transfer |
| Canonical public transport transfer | 0002C route registry | Models missing boat/zeppelin/tram client behavior | Audit | Living transport service | `PUBLIC_TRANSPORT_TRANSFER` |
| Protected transport group sync | near-transport master sync | Relocates companion onto owner's transport context | Narrow audit | group/transport service | `TRANSPORT_GROUP_SYNC` |
| Taxi/hearth/portal/area trigger | core eligibility/handlers | Normal travel | Allowed gameplay | core handlers | No |
| Free/innkeeper summon | summon settings/actions | Relocates without normal source | Denied | summon action | None |
| Eligible player/spell summon | core summon rules | Normal travel | Allowed gameplay | core | No |
| Automatic manager revive | random revive timer/refresh | Resurrects/relocates directly | Denied | manager/refresh | None |
| Corpse release/spirit healer/resurrection spell | core rules | Normal death recovery | Allowed gameplay | existing actions/core | No |
| Stuck emergency relocation | 0002C recovery ladder | Last-resort relocation | Audit after all normal recovery fails | recovery service | `STUCK_EMERGENCY_TELEPORT` |
| Legacy login path | `AddRandomBots`, legacy `ProcessBot`, `asyncBotLogin=false` | Logs/rotates bots outside reconciled schedule | Denied for managed identities | manager dispatch/startup validation | Require `AsyncBotLogin=1` |
| Legacy timed online/offline state | `add`/`logout`, `RandomBotTimedLogout/Offline` | Rotates bots by event timers | Denied for managed identities | login/logout callbacks and criteria | None |
| Broad `init/upgrade/refresh/maintenance` | console/chat actions | Mutates many domains | Denied | command dispatch | Fixture profile only |
| Explicit bounded admin mutation | future authenticated admin | Intentional bypass | Audit only with action-specific reconciler | admin/policy | Named future action |
| Delete/reset population | existing account/reset paths | Deletes/recreates bots | Only through managed global operation | managed reset coordinator | New nonces; operation audit |
| Raw delete/reset SQL | `delete_randombots.sql`, `reset_randombots.sql` | Bypasses managed hooks | Unsupported once Organic is enabled | startup/reset validation | Block and repair/reset |
| Offline material progression | future planners | Simulated gains | Denied | planner/store | None |
| Account-transfer hire | `HireAction`: `SetMoney` + direct `characters.account` update | Moves bot to player account, mutates money | Denied | hire action | Future managed operation only |
| Instant repop relocation | `RepopAction` (`ReleaseSpiritAction.h`): `ResurrectPlayer(1.0)` + teleport to spawn/homebind | Full resurrection and relocation outside core death rules | Denied | repop action | Normal death recovery allowed |
| BG/arena regroup teleport | `BattleGroundJoinAction` member teleport to leader | Relocates group bots cross-world on queue/join | Denied | BG join action | Core BG entry rules remain gameplay |
| Direct split-stack item transfer | `GuildShareItemAction`: `Item::CreateItem` split-stack construction | Creates item stack outside trade/mail handlers | Denied | guild share action | Normal trade/mail allowed |
| Direct item ownership transfer | `GiveItemAction`/`GuildShareItemAction` full stack: `MoveItemFromInventory` + `SetOwnerGuid` | Moves items between characters outside trade handlers | Denied | give/share actions | Normal trade/mail allowed |
| Legacy direct mail mutation | `CheckMailAction` unconditional `DELETE FROM mail/mail_items` after partial `ProcessMail`; `SendMailAction` direct state changes | Can discard money/attachments; bypasses core mail handlers | Denied | mail actions | Core mail session handlers only |
| Minimal/off-screen move teleport | `AiPlayerbot.EnableMinimalMove` direct `TeleportTo` along path | Relocates without normal movement when unobserved | Denied | movement action | None |
| Direct summon helper | `SummonAction`/`UseMeetingStoneAction` helper: `ResurrectPlayer(1.0)` + `TeleportTo` | Resurrects and relocates without core summon rules | Denied | summon/meeting-stone actions | Core-rule summons remain gameplay |
| RTSC spell grant | `RtscAction` `learnSpell`/`removeSpell` on requesting player | Mutates a real player's spell book | Denied | rtsc action | None |
| Direct auction mutation | `GuildShareAhBuyAction`: `AuctionEntry::UpdateBid` bypassing `HandleAuctionPlaceBid` | Direct auction state change | Denied | guild AH action | Core auction handlers only |
| Debug mutation subcommands | `DebugAction` (`cdebug`) do/quest/position/npc paths reachable by group members | Arbitrary actions, quest/item/money fabrication, teleports | Denied | debug action | Fixture profile only; read-only diagnostics excluded |
| Unobserved movement teleport | `MovementActions` activity-throttled `TeleportTo` (area-trigger, long-distance, flight shortcuts) | Relocates without normal movement | Denied | movement action | None |
| Death-recovery teleport | `ReviveFromCorpseAction` direct `TeleportTo` corpse/graveyard | Relocates during death recovery when unobserved | Denied | revive action | Normal corpse run remains gameplay |
| Command character provisioning | `PlayerbotHolder::CreateBot` addclass-style creation with post-create grants | Creates leveled/geared characters on demand; the compound is indivisible (create + grants before the first save), so it cannot satisfy the create → commit FIXTURE root → provision boundary | Denied, including in the fixture profile | bot command | None; fixtures use `FIXTURE_CHARACTER_CREATE` + `FIXTURE_PROVISION` through a future dedicated wrapper |
| Guild-bank tab mutation | `BuyGuildBankTabAction` buys tabs/sets rights without interaction | Guild-bank state change | Denied | guild-bank action | None |
| Remote quest accept | `TravelAction` hardcoded Dark Portal `AddQuest` (10119/9407) | Accepts quests without quest-giver interaction | Denied | travel action | Normal quest-giver handlers only |
| Direct quest abandon | `PlayerbotAI::DropQuest` callers: quest-log pruning, failed-timer, guild-order actions | Abandons quests outside the core abandon handler | Denied | shared DropQuest boundary | Core abandon semantics only |
| Free pet happiness | `FeedPetAction`: `SetPower(POWER_HAPPINESS)` | Fills pet happiness without food or the Feed Pet spell | Denied | feed-pet action | Normal feed item/spell path |
| Direct aura removal | `RemoveAuraAction` (`ra` command), `WorldBuffStrategy::OnStrategyRemoved` | Removes arbitrary named auras, including hostile debuffs | Denied | remove-aura action and strategy-removed hook | Core-validated cancellation only |
| Direct item destruction | `SmartDestroyItemAction`/`DestroyItemAction` | Destroys items directly from maintenance/bag-pressure paths | Denied | destroy-item action | Core destroy-item handler |
| Encounter aura mutation | `KarazhanDungeonActions` (Netherspite): boss beam removal + Perseverance aura | Mutates shared encounter state, can affect real raid members | Denied | encounter action | Core encounter mechanics only |
| Login item fabrication | `PlayerbotHolder::OnBotLogin` hearthstone/death-gate creation | Recreates items on every login when missing | Denied | login hook | Starter items at managed bootstrap only |
| Free talent respec | `ChangeTalentsAction` / `TalentSpec::ApplyTalents` | Removes/learns talent spells without respec cost | Denied | talent actions | Core respec rules; earned-point spending stays automation |
| Direct glyph mutation | `GlyphAction` direct slot application (WotLK) | Applies/removes glyphs outside the core handler | Denied | glyph action | Core glyph handler only |
| Normal quest acceptance | core-eligible `AddQuest` via quest-giver/shared/item handlers | Legitimate quest intake | Allowed gameplay | the core-handler branch inside `QuestAction::AcceptQuest` (not the action entry, which also holds the denied `SyncQuestWithPlayer` fallback) | No |
| Shared-world respawn acceleration | `PlayerbotAI::AccelerateRespawn` (XpGain kill credit, Loot release); `RespawnModHostile`/`RespawnModNeutral` ship nonzero | Shortens a creature's respawn delay and can remove its corpse; alters spawn availability real players share | Denied | `AccelerateRespawn` shared boundary | None |
| Fixture character creation | fixture bootstrap under the test profile | Creates a fixture character before its FIXTURE root exists | Fixture profile only, pre-identity | fixture creation wrapper | Fixture profile only |
| Direct spirit-healer resurrection | `SpiritHealerAction`: resurrects even when no healer was found | Resurrects/damages durability without the core healer handler | Denied | revive action | Core spirit-healer handler only |

The rows from "Account-transfer hire" down were added by the Phase 0 source audit
under the A.2 completion rule; they were not in the original reviewed seed.

## A.2 Completion rule

Before 0.1 acceptance, repository search, targeted call-graph review, and runtime
instrumentation MUST show no known state-changing shortcut outside this matrix.
Phase 0 starts with the rows above as seed input; it does not assume the list is
already complete.

New code adding a shortcut MUST add a matrix row, `ActionKind`, policy test,
failure behavior, and action-specific reconciler when applicable. A generic
"misc synthetic action" category is prohibited.
