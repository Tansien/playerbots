# Living Realm 0001A: implementation contract

[Back to the umbrella architecture](0001-living-realm.md)

## A.1 Reviewed baselines

The architecture was checked against these revisions:

| Component | Revision |
|---|---|
| CMaNGOS Playerbots | `92f12098dc8c5c25ef268f89cac0f43c6ca55d4b` |
| CMaNGOS Classic | `de8f72993b9e1faa1bf92dd5505f3daec5cea3ce` |
| CMaNGOS TBC | `e107b53c175e47d7e5b3496181353f5a6349be43` |
| CMaNGOS WotLK | `57a34d33fb8cb1822258c690c583cc9be8d04708` |
| AzerothCore mod-playerbots reference | `93aaea3de19243c09ce9ecb25627dc9671715eed` |

A design or implementation review MUST update the relevant baseline when it
depends on changed core behavior. "No drift" is never inferred from dates alone.

## A.2 Persistence and database-access model

Living Realm 0.1 deliberately does **not** assume interactive database
transactions, `SELECT ... FOR UPDATE`, affected-row compare-and-swap, or
read-your-own-write behavior through the existing buffered transaction API.

Instead:

1. `LivingRealmStateWriter` is the only in-process writer for Living Realm
   identity, schedule, goal, reservation, operation, and audit state.
2. It runs on the world thread or a world-thread-owned serial command queue.
3. Workers and login-selection tasks return proposals only; they never write
   Living Realm state.
4. Critical transitions use the existing synchronous direct-commit path
   (`CommitTransactionDirect` or a verified equivalent) and MUST complete before
   a dependent world mutation or queue decision is dispatched.
5. The writer reads current rows before the transition, validates the identity
   nonce and expected `state_version`, performs one direct transaction, then
   synchronously re-reads critical rows to verify the operation token and new
   version.
6. Because the current API does not expose reliable affected-row results,
   uniqueness constraints, operation tokens, re-read verification, and
   single-writer serialization enforce invariants. `state_version` detects stale
   proposals and external modification; it is not claimed as multi-writer CAS.
7. External SQL writers and active-active `mangosd` writers are unsupported.
   Detection of an unexpected version/token mismatch quarantines the affected
   identity or blocks the global operation.
8. Non-critical metrics may use bounded buffered writes. Audit `REQUESTED`,
   identity creation, goal-slot transitions, schedule/commitment ownership, and
   managed reset state are critical and use direct commits.
9. Living Realm stops accepting transitions, drains its serial queue, directly
   flushes critical state, and unregisters callbacks before the core halts its
   database delay threads. No Living Realm write is allowed after store shutdown.

A future multi-writer or high-throughput design may introduce a dedicated
synchronous database connection, but 0.1 does not require one.

## A.3 Persistence conventions

- Character identity uses `uint32 character_guid` plus `BINARY(16)
  identity_nonce`.
- Current and retired identities are stored separately so a reused low GUID never
  attaches to an old root.
- All new tables are InnoDB and use project-compatible MySQL/MariaDB features.
- UTC times are unsigned epoch milliseconds; monotonic time is used only inside a
  process.
- Structured payloads are validated JSON stored in `MEDIUMTEXT`, limited by the
  application to 65,535 bytes. Database-native JSON, filtered indexes, and
  generated columns are not assumed.
- Enum strings and payload versions are application-validated.
- Direct database edits/imports that bypass managed hooks are unsupported and
  cause deterministic quarantine or a global startup block according to scope.

## A.4 Build and test integration

Proposed source layout:

```text
playerbot/living/config/
playerbot/living/policy/
playerbot/living/audit/
playerbot/living/lifecycle/
playerbot/living/goals/
playerbot/living/persistence/
playerbot/living/overlays/
playerbot/living/events/
playerbot/living/tests/
```

Each directory MUST be added explicitly to the module CMake source list.
Production code remains under `BUILD_PLAYERBOTS`.

Phase 0 introduces a new option:

```text
BUILD_PLAYERBOTS_LIVING_TESTS
```

When enabled, it builds a host-side `playerbots_living_tests` CTest target for
pure policy, schema-transition, deterministic-clock, snapshot, and fault-injection
tests. The existing in-world `playerbot/strategy/tests` DSL remains the home for
scenario tests that need live maps, quests, groups, travel, and combat. There is
no pre-existing host-side test guard to reuse.

Expansion-specific behavior uses `MANGOSBOT_ZERO`, `MANGOSBOT_ONE`, or
`MANGOSBOT_TWO` at the narrowest boundary. Classic builds MUST NOT reference
TBC/WotLK-only identifiers outside guards.

