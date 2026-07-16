#include "LivingRelocation.h"

namespace living
{
    uint64_t RelocationTracker::Begin(uint32_t botGuid, PendingRelocation record)
    {
        record.token = nextToken++;
        pending[botGuid] = record;
        return record.token;
    }

    bool RelocationTracker::Complete(uint32_t botGuid, uint32_t mapId, float x, float y, float z, PendingRelocation& out)
    {
        auto const it = pending.find(botGuid);
        if (it == pending.end())
            return false;

        PendingRelocation const& record = it->second;
        if (record.mapId != mapId)
            return false;

        float const dx = record.x - x;
        float const dy = record.y - y;
        float const dz = record.z - z;
        if (dx * dx + dy * dy + dz * dz > RELOCATION_ACK_TOLERANCE * RELOCATION_ACK_TOLERANCE)
            return false;

        out = record;
        pending.erase(it);
        return true;
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
