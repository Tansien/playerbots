#pragma once

#include <cstdint>

namespace living
{
    // Single source of truth for quest-log occupancy, shared by every consumer so
    // capacity cannot mean two different things in two places.
    //
    // Occupancy is decided by the quest slot ID alone: a nonzero slot occupies the
    // log regardless of whether a quest template still exists for it. Deciding
    // occupancy from the template made template-less ("orphaned") slots read as free
    // space, so a full log reported room, quest acceptance wedged, and cleanup
    // discarded valid quests while keeping the orphan.
    inline bool IsQuestSlotOccupied(uint32_t questId)
    {
        return questId != 0;
    }

    // An orphaned slot is occupied but has no quest template. It cannot be evaluated
    // for pruning (no level, no progress, no class requirement) and it MUST NOT be
    // partially repaired: clearing only the slot leaves the in-memory/persisted quest
    // status, source items, and timers behind, so a restored template would leave the
    // character holding an invisible status it can never re-accept. Until a
    // cross-expansion, core-backed atomic repair exists (slot + status map/DB +
    // source items + timers), an orphan is quarantined: counted as occupied, logged,
    // and left untouched while automated cleanup fails rather than dropping valid
    // quests.
    inline bool IsOrphanedQuestSlot(uint32_t questId, bool hasTemplate)
    {
        return IsQuestSlotOccupied(questId) && !hasTemplate;
    }

    // The complete, non-mutating cleanup preflight CleanQuestLogAction runs before
    // any pruning pass. One orphan anywhere quarantines the whole log from automated
    // cleanup: the pruning passes drop VALID quests toward a target count, so
    // pruning "around" an orphan sacrifices real quests for capacity the orphan
    // still holds - with 24 orphans and one valid quest, the old skip-and-continue
    // behavior dropped the only valid quest and left all 24 orphans in place.
    struct QuestLogPreflight
    {
        uint8_t occupied = 0;
        uint8_t orphans = 0;
        uint64_t orphanFingerprint = 0;

        // Observe one slot; returns true when the slot is a newly counted orphan so
        // the caller can log it.
        bool Observe(uint32_t questId, bool hasTemplate)
        {
            if (!IsQuestSlotOccupied(questId))
                return false;

            ++occupied;
            if (!IsOrphanedQuestSlot(questId, hasTemplate))
                return false;

            ++orphans;

            // FNV-1a over the orphan IDs in slot order. Cleanup runs from a
            // recurring trigger, so the caller logs the quarantine only when this
            // fingerprint changes instead of once per orphan per attempt. Bounded
            // per-bot state: one uint64 remembered by the caller, no global map.
            if (orphanFingerprint == 0)
                orphanFingerprint = 14695981039346656037ull;
            for (int shift = 0; shift < 32; shift += 8)
            {
                orphanFingerprint ^= (questId >> shift) & 0xFF;
                orphanFingerprint *= 1099511628211ull;
            }

            return true;
        }

        bool CleanupPermitted() const { return orphans == 0; }

        // 0 when the log holds no orphans, a stable nonzero fingerprint of the
        // orphan ID sequence otherwise.
        uint64_t OrphanFingerprint() const
        {
            // The FNV accumulator is nonzero for any observed orphan; guarantee the
            // "no orphans" sentinel stays distinct even if a pathological sequence
            // hashed to 0.
            if (orphans == 0)
                return 0;
            return orphanFingerprint != 0 ? orphanFingerprint : 1;
        }
    };
}
