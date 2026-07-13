# Living Realm 0002B: synthetic-action audit protocol

[Back to design 0002](0002-organic-policy-and-audit.md)

## B.1 Guarantee

A database transaction cannot include a live world mutation. The system does
**not** promise exactly-once world execution. It promises:

> Every successful synthetic mutation is eventually associated with one
> `APPLIED` or reconciled-applied audit action, and every incomplete request is
> deterministically reconciled after restart.

Action types are narrow and own an action-specific postcondition and reconciler.
0.1 action definitions live in 0002C.

## B.2 Database prerequisite

All audit transitions use the single-writer model in 0001A:

- no `SELECT ... FOR UPDATE`;
- no affected-row CAS assumption;
- no dependency on buffered async commits for pre-mutation durability;
- `REQUESTED` and terminal audit writes use direct synchronous commit;
- critical rows are synchronously re-read and verified before dependent work;
- `state_version` and tokens reject stale proposals and detect external writes.

If direct commit or verification fails, the world mutation MUST NOT run.

## B.3 State fingerprints

A `StateFingerprint` is a versioned canonical JSON object whose field set is
defined by the action type. Equality is semantic field equality after canonical
serialization; a cryptographic hash is optional and not authoritative.

All fingerprints include:

```text
fingerprint_version
action_type
character_guid
identity_nonce
observed_at_ms
character_state_version
map_id
instance_id
position
transport_context
death_state
group_context
```

Each action adds only the fields needed to decide whether its postcondition is
satisfied, unchanged, failed, or ambiguous. It never stores live pointers,
process addresses, or monotonic timestamps.

## B.4 Phases

- `REQUESTED`: durable intent, before fingerprint, expected postcondition, and
  unique action token exist.
- `APPLIED`: world-thread execution returned success and the observed
  postcondition was recorded.
- `FAILED`: validation or execution failed and no success postcondition is
  observed.
- `RECONCILED`: startup/periodic inspection resolved an ambiguous prior phase.
  `outcome_code` records `APPLIED`, `NOT_APPLIED`, or `AMBIGUOUS_QUARANTINED`.
- `CANCELLED`: a still-safe request became stale before execution.

Phase transitions are monotonic. Terminal rows are immutable except explicit
retention-hold metadata.

## B.5 Sequence

1. The world-thread state writer serializes the request for the identity.
2. It synchronously reads root, policy, and relevant state, then validates the
   identity nonce and action-specific preconditions.
3. It generates `BINARY(16) action_token`, buffers the `REQUESTED` insert and any
   same-domain state update, and executes them through direct commit.
4. It synchronously re-reads the audit row and verifies token, phase,
   fingerprint version, and expected `state_version`.
5. Only after verification, it queues an immutable world command containing the
   token, character identity, expected versions, expiry, and bounded parameters.
6. On the world thread, the executor reloads the live player and revalidates
   identity, policy, commitment, staleness, and action-specific preconditions.
7. If the expected postcondition already holds, it does not mutate and writes
   `RECONCILED` with `already_satisfied`.
8. Otherwise it executes once in this process, captures the observed
   postcondition immediately, and submits normal character persistence where
   required. Submission is not claimed as durable completion.
9. The writer directly commits `APPLIED` or `FAILED` and re-reads the terminal
   row. If this fails, the row remains `REQUESTED`; later reconciliation inspects
   authoritative live and durable character state.
10. Startup scans incomplete/ambiguous rows before managed login selection.

Duplicate worker delivery and duplicate world dispatch are suppressed by the
action token plus an in-process dispatch set, but correctness depends on
postcondition reconciliation, not perfect deduplication.

## B.6 Crash windows

