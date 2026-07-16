#pragma once

#include <cstddef>
#include <string>

namespace living
{
    // Pure chat-link grammar helpers shared by production command parsing and the
    // host tests, so the exact production decisions are regression-tested.

    // Non-overlapping occurrence count of marker in text. Used to count ACTUAL item
    // links ("|Hitem:") in a command: duplicate links collapse in an ID set, so a
    // set size is not an occurrence count.
    inline size_t CountOccurrences(std::string const& text, std::string const& marker)
    {
        if (marker.empty())
            return 0;

        size_t count = 0;
        for (size_t pos = text.find(marker); pos != std::string::npos; pos = text.find(marker, pos + marker.size()))
            ++count;

        return count;
    }

    // The trimmed token after the FINAL item-link terminator "|r". A link ends in
    // bare "|r" - searching for "|r " (with a space) missed the last link of a
    // multi-link command and misread the second link as the token. Returns false
    // when text contains no "|r" at all; an empty token means "nothing after the
    // final link".
    inline bool TryExtractTokenAfterFinalLink(std::string const& text, std::string& token)
    {
        size_t const lastLinkEnd = text.rfind("|r");
        if (lastLinkEnd == std::string::npos)
            return false;

        std::string suffix = text.substr(lastLinkEnd + 2);

        auto const isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
        size_t begin = 0;
        size_t end = suffix.size();
        while (begin < end && isSpace(suffix[begin]))
            ++begin;
        while (end > begin && isSpace(suffix[end - 1]))
            --end;

        token = suffix.substr(begin, end - begin);
        return true;
    }
}
