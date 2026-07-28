#pragma once

#include "LivingCreationBatch.h"     // BatchPollResult / BatchPollStatus
#include "LivingCreationFinalizer.h" // CreationPollResult / CreationPollStatus
#include "LivingNumericParse.h"      // TryParseUInt32

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
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
    //   - once terminal, the finalized GUIDs are copied into this owner's own
    //     ledger and the bounded poll result is acknowledged immediately, so a
    //     prolonged deletion-intent outage cannot make token expiry lose them;
    //   - a finalized temporary character is handed to the durable deletion
    //     owner EXACTLY ONCE (a Created single, or every finalized GUID of a
    //     COMPLETE batch), then dropped from this ledger;
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
        // Deletes one finalized temporary character by GUID (DeleteBot) and
        // returns whether DURABLE DELETION OWNERSHIP was secured (the
        // fail-closed deletion path refuses when its intent cannot be
        // persisted). On false the copied GUID stays in this cleanup ledger.
        std::function<bool(uint32_t /*guid*/)> deleteCharacter;
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
                singles.push_back(SingleCleanup{ token, 0 });
        }

        void AdoptBatch(uint64_t token)
        {
            if (token && batches.size() < kMaxAdopted)
                batches.push_back(BatchCleanup{ token, {} });
        }

        void AdoptGuid(uint32_t guid)
        {
            if (guid && guids.size() < kMaxAdopted)
                guids.insert(guid);
        }

        void Pump(CreationCleanupOps const& ops)
        {
            // `singles` and `batches` are vectors, and every ops callback below
            // can re-enter AdoptSingle/AdoptBatch (deleteCharacter reaches bot
            // teardown, which abandons whatever creation work that bot owned).
            // A push_back there reallocates and would dangle a held iterator,
            // so both loops index instead and re-subscript after every call.
            // `guids` is a std::set, whose iterators survive insertion, so it
            // keeps its iterator loop.
            for (size_t i = 0; i < singles.size();)
            {
                if (singles[i].token)
                {
                    CreationPollResult const poll = ops.pollSingle
                        ? ops.pollSingle(singles[i].token, false) : CreationPollResult{};
                    if (poll.status == CreationPollStatus::Pending)
                    {
                        ++i; // still being created: keep owning it
                        continue;
                    }

                    if (poll.status != CreationPollStatus::Created || !poll.guid)
                    {
                        if (ops.pollSingle)
                            ops.pollSingle(singles[i].token, true);
                        singles.erase(singles.begin() + static_cast<std::ptrdiff_t>(i));
                        continue;
                    }

                    // Transfer terminal ownership out of the expiring poll
                    // surface before retrying the durable deletion boundary.
                    singles[i].guid = poll.guid;
                    if (ops.pollSingle)
                        ops.pollSingle(singles[i].token, true);
                    singles[i].token = 0;
                }

                if (!ops.deleteCharacter || !ops.deleteCharacter(singles[i].guid))
                {
                    ++i;
                    continue;
                }

                singles.erase(singles.begin() + static_cast<std::ptrdiff_t>(i));
            }

            for (size_t i = 0; i < batches.size();)
            {
                if (batches[i].token)
                {
                    BatchPollResult const poll = ops.pollBatch
                        ? ops.pollBatch(batches[i].token, false) : BatchPollResult{};
                    if (poll.status == BatchPollStatus::Pending)
                    {
                        ++i; // a live batch may still finalize/act on members: never touch its GUIDs
                        continue;
                    }

                    if (poll.status != BatchPollStatus::Complete)
                    {
                        batches.erase(batches.begin() + static_cast<std::ptrdiff_t>(i));
                        continue;
                    }

                    batches[i].pendingGuids.insert(poll.finalizedGuids.begin(), poll.finalizedGuids.end());
                    if (ops.pollBatch)
                        ops.pollBatch(batches[i].token, true);
                    batches[i].token = 0;
                }

                // Snapshot the guids and erase by value. The set's own
                // iterators would actually survive a reallocation of `batches`
                // (moving a std::set transfers its nodes, LWG 2321) - what
                // dangled before was the enclosing VECTOR iterator in
                // `it->pendingGuids`. Re-subscripting batches[i] is what fixes
                // that; the snapshot additionally keeps this loop from
                // depending on the node-transfer guarantee at all.
                std::vector<uint32_t> const pending(batches[i].pendingGuids.begin(),
                    batches[i].pendingGuids.end());
                for (uint32_t guid : pending)
                {
                    if (ops.deleteCharacter && ops.deleteCharacter(guid))
                        batches[i].pendingGuids.erase(guid);
                }

                if (batches[i].pendingGuids.empty())
                    batches.erase(batches.begin() + static_cast<std::ptrdiff_t>(i));
                else
                    ++i;
            }

            for (auto it = guids.begin(); it != guids.end();)
            {
                if (ops.deleteCharacter && ops.deleteCharacter(*it))
                    it = guids.erase(it);
                else
                    ++it;
            }
        }

        size_t PendingSingles() const { return singles.size(); }
        size_t PendingBatches() const { return batches.size(); }
        size_t PendingGuids() const { return guids.size(); }
        bool Empty() const { return singles.empty() && batches.empty() && guids.empty(); }

    private:
        struct SingleCleanup
        {
            uint64_t token = 0;
            uint32_t guid = 0;
        };

        struct BatchCleanup
        {
            uint64_t token = 0;
            std::set<uint32_t> pendingGuids;
        };

        std::vector<SingleCleanup> singles;
        std::vector<BatchCleanup> batches;
        std::set<uint32_t> guids;
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
        // request must be distinguishable. Returns whether the query was
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

        // Route through the shared exact parser, exactly as DecodeCreationIntent
        // does for the same field. The hand-rolled accumulator this replaces
        // checked its overflow bound BEFORE the multiply-add, so a value one
        // decimal digit past UINT32_MAX ("|account:4294967297") passed the guard
        // on its last digit and then truncated to a different, valid-looking
        // account - which restart recovery would carry into confirmed-deletion
        // account cleanup against an unrelated account. TryParseUInt32 rejects
        // the whole token instead, and an unparsable account keeps the
        // name-only interpretation (accountId stays 0) so identity checks still
        // fail closed.
        uint32_t decoded = 0;
        if (!TryParseUInt32(data.substr(tag + 9), decoded))
            return; // malformed: keep name-only interpretation

        name = data.substr(0, tag);
        accountId = decoded;
    }

    // Typed deletion preflight over ONE identity query
    // (COUNT + MIN(name) + MIN(account) by guid). Distinguishes a verified
    // present identity from confirmed absence from an unanswerable lookup -
    // the legacy GetPlayerNameByGUID/GetPlayerAccountIdByGUID pair conflated
    // missing rows with query failure and accepted account 0, losing correct
    // realm/account bookkeeping and leaving recovery unable to prove the
    // identity it recorded. Unknown or incomplete identity (failed query,
    // empty name, account 0) must write no intent and queue no deletion.
    enum class DeletionPreflight
    {
        Unknown,         // query failed or identity incomplete: fail closed
        ConfirmedAbsent, // no characters row: nothing destructive to queue
        VerifiedPresent, // full (name, account) identity verified
    };

    inline DeletionPreflight ClassifyDeletionPreflight(std::optional<uint64_t> count,
        std::string const& name, uint32_t accountId)
    {
        if (!count)
            return DeletionPreflight::Unknown;
        if (*count == 0)
            return DeletionPreflight::ConfirmedAbsent;
        if (name.empty() || accountId == 0)
            return DeletionPreflight::Unknown; // present but incomplete: fail closed
        return DeletionPreflight::VerifiedPresent;
    }

    // Startup/reload recovery decision for one durable deletion-intent row.
    // (guid, name, account) is NOT an immutable identity across a restart:
    // the guid may have been reused by a NEW character with the same name on
    // the same account, and no immutable creation nonce exists in the schema.
    // A PRESENT row is therefore NEVER automatically re-deleted after a
    // restart - it is adopted pre-quarantined (fail closed, login blocked,
    // manual resolution). Only a confirmed-ABSENT row proceeds to
    // metadata/account cleanup using the RECORDED account.
    enum class DeletionIntentRecovery
    {
        AdoptForCleanup,   // character absent: confirm + clean up
        QuarantinePresent, // character present: identity unprovable, fail closed
    };

    inline DeletionIntentRecovery PlanDeletionIntentRecovery(bool characterPresent)
    {
        return characterPresent ? DeletionIntentRecovery::QuarantinePresent
                                : DeletionIntentRecovery::AdoptForCleanup;
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
    //
    // Each record has at most one outstanding readback, stamped with a
    // GENERATION so a result from a superseded request is distinguishable. A
    // callback that never arrives (DB thread torn down, result queue dropped,
    // or simply a very deep write backlog) is recovered by the pump watchdog:
    // the wait is abandoned - consuming one bounded attempt - and a NEW
    // generation re-requests; stale results are ignored. Only attempt
    // exhaustion quarantines, and quarantine DEMOTES the record to an inert
    // owned guid: login and guid reuse stay blocked, the durable rows stay
    // for the next restart's scan, and active-record capacity is never
    // consumed by dead weight.
    class DurableCharacterDeletions
    {
    public:
        static constexpr size_t kMaxRecords = 256;
        static constexpr uint32_t kMaxVerifyAttempts = 5;
        static constexpr uint32_t kMaxClearAttempts = 5;
        // Pumps a readback may stay outstanding before its callback is
        // considered lost and the wait is re-armed. Denominated in world
        // ticks (>= 50ms each, so >= ~60 seconds per wait): the module treats
        // a 10-second character-DB backlog as routine (the GetDatabaseDelay
        // gates), and the readback is queued BEHIND the deletion on exactly
        // that backlog, so the margin is deliberately wide - and expiry
        // re-arms rather than terminating, bounded by kMaxVerifyAttempts
        // overall.
        static constexpr uint32_t kMaxAwaitingPumps = 1200;

        // Adopts ownership of one ALREADY-QUEUED character deletion, with the
        // recorded identity (name; may be empty when the character row was
        // already absent at intent time - a PRESENT row then always fails
        // closed). Login eligibility is revoked immediately, even for
        // duplicate or over-capacity adoptions - an abandoned auto-added
        // character must never race a login.
        // `identityProvenInProcess`: true only when the caller verified the
        // present identity in THIS process (the typed deletion preflight) -
        // a Verified readback may then re-issue the idempotent deletion.
        // Recovery adoptions (restart boundary) pass false: any PRESENT
        // readback fails closed, because the recorded identity cannot prove
        // the row was not reused since the original process died.
        //
        // Over-capacity adoptions get no confirmation record - that work is
        // deferred to the next restart's scan, which the durable intent rows
        // survive for. The guid still quarantines as OWNED (fail closed):
        // Owns() gates IsDeletionPending, which is what blocks login and
        // refuses the guid to a new creation while its DeleteFromDB is queued.
        // Forgetting it here would let a character with a queued deletion log
        // back in, or a fresh creation land on its guid. Because quarantined
        // guids do not occupy record capacity, the registry can only be full
        // of genuinely in-flight confirmations - the ceiling never wedges.
        void Adopt(uint32_t guid, uint32_t accountId, std::string expectedName,
            bool identityProvenInProcess, CharacterDeletionOps const& ops)
        {
            if (ops.revokeLogin)
                ops.revokeLogin(guid);

            if (records.find(guid) != records.end() || quarantined.count(guid))
                return; // already owned (live or inert)

            if (records.size() >= kMaxRecords)
            {
                Quarantine(guid, ops); // owned, inert, disclosed; restart retries
                return;
            }

            Record& record = records[guid];
            record.accountId = accountId;
            record.expectedName = std::move(expectedName);
            record.identityProvenInProcess = identityProvenInProcess;
        }

        // Adopts a recovery-time PRESENT row directly into quarantine: login
        // stays blocked and everything is retained for manual resolution.
        // NEVER downgrades a live record: a config reload re-scanning a
        // deletion this process already owns with a PROVEN identity must not
        // strand it in quarantine - the live record keeps confirming.
        void AdoptQuarantined(uint32_t guid, CharacterDeletionOps const& ops)
        {
            if (ops.revokeLogin)
                ops.revokeLogin(guid);

            if (records.find(guid) != records.end())
                return; // already owned (possibly mid-confirmation): keep it

            Quarantine(guid, ops);
        }

        // SQL-callback entry: copies the outcome into the record's mailbox and
        // returns. Absent = the row is verifiably gone; Verified = still
        // present under the PROVEN recorded identity (name and account both
        // match); IdentityMismatch = present but the identity is unproven
        // (renamed, account-moved, reused, or never recorded) - fails closed.
        // Results from superseded generations (a late callback after a
        // watchdog re-arm) are ignored, as are results for completed or
        // quarantined guids (no record).
        void OnAbsenceVerify(uint32_t guid, uint32_t generation, RowVerifyOutcome outcome)
        {
            auto it = records.find(guid);
            if (it == records.end())
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
            std::vector<uint32_t> quarantinedNow;
            for (auto& [guid, record] : records)
            {
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
                        quarantinedNow.push_back(guid);
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
                        quarantinedNow.push_back(guid);
                        continue;
                    }

                    if (++record.attempts >= kMaxVerifyAttempts)
                    {
                        quarantinedNow.push_back(guid);
                        continue;
                    }

                    // Still present under the recorded identity: re-issue the
                    // idempotent deletion ONLY when this process itself proved
                    // the identity (in-process adoption). A recovery adoption
                    // can never prove the row was not reused: fail closed.
                    if (record.pendingOutcome == RowVerifyOutcome::Verified)
                    {
                        if (!record.identityProvenInProcess)
                        {
                            quarantinedNow.push_back(guid);
                            continue;
                        }
                        if (ops.deleteCharacter)
                            ops.deleteCharacter(guid, record.accountId);
                    }
                }
                else if (record.verifyRequested && ++record.awaitingPumps > kMaxAwaitingPumps)
                {
                    // Lost-callback watchdog: abandon the wait (consuming one
                    // bounded attempt) and re-arm below under a NEW
                    // generation; the stale result - should it straggle in -
                    // no longer matches.
                    record.verifyRequested = false;
                    record.awaitingPumps = 0;
                    if (++record.attempts >= kMaxVerifyAttempts)
                    {
                        quarantinedNow.push_back(guid);
                        continue;
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
            for (uint32_t guid : quarantinedNow)
            {
                records.erase(guid);
                Quarantine(guid, ops);
            }
        }

        bool Owns(uint32_t guid) const
        {
            return records.find(guid) != records.end() || quarantined.count(guid) != 0;
        }

        bool IsQuarantined(uint32_t guid) const { return quarantined.count(guid) != 0; }

        size_t RecordCount() const { return records.size(); }

    private:
        struct Record
        {
            uint32_t accountId = 0;
            std::string expectedName;
            bool identityProvenInProcess = false;
            uint32_t attempts = 0;      // verify outcomes consumed (present/failed/lost)
            uint32_t clearAttempts = 0; // metadata/intent-clear failures
            uint32_t generation = 0;    // stamps requests; stale results ignored
            uint32_t awaitingPumps = 0; // watchdog while a request is outstanding
            bool verifyRequested = false;
            bool absenceConfirmed = false;
            bool hasPendingOutcome = false;
            RowVerifyOutcome pendingOutcome = RowVerifyOutcome::QueryFailed;
        };

        // Discloses once per guid; repeated adoption of an already-quarantined
        // guid is silent (it is already owned and already reported).
        void Quarantine(uint32_t guid, CharacterDeletionOps const& ops)
        {
            if (quarantined.insert(guid).second && ops.onQuarantined)
                ops.onQuarantined(guid);
        }

        std::map<uint32_t, Record> records;
        // Failed-closed guids: quarantined outcomes and over-capacity
        // adoptions. Inert (no pump work, ~a set node each) but still OWNED,
        // so login and guid reuse stay blocked; the durable rows carry the
        // retry across the next restart. Terminal for the process lifetime by
        // design - in-process retry of an unprovable or exhausted deletion is
        // exactly what fail-closed forbids.
        std::set<uint32_t> quarantined;
    };
}
