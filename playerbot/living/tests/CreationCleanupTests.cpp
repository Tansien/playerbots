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
// adoption flows through: absence must be execution-confirmed by an
// identity-aware, generation-bound readback before any metadata clear or
// account cleanup; still-present/failed/lost readbacks retry bounded, a
// reused guid clears only the intent, and exhaustion fails closed (rows kept
// for a restart to retry).

namespace
{
    struct DeletionSpyOps
    {
        std::vector<uint32_t> deletes;
        std::vector<std::pair<uint32_t, uint32_t>> verifyRequests; // (guid, generation)
        std::vector<uint32_t> metadataClears;
        std::vector<uint32_t> intentClears;
        std::vector<uint32_t> revokedLogins;
        std::vector<std::pair<uint32_t, uint32_t>> confirmed;
        std::vector<uint32_t> quarantined;

        bool verifyEnqueueSucceeds = true;
        bool clearSucceeds = true;
        bool intentClearSucceeds = true;

        CharacterDeletionOps Make()
        {
            CharacterDeletionOps ops;
            ops.deleteCharacter = [this](uint32_t guid, uint32_t) { deletes.push_back(guid); };
            ops.requestAbsenceVerify = [this](uint32_t guid, uint32_t generation)
            {
                verifyRequests.push_back({ guid, generation });
                return verifyEnqueueSucceeds;
            };
            ops.clearMetadata = [this](uint32_t guid) { metadataClears.push_back(guid); return clearSucceeds; };
            ops.clearIntent = [this](uint32_t guid) { intentClears.push_back(guid); return intentClearSucceeds; };
            ops.revokeLogin = [this](uint32_t guid) { revokedLogins.push_back(guid); };
            ops.onConfirmedDeleted = [this](uint32_t guid, uint32_t accountId) { confirmed.push_back({ guid, accountId }); };
            ops.onQuarantined = [this](uint32_t guid) { quarantined.push_back(guid); };
            return ops;
        }

        uint32_t LastGeneration() const { return verifyRequests.empty() ? 0 : verifyRequests.back().second; }
    };
}

LIVING_TEST(durable_deletion_confirmed_absence_clears_metadata_then_completes)
{
    // The happy path in execution order: adopt (login revoked at once, the
    // auto-add cleanup), readback confirms absence, ONLY THEN metadata clears
    // and the account-cleanup hook fires with the truthful durable count.
    DurableCharacterDeletions deletions;
    DeletionSpyOps spy;

    deletions.Adopt(7001, 42, "Botname", spy.Make());
    LIVING_CHECK(spy.revokedLogins == std::vector<uint32_t>{ 7001 }); // immediately login-ineligible
    LIVING_CHECK(deletions.Owns(7001));
    LIVING_CHECK(deletions.ExpectedNameFor(7001) == "Botname");

    deletions.Pump(spy.Make());
    LIVING_CHECK(spy.verifyRequests.size() == 1);
    LIVING_CHECK(spy.metadataClears.empty()); // nothing cleared before confirmation

    deletions.OnAbsenceVerify(7001, spy.LastGeneration(), RowVerifyOutcome::Absent);
    deletions.Pump(spy.Make()); // consumes the outcome
    deletions.Pump(spy.Make()); // clears + confirms
    LIVING_CHECK(spy.metadataClears == std::vector<uint32_t>{ 7001 });
    LIVING_CHECK(spy.confirmed.size() == 1 && spy.confirmed[0].first == 7001 && spy.confirmed[0].second == 42);
    LIVING_CHECK(!deletions.Owns(7001));
    LIVING_CHECK(spy.quarantined.empty() && spy.intentClears.empty());

    // Duplicate adoption merges: re-adopting an owned guid re-revokes login
    // but never duplicates the record.
    deletions.Adopt(7002, 42, "Other", spy.Make());
    deletions.Adopt(7002, 42, "Other", spy.Make());
    LIVING_CHECK(deletions.RecordCount() == 1);
}

