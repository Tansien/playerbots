
#include "playerbot/playerbot.h"
#include "playerbot/Talentspec.h"
#include "ChangeTalentsAction.h"
#include "playerbot/AiFactory.h"
#include "playerbot/living/util/LivingEventSchema.h"
#include "playerbot/living/util/LivingRoles.h"

using namespace ai;

namespace
{
    bool PersistBotTalentMetadata(uint32 botGuidLow, int specId, std::string const& specLink)
    {
        return living::PersistTalentMetadata(specId, specLink,
            [botGuidLow](char const* event, uint32 value, std::string const& data)
            {
                return sRandomPlayerbotMgr.SetValue(botGuidLow, event, value, data);
            });
    }
}

bool ChangeTalentsAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    std::ostringstream out;
    TalentSpec botSpec(bot);
    uint8 cls = bot->getClass();
    std::string param = event.getParam();

    if (!param.empty())
    {
        if (param.find("auto") != std::string::npos)
        {
            AutoSelectTalents(bot, &out);
        }
        else  if (param.find("list ") != std::string::npos)
        {
            listPremadePaths(cls,getPremadePaths(cls, param.substr(5)), &out);
        }
        else  if (param.find("list") != std::string::npos)
        {
            listPremadePaths(cls, getPremadePaths(cls, ""), &out);
        }
        else if (param.find("reset") != std::string::npos)
        {
            out << "Reset talents and spec";
            TalentSpec newSpec(bot, "0-0-0");
            newSpec.ApplyTalents(bot, &out);
            if (!PersistBotTalentMetadata(bot->GetGUIDLow(), -1, ""))
                out << " Talent preference could not be saved; retry later.";
        }
        else
        {
            bool crop = false;
            bool shift = false;
            if (param.find("do ") != std::string::npos)
            {
                crop = true;
                param = param.substr(3);
            }
            else if (param.find("shift ") != std::string::npos)
            {
                shift = true;
                param = param.substr(6);
            }

            out << "Apply talents [" << param << "] ";
            if (botSpec.CheckTalentLink(param, &out))
            {
                TalentSpec newSpec(bot, param);
                std::string specLink = newSpec.GetTalentLink();

                if (crop)
                {
                    newSpec.CropTalents(bot);
                    out << "becomes: " << newSpec.GetTalentLink();
                }

                if (shift)
                {
                    TalentSpec botSpec(bot);
                    newSpec.ShiftTalents(&botSpec, bot);
                    out << "becomes: " << newSpec.GetTalentLink();
                }

                if (newSpec.CheckTalents(bot, &out))
                {
                    newSpec.ApplyTalents(bot, &out);
                    if (!PersistBotTalentMetadata(bot->GetGUIDLow(), -1, specLink))
                        out << " Talent preference could not be saved; retry later.";
                }

                ai->UpdateTalentSpec();
            }
            else
            {
                std::vector<TalentPath*> paths = getPremadePaths(bot->getClass(), param);
                if (paths.size() > 0)
                {
                    out.str("");
                    out.clear();

                    if (paths.size() > 1 && sPlayerbotAIConfig.autoPickTalents != "full")
                    {
                        out << "Found multiple specs: ";
                        listPremadePaths(cls, paths, &out);
                    }
                    else
                    {
                        if (paths.size() > 1)
                            out << "Found " << paths.size() << " possible specs to choose from. ";
                        
                        TalentPath* path = PickPremadePath(paths, sRandomPlayerbotMgr.IsRandomBot(bot));
                        TalentSpec newSpec = *GetBestPremadeSpec(bot, path->id);
                        std::string specLink = newSpec.GetTalentLink();
                        newSpec.CropTalents(bot);
                        newSpec.ApplyTalents(bot, &out);

                        if (newSpec.GetTalentPoints() > 0)
                        {
                            out << "Apply spec " << "|h|cffffffff" << path->name << " " << newSpec.formatSpec(cls);
                            if (!PersistBotTalentMetadata(bot->GetGUIDLow(), path->id, ""))
                                out << " Talent preference could not be saved; retry later.";

                            ai->UpdateTalentSpec();
                        }
                    }
                }
            }
        }

        // learn available spells
        ai->DoSpecificAction("auto learn spell");
    }
    else
    {
        botSpec.ApplyTalents(bot, &out);
        out.str("");
        out.clear();

        uint32 specNo = 0;
        sRandomPlayerbotMgr.TryGetEventValue(bot->GetGUIDLow(), "specNo", specNo);
        uint32 specId = specNo ? specNo - 1 : 0;
        std::string specName = "";
        TalentPath* specPath;
        if (specId)
        {
            specPath = getPremadePath(bot->getClass(), specId);

            if (!specPath)
            {
                ai->TellPlayer(requester, "Default talent spec for this class was not fount. Please check your config",PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
                return false;
            }


            if (specPath->id == specId)
                specName = specPath->name;
        }

        out << "My current talent spec is: " << "|h|cffffffff";

        if (specName != "")
            out << specName << " (" << botSpec.formatSpec(cls) << ")";
        else
            out << chat->formatClass(bot, botSpec.highestTree());

        out << " Link: ";
        out << botSpec.GetTalentLink();
    }

    ai->TellPlayer(requester, out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);

    return true;
}

