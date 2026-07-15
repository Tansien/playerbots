#pragma once

#include <cstdint>
#include <string>

namespace living
{
    // Pure, dependency-free parsing helpers shared by runtime code and the
    // host-side test target. They live under playerbot/living/ because that is the
    // module's home for code with no core/world dependency, which is exactly what
    // lets the host tests compile - and therefore regression-test - the same
    // functions the runtime calls.
    //
    // Safety invariant: these never throw and never narrow silently. The legacy
    // idiom they replace (isNumeric/isValidNumberString + std::stoi) accepts a lone
    // sign, ignores trailing garbage, throws std::out_of_range above INT_MAX, and
    // leaves range checks to the caller - which chat-command handlers then perform
    // after narrowing to uint8, mutating the wrong slot.

    // Parses text as an unsigned decimal integer. Requires: non-empty, digits only
    // (no sign, no whitespace, no suffix), full consumption, and a value that fits
    // in uint32. Returns false and leaves `out` untouched otherwise.
    bool TryParseUInt32(std::string const& text, uint32_t& out);

    // True when text is exactly a uint32 by the rule above. Use instead of an
    // isNumeric()-style check when the value will be parsed afterwards.
    bool IsExactUInt32(std::string const& text);

    // Parses an explicit slot index and validates it against `slotCount` BEFORE any
    // narrowing, so an out-of-range value can never alias a valid slot (e.g. 256
    // becoming 0 in a uint8). Returns false for any input TryParseUInt32 rejects
    // and for every value >= slotCount.
    bool TryParseSlotIndex(std::string const& text, uint32_t slotCount, uint8_t& out);
}
