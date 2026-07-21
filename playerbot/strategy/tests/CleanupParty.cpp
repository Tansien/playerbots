#include "playerbot/playerbot.h"
#include "CleanupParty.h"

using namespace ai;

// =====================================================
// CleanupParty implementation
// =====================================================
TestResult CleanupParty::Execute(const std::string& params, Player* bot,
                    PlayerbotAI* ai, TestContext& ctx, std::string& message)
{
    // Default cleanup: despawn all spawned bots
    for (auto it = ctx.spawnedBots.begin(); it != ctx.spawnedBots.end();)
    {
        if (it->IsPlayer())
        {
            if (!ai->GetHolder() || !ai->GetHolder()->DeleteBot(*it, false))
            {
                ++it;
                continue;
            }
        }
        else if (Creature* creature = ai->GetCreature(*it))
        {
            creature->ForcedDespawn();
        }

        it = ctx.spawnedBots.erase(it);
    }

    if (!ctx.spawnedBots.empty())
    {
        message = "Spawned-bot deletion could not be durably owned; cleanup will retry";
        return TestResult::PENDING;
    }

    ctx.observing = false;
    return TestResult::PASS;
}
