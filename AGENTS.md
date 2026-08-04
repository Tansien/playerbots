# Playerbots Agent Guide

## Start here

- Read `README.md` and `docs/design/README.md`, then the current design for the subsystem you are changing. Use `docs/design/phase-0-implementation-map.md` to locate shipped Phase 0 coverage, not as architectural authority.
- Design documents distinguish **Existing** from **Proposed** behavior. Code, SQL, configuration templates, and tests describe what is shipped.
- Trace the complete live path before editing: config, manager or caller, world-thread action, core handler, persistence, and relevant tests.
- For core-dependent behavior, inspect the matching pinned CMaNGOS Classic, TBC, and WotLK sources instead of guessing from one expansion.
- Put durable decisions in the relevant existing design document. Respect each document's status in `docs/design/README.md`; informative references and future proposals are not current requirements. Do not add PR handoff notes or duplicate plans.

## Project stage

This repository is the Playerbots module for CMaNGOS Classic, TBC, and WotLK. Existing Playerbots behavior remains supported while Living Realm is implemented incrementally.

Classic behavior is the first acceptance target, but all three expansion builds must remain green. Put expansion-specific identifiers and behavior behind `MANGOSBOT_ZERO`, `MANGOSBOT_ONE`, or `MANGOSBOT_TWO` at the narrowest boundary.

Living Realm is opt-in and off by default (`AiPlayerbot.LivingRealm.Enabled = 0`); no Living Realm schema or runtime path may be required when it is disabled. Do not turn a proposed design into runtime behavior outside the requested implementation scope.

## Architectural direction

Existing Playerbots remains the moment-to-moment execution engine. Living Realm adds durable identity, schedules, goals, policy, and owned strategy composition; it does not replace the strategy engine or CMaNGOS world rules.

Live CMaNGOS player, session, group, quest, inventory, and world state is authoritative. Persisted Living Realm state records intent, ownership, and history and must be reconciled against live state.

Critical Living Realm transitions have one world-thread-owned in-process writer. Workers consume immutable snapshots and return generation-checked proposals; they do not access live game objects or write authoritative state.

Use ordinary CMaNGOS handlers and gameplay rules whenever they cover the operation.

Move toward this architecture incrementally. Reuse the current managers, strategies, handlers, and persistence seams instead of creating parallel planners, lifecycle systems, or state representations.

## State ownership and change scope

- Before changing durable state, identify its authority in `docs/design/0001-living-realm.md` and the relevant child design.
- Events, queue states, cached database flags, and telemetry are observations, not authority by themselves.
- Preserve the single-writer boundary and the documented ordering between durable intent and world mutation.
- Files in `PLAYERBOTS_LIVING_SRCS` must remain independent of CMaNGOS headers and live game objects so the standalone test target builds. Adapt to core types outside that source list at the narrow runtime boundary.
- Keep a change focused on one ownership boundary. Architectural prerequisites in another boundary should normally land separately.
- Classify review findings as:
  1. introduced by the change;
  2. required prerequisites for correctness;
  3. adjacent pre-existing issues.

Category 3 normally becomes follow-up work instead of expanding the current change.

## Non-negotiable boundaries

- Treat the invariants in `docs/design/0001-living-realm.md` section 3 and the relevant child design as authoritative; do not restate, weaken, or silently bypass them in code.
- Preserve enabled Organic fail-closed behavior. New synthetic mutation paths require an explicit policy classification and the documented audit/reconciliation boundary.
- Never infer CMaNGOS database, transaction, handler, identifier, or expansion behavior. Verify it in the pinned core baseline and record a baseline update when correctness depends on newer core behavior.
- Do not use live realm data in tests or run destructive database/reset commands without explicit confirmation and a verified backup.

## Repository conventions

