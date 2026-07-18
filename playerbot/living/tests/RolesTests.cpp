#include "LivingTest.h"

#include "../util/LivingRoles.h"

using namespace living;

// SpecTabRoles is THE canonical creation-time role mapping: AiFactory's
// spec-tab overload delegates to it (bit values static_asserted there), and
// tuple capability filtering, talent-path filtering, post-selection role
// verification and BotCreationResult all consume it. The mapping is
// core-independent - the same table serves Classic, TBC and WotLK; the Death
// Knight row is reachable only where the class exists.

LIVING_TEST(roles_druid_feral_capability_mask_is_form_independent_but_verified_role_is_concrete)
{
    // Druid (11), Feral (tab 1) CAPABILITY (planning/candidacy) mask is TANK|DPS
    // with no aura/form input: a druid CAN tank, so BOTH the configured Feral DPS
    // and Feral Tank paths are legitimate CANDIDATES for a role=tank request and
    // the tuple selector / talent filter agree. This mask is a creation-PLANNING
    // concept only.
    uint8_t const feral = SpecTabRoles(11, 1);
    LIVING_CHECK(feral == (ROLE_TANK | ROLE_DPS));
    LIVING_CHECK(RolesSatisfy(feral, ROLE_TANK));  // candidacy: a tank request may pick a feral path
    LIVING_CHECK(RolesSatisfy(feral, ROLE_DPS));
    LIVING_CHECK(!RolesSatisfy(feral, ROLE_HEALER));
    LIVING_CHECK(RolesSatisfy(ClassRolesMask(11), ROLE_TANK));

    // Balance is DPS, Restoration heals.
    LIVING_CHECK(SpecTabRoles(11, 0) == ROLE_DPS);
    LIVING_CHECK(SpecTabRoles(11, 2) == ROLE_HEALER);

    // Finding 5: the VERIFIED role (what quota accounting stores/charges) is NOT
    // this mask - it is the concrete role of the APPLIED build (ConcreteRuntimeRole
    // fed the learned-talent tank signal), so a DPS feral build cannot pass as a
    // tank even though it is a candidate. Covered in detail below.
    LIVING_CHECK(ConcreteRuntimeRole(11, 1, /*tankBuild*/false) == ROLE_DPS);
    LIVING_CHECK(ConcreteRuntimeRole(11, 1, /*tankBuild*/true) == ROLE_TANK);
}

LIVING_TEST(roles_feral_dps_build_cannot_satisfy_tank_verified_role)
{
    // Finding 5 (TBC "pve dps feral cat" / WotLK "Feral dps Pve"): the applied
    // build has NO learned tank talents, so the VERIFIED role is DPS - it can
    // neither satisfy nor consume a tank quota. (Same pure mapping on TBC and
    // WotLK; the host suite cannot load DBC/config, so the tank/dps distinction
    // is the learned-talent boolean.)
    LIVING_CHECK(ConcreteRuntimeRole(11, 1, /*tankBuild*/false) == ROLE_DPS);
    LIVING_CHECK(!RolesSatisfy(ConcreteRuntimeRole(11, 1, false), ROLE_TANK));
    LIVING_CHECK(RolesSatisfy(ConcreteRuntimeRole(11, 1, false), ROLE_DPS));

    // TBC "pve dps feral tank" / WotLK "Feral Tank Pve": the applied build HAS
    // the tank talents, so the verified role is TANK and satisfies a tank request.
    LIVING_CHECK(ConcreteRuntimeRole(11, 1, /*tankBuild*/true) == ROLE_TANK);
    LIVING_CHECK(RolesSatisfy(ConcreteRuntimeRole(11, 1, true), ROLE_TANK));
    LIVING_CHECK(!RolesSatisfy(ConcreteRuntimeRole(11, 1, true), ROLE_DPS));
}

