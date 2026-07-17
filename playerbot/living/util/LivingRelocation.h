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

    // Pure destination-eligibility policy pieces, shared by the ONE final
    // destination validator in RandomPlayerbotMgr: the same rules run for the
    // pre-filter (raw candidate) and for the FINAL tuple after retry jitter and
    // terrain-height adjustment, so jitter can never cross a boundary the
    // candidate check enforced.

    // An enemy zone blocks low-level bots entirely and capitals at any level.
    inline bool DestinationBlockedByEnemyZone(bool zoneIsEnemy, bool zoneIsCapital, uint32_t botLevel)
    {
        return zoneIsEnemy && (botLevel < 21 || zoneIsCapital);
    }

    // An enemy area (sub-zone team) blocks low-level bots.
    inline bool DestinationBlockedByEnemyArea(bool areaIsEnemy, uint32_t botLevel)
    {
        return areaIsEnemy && botLevel < 21;
    }

    // Low-level starter/race zone policy (zone and race ids are core-stable).
    // Returns true when the zone excludes this race/team below level 30.
    // `expansionZones` gates the TBC-only starter zones on cores where those
    // races exist.
    inline bool DestinationBlockedByStarterZone(uint32_t zoneId, uint8_t race, bool isAlliance,
        uint32_t botLevel, bool expansionZones)
    {
        if (botLevel >= 30)
            return false;

        switch (zoneId)
        {
            case 12: case 40:     return race != 1;              // Elwynn/Westfall: human only
            case 1: case 38:      return race != 3;              // Dun Morogh/Loch Modan: dwarf only
            case 85: case 130:    return race != 5;              // Tirisfal/Silverpine: undead only
            case 141: case 148:   return race != 4;              // Teldrassil/Darkshore: night elf only
            case 14: case 17:     return race != 2 && race != 8; // Durotar/Barrens: orc/troll only
            case 215:             return race != 6;              // Mulgore: tauren only
            case 44: case 10:     return !isAlliance;            // Redridge/Duskwood: alliance only
            case 3524: case 3525: return expansionZones && race != 11; // Azuremyst/Bloodmyst: draenei only
            case 3430: case 3433: return expansionZones && race != 10; // Eversong/Ghostlands: blood elf only
            default:              return false;
        }
    }

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
