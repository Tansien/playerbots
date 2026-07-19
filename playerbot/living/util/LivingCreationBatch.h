#pragma once

#include "LivingBotCreation.h"
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
    // HandleGroup (and the mgroup test command) plan several member slots
    // whose creations are confirmed later by the creation finalizer. Exactly
    // ONE batch owns a target group's deficit at a time: a repeated group
    // command extends/coalesces onto the initiator's existing batch (or is
    // refused when its options are incompatible) - it never creates an
    // independent residual batch that could report success using members
    // owned by an older batch that later fails.
    //
    // Each planned slot is a stateful member:
    //   AwaitingAttempt    - planned slot, no creation token yet (initial
    //                        transient shortfall, or a replacement); retried
    //                        with bounded tick-based pacing;
    //   PendingPersistence - a creation token is outstanding;
    //   Finalized          - the character exists; the slot stays OWNED until
    //                        its verified group join (or batch expiry, the
    //                        bounded terminal path for joins that never
    //                        happen);
    //   TerminalFailure    - recorded, surfaced, never silently shrunk away.
    //
    // Transient database unavailability keeps the slot in AwaitingAttempt and
    // consumes its own bounded transient budget - never the semantic
    // replacement budget, and never a terminal record until that budget is
    // exhausted.

    enum class BatchMemberState
    {
        AwaitingAttempt,
        PendingPersistence,
        Finalized,
        TerminalFailure,
    };

    struct CreationBatchMember
    {
        uint64_t creationToken = 0; // nonzero only while PendingPersistence
        uint8_t cls = 0;  // planned class (0 = drawn at attempt time)
        uint8_t role = 0; // planned role (ai::BotRoles; 0 = drawn at attempt time)
        BatchMemberState state = BatchMemberState::PendingPersistence;
        uint32_t finalizedGuid = 0;  // set when Finalized
        uint32_t transientRetries = 0;
        uint64_t nextAttemptTick = 0; // pump tick pacing while AwaitingAttempt
    };

    enum class BatchPollStatus
    {
        Unknown,  // token never issued or already acknowledged/expired
        Pending,  // members still awaiting attempts or terminal results
        Complete, // every member is Finalized or TerminalFailure
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
        // This catches shortfalls the failure list alone can miss - a partial
        // group must never report success.
        bool undersized = false;
    };

    // A creation attempt the production pump owes for one AwaitingAttempt
    // member whose pacing delay elapsed.
    struct DueAttempt
    {
        uint64_t batchToken = 0;
        size_t memberIndex = 0;
        uint8_t cls = 0;
        uint8_t role = 0;
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
            uint32_t preexistingMembers = 0; // LIVE members when the batch began
            std::vector<CreationBatchMember> members;
            uint32_t replacementBudget = 0;
            std::vector<std::string> failures;
            // Creation parameters every attempt of this batch must reuse (a
            // replacement or extension joins the SAME run, not a fresh one).
            std::string memberGear;
            bool memberAutoAdd = false;
            bool memberTemporary = false;
            // Allocation mode of the ORIGINAL run: attempts must allocate on
            // the same holder (random-account manager vs the initiator's
            // personal manager).
            bool useRandomAccounts = false;
            // Set once the completion summary has been surfaced.
            bool completionReported = false;
        };

        static constexpr size_t kMaxBatches = 16;
        // Completed batches are retained this many pumps for pollers that
        // never acknowledge; expiry is also the bounded terminal path for
        // finalized members whose group join never completes.
        static constexpr uint64_t kRetentionPumps = 4096;
        // Bounded transient-unavailability budget per member slot, paced.
        static constexpr uint32_t kMaxTransientAttempts = 60;
        static constexpr uint64_t kTransientBackoffPumps = 100; // ~5s of ticks

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

        // Adds a pending-persistence member (initial run and extensions).
        bool AddPendingMember(uint64_t batchToken, CreationBatchMember member)
        {
            auto it = batches.find(batchToken);
            if (it == batches.end())
                return false;

            member.state = BatchMemberState::PendingPersistence;
            it->second.members.push_back(member);
            return true;
        }

        // Adds `count` planned-but-unattempted slots (initial run stopped on
        // a transient failure, or an extension's residual deficit while the
        // database is unavailable). They are attempted from the pump with
        // pacing.
        bool AddPlannedSlots(uint64_t batchToken, uint32_t count, uint64_t pumpCount)
        {
            auto it = batches.find(batchToken);
            if (it == batches.end())
                return false;

            for (uint32_t i = 0; i < count; ++i)
            {
                CreationBatchMember slot;
                slot.state = BatchMemberState::AwaitingAttempt;
                slot.nextAttemptTick = pumpCount + kTransientBackoffPumps;
                it->second.members.push_back(slot);
            }

            return true;
        }

        // Raises the batch's target size (a compatible repeated request
        // extends the EXISTING batch; it never shrinks). Growing the target
        // also grants one replacement per ADDED slot: the strictly-greater
        // guard makes the delta positive, so replacementBudget keeps pace with
        // desiredSize (one replacement per requested slot) instead of leaving
        // the added slots to share the original run's budget.
        //
        // The live-membership baseline is refreshed from the extension's OWN
        // observation: unrelated members who joined since Begin used to stay
        // uncounted (the stale baseline made completion report a false
        // undersize). The batch's own already-joined finalized members are
        // subtracted so they are never counted both as preexisting AND as
        // finalized in the completion invariant.
        template <typename JoinedFn>
        bool ExtendDesiredSize(uint64_t batchToken, uint32_t desiredSize, uint32_t liveMembers,
            JoinedFn&& isJoined)
        {
            auto it = batches.find(batchToken);
            if (it == batches.end())
                return false;

            Batch& batch = it->second;

            uint32_t ownJoined = 0;
            for (CreationBatchMember const& member : batch.members)
                if (member.state == BatchMemberState::Finalized && isJoined(member.finalizedGuid))
                    ++ownJoined;
            batch.preexistingMembers = liveMembers > ownJoined ? liveMembers - ownJoined : 0;

            if (desiredSize > batch.desiredSize)
            {
                batch.replacementBudget += desiredSize - batch.desiredSize;
                batch.desiredSize = desiredSize;
            }
            return true;
        }

        // Feeds one finalizer terminal completion. The member transitions in
        // place: Created -> Finalized (slot stays owned until verified join);
        // FailedRetryable -> back to AwaitingAttempt within the replacement
        // budget (the slot's planned class/role are retained), else a
        // recorded terminal failure; terminal/quarantined -> recorded
        // failure. Returns whether a batch consumed the completion.
        bool OnCreationTerminal(CreationCompletion const& completion, uint64_t pumpCount)
        {
            for (auto& [token, batch] : batches)
            {
                for (CreationBatchMember& member : batch.members)
                {
                    if (member.state != BatchMemberState::PendingPersistence
                        || member.creationToken != completion.token)
                        continue;

                    member.creationToken = 0;
                    switch (completion.status)
                    {
                        case CreationPollStatus::Created:
                            member.state = BatchMemberState::Finalized;
                            member.finalizedGuid = completion.guid;
                            break;
                        case CreationPollStatus::FailedRetryable:
                            if (batch.replacementBudget > 0)
                            {
                                --batch.replacementBudget;
                                member.state = BatchMemberState::AwaitingAttempt;
                                member.nextAttemptTick = pumpCount + 1;
                            }
                            else
                            {
                                member.state = BatchMemberState::TerminalFailure;
                                batch.failures.push_back("member '" + completion.name
                                    + "' failed retryably and the replacement budget is exhausted: " + completion.message);
                            }
                            break;
                        default:
                            member.state = BatchMemberState::TerminalFailure;
                            batch.failures.push_back("member '" + completion.name
                                + "' failed terminally: " + completion.message);
                            break;
                    }

                    return true;
                }
            }

            return false;
        }

        // Members whose paced attempt is due this pump. The caller performs
        // each attempt synchronously and reports through OnAttemptResult.
        std::vector<DueAttempt> TakeDueAttempts(uint64_t pumpCount)
        {
            std::vector<DueAttempt> due;
            for (auto& [token, batch] : batches)
            {
                for (size_t i = 0; i < batch.members.size(); ++i)
                {
                    CreationBatchMember& member = batch.members[i];
                    if (member.state != BatchMemberState::AwaitingAttempt || member.nextAttemptTick > pumpCount)
                        continue;

                    // Re-armed by OnAttemptResult; prevents double dispatch if
                    // the caller skips an attempt.
                    member.nextAttemptTick = pumpCount + kTransientBackoffPumps;
                    due.push_back(DueAttempt{ token, i, member.cls, member.role });
                }
            }

            return due;
        }

        // Reports one attempt's typed creation result for an AwaitingAttempt
        // member. Transient failures keep the slot with paced, bounded
        // retries and never consume the replacement budget; retryable
        // failures consume it; terminal failures are recorded.
        void OnAttemptResult(uint64_t batchToken, size_t memberIndex, BotCreateStatus status,
            uint64_t creationToken, uint8_t actualClass, uint64_t pumpCount, uint8_t actualRole = 0)
        {
            auto it = batches.find(batchToken);
            if (it == batches.end() || memberIndex >= it->second.members.size())
                return;

            Batch& batch = it->second;
            CreationBatchMember& member = batch.members[memberIndex];
            if (member.state != BatchMemberState::AwaitingAttempt)
                return;

            switch (status)
            {
                case BotCreateStatus::PendingPersistence:
                    member.state = BatchMemberState::PendingPersistence;
                    member.creationToken = creationToken;
                    member.cls = actualClass;
                    // Persist the role the pump drew/verified for this slot so
                    // quota accounting (which skips role-0 outstanding members)
                    // charges the right role and a later retry/extension keeps
                    // it. A 0 never clobbers an already-selected role.
                    if (actualRole)
                        member.role = actualRole;
                    member.transientRetries = 0;
                    break;
                case BotCreateStatus::TransientFailure:
                    if (++member.transientRetries >= kMaxTransientAttempts)
                    {
                        member.state = BatchMemberState::TerminalFailure;
                        batch.failures.push_back("member slot abandoned: database unavailable through the bounded retry budget");
                    }
                    else
                        member.nextAttemptTick = pumpCount + kTransientBackoffPumps;
                    break;
                case BotCreateStatus::RetryableFailure:
                    if (batch.replacementBudget > 0)
                    {
                        --batch.replacementBudget;
                        member.nextAttemptTick = pumpCount + 1;
                    }
                    else
                    {
                        member.state = BatchMemberState::TerminalFailure;
                        batch.failures.push_back("member slot failed retryably and the replacement budget is exhausted");
                    }
                    break;
                case BotCreateStatus::Created: // defensive: creation is normally async
                    member.state = BatchMemberState::Finalized;
                    break;
                case BotCreateStatus::TerminalFailure:
                default:
                    member.state = BatchMemberState::TerminalFailure;
                    batch.failures.push_back("member slot failed terminally at creation");
                    break;
            }
        }

        // A batch is complete when no member awaits an attempt or a terminal
        // creation result. (Finalized members may still owe their group join;
        // the OUTSTANDING accounting below keeps owning those slots.)
        BatchPollResult Poll(uint64_t batchToken, bool acknowledgeComplete)
        {
            BatchPollResult result;
            auto it = batches.find(batchToken);
            if (it == batches.end())
                return result;

            Batch const& batch = it->second;
            bool anyOutstanding = false;
            for (CreationBatchMember const& member : batch.members)
            {
                if (member.state == BatchMemberState::AwaitingAttempt
                    || member.state == BatchMemberState::PendingPersistence)
                    anyOutstanding = true;
                else if (member.state == BatchMemberState::Finalized)
                    result.finalizedGuids.push_back(member.finalizedGuid);
            }

            result.status = anyOutstanding ? BatchPollStatus::Pending : BatchPollStatus::Complete;
            result.desiredSize = batch.desiredSize;
            result.preexistingMembers = batch.preexistingMembers;
            result.failures = batch.failures;
            result.undersized = result.status == BatchPollStatus::Complete
                && batch.preexistingMembers + result.finalizedGuids.size() < batch.desiredSize;

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

        // Outstanding group slots already owned by this initiator's batches:
        // planned/pending creations PLUS finalized members that have not yet
        // joined the group (`isJoined(guid)` decides). A repeated group
        // command must count these as effective membership; only verified
        // membership (or batch expiry) releases a slot.
        template <typename JoinedFn>
        uint32_t OutstandingSlotsForInitiator(uint32_t initiatorGuid, JoinedFn&& isJoined) const
        {
            uint32_t outstanding = 0;
            ForEachOutstandingMember(initiatorGuid, isJoined,
                [&outstanding](CreationBatchMember const&) { ++outstanding; });
            return outstanding;
        }

        // Visits every outstanding member slot (planned, pending, or
        // finalized-but-not-joined) the initiator's batches own - extension
        // runs subtract their classes/roles from the composition quotas so
        // older allocations constrain newer selections.
        template <typename JoinedFn, typename MemberFn>
        void ForEachOutstandingMember(uint32_t initiatorGuid, JoinedFn&& isJoined, MemberFn&& fn) const
        {
            if (initiatorGuid == 0)
                return;

            for (auto const& [token, batch] : batches)
            {
                if (batch.initiatorGuid != initiatorGuid)
                    continue;

                for (CreationBatchMember const& member : batch.members)
                {
                    switch (member.state)
                    {
                        case BatchMemberState::AwaitingAttempt:
                        case BatchMemberState::PendingPersistence:
                            fn(member);
                            break;
                        case BatchMemberState::Finalized:
                            if (!isJoined(member.finalizedGuid))
                                fn(member);
                            break;
                        default:
                            break;
                    }
                }
            }
        }

        // Visits every batch of the initiator that still OWNS at least one
        // outstanding dependency (a planned/pending creation, or a finalized
        // member that has not joined the group). COMPLETED retained batches
        // count: their finalized-unjoined members are credited as effective
        // membership by new requests, so those requests must also honor the
        // options those members were created with.
        template <typename JoinedFn, typename BatchFn>
        void ForEachBatchWithOutstanding(uint32_t initiatorGuid, JoinedFn&& isJoined, BatchFn&& fn) const
        {
            if (initiatorGuid == 0)
                return;

            for (auto const& [token, batch] : batches)
            {
                if (batch.initiatorGuid != initiatorGuid)
                    continue;

                bool outstanding = false;
                for (CreationBatchMember const& member : batch.members)
                {
                    if (member.state == BatchMemberState::AwaitingAttempt
                        || member.state == BatchMemberState::PendingPersistence
                        || (member.state == BatchMemberState::Finalized && !isJoined(member.finalizedGuid)))
                    {
                        outstanding = true;
                        break;
                    }
                }

                if (outstanding)
                    fn(batch);
            }
        }

        // The initiator's most recent ACTIVE batch (0 when none): the single
        // owner of that group's deficit; a repeated command extends it instead
        // of enqueueing a second one. Only a genuinely active batch (some member
        // still awaiting an attempt or pending persistence) is extendable - a
        // completed, merely-retained batch is NOT returned, so a later request
        // starts a FRESH batch with re-armed completion/retention/replacement
        // state instead of reviving one carrying a stale completionReported flag
        // and an expired retention timer. Deficit/quota accounting stays correct
        // because OutstandingSlotsForInitiator/ForEachOutstandingMember still
        // iterate every one of the initiator's batches, including the retained
        // completed one's finalized-but-unjoined members.
        uint64_t FindBatchTokenForInitiator(uint32_t initiatorGuid) const
        {
            if (initiatorGuid == 0)
                return 0;

            uint64_t newest = 0;
            for (auto const& [token, batch] : batches)
            {
                if (batch.initiatorGuid != initiatorGuid || token <= newest)
                    continue;

                bool active = false;
                for (CreationBatchMember const& member : batch.members)
                    if (member.state == BatchMemberState::AwaitingAttempt
                        || member.state == BatchMemberState::PendingPersistence)
                    {
                        active = true;
                        break;
                    }

                if (active)
                    newest = token;
            }
            return newest;
        }

        // Batches whose members all reached terminal creation results and
        // whose completion has not been surfaced yet. Each batch is returned
        // exactly once; the requested group is never left silently
        // undersized.
        std::vector<Batch const*> TakeNewlyCompleted()
        {
            std::vector<Batch const*> completed;
            for (auto& [token, batch] : batches)
            {
                bool anyOutstanding = false;
                for (CreationBatchMember const& member : batch.members)
                    if (member.state == BatchMemberState::AwaitingAttempt
                        || member.state == BatchMemberState::PendingPersistence)
                        anyOutstanding = true;

                if (!anyOutstanding && !batch.completionReported)
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
                bool anyOutstanding = false;
                for (CreationBatchMember const& member : batch.members)
                    if (member.state == BatchMemberState::AwaitingAttempt
                        || member.state == BatchMemberState::PendingPersistence)
                        anyOutstanding = true;

                if (!anyOutstanding)
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

    // Creation options a repeated group request must match while ANY of the
    // initiator's batches still owns outstanding dependencies.
    struct GroupBatchOptions
    {
        std::string gear;
        bool autoAdd = false;
        bool temporary = false;
        bool useRandomAccounts = false;
    };

    enum class GroupBatchRequestAction
    {
        // Outstanding dependencies (possibly from a completed retained batch)
        // were created with DIFFERENT options: refuse with an explicit message
        // instead of silently crediting members the new options never applied
        // to - or silently queueing nothing at all.
        RejectIncompatible,
        // An active batch exists: extend it (same token, shared ledger).
        ExtendExisting,
        // No active batch, and live membership plus credited outstanding
        // dependencies already reach the requested size.
        AlreadyCovered,
        // No active batch: begin a fresh batch whose baseline explicitly
        // credits the outstanding dependencies (creditedPreexisting), so its
        // completion invariant cannot report a false undersize for slots an
        // older batch still owns.
        BeginNew,
    };

    struct GroupBatchRequestPlan
    {
        GroupBatchRequestAction action = GroupBatchRequestAction::BeginNew;
        uint64_t existingToken = 0;      // nonzero only for ExtendExisting
        uint32_t outstandingSlots = 0;   // credited dependencies across ALL batches
        uint32_t effectiveMembers = 0;   // live + outstanding
        uint32_t creditedPreexisting = 0;// BeginNew's preexisting baseline
    };

    // The ONE ownership/accounting rule for a repeated group request. The
    // handler used to check option compatibility only against the ACTIVE
    // batch, while OutstandingSlotsForInitiator still credited a completed
    // retained batch's finalized-unjoined members - so a request with changed
    // options could silently create no batch, ignore the new options, and the
    // fresh batch (when one was made) later reported a false undersize because
    // the credited members belonged to another batch's ledger.
    template <typename JoinedFn>
    GroupBatchRequestPlan PlanGroupBatchRequest(CreationBatchRegistry const& registry,
        uint32_t initiatorGuid, JoinedFn&& isJoined, uint32_t liveMembers,
        uint32_t requestedSize, GroupBatchOptions const& options)
    {
        GroupBatchRequestPlan plan;
        plan.outstandingSlots = registry.OutstandingSlotsForInitiator(initiatorGuid, isJoined);
        plan.effectiveMembers = liveMembers + plan.outstandingSlots;
        plan.creditedPreexisting = plan.effectiveMembers;

        bool incompatible = false;
        registry.ForEachBatchWithOutstanding(initiatorGuid, isJoined,
            [&](CreationBatchRegistry::Batch const& batch)
            {
                if (batch.memberGear != options.gear
                    || batch.memberAutoAdd != options.autoAdd
                    || batch.memberTemporary != options.temporary
                    || batch.useRandomAccounts != options.useRandomAccounts)
                    incompatible = true;
            });

        if (incompatible)
        {
            plan.action = GroupBatchRequestAction::RejectIncompatible;
            return plan;
        }

        plan.existingToken = registry.FindBatchTokenForInitiator(initiatorGuid);
        if (plan.existingToken)
        {
            plan.action = GroupBatchRequestAction::ExtendExisting;
            return plan;
        }

        plan.action = plan.effectiveMembers >= requestedSize
            ? GroupBatchRequestAction::AlreadyCovered
            : GroupBatchRequestAction::BeginNew;
        return plan;
    }
}
