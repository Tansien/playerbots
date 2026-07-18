#pragma once

#include "LivingCreationFinalizer.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace living
{
    // Asynchronous completion propagation for group/test creation runs.
    //
    // HandleGroup (and the mgroup test command) enqueue several creations
    // whose durability is confirmed later by the creation finalizer. Without a
    // batch record the per-run ledger died with the call: a member whose save
    // rolled back or whose metadata failed was only ever logged, the requested
    // group stayed silently undersized, and the test DSL had nothing to poll.
    //
    // A CreationBatch tracks every pending member token of one run. The
    // finalizer's terminal completions are fed to OnCreationTerminal BEFORE
    // the finalizer removes the creation record; a retryable member failure
    // returns a replacement plan (bounded by the batch retry budget) carrying
    // the reserved role/class of the failed slot, and terminal failures are
    // recorded for the initiator/test surface instead of vanishing.

    struct CreationBatchMember
    {
        uint64_t creationToken = 0;
        uint8_t cls = 0;  // planned class of the reserved composition slot
        uint8_t role = 0; // planned role (ai::BotRoles) of the reserved slot
    };

    // What the production caller owes after feeding one terminal completion.
    struct BatchMemberResolution
    {
        uint64_t batchToken = 0;
        // Set when the failed member should be replaced: the freed slot's
        // planned role/class travel back so the replacement targets the SAME
        // composition slot instead of rerolling the whole plan.
        bool enqueueReplacement = false;
        uint8_t cls = 0;
        uint8_t role = 0;
        // Human-readable failure to surface to the initiator/test queue
        // (empty for successful members).
        std::string failure;
    };

    enum class BatchPollStatus
    {
        Unknown,  // token never issued or already acknowledged/expired
        Pending,  // members still awaiting terminal creation results
        Complete, // every member reached a terminal result
    };

    struct BatchPollResult
    {
        BatchPollStatus status = BatchPollStatus::Unknown;
        uint32_t desiredSize = 0;
        uint32_t preexistingMembers = 0;
        std::vector<uint32_t> finalizedGuids;
        std::vector<std::string> failures;
        // Desired-size invariant, computed at poll time for COMPLETE batches:
        // preexisting + finalized members fell short of the requested size.
        // This catches shortfalls the failure list alone can miss (the
        // initial run stopping on a terminal error or maxTries exhaustion
        // before enough members were even queued) - a partial group must
        // never report success.
        bool undersized = false;
    };

    class CreationBatchRegistry
    {
    public:
        struct Batch
        {
            uint64_t token = 0;
            std::string initiatorName; // master name or test identity
            uint32_t initiatorGuid = 0; // 0 for console/test batches
            uint32_t desiredSize = 0;
            uint32_t preexistingMembers = 0;
            std::vector<CreationBatchMember> pending;
            std::vector<uint32_t> finalizedGuids;
            uint32_t replacementBudget = 0;
            std::vector<std::string> failures;
            // Creation parameters replacements must reuse (a replacement joins
            // the SAME run, not a fresh default one).
            std::string memberGear;
            bool memberAutoAdd = false;
            bool memberTemporary = false;
            // Allocation mode of the ORIGINAL run: replacements must allocate
            // on the same holder (random-account manager vs the initiator's
            // personal manager) - a retry must never silently move a random
            // group member onto the master's personal account.
            bool useRandomAccounts = false;
            // Set once the completion summary has been surfaced.
            bool completionReported = false;
        };

        static constexpr size_t kMaxBatches = 16;
        // Completed batches are retained this many pumps for pollers that
        // never acknowledge.
        static constexpr uint64_t kRetentionPumps = 4096;

        // Registers a batch. Returns 0 when the bounded registry is full (the
        // caller reports the run as unmonitored rather than dropping members
        // silently).
        uint64_t Begin(Batch batch)
        {
            if (batches.size() >= kMaxBatches)
                return 0;

            batch.token = nextToken++;
            uint64_t const token = batch.token;
            batches[token] = std::move(batch);
            return token;
        }

        // Adds a (replacement) pending member to a live batch.
        bool AddPendingMember(uint64_t batchToken, CreationBatchMember member)
        {
            auto it = batches.find(batchToken);
            if (it == batches.end())
                return false;

            it->second.pending.push_back(member);
            return true;
        }

        // Feeds one finalizer terminal completion. Returns the owed
        // resolution when the token belonged to a live batch.
        std::optional<BatchMemberResolution> OnCreationTerminal(CreationCompletion const& completion)
        {
            for (auto& [token, batch] : batches)
            {
                for (auto memberIt = batch.pending.begin(); memberIt != batch.pending.end(); ++memberIt)
                {
                    if (memberIt->creationToken != completion.token)
                        continue;

                    BatchMemberResolution resolution;
                    resolution.batchToken = token;
                    resolution.cls = memberIt->cls;
                    resolution.role = memberIt->role;
                    batch.pending.erase(memberIt);

                    switch (completion.status)
                    {
                        case CreationPollStatus::Created:
                            batch.finalizedGuids.push_back(completion.guid);
                            break;
                        case CreationPollStatus::FailedRetryable:
                            // The planned slot is released; replace it while
                            // the bounded budget lasts, otherwise record the
                            // shortfall for the initiator.
                            if (batch.replacementBudget > 0)
                            {
                                --batch.replacementBudget;
                                resolution.enqueueReplacement = true;
                            }
                            else
                                batch.failures.push_back("member '" + completion.name
                                    + "' failed retryably and the replacement budget is exhausted: " + completion.message);
                            break;
                        default:
                            batch.failures.push_back("member '" + completion.name
                                + "' failed terminally: " + completion.message);
                            break;
                    }

                    resolution.failure = resolution.enqueueReplacement || completion.status == CreationPollStatus::Created
                        ? "" : batch.failures.back();
                    return resolution;
                }
            }

            return std::nullopt;
        }

        // A batch is complete when no member is pending. (A replacement owed
        // by a just-fed resolution must be enqueued - AddPendingMember - before
        // the next completeness check; OnCreationTerminal and the replacement
        // run in the same pump step.)
        BatchPollResult Poll(uint64_t batchToken, bool acknowledgeComplete)
        {
            BatchPollResult result;
            auto it = batches.find(batchToken);
            if (it == batches.end())
                return result;

            Batch const& batch = it->second;
            result.status = batch.pending.empty() ? BatchPollStatus::Complete : BatchPollStatus::Pending;
            result.desiredSize = batch.desiredSize;
            result.preexistingMembers = batch.preexistingMembers;
            result.finalizedGuids = batch.finalizedGuids;
            result.failures = batch.failures;
            result.undersized = result.status == BatchPollStatus::Complete
                && batch.preexistingMembers + batch.finalizedGuids.size() < batch.desiredSize;

            if (result.status == BatchPollStatus::Complete && acknowledgeComplete)
                batches.erase(it);

            return result;
        }

        // Retrieves initiator identity for surfacing failures.
        Batch const* Find(uint64_t batchToken) const
        {
            auto it = batches.find(batchToken);
            return it == batches.end() ? nullptr : &it->second;
        }

        // Outstanding group slots already owned by this initiator's batches:
        // pending creations PLUS finalized members that have not yet joined
        // the group (`isJoined(guid)` decides). A repeated group command must
        // count these as effective membership - a finalized-but-not-joined
        // member still occupies its slot, and only verified membership (or
        // batch expiry, the bounded terminal path for joins that never
        // happen) releases it.
        template <typename JoinedFn>
        uint32_t OutstandingSlotsForInitiator(uint32_t initiatorGuid, JoinedFn&& isJoined) const
        {
            uint32_t outstanding = 0;
            for (auto const& [token, batch] : batches)
            {
                if (batch.initiatorGuid != initiatorGuid || initiatorGuid == 0)
                    continue;

                outstanding += static_cast<uint32_t>(batch.pending.size());
                for (uint32_t const guid : batch.finalizedGuids)
                    if (!isJoined(guid))
                        ++outstanding;
            }

            return outstanding;
        }

        // The initiator's most recent still-tracked batch (0 when none): a
        // repeated command coalesces onto it instead of enqueueing a second
        // deficit.
        uint64_t FindBatchTokenForInitiator(uint32_t initiatorGuid) const
        {
            uint64_t newest = 0;
            for (auto const& [token, batch] : batches)
                if (initiatorGuid != 0 && batch.initiatorGuid == initiatorGuid && token > newest)
                    newest = token;
            return newest;
        }

        // Records a failure produced OUTSIDE the finalizer (immediate
        // replacement-creation failure, initiator offline).
        bool RecordFailure(uint64_t batchToken, std::string failure)
        {
            auto it = batches.find(batchToken);
            if (it == batches.end())
                return false;

            it->second.failures.push_back(std::move(failure));
            return true;
        }

        // Batches whose members all reached terminal results and whose
        // completion has not been surfaced yet. Each batch is returned exactly
        // once; the requested group is never left silently undersized.
        std::vector<Batch const*> TakeNewlyCompleted()
        {
            std::vector<Batch const*> completed;
            for (auto& [token, batch] : batches)
            {
                if (batch.pending.empty() && !batch.completionReported)
                {
                    batch.completionReported = true;
                    completed.push_back(&batch);
                }
            }
            return completed;
        }

        // Drops completed batches nobody acknowledged within the retention
        // window (bounded memory; pumpCount is supplied by the caller's pump).
        void PruneExpired(uint64_t pumpCount)
        {
            for (auto it = batches.begin(); it != batches.end();)
            {
                Batch& batch = it->second;
                if (batch.pending.empty())
                {
                    if (completedAtPump.find(it->first) == completedAtPump.end())
                        completedAtPump[it->first] = pumpCount;
                    if (completedAtPump[it->first] + kRetentionPumps <= pumpCount)
                    {
                        completedAtPump.erase(it->first);
                        it = batches.erase(it);
                        continue;
                    }
                }
                ++it;
            }

            for (auto it = completedAtPump.begin(); it != completedAtPump.end();)
            {
                if (batches.find(it->first) == batches.end())
                    it = completedAtPump.erase(it);
                else
                    ++it;
            }
        }

        size_t BatchCount() const { return batches.size(); }

    private:
        std::map<uint64_t, Batch> batches;
        std::map<uint64_t, uint64_t> completedAtPump;
        uint64_t nextToken = 1;
    };
}
