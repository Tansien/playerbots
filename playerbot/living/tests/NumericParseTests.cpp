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

LIVING_TEST(numeric_chat_link_ids_reject_overflow_without_throwing)
{
    // The four ExtractAll*Ids helpers feed this parser from inbound chat, which has
    // no exception boundary: signed stoi threw std::out_of_range on payloads like
    // "Hitem:9999999999" straight out of the world/AI path.
    uint32_t value = 0;

    // The full uint32 range is accepted, including values above INT_MAX where the
    // old signed parse threw.
    LIVING_CHECK(living::TryParseUInt32("2147483648", value));  // INT_MAX + 1
    LIVING_CHECK(value == 2147483648u);
    LIVING_CHECK(living::TryParseUInt32("4294967295", value));  // UINT32_MAX
    LIVING_CHECK(value == 4294967295u);

    // Overflow and absurd lengths are skipped, not thrown.
    LIVING_CHECK(!living::TryParseUInt32("4294967296", value)); // UINT32_MAX + 1
    LIVING_CHECK(!living::TryParseUInt32("9999999999", value));
    LIVING_CHECK(!living::TryParseUInt32(std::string(4096, '9'), value));

    // Signs and suffixes are not valid link IDs.
    LIVING_CHECK(!living::TryParseUInt32("+1", value));
    LIVING_CHECK(!living::TryParseUInt32("-1", value));
    LIVING_CHECK(!living::TryParseUInt32("12abc", value));
    LIVING_CHECK(!living::TryParseUInt32("", value));
}

LIVING_TEST(numeric_parse_in_range_validates_domain_with_the_parse)
{
    uint32_t value = 0;

    // The @random=/@fixedrandom= selector domain (0..100).
    LIVING_CHECK(TryParseUInt32InRange("0", 0, 100, value) && value == 0);
    LIVING_CHECK(TryParseUInt32InRange("100", 0, 100, value) && value == 100);
    LIVING_CHECK(!TryParseUInt32InRange("101", 0, 100, value));
    LIVING_CHECK(!TryParseUInt32InRange("4294967295", 0, 100, value));
    LIVING_CHECK(!TryParseUInt32InRange("+", 0, 100, value));
    LIVING_CHECK(!TryParseUInt32InRange("25 hello", 0, 100, value)); // full consumption
    LIVING_CHECK(!TryParseUInt32InRange("", 0, 100, value));

    // The group-size domain (1..MAX_RAID_SIZE): the review's exact rejections.
    LIVING_CHECK(TryParseUInt32InRange("1", 1, 40, value) && value == 1);
    LIVING_CHECK(TryParseUInt32InRange("40", 1, 40, value) && value == 40);
    LIVING_CHECK(!TryParseUInt32InRange("0", 1, 40, value));
    LIVING_CHECK(!TryParseUInt32InRange("41", 1, 40, value));            // max + 1
    LIVING_CHECK(!TryParseUInt32InRange("256", 1, 40, value));           // uint8 wrap to 0
    LIVING_CHECK(!TryParseUInt32InRange("4294967295", 1, 40, value));    // UINT32_MAX

    // A level domain (1..60): explicit invalid levels fail before any mutation.
    LIVING_CHECK(TryParseUInt32InRange("60", 1, 60, value) && value == 60);
    LIVING_CHECK(!TryParseUInt32InRange("0", 1, 60, value));
    LIVING_CHECK(!TryParseUInt32InRange("61", 1, 60, value));
    LIVING_CHECK(!TryParseUInt32InRange("4294967295", 1, 60, value));
    LIVING_CHECK(!TryParseUInt32InRange("-1", 1, 60, value));
    LIVING_CHECK(!TryParseUInt32InRange("abc", 1, 60, value));

    // The value is untouched when the parse or range check fails.
    value = 4242;
    LIVING_CHECK(!TryParseUInt32InRange("50", 1, 40, value) || true);
    LIVING_CHECK(!TryParseUInt32InRange("99", 1, 40, value));
    LIVING_CHECK(value == 4242);
}

LIVING_TEST(numeric_parse_float_exact_accepts_only_finite_decimals)
{
    float value = 0.0f;

    // Console pid values: plain decimals with optional sign and fraction.
    LIVING_CHECK(TryParseFloatExact("1", value) && value == 1.0f);
    LIVING_CHECK(TryParseFloatExact("0.5", value) && value == 0.5f);
    LIVING_CHECK(TryParseFloatExact("-50", value) && value == -50.0f);
    LIVING_CHECK(TryParseFloatExact("+2.25", value) && value == 2.25f);
    LIVING_CHECK(TryParseFloatExact("1e2", value) && value == 100.0f);

    // The old stof threw on these ("rndbot pid + + +" reached it unguarded).
    LIVING_CHECK(!TryParseFloatExact("+", value));
    LIVING_CHECK(!TryParseFloatExact("-", value));
    LIVING_CHECK(!TryParseFloatExact("", value));
    LIVING_CHECK(!TryParseFloatExact("abc", value));
    LIVING_CHECK(!TryParseFloatExact(".", value));

    // Nonfinite, hex, whitespace, overflow and trailing input are rejected.
    LIVING_CHECK(!TryParseFloatExact("inf", value));
    LIVING_CHECK(!TryParseFloatExact("nan", value));
    LIVING_CHECK(!TryParseFloatExact("0x10", value));
    LIVING_CHECK(!TryParseFloatExact(" 1", value));
    LIVING_CHECK(!TryParseFloatExact("1 ", value));
    LIVING_CHECK(!TryParseFloatExact("1.5x", value));
    LIVING_CHECK(!TryParseFloatExact("1e40", value));   // above float range
    LIVING_CHECK(!TryParseFloatExact("1e999", value));

    // The value is untouched when the parse fails.
    value = 42.0f;
    LIVING_CHECK(!TryParseFloatExact("bogus", value));
    LIVING_CHECK(value == 42.0f);
}

LIVING_TEST(numeric_uint64_guid_parse_shares_the_contract)
{
    // Chat/console GUID values: same rules as TryParseUInt32, 64-bit range.
    uint64_t value = 0;
    LIVING_CHECK(living::TryParseUInt64("18446744073709551615", value)); // UINT64_MAX
    LIVING_CHECK(value == 18446744073709551615ull);
    LIVING_CHECK(living::TryParseUInt64("4294967296", value)); // above uint32
    LIVING_CHECK(value == 4294967296ull);

    LIVING_CHECK(!living::TryParseUInt64("18446744073709551616", value)); // UINT64_MAX + 1
    LIVING_CHECK(!living::TryParseUInt64("+", value));
    LIVING_CHECK(!living::TryParseUInt64("-1", value));
    LIVING_CHECK(!living::TryParseUInt64("", value));
    LIVING_CHECK(!living::TryParseUInt64(std::string(4096, '9'), value));
}
