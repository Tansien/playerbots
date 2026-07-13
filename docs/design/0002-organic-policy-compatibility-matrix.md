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
| Temporary-money flight/purchase tricks | RPG flight and purchase helper paths | Grants money, performs action, restores balance | Denied | action-level guard | None |
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

## A.2 Completion rule

Before 0.1 acceptance, repository search, targeted call-graph review, and runtime
instrumentation MUST show no known state-changing shortcut outside this matrix.
Phase 0 starts with the rows above as seed input; it does not assume the list is
already complete.

New code adding a shortcut MUST add a matrix row, `ActionKind`, policy test,
failure behavior, and action-specific reconciler when applicable. A generic
"misc synthetic action" category is prohibited.
