# Living Realm 0004A: five 0.1 goal contracts

[Back to design 0004](0004-goal-execution-and-strategy-overlays.md)

## 1. `SAFE_IDLE`

- **Candidate:** fallback when no valid goal or fail-closed degradation permits gameplay.
- **Payload:** reason code, optional supported safe-area ID, expiry.
- **Preconditions:** identity/profile valid; no higher-priority commitment/safety action.
- **Phases:** `EVALUATE`, `AMBIENT`, optional `MOVE_TO_SAFE_AREA`, `WAIT`.
- **Binding:** require ordinary survival/food/loot only as legal; suppress randomization, grind/travel target selection that could select unsupported/synthetic work; optional travel uses a specific supported safe destination.
- **Success:** intentionally non-terminal until replaced/expired; it is not offline progression.
- **Failure:** unsupported destination drops movement and remains stationary safe idle; no teleport.
- **Restart/cleanup:** revalidate reason/destination; remove only owned values/directives when replaced.

## 2. `COMPLETE_QUEST`

- **Candidate:** one quest already in the authoritative quest log, eligible and not terminal; 0.1 does not autonomously discover arbitrary chains.
- **Payload:** `quest_id`, optional objective index, expected quest-status generation, selected reward policy, expiry.
- **Preconditions:** quest exists in log, class/faction/level valid, supported objective/travel type, route does not require autonomous ship/zeppelin.
- **Phases:** `VALIDATE`, `TRAVEL_OBJECTIVE`, `EXECUTE_OBJECTIVE`, `TRAVEL_TURN_IN`, `TURN_IN`, `VERIFY`.
- **Binding:** set exact focus quest ID/target values; require existing quest/travel/loot/combat/RPG actions; constrain generic quest/travel selectors to the selected quest; suppress unrelated grind/travel only when not needed by this objective.
- **Success:** CMaNGOS reports selected quest rewarded (or configured terminal state) for this character.
- **Failure:** target unavailable, unsupported script/transport, repeated path failure, quest removed/abandoned, objective not advancing, or reward failure. Retry is bounded per phase with backoff; then `BLOCKED` with code.
- **Pause/resume:** combat and protected commitment may pause; restart reloads quest state and derives phase rather than trusting persisted phase.
- **Cleanup:** clear focus quest/targets and goal directives; never complete/reward directly.

## 3. `TRAIN_CLASS`

- **Candidate:** authoritative trainer data shows at least one affordable, eligible class spell not known.
- **Payload:** trainer entry/GUID locator, expected spell IDs or “eligible-at-activation” set, budget cap, map/area, expiry.
- **Preconditions:** class trainer compatible, ordinary path reachable, required money reserved, no free-learning config/cheat.
- **Phases:** `VALIDATE`, `TRAVEL`, `INTERACT`, `PURCHASE`, `VERIFY`.
- **Binding:** require exact travel target and existing trainer action; constrain generic RPG target to selected trainer.
- **Success:** expected eligible spells are known and normal money cost occurred, or no eligible affordable spells remain.
- **Failure:** trainer missing/wrong, unaffordable, route unsupported, handler rejection, repeated no-progress. Never call free spell learning.
- **Restart/cleanup:** recompute known/eligible spells and cost; clear trainer target/directives.

## 4. `VISIT_VENDOR_OR_REPAIR`

- **Candidate:** bags exceed configured pressure, vendor trash exists, repair need exceeds threshold, or required ordinary supplies are missing.
- **Payload:** purpose flags, vendor/repair NPC entry/locator, item-use snapshot generation, budget cap, area, expiry.
- **Preconditions:** compatible reachable NPC, ordinary vendor/repair handlers, sufficient budget for requested purchases/repair.
- **Phases:** `VALIDATE`, `TRAVEL`, `SELL`, `REPAIR`, optional `BUY`, `VERIFY`.
- **Binding:** exact NPC target; require travel/RPG vendor/repair actions; constrain generic vendor selection. Auction House behavior is not part of this goal.
- **Success:** requested bag/repair/supply postconditions are satisfied or no legal affordable action remains.
- **Failure:** missing NPC, unsupported route, transaction rejection, repeated no-progress. Partial legal completion may succeed with outcome details.
- **Restart/cleanup:** refresh inventory/durability/budget; clear target/directives.

## 5. `PREPARE_LOGOUT`

- **Candidate/owner:** created by schedule wind-down, not the general planner.
- **Payload:** schedule generation, requested/deadline times, preferred supported safe destination, required cleanup flags.
- **Preconditions:** desired offline and no higher-priority protected commitment. The goal may exist while deferred.
- **Phases:** `DEFER`, `FINISH_COMBAT`, `RECOVER_NORMAL`, `OPTIONAL_MAINTENANCE`, `TRAVEL_SAFE`, `SAVE`, `READY`.
- **Binding:** suppress new long quest/grind/group work; require ordinary survival/recovery and exact safe travel/maintenance actions. No hearth/teleport cheat.
- **Success:** not in combat/trade/taxi/transport/instance/BG, no protected commitment, supported safe state reached or explicit policy permits current location, character save completes, and lifecycle marks logout-eligible. Actual logout is performed/observed by the login manager.
- **Deferral:** protected commitment, instance/BG/arena, combat, death recovery, trade/mail/AH, taxi/transport, pending group transition. Deadline emits diagnostics but cannot break a protected real-player group.
- **Restart:** actual session/group/map state and schedule generation determine phase; persisted `READY` never proves logout.
- **Failure:** inaccessible safe point falls back to current legal safe state if policy permits; otherwise block/diagnose. No emergency teleport merely to meet schedule.
- **Cleanup:** clear wind-down overlay only after actual offline observation or explicit schedule cancellation.

## 6. Common bounded retries

Each adapter records `retry_count`, phase-specific failure code, last attempt, and next eligible attempt. Default limits/backoffs are configuration within bounded ranges and must be deterministic in tests. Repeated unsupported/path/script failures become `BLOCKED`; they never trigger legacy randomization. A blocked goal may be replaced by `SAFE_IDLE` transactionally.
