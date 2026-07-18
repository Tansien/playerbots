#include "LivingTest.h"

#include "../util/LivingActivation.h"
#include "../util/LivingBotCreation.h"

using namespace living;

// GroupCreationLedger is the accounting HandleGroup runs per creation attempt.
// The old code decided success by matching "Bot created:" against the FRONT of
// the accumulated message list, so ordering - not the attempt's own result -
// determined the count.

LIVING_TEST(bot_creation_ledger_success_then_failures_counts_one)
{
    // One success followed by failures: the old front-message sniffing kept
    // counting every later failure as created.
    GroupCreationLedger ledger;
    LIVING_CHECK(!ledger.Record(BotCreateStatus::Created));
    LIVING_CHECK(!ledger.Record(BotCreateStatus::RetryableFailure));
    LIVING_CHECK(!ledger.Record(BotCreateStatus::RetryableFailure));
    LIVING_CHECK(ledger.created == 1);
}

LIVING_TEST(bot_creation_ledger_failure_then_successes_counts_all)
{
    // A first failure followed by successes: the old code never counted the
    // successes and retried to exhaustion.
    GroupCreationLedger ledger;
    LIVING_CHECK(!ledger.Record(BotCreateStatus::RetryableFailure));
    LIVING_CHECK(!ledger.Record(BotCreateStatus::Created));
    LIVING_CHECK(!ledger.Record(BotCreateStatus::Created));
    LIVING_CHECK(ledger.created == 2);
}

LIVING_TEST(bot_creation_ledger_terminal_failure_stops_the_run)
{
    GroupCreationLedger ledger;
    LIVING_CHECK(!ledger.Record(BotCreateStatus::Created));
    LIVING_CHECK(ledger.Record(BotCreateStatus::TerminalFailure)); // stop now
    LIVING_CHECK(ledger.created == 1);

    // Composition slots are consumed by confirmed AND pending members (both
    // reserve quota for planning) - never by failures.
    LIVING_CHECK(GroupCreationLedger::CountsTowardComposition(BotCreateStatus::Created));
    LIVING_CHECK(GroupCreationLedger::CountsTowardComposition(BotCreateStatus::PendingPersistence));
    LIVING_CHECK(!GroupCreationLedger::CountsTowardComposition(BotCreateStatus::RetryableFailure));
    LIVING_CHECK(!GroupCreationLedger::CountsTowardComposition(BotCreateStatus::TerminalFailure));
}

LIVING_TEST(bot_creation_ledger_pending_is_never_a_persisted_member)
{
    // A queued-but-unconfirmed creation reserves a composition slot yet is
    // counted separately: `created` alone may be claimed as persisted.
    GroupCreationLedger ledger;
    LIVING_CHECK(!ledger.Record(BotCreateStatus::PendingPersistence, 11 /*druid*/));
    LIVING_CHECK(!ledger.Record(BotCreateStatus::PendingPersistence, 1 /*warrior*/));
    LIVING_CHECK(!ledger.Record(BotCreateStatus::Created, 5 /*priest*/));

    LIVING_CHECK(ledger.created == 1);
    LIVING_CHECK(ledger.pendingPersistence == 2);
}

LIVING_TEST(bot_creation_ledger_tallies_the_persisted_class_not_the_assumption)
{
    // HandleGroup preselects a class per attempt but the ledger records what the
    // creation ACTUALLY persisted; the debug summary and quota accounting read
    // these values. Mixed success/failure: failures never tally a class.
    GroupCreationLedger ledger;
    ledger.Record(BotCreateStatus::Created, 1 /*warrior*/);
    ledger.Record(BotCreateStatus::RetryableFailure, 8 /*mage - ignored*/);
    ledger.Record(BotCreateStatus::Created, 5 /*priest*/);
    ledger.Record(BotCreateStatus::Created, 1 /*warrior*/);

    LIVING_CHECK(ledger.created == 3);
    LIVING_CHECK(ledger.createdByClass.size() == 2);
    LIVING_CHECK(ledger.createdByClass[1] == 2);
    LIVING_CHECK(ledger.createdByClass[5] == 1);
    LIVING_CHECK(ledger.createdByClass.find(8) == ledger.createdByClass.end());
}

