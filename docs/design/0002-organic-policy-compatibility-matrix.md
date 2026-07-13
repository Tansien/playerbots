# Living Realm 0002: Organic compatibility matrix

[Back to design 0002](0002-organic-policy-and-audit.md)

Each production path MUST have an enum/classification and a guarded enforcement point. “Denied” means no mutation; “audit” means the protocol in the audit appendix.

| Mechanism | Current key/path | Legacy effect | Organic policy | Enforcement | Audit/exception |
|---|---|---|---|---|---|
| Instant/full randomization | `InstantRandomize`, `Randomize*`, `PlayerbotFactory` | Generates broad state | Denied after fresh bootstrap | factory/manager entry | None |
| Level/XP assignment | `DisableRandomLevels`, `SetLevel`, sync | Sets level/XP | Denied | factory/login/sync | Admin action only, future |
| XP multiplier | `XPRate` | Changes earned XP | Realm-normal only | XP award hook/config | None |
| Random gear/upgrade | `RandomGear*`, `InitEquipment`, upgrade | Creates/replaces gear | Denied | factory/upgrade | None |
| Starting/periodic money | factory incremental/full money | Creates currency | Denied | factory | None |
| Bags/inventory | `InitBags`, `InitInventory` | Creates items | Only core starter inventory at creation | bootstrap/factory | None |
| Food/ammo/reagents/potions | init/refresh/consumables | Creates items | Denied | factory/refresh | None |
| Skills/professions | `InitAllSkills`, `UpdateTradeSkills` | Grants skills | Normal trainers/recipes only | factory/trainer | None |
| Spell initialization | `InitAvailableSpells`, specials | Grants spells | Denied except core starter spells | factory/bootstrap | None |
| Free spell learning | auto trainer/quest/drop keys | Learns without normal cost/path | Denied | actions/config | None |
| Talents | auto-talent action | Spends earned points | Allowed automation | talent action | No |
| Prequests/global quest rewards | `PreQuests`, `RandomBotQuestIds` | Completes/rewards quests | Denied | bootstrap/factory | None |
| Reputation/taxi | `InitReputations`, `InitTaxiNodes` | Grants state | Denied | factory | None |
| Mount/pet init | `InitMounts`, `InitPet*` | Creates/teaches | Denied except normal quest/trainer/tame/summon | factory/actions | None |
| Item/taxi/breath cheats | random-bot cheat mask | Bypasses inventory/travel/breath | Denied; strip from managed bots | AI init/cheat action | None |
| Auto equip/quest reward | normal loot/quest handlers | Chooses legal reward/equip | Allowed automation | action/handler | No |
| Vendor/repair/train | normal session handlers | Pays normal cost | Allowed automation | existing actions | No |
| AH/mail/trade | normal session handlers | Real transactions | Allowed gameplay/automation | existing actions | No |
| Random teleport | random manager timers | Relocates | Denied | manager/teleport | None |
| Teleport near player | config/manager | Relocates | Denied | manager | None |
| RPG camp teleport | RPG/random selection | Relocates | Denied | goal/manager | None |
| Transport mode 1/2 | `TransportTeleportType` | Transport/dock shortcut | Denied autonomously | travel/teleport | Protected `TRANSPORT_GROUP_SYNC` only |
| Taxi/hearth/portal | core eligibility/handlers | Normal travel | Allowed gameplay | core handlers | No |
| Free/innkeeper summon | summon settings/action | Relocates without normal source | Denied | summon action | None |
| Eligible player/spell summon | core summon rules | Normal travel | Allowed gameplay | core | No |
| Automatic manager revive | random revive timer/refresh | Resurrects directly | Denied | manager/refresh | None |
| Corpse release/spirit healer/res spell | core rules | Normal death recovery | Allowed gameplay | existing actions/core | No |
| Stuck teleport | recovery path | Relocates | Last-resort audited recovery | recovery service | `STUCK_EMERGENCY_TELEPORT` |
| Protected transport sync | near-transport group sync | Relocates companion | Narrow audited compatibility | group/teleport | `TRANSPORT_GROUP_SYNC` |
| Broad `init/upgrade/refresh` command | console/chat actions | Mutates many domains | Denied in 0.1 | command dispatch | No |
| Explicit bounded admin mutation | future authenticated admin | Intentional bypass | Audit only if action-specific reconciler exists | admin/policy | Named action |
| Delete/reset population | random account reset paths | Deletes/recreates bots | Allowed managed lifecycle operation | account factory/root store | Audit operation, new nonce |
| Offline progression | none required | Simulated gains | Denied | planner/store | None |

### Completion rule

Before 0.1 acceptance, repository search and runtime instrumentation MUST show no known state-changing shortcut outside this matrix. New code adding a shortcut MUST add a matrix row, policy enum, tests, failure behavior, and audit reconciler when applicable. A generic “misc synthetic action” category is prohibited.
