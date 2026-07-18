#include "LivingTest.h"

#include "../util/LivingRelocation.h"

#include <string>
#include <vector>

using namespace living;

// RelocationTracker is the exact staged pending-relocation registry
// RandomPlayerbotMgr uses: Begin on TeleportTo acceptance, Acknowledge from
// the FINISHED teleport acknowledgement (exact landing -> Finalizing, any
// other landing -> terminal cancellation), Advance for the owed completion
// operations (retried until every durable write is confirmed; the record and
// its destination reservation are released only then), Cancel on
// logout/removal/relogin. These are fault-injection tests of that production
// decision core; the live TeleportTo/MySQL wiring is exercised in-world only.

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
        return record;
    }

    // Ops whose durable writes always succeed and whose homebind path has
    // nothing to write; counters observe each operation.
    struct SpyOps
    {
        int runtimeResets = 0;
        int rpgCooldowns = 0;
        int markerClears = 0;
        int homebindApplies = 0;
        int homebindVerifyRequests = 0;
        int scheduleAttempts = 0;
        int homebindGaveUpNotices = 0;

        MarkerClearOutcome markerOutcome = MarkerClearOutcome::Cleared;
        bool scheduleSucceeds = true;
        bool homebindHasTarget = false;
        bool verifyEnqueueSucceeds = true;
        HomebindWrite homebindTarget;

        RelocationAdvanceOps Make()
        {
            RelocationAdvanceOps ops;
            ops.runtimeReset = [this]() { ++runtimeResets; };
            ops.applyRpgCooldown = [this]() { ++rpgCooldowns; };
            ops.clearReviveMarkers = [this]() { ++markerClears; return markerOutcome; };
            ops.applyHomebind = [this]() -> std::optional<HomebindWrite>
            {
                ++homebindApplies;
                if (!homebindHasTarget)
                    return std::nullopt;
                return homebindTarget;
            };
            ops.requestHomebindVerify = [this]() { ++homebindVerifyRequests; return verifyEnqueueSucceeds; };
            ops.scheduleNextTeleport = [this]() { ++scheduleAttempts; return scheduleSucceeds; };
            ops.onHomebindGaveUp = [this]() { ++homebindGaveUpNotices; };
            return ops;
        }
    };
}

LIVING_TEST(relocation_exact_ack_moves_to_finalizing_and_completes_through_advance)
{
    RelocationTracker tracker;
    uint64_t const token = tracker.Begin(1001, MakeRecord(1, 100.0f, 200.0f, 30.0f));
    LIVING_CHECK(token != 0);
    LIVING_CHECK(tracker.HasPending(1001));
    LIVING_CHECK(!tracker.IsFinalizing(1001));

    // The acknowledged landing is exactly the accepted destination: the
    // record is RETAINED (Finalizing), not consumed - completion work is owed.
    PendingRelocation acked;
    LIVING_CHECK(tracker.Acknowledge(1001, 1, 100.0f, 200.0f, 30.0f, 0.0f, acked) == RelocationAckResult::Landed);
    LIVING_CHECK(acked.token == token);
    LIVING_CHECK(tracker.HasPending(1001));
    LIVING_CHECK(tracker.IsFinalizing(1001));

    // A repeated acknowledgement resolves as AlreadyFinalizing (no erase).
    LIVING_CHECK(tracker.Acknowledge(1001, 1, 100.0f, 200.0f, 30.0f, 0.0f, acked) == RelocationAckResult::AlreadyFinalizing);

    // Advance with everything succeeding releases the record exactly once.
    SpyOps spy;
    LIVING_CHECK(tracker.Advance(1001, spy.Make()) == RelocationAdvanceResult::Completed);
    LIVING_CHECK(spy.runtimeResets == 1);
    LIVING_CHECK(!tracker.HasPending(1001));
    LIVING_CHECK(tracker.Advance(1001, spy.Make()) == RelocationAdvanceResult::NoPending);
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
        LIVING_CHECK(tracker.Acknowledge(7, 1, 100.0f + offset, 200.0f, 30.0f, 0.0f, record) == RelocationAckResult::TerminalMismatch);
        LIVING_CHECK(!tracker.HasPending(7));
        LIVING_CHECK(tracker.Acknowledge(7, 1, 100.0f, 200.0f, 30.0f, 0.0f, record) == RelocationAckResult::NoPending);
    }
}

