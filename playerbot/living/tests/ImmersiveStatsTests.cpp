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