LIVING_TEST(roles_frost_dk_hybrid_tab_verified_role)
{
    // Finding 5 analogue (WotLK): a Frost DK build resolves to DPS, so it cannot
    // consume a DK tank slot; Blood (tab 0) is the single-role DK tank.
    LIVING_CHECK(ConcreteRuntimeRole(6, 1, /*tankBuild*/false) == ROLE_DPS);
    LIVING_CHECK(!RolesSatisfy(ConcreteRuntimeRole(6, 1, false), ROLE_TANK));
    LIVING_CHECK(ConcreteRuntimeRole(6, 0, false) == ROLE_TANK);
    LIVING_CHECK(RolesSatisfy(ConcreteRuntimeRole(6, 0, false), ROLE_TANK));
}

LIVING_TEST(roles_disabled_mode_restores_legacy_runtime_classification)
{
    // Finding 11: with Living Realm DISABLED, runtime role/LFG classification
    // must reproduce the pre-Living fork exactly. The only class/tab where the
    // Living resolver and legacy differ is a Frost DK (6, tab 1) with no tank
    // presence: legacy reports the TANK|DPS mask, the resolver reports DPS.
    LIVING_CHECK(RuntimeRoleForMode(6, 1, false, /*enabled*/true) == ROLE_DPS);
    LIVING_CHECK(RuntimeRoleForMode(6, 1, false, /*enabled*/false) == (ROLE_TANK | ROLE_DPS));

    // Frost Presence -> TANK in both modes.
    LIVING_CHECK(RuntimeRoleForMode(6, 1, true, true) == ROLE_TANK);
    LIVING_CHECK(RuntimeRoleForMode(6, 1, true, false) == ROLE_TANK);

    // Disabled druid feral (non-bear) stays DPS - NOT the new TANK|DPS mask
    // (that would be a third behavior matching neither legacy nor the resolver).
    LIVING_CHECK(RuntimeRoleForMode(11, 1, false, false) == ROLE_DPS);
    LIVING_CHECK(RuntimeRoleForMode(11, 1, true, false) == ROLE_TANK);

    // Every other (cls, tab, form) is identical in both modes: (6,1,false) is
    // the sole divergence.
    for (uint8_t cls : { 1, 2, 5, 6, 7, 11, 4 })
        for (uint8_t tab = 0; tab < 3; ++tab)
            for (bool form : { false, true })
                if (!(cls == 6 && tab == 1 && !form))
                    LIVING_CHECK(RuntimeRoleForMode(cls, tab, form, true)
                              == RuntimeRoleForMode(cls, tab, form, false));
}

LIVING_TEST(roles_canonical_mapping_per_class)
{
    // Warrior: arms/fury DPS, protection tank.
    LIVING_CHECK(SpecTabRoles(1, 0) == ROLE_DPS);
    LIVING_CHECK(SpecTabRoles(1, 1) == ROLE_DPS);
    LIVING_CHECK(SpecTabRoles(1, 2) == ROLE_TANK);

    // Paladin: holy/protection/retribution.
    LIVING_CHECK(SpecTabRoles(2, 0) == ROLE_HEALER);
    LIVING_CHECK(SpecTabRoles(2, 1) == ROLE_TANK);
    LIVING_CHECK(SpecTabRoles(2, 2) == ROLE_DPS);

    // Priest: discipline/holy heal, shadow DPS.
    LIVING_CHECK(SpecTabRoles(5, 0) == ROLE_HEALER);
    LIVING_CHECK(SpecTabRoles(5, 1) == ROLE_HEALER);
    LIVING_CHECK(SpecTabRoles(5, 2) == ROLE_DPS);

    // Shaman: elemental/enhancement DPS, restoration heals.
    LIVING_CHECK(SpecTabRoles(7, 0) == ROLE_DPS);
    LIVING_CHECK(SpecTabRoles(7, 1) == ROLE_DPS);
    LIVING_CHECK(SpecTabRoles(7, 2) == ROLE_HEALER);

    // Death knight (WotLK): blood tank, frost tank/DPS, unholy DPS.
    LIVING_CHECK(SpecTabRoles(6, 0) == ROLE_TANK);
    LIVING_CHECK(SpecTabRoles(6, 1) == (ROLE_TANK | ROLE_DPS));
    LIVING_CHECK(SpecTabRoles(6, 2) == ROLE_DPS);

    // Pure damage classes: hunter(3), rogue(4), mage(8), warlock(9).
    for (uint8_t cls : { 3, 4, 8, 9 })
        for (uint8_t tab = 0; tab < 3; ++tab)
            LIVING_CHECK(SpecTabRoles(cls, tab) == ROLE_DPS);
}

