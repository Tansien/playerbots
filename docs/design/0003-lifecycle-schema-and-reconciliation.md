# Living Realm 0003A: lifecycle schema and reconciliation

[Back to design 0003](0003-lifecycle-persistence-and-reconciliation.md)

## A.1 Schema examples

```sql
CREATE TABLE ai_playerbot_living_schema (
  component VARCHAR(64) NOT NULL,
  schema_version INT UNSIGNED NOT NULL,
  migration_state VARCHAR(16) NOT NULL,
  updated_at_ms BIGINT UNSIGNED NOT NULL,
  PRIMARY KEY (component)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE ai_playerbot_living_operation (
  operation_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  operation_token BINARY(16) NOT NULL,
  operation_type VARCHAR(32) NOT NULL,
  phase VARCHAR(24) NOT NULL,
  requested_at_ms BIGINT UNSIGNED NOT NULL,
  terminal_at_ms BIGINT UNSIGNED NULL,
  state_version BIGINT UNSIGNED NOT NULL DEFAULT 0,
  payload_version SMALLINT UNSIGNED NOT NULL,
  payload MEDIUMTEXT NOT NULL,
  failure_code VARCHAR(64) NULL,
  PRIMARY KEY (operation_id),
  UNIQUE KEY uq_living_operation_token (operation_token),
  KEY ix_living_operation_phase (phase, requested_at_ms)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE ai_playerbot_living_character (
  character_guid INT UNSIGNED NOT NULL,
  identity_nonce BINARY(16) NOT NULL,
  provenance VARCHAR(32) NOT NULL,
  account_id INT UNSIGNED NOT NULL,
  race TINYINT UNSIGNED NOT NULL,
  class TINYINT UNSIGNED NOT NULL,
  status VARCHAR(24) NOT NULL,
  quarantine_code VARCHAR(64) NULL,
  state_version BIGINT UNSIGNED NOT NULL DEFAULT 0,
  created_at_ms BIGINT UNSIGNED NOT NULL,
  updated_at_ms BIGINT UNSIGNED NOT NULL,
  PRIMARY KEY (character_guid),
  UNIQUE KEY uq_living_current_identity (character_guid, identity_nonce),
  KEY ix_living_current_status (status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE ai_playerbot_living_character_history (
  character_guid INT UNSIGNED NOT NULL,
  identity_nonce BINARY(16) NOT NULL,
  provenance VARCHAR(32) NOT NULL,
  account_id INT UNSIGNED NOT NULL,
  race TINYINT UNSIGNED NOT NULL,
  class TINYINT UNSIGNED NOT NULL,
  retired_reason VARCHAR(64) NOT NULL,
  created_at_ms BIGINT UNSIGNED NOT NULL,
  retired_at_ms BIGINT UNSIGNED NOT NULL,
  final_state_version BIGINT UNSIGNED NOT NULL,
  payload MEDIUMTEXT NOT NULL,
  PRIMARY KEY (character_guid, identity_nonce),
  KEY ix_living_history_retired (retired_at_ms)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE ai_playerbot_living_profile (
  character_guid INT UNSIGNED NOT NULL,
  identity_nonce BINARY(16) NOT NULL,
  profile_version SMALLINT UNSIGNED NOT NULL,
  profile_seed BINARY(16) NOT NULL,
  archetype VARCHAR(32) NOT NULL,
  traits_payload MEDIUMTEXT NOT NULL,
  state_version BIGINT UNSIGNED NOT NULL DEFAULT 0,
  created_at_ms BIGINT UNSIGNED NOT NULL,
  updated_at_ms BIGINT UNSIGNED NOT NULL,
  PRIMARY KEY (character_guid, identity_nonce)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE ai_playerbot_living_schedule (
  character_guid INT UNSIGNED NOT NULL,
  identity_nonce BINARY(16) NOT NULL,
  schedule_version SMALLINT UNSIGNED NOT NULL,
  schedule_generation BIGINT UNSIGNED NOT NULL,
  desired_online TINYINT UNSIGNED NOT NULL,
  window_start_ms BIGINT UNSIGNED NULL,
  window_end_ms BIGINT UNSIGNED NULL,
  next_window_start_ms BIGINT UNSIGNED NULL,
  wind_down_requested_ms BIGINT UNSIGNED NULL,
  wind_down_deadline_ms BIGINT UNSIGNED NULL,
  last_reconciled_at_ms BIGINT UNSIGNED NULL,
  state_version BIGINT UNSIGNED NOT NULL DEFAULT 0,
  payload MEDIUMTEXT NOT NULL,
  created_at_ms BIGINT UNSIGNED NOT NULL,
  updated_at_ms BIGINT UNSIGNED NOT NULL,
  PRIMARY KEY (character_guid, identity_nonce),
  KEY ix_living_schedule_due (desired_online, next_window_start_ms)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE ai_playerbot_living_reservation (
  reservation_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  reservation_type VARCHAR(32) NOT NULL,
  owner_real_guid INT UNSIGNED NULL,
  lease_token BINARY(16) NOT NULL,
  state VARCHAR(16) NOT NULL,
  protected_real_player TINYINT UNSIGNED NOT NULL,
  created_at_ms BIGINT UNSIGNED NOT NULL,
  expires_at_ms BIGINT UNSIGNED NOT NULL,
  terminal_at_ms BIGINT UNSIGNED NULL,
  state_version BIGINT UNSIGNED NOT NULL DEFAULT 0,
  payload_version SMALLINT UNSIGNED NOT NULL,
  payload MEDIUMTEXT NOT NULL,
  PRIMARY KEY (reservation_id),
  UNIQUE KEY uq_living_reservation_lease (lease_token),
  KEY ix_living_reservation_state (state, expires_at_ms)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE ai_playerbot_living_reservation_member (
  reservation_member_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  reservation_id BIGINT UNSIGNED NOT NULL,
  character_guid INT UNSIGNED NOT NULL,
  identity_nonce BINARY(16) NOT NULL,
  member_role VARCHAR(24) NOT NULL,
  active_slot TINYINT UNSIGNED NULL,
  join_generation INT UNSIGNED NOT NULL DEFAULT 1,
  joined_at_ms BIGINT UNSIGNED NOT NULL,
  released_at_ms BIGINT UNSIGNED NULL,
  state_version BIGINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (reservation_member_id),
  UNIQUE KEY uq_living_active_member
    (character_guid, identity_nonce, active_slot),
  KEY ix_living_reservation_members
    (reservation_id, character_guid, identity_nonce)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
```

