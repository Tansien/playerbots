#pragma once

#include <cstddef>
#include <string>

namespace living
{
    // Column limits of the ai_playerbot_random_bots schema (event varchar(45),
    // data varchar(255)). Values are validated against these BEFORE any DELETE,
    // INSERT, or cache mutation: strict SQL would otherwise delete the old row
    // and reject the insert, and permissive SQL would truncate the stored value
    // while the cache kept the original - either way DB and cache diverge.
    inline constexpr size_t EVENT_NAME_MAX_BYTES = 45;
    inline constexpr size_t EVENT_DATA_MAX_BYTES = 255;

    inline bool EventValueFitsSchema(std::string const& event, std::string const& data)
    {
        return !event.empty()
            && event.size() <= EVENT_NAME_MAX_BYTES
            && data.size() <= EVENT_DATA_MAX_BYTES;
    }
}