| Window | Reconciliation |
|---|---|
| Direct `REQUESTED` commit failed | No mutation was dispatched; request is absent/failed |
| `REQUESTED` committed, command never ran | If before fingerprint still matches and not expired, requeue; otherwise cancel/fail |
| Mutation ran, outcome write lost | Inspect action-specific postcondition; mark reconciled-applied or quarantine if ambiguous |
| Mutation ran, character save only submitted | Compare durable character row, transport fields, and live state before any replay |
| Character save became durable, terminal audit did not | Reconciler marks applied from durable postcondition |
| Duplicate command | If postcondition satisfied, reconcile without mutation |
| Stale retry after movement/state change | Cancel/fail with `STALE_PRECONDITION` |
| Process dies during apply | Action reconciler decides from observed state; ambiguity quarantines |
| Core shutdown begins | Stop dispatch, drain writer, direct-flush critical transitions before DB delay threads halt |

No generic teleport-region rule applies to every action. Public transport,
transport-group sync, and stuck recovery have different fingerprints and
postconditions in 0002C.

## B.7 Schema example

```sql
CREATE TABLE ai_playerbot_living_audit_action (
  action_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  action_token BINARY(16) NOT NULL,
  character_guid INT UNSIGNED NOT NULL,
  identity_nonce BINARY(16) NOT NULL,
  action_type VARCHAR(64) NOT NULL,
  phase VARCHAR(16) NOT NULL,
  outcome_code VARCHAR(32) NULL,
  request_reason_code VARCHAR(64) NOT NULL,
  failure_code VARCHAR(64) NULL,
  source_component VARCHAR(64) NOT NULL,
  fingerprint_version SMALLINT UNSIGNED NOT NULL,
  requested_at_ms BIGINT UNSIGNED NOT NULL,
  applied_at_ms BIGINT UNSIGNED NULL,
  reconciled_at_ms BIGINT UNSIGNED NULL,
  terminal_at_ms BIGINT UNSIGNED NULL,
  expires_at_ms BIGINT UNSIGNED NOT NULL,
  state_version BIGINT UNSIGNED NOT NULL DEFAULT 0,
  before_payload MEDIUMTEXT NOT NULL,
  expected_after_payload MEDIUMTEXT NOT NULL,
  observed_after_payload MEDIUMTEXT NULL,
  payload_version SMALLINT UNSIGNED NOT NULL,
  retention_hold TINYINT UNSIGNED NOT NULL DEFAULT 0,
  retention_reason VARCHAR(128) NULL,
  PRIMARY KEY (action_id),
  UNIQUE KEY uq_living_audit_token (action_token),
  KEY ix_living_audit_character
    (character_guid, identity_nonce, requested_at_ms),
  KEY ix_living_audit_phase (phase, requested_at_ms),
  KEY ix_living_audit_retention
    (retention_hold, terminal_at_ms)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
```

Application validation limits each payload to 65,535 bytes and permits only known
phase/action/outcome strings. The single writer verifies an expected
`state_version`, directly commits the transition, increments the version, and
re-reads the row. It does not claim database-level CAS based on affected-row
counts.

`APPLIED`, `FAILED`, `RECONCILED`, and `CANCELLED` are terminal and set
`terminal_at_ms`.

## B.8 Global operations

Population bootstrap/reset and schema migration are not represented by a
sentinel character audit row. They use the global operation table in 0003A.
Per-character retirement and action history remain linked to the original
identity nonce.

## B.9 Retention and cleanup

Terminal rows MUST outlive the maximum reconciliation, backup, and operational
investigation window. Retention defaults are chosen after Phase 0 measurements.
Deleting a managed character moves its root to identity history but does not
immediately erase audit history.

Retention jobs operate only on terminal rows older than policy and skip
`retention_hold=1`. Holds have an explicit reason and operator-managed release.

## B.10 Fault-injection tests

Inject failure after initial read, before direct commit, after direct commit
before verification, before dispatch, before mutation, after mutation, before
save submission, after save submission, before terminal commit, after terminal
commit before verification, during duplicate dispatch, during DB outage, and
during startup reconciliation.

Tests assert: no mutation before durable verified `REQUESTED`; no blind replay;
deterministic terminal phase; quarantine on genuine ambiguity; and no mutation
when the audit store is unavailable.
