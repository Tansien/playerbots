# Living Realm design documents

This directory defines **Living Realm**, an optional persistent-population layer for CMaNGOS Playerbots.

The documents use **MUST**, **MUST NOT**, **SHOULD**, **SHOULD NOT**, and **MAY** normatively. “Existing” means confirmed in the reviewed code baseline. “Proposed” means specified here but not yet implemented.

| ID | Document | Status | Target |
|---|---|---|---|
| 0001 | [Umbrella architecture](0001-living-realm.md) | Draft umbrella design | Phase 0, 0.1, long-term boundaries |
| 0002 | [Organic policy and synthetic-action audit](0002-organic-policy-and-audit.md) | Draft authoritative child design | Phase 0 and 0.1 |
| 0003 | [Lifecycle persistence and reconciliation](0003-lifecycle-persistence-and-reconciliation.md) | Draft authoritative child design | 0.1 |
| 0004 | [Goal execution and owned strategy overlays](0004-goal-execution-and-strategy-overlays.md) | Draft authoritative child design | 0.1 foundation; 0.2 overlays |
| 0005 | [Population selection and performance](0005-population-and-performance.md) | Draft authoritative child design | 0.2 |

Implementation appendices are normative parts of their parent designs: [0001 implementation contract](0001-living-realm-implementation-contract.md), [0002 compatibility matrix](0002-organic-policy-compatibility-matrix.md), [0002 audit protocol](0002-organic-policy-audit-protocol.md), [0003 schema/reconciliation matrix](0003-lifecycle-schema-and-reconciliation.md), [0004 goal contracts](0004-goal-contracts.md), and [0005 implementation contract](0005-population-performance-contract.md).

Implementation order:

1. Phase 0 inventory, deterministic fixtures, disabled-mode parity, and fault-injection seams.
2. Organic policy and synthetic-action audit from 0002.
3. Identity, schedules, lifecycle reconciliation, and protected commitments from 0003.
4. The five 0.1 goals and overlay foundation from 0004.
5. Population Director, fairness, and backpressure from 0005.

Classic behavior is the first acceptance target. Classic, TBC, and WotLK compilation MUST remain green for every merged change unless a separately approved compatibility exception records its guard, owner, and removal plan.

Future work requires separate child designs before implementation: persistent economy/professions, relationships/guild identities, bot-only dungeon scheduling, encounter rules, structured LLM dialogue, authenticated administration, and autonomous ship/zeppelin traversal.