LIVING_TEST(relocation_wrong_map_and_wrong_orientation_are_terminal)
{
    // Wrong map: a chained/redirected teleport landed elsewhere.
    RelocationTracker tracker;
    tracker.Begin(1001, MakeRecord(1, 100.0f, 200.0f, 30.0f));

    PendingRelocation record;
    LIVING_CHECK(tracker.Acknowledge(1001, 2, 100.0f, 200.0f, 30.0f, 0.0f, record) == RelocationAckResult::TerminalMismatch);
    LIVING_CHECK(!tracker.HasPending(1001));

    // Wrong orientation with identical coordinates is still a different landing.
    RelocationTracker byOrientation;
    byOrientation.Begin(1001, MakeRecord(1, 100.0f, 200.0f, 30.0f, 0.0f));
    LIVING_CHECK(byOrientation.Acknowledge(1001, 1, 100.0f, 200.0f, 30.0f, 1.5f, record) == RelocationAckResult::TerminalMismatch);
    LIVING_CHECK(!byOrientation.HasPending(1001));

    // A different bot's acknowledgement never resolves this record.
    RelocationTracker other;
    other.Begin(1001, MakeRecord(1, 100.0f, 200.0f, 30.0f));
    LIVING_CHECK(other.Acknowledge(2002, 1, 100.0f, 200.0f, 30.0f, 0.0f, record) == RelocationAckResult::NoPending);
    LIVING_CHECK(other.HasPending(1001));
}

LIVING_TEST(relocation_begin_never_silently_replaces_an_active_record)
{
    // Begin refuses while ANY record is active - PendingAck included: an
    // accepted transfer holds a density reservation and owed work, and a
    // silent replacement would leak both. Stale PendingAck records are
    // resolved through the acknowledgement path (the production pump sweeps
    // them), so the refusal cannot wedge a bot.
    RelocationTracker tracker;
    uint64_t const first = tracker.Begin(1001, MakeRecord(1, 100.0f, 200.0f, 30.0f));
    LIVING_CHECK(first != 0);
    LIVING_CHECK(tracker.Begin(1001, MakeRecord(2, 700.0f, 800.0f, 90.0f)) == 0);
    LIVING_CHECK(tracker.PendingCount() == 1);

    // The original record survived intact and its own landing still resolves.
    PendingRelocation record;
    LIVING_CHECK(tracker.Acknowledge(1001, 1, 100.0f, 200.0f, 30.0f, 0.0f, record) == RelocationAckResult::Landed);
    LIVING_CHECK(record.token == first);

    // Finalizing still refuses; completion (or cancellation) releases.
    LIVING_CHECK(tracker.Begin(1001, MakeRecord(2, 700.0f, 800.0f, 90.0f)) == 0);
    SpyOps spy;
    LIVING_CHECK(tracker.Advance(1001, spy.Make()) == RelocationAdvanceResult::Completed);
    LIVING_CHECK(tracker.Begin(1001, MakeRecord(2, 700.0f, 800.0f, 90.0f)) != 0);

    // A stale PendingAck record resolved by the pump sweep: a finished
    // landing somewhere else terminally mismatches, freeing the bot for a
    // fresh Begin - retry markers (owned by the caller) drive that attempt.
    RelocationTracker stale;
    stale.Begin(7, MakeRecord(1, 100.0f, 200.0f, 30.0f));
    LIVING_CHECK(stale.Begin(7, MakeRecord(2, 5.0f, 6.0f, 7.0f)) == 0);
    LIVING_CHECK(stale.Acknowledge(7, 1, 500.0f, 200.0f, 30.0f, 0.0f, record) == RelocationAckResult::TerminalMismatch);
    LIVING_CHECK(stale.Begin(7, MakeRecord(2, 5.0f, 6.0f, 7.0f)) != 0);
}

LIVING_TEST(relocation_finalizing_blocks_new_attempts_until_released)
{
    // While a relocation is Finalizing (owed side effects unconfirmed) every
    // path that could start another random relocation must refuse: Begin
    // returns 0 and the production preflight rejects. Only completion (or
    // cancellation) releases the bot for a new relocation.
    RelocationTracker tracker;
    tracker.Begin(1001, MakeRecord(1, 100.0f, 200.0f, 30.0f));

    PendingRelocation acked;
    tracker.Acknowledge(1001, 1, 100.0f, 200.0f, 30.0f, 0.0f, acked);
    LIVING_CHECK(tracker.IsFinalizing(1001));

    LIVING_CHECK(tracker.Begin(1001, MakeRecord(2, 5.0f, 6.0f, 7.0f)) == 0);
    LIVING_CHECK(tracker.IsFinalizing(1001)); // the finalizing record survived

    // Complete the owed work; a new attempt is then accepted again.
    SpyOps spy;
    LIVING_CHECK(tracker.Advance(1001, spy.Make()) == RelocationAdvanceResult::Completed);
    LIVING_CHECK(tracker.Begin(1001, MakeRecord(2, 5.0f, 6.0f, 7.0f)) != 0);
}

