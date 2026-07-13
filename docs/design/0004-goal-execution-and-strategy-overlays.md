# Living Realm 0004: goal execution and owned strategy overlays

- **Status:** Draft authoritative child design
- **Target:** Living Realm 0.1 foundation and 0.2 planner
- **Depends on:** [0001](0001-living-realm.md), [0002](0002-organic-policy-and-audit.md), [0003](0003-lifecycle-persistence-and-reconciliation.md)
- **Five goal contracts:** [0004A](0004-goal-contracts.md)

## 1. Decision

0.1 implements exactly five durable goals and exact adapters. It does not ship a broad utility planner. Each adapter binds a persisted target to existing Playerbots values/actions, constrains competing target selectors, defines success/failure/retry/restart semantics, and never bypasses Organic policy.

Because current strategies form a flat mutable set, Living Realm introduces owned directives and deterministic recomposition. The 0.1 implementation may support only layers needed by the five goals/commitments, but the data model and conflict rules must not rely on opportunistic add/remove.

## 2. Goal model

Goal roles are `PRIMARY` and `SUPPORTING`; 0.1 permits one active primary and no independently scheduled supporting goals. States are `PENDING`, `ACTIVE`, `PAUSED`, `BLOCKED`, `SUCCEEDED`, `FAILED`, `CANCELLED`. Every goal stores type, role, exact target payload, phase, generation, state version, lease owner/token/expiry, activated/expiry/terminal times, retry count, failure code, and payload version.

A separate primary-slot row enforces at most one active primary goal. Activation transaction:

1. lock the character root and primary slot;
2. validate identity, Organic policy, commitment/schedule precedence, and prior slot;
3. terminalize or pause the replaced goal according to adapter rules;
4. insert/activate goal with new generation and lease;
5. update slot to goal ID; commit;
6. world-thread adapter revalidates before applying directives.

Completion locks slot/goal, verifies generation/version, records terminal state/time/outcome, clears the slot, commits, and requests recomposition. Events alone never clear the slot.

## 3. Goal schema

```sql
CREATE TABLE ai_playerbot_living_goal (
  goal_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  character_guid INT UNSIGNED NOT NULL,
  identity_nonce BINARY(16) NOT NULL,
  goal_type VARCHAR(48) NOT NULL,
  goal_role VARCHAR(16) NOT NULL,
  parent_goal_id BIGINT UNSIGNED NULL,
  state VARCHAR(16) NOT NULL,
  phase VARCHAR(32) NOT NULL,
  generation BIGINT UNSIGNED NOT NULL,
  state_version BIGINT UNSIGNED NOT NULL DEFAULT 0,
  lease_owner VARCHAR(64) NULL,
  lease_token BINARY(16) NULL,
  lease_until_ms BIGINT UNSIGNED NULL,
  activated_at_ms BIGINT UNSIGNED NULL,
  expires_at_ms BIGINT UNSIGNED NULL,
  terminal_at_ms BIGINT UNSIGNED NULL,
  retry_count SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  failure_code VARCHAR(64) NULL,
  payload_version SMALLINT UNSIGNED NOT NULL,
  payload MEDIUMTEXT NOT NULL,
  created_at_ms BIGINT UNSIGNED NOT NULL,
  updated_at_ms BIGINT UNSIGNED NOT NULL,
  PRIMARY KEY (goal_id),
  UNIQUE KEY uq_living_goal_lease (lease_token),
  KEY ix_living_goal_character (character_guid, identity_nonce, state)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE ai_playerbot_living_goal_slot (
  character_guid INT UNSIGNED NOT NULL,
  identity_nonce BINARY(16) NOT NULL,
  primary_goal_id BIGINT UNSIGNED NULL,
  slot_generation BIGINT UNSIGNED NOT NULL,
  state_version BIGINT UNSIGNED NOT NULL DEFAULT 0,
  updated_at_ms BIGINT UNSIGNED NOT NULL,
  PRIMARY KEY (character_guid, identity_nonce),
  UNIQUE KEY uq_living_primary_goal (primary_goal_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
```

