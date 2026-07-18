# Living Realm Phase 0: implementation map

Maps the **first Phase 0 tranche** and its tests to the requirements in
[0001A section A.7](0001-living-realm-implementation-contract.md). The Living
Realm feature itself is inert until enabled (with `AiPlayerbot.LivingRealm.Enabled = 0`
it only logs a report - it starts nothing, blocks nothing, and needs no schema).
This tranche is **not** purely additive, though: alongside the pure models,
seams, and tests it lands a disclosed set of **unconditional legacy correctness
fixes** that apply in both modes, plus one **mode-gated runtime change** -
role/LFG classification - that restores legacy behavior when the feature is
disabled. Both categories are inventoried under LR-001 below; the earlier claim
that "nothing here changes runtime bot behavior" was inaccurate and is corrected
there.

Phase 0 items from [0001 section 7](0001-living-realm.md) that are **not** in
this tranche and land with the next implementation PR: the single-writer
persistence seam and migration/version detection model, production telemetry
call sites for the existing observations (login/logout, group, travel, quest),
baseline CPU/database/login/travel/activity metrics, and the 0006 comparison
fixtures. Until then, "Phase 0 implemented" claims should be read as this map:
partial, with the remainder enumerated here.

## Components

| Component | Files |
|---|---|
| Host test target | `CMakeLists.txt` (`BUILD_PLAYERBOTS_LIVING_TESTS`, standalone configure, `playerbots_living_tests` CTest target) |
| Test harness | `playerbot/living/tests/LivingTest.h`, `playerbot/living/tests/LivingTestMain.cpp` |
| Action inventory | `playerbot/living/policy/OrganicActionKind.h`, `playerbot/living/policy/OrganicActionMetadata.h/.cpp` (one metadata row per kind in enum order, constexpr-checked; count grows with source audits per 0002A A.2) |
| Organic policy | `playerbot/living/policy/OrganicPolicy.h/.cpp` (`EvaluateOrganicPolicy`, pure and deterministic) |
| Provenance boundary | `playerbot/living/policy/BotProvenance.h/.cpp` |
| Config model and report | `playerbot/living/config/LivingRealmConfig.h/.cpp`, `playerbot/living/config/EffectiveConfigReport.h/.cpp` |
| Config keys | `playerbot/aiplayerbot.conf.dist.in`, `.in.tbc`, `.in.wotlk` (`AiPlayerbot.LivingRealm.Enabled/Profile/Strict`), parsed in `playerbot/PlayerbotAIConfig.cpp` (report-only) |
| Snapshots | `playerbot/living/snapshots/LivingSnapshots.h/.cpp` |
| Events | `playerbot/living/events/LivingEvents.h/.cpp` (15 stable names, no-op and ordered test sinks) |
| Determinism/fault seams | `playerbot/living/testing/LivingDeterminism.h/.cpp` (clocks, nonce/token providers, seeded random, named 0002B fault points) |
| Shared pure helpers | `playerbot/living/util/LivingNumericParse.h/.cpp` (non-throwing full-consumption `uint32`/slot parsing used by `ChatHelper`, `ChatFilter`, `ChooseTravelTargetAction`, and both `GlyphAction` slot branches), `playerbot/living/util/LivingQuestSlots.h` (quest-log occupancy/orphan rule plus the `QuestLogPreflight` cleanup-quarantine decision used by `CleanQuestLogAction` and `FreeQuestLogSlotValue`), `playerbot/living/util/LivingLinkGrammar.h` (final-link terminator and link-occurrence rules used by `GlyphAction`). These live under `living/` because they have no core dependency, which is what lets the host tests compile and regression-test the exact functions the runtime calls. |
| CI | `.github/workflows/cmangos-ubuntu-build.yml` (self-hosted Ubuntu only, fork-PR guard, pinned core baselines, host-test job) |

## Requirement traceability

### LR-001: disabled Living Realm preserves legacy behavior

- `EvaluateOrganicPolicy` returns a legacy passthrough for every action, known or
  unknown, when `livingRealmEnabled` is false; `BuildEffectiveConfigReport`
  emits a single informational entry and validates nothing when disabled.
- The config hook in `PlayerbotAIConfig.cpp` reads three keys and, when enabled,
  logs the report - it starts nothing, blocks nothing, and needs no schema.