LIVING_TEST(relocation_failed_schedule_write_keeps_record_armed_and_retries)
{
    // The task-8 injection scenario: acknowledged landing, schedule write
    // fails. The record must stay Finalizing (blocking a second teleport),
    // the runtime reset must NOT re-run on retries, and a later successful
    // write must complete and release the record.
    RelocationTracker tracker;
    PendingRelocation request = MakeRecord(1, 10.0f, 20.0f, 3.0f);
    request.scheduleNextTeleport = true;
    tracker.Begin(42, request);

    PendingRelocation acked;
    tracker.Acknowledge(42, 1, 10.0f, 20.0f, 3.0f, 0.0f, acked);

    SpyOps spy;
    spy.scheduleSucceeds = false;
    LIVING_CHECK(tracker.Advance(42, spy.Make()) == RelocationAdvanceResult::Finalizing);
    LIVING_CHECK(spy.runtimeResets == 1);
    LIVING_CHECK(spy.scheduleAttempts == 1);

    // Still armed: a new relocation attempt is refused - no second teleport
    // triggered by the old expired event.
    LIVING_CHECK(tracker.Begin(42, MakeRecord(2, 1.0f, 2.0f, 3.0f)) == 0);

    // Retry with the write still failing: no duplicate runtime reset.
    LIVING_CHECK(tracker.Advance(42, spy.Make()) == RelocationAdvanceResult::Finalizing);
    LIVING_CHECK(spy.runtimeResets == 1);
    LIVING_CHECK(spy.scheduleAttempts == 2);

    // The write recovers: durable completion, record released.
    spy.scheduleSucceeds = true;
    LIVING_CHECK(tracker.Advance(42, spy.Make()) == RelocationAdvanceResult::Completed);
    LIVING_CHECK(spy.runtimeResets == 1);
    LIVING_CHECK(!tracker.HasPending(42));
}

LIVING_TEST(relocation_marker_clear_write_failure_retries_but_still_dead_completes)
{
    // A FAILED marker write retries on later advances; a bot that is simply
    // still dead keeps its markers deliberately (the event-driven revive
    // retry owns recovery) and the operation completes.
    RelocationTracker tracker;
    PendingRelocation request = MakeRecord(1, 10.0f, 20.0f, 3.0f);
    request.reviveRecovery = true;
    tracker.Begin(7, request);

    PendingRelocation acked;
    tracker.Acknowledge(7, 1, 10.0f, 20.0f, 3.0f, 0.0f, acked);

    SpyOps spy;
    spy.markerOutcome = MarkerClearOutcome::WriteFailed;
    LIVING_CHECK(tracker.Advance(7, spy.Make()) == RelocationAdvanceResult::Finalizing);
    LIVING_CHECK(spy.markerClears == 1);

    spy.markerOutcome = MarkerClearOutcome::Cleared;
    LIVING_CHECK(tracker.Advance(7, spy.Make()) == RelocationAdvanceResult::Completed);
    LIVING_CHECK(spy.markerClears == 2);

    // Still-dead completes without a marker write ever succeeding.
    RelocationTracker stillDead;
    stillDead.Begin(8, request);
    stillDead.Acknowledge(8, 1, 10.0f, 20.0f, 3.0f, 0.0f, acked);
    SpyOps deadSpy;
    deadSpy.markerOutcome = MarkerClearOutcome::KeptStillDead;
    LIVING_CHECK(stillDead.Advance(8, deadSpy.Make()) == RelocationAdvanceResult::Completed);
}

