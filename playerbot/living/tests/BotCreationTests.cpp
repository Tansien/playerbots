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