- `playerbot/`: managers, configuration, lifecycle, commands, and shared runtime behavior.
- `playerbot/strategy/`: strategies, actions, triggers, values, class behavior, and the existing in-world scenario-test DSL.
- `playerbot/living/`: pure Living Realm config, policy, events, snapshots, deterministic seams, utilities, and host-side tests.
- `ahbot/`: Auction House bot behavior.
- `sql/characters/`: CharacterDatabase schema/data used by Playerbots.
- `sql/world/`: shared and expansion-specific WorldDatabase data.
- `playerbot/aiplayerbot.conf.dist.in*`: Classic, TBC, and WotLK configuration templates.
- `docs/design/`: Living Realm designs, contracts, implementation maps, and informative references; authority depends on the status and precedence recorded in `docs/design/README.md`.

Keep managers and actions thin when an existing shared helper is the real ownership point. Fix shared root causes once instead of adding guards to individual callers.

Reuse existing helpers, core facilities, and standard C++ before adding abstractions or dependencies. Avoid speculative frameworks and duplicate representations of state.

Match the surrounding C++ style. Validate untrusted command/config input without throwing, guard narrowing and overflow, and keep deterministic code independent of wall-clock time and ambient randomness.

Every new non-test, core-independent `playerbot/living/` source directory must be added explicitly to `PLAYERBOTS_LIVING_SRCS` in `CMakeLists.txt`; that single list feeds both the module and standalone host-test target. Test-only sources stay in the separate test glob.

Add the smallest regression test that proves each non-trivial behavior change:

- pure policy, parsing, schema-transition, snapshot, and deterministic behavior belongs in `playerbot/living/tests/`;
- scenarios requiring maps, quests, groups, travel, combat, or live core objects belong in `playerbot/strategy/tests/` and require an in-world test run;
- core-dependent manager behavior should use the in-world harness when practical. Otherwise document targeted manual verification; the CI matrix proves compilation and installation, not runtime behavior.

## Configuration and SQL

- Keep shared configuration keys and defaults synchronized across Classic, TBC, and WotLK templates unless the difference is deliberately expansion-specific.
- Configuration reports must show configured and effective behavior; never rewrite the operator's config silently.
- Follow the database model and supported API constraints in `docs/design/0001-living-realm-implementation-contract.md` sections A.2 and A.3. Do not introduce SQL or transaction semantics that those sections explicitly reject.
- Character identity and Living Realm durable state belong in CharacterDatabase as specified by the design. World data belongs under the correct shared or expansion-specific `sql/world/` path.
- Keep schema SQL, code, tests, config, and the relevant design/implementation map synchronized as one coherent change.
- Prefer forward-compatible SQL changes. Clearly state any required manual migration or reset; never apply it to a remote or live realm as part of routine verification.

## CI and verification

Run the narrowest relevant checks while iterating. For pure Living Realm code, the standalone gate is:

```bash
cmake -DBUILD_PLAYERBOTS_LIVING_TESTS=ON -DCMAKE_BUILD_TYPE=Release -B build -S .
cmake --build build --parallel
ctest --test-dir build --output-on-failure
git diff --check
```

Changes touching core-dependent runtime code, CMake integration, SQL, expansion-specific identifiers, or configuration also require the self-hosted CI matrix against the pinned CMaNGOS Classic, TBC, and WotLK baselines.

The workflow intentionally runs only trusted push refs and manual dispatches on `[self-hosted, linux, playerbots]` runners. Do not add a `pull_request` trigger or move jobs to GitHub-hosted runners without revisiting the documented trust boundary in `.github/workflows/cmangos-ubuntu-build.yml`.

## Commits and cross-model review

When commits are requested, keep each bugfix or feature in one focused commit and split unrelated work.

Before pushing, self-review the commit's own diff, run the applicable local checks, then ask the other model to review it. Fix every real category 1 or 2 finding, amend, and repeat until clean. Category 3 may be recorded as follow-up work. After pushing, require the three-expansion CI matrix to pass before merge when applicable.

Claude calls Codex:

```bash
codex exec review --commit HEAD -m gpt-5.6-sol -c model_reasoning_effort=high
```

Codex calls Claude:

```bash
claude -p --model claude-opus-5 --effort high "Review the changes in commit HEAD."
```
