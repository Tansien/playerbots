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

// DurableOneShotMarker is the fingerprint-verified consume for the
// DESTRUCTIVE post-create markers (create gear / create levelup). The model
// below mirrors the production pass exactly: equipment "hashes" are abstract
// state values, the durable row is (phase, data), a save copies live state to
// durable state (or rolls back), and the verify readback compares durable
// state against the recorded POST fingerprint. Restart() reloads live state
// from durable state with a fresh in-memory ledger.

namespace
{
    struct DurableMarkerHarness
    {
        DurableOneShotMarker ledger;
        uint32_t durablePhase = kMarkerPhasePending;
        std::string durableData = "epic";
        uint64_t liveState = 100;    // abstract equipment fingerprint
        uint64_t durableState = 100; // last committed save
        uint64_t nextStateAfterEffect = 200;
        int effects = 0;
        int saves = 0;
        int verifyRequests = 0;

        bool phaseWriteConfirmed = true;
        bool clearWriteConfirmed = true;
        bool verifyEnqueueSucceeds = true;
        bool savesCommit = true; // false simulates rollback

        uint32_t lastRequestedGeneration = 0;
        uint64_t lastExpectedHash = 0;

        bool Pass()
        {
            auto clearProven = [&]() -> bool
            {
                if (ledger.OnClearResult(clearWriteConfirmed ? EventWriteResult::DesiredStateConfirmed
                                                             : EventWriteResult::StateUnknown))
                {
                    durablePhase = 0;
                    return true;
                }
                return false;
            };

            auto recordThenSaveAndVerify = [&]() -> bool
            {
                if (!ledger.phaseRecorded)
                {
                    if (!phaseWriteConfirmed)
                        return false;
                    durablePhase = kMarkerPhaseApplied;
                    durableData = EncodeDurableMarkerData(ledger.originalData, ledger.preHash, ledger.postHash);
                    ledger.OnPhaseRecorded();
                }
                ++saves;
                if (savesCommit)
                    durableState = liveState; // the queued save "executes"
                ++verifyRequests;
                lastRequestedGeneration = ledger.BeginVerify();
                lastExpectedHash = ledger.postHash;
                ledger.OnVerifyRequested(verifyEnqueueSucceeds);
                return false;
            };

            switch (ledger.Plan(durablePhase))
            {
                case DurableMarkerStep::Idle:
                    return true;
                case DurableMarkerStep::ApplyThenRecord:
                {
                    uint64_t const pre = liveState;
                    liveState = nextStateAfterEffect;
                    ++effects;
                    ledger.OnEffectApplied(pre, liveState, durableData);
                    return recordThenSaveAndVerify();
                }
                case DurableMarkerStep::RecordApplied:
                case DurableMarkerStep::SaveAndVerify:
                    return recordThenSaveAndVerify();
                case DurableMarkerStep::AwaitVerify:
                    if (ledger.TickAwaitingVerify() && !ledger.quarantined)
                    {
                        ++verifyRequests;
                        lastRequestedGeneration = ledger.BeginVerify();
                        ledger.OnVerifyRequested(verifyEnqueueSucceeds);
                    }
                    return false;
                case DurableMarkerStep::ClearConfirmed:
                    return clearProven();
                case DurableMarkerStep::RecoverProbe:
                {
                    std::string original;
                    uint64_t pre = 0, post = 0;
                    if (!TryDecodeDurableMarkerData(durableData, original, pre, post))
                    {
                        ledger.MarkQuarantined();
                        return true;
                    }
                    switch (DurableOneShotMarker::Reconcile(liveState, pre, post))
                    {
                        case DurableOneShotMarker::RecoverDecision::ProvenDurable:
                            ledger.MarkProven();
                            return clearProven();
                        case DurableOneShotMarker::RecoverDecision::ReapplySafe:
                            durablePhase = kMarkerPhasePending;
                            durableData = original;
                            return false;
                        case DurableOneShotMarker::RecoverDecision::Ambiguous:
                        default:
                            ledger.MarkQuarantined();
                            return true;
                    }
                }
                case DurableMarkerStep::Quarantined:
                    return true;
            }
            return false;
        }

