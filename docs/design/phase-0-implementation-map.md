# Living Realm Phase 0: implementation map

Maps the Phase 0 implementation and its tests to the requirements in
[0001A section A.7](0001-living-realm-implementation-contract.md). Phase 0 is
foundations only: pure models, seams, and tests. Nothing here executes synthetic
actions, blocks startup, touches the database, or changes runtime bot behavior.

## Components

| Component | Files |
|---|---|
| Host test target | `CMakeLists.txt` (`BUILD_PLAYERBOTS_LIVING_TESTS`, standalone configure, `playerbots_living_tests` CTest target) |
| Test harness | `playerbot/living/tests/LivingTest.h`, `playerbot/living/tests/LivingTestMain.cpp` |
| Action inventory | `playerbot/living/policy/OrganicActionKind.h`, `playerbot/living/policy/OrganicActionMetadata.h/.cpp` (85 kinds, one metadata row each, constexpr completeness check) |
| Organic policy | `playerbot/living/policy/OrganicPolicy.h/.cpp` (`EvaluateOrganicPolicy`, pure and deterministic) |
| Provenance boundary | `playerbot/living/policy/BotProvenance.h/.cpp` |
| Config model and report | `playerbot/living/config/LivingRealmConfig.h/.cpp`, `playerbot/living/config/EffectiveConfigReport.h/.cpp` |
| Config keys | `playerbot/aiplayerbot.conf.dist.in`, `.in.tbc`, `.in.wotlk` (`AiPlayerbot.LivingRealm.Enabled/Profile/Strict`), parsed in `playerbot/PlayerbotAIConfig.cpp` (report-only) |
| Snapshots | `playerbot/living/snapshots/LivingSnapshots.h/.cpp` |
| Events | `playerbot/living/events/LivingEvents.h/.cpp` (15 stable names, no-op and ordered test sinks) |
| Determinism/fault seams | `playerbot/living/testing/LivingDeterminism.h/.cpp` (clocks, nonce/token providers, seeded random, named 0002B fault points) |
| CI | `.github/workflows/cmangos-ubuntu-build.yml` (self-hosted Ubuntu only, fork-PR guard, pinned core baselines, host-test job) |

## Requirement traceability

### LR-001: disabled Living Realm preserves legacy behavior

- `EvaluateOrganicPolicy` returns a legacy passthrough for every action, known or
  unknown, when `livingRealmEnabled` is false; `BuildEffectiveConfigReport`
  emits a single informational entry and validates nothing when disabled.
- Enabling nothing: the config hook in `PlayerbotAIConfig.cpp` only reads three
  keys and, when enabled, logs the report. No schema, query, or behavior change
  exists in disabled mode.
- Tests: `policy_disabled_realm_preserves_legacy_behavior`,
  `config_disabled_realm_validates_nothing_and_needs_no_schema`,
  `config_unknown_profile_blocks_only_when_enabled`.

### LR-003: Organic config and unknown actions fail closed

- Unknown `OrganicActionKind` values deny with `UNKNOWN_ACTION`; unsupported
  profiles deny everything; the report model represents unknown profile,
  strict conflicts, runtime overrides, `AsyncBotLogin=0`, and the missing
  Living Realm schema as structured entries with stable reason codes.
- Tests: `policy_unknown_actions_fail_closed_when_enabled`,
  `policy_unsupported_profile_fails_closed`,
  `config_report_represents_every_required_conflict`,
  `config_strict_mode_conflicts_are_deterministic`,
  `config_report_output_is_stable`,
  `config_valid_organic_configuration_has_no_blocking_entries`,
  `config_templates_declare_matching_living_realm_defaults`.

### LR-004: every known fabrication path is classified and guarded

- All 0002A matrix rows and cheat categories are explicit `OrganicActionKind`
  values with metadata (name, category, legacy source, classification,
  reconciler requirement, production eligibility, fixture-only status, design
  reference). A constexpr check fails compilation on a kind without a
  consistent row; a generic misc/unknown kind does not exist.
- Only `STUCK_EMERGENCY_TELEPORT`, `TRANSPORT_GROUP_SYNC`, and
  `PUBLIC_TRANSPORT_TRANSFER` may return `RequireAudit`, each gated on explicit
  pure-data context (classification only; execution is later work).
