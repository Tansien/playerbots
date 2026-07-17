#include "LivingTest.h"

#include "../util/LivingRelocation.h"

using namespace living;

// RelocationTracker is the exact pending-relocation registry RandomPlayerbotMgr
// uses: Begin on TeleportTo acceptance, Complete from the FINISHED teleport
// acknowledgement, Cancel on logout/removal/relogin. Completion requires the
// exact accepted destination (the pinned cores install the stored components
// directly); any other finished landing terminally cancels the obsolete record.

namespace
{
    PendingRelocation MakeRecord(uint32_t mapId, float x, float y, float z, float o = 0.0f)
    {
        PendingRelocation record;
        record.mapId = mapId;
        record.x = x;
        record.y = y;
        record.z = z;
        record.orientation = o;
        record.setHomebind = true;
        record.homebindAreaId = 42;
        return record;
    }
}

LIVING_TEST(relocation_completes_exactly_once_on_exact_ack)
{
    RelocationTracker tracker;
    uint64_t const token = tracker.Begin(1001, MakeRecord(1, 100.0f, 200.0f, 30.0f));
    LIVING_CHECK(token != 0);
    LIVING_CHECK(tracker.HasPending(1001));

    // The acknowledged landing is exactly the accepted destination.
    PendingRelocation completed;
    LIVING_CHECK(tracker.Complete(1001, 1, 100.0f, 200.0f, 30.0f, 0.0f, completed) == RelocationCompleteResult::Completed);
    LIVING_CHECK(completed.token == token);
    LIVING_CHECK(completed.setHomebind && completed.homebindAreaId == 42);

    // Exactly once: the record is gone, a second acknowledgement finds nothing.
    LIVING_CHECK(!tracker.HasPending(1001));
    LIVING_CHECK(tracker.Complete(1001, 1, 100.0f, 200.0f, 30.0f, 0.0f, completed) == RelocationCompleteResult::NoPending);
}

LIVING_TEST(relocation_any_displacement_is_a_terminal_mismatch)
{
    // The cores install the exact destination on acknowledgement, so even a
    // 0.25-yard displacement is a different landing - the old gameplay-distance
    // tolerance let a foreign teleport finalize this relocation. The obsolete
    // record is ERASED (terminal), never left armed for a later landing.
    float const offsets[] = { 0.25f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 500.0f };
    for (float const offset : offsets)
    {
        RelocationTracker tracker;
        tracker.Begin(7, MakeRecord(1, 100.0f, 200.0f, 30.0f));

        PendingRelocation record;
        LIVING_CHECK(tracker.Complete(7, 1, 100.0f + offset, 200.0f, 30.0f, 0.0f, record) == RelocationCompleteResult::TerminalMismatch);
        LIVING_CHECK(!tracker.HasPending(7));
        LIVING_CHECK(tracker.Complete(7, 1, 100.0f, 200.0f, 30.0f, 0.0f, record) == RelocationCompleteResult::NoPending);
    }
}

LIVING_TEST(relocation_wrong_map_and_wrong_orientation_are_terminal)
{
    // Wrong map: a chained/redirected teleport landed elsewhere.
    RelocationTracker tracker;
    tracker.Begin(1001, MakeRecord(1, 100.0f, 200.0f, 30.0f));

    PendingRelocation record;
    LIVING_CHECK(tracker.Complete(1001, 2, 100.0f, 200.0f, 30.0f, 0.0f, record) == RelocationCompleteResult::TerminalMismatch);
    LIVING_CHECK(!tracker.HasPending(1001));

    // Wrong orientation with identical coordinates is still a different landing.
    RelocationTracker byOrientation;
    byOrientation.Begin(1001, MakeRecord(1, 100.0f, 200.0f, 30.0f, 0.0f));
    LIVING_CHECK(byOrientation.Complete(1001, 1, 100.0f, 200.0f, 30.0f, 1.5f, record) == RelocationCompleteResult::TerminalMismatch);
    LIVING_CHECK(!byOrientation.HasPending(1001));

    // A different bot's acknowledgement never resolves this record.
    RelocationTracker other;
    other.Begin(1001, MakeRecord(1, 100.0f, 200.0f, 30.0f));
    LIVING_CHECK(other.Complete(2002, 1, 100.0f, 200.0f, 30.0f, 0.0f, record) == RelocationCompleteResult::NoPending);
    LIVING_CHECK(other.HasPending(1001));
}

