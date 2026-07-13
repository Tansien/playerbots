# Living Realm 0001: implementation contract

[Back to the umbrella architecture](0001-living-realm.md)

## 8. Transport decision for 0.1

Autonomous random bots MUST NOT select a goal whose only route requires a ship or zeppelin, and routine dock-to-dock/transport teleports are prohibited. Taxis, hearthstones, eligible spell portals, normal summons, and ordinary area transitions remain gameplay.

A bot in a protected real-player group MAY use the existing near-transport synchronization only as audited `TRANSPORT_GROUP_SYNC`, after revalidation that the owner is on the destination transport context. Autonomous traversal remains blocked until a child design implements boarding, waiting, riding, disembarking, and recovery.

## 9. Persistence conventions

- Character identity uses `uint32 character_guid` plus `BINARY(16) identity_nonce`.
- All new tables are InnoDB and use project-compatible MySQL/MariaDB features.
- UTC times are unsigned epoch milliseconds; monotonic time is used only in process.
- Structured payloads are validated JSON stored in `MEDIUMTEXT`, limited by the application to 65,535 bytes; database-native JSON and filtered indexes are not assumed.
- Active slots use unique keys, row locking, and `state_version`; no invariant is claimed without SQL or transactional enforcement.
- Direct database edits/imports that bypass managed hooks are unsupported and quarantine ambiguous identities.

## 10. Build and compatibility

Proposed source layout:

```text
playerbot/living/{config,policy,audit,lifecycle,goals,persistence,overlays,events,tests}/
```

Each directory MUST be added explicitly to the module CMake source list. Production code remains under `BUILD_PLAYERBOTS`; tests retain the existing test guard. Expansion-specific behavior uses `MANGOSBOT_ZERO`, `MANGOSBOT_ONE`, or `MANGOSBOT_TWO` at the narrowest boundary. Classic builds MUST NOT reference TBC/WotLK-only identifiers outside guards.

## 11. Assumptions and alternatives

Assumptions: one authoritative `mangosd` writer per realm; Living Realm tables share the CharacterDatabase; InnoDB transactions and `SELECT ... FOR UPDATE` are available; C++20 is available; direct external character mutation is unsupported.

Rejected alternatives: using `ai_playerbot_random_bots` for all new state; persisting queue states; an external authoritative planner; config-only safety; replacing the strategy engine; and offline progression in Organic Realm.

## 12. Risk register

| Risk | Mitigation |
|---|---|
| Missed synthetic path | Phase 0 inventory, central guard, mutation telemetry, fail closed |
| Crash between DB and world mutation | Action-specific request/apply/reconcile protocol |
| Stale state attaches after recreation | Identity nonce and managed deletion/reset hooks |
| Goal fights existing AI | Exact adapters and owned directives |
| Quest/path gap stalls population | Bounded retry, blocked state, coverage metrics, no silent teleport |
| Database/write amplification | Coalescing, state versions, bounded queues, 0.2 backpressure |
| Upstream drift | Narrow adapters, three-expansion compile matrix, doc updates with code |
| Disabled-mode regression | No required schema/path when disabled and parity tests |

## 13. Traceability and readiness

Requirements LR-001 through LR-014 map respectively to: Organic policy/audit (0002), lifecycle/identity/schedules/commitments (0003), goal slot/adapters/overlays (0004), population fairness/backpressure (0005), disabled parity, and three-expansion compilation. Each 0.1 requirement needs deterministic policy tests, an integration scenario, restart/stale-state coverage, fail-closed coverage, and parity coverage when it touches legacy code.

This set is ready for **Phase 0 implementation after maintainer review**. Living Realm 0.1 implementation should begin only after designs 0002–0004 are approved. Design 0005 is an implementation-ready 0.2 proposal, not a 0.1 prerequisite.
