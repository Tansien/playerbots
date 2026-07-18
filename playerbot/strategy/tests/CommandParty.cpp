#include "playerbot/playerbot.h"
#include "CommandParty.h"
#include "Grids/GridNotifiers.h"
#include "Grids/GridNotifiersImpl.h"
#include "Grids/CellImpl.h"
#include "TestContext.h"
#include "playerbot/PlayerbotMgr.h"
#include "Groups/Group.h"

using namespace ai;

TestResult CommandPartySpawnBot::Execute(const std::string& params, Player* bot, PlayerbotAI* ai, TestContext& ctx, std::string& message)
{
    if (!ai->GetHolder())
    {
        message = "Failed to spawn bot with params: " + params;
        return TestResult::IMPOSSIBLE;
    }

    // First call: build the typed options directly (the registered scenarios
    // already pass temporary=1; setting the typed field is idempotent, and no
    // duplicate textual key is ever appended), enqueue ONCE, and hold the
    // opaque completion token. Later calls poll the same token - the DSL
    // re-executes a PENDING command with identical params, so this command is
    // the natural polling loop.
    if (!ctx.spawnBotToken)
    {
        CreateBotOptions options;
        std::string error;
        if (!options.Parse(bot, params, error))
        {
            message = "Failed to parse spawn params '" + params + "': " + error;
            return TestResult::IMPOSSIBLE;
        }
        options.temporary = true; // exactly once, typed

        BotCreationResult result = ai->GetHolder()->CreateBot(bot, options);
        switch (result.status)
        {
            case living::BotCreateStatus::PendingPersistence:
                ctx.spawnBotToken = result.creationToken;
                message = "Bot creation queued; awaiting durable confirmation";
                return TestResult::PENDING;
            case living::BotCreateStatus::Created:
                // Defensive: creation is normally asynchronous.
                if (result.guid)
                {
                    ctx.spawnedBots.push_back(result.guid);
                    return TestResult::PASS;
                }
                message = "Creation reported Created without a GUID";
                return TestResult::IMPOSSIBLE;
            case living::BotCreateStatus::TransientFailure:
                // Database transiently unavailable: defer with bounded
                // tick-based backoff (the DSL re-executes PENDING commands).
                if (++ctx.spawnTransientRetries >= 30)
                {
                    message = "Database unavailable for bot creation (deferred attempts exhausted)";
                    return TestResult::IMPOSSIBLE;
                }
                message = "Database unavailable; deferring bot creation";
                return TestResult::PENDING;
            default:
                message = "Failed to spawn bot with params '" + params + "': "
                    + (result.messages.empty() ? "unknown" : result.messages.front());
                return TestResult::IMPOSSIBLE;
        }
    }

    living::CreationPollResult poll = PlayerbotHolder::PollBotCreation(ctx.spawnBotToken, true);
    switch (poll.status)
    {
        case living::CreationPollStatus::Pending:
            message = "Awaiting bot creation confirmation";
            return TestResult::PENDING;
        case living::CreationPollStatus::Created:
            ctx.spawnBotToken = 0;
            // The finalized GUID joins the cleanup ledger - the test must not
            // leave an untracked temporary character.
            ctx.spawnedBots.push_back(ObjectGuid(HIGHGUID_PLAYER, poll.guid));
            return TestResult::PASS;
        default:
            ctx.spawnBotToken = 0;
            message = "Bot creation failed: " + poll.message;
            return TestResult::IMPOSSIBLE;
    }
}

TestResult CommandPartyDespawnBot::Execute(const std::string& params, Player* bot,
                    PlayerbotAI* ai, TestContext& ctx, std::string& message)
{
    if (!ai->GetHolder())
    {
        message = "Failed to delete spawned bots";
        return TestResult::IMPOSSIBLE;
    }

    // Resolve an outstanding spawn first so a just-finalized temporary GUID
    // is deleted here instead of being orphaned.
    if (ctx.spawnBotToken)
    {
        living::CreationPollResult poll = PlayerbotHolder::PollBotCreation(ctx.spawnBotToken, true);
        if (poll.status == living::CreationPollStatus::Pending)
        {
            message = "Awaiting pending bot creation before despawn";
            return TestResult::PENDING;
        }

        ctx.spawnBotToken = 0;
        if (poll.status == living::CreationPollStatus::Created && poll.guid)
            ctx.spawnedBots.push_back(ObjectGuid(HIGHGUID_PLAYER, poll.guid));
    }

    for (auto& guid : ctx.spawnedBots)
    {
        ai->GetHolder()->DeleteBot(guid);
    }
    ctx.spawnedBots.clear();
    return TestResult::PASS;
}

