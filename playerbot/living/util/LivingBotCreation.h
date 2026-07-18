#pragma once

#include "LivingEventSchema.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace living
{
    // Validates every event value bot creation may later persist against the
    // real schema limits BEFORE any account allocation, GUID allocation, or
    // SaveToDB. Returns "" when everything fits, otherwise the name of the
    // offending field: an oversized value used to be rejected only after the
    // character was already persisted, leaving a permanent bot without its
    // required grouping/gear/test metadata while creation reported success.
    inline std::string FindOversizedCreationValue(std::string const& gear, std::string const& groupWith,
        std::string const& testName, std::string const& temporaryName)
    {
        if (!EventValueFitsSchema("create gear", gear))
            return "gear";
        if (!EventValueFitsSchema("create group", groupWith))
            return "group";
        if (!EventValueFitsSchema("test", testName))
            return "test";
        if (!EventValueFitsSchema("temporary", temporaryName))
            return "name";

        return "";
    }

    // Typed outcome of one bot-creation attempt. Group creation must decide
    // success from THIS value, never by matching substrings in accumulated chat
    // messages: message sniffing counted later failures as created after one
    // success and never counted successes after one failure.
    enum class BotCreateStatus
    {
        // A character was created and persisted; a GUID is available.
        Created,
        // The character transaction was QUEUED and passed every synchronous
        // gate; durability is confirmed asynchronously by the creation
        // finalizer. No GUID is exposed, nothing is counted as persisted, and
        // required metadata is written only after the durable row is verified.
        PendingPersistence,
        // This attempt failed for a reason another attempt may avoid
        // (e.g. a random name collision). The caller may try again.
        RetryableFailure,
        // The database is transiently unavailable (a count/selection query
        // failed). NOT terminal - capacity is unknown, not exhausted - and
        // NOT same-call retryable: hammering the database inside the failing
        // call amplifies the outage. Coordinators may retry only with
        // bounded tick-based backoff.
        TransientFailure,
        // No further attempt can succeed (invalid arguments, no account
        // capacity, infrastructure error). The caller must stop retrying.
        TerminalFailure,
    };

    // Per-run accounting for group creation. Persisted members and pending
    // members are DIFFERENT counts: a pending creation fills a composition
    // slot for planning (its class/role were verified before the character
    // transaction was queued) but is never reported as a persisted member -
    // the finalizer confirms or discards it asynchronously.
    struct GroupCreationLedger
    {
        uint32_t created = 0;
        uint32_t pendingPersistence = 0;
        std::map<uint8_t, uint32_t> createdByClass;

        // Records one attempt with the planned/persisted class. Returns true
        // when the run must stop (terminal failure, or a transient database
        // failure - continuing the loop would hammer the unavailable DB with
        // synchronous queries inside one call).
        bool Record(BotCreateStatus status, uint8_t actualClass = 0)
        {
            if (status == BotCreateStatus::Created)
            {
                ++created;
                ++createdByClass[actualClass];
            }
            else if (status == BotCreateStatus::PendingPersistence)
            {
                ++pendingPersistence;
                ++createdByClass[actualClass];
            }

            return status == BotCreateStatus::TerminalFailure
                || status == BotCreateStatus::TransientFailure;
        }

        // A slot is consumed for composition PLANNING by both confirmed and
        // pending members; only `created` may ever be claimed as persisted.
        static bool CountsTowardComposition(BotCreateStatus status)
        {
            return status == BotCreateStatus::Created || status == BotCreateStatus::PendingPersistence;
        }
    };

    // One reshuffled fixed-count fill sweep's outcome: how many characters it
    // queued, and whether a shared-slot reservation was refused during it.
    struct AccountFillRound
    {
        uint32_t created = 0;
        bool reservationBlocked = false;
    };

    struct AccountFillResult
    {
        uint32_t created = 0;
        bool reservationBlocked = false;
    };

    // Pure driver for ONE account's fixed-count bulk fill. `runRound` performs a
    // single reshuffled sweep, queuing up to `allowance` characters and reporting
    // whether a shared reservation was refused. The fill terminates when the cap
    // is reached, a reservation is refused, OR a whole round makes no progress.
    //
    // The production nested loop broke only the INNER sweep on a refused
    // reservation, so once the shared ledger blocked a slot that the durable-only
    // maxAllowed still counted as free, the OUTER loop respun forever with no
    // progress - hanging the world thread. Terminating on any non-Reserved
    // outcome (and on a zero-progress round) fixes that at the root.
    template <typename RunRoundFn>
    inline AccountFillResult FillAccount(uint32_t maxAllowed, RunRoundFn&& runRound)
    {
        AccountFillResult result;
        while (result.created < maxAllowed)
        {
            AccountFillRound const round = runRound(maxAllowed - result.created);
            result.created += round.created;
            if (round.reservationBlocked)
            {
                result.reservationBlocked = true;
                break;
            }
            if (round.created == 0)
                break; // no progress this round: stop instead of respinning forever
        }
        return result;
    }

    // Outcome planning for one requested-group join attempt at bot login. The
    // `create group` event is cleared ONLY on verified group membership or a
    // deliberately terminal outcome (deleted target, retry budget exhausted).
    // Offline targets, temporarily full groups and failed/declined invites keep
    // the event for a later bounded retry - the join result is never ignored
    // and the event never cleared unconditionally.
    enum class GroupJoinDecision
    {
        // Keep the event and retry after the backoff delay.
        RetryLater,
        // Membership was verified: clear the event as completed work.
        ClearJoined,
        // No later attempt can succeed (deleted target) or the retry budget is
        // exhausted: clear the event deliberately.
        ClearTerminal,
    };

    struct GroupJoinPlan
    {
        GroupJoinDecision decision = GroupJoinDecision::RetryLater;
        uint32_t attemptNumber = 0;     // the attempt just consumed (1-based)
        uint32_t retryDelaySeconds = 0; // backoff before the next attempt
    };

    // Typed existence of a group-join target. The core lookups
    // (GetPlayerAccountIdByGUID / GetPlayerGuidByName) return zero/empty for BOTH
    // "row absent" and "query failed", so a bool cannot express "unknown": a
    // COUNT-first probe (which yields exactly one row on success) separates a
    // confirmed absence from an unanswerable one.
    enum class TargetExistence
    {
        Found,           // COUNT > 0: the target row exists
        ConfirmedMissing,// COUNT succeeded and returned zero: really deleted
        Unavailable,     // the COUNT query itself failed: existence is UNKNOWN
    };

    inline TargetExistence ClassifyTargetExistence(std::optional<uint64_t> count)
    {
        if (!count)
            return TargetExistence::Unavailable;
        return *count > 0 ? TargetExistence::Found : TargetExistence::ConfirmedMissing;
    }

    inline GroupJoinPlan PlanGroupJoinAttempt(TargetExistence existence, bool targetOnline, bool membershipVerified,
        uint32_t attemptsSoFar, uint32_t maxAttempts, uint32_t baseDelaySeconds)
    {
        GroupJoinPlan plan;

        // A database outage cannot be distinguished from a real deletion by the
        // core lookups, so it must NEVER terminally clear the join intent: retry
        // later WITHOUT consuming the attempt budget or clearing the marker.
        if (existence == TargetExistence::Unavailable)
        {
            plan.decision = GroupJoinDecision::RetryLater;
            plan.attemptNumber = attemptsSoFar;        // budget NOT consumed
            plan.retryDelaySeconds = baseDelaySeconds;  // short fixed backoff
            return plan;
        }

        plan.attemptNumber = attemptsSoFar + 1;

        if (existence == TargetExistence::ConfirmedMissing)
        {
            plan.decision = GroupJoinDecision::ClearTerminal;
            return plan;
        }

        if (targetOnline && membershipVerified)
        {
            plan.decision = GroupJoinDecision::ClearJoined;
            return plan;
        }

        if (plan.attemptNumber >= maxAttempts)
        {
            plan.decision = GroupJoinDecision::ClearTerminal;
            return plan;
        }

        plan.decision = GroupJoinDecision::RetryLater;
        // ponytail: linear backoff; switch to exponential if login passes ever
        // measurably spam.
        plan.retryDelaySeconds = baseDelaySeconds * plan.attemptNumber;
        return plan;
    }

    // Consumes one unit of a role/class quota, refusing to wrap: decrementing a
    // zero quota corrupted the remaining allowance for the whole run. Returns
    // whether a unit was actually consumed.
    inline bool TryConsumeQuota(uint32_t& counter)
    {
        if (counter == 0)
            return false;

        --counter;
        return true;
    }

    // Streamed minimum selection with an explicit "nothing selected yet" state.
    // Replaces the -1.0f sentinel idiom where every nonnegative distance compared
    // as "not closer" and no candidate could ever be selected.
    class MinimumTracker
    {
    public:
        // Returns true when `value` becomes the new minimum.
        bool Consider(float value)
        {
            if (selected && value >= best)
                return false;

            best = value;
            selected = true;
            return true;
        }

        bool HasSelection() const { return selected; }
        float Best() const { return best; }

    private:
        float best = 0.0f;
        bool selected = false;
    };

    // Minimum selection restricted to one map: candidates on any other map are
    // never considered, no matter how close their raw coordinates compare.
    // Cross-map squared distances are meaningless, and ranking every inn with
    // them could persist a homebind on the wrong continent.
    class MapLocalMinimum
    {
    public:
        explicit MapLocalMinimum(uint32_t wantedMapId) : mapId(wantedMapId) {}

        // Returns true when the candidate is on the wanted map AND becomes the
        // new minimum.
        bool Consider(uint32_t candidateMapId, float value)
        {
            if (candidateMapId != mapId)
                return false;

            return tracker.Consider(value);
        }

        bool HasSelection() const { return tracker.HasSelection(); }

    private:
        MinimumTracker tracker;
        uint32_t mapId;
    };

    // Weighted selection with an EXCLUSIVE draw: draw must be < sum(weights),
    // zero-weight entries are never chosen, and every unit of weight maps to
    // exactly one entry. (The legacy samplers drew urand(0, total) inclusively,
    // so one draw in total+1 fell through every bucket into a fallback that
    // ignored the filters.) Returns false for an empty/all-zero weight set or
    // an out-of-range draw.
    bool PickWeightedIndex(std::vector<uint32_t> const& weights, uint64_t draw, size_t& outIndex);

    // Unbiased draw in [0, boundExclusive) assembled from full-range 32-bit
    // draws (rejection sampling: only the residue-complete prefix of the
    // 64-bit space is accepted, so every value keeps exactly equal
    // probability). `rand32` must return uniform uint32 over the full range.
    template <typename RandFn>
    uint64_t DrawBounded64(uint64_t boundExclusive, RandFn&& rand32)
    {
        if (boundExclusive <= 1)
            return 0;

        // Largest multiple of the bound representable in 64 bits; draws at or
        // above it are rejected instead of folding unevenly into the residues.
        uint64_t const acceptBelow = (UINT64_MAX / boundExclusive) * boundExclusive;
        for (;;)
        {
            uint64_t const hi = rand32();
            uint64_t const lo = rand32();
            uint64_t const r = (hi << 32) | lo;
            if (r < acceptBelow)
                return r % boundExclusive;
        }
    }

    // The production weighted-tuple selector: sums the weights in uint64 (a
    // custom-weight total above UINT32_MAX used to be truncated before urand),
    // draws unbiased over the full 64-bit total, and picks the bucket. Returns
    // false for an empty/all-zero weight set.
    template <typename RandFn>
    bool PickWeightedIndex64(std::vector<uint32_t> const& weights, RandFn&& rand32, size_t& outIndex)
    {
        uint64_t total = 0;
        for (uint32_t const weight : weights)
            total += weight;

        if (total == 0)
            return false;

        return PickWeightedIndex(weights, DrawBounded64(total, rand32), outIndex);
    }
}