        // Delivers the outstanding verify result for `generation` by comparing
        // the DURABLE state against the recorded expectation.
        MarkerVerifyAction DeliverVerify(uint32_t generation)
        {
            return ledger.OnVerifyResult(generation,
                durableState == lastExpectedHash ? DurableOneShotMarker::VerifyOutcome::Match
                                                 : DurableOneShotMarker::VerifyOutcome::Mismatch);
        }

        void Restart()
        {
            ledger = DurableOneShotMarker{};
            liveState = durableState; // a fresh process loads the durable state
        }
    };
}

LIVING_TEST(durable_marker_effect_before_record_and_fingerprint_verified_clear)
{
    // Happy path: effect first, phase-2 record after, save verified against
    // the recorded POST fingerprint, only then the confirmed clear. An
    // unconfirmed clear retries alone.
    DurableMarkerHarness h;

    LIVING_CHECK(!h.Pass()); // apply + record + save + request verify
    LIVING_CHECK(h.effects == 1 && h.saves == 1 && h.verifyRequests == 1);
    LIVING_CHECK(h.durablePhase == kMarkerPhaseApplied);

    LIVING_CHECK(h.DeliverVerify(h.lastRequestedGeneration) == MarkerVerifyAction::Proven);
    h.clearWriteConfirmed = false;
    LIVING_CHECK(!h.Pass()); // proven, but the clear did not confirm
    LIVING_CHECK(h.durablePhase == kMarkerPhaseApplied);
    h.clearWriteConfirmed = true;
    LIVING_CHECK(h.Pass());
    LIVING_CHECK(h.durablePhase == 0 && h.effects == 1);
}

LIVING_TEST(durable_marker_unconfirmed_record_never_loses_the_effect)
{
    // The phase-2 record cannot be confirmed: the effect ran once in this
    // process and is NOT re-applied while the record retries. A crash in this
    // window leaves durable phase 1 with pre-effect durable state, so the
    // restart re-applies as a safe first application - no silent loss.
    DurableMarkerHarness h;
    h.phaseWriteConfirmed = false;

    LIVING_CHECK(!h.Pass());
    LIVING_CHECK(!h.Pass());
    LIVING_CHECK(h.effects == 1 && h.saves == 0); // no save before the record
    LIVING_CHECK(h.durablePhase == kMarkerPhasePending);

    h.Restart(); // crash: durable state is still pre-effect
    h.phaseWriteConfirmed = true; // the database recovered after the restart
    LIVING_CHECK(h.liveState == 100);
    LIVING_CHECK(!h.Pass()); // re-applies (first durable application)
    LIVING_CHECK(h.DeliverVerify(h.lastRequestedGeneration) == MarkerVerifyAction::Proven);
    LIVING_CHECK(h.Pass());
    LIVING_CHECK(h.durablePhase == 0);
}

LIVING_TEST(durable_marker_crash_after_record_before_save_reapplies_safely)
{
    // Boundary: phase transition recorded, crash before the save executed.
    // Recovery reconciles: durable state matches the recorded PRE fingerprint,
    // so the intent is provably lost - the marker rewinds to phase 1 and the
    // effect re-applies. No silent loss, and the durable state never held the
    // effect twice.
    DurableMarkerHarness h;
    h.savesCommit = false; // the queued save never executes before the crash
    LIVING_CHECK(!h.Pass());
    LIVING_CHECK(h.durablePhase == kMarkerPhaseApplied && h.durableState == 100);

    h.Restart();
    LIVING_CHECK(!h.Pass()); // RecoverProbe: pre-match -> rewind to phase 1
    LIVING_CHECK(h.durablePhase == kMarkerPhasePending);
    LIVING_CHECK(h.durableData == "epic"); // original payload restored verbatim

    h.savesCommit = true;
    LIVING_CHECK(!h.Pass()); // fresh application
    LIVING_CHECK(h.effects == 2); // once per process; durable state applied once
    LIVING_CHECK(h.DeliverVerify(h.lastRequestedGeneration) == MarkerVerifyAction::Proven);
    LIVING_CHECK(h.Pass());
    LIVING_CHECK(h.durableState == h.liveState && h.durablePhase == 0);
}

