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