Synthetic **fixture bots** MAY be created only under the host/in-world test
guards and an explicit non-production fixture profile. They MUST use separate
accounts/prefixes, MUST NOT receive Organic provenance, and MUST NOT enter the
production economy, schedules, audit metrics, or fairness accounting.

## A.5 Assumptions and rejected alternatives

Assumptions:

- one authoritative `mangosd` process writes a realm;
- Living Realm tables share the CharacterDatabase;
- InnoDB direct transactions are available;
- the core is built with C++20;
- LoginDatabase and CharacterDatabase cannot be atomically committed together;
- direct external character mutation is unsupported;
- Organic 0.1 does not support mixed managed and legacy random-bot populations
  under the same configured random-bot account prefix.

Rejected alternatives:

- using `ai_playerbot_random_bots` for all new state;
- persisting login/logout queue states;
- relying on buffered asynchronous transactions for pre-mutation audit durability;
- pretending `SELECT ... FOR UPDATE` is available through the current API;
- an external authoritative planner;
- config-only Organic safety;
- replacing the strategy engine;
- offline progression in Organic Realm;
- importing mod-playerbots maintenance, autogear, raid cheats, or AddClass bots
  into the Organic population.

## A.6 Risk register

| Risk | Mitigation |
|---|---|
| Missed synthetic path | Phase 0 inventory seeded by 0002A, central guard, mutation telemetry, deny-by-default |
| Critical write not durable before mutation | Direct commit + re-read verification through the single writer |
| Login path bypasses schedules | Require `AsyncBotLogin=1`, skip managed IDs in legacy paths, mandatory Organic predicate |
| Data race in login selection | Immutable POD snapshot; no `Player*`, DB, or event-cache access in worker |
| Crash between DB and world mutation | Action-specific request/apply/reconcile protocol |
| Stale state attaches after recreation | Identity nonce, current/history split, managed reset hooks |
| Goal fights existing AI | Exact adapters, owned directives, transitive-closure-aware composer |
| Quest/path gap stalls population | Ambient intake, bounded no-progress detection, modeled transport, blocked state |
| Public transport becomes arbitrary teleport | Canonical route registry, origin wait, route mask, action-specific audit |
| Database/write amplification | Coalescing, bounded queues, critical/non-critical split, 0.2 backpressure |
| Upstream drift | Pinned baselines, narrow adapters, three-expansion compile matrix |
| Disabled-mode regression | No required schema/path when disabled and parity tests |
| External reference brings synthetic shortcuts | 0006 adopt/reject boundary and fixture isolation |

## A.7 Requirements and traceability

| ID | Requirement | Authoritative design | Minimum acceptance evidence |
|---|---|---|---|
| LR-001 | Disabled Living Realm preserves legacy behavior | 0001, 0002 | parity test and no-schema startup |
| LR-002 | Fresh Organic identity and provenance are unambiguous | 0003/0003A | create/delete/reuse/reset tests |
| LR-003 | Organic config and unknown actions fail closed | 0002 | effective report and deny tests |
| LR-004 | Every known fabrication path is classified and guarded | 0002A | one test per row plus runtime inventory |
| LR-005 | Synthetic intent is durable before mutation and reconciled after crash | 0002B | all crash-window tests |
| LR-006 | Critical persistence uses one writer and direct durability | 0001A/0003A | writer ordering and shutdown tests |
| LR-007 | Actual session/group/quest state remains authoritative | 0001/0003 | startup-matrix tests |
| LR-008 | UTC schedules and safe wind-down survive restart | 0003 | deterministic clock/restart tests |
| LR-009 | Real-player requests and commitments preempt schedules safely | 0003/0003A | multi-player/lease tests |
| LR-010 | Managed bots cannot use legacy login rotation | 0003, 0002A | default-config rejection and path tests |
| LR-011 | Exactly five goals bind to authoritative targets | 0004/0004A | per-adapter integration tests |
| LR-012 | Overlay ownership prevents preset contamination and unsafe removal | 0004 | reset/save/transitive strategy tests |
| LR-013 | Transport/recovery compatibility is bounded and audited | 0002C | route/stuck/action reconciliation tests |
| LR-014 | Build, test, and expansion compatibility remain explicit | 0001A | host tests + Classic/TBC/WotLK builds |

## A.8 Readiness

The set is ready for **Phase 0 implementation** after maintainer review.

Living Realm 0.1 implementation may begin after designs 0002–0004 and their
appendices are approved. Design 0005 is a 0.2 proposal and is not a 0.1
prerequisite. Design 0006 is informative and cannot override a normative
requirement.
