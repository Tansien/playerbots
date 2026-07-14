#include "LivingSnapshots.h"

namespace living
{
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
