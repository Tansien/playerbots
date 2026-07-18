#include "LivingTest.h"

#include "../util/LivingCreationBatch.h"
#include "../util/LivingCreationFinalizer.h"

#include <string>
#include <vector>

using namespace living;

// CreationFinalizer is the exact deferred-callback registry the production
// finalizer drives: SQL result callbacks may only call OnCallbackResult
// (bounded enqueue, no ops), and every lifecycle transition, metadata write,
// cleanup, retry and follow-up query runs in Pump. These are spy-backed
// fault-injection tests of that production decision core; the live
// async-queue/MySQL wiring is exercised only in-world.

namespace
{
    struct SpyOps
    {
        std::vector<uint32_t> verifyRequests;
        std::vector<uint32_t> cleanupVerifyRequests;
        std::vector<uint32_t> metadataWrites;
        std::vector<uint32_t> eventDeletes;
        std::vector<uint32_t> characterDeletes;
        std::vector<uint32_t> created;

        bool metadataSucceeds = true;
        bool eventDeleteSucceeds = true;

        size_t TotalOps() const
        {
            return verifyRequests.size() + cleanupVerifyRequests.size() + metadataWrites.size()
                + eventDeletes.size() + characterDeletes.size() + created.size();
        }

        CreationFinalizerOps Make()
        {
            CreationFinalizerOps ops;
            ops.requestVerify = [this](uint32_t guid) { verifyRequests.push_back(guid); };
            ops.requestCleanupVerify = [this](uint32_t guid) { cleanupVerifyRequests.push_back(guid); };
            ops.writeMetadata = [this](uint32_t guid) { metadataWrites.push_back(guid); return metadataSucceeds; };
            ops.deleteEventRows = [this](uint32_t guid) { eventDeletes.push_back(guid); return eventDeleteSucceeds; };
            ops.deleteCharacter = [this](uint32_t guid, uint32_t) { characterDeletes.push_back(guid); };
            ops.onCreated = [this](uint32_t guid, uint32_t, std::string const&, bool) { created.push_back(guid); };
            return ops;
        }
    };

    CreationFinalizer::Record MakeRecord(uint32_t guid, uint32_t accountId, std::string name)
    {
        CreationFinalizer::Record record;
        record.guid = guid;
        record.accountId = accountId;
        record.name = std::move(name);
        return record;
    }
}

LIVING_TEST(creation_finalizer_callback_only_enqueues_and_pump_finalizes)
{
    // The task-1 interleaving scenario: two queued creations whose verify
    // callbacks arrive while the SQL result queue is being dispatched. Each
    // callback must return WITHOUT performing any operation (in production:
    // any database work - the deadlock vector); all finalization work happens
    // later, through Pump.
    CreationFinalizer finalizer;
    SpyOps spy;

    uint64_t const tokenA = finalizer.Begin(MakeRecord(101, 1, "BotA"), spy.Make());
    uint64_t const tokenB = finalizer.Begin(MakeRecord(102, 1, "BotB"), spy.Make());
    LIVING_CHECK(tokenA != 0 && tokenB != 0 && tokenA != tokenB);
    LIVING_CHECK(spy.verifyRequests.size() == 2); // one verify scheduled per Begin

    size_t const opsBeforeCallbacks = spy.TotalOps();

    // Both callbacks delivered back-to-back (ProcessResultQueue context).
    finalizer.OnCallbackResult(101, RowVerifyOutcome::Verified, CreationCallbackKind::Verify);
    finalizer.OnCallbackResult(102, RowVerifyOutcome::Verified, CreationCallbackKind::Verify);

    // The callbacks performed NO operation - only the queue grew.
    LIVING_CHECK(spy.TotalOps() == opsBeforeCallbacks);
    LIVING_CHECK(finalizer.QueuedEventCount() == 2);

    // Pump (a later world update, after the result-queue mutex is released)
    // performs the metadata writes and completes both creations.
    finalizer.Pump(spy.Make());
    LIVING_CHECK(finalizer.QueuedEventCount() == 0);
    LIVING_CHECK(spy.metadataWrites.size() == 2);
    LIVING_CHECK(spy.created.size() == 2);
    LIVING_CHECK(finalizer.RecordCount() == 0);

    // The tokens now poll as Created with the finalized GUIDs.
    LIVING_CHECK(finalizer.Poll(tokenA, false).status == CreationPollStatus::Created);
    LIVING_CHECK(finalizer.Poll(tokenA, false).guid == 101);
    LIVING_CHECK(finalizer.Poll(tokenB, false).guid == 102);
}

