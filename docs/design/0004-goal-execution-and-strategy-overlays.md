# Living Realm 0004: goal execution and owned strategy overlays

- **Status:** Draft authoritative child design
- **Target:** Living Realm 0.1 foundation and 0.2 planner
- **Depends on:** [0001](0001-living-realm.md),
  [0002](0002-organic-policy-and-audit.md),
  [0003](0003-lifecycle-persistence-and-reconciliation.md)
- **Five goal contracts:** [0004A](0004-goal-contracts.md)

## 1. Decision

0.1 implements exactly five durable goals and exact adapters. It does not ship a
broad utility planner. Each adapter binds a persisted target to existing
Playerbots values/actions, constrains competing target selectors, defines
success/failure/retry/restart semantics, and never bypasses Organic policy.

A bounded **ambient quest-intake bridge** is part of `SAFE_IDLE`; it is not a
sixth goal or a general planner. It accepts reachable, eligible quests through
normal handlers and feeds a deterministic `COMPLETE_QUEST` candidate.

The exact-quest/no-progress state machine in mod-playerbots is an informative
reference described in 0006. Living Realm persists IDs and canonical snapshots,
never live quest pointers or process-local status state.

Because current strategies form a flat mutable set, Living Realm introduces
owned directives and deterministic recomposition. The 0.1 implementation
supports the layers needed by goals, wind-down, and commitments, while installing
the engine hooks required to prevent preset contamination and reset loss.

## 2. Goal model and transition writer

Goal roles are `PRIMARY` and `SUPPORTING`; 0.1 permits one active primary and no
independently scheduled supporting goals. States are `PENDING`, `ACTIVE`,
`PAUSED`, `BLOCKED`, `SUCCEEDED`, `FAILED`, and `CANCELLED`.

Every goal stores type, role, exact target payload, phase, generation, state
version, operation token, lease owner/token/expiry, activated/expiry/terminal
times, retry count, failure code, and payload version.

All transitions use the single writer in 0001A. Activation:

1. state writer reads current root and primary slot;
2. validates identity, expected versions, Organic policy, schedule/commitment
   precedence, and prior slot;
3. terminalizes or pauses the replaced goal according to adapter rules;
4. inserts/activates the new goal with operation token, generation, and lease;
5. updates the slot and directly commits;
6. re-reads slot/goal and verifies token/version;
7. world-thread adapter revalidates before applying directives.

Completion uses the same pattern: verify generation/version, write terminal
state/time/outcome, clear the slot, direct commit, re-read, then request
recomposition. Events alone never clear the slot.

## 3. Goal schema