LIVING_TEST(relocation_supersession_and_stale_ack)
{
    // One in-flight relocation per bot: a newer attempt explicitly supersedes.
    RelocationTracker tracker;
    uint64_t const first = tracker.Begin(1001, MakeRecord(1, 100.0f, 200.0f, 30.0f));
    uint64_t const second = tracker.Begin(1001, MakeRecord(2, 700.0f, 800.0f, 90.0f));
    LIVING_CHECK(second > first);
    LIVING_CHECK(tracker.PendingCount() == 1); // bounded: one record per bot

    // A stale acknowledgement at the SUPERSEDED destination is a finished
    // landing somewhere other than the current record's destination: terminal.
    // The current attempt's own completion can then never falsely fire, and
    // retry markers (owned by the caller) drive a fresh attempt.
    PendingRelocation record;
    LIVING_CHECK(tracker.Complete(1001, 1, 100.0f, 200.0f, 30.0f, 0.0f, record) == RelocationCompleteResult::TerminalMismatch);
    LIVING_CHECK(record.token == second); // the erased record was the current one
    LIVING_CHECK(!tracker.HasPending(1001));

    // Clean supersession: the second attempt's own exact landing completes.
    RelocationTracker clean;
    clean.Begin(1001, MakeRecord(1, 100.0f, 200.0f, 30.0f));
    uint64_t const current = clean.Begin(1001, MakeRecord(2, 700.0f, 800.0f, 90.0f));
    LIVING_CHECK(clean.Complete(1001, 2, 700.0f, 800.0f, 90.0f, 0.0f, record) == RelocationCompleteResult::Completed);
    LIVING_CHECK(record.token == current);
}

LIVING_TEST(relocation_cancellation_drops_without_completing)
{
    RelocationTracker tracker;
    tracker.Begin(1001, MakeRecord(1, 100.0f, 200.0f, 30.0f));
    tracker.Begin(2002, MakeRecord(1, 300.0f, 400.0f, 50.0f));
    LIVING_CHECK(tracker.PendingCount() == 2);

    // Logout/removal: the record is dropped, never finalized - a later
    // acknowledgement at the old destination finds nothing.
    tracker.Cancel(1001);
    LIVING_CHECK(!tracker.HasPending(1001));
    LIVING_CHECK(tracker.PendingCount() == 1);

    PendingRelocation record;
    LIVING_CHECK(tracker.Complete(1001, 1, 100.0f, 200.0f, 30.0f, 0.0f, record) == RelocationCompleteResult::NoPending);

    // Cancelling one bot never touches another's pending relocation.
    LIVING_CHECK(tracker.HasPending(2002));

    // Cancelling a bot with nothing pending is a no-op.
    tracker.Cancel(9999);
    LIVING_CHECK(tracker.PendingCount() == 1);
}

LIVING_TEST(relocation_revive_flags_travel_with_the_record)
{
    // The revive path defers marker clearing to completion: the flags must
    // arrive intact in the completed record - there is no other channel.
    RelocationTracker tracker;
    PendingRelocation record = MakeRecord(1, 10.0f, 20.0f, 3.0f);
    record.setHomebind = false;
    record.reviveRecovery = true;
    record.bindInn = true;
    record.scheduleNextTeleport = true;
    record.rpgTravelCooldown = true;
    tracker.Begin(7, record);

    PendingRelocation completed;
    LIVING_CHECK(tracker.Complete(7, 1, 10.0f, 20.0f, 3.0f, 0.0f, completed) == RelocationCompleteResult::Completed);
    LIVING_CHECK(completed.reviveRecovery);
    LIVING_CHECK(completed.bindInn);
    LIVING_CHECK(completed.scheduleNextTeleport);
    LIVING_CHECK(completed.rpgTravelCooldown);
    LIVING_CHECK(!completed.setHomebind);
}
