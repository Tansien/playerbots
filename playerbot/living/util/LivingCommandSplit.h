#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

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

    // Explicit per-command target policy for console player commands. No
    // command acquires implicit bulk targeting: bulk is a deliberate,
    // documented contract, and single-target commands resolve at most one bot.
    enum class CommandTargetMode
    {
        // Bare form targets every online random bot (help documents "all").
        BulkAll,
        // Requires one explicit exact target name; the bare form is usage-only.
        ExactOne,
        // Bare form deterministically targets ONE bot (the lowest online GUID);
        // an explicit name targets exactly that bot.
        FirstAvailable,
    };

    // Resolves the target GUIDs for one command invocation from the online
    // random-bot list. `name` is the explicit target ("" for the bare form);
    // names match exactly, never by prefix. Bulk cardinality is a property of
    // the MODE, so a FirstAvailable command can never fan out: its bare form
    // returns at most one GUID regardless of how many bots are online.
    inline std::vector<uint32_t> ResolveCommandTargets(CommandTargetMode mode, std::string const& name,
        std::vector<std::pair<uint32_t, std::string>> const& onlineBots)
    {
        std::vector<uint32_t> targets;

        if (!name.empty())
        {
            for (auto const& [guid, botName] : onlineBots)
                if (botName == name)
                    targets.push_back(guid);

            return targets;
        }

        switch (mode)
        {
            case CommandTargetMode::BulkAll:
                for (auto const& [guid, botName] : onlineBots)
                    targets.push_back(guid);
                break;
            case CommandTargetMode::FirstAvailable:
                for (auto const& [guid, botName] : onlineBots)
                    if (targets.empty() || guid < targets.front())
                        targets.assign(1, guid);
                break;
            case CommandTargetMode::ExactOne:
                // Bare form is invalid; the caller shows usage before resolving.
                break;
        }

        return targets;
    }
}