std::vector<TalentPath*> ChangeTalentsAction::getPremadePaths(uint8 cls, std::string findName, BotRoles role)
{
    std::vector<TalentPath*> ret;
    for (auto& path : sPlayerbotAIConfig.classSpecs[cls].talentPath)
    {
        if (!findName.empty() && path.name.find(findName) == std::string::npos)
            continue;

        // Bit containment against the canonical spec-role mapping: a Feral
        // (TANK|DPS) path satisfies a TANK request; the old equality test
        // rejected every multi-role spec.
        if (role != BotRoles::BOT_ROLE_NONE
            && !living::RolesSatisfy(static_cast<uint8>(AiFactory::GetSpecRoleCapabilities(cls, path.talentSpec.back().highestTree())), static_cast<uint8>(role)))
            continue;

        ret.push_back(&path);
    }

    return ret;
}

std::vector<TalentPath*> ChangeTalentsAction::getPremadePaths(Player* bot, TalentSpec* oldSpec)
{
    std::vector<TalentPath*> ret;
    
    for (auto& path : sPlayerbotAIConfig.classSpecs[bot->getClass()].talentPath)
    {
        TalentSpec newSpec = *GetBestPremadeSpec(bot, path.id);
        newSpec.CropTalents(bot);        
        if (oldSpec->isEarlierVersionOf(newSpec))
        {
            ret.push_back(&path);
        }
    }

    return ret;
}

TalentPath* ChangeTalentsAction::getPremadePath(uint8 cls, int id)
{
    for (auto& path : sPlayerbotAIConfig.classSpecs[cls].talentPath)
    {
        if (id == path.id)
        {
            return &path;
        }
    }

    if (sPlayerbotAIConfig.classSpecs[cls].talentPath.empty())
        return nullptr;

    return &sPlayerbotAIConfig.classSpecs[cls].talentPath[0];
}

void ChangeTalentsAction::listPremadePaths(uint8 cls, std::vector<TalentPath*> paths, std::ostringstream* out)
{
    if (paths.size() == 0)
    {
        *out << "No predefined talents found..";
    }

    *out << "|h|cffffffff";

    for (auto path : paths)
    {
        *out << path->name << " (" << path->talentSpec.back().formatSpec(cls) << "), ";
    }

    out->seekp(-2, out->cur);
    *out << ".";
}

TalentPath* ChangeTalentsAction::PickPremadePath(std::vector<TalentPath*> paths, bool useProbability)
{
    int totProbability = 0;
    int curProbability = 0;

    if(paths.size() == 1)
        return paths[0];

    for (auto path : paths)
    {
        totProbability += useProbability ? path->probability : 1;
    }

    totProbability = irand(0, totProbability);

    for (auto path : paths)
    {
        curProbability += (useProbability ? path->probability : 1);
        if (curProbability >= totProbability)
            return path;
    }

    return paths[0];
}

