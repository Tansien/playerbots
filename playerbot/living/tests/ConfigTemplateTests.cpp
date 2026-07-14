#include "LivingTest.h"

#include "../config/LivingRealmConfig.h"

#include <fstream>
#include <sstream>
#include <string>

using namespace living;

// PLAYERBOTS_LIVING_CONF_DIR is provided by CMake and points at the playerbot/
// source directory holding the three distributed configuration templates.
#ifndef PLAYERBOTS_LIVING_CONF_DIR
#error "PLAYERBOTS_LIVING_CONF_DIR must be defined by the build"
#endif

namespace
{
    std::string ReadFile(std::string const& path)
    {
        std::ifstream stream(path);
        LIVING_CHECK(stream.is_open());
        std::ostringstream content;
        content << stream.rdbuf();
        return content.str();
    }

    std::string Trim(std::string const& text)
    {
        size_t const begin = text.find_first_not_of(" \t\r");
        if (begin == std::string::npos)
            return std::string();
        size_t const end = text.find_last_not_of(" \t\r");
        return text.substr(begin, end - begin + 1);
    }

    // Returns the value of the first active (uncommented) `key = value` line, or an
    // empty string when the key is absent.
    std::string FindActiveValue(std::string const& content, std::string const& key)
    {
        std::istringstream lines(content);
        std::string line;
        while (std::getline(lines, line))
        {
            std::string const trimmed = Trim(line);
            if (trimmed.rfind(key, 0) != 0)
                continue;

            size_t const equals = trimmed.find('=');
            if (equals == std::string::npos)
                continue;

            std::string const beforeEquals = Trim(trimmed.substr(0, equals));
            if (beforeEquals != key)
                continue;

            return Trim(trimmed.substr(equals + 1));
        }

        return std::string();
    }

    void CheckTemplate(char const* fileName)
    {
        std::string const path = std::string(PLAYERBOTS_LIVING_CONF_DIR) + "/" + fileName;
        std::string const content = ReadFile(path);

        // Matching keys with matching defaults in every template: disabled, organic,
        // strict.
        LIVING_CHECK(FindActiveValue(content, LIVING_REALM_ENABLED_KEY) == "0");
        LIVING_CHECK(FindActiveValue(content, LIVING_REALM_PROFILE_KEY) == LIVING_REALM_PROFILE_DEFAULT);
        LIVING_CHECK(FindActiveValue(content, LIVING_REALM_STRICT_KEY) == "1");
    }
}

LIVING_TEST(config_templates_declare_matching_living_realm_defaults)
{
    // The template defaults must agree with the parser defaults...
    LIVING_CHECK(LIVING_REALM_ENABLED_DEFAULT == false);
    LIVING_CHECK(std::string(LIVING_REALM_PROFILE_DEFAULT) == "organic");
    LIVING_CHECK(LIVING_REALM_STRICT_DEFAULT == true);

    // ...and all three distributed templates must carry the same keys and values.
    CheckTemplate("aiplayerbot.conf.dist.in");
    CheckTemplate("aiplayerbot.conf.dist.in.tbc");
    CheckTemplate("aiplayerbot.conf.dist.in.wotlk");
}