LIVING_TEST(relocation_homebind_write_is_verified_before_release)
{
    // The staged homebind chain: the write is issued once, an
    // execution-ordered verification is requested (entering flight only
    // after a SUCCESSFUL enqueue), and the record is released only after the
    // verification MATCHES. Verification callback results are ENQUEUED by
    // OnHomebindVerifyResult (no decisions in callback context), carry the
    // relocation token, and are applied from the pump.
    RelocationTracker tracker;
    PendingRelocation request = MakeRecord(1, 10.0f, 20.0f, 3.0f);
    request.setHomebind = true;
    uint64_t const token = tracker.Begin(9, request);

    PendingRelocation acked;
    tracker.Acknowledge(9, 1, 10.0f, 20.0f, 3.0f, 0.0f, acked);

    SpyOps spy;
    spy.homebindHasTarget = true;
    spy.homebindTarget = HomebindWrite{ 1, 10.0f, 20.0f, 3.0f, 42 };

    // First advance: write issued + verification requested; NOT complete.
    LIVING_CHECK(tracker.Advance(9, spy.Make()) == RelocationAdvanceResult::Finalizing);
    LIVING_CHECK(spy.homebindApplies == 1);
    LIVING_CHECK(spy.homebindVerifyRequests == 1);

    // While the verification is in flight, advancing neither re-writes nor
    // completes (well below the watchdog window).
    LIVING_CHECK(tracker.Advance(9, spy.Make()) == RelocationAdvanceResult::Finalizing);
    LIVING_CHECK(spy.homebindApplies == 1);
    LIVING_CHECK(spy.homebindVerifyRequests == 1);

    // Callback delivers a match -> enqueue only; nothing changes until the
    // event is drained and applied.
    tracker.OnHomebindVerifyResult(token, HomebindVerifyOutcome::Match);
    LIVING_CHECK(tracker.IsFinalizing(9));

    auto events = tracker.DrainHomebindVerifyEvents();
    LIVING_CHECK(events.size() == 1);
    LIVING_CHECK(events[0].relocationToken == token);
    LIVING_CHECK(tracker.ApplyHomebindVerify(events[0].relocationToken, events[0].outcome,
        RelocationTracker::kMaxHomebindWriteAttempts) == HomebindVerifyAction::Confirmed);

    LIVING_CHECK(tracker.Advance(9, spy.Make()) == RelocationAdvanceResult::Completed);
    LIVING_CHECK(spy.homebindApplies == 1); // exactly one write
}

LIVING_TEST(relocation_homebind_verify_enqueue_failure_retries_then_gives_up)
{
    // All three pinned cores report AsyncPQuery ENQUEUE failure via a false
    // return. The record must not enter AwaitingResult on a failed enqueue -
    // it retries the request on later advances within a bounded budget, and
    // exhaustion completes the record with an explicit give-up instead of
    // stranding it (and its relocation block and density reservation).
    RelocationTracker tracker;
    PendingRelocation request = MakeRecord(1, 10.0f, 20.0f, 3.0f);
    request.setHomebind = true;
    tracker.Begin(9, request);

    PendingRelocation acked;
    tracker.Acknowledge(9, 1, 10.0f, 20.0f, 3.0f, 0.0f, acked);

    SpyOps spy;
    spy.homebindHasTarget = true;
    spy.homebindTarget = HomebindWrite{ 1, 10.0f, 20.0f, 3.0f, 42 };
    spy.verifyEnqueueSucceeds = false;

    // Each advance retries the request until the bounded budget is spent.
    for (uint32_t i = 0; i < RelocationTracker::kMaxHomebindVerifyRequests; ++i)
        LIVING_CHECK(tracker.Advance(9, spy.Make()) == RelocationAdvanceResult::Finalizing);
    LIVING_CHECK(spy.homebindVerifyRequests == static_cast<int>(RelocationTracker::kMaxHomebindVerifyRequests));
    LIVING_CHECK(spy.homebindApplies == 1); // the write itself is not repeated

    // Budget exhausted: explicit give-up, observed by the caller, releases.
    LIVING_CHECK(tracker.Advance(9, spy.Make()) == RelocationAdvanceResult::Completed);
    LIVING_CHECK(spy.homebindGaveUpNotices == 1);
    LIVING_CHECK(!tracker.HasPending(9));
}

