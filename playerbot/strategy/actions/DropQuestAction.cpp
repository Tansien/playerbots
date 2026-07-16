
#include "playerbot/playerbot.h"
#include "DropQuestAction.h"
#include "playerbot/living/util/LivingQuestSlots.h"

using namespace ai;

bool DropQuestAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    std::string link = event.getParam();
    if (!GetMaster())
        return false;

    PlayerbotChatHandler handler(GetMaster());
    uint32 entry = handler.extractQuestId(link);
    bool dropped = false;

    // remove all quest entries for 'entry' from quest log
    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 logQuest = bot->GetQuestSlotQuestId(slot);
        Quest const* quest = sObjectMgr.GetQuestTemplate(logQuest);
        if (!quest)
            continue;

        if (logQuest == entry || link.find(quest->GetTitle()) != std::string::npos || link == "all")
        {
            bot->SetQuestSlot(slot, 0);

            // we ignore unequippable quest items in this case, its' still be equipped
            bot->TakeQuestSourceItem(logQuest, false);
            entry = logQuest;

            bot->SetQuestStatus(entry, QUEST_STATUS_NONE);
            bot->getQuestStatusMap()[entry].m_rewarded = false;

            dropped = true;

            if (link != "all")
                break;
        }
    }

    if(dropped)
        ai->TellPlayer(requester, BOT_TEXT("quest_remove"));

    return dropped;
}

bool CleanQuestLogAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    std::string link = event.getParam();
    if (ai->HasActivePlayerMaster())
        return false;

    // Complete non-mutating preflight BEFORE any pruning pass: one orphaned slot
    // quarantines the whole log from automated cleanup. The pruning passes below
    // drop VALID quests toward a capacity target, so pruning "around" an orphan
    // sacrifices real quests for capacity the orphan still holds (24 orphans plus
    // one valid quest used to drop the only valid quest and keep all 24 orphans).
    // Orphans stay counted, logged, and untouched; cleanup fails instead.
    living::QuestLogPreflight preflight;
    std::ostringstream orphanList;
    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = bot->GetQuestSlotQuestId(slot);
        bool const hasTemplate = questId != 0 && sObjectMgr.GetQuestTemplate(questId) != nullptr;
        if (preflight.Observe(questId, hasTemplate))
            orphanList << (preflight.orphans > 1 ? ", " : "") << "slot " << uint32(slot) << " quest " << questId;
    }

    // Cleanup runs from a recurring trigger: log the quarantine once per orphan-set
    // change, not once per orphan per attempt.
    uint64 const fingerprint = preflight.OrphanFingerprint();
    if (fingerprint != loggedOrphanFingerprint)
    {
        loggedOrphanFingerprint = fingerprint;
        if (!preflight.CleanupPermitted())
            sLog.outError("Bot %s: quarantined %u orphaned quest-log slot(s) with no template (%s); "
                "automated quest-log cleanup is disabled for this character until a core-backed atomic repair exists",
                bot->GetName(), uint32(preflight.orphans), orphanList.str().c_str());
    }

    if (!preflight.CleanupPermitted())
        return false;

    uint8 totalQuests = 0;

    DropQuestType(requester, totalQuests); //Count the total quests

    if (MAX_QUEST_LOG_SIZE - totalQuests > 6)
    {
        DropQuestType(requester, totalQuests, MAX_QUEST_LOG_SIZE, true, true); //Drop failed quests
        return true;
    }

    if (AI_VALUE(bool, "can fight equal")) //Only drop gray quests when able to fight proper lvl quests.
    {
        DropQuestType(requester, totalQuests, MAX_QUEST_LOG_SIZE - 6); //Drop gray/red quests.
        DropQuestType(requester, totalQuests, MAX_QUEST_LOG_SIZE - 6, false, true); //Drop gray/red quests with progress.
        DropQuestType(requester, totalQuests, MAX_QUEST_LOG_SIZE - 6, false, true, true); //Drop gray/red completed quests.
    }

    if (MAX_QUEST_LOG_SIZE - totalQuests > 4)
        return true;

    DropQuestType(requester, totalQuests, MAX_QUEST_LOG_SIZE - 4, true); //Drop quests without progress.

    if (MAX_QUEST_LOG_SIZE - totalQuests > 2)
        return true;

    DropQuestType(requester, totalQuests, MAX_QUEST_LOG_SIZE - 2, true, true); //Drop quests with progress.

    if (MAX_QUEST_LOG_SIZE - totalQuests > 0)
        return true;

    DropQuestType(requester, totalQuests, MAX_QUEST_LOG_SIZE - 1, true, true, true); //Drop completed quests.

    if (MAX_QUEST_LOG_SIZE - totalQuests > 0)
        return true;

    return false;
}

