#pragma once

#include <cstdint>

namespace living
{
    // Identity provenance (designs 0003 section 2 and 0006 section 7). Provenance is
    // assigned exactly once when the identity root (character guid + identity nonce)
    // is created and never changes for that identity: acquiring a different
    // provenance requires retiring the identity and creating a new nonce through the
    // managed reset/bootstrap operation (0.1 BootstrapPolicy=require_fresh).
    enum class BotProvenance : uint8_t
    {
        ORGANIC_CREATED = 0,  // created by the managed Organic bootstrap
        FIXTURE = 1,          // test fixture; never a production Organic character
        LEGACY_UNMANAGED = 2, // pre-existing random bot without managed provenance

        Count
    };

    char const* ToString(BotProvenance value);

    // Only ORGANIC_CREATED identities may enter production Organic schedules,
    // fairness accounting, and identity semantics (0001 invariant 13).
    bool SatisfiesOrganicProvenance(BotProvenance value);

    // Provenance is immutable per identity: no transition between distinct values is
    // legal. In particular LEGACY_UNMANAGED is never silently certified as
    // ORGANIC_CREATED and FIXTURE is never promoted into the production population.
    bool IsAllowedProvenanceTransition(BotProvenance from, BotProvenance to);
}