LIVING_TEST(creation_finalizer_failed_query_retries_from_pump_not_callback)
{
    // A failed verify query must schedule its bounded retry from Pump - the
    // callback itself never requests anything.
    CreationFinalizer finalizer;
    SpyOps spy;
    finalizer.Begin(MakeRecord(101, 1, "BotA"), spy.Make());
    LIVING_CHECK(spy.verifyRequests.size() == 1);

    finalizer.OnCallbackResult(101, RowVerifyOutcome::QueryFailed, CreationCallbackKind::Verify);
    LIVING_CHECK(spy.verifyRequests.size() == 1); // callback requested nothing

    finalizer.Pump(spy.Make());
    LIVING_CHECK(spy.verifyRequests.size() == 2); // retry scheduled by Pump
    LIVING_CHECK(finalizer.RecordCount() == 1);
}

LIVING_TEST(creation_finalizer_rollback_and_quarantine_publish_terminal_results)
{
    CreationFinalizer finalizer;
    SpyOps spy;

    // Rolled-back transaction: confirmed absence -> FailedRetryable, record
    // erased, completion retained for the poller.
    uint64_t const rolledBack = finalizer.Begin(MakeRecord(201, 1, "RolledBack"), spy.Make());
    finalizer.OnCallbackResult(201, RowVerifyOutcome::Absent, CreationCallbackKind::Verify);
    finalizer.Pump(spy.Make());
    LIVING_CHECK(finalizer.RecordCount() == 0);
    LIVING_CHECK(finalizer.Poll(rolledBack, false).status == CreationPollStatus::FailedRetryable);
    // No GUID is ever exposed for a failed creation.
    LIVING_CHECK(finalizer.Poll(rolledBack, false).guid == 0);

    // Identity mismatch: quarantined - the record STAYS (occupying capacity)
    // and the token keeps reporting Quarantined even after the retained
    // completion is acknowledged.
    uint64_t const mismatched = finalizer.Begin(MakeRecord(202, 1, "Mismatch"), spy.Make());
    finalizer.OnCallbackResult(202, RowVerifyOutcome::IdentityMismatch, CreationCallbackKind::Verify);
    finalizer.Pump(spy.Make());
    LIVING_CHECK(finalizer.RecordCount() == 1);
    LIVING_CHECK(finalizer.Poll(mismatched, true).status == CreationPollStatus::Quarantined);
    LIVING_CHECK(finalizer.Poll(mismatched, true).status == CreationPollStatus::Quarantined); // from the live record
}

LIVING_TEST(creation_finalizer_metadata_failure_runs_confirmed_cleanup_through_pump)
{
    CreationFinalizer finalizer;
    SpyOps spy;
    spy.metadataSucceeds = false;

    uint64_t const token = finalizer.Begin(MakeRecord(301, 1, "MetaFail"), spy.Make());
    finalizer.OnCallbackResult(301, RowVerifyOutcome::Verified, CreationCallbackKind::Verify);
    finalizer.Pump(spy.Make());

    // Metadata failed after a durable row: event cleanup, queued character
    // deletion and a cleanup verification - all from Pump.
    LIVING_CHECK(spy.eventDeletes.size() == 1);
    LIVING_CHECK(spy.characterDeletes.size() == 1);
    LIVING_CHECK(spy.cleanupVerifyRequests.size() == 1);
    LIVING_CHECK(finalizer.Poll(token, false).status == CreationPollStatus::Pending);

    // Deletion verified absent -> retryable, record erased.
    finalizer.OnCallbackResult(301, RowVerifyOutcome::Absent, CreationCallbackKind::CleanupVerify);
    finalizer.Pump(spy.Make());
    LIVING_CHECK(finalizer.RecordCount() == 0);
    LIVING_CHECK(finalizer.Poll(token, true).status == CreationPollStatus::FailedRetryable);

    // Event cleanup failure quarantines instead.
    SpyOps quarantineSpy;
    quarantineSpy.metadataSucceeds = false;
    quarantineSpy.eventDeleteSucceeds = false;
    CreationFinalizer q;
    uint64_t const qToken = q.Begin(MakeRecord(302, 1, "EventFail"), quarantineSpy.Make());
    q.OnCallbackResult(302, RowVerifyOutcome::Verified, CreationCallbackKind::Verify);
    q.Pump(quarantineSpy.Make());
    LIVING_CHECK(q.RecordCount() == 1);
    LIVING_CHECK(q.Poll(qToken, false).status == CreationPollStatus::Quarantined);
}

