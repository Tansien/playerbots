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
}
