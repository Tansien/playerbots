#pragma once

#include <cstddef>
#include <cstdint>
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

    // Strict match of ONE quest chat link starting at position `pos`:
    //   [|cAARRGGBB] |Hquest:<id>:<level>|h[<title>]|h|r
    // with an optional 8-hex-digit color prefix, the id as pure digits fitting
    // uint32, the mandatory ':' level delimiter (level may be negative), and
    // the exact paired '|h[...]|h|r' close. The title may not contain '|' or
    // ']' - real quest titles never do, and accepting them would let malformed
    // input swallow arbitrary text up to a later terminator. On success yields
    // the quest id and the position one past the terminating "|r" (so the
    // caller's command remainder is untouched); on failure nothing is written.
    inline bool TryParseQuestLink(std::string const& text, size_t pos, uint32_t& outId, size_t& outEnd)
    {
        auto const isDigit = [](char c) { return c >= '0' && c <= '9'; };
        auto const isHex = [&](char c)
        {
            return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        };

        // Optional color prefix: '|c' + exactly 8 hex digits. '|c' followed by
        // anything else is malformed, not "no prefix".
        if (text.compare(pos, 2, "|c") == 0)
        {
            pos += 2;
            for (size_t i = 0; i < 8; ++i, ++pos)
                if (pos >= text.size() || !isHex(text[pos]))
                    return false;
        }

        std::string const tag = "|Hquest:";
        if (text.compare(pos, tag.size(), tag) != 0)
            return false;
        pos += tag.size();

        uint64_t id = 0;
        size_t const idBegin = pos;
        for (; pos < text.size() && isDigit(text[pos]); ++pos)
        {
            id = id * 10 + static_cast<uint64_t>(text[pos] - '0');
            if (id > 0xFFFFFFFFull)
                return false;
        }
        if (pos == idBegin || pos >= text.size() || text[pos] != ':')
            return false;
        ++pos;

        // Level: optional '-', at least one digit.
        if (pos < text.size() && text[pos] == '-')
            ++pos;
        size_t const levelBegin = pos;
        while (pos < text.size() && isDigit(text[pos]))
            ++pos;
        if (pos == levelBegin)
            return false;

        if (text.compare(pos, 3, "|h[") != 0)
            return false;
        pos += 3;

        while (pos < text.size() && text[pos] != ']')
        {
            if (text[pos] == '|')
                return false;
            ++pos;
        }
        if (text.compare(pos, 5, "]|h|r") != 0)
            return false;

        outId = static_cast<uint32_t>(id);
        outEnd = pos + 5;
        return true;
    }
}