ChangeTalentsAction::TalentSelectionResult ChangeTalentsAction::SelectTalents(Player* bot, std::ostringstream* out, BotRoles role)
{
    TalentSelectionResult selection;

    //Does the bot have talentpoints?
    if (bot->GetLevel() < 10)
    {
        *out << "No free talent points.";
        return selection;
    }

    uint32 specNo = 0;
    uint32 specLinkPresent = 0;
    if (!sRandomPlayerbotMgr.TryGetEventValue(bot->GetGUIDLow(), "specNo", specNo)
        || !sRandomPlayerbotMgr.TryGetEventValue(bot->GetGUIDLow(), "specLink", specLinkPresent))
    {
        *out << "Talent metadata is unavailable; retry later.";
        return selection;
    }
    uint32 specId = specNo ? specNo - 1 : 0;
    std::string specLink = specLinkPresent
        ? sRandomPlayerbotMgr.GetData(bot->GetGUIDLow(), "specLink") : std::string();
    uint8 cls = bot->getClass();

    //Continue the current spec
    if (specNo > 0)
    {
        TalentSpec newSpec = *GetBestPremadeSpec(bot, specId);
        newSpec.CropTalents(bot);
        newSpec.ApplyTalents(bot, out);
        selection.applied = true;
        if (bot->GetPlayerbotAI())
            bot->GetPlayerbotAI()->UpdateTalentSpec();
        if (newSpec.GetTalentPoints() > 0)
        {
            *out << "Upgrading spec " << "|h|cffffffff" << getPremadePath(bot->getClass(), specId)->name << " (" << newSpec.formatSpec(cls) << ")";
        }
    }
    else if (!specLink.empty())
    {
        TalentSpec newSpec(bot, specLink);
        newSpec.CropTalents(bot);
        newSpec.ApplyTalents(bot, out);
        selection.applied = true;
        if (bot->GetPlayerbotAI())
            bot->GetPlayerbotAI()->UpdateTalentSpec();
        if (newSpec.GetTalentPoints() > 0)
        {
            *out << "Upgrading saved spec " << "|h|cffffffff" << ChatHelper::formatClass(bot, newSpec.highestTree()) << " (" << newSpec.formatSpec(cls) << ")";
        }
    }

    //Spec was not found or not sufficient
    if (bot->CalculateTalentsPoints() > 0 || (!specNo && specLink.empty()))
    {
        TalentSpec oldSpec(bot);
        int currentTree = oldSpec.highestTree();
        std::vector<TalentPath*> paths;
        
        if (oldSpec.points)
            paths = getPremadePaths(bot, &oldSpec);

        if (paths.size() == 0) //No spec like the old one found. Pick any.
        {
            if (bot->CalculateTalentsPoints() > 0)
                *out << "No specs like the current spec found.";

            paths = getPremadePaths(bot->getClass(), "", role);

            if (paths.empty() && role != BotRoles::BOT_ROLE_NONE)
                paths = getPremadePaths(bot->getClass(), "", BotRoles::BOT_ROLE_NONE);
        }   

        if(paths.size() > 0 && oldSpec.GetTalentPoints() > 0)
        {
            //Check if any spec has the same tree as the current spec.
            bool hasSameTree = false;
            for (auto it : paths)
            {
                if (it->talentSpec.back().highestTree() == currentTree)
                {
                    hasSameTree = true;
                    break;
                }
            }

            if (hasSameTree) //Remove specs that do not end up in the same tree.
            {
                auto it = paths.begin();
                while (it != paths.end()) 
                {
                    TalentPath* path = *it;
                    if (path->talentSpec.back().highestTree() != currentTree) 
                    {
                        it = paths.erase(it);
                    }
                    else 
                    {
                        ++it;
                    }
                }
            }
        }

        if (paths.size() == 0)
        {
            *out << "No predefined talents found for this class.";
            specId = -1;
        }
        else if (paths.size() > 1 && sPlayerbotAIConfig.autoPickTalents != "full" && !sRandomPlayerbotMgr.IsRandomBot(bot))
        {
            *out << "Found multiple specs: ";
            listPremadePaths(cls, paths, out);
        }
        else
        {
            specId = PickPremadePath(paths, sRandomPlayerbotMgr.IsRandomBot(bot))->id;
            TalentSpec newSpec = *GetBestPremadeSpec(bot, specId);
            specLink = newSpec.GetTalentLink();
            newSpec.CropTalents(bot);
            newSpec.ApplyTalents(bot, out);
            selection.applied = true;
            if (bot->GetPlayerbotAI())
                bot->GetPlayerbotAI()->UpdateTalentSpec();

            if (paths.size() > 1)
                *out << "Found " << paths.size() << " possible specs to choose from. ";

            *out << "Apply spec " << "|h|cffffffff" << getPremadePath(cls, specId)->name << " " << newSpec.formatSpec(cls);
        }
    }

    // Selection only: persistence is the caller's explicit second stage
    // (PersistTalentSpec), run after the character itself is accepted. The old
    // combined flow wrote specNo/specLink here, which leaked orphan event rows
    // when creation later rejected the transient player.
    selection.evaluated = true;
    selection.specId = specId;
    selection.specLink = specLink;
    selection.hadExistingSpec = specNo != 0;
    // The APPLIED talents decide the CONCRETE verified role - the exact role
    // stored and charged in creation/batch accounting - through the learned-
    // talent distinction, NOT the capability mask (which would let a DPS Feral
    // build satisfy a tank request) and NOT a transient form/aura (a fresh bot
    // has none). When no concrete path was applied (none found, or several
    // merely listed with auto-pick disabled), the role stays ZERO: a
    // blank-talent character's default tab must never satisfy an explicit
    // role request.
    selection.selectedRoles = selection.applied
        ? static_cast<uint8>(AiFactory::GetAppliedSpecRole(bot))
        : 0;
    return selection;
}

