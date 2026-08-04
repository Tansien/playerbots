#include "LivingTest.h"

#include "playerbot/living/policy/OrganicActionMetadata.h"

#include <cstring>
#include <set>
#include <string>

// Inventory-mechanics tests for the Organic action inventory: unknown values
// fail closed, lookups are exact, and the table stays seeded by the normative
// 0002A matrix. Per-row policy decisions are tested with the evaluator.

using living::AllOrganicActionMetadata;
using living::FindOrganicActionByName;
using living::IsProductionEligible;
using living::OrganicActionKind;
using living::OrganicActionMetadata;
using living::OrganicClassification;
using living::TryGetOrganicActionMetadata;

namespace
{
    constexpr size_t KIND_COUNT = static_cast<size_t>(OrganicActionKind::Count);
}

LIVING_TEST(InventoryUnknownKindsFailClosed)
{
    LIVING_CHECK(TryGetOrganicActionMetadata(OrganicActionKind::Count) == nullptr);
    LIVING_CHECK(TryGetOrganicActionMetadata(static_cast<OrganicActionKind>(KIND_COUNT + 1)) == nullptr);
    LIVING_CHECK(TryGetOrganicActionMetadata(static_cast<OrganicActionKind>(0xFFFF)) == nullptr);
}

LIVING_TEST(InventoryLookupReturnsTheMatchingRow)
{
    for (size_t i = 0; i < KIND_COUNT; ++i)
    {
        OrganicActionKind const kind = static_cast<OrganicActionKind>(i);
        OrganicActionMetadata const* row = TryGetOrganicActionMetadata(kind);
        LIVING_CHECK(row != nullptr);
        LIVING_CHECK(row->kind == kind);
        LIVING_CHECK(row == &AllOrganicActionMetadata()[i]);
    }
}

LIVING_TEST(InventoryNameLookupIsExact)
{
    // Stable names key logs, reports, and audit rows: they must be unique
    // (enforced here, not in constexpr, to stay clear of MSVC's constexpr-step
    // budget) and each must round-trip to exactly its own row.
    std::set<std::string> names;
    for (OrganicActionMetadata const& row : AllOrganicActionMetadata())
    {
        LIVING_CHECK(names.insert(row.name).second);
        LIVING_CHECK(FindOrganicActionByName(row.name) == &row);
    }

    LIVING_CHECK(FindOrganicActionByName("NO_SUCH_ACTION") == nullptr);
    LIVING_CHECK(FindOrganicActionByName("") == nullptr);
    LIVING_CHECK(FindOrganicActionByName(nullptr) == nullptr);

    // Prefixes and case variants must not match.
    LIVING_CHECK(FindOrganicActionByName("CHEAT_") == nullptr);
    LIVING_CHECK(FindOrganicActionByName("cheat_gold") == nullptr);
}

LIVING_TEST(InventoryStringConversionsAreStable)
{
    LIVING_CHECK(std::strcmp(ToString(OrganicClassification::Deny), "Deny") == 0);
    LIVING_CHECK(std::strcmp(ToString(OrganicClassification::RequireAudit), "RequireAudit") == 0);

    for (OrganicActionMetadata const& row : AllOrganicActionMetadata())
    {
        LIVING_CHECK(std::strstr(ToString(row.classification), "INVALID") == nullptr);
        LIVING_CHECK(std::strstr(ToString(row.category), "INVALID") == nullptr);
    }

    LIVING_CHECK(std::strcmp(ToString(static_cast<OrganicClassification>(0xFF)), "INVALID_CLASSIFICATION") == 0);
    LIVING_CHECK(std::strcmp(ToString(static_cast<living::OrganicActionCategory>(0xFF)), "INVALID_CATEGORY") == 0);
}

LIVING_TEST(InventoryProductionEligibilityFollowsClassification)
{
    LIVING_CHECK(IsProductionEligible(OrganicClassification::AllowGameplay));
    LIVING_CHECK(IsProductionEligible(OrganicClassification::AllowAutomation));
    LIVING_CHECK(IsProductionEligible(OrganicClassification::BootstrapOnly));
    LIVING_CHECK(IsProductionEligible(OrganicClassification::RequireAudit));
    LIVING_CHECK(!IsProductionEligible(OrganicClassification::Deny));
    LIVING_CHECK(!IsProductionEligible(OrganicClassification::FixtureOnly));
}

LIVING_TEST(InventoryOnlyTheThreeNamedActionsRequireAudit)
{
    // 0002 section 3: only the named audited actions in 0002C are allowed in 0.1.
    std::set<OrganicActionKind> audited;
    for (OrganicActionMetadata const& row : AllOrganicActionMetadata())
        if (row.classification == OrganicClassification::RequireAudit)
            audited.insert(row.kind);

    std::set<OrganicActionKind> const expected = {
        OrganicActionKind::PUBLIC_TRANSPORT_TRANSFER,
        OrganicActionKind::TRANSPORT_GROUP_SYNC,
        OrganicActionKind::STUCK_EMERGENCY_TELEPORT,
    };
    LIVING_CHECK(audited == expected);
}

