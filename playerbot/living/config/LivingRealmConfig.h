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

    // Tri-state boolean parse result. The cores' GetBoolDefault maps every value
    // other than true/1/yes to false, so a typo silently selects a permissive
    // effective state (Living Realm quietly off, or Strict quietly downgraded to
    // warnings). Living Realm parses its own keys and reports malformed values.
    enum class LivingRealmBool : uint8_t
    {
        Malformed = 0,
        False,
        True
    };

    // Accepts exactly: 1/0, true/false, yes/no, on/off (ASCII case-insensitive,
    // surrounding whitespace ignored). Anything else - including an empty value -
    // is Malformed.
    LivingRealmBool ParseLivingRealmBool(std::string const& value);
    char const* ToString(LivingRealmBool value);

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

        // Malformed booleans are recorded, not guessed: the report turns them into
        // blocking entries rather than silently choosing a permissive state.
        LivingRealmBool enabledRaw = LivingRealmBool::False;
        LivingRealmBool strictRaw = LivingRealmBool::True;

        static LivingRealmConfig FromValues(bool enabled, std::string profileName, bool strict);
        static LivingRealmConfig FromRawValues(std::string const& enabled, std::string profileName,
            std::string const& strict);
    };
}