LIVING_TEST(relocation_homebind_lost_result_watchdog_recovers)
{
    // The 257th-event scenario: the bounded callback queue drops a result,
    // leaving the record AwaitingResult with nothing in flight. The watchdog
    // re-arms the request after the bounded waiting window, and the re-issued
    // verification completes the record - a lost result delays, never
    // strands.
    RelocationTracker tracker;
    PendingRelocation request = MakeRecord(1, 10.0f, 20.0f, 3.0f);
    request.setHomebind = true;
    uint64_t const token = tracker.Begin(9, request);

    PendingRelocation acked;
    tracker.Acknowledge(9, 1, 10.0f, 20.0f, 3.0f, 0.0f, acked);

    SpyOps spy;
    spy.homebindHasTarget = true;
    spy.homebindTarget = HomebindWrite{ 1, 10.0f, 20.0f, 3.0f, 42 };
    LIVING_CHECK(tracker.Advance(9, spy.Make()) == RelocationAdvanceResult::Finalizing);
    LIVING_CHECK(spy.homebindVerifyRequests == 1);

    // Fill the bounded queue with unrelated (stale-token) events...
    for (uint32_t i = 0; i < RelocationTracker::kMaxHomebindVerifyEvents; ++i)
        tracker.OnHomebindVerifyResult(999999, HomebindVerifyOutcome::QueryFailed);
    // ...so the REAL result (the 257th event) is dropped.
    tracker.OnHomebindVerifyResult(token, HomebindVerifyOutcome::Match);

    // Draining applies only stale events; none match a live record.
    for (auto const& event : tracker.DrainHomebindVerifyEvents())
        LIVING_CHECK(tracker.ApplyHomebindVerify(event.relocationToken, event.outcome,
            RelocationTracker::kMaxHomebindWriteAttempts) == HomebindVerifyAction::None);

    // The record waits out the bounded window, then the watchdog re-arms and
    // the next advance re-requests the verification.
    for (uint32_t i = 0; i <= RelocationTracker::kAwaitingResultTimeoutAdvances; ++i)
        LIVING_CHECK(tracker.Advance(9, spy.Make()) == RelocationAdvanceResult::Finalizing);
    LIVING_CHECK(tracker.Advance(9, spy.Make()) == RelocationAdvanceResult::Finalizing);
    LIVING_CHECK(spy.homebindVerifyRequests == 2);
    LIVING_CHECK(spy.homebindApplies == 1); // still the one write

    // The re-issued verification's result completes the record.
    tracker.OnHomebindVerifyResult(token, HomebindVerifyOutcome::Match);
    auto events = tracker.DrainHomebindVerifyEvents();
    LIVING_CHECK(events.size() == 1);
    LIVING_CHECK(tracker.ApplyHomebindVerify(events[0].relocationToken, events[0].outcome,
        RelocationTracker::kMaxHomebindWriteAttempts) == HomebindVerifyAction::Confirmed);
    LIVING_CHECK(tracker.Advance(9, spy.Make()) == RelocationAdvanceResult::Completed);
}

LIVING_TEST(relocation_stale_generation_verify_result_is_ignored)
{
    // A verification result from a cancelled/superseded generation must not
    // consume the CURRENT generation's retry budget or force a give-up:
    // events are token-addressed and stale tokens match nothing.
    RelocationTracker tracker;
    PendingRelocation request = MakeRecord(1, 10.0f, 20.0f, 3.0f);
    request.setHomebind = true;
    uint64_t const staleToken = tracker.Begin(5, request);

    PendingRelocation acked;
    tracker.Acknowledge(5, 1, 10.0f, 20.0f, 3.0f, 0.0f, acked);

    SpyOps spy;
    spy.homebindHasTarget = true;
    spy.homebindTarget = HomebindWrite{ 1, 10.0f, 20.0f, 3.0f, 42 };
    tracker.Advance(5, spy.Make()); // verification for staleToken in flight

    // The bot logs out; a NEW generation begins later.
    tracker.Cancel(5);
    uint64_t const currentToken = tracker.Begin(5, request);
    LIVING_CHECK(currentToken != 0 && currentToken != staleToken);
    tracker.Acknowledge(5, 1, 10.0f, 20.0f, 3.0f, 0.0f, acked);
    tracker.Advance(5, spy.Make());

    // The stale generation's late result arrives: ignored, no budget spent.
    tracker.OnHomebindVerifyResult(staleToken, HomebindVerifyOutcome::Mismatch);
    auto events = tracker.DrainHomebindVerifyEvents();
    LIVING_CHECK(events.size() == 1);
    LIVING_CHECK(tracker.ApplyHomebindVerify(events[0].relocationToken, events[0].outcome,
        RelocationTracker::kMaxHomebindWriteAttempts) == HomebindVerifyAction::None);

    // The current generation still confirms normally.
    tracker.OnHomebindVerifyResult(currentToken, HomebindVerifyOutcome::Match);
    events = tracker.DrainHomebindVerifyEvents();
    LIVING_CHECK(tracker.ApplyHomebindVerify(events[0].relocationToken, events[0].outcome,
        RelocationTracker::kMaxHomebindWriteAttempts) == HomebindVerifyAction::Confirmed);
    LIVING_CHECK(tracker.Advance(5, spy.Make()) == RelocationAdvanceResult::Completed);
}

