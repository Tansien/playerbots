#include "LivingTest.h"

#include "../util/LivingImmersiveStats.h"

using namespace living;

// These pin the two properties PlayerbotFactory::InitImmersive depends on and
// previously could not check: that every class it can be called for HAS a spread,
// and that every spread totals exactly 100. The per-class table used to live in a
// switch inside a core-dependent .cpp with no test, and the Death Knight arm was
// missing - the class fell through with an all-zero split that could never reach
// 100, so the caller re-derived and rewrote five durable rows on every call
// forever. A red test is the point: the coverage case below fails the moment a
// playable class has no row.

LIVING_TEST(immersive_spread_totals_exactly_100_for_every_supported_class)
{
    // Sweep well past the playable range so an accidentally-added row for a class
    // id that does not exist is still held to the invariant.
    for (uint32_t cls = 0; cls <= 255; ++cls)
    {
        ImmersiveStatSpread spread{};
        if (!TryImmersiveStatSpread(static_cast<uint8_t>(cls), spread))
            continue;

        LIVING_CHECK(ImmersiveStatTotal(spread) == 100);

        // A negative or >100 entry would survive the total check in pairs, and the
        // caller's shuffle assumes every entry is already a sane percentage.
        for (int32_t percent : spread)
            LIVING_CHECK(percent >= 0 && percent <= 100);
    }
}

// The totals test above is blind to a transposition: swapping two entries in a row
// keeps the sum at 100 and every bound satisfied, while silently applying one stat's
// budget as another. The delegation site static_asserts the core's Stats ORDER, but
// nothing there can tell whether these rows were written in it - so pin the slot each
// class's signature stat must land in. Chosen to be unambiguous: a mage is the most
// intellect-heavy class, a warrior the most strength-heavy, a priest the most spirit-heavy.
LIVING_TEST(immersive_spread_puts_each_signature_stat_in_the_right_slot)
{
    enum { STR = 0, AGI = 1, STA = 2, INT = 3, SPI = 4 };

    ImmersiveStatSpread mage{};
    LIVING_CHECK(TryImmersiveStatSpread(8, mage));
    LIVING_CHECK(mage[INT] == 65);
    LIVING_CHECK(mage[STR] == 0 && mage[AGI] == 0);

    ImmersiveStatSpread warrior{};
    LIVING_CHECK(TryImmersiveStatSpread(1, warrior));
    LIVING_CHECK(warrior[STR] == 30 && warrior[AGI] == 20 && warrior[STA] == 40);
    LIVING_CHECK(warrior[INT] == 0 && warrior[SPI] == 10);

    ImmersiveStatSpread priest{};
    LIVING_CHECK(TryImmersiveStatSpread(5, priest));
    LIVING_CHECK(priest[SPI] == 55 && priest[INT] == 15 && priest[STA] == 30);

    // Death knight shares the warrior row; pin that too, since it is the arm that
    // was missing entirely and the one most likely to be edited next.
    ImmersiveStatSpread deathKnight{};
    LIVING_CHECK(TryImmersiveStatSpread(6, deathKnight));
    LIVING_CHECK(deathKnight == warrior);
}

LIVING_TEST(immersive_spread_covers_every_playable_class)
{
    // warrior, paladin, hunter, rogue, priest, death knight, shaman, mage,
    // warlock, druid. 10 is unused on classic/tbc/wotlk.
    uint8_t const playable[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 11 };

    for (uint8_t cls : playable)
    {
        ImmersiveStatSpread spread{};
        LIVING_CHECK(TryImmersiveStatSpread(cls, spread));
    }
}

LIVING_TEST(immersive_spread_rejects_unknown_class_and_leaves_out_untouched)
{
    ImmersiveStatSpread spread{ { 7, 7, 7, 7, 7 } };

    LIVING_CHECK(!TryImmersiveStatSpread(0, spread));
    LIVING_CHECK(!TryImmersiveStatSpread(10, spread));  // monk: not on these expansions
    LIVING_CHECK(!TryImmersiveStatSpread(200, spread));

    // The caller distinguishes "no distribution for this class" from "a zeroed
    // distribution"; silently writing zeros here would persist a 0% budget.
    for (int32_t percent : spread)
        LIVING_CHECK(percent == 7);
}
