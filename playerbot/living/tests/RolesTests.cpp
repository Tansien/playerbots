#include "LivingTest.h"

#include "../util/LivingRoles.h"

using namespace living;

// SpecTabRoles is THE canonical creation-time role mapping: AiFactory's
// spec-tab overload delegates to it (bit values static_asserted there), and
// tuple capability filtering, talent-path filtering, post-selection role
// verification and BotCreationResult all consume it. The mapping is
// core-independent - the same table serves Classic, TBC and WotLK; the Death
// Knight row is reachable only where the class exists.

LIVING_TEST(roles_druid_feral_is_tank_and_dps_independent_of_form)
{
    // Druid (11), Feral (tab 1): TANK|DPS with no aura/form input at all -
    // there is no way to make this mapping depend on Bear Form. This is the
    // Classic/TBC/WotLK druid-tank creation seam: capability, path filter and
    // verification all read this value.
    uint8_t const feral = SpecTabRoles(11, 1);
    LIVING_CHECK(feral == (ROLE_TANK | ROLE_DPS));

    // An explicit role=tank request is satisfied by containment.
    LIVING_CHECK(RolesSatisfy(feral, ROLE_TANK));
    LIVING_CHECK(RolesSatisfy(feral, ROLE_DPS));
    LIVING_CHECK(!RolesSatisfy(feral, ROLE_HEALER));

    // The class capability mask advertises the same: a druid CAN tank, so the
    // tuple selector and the talent filter can no longer disagree.
    LIVING_CHECK(RolesSatisfy(ClassRolesMask(11), ROLE_TANK));

    // Balance is DPS, Restoration heals.
    LIVING_CHECK(SpecTabRoles(11, 0) == ROLE_DPS);
    LIVING_CHECK(SpecTabRoles(11, 2) == ROLE_HEALER);
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