LIVING_TEST(relocation_homebind_mismatch_reissues_then_bounded_gives_up)
{
    // A failed/mismatched verification re-arms the write for the next
    // advance; bounded attempts, then GaveUp completes the operation with an
    // explicit error instead of blocking the bot's relocations forever.
    RelocationTracker tracker;
    PendingRelocation request = MakeRecord(1, 10.0f, 20.0f, 3.0f);
    request.setHomebind = true;
    uint64_t const token = tracker.Begin(9, request);

    PendingRelocation acked;
    tracker.Acknowledge(9, 1, 10.0f, 20.0f, 3.0f, 0.0f, acked);

    SpyOps spy;
    spy.homebindHasTarget = true;
    spy.homebindTarget = HomebindWrite{ 1, 10.0f, 20.0f, 3.0f, 42 };

    uint32_t const maxAttempts = RelocationTracker::kMaxHomebindWriteAttempts;
    for (uint32_t attempt = 1; attempt < maxAttempts; ++attempt)
    {
        LIVING_CHECK(tracker.Advance(9, spy.Make()) == RelocationAdvanceResult::Finalizing);
        LIVING_CHECK(spy.homebindApplies == static_cast<int>(attempt));

        tracker.OnHomebindVerifyResult(token, HomebindVerifyOutcome::Mismatch);
        auto events = tracker.DrainHomebindVerifyEvents();
        LIVING_CHECK(tracker.ApplyHomebindVerify(events[0].relocationToken, events[0].outcome, maxAttempts)
            == HomebindVerifyAction::Reissue);
    }

    // The final failed attempt exhausts the budget: GaveUp, then release.
    LIVING_CHECK(tracker.Advance(9, spy.Make()) == RelocationAdvanceResult::Finalizing);
    tracker.OnHomebindVerifyResult(token, HomebindVerifyOutcome::QueryFailed);
    auto events = tracker.DrainHomebindVerifyEvents();
    LIVING_CHECK(tracker.ApplyHomebindVerify(events[0].relocationToken, events[0].outcome, maxAttempts)
        == HomebindVerifyAction::GaveUp);
    LIVING_CHECK(tracker.Advance(9, spy.Make()) == RelocationAdvanceResult::Completed);

    // A stale verification result after release resolves to None.
    tracker.OnHomebindVerifyResult(token, HomebindVerifyOutcome::Match);
    events = tracker.DrainHomebindVerifyEvents();
    LIVING_CHECK(tracker.ApplyHomebindVerify(events[0].relocationToken, events[0].outcome, maxAttempts)
        == HomebindVerifyAction::None);
}

LIVING_TEST(relocation_homebind_nothing_to_write_completes_by_absence)
{
    // bindInn with no eligible inn: the operation completes by absence, no
    // verification is requested.
    RelocationTracker tracker;
    PendingRelocation request = MakeRecord(1, 10.0f, 20.0f, 3.0f);
    request.bindInn = true;
    tracker.Begin(9, request);

    PendingRelocation acked;
    tracker.Acknowledge(9, 1, 10.0f, 20.0f, 3.0f, 0.0f, acked);

    SpyOps spy;
    spy.homebindHasTarget = false;
    LIVING_CHECK(tracker.Advance(9, spy.Make()) == RelocationAdvanceResult::Completed);
    LIVING_CHECK(spy.homebindApplies == 1);
    LIVING_CHECK(spy.homebindVerifyRequests == 0);
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
    LIVING_CHECK(tracker.Acknowledge(1001, 1, 100.0f, 200.0f, 30.0f, 0.0f, record) == RelocationAckResult::NoPending);

    // Cancelling one bot never touches another's pending relocation.
    LIVING_CHECK(tracker.HasPending(2002));

    // Cancelling a bot with nothing pending is a no-op.
    tracker.Cancel(9999);
    LIVING_CHECK(tracker.PendingCount() == 1);

    // A Finalizing record is cancellable too (logout during finalization
    // releases the reservation; retry markers stay with the caller).
    tracker.Acknowledge(2002, 1, 300.0f, 400.0f, 50.0f, 0.0f, record);
    LIVING_CHECK(tracker.IsFinalizing(2002));
    tracker.Cancel(2002);
    LIVING_CHECK(tracker.PendingCount() == 0);
}

