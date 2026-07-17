#pragma once

#include <cstdint>

namespace living
{
    // Role bits, identical to ai::BotRoles (static_asserted at the production
    // delegation site in AiFactory.cpp).
    inline constexpr uint8_t ROLE_TANK = 0x01;
    inline constexpr uint8_t ROLE_HEALER = 0x02;
    inline constexpr uint8_t ROLE_DPS = 0x04;

    // THE canonical intended/spec-role mapping: the role mask of one talent tab
    // for one class - a property of the selected spec, never of transient
    // combat auras or the character's current form. Druid Feral (tab 1) is
    // TANK|DPS whether or not Bear Form is currently active. Class ids are
    // core-stable (warrior=1 ... druid=11); the Death Knight row is only
    // reachable on cores where the class exists. Shared by tuple capability
    // filtering, talent-path filtering, post-selection role verification,
    // BotCreationResult and group quota accounting.
    inline uint8_t SpecTabRoles(uint8_t cls, uint8_t tab)
    {
        switch (cls)
        {
            case 1:  // warrior: arms / fury / protection
                return tab == 2 ? ROLE_TANK : ROLE_DPS;
            case 2:  // paladin: holy / protection / retribution
                return tab == 0 ? ROLE_HEALER : (tab == 1 ? ROLE_TANK : ROLE_DPS);
            case 5:  // priest: discipline / holy / shadow
                return tab == 2 ? ROLE_DPS : ROLE_HEALER;
            case 6:  // death knight: blood / frost / unholy
                return tab == 0 ? ROLE_TANK : (tab == 1 ? uint8_t(ROLE_TANK | ROLE_DPS) : ROLE_DPS);
            case 7:  // shaman: elemental / enhancement / restoration
                return tab == 2 ? ROLE_HEALER : ROLE_DPS;
            case 11: // druid: balance / feral / restoration
                return tab == 0 ? ROLE_DPS : (tab == 1 ? uint8_t(ROLE_TANK | ROLE_DPS) : ROLE_HEALER);
            default: // pure damage classes (hunter, rogue, mage, warlock)
                return ROLE_DPS;
        }
    }

    // Class capability = union of every spec tab's intended roles.
    inline uint8_t ClassRolesMask(uint8_t cls)
    {
        return uint8_t(SpecTabRoles(cls, 0) | SpecTabRoles(cls, 1) | SpecTabRoles(cls, 2));
    }

    // The one containment rule for explicit role requests: satisfied when the
    // selected/available role mask contains ANY requested bit.
    inline bool RolesSatisfy(uint8_t availableRoles, uint8_t requestedRoles)
    {
        return (availableRoles & requestedRoles) != 0;
    }

    // Concrete CURRENT role for runtime classification (IsTank/IsHeal, group
    // quota accounting): exactly ONE role. Capability masks are a
    // creation/planning concept - the Feral TANK|DPS capability never means
    // "currently tanking". The class's tank form/stance/presence decides
    // first; without it, a multi-role spec resolves to DPS (a Feral in cat or
    // caster form, a Frost DK outside Frost Presence), while single-role specs
    // are unambiguous.
    inline uint8_t ConcreteRuntimeRole(uint8_t cls, uint8_t tab, bool tankFormActive)
    {
        if (tankFormActive)
            return ROLE_TANK;

        uint8_t const capabilities = SpecTabRoles(cls, tab);
        if (capabilities == uint8_t(ROLE_TANK | ROLE_DPS))
            return ROLE_DPS;

        return capabilities;
    }
}
