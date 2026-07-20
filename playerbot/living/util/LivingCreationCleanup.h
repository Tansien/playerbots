#pragma once

#include "LivingCreationBatch.h"     // BatchPollResult / BatchPollStatus
#include "LivingCreationFinalizer.h" // CreationPollResult / CreationPollStatus

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace living
{
    // Deferred cleanup owner for creation tokens abandoned by a test-context
    // reset (TestContext::Reset). The reset used to poll each pending single /
    // group-batch token ONCE and clear it while still pending: a later
    // finalization then had no cleanup owner (a durable temporary character
    // leaked), and for a partial batch the already-finalized GUIDs were deleted
    // while the still-live batch kept tracking them.
    //
    // This owner survives the reset and is pumped from the same creation pump.
    // It reuses the existing finalizer/batch poll surface - it never runs a
    // parallel creation state machine:
    //   - ownership is secured by Adopt* BEFORE any acknowledgement;
    //   - Pump PEEKS each token (acknowledge = false) and keeps owning it while
    //     it is still Pending;
    //   - a finalized temporary character is deleted EXACTLY ONCE (a Created
    //     single, or every finalized GUID of a COMPLETE batch), and only THEN is
    //     the terminal result acknowledged and ownership dropped;
    //   - a batch is never acted on while Pending, so a partial batch's
    //     finalized GUIDs are never deleted out from under the still-live batch;
    //   - quarantined / failed outcomes expose no GUID, so nothing is deleted
    //     for them (the finalizer's "never touch quarantined durable state"
    //     contract is honored).
    struct CreationCleanupOps
    {
        // Poll a single creation token; `acknowledge` matches PollBotCreation.
        std::function<CreationPollResult(uint64_t /*token*/, bool /*acknowledge*/)> pollSingle;
        // Poll a group-batch token; `acknowledge` matches PollBotCreationBatch.
        std::function<BatchPollResult(uint64_t /*token*/, bool /*acknowledge*/)> pollBatch;
        // Delete one finalized temporary character by GUID (DeleteBot).
        std::function<void(uint32_t /*guid*/)> deleteCharacter;
    };

    class AbandonedCreationCleanup
    {
    public:
        // Bounded so a pathological test loop cannot grow the ledger without
        // limit; excess adoptions are dropped (a leaked test character is far
        // preferable to an unbounded owner on the world thread).
        static constexpr size_t kMaxAdopted = 256;

        void AdoptSingle(uint64_t token)
        {
            if (token && singles.size() < kMaxAdopted)
                singles.push_back(token);
        }

        void AdoptBatch(uint64_t token)
        {
            if (token && batches.size() < kMaxAdopted)
                batches.push_back(token);
        }

        void Pump(CreationCleanupOps const& ops)
        {
            for (auto it = singles.begin(); it != singles.end();)
            {
                // Peek WITHOUT acknowledging: ownership is retained until the
                // record reaches a terminal state and any finalized character
                // has been deleted.
                CreationPollResult const poll = ops.pollSingle ? ops.pollSingle(*it, false) : CreationPollResult{};
                if (poll.status == CreationPollStatus::Pending)
                {
                    ++it; // still being created: keep owning it
                    continue;
                }

                // Only a confirmed-created record exposes a GUID; delete that
                // temporary character exactly once. Other terminals (rolled
                // back, quarantined, unknown) expose no GUID and delete nothing.
                if (poll.status == CreationPollStatus::Created && poll.guid && ops.deleteCharacter)
                    ops.deleteCharacter(poll.guid);

                // Acknowledge only now that cleanup is secured, then drop it.
                if (ops.pollSingle)
                    ops.pollSingle(*it, true);
                it = singles.erase(it);
            }

            for (auto it = batches.begin(); it != batches.end();)
            {
                BatchPollResult const poll = ops.pollBatch ? ops.pollBatch(*it, false) : BatchPollResult{};
                if (poll.status == BatchPollStatus::Pending)
                {
                    ++it; // a live batch may still finalize/act on members: never touch its GUIDs
                    continue;
                }

                // A COMPLETE batch is done acting on its finalized members, so
                // their temporary characters are now safe to delete (each once).
                // Acknowledge (which erases the batch) only after cleanup.
                // Unknown = already acknowledged/expired: just drop it.
                if (poll.status == BatchPollStatus::Complete)
                {
                    if (ops.deleteCharacter)
                        for (uint32_t guid : poll.finalizedGuids)
                            ops.deleteCharacter(guid);
                    if (ops.pollBatch)
                        ops.pollBatch(*it, true);
                }
                it = batches.erase(it);
            }
        }

        size_t PendingSingles() const { return singles.size(); }
        size_t PendingBatches() const { return batches.size(); }
        bool Empty() const { return singles.empty() && batches.empty(); }

    private:
        std::vector<uint64_t> singles;
        std::vector<uint64_t> batches;
    };

    // World/database boundary for the durable character-deletion owner. Every
    // callable runs from the pump on the world thread, never from a SQL
    // result callback.
    struct CharacterDeletionOps
    {
        // Re-queues the (idempotent) character deletion for a still-present row.
        std::function<void(uint32_t /*guid*/, uint32_t /*accountId*/)> deleteCharacter;
        // Enqueues the execution-ordered absence readback (COUNT + identity by
        // guid) bound to `generation` - a late callback from a superseded
        // generation must be distinguishable. Returns whether the query was
        // actually ENQUEUED.
        std::function<bool(uint32_t /*guid*/, uint32_t /*generation*/)> requestAbsenceVerify;
        // Execution-confirmed removal of the character's event rows/markers.
        // Returns whether the delete statement actually executed.
        std::function<bool(uint32_t /*guid*/)> clearMetadata;
        // Revokes login eligibility immediately (freeAltBots etc.).
        std::function<void(uint32_t /*guid*/)> revokeLogin;
        // Observes a CONFIRMED-absent, metadata-cleared deletion (account
        // cleanup hooks in here - only now is the durable count truthful).
        std::function<void(uint32_t /*guid*/, uint32_t /*accountId*/)> onConfirmedDeleted;
        // Observes a record failing closed after bounded attempts: its durable
        // rows are KEPT so a restart retries the deletion.
        std::function<void(uint32_t /*guid*/)> onQuarantined;
    };

    // Durable deletion-intent payload: the character's name AND ORIGINAL
    // ACCOUNT. The account makes empty-account cleanup possible when the
    // character row is already absent at recovery, and (name, account)
    // together are the strongest identity the schema can represent - a
    // mismatch on either fails closed (quarantine), never deletes.
    inline std::string EncodeDeletionIntent(std::string const& name, uint32_t accountId)
    {
        return name + "|account:" + std::to_string(accountId);
    }

    inline void DecodeDeletionIntent(std::string const& data, std::string& name, uint32_t& accountId)
    {
        name = data;
        accountId = 0;
        size_t const tag = data.rfind("|account:");
        if (tag == std::string::npos)
            return; // legacy name-only intent: account unknown (0)

        uint64_t parsed = 0;
        for (size_t i = tag + 9; i < data.size(); ++i)
        {
            char const c = data[i];
            if (c < '0' || c > '9' || parsed > 0xFFFFFFFFull)
                return; // malformed: keep name-only interpretation
            parsed = parsed * 10 + static_cast<uint64_t>(c - '0');
        }
        if (tag + 9 == data.size())
            return;

        name = data.substr(0, tag);
        accountId = static_cast<uint32_t>(parsed);
    }

    // Startup/reload recovery decision for one durable deletion-intent row.
    // Only a character present under BOTH its recorded name and recorded
    // account is provably the intended one: re-run the full deletion path.
    // Anything else - absent, renamed, account-moved, or reused - is adopted
    // for confirmation only; the identity-aware verify readback then confirms
    // absence (cleanup with the RECORDED account) or fails closed
    // (quarantine). A deletion is never re-issued at an unproven identity.
    enum class DeletionIntentRecovery
    {
        RequeueDeletion,
        AdoptForConfirmation,
    };

    inline DeletionIntentRecovery PlanDeletionIntentRecovery(bool characterPresent,
        std::string const& currentName, uint32_t currentAccount,
        std::string const& recordedName, uint32_t recordedAccount)
    {
        bool const identityProven = characterPresent
            && !recordedName.empty() && currentName == recordedName
            && recordedAccount != 0 && currentAccount == recordedAccount;
        return identityProven ? DeletionIntentRecovery::RequeueDeletion
                              : DeletionIntentRecovery::AdoptForConfirmation;
    }

    // Durable per-GUID ownership of queued character deletions. In every
    // pinned core Player::DeleteFromDB only QUEUES the deletion transaction,
    // so "DeleteFromDB returned" proves nothing - the old cleanup paths
    // acknowledged their records and deleted event rows/markers immediately
    // after enqueueing, so a failed or lost deletion left a permanent
    // characters row with no marker (never retried) and an auto-added guid
    // could stay login-eligible in freeAltBots.
    //
    // The rule here mirrors the creation finalizer's readback pattern:
    // adoption revokes login eligibility at once; an execution-ordered
    // identity-aware readback (queued on the same FIFO thread AFTER the
    // deletion) must confirm the row absent before any metadata is cleared or
    // any completion is claimed. A still-present row under the RECORDED name
    // re-issues the idempotent deletion; a row under a DIFFERENT name proves
    // the guid was reused (the deletion succeeded) and clears only the intent;
    // failed queries retry. All bounded; exhaustion quarantines the record
    // (fail closed: durable rows stay, so a restart's startup sweep retries).
    // Every request carries a generation and a lost callback is recovered by
    // the deadline watchdog: the wait is abandoned (consuming one bounded
    // attempt), a NEW generation re-requests, and stale results are ignored.
    class DurableCharacterDeletions
    {
    public:
        static constexpr size_t kMaxRecords = 256;
        static constexpr uint32_t kMaxVerifyAttempts = 5;
        static constexpr uint32_t kMaxClearAttempts = 5;
        // Pumps a verification may stay outstanding before its callback is
        // considered lost (the relocation watchdog pattern; one pump per tick).
        static constexpr uint32_t kMaxAwaitingPumps = 128;

        // Adopts ownership of one ALREADY-QUEUED character deletion, with the
        // recorded identity (name; may be empty when the character row was
        // already absent at intent time - a PRESENT row then always fails
        // closed). Login eligibility is revoked immediately, even for
        // duplicate or over-capacity adoptions - an abandoned auto-added
        // character must never race a login. Over-capacity adoptions are
        // otherwise dropped (fail closed: rows stay, the next restart
        // retries).
        void Adopt(uint32_t guid, uint32_t accountId, std::string expectedName,
            CharacterDeletionOps const& ops)
        {
            if (ops.revokeLogin)
                ops.revokeLogin(guid);

            if (records.find(guid) != records.end())
                return; // already owned

            if (records.size() >= kMaxRecords)
            {
                if (ops.onQuarantined)
                    ops.onQuarantined(guid);
                return;
            }

            Record& record = records[guid];
            record.accountId = accountId;
            record.expectedName = std::move(expectedName);
        }

        // SQL-callback entry: copies the outcome into the record's mailbox and
        // returns. Absent = the row is verifiably gone; Verified = still
        // present under the PROVEN recorded identity (name and account both
        // match); IdentityMismatch = present but the identity is unproven
        // (renamed, account-moved, reused, or never recorded) - fails closed.
        // Results from superseded generations (a late callback after a
        // watchdog re-arm) are ignored.
        void OnAbsenceVerify(uint32_t guid, uint32_t generation, RowVerifyOutcome outcome)
        {
            auto it = records.find(guid);
            if (it == records.end() || it->second.quarantined)
                return;
            if (!it->second.verifyRequested || generation != it->second.generation)
                return; // stale generation

            it->second.hasPendingOutcome = true;
            it->second.pendingOutcome = outcome;
        }

        // The identity recorded for a guid: the production callback compares
        // the queried (name, account) against exactly this. False when the
        // guid is not owned.
        bool TryGetExpectedIdentity(uint32_t guid, std::string& name, uint32_t& accountId) const
        {
            auto it = records.find(guid);
            if (it == records.end())
                return false;

            name = it->second.expectedName;
            accountId = it->second.accountId;
            return true;
        }

        void Pump(CharacterDeletionOps const& ops)
        {
            std::vector<uint32_t> completed;
            for (auto& [guid, record] : records)
            {
                if (record.quarantined)
                    continue;

                if (record.absenceConfirmed)
                {
                    // Absence is proven; only the metadata clear is retried.
                    if (ops.clearMetadata && ops.clearMetadata(guid))
                    {
                        if (ops.onConfirmedDeleted)
                            ops.onConfirmedDeleted(guid, record.accountId);
                        completed.push_back(guid);
                    }
                    else if (++record.clearAttempts >= kMaxClearAttempts)
                        Quarantine(guid, record, ops);
                    continue;
                }

                if (record.hasPendingOutcome)
                {
                    record.hasPendingOutcome = false;
                    record.verifyRequested = false;
                    record.awaitingPumps = 0;

                    if (record.pendingOutcome == RowVerifyOutcome::Absent)
                    {
                        record.absenceConfirmed = true;
                        continue; // metadata clears on the next pump
                    }

                    if (record.pendingOutcome == RowVerifyOutcome::IdentityMismatch)
                    {
                        // The guid is occupied but the identity is UNPROVEN
                        // (rename and reuse are indistinguishable here): fail
                        // closed - never delete, never clear the intent, keep
                        // everything for manual resolution.
                        Quarantine(guid, record, ops);
                        continue;
                    }

                    if (++record.attempts >= kMaxVerifyAttempts)
                    {
                        Quarantine(guid, record, ops);
                        continue;
                    }

                    // Still present under the recorded identity: re-issue the
                    // idempotent deletion before the next readback. A failed
                    // query re-verifies only.
                    if (record.pendingOutcome == RowVerifyOutcome::Verified && ops.deleteCharacter)
                        ops.deleteCharacter(guid, record.accountId);
                }
                else if (record.verifyRequested)
                {
                    // Lost-callback watchdog: abandon the wait after the
                    // bounded pump budget (one attempt consumed); the request
                    // below re-arms under a NEW generation, and the stale
                    // result - should it straggle in - no longer matches.
                    if (++record.awaitingPumps > kMaxAwaitingPumps)
                    {
                        record.verifyRequested = false;
                        record.awaitingPumps = 0;
                        if (++record.attempts >= kMaxVerifyAttempts)
                        {
                            Quarantine(guid, record, ops);
                            continue;
                        }
                    }
                }

                if (!record.verifyRequested)
                {
                    ++record.generation;
                    if (ops.requestAbsenceVerify && ops.requestAbsenceVerify(guid, record.generation))
                        record.verifyRequested = true;
                    else
                    {
                        // Failed ENQUEUE feeds back through the mailbox and
                        // consumes one bounded attempt on the next pump.
                        record.verifyRequested = true; // so the stored outcome passes the gate
                        record.hasPendingOutcome = true;
                        record.pendingOutcome = RowVerifyOutcome::QueryFailed;
                    }
                }
            }

            for (uint32_t guid : completed)
                records.erase(guid);
        }

        bool Owns(uint32_t guid) const { return records.find(guid) != records.end(); }

        bool IsQuarantined(uint32_t guid) const
        {
            auto it = records.find(guid);
            return it != records.end() && it->second.quarantined;
        }

        // The generation of the outstanding request (0 when none) - test and
        // diagnostics surface.
        uint32_t CurrentGeneration(uint32_t guid) const
        {
            auto it = records.find(guid);
            return it == records.end() ? 0 : it->second.generation;
        }

        size_t RecordCount() const { return records.size(); }

    private:
        struct Record
        {
            uint32_t accountId = 0;
            std::string expectedName;
            uint32_t attempts = 0;      // verify outcomes consumed (present/failed/lost)
            uint32_t clearAttempts = 0; // metadata/intent-clear failures
            uint32_t generation = 0;    // stamps requests; stale results ignored
            uint32_t awaitingPumps = 0; // watchdog while a request is outstanding
            bool verifyRequested = false;
            bool absenceConfirmed = false;
            bool quarantined = false;
            bool hasPendingOutcome = false;
            RowVerifyOutcome pendingOutcome = RowVerifyOutcome::QueryFailed;
        };

        void Quarantine(uint32_t guid, Record& record, CharacterDeletionOps const& ops)
        {
            record.quarantined = true;
            if (ops.onQuarantined)
                ops.onQuarantined(guid);
        }

        std::map<uint32_t, Record> records;
    };
}
