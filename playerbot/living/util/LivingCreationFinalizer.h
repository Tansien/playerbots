#pragma once

#include "LivingCreationLifecycle.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace living
{
    // Deferred-callback creation finalizer core.
    //
    // In every pinned core the SQL worker thread still holds the async DB
    // connection when it hands a finished query to SqlResultQueue::Add, and
    // SqlResultQueue::Update holds the result-queue mutex while executing the
    // callback on the world thread. A callback that performs ANY database
    // operation (DirectPExecute, another query) can therefore deadlock the
    // server: world thread holds queue-lock -> waits for DB-lock, worker holds
    // DB-lock -> waits for queue-lock.
    //
    // The contract enforced here: a SQL result callback may only call
    // OnCallbackResult/OnBulkCallbackResult, which copy the parsed outcome
    // into the owning record's or bulk generation's MAILBOX and return. The
    // storage exists from admission (each request has at most one outstanding
    // query), so a callback for admitted work can never be dropped. Every
    // lifecycle transition, metadata write, cleanup, retry and follow-up
    // query scheduling runs in Pump(), which the module executes from a
    // normal world update - in all three pinned cores the playerbots update
    // hooks run BEFORE World::UpdateResultQueue in the same tick, so an
    // outcome stored while ProcessResultQueue dispatched a callback is always
    // pumped on a LATER tick, strictly after the result-queue mutex was
    // released.
    //
    // The database boundary is injected through CreationFinalizerOps so the
    // exact production flow is host-testable with spy callables.

    // Opaque completion status for one creation token (poll surface).
    enum class CreationPollStatus
    {
        // The creation is still being confirmed; keep polling.
        Pending,
        // Confirmed created; the finalized GUID is available in the result.
        Created,
        // Confirmed rolled back or verifiably cleaned up; a new attempt is safe.
        FailedRetryable,
        // No further attempt can succeed.
        FailedTerminal,
        // Durability or cleanup could not be established; never retried.
        Quarantined,
        // The token is not known (never issued, or its terminal completion
        // already expired/was acknowledged). Callers must treat this as
        // terminal, never as pending.
        Unknown,
    };

    struct CreationPollResult
    {
        CreationPollStatus status = CreationPollStatus::Unknown;
        uint32_t guid = 0; // nonzero only for Created
        std::string message;
    };

    // One terminal completion, published exactly once per record - BEFORE the
    // record is removed - and then retained for polling until acknowledged or
    // expired. `guid` is the record's character GUID for internal bookkeeping
    // (payload cleanup, logging); the POLL surface exposes it only for
    // Created - a failed creation's GUID is never handed to callers.
    struct CreationCompletion
    {
        uint64_t token = 0;
        uint32_t guid = 0;
        uint32_t accountId = 0;
        CreationPollStatus status = CreationPollStatus::Unknown;
        std::string name;
        std::string message;
    };

    enum class CreationCallbackKind
    {
        Verify,
        CleanupVerify,
        // Bulk-reservation count verification: the event's guid field carries
        // the ACCOUNT id, and the outcome is only Verified (count readback
        // succeeded) or QueryFailed.
        BulkCount,
    };

    // Outcome of the ONE shared character-slot reservation API. Capacity that
    // cannot be verified is transient unavailability - never "empty" (which
    // fails admission open) and never "full"/terminal (which misreports an
    // outage as exhaustion).
    enum class SlotReservationOutcome
    {
        Reserved,
        AtCapacity,
        DatabaseUnavailable,
    };

    // Database/world boundary. Every callable is executed from Pump()/Begin()
    // on the world thread, never from a SQL result callback. The request
    // callables return whether the query was actually ENQUEUED (the pinned
    // cores report enqueue failure via a false AsyncPQuery return); a failed
    // enqueue is fed back as a QueryFailed outcome so the bounded retry paths
    // own it instead of the request waiting forever for a callback that was
    // never queued.
    struct CreationFinalizerOps
    {
        // Schedules the execution-ordered character-row verification query.
        std::function<bool(uint32_t /*guid*/)> requestVerify;
        // Schedules the deletion-verification query.
        std::function<bool(uint32_t /*guid*/)> requestCleanupVerify;
        // Performs ALL execution-confirmed metadata writes for the record.
        std::function<bool(uint32_t /*guid*/)> writeMetadata;
        // Execution-confirmed removal of the record's event rows.
        std::function<bool(uint32_t /*guid*/)> deleteEventRows;
        // Queues the character deletion (idempotent).
        std::function<void(uint32_t /*guid*/, uint32_t /*accountId*/)> deleteCharacter;
        // The one place a confirmed GUID is exposed (freeAltBots etc.).
        std::function<void(uint32_t /*guid*/, uint32_t /*accountId*/, std::string const& /*name*/, bool /*autoAdd*/)> onCreated;
        // Schedules the execution-ordered bulk-reservation count readback for
        // ONE bulk generation (issued AFTER that generation's saves were
        // queued on the same FIFO thread, so its execution is a durability
        // barrier for exactly those saves).
        std::function<bool(uint32_t /*accountId*/, uint64_t /*generation*/)> requestBulkCountVerify;
        // Observes a bulk generation giving up after bounded attempts: its
        // reservations are KEPT (conservative direction) and the caller logs
        // the stuck capacity.
        std::function<void(uint32_t /*accountId*/, uint32_t /*keptReservations*/)> onBulkVerifyGaveUp;
    };

    class CreationFinalizer
    {
    public:
        struct Record
        {
            CreationLifecycle lifecycle;
            uint64_t token = 0; // stamped by Begin
            uint32_t guid = 0;
            uint32_t accountId = 0;
            std::string name;
            bool autoAdd = false;
            // True when this record carries one reserved account character
            // slot (see TryReserveAccountSlot); released only on a CONFIRMED
            // terminal outcome, kept forever by quarantined records.
            bool holdsAccountReservation = false;
            // Per-record callback MAILBOX: each record has at most one
            // outstanding query, its storage exists from admission, and the
            // SQL callback only copies the outcome here - a callback for an
            // admitted record can never be dropped.
            bool hasPendingOutcome = false;
            RowVerifyOutcome pendingOutcome = RowVerifyOutcome::QueryFailed;
            CreationCallbackKind pendingKind = CreationCallbackKind::Verify;
        };

        static constexpr uint32_t kMaxRecords = 64;
        static constexpr uint32_t kMaxVerifyAttempts = 5;
        static constexpr uint32_t kMaxCleanupAttempts = 5;
        static constexpr size_t kMaxCompletions = 128;
        // Terminal completions are retained this many pumps for pollers that
        // never acknowledge (bounded expiry).
        static constexpr uint64_t kCompletionRetentionPumps = 4096;

        // Bounded registry: quarantined records occupy capacity forever by
        // design - a full registry refuses new creations rather than reusing
        // uncertain state.
        bool CanAdmit() const { return records.size() < kMaxRecords; }

        bool HasPendingName(std::string const& name) const
        {
            for (auto const& [guid, record] : records)
                if (record.name == name)
                    return true;

            return false;
        }

        // --- Per-account character-capacity reservations -------------------
        // A queued-but-unexecuted SaveToDB is invisible to the synchronous
        // durable character count, so admission must charge pending (and
        // quarantined) creations against the account BEFORE the save is
        // queued. The caller reserves with the CURRENT durable count; the
        // reservation is bound to the record by Begin and released only when
        // the record reaches a CONFIRMED terminal outcome (created row counted
        // durably, or rollback/cleanup verified). A pre-save failure releases
        // immediately because no save was queued.

        bool TryReserveAccountSlot(uint32_t accountId, uint32_t durableCharacterCount, uint32_t accountCap)
        {
            if (durableCharacterCount + ReservedCount(accountId) >= accountCap)
                return false;

            ++reservedSlots[accountId];
            return true;
        }

        // Releases one unbound reservation (pre-save failure only; a bound
        // reservation is released by the record's terminal outcome).
        void ReleaseAccountSlot(uint32_t accountId)
        {
            auto it = reservedSlots.find(accountId);
            if (it == reservedSlots.end())
                return;

            if (--it->second == 0)
                reservedSlots.erase(it);
        }

        uint32_t ReservedCount(uint32_t accountId) const
        {
            auto it = reservedSlots.find(accountId);
            return it == reservedSlots.end() ? 0 : it->second;
        }

        // -------------------------------------------------------------------

        // --- Bulk-reservation verification (legacy mass factory) -----------
        // The bulk factory reserves one shared slot per queued character but
        // cannot register hundreds of records in this bounded registry.
        // Instead each bulk RUN registers one GENERATION per touched account:
        // a monotonic token owning exactly the slots that run reserved, with
        // its count readback queued AFTER that generation's saves on the same
        // FIFO thread. The barrier property is generation-scoped: a readback
        // releases ONLY its own generation's slots - it proves nothing about
        // reservations added after its query was queued, so generations are
        // never merged. Failed readbacks retry bounded per generation, and
        // exhaustion KEEPS that generation's reservations - capacity stays
        // blocked in the conservative direction, never released on "SaveToDB
        // returned". Stale or duplicate generation results match nothing and
        // are ignored.

        static constexpr uint32_t kMaxBulkVerifyAttempts = 5;

        // Returns the generation token (0 for an empty registration). The
        // caller must queue this generation's saves BEFORE calling (the
        // request is issued here, after them, preserving FIFO ordering).
        uint64_t BeginBulkVerify(uint32_t accountId, uint32_t reservedSlots, CreationFinalizerOps const& ops)
        {
            if (reservedSlots == 0)
                return 0;

            uint64_t const generation = nextToken++;
            BulkVerify& entry = bulkVerifies[generation];
            entry.accountId = accountId;
            entry.reservedSlots = reservedSlots;

            if (!ops.requestBulkCountVerify || !ops.requestBulkCountVerify(accountId, generation))
                StoreBulkOutcome(generation, RowVerifyOutcome::QueryFailed); // bounded retry from the pump

            return generation;
        }

        // SQL-callback entry for bulk count readbacks: copies the outcome
        // into the generation's mailbox and returns. Unknown generations
        // (stale, duplicate, completed) are ignored.
        void OnBulkCallbackResult(uint64_t generation, RowVerifyOutcome outcome)
        {
            StoreBulkOutcome(generation, outcome);
        }

        uint32_t PendingBulkVerifyCount() const { return static_cast<uint32_t>(bulkVerifies.size()); }

        // -------------------------------------------------------------------

        // Registers the record (adopting the caller's account reservation when
        // record.holdsAccountReservation is set) and schedules the first
        // verification. Returns the creation token. Callback storage lives in
        // the record itself, so admission (CanAdmit) is what bounds callback
        // work - an admitted record's callback can never be dropped.
        uint64_t Begin(Record record, CreationFinalizerOps const& ops)
        {
            record.token = nextToken++;
            uint32_t const guid = record.guid;
            uint64_t const token = record.token;
            tokenToGuid[token] = guid;
            Record& stored = records[guid];
            stored = std::move(record);
            if (!ops.requestVerify || !ops.requestVerify(guid))
            {
                // Enqueue failure: feed a QueryFailed outcome through the
                // record's own mailbox so the bounded verify-retry path owns
                // it (one attempt consumed per pump, then quarantine).
                stored.hasPendingOutcome = true;
                stored.pendingOutcome = RowVerifyOutcome::QueryFailed;
                stored.pendingKind = CreationCallbackKind::Verify;
            }
            return token;
        }

        // The ONLY entry point for ordinary SQL result callbacks: copies the
        // parsed outcome into the record's mailbox and returns. No lifecycle
        // transition, no ops callable, no database work happens here. A
        // callback for an erased/unknown record is ignored (its work is
        // already terminal).
        void OnCallbackResult(uint32_t guid, RowVerifyOutcome outcome, CreationCallbackKind kind)
        {
            auto it = records.find(guid);
            if (it == records.end())
                return;

            it->second.hasPendingOutcome = true;
            it->second.pendingOutcome = outcome;
            it->second.pendingKind = kind;
        }

        // Consumes every stored callback outcome and drives every lifecycle
        // transition, metadata write, cleanup, retry and follow-up query.
        // `onTerminal` observes each terminal completion BEFORE its record is
        // removed (the batch coordinator and logging hook in).
        void Pump(CreationFinalizerOps const& ops,
            std::function<void(CreationCompletion const&)> const& onTerminal = {})
        {
            ++pumpCount;

            // Collect first, process second: processing can erase records
            // (and enqueue-failure synthesis can store new outcomes that
            // belong to the NEXT pump).
            struct DueOutcome
            {
                uint32_t guid;
                RowVerifyOutcome outcome;
                CreationCallbackKind kind;
            };
            std::vector<DueOutcome> due;
            for (auto& [guid, record] : records)
            {
                if (!record.hasPendingOutcome)
                    continue;

                record.hasPendingOutcome = false;
                due.push_back(DueOutcome{ guid, record.pendingOutcome, record.pendingKind });
            }

            for (DueOutcome const& outcome : due)
            {
                auto it = records.find(outcome.guid);
                if (it == records.end())
                    continue;

                if (outcome.kind == CreationCallbackKind::Verify)
                    ProcessVerify(it->second, outcome.outcome, ops, onTerminal);
                else
                    ProcessCleanupVerify(it->second, outcome.outcome, ops, onTerminal);
            }

            struct DueBulk
            {
                uint64_t generation;
                RowVerifyOutcome outcome;
            };
            std::vector<DueBulk> dueBulk;
            for (auto& [generation, entry] : bulkVerifies)
            {
                if (!entry.hasPendingOutcome)
                    continue;

                entry.hasPendingOutcome = false;
                dueBulk.push_back(DueBulk{ generation, entry.pendingOutcome });
            }

            for (DueBulk const& outcome : dueBulk)
                ProcessBulkCount(outcome.generation, outcome.outcome, ops);

            PruneExpiredCompletions();
        }

        // Bounded poll surface for creation tokens. While the record is alive
        // the status reflects its stage; a terminal completion is served until
        // acknowledged (acknowledgeTerminal consumes it) or expired.
        CreationPollResult Poll(uint64_t token, bool acknowledgeTerminal)
        {
            if (auto it = completions.find(token); it != completions.end())
            {
                CreationPollResult result;
                result.status = it->second.completion.status;
                result.guid = it->second.completion.status == CreationPollStatus::Created
                    ? it->second.completion.guid : 0;
                result.message = it->second.completion.message;
                if (acknowledgeTerminal)
                    completions.erase(it);
                return result;
            }

            if (auto tokenIt = tokenToGuid.find(token); tokenIt != tokenToGuid.end())
            {
                if (auto recordIt = records.find(tokenIt->second); recordIt != records.end())
                {
                    CreationPollResult result;
                    switch (recordIt->second.lifecycle.stage)
                    {
                        case CreationStage::Quarantined:
                            // The completion was already published (and maybe
                            // acknowledged/expired) but the record stays
                            // forever by design.
                            result.status = CreationPollStatus::Quarantined;
                            result.message = "creation quarantined";
                            break;
                        default:
                            result.status = CreationPollStatus::Pending;
                            break;
                    }
                    return result;
                }
            }

            CreationPollResult unknown;
            unknown.status = CreationPollStatus::Unknown;
            unknown.message = "unknown or expired creation token";
            return unknown;
        }

        // Read-only access to a live record. The production SQL callback uses
        // this to compare the queried identity against the expected one - a
        // pure in-memory read; the callback still may not mutate anything.
        template <typename Fn>
        bool WithRecord(uint32_t guid, Fn&& fn) const
        {
            auto it = records.find(guid);
            if (it == records.end())
                return false;

            fn(it->second);
            return true;
        }

        size_t RecordCount() const { return records.size(); }

        // Stored-but-unconsumed callback outcomes (record mailboxes plus bulk
        // generation mailboxes).
        size_t PendingCallbackCount() const
        {
            size_t count = 0;
            for (auto const& [guid, record] : records)
                if (record.hasPendingOutcome)
                    ++count;
            for (auto const& [generation, entry] : bulkVerifies)
                if (entry.hasPendingOutcome)
                    ++count;
            return count;
        }

    private:
        struct StoredCompletion
        {
            CreationCompletion completion;
            uint64_t expiresAtPump = 0;
        };

        // Feeds an outcome into a record's mailbox from PUMP context (retry
        // request enqueue failures): consumed on the NEXT pump, which paces
        // the bounded retries one attempt per tick.
        void StoreRecordOutcome(uint32_t guid, RowVerifyOutcome outcome, CreationCallbackKind kind)
        {
            auto it = records.find(guid);
            if (it == records.end())
                return;

            it->second.hasPendingOutcome = true;
            it->second.pendingOutcome = outcome;
            it->second.pendingKind = kind;
        }

        void StoreBulkOutcome(uint64_t generation, RowVerifyOutcome outcome)
        {
            auto it = bulkVerifies.find(generation);
            if (it == bulkVerifies.end())
                return; // stale/duplicate/completed generation: ignored

            it->second.hasPendingOutcome = true;
            it->second.pendingOutcome = outcome;
        }

        void ProcessVerify(Record& record, RowVerifyOutcome outcome, CreationFinalizerOps const& ops,
            std::function<void(CreationCompletion const&)> const& onTerminal)
        {
            switch (record.lifecycle.OnCharacterVerify(outcome, kMaxVerifyAttempts))
            {
                case CreationStage::PendingPersistence:
                    if (outcome == RowVerifyOutcome::Verified)
                        FinalizeVerified(record, ops, onTerminal);
                    // Bounded retry of a failed query; a failed ENQUEUE feeds
                    // back through the mailbox and consumes the next attempt.
                    else if (!ops.requestVerify || !ops.requestVerify(record.guid))
                        StoreRecordOutcome(record.guid, RowVerifyOutcome::QueryFailed, CreationCallbackKind::Verify);
                    break;
                case CreationStage::FailedRetryable:
                    // The character transaction executed and rolled back; no
                    // dependent state was ever written.
                    PublishTerminal(record, CreationPollStatus::FailedRetryable,
                        "character transaction rolled back; nothing persisted", onTerminal);
                    break;
                case CreationStage::Quarantined:
                    PublishTerminal(record, CreationPollStatus::Quarantined,
                        outcome == RowVerifyOutcome::IdentityMismatch
                            ? "durable state could not be established (identity mismatch)"
                            : "durable state could not be established (verify queries kept failing)",
                        onTerminal);
                    break;
                default:
                    break;
            }
        }

        void FinalizeVerified(Record& record, CreationFinalizerOps const& ops,
            std::function<void(CreationCompletion const&)> const& onTerminal)
        {
            bool const metadataOk = ops.writeMetadata ? ops.writeMetadata(record.guid) : false;

            if (record.lifecycle.OnMetadataResult(metadataOk) == CreationStage::Created)
            {
                // Only now is the GUID exposed and the creation complete.
                if (ops.onCreated)
                    ops.onCreated(record.guid, record.accountId, record.name, record.autoAdd);

                PublishTerminal(record, CreationPollStatus::Created, "creation finalized", onTerminal);
                return;
            }

            // Metadata failed after character durability: clean up, and only a
            // CONFIRMED cleanup may make this attempt retryable.
            bool const eventsGone = ops.deleteEventRows ? ops.deleteEventRows(record.guid) : false;

            if (record.lifecycle.OnEventCleanupResult(eventsGone) == CreationStage::Quarantined)
            {
                PublishTerminal(record, CreationPollStatus::Quarantined,
                    "event cleanup failed after metadata failure", onTerminal);
                return;
            }

            if (ops.deleteCharacter)
                ops.deleteCharacter(record.guid, record.accountId);
            if (!ops.requestCleanupVerify || !ops.requestCleanupVerify(record.guid))
                StoreRecordOutcome(record.guid, RowVerifyOutcome::QueryFailed, CreationCallbackKind::CleanupVerify);
        }

        void ProcessBulkCount(uint64_t generation, RowVerifyOutcome outcome, CreationFinalizerOps const& ops)
        {
            auto it = bulkVerifies.find(generation);
            if (it == bulkVerifies.end())
                return; // stale/duplicate generation result: ignored

            uint32_t const accountId = it->second.accountId;

            if (outcome != RowVerifyOutcome::QueryFailed)
            {
                // The execution-ordered readback for THIS generation returned:
                // every save queued before its barrier has executed (success
                // or rollback), so exactly this generation's reservations are
                // released - a rolled-back save occupies nothing, a landed
                // one is in the durable count. Reservations of NEWER
                // generations (saves queued after this barrier) stay
                // untouched until their own barriers complete.
                for (uint32_t i = 0; i < it->second.reservedSlots; ++i)
                    ReleaseAccountSlot(accountId);
                bulkVerifies.erase(it);
                return;
            }

            if (++it->second.attempts >= kMaxBulkVerifyAttempts)
            {
                // Bounded give-up KEEPS this generation's reservations:
                // unknown durable occupancy stays charged (the conservative
                // direction) and the caller logs the stuck capacity.
                if (ops.onBulkVerifyGaveUp)
                    ops.onBulkVerifyGaveUp(accountId, it->second.reservedSlots);
                bulkVerifies.erase(it);
                return;
            }

            // A retry of an old generation may execute after newer saves -
            // harmless, because it still releases only this generation's
            // slots. A failed ENQUEUE feeds back through the mailbox.
            if (!ops.requestBulkCountVerify || !ops.requestBulkCountVerify(accountId, generation))
                StoreBulkOutcome(generation, RowVerifyOutcome::QueryFailed);
        }

        void ProcessCleanupVerify(Record& record, RowVerifyOutcome outcome, CreationFinalizerOps const& ops,
            std::function<void(CreationCompletion const&)> const& onTerminal)
        {
            switch (record.lifecycle.OnCleanupVerify(outcome, kMaxCleanupAttempts))
            {
                case CreationStage::FailedRetryable:
                    PublishTerminal(record, CreationPollStatus::FailedRetryable,
                        "failed after metadata errors; character and events verifiably removed", onTerminal);
                    break;
                case CreationStage::PendingCleanup:
                    // Still present or unknown: re-issue the (idempotent)
                    // deletion and verify again, bounded by the lifecycle.
                    if (outcome == RowVerifyOutcome::Verified && ops.deleteCharacter)
                        ops.deleteCharacter(record.guid, record.accountId);
                    if (ops.requestCleanupVerify)
                        ops.requestCleanupVerify(record.guid);
                    break;
                case CreationStage::Quarantined:
                    PublishTerminal(record, CreationPollStatus::Quarantined,
                        "cleanup could not be confirmed", onTerminal);
                    break;
                default:
                    break;
            }
        }

        // Publishes the terminal completion (observer first, then the bounded
        // retained copy for pollers), releases the account reservation on
        // CONFIRMED outcomes, and erases the record - except quarantined
        // records, which keep both their registry slot and their reservation
        // forever: their durable state is unknown, so the capacity they may
        // occupy stays charged.
        void PublishTerminal(Record& record, CreationPollStatus status, std::string message,
            std::function<void(CreationCompletion const&)> const& onTerminal)
        {
            CreationCompletion completion;
            completion.token = record.token;
            completion.guid = record.guid;
            completion.accountId = record.accountId;
            completion.status = status;
            completion.name = record.name;
            completion.message = std::move(message);

            if (onTerminal)
                onTerminal(completion);

            if (completions.size() >= kMaxCompletions)
                completions.erase(completions.begin());
            completions[completion.token] = StoredCompletion{ completion, pumpCount + kCompletionRetentionPumps };

            if (status == CreationPollStatus::Quarantined)
                return;

            if (record.holdsAccountReservation)
                ReleaseAccountSlot(record.accountId);

            tokenToGuid.erase(record.token);
            records.erase(record.guid);
        }

        void PruneExpiredCompletions()
        {
            for (auto it = completions.begin(); it != completions.end();)
            {
                if (it->second.expiresAtPump <= pumpCount)
                    it = completions.erase(it);
                else
                    ++it;
            }
        }

        struct BulkVerify
        {
            uint32_t accountId = 0;
            uint32_t reservedSlots = 0;
            uint32_t attempts = 0;
            // Per-generation callback mailbox (same losslessness rule as the
            // per-record mailboxes).
            bool hasPendingOutcome = false;
            RowVerifyOutcome pendingOutcome = RowVerifyOutcome::QueryFailed;
        };

        std::map<uint32_t, Record> records;        // by character GUID
        std::map<uint64_t, uint32_t> tokenToGuid;  // erased with the record
        std::map<uint64_t, BulkVerify> bulkVerifies; // by GENERATION token
        std::map<uint64_t, StoredCompletion> completions;
        std::map<uint32_t, uint32_t> reservedSlots; // account -> reserved character slots
        uint64_t nextToken = 1;
        uint64_t pumpCount = 0;
    };
}