LIVING_TEST(durable_marker_save_rollback_is_detected_and_bounded)
{
    // Boundary: the save is queued but rolls back. The fingerprint readback
    // reports Mismatch (a bare ordering barrier would have claimed success);
    // the save is re-issued bounded, and persistent failure QUARANTINES the
    // marker - retained with an error, never silently cleared.
    DurableMarkerHarness h;
    h.savesCommit = false;

    LIVING_CHECK(!h.Pass());
    for (uint32_t i = 0; i < DurableOneShotMarker::kMaxVerifyAttempts - 1; ++i)
    {
        LIVING_CHECK(h.DeliverVerify(h.lastRequestedGeneration) == MarkerVerifyAction::RetrySave);
        LIVING_CHECK(!h.Pass()); // re-save + re-verify
    }
    LIVING_CHECK(h.DeliverVerify(h.lastRequestedGeneration) == MarkerVerifyAction::Quarantined);
    LIVING_CHECK(h.Pass()); // quarantined: released for scheduling...
    LIVING_CHECK(h.durablePhase == kMarkerPhaseApplied); // ...but the row is RETAINED
    LIVING_CHECK(h.effects == 1); // never replayed

    // A transient rollback recovers: mismatch, then the re-save commits.
    DurableMarkerHarness recovers;
    recovers.savesCommit = false;
    LIVING_CHECK(!recovers.Pass());
    LIVING_CHECK(recovers.DeliverVerify(recovers.lastRequestedGeneration) == MarkerVerifyAction::RetrySave);
    recovers.savesCommit = true;
    LIVING_CHECK(!recovers.Pass());
    LIVING_CHECK(recovers.DeliverVerify(recovers.lastRequestedGeneration) == MarkerVerifyAction::Proven);
    LIVING_CHECK(recovers.Pass());
    LIVING_CHECK(recovers.effects == 1 && recovers.durablePhase == 0);
}

LIVING_TEST(durable_marker_lost_callback_watchdog_rearms_and_ignores_stale)
{
    // Boundary: save completed but the verification callback never arrives.
    // The watchdog abandons the wait after the bounded pass budget, re-arms
    // under a NEW generation, and the stale (old-generation) callback that
    // straggles in later is ignored - only the current generation may settle.
    DurableMarkerHarness h;
    LIVING_CHECK(!h.Pass());
    uint32_t const staleGeneration = h.lastRequestedGeneration;

    for (uint32_t i = 0; i < DurableOneShotMarker::kMaxAwaitVerifyPasses; ++i)
        LIVING_CHECK(!h.Pass()); // waiting, no re-request yet
    LIVING_CHECK(h.verifyRequests == 1);

    LIVING_CHECK(!h.Pass()); // timeout: re-armed under a new generation
    LIVING_CHECK(h.verifyRequests == 2);
    LIVING_CHECK(h.lastRequestedGeneration != staleGeneration);

    // The stale callback arrives now: ignored entirely.
    LIVING_CHECK(h.ledger.OnVerifyResult(staleGeneration,
        DurableOneShotMarker::VerifyOutcome::Match) == MarkerVerifyAction::None);
    LIVING_CHECK(!h.ledger.postconditionProven);

    // The current generation settles the owner.
    LIVING_CHECK(h.DeliverVerify(h.lastRequestedGeneration) == MarkerVerifyAction::Proven);
    LIVING_CHECK(h.Pass());
    LIVING_CHECK(h.durablePhase == 0 && h.effects == 1);
}