LIVING_TEST(creation_finalizer_poll_acknowledge_and_expiry_are_bounded)
{
    CreationFinalizer finalizer;
    SpyOps spy;

    uint64_t const token = finalizer.Begin(MakeRecord(401, 1, "AckMe"), spy.Make());
    finalizer.OnCallbackResult(401, RowVerifyOutcome::Verified, CreationCallbackKind::Verify);
    finalizer.Pump(spy.Make());

    // Unacknowledged terminal results are retained...
    LIVING_CHECK(finalizer.Poll(token, false).status == CreationPollStatus::Created);
    // ...until acknowledged - after which the token is Unknown (terminal for
    // callers, never pending).
    LIVING_CHECK(finalizer.Poll(token, true).status == CreationPollStatus::Created);
    LIVING_CHECK(finalizer.Poll(token, true).status == CreationPollStatus::Unknown);

    // A never-acknowledged completion expires after the bounded retention.
    uint64_t const expiring = finalizer.Begin(MakeRecord(402, 1, "Expire"), spy.Make());
    finalizer.OnCallbackResult(402, RowVerifyOutcome::Verified, CreationCallbackKind::Verify);
    finalizer.Pump(spy.Make());
    for (uint64_t i = 0; i <= CreationFinalizer::kCompletionRetentionPumps; ++i)
        finalizer.Pump(spy.Make());
    LIVING_CHECK(finalizer.Poll(expiring, false).status == CreationPollStatus::Unknown);

    // A token that was never issued is Unknown.
    LIVING_CHECK(finalizer.Poll(999999, false).status == CreationPollStatus::Unknown);
}

