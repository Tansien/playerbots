#include "LivingTest.h"

#include "../util/LivingActivation.h"
#include "../util/LivingBotCreation.h"
#include "../util/LivingCreationLifecycle.h"

#include <algorithm>

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

// RunFixedCountRound is the exact sweep the production fixed-count fill runs
// per reshuffle: zero quotas are disabled (the raw decrement used to wrap them
// to UINT_MAX and mass-create the disabled combination), consumption is
// checked, and a refused reservation still terminates the round.

namespace
{
    using ClassRace = std::pair<uint8_t, uint8_t>;

    std::vector<ClassRace> KeysOf(std::map<ClassRace, uint32_t> const& remaining)
    {
        std::vector<ClassRace> keys;
        for (auto const& entry : remaining)
            keys.push_back(entry.first);
        return keys;
    }
}

LIVING_TEST(fixed_count_round_zero_only_map_creates_nothing_and_terminates)
{
    // Every configured count is zero: nothing may be reserved or created, the
    // zero entries are dropped, and the outer fill stops on the zero-progress
    // round instead of respinning.
    std::map<ClassRace, uint32_t> remaining{ {{1, 1}, 0}, {{4, 2}, 0} };
    uint32_t attempts = 0;

    AccountFillResult const fill = FillAccount(9, [&](uint32_t allowance) -> AccountFillRound
    {
        return RunFixedCountRound(remaining, KeysOf(remaining), allowance,
            [&](ClassRace const&) { ++attempts; return FixedCountAttempt::Created; });
    });

    LIVING_CHECK(attempts == 0);        // zero = disabled: attempt never invoked
    LIVING_CHECK(fill.created == 0);
    LIVING_CHECK(remaining.empty());    // dropped, so no false shortfall report
}

LIVING_TEST(fixed_count_round_mixed_map_creates_only_positive_quotas)
{
    // Mixed map: the zero entry is skipped entirely while positive quotas are
    // consumed exactly, with checked decrements (never past zero).
    std::map<ClassRace, uint32_t> remaining{ {{1, 1}, 2}, {{4, 2}, 0}, {{8, 8}, 1} };
    std::map<ClassRace, uint32_t> attempted;

    AccountFillResult const fill = FillAccount(9, [&](uint32_t allowance) -> AccountFillRound
    {
        return RunFixedCountRound(remaining, KeysOf(remaining), allowance,
            [&](ClassRace const& key) { ++attempted[key]; return FixedCountAttempt::Created; });
    });

    LIVING_CHECK(fill.created == 3);                   // 2 + 0 + 1
    LIVING_CHECK((attempted[{1, 1}] == 2));
    LIVING_CHECK((attempted.find({4, 2}) == attempted.end())); // disabled combo never attempted
    LIVING_CHECK((attempted[{8, 8}] == 1));
    LIVING_CHECK(remaining.empty());                   // all quotas exhausted and erased
}