LIVING_TEST(bot_creation_quota_consumption_never_wraps)
{
    // Role/class quotas are charged to the ACTUAL persisted role. When spec
    // selection lands on a role whose quota is already 0, the decrement must not
    // wrap the uint32 into a huge allowance.
    uint32_t quota = 2;
    LIVING_CHECK(TryConsumeQuota(quota) && quota == 1);
    LIVING_CHECK(TryConsumeQuota(quota) && quota == 0);
    LIVING_CHECK(!TryConsumeQuota(quota));
    LIVING_CHECK(quota == 0); // exhausted stays exhausted, no 0xFFFFFFFF
}

// MinimumTracker is the closest-inn selection RandomTeleportForLevel streams
// squared distances through. The old -1.0f sentinel compared every nonnegative
// distance as "not closer", so no inn was ever selected.

LIVING_TEST(minimum_tracker_zero_candidates_selects_nothing)
{
    MinimumTracker tracker;
    LIVING_CHECK(!tracker.HasSelection());
}

LIVING_TEST(minimum_tracker_one_candidate_is_selected)
{
    MinimumTracker tracker;
    LIVING_CHECK(tracker.Consider(1234.5f)); // the regression: this returned false forever
    LIVING_CHECK(tracker.HasSelection());
    LIVING_CHECK(tracker.Best() == 1234.5f);

    // Zero distance (standing on the inn) is a valid selection too.
    MinimumTracker zero;
    LIVING_CHECK(zero.Consider(0.0f));
    LIVING_CHECK(zero.HasSelection());
}

LIVING_TEST(minimum_tracker_several_candidates_keeps_the_smallest)
{
    MinimumTracker tracker;
    LIVING_CHECK(tracker.Consider(50.0f));
    LIVING_CHECK(!tracker.Consider(80.0f));  // farther: not selected
    LIVING_CHECK(tracker.Consider(20.0f));   // closer: new minimum
    LIVING_CHECK(!tracker.Consider(20.0f));  // equal: first wins
    LIVING_CHECK(!tracker.Consider(75.0f));
    LIVING_CHECK(tracker.HasSelection());
    LIVING_CHECK(tracker.Best() == 20.0f);
}

LIVING_TEST(map_local_minimum_ignores_foreign_map_candidates)
{
    // The inn-binding selection RandomPlayerbotMgr runs: cross-map squared
    // distances are meaningless, so a numerically closer inn on ANOTHER map
    // must never win over a same-map inn (or produce a wrong-map homebind).
    MapLocalMinimum closest(0 /*bot's map*/);

    LIVING_CHECK(!closest.Consider(1, 5.0f));      // foreign map, "closest" coords
    LIVING_CHECK(closest.Consider(0, 900.0f));     // same map wins regardless
    LIVING_CHECK(!closest.Consider(530, 1.0f));    // another foreign map
    LIVING_CHECK(closest.Consider(0, 400.0f));     // closer same-map candidate
    LIVING_CHECK(closest.HasSelection());

    // Only foreign candidates: nothing is ever selected, so nothing binds.
    MapLocalMinimum onlyForeign(0);
    LIVING_CHECK(!onlyForeign.Consider(1, 1.0f));
    LIVING_CHECK(!onlyForeign.Consider(571, 2.0f));
    LIVING_CHECK(!onlyForeign.HasSelection());
}

