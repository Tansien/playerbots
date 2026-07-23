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
    // filtering, talent-path validation/filtering, post-selection role
    // verification, BotCreationResult and group quota accounting.
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

    // A premade path may declare one concrete role when its talent tab/build
    // cannot distinguish that intent (for example TBC cat and bear paths can
    // legitimately share a hybrid Feral build). Unannotated paths preserve the
    // existing inferred-role behavior.
    inline uint8_t ResolveTalentPathRoles(uint8_t inferredRoles, uint8_t configuredRole)
    {
        return configuredRole ? configuredRole : inferredRoles;
    }

    // Concrete CURRENT role for runtime classification (IsTank/IsHeal, group
    // quota accounting): exactly ONE role. Capability masks are a
    // creation/planning concept - the Feral TANK|DPS capability never means
    // "currently tanking". The class's tank form/stance/presence decides
    // first; without it, a multi-role spec resolves to DPS (a Feral in cat or
    // caster form, a Frost DK outside Frost Presence), while single-role specs
    // are unambiguous.
    // The tank signal is the class's active tank form/stance/presence at
    // runtime (bear/dire bear, defensive stance, righteous fury, frost
    // presence) OR, at creation time, the learned tank talents of the applied
    // build (a freshly created bot has no auras yet) - see AiFactory.
    inline uint8_t ConcreteRuntimeRole(uint8_t cls, uint8_t tab, bool tankFormActive)
    {
        if (tankFormActive)
            return ROLE_TANK;

        uint8_t const capabilities = SpecTabRoles(cls, tab);
        if (capabilities == uint8_t(ROLE_TANK | ROLE_DPS))
            return ROLE_DPS;

        return capabilities;
    }

    // Pre-Living-Realm runtime classification, reproduced EXACTLY for disabled
    // mode. It differs from ConcreteRuntimeRole in only one case: a Frost DK
    // (cls 6, tab 1) with no tank presence - legacy reported the TANK|DPS mask,
    // the Living resolver reports the single concrete DPS. Druid Feral (cls 11,
    // tab 1) out of bear form was DPS in legacy too, so it must NOT inherit the
    // PR's new TANK|DPS capability mask here (that would be a THIRD behavior
    // matching neither legacy nor the Living resolver). Every other (cls, tab)
    // already maps identically to the legacy 2-arg classifier.
    inline uint8_t LegacyRuntimeRoleMask(uint8_t cls, uint8_t tab, bool tankFormActive)
    {
        if (tankFormActive)
            return ROLE_TANK;
        if (cls == 11 && tab == 1)
            return ROLE_DPS; // legacy druid feral (non-bear) is DPS, not TANK|DPS
        return SpecTabRoles(cls, tab);
    }

    // Living Realm resolves ONE concrete runtime role; with the feature disabled,
    // the legacy classification mask is restored so autonomous role/LFG behavior
    // (IsTank/IsHeal, group composition, dungeon-finder) is unchanged from the
    // pre-Living fork - the disabled-mode parity contract.
    inline uint8_t RuntimeRoleForMode(uint8_t cls, uint8_t tab, bool tankFormActive, bool livingRealmEnabled)
    {
        return livingRealmEnabled ? ConcreteRuntimeRole(cls, tab, tankFormActive)
                                  : LegacyRuntimeRoleMask(cls, tab, tankFormActive);
    }
}
