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
    ops.deleteCharacter = [&](uint32_t guid) { deleted.push_back(guid); return true; };

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
    ops.deleteCharacter = [&](uint32_t guid) { deleted.push_back(guid); return true; };

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
    ops.deleteCharacter = [&](uint32_t guid) { deleted.push_back(guid); return true; };

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
        std::vector<uint32_t> revokedLogins;
        std::vector<std::pair<uint32_t, uint32_t>> confirmed;
        std::vector<uint32_t> quarantined;

        bool verifyEnqueueSucceeds = true;
        bool clearSucceeds = true;

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

    deletions.Adopt(7001, 42, "Botname", true, spy.Make());
    LIVING_CHECK(spy.revokedLogins == std::vector<uint32_t>{ 7001 }); // immediately login-ineligible
    LIVING_CHECK(deletions.Owns(7001));
    std::string expectedName;
    uint32_t expectedAccount = 0;
    LIVING_CHECK(deletions.TryGetExpectedIdentity(7001, expectedName, expectedAccount));
    LIVING_CHECK(expectedName == "Botname" && expectedAccount == 42);

    deletions.Pump(spy.Make());
    LIVING_CHECK(spy.verifyRequests.size() == 1);
    LIVING_CHECK(spy.metadataClears.empty()); // nothing cleared before confirmation

    deletions.OnAbsenceVerify(7001, spy.LastGeneration(), RowVerifyOutcome::Absent);
    deletions.Pump(spy.Make()); // consumes the outcome
    deletions.Pump(spy.Make()); // clears + confirms
    LIVING_CHECK(spy.metadataClears == std::vector<uint32_t>{ 7001 });
    LIVING_CHECK(spy.confirmed.size() == 1 && spy.confirmed[0].first == 7001 && spy.confirmed[0].second == 42);
    LIVING_CHECK(!deletions.Owns(7001));
    LIVING_CHECK(spy.quarantined.empty());

    // Duplicate adoption merges: re-adopting an owned guid re-revokes login
    // but never duplicates the record.
    deletions.Adopt(7002, 42, "Other", true, spy.Make());
    deletions.Adopt(7002, 42, "Other", true, spy.Make());
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

    deletions.Adopt(7010, 42, "Stuck", true, spy.Make());
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

    deletions.Adopt(7020, 42, "Outage", true, spy.Make());
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
    enqueueFailures.Adopt(7021, 42, "NoQueue", true, enqueueSpy.Make());
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

    deletions.Adopt(7030, 42, "ClearFail", true, spy.Make());
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

LIVING_TEST(durable_deletion_unproven_identity_fails_closed)
{
    // The readback finds the guid occupied but the identity does not match
    // the recorded (name, account) - rename and reuse are indistinguishable
    // here, so the owner FAILS CLOSED: it quarantines with everything
    // retained. Nothing is deleted at the unproven guid, no metadata or
    // intent is cleared, and nothing reports success.
    DurableCharacterDeletions deletions;
    DeletionSpyOps spy;

    deletions.Adopt(7040, 42, "OldBot", true, spy.Make());
    deletions.Pump(spy.Make());
    deletions.OnAbsenceVerify(7040, spy.LastGeneration(), RowVerifyOutcome::IdentityMismatch);
    deletions.Pump(spy.Make()); // consume the outcome -> quarantine
    deletions.Pump(spy.Make());
    LIVING_CHECK(deletions.IsQuarantined(7040));
    LIVING_CHECK(spy.quarantined == std::vector<uint32_t>{ 7040 });
    LIVING_CHECK(spy.metadataClears.empty()); // rows retained fail-closed
    LIVING_CHECK(spy.deletes.empty());        // never delete an unproven guid
    LIVING_CHECK(spy.confirmed.empty());
    LIVING_CHECK(deletions.Owns(7040));       // ownership retained (quarantined)
}

