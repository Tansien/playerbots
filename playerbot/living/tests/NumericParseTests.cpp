#include "LivingTest.h"

#include "../util/LivingNumericParse.h"

#include <limits>
#include <string>

using namespace living;

// These cover the exact malformed inputs the review reported against the glyph
// command. The parser is the same function the runtime calls (ChatHelper's item
// scan and both GlyphAction slot branches), so these are semantic regressions for
// that code, not a parallel reimplementation.

LIVING_TEST(numeric_parse_accepts_only_exact_unsigned_decimals)
{
    uint32_t value = 0;

    LIVING_CHECK(TryParseUInt32("0", value) && value == 0);
    LIVING_CHECK(TryParseUInt32("6", value) && value == 6);
    LIVING_CHECK(TryParseUInt32("256", value) && value == 256);
    LIVING_CHECK(TryParseUInt32("2147483648", value) && value == 2147483648u);   // > INT_MAX: stoi threw here
    LIVING_CHECK(TryParseUInt32("4294967295", value) && value == 4294967295u);   // uint32 max

    // Overflow past uint32 is rejected, not wrapped and not thrown.
    LIVING_CHECK(!TryParseUInt32("4294967296", value));
    LIVING_CHECK(!TryParseUInt32("99999999999999999999", value));

    // Signs are rejected outright ("+"/"-1" reached stoi and threw).
    LIVING_CHECK(!TryParseUInt32("+", value));
    LIVING_CHECK(!TryParseUInt32("-", value));
    LIVING_CHECK(!TryParseUInt32("-1", value));
    LIVING_CHECK(!TryParseUInt32("+6", value));

    // Partial consumption is rejected: stoi silently accepted trailing garbage.
    LIVING_CHECK(!TryParseUInt32("12abc", value));
    LIVING_CHECK(!TryParseUInt32("6 ", value));
    LIVING_CHECK(!TryParseUInt32(" 6", value));
    LIVING_CHECK(!TryParseUInt32("6.0", value));
    LIVING_CHECK(!TryParseUInt32("0x10", value));
    LIVING_CHECK(!TryParseUInt32("", value));
    LIVING_CHECK(!TryParseUInt32("abc", value));

    // The value is untouched when the parse fails.
    uint32_t untouched = 4242;
    LIVING_CHECK(!TryParseUInt32("nope", untouched));
    LIVING_CHECK(untouched == 4242);

    LIVING_CHECK(IsExactUInt32("7"));
    LIVING_CHECK(!IsExactUInt32("7x"));
}

LIVING_TEST(numeric_parse_slot_index_validates_before_narrowing)
{
    uint8_t slot = 0;

    // A six-slot glyph bar: every explicit slot >= size is rejected.
    LIVING_CHECK(TryParseSlotIndex("0", 6, slot) && slot == 0);
    LIVING_CHECK(TryParseSlotIndex("5", 6, slot) && slot == 5);
    LIVING_CHECK(!TryParseSlotIndex("6", 6, slot));   // first out-of-range slot
    LIVING_CHECK(!TryParseSlotIndex("99", 6, slot));

    // 256 must not wrap onto slot 0: this is the exact aliasing the review found.
    slot = 42;
    LIVING_CHECK(!TryParseSlotIndex("256", 6, slot));
    LIVING_CHECK(slot == 42); // untouched, definitely not 0
    LIVING_CHECK(!TryParseSlotIndex("512", 6, slot));
    LIVING_CHECK(!TryParseSlotIndex("2147483648", 6, slot));
    LIVING_CHECK(!TryParseSlotIndex("4294967296", 6, slot));

    // Malformed tokens are rejected without throwing.
    LIVING_CHECK(!TryParseSlotIndex("+", 6, slot));
    LIVING_CHECK(!TryParseSlotIndex("-1", 6, slot));
    LIVING_CHECK(!TryParseSlotIndex("", 6, slot));
    LIVING_CHECK(!TryParseSlotIndex("1abc", 6, slot));

    // An empty glyph bar accepts no slot at all.
    LIVING_CHECK(!TryParseSlotIndex("0", 0, slot));

    // A slot count above uint8 range still cannot yield a narrowed alias.
    LIVING_CHECK(TryParseSlotIndex("255", 300, slot) && slot == 255);
    LIVING_CHECK(!TryParseSlotIndex("256", 300, slot));
}