- Tests: `inventory_every_kind_has_metadata`,
  `inventory_stable_names_unique_and_resolvable`,
  `inventory_only_approved_actions_require_audit_and_name_reconcilers`,
  `inventory_represents_all_13_cheat_categories`,
  `inventory_covers_required_shortcut_families`,
  `inventory_fixture_only_rows_are_never_production_eligible`,
  `policy_fabrication_families_are_denied`, `policy_all_13_cheat_bits_are_denied`,
  `policy_broad_maintenance_and_lifecycle_shortcuts_are_denied`,
  `policy_bootstrap_creation_requires_active_managed_bootstrap`,
  `policy_stuck_teleport_requires_exhausted_ladder_and_owner_consent`,
  `policy_transport_group_sync_requires_protected_commitment`,
  `policy_public_transport_requires_allowlisted_route`,
  `policy_only_three_actions_can_ever_require_audit`,
  `policy_legal_automation_is_distinguishable_from_gameplay`.

### LR-006 groundwork: single-writer/direct-durability seams

- Phase 0 supplies the deterministic inputs the future `LivingRealmStateWriter`
  needs: injectable UTC/monotonic clocks, unique 16-byte nonce/token
  sequences, and the named 0002B B.10 fault-injection points
  (`LivingFaultPoints`). No writer or database code exists yet by design.
- Immutable snapshot types with per-dimension staleness (identity nonce, state
  version, schedule/goal/snapshot generation, expiry) implement the proposal
  revalidation contract of 0003A A.7 without touching `PlayerbotLoginMgr`.
- Tests: `determinism_test_clock_is_deterministic`,
  `determinism_token_sequences_are_reproducible_and_unique`,
  `determinism_seeded_random_is_reproducible_and_bounded`,
  `determinism_fault_injector_fires_exactly_where_armed`,
  `snapshot_equality_covers_every_field`,
  `snapshot_staleness_is_detected_per_dimension`,
  `snapshot_same_guid_different_nonce_is_a_different_identity`.

### LR-010 groundwork: managed bots cannot use legacy login rotation

- `LEGACY_LOGIN_ROTATION` and `LEGACY_TIMED_ROTATION` are inventoried and deny
  with the stable reason `LEGACY_LIFECYCLE_EXCLUDED` in enabled Organic mode;
  `AiPlayerbot.AsyncBotLogin = 0` is modeled as a blocking 0.1 prerequisite in
  the effective-config report. Runtime login-path changes are 0.1 work.
- Tests: `policy_broad_maintenance_and_lifecycle_shortcuts_are_denied`,
  `config_report_represents_every_required_conflict`.

### LR-014: build, test, and expansion compatibility remain explicit

- `BUILD_PLAYERBOTS_LIVING_TESTS` (default OFF) builds the host-side
  `playerbots_living_tests` CTest executable; standalone configure needs no
  core, no database, and no live game objects. Every `living/` directory is an
  explicit CMake source group; the existing in-world test DSL under
  `playerbot/strategy/tests` is untouched.
- CI builds Classic (the deployment target) on self-hosted Ubuntu 26.04 runners
  against the pinned 0001A baseline and runs the host tests; macOS/Windows jobs
  and Discord notifications are removed. TBC/WotLK are not built in CI for now,
  but their expansion guards remain in the source and all three expansions were
  compile-verified on Ubuntu when Phase 0 landed.
- Tests: the whole suite (46 tests) plus the Classic core build;
  `config_templates_declare_matching_living_realm_defaults` keeps the three
  expansion templates aligned.

### Fixture-bot provenance boundary (0001 invariant 13, 0006 section 7)

- `BotProvenance` (`ORGANIC_CREATED`, `FIXTURE`, `LEGACY_UNMANAGED`) with
  immutable-per-identity transitions; fixture-only actions
  (`FIXTURE_PROVISION`, `CHEAT_RUNTIME_OVERRIDE`, `BROAD_MAINTENANCE_COMMAND`)
  require the fixture test profile and `FIXTURE` provenance. No fixture
  accounts or characters are created in Phase 0.
- Tests: `provenance_fixture_cannot_satisfy_organic_provenance`,
  `provenance_transitions_never_relabel_identities`,
  `provenance_fixture_actions_denied_outside_test_profile`,
  `provenance_fixture_identity_cannot_enter_production_semantics`,
  `provenance_legacy_unmanaged_cannot_use_managed_organic_semantics`,
  `provenance_names_are_stable`.

### Event vocabulary (0002 section 5)

- Fifteen stable event names covering login/logout, schedule/goal transitions,
  group join/leave, travel, quest accept/reward, and the five synthetic-action
  phases; no-op production sink and ordered deterministic test sink. Events are
  telemetry, never lifecycle authority.
- Tests: `events_names_are_stable`, `events_test_sink_preserves_order`,
  `events_noop_sink_has_no_observable_effect`.

## Explicitly not in Phase 0

No production SQL or schema, no `LivingRealmStateWriter`, no synthetic-action
execution, no legacy-behavior suppression, no schedules/goals/commitments, no
login-manager changes, no overlays, no Population Director, and no fixture
account/character creation. See the pull request body for the full non-goal
list.
