#pragma once

// Observe-first Organic interception (design 0002 sections 2 and 5). A legacy
// fabrication call site evaluates the pure policy and records the decision
// through the process telemetry sink; nothing is enforced yet. Recording
// returns void by contract, so no call site can branch on a decision and every
// intercepted path keeps its existing behavior exactly.
//
// This adapter lives outside playerbot/living/ because it touches CMaNGOS
// types: the pure evaluator never learns about them.

#include "playerbot/living/policy/OrganicPolicy.h"

class Player;

namespace living_observer
{
    // Records one intercepted call for a bot character. Only characters on a
    // random-bot account are recorded; player-owned alts stay outside Living
    // Realm scope (0002 section 2). A null bot or session records nothing.
    void RecordDecision(living::OrganicActionKind kind, living::OrganicSourceKind source, Player* bot);

    // Records one intercepted call by character guid. Batch operations with no
    // single subject (guild/arena-team creation, population reset) pass guid 0
    // and are always recorded.
    void RecordDecision(living::OrganicActionKind kind, living::OrganicSourceKind source, uint32 characterGuid);
}
