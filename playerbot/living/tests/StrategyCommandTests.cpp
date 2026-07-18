#include "LivingTest.h"

#include "../util/LivingStrategyCommand.h"

using namespace living;

LIVING_TEST(strategy_directive_defaults_to_add_and_validates_added_names)
{
    // Finding N6: change_strategy must apply the NAMED strategy (not a random
    // action) and expose the names to validate before applying. Default sign
    // is +, - removes (no validation), ~ toggles-on (validate).
    StrategyDirective d;

    LIVING_CHECK(ParseStrategyDirective("grind", d) == StrategyDirectiveParse::Parsed);
    LIVING_CHECK(d.normalized == "+grind" && d.addedNames.size() == 1 && d.addedNames[0] == "grind");

    LIVING_CHECK(ParseStrategyDirective("-rpg", d) == StrategyDirectiveParse::Parsed);
    LIVING_CHECK(d.normalized == "-rpg" && d.addedNames.empty()); // removal needs no existence check

    LIVING_CHECK(ParseStrategyDirective("~tank", d) == StrategyDirectiveParse::Parsed);
    LIVING_CHECK(d.normalized == "~tank" && d.addedNames.size() == 1 && d.addedNames[0] == "tank");

    LIVING_CHECK(ParseStrategyDirective("+grind,-rpg", d) == StrategyDirectiveParse::Parsed);
    LIVING_CHECK(d.normalized == "+grind,-rpg" && d.addedNames.size() == 1 && d.addedNames[0] == "grind");

    LIVING_CHECK(ParseStrategyDirective("  grind , -rpg ", d) == StrategyDirectiveParse::Parsed);
    LIVING_CHECK(d.normalized == "+grind,-rpg"); // whitespace trimmed around tokens
}

LIVING_TEST(strategy_directive_rejects_empty_and_malformed)
{
    StrategyDirective d;
    LIVING_CHECK(ParseStrategyDirective("", d) == StrategyDirectiveParse::Empty);
    LIVING_CHECK(ParseStrategyDirective("   ", d) == StrategyDirectiveParse::Empty);
    LIVING_CHECK(ParseStrategyDirective("+", d) == StrategyDirectiveParse::Malformed);       // sign, no name
    LIVING_CHECK(ParseStrategyDirective("-", d) == StrategyDirectiveParse::Malformed);
    LIVING_CHECK(ParseStrategyDirective("+grind,", d) == StrategyDirectiveParse::Malformed); // trailing comma
    LIVING_CHECK(ParseStrategyDirective("grind,,rpg", d) == StrategyDirectiveParse::Malformed); // empty middle token
}