bool ChangeTalentsAction::PersistTalentSpec(Player* bot, TalentSelectionResult const& selection)
{
    return PersistTalentSpec(bot->GetGUIDLow(), selection);
}

bool ChangeTalentsAction::PersistTalentSpec(uint32 botGuidLow, TalentSelectionResult const& selection)
{
    // Nothing was selected (no talent points): nothing to persist, matching the
    // legacy early return before the writes.
    if (!selection.evaluated)
        return true;

    return PersistBotTalentMetadata(botGuidLow, selection.specId, selection.specLink);
}

bool ChangeTalentsAction::AutoSelectTalents(Player* bot, std::ostringstream* out, BotRoles role)
{
    TalentSelectionResult const selection = SelectTalents(bot, out, role);
    if (!selection.evaluated)
        return false;

    if (!PersistTalentSpec(bot, selection))
    {
        *out << " Talent preference could not be saved; retry later.";
        return false;
    }
    return selection.hadExistingSpec;
}

//Returns a pre-made talent spec that best suits the bots current talents. 
TalentSpec* ChangeTalentsAction::GetBestPremadeSpec(Player* bot, int specId)
{
    TalentPath* path = getPremadePath(bot->getClass(), specId);
    for (auto& spec : path->talentSpec)
    {
        if (spec.points >= bot->CalculateTalentsPoints())
            return &spec;
    }
    if (path->talentSpec.size())
        return &path->talentSpec.back();

    return &sPlayerbotAIConfig.classSpecs[bot->getClassMask()].baseSpec;
}

bool AutoSetTalentsAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    sPlayerbotAIConfig.logEvent(ai, "AutoSetTalentsAction", std::to_string(bot->m_Played_time[PLAYED_TIME_LEVEL]), std::to_string(bot->m_Played_time[PLAYED_TIME_TOTAL]));

    std::ostringstream out;

    if (sPlayerbotAIConfig.autoPickTalents == "no" && !sRandomPlayerbotMgr.IsRandomBot(bot))
    {
        return false;
    }

    if (bot->CalculateTalentsPoints() <= 0)
    {
        return false;
    }

    AutoSelectTalents(bot, &out);

    ai->TellPlayer(requester, out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);

    return true;
}