`active_slot=1` only for active memberships; terminal episodes set it `NULL`.
A rejoin creates a new membership row and increments `join_generation`, preserving
episode history while enforcing one active reservation per identity.

Application validation constrains enum strings, payload schema/version, and
payload size. New tables intentionally avoid foreign keys to legacy tables;
managed hooks and reconciliation enforce identity.

## A.2 Transition enforcement

The current database layer cannot provide interactive row locks through the
buffered transaction API. Therefore:

- all writes flow through the world-thread `LivingRealmStateWriter`;
- active-slot and version checks happen in the serialized writer;
- critical writes use direct commit;
- critical rows are re-read and verified by token/version;
- SQL uniqueness constraints are corruption/concurrency backstops;
- an unexpected mismatch indicates an external writer or invariant violation and
  fails closed.

No statement in this design relies on `SELECT ... FOR UPDATE` or affected-row
CAS.

## A.3 Migration protocol

Migrations are additive and ordered. A migration first directly records
`APPLYING`, performs schema/data work, validates the resulting schema, then
directly records `CLEAN`.

Startup accepts exactly the supported clean version. `APPLYING`, `FAILED`,
missing, or newer unsupported versions block managed startup. Roll-forward is
the default recovery; destructive automatic rollback is not provided.

## A.4 Deterministic startup matrix

| Persisted desire/state | Actual observation | Commitment/window | Result |
|---|---|---|---|
| desired offline | session online | no commitment | request wind-down; keep actual online until normal logout |
| desired offline | session online | protected commitment | remain online; defer wind-down |
| desired online | session absent | current window/commitment/request | login-eligible after all guards |
| desired online | session absent | window expired, no request | recompute desired offline; no login |
| desired online | session online | any valid | reconcile online and continue |
| stale login request | session absent | still eligible | issue new ephemeral attempt after backoff |
| stale login request | session online | any | discard request; reconcile online |
| stale logout request | session online | no commitment | resume/renew wind-down |
| stale logout request | session absent | any | discard; reconcile offline |
| missing schedule | any | identity valid | quarantine; do not synthesize in memory |
| malformed profile/current root | any | — | quarantine; if online, safe wind-down when legal |
| history row exists, current root absent | character absent | — | valid retired identity; no implicit recreation |
| history row exists, reused GUID has new root/nonce | character present | — | attach only to new nonce |
| character exists, current root absent | any | bootstrap not active | quarantine/global block according to scope |
| current root exists, character absent | session absent | — | retire orphan after managed-operation check |
| partial migration/global operation | any | — | global startup block |
| `AsyncBotLogin=false` | any managed identity | — | global managed-startup block |
| managed identity appears in legacy timer path | any | — | critical failure; remove from path and block until reconciled |
| persisted in instance/BG | session absent | stale location | classify through core rules; preserve/clear only explicitly |
| online in instance/BG | session online | schedule expired | defer logout until leave/normal completion |
| commitment row active | no live group/owner | grace expired | terminal release; schedule regains control |
| live real-player group | row missing | accepted policy | create/repair protected reservation through state writer |
| on-demand request active | session absent | TTL/capacity valid | login-eligible; not protected until group observed |
| on-demand request expired | session absent | no schedule window | terminalize request; remain offline |
| incomplete audit action | any | — | run action-specific reconciliation before eligibility |

## A.5 Per-character reconciliation order

1. validate current root/nonce/provenance and character fingerprint;
2. reconcile incomplete synthetic actions;
3. inspect actual session/player, live group, instance/BG, death,
   taxi/transport, trade, and persistence-submission state;
4. validate/repair request and protected reservation against live group;
5. validate schedule and compute desired state using UTC clock;
6. validate active goal slot;
7. derive wind-down/login eligibility;
8. publish an immutable reconciled snapshot.

Periodic reconciliation repeats bounded subsets and never depends solely on
events.

## A.6 Cleanup and retention

- current roots move to history before deletion/reuse;
- current-root history is retained at least as long as audit identity references;
- global reset/migration operations are retained under an explicit operation
  policy;
- expired requests, reservations, and goals become terminal before deletion;
- per-character audit retention follows 0002B;
- raw-reset damage is repaired only by a managed operation;
- no cleanup job acts on a `(guid, nonce)` pair without validating the current
  root and operation state.

## A.7 Concurrency and shutdown

Worker outputs contain identity nonce, schedule/goal generation, state version,
snapshot generation, and expiry and are discarded when stale. The state writer
serializes accepted transitions. Active-active writers require another design.

During shutdown:

1. stop producing selection/goal/audit proposals;
2. stop dispatching synthetic actions;
3. drain or terminalize the state-writer queue;
4. directly commit critical state;
5. unregister Living Realm callbacks;
6. allow normal core logout/save and database-delay-thread shutdown to continue.

No Living Realm write may occur after step 5.
