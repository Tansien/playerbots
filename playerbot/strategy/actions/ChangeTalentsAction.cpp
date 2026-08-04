
#include "playerbot/playerbot.h"
#include "playerbot/Talentspec.h"
#include "ChangeTalentsAction.h"
#include "playerbot/AiFactory.h"

using namespace ai;

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
            sRandomPlayerbotMgr.SetValue(bot->GetGUIDLow(), "specNo", 0);
            sRandomPlayerbotMgr.SetValue(bot->GetGUIDLow(), "specLink", 0);
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
                bool const linkComplete = newSpec.unreadLinkChars == 0;
                if (newSpec.unreadLinkChars > 0)
                {
                    out << "talent link is invalid. " << newSpec.unreadLinkChars
                        << " character(s) could not be read; check the tree separators.";
                }
                std::string specLink = newSpec.GetTalentLink();

                if (linkComplete && crop)
                {
                    newSpec.CropTalents(bot);
                    out << "becomes: " << newSpec.GetTalentLink();
                }

                if (linkComplete && shift)
                {
                    TalentSpec botSpec(bot);
                    newSpec.ShiftTalents(&botSpec, bot);
                    out << "becomes: " << newSpec.GetTalentLink();
                }

                if (linkComplete && newSpec.CheckTalents(bot, &out))
                {
                    newSpec.ApplyTalents(bot, &out);
                    sRandomPlayerbotMgr.SetValue(bot->GetGUIDLow(), "specNo", 0);
                    sRandomPlayerbotMgr.SetValue(bot->GetGUIDLow(), "specLink", 1, specLink);
                }

                if (linkComplete)
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
                            sRandomPlayerbotMgr.SetValue(bot->GetGUIDLow(), "specNo", path->id + 1);
                            sRandomPlayerbotMgr.SetValue(bot->GetGUIDLow(), "specLink", 0);

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

        uint32 specId = sRandomPlayerbotMgr.GetValue(bot->GetGUIDLow(), "specNo") - 1;
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

        if (role != BotRoles::BOT_ROLE_NONE && AiFactory::GetPlayerRoles(cls, path.talentSpec.back().highestTree()) != role)
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

bool ChangeTalentsAction::AutoSelectTalents(Player* bot, std::ostringstream* out, BotRoles role)
{
    //Does the bot have talentpoints?
    if (bot->GetLevel() < 10)
    {
        *out << "No free talent points.";
        return false;
    }

    uint32 specNo = sRandomPlayerbotMgr.GetValue(bot->GetGUIDLow(), "specNo");
    int32 specId = specNo ? int32(specNo) - 1 : -1;
    std::string specLink = sRandomPlayerbotMgr.GetData(bot->GetGUIDLow(), "specLink");
    uint8 cls = bot->getClass();

    // A persisted specNo can outlive the configuration that produced it: a
    // trimmed aiplayerbot.conf, a link newly rejected by CheckTalents, or a
    // reload that renumbers the paths.
    //
    // The id must be compared explicitly. getPremadePath falls back to the
    // FIRST configured path when the id is absent and only returns nullptr
    // when the class has no paths at all, so a null check alone catches only
    // the rare empty-class case. In the common one - a single path removed,
    // the rest still present - it silently hands back path 0, the wrong build
    // is applied, and specId + 1 re-persists the stale id at the end of this
    // function, so the mismatch survives every later pass.
    //
    // When the class has no paths at all, GetBestPremadeSpec instead falls
    // back to baseSpec, every class talent at rank zero, and applying that
    // strips every talent the bot has learned. The name lookup below would
    // have dereferenced the same nullptr.
    TalentPath* currentPath = specNo > 0 ? getPremadePath(cls, specId) : nullptr;
    if (specNo > 0 && (!currentPath || currentPath->id != specId || currentPath->talentSpec.empty()))
    {
        *out << "Stored spec is no longer configured; selecting a new one. ";
        specNo = 0;
        specId = -1;
        currentPath = nullptr;
    }

    //Continue the current spec
    if (specNo > 0)
    {
        TalentSpec newSpec = *GetBestPremadeSpec(bot, specId);
        newSpec.CropTalents(bot);
        newSpec.ApplyTalents(bot, out);
        if (bot->GetPlayerbotAI())
            bot->GetPlayerbotAI()->UpdateTalentSpec();
        if (newSpec.GetTalentPoints() > 0)
        {
            *out << "Upgrading spec " << "|h|cffffffff" << currentPath->name << " (" << newSpec.formatSpec(cls) << ")";
        }
    }
    else if (!specLink.empty())
    {
        TalentSpec newSpec(bot, specLink);
        newSpec.CropTalents(bot);
        newSpec.ApplyTalents(bot, out);
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
            if (bot->GetPlayerbotAI())
                bot->GetPlayerbotAI()->UpdateTalentSpec();

            if (paths.size() > 1)
                *out << "Found " << paths.size() << " possible specs to choose from. ";

            *out << "Apply spec " << "|h|cffffffff" << getPremadePath(cls, specId)->name << " " << newSpec.formatSpec(cls);
        }
    }

    sRandomPlayerbotMgr.SetValue(bot->GetGUIDLow(), "specNo", specId + 1);
    if (!specLink.empty() && specId == -1)
        sRandomPlayerbotMgr.SetValue(bot->GetGUIDLow(), "specLink", 1, specLink);
    else
        sRandomPlayerbotMgr.SetValue(bot->GetGUIDLow(), "specLink", 0);

    return (specNo == 0) ? false : true;
}

//Returns a pre-made talent spec that best suits the bots current talents. 
TalentSpec* ChangeTalentsAction::GetBestPremadeSpec(Player* bot, int specId)
{
    // getPremadePath returns nullptr when the class has no premade paths
    // configured at all, and every caller dereferences what we return, so the
    // fallback below has to stay reachable and has to be safe.
    TalentPath* path = getPremadePath(bot->getClass(), specId);
    if (path)
    {
        for (auto& spec : path->talentSpec)
        {
            if (spec.points >= bot->CalculateTalentsPoints())
                return &spec;
        }

        if (path->talentSpec.size())
            return &path->talentSpec.back();
    }

    // classSpecs is 'ClassSpecs classSpecs[MAX_CLASSES]', indexed by class id.
    // getClassMask() is a bitmask - 1 << (class - 1), so up to 1024 - and using
    // it here read far past the end of the array for every class above the
    // fourth.
    return &sPlayerbotAIConfig.classSpecs[bot->getClass()].baseSpec;
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


