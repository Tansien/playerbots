#pragma once

#include "OrganicActionKind.h"

#include <array>
#include <cstddef>

namespace living
{
    // Static classification of an action kind. This is the Organic compatibility
    // matrix (0002A) as data; the pure evaluator in OrganicPolicy.cpp turns it into
    // decisions using explicit request context.
    enum class OrganicClassification : uint8_t
    {
        AllowGameplay,   // ordinary core handlers and rules
        AllowAutomation, // ordinary gameplay chosen automatically; never bypasses eligibility or cost
        BootstrapOnly,   // ordinary creation path, legal only inside the managed bootstrap operation
        Deny,            // never mutates in Organic mode
        RequireAudit,    // named bounded compatibility/recovery action with an action-specific reconciler
        FixtureOnly,     // denied in production; available only to FIXTURE identities in a test profile
        FixtureBootstrap // pre-identity fixture creation: test profile + fixture source, no root yet
    };

    enum class OrganicActionCategory : uint8_t
    {
        Bootstrap,
        Randomization,
        Progression,
        Gear,
        Wealth,
        Inventory,
        Learning,
        Quests,
        CharacterInit,
        Social,
        Cheat,
        Relocation,
        Transport,
        Recovery,
        Lifecycle,
        Maintenance,
        Gameplay,
        Economy,
        Fixture
    };

    struct OrganicActionMetadata
    {
        OrganicActionKind kind;
        char const* name;                    // stable unique identifier for logs, reports, and audit rows
        OrganicActionCategory category;
        char const* legacySource;            // legacy code/config family that implements the path today
        OrganicClassification classification;
        char const* auditReconciler;         // required action-specific reconciler; null unless RequireAudit
        bool productionEligible;             // may ever be authorized for a production Organic identity
        bool fixtureOnly;                    // only meaningful under the fixture/test profile
        char const* designReference;         // normative design row/section implemented by this row
    };

    using OrganicActionMetadataTable =
        std::array<OrganicActionMetadata, static_cast<size_t>(OrganicActionKind::Count)>;

    OrganicActionMetadataTable const& AllOrganicActionMetadata();

    // Returns nullptr for values outside the inventory (unknown actions fail closed).
    OrganicActionMetadata const* TryGetOrganicActionMetadata(OrganicActionKind kind);

    // Exact stable-name lookup; returns nullptr when no row matches.
    OrganicActionMetadata const* FindOrganicActionByName(char const* name);

    char const* ToString(OrganicClassification value);
    char const* ToString(OrganicActionCategory value);
}
