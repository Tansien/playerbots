#include "LivingTest.h"

#include "../util/LivingNumericParse.h"

#include <limits>
#include <string>

using namespace living;

// These exercise the exact parser ChatHelper::parseMoney delegates to for AH
// posting/bidding, trade gold, and mailed money. The cap below is the core's
// MAX_MONEY_AMOUNT (0x7FFFFFFF-1), identical across Classic/TBC/WotLK.

namespace
{
    constexpr uint64_t kCoreMoneyCap = 0x7FFFFFFFull - 1; // 2147483646 copper

    MoneyParseStatus Parse(std::string const& text, uint32_t& copper)
    {
        return TryParseMoneyPrefix(text, kCoreMoneyCap, copper);
    }
}

LIVING_TEST(money_parse_accepts_documented_ngnsnc_forms)
{
    uint32_t copper = 0;

    LIVING_CHECK(Parse("1g", copper) == MoneyParseStatus::Ok && copper == 10000);
    LIVING_CHECK(Parse("2s", copper) == MoneyParseStatus::Ok && copper == 200);
    LIVING_CHECK(Parse("3c", copper) == MoneyParseStatus::Ok && copper == 3);
    LIVING_CHECK(Parse("1g2s3c", copper) == MoneyParseStatus::Ok && copper == 10203);
    LIVING_CHECK(Parse("1g3c", copper) == MoneyParseStatus::Ok && copper == 10003);
    LIVING_CHECK(Parse("0c", copper) == MoneyParseStatus::Ok && copper == 0);

    // Text after the first space belongs to the caller (item links, counts).
    LIVING_CHECK(Parse("10g some item link", copper) == MoneyParseStatus::Ok && copper == 100000);
}

LIVING_TEST(money_parse_distinguishes_no_money_from_invalid)
{
    uint32_t copper = 4242;

    // Not money at all: callers fall through to item handling.
    LIVING_CHECK(Parse("", copper) == MoneyParseStatus::NoMoney);
    LIVING_CHECK(Parse("sword", copper) == MoneyParseStatus::NoMoney);
    LIVING_CHECK(Parse("523", copper) == MoneyParseStatus::NoMoney);      // bare number, no unit
    LIVING_CHECK(Parse("10x", copper) == MoneyParseStatus::NoMoney);      // digit start, no money unit
    LIVING_CHECK(Parse(" 5g", copper) == MoneyParseStatus::NoMoney);      // leading space: empty token
    LIVING_CHECK(Parse("|cff9d9d9d|Hitem:1:0|h[x]|h|r", copper) == MoneyParseStatus::NoMoney);
    LIVING_CHECK(Parse(std::string(300, 'x'), copper) == MoneyParseStatus::NoMoney);

    // Money-LIKE but malformed: must never execute as a zero transaction.
    LIVING_CHECK(Parse("5g5g", copper) == MoneyParseStatus::Invalid);     // repeated unit
    LIVING_CHECK(Parse("5c5g", copper) == MoneyParseStatus::Invalid);     // out-of-order units
    LIVING_CHECK(Parse("10gg", copper) == MoneyParseStatus::Invalid);     // unit with no digits
    LIVING_CHECK(Parse("10gx", copper) == MoneyParseStatus::Invalid);     // trailing garbage in token
    LIVING_CHECK(Parse("10x5g", copper) == MoneyParseStatus::Invalid);    // garbage between components
    LIVING_CHECK(Parse("1g2s3", copper) == MoneyParseStatus::Invalid);    // dangling digits after units

    // The out value is untouched on every non-Ok result.
    LIVING_CHECK(copper == 4242);
}

LIVING_TEST(money_parse_rejects_overflow_and_amounts_above_the_core_cap)
{
    uint32_t copper = 0;

    // The exact core maximum is accepted; one copper more is rejected.
    LIVING_CHECK(Parse("2147483646c", copper) == MoneyParseStatus::Ok && copper == 2147483646u);
    LIVING_CHECK(Parse("214748g36s46c", copper) == MoneyParseStatus::Ok && copper == 2147483646u);
    LIVING_CHECK(Parse("2147483647c", copper) == MoneyParseStatus::Invalid);
    LIVING_CHECK(Parse("214749g", copper) == MoneyParseStatus::Invalid);

    // "4294967297g" wrapped the old uint32 accumulator to exactly 10000 copper and
    // executed as a tiny real transaction. It is Invalid, not 1g.
    copper = 4242;
    LIVING_CHECK(Parse("4294967297g", copper) == MoneyParseStatus::Invalid);
    LIVING_CHECK(copper == 4242);

    // Component overflow beyond uint64 and absurd digit runs are Invalid, and a
    // token longer than 255 bytes cannot wrap the (formerly uint8) index.
    LIVING_CHECK(Parse("18446744073709551616c", copper) == MoneyParseStatus::Invalid);
    LIVING_CHECK(Parse(std::string(300, '9') + "g", copper) == MoneyParseStatus::Invalid);
    LIVING_CHECK(Parse(std::string(300, '9') + "g and more text", copper) == MoneyParseStatus::Invalid);

    // A smaller caller-supplied cap is enforced the same way.
    LIVING_CHECK(TryParseMoneyPrefix("5g", 100, copper) == MoneyParseStatus::Invalid);
    LIVING_CHECK(TryParseMoneyPrefix("1s", 100, copper) == MoneyParseStatus::Ok && copper == 100);
}