LIVING_TEST(weighted_pick_uses_exclusive_bound_and_filtered_weights)
{
    // The joint race/class tuple selector samples with this helper: every unit
    // of weight maps to exactly one candidate and the draw domain is exactly
    // sum(weights) - the legacy inclusive urand(0, total) fell through every
    // bucket once per total+1 draws into a filter-ignoring fallback.
    std::vector<uint32_t> const weights = { 3, 0, 2 };
    size_t index = 99;

    LIVING_CHECK(PickWeightedIndex(weights, 0, index) && index == 0);
    LIVING_CHECK(PickWeightedIndex(weights, 2, index) && index == 0);
    LIVING_CHECK(PickWeightedIndex(weights, 3, index) && index == 2); // zero-weight entry skipped
    LIVING_CHECK(PickWeightedIndex(weights, 4, index) && index == 2);

    // The exclusive bound: draw == sum is out of range, not a fallback.
    LIVING_CHECK(!PickWeightedIndex(weights, 5, index));

    // Empty and all-zero weight sets select nothing.
    LIVING_CHECK(!PickWeightedIndex({}, 0, index));
    LIVING_CHECK(!PickWeightedIndex({ 0, 0 }, 0, index));

    // Exhaustive: every draw maps to a nonzero-weight entry, proportionally.
    uint32_t counts[3] = { 0, 0, 0 };
    for (uint64_t draw = 0; draw < 5; ++draw)
    {
        LIVING_CHECK(PickWeightedIndex(weights, draw, index));
        ++counts[index];
    }
    LIVING_CHECK(counts[0] == 3 && counts[1] == 0 && counts[2] == 2);
}

LIVING_TEST(creation_metadata_validated_before_any_mutation)
{
    // 255 bytes fits every field; 256 must fail BEFORE account/character
    // mutation (CreateBot runs exactly this check before GetOrCreateAccount).
    std::string const atLimit(255, 'x');
    std::string const oversized(256, 'x');

    LIVING_CHECK(FindOversizedCreationValue(atLimit, atLimit, atLimit, atLimit).empty());
    LIVING_CHECK(FindOversizedCreationValue(oversized, "", "", "") == "gear");
    LIVING_CHECK(FindOversizedCreationValue("", oversized, "", "") == "group");
    LIVING_CHECK(FindOversizedCreationValue("", "", oversized, "") == "test");
    LIVING_CHECK(FindOversizedCreationValue("", "", "", oversized) == "name");

    // Defaults (empty fields) always pass.
    LIVING_CHECK(FindOversizedCreationValue("", "", "", "").empty());
}

// FillAccount is the pure driver behind the fixed-count bulk-creation fill
// (finding 4). The production nested loop broke only the inner sweep on a
// refused shared reservation, so the outer loop respun forever with no progress
// once the ledger blocked a slot the durable-only maxAllowed still counted free.

LIVING_TEST(bulk_fill_stops_when_reservation_is_blocked)
{
    // The finding's exact repro: durable 8/9 plus one pre-existing manual/group
    // reservation, so the very first reservation is refused. The fill must
    // return immediately after ONE round, never respin.
    uint32_t rounds = 0;
    AccountFillResult const fill = FillAccount(1, [&](uint32_t) -> AccountFillRound
    {
        ++rounds;
        AccountFillRound round;
        round.reservationBlocked = true; // ledger refuses the slot
        return round;
    });
    LIVING_CHECK(fill.created == 0);
    LIVING_CHECK(fill.reservationBlocked);
    LIVING_CHECK(rounds == 1); // terminated cleanly; did not respin forever
}

LIVING_TEST(bulk_fill_reaches_cap_without_overfilling)
{
    uint32_t rounds = 0;
    AccountFillResult const fill = FillAccount(3, [&](uint32_t allowance) -> AccountFillRound
    {
        ++rounds;
        LIVING_CHECK(allowance >= 1); // never invoked past the cap
        AccountFillRound round;
        round.created = 1;
        return round;
    });
    LIVING_CHECK(fill.created == 3);        // exactly the cap, never more
    LIVING_CHECK(!fill.reservationBlocked);
    LIVING_CHECK(rounds == 3);
}