LIVING_TEST(roles_capability_masks_and_containment)
{
    LIVING_CHECK(ClassRolesMask(1) == (ROLE_TANK | ROLE_DPS));            // warrior
    LIVING_CHECK(ClassRolesMask(2) == (ROLE_TANK | ROLE_HEALER | ROLE_DPS)); // paladin
    LIVING_CHECK(ClassRolesMask(5) == (ROLE_HEALER | ROLE_DPS));          // priest
    LIVING_CHECK(ClassRolesMask(6) == (ROLE_TANK | ROLE_DPS));            // death knight
    LIVING_CHECK(ClassRolesMask(7) == (ROLE_HEALER | ROLE_DPS));          // shaman
    LIVING_CHECK(ClassRolesMask(11) == (ROLE_TANK | ROLE_HEALER | ROLE_DPS)); // druid
    LIVING_CHECK(ClassRolesMask(4) == ROLE_DPS);                          // rogue

    // Multi-role containment: a multi-bit request is satisfied by ANY shared
    // bit, and an empty intersection never is.
    LIVING_CHECK(RolesSatisfy(ROLE_TANK | ROLE_DPS, ROLE_TANK | ROLE_HEALER));
    LIVING_CHECK(!RolesSatisfy(ROLE_HEALER, ROLE_TANK | ROLE_DPS));
    LIVING_CHECK(!RolesSatisfy(0, ROLE_TANK));
}

LIVING_TEST(roles_concrete_runtime_role_is_one_role_never_a_mask)
{
    // Feral druid (11, tab 1): the TANK|DPS capability mask never leaks into
    // runtime classification. Bear/dire-bear form -> tank; cat or no form ->
    // DPS - a Feral DPS master must not be counted as a group's tank.
    LIVING_CHECK(ConcreteRuntimeRole(11, 1, true) == ROLE_TANK);
    LIVING_CHECK(ConcreteRuntimeRole(11, 1, false) == ROLE_DPS);

    // Frost DK (6, tab 1) resolves the same way via Frost Presence.
    LIVING_CHECK(ConcreteRuntimeRole(6, 1, true) == ROLE_TANK);
    LIVING_CHECK(ConcreteRuntimeRole(6, 1, false) == ROLE_DPS);

    // Single-role specs are unambiguous with or without a stance: prot
    // warrior tanks, resto druid heals, balance druid damages.
    LIVING_CHECK(ConcreteRuntimeRole(1, 2, false) == ROLE_TANK);
    LIVING_CHECK(ConcreteRuntimeRole(1, 0, true) == ROLE_TANK); // defensive stance overrides
    LIVING_CHECK(ConcreteRuntimeRole(11, 2, false) == ROLE_HEALER);
    LIVING_CHECK(ConcreteRuntimeRole(11, 0, false) == ROLE_DPS);
    LIVING_CHECK(ConcreteRuntimeRole(4, 1, false) == ROLE_DPS);

    // Exactly one role bit in every case.
    for (uint8_t cls : { 1, 2, 5, 6, 7, 11, 4 })
        for (uint8_t tab = 0; tab < 3; ++tab)
            for (bool form : { false, true })
            {
                uint8_t const role = ConcreteRuntimeRole(cls, tab, form);
                LIVING_CHECK(role == ROLE_TANK || role == ROLE_HEALER || role == ROLE_DPS);
            }
}
