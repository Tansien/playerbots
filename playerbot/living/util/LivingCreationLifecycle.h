#pragma once

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
}