LIVING_TEST(bulk_fill_stops_on_zero_progress_round)
{
    // A round that queues nothing without a reservation refusal (every key
    // class-filtered, or CreateRandomBot failing for all of them) must also
    // terminate instead of respinning.
    uint32_t rounds = 0;
    AccountFillResult const fill = FillAccount(5, [&](uint32_t) -> AccountFillRound
    {
        ++rounds;
        return AccountFillRound{}; // created 0, not blocked
    });
    LIVING_CHECK(fill.created == 0);
    LIVING_CHECK(!fill.reservationBlocked);
    LIVING_CHECK(rounds == 1);
}

LIVING_TEST(bulk_fill_partial_progress_then_blocked_terminates)
{
    int call = 0;
    AccountFillResult const fill = FillAccount(5, [&](uint32_t) -> AccountFillRound
    {
        AccountFillRound round;
        if (call++ == 0)
            round.created = 2;               // first sweep queues two
        else
            round.reservationBlocked = true; // then the ledger fills
        return round;
    });
    LIVING_CHECK(fill.created == 2);
    LIVING_CHECK(fill.reservationBlocked);
}

LIVING_TEST(group_join_plan_clears_only_on_success_or_terminal)
{
    // Verified membership: clear as completed work.
    auto joined = PlanGroupJoinAttempt(TargetExistence::Found, true, true, 0, 10, 30);
    LIVING_CHECK(joined.decision == GroupJoinDecision::ClearJoined);

    // Confirmed-missing target: deliberately terminal, clear.
    auto deleted = PlanGroupJoinAttempt(TargetExistence::ConfirmedMissing, false, false, 0, 10, 30);
    LIVING_CHECK(deleted.decision == GroupJoinDecision::ClearTerminal);

    // Offline target: keep the event, retry with backoff.
    auto offline = PlanGroupJoinAttempt(TargetExistence::Found, false, false, 0, 10, 30);
    LIVING_CHECK(offline.decision == GroupJoinDecision::RetryLater);
    LIVING_CHECK(offline.attemptNumber == 1);
    LIVING_CHECK(offline.retryDelaySeconds == 30);

    // Online but membership not verified (full group, declined/failed invite):
    // keep the event, retry.
    auto fullGroup = PlanGroupJoinAttempt(TargetExistence::Found, true, false, 3, 10, 30);
    LIVING_CHECK(fullGroup.decision == GroupJoinDecision::RetryLater);
    LIVING_CHECK(fullGroup.attemptNumber == 4);
    LIVING_CHECK(fullGroup.retryDelaySeconds == 120); // backoff grows with attempts

    // Bounded: the retry budget exhausts into a terminal clear, so login
    // processing cannot spam attempts forever.
    auto exhausted = PlanGroupJoinAttempt(TargetExistence::Found, true, false, 9, 10, 30);
    LIVING_CHECK(exhausted.decision == GroupJoinDecision::ClearTerminal);
}

LIVING_TEST(group_join_target_existence_classifier)
{
    // COUNT(*) yields one row on success, so a null result is a FAILED query
    // (unknown), never a confirmed absence.
    LIVING_CHECK(ClassifyTargetExistence(std::nullopt) == TargetExistence::Unavailable);
    LIVING_CHECK(ClassifyTargetExistence(std::optional<uint64_t>(0)) == TargetExistence::ConfirmedMissing);
    LIVING_CHECK(ClassifyTargetExistence(std::optional<uint64_t>(1)) == TargetExistence::Found);
    LIVING_CHECK(ClassifyTargetExistence(std::optional<uint64_t>(5)) == TargetExistence::Found);
}

