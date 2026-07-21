#pragma once

#include "LivingEventSchema.h"

#include <cstdint>
#include <map>
#include <string>

namespace living
{
    // A character may materialize only after the durable intent scan has
    // established what owns its GUID and no live finalizer still owns it.
    inline bool CreationMaterializationBlocked(bool intentStateKnown, bool finalizerOwns)
    {
        return !intentStateKnown || finalizerOwns;
    }

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
        // Metadata failed after a durable character: character deletion is in
        // flight while the durable creation intent remains as restart owner.
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

        // Result of event-state cleanup after character absence was confirmed.
        // A failed event cleanup is an UNCERTAIN cleanup.
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
    // gear, level up, install a test strategy) must run EXACTLY ONCE per
    // process, then the durable marker row is cleared. A marker that is
    // `create X = 1` and never cleared - or whose clear is unconfirmed - would
    // otherwise replay the mutation every manager pass for an always-online
    // bot. This separates "effect applied" from "marker consumed": the effect
    // runs once and, until the clear is execution-confirmed, only the CLEAR is
    // retried; a confirmed clear ends the obligation. A transient read blip
    // (marker briefly absent) does NOT reset the applied flag, so it can never
    // license a replay.
    //
    // ponytail: crash-window disclosure. Every marker effect (re-randomize
    // gear, level up, install a test strategy) is a bot-facing randomization,
    // so BOTH failure directions are tolerable and no durable
    // applied-phase/fingerprint protocol is warranted here:
    //   - crash before the clear -> the effect replays once next login pass;
    //   - crash after the clear  -> that one application is lost.
    // The caller is still required to queue the effect's persistence BEFORE
    // asking for the clear. The clear is durable the instant it returns, so
    // clearing first would widen the second window from "a queued save that
    // had not drained yet" to "no save was ever queued at all".
    struct OneShotMarker
    {
        // Failed clear attempts after which the stuck clear is DISCLOSED
        // (once). The retry itself is deliberately unbounded and self-healing:
        // the ledger is retained, so only the CLEAR is ever retried and the
        // first confirmed clear ends the obligation the moment the database
        // recovers. A give-up that erased the ledger would re-license the
        // effect against the still-present durable row - a replay loop, which
        // is strictly worse than an idle retry.
        static constexpr uint32_t kStuckClearWarnAttempts = 5;

        bool effectApplied = false;
        uint32_t clearAttempts = 0;

        MarkerConsumeStep Plan(bool present) const
        {
            if (!present)
                return MarkerConsumeStep::Idle;
            return effectApplied ? MarkerConsumeStep::ClearOnly : MarkerConsumeStep::ApplyThenClear;
        }

        void OnEffectApplied() { effectApplied = true; }

        // Feeds the typed clear result; returns whether the marker is now
        // fully consumed. The ledger entry itself is erased by the consume
        // protocol below - never reset in place.
        bool OnClearResult(EventWriteResult clearResult)
        {
            bool const consumed = clearResult == EventWriteResult::DesiredStateConfirmed;
            if (!consumed)
                ++clearAttempts;
            return consumed;
        }
    };

    // Ledger of one-shot marker state: bot guid -> marker name -> ledger.
    // Owned by the manager; ForgetEventCache erases a bot's entry wholesale.
    using OneShotLedgers = std::map<uint32_t, std::map<std::string, OneShotMarker>>;

    // Drives ONE pass of the one-shot consume protocol - INCLUDING the ledger
    // lifecycle, which is exactly the part a caller must never improvise: the
    // ledger entry is the only record that the effect already ran, so it may
    // be erased only when the durable row is authoritatively absent or the
    // clear is execution-confirmed. Returns whether the marker is settled.
    //
    //   presentKnown == false -> unknown durable state: mutate nothing, keep
    //                            ownership (not settled).
    //   present == false      -> settled; any tracked state is dropped.
    //   effect not yet run    -> applyEffect() exactly once, then the clear.
    //   clear unconfirmed     -> ledger RETAINED; only the clear retries next
    //                            pass, unbounded and self-healing.
    //                            onClearStuck fires once when the attempt
    //                            count first reaches kStuckClearWarnAttempts.
    //
    // The ledgers are re-consulted after applyEffect: its call graph is large
    // (factory runs, character saves) and must not be assumed to leave the
    // ledgers untouched.
    template <typename ApplyFn, typename ClearFn, typename StuckFn>
    bool ConsumeOneShotMarker(OneShotLedgers& ledgers, uint32_t bot, std::string const& marker,
        bool presentKnown, bool present, ApplyFn&& applyEffect, ClearFn&& clearMarker,
        StuckFn&& onClearStuck)
    {
        // find(), never operator[]: probing must not default-insert per-bot
        // maps for the common no-marker case.
        auto tracked = [&]() -> OneShotMarker*
        {
            auto botIt = ledgers.find(bot);
            if (botIt == ledgers.end())
                return nullptr;
            auto markerIt = botIt->second.find(marker);
            return markerIt == botIt->second.end() ? nullptr : &markerIt->second;
        };
        auto dropTracked = [&]()
        {
            auto botIt = ledgers.find(bot);
            if (botIt == ledgers.end())
                return;
            botIt->second.erase(marker);
            if (botIt->second.empty())
                ledgers.erase(botIt); // never retain empty per-bot maps
        };

        if (!presentKnown)
            return false;

        if (!present)
        {
            dropTracked();
            return true; // authoritatively absent: settled
        }

        OneShotMarker* oneShot = tracked();
        if (!oneShot || !oneShot->effectApplied)
        {
            applyEffect();
            ledgers[bot][marker].OnEffectApplied(); // re-acquired post-effect
        }

        bool const cleared = clearMarker();
        oneShot = tracked();
        if (!oneShot)
            return false; // ledger vanished mid-consume (bot forgotten): keep ownership

        if (oneShot->OnClearResult(cleared ? EventWriteResult::DesiredStateConfirmed
                                           : EventWriteResult::StateUnknown))
        {
            dropTracked();
            return true;
        }

        if (oneShot->clearAttempts == OneShotMarker::kStuckClearWarnAttempts)
            onClearStuck(oneShot->clearAttempts);
        return false;
    }
}
