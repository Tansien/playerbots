#pragma once

#include <cstdint>

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
    // and tells the caller when to stop, so counters, class tallies and role
    // quotas can never drift from persisted reality.
    struct GroupCreationLedger
    {
        uint32_t created = 0;

        // Records one attempt. Returns true when the run must stop (terminal
        // failure); quota/counter updates belong on Counted() outcomes only.
        bool Record(BotCreateStatus status)
        {
            if (status == BotCreateStatus::Created)
                ++created;
            return status == BotCreateStatus::TerminalFailure;
        }

        static bool Counted(BotCreateStatus status) { return status == BotCreateStatus::Created; }
    };

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
}
