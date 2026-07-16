#pragma once
#include "GenericActions.h"

namespace ai
{
    class DropQuestAction : public ChatCommandAction
    {
    public:
        DropQuestAction(PlayerbotAI* ai) : ChatCommandAction(ai, "drop quest") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUsefulWhenStunned() override { return true; }

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "drop quest"; }
        virtual std::string GetHelpDescription()
        {
            return "This command makes the bot drop a specific quest.\n"
                   "Usage: drop [quest_name or link]\n"
                   "Example: drop [The Missing Diplomat] (drops the specified quest)";
        }
        virtual std::vector<std::string> GetUsedActions() { return {}; }
        virtual std::vector<std::string> GetUsedValues() { return {}; }
#endif 
    };

    class CleanQuestLogAction : public ChatCommandAction
    {
    public:
        CleanQuestLogAction(PlayerbotAI* ai) : ChatCommandAction(ai, "clean quest log") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUsefulWhenStunned() override { return true; }

        virtual bool isUseful() override { return ai->HasStrategy("rpg quest", BotState::BOT_STATE_NON_COMBAT); }

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "clean quest log"; }
        virtual std::string GetHelpDescription()
        {
            return "This command makes the bot automatically drop certain quests to make room for new ones.\n"
                   "It selects quests to drop based on priority (ie. gray without progress ones first)\n";
        }
        virtual std::vector<std::string> GetUsedActions() { return {}; }
        virtual std::vector<std::string> GetUsedValues() { return {"can fight equal"}; }
#endif 

        void DropQuestType(Player* requester, uint8& numQuest, uint8 wantNum = 100, bool isGreen = false, bool hasProgress = false, bool isComplete = false);

        static bool HasProgress(Player* bot, Quest const* quest);

    private:
        // Fingerprint of the orphan set last logged for this bot. Cleanup runs
        // from a recurring trigger; the quarantine is logged only when the orphan
        // set changes instead of once per orphan per attempt. Action instances are
        // per-bot and live with the AI, so this state is bounded and transient.
        uint64 loggedOrphanFingerprint = 0;
    };
}
