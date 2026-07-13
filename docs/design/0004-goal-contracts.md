# Living Realm 0004A: five 0.1 goal contracts

[Back to design 0004](0004-goal-execution-and-strategy-overlays.md)

## A.1 `SAFE_IDLE` and ambient quest intake

- **Candidate:** fallback when no valid primary goal exists or fail-closed
  degradation still permits ordinary gameplay.
- **Payload:** reason code, current supported-region ID, optional safe-area ID,
  ambient-intake generation, expiry.
- **Preconditions:** identity/profile valid; no higher-priority
  commitment/safety/wind-down action.
- **Phases:** `EVALUATE`, `AMBIENT`, optional `MOVE_TO_SAFE_AREA`, `WAIT`.
- **Retained ambient behavior:** ordinary survival, eating/drinking with real
  inventory, legal loot, nearby combat self-defense, local grinding within the
  current supported region, nearby eligible quest accept/turn-in, nearby
  vendor/repair, rest, and non-authoritative NPC wandering.
- **Suppressed behavior:** randomization, arbitrary cross-zone target selection,
  legacy teleport, unsupported transport, free maintenance, quest completion,
  broad guild/raid formation, and any synthetic shortcut.
- **Quest intake:** an eligible nearby quest MAY be accepted through the normal
  quest-giver handler. The intake records the authoritative accepted quest ID and
  a quest-state fingerprint, then asks the state writer to activate
  `COMPLETE_QUEST` deterministically. It does not discover chains remotely or
  fabricate prerequisites.
- **Binding:** local ambient selectors are constrained by supported-region and
  route-capability values. Optional safe travel uses a specific safe destination.
- **Success:** intentionally non-terminal until replaced or expired; it is not
  offline progression.
- **Failure:** unsupported destination drops movement and remains stationary safe
  idle; no teleport.
- **Restart/cleanup:** revalidate reason, region, accepted quests, and destination;
  remove only owned values/directives when replaced.

This makes quest acquisition explicit: fresh bots are not expected to begin with
a populated quest log, and `COMPLETE_QUEST` is not responsible for accepting an
unknown remote chain.

## A.2 `COMPLETE_QUEST`

- **Candidate:** one quest already in the authoritative quest log, usually
  supplied by ambient intake; eligible and not terminal.
- **Payload:** `quest_id`, optional objective index, quest-state fingerprint,
  selected reward policy, route capability/route ID, no-progress window, expiry.
- **Quest-state fingerprint:** quest ID, status, rewarded flag, objective
  creature/GO counts, item counts, timer state, map/area, and fingerprint version.
  It is a Living Realm snapshot, not a core "quest generation."
- **Preconditions:** quest exists in log, class/faction/level valid, objective
  type supported, and route is normal or an allowlisted 0002C modeled transport.
- **Phases:** `VALIDATE`, `TRAVEL_OBJECTIVE`, `EXECUTE_OBJECTIVE`,
  `TRAVEL_TURN_IN`, `TURN_IN`, `VERIFY`.
- **Binding:** set exact focus quest ID and objective values; require existing
  quest/travel/loot/combat/RPG actions; constrain generic quest/travel selectors
  to the selected quest; suppress unrelated grind/travel only when not needed by
  the objective.
- **Progress detection:** compare authoritative objective counters at bounded
  intervals. Reaching a POI without counter progress consumes the phase retry
  budget and records a per-quest failure reason. It never auto-completes.
- **Success:** CMaNGOS reports the selected quest rewarded, or the explicitly
  configured legitimate terminal state for that quest.
- **Failure:** target unavailable, unsupported script/route, repeated path
  failure, quest removed/abandoned, objective not advancing, or reward handler
  failure. Retry is bounded per phase with backoff; then `BLOCKED`.
- **Pause/resume:** combat and higher precedence may pause; restart reloads quest
  state and derives phase rather than trusting persisted phase.
- **Cleanup:** clear focus quest/targets and goal directives; never complete,
  reward, or generate required money/items directly.

The typed phase and no-progress behavior is intentionally comparable to the
useful parts of mod-playerbots' newer RPG quest executor, while persistence and
execution remain CMaNGOS-native.

## A.3 `TRAIN_CLASS`

