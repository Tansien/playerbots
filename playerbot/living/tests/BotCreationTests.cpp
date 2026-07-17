#include "LivingTest.h"

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

    // Quota/counter updates apply to created bots only - never to failures.
    LIVING_CHECK(GroupCreationLedger::Counted(BotCreateStatus::Created));
    LIVING_CHECK(!GroupCreationLedger::Counted(BotCreateStatus::RetryableFailure));
    LIVING_CHECK(!GroupCreationLedger::Counted(BotCreateStatus::TerminalFailure));
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

LIVING_TEST(group_join_plan_clears_only_on_success_or_terminal)
{
    // Verified membership: clear as completed work.
    auto joined = PlanGroupJoinAttempt(true, true, true, 0, 10, 30);
    LIVING_CHECK(joined.decision == GroupJoinDecision::ClearJoined);

    // Deleted target: deliberately terminal, clear.
    auto deleted = PlanGroupJoinAttempt(false, false, false, 0, 10, 30);
    LIVING_CHECK(deleted.decision == GroupJoinDecision::ClearTerminal);

    // Offline target: keep the event, retry with backoff.
    auto offline = PlanGroupJoinAttempt(true, false, false, 0, 10, 30);
    LIVING_CHECK(offline.decision == GroupJoinDecision::RetryLater);
    LIVING_CHECK(offline.attemptNumber == 1);
    LIVING_CHECK(offline.retryDelaySeconds == 30);

    // Online but membership not verified (full group, declined/failed invite):
    // keep the event, retry.
    auto fullGroup = PlanGroupJoinAttempt(true, true, false, 3, 10, 30);
    LIVING_CHECK(fullGroup.decision == GroupJoinDecision::RetryLater);
    LIVING_CHECK(fullGroup.attemptNumber == 4);
    LIVING_CHECK(fullGroup.retryDelaySeconds == 120); // backoff grows with attempts

    // Bounded: the retry budget exhausts into a terminal clear, so login
    // processing cannot spam attempts forever.
    auto exhausted = PlanGroupJoinAttempt(true, true, false, 9, 10, 30);
    LIVING_CHECK(exhausted.decision == GroupJoinDecision::ClearTerminal);
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
