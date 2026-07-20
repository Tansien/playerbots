#pragma once

#include "LivingEventSchema.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
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

    // What a group-join pass may do to its retry/backoff bookkeeping AFTER
    // seeing the typed result of its durable `create group` write. The backoff
    // clock and the marker consume must advance ONLY on a confirmed write - the
    // legacy code advanced them unconditionally, so a lost/ambiguous write
    // replayed the join work and defeated the bounded retry budget (the durable
    // attempt count never moved).
    struct GroupJoinPersist
    {
        bool advanceBackoff = false; // RetryLater confirmed: arm the normal backoff, budget advanced durably
        bool consumed = false;       // ClearJoined/ClearTerminal confirmed: the marker is gone
        bool holdoff = false;        // write not confirmed: leave durable state as-is, short holdoff, retry later
    };

    inline GroupJoinPersist PlanGroupJoinPersist(GroupJoinDecision decision, EventWriteResult writeResult)
    {
        GroupJoinPersist out;
        bool const confirmed = writeResult == EventWriteResult::DesiredStateConfirmed;
        if (decision == GroupJoinDecision::RetryLater)
        {
            out.advanceBackoff = confirmed;
            out.holdoff = !confirmed;
        }
        else // ClearJoined / ClearTerminal
        {
            out.consumed = confirmed;
            out.holdoff = !confirmed;
        }
        return out;
    }

    // The FINITE set of supported gear values. Creation input is validated
    // against exactly this list BEFORE any account/character mutation: an
    // arbitrary payload used to be accepted (acting as "default"), applied
    // as an effect, and could then exceed the durable event envelope once
    // the phase-2 record metadata was appended - an accepted input must
    // never become impossible to persist.
    inline bool IsSupportedGearValue(std::string const& value)
    {
        static char const* const kSupported[] = {
            "", "default", "empty", "green", "uncommon", "blue", "rare",
            "purple", "epic", "upgrade", "sync", "best", "partial",
        };
        for (char const* supported : kSupported)
            if (value == supported)
                return true;
        return false;
    }

    // Splits a gear payload into its base value and the captured verified
    // group-target level ("sync@70" -> {"sync", 70}). The level suffix is
    // stamped by the post-create pass when the group join VERIFIES, so the
    // dependent gear obligation keeps its target level even after the
    // (only) durable group marker is cleared - recovery must never silently
    // fall back to the bot's own level. Absent/malformed suffix -> level 0.
    inline void SplitGearTarget(std::string const& data, std::string& base, uint32_t& level)
    {
        base = data;
        level = 0;
        size_t const at = data.rfind('@');
        if (at == std::string::npos)
            return;

        uint32_t parsed = 0;
        for (size_t i = at + 1; i < data.size(); ++i)
        {
            char const c = data[i];
            if (c < '0' || c > '9' || parsed > 0xFFFFFFu)
                return; // malformed suffix: treat the whole payload as base
            parsed = parsed * 10 + static_cast<uint32_t>(c - '0');
        }
        if (at + 1 == data.size())
            return;

        base = data.substr(0, at);
        level = parsed;
    }

    inline std::string StampGearTarget(std::string const& base, uint32_t level)
    {
        return base + "@" + std::to_string(level);
    }

    // Reconciliation rule for the transient post-create owner set after a
    // durable marker scan: only a SUCCESSFUL scan is authoritative and
    // replaces the set; a failed scan (database unavailable) keeps every
    // existing owner - unknown state must never remove an owner that still
    // has unsettled durable work.
    template <typename OwnerMap>
    OwnerMap ReconcilePostCreateOwners(OwnerMap const& existing, bool scanSucceeded,
        OwnerMap const& scanned)
    {
        return scanSucceeded ? scanned : existing;
    }

    // Master-derived gear gate for the post-create pass. gear=sync/upgrade is
    // DEFINED relative to the requested group master's level, so it may run
    // only once membership is VERIFIED (the verified master supplies the
    // level) or once the join is terminally settled (no master will ever be
    // confirmed: the effect falls back to the bot's own level). While the
    // join is merely retryable the gear marker is retained un-applied - a
    // failed/full-group attempt's candidate target must never leak its level
    // into the gear effect.
    inline bool MayApplyMasterDerivedGear(bool needsMaster, bool joinSettled, bool masterVerified)
    {
        return !needsMaster || masterVerified || joinSettled;
    }

    // Durable creation-intent lifecycle. The intent row ('create pending') is
    // execution-confirmed BEFORE the character transaction is queued, so a
    // committed character can never exist without a restart-reconstructable
    // creation owner: value 1 = pre-persistence (the finalizer owns it; a
    // present character resumes finalization, an absent one is residue),
    // value 2 = finalized with outstanding post-create obligations (the row
    // carries the lifecycle LOGIN AUTHORIZATION until settlement clears it).
    inline constexpr uint32_t kCreationIntentPrePersistence = 1;
    inline constexpr uint32_t kCreationIntentFinalized = 2;

    // Small fixed-size payload: {level, login authorization, has-obligations}.
    // The heavyweight creation metadata (markers, spec events) is persisted as
    // its own rows pre-save, so this always fits the durable envelope.
    inline std::string EncodeCreationIntent(uint32_t level, bool autoAdd, bool hasObligations)
    {
        return "level:" + std::to_string(level) + "|login:" + (autoAdd ? "1" : "0")
            + "|obligations:" + (hasObligations ? "1" : "0");
    }

    inline void DecodeCreationIntent(std::string const& data, uint32_t& level, bool& autoAdd,
        bool& hasObligations)
    {
        level = 0;
        autoAdd = false;
        hasObligations = false;

        size_t const levelPos = data.find("level:");
        if (levelPos != std::string::npos)
        {
            uint64_t parsed = 0;
            for (size_t i = levelPos + 6; i < data.size() && data[i] >= '0' && data[i] <= '9'; ++i)
            {
                if (parsed > 0xFFFFFFull)
                    break;
                parsed = parsed * 10 + static_cast<uint64_t>(data[i] - '0');
            }
            level = static_cast<uint32_t>(parsed);
        }
        size_t const loginPos = data.find("|login:");
        autoAdd = loginPos != std::string::npos && loginPos + 7 < data.size() && data[loginPos + 7] == '1';
        size_t const obligationsPos = data.find("|obligations:");
        hasObligations = obligationsPos != std::string::npos && obligationsPos + 13 < data.size()
            && data[obligationsPos + 13] == '1';
    }

    // Startup recovery decision for one intent row.
    enum class CreationIntentRecovery
    {
        DiscardResidue,     // character absent: nothing committed (or rolled back)
        ResumeFinalization, // pre-persistence intent, character committed: re-own it
        ReconstructOwner,   // finalized intent: rebuild the post-create owner + login auth
    };

    inline CreationIntentRecovery PlanCreationIntentRecovery(bool characterPresent, uint32_t value)
    {
        if (!characterPresent)
            return CreationIntentRecovery::DiscardResidue;
        return value >= kCreationIntentFinalized ? CreationIntentRecovery::ReconstructOwner
                                                 : CreationIntentRecovery::ResumeFinalization;
    }

    // Direction-aware one-copper commit token: the delta must ALWAYS change
    // the purse, and every pinned core silently clamps a positive change at
    // the money cap - so a saturated purse toggles down instead. Gameplay
    // money is never permanently altered beyond one copper either way.
    inline int32_t CommitTokenDelta(uint32_t money, uint32_t maxMoney)
    {
        return money < maxMoney ? 1 : -1;
    }

    // Login decision for one offline sweep entry: an automatic login needs
    // the MODE to allow it (LOGIN_ONLY_ALWAYS_ACTIVE suppresses sweep logins
    // entirely), the entry's OWN authorization (lifecycle ownership never
    // overrides login=0 - reconstructed owners are authorized only through
    // the always-online membership), and no pending deletion (a
    // deletion-pending character never logs in).
    inline bool MayAutoLoginPostCreateOwner(bool modeAllowsAutoLogin, bool entryMayAutoLogin,
        bool deletionPending)
    {
        return modeAllowsAutoLogin && entryMayAutoLogin && !deletionPending;
    }

    // Scheduler-ownership rule for LoginFreeBots: a bot stays scheduled while
    // it still owns TRANSIENT post-create work (an unknown/typed-untrusted
    // marker read, an unconsumed one-shot marker, an unconfirmed clear, or a
    // group join that has not reached a confirmed terminal persist), even when
    // it is not always-online - removal used to orphan that work forever. The
    // always-online membership itself releases only from a KNOWN, valid state:
    // an unreadable `always` row must not unschedule a possibly-active bot.
    inline bool MayReleasePostCreateOwner(bool alwaysKnown, bool alwaysActive, bool postCreateWorkPending)
    {
        return alwaysKnown && !alwaysActive && !postCreateWorkPending;
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

    // Builds the fixed class/race quota map from FRESH configuration state.
    // The result is assigned wholesale by the config loader on EVERY
    // Initialize, so a reload can never retain an earlier positive entry
    // whose key meanwhile became zero, was removed, became unavailable, or
    // whose fixed mode was toggled off and on. Zero and negative counts mean
    // DISABLED and never enter the map; unavailable combinations are skipped.
    // `getCount(cls, race)` returns the configured value (negative = absent),
    // `isAvailable(cls, race)` the factory availability rule.
    template <typename GetCountFn, typename AvailableFn>
    std::map<std::pair<uint8_t, uint8_t>, uint32_t> BuildFixedClassRaceCounts(
        uint32_t maxClasses, uint32_t maxRaces, GetCountFn&& getCount, AvailableFn&& isAvailable)
    {
        std::map<std::pair<uint8_t, uint8_t>, uint32_t> counts;
        for (uint32_t cls = 1; cls < maxClasses; ++cls)
        {
            for (uint32_t race = 1; race < maxRaces; ++race)
            {
                int const count = getCount(cls, race);
                if (count > 0 && isAvailable(cls, race))
                    counts[{ static_cast<uint8_t>(cls), static_cast<uint8_t>(race) }] =
                        static_cast<uint32_t>(count);
            }
        }
        return counts;
    }

    // Outcome of one attempt inside a fixed-count sweep, reported by the
    // production reserve+create callable.
    enum class FixedCountAttempt
    {
        Created,            // reserved and queued: consume one unit of quota
        CreateFailed,       // reservation released, nothing queued: quota kept
        ReservationBlocked, // shared slot refused: terminate this round
        Skipped,            // combination not attemptable (invalid class etc.)
    };

    // One reshuffled sweep of a fixed class/race fill: walks `keys` (the
    // caller's shuffle order) against the shared `remaining` quota map,
    // attempting up to `allowance` creations. Zero-count entries are DISABLED:
    // they are dropped without reserving or creating anything - the raw
    // `--remaining[key]` this replaces wrapped a zero to UINT_MAX and
    // mass-created the disabled combination. Consumption is checked
    // (TryConsumeQuota) and an exhausted entry is erased so the end-of-run
    // shortfall report never lists it.
    template <typename Key, typename AttemptFn>
    AccountFillRound RunFixedCountRound(std::map<Key, uint32_t>& remaining,
        std::vector<Key> const& keys, uint32_t allowance, AttemptFn&& attempt)
    {
        AccountFillRound round;
        for (Key const& key : keys)
        {
            if (round.created >= allowance)
                break;

            auto quota = remaining.find(key);
            if (quota == remaining.end())
                continue;
            if (quota->second == 0)
            {
                remaining.erase(quota); // zero = disabled: never reserve or create
                continue;
            }

            switch (attempt(key))
            {
                case FixedCountAttempt::Created:
                    ++round.created;
                    if (TryConsumeQuota(quota->second) && quota->second == 0)
                        remaining.erase(quota);
                    break;
                case FixedCountAttempt::ReservationBlocked:
                    round.reservationBlocked = true;
                    return round;
                case FixedCountAttempt::CreateFailed:
                case FixedCountAttempt::Skipped:
                    break;
            }
        }
        return round;
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
