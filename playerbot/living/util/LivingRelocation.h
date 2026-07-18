#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

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

    // Stage of one tracked relocation. The record is the reservation AND the
    // ledger of owed completion work: it exists from TeleportTo acceptance
    // until every owed runtime and durable side effect is confirmed.
    enum class RelocationStage
    {
        // Transfer queued; awaiting the bot's own teleport acknowledgement.
        PendingAck,
        // The acknowledgement landed on the exact accepted destination; owed
        // completion work (runtime reset, markers, homebind + durability
        // verification, scheduling) is running/retrying. While a record is in
        // this stage NO new random relocation may start for the bot.
        Finalizing,
    };

    // Verification state of the owed homebind write. SetHomebindToLocation in
    // every pinned core queues an async UPDATE and returns void, so the write
    // is only trusted after an execution-ordered verification query (same FIFO
    // delay thread) confirms the stored row.
    enum class HomebindVerifyState
    {
        NotStarted,     // write not issued yet (or owed a reissue)
        AwaitingResult, // write issued, verification query in flight
        Confirmed,      // stored row matches the written target
        GaveUp,         // bounded attempts exhausted; completed with an error
    };

    // Everything the acknowledgement/finalization path needs for one accepted
    // relocation: the EXACT destination passed to TeleportTo (also reused for
    // homebind so the bind can never diverge from the landing spot), the work
    // owed on completion, and the per-operation completion ledger.
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

        // --- staged completion ledger (managed by the tracker) ---
        RelocationStage stage = RelocationStage::PendingAck;
        bool runtimeResetDone = false;      // full AI reset + Refresh, exactly once
        bool rpgCooldownDone = false;
        bool reviveMarkersHandled = false;  // cleared, or deliberately kept (still dead)
        bool homebindHandled = false;       // written+verified, nothing to write, or gave up
        HomebindVerifyState homebindVerify = HomebindVerifyState::NotStarted;
        uint32_t homebindWriteAttempts = 0;
        bool nextTeleportScheduled = false;
        // The homebind target actually written (setHomebind uses the landing
        // spot; bindInn resolves the closest inn at finalization time) - the
        // verification query compares against exactly this.
        bool homebindTargetKnown = false;
        uint32_t homebindTargetMap = 0;
        float homebindTargetX = 0.0f;
        float homebindTargetY = 0.0f;
        float homebindTargetZ = 0.0f;
        uint32_t homebindTargetArea = 0;
    };

    // Outcome of resolving a FINISHED teleport acknowledgement.
    enum class RelocationAckResult
    {
        // No relocation is tracked for this bot.
        NoPending,
        // The bot landed on the EXACT accepted destination: the record moved
        // to Finalizing (it is NOT erased - completion work is still owed).
        Landed,
        // The record is already Finalizing (a repeated call); nothing changed.
        AlreadyFinalizing,
        // The acknowledgement finished but the bot is NOT on the accepted
        // destination: the tracked teleport chain is dead (redirected,
        // clobbered by a foreign teleport, or superseded mid-chain). The
        // obsolete record is erased so no stale work stays armed for a later
        // unrelated landing; retry markers are untouched.
        TerminalMismatch,
    };

    // Result of one finalization advance.
    enum class RelocationAdvanceResult
    {
        NoPending,   // no record, or the record is not Finalizing yet
        Finalizing,  // owed work remains (failed writes retry on later advances)
        Completed,   // every owed operation confirmed; the record was released
    };

    // Result of one owed revive-marker clear attempt.
    enum class MarkerClearOutcome
    {
        Cleared,       // both markers execution-confirmed cleared
        KeptStillDead, // bot is still dead: markers deliberately kept (the
                       // event-driven retry owns recovery); never retried here
        WriteFailed,   // SetEventValue failed; retried on a later advance
    };

    // The homebind write actually issued (target for verification).
    struct HomebindWrite
    {
        uint32_t mapId = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        uint32_t areaId = 0;
    };

    enum class HomebindVerifyOutcome { QueryFailed, Mismatch, Match };

    // What the caller owes after feeding one homebind verification result.
    enum class HomebindVerifyAction
    {
        None,      // no record / not awaiting a result (stale callback)
        Confirmed, // durably verified; advance to release the record
        Reissue,   // write not confirmed; the next advance re-writes and re-verifies
        GaveUp,    // bounded attempts exhausted; the caller logs and the record
                   // completes WITHOUT durable confirmation (disclosed, bounded)
    };

    // World/database boundary for one finalization advance. Every callable
    // runs from a normal world update, NEVER from a SQL result callback.
    struct RelocationAdvanceOps
    {
        // Full AI reset + Refresh (resurrection included) - in-memory.
        std::function<void()> runtimeReset;
        // RPG travel-target cooldown - in-memory.
        std::function<void()> applyRpgCooldown;
        // Clears the dead/revive markers (execution-confirmed writes).
        std::function<MarkerClearOutcome()> clearReviveMarkers;
        // Issues the homebind write (resolving the inn for bindInn) and
        // returns the target written, or nullopt when there is nothing to
        // write (no eligible inn) - which completes the operation.
        std::function<std::optional<HomebindWrite>()> applyHomebind;
        // Enqueues the execution-ordered homebind verification query for the
        // stored target.
        std::function<void()> requestHomebindVerify;
        // Schedules the next teleport event (execution-confirmed write).
        std::function<bool()> scheduleNextTeleport;
    };

    // Bounded pending-relocation registry: at most one in-flight entry per bot.
    //
    // Lifecycle: Begin on TeleportTo acceptance (PendingAck; a newer attempt
    // supersedes a PendingAck record, but a Finalizing record REFUSES new
    // attempts - its owed work must finish first). Acknowledge resolves the
    // finished teleport acknowledgement: an exact landing moves the record to
    // Finalizing, anything else terminally cancels it. Advance runs the owed
    // completion operations idempotently (each tracked separately; failed
    // durable writes retry on later advances) and releases the record only
    // when every owed operation is confirmed. Cancel (logout/removal/relogin)
    // drops the record without finalizing; retry markers are untouched, so
    // event-driven retry doubles as the timeout path for bots that never
    // acknowledge.
    //
    // The registry doubles as the destination reservation ledger: records in
    // BOTH stages count against destination density until released.
    class RelocationTracker
    {
    public:
        // Registers (or supersedes) the bot's pending relocation and stamps a
        // fresh token. Returns that token, or 0 when refused because the bot's
        // previous relocation is still Finalizing (owed side effects must
        // complete before another relocation may start).
        uint64_t Begin(uint32_t botGuid, PendingRelocation record);

        // Resolves a FINISHED acknowledgement against the pending record using
        // exact destination equality (map, x, y, z, orientation): the pinned
        // cores install the stored destination components directly, so anything
        // else is a different landing, not float noise. Landed moves the record
        // to Finalizing; TerminalMismatch erases it (`out` receives the erased
        // record for logging).
        RelocationAckResult Acknowledge(uint32_t botGuid, uint32_t mapId, float x, float y, float z,
            float orientation, PendingRelocation& out);

        // Runs the owed completion operations for a Finalizing record. Safe to
        // call repeatedly: completed operations never re-run, failed durable
        // writes are retried, and the record is erased only when everything
        // owed is confirmed.
        RelocationAdvanceResult Advance(uint32_t botGuid, RelocationAdvanceOps const& ops);

        // SQL-callback entry for homebind verification results: ONLY enqueues
        // (bounded) and returns - no decisions, no database work.
        void OnHomebindVerifyResult(uint32_t botGuid, HomebindVerifyOutcome outcome);

        struct HomebindVerifyEvent
        {
            uint32_t botGuid = 0;
            HomebindVerifyOutcome outcome = HomebindVerifyOutcome::QueryFailed;
        };

        // Drains queued verification events (pump context).
        std::vector<HomebindVerifyEvent> DrainHomebindVerifyEvents();

        // Applies one drained verification result to the record's ledger and
        // reports what the caller owes (Reissue re-arms the write for the next
        // Advance; GaveUp completes the operation with an error after bounded
        // attempts).
        HomebindVerifyAction ApplyHomebindVerify(uint32_t botGuid, HomebindVerifyOutcome outcome,
            uint32_t maxAttempts);

        // Drops the bot's pending record (logout/removal/relogin). Never touches
        // retry markers; a cancelled relocation is simply never finalized.
        void Cancel(uint32_t botGuid);

        bool HasPending(uint32_t botGuid) const;
        bool IsFinalizing(uint32_t botGuid) const;
        size_t PendingCount() const { return pending.size(); }

        // Every bot with a Finalizing record (retry pump surface).
        std::vector<uint32_t> FinalizingBots() const;

        // Destination reservation count for density admission: records in BOTH
        // stages targeting `mapId` within `radius` (2D, matching the same-map
        // WorldPosition::fDist metric the live density scan uses) count,
        // excluding `excludeBotGuid`'s own record.
        uint32_t CountReservedDestinationsNear(uint32_t mapId, float x, float y, float radius,
            uint32_t excludeBotGuid) const;

        PendingRelocation const* Find(uint32_t botGuid) const;

        static constexpr size_t kMaxHomebindVerifyEvents = 256;
        static constexpr uint32_t kMaxHomebindWriteAttempts = 3;

    private:
        uint64_t nextToken = 1;
        std::unordered_map<uint32_t, PendingRelocation> pending;
        std::deque<HomebindVerifyEvent> homebindVerifyEvents;
    };

    // The optional Dark Portal override never REPLACES the attempt's original
    // candidate: a drawn override is tried first, and when its tuple is
    // rejected (disabled map, inactive zone, over-density, invalid coords) the
    // original candidate is tried in the SAME attempt. Only genuine exhaustion
    // of the original candidates can therefore reach the caller's
    // activeOnly=false fallback.
    template <typename Loc>
    std::vector<Loc> OverrideThenOriginal(bool overrideDrawn, Loc const& overrideLoc, Loc const& original)
    {
        std::vector<Loc> order;
        order.reserve(overrideDrawn ? 2 : 1);
        if (overrideDrawn)
            order.push_back(overrideLoc);
        order.push_back(original);
        return order;
    }

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
}
