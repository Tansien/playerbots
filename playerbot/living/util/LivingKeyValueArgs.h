#pragma once

#include <map>
#include <set>
#include <string>

namespace living
{
    // Strict space-separated key=value argument parsing for command entry
    // points. Everything is validated BEFORE the caller mutates anything:
    //   - every token must be key=value (a bare word is rejected, not ignored);
    //   - the key must be in `allowedKeys` (unknown keys are rejected, not
    //     silently dropped);
    //   - explicit empty values ("name=") are rejected - an absent key and a
    //     present-but-empty value are different things;
    //   - duplicate keys are rejected (the old last-one-wins parse silently
    //     changed semantics).
    // Returns false and sets `error` (naming the offending token) on the first
    // violation; `out` then remains untouched by that token's content only in
    // the sense that the caller must discard it - treat a false return as
    // "no arguments parsed".
    bool TryParseKeyValueArgs(std::string const& text, std::set<std::string> const& allowedKeys,
        std::map<std::string, std::string>& out, std::string& error);
}
