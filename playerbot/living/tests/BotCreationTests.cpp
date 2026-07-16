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
