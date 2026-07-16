#include "LivingTest.h"

#include "../util/LivingCommandSplit.h"
#include "../util/LivingKeyValueArgs.h"

using namespace living;

// MatchExactCommand is the dispatch rule both RandomPlayerbotMgr handler maps
// use; TryParseKeyValueArgs is the argument gate CreateBotOptions::Parse and
// HandleGroup run before any account/character/event mutation.

LIVING_TEST(command_split_matches_exact_command_or_space_separator_only)
{
    std::string params = "sentinel";

    // Bare form: exact match, empty params.
    LIVING_CHECK(MatchExactCommand("reset", "reset", params));
    LIVING_CHECK(params.empty());

    // Command plus parameters after exactly one space.
    LIVING_CHECK(MatchExactCommand("pid 1 2 3", "pid", params));
    LIVING_CHECK(params == "1 2 3");
    LIVING_CHECK(MatchExactCommand("diff 100 200", "diff", params));
    LIVING_CHECK(params == "100 200");
    LIVING_CHECK(MatchExactCommand("clean map", "clean map", params));
    LIVING_CHECK(params.empty());

    // Typo prefixes must NOT dispatch - these reached destructive handlers
    // (reset deletes every random-bot row) with one character blindly stripped.
    LIVING_CHECK(!MatchExactCommand("resetX", "reset", params));
    LIVING_CHECK(!MatchExactCommand("reset-everything", "reset", params));
    LIVING_CHECK(!MatchExactCommand("pidX1 2 3", "pid", params));
    LIVING_CHECK(!MatchExactCommand("diffX100", "diff", params));
    LIVING_CHECK(!MatchExactCommand("removeBob", "remove", params));
    LIVING_CHECK(!MatchExactCommand("reviveX", "revive", params));
    LIVING_CHECK(!MatchExactCommand("updateee", "update", params));

    // Shorter input, different command, or empty input never match.
    LIVING_CHECK(!MatchExactCommand("res", "reset", params));
    LIVING_CHECK(!MatchExactCommand("", "reset", params));
    LIVING_CHECK(!MatchExactCommand("stats", "reset", params));

    // The separator must be the single character after the command.
    LIVING_CHECK(MatchExactCommand("remove Bob", "remove", params));
    LIVING_CHECK(params == "Bob");
}

LIVING_TEST(key_value_args_accept_only_known_complete_pairs)
{
    std::set<std::string> const allowed = { "name", "level", "login" };
    std::map<std::string, std::string> out;
    std::string error;

    LIVING_CHECK(TryParseKeyValueArgs("name=Bob level=60", allowed, out, error));
    LIVING_CHECK(out.size() == 2 && out["name"] == "Bob" && out["level"] == "60");

    // Repeated separators collapse; an empty string parses to nothing.
    out.clear();
    LIVING_CHECK(TryParseKeyValueArgs("  name=Bob   ", allowed, out, error));
    LIVING_CHECK(out.size() == 1);
    out.clear();
    LIVING_CHECK(TryParseKeyValueArgs("", allowed, out, error));
    LIVING_CHECK(out.empty());
}

LIVING_TEST(key_value_args_reject_every_malformed_shape)
{
    std::set<std::string> const allowed = { "name", "level", "login" };
    std::map<std::string, std::string> out;
    std::string error;

    // A bare word is rejected, not silently ignored.
    LIVING_CHECK(!TryParseKeyValueArgs("Bob", allowed, out, error));

    // Unknown keys are rejected, not silently dropped.
    out.clear();
    error.clear();
    LIVING_CHECK(!TryParseKeyValueArgs("name=Bob hax=1", allowed, out, error));
    LIVING_CHECK(error.find("hax") != std::string::npos);

    // An explicit empty value differs from an absent key and is rejected.
    out.clear();
    LIVING_CHECK(!TryParseKeyValueArgs("name=", allowed, out, error));

    // Duplicate keys are rejected (last-one-wins silently changed semantics).
    out.clear();
    LIVING_CHECK(!TryParseKeyValueArgs("level=60 level=1", allowed, out, error));

    // A missing key ("=value") is rejected.
    out.clear();
    LIVING_CHECK(!TryParseKeyValueArgs("=Bob", allowed, out, error));

    // Injection-shaped values stay inert data here: the value is carried
    // verbatim to validators that never interpolate it into SQL.
    out.clear();
    LIVING_CHECK(TryParseKeyValueArgs("name=Rob');DROP", allowed, out, error));
    LIVING_CHECK(out["name"] == "Rob');DROP");
}
