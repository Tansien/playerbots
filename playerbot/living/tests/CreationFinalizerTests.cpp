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
        bool cleanupEnqueueSucceeds = true;
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
            ops.requestCleanupVerify = [this](uint32_t guid) { cleanupVerifyRequests.push_back(guid); return cleanupEnqueueSucceeds; };
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

LIVING_TEST(creation_finalizer_cleanup_reenqueue_failure_is_bounded_not_stranded)
{
    // Finding 3: while PendingCleanup, the re-issued cleanup verify can fail to
    // ENQUEUE (or the callback is missing). The pinned cores report that via a
    // false AsyncPQuery return. Without feeding a synthetic QueryFailed back
    // through the mailbox no outcome ever arrives and the record - with its
    // account reservation and one of the 64 registry slots - is stranded in
    // PendingCleanup forever, with no terminal completion. The fix mirrors the
    // ordinary verify/bulk paths: a missing/false enqueue synthesizes
    // QueryFailed so the bounded lifecycle owns the record and quarantines it.
    CreationFinalizer finalizer;
    SpyOps spy;
    spy.metadataSucceeds = false;

    uint64_t const token = finalizer.Begin(MakeRecord(601, 1, "CleanupStrand"), spy.Make());
    finalizer.OnCallbackResult(601, RowVerifyOutcome::Verified, CreationCallbackKind::Verify);
    finalizer.Pump(spy.Make()); // metadata fails -> PendingCleanup + one cleanup verify requested
    LIVING_CHECK(spy.cleanupVerifyRequests.size() == 1);
    LIVING_CHECK(finalizer.Poll(token, false).status == CreationPollStatus::Pending);
    LIVING_CHECK(finalizer.RecordCount() == 1);

    // The character keeps verifying as still present and EVERY re-enqueue fails.
    spy.cleanupEnqueueSucceeds = false;
    finalizer.OnCallbackResult(601, RowVerifyOutcome::Verified, CreationCallbackKind::CleanupVerify);
    for (uint32_t i = 0; i < CreationFinalizer::kMaxCleanupAttempts + 2; ++i)
        finalizer.Pump(spy.Make());

    // Bounded, not stranded: the record reaches a real terminal (Quarantined)
    // and no orphaned mailbox entry remains.
    LIVING_CHECK(finalizer.Poll(token, false).status == CreationPollStatus::Quarantined);
    LIVING_CHECK(finalizer.PendingCallbackCount() == 0);
}

