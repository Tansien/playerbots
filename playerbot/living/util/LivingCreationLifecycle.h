#pragma once

#include "LivingEventSchema.h"

#include <cstdint>

namespace living
{
    // Lifecycle of one bot creation. In every pinned core Player::SaveToDB
    // commits through the asynchronous transaction queue, so returning from it
    // confirms ENQUEUEING, not execution - and Player::DeleteFromDB queues the
    // same way. Durability is established only by an execution-ordered
    // verification (an async query on the same FIFO delay thread, whose
    // callback observes the state AFTER the transaction executed). Until then
    // the GUID is never exposed, counted, retried or reused.
    enum class CreationStage
    {
        // Character transaction queued; durability unknown. No metadata has
        // been written, no quota counted, no GUID exposed.
        PendingPersistence,
        // Character row verified AND every required metadata write
        // execution-confirmed: the creation is complete and the GUID may be
        // exposed (freeAltBots, group accounting).
        Created,
        // Metadata failed after a durable character: deletion of character and
        // event state is in flight; nothing is retryable yet.
        PendingCleanup,
        // Terminal, confirmed: either the character transaction rolled back
        // before any dependent write existed, or cleanup verifiably removed
        // everything. A new attempt is safe.
        FailedRetryable,
        // Durability or cleanup is UNKNOWN (verify/cleanup queries kept
        // failing, identity mismatch, or event cleanup failed). The record
        // stays quarantined forever - never retried, never reported clean.
        Quarantined,
    };

    // Outcome of one async row-verification callback. A COUNT-based query
    // cannot conflate failure with absence: a null result is QueryFailed,
    // never Absent.
    enum class RowVerifyOutcome
    {
        QueryFailed,
        Absent,
        IdentityMismatch,
        Verified,
    };

    // Pure transition rules; the production finalizer drives them from async
    // callback results and execution-confirmed write results. Every unknown
    // outcome ends in Quarantined - never in a retryable claim.
    struct CreationLifecycle
    {
        CreationStage stage = CreationStage::PendingPersistence;
        uint32_t verifyAttempts = 0;
        uint32_t cleanupAttempts = 0;

        // One character-row verification callback while PendingPersistence.
        // Verified keeps the stage: the caller proceeds to the metadata phase
        // and reports its result through OnMetadataResult.
        CreationStage OnCharacterVerify(RowVerifyOutcome outcome, uint32_t maxAttempts)
        {
            if (stage != CreationStage::PendingPersistence)
                return stage;

            switch (outcome)
            {
                case RowVerifyOutcome::QueryFailed:
                    if (++verifyAttempts >= maxAttempts)
                        stage = CreationStage::Quarantined;
                    break;
                case RowVerifyOutcome::Absent:
                    // The transaction executed and rolled back; no dependent
                    // state was ever written, so a new attempt is safe.
                    stage = CreationStage::FailedRetryable;
                    break;
                case RowVerifyOutcome::IdentityMismatch:
                    stage = CreationStage::Quarantined;
                    break;
                case RowVerifyOutcome::Verified:
                    break;
            }

            return stage;
        }

        // Result of the execution-confirmed metadata phase (after Verified).
        CreationStage OnMetadataResult(bool allPersisted)
        {
            if (stage != CreationStage::PendingPersistence)
                return stage;

            stage = allPersisted ? CreationStage::Created : CreationStage::PendingCleanup;
            return stage;
        }

        // Result of the execution-confirmed event-state cleanup while
        // PendingCleanup. A failed event cleanup is an UNCERTAIN cleanup.
        CreationStage OnEventCleanupResult(bool eventsConfirmedGone)
        {
            if (stage != CreationStage::PendingCleanup)
                return stage;

            if (!eventsConfirmedGone)
                stage = CreationStage::Quarantined;

            return stage;
        }

        // One character-deletion verification callback while PendingCleanup.
        // Absent = the deletion transaction executed and the row is verifiably
        // gone; Verified here means the character is STILL PRESENT (deletion
        // did not take effect yet or failed) and consumes a cleanup attempt.
        CreationStage OnCleanupVerify(RowVerifyOutcome outcome, uint32_t maxAttempts)
        {
            if (stage != CreationStage::PendingCleanup)
                return stage;

            switch (outcome)
            {
                case RowVerifyOutcome::Absent:
                    stage = CreationStage::FailedRetryable;
                    break;
                case RowVerifyOutcome::QueryFailed:
                case RowVerifyOutcome::IdentityMismatch:
                case RowVerifyOutcome::Verified:
                    if (++cleanupAttempts >= maxAttempts)
                        stage = CreationStage::Quarantined;
                    break;
            }

            return stage;
        }
    };

    // Which step a one-shot post-create marker (create gear / levelup / test)
    // owes this pass.
    enum class MarkerConsumeStep
    {
        Idle,           // marker absent: nothing to do
        ApplyThenClear, // present and the runtime effect has not run: apply once, then clear
        ClearOnly,      // present but the effect already ran: retry ONLY the durable clear
    };