LIVING_TEST(durable_marker_restart_boundaries_prove_or_quarantine)
{
    // Save completed + crash before the verify result: recovery reconciles
    // POST-match -> proven -> clear, without replay.
    DurableMarkerHarness afterSave;
    LIVING_CHECK(!afterSave.Pass());
    afterSave.Restart();
    LIVING_CHECK(afterSave.Pass()); // RecoverProbe: post-match -> proven -> cleared
    LIVING_CHECK(afterSave.durablePhase == 0 && afterSave.effects == 1);

    // Verified + crash before the clear: same proven path, no replay.
    DurableMarkerHarness beforeClear;
    LIVING_CHECK(!beforeClear.Pass());
    LIVING_CHECK(beforeClear.DeliverVerify(beforeClear.lastRequestedGeneration) == MarkerVerifyAction::Proven);
    beforeClear.Restart();
    LIVING_CHECK(beforeClear.Pass());
    LIVING_CHECK(beforeClear.durablePhase == 0 && beforeClear.effects == 1);

    // The durable state matches NEITHER fingerprint (the character changed
    // after the effect, or the record is foreign): ambiguous - quarantined
    // with the row retained, never cleared, never replayed.
    DurableMarkerHarness ambiguous;
    LIVING_CHECK(!ambiguous.Pass());
    ambiguous.durableState = 999; // diverged durable state
    ambiguous.Restart();
    LIVING_CHECK(ambiguous.Pass()); // released for scheduling as QUARANTINED
    LIVING_CHECK(ambiguous.ledger.quarantined);
    LIVING_CHECK(ambiguous.durablePhase == kMarkerPhaseApplied); // row retained
    LIVING_CHECK(ambiguous.effects == 1); // no replay

    // An undecodable phase-2 record (legacy row) also quarantines.
    DurableMarkerHarness undecodable;
    undecodable.durablePhase = kMarkerPhaseApplied;
    undecodable.durableData = "epic"; // no fingerprints
    LIVING_CHECK(undecodable.Pass());
    LIVING_CHECK(undecodable.ledger.quarantined);
    LIVING_CHECK(undecodable.durablePhase == kMarkerPhaseApplied);
}

LIVING_TEST(durable_marker_data_codec_roundtrips_and_rejects_garbage)
{
    std::string original;
    uint64_t pre = 0, post = 0;

    std::string const encoded = EncodeDurableMarkerData("epic", 0x0123456789abcdefull, 0xfedcba9876543210ull);
    LIVING_CHECK(TryDecodeDurableMarkerData(encoded, original, pre, post));
    LIVING_CHECK(original == "epic");
    LIVING_CHECK(pre == 0x0123456789abcdefull && post == 0xfedcba9876543210ull);

    // Empty original payload (create levelup) round-trips too.
    LIVING_CHECK(TryDecodeDurableMarkerData(EncodeDurableMarkerData("", 1, 2), original, pre, post));
    LIVING_CHECK(original.empty() && pre == 1 && post == 2);

    LIVING_CHECK(!TryDecodeDurableMarkerData("epic", original, pre, post));
    LIVING_CHECK(!TryDecodeDurableMarkerData("epic|pre:zzzz|post:0000000000000000", original, pre, post));
    LIVING_CHECK(!TryDecodeDurableMarkerData(encoded + "x", original, pre, post));

    // The equipment hash is order-canonical and distinguishes slots/items.
    LIVING_CHECK(HashEquipmentState({}) != HashEquipmentState({ { 0, 1 } }));
    LIVING_CHECK(HashEquipmentState({ { 0, 1 } }) != HashEquipmentState({ { 1, 1 } }));
    LIVING_CHECK(HashEquipmentState({ { 0, 1 }, { 1, 2 } }) == HashEquipmentState({ { 0, 1 }, { 1, 2 } }));
}
