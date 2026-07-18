#pragma once

#include "LivingCreationBatch.h"     // BatchPollResult / BatchPollStatus
#include "LivingCreationFinalizer.h" // CreationPollResult / CreationPollStatus

#include <cstddef>
#include <cstdint>
#include <functional>
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
}