TestResult CommandPartyForm::Execute(const std::string& params, Player* bot,
                    PlayerbotAI* ai, TestContext& ctx, std::string& message)
{
    Group* group = bot->GetGroup();
    if (!group)
    {
        group = new Group;
        if (!group->Create(bot->GetObjectGuid(), bot->GetName()))
        {
            delete group;
            message = "Failed to create group";
            return TestResult::IMPOSSIBLE;
        }
        sObjectMgr.AddGroup(group);
    }

    if (!ctx.spawnedBots.empty())
    {
        for (const auto& guid : ctx.spawnedBots)
        {
            if (!guid || !guid.IsPlayer())
                continue;

            Player* member = sObjectMgr.GetPlayer(guid);
            if (!member || !member->IsInWorld())
            {
                message = "Waiting for spawned bots to enter world before forming party";
                return TestResult::PENDING;
            }
        }
    }

    for (const auto& guid : ctx.spawnedBots)
    {
        if (!guid || !guid.IsPlayer())
            continue;

        Player* member = sObjectMgr.GetPlayer(guid);
        if (!member)
            continue;

        if (!group->IsMember(guid) && !group->AddMember(guid, member->GetName()))
        {
            message = "Failed to add member to group: " + std::string(member->GetName());
            return TestResult::IMPOSSIBLE;
        }

        if (PlayerbotAI* memberAi = member->GetPlayerbotAI())
            memberAi->HandleCommand(CHAT_MSG_WHISPER, "follow " + std::string(bot->GetName()), *bot);
    }

    return TestResult::PASS;
}

TestResult CommandPartySpawnGroup::Execute(const std::string& params, Player* bot,
                    PlayerbotAI* ai, TestContext& ctx, std::string& message)
{
    // First call: enqueue the group run once and hold its batch token. The
    // command then returns PENDING until every member reached a REAL terminal
    // creation result - enqueueing is not success.
    if (!ctx.spawnGroupBatchToken)
    {
        uint64 batchToken = 0;
        std::list<std::string> messages = sRandomPlayerbotMgr.HandleGroup(bot, params + " temporary=true", SEC_PLAYER, batchToken);
        if (!batchToken)
        {
            // Nothing was enqueued: an immediate parse/admission failure must
            // fail the test rather than leave it pending forever.
            message = "Group creation enqueued no members: "
                + (messages.empty() ? std::string("unknown") : messages.front());
            return TestResult::IMPOSSIBLE;
        }

        ctx.spawnGroupBatchToken = batchToken;
        message = "Group creation queued; awaiting member confirmations";
        return TestResult::PENDING;
    }

    living::BatchPollResult poll = PlayerbotHolder::PollBotCreationBatch(ctx.spawnGroupBatchToken, true);
    switch (poll.status)
    {
        case living::BatchPollStatus::Pending:
            message = "Awaiting group member confirmations";
            return TestResult::PENDING;
        case living::BatchPollStatus::Complete:
        {
            ctx.spawnGroupBatchToken = 0;
            // Every finalized member joins the cleanup ledger.
            for (uint32 const guid : poll.finalizedGuids)
                ctx.spawnedBots.push_back(ObjectGuid(HIGHGUID_PLAYER, guid));

            if (!poll.failures.empty())
            {
                message = "Group creation completed with failures: " + poll.failures.front();
                return TestResult::IMPOSSIBLE;
            }

            // Desired-size invariant, independent of the failure list: the
            // initial run stopping early (terminal error, attempt budget)
            // must not let a partial group report PASS.
            if (poll.undersized)
            {
                message = "Group creation completed undersized: "
                    + std::to_string(poll.preexistingMembers + poll.finalizedGuids.size())
                    + " of " + std::to_string(poll.desiredSize) + " members";
                return TestResult::IMPOSSIBLE;
            }

            return TestResult::PASS;
        }
        default:
            ctx.spawnGroupBatchToken = 0;
            message = "Group creation batch expired or unknown";
            return TestResult::IMPOSSIBLE;
    }
}