LIVING_TEST(durable_deletion_still_present_reissues_then_fails_closed)
{
    // Deletion failure: every readback finds the row STILL PRESENT under the
    // recorded identity. The idempotent deletion is re-issued per attempt;
    // exhaustion quarantines WITHOUT clearing metadata - the durable intent
    // survives for a restart to retry.
    DurableCharacterDeletions deletions;
    DeletionSpyOps spy;

    deletions.Adopt(7010, 42, "Stuck", spy.Make());
    for (uint32_t i = 0; i < DurableCharacterDeletions::kMaxVerifyAttempts + 2; ++i)
    {
        deletions.Pump(spy.Make());
        deletions.OnAbsenceVerify(7010, spy.LastGeneration(), RowVerifyOutcome::Verified);
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

    deletions.Adopt(7020, 42, "Outage", spy.Make());
    for (uint32_t i = 0; i < DurableCharacterDeletions::kMaxVerifyAttempts + 2; ++i)
    {
        deletions.Pump(spy.Make());
        deletions.OnAbsenceVerify(7020, spy.LastGeneration(), RowVerifyOutcome::QueryFailed);
    }
    deletions.Pump(spy.Make());
    LIVING_CHECK(deletions.IsQuarantined(7020));
    LIVING_CHECK(spy.metadataClears.empty() && spy.confirmed.empty());
    LIVING_CHECK(spy.deletes.empty()); // a failed QUERY never re-issues the deletion

    DurableCharacterDeletions enqueueFailures;
    DeletionSpyOps enqueueSpy;
    enqueueSpy.verifyEnqueueSucceeds = false;
    enqueueFailures.Adopt(7021, 42, "NoQueue", enqueueSpy.Make());
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

    deletions.Adopt(7030, 42, "ClearFail", spy.Make());
    deletions.Pump(spy.Make());
    deletions.OnAbsenceVerify(7030, spy.LastGeneration(), RowVerifyOutcome::Absent);
    deletions.Pump(spy.Make());

    size_t const verifyRequestsAfterConfirm = spy.verifyRequests.size();
    for (uint32_t i = 0; i < DurableCharacterDeletions::kMaxClearAttempts + 2; ++i)
        deletions.Pump(spy.Make());

    LIVING_CHECK(deletions.IsQuarantined(7030));
    LIVING_CHECK(spy.confirmed.empty());
    LIVING_CHECK(spy.verifyRequests.size() == verifyRequestsAfterConfirm); // clear-only retries
    LIVING_CHECK(spy.metadataClears.size() == DurableCharacterDeletions::kMaxClearAttempts);
}

LIVING_TEST(durable_deletion_guid_reuse_clears_only_the_intent)
{
    // GUID reuse: the readback finds the guid occupied by a DIFFERENT name -
    // the original deletion verifiably succeeded. Only the deletion-intent
    // row is cleared; the metadata rows (which may belong to the new
    // identity) and the character itself are never touched, and the deletion
    // is never re-issued at the reused guid.
    DurableCharacterDeletions deletions;
    DeletionSpyOps spy;

    deletions.Adopt(7040, 42, "OldBot", spy.Make());
    deletions.Pump(spy.Make());
    deletions.OnAbsenceVerify(7040, spy.LastGeneration(), RowVerifyOutcome::IdentityMismatch);
    deletions.Pump(spy.Make()); // consume the outcome
    deletions.Pump(spy.Make()); // intent-only clear + completion
    LIVING_CHECK(spy.intentClears == std::vector<uint32_t>{ 7040 });
    LIVING_CHECK(spy.metadataClears.empty()); // the new identity's rows survive
    LIVING_CHECK(spy.deletes.empty());        // never delete the reused guid
    LIVING_CHECK(spy.confirmed.size() == 1);
    LIVING_CHECK(!deletions.Owns(7040));
}

LIVING_TEST(durable_deletion_lost_callback_watchdog_rearms_and_ignores_stale)
{
    // Enqueue succeeds but the callback never arrives: the deadline watchdog
    // abandons the wait (one bounded attempt), re-arms under a NEW
    // generation, and the stale callback that straggles in afterwards cannot
    // settle the owner - only the current generation may.
    DurableCharacterDeletions deletions;
    DeletionSpyOps spy;

    deletions.Adopt(7050, 42, "Lost", spy.Make());
    deletions.Pump(spy.Make());
    LIVING_CHECK(spy.verifyRequests.size() == 1);
    uint32_t const staleGeneration = spy.LastGeneration();

    for (uint32_t i = 0; i < DurableCharacterDeletions::kMaxAwaitingPumps; ++i)
        deletions.Pump(spy.Make());
    LIVING_CHECK(spy.verifyRequests.size() == 1); // still waiting within the deadline

    deletions.Pump(spy.Make()); // timeout: watchdog re-arms
    LIVING_CHECK(spy.verifyRequests.size() == 2);
    LIVING_CHECK(spy.LastGeneration() != staleGeneration);

    // The stale callback arrives now: ignored entirely (no absence claimed).
    deletions.OnAbsenceVerify(7050, staleGeneration, RowVerifyOutcome::Absent);
    deletions.Pump(spy.Make());
    LIVING_CHECK(spy.metadataClears.empty());

    // The current generation settles the owner.
    deletions.OnAbsenceVerify(7050, spy.LastGeneration(), RowVerifyOutcome::Absent);
    deletions.Pump(spy.Make());
    deletions.Pump(spy.Make());
    LIVING_CHECK(spy.metadataClears == std::vector<uint32_t>{ 7050 });
    LIVING_CHECK(!deletions.Owns(7050));
}

LIVING_TEST(durable_deletion_capacity_pressure_revokes_but_fails_closed)
{
    // Over-capacity adoptions still revoke login eligibility immediately, but
    // the record is dropped fail-closed (rows stay; the next restart's intent
    // scan retries) and disclosed through onQuarantined.
    DurableCharacterDeletions deletions;
    DeletionSpyOps spy;

    for (uint32_t i = 0; i < DurableCharacterDeletions::kMaxRecords; ++i)
        deletions.Adopt(10000 + i, 1, "Bulk", spy.Make());
    LIVING_CHECK(deletions.RecordCount() == DurableCharacterDeletions::kMaxRecords);

    deletions.Adopt(99999, 1, "Overflow", spy.Make());
    LIVING_CHECK(spy.revokedLogins.back() == 99999);          // revoked regardless
    LIVING_CHECK(deletions.RecordCount() == DurableCharacterDeletions::kMaxRecords);
    LIVING_CHECK(!deletions.Owns(99999));
    LIVING_CHECK(spy.quarantined.back() == 99999);            // disclosed
}

LIVING_TEST(durable_deletion_restart_recovery_follows_the_intent_plan)
{
    // Crash before the queued deletion executed: the intent row survives, and
    // the startup scan's recovery plan re-runs the full deletion for a
    // character still present under the RECORDED name, adopts for
    // confirmation when absent, and adopts (identity mismatch path) when the
    // guid was reused.
    LIVING_CHECK(PlanDeletionIntentRecovery(true, "OldBot", "OldBot")
        == DeletionIntentRecovery::RequeueDeletion);
    LIVING_CHECK(PlanDeletionIntentRecovery(false, "", "OldBot")
        == DeletionIntentRecovery::AdoptForConfirmation);
    LIVING_CHECK(PlanDeletionIntentRecovery(true, "NewOwner", "OldBot")
        == DeletionIntentRecovery::AdoptForConfirmation);

    // The re-adopted owner in a FRESH process completes normally.
    DurableCharacterDeletions restarted;
    DeletionSpyOps spy;
    restarted.Adopt(7060, 42, "OldBot", spy.Make());
    LIVING_CHECK(spy.revokedLogins == std::vector<uint32_t>{ 7060 });
    restarted.Pump(spy.Make());
    restarted.OnAbsenceVerify(7060, spy.LastGeneration(), RowVerifyOutcome::Absent);
    restarted.Pump(spy.Make());
    restarted.Pump(spy.Make());
    LIVING_CHECK(spy.confirmed.size() == 1);
    LIVING_CHECK(!restarted.Owns(7060));
}

LIVING_TEST(post_create_owner_reconciliation_keeps_owners_on_failed_scans)
{
    // The transient post-create owner set is rebuilt only from a SUCCESSFUL
    // durable scan; a failed scan (database unavailable at startup/reload)
    // keeps every existing owner, so unknown state never orphans unsettled
    // work - at any stage (before effect, awaiting persistence, before clear,
    // the marker rows all still exist and the scan finds them).
    std::map<uint32_t, uint32_t> existing{ { 501, 1 }, { 502, 2 } };

    // Successful scan: authoritative replacement (bot 501 settled meanwhile,
    // bot 503 was created by another session).
    std::map<uint32_t, uint32_t> scanned{ { 502, 2 }, { 503, 3 } };
    LIVING_CHECK(ReconcilePostCreateOwners(existing, true, scanned) == scanned);

    // Failed scan: every existing owner is kept, nothing is invented.
    LIVING_CHECK(ReconcilePostCreateOwners(existing, false, {}) == existing);

    // Successful EMPTY scan: everything settled - the set clears.
    LIVING_CHECK(ReconcilePostCreateOwners(existing, true, {}).empty());
}
