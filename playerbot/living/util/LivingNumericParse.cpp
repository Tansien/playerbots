#include "LivingNumericParse.h"

#include <charconv>
#include <limits>
#include <system_error>

namespace living
{
    bool TryParseUInt32(std::string const& text, uint32_t& out)
    {
        if (text.empty())
            return false;

        // std::from_chars on an unsigned type rejects '+'/'-' outright and reports
        // out_of_range instead of throwing, but it stops at the first non-digit, so
        // full consumption must be checked explicitly to reject "12abc"/"12 ".
        uint32_t value = 0;
        char const* const begin = text.data();
        char const* const end = begin + text.size();

        std::from_chars_result const result = std::from_chars(begin, end, value);
        if (result.ec != std::errc() || result.ptr != end)
            return false;

        out = value;
        return true;
    }

    bool TryParseUInt64(std::string const& text, uint64_t& out)
    {
        if (text.empty())
            return false;

        // std::from_chars on an unsigned type rejects '+'/'-' outright and reports
        // out_of_range instead of throwing, but it stops at the first non-digit, so
        // full consumption must be checked explicitly to reject "12abc"/"12 ".
        uint64_t value = 0;
        char const* const begin = text.data();
        char const* const end = begin + text.size();

        std::from_chars_result const result = std::from_chars(begin, end, value);
        if (result.ec != std::errc() || result.ptr != end)
            return false;

        out = value;
        return true;
    }

    bool IsExactUInt32(std::string const& text)
    {
        uint32_t ignored = 0;
        return TryParseUInt32(text, ignored);
    }

    bool TryParseSlotIndex(std::string const& text, uint32_t slotCount, uint8_t& out)
    {
        uint32_t value = 0;
        if (!TryParseUInt32(text, value))
            return false;

        // Range-check against the real slot count first: narrowing a value like 256
        // into uint8 would otherwise wrap it onto slot 0.
        if (value >= slotCount || value > std::numeric_limits<uint8_t>::max())
            return false;

        out = static_cast<uint8_t>(value);
        return true;
    }
}
