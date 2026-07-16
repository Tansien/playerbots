#pragma once

#include <string>

namespace living
{
    // Exact console-command match: `input` must be exactly `command`, or begin
    // with `command` followed by a single space separator. On match, `params`
    // receives the text after the separator ("" for the bare form) and the
    // function returns true.
    //
    // Replaces the prefix-only `input.find(command) == 0` dispatch that let a
    // typo like "resetX" or "removeBob" invoke destructive handlers with the
    // remainder blindly stripped by one extra character.
    inline bool MatchExactCommand(std::string const& input, std::string const& command, std::string& params)
    {
        if (command.empty() || input.compare(0, command.size(), command) != 0)
            return false;

        if (input.size() == command.size())
        {
            params.clear();
            return true;
        }

        if (input[command.size()] != ' ')
            return false;

        params = input.substr(command.size() + 1);
        return true;
    }
}
