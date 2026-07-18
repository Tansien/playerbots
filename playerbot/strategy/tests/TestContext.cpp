#include "TestContext.h"
#include "playerbot/PlayerbotMgr.h"

using namespace ai;

void TestContext::Reset()
{
    script.clear();
    pc = 0;
    observing = false;
    testStartTime = 0;
    monitorTime = 0;
    waitTime = 0;
    monitors.clear();
    deferredCleanups.clear();
    cleanupPc = 0;
    cleanupPrepared = false;
    whoResponded = false;
    result = TestResult::PENDING;
    resultMessage.clear();
    testName.clear();
    testStartPosition = WorldPosition();
    destinationPosition = GuidPosition();

    for (ObjectGuid const& guid : spawnedBots)
    {
        if (guid && guid.IsPlayer())
        {
            sRandomPlayerbotMgr.DeleteBot(guid, true);
        }
    }
    spawnedBots.clear();

    // A creation still pending at reset finalizes on its own (bounded), so the
    // reset must NOT poll-once-and-clear: that abandoned a still-pending token
    // (a later finalization leaked a temporary character), and for a partial
    // batch it deleted already-finalized GUIDs out from under the still-live
    // batch. Instead, transfer ownership to the deferred cleanup owner, which
    // survives the reset, polls each token to terminal, and deletes every
    // finalized temporary character exactly once.
    if (spawnBotToken)
    {
        PlayerbotHolder::AbandonCreationToken(spawnBotToken);
        spawnBotToken = 0;
    }

    if (spawnGroupBatchToken)
    {
        PlayerbotHolder::AbandonBatchToken(spawnGroupBatchToken);
        spawnGroupBatchToken = 0;
    }

    spawnTransientRetries = 0;
}