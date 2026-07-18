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

    QuestChatSelectorParse ParseQuestChatSelector(std::string const& message, QuestChatSelector& out)
    {
        std::string const prefix = "@quest=";
        if (message.compare(0, prefix.size(), prefix) != 0)
            return QuestChatSelectorParse::NotSelector;

        std::string const body = message.substr(prefix.size());

        if (!body.empty() && body[0] == '|')
        {
            // LINK form: the selector is the FIRST quest link only. It is
            // delimited by its "|r" terminator (titles contain spaces), so
            // trailing operands - including further quest links - survive intact.
            std::string const tag = "|Hquest:";
            size_t const tagPos = body.find(tag);
            if (tagPos == std::string::npos)
                return QuestChatSelectorParse::Malformed; // a link, but not a quest link

            size_t const idPos = tagPos + tag.size();
            size_t idEnd = idPos;
            while (idEnd < body.size() && body[idEnd] >= '0' && body[idEnd] <= '9')
                ++idEnd;

            uint32_t id = 0;
            if (idEnd == idPos || !TryParseUInt32(body.substr(idPos, idEnd - idPos), id))
                return QuestChatSelectorParse::Malformed;

            size_t const linkEnd = body.find("|r", idEnd);
            if (linkEnd == std::string::npos)
                return QuestChatSelectorParse::Malformed;

            std::string const rem = body.substr(linkEnd + 2);
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