```sql
CREATE TABLE ai_playerbot_living_goal (
  goal_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  operation_token BINARY(16) NOT NULL,
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
  UNIQUE KEY uq_living_goal_operation (operation_token),
  UNIQUE KEY uq_living_goal_lease (lease_token),
  KEY ix_living_goal_character
    (character_guid, identity_nonce, state)
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

Application code verifies that `primary_goal_id` belongs to the same identity, is
`PRIMARY`, and is active/non-terminal. Single-writer direct transitions and
re-read verification enforce the active slot; no row-lock or affected-row CAS is
assumed.

## 4. Owned strategy layers

Layers, highest precedence first, exactly matching 0001:

1. immediate safety/core legality;
2. Organic policy deny set;
3. protected real-player commitment;
4. encounter (future);
5. reservation/group;
6. ordinary maintenance prerequisite;
7. wind-down;
8. durable goal;
9. Population Director recommendation (0.2);
10. legal user/preset directives;
11. base/class and bounded ambient RPG behavior.

Each owner publishes directives `{Require, Prefer, Suppress}` for
`(BotState, strategy)`, plus owned AI values with generation.

`Suppress` wins only when its layer outranks all `Require` directives for that
strategy. At equal precedence, `Require` beats `Suppress` and emits a conflict
diagnostic unless an explicit deterministic rule exists. `Prefer` never
overrides `Require` or `Suppress`.

## 5. Required engine touchpoints

The ownership contract requires explicit changes to existing engine/store code:

### 5.1 Preset isolation

`PlayerbotDbStore::Save` MUST save only the legal user/preset layer. It MUST NOT
serialize the current live effective engine set. Runtime Living Realm overlays,
commitment strategies, encounter strategies, and safety suppressions are
non-persistable.

Player strategy commands update the user layer, then trigger recomposition.

### 5.2 Reset recomposition hook

`PlayerbotAI::ResetStrategies` rebuilds base/class strategies and loads the
user/preset layer, then MUST invoke a Living Realm recomposition callback before
the engines resume normal decisions. Master changes, BG/arena resets, and other
current reset callers therefore cannot silently drop overlays.

### 5.3 Transitive strategy closure

Strategies such as RPG add subordinate strategies in `OnStrategyAdded`.
The composer owns top-level directives and records the engine-derived transitive
closure under the parent strategy. It does not repeatedly remove children merely
because they were not individually requested.

Removal of a parent lets existing `OnStrategyRemoved` behavior remove its closure,
then the composer observes and verifies the effective set. A higher-priority
Organic suppression may still reject a forbidden transitive strategy and
diagnose the parent conflict.

### 5.4 Minimal effective-set application

Recomposition calculates top-level intended directives, applies the minimum
parent-level diff, allows engine hooks to settle, then inspects the resulting
effective set. Unknown direct mutations affecting a managed bot trigger
diagnostics and recomposition.

Player-owned alt bots do not receive Living Realm layers by default.

## 6. Exact adapter contract

Every goal adapter defines:

- candidate source and preconditions;
- payload schema/version and exact target IDs;
- phase state machine;
- AI values/directives supplied;
- existing selectors constrained/suppressed;
- success postcondition from authoritative game state;
- failure codes, bounded retry/backoff, and blocked behavior;
- common pause conditions;
- resume/restart validation;
- cleanup of owned values/directives;
- expiry/invalidation.

Adapters do not persist world pointers. Payload IDs are re-resolved on the world
thread. Unsupported travel/transport blocks the goal or selects a 0002C canonical
modeled route; it never invokes legacy random teleportation.

## 7. Worker/world model

0.1 selection may remain deterministic on the world thread because only five
goals exist. Any worker receives a bounded immutable snapshot: identity nonce,
goal generation/version, level/class/map, quest/trainer/vendor summaries,
schedule/commitment state, route capabilities, and expiry.

It returns candidate type/target/score/reason with the same versions. The world
thread discards stale results and revalidates Organic policy and core state before
activation. No worker reads live `Player`, `Group`, `TravelTarget`, quest-status
map, item, or engine state.

## 8. Failure behavior

| Failure | Result |
|---|---|
| Malformed/unknown goal | Quarantine or terminalize; activate persisted `SAFE_IDLE` if stores healthy |
| Goal target invalid after restart | Adapter invalidates/blocks and cleans directives |
| Lease expired | Reconcile; no second executor until state writer resolves slot |
| Stale worker/event | Discard by identity/generation/version |
| Overlay conflict | Apply precedence, log conflict; fail closed where Organic policy involved |
| Reset/preset load | Rebuild base/user layer, invoke recomposition hook |
| Preset save while overlay active | Save user layer only |
| Transitive forbidden strategy | Reject/diagnose parent strategy; do not oscillate child set |
| Database unavailable | Continue only already validated safe in-memory behavior; no durable transition claim |
| Unsupported route | Block or use canonical 0002C transfer; never random teleport |

## 9. Tests and acceptance

Tests cover concurrent proposal submission to the single writer, duplicate
completion, direct-commit/re-read failure, lease expiry, restart per phase, stale
results, overlay require/suppress conflicts, reset/preset behavior, preset
isolation, transitive strategy closure, player command interaction,
commitment/wind-down precedence, per-goal happy/failure/blocked/retry cases,
malformed payloads, and disabled parity.

Acceptance requires one enforced primary slot, exact target binding for all five
goals, authoritative success checks, owner-safe removal/recomposition, no preset
contamination, no world pointers off-thread, no Organic bypass, and deterministic
restart behavior.
