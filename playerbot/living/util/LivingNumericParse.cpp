#include "LivingNumericParse.h"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
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

    bool TryParseUInt32InRange(std::string const& text, uint32_t minValue, uint32_t maxValue, uint32_t& out)
    {
        uint32_t value = 0;
        if (!TryParseUInt32(text, value))
            return false;

        if (value < minValue || value > maxValue)
            return false;

        out = value;
        return true;
    }

    bool TryParseFloatExact(std::string const& text, float& out)
    {
        if (text.empty())
            return false;

        // strtof accepts leading whitespace, "inf"/"nan" and hex floats; restricting
        // the alphabet up front rejects all of those (no 'x', 'i', 'n', spaces).
        for (char const c : text)
        {
            bool const allowed = (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E';
            if (!allowed)
                return false;
        }

        // std::from_chars for floating point is not available on every supported
        // standard library, so this uses strtof with explicit guards: errno for
        // range, end-pointer for full consumption, isfinite for the result.
        errno = 0;
        char const* const begin = text.c_str();
        char* end = nullptr;
        float const value = std::strtof(begin, &end);
        if (errno == ERANGE || end != begin + text.size() || !std::isfinite(value))
            return false;

        out = value;
        return true;
    }

    MoneyParseStatus TryParseMoneyPrefix(std::string const& text, uint64_t maxCopper, uint32_t& outCopper)
    {
        size_t const spacePos = text.find(' ');
        size_t const tokenSize = spacePos == std::string::npos ? text.size() : spacePos;

        auto const isDigit = [](char c) { return c >= '0' && c <= '9'; };

        if (tokenSize == 0 || !isDigit(text[0]))
            return MoneyParseStatus::NoMoney;

        // A token that starts with a digit and carries a money unit anywhere is
        // money-LIKE: if it then fails the strict grammar it is Invalid (must not
        // execute as zero), while a plain number or other word is NoMoney.
        bool moneyLike = false;
        for (size_t i = 0; i < tokenSize; ++i)
            if (text[i] == 'g' || text[i] == 's' || text[i] == 'c')
                moneyLike = true;

        MoneyParseStatus const failure = moneyLike ? MoneyParseStatus::Invalid : MoneyParseStatus::NoMoney;

        // Strict NgNsNc grammar: components in gold, silver, copper order, each at
        // most once, each "digits+unit", full token consumption.
        constexpr char units[3] = { 'g', 's', 'c' };
        constexpr uint64_t multipliers[3] = { 100 * 100, 100, 1 };

        uint64_t total = 0;
        size_t pos = 0;
        int nextUnit = 0;
        while (pos < tokenSize)
        {
            size_t const digitsBegin = pos;
            while (pos < tokenSize && isDigit(text[pos]))
                ++pos;

            // Digits must be followed by a unit letter inside the token.
            if (pos == digitsBegin || pos == tokenSize)
                return failure;

            int unitIndex = -1;
            for (int u = nextUnit; u < 3; ++u)
                if (text[pos] == units[u])
                    unitIndex = u;

            // Unknown unit, repeated unit, or out-of-order unit ("5c5g").
            if (unitIndex == -1)
                return failure;

            uint64_t component = 0;
            std::from_chars_result const result = std::from_chars(text.data() + digitsBegin, text.data() + pos, component);
            if (result.ec != std::errc() || result.ptr != text.data() + pos)
                return MoneyParseStatus::Invalid;

            // Checked multiply-add in uint64: overflow is Invalid, never a wrap.
            uint64_t const multiplier = multipliers[unitIndex];
            if (component > (std::numeric_limits<uint64_t>::max() - total) / multiplier)
                return MoneyParseStatus::Invalid;

            total += component * multiplier;
            nextUnit = unitIndex + 1;
            ++pos;
        }

        if (total > maxCopper || total > std::numeric_limits<uint32_t>::max())
            return MoneyParseStatus::Invalid;

        outCopper = static_cast<uint32_t>(total);
        return MoneyParseStatus::Ok;
    }
}
