#include "LivingTest.h"

#include "../util/LivingCreationLifecycle.h"

using namespace living;

// CreationLifecycle is the exact transition table the asynchronous creation
// finalizer drives from execution-ordered verify callbacks and
// execution-confirmed write results. These are fault-injection tests of that
// production decision core; the live async-queue/MySQL wiring is
// compile-verified only.

LIVING_TEST(creation_lifecycle_character_save_failure_is_retryable_only_when_confirmed)
{
    // The character INSERT transaction rolled back: the verify observes a
    // confirmed absence, and since no dependent state was ever written the
    // attempt is retryable.
    CreationLifecycle rolledBack;
    LIVING_CHECK(rolledBack.OnCharacterVerify(RowVerifyOutcome::Absent, 5) == CreationStage::FailedRetryable);

    // A FAILED verify query is not absence: bounded retries, then quarantine -
    // unknown durability never becomes a retryable claim.
    CreationLifecycle unknown;
    LIVING_CHECK(unknown.OnCharacterVerify(RowVerifyOutcome::QueryFailed, 3) == CreationStage::PendingPersistence);
    LIVING_CHECK(unknown.OnCharacterVerify(RowVerifyOutcome::QueryFailed, 3) == CreationStage::PendingPersistence);
    LIVING_CHECK(unknown.OnCharacterVerify(RowVerifyOutcome::QueryFailed, 3) == CreationStage::Quarantined);

    // A row with the wrong identity is never adopted.
    CreationLifecycle mismatch;
    LIVING_CHECK(mismatch.OnCharacterVerify(RowVerifyOutcome::IdentityMismatch, 5) == CreationStage::Quarantined);
}

LIVING_TEST(creation_lifecycle_created_only_after_verified_row_and_metadata)
{
    CreationLifecycle lifecycle;

    // Verified keeps the stage: metadata has not run yet, so nothing may be
    // exposed or counted.
    LIVING_CHECK(lifecycle.OnCharacterVerify(RowVerifyOutcome::Verified, 5) == CreationStage::PendingPersistence);

    // Every required metadata write execution-confirmed -> Created.
    LIVING_CHECK(lifecycle.OnMetadataResult(true) == CreationStage::Created);

    // Terminal stages ignore later events (no resurrection of a record).
    LIVING_CHECK(lifecycle.OnCharacterVerify(RowVerifyOutcome::Absent, 5) == CreationStage::Created);
}

LIVING_TEST(creation_lifecycle_metadata_failure_requires_confirmed_cleanup)
{
    CreationLifecycle lifecycle;
    lifecycle.OnCharacterVerify(RowVerifyOutcome::Verified, 5);

    // A metadata write failed after character durability: cleanup begins;
    // nothing is retryable yet.
    LIVING_CHECK(lifecycle.OnMetadataResult(false) == CreationStage::PendingCleanup);

    // Event cleanup confirmed, deletion verified absent -> retryable.
    LIVING_CHECK(lifecycle.OnEventCleanupResult(true) == CreationStage::PendingCleanup);
    LIVING_CHECK(lifecycle.OnCleanupVerify(RowVerifyOutcome::Absent, 3) == CreationStage::FailedRetryable);
}

LIVING_TEST(creation_lifecycle_uncertain_cleanup_stays_quarantined)
{
    // Event-state cleanup failure: uncertain cleanup, quarantined.
    CreationLifecycle eventsFail;
    eventsFail.OnCharacterVerify(RowVerifyOutcome::Verified, 5);
    eventsFail.OnMetadataResult(false);
    LIVING_CHECK(eventsFail.OnEventCleanupResult(false) == CreationStage::Quarantined);

    // Character deletion that never confirms: bounded attempts, then
    // quarantine - never a retryable claim while rows may exist.
    CreationLifecycle deleteFails;
    deleteFails.OnCharacterVerify(RowVerifyOutcome::Verified, 5);
    deleteFails.OnMetadataResult(false);
    deleteFails.OnEventCleanupResult(true);
    LIVING_CHECK(deleteFails.OnCleanupVerify(RowVerifyOutcome::Verified, 3) == CreationStage::PendingCleanup);
    LIVING_CHECK(deleteFails.OnCleanupVerify(RowVerifyOutcome::QueryFailed, 3) == CreationStage::PendingCleanup);
    LIVING_CHECK(deleteFails.OnCleanupVerify(RowVerifyOutcome::Verified, 3) == CreationStage::Quarantined);

    // Quarantined is permanent: no later callback revives the record, so its
    // GUID/name capacity is never reused.
    LIVING_CHECK(deleteFails.OnCleanupVerify(RowVerifyOutcome::Absent, 3) == CreationStage::Quarantined);
    LIVING_CHECK(deleteFails.OnMetadataResult(true) == CreationStage::Quarantined);
}