LIVING_TEST(durable_deletion_lost_callback_watchdog_rearms_and_ignores_stale)
{
    // Enqueue succeeds but the callback never arrives: the deadline watchdog
    // abandons the wait (one bounded attempt), re-arms under a NEW
    // generation, and the stale callback that straggles in afterwards cannot
    // settle the owner - only the current generation may.
    DurableCharacterDeletions deletions;
    DeletionSpyOps spy;

    deletions.Adopt(7050, 42, "Lost", true, spy.Make());
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

LIVING_TEST(durable_deletion_capacity_overflow_is_retained_and_promoted)
{
    // Over-capacity adoptions are FAIL-CLOSED: login eligibility is revoked,
    // the guid is DEFERRED (not dropped) into a bounded overflow set that
    // still counts as owned - so IsDeletionPending keeps blocking logins and
    // metadata mutation - and it is promoted into an active record as
    // capacity frees, eventually confirming like any other deletion.
    DurableCharacterDeletions deletions;
    DeletionSpyOps spy;

    for (uint32_t i = 0; i < DurableCharacterDeletions::kMaxRecords; ++i)
        deletions.Adopt(10000 + i, 1, "Bulk", true, spy.Make());
    LIVING_CHECK(deletions.RecordCount() == DurableCharacterDeletions::kMaxRecords);

    deletions.Adopt(99999, 1, "Overflow", true, spy.Make());
    LIVING_CHECK(spy.revokedLogins.back() == 99999); // revoked regardless
    LIVING_CHECK(deletions.RecordCount() == DurableCharacterDeletions::kMaxRecords);
    LIVING_CHECK(deletions.Owns(99999));             // STILL OWNED: login stays blocked
    LIVING_CHECK(spy.quarantined.back() == 99999);   // deferral disclosed

    // One active record completes -> the deferred guid is promoted and runs
    // the normal confirmation flow to completion.
    deletions.Pump(spy.Make());
    deletions.OnAbsenceVerify(10000, deletions.CurrentGeneration(10000), RowVerifyOutcome::Absent);
    deletions.Pump(spy.Make()); // consume outcome
    deletions.Pump(spy.Make()); // clear + confirm 10000; capacity frees; 99999 promoted
    LIVING_CHECK(!deletions.Owns(10000));
    LIVING_CHECK(deletions.Owns(99999));

    deletions.Pump(spy.Make()); // promoted record requests its verify
    deletions.OnAbsenceVerify(99999, deletions.CurrentGeneration(99999), RowVerifyOutcome::Absent);
    deletions.Pump(spy.Make());
    deletions.Pump(spy.Make());
    LIVING_CHECK(!deletions.Owns(99999)); // eventually adopted AND completed
    bool confirmed99999 = false;
    for (auto const& entry : spy.confirmed)
        confirmed99999 = confirmed99999 || entry.first == 99999;
    LIVING_CHECK(confirmed99999);
}

LIVING_TEST(reload_adopt_quarantined_never_downgrades_a_proven_deletion)
{
    // A config reload re-scans durable intents while an in-process deletion
    // (identity proven by THIS process's typed preflight) is mid-
    // confirmation and its character still present: the reload-style
    // AdoptQuarantined must be a no-op - downgrading would strand a valid
    // deletion in quarantine forever.
    DurableCharacterDeletions deletions;
    DeletionSpyOps spy;

    deletions.Adopt(7100, 42, "Live", true, spy.Make());
    deletions.Pump(spy.Make()); // verify requested; deletion still executing

    deletions.AdoptQuarantined(7100, 42, "Live", spy.Make()); // reload re-scan
    LIVING_CHECK(!deletions.IsQuarantined(7100));             // NOT downgraded

    // The original owner completes normally.
    deletions.OnAbsenceVerify(7100, deletions.CurrentGeneration(7100), RowVerifyOutcome::Absent);
    deletions.Pump(spy.Make());
    deletions.Pump(spy.Make());
    LIVING_CHECK(!deletions.Owns(7100));
    LIVING_CHECK(spy.confirmed.size() == 1 && spy.confirmed[0].first == 7100);
}

LIVING_TEST(durable_deletion_restart_recovery_follows_the_intent_plan)
{
    // Crash before the queued deletion executed: the intent row survives. A
    // PRESENT character is NEVER automatically re-deleted after a restart -
    // (guid, name, account) is not immutable (same-guid reuse under the same
    // name and account is representable in the schema, and no creation nonce
    // exists), so present rows quarantine for manual resolution while absent
    // rows adopt for confirmation/cleanup with the RECORDED account.
    LIVING_CHECK(PlanDeletionIntentRecovery(true) == DeletionIntentRecovery::QuarantinePresent);
    LIVING_CHECK(PlanDeletionIntentRecovery(false) == DeletionIntentRecovery::AdoptForCleanup);

    // The durable intent payload round-trips name and ORIGINAL account (the
    // account makes empty-account cleanup possible when the row is already
    // absent); legacy name-only payloads decode with account 0.
    std::string name;
    uint32_t account = 0;
    DecodeDeletionIntent(EncodeDeletionIntent("OldBot", 42), name, account);
    LIVING_CHECK(name == "OldBot" && account == 42);
    DecodeDeletionIntent(EncodeDeletionIntent("", 7), name, account);
    LIVING_CHECK(name.empty() && account == 7);
    DecodeDeletionIntent("LegacyName", name, account);
    LIVING_CHECK(name == "LegacyName" && account == 0);

    // The re-adopted owner in a FRESH process completes normally.
    DurableCharacterDeletions restarted;
    DeletionSpyOps spy;
    restarted.Adopt(7060, 42, "OldBot", true, spy.Make());
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

LIVING_TEST(deletion_preflight_classifies_identity_before_anything_destructive)
{
    // One typed query decides the whole deletion boundary. A failed lookup
    // (null count) or an incomplete present row (empty name / account 0) is
    // UNKNOWN: no intent may be written and no deletion queued.
    LIVING_CHECK(ClassifyDeletionPreflight(std::nullopt, "", 0) == DeletionPreflight::Unknown);
    LIVING_CHECK(ClassifyDeletionPreflight(std::nullopt, "Bot", 42) == DeletionPreflight::Unknown);
    LIVING_CHECK(ClassifyDeletionPreflight(uint64_t(0), "", 0) == DeletionPreflight::ConfirmedAbsent);
    LIVING_CHECK(ClassifyDeletionPreflight(uint64_t(1), "Bot", 42) == DeletionPreflight::VerifiedPresent);
    LIVING_CHECK(ClassifyDeletionPreflight(uint64_t(1), "", 42) == DeletionPreflight::Unknown);  // no name
    LIVING_CHECK(ClassifyDeletionPreflight(uint64_t(1), "Bot", 0) == DeletionPreflight::Unknown); // account 0
}

LIVING_TEST(durable_deletion_recovery_present_rows_quarantine_not_redelete)
{
    // Restart boundary: a recovery adoption can never prove the present row
    // was not reused (identityProvenInProcess = false), so ANY present
    // readback - even a full (name, account) match - fails closed instead of
    // re-issuing the deletion at what may be a replacement character.
    DurableCharacterDeletions deletions;
    DeletionSpyOps spy;

    deletions.Adopt(7070, 42, "SameName", false, spy.Make());
    deletions.Pump(spy.Make());
    deletions.OnAbsenceVerify(7070, spy.LastGeneration(), RowVerifyOutcome::Verified); // present, identity matches
    deletions.Pump(spy.Make());
    LIVING_CHECK(deletions.IsQuarantined(7070));
    LIVING_CHECK(spy.deletes.empty());        // NEVER re-deleted after restart
    LIVING_CHECK(spy.metadataClears.empty());
    LIVING_CHECK(deletions.Owns(7070));       // login stays blocked

    // The pre-quarantined recovery adoption for scan-discovered present rows.
    DurableCharacterDeletions preQuarantined;
    DeletionSpyOps preSpy;
    preQuarantined.AdoptQuarantined(7071, 42, "Reused", preSpy.Make());
    LIVING_CHECK(preQuarantined.IsQuarantined(7071));
    LIVING_CHECK(preSpy.revokedLogins == std::vector<uint32_t>{ 7071 });
    LIVING_CHECK(preSpy.quarantined == std::vector<uint32_t>{ 7071 });
    preQuarantined.Pump(preSpy.Make());
    LIVING_CHECK(preSpy.verifyRequests.empty()); // no work: manual resolution only

    // A stale callback from the prior identity's generation is ignored.
    preQuarantined.OnAbsenceVerify(7071, 1, RowVerifyOutcome::Absent);
    preQuarantined.Pump(preSpy.Make());
    LIVING_CHECK(preSpy.metadataClears.empty());
    LIVING_CHECK(preQuarantined.Owns(7071));

    // In-process adoptions (identity proven by the typed preflight THIS
    // process) still re-issue the idempotent deletion on a present readback.
    DurableCharacterDeletions inProcess;
    DeletionSpyOps inSpy;
    inProcess.Adopt(7072, 42, "Live", true, inSpy.Make());
    inProcess.Pump(inSpy.Make());
    inProcess.OnAbsenceVerify(7072, inSpy.LastGeneration(), RowVerifyOutcome::Verified);
    inProcess.Pump(inSpy.Make());
    LIVING_CHECK(inSpy.deletes == std::vector<uint32_t>{ 7072 });
    LIVING_CHECK(!inProcess.IsQuarantined(7072));
}

LIVING_TEST(abandoned_cleanup_retains_tokens_until_deletion_ownership_secured)
{
    // Fail-closed DeleteBot can refuse (intent write failure): the cleanup
    // owner must keep its token and retry - acknowledging would orphan the
    // temporary character with no owner anywhere.
    CreationFinalizer finalizer;
    CreationBatchRegistry registry;
    AbandonedCreationCleanup cleanup;
    std::vector<uint32_t> deleted;
    bool ownershipSecured = false;

    CreationCleanupOps ops;
    ops.pollSingle = [&](uint64_t t, bool ack) { return finalizer.Poll(t, ack); };
    ops.pollBatch = [&](uint64_t t, bool ack) { return registry.Poll(t, ack); };
    ops.deleteCharacter = [&](uint32_t guid) { deleted.push_back(guid); return ownershipSecured; };

    // --- single token
    uint64_t const token = finalizer.Begin(MakeRecord(721, 1, "Temp"), HappyOps());
    cleanup.AdoptSingle(token);
    finalizer.OnCallbackResult(721, RowVerifyOutcome::Verified, CreationCallbackKind::Verify);
    finalizer.Pump(HappyOps());

    cleanup.Pump(ops); // deletion refused: token retained
    LIVING_CHECK(deleted == std::vector<uint32_t>{ 721 });
    LIVING_CHECK(cleanup.PendingSingles() == 1);
    LIVING_CHECK(finalizer.Poll(token, false).status == CreationPollStatus::Created); // NOT acknowledged

    ownershipSecured = true;
    cleanup.Pump(ops); // retry succeeds: token acknowledged and dropped
    LIVING_CHECK(cleanup.PendingSingles() == 0);
    LIVING_CHECK(finalizer.Poll(token, false).status == CreationPollStatus::Unknown);

    // --- batch with PARTIAL success: the secured member is never re-processed
    CreationBatchRegistry::Batch batch;
    batch.initiatorGuid = 9;
    batch.desiredSize = 2;
    batch.members = { PendingMember(31, 1, 1), PendingMember(32, 2, 2) };
    uint64_t const batchToken = registry.Begin(std::move(batch));
    cleanup.AdoptBatch(batchToken);
    registry.OnCreationTerminal(MakeCompletion(31, CreationPollStatus::Created, 731), 1);
    registry.OnCreationTerminal(MakeCompletion(32, CreationPollStatus::Created, 732), 2);

    deleted.clear();
    ownershipSecured = true;
    bool refuse732 = true;
    ops.deleteCharacter = [&](uint32_t guid)
    {
        deleted.push_back(guid);
        return !(guid == 732 && refuse732);
    };

    cleanup.Pump(ops); // 731 secured, 732 refused -> batch token retained
    LIVING_CHECK(cleanup.PendingBatches() == 1);
    LIVING_CHECK(registry.Poll(batchToken, false).status == BatchPollStatus::Complete); // not acknowledged

    deleted.clear();
    refuse732 = false;
    cleanup.Pump(ops); // retry processes ONLY the unsecured member
    LIVING_CHECK(deleted == std::vector<uint32_t>{ 732 }); // 731 never re-deleted
    LIVING_CHECK(cleanup.PendingBatches() == 0);
    LIVING_CHECK(registry.Poll(batchToken, false).status == BatchPollStatus::Unknown); // acknowledged now
}
