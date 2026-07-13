# Living Realm 0003A: lifecycle schema and reconciliation

[Back to design 0003](0003-lifecycle-persistence-and-reconciliation.md)

## 1. Schema examples

```sql
CREATE TABLE ai_playerbot_living_schema (
  component VARCHAR(64) NOT NULL,
  schema_version INT UNSIGNED NOT NULL,
  migration_state VARCHAR(16) NOT NULL,
  updated_at_ms BIGINT UNSIGNED NOT NULL,
  PRIMARY KEY (component)
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
  retired_at_ms BIGINT UNSIGNED NULL,
  PRIMARY KEY (character_guid),
  UNIQUE KEY uq_living_identity (character_guid, identity_nonce),
  KEY ix_living_status (status)
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
  state_version BIGINT UNSIGNED NOT NULL DEFAULT 0,
  payload MEDIUMTEXT NOT NULL,
  PRIMARY KEY (reservation_id),
  UNIQUE KEY uq_living_reservation_lease (lease_token),
  KEY ix_living_reservation_state (state, expires_at_ms)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE ai_playerbot_living_reservation_member (
  reservation_id BIGINT UNSIGNED NOT NULL,
  character_guid INT UNSIGNED NOT NULL,
  identity_nonce BINARY(16) NOT NULL,
  member_role VARCHAR(24) NOT NULL,
  active_slot TINYINT UNSIGNED NULL,
  joined_at_ms BIGINT UNSIGNED NOT NULL,
  released_at_ms BIGINT UNSIGNED NULL,
  state_version BIGINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (reservation_id, character_guid, identity_nonce),
  UNIQUE KEY uq_living_active_member (character_guid, identity_nonce, active_slot)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
```

`active_slot=1` only for active memberships; terminal rows set it `NULL`, allowing history while enforcing one active reservation per identity. Application validation constrains enum strings, payload schema/version, and payload size. New tables intentionally avoid foreign keys to legacy tables; managed hooks and reconciliation enforce identity.

## 2. Migration protocol

Migrations are additive and ordered. Before applying, set component state to `APPLYING`; after schema/data validation, set `CLEAN`. Startup accepts exactly the supported clean version. `APPLYING`, `FAILED`, missing, or newer unsupported versions block managed startup. Roll-forward is the default recovery; destructive automatic rollback is not provided.

When Living Realm is disabled, missing tables are acceptable and no queries are issued. Enabling requires clean schema.

## 3. Deterministic startup matrix

| Persisted desire/state | Actual observation | Commitment/window | Result |
|---|---|---|---|
| desired offline | session online | no commitment | request wind-down; keep actual online until normal logout |
| desired offline | session online | protected commitment | remain online; defer wind-down |
| desired online | session absent | current window/commitment | login-eligible after all guards |
| desired online | session absent | window expired, no commitment | recompute desired offline; no login |
| desired online | session online | any valid | reconcile online and continue |
| stale login request | session absent | still eligible | issue new ephemeral attempt after backoff |
| stale login request | session online | any | discard request; reconcile online |
| stale logout request | session online | no commitment | resume/renew wind-down |
| stale logout request | session absent | any | discard; reconcile offline |
| missing schedule | any | identity valid | quarantine; do not synthesize in memory |
| malformed profile/root | any | — | quarantine; if online, safe wind-down when legal |
| deleted/retired character | session absent | — | terminal cleanup; never recreate implicitly |
| partial migration | any | — | global startup block |
| persisted in instance/BG | session absent | stale location | classify through core rules; preserve/clear only by explicit reconciliation |
| online in instance/BG | session online | schedule expired | defer logout until leave/normal completion |
| commitment row active | no live group/owner | grace expired | terminal release; schedule regains control |
| live real-player group | row missing | accepted policy | create/repair protected lease transactionally |

## 4. Per-character reconciliation order

1. validate root/nonce/provenance and character fingerprint;
2. reconcile incomplete synthetic actions;
3. inspect actual session/player, live group, instance/BG, death, taxi/transport, trade, and save state;
4. validate/repair commitment against live group;
5. validate schedule and compute desired state using UTC clock;
6. validate active goal slot;
7. derive wind-down/login eligibility;
8. publish an immutable reconciled snapshot to login selection.

Periodic reconciliation repeats bounded subsets and never depends solely on events.

## 5. Concurrency

All lifecycle/reservation updates use InnoDB transactions, `SELECT ... FOR UPDATE` for active slots, lease tokens, and `state_version` compare-and-swap. The architecture assumes one authoritative world process per realm; active-active writers require another design. Worker outputs contain identity nonce, schedule/goal generation, state version, and expiry and are discarded when stale.