LIVING_TEST(relocation_revive_flags_travel_with_the_record)
{
    // The revive path defers marker clearing to completion: the flags must
    // arrive intact in the acknowledged record - there is no other channel.
    RelocationTracker tracker;
    PendingRelocation record = MakeRecord(1, 10.0f, 20.0f, 3.0f);
    record.setHomebind = false;
    record.reviveRecovery = true;
    record.bindInn = true;
    record.scheduleNextTeleport = true;
    record.rpgTravelCooldown = true;
    tracker.Begin(7, record);

    PendingRelocation acked;
    LIVING_CHECK(tracker.Acknowledge(7, 1, 10.0f, 20.0f, 3.0f, 0.0f, acked) == RelocationAckResult::Landed);
    LIVING_CHECK(acked.reviveRecovery);
    LIVING_CHECK(acked.bindInn);
    LIVING_CHECK(acked.scheduleNextTeleport);
    LIVING_CHECK(acked.rpgTravelCooldown);
    LIVING_CHECK(!acked.setHomebind);
}

LIVING_TEST(relocation_density_reservations_count_both_stages_and_exclude_self)
{
    // The task-9 scenario: destination capacity one, two bots attempting
    // in-flight relocations to the same destination. The second bot's
    // admission must see the first bot's reservation - in BOTH stages - while
    // never counting its own record.
    RelocationTracker tracker;
    tracker.Begin(1, MakeRecord(0, 100.0f, 200.0f, 30.0f));

    // Bot 2's admission: one reservation within the radius -> at a capacity
    // of one, 1 (reserved) >= 1 (cap) rejects the second relocation.
    LIVING_CHECK(tracker.CountReservedDestinationsNear(0, 100.0f, 200.0f, 50.0f, /*exclude*/ 2) == 1);

    // The same query for bot 1 itself excludes its own record.
    LIVING_CHECK(tracker.CountReservedDestinationsNear(0, 100.0f, 200.0f, 50.0f, /*exclude*/ 1) == 0);

    // Outside the radius or on another map the reservation does not count
    // (2D distance, matching the live scan's same-map fDist metric).
    LIVING_CHECK(tracker.CountReservedDestinationsNear(0, 100.0f, 300.0f, 50.0f, 2) == 0);
    LIVING_CHECK(tracker.CountReservedDestinationsNear(1, 100.0f, 200.0f, 50.0f, 2) == 0);

    // The reservation persists through Finalizing (landed, side effects
    // unconfirmed)...
    PendingRelocation acked;
    tracker.Acknowledge(1, 0, 100.0f, 200.0f, 30.0f, 0.0f, acked);
    LIVING_CHECK(tracker.IsFinalizing(1));
    LIVING_CHECK(tracker.CountReservedDestinationsNear(0, 100.0f, 200.0f, 50.0f, 2) == 1);

    // ...and is released only on confirmed completion.
    SpyOps spy;
    LIVING_CHECK(tracker.Advance(1, spy.Make()) == RelocationAdvanceResult::Completed);
    LIVING_CHECK(tracker.CountReservedDestinationsNear(0, 100.0f, 200.0f, 50.0f, 2) == 0);

    // Cancellation (logout) also releases.
    tracker.Begin(3, MakeRecord(0, 100.0f, 200.0f, 30.0f));
    LIVING_CHECK(tracker.CountReservedDestinationsNear(0, 100.0f, 200.0f, 50.0f, 2) == 1);
    tracker.Cancel(3);
    LIVING_CHECK(tracker.CountReservedDestinationsNear(0, 100.0f, 200.0f, 50.0f, 2) == 0);

    // A terminal mismatch releases as well.
    tracker.Begin(4, MakeRecord(0, 100.0f, 200.0f, 30.0f));
    tracker.Acknowledge(4, 0, 999.0f, 200.0f, 30.0f, 0.0f, acked);
    LIVING_CHECK(tracker.CountReservedDestinationsNear(0, 100.0f, 200.0f, 50.0f, 2) == 0);
}

LIVING_TEST(relocation_override_is_tried_first_but_never_replaces_the_original)
{
    // The task-10 scenario: the Dark Portal override draw fires while map 0
    // is disabled/inactive/full. The attempt's tuple order must still contain
    // the original candidate AFTER the rejected override, so a validator that
    // rejects map 0 falls back to the valid raw candidate in the SAME attempt
    // - "optional override rejected" is structurally distinct from "active
    // candidates exhausted".
    struct Loc { uint32_t mapId; float x; };
    Loc const portal{ 0, -11772.43f };
    Loc const original{ 1, 500.0f };

    // Deterministic drawn override: portal first, original second.
    std::vector<Loc> drawn = OverrideThenOriginal(true, portal, original);
    LIVING_CHECK(drawn.size() == 2);
    LIVING_CHECK(drawn[0].mapId == 0);
    LIVING_CHECK(drawn[1].mapId == 1);

    // A validator rejecting map 0 (disabled/inactive/over-density) still
    // reaches the valid original candidate within this attempt.
    bool acceptedOriginal = false;
    for (Loc const& loc : drawn)
    {
        if (loc.mapId == 0)
            continue; // override rejected
        acceptedOriginal = true;
        break;
    }
    LIVING_CHECK(acceptedOriginal);

    // No draw: the original is the only tuple.
    std::vector<Loc> notDrawn = OverrideThenOriginal(false, portal, original);
    LIVING_CHECK(notDrawn.size() == 1);
    LIVING_CHECK(notDrawn[0].mapId == 1);
}

