#pragma once

#include "playerbot/playerbot.h"
#include "playerbot/Talentspec.h"
#include "GenericActions.h"

namespace ai
{
    class ChangeTalentsAction : public ChatCommandAction
    {
    public:
        ChangeTalentsAction(PlayerbotAI* ai, std::string name = "talents") : ChatCommandAction(ai, name) {}

        virtual bool isUsefulWhenStunned() override { return true; }

    public:
        virtual bool Execute(Event& event) override;

        // Result of a side-effect-free talent selection: the plan was applied to
        // the (possibly transient, not yet persisted) Player only. Spec metadata
        // is persisted separately via PersistTalentSpec, and only after the
        // character itself has been accepted - selection during creation must
        // leave no DB/cache rows for a GUID that may still be rejected.
        struct TalentSelectionResult
        {
            bool evaluated = false;  // false = no talent points, nothing selected or to persist
            int specId = -1;         // chosen premade spec, -1 = none/custom link
            std::string specLink;    // custom link when specId == -1
            bool hadExistingSpec = false;
        };

        // Stages 1-2: select and apply a talent plan to the player. No
        // persistence of any kind.
        static TalentSelectionResult SelectTalents(Player* bot, std::ostringstream* out, BotRoles role = BotRoles::BOT_ROLE_NONE);

        // Stage 5: persist the chosen spec metadata. Call only after the
        // character has been accepted/persisted. Returns false when the event
        // writes failed.
        static bool PersistTalentSpec(Player* bot, TalentSelectionResult const& selection);

        // Legacy combined behavior for LIVE bots (select + persist).
        static bool AutoSelectTalents(Player* bot, std::ostringstream* out, BotRoles role = BotRoles::BOT_ROLE_NONE);
    private:
        static std::vector<TalentPath*> getPremadePaths(uint8 cls, std::string findName, BotRoles role = BotRoles::BOT_ROLE_NONE);
        static std::vector<TalentPath*> getPremadePaths(Player* bot, TalentSpec* oldSpec);
        static TalentPath* getPremadePath(uint8 cls, int id);
        static void listPremadePaths(uint8 cls, std::vector<TalentPath*> paths, std::ostringstream* out);
        static TalentPath* PickPremadePath(std::vector<TalentPath*> paths, bool useProbability);
        static TalentSpec* GetBestPremadeSpec(Player* bot, int spec);
    };

    class AutoSetTalentsAction : public ChangeTalentsAction 
    {
    public:
        AutoSetTalentsAction(PlayerbotAI* ai) : ChangeTalentsAction(ai, "auto talents") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUsefulWhenStunned() override { return true; }
    };
}