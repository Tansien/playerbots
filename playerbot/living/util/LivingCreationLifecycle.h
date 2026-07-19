#pragma once

#include "LivingEventSchema.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

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
    // process can tell "effect provably never persisted" from "effect ran at
    // least once" - the in-memory OneShotMarker ledger alone could not, and a
    // crash either replayed the destructive mutation or silently lost it.
    //
    // The protocol proves outcomes instead of assuming them:
    //   phase 1 (Pending): the effect was never durably recorded. Because the
    //     character save is queued only AFTER the phase-2 record is
    //     execution-confirmed, durable phase 1 implies the durable character
    //     state is still pre-effect - re-applying is a safe first application.
    //   phase 2 (Applied): the effect ran, and the record carries the PRE and
    //     POST equipment-state fingerprints captured around it. Durability is
    //     then PROVEN by comparing the fingerprint against the stored
    //     equipment (an execution-ordered readback behind the queued save; on
    //     restart, against the freshly loaded character). post-match = proven
    //     durable (clear); pre-match = provably lost (safe to re-apply by
    //     rewinding the record to phase 1); anything else = ambiguous, which
    //     QUARANTINES the marker with an actionable error - never a silent
    //     clear, never a blind replay.
    // Known limitation, disclosed: an effect that leaves equipment unchanged
    // (pre == post) cannot be distinguished by the fingerprint; its
    // non-equipment changes ride the same save transaction and are treated as
    // proven when the equipment matches.
    inline constexpr uint32_t kMarkerPhasePending = 1; // effect not durably recorded
    inline constexpr uint32_t kMarkerPhaseApplied = 2; // effect ran; fingerprints recorded

    // FNV-1a 64 over a canonical (slot, item-guid) equipment serialization;
    // the SAME function hashes the in-memory snapshot and the
    // character_inventory readback so the two are directly comparable.
    inline uint64_t HashEquipmentState(std::vector<std::pair<uint8_t, uint32_t>> const& slotItems)
    {
        uint64_t hash = 14695981039346656037ull;
        auto mix = [&hash](uint64_t value)
        {
            for (int i = 0; i < 8; ++i)
            {
                hash ^= (value >> (i * 8)) & 0xFF;
                hash *= 1099511628211ull;
            }
        };
        for (auto const& [slot, itemGuid] : slotItems)
        {
            mix(slot);
            mix(itemGuid);
        }
        return hash;
    }

    // The phase-2 record's data payload: the ORIGINAL payload (gear quality
    // etc., needed verbatim if the effect must be re-applied) plus both
    // fingerprints. Fits EVENT_DATA_MAX_BYTES for every legal original.
    inline std::string EncodeDurableMarkerData(std::string const& original, uint64_t preHash, uint64_t postHash)
    {
        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), "|pre:%016llx|post:%016llx",
            static_cast<unsigned long long>(preHash), static_cast<unsigned long long>(postHash));
        return original + buffer;
    }

    inline bool TryDecodeDurableMarkerData(std::string const& data, std::string& original,
        uint64_t& preHash, uint64_t& postHash)
    {
        size_t const prePos = data.rfind("|pre:");
        size_t const postPos = data.rfind("|post:");
        if (prePos == std::string::npos || postPos == std::string::npos || postPos != prePos + 5 + 16)
            return false;

        auto parseHex16 = [&](size_t begin, uint64_t& out) -> bool
        {
            uint64_t value = 0;
            for (size_t i = 0; i < 16; ++i)
            {
                char const c = begin + i < data.size() ? data[begin + i] : '\0';
                uint64_t digit;
                if (c >= '0' && c <= '9') digit = static_cast<uint64_t>(c - '0');
                else if (c >= 'a' && c <= 'f') digit = static_cast<uint64_t>(c - 'a') + 10;
                else return false;
                value = (value << 4) | digit;
            }
            out = value;
            return true;
        };

        if (data.size() != postPos + 6 + 16)
            return false;
        if (!parseHex16(prePos + 5, preHash) || !parseHex16(postPos + 6, postHash))
            return false;

        original = data.substr(0, prePos);
        return true;
    }

    // What a destructive marker owes this pass.
    enum class DurableMarkerStep
    {
        // Marker absent: nothing owed.
        Idle,
        // Phase 1, effect not applied this process: capture the PRE
        // fingerprint, apply the effect, capture POST, then durably record
        // phase 2 with both fingerprints (execution-confirmed).
        ApplyThenRecord,
        // Effect applied this process but the phase-2 record is still owed
        // (its confirmed write failed): retry ONLY the record - never the
        // effect (the kept fingerprints travel with the ledger).
        RecordApplied,
        // Phase 2 recorded this process: queue/re-queue the character save
        // and request the execution-ordered fingerprint verification.
        SaveAndVerify,
        // A verification request is in flight: tick the lost-callback
        // watchdog; timeout invalidates the generation and re-arms.
        AwaitVerify,
        // Postcondition proven durable: retry only the confirmed clear.
        ClearConfirmed,
        // Fresh process, phase 2: reconcile the recorded fingerprints against
        // the freshly loaded character (post-match -> proven; pre-match ->
        // rewind to phase 1 and re-apply; neither -> quarantine).
        RecoverProbe,
        // Ambiguous durable state: retained with an actionable error; no
        // further automatic work, never silently cleared.
        Quarantined,
    };

    // What the caller owes after feeding one verification result.
    enum class MarkerVerifyAction
    {
        None,        // stale generation / not awaiting: ignore
        Proven,      // postcondition matched: proceed to the confirmed clear
        RetrySave,   // mismatch or failed query: re-save + re-verify (bounded)
        Quarantined, // bounded attempts exhausted: retained with error
    };

    // Per-(bot, marker) ledger for the durable fingerprint-verified consume.
    struct DurableOneShotMarker
    {
        static constexpr uint32_t kMaxVerifyAttempts = 5;
        // Passes a verification may stay outstanding before its callback is
        // considered lost and the request is re-armed under a new generation
        // (the relocation watchdog pattern; one pass per manager tick).
        static constexpr uint32_t kMaxAwaitVerifyPasses = 128;

        bool effectApplied = false;      // this process
        bool phaseRecorded = false;      // confirmed durable phase-2 record
        bool verifyOutstanding = false;  // a verification query is in flight
        bool postconditionProven = false;
        bool quarantined = false;
        uint32_t verifyAttempts = 0;     // mismatches/failures/timeouts consumed
        uint32_t verifyGeneration = 0;   // stamps requests; stale results ignored
        uint32_t awaitingPasses = 0;     // watchdog while a verify is outstanding
        uint64_t preHash = 0;
        uint64_t postHash = 0;
        std::string originalData;        // payload preserved for a re-apply

        DurableMarkerStep Plan(uint32_t durablePhase) const
        {
            if (quarantined)
                return DurableMarkerStep::Quarantined;
            if (durablePhase == 0)
                return DurableMarkerStep::Idle;
            if (durablePhase < kMarkerPhaseApplied)
                return effectApplied ? DurableMarkerStep::RecordApplied
                                     : DurableMarkerStep::ApplyThenRecord;
            if (!effectApplied)
                return DurableMarkerStep::RecoverProbe;
            if (postconditionProven)
                return DurableMarkerStep::ClearConfirmed;
            return verifyOutstanding ? DurableMarkerStep::AwaitVerify
                                     : DurableMarkerStep::SaveAndVerify;
        }

        void OnEffectApplied(uint64_t pre, uint64_t post, std::string original)
        {
            effectApplied = true;
            preHash = pre;
            postHash = post;
            originalData = std::move(original);
        }

        void OnPhaseRecorded() { phaseRecorded = true; }

        // Stamps a fresh request generation (invalidating any stale in-flight
        // result) and returns it for the request binding.
        uint32_t BeginVerify()
        {
            ++verifyGeneration;
            awaitingPasses = 0;
            return verifyGeneration;
        }

        void OnVerifyRequested(bool enqueued)
        {
            verifyOutstanding = enqueued;
            if (!enqueued)
                ConsumeVerifyAttempt(); // failed enqueue: bounded retry next pass
        }

        // Watchdog tick while AwaitVerify: returns true when the outstanding
        // result is considered LOST - the wait is abandoned (consuming one
        // bounded attempt) and the caller re-requests under a new generation.
        bool TickAwaitingVerify()
        {
            if (!verifyOutstanding)
                return false;

            if (++awaitingPasses <= kMaxAwaitVerifyPasses)
                return false;

            verifyOutstanding = false;
            ConsumeVerifyAttempt();
            return true;
        }

        enum class VerifyOutcome { Match, Mismatch, QueryFailed };

        // Feeds one verification result. Results whose generation does not
        // match the CURRENT outstanding request are stale (a late callback
        // after a watchdog re-arm) and are ignored.
        MarkerVerifyAction OnVerifyResult(uint32_t generation, VerifyOutcome outcome)
        {
            if (!verifyOutstanding || generation != verifyGeneration)
                return MarkerVerifyAction::None;

            verifyOutstanding = false;
            awaitingPasses = 0;

            if (outcome == VerifyOutcome::Match)
            {
                postconditionProven = true;
                return MarkerVerifyAction::Proven;
            }

            ConsumeVerifyAttempt();
            return quarantined ? MarkerVerifyAction::Quarantined : MarkerVerifyAction::RetrySave;
        }

        // Recovery decision for a fresh process holding a phase-2 record.
        enum class RecoverDecision { ProvenDurable, ReapplySafe, Ambiguous };

        static RecoverDecision Reconcile(uint64_t currentHash, uint64_t recordedPre, uint64_t recordedPost)
        {
            if (currentHash == recordedPost)
                return RecoverDecision::ProvenDurable; // ties (pre==post) favor proven
            if (currentHash == recordedPre)
                return RecoverDecision::ReapplySafe;
            return RecoverDecision::Ambiguous;
        }

        void MarkProven() { effectApplied = true; postconditionProven = true; }

        void MarkQuarantined() { quarantined = true; }

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

    private:
        void ConsumeVerifyAttempt()
        {
            if (++verifyAttempts >= kMaxVerifyAttempts)
                quarantined = true;
        }
    };
}