LIVING_TEST(group_join_db_unavailable_retries_without_consuming_budget)
{
    // Finding 8: a transient outage during the target lookup must NOT be read as
    // a deletion. It retries and preserves the attempt budget and the marker.
    auto down = PlanGroupJoinAttempt(TargetExistence::Unavailable, false, false, 3, 10, 30);
    LIVING_CHECK(down.decision == GroupJoinDecision::RetryLater);
    LIVING_CHECK(down.attemptNumber == 3); // budget NOT consumed (stays at attemptsSoFar)

    // Even on what would be the last attempt, an outage must not terminally clear.
    auto downLast = PlanGroupJoinAttempt(TargetExistence::Unavailable, false, false, 9, 10, 30);
    LIVING_CHECK(downLast.decision == GroupJoinDecision::RetryLater);
    LIVING_CHECK(downLast.attemptNumber == 9);

    // Recovery -> confirmed missing -> a legitimate terminal clear.
    auto recoveredMissing = PlanGroupJoinAttempt(TargetExistence::ConfirmedMissing, false, false, 3, 10, 30);
    LIVING_CHECK(recoveredMissing.decision == GroupJoinDecision::ClearTerminal);

    // Recovery -> found but offline -> a normal budgeted retry (budget consumed).
    auto recoveredFound = PlanGroupJoinAttempt(TargetExistence::Found, false, false, 3, 10, 30);
    LIVING_CHECK(recoveredFound.decision == GroupJoinDecision::RetryLater);
    LIVING_CHECK(recoveredFound.attemptNumber == 4);
}

LIVING_TEST(weighted_draw_stays_in_uint64_at_and_past_uint32_max)
{
    // Deterministic 32-bit source: returns queued values in order.
    auto makeRand = [](std::vector<uint32_t> values)
    {
        size_t next = 0;
        return [values, next]() mutable { return values[next++ % values.size()]; };
    };

    size_t index = 999;

    // Total exactly UINT32_MAX through the production selector.
    std::vector<uint32_t> atMax = { 0xFFFFFFFEu, 1u };
    LIVING_CHECK(PickWeightedIndex64(atMax, makeRand({ 0, 0 }), index)); // draw 0
    LIVING_CHECK(index == 0);
    LIVING_CHECK(PickWeightedIndex64(atMax, makeRand({ 0, 0xFFFFFFFEu }), index)); // draw UINT32_MAX-1
    LIVING_CHECK(index == 1);

    // Total UINT32_MAX + 1: the old path truncated this to 0 before urand.
    std::vector<uint32_t> pastMax = { 0xFFFFFFFFu, 1u };
    LIVING_CHECK(PickWeightedIndex64(pastMax, makeRand({ 0, 0xFFFFFFFEu }), index)); // last unit of bucket 0
    LIVING_CHECK(index == 0);
    LIVING_CHECK(PickWeightedIndex64(pastMax, makeRand({ 0, 0xFFFFFFFFu }), index)); // the 2^32-th unit -> bucket 1
    LIVING_CHECK(index == 1);

    // Heavily skewed custom weights: a huge bucket followed by tiny ones still
    // maps every unit of weight to exactly one entry.
    std::vector<uint32_t> skewed = { 0xFFFFFFF0u, 1u, 1u };
    LIVING_CHECK(PickWeightedIndex64(skewed, makeRand({ 0, 0xFFFFFFF0u }), index)); // first unit after bucket 0
    LIVING_CHECK(index == 1);
    LIVING_CHECK(PickWeightedIndex64(skewed, makeRand({ 0, 0xFFFFFFF1u }), index));
    LIVING_CHECK(index == 2);

    // All-zero and empty weight sets fail closed.
    LIVING_CHECK(!PickWeightedIndex64({}, makeRand({ 0 }), index));
    LIVING_CHECK(!PickWeightedIndex64({ 0, 0 }, makeRand({ 0 }), index));
}