void CleanQuestLogAction::DropQuestType(Player* requester, uint8 &numQuest, uint8 wantNum, bool isGreen, bool hasProgress, bool isComplete)
{
    std::vector<uint8> slots;

    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        slots.push_back(slot);


    if (wantNum < 100)
    {
        std::shuffle(slots.begin(), slots.end(), *GetRandomGenerator());
    }

    for (uint8 slot : slots)
    {
        uint32 questId = bot->GetQuestSlotQuestId(slot);

        if (!living::IsQuestSlotOccupied(questId))
            continue;

        // Counting pass: count EVERY occupied slot and mutate nothing. Failed,
        // class-specific, and orphaned (template-less) quests all occupy log space;
        // skipping any of them made a full log read as having room and wedged quest
        // acceptance, and dropping during "counting" desynced the counter (an
        // unconditional decrement could wrap the uint8 to 255 and make the later
        // pruning stages discard progressed or completed quests).
        if (wantNum == 100)
        {
            numQuest++;
            continue;
        }

        // The preflight in Execute quarantines any log containing an orphan before
        // this runs, so a missing template here is only a race backstop: skip, never
        // drop or clear.
        Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
        if (!quest)
            continue;

        if (bot->GetQuestStatus(questId) != QUEST_STATUS_FAILED)
        {
            if (quest->GetRequiredClasses()) //Do not drop class specific quests
                continue;

            int32 lowLevelDiff = sWorld.getConfig(CONFIG_INT32_QUEST_LOW_LEVEL_HIDE_DIFF);
            if (lowLevelDiff < 0 || bot->GetLevel() <= bot->GetQuestLevelForPlayer(quest) + uint32(lowLevelDiff)) //Quest is not gray
            {
                if (bot->GetLevel() + 5 > bot->GetQuestLevelForPlayer(quest)) //Quest is not red
                    if (!isGreen)
                        continue;
            }
            else //Quest is gray
            {
                if (isGreen)
                    continue;
            }

            if (HasProgress(bot, quest) && !hasProgress && bot->GetQuestStatus(questId) != QUEST_STATUS_FAILED)
                continue;

            if (bot->GetQuestStatus(questId) == QUEST_STATUS_COMPLETE && !isComplete)
                continue;

            if (numQuest <= wantNum)
                continue;
        }

        //Drop quest. Every occupied slot was counted, so every drop decrements,
        //floored at zero as an underflow backstop.
        bot->GetPlayerbotAI()->DropQuest(questId);

        if (numQuest > 0)
            numQuest--;

        ai->TellPlayer(requester, BOT_TEXT("quest_remove") + " " + chat->formatQuest(quest), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
    }
}

bool CleanQuestLogAction::HasProgress(Player* bot, Quest const* quest)
{
    uint32 questId = quest->GetQuestId();

    if (bot->GetQuestStatus(questId) == QUEST_STATUS_COMPLETE)
        return true;

    QuestStatusData questStatus = bot->getQuestStatusMap()[questId];

    for (int i = 0; i < QUEST_OBJECTIVES_COUNT; i++)
    {
        if (!quest->ObjectiveText[i].empty())
            return true;

        if (quest->ReqItemId[i])
        {
            int required = quest->ReqItemCount[i];
            int available = questStatus.m_itemcount[i];
            if (available > 0 && required > 0)
                return true;
        }

        if (quest->ReqCreatureOrGOId[i])
        {
            int required = quest->ReqCreatureOrGOCount[i];
            int available = questStatus.m_creatureOrGOcount[i];

            if (available > 0 && required > 0)
                return true;
        }
    }

    return false;
}