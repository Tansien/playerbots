#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace living
{
    // Typed outcome of one bot-creation attempt. Group creation must decide
    // success from THIS value, never by matching substrings in accumulated chat
    // messages: message sniffing counted later failures as created after one
    // success and never counted successes after one failure.
    enum class BotCreateStatus
    {
        // A character was created and persisted; a GUID is available.
        Created,
        // This attempt failed for a reason another attempt may avoid
        // (e.g. a random name collision). The caller may try again.
        RetryableFailure,
        // No further attempt can succeed (invalid arguments, no account
        // capacity, infrastructure error). The caller must stop retrying.
        TerminalFailure,
    };

    // Per-run accounting for group creation. Counts only actually created bots
    // - tallied by the class the creation ACTUALLY persisted, never by the
    // preselected assumption - and tells the caller when to stop, so counters,
    // class tallies and role quotas can never drift from persisted reality.
    struct GroupCreationLedger
    {
        uint32_t created = 0;
        std::map<uint8_t, uint32_t> createdByClass;

        // Records one attempt with the persisted class (ignored unless the
        // attempt actually created a bot). Returns true when the run must stop
        // (terminal failure); quota/counter updates belong on Counted() outcomes
        // only.
        bool Record(BotCreateStatus status, uint8_t actualClass = 0)
        {
            if (status == BotCreateStatus::Created)
            {
                ++created;
                ++createdByClass[actualClass];
            }
            return status == BotCreateStatus::TerminalFailure;
        }

        static bool Counted(BotCreateStatus status) { return status == BotCreateStatus::Created; }
    };

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
}
