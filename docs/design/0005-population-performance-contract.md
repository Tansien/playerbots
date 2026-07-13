# Living Realm 0005: population and performance implementation contract

[Back to population selection and performance](0005-population-and-performance.md)

## 7. Immutable snapshots and stale-result rejection

The world thread builds bounded snapshots containing identity nonce, schedule/goal generations, state versions, actual map/zone/group classification, role summary, recent service, estimated cost, capacity, and expiry. Workers may score snapshots but MUST NOT retain world pointers or query mutable engine state.

A proposal returns candidate order, scores, reasons, input generation, and expiry. Before queueing login/logout or changing discretionary activity, the world thread revalidates:

- identity nonce and state versions;
- actual online/queue/group/map state;
- schedule and commitment generation;
- capacity and policy;
- proposal expiry.

Stale results are discarded without side effects.

## 8. Backpressure and degradation order

The Director samples world diff/latency, AI update cost, database delay/queue depth, login-holder latency, planner queue depth, and audit backlog. It applies hysteresis and bounded rate changes. The degradation order is:

1. stop discretionary new planner work;
2. reduce non-critical telemetry/persistence frequency through coalescing;
3. hold discretionary logins;
4. reduce background activity using existing safe controls;
5. safely wind down discretionary bots when sustained overload persists;
6. preserve protected real-player commitments and foreground safety work unless the core itself rejects capacity.

Backpressure MUST NOT:

- force logout during combat, instance/BG, taxi/transport, trade, pending save, or protected commitment;
- convert a bot to legacy random behavior;
- skip required audit persistence for a synthetic action;
- mutate character progression; or
- treat a queued logout as completed.

Recovery uses separate enter/exit thresholds and a minimum stable interval to prevent oscillation.

## 9. Future Auction House scaling boundary

Persistent economy is not part of 0.2, but its performance boundary is fixed here because current AH actions serialize through a global mutex and may sample many live listings.

A future economy design MUST use:

1. a world-thread `MarketSnapshotService` producing immutable, bounded aggregate/listing snapshots;
2. worker-side price/demand analysis against those snapshots;
3. a serialized, rate-limited execution queue for actual post/bid/buy actions;
4. world-thread revalidation through existing normal session handlers;
5. per-bot spending, listing, and outstanding-bid limits;
6. proposal expiry, duplicate suppression, and backpressure; and
7. no independent large/full live-market scan by every bot.

The current global serialization is retained until that child design safely replaces it.

## 10. Persistence

0.2 MAY add bounded accounting tables such as:

```sql
CREATE TABLE ai_playerbot_living_service_accounting (
  character_guid INT UNSIGNED NOT NULL,
  identity_nonce BINARY(16) NOT NULL,
  window_start_ms BIGINT UNSIGNED NOT NULL,
  desired_ms BIGINT UNSIGNED NOT NULL DEFAULT 0,
  granted_ms BIGINT UNSIGNED NOT NULL DEFAULT 0,
  foreground_ms BIGINT UNSIGNED NOT NULL DEFAULT 0,
  selection_count INT UNSIGNED NOT NULL DEFAULT 0,
  last_grant_ms BIGINT UNSIGNED NULL,
  state_version BIGINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (character_guid, identity_nonce, window_start_ms),
  KEY ix_living_service_window (window_start_ms)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
```

Accounting is derived and repairable; it never overrides actual session state. Windows are compacted/expired under a documented retention policy. Critical lifecycle/goal state is never dropped to protect metric writes.

## 11. Failure behavior

| Failure | Result |
|---|---|
| Director/worker unavailable | Deterministic bounded fallback among eligible candidates or hold discretionary changes |
| Snapshot stale | Discard and resnapshot |
| Accounting unavailable | Continue safe eligibility with conservative deterministic ordering; emit health failure |
| Capacity metrics unavailable | Freeze discretionary expansion; do not guess extra capacity |
| Sustained overload | Apply degradation order and safe wind-down |
| Fairness state malformed | Recompute from bounded observed history or quarantine accounting row, never character state |
| Role/zone target impossible | Report unmet need; do not fabricate or teleport |
| Protected demand exceeds capacity | Preserve accepted commitments where possible; reject/defer new commitments deterministically |

## 12. Tests and acceptance

Tests cover deterministic ranking, fairness/starvation bounds, identical candidates, downtime, protected-capacity reservation, role/zone scarcity, stale snapshots, worker/database/metric failure, load hysteresis, queue saturation, safe logout deferrals, loaded/unloaded map classifications, and three-expansion compilation.

0.2 acceptance requires:

- eligibility and selection are separate and enforced;
- protected commitments preempt discretionary capacity without bypassing safety/Organic policy;
- fairness metrics meet measured thresholds in deterministic simulations;
- no favored subset persists after service deficits normalize;
- overload reduces work in the specified order without unsafe logout or fabricated progression;
- foreground/background labels match observed current-core behavior; and
- disabled Living Realm and 0.1-without-Director behavior remain unchanged.