LIVING_TEST(creation_finalizer_cleanup_reenqueue_recovers_and_frees_the_slot)
{
    // The same failure window, but the async queue recovers within the bounded
    // budget: the re-enqueue then succeeds, the real callback confirms the
    // character absent, and the record is erased - freeing both its registry
    // slot and its account reservation (never permanently retained).
    CreationFinalizer finalizer;
    SpyOps spy;
    spy.metadataSucceeds = false;

    LIVING_CHECK(finalizer.TryReserveAccountSlot(5, 0, 9));
    CreationFinalizer::Record record = MakeRecord(602, 5, "CleanupRecover");
    record.holdsAccountReservation = true;
    uint64_t const token = finalizer.Begin(std::move(record), spy.Make());
    finalizer.OnCallbackResult(602, RowVerifyOutcome::Verified, CreationCallbackKind::Verify);
    finalizer.Pump(spy.Make()); // -> PendingCleanup
    LIVING_CHECK(finalizer.ReservedCount(5) == 1);
    LIVING_CHECK(finalizer.RecordCount() == 1);

    // Two failed re-enqueues keep the bounded lifecycle alive via synthetic
    // QueryFailed (attempts stay under kMaxCleanupAttempts).
    spy.cleanupEnqueueSucceeds = false;
    finalizer.OnCallbackResult(602, RowVerifyOutcome::QueryFailed, CreationCallbackKind::CleanupVerify);
    finalizer.Pump(spy.Make());
    finalizer.Pump(spy.Make());
    LIVING_CHECK(finalizer.Poll(token, false).status == CreationPollStatus::Pending);

    // Recovery: the next re-enqueue succeeds and the real callback reports the
    // character verifiably gone.
    spy.cleanupEnqueueSucceeds = true;
    finalizer.Pump(spy.Make()); // consumes the last synthetic; real cleanup verify requested
    finalizer.OnCallbackResult(602, RowVerifyOutcome::Absent, CreationCallbackKind::CleanupVerify);
    finalizer.Pump(spy.Make());

    LIVING_CHECK(finalizer.RecordCount() == 0);    // slot freed
    LIVING_CHECK(finalizer.ReservedCount(5) == 0); // reservation released
    LIVING_CHECK(finalizer.Poll(token, true).status == CreationPollStatus::FailedRetryable);
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

    // Second request (size 3): same owner - extend, never a second batch. The
    // extension observes the same one live member the batch began with.
    LIVING_CHECK(registry.FindBatchTokenForInitiator(88) == token);
    LIVING_CHECK(registry.ExtendDesiredSize(token, 3, /*liveMembers*/ 1, [](uint32_t) { return false; }));
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

LIVING_TEST(creation_batch_planned_slot_persists_drawn_role)
{
    // Finding 7a: a planned/transient slot is created role-0 and the pump draws
    // its role at attempt time. If OnAttemptResult drops that role, quota
    // accounting (which skips role-0 outstanding members) charges the wrong
    // role and a retryable rollback redraws it. The role must persist.
    CreationBatchRegistry registry;

    CreationBatchRegistry::Batch batch;
    batch.initiatorGuid = 91;
    batch.desiredSize = 3;
    batch.preexistingMembers = 1;
    batch.replacementBudget = 2;
    uint64_t const token = registry.Begin(std::move(batch));
    LIVING_CHECK(registry.AddPlannedSlots(token, 1, 0));

    auto nobodyJoined = [](uint32_t) { return false; };
    auto countTanks = [&]() {
        uint32_t tanks = 0;
        registry.ForEachOutstandingMember(91, nobodyJoined,
            [&tanks](CreationBatchMember const& m) { if (m.role == 1) ++tanks; });
        return tanks;
    };

    // Before the attempt the planned slot's role is unknown (0): outstanding,
    // but not yet counted as a tank.
    LIVING_CHECK(countTanks() == 0);

    auto due = registry.TakeDueAttempts(CreationBatchRegistry::kTransientBackoffPumps);
    LIVING_CHECK(due.size() == 1);
    LIVING_CHECK(due[0].role == 0); // planned slot: role is drawn by the pump

    // The pump draws a tank role (1) and reports the queued attempt.
    registry.OnAttemptResult(due[0].batchToken, due[0].memberIndex,
        BotCreateStatus::PendingPersistence, /*creationToken*/55, /*cls*/1, /*pump*/1, /*role*/1);
    LIVING_CHECK(countTanks() == 1); // pre-fix: 0 (role dropped) -> quota over-allocation

    // A retryable rollback returns the slot to AwaitingAttempt with its class
    // AND role preserved for the paced replacement (pre-fix: redrawn as 0).
    registry.OnCreationTerminal(MakeCompletion(55, CreationPollStatus::FailedRetryable, 0), 2);
    auto retry = registry.TakeDueAttempts(3);
    LIVING_CHECK(retry.size() == 1);
    LIVING_CHECK(retry[0].role == 1);
    LIVING_CHECK(retry[0].cls == 1);
}

LIVING_TEST(creation_batch_completed_batch_never_blocks_a_new_run)
{
    // Finding N4: a completed/joined batch is retained for polling but must NOT
    // own the deficit or block a differently-configured new request. It stays
    // counted for anti-duplication until its members are verified joined, but
    // FindBatchTokenForInitiator (the block gate) never returns it.
    CreationBatchRegistry registry;

    CreationBatchRegistry::Batch a;
    a.initiatorGuid = 77;
    a.desiredSize = 2;
    a.preexistingMembers = 1;
    a.memberGear = "epic";
    a.useRandomAccounts = true; // distinct options
    a.members.push_back(PendingMember(51, 1, 1));
    uint64_t const tokenA = registry.Begin(std::move(a));
    registry.OnCreationTerminal(MakeCompletion(51, CreationPollStatus::Created, 7001), 1);

    auto nobodyJoined = [](uint32_t) { return false; };
    auto joined7001 = [](uint32_t g) { return g == 7001; };

    // Finalized-but-unjoined: retained + pollable + still counted for the
    // deficit, but NOT the active owner. (While that unjoined dependency
    // exists, the request PLANNER refuses different options - see the
    // group_batch_request_plan tests; the registry itself never blocks.)
    LIVING_CHECK(registry.Poll(tokenA, false).status == BatchPollStatus::Complete);
    LIVING_CHECK(registry.Find(tokenA) != nullptr);
    LIVING_CHECK(registry.FindBatchTokenForInitiator(77) == 0);
    LIVING_CHECK(registry.OutstandingSlotsForInitiator(77, nobodyJoined) == 1); // anti-dup retained
    LIVING_CHECK(registry.OutstandingSlotsForInitiator(77, joined7001) == 0);   // joined -> no deficit

    // Once the dependency is JOINED, a differently-configured request opens a
    // FRESH batch instead of being refused.
    CreationBatchRegistry::Batch b;
    b.initiatorGuid = 77;
    b.desiredSize = 3;
    b.preexistingMembers = 1;
    b.memberGear = "default";
    b.useRandomAccounts = false;
    b.members.push_back(PendingMember(52, 2, 2));
    uint64_t const tokenB = registry.Begin(std::move(b));
    LIVING_CHECK(tokenB && tokenB != tokenA);
    LIVING_CHECK(registry.FindBatchTokenForInitiator(77) == tokenB);
}

LIVING_TEST(creation_batch_extension_grows_replacement_budget_by_positive_delta)
{
    // Finding N3: extending desiredSize must grant one replacement per ADDED
    // slot. A 5 -> 40 extension leaving the original 5 replacements would starve
    // the 35 new slots. The delta is added to the REMAINING budget; equal/smaller
    // extensions never shrink either field.
    CreationBatchRegistry registry;

    CreationBatchRegistry::Batch batch;
    batch.initiatorGuid = 501;
    batch.desiredSize = 5;
    batch.replacementBudget = 5; // seeded == groupSize
    batch.members.push_back(PendingMember(71, 1, 1));
    uint64_t const token = registry.Begin(std::move(batch));

    // A retryable failure BEFORE extending draws the budget down, proving the
    // delta adds to the REMAINING budget, not the original seed.
    registry.OnCreationTerminal(MakeCompletion(71, CreationPollStatus::FailedRetryable, 0), 1); // 5 -> 4
    LIVING_CHECK(registry.Find(token)->replacementBudget == 4);

    auto nobodyJoined = [](uint32_t) { return false; };
    LIVING_CHECK(registry.ExtendDesiredSize(token, 40, 0, nobodyJoined)); // +35 delta
    LIVING_CHECK(registry.Find(token)->desiredSize == 40);
    LIVING_CHECK(registry.Find(token)->replacementBudget == 39); // 4 + 35 (pre-fix: stuck at 4)

    LIVING_CHECK(registry.ExtendDesiredSize(token, 40, 0, nobodyJoined)); // equal: no-op
    LIVING_CHECK(registry.Find(token)->replacementBudget == 39);
    LIVING_CHECK(registry.ExtendDesiredSize(token, 10, 0, nobodyJoined)); // smaller: never shrinks
    LIVING_CHECK(registry.Find(token)->desiredSize == 40);
    LIVING_CHECK(registry.Find(token)->replacementBudget == 39);
}

LIVING_TEST(creation_batch_completed_batch_is_not_extended)
{
    // Finding 7b: a completed batch is retained for pollers but must NOT be
    // extended - reviving it keeps a stale completionReported flag (so a later
    // completion never surfaces) and an expired retention timer. A repeat
    // request must start a FRESH batch.
    CreationBatchRegistry registry;

    CreationBatchRegistry::Batch batchA;
    batchA.initiatorGuid = 92;
    batchA.desiredSize = 2;
    batchA.preexistingMembers = 1;
    batchA.members.push_back(PendingMember(41, 1, 1));
    uint64_t const tokenA = registry.Begin(std::move(batchA));
    LIVING_CHECK(registry.FindBatchTokenForInitiator(92) == tokenA); // active -> extendable

    registry.OnCreationTerminal(MakeCompletion(41, CreationPollStatus::Created, 4001), 1);
    LIVING_CHECK(registry.Poll(tokenA, false).status == BatchPollStatus::Complete);
    LIVING_CHECK(registry.TakeNewlyCompleted().size() == 1); // completionReported now set

    // Retained but no longer extendable: a repeat request cannot revive A.
    LIVING_CHECK(registry.Find(tokenA) != nullptr);
    LIVING_CHECK(registry.FindBatchTokenForInitiator(92) == 0); // pre-fix: returned tokenA

    CreationBatchRegistry::Batch batchB;
    batchB.initiatorGuid = 92;
    batchB.desiredSize = 2;
    batchB.preexistingMembers = 1;
    batchB.members.push_back(PendingMember(42, 2, 2));
    uint64_t const tokenB = registry.Begin(std::move(batchB));
    LIVING_CHECK(tokenB != tokenA);
    LIVING_CHECK(registry.FindBatchTokenForInitiator(92) == tokenB);

    // B's completion surfaces on its own (pre-fix, extending A left A's
    // completionReported=true and swallowed the second completion).
    registry.OnCreationTerminal(MakeCompletion(42, CreationPollStatus::Created, 4002), 2);
    LIVING_CHECK(registry.Poll(tokenB, false).status == BatchPollStatus::Complete);
    LIVING_CHECK(registry.TakeNewlyCompleted().size() == 1);
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

// PlanGroupBatchRequest is the exact ownership/accounting decision HandleGroup
// runs for every repeated group request; these tests drive the same decision
// flow the handler follows (plan -> reject / extend / credit / begin) instead
// of manually assembling a second batch around the registry.

namespace
{
    GroupBatchOptions Options(std::string gear, bool autoAdd = false, bool temporary = false,
        bool randomAccounts = false)
    {
        GroupBatchOptions options;
        options.gear = std::move(gear);
        options.autoAdd = autoAdd;
        options.temporary = temporary;
        options.useRandomAccounts = randomAccounts;
        return options;
    }
}

LIVING_TEST(group_batch_request_plan_rejects_changed_options_over_retained_unjoined_members)
{
    // Completed retained batch with a finalized-but-unjoined member, then the
    // SAME size requested with different options. The old flow skipped the
    // compatibility gate (no ACTIVE batch), credited the unjoined member, and
    // silently queued nothing under the new options.
    CreationBatchRegistry registry;

    CreationBatchRegistry::Batch a;
    a.initiatorGuid = 77;
    a.desiredSize = 2;
    a.preexistingMembers = 1;
    a.memberGear = "epic";
    a.members.push_back(PendingMember(51, 1, 1));
    registry.Begin(std::move(a));
    registry.OnCreationTerminal(MakeCompletion(51, CreationPollStatus::Created, 7001), 1);

    auto nobodyJoined = [](uint32_t) { return false; };

    GroupBatchRequestPlan plan = PlanGroupBatchRequest(registry, 77, nobodyJoined,
        /*live*/ 1, /*requested*/ 2, Options("default"));
    LIVING_CHECK(plan.action == GroupBatchRequestAction::RejectIncompatible);
    LIVING_CHECK(plan.outstandingSlots == 1); // reported in the explicit refusal

    // The SAME options are compatible: the dependency is explicitly credited
    // and the request is already covered - a deliberate message, not silence.
    plan = PlanGroupBatchRequest(registry, 77, nobodyJoined, 1, 2, Options("epic"));
    LIVING_CHECK(plan.action == GroupBatchRequestAction::AlreadyCovered);
    LIVING_CHECK(plan.outstandingSlots == 1 && plan.effectiveMembers == 2);

    // Once the member joins, no dependency remains: different options may
    // begin a fresh batch.
    auto joined7001 = [](uint32_t guid) { return guid == 7001; };
    plan = PlanGroupBatchRequest(registry, 77, joined7001, 2, 2, Options("default"));
    LIVING_CHECK(plan.action == GroupBatchRequestAction::AlreadyCovered);
    LIVING_CHECK(plan.outstandingSlots == 0);
    plan = PlanGroupBatchRequest(registry, 77, joined7001, 2, 5, Options("default"));
    LIVING_CHECK(plan.action == GroupBatchRequestAction::BeginNew);
}

LIVING_TEST(group_batch_request_plan_credits_dependencies_without_false_undersize)
{
    // Completed-unjoined batch followed by a compatible LARGER request: the
    // fresh batch credits the dependency in its baseline, queues only the
    // residual, and its completion must NOT report a false undersize for the
    // slot the older batch's ledger still owns.
    CreationBatchRegistry registry;

    CreationBatchRegistry::Batch a;
    a.initiatorGuid = 88;
    a.desiredSize = 2;
    a.preexistingMembers = 1;
    a.memberGear = "epic";
    a.members.push_back(PendingMember(61, 1, 1));
    registry.Begin(std::move(a));
    registry.OnCreationTerminal(MakeCompletion(61, CreationPollStatus::Created, 8001), 1);

    auto nobodyJoined = [](uint32_t) { return false; };

    GroupBatchRequestPlan const plan = PlanGroupBatchRequest(registry, 88, nobodyJoined,
        /*live*/ 1, /*requested*/ 4, Options("epic"));
    LIVING_CHECK(plan.action == GroupBatchRequestAction::BeginNew);
    LIVING_CHECK(plan.outstandingSlots == 1);
    LIVING_CHECK(plan.creditedPreexisting == 2); // live + credited dependency
    LIVING_CHECK(plan.creditedDependencies.size() == 1); // tracked by reference

    // The handler's follow-through: residual deficit is 4 - 2 = 2 members;
    // the live baseline and the credited REFERENCE are stored separately.
    CreationBatchRegistry::Batch b;
    b.initiatorGuid = 88;
    b.desiredSize = 4;
    b.preexistingMembers = 1; // live members only
    b.creditedDependencies = plan.creditedDependencies;
    b.members = { PendingMember(62, 2, 2), PendingMember(63, 3, 4) };
    uint64_t const tokenB = registry.Begin(std::move(b));

    registry.OnCreationTerminal(MakeCompletion(62, CreationPollStatus::Created, 8002), 2);
    registry.OnCreationTerminal(MakeCompletion(63, CreationPollStatus::Created, 8003), 3);

    // Own members are done, but the referenced credit is UNRESOLVED: the
    // batch must NOT report final success yet - the credited member could
    // still vanish. (This used to poll Complete prematurely.)
    LIVING_CHECK(registry.Poll(tokenB, false).status == BatchPollStatus::Pending);

    // The credited member joins: the per-pump re-evaluation confirms the
    // credit, and only now is completion final and genuinely full-size.
    auto joined8001For = [](uint32_t, uint32_t guid) { return guid == 8001; };
    registry.ReevaluateCredits(10, joined8001For);
    BatchPollResult const result = registry.Poll(tokenB, false);
    LIVING_CHECK(result.status == BatchPollStatus::Complete);
    LIVING_CHECK(!result.undersized); // pre-fix: 1 + 2 < 4 -> false undersize
    LIVING_CHECK(result.failures.empty());
}

LIVING_TEST(group_batch_expired_predecessor_credit_is_replaced_or_reported)
{
    // Finding: a fresh batch's credit for a predecessor's finalized-unjoined
    // member used to be a bare number, so the predecessor could expire (be
    // pruned after retention) while the new batch was still active - and the
    // new batch then reported a FULL-SIZE success on the vanished credit.
    // Tracked credits re-evaluate every pump: a joined member confirms, a
    // vanished one is replaced within the bounded budget or reported.
    auto nobodyJoined = [](uint32_t) { return false; };
    auto nobodyJoinedFor = [](uint32_t, uint32_t) { return false; };

    // --- Lost credit WITH replacement budget: a replacement slot is added.
    {
        CreationBatchRegistry registry;
        CreationBatchRegistry::Batch a;
        a.initiatorGuid = 91;
        a.desiredSize = 2;
        a.preexistingMembers = 1;
        a.memberGear = "epic";
        a.members.push_back(PendingMember(71, 1, 1));
        uint64_t const tokenA = registry.Begin(std::move(a));
        registry.OnCreationTerminal(MakeCompletion(71, CreationPollStatus::Created, 9101), 1);
        // A is now completed-retained with one finalized-UNJOINED member.

        GroupBatchRequestPlan const plan = PlanGroupBatchRequest(registry, 91, nobodyJoined,
            /*live*/ 1, /*requested*/ 3, Options("epic"));
        LIVING_CHECK(plan.action == GroupBatchRequestAction::BeginNew);
        LIVING_CHECK(plan.creditedDependencies.size() == 1);

        CreationBatchRegistry::Batch b;
        b.initiatorGuid = 91;
        b.desiredSize = 3;
        b.preexistingMembers = 1;
        b.creditedDependencies = plan.creditedDependencies;
        b.replacementBudget = 3;
        // The residual deficit is still pending, so B stays ACTIVE far beyond
        // the predecessor's retention - the finding's exact window.
        b.members = { PendingMember(72, 2, 2) };
        uint64_t const tokenB = registry.Begin(std::move(b));

        // The predecessor expires (retention elapsed, member never joined)
        // while B is still active.
        registry.PruneExpired(10);
        registry.PruneExpired(10 + CreationBatchRegistry::kRetentionPumps);
        LIVING_CHECK(registry.Find(tokenA) == nullptr);
        LIVING_CHECK(registry.Find(tokenB) != nullptr);

        // Re-evaluation replaces the vanished credit within the budget.
        registry.ReevaluateCredits(20000, nobodyJoinedFor);
        LIVING_CHECK(registry.Find(tokenB)->creditedDependencies.empty());
        LIVING_CHECK(registry.Find(tokenB)->replacementBudget == 2);

        // Both the residual member and the replacement finalize: completion
        // is genuine (1 live + 2 own members = 3), not credit-inflated.
        registry.OnCreationTerminal(MakeCompletion(72, CreationPollStatus::Created, 9102), 20000);
        auto due = registry.TakeDueAttempts(20001);
        LIVING_CHECK(due.size() == 1);
        registry.OnAttemptResult(due[0].batchToken, due[0].memberIndex,
            BotCreateStatus::PendingPersistence, 90, 3, 20001, 4);
        registry.OnCreationTerminal(MakeCompletion(90, CreationPollStatus::Created, 9103), 20002);
        BatchPollResult const result = registry.Poll(tokenB, false);
        LIVING_CHECK(result.status == BatchPollStatus::Complete);
        LIVING_CHECK(!result.undersized);
    }

    // --- Lost credit WITHOUT budget: reported, and completion is undersized.
    {
        CreationBatchRegistry registry;
        CreationBatchRegistry::Batch a;
        a.initiatorGuid = 92;
        a.desiredSize = 2;
        a.preexistingMembers = 1;
        a.memberGear = "epic";
        a.members.push_back(PendingMember(81, 1, 1));
        registry.Begin(std::move(a));
        registry.OnCreationTerminal(MakeCompletion(81, CreationPollStatus::Created, 9201), 1);

        GroupBatchRequestPlan const plan = PlanGroupBatchRequest(registry, 92, nobodyJoined,
            1, 3, Options("epic"));
        CreationBatchRegistry::Batch b;
        b.initiatorGuid = 92;
        b.desiredSize = 3;
        b.preexistingMembers = 1;
        b.creditedDependencies = plan.creditedDependencies;
        b.replacementBudget = 0; // exhausted
        b.members = { PendingMember(82, 2, 2) }; // keeps B active through the prunes
        uint64_t const tokenB = registry.Begin(std::move(b));

        registry.PruneExpired(10);
        registry.PruneExpired(10 + CreationBatchRegistry::kRetentionPumps);
        registry.ReevaluateCredits(20000, nobodyJoinedFor);
        registry.OnCreationTerminal(MakeCompletion(82, CreationPollStatus::Created, 9202), 20001);

        BatchPollResult const result = registry.Poll(tokenB, false);
        LIVING_CHECK(result.status == BatchPollStatus::Complete);
        LIVING_CHECK(result.undersized);          // pre-fix: full-size success
        LIVING_CHECK(!result.failures.empty());   // the loss is reported
    }

    // --- The credited member JOINS before expiry: confirmed permanently,
    // and survives the predecessor's later pruning.
    {
        CreationBatchRegistry registry;
        CreationBatchRegistry::Batch a;
        a.initiatorGuid = 93;
        a.desiredSize = 2;
        a.preexistingMembers = 1;
        a.memberGear = "epic";
        a.members.push_back(PendingMember(85, 1, 1));
        registry.Begin(std::move(a));
        registry.OnCreationTerminal(MakeCompletion(85, CreationPollStatus::Created, 9301), 1);

        GroupBatchRequestPlan const plan = PlanGroupBatchRequest(registry, 93, nobodyJoined,
            1, 3, Options("epic"));
        CreationBatchRegistry::Batch b;
        b.initiatorGuid = 93;
        b.desiredSize = 3;
        b.preexistingMembers = 1;
        b.creditedDependencies = plan.creditedDependencies;
        b.members = { PendingMember(86, 2, 2) }; // keeps B active through the prunes
        uint64_t const tokenB = registry.Begin(std::move(b));

        auto joined9301For = [](uint32_t, uint32_t guid) { return guid == 9301; };
        registry.ReevaluateCredits(100, joined9301For);
        LIVING_CHECK(registry.Find(tokenB)->preexistingMembers == 2); // confirmed
        LIVING_CHECK(registry.Find(tokenB)->creditedDependencies.empty());

        registry.PruneExpired(10);
        registry.PruneExpired(10 + CreationBatchRegistry::kRetentionPumps);
        registry.OnCreationTerminal(MakeCompletion(86, CreationPollStatus::Created, 9302), 20001);
        BatchPollResult const result = registry.Poll(tokenB, false);
        LIVING_CHECK(result.status == BatchPollStatus::Complete);
        LIVING_CHECK(!result.undersized); // 2 + 1 own >= 3
    }
}

LIVING_TEST(group_batch_extension_refreshes_live_membership_baseline)
{
    // Live membership grows (unrelated players join) while a batch is active;
    // a compatible extension must adopt the fresh baseline. The batch's own
    // already-joined finalized member is subtracted so it is never counted
    // both as preexisting and as finalized.
    CreationBatchRegistry registry;

    CreationBatchRegistry::Batch batch;
    batch.initiatorGuid = 99;
    batch.desiredSize = 3;
    batch.preexistingMembers = 1; // master alone at Begin
    batch.memberGear = "epic";
    batch.members = { PendingMember(71, 1, 1), PendingMember(72, 2, 2) };
    uint64_t const token = registry.Begin(std::move(batch));

    // One member finalizes and JOINS; additionally two unrelated players
    // joined the live group: live membership is now 1 + 1 (own) + 2 = 4.
    registry.OnCreationTerminal(MakeCompletion(71, CreationPollStatus::Created, 9001), 1);
    auto joined9001 = [](uint32_t guid) { return guid == 9001; };

    GroupBatchRequestPlan const plan = PlanGroupBatchRequest(registry, 99, joined9001,
        /*live*/ 4, /*requested*/ 6, Options("epic"));
    LIVING_CHECK(plan.action == GroupBatchRequestAction::ExtendExisting);
    LIVING_CHECK(plan.existingToken == token);
    LIVING_CHECK(plan.outstandingSlots == 1); // only the still-pending member

    LIVING_CHECK(registry.ExtendDesiredSize(token, 6, /*live*/ 4, joined9001));
    // Baseline: 4 live minus the batch's own joined member = 3.
    LIVING_CHECK(registry.Find(token)->preexistingMembers == 3);
    LIVING_CHECK(registry.Find(token)->desiredSize == 6);

    // Residual deficit the extension queues: 6 - (4 live + 1 outstanding) = 1.
    registry.AddPendingMember(token, PendingMember(73, 3, 4));

    registry.OnCreationTerminal(MakeCompletion(72, CreationPollStatus::Created, 9002), 2);
    registry.OnCreationTerminal(MakeCompletion(73, CreationPollStatus::Created, 9003), 3);

    BatchPollResult const result = registry.Poll(token, false);
    LIVING_CHECK(result.status == BatchPollStatus::Complete);
    // 3 preexisting + 3 finalized >= 6: the stale baseline (1) would have
    // reported this complete group as undersized.
    LIVING_CHECK(!result.undersized);
}

LIVING_TEST(group_batch_credits_have_exactly_one_owner)
{
    // Three identical covered requests over one predecessor slot: the FIRST
    // creates the tracking owner; the second and third find that owner ACTIVE
    // (unresolved credits make a batch active) and never spawn independent
    // trackers - so predecessor expiry produces exactly ONE replacement.
    CreationBatchRegistry registry;
    auto nobodyJoined = [](uint32_t) { return false; };
    auto nobodyJoinedFor = [](uint32_t, uint32_t) { return false; };

    CreationBatchRegistry::Batch a;
    a.initiatorGuid = 95;
    a.desiredSize = 2;
    a.preexistingMembers = 1;
    a.memberGear = "epic";
    a.members.push_back(PendingMember(41, 1, 1));
    uint64_t const tokenA = registry.Begin(std::move(a));
    registry.OnCreationTerminal(MakeCompletion(41, CreationPollStatus::Created, 9401), 1);
    // A is completed-retained with one finalized-UNJOINED member.

    // Request 1: covered - creates the tracking owner with the credit.
    GroupBatchRequestPlan plan1 = PlanGroupBatchRequest(registry, 95, nobodyJoined, 1, 2, Options("epic"));
    LIVING_CHECK(plan1.action == GroupBatchRequestAction::AlreadyCovered);
    LIVING_CHECK(plan1.creditedDependencies.size() == 1);
    CreationBatchRegistry::Batch tracker;
    tracker.initiatorGuid = 95;
    tracker.desiredSize = 2;
    tracker.preexistingMembers = 1;
    tracker.creditedDependencies = plan1.creditedDependencies;
    tracker.replacementBudget = 2;
    tracker.memberGear = "epic";
    uint64_t const trackerToken = registry.Begin(std::move(tracker));

    // Requests 2 and 3: the tracker is now the ACTIVE owner (unresolved
    // credits count), so the handler extends it instead of creating another -
    // and even if a plan were computed, the credit is no longer collectable.
    LIVING_CHECK(registry.FindBatchTokenForInitiator(95) == trackerToken);
    GroupBatchRequestPlan plan2 = PlanGroupBatchRequest(registry, 95, nobodyJoined, 1, 2, Options("epic"));
    LIVING_CHECK(plan2.action == GroupBatchRequestAction::ExtendExisting);
    LIVING_CHECK(plan2.existingToken == trackerToken);
    LIVING_CHECK(registry.CollectOutstandingMemberRefs(95, nobodyJoined).empty()); // credit already owned
    LIVING_CHECK(registry.IsCreditOwned(tokenA, 0));

    // Changed options while the credit is unresolved: REJECTED - the credit
    // holder participates in option-compatibility accounting.
    GroupBatchRequestPlan incompatible = PlanGroupBatchRequest(registry, 95, nobodyJoined, 1, 2, Options("default"));
    LIVING_CHECK(incompatible.action == GroupBatchRequestAction::RejectIncompatible);

    // Predecessor expires: exactly ONE replacement appears, in the tracker.
    registry.PruneExpired(10);
    registry.PruneExpired(10 + CreationBatchRegistry::kRetentionPumps);
    LIVING_CHECK(registry.Find(tokenA) == nullptr);
    registry.ReevaluateCredits(20000, nobodyJoinedFor);

    uint32_t replacements = 0;
    for (living::CreationBatchMember const& member : registry.Find(trackerToken)->members)
        if (member.state == BatchMemberState::AwaitingAttempt)
            ++replacements;
    LIVING_CHECK(replacements == 1);
    LIVING_CHECK(registry.Find(trackerToken)->creditedDependencies.empty());

    // The replacement finalizes: an observable, genuine completion.
    auto due = registry.TakeDueAttempts(20001);
    LIVING_CHECK(due.size() == 1);
    registry.OnAttemptResult(due[0].batchToken, due[0].memberIndex,
        BotCreateStatus::PendingPersistence, 99, 3, 20001, 4);
    registry.OnCreationTerminal(MakeCompletion(99, CreationPollStatus::Created, 9402), 20002);
    LIVING_CHECK(registry.Poll(trackerToken, false).status == BatchPollStatus::Complete);
    LIVING_CHECK(!registry.Poll(trackerToken, false).undersized);
    LIVING_CHECK(registry.TakeNewlyCompleted().size() == 1); // notified exactly once

    // Predecessor JOINS variant: the credit confirms into the baseline and no
    // replacement is created.
    CreationBatchRegistry joins;
    CreationBatchRegistry::Batch b;
    b.initiatorGuid = 96;
    b.desiredSize = 2;
    b.preexistingMembers = 1;
    b.memberGear = "epic";
    b.members.push_back(PendingMember(51, 1, 1));
    joins.Begin(std::move(b));
    joins.OnCreationTerminal(MakeCompletion(51, CreationPollStatus::Created, 9501), 1);
    GroupBatchRequestPlan planJ = PlanGroupBatchRequest(joins, 96, nobodyJoined, 1, 2, Options("epic"));
    CreationBatchRegistry::Batch trackerJ;
    trackerJ.initiatorGuid = 96;
    trackerJ.desiredSize = 2;
    trackerJ.preexistingMembers = 1;
    trackerJ.creditedDependencies = planJ.creditedDependencies;
    trackerJ.replacementBudget = 2;
    trackerJ.memberGear = "epic";
    uint64_t const tokenJ = joins.Begin(std::move(trackerJ));
    joins.ReevaluateCredits(5, [](uint32_t, uint32_t guid) { return guid == 9501; });
    LIVING_CHECK(joins.Find(tokenJ)->preexistingMembers == 2); // confirmed
    LIVING_CHECK(joins.Find(tokenJ)->members.empty());          // no replacement
    LIVING_CHECK(joins.Poll(tokenJ, false).status == BatchPollStatus::Complete);
}

LIVING_TEST(group_batch_prune_request_reevaluate_creates_one_replacement)
{
    // The production ordering that used to double-replace one lost slot:
    // the credited predecessor is PRUNED, a repeated request arrives BEFORE
    // the next credit re-evaluation, and only then does the re-evaluation
    // run. Source-gone credits now count as outstanding, so the repeated
    // request sees the deficit covered and enqueues nothing; the
    // re-evaluation then produces exactly ONE replacement.
    CreationBatchRegistry registry;
    auto nobodyJoined = [](uint32_t) { return false; };
    auto nobodyJoinedFor = [](uint32_t, uint32_t) { return false; };

    CreationBatchRegistry::Batch a;
    a.initiatorGuid = 97;
    a.desiredSize = 2;
    a.preexistingMembers = 1;
    a.memberGear = "epic";
    a.members.push_back(PendingMember(61, 1, 1));
    uint64_t const tokenA = registry.Begin(std::move(a));
    registry.OnCreationTerminal(MakeCompletion(61, CreationPollStatus::Created, 9601), 1);

    GroupBatchRequestPlan const plan1 = PlanGroupBatchRequest(registry, 97, nobodyJoined, 1, 2, Options("epic"));
    CreationBatchRegistry::Batch tracker;
    tracker.initiatorGuid = 97;
    tracker.desiredSize = 2;
    tracker.preexistingMembers = 1;
    tracker.creditedDependencies = plan1.creditedDependencies;
    tracker.replacementBudget = 2;
    tracker.memberGear = "epic";
    uint64_t const trackerToken = registry.Begin(std::move(tracker));

    // Predecessor expires; the repeated request lands BEFORE re-evaluation.
    registry.PruneExpired(10);
    registry.PruneExpired(10 + CreationBatchRegistry::kRetentionPumps);
    LIVING_CHECK(registry.Find(tokenA) == nullptr);

    // The source-gone credit still counts as one outstanding slot, so the
    // repeated request is COVERED (1 live + 1 owed = 2) - it must not
    // recreate the deficit that the coming re-evaluation will also replace.
    LIVING_CHECK(registry.OutstandingSlotsForInitiator(97, nobodyJoined) == 1);
    GroupBatchRequestPlan const plan2 = PlanGroupBatchRequest(registry, 97, nobodyJoined, 1, 2, Options("epic"));
    LIVING_CHECK(plan2.action == GroupBatchRequestAction::ExtendExisting);
    LIVING_CHECK(plan2.existingToken == trackerToken);
    LIVING_CHECK(plan2.effectiveMembers >= 2); // covered: the handler enqueues nothing

    // Re-evaluation: exactly one replacement for the one lost slot.
    registry.ReevaluateCredits(20000, nobodyJoinedFor);
    uint32_t replacements = 0;
    for (living::CreationBatchMember const& member : registry.Find(trackerToken)->members)
        if (member.state == BatchMemberState::AwaitingAttempt)
            ++replacements;
    LIVING_CHECK(replacements == 1);
    LIVING_CHECK(registry.Find(trackerToken)->creditedDependencies.empty());
    // And afterwards the deficit is carried by the concrete member, still 1.
    LIVING_CHECK(registry.OutstandingSlotsForInitiator(97, nobodyJoined) == 1);
}
