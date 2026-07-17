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

    // Acknowledged position must land ON the accepted destination. The pinned
    // cores install the exact teleport destination on acknowledgement, so this
    // absorbs only float round-trip noise - it is deliberately far below one
    // yard so a stale or foreign teleport that happens to land nearby can never
    // be finalized as this relocation.
    inline constexpr float RELOCATION_ACK_TOLERANCE = 0.25f;

    // Bounded pending-relocation registry: at most one entry per bot. A newer
    // attempt supersedes the previous one (its token dies with it), completion
    // and cancellation erase, and a bot that never acknowledges simply leaves a
    // record that the next attempt overwrites - retry markers are untouched, so
    // event-driven retry doubles as the timeout path.
    class RelocationTracker
    {
    public:
        // Registers (or supersedes) the bot's pending relocation and stamps a
        // fresh token. Returns that token.
        uint64_t Begin(uint32_t botGuid, PendingRelocation record);

        // Finalizes iff a record exists for the bot AND the acknowledged position
        // matches the accepted destination (same map, within tolerance). The
        // record is removed - finalization happens exactly once. A mismatched
        // acknowledgement (stale teleport, foreign relocation) leaves the record
        // pending and returns false.
        bool Complete(uint32_t botGuid, uint32_t mapId, float x, float y, float z, PendingRelocation& out);

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
