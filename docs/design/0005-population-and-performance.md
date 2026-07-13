# Living Realm 0005: population selection and performance

- **Status:** Draft authoritative child design
- **Target:** Living Realm 0.2
- **0.1 dependency:** None; 0.1 keeps bounded legacy-count selection behind schedule eligibility
- **Parent:** [0001 Living Realm architecture](0001-living-realm.md)

## 1. Scope

This design adds a **Population Director** after 0.1 has established identity, UTC schedules, lifecycle reconciliation, protected real-player commitments, Organic policy, and durable goals. It determines which already-eligible managed bots receive scarce online/activity capacity. It does not change levels, fabricate roles or equipment, override protected commitments, or become authoritative for actual online state.

The current random-bot system already exposes minimum/maximum bot counts, login criteria, per-interval login limits, near-player activation, `botActiveAlone`, activity-priority controls, minimal movement, and combat throttling. 0.2 composes those mechanisms through measurable selection and backpressure rather than promising that every remote bot executes full gameplay continuously.

## 2. Eligibility and selection are separate

The lifecycle reconciler in 0003 produces an immutable candidate snapshot. A bot is **eligible** only when:

- identity, provenance, schema, profile, and schedule are valid;
- desired state is online, or a protected commitment requires it;
- no quarantine, terminal deletion, incompatible migration, or unresolved audit ambiguity exists;
- class/race/expansion and core login rules permit login;
- no active queue attempt already represents the same transition; and
- policy precedence does not require the bot to remain offline.

The Population Director only ranks eligible candidates. It MUST NOT make an ineligible bot eligible, reinterpret actual session state, or write `ONLINE` as a durable fact. The existing login manager queues attempts; actual CMaNGOS session state remains authoritative.

## 3. Capacity model

0.2 maintains separately bounded budgets:

| Budget | Purpose |
|---|---|
| Online slots | Maximum managed random bots with sessions |
| Login operations | Maximum new logins per interval/tick |
| Logout operations | Maximum safe logouts per interval/tick |
| Foreground AI | Fully active bots near real players, in protected groups, instances, BGs, or explicit tests |
| Background AI | Remote logged-in bots receiving existing reduced/minimal activity |
| Planner work | Snapshot scoring operations per interval |
| Persistence writes | Coalesced non-critical state updates per interval |
| Synthetic actions | Audited recovery dispatches per interval |

Protected real-player commitments reserve required online/foreground capacity before discretionary selection. Safety and Organic policy remain higher priority than every budget; a capacity shortage cannot authorize fabrication or an unsafe forced logout.

## 4. Measurable fairness

Each eligible character has durable accounting:

```text
desired_ms     schedule time eligible for service
granted_ms     actual observed online time while not quarantined
foreground_ms  actual foreground-classified time
last_grant_ms  last transition to actual online
selection_count
```

A character's normalized service deficit is:

```text
deficit_ratio = (desired_ms - granted_ms) / max(desired_ms, fairness_floor_ms)
```

The 0.2 ranking score is deterministic:

```text
score =
    weight_deficit       * clamp(deficit_ratio)
  + weight_role_scarcity * role_scarcity
  + weight_zone_need     * zone_need
  + weight_player_need   * protected_or_requested_need
  - weight_recent        * recent_service_penalty
  - weight_zone_pressure * zone_pressure
  - weight_cost          * estimated_activity_cost
```

Rules:

- protected commitments are mandatory selections, not merely high scores;
- no score may bypass eligibility or Organic policy;
- stable ties use identity nonce plus schedule generation, not container/database iteration order;
- granted time comes from observed actual sessions, never queue intent;
- downtime and periods blocked by policy do not count as denied desired service;
- profiles may influence preferences only within bounded weights.

Acceptance metrics over a configured observation window:

- no continuously eligible, non-quarantined bot starves longer than `MaxFairnessWait` when capacity exists;
- p95 absolute service-deficit ratio stays below a configured threshold in a steady synthetic workload;
- Jain's fairness index for comparable candidates exceeds a configured threshold;
- repeated deterministic runs with the same snapshots/seeds produce the same ordering; and
- role/zone bonuses cannot keep a permanently favored subset online after its deficit is repaid.

Threshold defaults are established from Phase 0/0.1 measurements; the design does not invent production values before measurement.

## 5. Role and zone recommendations

Role scarcity uses actual class/spec/strategy-derived capabilities and current group demand. It MUST NOT respec, level, equip, or fabricate a character to satisfy a target. Zone need uses reconciled map/zone snapshots and configured soft targets. A zone-pressure penalty prevents starter-zone flooding but never teleports a selected bot elsewhere.

Recommendations may select among already eligible bots and may later propose a compatible goal through the 0004 planner boundary. They cannot replace a protected commitment, schedule wind-down, survival prerequisite, or active durable goal without the policy precedence and goal transition rules in 0001/0004.

## 6. Operational activity classifications

The terms below describe observed operating modes, not a promise that unloaded CMaNGOS grids simulate full gameplay:

| Classification | Definition | Existing mechanisms used |
|---|---|---|
| Foreground | Near a real player; protected group; instance/BG; explicit high-priority/test work | force-near activation, full AI updates, current group/BG behavior |
| Background | Logged in but remote and not foreground | existing activity priorities, minimal updates/movement, passive delays, `botActiveAlone`/optimization controls |
| Offline | No actual session | schedule/planner persistence only; no material progression |

0.2 MUST first instrument how these current controls behave on loaded/unloaded maps. It MAY tune or compose them, but MUST NOT claim remote genuine quest progression where the core does not actually update the relevant grid. Formal new simulation guarantees require another design and tests.

Classification changes are derived from authoritative world/group/map state. They are not persisted as lifecycle truth; aggregated accounting may be persisted for fairness/metrics.

## 7. Implementation contract and acceptance

Snapshot validation, backpressure, Auction House scaling boundaries, persistence, failure behavior, and acceptance are normative in [the 0005 implementation contract](0005-population-performance-contract.md).