LIVING_TEST(InventoryOnlyCoreCreationIsBootstrapOnly)
{
    // 0002A: managed-bootstrap creation is the single conditional-gameplay row.
    for (OrganicActionMetadata const& row : AllOrganicActionMetadata())
        LIVING_CHECK((row.classification == OrganicClassification::BootstrapOnly)
            == (row.kind == OrganicActionKind::CORE_CHARACTER_CREATE));
}

LIVING_TEST(InventoryCoversEvery0002AMechanism)
{
    // Every mechanism row of docs/design/0002-organic-policy-compatibility-matrix.md
    // must stay covered by its exact number of metadata rows, all carrying the
    // classification the matrix assigns. Pinning the count keeps a multi-row
    // mechanism (for example the 13 cheat bits) from silently losing a member;
    // pinning the classification keeps a row from being weakened without a
    // design revision. Backticks in the document's row titles are dropped here.
    struct MechanismExpectation
    {
        char const* mechanism;
        OrganicClassification classification;
        int rowCount;
    };

    static MechanismExpectation const MECHANISMS[] = {
        { "Core character creation", OrganicClassification::BootstrapOnly, 1 },
        { "Instant/full/incremental randomization", OrganicClassification::Deny, 3 },
        { "Randomizer hotfix migration", OrganicClassification::Deny, 1 },
        { "Level/XP assignment", OrganicClassification::Deny, 2 },
        { "Player-level synchronization", OrganicClassification::Deny, 1 },
        { "XP multiplier", OrganicClassification::Deny, 1 },
        { "Random gear initialization", OrganicClassification::Deny, 1 },
        { "Periodic gear upgrade/autogear", OrganicClassification::Deny, 1 },
        { "Starting/periodic money", OrganicClassification::Deny, 2 },
        { "Temporary-money flight/purchase tricks", OrganicClassification::Deny, 1 },
        { "Bags and generated inventory", OrganicClassification::Deny, 1 },
        { "Runtime ammo replenishment", OrganicClassification::Deny, 1 },
        { "Food/ammo/reagents/potions/consumables", OrganicClassification::Deny, 1 },
        { "Skills/professions", OrganicClassification::Deny, 1 },
        { "Spell initialization", OrganicClassification::Deny, 1 },
        { "Free trainer mode", OrganicClassification::Deny, 1 },
        { "Auto trainer/quest/drop learning", OrganicClassification::Deny, 3 },
        { "Auto-learn reward-item fabrication", OrganicClassification::Deny, 1 },
        { "Earned talent spending", OrganicClassification::AllowAutomation, 1 },
        { "Enchant/gem/template generation", OrganicClassification::Deny, 1 },
        { "Auto-enchant on upgrade", OrganicClassification::Deny, 1 },
        { "Prequests/global bot quest rewards", OrganicClassification::Deny, 2 },
        { "Generic quest completion commands", OrganicClassification::Deny, 1 },
        { "Required-money fabrication during quest completion", OrganicClassification::Deny, 1 },
        { "Quest sync to bot", OrganicClassification::Deny, 1 },
        { "Quest sync to real player", OrganicClassification::Deny, 1 },
        { "World buffs", OrganicClassification::Deny, 1 },
        { "Reputation initialization", OrganicClassification::Deny, 1 },
        { "Taxi-node initialization", OrganicClassification::Deny, 1 },
        { "Mount initialization", OrganicClassification::Deny, 1 },
        { "Pet initialization", OrganicClassification::Deny, 1 },
        { "Guild bootstrap/free tabard", OrganicClassification::Deny, 1 },
        { "Arena-team bootstrap", OrganicClassification::Deny, 1 },
        { "All 13 configured cheat bits", OrganicClassification::Deny, 13 },
        { "Per-bot runtime cheat override", OrganicClassification::FixtureOnly, 1 },
        { "Normal quest reward/loot/equip", OrganicClassification::AllowGameplay, 4 },
        { "Vendor/repair/train", OrganicClassification::AllowAutomation, 2 },
        { "AH/mail/trade", OrganicClassification::AllowAutomation, 3 },
        { "Random teleport", OrganicClassification::Deny, 1 },
        { "Teleport near player", OrganicClassification::Deny, 1 },
        { "RPG camp/grind teleport", OrganicClassification::Deny, 1 },
        { "Legacy transport mode 1/2", OrganicClassification::Deny, 1 },
        { "Canonical public transport transfer", OrganicClassification::RequireAudit, 1 },
        { "Protected transport group sync", OrganicClassification::RequireAudit, 1 },
        { "Taxi/hearth/portal/area trigger", OrganicClassification::AllowGameplay, 3 },
        { "Free/innkeeper summon", OrganicClassification::Deny, 1 },
        { "Eligible player/spell summon", OrganicClassification::AllowGameplay, 1 },
        { "Automatic manager revive", OrganicClassification::Deny, 1 },
        { "Corpse release/spirit healer/resurrection spell", OrganicClassification::AllowGameplay, 1 },
        { "Stuck emergency relocation", OrganicClassification::RequireAudit, 1 },
        { "Legacy login path", OrganicClassification::Deny, 1 },
        { "Legacy timed online/offline state", OrganicClassification::Deny, 1 },
        // 0002A classifies the future admin bypass as audit-only, but 0.1 has no
        // approved reconciler for it, so the inventory row denies (0002 section 3).
        { "Explicit bounded admin mutation", OrganicClassification::Deny, 1 },
        { "Broad init/upgrade/refresh/maintenance", OrganicClassification::FixtureOnly, 1 },
        { "Delete/reset population", OrganicClassification::Deny, 1 },
        { "Raw delete/reset SQL", OrganicClassification::Deny, 1 },
        { "Offline material progression", OrganicClassification::Deny, 1 },
    };

    for (MechanismExpectation const& expected : MECHANISMS)
    {
        int matches = 0;
        bool classificationsMatch = true;

        for (OrganicActionMetadata const& row : AllOrganicActionMetadata())
        {
            if (std::strstr(row.designReference, expected.mechanism) == nullptr)
                continue;

            ++matches;
            if (row.classification != expected.classification)
                classificationsMatch = false;
        }

        if (matches != expected.rowCount || !classificationsMatch)
            throw living::test::CheckFailure{ __FILE__, __LINE__, expected.mechanism };
    }
}