- **Mode-gated runtime behavior (restored to legacy when disabled).** Runtime
  role/LFG classification: `AiFactory::GetPlayerRoles(Player*)` resolves ONE
  concrete role when enabled, but restores the legacy multi-bit mask when
  disabled through `living::RuntimeRoleForMode`, so IsTank/IsHeal, group
  composition and the dungeon finder are byte-for-byte legacy with the feature
  off. The only class/tab this gate affects is a WotLK Frost DK outside Frost
  Presence (legacy `TANK|DPS` vs the resolver's `DPS`); every other case already
  maps identically. Test: `roles_disabled_mode_restores_legacy_runtime_classification`.
- **Unconditional legacy correctness fixes (apply in BOTH modes, by design).**
  These are bug fixes, not feature behavior, so they are intentionally not gated:
  - event-write durability - full-row write confirmation, dirty typed-read
    gating, execution-confirmed persistence - in `RandomPlayerbotMgr`;
  - one-shot post-create markers (`create gear`/`levelup`/`test`) apply their
    runtime effect exactly once and retry only the durable clear, so an
    always-online bot no longer replays the mutation (e.g. `gear=empty`
    destroying items) every manager pass;
  - creation finalizer / group-batch lifecycle - cleanup-verify enqueue
    ownership, fixed-count bulk-fill loop termination, role-preserving batch
    accounting, replacement-budget growth on extension, a fresh batch after
    completion (a completed batch never blocks a new request), and deferred
    cleanup of test-creation tokens abandoned by a context reset (with a
    per-command transient-spawn-retry budget);
  - concrete creation-time role verification (a DPS Feral build can no longer
    satisfy or consume a tank quota) - the *creation* path, distinct from the
    gated *runtime* classifier above;
  - relocation finalization debt - a Finalizing relocation's owed homebind
    verify / marker clear / next-teleport schedule is retained across an
    ordinary logout and resumed on relogin (only an explicit removal
    force-cancels it), each effect run exactly once;
  - group-join target existence (COUNT-first, resilient to a database outage),
    with backoff/retry state advanced only after its persistence is confirmed;
  - the `.bot always` durable-write boundary;
  - command correctness - `@quest=<selector>` consumes only the leading selector
    and preserves trailing operands; `change_strategy <bot> <strategy>` validates
    and applies the named strategy instead of choosing one at random;
  - input parsing / overflow safety (numeric, chat-link, money and quest-link
    IDs; non-throwing);
  - relocation staging and auction bid math.
- Tests: `policy_disabled_realm_preserves_legacy_behavior`,
  `config_disabled_realm_validates_nothing_and_needs_no_schema`,
  `config_unknown_profile_blocks_only_when_enabled`,
  `roles_disabled_mode_restores_legacy_runtime_classification`.

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
- Successive Phase 0 source audits have added rows beyond the reviewed 0002A
  seed, each recorded in 0002A per its A.2 rule (account-transfer hire, instant
  repop, BG regroup teleport, direct item split/ownership/destruction, legacy
  direct mail, direct auction mutation, debug mutation commands, minimal and
  unobserved move teleports, direct summon, death-recovery teleport, direct
  spirit-healer resurrection, RTSC spell grant, command character provisioning,
  guild-bank tab mutation, remote quest accept, direct quest abandon, free pet
  happiness, direct aura removal, encounter aura mutation, login item
  fabrication, free talent respec, direct glyph mutation, plus the positive
  `GAMEPLAY_QUEST_ACCEPT` counterpart). Per A.2 the inventory is a growing seed;
  proving completeness is a 0.1 acceptance gate, not a Phase 0 claim. The count
  is deliberately not restated here so it cannot drift again - the enum and its
  constexpr-checked metadata table are the source of truth.
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
  `policy_bootstrap_creation_is_pre_identity_and_factory_bound`,
  `policy_stuck_teleport_requires_every_recovery_gate`,
  `policy_transport_group_sync_requires_every_gate`,
  `policy_public_transport_requires_every_gate`,
  `policy_only_three_actions_can_ever_require_audit`,
  `policy_every_classification_maps_to_its_decision`,
  `policy_identity_root_must_be_bindable`,
  `policy_legal_automation_is_distinguishable_from_gameplay`.

### LR-006 groundwork: single-writer/direct-durability seams

- Phase 0 supplies the deterministic inputs the future `LivingRealmStateWriter`
  needs: injectable UTC/monotonic clocks, unique 16-byte nonce/token
  sequences, and the named 0002B B.10 fault-injection points
  (`LivingFaultPoints`). No writer or database code exists yet by design.
- Immutable snapshot types with per-dimension **character-state** staleness
  (identity nonce, state version, schedule/goal/snapshot generation, queue
  attempt state, group/commitment summary, location, expiry) support the
  proposal revalidation of 0003A A.7 without touching `PlayerbotLoginMgr`. The
  helper is named `IsCharacterStateStale` because it is deliberately narrower
  than 0005A A.1: role, recent-service, estimated-cost, capacity, and policy
  inputs are not modelled here (the 0.2 Director owns them) and callers must
  revalidate capacity/policy separately.
- Tests: `determinism_test_clock_is_deterministic`,
  `determinism_token_sequences_are_reproducible_and_unique`,
  `determinism_seeded_random_is_reproducible_and_bounded`,
  `determinism_fault_injector_fires_exactly_where_armed`,
  `snapshot_equality_covers_every_field`,
  `snapshot_character_state_staleness_is_detected_per_dimension`,
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
- CI builds Classic, TBC, and WotLK on self-hosted Ubuntu 26.04 runners
  (labels `[self-hosted, linux, playerbots]`) against the pinned 0001A
  baselines and runs the host tests; macOS/Windows jobs and Discord
  notifications are removed, satisfying the three-expansion rule in the design
  README.
- Tests: the whole suite plus the three-core build matrix;
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

## Tracked 0.1 acceptance blockers (runtime work owed on classified paths)

Classification is Phase 0's deliverable; these classified paths additionally
need runtime changes before 0.1 acceptance, and are tracked here as mandatory
blockers per 0002A A.2:

- `LEGACY_DIRECT_MAIL_MUTATION`: make `CheckMailAction` deletion conditional on
  every money/attachment operation succeeding (atomic with processing), or
  route mail through the core handlers.
- `DEBUG_MUTATION_COMMAND`: gate `cdebug` mutation subcommands behind
  moderator/fixture authorization; keep read-only diagnostics separate.
- `BROAD_MAINTENANCE_COMMAND` wildcard targeting: preflight that every `%`
  target is a FIXTURE identity before any mutation (per-identity policy
  evaluation already denies non-fixture targets).
- `UNOBSERVED_MOVE_TELEPORT` / `MINIMAL_MOVE_TELEPORT` /
  `DEATH_RECOVERY_TELEPORT` / `DIRECT_SUMMON_TELEPORT` /
  `BG_REGROUP_TELEPORT`: guard the direct `TeleportTo` call sites for managed
  identities.
- `DIRECT_AUCTION_MUTATION` / `DIRECT_ITEM_*_TRANSFER`: route through the core
  session handlers or deny at the call site.
- `COMMAND_CHARACTER_PROVISION`: the existing `PlayerbotHolder::CreateBot`
  compound is denied outright, including in the fixture profile - it creates and
  grants before the first save, so it cannot satisfy the identity boundary.
  Fixtures need a dedicated wrapper that runs the explicit sequence: authorize
  `FIXTURE_CHARACTER_CREATE`, create through the core, commit the
  `(guid, nonce, FIXTURE)` root, then authorize `FIXTURE_PROVISION` grants.
  No placeholder persistence exists in Phase 0; the wrapper is 0.1 work.
- `GUILD_BANK_TAB_MUTATION` / `REMOTE_QUEST_ACCEPT` / `WORLD_BUFF_APPLY`: guard
  the guild-bank, hardcoded-quest, and world-buff call sites for managed
  identities.
- `DIRECT_QUEST_ABANDON`: route quest abandonment through the core abandon
  handler (item cleanup included) instead of direct `DropQuest` pruning. The
  guard belongs at the shared `PlayerbotAI::DropQuest` boundary, which
  `CleanQuestLogAction`, `DropQuestAction`, `QuestUpdateFailedTimerAction`, and
  `GuildAcceptQuestOrderAction` all reach.
- `GAMEPLAY_QUEST_ACCEPT` vs `QUEST_SYNC_TO_BOT`: both live inside
  `QuestAction::AcceptQuest` (the core handler call, then the
  `syncQuestWithPlayer` `AddQuest` fallback). The guard must bind to those two
  branches individually; an action-entry allow would expose the denied sync.
- `DIRECT_AURA_REMOVAL`: restrict the `ra` command to core-validated
  cancellation of positive, cancelable auras.
- `DIRECT_ITEM_DESTRUCTION` / `FREE_PET_HAPPINESS`: use the core destroy-item
  handler and the normal feed-pet item/spell path.
- `ENCOUNTER_AURA_MUTATION` / `LOGIN_ITEM_FABRICATION` / `FREE_TALENT_RESPEC` /
  `DIRECT_GLYPH_MUTATION`: guard the Netherspite aura shortcut, login
  starter-item creation, talent respec, and glyph application call sites.
- `DIRECT_QUEST_ABANDON` guard belongs at the shared `PlayerbotAI::DropQuest`
  boundary so the pruning, failed-timer, and guild-order callers are all
  covered.
- In-world regression scenarios for paths the host tests cannot construct
  (no core `Player`/`WorldSession`/`Group`/DB objects): quest-cleanup
  accounting and orphan quarantine (orphan-plus-loot, save/reload), the
  taxi-money restoration fix, the `@quest`/travel chat entry points with
  malformed and overflowing IDs, solo random-teleport success/failure and
  grouped-bot denial, and the glyph command grammar. Use the
  `playerbot/strategy/tests` DSL on a test realm.
- `TEMP_MONEY_TRICK` (**data-integrity hazard, not just a shortcut**): the
  temporary `SetMoney` grant used by the RPG/world-buff flight helpers and the
  `taxi`/`gold` cheat fare runs `MoneyChanged` synchronously, which can
  permanently auto-reward a money-required quest (`CompleteQuest` ->
  `RewardQuest` for `QUEST_FLAGS_AUTO_REWARDED`) and advance WotLK
  highest-gold achievements. Restoring the balance cannot undo either. The
  guard must remove the temporary grant at the shared boundary (real fare or
  no flight), not merely restore the number.
- `RANDOM_TELEPORT`/`RPG_CAMP_TELEPORT` compound with
  `RandomPlayerbotMgr::Refresh`, which resurrects, repairs, refills resources,
  creates consumables, and grants money after the teleport: guard the shared
  refresh boundary, not only the teleport call.
- `FIXTURE_PROVISION` currently names the host-test guard; the live in-world
  scenario DSL (`GenerateBotTests`) can also mutate items, quests, creatures,
  GM state, and group-member locations. 0.1 wiring must require the
  non-production profile and FIXTURE provenance for every affected target,
  including real group members.
- `SHARED_RESPAWN_ACCELERATION`: guard `PlayerbotAI::AccelerateRespawn` once at
  the shared boundary (both callers: `XpGainAction` kill credit and
  `LootAction` release). `AiPlayerbot.RespawnModHostile`/`RespawnModNeutral`
  ship nonzero, so this shared-world mutation is on by default; it changes
  respawn delay and can remove a corpse, which real players share.
- Orphaned quest slots: implement a core-backed ATOMIC repair (slot + quest
  status map/DB + source items + timers) before any automated cleanup may free
  one. Phase 0 quarantines instead: the orphan is counted as occupied, logged,
  and left untouched, and automated cleanup fails rather than dropping valid
  quests. Clearing only the slot is NOT an acceptable repair - it leaves a
  status the character can never re-accept if the template returns.
- Population inspection: implement the database check that moves
  `PopulationInspection` off `NotInspected`.

## Explicitly outside Phase 0 (any tranche)

No production SQL or schema, no `LivingRealmStateWriter` implementation, no
synthetic-action execution, no legacy-behavior suppression, no
schedules/goals/commitments, no login-manager changes, no overlays, no
Population Director, and no fixture account/character creation. (Deferred
*Phase 0* items are listed at the top of this document; this section lists
work that belongs to Living Realm 0.1 and later.) See the pull request body
for the full non-goal list.
