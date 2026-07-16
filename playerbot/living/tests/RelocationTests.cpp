#include "LivingTest.h"

#include "../util/LivingRelocation.h"

using namespace living;

// RelocationTracker is the exact pending-relocation registry RandomPlayerbotMgr
// uses: Begin on TeleportTo acceptance, Complete from the teleport
// acknowledgement, Cancel on logout/removal/relogin.

namespace
{
    PendingRelocation MakeRecord(uint32_t mapId, float x, float y, float z)
    {
        PendingRelocation record;
        record.mapId = mapId;
        record.x = x;
        record.y = y;
        record.z = z;
        record.setHomebind = true;
        record.homebindAreaId = 42;
        return record;
    }
}

LIVING_TEST(relocation_completes_exactly_once_on_matching_ack)
{
    RelocationTracker tracker;
    uint64_t const token = tracker.Begin(1001, MakeRecord(1, 100.0f, 200.0f, 30.0f));
    LIVING_CHECK(token != 0);
    LIVING_CHECK(tracker.HasPending(1001));

    // The acknowledged position matches the accepted destination.
    PendingRelocation completed;
    LIVING_CHECK(tracker.Complete(1001, 1, 100.0f, 200.0f, 30.0f, completed));
    LIVING_CHECK(completed.token == token);
    LIVING_CHECK(completed.setHomebind && completed.homebindAreaId == 42);

    // Exactly once: the record is gone, a second acknowledgement finalizes nothing.
    LIVING_CHECK(!tracker.HasPending(1001));
    LIVING_CHECK(!tracker.Complete(1001, 1, 100.0f, 200.0f, 30.0f, completed));
}

LIVING_TEST(relocation_small_landing_adjustment_still_matches)
{
    RelocationTracker tracker;
    tracker.Begin(1001, MakeRecord(1, 100.0f, 200.0f, 30.0f));

    PendingRelocation completed;
    LIVING_CHECK(tracker.Complete(1001, 1, 100.5f, 199.5f, 30.25f, completed));
}

LIVING_TEST(relocation_stale_or_foreign_ack_does_not_finalize)
{
    RelocationTracker tracker;
    tracker.Begin(1001, MakeRecord(1, 100.0f, 200.0f, 30.0f));

    PendingRelocation completed;

    // Wrong map: some other teleport moved the bot. Nothing finalizes and the
    // record stays pending (retry markers are never cleared by a mismatch).
    LIVING_CHECK(!tracker.Complete(1001, 2, 100.0f, 200.0f, 30.0f, completed));
    LIVING_CHECK(tracker.HasPending(1001));

    // Same map but far from the accepted destination: also stale.
    LIVING_CHECK(!tracker.Complete(1001, 1, 500.0f, 200.0f, 30.0f, completed));
    LIVING_CHECK(tracker.HasPending(1001));

    // A different bot's acknowledgement never completes this record.
    LIVING_CHECK(!tracker.Complete(2002, 1, 100.0f, 200.0f, 30.0f, completed));
    LIVING_CHECK(tracker.HasPending(1001));
}

LIVING_TEST(relocation_new_attempt_supersedes_the_stale_pending)
{
    RelocationTracker tracker;
    uint64_t const first = tracker.Begin(1001, MakeRecord(1, 100.0f, 200.0f, 30.0f));
    uint64_t const second = tracker.Begin(1001, MakeRecord(2, 700.0f, 800.0f, 90.0f));
    LIVING_CHECK(second > first);
    LIVING_CHECK(tracker.PendingCount() == 1); // bounded: one record per bot

    // An acknowledgement for the SUPERSEDED destination no longer matches...
    PendingRelocation completed;
    LIVING_CHECK(!tracker.Complete(1001, 1, 100.0f, 200.0f, 30.0f, completed));

    // ...only the current attempt completes, carrying the fresh token.
    LIVING_CHECK(tracker.Complete(1001, 2, 700.0f, 800.0f, 90.0f, completed));
    LIVING_CHECK(completed.token == second);
}

LIVING_TEST(relocation_cancellation_drops_without_completing)
{
    RelocationTracker tracker;
    tracker.Begin(1001, MakeRecord(1, 100.0f, 200.0f, 30.0f));
    tracker.Begin(2002, MakeRecord(1, 300.0f, 400.0f, 50.0f));
    LIVING_CHECK(tracker.PendingCount() == 2);

    // Logout/removal: the record is dropped, never finalized - a later
    // acknowledgement at the old destination cannot claim completion.
    tracker.Cancel(1001);
    LIVING_CHECK(!tracker.HasPending(1001));
    LIVING_CHECK(tracker.PendingCount() == 1);

    PendingRelocation completed;
    LIVING_CHECK(!tracker.Complete(1001, 1, 100.0f, 200.0f, 30.0f, completed));

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
    LIVING_CHECK(tracker.Complete(7, 1, 10.0f, 20.0f, 3.0f, completed));
    LIVING_CHECK(completed.reviveRecovery);
    LIVING_CHECK(completed.bindInn);
    LIVING_CHECK(completed.scheduleNextTeleport);
    LIVING_CHECK(completed.rpgTravelCooldown);
    LIVING_CHECK(!completed.setHomebind);
}