    // Per-(bot, marker) one-shot ledger. The runtime effect (destroy/generate
    // gear, level up, install a test strategy) must run EXACTLY ONCE, then the
    // durable marker row is cleared. A marker that is `create X = 1` and never
    // cleared - or whose clear is unconfirmed - would otherwise replay the
    // mutation every manager pass for an always-online bot. This separates
    // "effect applied" from "marker consumed": the effect runs once and, until
    // the clear is execution-confirmed, only the CLEAR is retried; a confirmed
    // clear ends the obligation. A transient read blip (marker briefly absent)
    // does NOT reset the applied flag, so it can never license a replay.
    struct OneShotMarker
    {
        bool effectApplied = false;

        MarkerConsumeStep Plan(bool present) const
        {
            if (!present)
                return MarkerConsumeStep::Idle;
            return effectApplied ? MarkerConsumeStep::ClearOnly : MarkerConsumeStep::ApplyThenClear;
        }

        void OnEffectApplied() { effectApplied = true; }

        // Feeds the typed clear result; returns whether the marker is now fully
        // consumed (only a confirmed clear ends it and resets the ledger).
        bool OnClearResult(EventWriteResult clearResult)
        {
            bool const consumed = clearResult == EventWriteResult::DesiredStateConfirmed;
            if (consumed)
                effectApplied = false;
            return consumed;
        }
    };

    // Durable phase values for a DESTRUCTIVE one-shot marker (create gear /
    // create levelup): the phase lives in the marker row's value, so a fresh
    // process can tell "effect provably never ran" from "effect ran at least
    // once" - the in-memory OneShotMarker ledger alone could not, and a crash
    // either replayed the destructive mutation or silently lost it.
    inline constexpr uint32_t kMarkerPhasePending = 1; // intent recorded, effect NOT applied
    inline constexpr uint32_t kMarkerPhaseApplied = 2; // effect ran; save durability unconfirmed

    // What a destructive marker owes this pass.
    enum class DurableMarkerStep
    {
        // Marker absent: nothing owed.
        Idle,
        // Pending phase: durably confirm phase=Applied FIRST, then (only after
        // that confirmed write, and only if the effect has not run in this
        // process) apply the effect, queue the character save, and request the
        // execution-ordered save barrier. An unconfirmed phase write retries
        // the whole step on a later pass with the effect still un-run.
        AdvanceThenApply,
        // Effect applied this process: wait for the barrier (re-requesting it
        // when the enqueue failed or the result was a failed query).
        AwaitBarrier,
        // Barrier passed: the effect is durable; retry ONLY the confirmed
        // durable clear.
        ClearConfirmed,
        // A fresh process found phase=Applied with no in-memory ledger: the
        // effect ran before a crash and its durability is ambiguous. NEVER
        // blindly replay a destructive effect - clear the marker (confirmed)
        // and disclose; the only loss window is a crash that also beat the
        // queued save, and that loss is logged instead of silently replayed
        // onto possibly-post-effect state.
        RecoverNoReplay,
    };

    // Per-(bot, marker) ledger for the durable two-phase consume. The phase
    // ordering gives recovery its guarantees: phase=Pending durably implies
    // the effect never ran (the Applied write is execution-confirmed BEFORE
    // the effect), and phase=Applied implies it ran at least once - so a
    // restart replays only provably-unapplied intents. The clear is issued
    // only after the save barrier (an async query behind the queued SaveToDB
    // on the same FIFO thread) confirms the save transaction executed, so the
    // durable intent outlives every crash that could lose the effect.
    struct DurableOneShotMarker
    {
        bool effectApplied = false;    // this process
        bool barrierRequested = false; // a barrier query is in flight
        bool barrierPassed = false;    // the save provably executed

        DurableMarkerStep Plan(uint32_t durablePhase) const
        {
            if (durablePhase == 0)
                return DurableMarkerStep::Idle;
            if (durablePhase < kMarkerPhaseApplied)
                return DurableMarkerStep::AdvanceThenApply;
            if (!effectApplied)
                return DurableMarkerStep::RecoverNoReplay;
            return barrierPassed ? DurableMarkerStep::ClearConfirmed : DurableMarkerStep::AwaitBarrier;
        }

        void OnEffectApplied() { effectApplied = true; }

        void OnBarrierRequested(bool enqueued) { barrierRequested = enqueued; }

        // Feeds one barrier result: a null/failed query proves nothing (the
        // request is re-armed); any real result proves the earlier queued
        // save transaction executed.
        void OnBarrierResult(bool executed)
        {
            barrierRequested = false;
            if (executed)
                barrierPassed = true;
        }

        // Feeds the typed clear result; returns whether the marker is now
        // fully consumed (only a confirmed clear ends it and resets the
        // ledger).
        bool OnClearResult(EventWriteResult clearResult)
        {
            bool const consumed = clearResult == EventWriteResult::DesiredStateConfirmed;
            if (consumed)
                *this = DurableOneShotMarker{};
            return consumed;
        }
    };
}
