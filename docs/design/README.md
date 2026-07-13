# Living Realm design documents

This directory defines **Living Realm**, an optional persistent-population layer
for CMaNGOS Playerbots.

The documents use **MUST**, **MUST NOT**, **SHOULD**, **SHOULD NOT**, and **MAY**
normatively. **Existing** means confirmed in the recorded code baselines.
**Proposed** means specified here but not yet implemented.

## Document map

| ID | Document | Status | Target |
|---|---|---|---|
| 0001 | [Umbrella architecture](0001-living-realm.md) | Draft umbrella design | Phase 0, 0.1, long-term boundaries |
| 0001A | [Implementation contract](0001-living-realm-implementation-contract.md) | Normative appendix | Baselines, persistence, build, risks, traceability |
| 0002 | [Organic policy](0002-organic-policy-and-audit.md) | Draft authoritative child design | Phase 0 and 0.1 |
| 0002A | [Organic compatibility matrix](0002-organic-policy-compatibility-matrix.md) | Normative appendix | Mutation inventory and classifications |
| 0002B | [Synthetic-action audit protocol](0002-organic-policy-audit-protocol.md) | Normative appendix | Durable request/apply/reconcile behavior |
| 0002C | [Recovery and transport compatibility](0002-recovery-and-transport.md) | Normative appendix | Public transports, group sync, stuck recovery |
| 0003 | [Lifecycle persistence and reconciliation](0003-lifecycle-persistence-and-reconciliation.md) | Draft authoritative child design | 0.1 |
| 0003A | [Lifecycle schema and reconciliation](0003-lifecycle-schema-and-reconciliation.md) | Normative appendix | Schema, startup matrix, concurrency |
| 0004 | [Goal execution and owned strategy overlays](0004-goal-execution-and-strategy-overlays.md) | Draft authoritative child design | 0.1 foundation; 0.2 overlays |
| 0004A | [Five 0.1 goal contracts](0004-goal-contracts.md) | Normative appendix | Exact adapters and ambient quest intake |
| 0005 | [Population selection and performance](0005-population-and-performance.md) | Draft authoritative child design | 0.2 |
| 0005A | [Population/performance contract](0005-population-performance-contract.md) | Normative appendix | Snapshots, backpressure, AH boundary |
| 0006 | [mod-playerbots reference and porting boundaries](0006-mod-playerbots-reference.md) | Informative reference | Reusable ideas and explicit non-goals |

Appendices use `A.n`, `B.n`, and `C.n` section numbers. A statement in a parent
document wins only when it explicitly overrides an appendix; otherwise the more
specific child or appendix is authoritative for its subject.

## Implementation order

1. Phase 0 mutation inventory, host-side tests, deterministic fixtures,
   disabled-mode parity, instrumentation, and persistence seams.
2. Organic policy, modeled compatibility actions, and synthetic-action audit
   from 0002/0002A/0002B/0002C.
3. Identity, schedules, lifecycle reconciliation, managed reset, and protected
   commitments from 0003/0003A.
4. The five 0.1 goals, ambient quest intake, and minimum overlay foundation
   from 0004/0004A.
5. Population Director, measurable fairness, and backpressure from 0005/0005A.
6. Future child designs for economy, relationships, dungeons, encounters,
   dialogue, and administration.

Classic behavior is the first acceptance target. Classic, TBC, and WotLK
compilation MUST remain green for every merged change unless a separately
approved compatibility exception records its guard, owner, test, and removal
plan.

Future work requires separate child designs before implementation: persistent
economy/professions, relationships/guild identities, autonomous dungeon
assembly, encounter rules, structured LLM dialogue, authenticated
administration, and full physical ship/zeppelin boarding.
