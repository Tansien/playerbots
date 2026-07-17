#include "LivingRelocation.h"

namespace living
{
    uint64_t RelocationTracker::Begin(uint32_t botGuid, PendingRelocation record)
    {
        record.token = nextToken++;
        pending[botGuid] = record;
        return record.token;
    }

    RelocationCompleteResult RelocationTracker::Complete(uint32_t botGuid, uint32_t mapId, float x, float y, float z,
        float orientation, PendingRelocation& out)
    {
        auto const it = pending.find(botGuid);
        if (it == pending.end())
            return RelocationCompleteResult::NoPending;

        // Copy out BEFORE erasing - the map node dies with the erase.
        out = it->second;
        pending.erase(it);

        // Exact equality: the acknowledgement handlers relocate to the exact
        // stored destination floats, so any difference means a different
        // teleport landed. The record was erased above either way - obsolete
        // work must never stay armed for a later unrelated landing.
        bool const exactMatch = out.mapId == mapId
            && out.x == x && out.y == y && out.z == z
            && out.orientation == orientation;

        return exactMatch ? RelocationCompleteResult::Completed : RelocationCompleteResult::TerminalMismatch;
    }

    void RelocationTracker::Cancel(uint32_t botGuid)
    {
        pending.erase(botGuid);
    }

    bool RelocationTracker::HasPending(uint32_t botGuid) const
    {
        return pending.find(botGuid) != pending.end();
    }
}
