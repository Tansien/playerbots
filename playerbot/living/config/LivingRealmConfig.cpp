#include "LivingRealmConfig.h"

#include <algorithm>
#include <cctype>

namespace living
{
    LivingRealmProfile ParseLivingRealmProfile(std::string const& name)
    {
        std::string lowered = name;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (lowered == LIVING_REALM_PROFILE_DEFAULT)
            return LivingRealmProfile::Organic;

        return LivingRealmProfile::Unknown;
    }

    char const* ToString(LivingRealmProfile value)
    {
        switch (value)
        {
            case LivingRealmProfile::Organic: return "organic";
            case LivingRealmProfile::Unknown: return "unknown";
        }

        return "invalid";
    }

    LivingRealmConfig LivingRealmConfig::FromValues(bool enabled, std::string profileName, bool strict)
    {
        LivingRealmConfig config;
        config.enabled = enabled;
        config.profile = ParseLivingRealmProfile(profileName);
        config.profileName = std::move(profileName);
        config.strict = strict;
        return config;
    }
}
