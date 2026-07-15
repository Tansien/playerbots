#include "LivingSnapshots.h"

namespace living
{
    char const* ToString(LivingQueueState value)
    {
        switch (value)
        {
            case LivingQueueState::Offline: return "offline";
            case LivingQueueState::OnLoginQueue: return "on_login_queue";
            case LivingQueueState::Online: return "online";
            case LivingQueueState::OnLogoutQueue: return "on_logout_queue";
        }

        return "invalid_queue_state";
    }

    char const* ToString(LivingMapKind value)
    {
        switch (value)
        {
            case LivingMapKind::OpenWorld: return "open_world";
            case LivingMapKind::Dungeon: return "dungeon";
            case LivingMapKind::Battleground: return "battleground";
            case LivingMapKind::Arena: return "arena";
        }

        return "invalid_map_kind";
    }
}
