#pragma once

#include <cstdint>
#include <string>

namespace living
{
    // Configuration keys and defaults shared by the parser, the effective-config
    // report, the distributed templates, and their parity tests. Living Realm ships
    // disabled; the defaults here must match all three aiplayerbot.conf.dist.in*
    // templates.
    inline constexpr char const* LIVING_REALM_ENABLED_KEY = "AiPlayerbot.LivingRealm.Enabled";
    inline constexpr char const* LIVING_REALM_PROFILE_KEY = "AiPlayerbot.LivingRealm.Profile";
    inline constexpr char const* LIVING_REALM_STRICT_KEY = "AiPlayerbot.LivingRealm.Strict";

    inline constexpr bool LIVING_REALM_ENABLED_DEFAULT = false;
    inline constexpr char const* LIVING_REALM_PROFILE_DEFAULT = "organic";
    inline constexpr bool LIVING_REALM_STRICT_DEFAULT = true;

    enum class LivingRealmProfile : uint8_t
    {
        Organic,
        Unknown
    };

    // Exact ASCII case-insensitive match of "organic"; anything else is Unknown and
    // becomes a blocking validation failure when Living Realm is enabled.
    LivingRealmProfile ParseLivingRealmProfile(std::string const& name);
    char const* ToString(LivingRealmProfile value);

    // Pure value model of the AiPlayerbot.LivingRealm.* keys. The operator's
    // configuration file is never rewritten; validation only reports.
    struct LivingRealmConfig
    {
        bool enabled = LIVING_REALM_ENABLED_DEFAULT;
        std::string profileName = LIVING_REALM_PROFILE_DEFAULT; // raw configured value
        LivingRealmProfile profile = LivingRealmProfile::Organic;
        bool strict = LIVING_REALM_STRICT_DEFAULT;

        static LivingRealmConfig FromValues(bool enabled, std::string profileName, bool strict);
    };
}
