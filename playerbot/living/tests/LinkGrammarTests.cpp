#include "LivingTest.h"

#include "../util/LivingLinkGrammar.h"

#include <string>

using namespace living;

namespace
{
    // A realistic glyph item link as the client sends it: it ends in bare "|r",
    // NOT "|r " - that distinction is what broke multi-link parsing.
    std::string Link(unsigned id)
    {
        return "|cff71d5ff|Hitem:" + std::to_string(id) + ":0:0:0:0:0:0:0|h[Glyph]|h|r";
    }
}

LIVING_TEST(link_grammar_counts_actual_link_occurrences)
{
    // Duplicate links collapse in an ID set, so a set size is not an occurrence
    // count; the production rule counts "|Hitem:" occurrences instead.
    LIVING_CHECK(CountOccurrences(Link(42), "|Hitem:") == 1);
    LIVING_CHECK(CountOccurrences(Link(42) + " " + Link(43), "|Hitem:") == 2);
    LIVING_CHECK(CountOccurrences(Link(42) + " " + Link(42), "|Hitem:") == 2); // duplicates still count
    LIVING_CHECK(CountOccurrences("no links here", "|Hitem:") == 0);
    LIVING_CHECK(CountOccurrences("", "|Hitem:") == 0);
    LIVING_CHECK(CountOccurrences("anything", "") == 0);
}

LIVING_TEST(link_grammar_token_follows_the_final_bare_terminator)
{
    // The exact production decisions for the glyph set grammar.
    std::string token;

    // Two links, no slot: the final link ends in bare "|r"; the old "|r " search
    // found link 1's terminator and misread link 2 as a slot token. The correct
    // token is empty, which keeps legacy multi-link auto-slotting.
    LIVING_CHECK(TryExtractTokenAfterFinalLink(Link(42) + " " + Link(43), token));
    LIVING_CHECK(token.empty());

    // Duplicate links, no slot: same shape, still auto-slotting.
    LIVING_CHECK(TryExtractTokenAfterFinalLink(Link(42) + " " + Link(42), token));
    LIVING_CHECK(token.empty());

    // One link plus an explicit slot: the suffix after the final "|r" is the slot.
    LIVING_CHECK(TryExtractTokenAfterFinalLink(Link(42) + " 3", token));
    LIVING_CHECK(token == "3");

    // Multi-link plus slot: the token still parses ("2"), and the production rule
    // then rejects because the occurrence count is not exactly one - the reject
    // happens before any mutation.
    std::string const multiWithSlot = Link(42) + " " + Link(43) + " 2";
    LIVING_CHECK(TryExtractTokenAfterFinalLink(multiWithSlot, token));
    LIVING_CHECK(token == "2");
    LIVING_CHECK(CountOccurrences(multiWithSlot, "|Hitem:") == 2); // -> rejected

    // Whitespace-only suffix is an empty token (auto-slotting), and text with no
    // link terminator at all reports false.
    LIVING_CHECK(TryExtractTokenAfterFinalLink(Link(42) + "   ", token));
    LIVING_CHECK(token.empty());
    LIVING_CHECK(!TryExtractTokenAfterFinalLink("remove 3", token));
}

LIVING_TEST(link_grammar_quest_link_matches_only_the_exact_grammar)
{
    uint32_t id = 0;
    size_t end = 0;

    // Full client form with color prefix, and the bare form without one.
    std::string const link = "|cFFFFFF00|Hquest:983:70|h[Terokk]|h|r";
    LIVING_CHECK(TryParseQuestLink(link, 0, id, end));
    LIVING_CHECK(id == 983 && end == link.size());
    LIVING_CHECK(TryParseQuestLink("|Hquest:7:1|h[A]|h|r", 0, id, end));
    LIVING_CHECK(id == 7);

    // Negative level (the client emits one for some quests).
    LIVING_CHECK(TryParseQuestLink("|Hquest:5:-1|h[B]|h|r", 0, id, end));

    // Trailing operand stays outside the match.
    std::string const withOperand = link + " accept";
    LIVING_CHECK(TryParseQuestLink(withOperand, 0, id, end));
    LIVING_CHECK(withOperand.substr(end) == " accept");

    // Malformed color prefix: wrong length, non-hex digits, junk between the
    // prefix and the tag.
    LIVING_CHECK(!TryParseQuestLink("|cFFF|Hquest:1:1|h[A]|h|r", 0, id, end));
    LIVING_CHECK(!TryParseQuestLink("|cGGGGGGGG|Hquest:1:1|h[A]|h|r", 0, id, end));
    LIVING_CHECK(!TryParseQuestLink("|junk|Hquest:1:1|h[A]|h|r", 0, id, end));

    // Missing delimiters: no ':' after the id, no level digits, unpaired close.
    LIVING_CHECK(!TryParseQuestLink("|Hquest:12|h[A]|h|r", 0, id, end));
    LIVING_CHECK(!TryParseQuestLink("|Hquest:12:|h[A]|h|r", 0, id, end));
    LIVING_CHECK(!TryParseQuestLink("|Hquest:12:3|h[A]|r", 0, id, end));
    LIVING_CHECK(!TryParseQuestLink("|Hquest:12:3|h[A]|h", 0, id, end));

    // Junk inside the link body and non-digit ids.
    LIVING_CHECK(!TryParseQuestLink("|Hquest:12:3junk|h[A]|h|r", 0, id, end));
    LIVING_CHECK(!TryParseQuestLink("|Hquest:abc:3|h[A]|h|r", 0, id, end));
    LIVING_CHECK(!TryParseQuestLink("|Hquest:12:3|h[A|B]|h|r", 0, id, end));

    // Id overflow never wraps.
    LIVING_CHECK(!TryParseQuestLink("|Hquest:4294967296:1|h[A]|h|r", 0, id, end));
}