Application code verifies that `primary_goal_id` belongs to the same identity, is `PRIMARY`, and is active/non-terminal. MySQL/MariaDB-portable transactions enforce transitions; no partial/filtered unique index is assumed.

## 4. Owned strategy layers

Layers, highest precedence first:

1. immediate safety/core legality;
2. Organic policy deny set;
3. protected real-player commitment;
4. encounter (future);
5. reservation/group;
6. maintenance prerequisite;
7. wind-down;
8. durable goal;
9. Population Director recommendation (0.2);
10. user/preset and base/class behavior as allowed by higher policy.

Each owner publishes directives `{Require, Prefer, Suppress}` for `(BotState, strategy)`, plus owned AI values with generation. `Suppress` only wins when its layer outranks all `Require` directives for that strategy. At equal precedence, `Require` beats `Suppress` and emits a conflict diagnostic unless an explicit deterministic rule exists. `Prefer` never overrides `Require/Suppress`.

Recomposition builds the intended effective set from current base/class strategies, user/preset layer, and all active overlays, then applies a minimal diff to each existing engine. Removing an overlay recomputes from remaining owners; it does not blindly remove its prior additions. Reset reconstructs base/preset first, then reapplies live overlays. Living Realm directives are never stored in ordinary Playerbot presets.

Player chat/operator commands modify the user layer or explicitly replace a goal. They cannot alter Organic deny directives. Unknown direct strategy mutations affecting a managed bot trigger recomposition/diagnostics. Player-owned alt bots do not receive Living Realm layers by default.

0.1 may implement a narrow composer for goal/wind-down/commitment directives while preserving this ownership contract. 0.2 completes general layering and planner recommendations.

## 5. Exact adapter contract

Every goal adapter defines:

- candidate source and preconditions;
- payload schema/version and exact target IDs;
- phase state machine;
- AI values/directives supplied;
- existing selectors constrained/suppressed;
- success postcondition from authoritative game state;
- failure codes, bounded retry/backoff, and blocked behavior;
- common pause conditions (combat, protected commitment precedence, instance/BG/trade/taxi where incompatible);
- resume/restart validation;
- cleanup of owned values/directives;
- expiry/invalidation.

Adapters do not persist world pointers. Payload IDs are re-resolved on the world thread. Unsupported travel/transport invalidates or blocks a goal rather than teleporting.

## 6. Worker/world model

0.1 selection may remain deterministic on the world thread because only five goals exist. Any worker receives a bounded immutable snapshot: identity nonce, goal generation/version, level/class/map, quest/trainer/vendor summaries, schedule/commitment state, and expiry. It returns candidate type/target/score/reason with the same versions. The world thread discards stale results and revalidates Organic policy and core state before activation.

## 7. Failure behavior

| Failure | Result |
|---|---|
| Malformed/unknown goal | Quarantine or terminalize; activate persisted `SAFE_IDLE` if stores healthy |
| Goal target invalid after restart | Adapter invalidates/blocks and cleans directives |
| Lease expired | Reconcile; no second executor until slot lock resolves |
| Stale worker/event | Discard by identity/generation/version |
| Overlay conflict | Apply precedence, log conflict; fail closed where Organic policy involved |
| Reset/preset load | Rebuild base then recompose overlays |
| Database unavailable | Continue only already validated safe in-memory behavior; no durable transition claim |
| Unsupported route | Block goal; no synthetic travel |

## 8. Tests and acceptance

Tests cover concurrent activation, duplicate completion, lease expiry, restart per phase, stale results, overlay require/suppress conflicts, reset/preset behavior, player command interaction, commitment/wind-down precedence, per-goal happy/failure/blocked/retry cases, DB failures at slot boundaries, malformed payloads, and disabled parity.

Acceptance requires one enforced primary slot, exact target binding for all five goals, authoritative success checks, owner-safe removal/recomposition, no preset contamination, no world pointers off-thread, no Organic bypass, and deterministic restart behavior.
