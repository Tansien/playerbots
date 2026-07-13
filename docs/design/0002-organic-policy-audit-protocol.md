# Living Realm 0002: synthetic-action audit protocol

[Back to design 0002](0002-organic-policy-and-audit.md)

## 1. Guarantee

A database transaction cannot include a live world mutation. The system therefore does **not** promise exactly-once world execution. It promises:

> Every successful synthetic mutation is eventually associated with one applied/reconciled audit action, and every incomplete request is deterministically reconciled after restart.

Action types are narrow and own an action-specific postcondition/reconciler.

## 2. Phases

- `REQUESTED`: durable intent, before fingerprint, expected postcondition, and unique action token exist.
- `APPLIED`: world-thread execution returned success and the observed postcondition was recorded.
- `FAILED`: validation/execution failed and no success postcondition is observed.
- `RECONCILED`: startup/periodic inspection resolved an ambiguous prior phase.
- `CANCELLED`: still-safe request became stale before execution.

Phase transitions are monotonic. Terminal rows are immutable except retention metadata.

## 3. Sequence

1. In one DB transaction, validate identity nonce/policy, generate `BINARY(16) action_token`, insert `REQUESTED`, and commit.
2. Queue an immutable command containing token, character identity, action kind, expected state version, before fingerprint, and bounded parameters.
3. On the world thread, reload the live player, revalidate identity, policy, commitment, state version, staleness, and action-specific preconditions.
4. If already at the expected postcondition, do not mutate; record `RECONCILED` with `already_satisfied`.
5. Otherwise execute at most once in this process and immediately capture observed postcondition. Save character state through the normal core path where required.
6. Persist `APPLIED` or `FAILED`. If this write fails, the row remains `REQUESTED`; later reconciliation inspects authoritative state.
7. Startup scans non-terminal/ambiguous rows before managed bot login selection.

Duplicate worker delivery and duplicate world dispatch are suppressed by the action token plus an in-process dispatch set, but correctness depends on postcondition reconciliation—not perfect deduplication.

## 4. Crash windows

| Window | Reconciliation |
|---|---|
| `REQUESTED` committed, command never ran | If before fingerprint still matches, requeue; otherwise cancel/fail |
| Mutation ran, outcome write lost | Inspect action-specific postcondition; mark reconciled-applied or quarantine if ambiguous |
| Mutation ran, character save uncertain | Compare durable character state and live state before login; never blindly replay |
| Duplicate command | If postcondition satisfied, reconcile without mutation |
| Stale retry after movement/state change | Cancel or fail with `STALE_PRECONDITION` |
| Process dies during apply | Action reconciler decides from observed state; ambiguity quarantines bot |

Teleport reconciliation records source map/position/fingerprint and destination region/radius. A bot observed at destination is reconciled applied; at unchanged source is retryable within expiry; elsewhere is ambiguous and quarantined. Synthetic revive (if later approved) must record death/corpse identity and expected alive state; ordinary core resurrection is preferred.

## 5. Schema example

```sql
CREATE TABLE ai_playerbot_living_audit_action (
  action_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  action_token BINARY(16) NOT NULL,
  character_guid INT UNSIGNED NOT NULL,
  identity_nonce BINARY(16) NOT NULL,
  action_type VARCHAR(64) NOT NULL,
  phase VARCHAR(16) NOT NULL,
  reason_code VARCHAR(64) NOT NULL,
  source_component VARCHAR(64) NOT NULL,
  requested_at_ms BIGINT UNSIGNED NOT NULL,
  applied_at_ms BIGINT UNSIGNED NULL,
  reconciled_at_ms BIGINT UNSIGNED NULL,
  expires_at_ms BIGINT UNSIGNED NOT NULL,
  state_version BIGINT UNSIGNED NOT NULL DEFAULT 0,
  before_payload MEDIUMTEXT NOT NULL,
  expected_after_payload MEDIUMTEXT NOT NULL,
  observed_after_payload MEDIUMTEXT NULL,
  failure_code VARCHAR(64) NULL,
  payload_version SMALLINT UNSIGNED NOT NULL,
  PRIMARY KEY (action_id),
  UNIQUE KEY uq_living_audit_token (action_token),
  KEY ix_living_audit_character (character_guid, identity_nonce, requested_at_ms),
  KEY ix_living_audit_phase (phase, requested_at_ms)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
```

Application validation limits each payload to 65,535 bytes and permits only known phase/action strings. Updates use `WHERE action_id=? AND phase=? AND state_version=?`, incrementing `state_version`. `APPLIED`, `FAILED`, `RECONCILED`, and `CANCELLED` are terminal.

## 6. Retention and cleanup

Operational retention is configurable after measurement; terminal rows MUST outlive the maximum reconciliation/backup window. Deleting a managed character retires identity state but does not immediately erase audit history. Population reset records a reset action and creates new nonces. Retention jobs operate only on terminal rows older than policy and never remove rows referenced by an investigation/quarantine marker.

## 7. Fault-injection tests

Inject failure after request insert, before dispatch, before mutation, after mutation, before character save, after save, before outcome update, during duplicate dispatch, during DB outage, and during startup reconciliation. Tests assert no blind replay, deterministic terminal phase, quarantine on ambiguity, and no mutation when the audit store is unavailable.
