#pragma once

#include "OrganicActionKind.h"

#include <array>
#include <cstddef>

namespace living
{
    // Static classification of an action kind: the normative Organic
    // compatibility matrix (0002A) as data. The pure evaluator turns rows into
    // decisions; rows must not be weakened without a design revision.
    enum class OrganicClassification : uint8_t
    {
        AllowGameplay,   // ordinary core handlers and rules
        AllowAutomation, // ordinary gameplay chosen automatically; never bypasses eligibility or cost
        BootstrapOnly,   // ordinary creation path, legal only inside the managed bootstrap operation
        Deny,            // never mutates in Organic mode
        RequireAudit,    // named bounded compatibility/recovery action with an action-specific reconciler
        FixtureOnly      // denied in production; reserved for fixture identities under a test profile
    };

    // Whether a classification may ever be authorized for a production Organic
    // identity. Derived, not stored: a metadata row cannot disagree with it.
    constexpr bool IsProductionEligible(OrganicClassification classification)
    {
        return classification == OrganicClassification::AllowGameplay
            || classification == OrganicClassification::AllowAutomation
            || classification == OrganicClassification::BootstrapOnly
            || classification == OrganicClassification::RequireAudit;
    }

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
        Gameplay,
        Economy,
        Lifecycle,
        Maintenance,
        Fixture
    };

    struct OrganicActionMetadata
    {
        OrganicActionKind kind;
        char const* name;             // stable unique identifier for logs, reports, and audit rows
        OrganicActionCategory category;
        char const* legacySource;     // legacy code/config family implementing the path today
        OrganicClassification classification;
        char const* auditReconciler;  // action-specific reconciler; non-null exactly when RequireAudit
        char const* designReference;  // normative design row/section justifying this row
    };

    using OrganicActionMetadataTable =
        std::array<OrganicActionMetadata, static_cast<size_t>(OrganicActionKind::Count)>;

    OrganicActionMetadataTable const& AllOrganicActionMetadata();

    // Returns nullptr for any value outside the inventory: unknown actions fail
    // closed at the lookup layer, before any policy decision.
    OrganicActionMetadata const* TryGetOrganicActionMetadata(OrganicActionKind kind);

    // Exact stable-name lookup; returns nullptr when no row matches.
    OrganicActionMetadata const* FindOrganicActionByName(char const* name);

    char const* ToString(OrganicClassification value);
    char const* ToString(OrganicActionCategory value);
}