LIVING_TEST(one_shot_marker_applies_effect_once_and_retries_only_the_clear)
{
    // Finding N2: a one-shot post-create marker (create gear/levelup/test) must
    // apply its runtime effect EXACTLY ONCE; if the durable clear fails or is
    // ambiguous, only the CLEAR is retried - the mutation is never replayed.
    OneShotMarker marker;
    int effects = 0;
    int clears = 0;

    auto pump = [&](bool present, EventWriteResult clearResult) -> bool
    {
        MarkerConsumeStep const step = marker.Plan(present);
        if (step == MarkerConsumeStep::ApplyThenClear)
        {
            ++effects;
            marker.OnEffectApplied();
        }
        if (step == MarkerConsumeStep::Idle)
            return false;
        ++clears;
        return marker.OnClearResult(clearResult);
    };

    // Present: apply the effect once, then the clear fails.
    LIVING_CHECK(!pump(true, EventWriteResult::DefinitelyNotApplied));
    LIVING_CHECK(effects == 1 && clears == 1);

    // Still present (clear did not land): retry the CLEAR only - no replay.
    LIVING_CHECK(!pump(true, EventWriteResult::StateUnknown));
    LIVING_CHECK(effects == 1 && clears == 2); // effect count unchanged: the replay guard

    // Confirmed clear consumes the marker.
    LIVING_CHECK(pump(true, EventWriteResult::DesiredStateConfirmed));
    LIVING_CHECK(effects == 1 && clears == 3);

    // Gone: nothing to do.
    LIVING_CHECK(!pump(false, EventWriteResult::DesiredStateConfirmed));
    LIVING_CHECK(effects == 1 && clears == 3);

    // A fresh marker for the same slot (e.g. a re-add wrote it again) applies once more.
    OneShotMarker readded;
    LIVING_CHECK(readded.Plan(true) == MarkerConsumeStep::ApplyThenClear);
}

// DurableOneShotMarker is the crash-safe consume for the DESTRUCTIVE
// post-create markers (create gear / create levelup): a durable phase
// (1 = pending, 2 = applied) confirmed BEFORE the effect, an execution-ordered
// save barrier before the clear, and a no-replay recovery rule for ambiguous
// (phase 2, fresh process) state.

namespace
{
    // One production pass over a durable marker: mirrors the consume flow in
    // LoginFreeBots. `phaseWrite` is the typed result of advancing 1 -> 2,
    // `clearWrite` of clearing; `barrier` drives the enqueue result.
    struct DurableMarkerHarness
    {
        DurableOneShotMarker ledger;
        uint32_t durablePhase = kMarkerPhasePending; // the durable row's value
        int effects = 0;
        int saves = 0;
        int barrierRequests = 0;

        bool phaseWriteConfirmed = true;
        bool clearWriteConfirmed = true;
        bool barrierEnqueueSucceeds = true;

        // Returns whether the marker is settled after the pass.
        bool Pass()
        {
            switch (ledger.Plan(durablePhase))
            {
                case DurableMarkerStep::Idle:
                    return true;
                case DurableMarkerStep::AdvanceThenApply:
                    if (!phaseWriteConfirmed)
                        return false;
                    durablePhase = kMarkerPhaseApplied;
                    if (!ledger.effectApplied)
                    {
                        ++effects;
                        ledger.OnEffectApplied();
                    }
                    ++saves;
                    ++barrierRequests;
                    ledger.OnBarrierRequested(barrierEnqueueSucceeds);
                    return false;
                case DurableMarkerStep::AwaitBarrier:
                    if (!ledger.barrierRequested)
                    {
                        ++barrierRequests;
                        ledger.OnBarrierRequested(barrierEnqueueSucceeds);
                    }
                    return false;
                case DurableMarkerStep::RecoverNoReplay:
                case DurableMarkerStep::ClearConfirmed:
                    if (ledger.OnClearResult(clearWriteConfirmed ? EventWriteResult::DesiredStateConfirmed
                                                                 : EventWriteResult::StateUnknown))
                    {
                        durablePhase = 0;
                        return true;
                    }
                    return false;
            }
            return false;
        }
    };
}

