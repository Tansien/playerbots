#include "LivingTest.h"

#include "../util/LivingCreationBatch.h"
#include "../util/LivingCreationFinalizer.h"

#include <string>
#include <vector>

using namespace living;

// CreationFinalizer is the exact deferred-callback registry the production
// finalizer drives: SQL result callbacks may only call OnCallbackResult /
// OnBulkCallbackResult (a copy into the owning record's or generation's
// mailbox - never dropped, never any ops), and every lifecycle transition,
// metadata write, cleanup, retry and follow-up query runs in Pump. These are
// spy-backed fault-injection tests of that production decision core; the live
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
        std::vector<std::pair<uint32_t, uint64_t>> bulkVerifyRequests;

        bool metadataSucceeds = true;
        bool eventDeleteSucceeds = true;
        bool verifyEnqueueSucceeds = true;
        bool bulkEnqueueSucceeds = true;

        uint32_t bulkGaveUpAccount = 0;
        uint32_t bulkGaveUpKept = 0;

        size_t TotalOps() const
        {
            return verifyRequests.size() + cleanupVerifyRequests.size() + metadataWrites.size()
                + eventDeletes.size() + characterDeletes.size() + created.size()
                + bulkVerifyRequests.size();
        }

        CreationFinalizerOps Make()
        {
            CreationFinalizerOps ops;
            ops.requestVerify = [this](uint32_t guid) { verifyRequests.push_back(guid); return verifyEnqueueSucceeds; };
            ops.requestCleanupVerify = [this](uint32_t guid) { cleanupVerifyRequests.push_back(guid); return verifyEnqueueSucceeds; };
            ops.writeMetadata = [this](uint32_t guid) { metadataWrites.push_back(guid); return metadataSucceeds; };
            ops.deleteEventRows = [this](uint32_t guid) { eventDeletes.push_back(guid); return eventDeleteSucceeds; };
            ops.deleteCharacter = [this](uint32_t guid, uint32_t) { characterDeletes.push_back(guid); };
            ops.onCreated = [this](uint32_t guid, uint32_t, std::string const&, bool) { created.push_back(guid); };
            ops.requestBulkCountVerify = [this](uint32_t accountId, uint64_t generation)
            {
                bulkVerifyRequests.push_back({ accountId, generation });
                return bulkEnqueueSucceeds;
            };
            ops.onBulkVerifyGaveUp = [this](uint32_t accountId, uint32_t kept)
            {
                bulkGaveUpAccount = accountId;
                bulkGaveUpKept = kept;
            };
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

LIVING_TEST(creation_finalizer_callback_only_stores_and_pump_finalizes)
{
    // The interleaving scenario: two queued creations whose verify callbacks
    // arrive while the SQL result queue is being dispatched. Each callback
    // must return WITHOUT performing any operation (in production: any
    // database work - the deadlock vector); the outcome lands in the owning
    // record's mailbox and all finalization work happens later, through Pump.
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

    // The callbacks performed NO operation - only the mailboxes filled.
    LIVING_CHECK(spy.TotalOps() == opsBeforeCallbacks);
    LIVING_CHECK(finalizer.PendingCallbackCount() == 2);

    // Pump (a later world update, after the result-queue mutex is released)
    // performs the metadata writes and completes both creations.
    finalizer.Pump(spy.Make());
    LIVING_CHECK(finalizer.PendingCallbackCount() == 0);
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

LIVING_TEST(creation_finalizer_verify_enqueue_failure_is_bounded_not_stuck)
{
    // The pinned cores report AsyncPQuery ENQUEUE failure via a false return.
    // A failed enqueue feeds a QueryFailed outcome through the record's own
    // mailbox: one bounded verify attempt is consumed per pump, and
    // exhaustion quarantines - the record can never wait forever for a
    // callback that was never queued.
    CreationFinalizer finalizer;
    SpyOps spy;
    spy.verifyEnqueueSucceeds = false;

    uint64_t const token = finalizer.Begin(MakeRecord(150, 1, "NoQueue"), spy.Make());
    LIVING_CHECK(finalizer.PendingCallbackCount() == 1); // synthesized outcome stored

    for (uint32_t i = 0; i < CreationFinalizer::kMaxVerifyAttempts; ++i)
        finalizer.Pump(spy.Make());

    LIVING_CHECK(finalizer.Poll(token, false).status == CreationPollStatus::Quarantined);
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
    // An account at cap-minus-one with the async queue delayed. Several
    // creation attempts arrive; exactly ONE may reserve the remaining slot -
    // the second admission sees durable + reserved.
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

LIVING_TEST(creation_finalizer_bulk_barriers_are_generation_scoped)
{
    // The exact ordering finding: generation A's saves and barrier are
    // queued, then generation B's saves and barrier, BEFORE A's callback
    // arrives. A's callback releases ONLY A's slots (its query proves nothing
    // about B's later saves); B stays reserved until B's own barrier returns.
    CreationFinalizer finalizer;
    SpyOps spy;
    auto ops = spy.Make();

    // Generation A: 2 slots on account 70.
    LIVING_CHECK(finalizer.TryReserveAccountSlot(70, 0, 9));
    LIVING_CHECK(finalizer.TryReserveAccountSlot(70, 0, 9));
    uint64_t const generationA = finalizer.BeginBulkVerify(70, 2, ops);
    LIVING_CHECK(generationA != 0);

    // Generation B: 3 more slots on the SAME account, before A's callback.
    LIVING_CHECK(finalizer.TryReserveAccountSlot(70, 0, 9));
    LIVING_CHECK(finalizer.TryReserveAccountSlot(70, 0, 9));
    LIVING_CHECK(finalizer.TryReserveAccountSlot(70, 0, 9));
    uint64_t const generationB = finalizer.BeginBulkVerify(70, 3, ops);
    LIVING_CHECK(generationB != 0 && generationB != generationA);
    LIVING_CHECK(finalizer.ReservedCount(70) == 5);
    LIVING_CHECK(spy.bulkVerifyRequests.size() == 2); // one barrier per generation

    // A's callback: releases exactly A's 2 slots; B's 3 remain reserved.
    finalizer.OnBulkCallbackResult(generationA, RowVerifyOutcome::Verified);
    finalizer.Pump(ops);
    LIVING_CHECK(finalizer.ReservedCount(70) == 3);
    LIVING_CHECK(finalizer.PendingBulkVerifyCount() == 1);

    // A duplicate/stale result for A matches nothing and is ignored.
    finalizer.OnBulkCallbackResult(generationA, RowVerifyOutcome::Verified);
    finalizer.Pump(ops);
    LIVING_CHECK(finalizer.ReservedCount(70) == 3);

    // B's own barrier releases B.
    finalizer.OnBulkCallbackResult(generationB, RowVerifyOutcome::Verified);
    finalizer.Pump(ops);
    LIVING_CHECK(finalizer.ReservedCount(70) == 0);
    LIVING_CHECK(finalizer.PendingBulkVerifyCount() == 0);
}

LIVING_TEST(creation_finalizer_bulk_generation_retries_independently)
{
    // Generation A keeps failing (retrying after B's saves is harmless: it
    // still releases only A's slots) while B completes; A's bounded give-up
    // keeps only A's reservations.
    CreationFinalizer finalizer;
    SpyOps spy;
    auto ops = spy.Make();

    LIVING_CHECK(finalizer.TryReserveAccountSlot(71, 0, 9));
    uint64_t const generationA = finalizer.BeginBulkVerify(71, 1, ops);

    LIVING_CHECK(finalizer.TryReserveAccountSlot(71, 0, 9));
    LIVING_CHECK(finalizer.TryReserveAccountSlot(71, 0, 9));
    uint64_t const generationB = finalizer.BeginBulkVerify(71, 2, ops);

    // A fails once; B completes meanwhile.
    finalizer.OnBulkCallbackResult(generationA, RowVerifyOutcome::QueryFailed);
    finalizer.OnBulkCallbackResult(generationB, RowVerifyOutcome::Verified);
    finalizer.Pump(ops);
    LIVING_CHECK(finalizer.ReservedCount(71) == 1); // only A's slot remains
    LIVING_CHECK(spy.bulkVerifyRequests.size() == 3); // A's retry was issued

    // A's retry succeeds after B's saves: releases exactly A's slot.
    finalizer.OnBulkCallbackResult(generationA, RowVerifyOutcome::Verified);
    finalizer.Pump(ops);
    LIVING_CHECK(finalizer.ReservedCount(71) == 0);

    // A generation whose readback NEVER succeeds gives up bounded and keeps
    // its slots.
    LIVING_CHECK(finalizer.TryReserveAccountSlot(72, 0, 9));
    uint64_t const generationC = finalizer.BeginBulkVerify(72, 1, ops);
    for (uint32_t i = 0; i < CreationFinalizer::kMaxBulkVerifyAttempts; ++i)
    {
        finalizer.OnBulkCallbackResult(generationC, RowVerifyOutcome::QueryFailed);
        finalizer.Pump(ops);
    }
    LIVING_CHECK(spy.bulkGaveUpAccount == 72 && spy.bulkGaveUpKept == 1);
    LIVING_CHECK(finalizer.ReservedCount(72) == 1); // conservatively blocked
    LIVING_CHECK(finalizer.PendingBulkVerifyCount() == 0);
}

LIVING_TEST(creation_finalizer_callback_delivery_is_lossless_past_256)
{
    // The former drop-on-256 queue scenario: more than 256 combined ordinary
    // and bulk callbacks (the 257th event included) - every one is delivered
    // through its owner's mailbox and every reservation reaches release.
    CreationFinalizer finalizer;
    SpyOps spy;
    auto ops = spy.Make();

    // 60 ordinary records (within the 64-record admission bound)...
    std::vector<uint64_t> tokens;
    for (uint32_t guid = 1000; guid < 1060; ++guid)
        tokens.push_back(finalizer.Begin(MakeRecord(guid, 1, "Bot" + std::to_string(guid)), ops));

    // ...plus 200 bulk generations with one reserved slot each.
    std::vector<uint64_t> generations;
    for (uint32_t account = 2000; account < 2200; ++account)
    {
        LIVING_CHECK(finalizer.TryReserveAccountSlot(account, 0, 9));
        generations.push_back(finalizer.BeginBulkVerify(account, 1, ops));
    }

    // 260 callbacks arrive before any pump - beyond the old 256-event queue.
    for (uint32_t guid = 1000; guid < 1060; ++guid)
        finalizer.OnCallbackResult(guid, RowVerifyOutcome::Verified, CreationCallbackKind::Verify);
    for (uint64_t generation : generations)
        finalizer.OnBulkCallbackResult(generation, RowVerifyOutcome::Verified);

    LIVING_CHECK(finalizer.PendingCallbackCount() == 260);

    finalizer.Pump(ops);

    // Nothing was dropped: every record finalized and every bulk generation
    // released its reservation.
    LIVING_CHECK(finalizer.PendingCallbackCount() == 0);
    LIVING_CHECK(finalizer.RecordCount() == 0);
    LIVING_CHECK(finalizer.PendingBulkVerifyCount() == 0);
    LIVING_CHECK(spy.created.size() == 60);
    for (uint32_t account = 2000; account < 2200; ++account)
        LIVING_CHECK(finalizer.ReservedCount(account) == 0);
}

LIVING_TEST(creation_finalizer_bulk_and_manual_reservations_share_one_ledger)
{
    // Legacy reload creation and a queued manual/group creation admit against
    // the SAME ledger. With cap 9 and a durable count of 7, one bulk slot
    // plus one manual pending reservation fill the account: the next attempt
    // - from either path - is refused.
    CreationFinalizer finalizer;

    LIVING_CHECK(finalizer.TryReserveAccountSlot(80, 7, 9));  // bulk slot
    LIVING_CHECK(finalizer.TryReserveAccountSlot(80, 7, 9));  // manual pending
    LIVING_CHECK(!finalizer.TryReserveAccountSlot(80, 7, 9)); // 7 + 2 >= 9: refused
    LIVING_CHECK(finalizer.ReservedCount(80) == 2);
}

// ---------------------------------------------------------------------------
// CreationBatchRegistry: the single owner of one target group's deficit.

namespace
{
    CreationCompletion MakeCompletion(uint64_t creationToken, CreationPollStatus status, uint32_t guid)
    {
        CreationCompletion completion;
        completion.token = creationToken;
        completion.guid = guid;
        completion.status = status;
        completion.name = "Member" + std::to_string(creationToken);
        completion.message = "test outcome";
        return completion;
    }

    CreationBatchMember PendingMember(uint64_t token, uint8_t cls, uint8_t role)
    {
        CreationBatchMember member;
        member.creationToken = token;
        member.cls = cls;
        member.role = role;
        member.state = BatchMemberState::PendingPersistence;
        return member;
    }
}

LIVING_TEST(creation_batch_members_transition_in_place_through_mixed_outcomes)
{
    // One batch with successful, rolled-back (in-slot replacement within the
    // budget), budget-exhausted-retryable and quarantined members - the
    // ledger is shared, failures are recorded, nothing silently shrinks.
    CreationBatchRegistry registry;

    CreationBatchRegistry::Batch batch;
    batch.initiatorName = "Master";
    batch.initiatorGuid = 77;
    batch.desiredSize = 5;
    batch.preexistingMembers = 1;
    batch.replacementBudget = 1;
    batch.members = {
        PendingMember(11, 1, 1), // success
        PendingMember(12, 2, 2), // rolled back -> in-slot replacement (budget 1)
        PendingMember(13, 3, 4), // retryable -> budget exhausted -> failure
        PendingMember(14, 4, 4), // quarantined -> terminal failure
    };

    uint64_t const token = registry.Begin(std::move(batch));
    LIVING_CHECK(token != 0);
    LIVING_CHECK(registry.Poll(token, false).status == BatchPollStatus::Pending);

    // Successful member: finalized, slot stays owned (join outstanding).
    LIVING_CHECK(registry.OnCreationTerminal(MakeCompletion(11, CreationPollStatus::Created, 1001), 1));

    // Rolled-back member: becomes AwaitingAttempt IN PLACE, keeping its
    // planned class/role for the paced replacement attempt.
    LIVING_CHECK(registry.OnCreationTerminal(MakeCompletion(12, CreationPollStatus::FailedRetryable, 0), 1));
    auto due = registry.TakeDueAttempts(2);
    LIVING_CHECK(due.size() == 1);
    LIVING_CHECK(due[0].cls == 2 && due[0].role == 2);

    // The replacement attempt queues successfully with a fresh token.
    registry.OnAttemptResult(due[0].batchToken, due[0].memberIndex,
        BotCreateStatus::PendingPersistence, 15, 2, 2);

    // Budget is now exhausted: the next retryable failure is terminal.
    LIVING_CHECK(registry.OnCreationTerminal(MakeCompletion(13, CreationPollStatus::FailedRetryable, 0), 3));

    // Quarantined member: terminal failure, surfaced.
    LIVING_CHECK(registry.OnCreationTerminal(MakeCompletion(14, CreationPollStatus::Quarantined, 0), 3));

    // The replacement is still pending: the batch is not complete.
    LIVING_CHECK(registry.Poll(token, false).status == BatchPollStatus::Pending);

    // The replacement finalizes: batch complete, with the full status report.
    registry.OnCreationTerminal(MakeCompletion(15, CreationPollStatus::Created, 1002), 4);
    BatchPollResult result = registry.Poll(token, false);
    LIVING_CHECK(result.status == BatchPollStatus::Complete);
    LIVING_CHECK(result.finalizedGuids.size() == 2);
    LIVING_CHECK(result.failures.size() == 2);
    LIVING_CHECK(result.undersized); // 1 + 2 < 5: two slots failed terminally

    // Completion is reported exactly once, then the acknowledged batch is gone.
    LIVING_CHECK(registry.TakeNewlyCompleted().size() == 1);
    LIVING_CHECK(registry.TakeNewlyCompleted().empty());
    LIVING_CHECK(registry.Poll(token, true).status == BatchPollStatus::Complete);
    LIVING_CHECK(registry.Poll(token, false).status == BatchPollStatus::Unknown);

    // A completion for an unknown token belongs to no batch.
    LIVING_CHECK(!registry.OnCreationTerminal(MakeCompletion(99, CreationPollStatus::Created, 1), 5));
}

LIVING_TEST(creation_batch_transient_attempts_are_paced_and_never_terminal_until_exhausted)
{
    // A planned slot facing transient database unavailability: retried with
    // pacing, its transient budget separate from the replacement budget, and
    // only exhaustion records one explicit terminal failure.
    CreationBatchRegistry registry;

    CreationBatchRegistry::Batch batch;
    batch.initiatorGuid = 42;
    batch.desiredSize = 2;
    batch.preexistingMembers = 1;
    batch.replacementBudget = 2;
    uint64_t const token = registry.Begin(std::move(batch));

    // Initial transient shortfall: one planned slot, no token yet.
    LIVING_CHECK(registry.AddPlannedSlots(token, 1, /*pump*/ 0));
    LIVING_CHECK(registry.Poll(token, false).status == BatchPollStatus::Pending);

    // Not due before its backoff elapses - no per-tick hammering.
    LIVING_CHECK(registry.TakeDueAttempts(1).empty());
    auto due = registry.TakeDueAttempts(CreationBatchRegistry::kTransientBackoffPumps);
    LIVING_CHECK(due.size() == 1);

    // The attempt hits a transient failure: slot retained, replacement budget
    // untouched, next attempt paced.
    registry.OnAttemptResult(token, due[0].memberIndex, BotCreateStatus::TransientFailure, 0, 0,
        CreationBatchRegistry::kTransientBackoffPumps);
    LIVING_CHECK(registry.Find(token)->replacementBudget == 2);
    LIVING_CHECK(registry.TakeDueAttempts(CreationBatchRegistry::kTransientBackoffPumps + 1).empty());

    // Recovery: the paced retry succeeds and the slot becomes pending.
    due = registry.TakeDueAttempts(2 * CreationBatchRegistry::kTransientBackoffPumps);
    LIVING_CHECK(due.size() == 1);
    registry.OnAttemptResult(token, due[0].memberIndex, BotCreateStatus::PendingPersistence, 21, 1,
        2 * CreationBatchRegistry::kTransientBackoffPumps);
    LIVING_CHECK(registry.Poll(token, false).status == BatchPollStatus::Pending);

    // A slot that never recovers: bounded transient budget, then ONE explicit
    // terminal failure.
    uint64_t pump = 10 * CreationBatchRegistry::kTransientBackoffPumps;
    CreationBatchRegistry::Batch exhaust;
    exhaust.initiatorGuid = 43;
    exhaust.desiredSize = 2;
    exhaust.preexistingMembers = 1;
    uint64_t const exhaustToken = registry.Begin(std::move(exhaust));
    registry.AddPlannedSlots(exhaustToken, 1, pump);
    for (uint32_t i = 0; i < CreationBatchRegistry::kMaxTransientAttempts; ++i)
    {
        pump += CreationBatchRegistry::kTransientBackoffPumps;
        auto attempts = registry.TakeDueAttempts(pump);
        LIVING_CHECK(attempts.size() == 1);
        registry.OnAttemptResult(exhaustToken, attempts[0].memberIndex,
            BotCreateStatus::TransientFailure, 0, 0, pump);
    }
    BatchPollResult exhausted = registry.Poll(exhaustToken, false);
    LIVING_CHECK(exhausted.status == BatchPollStatus::Complete);
    LIVING_CHECK(exhausted.failures.size() == 1);
    LIVING_CHECK(exhausted.undersized);
}

LIVING_TEST(creation_batch_extension_shares_one_ledger_and_cannot_pass_after_member_failure)
{
    // The cross-batch scenario that used to PASS: request size 2 with one
    // pending member, then request size 3. The second request EXTENDS the
    // same batch (same token, shared ledger). When the ORIGINAL member later
    // fails terminally, the shared batch records it - completion is
    // undersized/failed, never a silent PASS with fewer live members.
    CreationBatchRegistry registry;

    CreationBatchRegistry::Batch batch;
    batch.initiatorGuid = 88;
    batch.desiredSize = 2;
    batch.preexistingMembers = 1;
    batch.members = { PendingMember(31, /*cls*/ 1, /*role*/ 1) }; // the tank slot
    uint64_t const token = registry.Begin(std::move(batch));

    // Second request (size 3): same owner - extend, never a second batch.
    LIVING_CHECK(registry.FindBatchTokenForInitiator(88) == token);
    LIVING_CHECK(registry.ExtendDesiredSize(token, 3));
    LIVING_CHECK(registry.AddPendingMember(token, PendingMember(32, 2, 4)));

    // The pending tank slot constrains the extension's quotas: the caller
    // subtracts every outstanding member's class/role before selecting.
    uint32_t outstandingTanks = 0;
    registry.ForEachOutstandingMember(88, [](uint32_t) { return false; },
        [&outstandingTanks](CreationBatchMember const& member)
        {
            if (member.role == 1)
                ++outstandingTanks;
        });
    LIVING_CHECK(outstandingTanks == 1);
    LIVING_CHECK(registry.OutstandingSlotsForInitiator(88, [](uint32_t) { return false; }) == 2);

    // The ORIGINAL member fails terminally (no budget on this batch): the
    // shared ledger records the failure.
    LIVING_CHECK(registry.OnCreationTerminal(MakeCompletion(31, CreationPollStatus::Quarantined, 0), 1));

    // The extension member succeeds - but the batch cannot report an
    // unqualified success: failures are recorded and it is undersized
    // (1 preexisting + 1 finalized < 3 desired).
    registry.OnCreationTerminal(MakeCompletion(32, CreationPollStatus::Created, 3002), 2);
    BatchPollResult result = registry.Poll(token, false);
    LIVING_CHECK(result.status == BatchPollStatus::Complete);
    LIVING_CHECK(!result.failures.empty());
    LIVING_CHECK(result.undersized);
}

LIVING_TEST(creation_batch_outstanding_slots_hold_the_group_deficit)
{
    // Back-to-back accounting: outstanding slots count BEFORE creation
    // finalizes (pending members) and AFTER finalization but before the
    // member joined the group. Verified membership releases the slot; batch
    // expiry is the bounded terminal path.
    CreationBatchRegistry registry;

    CreationBatchRegistry::Batch batch;
    batch.initiatorGuid = 42;
    batch.desiredSize = 5;
    batch.preexistingMembers = 1;
    batch.members = { PendingMember(61, 1, 1), PendingMember(62, 2, 2) };
    uint64_t const token = registry.Begin(std::move(batch));

    auto nobodyJoined = [](uint32_t) { return false; };

    // Before finalization: both pending members hold their slots.
    LIVING_CHECK(registry.OutstandingSlotsForInitiator(42, nobodyJoined) == 2);
    LIVING_CHECK(registry.FindBatchTokenForInitiator(42) == token);
    // Another initiator owns nothing here.
    LIVING_CHECK(registry.OutstandingSlotsForInitiator(43, nobodyJoined) == 0);

    // One member finalizes but has not joined: still 2 outstanding.
    registry.OnCreationTerminal(MakeCompletion(61, CreationPollStatus::Created, 6001), 1);
    LIVING_CHECK(registry.OutstandingSlotsForInitiator(42, nobodyJoined) == 2);

    // The finalized member JOINS the group: its slot is released; the still-
    // pending member keeps holding.
    auto joined6001 = [](uint32_t guid) { return guid == 6001; };
    LIVING_CHECK(registry.OutstandingSlotsForInitiator(42, joined6001) == 1);

    // The second member finalizes and joins too: no outstanding slots left.
    registry.OnCreationTerminal(MakeCompletion(62, CreationPollStatus::Created, 6002), 2);
    auto bothJoined = [](uint32_t guid) { return guid == 6001 || guid == 6002; };
    LIVING_CHECK(registry.OutstandingSlotsForInitiator(42, bothJoined) == 0);

    // Batch expiry is the bounded terminal path for joins that never happen:
    // after the retention window the slots stop counting.
    LIVING_CHECK(registry.OutstandingSlotsForInitiator(42, nobodyJoined) == 2);
    registry.PruneExpired(1);
    registry.PruneExpired(1 + CreationBatchRegistry::kRetentionPumps);
    LIVING_CHECK(registry.OutstandingSlotsForInitiator(42, nobodyJoined) == 0);
}

LIVING_TEST(creation_batch_registry_is_bounded_and_prunes_expired)
{
    CreationBatchRegistry registry;
    for (size_t i = 0; i < CreationBatchRegistry::kMaxBatches; ++i)
    {
        CreationBatchRegistry::Batch batch;
        batch.members = { PendingMember(static_cast<uint64_t>(i + 1), 1, 1) };
        LIVING_CHECK(registry.Begin(std::move(batch)) != 0);
    }

    // The bounded registry refuses the next batch (callers report the run as
    // unmonitored instead of silently dropping members).
    CreationBatchRegistry::Batch overflow;
    overflow.members = { PendingMember(999, 1, 1) };
    LIVING_CHECK(registry.Begin(std::move(overflow)) == 0);

    // Completed batches expire after the bounded retention window.
    registry.OnCreationTerminal(MakeCompletion(1, CreationPollStatus::Created, 1), 1);
    registry.PruneExpired(1);
    LIVING_CHECK(registry.BatchCount() == CreationBatchRegistry::kMaxBatches);
    registry.PruneExpired(1 + CreationBatchRegistry::kRetentionPumps);
    LIVING_CHECK(registry.BatchCount() == CreationBatchRegistry::kMaxBatches - 1);
}
