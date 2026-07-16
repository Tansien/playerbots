#include "LivingChatSelector.h"

#include "LivingNumericParse.h"

namespace living
{
    ChatSelectorParse ParseRandomChatSelector(std::string const& message, RandomChatSelector& out)
    {
        size_t const spacePos = message.find(' ');
        std::string const token = spacePos == std::string::npos ? message : message.substr(0, spacePos);
        std::string const remainder = spacePos == std::string::npos ? "" : message.substr(spacePos + 1);

        for (bool const fixed : { false, true })
        {
            std::string const bare = fixed ? "@fixedrandom" : "@random";

            if (token == bare)
            {
                out.fixed = fixed;
                out.chance = 50;
                out.remainder = remainder;
                return ChatSelectorParse::Parsed;
            }

            if (token.compare(0, bare.size() + 1, bare + "=") == 0)
            {
                uint32_t chance = 0;
                if (!TryParseUInt32InRange(token.substr(bare.size() + 1), 0, 100, chance))
                    return ChatSelectorParse::Malformed;

                out.fixed = fixed;
                out.chance = chance;
                out.remainder = remainder;
                return ChatSelectorParse::Parsed;
            }
        }

        return ChatSelectorParse::NotSelector;
    }
}