LIVING_TEST(durable_marker_clears_only_after_the_save_barrier)
{
    // The skipped/delayed-save finding: the durable intent must survive until
    // the character save PROVABLY executed - a slow database keeps the marker
    // (and its scheduler owner) alive instead of clearing a mutation that was
    // never persisted.
    DurableMarkerHarness h;

    // Pass 1: phase advanced, effect applied once, save queued, barrier requested.
    LIVING_CHECK(!h.Pass());
    LIVING_CHECK(h.effects == 1 && h.saves == 1 && h.barrierRequests == 1);
    LIVING_CHECK(h.durablePhase == kMarkerPhaseApplied);

    // Passes with the barrier still outstanding: no clear, no replay.
    LIVING_CHECK(!h.Pass());
    LIVING_CHECK(h.effects == 1 && h.barrierRequests == 1);

    // A FAILED barrier query proves nothing: the request is re-armed.
    h.ledger.OnBarrierResult(false);
    LIVING_CHECK(!h.Pass());
    LIVING_CHECK(h.barrierRequests == 2 && h.effects == 1);

    // The barrier passes: the clear runs - and an unconfirmed clear retries
    // ONLY the clear.
    h.ledger.OnBarrierResult(true);
    h.clearWriteConfirmed = false;
    LIVING_CHECK(!h.Pass());
    LIVING_CHECK(h.effects == 1 && h.durablePhase == kMarkerPhaseApplied);

    h.clearWriteConfirmed = true;
    LIVING_CHECK(h.Pass());
    LIVING_CHECK(h.effects == 1 && h.durablePhase == 0);
}

LIVING_TEST(durable_marker_unconfirmed_phase_write_never_runs_the_effect)
{
    // The phase write is the effect's precondition: while it cannot be
    // execution-confirmed, the effect must not run (recovery relies on
    // "phase 1 durably implies the effect never ran").
    DurableMarkerHarness h;
    h.phaseWriteConfirmed = false;

    LIVING_CHECK(!h.Pass());
    LIVING_CHECK(!h.Pass());
    LIVING_CHECK(h.effects == 0 && h.durablePhase == kMarkerPhasePending);

    h.phaseWriteConfirmed = true;
    LIVING_CHECK(!h.Pass());
    LIVING_CHECK(h.effects == 1);
}

LIVING_TEST(durable_marker_fresh_process_replays_only_provably_unapplied_intents)
{
    // Fresh-process reconstruction across both crash windows.

    // Crash BEFORE the confirmed phase advance (or before the effect): the
    // durable row still reads phase 1, which proves the effect never ran -
    // the restart applies it normally.
    DurableMarkerHarness afterCrashPending;
    LIVING_CHECK(!afterCrashPending.Pass());
    LIVING_CHECK(afterCrashPending.effects == 1);

    // Crash AFTER the effect ran (phase 2) - whether before or after the save
    // became durable, the fresh process cannot tell: the effect is NEVER
    // blindly replayed; the ambiguous intent is cleared (and disclosed by the
    // production log).
    for (bool saveWasDurable : { false, true })
    {
        (void)saveWasDurable; // both windows look identical to recovery
        DurableMarkerHarness recovered;
        recovered.durablePhase = kMarkerPhaseApplied; // fresh ledger, applied phase
        LIVING_CHECK(recovered.ledger.Plan(kMarkerPhaseApplied) == DurableMarkerStep::RecoverNoReplay);
        LIVING_CHECK(recovered.Pass());
        LIVING_CHECK(recovered.effects == 0); // no replay, ever
        LIVING_CHECK(recovered.durablePhase == 0);
    }

    // Crash after durability but before a CONFIRMED clear, where the clear
    // keeps failing after restart: the clear alone is retried; the effect
    // still never replays.
    DurableMarkerHarness retryClear;
    retryClear.durablePhase = kMarkerPhaseApplied;
    retryClear.clearWriteConfirmed = false;
    LIVING_CHECK(!retryClear.Pass());
    LIVING_CHECK(!retryClear.Pass());
    LIVING_CHECK(retryClear.effects == 0);
    retryClear.clearWriteConfirmed = true;
    LIVING_CHECK(retryClear.Pass());
    LIVING_CHECK(retryClear.effects == 0);
}
