#pragma once

#include <array>
#include <cstdint>

namespace living
{
    // Starting percentage budget for the immersive stat distribution, before the
    // per-bot shuffle. Index order is the core's Stats enum exactly
    // (SharedDefines.h): 0 strength, 1 agility, 2 stamina, 3 intellect, 4 spirit.
    inline constexpr size_t IMMERSIVE_STAT_COUNT = 5;

    using ImmersiveStatSpread = std::array<int32_t, IMMERSIVE_STAT_COUNT>;

    inline int32_t ImmersiveStatTotal(ImmersiveStatSpread const& spread)
    {
        int32_t total = 0;
        for (int32_t percent : spread)
            total += percent;
        return total;
    }

    // Every supported class totals exactly 100. That is an invariant, not a
    // convention: the caller persists this split and re-derives it whenever the
    // stored rows do not sum to 100, so a class whose row totals 99 would be
    // rewritten on every single call forever. The host test pins both the total
    // and the class coverage, which is what makes a missing row a red build
    // instead of a runtime log line - the Death Knight row below was absent for
    // exactly that reason until a code reading found it.
    //
    // Class ids are core-stable (warrior=1 ... druid=11; 10 is unused on these
    // expansions). Death knight (6) exists only on WotLK cores; the row is inert
    // elsewhere because getClass() never returns it there, so it needs no #ifdef.
    //
    // Returns false for a class with no defined spread, leaving `out` untouched -
    // callers must treat that as "no distribution", never as an all-zero one.
    inline bool TryImmersiveStatSpread(uint8_t cls, ImmersiveStatSpread& out)
    {
        switch (cls)
        {                          //  str  agi  sta  int  spi
            case 1:                // warrior
            case 6:                // death knight: the other plate strength class without mana
                out =                 {  30,  20,  40,   0,  10 }; return true;
            case 2:                // paladin
                out =                 {  35,  15,  35,  10,   5 }; return true;
            case 3:                // hunter
            case 4:                // rogue
                out =                 {  15,  40,  40,   0,   5 }; return true;
            case 5:                // priest
                out =                 {   0,   0,  30,  15,  55 }; return true;
            case 7:                // shaman
            case 11:               // druid
                out =                 {  15,  35,  35,  10,   5 }; return true;
            case 8:                // mage
                out =                 {   0,   0,  30,  65,   5 }; return true;
            case 9:                // warlock
                out =                 {   0,   0,  55,  30,  15 }; return true;
        }

        return false;
    }
}
