#include "BotProvenance.h"

namespace living
{
    char const* ToString(BotProvenance value)
    {
        switch (value)
        {
            case BotProvenance::ORGANIC_CREATED: return "ORGANIC_CREATED";
            case BotProvenance::FIXTURE: return "FIXTURE";
            case BotProvenance::LEGACY_UNMANAGED: return "LEGACY_UNMANAGED";
            case BotProvenance::Count: break;
        }

        return "INVALID_PROVENANCE";
    }

    bool SatisfiesOrganicProvenance(BotProvenance value)
    {
        return value == BotProvenance::ORGANIC_CREATED;
    }

    bool IsAllowedProvenanceTransition(BotProvenance from, BotProvenance to)
    {
        if (from >= BotProvenance::Count || to >= BotProvenance::Count)
            return false;

        // Identity provenance never changes; a new provenance requires a new
        // identity nonce, which is a creation, not a transition.
        return from == to;
    }
}
