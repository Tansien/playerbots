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

    LivingRealmBool ParseLivingRealmBool(std::string const& value)
    {
        std::string text;
        for (char c : value)
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
                text += (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;

        if (text == "1" || text == "true" || text == "yes" || text == "on")
            return LivingRealmBool::True;
        if (text == "0" || text == "false" || text == "no" || text == "off")
            return LivingRealmBool::False;

        return LivingRealmBool::Malformed;
    }

    char const* ToString(LivingRealmBool value)
    {
        switch (value)
        {
            case LivingRealmBool::Malformed: return "malformed";
            case LivingRealmBool::False: return "0";
            case LivingRealmBool::True: return "1";
        }

        return "invalid";
    }

    LivingRealmConfig LivingRealmConfig::FromValues(bool enabled, std::string profileName, bool strict)
    {
        LivingRealmConfig config;
        config.enabled = enabled;
        config.enabledRaw = enabled ? LivingRealmBool::True : LivingRealmBool::False;
        config.profile = ParseLivingRealmProfile(profileName);
        config.profileName = std::move(profileName);
        config.strict = strict;
        config.strictRaw = strict ? LivingRealmBool::True : LivingRealmBool::False;
        return config;
    }

    LivingRealmConfig LivingRealmConfig::FromRawValues(std::string const& enabled, std::string profileName,
        std::string const& strict)
    {
        LivingRealmConfig config;
        config.enabledRaw = ParseLivingRealmBool(enabled);
        config.strictRaw = ParseLivingRealmBool(strict);

        // Fail-closed interpretation while the report blocks on the malformed value:
        // a malformed Enabled is treated as enabled (so its blocking entry is
        // emitted and the realm is not silently switched off), and a malformed
        // Strict keeps the strict default rather than downgrading to warnings.
        config.enabled = config.enabledRaw != LivingRealmBool::False;
        config.strict = config.strictRaw != LivingRealmBool::False;
        config.profile = ParseLivingRealmProfile(profileName);
        config.profileName = std::move(profileName);
        return config;
    }
}