LIVING_TEST(bounded_draw_rejection_sampling_is_unbiased)
{
    // For bound 6, acceptBelow = (UINT64_MAX / 6) * 6 = 2^64 - 4: the top four
    // 64-bit values fold unevenly and MUST be rejected, not wrapped.
    size_t calls = 0;
    auto rigged = [&calls]() -> uint32_t
    {
        // First 64-bit draw: 0xFFFFFFFF'FFFFFFFF (rejected), second: 7.
        static uint32_t const script[] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0u, 7u };
        return script[calls++];
    };

    LIVING_CHECK(DrawBounded64(6, rigged) == 1); // 7 % 6, after one rejection
    LIVING_CHECK(calls == 4);

    // Degenerate bounds need no randomness.
    auto neverCalled = []() -> uint32_t { throw living::test::Failure{}; };
    LIVING_CHECK(DrawBounded64(1, neverCalled) == 0);
    LIVING_CHECK(DrawBounded64(0, neverCalled) == 0);
}

LIVING_TEST(activation_plan_confirms_all_writes_before_reporting_success)
{
    // The durable activation boundary consumes TYPED write results: a write
    // reporting failure may still have applied (StateUnknown), so
    // compensation restores the failed index itself in that case - the old
    // bool contract silently assumed a failed writer never mutated.
    auto makePlan = []()
    {
        return std::vector<PlannedEventWrite>{
            { "add", 1, 600, 0, 0 },
            { "logout", 0, 0, 0, 0 },
            { "login", 1, 4294967295u, 0, 0 },
            { "update", 1, 120, 7, 55 }, // a KNOWN nonzero prior
        };
    };

    // All writes confirmed: Persisted, in plan order, no compensation.
    {
        std::vector<std::string> writes;
        auto outcome = ExecuteActivationPlan(makePlan(),
            [&](std::string const& event, uint32_t value, uint32_t)
            {
                writes.push_back(event + "=" + std::to_string(value));
                return EventWriteResult::DesiredStateConfirmed;
            });
        LIVING_CHECK(outcome == ActivationOutcome::Persisted);
        LIVING_CHECK(writes.size() == 4);
        LIVING_CHECK(writes[0] == "add=1" && writes[3] == "update=1");
    }

    // DefinitelyNotApplied at index 0: provably untouched - NO compensation
    // is owed at all.
    {
        size_t calls = 0;
        auto outcome = ExecuteActivationPlan(makePlan(),
            [&](std::string const&, uint32_t, uint32_t)
            {
                ++calls;
                return EventWriteResult::DefinitelyNotApplied;
            });
        LIVING_CHECK(outcome == ActivationOutcome::FailedCompensated);
        LIVING_CHECK(calls == 1);
    }

    // StateUnknown at index 0: the write MAY have applied despite the
    // report - the failed index ITSELF is restored (this is the case the old
    // bool contract got wrong: `add=1` landing while reporting failure left
    // a durable activation memory considered absent).
    {
        std::vector<std::string> writes;
        size_t calls = 0;
        auto outcome = ExecuteActivationPlan(makePlan(),
            [&](std::string const& event, uint32_t value, uint32_t)
            {
                writes.push_back(event + "=" + std::to_string(value));
                return calls++ == 0 ? EventWriteResult::StateUnknown
                                    : EventWriteResult::DesiredStateConfirmed;
            });
        LIVING_CHECK(outcome == ActivationOutcome::FailedCompensated);
        LIVING_CHECK(writes.size() == 2);
        LIVING_CHECK(writes[1] == "add=0"); // the ambiguous index restored to prior
    }

    // StateUnknown at a LATER index: the failed index AND every confirmed
    // earlier write are restored, in reverse order, to their priors.
    {
        std::vector<std::string> writes;
        size_t calls = 0;
        auto outcome = ExecuteActivationPlan(makePlan(),
            [&](std::string const& event, uint32_t value, uint32_t validIn)
            {
                writes.push_back(event + "=" + std::to_string(value) + "/" + std::to_string(validIn));
                return calls++ == 3 ? EventWriteResult::StateUnknown
                                    : EventWriteResult::DesiredStateConfirmed;
            });
        LIVING_CHECK(outcome == ActivationOutcome::FailedCompensated);
        // 4 plan writes + 4 restores (update itself, then login, logout, add).
        LIVING_CHECK(writes.size() == 8);
        LIVING_CHECK(writes[4] == "update=7/55"); // failed index restored FIRST, to its PRIOR
        LIVING_CHECK(writes[7] == "add=0/0");
    }

    // DefinitelyNotApplied at a later index: only the confirmed prefix is
    // restored - the failed write provably left its own event untouched.
    {
        std::vector<std::string> writes;
        size_t calls = 0;
        auto outcome = ExecuteActivationPlan(makePlan(),
            [&](std::string const& event, uint32_t value, uint32_t)
            {
                writes.push_back(event + "=" + std::to_string(value));
                return calls++ == 1 ? EventWriteResult::DefinitelyNotApplied
                                    : EventWriteResult::DesiredStateConfirmed;
            });
        LIVING_CHECK(outcome == ActivationOutcome::FailedCompensated);
        LIVING_CHECK(writes.size() == 3); // 2 writes + 1 restore (index 0 only)
        LIVING_CHECK(writes.back() == "add=0");
    }

    // A restore counts ONLY when the prior state is CONFIRMED: an unknown
    // restore of the failed index reports uncertain compensation, forcing
    // the caller's dirty/reconciliation path.
    {
        size_t calls = 0;
        auto outcome = ExecuteActivationPlan(makePlan(),
            [&](std::string const&, uint32_t, uint32_t)
            {
                ++calls;
                if (calls == 1) return EventWriteResult::StateUnknown;      // plan write fails ambiguously
                return EventWriteResult::StateUnknown;                       // its restore is ALSO unknown
            });
        LIVING_CHECK(outcome == ActivationOutcome::FailedCompensationUncertain);
    }

    // Prefix restoration failure: earlier confirmed writes that cannot be
    // confirmed restored are uncertain too.
    {
        size_t calls = 0;
        auto outcome = ExecuteActivationPlan(makePlan(),
            [&](std::string const&, uint32_t, uint32_t)
            {
                ++calls;
                if (calls == 1) return EventWriteResult::DesiredStateConfirmed; // add applied
                if (calls == 2) return EventWriteResult::DefinitelyNotApplied;  // logout provably untouched
                return EventWriteResult::StateUnknown;                          // restoring add: unknown
            });
        LIVING_CHECK(outcome == ActivationOutcome::FailedCompensationUncertain);
    }

    // A restore that lands as DefinitelyNotApplied (failed before mutating,
    // prior unchanged) is NOT a confirmed restore of the requested prior
    // unless the prior already matched - the executor requires
    // DesiredStateConfirmed. Here the failed-index restore reports
    // DefinitelyNotApplied against a NONZERO prior: uncertain.
    {
        std::vector<PlannedEventWrite> plan = { { "update", 1, 120, /*prior*/ 7, 55 } };
        size_t calls = 0;
        auto outcome = ExecuteActivationPlan(plan,
            [&](std::string const&, uint32_t, uint32_t)
            {
                ++calls;
                if (calls == 1) return EventWriteResult::StateUnknown;
                return EventWriteResult::DefinitelyNotApplied; // restore did not run either
            });
        LIVING_CHECK(outcome == ActivationOutcome::FailedCompensationUncertain);
    }
}

LIVING_TEST(always_toggle_applies_only_on_confirmed_write)
{
    // Finding 9: `.bot always` may mutate freeAltBots and log the bot in/out
    // ONLY when the durable `always` write is confirmed. A rejected/ambiguous
    // write (SetValue == false) must leave runtime state unchanged and report
    // failure/retry. The same gate guards both the enable and disable branches.
    LIVING_CHECK(AlwaysToggleMayApply(true));   // confirmed -> apply runtime side effects
    LIVING_CHECK(!AlwaysToggleMayApply(false)); // unconfirmed -> leave runtime unchanged, report failure
}