LIVING_TEST(fixed_count_round_failed_create_keeps_quota_and_blocked_terminates)
{
    std::map<ClassRace, uint32_t> remaining{ {{1, 1}, 1} };

    // A failed create keeps the quota for a later round.
    AccountFillRound round = RunFixedCountRound(remaining, KeysOf(remaining), 5,
        [&](ClassRace const&) { return FixedCountAttempt::CreateFailed; });
    LIVING_CHECK(round.created == 0 && !round.reservationBlocked);
    LIVING_CHECK((remaining[{1, 1}] == 1));

    // A refused shared reservation terminates the round with the flag set.
    round = RunFixedCountRound(remaining, KeysOf(remaining), 5,
        [&](ClassRace const&) { return FixedCountAttempt::ReservationBlocked; });
    LIVING_CHECK(round.reservationBlocked);
    LIVING_CHECK((remaining[{1, 1}] == 1));
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

LIVING_TEST(always_toggle_refuses_untrusted_or_invalid_prior_state)
{
    // `.bot always` decides its direction from the TYPED prior read. Trusted
    // valid states toggle normally...
    LIVING_CHECK(PlanAlwaysToggle(true, 0 /*DISABLED*/) == AlwaysToggleDecision::Enable);
    LIVING_CHECK(PlanAlwaysToggle(true, 2 /*DISABLED_BY_COMMAND*/) == AlwaysToggleDecision::Enable);
    LIVING_CHECK(PlanAlwaysToggle(true, 1 /*ACTIVE*/) == AlwaysToggleDecision::Disable);

    // ...but a failed/dirty read (untrusted, raw collapses to zero) and an
    // invalid stored value both refuse: no durable write, no runtime mutation.
    LIVING_CHECK(PlanAlwaysToggle(false, 0) == AlwaysToggleDecision::RefuseUnknown);
    LIVING_CHECK(PlanAlwaysToggle(false, 1) == AlwaysToggleDecision::RefuseUnknown); // dirty ACTIVE read
    LIVING_CHECK(PlanAlwaysToggle(true, 3) == AlwaysToggleDecision::RefuseUnknown);  // invalid enum value
}

LIVING_TEST(always_online_loader_excludes_disabled_by_command_even_on_failed_reads)
{
    // Startup/reload scheduling from the durable `always` rows. Trusted states
    // behave as configured...
    LIVING_CHECK(ShouldScheduleAlwaysOnline(true, 1 /*ACTIVE*/, false));
    LIVING_CHECK(ShouldScheduleAlwaysOnline(true, 0 /*DISABLED*/, true));   // config default online
    LIVING_CHECK(!ShouldScheduleAlwaysOnline(true, 0, false));
    LIVING_CHECK(!ShouldScheduleAlwaysOnline(true, 2 /*DISABLED_BY_COMMAND*/, true)); // command wins over config

    // ...and a reload during a database failure schedules NOTHING: the failed
    // read collapses to zero, which used to re-include DISABLED_BY_COMMAND
    // characters and drop ACTIVE ones. Invalid stored values are never coerced.
    LIVING_CHECK(!ShouldScheduleAlwaysOnline(false, 0, true));  // was DISABLED_BY_COMMAND, read failed
    LIVING_CHECK(!ShouldScheduleAlwaysOnline(false, 1, true));  // was ACTIVE, read failed: retried next reload
    LIVING_CHECK(!ShouldScheduleAlwaysOnline(true, 7, true));   // invalid enum value
}

LIVING_TEST(group_join_persist_advances_state_only_when_confirmed)
{
    // Finding N2: the group-join backoff/consume bookkeeping advances ONLY on a
    // confirmed durable write; an unconfirmed write holds off and changes nothing.
    auto ok = PlanGroupJoinPersist(GroupJoinDecision::RetryLater, EventWriteResult::DesiredStateConfirmed);
    LIVING_CHECK(ok.advanceBackoff && !ok.consumed && !ok.holdoff);

    auto notApplied = PlanGroupJoinPersist(GroupJoinDecision::RetryLater, EventWriteResult::DefinitelyNotApplied);
    LIVING_CHECK(!notApplied.advanceBackoff && notApplied.holdoff);

    auto unknown = PlanGroupJoinPersist(GroupJoinDecision::RetryLater, EventWriteResult::StateUnknown);
    LIVING_CHECK(!unknown.advanceBackoff && unknown.holdoff);

    for (GroupJoinDecision term : { GroupJoinDecision::ClearJoined, GroupJoinDecision::ClearTerminal })
    {
        LIVING_CHECK(PlanGroupJoinPersist(term, EventWriteResult::DesiredStateConfirmed).consumed);
        LIVING_CHECK(!PlanGroupJoinPersist(term, EventWriteResult::DesiredStateConfirmed).holdoff);
        LIVING_CHECK(PlanGroupJoinPersist(term, EventWriteResult::StateUnknown).holdoff);
        LIVING_CHECK(!PlanGroupJoinPersist(term, EventWriteResult::StateUnknown).consumed);
    }
}

LIVING_TEST(group_join_attempt_count_advances_only_on_confirmed_persist)
{
    // The durable attempt count (F8's bounded budget) may only advance when its
    // increment write is confirmed; a lost/ambiguous write leaves it, so the
    // budget cannot be silently unbounded by replayed login passes.
    uint32_t durable = 1;
    auto onePass = [&](EventWriteResult w) -> uint32_t
    {
        auto plan = PlanGroupJoinAttempt(TargetExistence::Found, true, false, durable - 1, 10, 30);
        LIVING_CHECK(plan.decision == GroupJoinDecision::RetryLater);
        if (PlanGroupJoinPersist(plan.decision, w).advanceBackoff)
            durable = plan.attemptNumber + 1;
        return plan.attemptNumber;
    };
    LIVING_CHECK(onePass(EventWriteResult::DefinitelyNotApplied) == 1 && durable == 1); // stuck: budget honored
    LIVING_CHECK(onePass(EventWriteResult::StateUnknown)         == 1 && durable == 1);
    LIVING_CHECK(onePass(EventWriteResult::DesiredStateConfirmed) == 1 && durable == 2); // advances only now
    LIVING_CHECK(onePass(EventWriteResult::DesiredStateConfirmed) == 2 && durable == 3);
}

// Scheduler-ownership across LoginFreeBots passes: the release decision
// (MayReleasePostCreateOwner) composed with the group-join planner and the
// one-shot marker ledgers, exactly as the production pass computes it. A
// non-always bot must stay scheduled while any post-create obligation is
// unsettled - unknown reads never count as settled.

LIVING_TEST(post_create_owner_retained_across_group_join_retries)
{
    // Two manager passes per scenario: pass 1 hits a non-terminal join
    // outcome (offline target / full group / database unavailable) and must
    // retain the bot; a later pass reaches a confirmed terminal persist and
    // releases it.
    auto joinSettledAfterPass = [](TargetExistence existence, bool online, bool verified,
        uint32_t attempts, EventWriteResult persistWrite) -> bool
    {
        GroupJoinPlan const plan = PlanGroupJoinAttempt(existence, online, verified, attempts, 10, 30);
        if (plan.decision == GroupJoinDecision::RetryLater)
            return false; // marker kept for a bounded retry
        return PlanGroupJoinPersist(plan.decision, persistWrite).consumed;
    };

    bool const alwaysKnown = true, alwaysActive = false;

    // Offline target, then the target comes online and membership verifies.
    bool settled = joinSettledAfterPass(TargetExistence::Found, false, false, 0,
        EventWriteResult::DesiredStateConfirmed);
    LIVING_CHECK(!MayReleasePostCreateOwner(alwaysKnown, alwaysActive, !settled)); // pass 1: retained
    settled = joinSettledAfterPass(TargetExistence::Found, true, true, 1,
        EventWriteResult::DesiredStateConfirmed);
    LIVING_CHECK(MayReleasePostCreateOwner(alwaysKnown, alwaysActive, !settled));  // pass 2: released

    // Full group / failed invite (online but membership not verified).
    settled = joinSettledAfterPass(TargetExistence::Found, true, false, 3,
        EventWriteResult::DesiredStateConfirmed);
    LIVING_CHECK(!MayReleasePostCreateOwner(alwaysKnown, alwaysActive, !settled));

    // Database unavailable: existence unknown - retained without consuming
    // the attempt budget.
    GroupJoinPlan const unavailable = PlanGroupJoinAttempt(TargetExistence::Unavailable,
        false, false, 4, 10, 30);
    LIVING_CHECK(unavailable.decision == GroupJoinDecision::RetryLater);
    LIVING_CHECK(unavailable.attemptNumber == 4); // budget not consumed
    LIVING_CHECK(!MayReleasePostCreateOwner(alwaysKnown, alwaysActive, true));

    // Terminal decision whose durable CLEAR is unconfirmed: still retained;
    // the confirmed clear on the next pass releases.
    settled = joinSettledAfterPass(TargetExistence::ConfirmedMissing, false, false, 2,
        EventWriteResult::StateUnknown);
    LIVING_CHECK(!MayReleasePostCreateOwner(alwaysKnown, alwaysActive, !settled));
    settled = joinSettledAfterPass(TargetExistence::ConfirmedMissing, false, false, 2,
        EventWriteResult::DesiredStateConfirmed);
    LIVING_CHECK(MayReleasePostCreateOwner(alwaysKnown, alwaysActive, !settled));
}

LIVING_TEST(post_create_owner_retained_on_unknown_reads_and_failed_clears)
{
    // Unknown marker reads are never interpreted as "no work": the bot stays
    // scheduled with nothing mutated.
    LIVING_CHECK(!MayReleasePostCreateOwner(/*alwaysKnown*/ true, /*active*/ false,
        /*pending*/ true)); // an untrusted marker read reports pending work

    // A failed marker clear keeps the obligation across passes; only the
    // confirmed clear settles it.
    OneShotMarker marker;
    marker.OnEffectApplied();
    LIVING_CHECK(!marker.OnClearResult(EventWriteResult::StateUnknown));
    LIVING_CHECK(!MayReleasePostCreateOwner(true, false, true));
    LIVING_CHECK(marker.OnClearResult(EventWriteResult::DesiredStateConfirmed));
    LIVING_CHECK(MayReleasePostCreateOwner(true, false, false));

    // The always-online membership itself releases only from a KNOWN state:
    // an unreadable `always` row retains even a fully-settled bot, and a
    // known ACTIVE bot is never released.
    LIVING_CHECK(!MayReleasePostCreateOwner(/*alwaysKnown*/ false, false, false));
    LIVING_CHECK(!MayReleasePostCreateOwner(true, /*active*/ true, false));
}

LIVING_TEST(fixed_count_config_rebuild_never_retains_stale_entries)
{
    // The reload sequence uses ONE mutable "config" and "availability" state,
    // exactly like a GM/console reload of the real config object: the loader
    // clears and reassigns from BuildFixedClassRaceCounts every Initialize,
    // so no earlier positive entry can survive.
    std::map<std::pair<uint32_t, uint32_t>, int> configured;
    std::map<std::pair<uint32_t, uint32_t>, bool> available;
    bool fixedMode = true;

    std::map<std::pair<uint8_t, uint8_t>, uint32_t> quotas;
    auto reload = [&]()
    {
        // Mirrors PlayerbotAIConfig::Initialize: unconditional clear, rebuild
        // only while fixed mode is on.
        quotas.clear();
        if (!fixedMode)
            return;
        quotas = BuildFixedClassRaceCounts(12, 12,
            [&](uint32_t cls, uint32_t race) -> int
            {
                auto it = configured.find({ cls, race });
                return it == configured.end() ? -1 : it->second; // absent key = missing
            },
            [&](uint32_t cls, uint32_t race)
            {
                auto it = available.find({ cls, race });
                return it != available.end() && it->second;
            });
    };

    configured[{1, 1}] = 5;
    configured[{4, 2}] = 3;
    available[{1, 1}] = true;
    available[{4, 2}] = true;
    reload();
    LIVING_CHECK(quotas.size() == 2);
    LIVING_CHECK((quotas[{1, 1}] == 5 && quotas[{4, 2}] == 3));

    // positive -> zero: the stale 5 must not survive the reload.
    configured[{1, 1}] = 0;
    reload();
    LIVING_CHECK((quotas.find({1, 1}) == quotas.end()));
    LIVING_CHECK((quotas[{4, 2}] == 3));

    // positive -> missing (key removed from the config entirely).
    configured.erase({4, 2});
    configured[{1, 1}] = 5; // back to positive so the next steps have a subject
    reload();
    LIVING_CHECK((quotas.find({4, 2}) == quotas.end()));
    LIVING_CHECK((quotas[{1, 1}] == 5));

    // available -> unavailable: the entry disappears even though the count
    // stays positive.
    available[{1, 1}] = false;
    reload();
    LIVING_CHECK(quotas.empty());
    available[{1, 1}] = true;

    // fixed on -> off -> on: the off reload clears; the on reload rebuilds
    // only from current configuration.
    reload();
    LIVING_CHECK((quotas[{1, 1}] == 5));
    fixedMode = false;
    reload();
    LIVING_CHECK(quotas.empty());
    fixedMode = true;
    configured[{1, 1}] = 2; // changed while off
    reload();
    LIVING_CHECK(quotas.size() == 1);
    LIVING_CHECK((quotas[{1, 1}] == 2));

    // No disabled combination can reach bulk creation: the rebuilt map IS the
    // sweep's quota source, and zero-count keys were never inserted.
    configured[{4, 2}] = 0;
    reload();
    std::map<std::pair<uint8_t, uint8_t>, uint32_t> remaining = quotas;
    uint32_t disabledAttempts = 0;
    RunFixedCountRound(remaining, std::vector<std::pair<uint8_t, uint8_t>>{ {1, 1}, {4, 2} }, 9,
        [&](std::pair<uint8_t, uint8_t> const& key)
        {
            if (key == std::pair<uint8_t, uint8_t>{4, 2})
                ++disabledAttempts;
            return FixedCountAttempt::Created;
        });
    LIVING_CHECK(disabledAttempts == 0);
}

LIVING_TEST(group_side_effects_gate_on_verified_membership)
{
    // The candidate join target is promoted to the post-create master ONLY on
    // verified membership; teleport and master-derived gear consume that
    // master. This drives the production pass's decision sequence across
    // repeated failed/full-group attempts and the eventual success.
    uint32_t teleports = 0;
    uint32_t masterGearApplies = 0;
    uint32_t attempts = 0;
    bool joinSettled = false;

    auto pass = [&](bool targetOnline, bool inviteSticks) -> GroupJoinDecision
    {
        bool const membershipVerified = targetOnline && inviteSticks;
        bool const masterVerified = membershipVerified; // master = target only when verified

        GroupJoinPlan const plan = PlanGroupJoinAttempt(TargetExistence::Found, targetOnline,
            membershipVerified, attempts, 10, 30);
        if (plan.decision != GroupJoinDecision::RetryLater)
            joinSettled = true; // confirmed terminal persist assumed for the model
        else
            attempts = plan.attemptNumber;

        // gear=sync processing: runs only per the gate.
        if (MayApplyMasterDerivedGear(true, joinSettled, masterVerified))
            ++masterGearApplies;

        if (masterVerified)
            ++teleports; // production: if (master) TeleportTo(master)

        return plan.decision;
    };

    // Repeated failed/full-group attempts: no teleport, no master-derived
    // gear, marker retained for retry.
    LIVING_CHECK(pass(false, false) == GroupJoinDecision::RetryLater); // offline target
    LIVING_CHECK(pass(true, false) == GroupJoinDecision::RetryLater);  // full group / declined invite
    LIVING_CHECK(pass(true, false) == GroupJoinDecision::RetryLater);
    LIVING_CHECK(teleports == 0 && masterGearApplies == 0);

    // Success: verified membership applies the master effects exactly once
    // (the cleared marker never re-enters this path).
    LIVING_CHECK(pass(true, true) == GroupJoinDecision::ClearJoined);
    LIVING_CHECK(teleports == 1 && masterGearApplies == 1);

    // Terminal failure instead: gear falls back to the bot's own level (the
    // gate opens with no verified master).
    uint32_t fallbackApplies = 0;
    bool terminalSettled = false;
    GroupJoinPlan const terminal = PlanGroupJoinAttempt(TargetExistence::ConfirmedMissing,
        false, false, 0, 10, 30);
    LIVING_CHECK(terminal.decision == GroupJoinDecision::ClearTerminal);
    terminalSettled = true;
    if (MayApplyMasterDerivedGear(true, terminalSettled, false))
        ++fallbackApplies;
    LIVING_CHECK(fallbackApplies == 1);

    // Non-master gear (e.g. "epic") is never deferred by the join.
    LIVING_CHECK(MayApplyMasterDerivedGear(false, false, false));
}

LIVING_TEST(gear_values_are_whitelisted_before_any_mutation)
{
    // The finite supported set is accepted...
    for (char const* value : { "", "default", "empty", "green", "uncommon", "blue",
        "rare", "purple", "epic", "upgrade", "sync", "best", "partial" })
        LIVING_CHECK(IsSupportedGearValue(value));

    // ...and everything else is rejected BEFORE any account/character
    // mutation.
    LIVING_CHECK(!IsSupportedGearValue("legendary"));
    LIVING_CHECK(!IsSupportedGearValue("Epic"));      // case-sensitive, like the effect switch
    LIVING_CHECK(!IsSupportedGearValue("sync@70"));   // internal stamped form is not user input
    LIVING_CHECK(!IsSupportedGearValue(std::string(255, 'x')));

    // Every ACCEPTED value still fits the durable envelope with the
    // worst-case captured-level suffix stamped on: accepted input can never
    // become impossible to persist.
    for (char const* value : { "", "default", "empty", "green", "uncommon", "blue",
        "rare", "purple", "epic", "upgrade", "sync", "best", "partial" })
        LIVING_CHECK(EventValueFitsSchema("create gear", StampGearTarget(value, 4294967295u)));
}

LIVING_TEST(gear_target_capture_survives_group_marker_clear)
{
    // The verified group target's level is stamped into a dependent
    // sync/upgrade gear payload BEFORE the (only) durable group marker is
    // cleared; recovery after a crash between those steps therefore gears
    // against the original verified target, never the bot's own level.
    std::string base;
    uint32_t level = 0;

    SplitGearTarget(StampGearTarget("sync", 70), base, level);
    LIVING_CHECK(base == "sync" && level == 70);
    SplitGearTarget(StampGearTarget("upgrade", 1), base, level);
    LIVING_CHECK(base == "upgrade" && level == 1);

    // Un-stamped payloads keep level 0 (the master/own-level runtime path).
    SplitGearTarget("sync", base, level);
    LIVING_CHECK(base == "sync" && level == 0);

    // Malformed suffixes never mis-split: the whole payload stays the base.
    SplitGearTarget("sync@", base, level);
    LIVING_CHECK(base == "sync@" && level == 0);
    SplitGearTarget("sync@7x", base, level);
    LIVING_CHECK(base == "sync@7x" && level == 0);

    // The capture gate: an uncaptured sync payload still needs the join to
    // settle or a verified master; a CAPTURED one applies regardless - the
    // crash-recovery pass has no master and no group marker left.
    SplitGearTarget(StampGearTarget("sync", 70), base, level);
    bool const capturedNeedsMaster = (base == "sync" || base == "upgrade") && level == 0;
    LIVING_CHECK(!capturedNeedsMaster);
    LIVING_CHECK(MayApplyMasterDerivedGear(capturedNeedsMaster, /*joinSettled*/ false, /*master*/ false));
}

LIVING_TEST(post_create_owner_login_policy)
{
    // Lifecycle ownership never authorizes a login by itself.
    // login=0 (not authorized) stays excluded in every mode...
    LIVING_CHECK(!MayAutoLoginPostCreateOwner(true, false, false));
    // ...deletion-pending characters never log in...
    LIVING_CHECK(!MayAutoLoginPostCreateOwner(true, true, true));
    // ...LOGIN_ONLY_ALWAYS_ACTIVE suppresses sweep logins entirely...
    LIVING_CHECK(!MayAutoLoginPostCreateOwner(false, true, false));
    // ...and an authorized entry logs in under normal autologin.
    LIVING_CHECK(MayAutoLoginPostCreateOwner(true, true, false));
}

LIVING_TEST(owner_reconciliation_preserves_login_authorization)
{
    // The owner map now carries login authorization; a failed scan keeps the
    // EXISTING owners with their authorization untouched, and a successful
    // scan is wholesale-authoritative.
    struct Owner { uint32_t account; bool mayAutoLogin;
        bool operator==(Owner const& o) const { return account == o.account && mayAutoLogin == o.mayAutoLogin; } };
    std::map<uint32_t, Owner> existing{ { 1, { 10, true } }, { 2, { 20, false } } };
    std::map<uint32_t, Owner> scanned{ { 2, { 20, false } }, { 3, { 30, true } } };
    std::map<uint32_t, Owner> const empty;

    LIVING_CHECK(ReconcilePostCreateOwners(existing, true, scanned) == scanned);
    LIVING_CHECK(ReconcilePostCreateOwners(existing, false, empty) == existing);
    LIVING_CHECK(ReconcilePostCreateOwners(existing, true, empty).empty());
}

LIVING_TEST(creation_intent_codec_and_recovery_boundaries)
{
    // The intent codec carries {identity name, original account, level,
    // login authorization, has-obligations} in a small fixed envelope. The
    // recorded identity anchors recovery to THE character transaction the
    // intent was written for - never to whatever row occupies the guid now.
    std::string name;
    uint32_t account = 0;
    uint32_t level = 0;
    bool autoAdd = false;
    bool hasObligations = false;

    LIVING_CHECK(DecodeCreationIntent(EncodeCreationIntent("Botname", 42, 70, true, true),
        name, account, level, autoAdd, hasObligations));
    LIVING_CHECK(name == "Botname" && account == 42);
    LIVING_CHECK(level == 70 && autoAdd && hasObligations);
    LIVING_CHECK(DecodeCreationIntent(EncodeCreationIntent("X", 1, 1, false, false),
        name, account, level, autoAdd, hasObligations));
    LIVING_CHECK(name == "X" && account == 1);
    LIVING_CHECK(level == 1 && !autoAdd && !hasObligations);
    LIVING_CHECK(EventValueFitsSchema("create pending",
        EncodeCreationIntent("Longestname1", 4294967295u, 4294967295u, true, true)));
    LIVING_CHECK(!DecodeCreationIntent("garbage", name, account, level, autoAdd, hasObligations));
    LIVING_CHECK(name.empty() && account == 0 && level == 0 && !autoAdd && !hasObligations);

    for (std::string const& malformed : {
            "name:X|account:1|level:1|login:1",
            "name:X|account:1|level:1|login:2|obligations:0",
            "name:X|account:1|level:1|login:1|obligations:0junk",
            "name:X|account:4294967296|level:1|login:1|obligations:0",
            "name:X|account:1|level:4294967296|login:1|obligations:0" })
        LIVING_CHECK(!DecodeCreationIntent(malformed, name, account, level, autoAdd, hasObligations));

    LIVING_CHECK(CreationIdentityMatches("Botname", 42, "Botname", 42));
    LIVING_CHECK(!CreationIdentityMatches("Botname", 42, "Other", 42));
    LIVING_CHECK(!CreationIdentityMatches("Botname", 42, "Botname", 43));
    LIVING_CHECK(!CreationIdentityMatches("", 42, "", 42));
    LIVING_CHECK(!CreationIdentityMatches("Botname", 0, "Botname", 0));

    // Crash-boundary recovery over the durable intent:
    // - before the character save committed (or after a rollback): residue;
    LIVING_CHECK(PlanCreationIntentRecovery(false, kCreationIntentPrePersistence)
        == CreationIntentRecovery::DiscardResidue);
    LIVING_CHECK(PlanCreationIntentRecovery(false, kCreationIntentFinalized)
        == CreationIntentRecovery::DiscardResidue);
    // - committed but the finalizer was never registered (the exact
    //   commit-before-Begin window) OR crashed during metadata: resume;
    LIVING_CHECK(PlanCreationIntentRecovery(true, kCreationIntentPrePersistence)
        == CreationIntentRecovery::ResumeFinalization);
    // - finalized with outstanding post-create work: rebuild the owner with
    //   the intent's persisted login authorization.
    LIVING_CHECK(PlanCreationIntentRecovery(true, kCreationIntentFinalized)
        == CreationIntentRecovery::ReconstructOwner);
    LIVING_CHECK(PlanCreationIntentRecovery(true, 0) == CreationIntentRecovery::Quarantine);
    LIVING_CHECK(PlanCreationIntentRecovery(true, 3) == CreationIntentRecovery::Quarantine);
}

LIVING_TEST(creation_login_authorization_survives_restart_via_intent)
{
    // login=1 without always-online membership: the durable intent restores
    // the authorization on reconstruction; login=0 stays unauthorized in
    // every mode - lifecycle ownership never authorizes a login by itself.
    std::string name;
    uint32_t account = 0;
    uint32_t level = 0;
    bool autoAdd = false;
    bool hasObligations = false;

    LIVING_CHECK(DecodeCreationIntent(EncodeCreationIntent("Botname", 42, 60, true, true),
        name, account, level, autoAdd, hasObligations));
    bool const alwaysMember = false; // ordinary creation never persists `always`
    bool const mayAutoLogin = alwaysMember || autoAdd;
    LIVING_CHECK(mayAutoLogin);                                          // login=1 restored
    LIVING_CHECK(MayAutoLoginPostCreateOwner(true, mayAutoLogin, false));
    LIVING_CHECK(!MayAutoLoginPostCreateOwner(false, mayAutoLogin, false)); // LOGIN_ONLY_ALWAYS_ACTIVE

    LIVING_CHECK(DecodeCreationIntent(EncodeCreationIntent("Botname", 42, 60, false, true),
        name, account, level, autoAdd, hasObligations));
    LIVING_CHECK(!(alwaysMember || autoAdd)); // login=0 preserved across restart
}

LIVING_TEST(markerless_creations_register_no_scheduler_owner)
{
    // A level-1 login=0 creation with no group/test/temporary markers has no
    // durable obligations: its intent encodes obligations=false, finalization
    // clears the intent instead of holding it, no owner is registered, and
    // recovery of a stray finalized markerless intent clears rather than
    // reconstructs.
    std::string name;
    uint32_t account = 0;
    uint32_t level = 0;
    bool autoAdd = false;
    bool hasObligations = false;
    LIVING_CHECK(DecodeCreationIntent(EncodeCreationIntent("Botname", 42, 1, false, false),
        name, account, level, autoAdd, hasObligations));
    LIVING_CHECK(!hasObligations); // -> exposure runs first, THEN the confirmed clear

    // Marker-bearing creations DO retain ownership.
    LIVING_CHECK(DecodeCreationIntent(EncodeCreationIntent("Botname", 42, 60, false, true),
        name, account, level, autoAdd, hasObligations));
    LIVING_CHECK(hasObligations);
}

LIVING_TEST(creation_intent_is_written_before_all_other_metadata)
{
    // The ordering invariant behind restart discoverability: the intent row
    // is the FIRST durable write of a creation, so if ANY metadata row
    // exists, the intent exists too and owns the residue - a failure after
    // any later write can never leave ownerless rows. Modeled over the exact
    // production write sequence with a fault at every boundary.
    std::vector<std::string> const writeOrder = {
        "create pending", "specNo", "create levelup", "create gear",
        "create group", "test", "temporary",
    };

    for (size_t failAt = 0; failAt <= writeOrder.size(); ++failAt)
    {
        std::vector<std::string> written;
        for (size_t i = 0; i < writeOrder.size() && i < failAt; ++i)
            written.push_back(writeOrder[i]);

        bool const anyRows = !written.empty();
        bool const intentOwnsResidue = !anyRows
            || std::find(written.begin(), written.end(), "create pending") != written.end();
        LIVING_CHECK(intentOwnsResidue);

        // Every possible residue is discoverable by the intent scan, and a
        // present-but-rolled-back character resolves to DiscardResidue.
        if (anyRows)
            LIVING_CHECK(PlanCreationIntentRecovery(false, kCreationIntentPrePersistence)
                == CreationIntentRecovery::DiscardResidue);
    }
}
