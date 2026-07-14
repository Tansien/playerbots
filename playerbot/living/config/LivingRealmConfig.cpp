#include "LivingRealmConfig.h"

#include <algorithm>
#include <cctype>

namespace living
{
    LivingRealmProfile ParseLivingRealmProfile(std::string const& name)
    {
        // ASCII-only lowering: std::tolower is locale-sensitive (e.g. Turkish 'I'),
        // and config keys are ASCII by contract.
        std::string lowered = name;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
            [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c; });

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
