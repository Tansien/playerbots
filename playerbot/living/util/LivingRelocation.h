#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace living
{
    // Outcome of a relocation request. In the pinned cores TeleportTo returns
    // after QUEUING a near/far transfer - the move completes only when the bot's
    // own teleport acknowledgement is processed - so acceptance must never be
    // reported (or acted on) as completion.
    enum class RelocationOutcome
    {
        // No candidate was accepted; nothing was mutated.
        Rejected,
        // A candidate was accepted by TeleportTo; completion (Refresh, homebind,
        // inn binding, marker clearing, scheduling) is deferred to the teleport
        // acknowledgement.
        Pending,
        // The relocation was acknowledged and finalized.
        Completed,
    };

    // Everything the acknowledgement path needs to finalize one accepted
    // relocation: the EXACT destination passed to TeleportTo (also reused for
    // homebind so the bind can never diverge from the landing spot) and the
    // work owed on completion.
    struct PendingRelocation
    {
        uint64_t token = 0; // stamped by RelocationTracker::Begin
        uint32_t mapId = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float orientation = 0.0f;
        uint32_t homebindAreaId = 0;
        bool setHomebind = false;
        bool bindInn = false;
        bool rpgTravelCooldown = false;
        bool reviveRecovery = false;      // clear dead/revive markers only on success
        bool scheduleNextTeleport = false;
    };

    // Outcome of a completion attempt from a finished teleport acknowledgement.
    enum class RelocationCompleteResult
    {
        // No relocation is tracked for this bot.
        NoPending,
        // The acknowledgement has not finished yet (bot not in-world or still
        // teleporting); the record stays armed. Decided by the caller, which
        // owns those world-state checks.
        StillPending,
        // The bot landed on the EXACT accepted destination; the record is
        // removed and finalization work may run - exactly once.
        Completed,
        // The acknowledgement finished but the bot is NOT on the accepted
        // destination: the tracked teleport chain is dead (redirected, clobbered
        // by a foreign teleport, or superseded mid-chain). The obsolete record
        // is erased so no stale homebind/revive/scheduling work stays armed for
        // a later unrelated landing; retry markers are untouched.
        TerminalMismatch,
    };

    // Bounded pending-relocation registry: at most one in-flight entry per bot.
    // A newer attempt explicitly supersedes the previous one (its token dies
    // with it), completion/mismatch/cancellation erase, and a bot that never
    // acknowledges simply leaves a record that the next attempt overwrites -
    // retry markers are untouched, so event-driven retry doubles as the timeout
    // path.
    class RelocationTracker
    {
    public:
        // Registers (or supersedes) the bot's pending relocation and stamps a
        // fresh token. Returns that token.
        uint64_t Begin(uint32_t botGuid, PendingRelocation record);

        // Resolves a FINISHED acknowledgement against the pending record using
        // exact destination equality (map, x, y, z, orientation): the pinned
        // cores install the stored destination components directly, so anything
        // else is a different landing, not float noise. Completed and
        // TerminalMismatch both erase the record; `out` receives it in both
        // cases (for finalization or logging).
        RelocationCompleteResult Complete(uint32_t botGuid, uint32_t mapId, float x, float y, float z,
            float orientation, PendingRelocation& out);

        // Drops the bot's pending record (logout/removal/relogin). Never touches
        // retry markers; a cancelled relocation is simply never finalized.
        void Cancel(uint32_t botGuid);

        bool HasPending(uint32_t botGuid) const;
        size_t PendingCount() const { return pending.size(); }

    private:
        uint64_t nextToken = 1;
        std::unordered_map<uint32_t, PendingRelocation> pending;
    };
}
