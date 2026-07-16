#pragma once

#include <cstdint>
#include <string>

namespace living
{
    // The random chat-selector domain: chances are percentages, draws come from
    // 0..99, and selection is draw < chance - so 0 never selects and 100 always
    // selects, exactly.
    inline constexpr uint32_t RANDOM_CHAT_DRAW_MAX = 99;

    enum class ChatSelectorParse
    {
        // The message does not start with a @random/@fixedrandom selector token.
        NotSelector,
        // Selector-shaped but invalid (bad chance, out of 0..100): the caller
        // must reject the message without dispatching anything.
        Malformed,
        // A valid selector; the caller draws once and decides once.
        Parsed,
    };

    struct RandomChatSelector
    {
        bool fixed = false;      // @fixedrandom* (deterministic per-bot draw)
        uint32_t chance = 0;     // 0..100
        std::string remainder;   // the command after the selector ("" if none)
    };

    // Parses the FIRST token of `message` as one of the four documented forms:
    // "@random", "@random=N", "@fixedrandom", "@fixedrandom=N" (N in 0..100; the
    // bare forms mean 50). The selector must be the entire first token -
    // "@randomx" is not a selector. The remainder is the untouched text after
    // the token's separating space.
    ChatSelectorParse ParseRandomChatSelector(std::string const& message, RandomChatSelector& out);

    // The single selection decision: exactly one draw (0..99) against the
    // parsed chance. Callers must invoke this once per message - a rejected
    // decision is terminal and must not be re-drawn.
    inline bool RandomChatSelected(uint32_t chance, uint32_t draw)
    {
        return draw < chance;
    }
}
