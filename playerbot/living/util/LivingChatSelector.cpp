#include "LivingChatSelector.h"

#include "LivingLinkGrammar.h"
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

    QuestChatSelectorParse ParseQuestChatSelector(std::string const& message, QuestChatSelector& out)
    {
        std::string const prefix = "@quest=";
        if (message.compare(0, prefix.size(), prefix) != 0)
            return QuestChatSelectorParse::NotSelector;

        std::string const body = message.substr(prefix.size());

        if (!body.empty() && body[0] == '|')
        {
            // LINK form: the selector is exactly ONE quest link, anchored at the
            // start of the body and matched against the full supported grammar
            // (optional color prefix, "|Hquest:" in place, id/level delimiters,
            // paired "|h[...]|h|r"). The old scan found "|Hquest:" and "|r"
            // anywhere, so junk before the tag or a mangled close was accepted
            // and could swallow trailing operands. The remainder after the
            // link's own terminator survives intact.
            uint32_t id = 0;
            size_t linkEnd = 0;
            if (!TryParseQuestLink(body, 0, id, linkEnd))
                return QuestChatSelectorParse::Malformed;

            std::string const rem = body.substr(linkEnd);

            // Text glued straight onto the link terminator is junk, not an
            // operand - the bare numeric form rejects "523x" the same way.
            if (!rem.empty() && rem[0] != ' ' && rem[0] != '\t')
                return QuestChatSelectorParse::Malformed;
            size_t const nonSpace = rem.find_first_not_of(" \t");
            out.questId = id;
            out.fromLink = true;
            out.remainder = nonSpace == std::string::npos ? "" : rem.substr(nonSpace);
            return QuestChatSelectorParse::Parsed;
        }

        // BARE numeric form: the leading whitespace-delimited token is the id.
        size_t const space = body.find(' ');
        uint32_t id = 0;
        if (!TryParseUInt32(space == std::string::npos ? body : body.substr(0, space), id))
            return QuestChatSelectorParse::Malformed;

        out.questId = id;
        out.fromLink = false;
        out.remainder = space == std::string::npos ? "" : body.substr(space + 1);
        return QuestChatSelectorParse::Parsed;
    }
}
