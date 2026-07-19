#include "LivingTest.h"

#include "../util/LivingCreationBatch.h"
#include "../util/LivingCreationCleanup.h"
#include "../util/LivingCreationFinalizer.h"

#include <string>
#include <vector>

using namespace living;

// AbandonedCreationCleanup is the deferred owner TestContext::Reset transfers
// still-pending creation tokens to. These tests drive it against a REAL
// CreationFinalizer / CreationBatchRegistry so the reset-during-creation
// interleavings are exercised end to end, not modeled.

namespace
{
    CreationFinalizer::Record MakeRecord(uint32_t guid, uint32_t accountId, std::string name)
    {
        CreationFinalizer::Record record;
        record.guid = guid;
        record.accountId = accountId;
        record.name = std::move(name);
        return record;
    }

    // Finalizer ops whose verify+metadata succeed, so a record reaches Created.
    CreationFinalizerOps HappyOps()
    {
        CreationFinalizerOps ops;
        ops.requestVerify = [](uint32_t) { return true; };
        ops.requestCleanupVerify = [](uint32_t) { return true; };
        ops.writeMetadata = [](uint32_t) { return true; };
        ops.deleteEventRows = [](uint32_t) { return true; };
        ops.deleteCharacter = [](uint32_t, uint32_t) {};
        ops.onCreated = [](uint32_t, uint32_t, std::string const&, bool) {};
        return ops;
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

    CreationCompletion MakeCompletion(uint64_t token, CreationPollStatus status, uint32_t guid)
    {
        CreationCompletion completion;
        completion.token = token;
        completion.guid = guid;
        completion.status = status;
        completion.name = "Member" + std::to_string(token);
        completion.message = "test outcome";
        return completion;
    }
}

LIVING_TEST(cleanup_single_reset_before_finalize_deletes_exactly_once)
{
    // Reset happens while a single creation is still Pending. The owner adopts
    // the token, keeps it while pending, and deletes the finalized temporary
    // character exactly once when it later completes - the leak the reset caused.
    CreationFinalizer finalizer;
    CreationBatchRegistry registry;
    AbandonedCreationCleanup cleanup;
    std::vector<uint32_t> deleted;

    CreationCleanupOps ops;
    ops.pollSingle = [&](uint64_t t, bool ack) { return finalizer.Poll(t, ack); };
    ops.pollBatch = [&](uint64_t t, bool ack) { return registry.Poll(t, ack); };
    ops.deleteCharacter = [&](uint32_t guid) { deleted.push_back(guid); };

    uint64_t const token = finalizer.Begin(MakeRecord(701, 1, "Temp"), HappyOps());
    cleanup.AdoptSingle(token);

    // Still Pending: the owner keeps it and deletes nothing.
    cleanup.Pump(ops);
    LIVING_CHECK(deleted.empty());
    LIVING_CHECK(cleanup.PendingSingles() == 1);

    // The creation finalizes as Created with nobody watching.
    finalizer.OnCallbackResult(701, RowVerifyOutcome::Verified, CreationCallbackKind::Verify);
    finalizer.Pump(HappyOps());

    // The owner deletes the finalized character exactly once and releases it.
    cleanup.Pump(ops);
    LIVING_CHECK(deleted.size() == 1 && deleted[0] == 701);
    LIVING_CHECK(cleanup.PendingSingles() == 0);

    // Idempotent: a later pump does not delete again.
    cleanup.Pump(ops);
    LIVING_CHECK(deleted.size() == 1);
}

LIVING_TEST(cleanup_partial_batch_never_deletes_under_a_live_batch)
{
    // Reset happens after only PART of a group batch has finalized. The owner
    // must NOT delete the already-finalized GUID while the batch is still live,
    // then delete every finalized GUID exactly once once the batch completes.
    CreationFinalizer finalizer;
    CreationBatchRegistry registry;
    AbandonedCreationCleanup cleanup;
    std::vector<uint32_t> deleted;

    CreationCleanupOps ops;
    ops.pollSingle = [&](uint64_t t, bool ack) { return finalizer.Poll(t, ack); };
    ops.pollBatch = [&](uint64_t t, bool ack) { return registry.Poll(t, ack); };
    ops.deleteCharacter = [&](uint32_t guid) { deleted.push_back(guid); };

    CreationBatchRegistry::Batch batch;
    batch.initiatorGuid = 60;
    batch.desiredSize = 3;
    batch.preexistingMembers = 1;
    batch.members.push_back(PendingMember(81, 1, 1));
    batch.members.push_back(PendingMember(82, 2, 4));
    uint64_t const token = registry.Begin(std::move(batch));

    // One member finalizes; the other is still outstanding when the reset hits.
    registry.OnCreationTerminal(MakeCompletion(81, CreationPollStatus::Created, 8001), 1);
    cleanup.AdoptBatch(token);

    // Batch still Pending: the finalized 8001 must NOT be deleted out from under
    // the live batch (the exact pre-fix defect).
    cleanup.Pump(ops);
    LIVING_CHECK(deleted.empty());
    LIVING_CHECK(cleanup.PendingBatches() == 1);

    // The remaining member finalizes: the batch is now Complete.
    registry.OnCreationTerminal(MakeCompletion(82, CreationPollStatus::Created, 8002), 2);
    LIVING_CHECK(registry.Poll(token, false).status == BatchPollStatus::Complete);

    // Now both finalized GUIDs are deleted exactly once and the batch is dropped.
    cleanup.Pump(ops);
    LIVING_CHECK(deleted.size() == 2);
    bool has8001 = false, has8002 = false;
    for (uint32_t g : deleted)
    {
        if (g == 8001) has8001 = true;
        if (g == 8002) has8002 = true;
    }
    LIVING_CHECK(has8001 && has8002);
    LIVING_CHECK(cleanup.PendingBatches() == 0);
    LIVING_CHECK(registry.Poll(token, false).status == BatchPollStatus::Unknown); // acknowledged/erased

    // Idempotent.
    cleanup.Pump(ops);
    LIVING_CHECK(deleted.size() == 2);
}

LIVING_TEST(cleanup_quarantined_single_is_dropped_without_delete)
{
    // A quarantined record exposes no GUID and its durable state must not be
    // touched: the owner drops it without deleting anything.
    CreationFinalizer finalizer;
    CreationBatchRegistry registry;
    AbandonedCreationCleanup cleanup;
    std::vector<uint32_t> deleted;

    CreationCleanupOps ops;
    ops.pollSingle = [&](uint64_t t, bool ack) { return finalizer.Poll(t, ack); };
    ops.pollBatch = [&](uint64_t t, bool ack) { return registry.Poll(t, ack); };
    ops.deleteCharacter = [&](uint32_t guid) { deleted.push_back(guid); };

    uint64_t const token = finalizer.Begin(MakeRecord(703, 1, "Quar"), HappyOps());
    cleanup.AdoptSingle(token);

    finalizer.OnCallbackResult(703, RowVerifyOutcome::IdentityMismatch, CreationCallbackKind::Verify);
    finalizer.Pump(HappyOps());
    LIVING_CHECK(finalizer.Poll(token, false).status == CreationPollStatus::Quarantined);

    cleanup.Pump(ops);
    LIVING_CHECK(deleted.empty());
    LIVING_CHECK(cleanup.PendingSingles() == 0);
}

// DurableCharacterDeletions is the per-GUID deletion owner every DeleteBot
// adoption flows through: absence must be execution-confirmed before any
// metadata clear or account cleanup, still-present/failed readbacks retry
// bounded, and exhaustion fails closed (rows kept for a restart to retry).

namespace
{
    struct DeletionSpyOps
    {
        std::vector<uint32_t> deletes;
        std::vector<uint32_t> verifyRequests;
        std::vector<uint32_t> metadataClears;
        std::vector<uint32_t> revokedLogins;
        std::vector<std::pair<uint32_t, uint32_t>> confirmed;
        std::vector<uint32_t> quarantined;