LIVING_TEST(InventoryClassificationPartitionIsExact)
{
    // The complete non-Deny partition of the inventory, pinned kind by kind:
    // every row not named here must classify Deny. A denied fabrication path
    // cannot drift to an allow classification (or vice versa) without editing
    // this expectation alongside the table.
    std::set<OrganicActionKind> const allowGameplay = {
        OrganicActionKind::GAMEPLAY_QUEST_ACCEPT,
        OrganicActionKind::GAMEPLAY_DEATH_RECOVERY,
        OrganicActionKind::GAMEPLAY_LOOT,
        OrganicActionKind::GAMEPLAY_QUEST_REWARD,
        OrganicActionKind::GAMEPLAY_EQUIP,
        OrganicActionKind::GAMEPLAY_TAXI_FLIGHT,
        OrganicActionKind::GAMEPLAY_HEARTHSTONE,
        OrganicActionKind::GAMEPLAY_ELIGIBLE_PORTAL,
        OrganicActionKind::GAMEPLAY_ELIGIBLE_SUMMON,
    };
    std::set<OrganicActionKind> const allowAutomation = {
        OrganicActionKind::TALENT_SPEND_EARNED,
        OrganicActionKind::TRAINER_PURCHASE,
        OrganicActionKind::VENDOR_REPAIR_TRANSACTION,
        OrganicActionKind::AUCTION_TRANSACTION,
        OrganicActionKind::MAIL_TRANSACTION,
        OrganicActionKind::TRADE_TRANSACTION,
    };
    std::set<OrganicActionKind> const bootstrapOnly = {
        OrganicActionKind::CORE_CHARACTER_CREATE,
    };
    std::set<OrganicActionKind> const requireAudit = {
        OrganicActionKind::PUBLIC_TRANSPORT_TRANSFER,
        OrganicActionKind::TRANSPORT_GROUP_SYNC,
        OrganicActionKind::STUCK_EMERGENCY_TELEPORT,
    };
    std::set<OrganicActionKind> const fixtureOnly = {
        OrganicActionKind::CHEAT_RUNTIME_OVERRIDE,
        OrganicActionKind::BROAD_MAINTENANCE_COMMAND,
        OrganicActionKind::DEBUG_MUTATION_COMMAND,
        OrganicActionKind::FIXTURE_CHARACTER_CREATE,
        OrganicActionKind::FIXTURE_PROVISION,
    };

    for (OrganicActionMetadata const& row : AllOrganicActionMetadata())
    {
        OrganicClassification expected = OrganicClassification::Deny;
        if (allowGameplay.count(row.kind) != 0)
            expected = OrganicClassification::AllowGameplay;
        else if (allowAutomation.count(row.kind) != 0)
            expected = OrganicClassification::AllowAutomation;
        else if (bootstrapOnly.count(row.kind) != 0)
            expected = OrganicClassification::BootstrapOnly;
        else if (requireAudit.count(row.kind) != 0)
            expected = OrganicClassification::RequireAudit;
        else if (fixtureOnly.count(row.kind) != 0)
            expected = OrganicClassification::FixtureOnly;

        if (row.classification != expected)
            throw living::test::CheckFailure{ __FILE__, __LINE__, row.name };
    }
}
