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

    // A creation still pending at reset finalizes on its own (bounded) and
    // may leave a temporary character until the finalizer/quarantine path
    // resolves it - collect any already-terminal result so a finalized GUID
    // is deleted rather than orphaned.
    if (spawnBotToken)
    {
        living::CreationPollResult const poll = PlayerbotHolder::PollBotCreation(spawnBotToken, true);
        if (poll.status == living::CreationPollStatus::Created && poll.guid)
            sRandomPlayerbotMgr.DeleteBot(ObjectGuid(HIGHGUID_PLAYER, poll.guid), true);
        spawnBotToken = 0;
    }

    if (spawnGroupBatchToken)
    {
        living::BatchPollResult const poll = PlayerbotHolder::PollBotCreationBatch(spawnGroupBatchToken, true);
        for (uint32 const guid : poll.finalizedGuids)
            sRandomPlayerbotMgr.DeleteBot(ObjectGuid(HIGHGUID_PLAYER, guid), true);
        spawnGroupBatchToken = 0;
    }
}