        bool verifyEnqueueSucceeds = true;
        bool clearSucceeds = true;

        CharacterDeletionOps Make()
        {
            CharacterDeletionOps ops;
            ops.deleteCharacter = [this](uint32_t guid, uint32_t) { deletes.push_back(guid); };
            ops.requestAbsenceVerify = [this](uint32_t guid)
            {
                verifyRequests.push_back(guid);
                return verifyEnqueueSucceeds;
            };
            ops.clearMetadata = [this](uint32_t guid) { metadataClears.push_back(guid); return clearSucceeds; };
            ops.revokeLogin = [this](uint32_t guid) { revokedLogins.push_back(guid); };
            ops.onConfirmedDeleted = [this](uint32_t guid, uint32_t accountId) { confirmed.push_back({ guid, accountId }); };
            ops.onQuarantined = [this](uint32_t guid) { quarantined.push_back(guid); };
            return ops;
        }
    };
}

LIVING_TEST(durable_deletion_confirmed_absence_clears_metadata_then_completes)
{
    // The happy path in execution order: adopt (login revoked at once, the
    // auto-add cleanup), readback confirms absence, ONLY THEN metadata clears
    // and the account-cleanup hook fires with the truthful durable count.
    DurableCharacterDeletions deletions;
    DeletionSpyOps spy;

    deletions.Adopt(7001, 42, spy.Make());
    LIVING_CHECK(spy.revokedLogins == std::vector<uint32_t>{ 7001 }); // immediately login-ineligible
    LIVING_CHECK(deletions.Owns(7001));

    deletions.Pump(spy.Make());
    LIVING_CHECK(spy.verifyRequests.size() == 1);
    LIVING_CHECK(spy.metadataClears.empty()); // nothing cleared before confirmation

    deletions.OnAbsenceVerify(7001, RowVerifyOutcome::Absent);
    deletions.Pump(spy.Make()); // consumes the outcome
    deletions.Pump(spy.Make()); // clears + confirms
    LIVING_CHECK(spy.metadataClears == std::vector<uint32_t>{ 7001 });
    LIVING_CHECK(spy.confirmed.size() == 1 && spy.confirmed[0].first == 7001 && spy.confirmed[0].second == 42);
    LIVING_CHECK(!deletions.Owns(7001));
    LIVING_CHECK(spy.quarantined.empty());

    // Duplicate adoption merges: re-adopting an owned guid re-revokes login
    // but never duplicates the record.
    deletions.Adopt(7002, 42, spy.Make());
    deletions.Adopt(7002, 42, spy.Make());
    LIVING_CHECK(deletions.RecordCount() == 1);
}

LIVING_TEST(durable_deletion_still_present_reissues_then_fails_closed)
{
    // Deletion failure: every readback finds the row STILL PRESENT. The
    // idempotent deletion is re-issued per attempt; exhaustion quarantines
    // WITHOUT clearing metadata - the marker survives for a restart to retry.
    DurableCharacterDeletions deletions;
    DeletionSpyOps spy;

    deletions.Adopt(7010, 42, spy.Make());
    for (uint32_t i = 0; i < DurableCharacterDeletions::kMaxVerifyAttempts + 2; ++i)
    {
        deletions.Pump(spy.Make());
        deletions.OnAbsenceVerify(7010, RowVerifyOutcome::Verified);
    }
    deletions.Pump(spy.Make());

    LIVING_CHECK(spy.quarantined == std::vector<uint32_t>{ 7010 });
    LIVING_CHECK(deletions.IsQuarantined(7010));
    LIVING_CHECK(spy.metadataClears.empty());          // fail closed: rows kept
    LIVING_CHECK(spy.confirmed.empty());
    LIVING_CHECK(!spy.deletes.empty());                // the deletion WAS re-issued
    LIVING_CHECK(spy.deletes.size() < DurableCharacterDeletions::kMaxVerifyAttempts); // bounded

    // Quarantined records do no further work.
    size_t const requestsAtQuarantine = spy.verifyRequests.size();
    deletions.Pump(spy.Make());
    LIVING_CHECK(spy.verifyRequests.size() == requestsAtQuarantine);
}

LIVING_TEST(durable_deletion_query_failures_are_bounded_and_fail_closed)
{
    // Absence readbacks keep FAILING (outage): bounded retries, then
    // quarantine with nothing cleared. A failed ENQUEUE consumes the same
    // bounded budget through the mailbox.
    DurableCharacterDeletions deletions;
    DeletionSpyOps spy;

    deletions.Adopt(7020, 42, spy.Make());
    for (uint32_t i = 0; i < DurableCharacterDeletions::kMaxVerifyAttempts + 2; ++i)
    {
        deletions.Pump(spy.Make());
        deletions.OnAbsenceVerify(7020, RowVerifyOutcome::QueryFailed);
    }
    deletions.Pump(spy.Make());
    LIVING_CHECK(deletions.IsQuarantined(7020));
    LIVING_CHECK(spy.metadataClears.empty() && spy.confirmed.empty());
    LIVING_CHECK(spy.deletes.empty()); // a failed QUERY never re-issues the deletion

    DurableCharacterDeletions enqueueFailures;
    DeletionSpyOps enqueueSpy;
    enqueueSpy.verifyEnqueueSucceeds = false;
    enqueueFailures.Adopt(7021, 42, enqueueSpy.Make());
    for (uint32_t i = 0; i < DurableCharacterDeletions::kMaxVerifyAttempts + 2; ++i)
        enqueueFailures.Pump(enqueueSpy.Make());
    LIVING_CHECK(enqueueFailures.IsQuarantined(7021));
    LIVING_CHECK(enqueueSpy.metadataClears.empty());
}

LIVING_TEST(durable_deletion_failed_metadata_clear_retries_then_fails_closed)
{
    // Absence confirmed but the metadata clear keeps failing: only the clear
    // retries (no new readbacks, no re-deletes), bounded, then quarantine.
    DurableCharacterDeletions deletions;
    DeletionSpyOps spy;
    spy.clearSucceeds = false;

    deletions.Adopt(7030, 42, spy.Make());
    deletions.Pump(spy.Make());
    deletions.OnAbsenceVerify(7030, RowVerifyOutcome::Absent);
    deletions.Pump(spy.Make());

    size_t const verifyRequestsAfterConfirm = spy.verifyRequests.size();
    for (uint32_t i = 0; i < DurableCharacterDeletions::kMaxClearAttempts + 2; ++i)
        deletions.Pump(spy.Make());

    LIVING_CHECK(deletions.IsQuarantined(7030));
    LIVING_CHECK(spy.confirmed.empty());
    LIVING_CHECK(spy.verifyRequests.size() == verifyRequestsAfterConfirm); // clear-only retries
    LIVING_CHECK(spy.metadataClears.size() == DurableCharacterDeletions::kMaxClearAttempts);
}

LIVING_TEST(durable_deletion_restart_readopts_and_completes)
{
    // Restart/retry: the first process quarantined (or crashed) without
    // clearing metadata, so the startup sweep re-adopts the same guid in a
    // FRESH owner - which then completes normally once the deletion lands.
    DurableCharacterDeletions firstProcess;
    DeletionSpyOps firstSpy;
    firstProcess.Adopt(7040, 42, firstSpy.Make());
    firstProcess.Pump(firstSpy.Make());
    firstProcess.OnAbsenceVerify(7040, RowVerifyOutcome::Verified); // deletion had not landed
    // ... crash: nothing was cleared, the marker row survives.
    LIVING_CHECK(firstSpy.metadataClears.empty());

    DurableCharacterDeletions restarted;
    DeletionSpyOps restartSpy;
    restarted.Adopt(7040, 42, restartSpy.Make());
    LIVING_CHECK(restartSpy.revokedLogins == std::vector<uint32_t>{ 7040 });
    restarted.Pump(restartSpy.Make());
    restarted.OnAbsenceVerify(7040, RowVerifyOutcome::Absent);
    restarted.Pump(restartSpy.Make());
    restarted.Pump(restartSpy.Make());
    LIVING_CHECK(restartSpy.confirmed.size() == 1);
    LIVING_CHECK(!restarted.Owns(7040));
}
