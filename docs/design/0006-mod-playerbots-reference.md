# Living Realm 0006: mod-playerbots reference and porting boundaries

- **Status:** Informative reference; not authoritative
- **Reviewed baseline:**
  `mod-playerbots/mod-playerbots@93aaea3de19243c09ce9ecb25627dc9671715eed`
- **Purpose:** Record useful mature ideas without importing AzerothCore/WotLK
  assumptions or weakening Organic Realm.

## 1. Relationship and limits

CMaNGOS Playerbots and AzerothCore mod-playerbots descend from related Playerbots
lineages, but their core APIs, packets, database models, group/LFG systems,
movement code, and expansion rules have diverged.

No mod-playerbots subsystem is treated as drop-in code. Any adapted code requires
a dedicated review for:

- CMaNGOS API and world-thread ownership;
- Classic/TBC/WotLK expansion guards;
- persistence and restart semantics;
- Organic policy classification;
- tests and performance;
- original copyright and GPL-compatible attribution.

This document cannot override 0001–0005.

## 2. Ideas worth adopting

| mod-playerbots area | Mature idea | Living Realm use |
|---|---|---|
| New RPG state machine | Typed `GoGrind`, `GoCamp`, `WanderNpc`, `DoQuest`, `TravelFlight`, `Rest`, and PvP states | Reference for typed phases, bounded rest/camp behavior, and exact quest execution |
| Quest executor | Exact quest/objective, POI movement, objective counters, no-progress timeout, low-priority failures | 0004A quest fingerprint and no-progress rules |
| Raid strategies | Dedicated MC/Onyxia/BWL/AQ and later-expansion mechanics; tank assignments, movement, multipliers | Future encounter child design and test oracle |
| Dungeon/LFG lifecycle | Role checks, proposals, group leadership and completion flow | Future Vanilla-native dungeon reservation lifecycle, not RDF teleport |
| Activity scaling | Priority tiers, periodic online/offline pools, latency/performance controls | Comparative input for 0005 instrumentation and backpressure |
| AddClass pools | Fast deterministic class/role provisioning | Fixture-bot test profile only |
| Addons | Bot roster, raid composition, commands, gear/status visibility | Future Vanilla 1.12 operator UI |
| Loot/gear evaluation | Spec-aware upgrades, recipe/disenchant decisions | Future legitimate item valuation, without item creation |
| AH operation | Shared operational experience with many bots | Future market snapshot/serialized execution design |

## 3. Explicitly rejected imports for Organic Realm

The following mod-playerbots conveniences are useful on instant-party or test
servers but conflict with Organic provenance:

- random level or gear initialization;
- AddClass characters in the production population;
- maintenance that creates bags, ammo, food, reagents, potions, skills, spells,
  mounts, pets, reputation, keys, or attunements;
- autogear or BiS item creation;
- free repair, free trainer learning, automatic attunement completion;
- `food`, `taxi`, `raid`, gold, health, mana, power, item, or similar cheats;
- LFG/RDF teleport semantics;
- WotLK packet or vehicle assumptions;
- transient live pointers as durable goal state;
- LLM authority over gameplay.

Equivalent functionality may exist only in an isolated fixture/test profile or
a future explicitly non-Organic mode.

## 4. Phase 0 comparison work

Phase 0 SHOULD:

1. create behavioral fixtures for an exact quest with multiple objectives,
   no-progress failure, rest, and camp return;
2. compare CMaNGOS focus-quest/TravelMgr behavior with mod-playerbots'
   `NewRpgInfo`/quest executor;
3. record which encounter primitives from its Vanilla raid strategies are
   reusable concepts;
4. compare activity-priority inputs and overload metrics with CMaNGOS's existing
   activity controls;
5. define a fixture-bot account prefix and build guard isolated from managed
   Organic roots;
6. document addon workflows worth reproducing for the 1.12 client.

This work produces tests and future-design input. It does not expand 0.1's five
goal types.

## 5. Quest-adapter lessons incorporated into 0.1

0004A adopts these concepts:

- typed durable phases rather than one broad "questing" flag;
- exact quest and objective IDs;
- authoritative objective-counter comparisons;
- a bounded no-progress window at a quest location;
- explicit blocked/failure reasons rather than silent target replacement;
- local quest acquisition feeding the exact quest goal;
- no live `Quest*` pointer in persistence.

CMaNGOS TravelMgr, focus-quest values, quest handlers, and strategy engine remain
the execution layer.

## 6. Future encounter design

A future encounter child design SHOULD study mod-playerbots' dedicated strategies
for Molten Core, Onyxia, Blackwing Lair, and AQ20 first.

Candidate reusable primitives include:

```text
assign main/assist tank
mark target
move away from group
move away from boss
stack/spread
disable AoE
stop DPS
switch target
interrupt/dispel assignment
resistance preparation
```

The future design must separate reusable mechanic primitives from
encounter-specific IDs, keep all commands on the world thread, and provide
deterministic scenario tests. Encounter logic is not part of Living Realm 0.1.

## 7. Fixture-bot boundary

A fixture bot:

- exists only with `BUILD_PLAYERBOTS_LIVING_TESTS` or an explicit test realm;
- uses a distinct account prefix;
- may receive deterministic synthetic level/spec/gear for repeatable tests;
- is marked `FIXTURE`, never `ORGANIC_CREATED`;
- cannot enter production AH/mail/trade, schedules, relationships, fairness, or
  audit accounting;
- is destroyed or reset by fixture tooling, not the managed Organic reset flow.

This preserves the testing benefit of AddClass-style provisioning without
polluting the realm's progression guarantees.

## 8. Operator experience

The mature addon workflows are useful product references. A future 1.12 addon
may display:

- bot identity, role, gear/durability, health/mana;
- current goal/phase, travel target, schedule and wind-down;
- protected commitment owner;
- tank/healer/encounter assignment;
- group presets and safe commands.

The addon remains a view/command surface. It cannot authorize a denied Organic
action or become a persistence authority.