LIVING_TEST(creation_finalizer_reserves_account_capacity_before_save)
{
    // The task-4 scenario: an account at cap-minus-one with the async queue
    // delayed. Several creation attempts arrive; exactly ONE may reserve the
    // remaining slot - the second admission sees durable + reserved.
    CreationFinalizer finalizer;
    SpyOps spy;

    uint32_t const durable = 8; // cap 9, one slot left
    LIVING_CHECK(finalizer.TryReserveAccountSlot(50, durable, 9));
    LIVING_CHECK(finalizer.ReservedCount(50) == 1);
    LIVING_CHECK(!finalizer.TryReserveAccountSlot(50, durable, 9)); // second attempt refused
    LIVING_CHECK(!finalizer.TryReserveAccountSlot(50, durable, 9)); // and every later one

    // Another account is unaffected.
    LIVING_CHECK(finalizer.TryReserveAccountSlot(51, 0, 9));

    // A pre-save failure releases immediately (no save was queued).
    finalizer.ReleaseAccountSlot(51);
    LIVING_CHECK(finalizer.ReservedCount(51) == 0);

    // The reservation binds to the record and is released only on a CONFIRMED
    // terminal outcome (here: confirmed created - the durable count now
    // includes the character).
    CreationFinalizer::Record record = MakeRecord(501, 50, "Reserved");
    record.holdsAccountReservation = true;
    uint64_t const token = finalizer.Begin(std::move(record), spy.Make());
    LIVING_CHECK(finalizer.ReservedCount(50) == 1); // still charged while pending

    finalizer.OnCallbackResult(501, RowVerifyOutcome::Verified, CreationCallbackKind::Verify);
    finalizer.Pump(spy.Make());
    LIVING_CHECK(finalizer.Poll(token, false).status == CreationPollStatus::Created);
    LIVING_CHECK(finalizer.ReservedCount(50) == 0); // released on confirmation

    // A confirmed rollback releases too.
    LIVING_CHECK(finalizer.TryReserveAccountSlot(50, durable, 9));
    CreationFinalizer::Record rollback = MakeRecord(502, 50, "RolledBack");
    rollback.holdsAccountReservation = true;
    finalizer.Begin(std::move(rollback), spy.Make());
    finalizer.OnCallbackResult(502, RowVerifyOutcome::Absent, CreationCallbackKind::Verify);
    finalizer.Pump(spy.Make());
    LIVING_CHECK(finalizer.ReservedCount(50) == 0);

    // A quarantined record KEEPS its reservation forever - its durable state
    // is unknown, so the capacity it may occupy stays charged.
    LIVING_CHECK(finalizer.TryReserveAccountSlot(50, durable, 9));
    CreationFinalizer::Record quarantined = MakeRecord(503, 50, "Quarantined");
    quarantined.holdsAccountReservation = true;
    finalizer.Begin(std::move(quarantined), spy.Make());
    finalizer.OnCallbackResult(503, RowVerifyOutcome::IdentityMismatch, CreationCallbackKind::Verify);
    finalizer.Pump(spy.Make());
    LIVING_CHECK(finalizer.ReservedCount(50) == 1);
    LIVING_CHECK(!finalizer.TryReserveAccountSlot(50, durable, 9));
}

LIVING_TEST(creation_finalizer_event_queue_is_bounded)
{
    CreationFinalizer finalizer;
    SpyOps spy;
    finalizer.Begin(MakeRecord(601, 1, "Flood"), spy.Make());

    for (uint32_t i = 0; i < CreationFinalizer::kMaxQueuedEvents + 5; ++i)
        finalizer.OnCallbackResult(601, RowVerifyOutcome::QueryFailed, CreationCallbackKind::Verify);

    LIVING_CHECK(finalizer.QueuedEventCount() == CreationFinalizer::kMaxQueuedEvents);
    LIVING_CHECK(finalizer.DroppedEventCount() == 5);
}