LIVING_TEST(relocation_homebind_verify_queue_is_bounded_and_drains_in_order)
{
    RelocationTracker tracker;
    for (uint64_t token = 1; token <= RelocationTracker::kMaxHomebindVerifyEvents + 10; ++token)
        tracker.OnHomebindVerifyResult(token, HomebindVerifyOutcome::Match);

    auto events = tracker.DrainHomebindVerifyEvents();
    LIVING_CHECK(events.size() == RelocationTracker::kMaxHomebindVerifyEvents);
    LIVING_CHECK(events.front().relocationToken == 1);
    LIVING_CHECK(tracker.DrainHomebindVerifyEvents().empty());
}

// Destination-eligibility policy: the SAME rules the one production validator
// (IsEligibleTeleportDestination) runs on the FINAL jittered/ground-adjusted
// tuple. Terrain/vertical area resolution itself is core-bound and only
// exercised in-world; these cover the policy boundaries.

LIVING_TEST(relocation_destination_enemy_zone_boundaries)
{
    // Enemy zone blocks below 21; level 21 is allowed.
    LIVING_CHECK(DestinationBlockedByEnemyZone(true, false, 20));
    LIVING_CHECK(!DestinationBlockedByEnemyZone(true, false, 21));

    // Enemy CAPITALS block at any level.
    LIVING_CHECK(DestinationBlockedByEnemyZone(true, true, 60));

    // Friendly/neutral zones never block, capital or not.
    LIVING_CHECK(!DestinationBlockedByEnemyZone(false, true, 5));

    // Sub-zone (area team) rule: below 21 only.
    LIVING_CHECK(DestinationBlockedByEnemyArea(true, 20));
    LIVING_CHECK(!DestinationBlockedByEnemyArea(true, 21));
    LIVING_CHECK(!DestinationBlockedByEnemyArea(false, 20));
}

LIVING_TEST(relocation_destination_starter_zone_boundaries)
{
    // Elwynn (12) admits humans (race 1) only, below level 30.
    LIVING_CHECK(DestinationBlockedByStarterZone(12, 4, true, 29, true));  // night elf
    LIVING_CHECK(!DestinationBlockedByStarterZone(12, 1, true, 29, true)); // human
    LIVING_CHECK(!DestinationBlockedByStarterZone(12, 4, true, 30, true)); // level 30+

    // Durotar (14) admits orcs (2) and trolls (8).
    LIVING_CHECK(!DestinationBlockedByStarterZone(14, 2, false, 10, true));
    LIVING_CHECK(!DestinationBlockedByStarterZone(14, 8, false, 10, true));
    LIVING_CHECK(DestinationBlockedByStarterZone(14, 6, false, 10, true)); // tauren

    // Redridge (44) / Duskwood (10) are team-gated, not race-gated.
    LIVING_CHECK(DestinationBlockedByStarterZone(44, 2, false, 10, true));
    LIVING_CHECK(!DestinationBlockedByStarterZone(44, 1, true, 10, true));

    // TBC starter zones apply only where the expansion races exist.
    LIVING_CHECK(DestinationBlockedByStarterZone(3524, 1, true, 10, true));   // human in Azuremyst
    LIVING_CHECK(!DestinationBlockedByStarterZone(3524, 11, true, 10, true)); // draenei
    LIVING_CHECK(!DestinationBlockedByStarterZone(3524, 1, true, 10, false)); // classic core
    LIVING_CHECK(DestinationBlockedByStarterZone(3430, 1, false, 10, true));  // Eversong
    LIVING_CHECK(!DestinationBlockedByStarterZone(3430, 10, false, 10, true)); // blood elf

    // Non-starter zones never block.
    LIVING_CHECK(!DestinationBlockedByStarterZone(33, 1, true, 10, true)); // Stranglethorn
}
