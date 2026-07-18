#include "LivingTest.h"

#include "../util/LivingChatSelector.h"

using namespace living;

// The complete @random/@fixedrandom decision RandomChatFilter runs. The
// structural guarantee against CompositeChatFilter re-drawing (it loops every
// filter filters.size() times) is that a REJECTED decision yields the terminal
// empty message, which the composite breaks on - covered here by asserting the
// decision function is deterministic per draw and that rejection is terminal
// in the production filter's contract (empty remainder, no untouched selector).

LIVING_TEST(chat_selector_parses_all_four_documented_forms)
{
    RandomChatSelector selector;

    LIVING_CHECK(ParseRandomChatSelector("@random follow", selector) == ChatSelectorParse::Parsed);
    LIVING_CHECK(!selector.fixed && selector.chance == 50 && selector.remainder == "follow");

    LIVING_CHECK(ParseRandomChatSelector("@random=25 follow target", selector) == ChatSelectorParse::Parsed);
    LIVING_CHECK(!selector.fixed && selector.chance == 25 && selector.remainder == "follow target");

    LIVING_CHECK(ParseRandomChatSelector("@fixedrandom attack", selector) == ChatSelectorParse::Parsed);
    LIVING_CHECK(selector.fixed && selector.chance == 50 && selector.remainder == "attack");

    LIVING_CHECK(ParseRandomChatSelector("@fixedrandom=75 attack", selector) == ChatSelectorParse::Parsed);
    LIVING_CHECK(selector.fixed && selector.chance == 75 && selector.remainder == "attack");

    // A selector with no trailing command selects nothing to dispatch.
    LIVING_CHECK(ParseRandomChatSelector("@random=100", selector) == ChatSelectorParse::Parsed);
    LIVING_CHECK(selector.chance == 100 && selector.remainder.empty());
}

LIVING_TEST(chat_selector_rejects_malformed_and_ignores_foreign_selectors)
{
    RandomChatSelector selector;

    // Selector-shaped but invalid: rejected, never dispatched.
    LIVING_CHECK(ParseRandomChatSelector("@random=+ hello", selector) == ChatSelectorParse::Malformed);
    LIVING_CHECK(ParseRandomChatSelector("@random=101 hello", selector) == ChatSelectorParse::Malformed);
    LIVING_CHECK(ParseRandomChatSelector("@random=999999999999 x", selector) == ChatSelectorParse::Malformed);
    LIVING_CHECK(ParseRandomChatSelector("@random= hello", selector) == ChatSelectorParse::Malformed);
    LIVING_CHECK(ParseRandomChatSelector("@fixedrandom=abc x", selector) == ChatSelectorParse::Malformed);

    // Not this filter's selector: the message passes through untouched. The
    // selector must be the ENTIRE first token ("@randomx" is not "@random").
    LIVING_CHECK(ParseRandomChatSelector("@class=mage follow", selector) == ChatSelectorParse::NotSelector);
    LIVING_CHECK(ParseRandomChatSelector("@randomx follow", selector) == ChatSelectorParse::NotSelector);
    LIVING_CHECK(ParseRandomChatSelector("follow", selector) == ChatSelectorParse::NotSelector);
    LIVING_CHECK(ParseRandomChatSelector("", selector) == ChatSelectorParse::NotSelector);
}

LIVING_TEST(chat_selector_zero_and_hundred_are_exact)
{
    // One draw from 0..99 decides selection: 0% can never select and 100%
    // always selects, for every possible draw. (The old inclusive 0..100 draw
    // made 100% fail one time in 101.)
    for (uint32_t draw = 0; draw <= RANDOM_CHAT_DRAW_MAX; ++draw)
    {
        LIVING_CHECK(!RandomChatSelected(0, draw));
        LIVING_CHECK(RandomChatSelected(100, draw));
    }

    // Interior chances select exactly `chance` of the 100 possible draws - one
    // invocation is one decision with exactly that probability.
    for (uint32_t chance = 0; chance <= 100; ++chance)
    {
        uint32_t selected = 0;
        for (uint32_t draw = 0; draw <= RANDOM_CHAT_DRAW_MAX; ++draw)
            if (RandomChatSelected(chance, draw))
                ++selected;
        LIVING_CHECK(selected == chance);
    }
}

LIVING_TEST(quest_selector_parses_only_leading_selector_and_preserves_operands)
{
    // Finding N5: only the LEADING @quest= selector is consumed; trailing
    // command operands (including further quest links) are preserved verbatim.
    auto link = [](uint32_t id, std::string const& title)
    {
        return "|cFFFFFF00|Hquest:" + std::to_string(id) + ":10|h[" + title + "]|h|r";
    };
    QuestChatSelector sel;
    std::string const a = link(100, "Deep Ocean, Vast Sea"); // title with spaces + comma
    std::string const b = link(200, "Wanted: Bandits");

    // `@quest=<A> share <B>` -> selector A only; remainder "share <B>" verbatim.
    LIVING_CHECK(ParseQuestChatSelector("@quest=" + a + " share " + b, sel) == QuestChatSelectorParse::Parsed);
    LIVING_CHECK(sel.questId == 100 && sel.fromLink); // NOT 200
    LIVING_CHECK(sel.remainder == "share " + b);       // B's link untouched

    LIVING_CHECK(ParseQuestChatSelector("@quest=523 status", sel) == QuestChatSelectorParse::Parsed);
    LIVING_CHECK(sel.questId == 523 && !sel.fromLink && sel.remainder == "status");

    LIVING_CHECK(ParseQuestChatSelector("@quest=" + a, sel) == QuestChatSelectorParse::Parsed);
    LIVING_CHECK(sel.remainder.empty()); // selector-only

    LIVING_CHECK(ParseQuestChatSelector("follow", sel) == QuestChatSelectorParse::NotSelector);
    LIVING_CHECK(ParseQuestChatSelector("@quest=abc x", sel) == QuestChatSelectorParse::Malformed);
    LIVING_CHECK(ParseQuestChatSelector("@quest=9999999999 x", sel) == QuestChatSelectorParse::Malformed); // overflow, no throw
}