LIVING_TEST(creation_batch_tracks_mixed_outcomes_with_replacement_and_failures)
{
    // The task-5 scenario: one batch containing a successful member, a
    // rolled-back member (replaced within budget), a metadata-failed member
    // (retryable after confirmed cleanup - budget exhausted -> recorded
    // failure) and a quarantined member (terminal failure).
    CreationBatchRegistry registry;

    CreationBatchRegistry::Batch batch;
    batch.initiatorName = "Master";
    batch.initiatorGuid = 77;
    batch.desiredSize = 5;
    batch.preexistingMembers = 1;
    batch.replacementBudget = 1;
    batch.pending = {
        CreationBatchMember{ 11, 1, 1 }, // success
        CreationBatchMember{ 12, 2, 2 }, // rolled back -> replacement (budget 1)
        CreationBatchMember{ 13, 3, 4 }, // cleanup-confirmed retryable -> budget exhausted -> failure
        CreationBatchMember{ 14, 4, 4 }, // quarantined -> terminal failure
    };

    uint64_t const token = registry.Begin(std::move(batch));
    LIVING_CHECK(token != 0);
    LIVING_CHECK(registry.Poll(token, false).status == BatchPollStatus::Pending);

    auto complete = [](uint64_t creationToken, CreationPollStatus status, uint32_t guid)
    {
        CreationCompletion completion;
        completion.token = creationToken;
        completion.guid = guid;
        completion.status = status;
        completion.name = "Member" + std::to_string(creationToken);
        completion.message = "test outcome";
        return completion;
    };

    // Successful member: finalized GUID recorded, nothing owed.
    auto success = registry.OnCreationTerminal(complete(11, CreationPollStatus::Created, 1001));
    LIVING_CHECK(success && !success->enqueueReplacement && success->failure.empty());

    // Rolled-back member: replacement owed, carrying the reserved role slot.
    auto replaced = registry.OnCreationTerminal(complete(12, CreationPollStatus::FailedRetryable, 0));
    LIVING_CHECK(replaced && replaced->enqueueReplacement);
    LIVING_CHECK(replaced->role == 2);
    LIVING_CHECK(registry.AddPendingMember(token, CreationBatchMember{ 15, 2, 2 }));

    // Metadata-failed member (cleanup confirmed -> retryable), but the budget
    // is exhausted: recorded as a failure instead of silently shrinking.
    auto exhausted = registry.OnCreationTerminal(complete(13, CreationPollStatus::FailedRetryable, 0));
    LIVING_CHECK(exhausted && !exhausted->enqueueReplacement && !exhausted->failure.empty());

    // Quarantined member: terminal failure, surfaced.
    auto quarantined = registry.OnCreationTerminal(complete(14, CreationPollStatus::Quarantined, 0));
    LIVING_CHECK(quarantined && !quarantined->enqueueReplacement && !quarantined->failure.empty());

    // The replacement is still pending: the batch is not complete.
    LIVING_CHECK(registry.Poll(token, false).status == BatchPollStatus::Pending);

    // The replacement finalizes: batch complete, with the full status report.
    registry.OnCreationTerminal(complete(15, CreationPollStatus::Created, 1002));
    BatchPollResult result = registry.Poll(token, false);
    LIVING_CHECK(result.status == BatchPollStatus::Complete);
    LIVING_CHECK(result.finalizedGuids.size() == 2);
    LIVING_CHECK(result.failures.size() == 2);
    LIVING_CHECK(result.desiredSize == 5 && result.preexistingMembers == 1);

    // Completion is reported exactly once, then the acknowledged batch is
    // gone.
    LIVING_CHECK(registry.TakeNewlyCompleted().size() == 1);
    LIVING_CHECK(registry.TakeNewlyCompleted().empty());
    LIVING_CHECK(registry.Poll(token, true).status == BatchPollStatus::Complete);
    LIVING_CHECK(registry.Poll(token, false).status == BatchPollStatus::Unknown);

    // A completion for an unknown token belongs to no batch.
    LIVING_CHECK(!registry.OnCreationTerminal(complete(99, CreationPollStatus::Created, 1)));
}

LIVING_TEST(creation_batch_registry_is_bounded_and_prunes_expired)
{
    CreationBatchRegistry registry;
    for (size_t i = 0; i < CreationBatchRegistry::kMaxBatches; ++i)
    {
        CreationBatchRegistry::Batch batch;
        batch.pending = { CreationBatchMember{ static_cast<uint64_t>(i + 1), 1, 1 } };
        LIVING_CHECK(registry.Begin(std::move(batch)) != 0);
    }

    // The bounded registry refuses the next batch (callers report the run as
    // unmonitored instead of silently dropping members).
    CreationBatchRegistry::Batch overflow;
    overflow.pending = { CreationBatchMember{ 999, 1, 1 } };
    LIVING_CHECK(registry.Begin(std::move(overflow)) == 0);

    // Completed batches expire after the bounded retention window.
    CreationCompletion completion;
    completion.token = 1;
    completion.status = CreationPollStatus::Created;
    completion.guid = 1;
    registry.OnCreationTerminal(completion);
    registry.PruneExpired(1);
    LIVING_CHECK(registry.BatchCount() == CreationBatchRegistry::kMaxBatches);
    registry.PruneExpired(1 + CreationBatchRegistry::kRetentionPumps);
    LIVING_CHECK(registry.BatchCount() == CreationBatchRegistry::kMaxBatches - 1);
}