- **Candidate:** authoritative trainer data shows at least one affordable,
  eligible class spell not known.
- **Payload:** trainer entry and travel locator, expected spell IDs or
  "eligible-at-activation" set, budget cap, map/area, route ID, expiry.
- **Preconditions:** compatible class trainer, supported route, required money
  reserved, no free-learning mode, gold cheat, or generated spell path.
- **Phases:** `VALIDATE`, `TRAVEL`, `INTERACT`, `PURCHASE`, `VERIFY`.
- **Binding:** bind exact trainer travel destination and existing trainer action;
  constrain generic RPG target to the selected trainer. The final interaction may
  resolve the nearest compatible live spawn for that exact entry/area.
- **Success:** expected eligible spells are known and the normal money cost
  occurred, or no eligible affordable spells remain.
- **Failure:** trainer missing/wrong, unaffordable, route unsupported, handler
  rejection, or repeated no-progress. Never call free spell learning.
- **Restart/cleanup:** recompute known/eligible spells and cost; clear trainer
  target/directives.

## A.4 `VISIT_VENDOR_OR_REPAIR`

- **Candidate:** bags exceed configured pressure, vendor trash exists, repair
  need exceeds threshold, or ordinary required supplies are missing.
- **Payload:** purpose flags, vendor/repair entry and travel locator,
  inventory/durability fingerprint, budget cap, area, route ID, expiry.
- **Preconditions:** compatible reachable NPC class, ordinary vendor/repair
  handlers, sufficient budget for requested purchases/repair.
- **Phases:** `VALIDATE`, `TRAVEL`, `SELL`, `REPAIR`, optional `BUY`, `VERIFY`.
- **Binding:** exact travel binding to an NPC entry/area, not a promise that the
  existing action will use one immutable spawn GUID. At interaction time the
  action may use the nearest compatible live NPC satisfying the binding.
- **Success:** inventory, durability, supply, and money postconditions show the
  requested legal work completed or no legal affordable action remains.
- **Failure:** missing compatible NPC, unsupported route, transaction rejection,
  or repeated no-progress. Partial legal completion may succeed with details.
- **Restart/cleanup:** refresh inventory/durability/budget; clear target/directives.
- **Exclusion:** Auction House behavior is not part of this goal.

## A.5 `PREPARE_LOGOUT`

- **Candidate/owner:** created by schedule wind-down, not the general planner.
- **Payload:** schedule generation, requested/deadline times, preferred supported
  safe destination, required cleanup flags.
- **Preconditions:** desired offline and no higher-priority protected commitment.
  The goal may exist while deferred.
- **Phases:** `DEFER`, `FINISH_COMBAT`, `RECOVER_NORMAL`,
  `OPTIONAL_MAINTENANCE`, `TRAVEL_SAFE`, `SAVE_SUBMITTED`, `READY`.
- **Binding:** suppress new long quest/grind/group work; require ordinary
  survival/recovery and exact safe travel/maintenance actions. No hearth or
  teleport cheat.
- **Success:** not in combat/trade/taxi/transport/instance/BG, no protected
  commitment, supported safe state reached or policy permits current location,
  normal character persistence was submitted where required, and lifecycle marks
  logout-eligible. It does not claim the asynchronous save is durably complete.
- **Deferral:** protected commitment, instance/BG/arena, combat, death recovery,
  trade/mail/AH, taxi/transport, pending group transition.
- **Deadline:** emits diagnostics but cannot break a protected real-player group.
- **Restart:** actual session/group/map state and schedule generation determine
  phase; persisted `READY` never proves logout or save completion.
- **Failure:** inaccessible safe point falls back to current legal safe state if
  policy permits; otherwise block/diagnose. No emergency teleport merely to meet
  schedule.
- **Cleanup:** clear wind-down overlay only after actual offline observation or
  explicit schedule cancellation.

## A.6 Common bounded retries

Each adapter records `retry_count`, phase-specific failure code, last attempt,
next eligible attempt, and authoritative before/after fingerprints.

Default limits/backoffs are configuration within bounded ranges and deterministic
in tests. Repeated unsupported/path/script failures become `BLOCKED`; they never
trigger legacy randomization. A blocked goal may be replaced by `SAFE_IDLE`
through the state writer.
