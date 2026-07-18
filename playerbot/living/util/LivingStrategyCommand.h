#pragma once

#include <string>
#include <vector>

namespace living
{
    enum class StrategyDirectiveParse
    {
        Empty,     // no operand: the caller should print usage
        Malformed, // a sign with no name, or an empty token (leading/trailing/double comma)
        Parsed,    // a normalized directive; addedNames must be validated before applying
    };

    struct StrategyDirective
    {
        std::string normalized;              // engine grammar, e.g. "+grind,-rpg"
        std::vector<std::string> addedNames; // names that ADD a strategy (+/~), to validate vs the registry
    };

    // Normalizes a `change_strategy` operand into the engine's directive grammar
    // (comma-separated tokens, each optionally prefixed +/-/~, default +) and
    // collects the names that ADD a strategy. The command used to parse the
    // operand and then discard it, applying a RANDOM action instead; and
    // Engine::addStrategy silently no-ops an unknown name. So the caller must
    // reject unknown added names BEFORE applying - this helper isolates the pure
    // grammar so the decision is host-testable.
    inline StrategyDirectiveParse ParseStrategyDirective(std::string const& operand, StrategyDirective& out)
    {
        out.normalized.clear();
        out.addedNames.clear();

        size_t const begin = operand.find_first_not_of(" \t");
        if (begin == std::string::npos)
            return StrategyDirectiveParse::Empty;
        size_t const end = operand.find_last_not_of(" \t");
        std::string const trimmed = operand.substr(begin, end - begin + 1);

        std::string normalized;
        size_t pos = 0;
        for (;;)
        {
            size_t const comma = trimmed.find(',', pos);
            std::string token = comma == std::string::npos ? trimmed.substr(pos) : trimmed.substr(pos, comma - pos);

            size_t const tokenBegin = token.find_first_not_of(" \t");
            if (tokenBegin == std::string::npos)
                return StrategyDirectiveParse::Malformed; // empty token (double/trailing comma)
            size_t const tokenEnd = token.find_last_not_of(" \t");
            token = token.substr(tokenBegin, tokenEnd - tokenBegin + 1);

            char sign = '+';
            std::string name = token;
            if (token[0] == '+' || token[0] == '-' || token[0] == '~')
            {
                sign = token[0];
                name = token.substr(1);
            }
            if (name.empty())
                return StrategyDirectiveParse::Malformed; // a sign with no name

            if (!normalized.empty())
                normalized += ',';
            normalized += sign;
            normalized += name;
            if (sign != '-')
                out.addedNames.push_back(name); // + and ~ both add/toggle-on: validate them

            if (comma == std::string::npos)
                break;
            pos = comma + 1;
        }

        out.normalized = normalized;
        return StrategyDirectiveParse::Parsed;
    }
}