LIVING_TEST(creation_batch_completion_enforces_the_desired_size_invariant)
{
    // A batch whose initial run fell short (terminal stop, attempt budget)
    // can complete with an EMPTY failure list if every queued member
    // succeeds - the desired-size invariant at poll time is what stops a
    // partial group from reporting unqualified success.
    CreationBatchRegistry registry;

    CreationBatchRegistry::Batch batch;
    batch.desiredSize = 5;
    batch.preexistingMembers = 1;
    batch.pending = {
        CreationBatchMember{ 21, 1, 1 },
        CreationBatchMember{ 22, 2, 2 },
    }; // only 2 of the 4 needed members were ever queued

    uint64_t const token = registry.Begin(std::move(batch));

    auto created = [](uint64_t creationToken, uint32_t guid)
    {
        CreationCompletion completion;
        completion.token = creationToken;
        completion.guid = guid;
        completion.status = CreationPollStatus::Created;
        completion.name = "Member";
        return completion;
    };

    registry.OnCreationTerminal(created(21, 2001));
    registry.OnCreationTerminal(created(22, 2002));

    BatchPollResult result = registry.Poll(token, false);
    LIVING_CHECK(result.status == BatchPollStatus::Complete);
    LIVING_CHECK(result.failures.empty());
    LIVING_CHECK(result.undersized); // 1 + 2 < 5

    // A full batch is NOT undersized.
    CreationBatchRegistry::Batch full;
    full.desiredSize = 3;
    full.preexistingMembers = 1;
    full.pending = { CreationBatchMember{ 31, 1, 1 }, CreationBatchMember{ 32, 2, 2 } };
    uint64_t const fullToken = registry.Begin(std::move(full));
    registry.OnCreationTerminal(created(31, 3001));
    registry.OnCreationTerminal(created(32, 3002));
    BatchPollResult fullResult = registry.Poll(fullToken, false);
    LIVING_CHECK(fullResult.status == BatchPollStatus::Complete);
    LIVING_CHECK(!fullResult.undersized);

    // While a replacement is still pending, the invariant is not evaluated.
    CreationBatchRegistry::Batch pendingBatch;
    pendingBatch.desiredSize = 3;
    pendingBatch.preexistingMembers = 1;
    pendingBatch.pending = { CreationBatchMember{ 41, 1, 1 }, CreationBatchMember{ 42, 2, 2 } };
    uint64_t const pendingToken = registry.Begin(std::move(pendingBatch));
    registry.OnCreationTerminal(created(41, 4001));
    LIVING_CHECK(registry.Poll(pendingToken, false).status == BatchPollStatus::Pending);
    LIVING_CHECK(!registry.Poll(pendingToken, false).undersized);
}

LIVING_TEST(creation_batch_preserves_allocation_mode_and_slot_class_for_replacements)
{
    // The replacement path must reuse the ORIGINAL run's allocation mode (a
    // random-manager run keeps allocating on random accounts even when the
    // initiating master owns a personal manager) and the failed slot's
    // planned class (per-class composition caps were charged for it).
    CreationBatchRegistry registry;

    CreationBatchRegistry::Batch batch;
    batch.useRandomAccounts = true;
    batch.desiredSize = 2;
    batch.replacementBudget = 1;
    batch.pending = { CreationBatchMember{ 51, /*cls*/ 4, /*role*/ 2 } };
    uint64_t const token = registry.Begin(std::move(batch));

    LIVING_CHECK(registry.Find(token) != nullptr);
    LIVING_CHECK(registry.Find(token)->useRandomAccounts);

    CreationCompletion rolledBack;
    rolledBack.token = 51;
    rolledBack.status = CreationPollStatus::FailedRetryable;
    rolledBack.name = "Member";
    rolledBack.message = "rolled back";

    auto resolution = registry.OnCreationTerminal(rolledBack);
    LIVING_CHECK(resolution && resolution->enqueueReplacement);
    LIVING_CHECK(resolution->cls == 4);  // the slot's planned class travels back
    LIVING_CHECK(resolution->role == 2);
    LIVING_CHECK(registry.Find(resolution->batchToken)->useRandomAccounts);
}
