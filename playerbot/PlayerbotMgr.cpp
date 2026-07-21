#include "playerbot/playerbot.h"
#include "playerbot/living/config/LivingRealmConfig.h"
#include "playerbot/living/util/LivingActivation.h"
#include "playerbot/living/util/LivingCreationCleanup.h"
#include "playerbot/living/util/LivingCreationLifecycle.h"
#include "playerbot/living/util/LivingKeyValueArgs.h"
#include "playerbot/living/util/LivingNumericParse.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "PlayerbotDbStore.h"
#include "Database/DatabaseImpl.h"
#include "playerbot/PlayerbotFactory.h"
#include "playerbot/RandomPlayerbotFactory.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/TravelMgr.h"
#include "Chat/ChannelMgr.h"
#include "Social/SocialMgr.h"
#include "Accounts/AccountMgr.h"
#include "strategy/actions/ChangeTalentsAction.h"
#include "strategy/actions/InviteToGroupAction.h"
#include "AiFactory.h"
#include "Guilds/GuildMgr.h"
#include "Groups/Group.h"

#ifdef GenerateBotTests
#include "strategy/tests/TestAction.h"
#include "strategy/tests/TestRegistry.h"
#endif

class LoginQueryHolder;
class CharacterHandler;

namespace ai
{
    // Defined below with the creation finalizer; hooked into UpdateSessions.
    void PumpBotCreation();
    // The creation pump's tick counter (paces batch attempt scheduling).
    uint64 CreationPumpTick();
}

PlayerbotHolder::PlayerbotHolder() : PlayerbotAIBase()
{
    m_holderHandlers["list"] = &PlayerbotHolder::HandleList;
    m_holderHandlers["help"] = &PlayerbotHolder::HandleHelp;
    m_holderHandlers["reload"] = &PlayerbotHolder::HandleReload;
    m_holderHandlers["tweak"] = &PlayerbotHolder::HandleTweak;
    m_holderHandlers["self"] = &PlayerbotHolder::HandleSelf;
    m_holderHandlers["spoof"] = &PlayerbotHolder::HandleSpoof;
    m_holderHandlers["p"] = &PlayerbotHolder::HandleParty;
    m_holderHandlers["g"] = &PlayerbotHolder::HandleGuild;
    m_holderHandlers["r"] = &PlayerbotHolder::HandleRaid;
    m_holderHandlers["rl"] = &PlayerbotHolder::HandleRaidLeader;
    m_holderHandlers["create"] = &PlayerbotHolder::HandleCreate;
    m_holderHandlers["group"] = &PlayerbotHolder::HandleGroup;
#ifdef GenerateBotTests
    m_holderHandlers["runtest"] = &PlayerbotHolder::HandleRunTest;
#endif

    m_botCommandHandlers["add"] = &PlayerbotHolder::HandleBotAddLogin;
    m_botCommandHandlers["login"] = &PlayerbotHolder::HandleBotAddLogin;
    m_botCommandHandlers["remove"] = &PlayerbotHolder::HandleBotRemoveLogout;
    m_botCommandHandlers["logout"] = &PlayerbotHolder::HandleBotRemoveLogout;
    m_botCommandHandlers["rm"] = &PlayerbotHolder::HandleBotRemoveLogout;
    m_botCommandHandlers["delete"] = &PlayerbotHolder::HandleBotDelete;
    m_botCommandHandlers["gear"] = &PlayerbotHolder::HandleBotGear;
    m_botCommandHandlers["equip"] = &PlayerbotHolder::HandleBotGear;
    m_botCommandHandlers["train"] = &PlayerbotHolder::HandleBotTrainLearn;
    m_botCommandHandlers["learn"] = &PlayerbotHolder::HandleBotTrainLearn;
    m_botCommandHandlers["food"] = &PlayerbotHolder::HandleBotFoodDrink;
    m_botCommandHandlers["drink"] = &PlayerbotHolder::HandleBotFoodDrink;
    m_botCommandHandlers["potions"] = &PlayerbotHolder::HandleBotPotions;
    m_botCommandHandlers["pots"] = &PlayerbotHolder::HandleBotPotions;
    m_botCommandHandlers["consumes"] = &PlayerbotHolder::HandleBotConsumes;
    m_botCommandHandlers["consumables"] = &PlayerbotHolder::HandleBotConsumes;
    m_botCommandHandlers["consums"] = &PlayerbotHolder::HandleBotConsumes;
    m_botCommandHandlers["regs"] = &PlayerbotHolder::HandleBotReagents;
    m_botCommandHandlers["reg"] = &PlayerbotHolder::HandleBotReagents;
    m_botCommandHandlers["reagents"] = &PlayerbotHolder::HandleBotReagents;
    m_botCommandHandlers["prepare"] = &PlayerbotHolder::HandleBotPrepare;
    m_botCommandHandlers["prep"] = &PlayerbotHolder::HandleBotPrepare;
    m_botCommandHandlers["init"] = &PlayerbotHolder::HandleBotInit;
    m_botCommandHandlers["enchants"] = &PlayerbotHolder::HandleBotEnchants;
    m_botCommandHandlers["ammo"] = &PlayerbotHolder::HandleBotAmmo;
    m_botCommandHandlers["pet"] = &PlayerbotHolder::HandleBotPet;
    m_botCommandHandlers["levelup"] = &PlayerbotHolder::HandleBotLevelUp;
    m_botCommandHandlers["level"] = &PlayerbotHolder::HandleBotLevelUp;
    m_botCommandHandlers["random"] = &PlayerbotHolder::HandleBotRandom;

    m_botCommandHandlers["always"] = &PlayerbotHolder::HandleBotAlways;
    m_botCommandHandlers["debug"] = &PlayerbotHolder::HandleBotDebug;
    m_botCommandHandlers["c"] = &PlayerbotHolder::HandleBotC;
    m_botCommandHandlers["w"] = &PlayerbotHolder::HandleConsoleWhisper;
    m_botCommandHandlers["cmd"] = &PlayerbotHolder::HandleConsoleCmd;
    m_botCommandHandlers["test"] = &PlayerbotHolder::HandleBotTest;
    m_botCommandHandlers["do"] = &PlayerbotHolder::HandleBotDo;
    m_botCommandHandlers["record"] = &PlayerbotHolder::HandleBotRecord;
    m_botCommandHandlers["read"] = &PlayerbotHolder::HandleBotRead;
    m_botCommandHandlers["clear"] = &PlayerbotHolder::HandleBotClear;

    for (uint32 spellId = 0; spellId < sServerFacade.GetSpellInfoRows(); spellId++)
    {
        sServerFacade.LookupSpellInfo(spellId);
    }
}

PlayerbotHolder::~PlayerbotHolder()
{
}

void PlayerbotHolder::ForEachPlayerbot(std::function<void(Player*)> callback) const
{
    for (auto& itr : playerBots)
    {
        Player* bot = itr.second;
        if (bot)
        {
            callback(bot);
        }
    }
}

void PlayerbotHolder::MovePlayerBot(uint32 guid, PlayerbotHolder* newHolder)
{
    if (newHolder)
    {
        auto it = playerBots.find(guid); 
        if (it != playerBots.end() && it->second != nullptr)
        {
            newHolder->OnBotLogin(it->second);
            playerBots[guid] = nullptr;
        }
    }
}

void PlayerbotHolder::UpdateAIInternal(uint32 elapsed, bool minimal)
{
#ifdef GenerateBotTests
    UpdatePendingTests(elapsed);
#endif
}

void PlayerbotHolder::UpdateSessions(uint32 elapsed)
{
    // Deferred SQL-callback work (creation finalization, relocation homebind
    // verification and finalization retries) runs here: a normal world update
    // that every pinned core executes BEFORE World::UpdateResultQueue in the
    // tick, so it can never run inside the SQL result-queue mutex. Pumping is
    // gated to the ONE UpdateSessions call World::Update makes per tick (the
    // random manager's) - per-master holders also pass through here from
    // WorldSession::Update, and letting each advance the pumps multiplied
    // retention aging and per-tick write retries by the holder count.
    if (this == static_cast<PlayerbotHolder*>(&sRandomPlayerbotMgr))
    {
        ai::PumpBotCreation();
        sRandomPlayerbotMgr.PumpPendingRelocations();
    }

    ForEachPlayerbot([&](Player* bot)
    {
        if (bot->GetPlayerbotAI() && bot->IsBeingTeleported())
        {
            bot->GetPlayerbotAI()->HandleTeleportAck();
        }
        else if (bot->IsInWorld())
        {
            bot->GetSession()->HandleBotPackets();
        }

        if (bot->GetPlayerbotAI() && bot->GetPlayerbotAI()->GetShouldLogOut() && !bot->IsStunnedByLogout() && !bot->GetSession()->isLogingOut())
        {
            LogoutPlayerBot(bot->GetObjectGuid().GetRawValue());
        }
    });

    Cleanup();
}

void PlayerbotHolder::Cleanup()
{
    auto it = playerBots.begin();
    while (it != playerBots.end())
    {
        if (it->second == nullptr)
        {
            it = playerBots.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void PlayerbotHolder::LogoutAllBots()
{
    ForEachPlayerbot([&](Player* bot)
    {
        if (bot->GetPlayerbotAI() && !bot->GetPlayerbotAI()->IsRealPlayer())
        {
            LogoutPlayerBot(bot->GetGUIDLow());
        }
    });

    Cleanup();
}

void PlayerbotMgr::CancelLogout()
{
    Player* master = GetMaster();
    if (!master)
        return;

    ForEachPlayerbot([&](Player* bot)
    {
        PlayerbotAI* ai = bot->GetPlayerbotAI();
        if (ai && !ai->IsRealPlayer())
        {
            if (bot->IsStunnedByLogout() || bot->GetSession()->isLogingOut())
            {
                WorldPacket p;
                bot->GetSession()->HandleLogoutCancelOpcode(p);
                ai->TellPlayer(GetMaster(), BOT_TEXT("logout_cancel"));
            }
        }
    });

    sRandomPlayerbotMgr.ForEachPlayerbot([&](Player* bot)
    {
        PlayerbotAI* ai = bot->GetPlayerbotAI();
        if (ai && !ai->IsRealPlayer() && ai->GetMaster() == master)
        {
            if (bot->IsStunnedByLogout() || bot->GetSession()->isLogingOut())
            {
                WorldPacket p;
                bot->GetSession()->HandleLogoutCancelOpcode(p);
            }
        }
    });
}

void PlayerbotHolder::LogoutPlayerBot(uint32 guid, bool allowInstant, bool forDelete)
{
    Player* bot = GetPlayerBot(guid);
    if (bot)
    {
        PlayerbotAI* ai = bot->GetPlayerbotAI();
        if (!ai)
            return;

        // An ordinary logout drops a relocation still awaiting its teleport
        // acknowledgement (retry markers drive a later login), but RETAINS a
        // Finalizing record so its owed durable work resumes on relogin. Only a
        // logout for deletion FORCE-cancels (the character is going away).
        sRandomPlayerbotMgr.CancelPendingRelocation(bot->GetGUIDLow(),
            forDelete ? living::RelocationCancelMode::Force : living::RelocationCancelMode::Ordinary);

        if (!sPlayerbotAIConfig.bExplicitDbStoreSave)
        {
           Group* group = bot->GetGroup();
           if (group && !bot->InBattleGround() && !bot->InBattleGroundQueue() && ai->HasActivePlayerMaster())
           {
              sPlayerbotDbStore.Save(ai);
           }
        }
        sLog.outDebug("Bot %s logging out", bot->GetName());
        if (!forDelete)
            bot->SaveToDB();

        WorldSession* botWorldSessionPtr = bot->GetSession();
        WorldSession* masterWorldSessionPtr = nullptr;

        Player* master = ai->GetMaster();
        if (master)
            masterWorldSessionPtr = master->GetSession();

        // check for instant logout
        bool logout = botWorldSessionPtr->ShouldLogOut(time(nullptr));

        // if no instant logout, request normal logout
        if (!allowInstant)
        {
            if (bot && (bot->IsStunnedByLogout() || bot->GetSession()->isLogingOut()))
            {
                return;
            }
            else if (bot)
            {
                ai->TellPlayer(ai->GetMaster(), BOT_TEXT("logout_start"));

                WorldPacket p(CMSG_LOGOUT_REQUEST);
                std::unique_ptr<WorldPacket> packet(new WorldPacket(p));
                botWorldSessionPtr->QueuePacket(std::move(packet));

                //WorldPacket p;
                //botWorldSessionPtr->HandleLogoutRequestOpcode(p);
                if (!bot)
                {
                    playerBots[guid] = nullptr;
                    delete botWorldSessionPtr;    
                }
                
                return;
            }
            else
            {
                playerBots[guid] = nullptr;  // deletes bot player ptr inside this WorldSession PlayerBotMap
                delete botWorldSessionPtr;  // finally delete the bot's WorldSession
            }
            
            return;
        } 
        // if instant logout possible, do it
        else if (bot && (logout || !botWorldSessionPtr->isLogingOut()))
        {
            ai->TellPlayer(ai->GetMaster(), BOT_TEXT("goodbye"));
            playerBots[guid] = nullptr;    // deletes bot player ptr inside this WorldSession PlayerBotMap
            botWorldSessionPtr->LogoutPlayer(); // this will delete the bot Player object and PlayerbotAI object
            //botWorldSessionPtr->LogoutPlayer(true); // this will delete the bot Player object and PlayerbotAI object
            if(!sWorld.FindSession(botWorldSessionPtr->GetAccountId())) //Real player sessions will get removed later.
                delete botWorldSessionPtr;  // finally delete the bot's WorldSession
        }
    }
}

void PlayerbotHolder::DisablePlayerBot(uint32 guid, bool logOutPlayer)
{
    Player* bot = GetPlayerBot(guid);
    if (bot)
    {
        // This is the shared disable path behind core-driven logout, kicks and
        // disconnects: a pending relocation is abandoned here exactly like in
        // LogoutPlayerBot - dropped without claiming completion, retry markers
        // untouched.
        sRandomPlayerbotMgr.CancelPendingRelocation(guid);

        if (logOutPlayer && bot->GetPlayerbotAI()->IsRealPlayer() && bot->GetGroup() && sPlayerbotAIConfig.IsFreeAltBot(guid))
            bot->GetSession()->SetOffline(); //Prevent groupkick
        bot->GetPlayerbotAI()->TellPlayer(bot->GetPlayerbotAI()->GetMaster(), BOT_TEXT("goodbye"));
        bot->GetPlayerbotAI()->StopMoving();
        MotionMaster& mm = *bot->GetMotionMaster();
        mm.Clear();

        if (!sPlayerbotAIConfig.bExplicitDbStoreSave)
        {
           Group* group = bot->GetGroup();
           if (group && !bot->InBattleGround() && !bot->InBattleGroundQueue() && bot->GetPlayerbotAI()->HasActivePlayerMaster())
           {
              sPlayerbotDbStore.Save(bot->GetPlayerbotAI());
           }
        }

        sLog.outDebug("Bot %s logged out", bot->GetName());
        bot->SaveToDB();

        WorldSession* botWorldSessionPtr = bot->GetSession();
        playerBots[guid] = nullptr;    // deletes bot player ptr inside this WorldSession PlayerBotMap

        if (bot->GetPlayerbotAI()) 
        {
            bot->RemovePlayerbotAI();
        }
    }
}

Player* PlayerbotHolder::GetPlayerBot(uint32 playerGuid) const
{
    PlayerBotMap::const_iterator it = playerBots.find(playerGuid);
    return (it == playerBots.end()) ? nullptr : it->second ? it->second : nullptr;
}

void PlayerbotHolder::JoinChatChannels(Player* bot)
{
    // bots join World chat if not solo oriented
    if (bot->GetLevel() >= 10 && sRandomPlayerbotMgr.IsFreeBot(bot) && bot->GetPlayerbotAI() && bot->GetPlayerbotAI()->GetGrouperType() != GrouperType::SOLO)
    {
        // TODO make action/config
        // Make the bot join the world channel for chat
        WorldPacket pkt(CMSG_JOIN_CHANNEL);
#ifndef MANGOSBOT_ZERO
        pkt << uint32(0) << uint8(0) << uint8(0);
#endif
        pkt << std::string("World");
        pkt << ""; // Pass
        bot->GetSession()->HandleJoinChannelOpcode(pkt);
    }
    // join standard channels
    uint8 locale = BroadcastHelper::GetLocale();

    AreaTableEntry const* current_zone = bot->GetPlayerbotAI()->GetCurrentZone();
    ChannelMgr* cMgr = channelMgr(bot->GetTeam());
    std::string current_zone_name = current_zone ? bot->GetPlayerbotAI()->GetLocalizedAreaName(current_zone) : "";

    if (current_zone && cMgr)
    {
        for (uint32 i = 0; i < sChatChannelsStore.GetNumRows(); ++i)
        {
            ChatChannelsEntry const* channel = sChatChannelsStore.LookupEntry(i);
            if (!channel) continue;

            Channel* new_channel = nullptr;
            switch (channel->ChannelID)
            {
                case ChatChannelId::GENERAL:
                case ChatChannelId::LOCAL_DEFENSE:
                {
                    char new_channel_name_buf[100];
                    snprintf(new_channel_name_buf, 100, channel->pattern[locale], current_zone_name.c_str());
#ifdef MANGOSBOT_ZERO
                    new_channel = cMgr->GetJoinChannel(new_channel_name_buf);
#else
                    new_channel = cMgr->GetJoinChannel(new_channel_name_buf, channel->ChannelID);
#endif
                    break;
                }
                case ChatChannelId::TRADE:
                case ChatChannelId::GUILD_RECRUITMENT:
                {
                    char new_channel_name_buf[100];
                    //3459 is ID for a zone named "City" (only exists for the sake of using its name)
                    //Currently in magons TBC, if you switch zones, then you join "Trade - <zone>" and "GuildRecruitment - <zone>"
                    //which is a core bug, should be "Trade - City" and "GuildRecruitment - City" in both 1.12 and TBC
                    //but if you (actual player) logout in a city and log back in - you join "City" versions
                    snprintf(
                        new_channel_name_buf,
                        100,
                        channel->pattern[locale],
                        bot->GetPlayerbotAI()->GetLocalizedAreaName(GetAreaEntryByAreaID(ImportantAreaId::CITY)).c_str()
                    );

#ifdef MANGOSBOT_ZERO
                    new_channel = cMgr->GetJoinChannel(new_channel_name_buf);
#else
                    new_channel = cMgr->GetJoinChannel(new_channel_name_buf, channel->ChannelID);
#endif
                    break;
                }
                case ChatChannelId::LOOKING_FOR_GROUP:
                case ChatChannelId::WORLD_DEFENSE:
                {
#ifdef MANGOSBOT_ZERO
                    new_channel = cMgr->GetJoinChannel(channel->pattern[locale]);
#else
                    new_channel = cMgr->GetJoinChannel(channel->pattern[locale], channel->ChannelID);
#endif
                    break;
                }
                default:
                    break;
            }
            if (new_channel)
                new_channel->Join(bot, "");
        }
    }
}

void PlayerbotHolder::OnBotLogin(Player * const bot)
{
    if (!sPlayerbotAIConfig.enabled)
        return;

    // A fresh login invalidates any relocation left pending across a logout or
    // a mid-teleport disconnect - for EVERY holder and master arrangement
    // (OnBotLoginInternal is skipped for bots with a real-player master, so the
    // cancellation cannot live there). The record is dropped without claiming
    // completion; untouched retry markers drive a new attempt.
    sRandomPlayerbotMgr.CancelPendingRelocation(bot->GetGUIDLow());

    PlayerbotAI* ai = bot->GetPlayerbotAI();
    if (!ai)
    {
        bot->CreatePlayerbotAI();
        ai = bot->GetPlayerbotAI();
    }

    if(!ai->HasRealPlayerMaster())
	    OnBotLoginInternal(bot);

    playerBots[bot->GetGUIDLow()] = bot;

    Player* master = ai->GetMaster();
    if (!master && sPlayerbotAIConfig.IsFreeAltBot(bot))
    {
        ai->SetMaster(bot);
        master = bot;
    }

    if (master)
    {
        ObjectGuid masterGuid = master->GetObjectGuid();
        if (master->GetGroup() && !master->GetGroup()->IsLeader(masterGuid) && !sPlayerbotAIConfig.IsFreeAltBot(bot))
            master->GetGroup()->ChangeLeader(masterGuid);
    }

    Group* group = bot->GetGroup();
    if (group)
    {
        bool groupValid = false;
        Group::MemberSlotList const& slots = group->GetMemberSlots();
        for (Group::MemberSlotList::const_iterator i = slots.begin(); i != slots.end(); ++i)
        {
            ObjectGuid member = i->guid;
            if (master)
            {
                if (master->GetObjectGuid() == member)
                {
                    groupValid = true;
                    break;
                }
            }

            // Don't disband alt groups when master goes away
            // (will need to manually disband with leave command)
            uint32 account = sObjectMgr.GetPlayerAccountIdByGUID(member);
            if (!sPlayerbotAIConfig.IsInRandomAccountList(account))
            {
                groupValid = true;
                break;
            }
        }

        if (!groupValid)
        {
            WorldPacket p;
            std::string member = bot->GetName();
            p << uint32(PARTY_OP_LEAVE) << member << uint32(0);
            bot->GetSession()->HandleGroupDisbandOpcode(p);
        }
    }

    ai->ResetStrategies();

    if (master && !master->IsTaxiFlying())
    {
        bot->GetMotionMaster()->MovementExpired();
    }

    // check activity
    ai->AllowActivity(ALL_ACTIVITY, true);
    // set delay on login
    ai->SetActionDuration(urand(2000, 4000));

    ai->TellPlayer(ai->GetMaster(), BOT_TEXT("hello"));

    JoinChatChannels(bot);

    if (sRandomPlayerbotMgr.IsRandomBot(bot))
    {
        uint32 lowguid = bot->GetObjectGuid().GetCounter();
        auto result = CharacterDatabase.PQuery("SELECT 1 FROM character_social WHERE flags='%u' and friend='%d'", SOCIAL_FLAG_FRIEND, lowguid);
        if (result)
            bot->GetPlayerbotAI()->SetPlayerFriend(true);
        else
            bot->GetPlayerbotAI()->SetPlayerFriend(false);

        if (sPlayerbotAIConfig.instantRandomize && !sPlayerbotAIConfig.disableRandomLevels && !bot->GetTotalPlayedTime() && !sPlayerbotAIConfig.IsFreeAltBot(bot))
        {
            sRandomPlayerbotMgr.InstaRandomize(bot);
        }
    }

    if (!bot->HasItemCount(6948, 1)
#ifdef MANGOSBOT_TWO
        && !bot->HasItemCount(40582, 1)
#endif
        )
    {
#ifdef MANGOSBOT_TWO
        if (bot->getClass() == CLASS_DEATH_KNIGHT && bot->GetMapId() == 609)
            bot->StoreNewItemInBestSlots(40582, 1);
        else
#endif
            bot->StoreNewItemInBestSlots(6948, 1);
    }
}

std::string PlayerbotHolder::ProcessBotCommand(std::string cmd, ObjectGuid guid, ObjectGuid masterguid, bool admin, uint32 masterAccountId, uint32 masterGuildId, const std::string param)
{
    Player* bot = sObjectMgr.GetPlayer(guid);
    Player* master = masterguid ? sObjectMgr.GetPlayer(masterguid) : nullptr;

    if (!sPlayerbotAIConfig.enabled || guid.IsEmpty())
        return "Bot system is disabled";

    uint32 botAccount = sObjectMgr.GetPlayerAccountIdByGUID(guid);
    bool isRandomAccount = sPlayerbotAIConfig.IsInRandomAccountList(botAccount);
    bool isMasterAccount = (masterAccountId == botAccount);

    if (!isRandomAccount && (!isMasterAccount && !admin && masterguid))
    {
        if (master && (!sPlayerbotAIConfig.allowGuildBots || !masterGuildId || (masterGuildId && master->GetGuildIdFromDB(guid) != masterGuildId)))
            return "Not in your guild or account";
    }

    if (!isRandomAccount && this == &sRandomPlayerbotMgr && !admin)
    {
        return "Can not control alt-bots with this command.";
    }

    std::string subType;
    size_t eqPos = cmd.find('=');
    if (eqPos != std::string::npos)
    {
        subType = cmd.substr(eqPos + 1);
        cmd = cmd.substr(0, eqPos);
    }

    auto it = m_botCommandHandlers.find(cmd);
    if (it != m_botCommandHandlers.end())
    {
        std::string realParam;
        
        if (!subType.empty())
            realParam = subType;
        else if (it->second == &PlayerbotHolder::HandleBotAddLogin || it->second == &PlayerbotHolder::HandleBotAlways || it->second == &PlayerbotHolder::HandleBotDelete)
            realParam = std::to_string(guid.GetRawValue());        
        else
            realParam = param;            

        return (this->*it->second)(bot, master, realParam);
    }

    return "unknown command";
}

bool PlayerbotMgr::HandlePlayerbotMgrCommand(ChatHandler* handler, char const* args)
{
	if (!sPlayerbotAIConfig.enabled)
	{
		handler->PSendSysMessage("|cffff0000Playerbot system is currently disabled!");
        return false;
	}

    WorldSession *m_session = handler->GetSession();

    if (!m_session)
    {
        handler->PSendSysMessage("You may only add bots from an active session");
        return false;
    }

    Player* player = m_session->GetPlayer();
    PlayerbotMgr* mgr = player->GetPlayerbotMgr();
    if (!mgr)
    {
        handler->PSendSysMessage("you cannot control bots yet");
        return false;
    }

    std::list<std::string> messages = mgr->HandlePlayerbotCommand(args, player);
    if (messages.empty())
        return true;

    for (std::list<std::string>::iterator i = messages.begin(); i != messages.end(); ++i)
    {
        WorldSession* activeSession = handler->GetSession();
        if (!activeSession || !activeSession->GetPlayer())
            break;

        try
        {
            handler->PSendSysMessage("%s", i->c_str());
        }
        catch (...)
        {
            // RA/client can disconnect while a long response is being streamed.
            // Stop sending remaining lines instead of risking a server-side crash.
            break;
        }
    }

    return true;
}

std::list<std::string> PlayerbotHolder::HandlePlayerbotCommand(const std::string args, Player* master, AccountTypes security)
{
    AccountTypes useSecurity = master ? master->GetSession()->GetSecurity() : security;

    if (!master && m_spoofGuid)
        master = sObjectMgr.GetPlayer(m_spoofGuid);

    std::vector<std::string> params = Qualified::getMultiQualifiers(args, " ");

    std::list<std::string> messages;

    if (params.empty())
    {
        std::string helpText = GetCommandTexts("");
        messages.push_back(helpText);
        return messages;
    }

    std::string command = params[0];
    std::string param, charname;

    if (params.size() > 1)
    {
        param = args.substr(params[0].size() + 1);
        charname = params[1];
    }
    
    for (auto& [prefix, handler] : m_holderHandlers)
    {
        if (command != prefix)
            continue;

        messages = (this->*handler)(master, param, useSecurity);
        return messages;
    }

    std::set<std::string> bots;

    if (charname.empty())
    {
        if (master && master->GetTarget() && master->GetTarget()->IsPlayer() && !((Player*)master->GetTarget())->isRealPlayer())
        {
            bots.insert(master->GetTarget()->GetName());
        }
        if (args == "always")
        {
            bots.insert(master->GetName());
        }
        else
        {
            std::string helpText = GetCommandTexts("help");
            messages.push_back(helpText);
            return messages;
        }
    }    

    if (charname == "*" && master)
    {
        Group* group = master->GetGroup();
        if (!group)
        {
            messages.push_back("you must be in group");
            return messages;
        }

        Group::MemberSlotList slots = group->GetMemberSlots();
        for (Group::member_citerator i = slots.begin(); i != slots.end(); i++)
        {
            ObjectGuid member = i->guid;

            if (member.GetRawValue() == master->GetObjectGuid().GetRawValue())
                continue;

            std::string bot;
            if (sObjectMgr.GetPlayerNameByGUID(member, bot))
                bots.insert(bot);
        }
    }

    if (charname == "guild" && master)
    {
        if (!master->GetGuildId())
        {
            messages.push_back("you must be in a guild");
            return messages;
        }

        auto result = CharacterDatabase.PQuery("SELECT m.guid, (select name from characters c where c.guid = m.guid) FROM guild_member m WHERE guildid = '%u'", master->GetGuildId());

        if (!result)
        {
            messages.push_back("No guild members");
            return messages;
        }

        do
        {
            Field* fields = result->Fetch();
            uint32 guid = fields[0].GetUInt32();
            std::string bot = fields[1].GetString();

            if (guid == master->GetGUIDLow())
                continue;

            bots.insert(bot);
        } while (result->NextRow());
    }

    if (charname == "!" && useSecurity > SEC_GAMEMASTER)
    {
        for (auto& itr : playerBots)
        {
            Player* bot = itr.second;
            if (bot && (bot->IsInWorld() || param.find("add") == 0 || param.find("login") == 0 || param.find("delete") == 0))
                bots.insert(bot->GetName());
        }
    }

    if (bots.empty())
    {
        std::vector<std::string> chars = split(charname, ',');
        for (auto name : chars)
        {
            uint32 accountId = GetAccountId(name);
            if (!accountId)
            {
                bots.insert(name);
                continue;
            }

            auto results = CharacterDatabase.PQuery(
                "SELECT name FROM characters WHERE account = '%u'",
                accountId);
            if (results)
            {
                do
                {
                    Field* fields = results->Fetch();
                    std::string charName = fields[0].GetString();
                    bots.insert(charName);
                } while (results->NextRow());
            }
        }
    }

    if (bots.size())
    {
        if (params.size() > 2)
            param = args.substr(params[0].size() + params[1].size() + 2);
        else
            param = "";
    }

    for (auto bot :  bots)
    {
        std::ostringstream out;
        out << command << ": " << bot << " - ";

        ObjectGuid member = sObjectMgr.GetPlayerGuidByName(bot);
        if (!member)
        {
            out << "character not found";
        }
        else if (master)
        {
            out << ProcessBotCommand(command, member, master->GetObjectGuid(), useSecurity >= SEC_GAMEMASTER, master->GetSession()->GetAccountId(), master->GetGuildId(), param);
        }
        else
        {
            out << ProcessBotCommand(command, member, ObjectGuid(), useSecurity >= SEC_GAMEMASTER, -1, -1, param);
        }

        messages.push_back(out.str());
    }

    if (messages.empty())
        messages.push_back("Unknown command. Use 'help' for more information.");

    return messages;
}

uint32 PlayerbotHolder::GetAccountId(std::string name)
{
    uint32 accountId = 0;

    auto results = LoginDatabase.PQuery("SELECT id FROM account WHERE username = '%s'", name.c_str());
    if(results)
    {
        Field* fields = results->Fetch();
        accountId = fields[0].GetUInt32();
    }

    return accountId;
}

std::string PlayerbotHolder::ListBots(Player* master, const std::string param)
{
    std::set<std::string> bots;
    std::map<uint8, std::string> classNames;
    classNames[CLASS_DRUID] = "Druid";
    classNames[CLASS_HUNTER] = "Hunter";
    classNames[CLASS_MAGE] = "Mage";
    classNames[CLASS_PALADIN] = "Paladin";
    classNames[CLASS_PRIEST] = "Priest";
    classNames[CLASS_ROGUE] = "Rogue";
    classNames[CLASS_SHAMAN] = "Shaman";
    classNames[CLASS_WARLOCK] = "Warlock";
    classNames[CLASS_WARRIOR] = "Warrior";
#ifdef MANGOSBOT_TWO
    classNames[CLASS_DEATH_KNIGHT] = "DeathKnight";
#endif

    std::map<std::string, std::string> online;
    std::list<std::string> names;
    std::map<std::string, std::string> classes;

    for (auto& itr : playerBots)
    {
        Player* bot = itr.second;

        if (!bot)
            continue;

        std::string name = bot->GetName();

        if (!param.empty() && name.find(param) != 0)
            continue;

        bots.insert(name);
        names.push_back(name);
        online[name] = "+";
        classes[name] = classNames[bot->getClass()];
    }

    if (master)
    {
        auto results = CharacterDatabase.PQuery("SELECT class,name FROM characters where account = '%u'",
            master->GetSession()->GetAccountId());
        if (results != NULL)
        {
            do
            {
                Field* fields = results->Fetch();
                uint8 cls = fields[0].GetUInt8();
                std::string name = fields[1].GetString();

                if (!param.empty() && name.find(param) != 0)
                    continue;

                if (bots.find(name) == bots.end() && name != master->GetSession()->GetPlayerName())
                {
                    names.push_back(name);
                    online[name] = "-";
                    classes[name] = classNames[cls];
                }
            } while (results->NextRow());
        }
    }

    names.sort();

    if (master)
    {
        Group* group = master->GetGroup();
        if (group)
        {
            Group::MemberSlotList const& groupSlot = group->GetMemberSlots();
            for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
            {
                Player* member = sObjectMgr.GetPlayer(itr->guid);

                if (member && sRandomPlayerbotMgr.IsFreeBot(member))
                {
                    std::string name = member->GetName();

                    if (!param.empty() && name.find(param) != 0)
                        continue;

                    names.push_back(name);
                    online[name] = "+";
                    classes[name] = classNames[member->getClass()];
                }
            }
        }
    }

    std::ostringstream out;
    bool first = true;
    out << "Bot roster: ";
    for (std::list<std::string>::iterator i = names.begin(); i != names.end(); ++i)
    {
        if (first)
            first = false;
        else
            out << ", ";
        std::string name = *i;
        out << online[name] << name << " " << classes[name];
    }

    return out.str();
}


uint32 PlayerbotHolder::GetPlayerbotsAmount() const
{
    uint32 amount = 0;
    for (const auto& pair : playerBots)
    {
        if (pair.second)
        {
            amount++;
        }
    }

    return amount;
}

PlayerbotMgr::PlayerbotMgr(Player* const master) : PlayerbotHolder(),  master(master), lastErrorTell(0)
{
}

PlayerbotMgr::~PlayerbotMgr()
{
}

void PlayerbotMgr::UpdateAIInternal(uint32 elapsed, bool minimal)
{
    SetAIInternalUpdateDelay(sPlayerbotAIConfig.reactDelay);
    CheckTellErrors(elapsed);
}

void PlayerbotMgr::HandleCommand(uint32 type, const std::string& text, uint32 lang)
{
    Player *master = GetMaster();
    if (!master)
        return;

    if (!sPlayerbotAIConfig.enabled)
        return;

    if (text.find(sPlayerbotAIConfig.commandSeparator) != std::string::npos)
    {
        std::vector<std::string> commands;
        split(commands, text, sPlayerbotAIConfig.commandSeparator.c_str());
        for (std::vector<std::string>::iterator i = commands.begin(); i != commands.end(); ++i)
        {
            HandleCommand(type, *i,lang);
        }
        return;
    }

    ForEachPlayerbot([&](Player *bot)
    {
        if (type == CHAT_MSG_SAY)
            if (bot->GetMapId() != master->GetMapId() || sServerFacade.GetDistance2d(bot, master) > 25)
                return;

        if (type == CHAT_MSG_YELL)
            if (bot->GetMapId() != master->GetMapId() || sServerFacade.GetDistance2d(bot, master) > 300)
               return;

        bot->GetPlayerbotAI()->HandleCommand(type, text, *master, lang);
    });

    sRandomPlayerbotMgr.ForEachPlayerbot([&](Player* bot)
    {
        if (type == CHAT_MSG_SAY)
            if (bot->GetMapId() != master->GetMapId() || sServerFacade.GetDistance2d(bot, master) > 25)
               return;

        if (type == CHAT_MSG_YELL)
            if (bot->GetMapId() != master->GetMapId() || sServerFacade.GetDistance2d(bot, master) > 300)
               return;

        if (bot->GetPlayerbotAI()->GetMaster() == master)
            bot->GetPlayerbotAI()->HandleCommand(type, text, *master, lang);
    });
}

void PlayerbotMgr::HandleMasterIncomingPacket(const WorldPacket& packet)
{
    ForEachPlayerbot([&](Player* bot)
    {
        bot->GetPlayerbotAI()->HandleMasterIncomingPacket(packet);
    });

    sRandomPlayerbotMgr.ForEachPlayerbot([&](Player* bot)
    {
        if (bot->GetPlayerbotAI()->GetMaster() == GetMaster())
            bot->GetPlayerbotAI()->HandleMasterIncomingPacket(packet);
    });

    switch (packet.GetOpcode())
    {
        // if master is logging out, log out all bots
        case CMSG_LOGOUT_REQUEST:
        {
            LogoutAllBots();
            return;
        }
        // if master cancelled logout, cancel too
        case CMSG_LOGOUT_CANCEL:
        {
            CancelLogout();
            return;
        }
    }
}
void PlayerbotMgr::HandleMasterOutgoingPacket(const WorldPacket& packet)
{
   ForEachPlayerbot([&](Player* bot)
   {
        if (!bot->GetPlayerbotAI())
            return;

        bot->GetPlayerbotAI()->HandleMasterOutgoingPacket(packet);
    });

    sRandomPlayerbotMgr.ForEachPlayerbot([&](Player* bot)
    {
        if (bot->GetPlayerbotAI()->GetMaster() == GetMaster())
            bot->GetPlayerbotAI()->HandleMasterOutgoingPacket(packet);
    });
}

void PlayerbotMgr::SaveToDB()
{
    ForEachPlayerbot([&](Player* bot)
    {
        bot->SaveToDB();
    });

    sRandomPlayerbotMgr.ForEachPlayerbot([&](Player* bot)
    {
        if (bot->GetPlayerbotAI()->GetMaster() == GetMaster())
            bot->SaveToDB();
    });
}

void PlayerbotMgr::OnBotLoginInternal(Player * const bot)
{
    bot->GetPlayerbotAI()->SetMaster(master);
    bot->GetPlayerbotAI()->ResetStrategies();
    sLog.outDebug("Bot %s logged in", bot->GetName());
}

void PlayerbotMgr::OnPlayerLogin(Player* player)
{
    if (player->GetSession() != player->GetPlayerMenu()->GetGossipMenu().GetMenuSession())
    {
        player->GetPlayerMenu()->GetGossipMenu() = GossipMenu(player->GetSession());
    }

    if (!sPlayerbotAIConfig.enabled)
        return;

    // set locale priority for bot texts
    sPlayerbotTextMgr.AddLocalePriority(player->GetSession()->GetSessionDbLocaleIndex());
    sLog.outDetail("Player %s logged in, localeDbc %i, localeDb %i", player->GetName(), (uint32)(player->GetSession()->GetSessionDbcLocale()), player->GetSession()->GetSessionDbLocaleIndex());

    if (sPlayerbotAIConfig.IsFreeAltBot(player))
    {
        sLog.outDetail("Enabling selfbot on login for %s", player->GetName());
        HandlePlayerbotCommand("self", player);
    }

    if (sPlayerbotAIConfig.botAutologin == BotAutoLogin::DISABLED)
        return;

    uint32 accountId = player->GetSession()->GetAccountId();
    auto results = CharacterDatabase.PQuery(
        "SELECT guid, name FROM characters WHERE account = '%u'",
        accountId);
    if (results)
    {
        std::ostringstream out; out << "add ";
        bool first = true;
        do
        {
            Field* fields = results->Fetch();
            if (first) first = false; else out << ",";
            if(sPlayerbotAIConfig.botAutologin == BotAutoLogin::LOGIN_ONLY_ALWAYS_ACTIVE && !sPlayerbotAIConfig.IsFreeAltBot(fields[0].GetUInt32())) continue;
            out << fields[1].GetString();
        } while (results->NextRow());

        HandlePlayerbotCommand(out.str().c_str(), player);
    }
}

void PlayerbotMgr::TellError(std::string botName, std::string text)
{
    std::set<std::string> names = errors[text];
    if (names.find(botName) == names.end())
    {
        names.insert(botName);
    }
    errors[text] = names;
}

std::vector<std::string> PlayerbotMgr::GetBotErrors(std::string botName)
{
    std::vector<std::string> botErrors;
    for (auto& [error, names] : errors)
    {
        if (names.find(botName) != names.end())
            botErrors.push_back(error);
    }

    return botErrors;
}

void PlayerbotMgr::CheckTellErrors(uint32 elapsed)
{
    time_t now = time(0);
    if ((now - lastErrorTell) < sPlayerbotAIConfig.errorDelay / 1000)
        return;

    lastErrorTell = now;

    for (PlayerBotErrorMap::iterator i = errors.begin(); i != errors.end(); ++i)
    {
        std::string text = i->first;
        std::set<std::string> names = i->second;

        std::ostringstream out;
        bool first = true;
        for (std::set<std::string>::iterator j = names.begin(); j != names.end(); ++j)
        {
            if (!first) out << ", "; else first = false;
            out << *j;
        }
        out << "|cfff00000: " << text;
        
        ChatHandler(master->GetSession()).PSendSysMessage("%s", out.str().c_str());
    }
    errors.clear();
}

std::list<std::string> PlayerbotHolder::HandleList(Player* master, const std::string param, AccountTypes security)
{
    std::list<std::string> messages;
    messages.push_back(ListBots(master, param));
    return messages;
}

std::list<std::string> PlayerbotHolder::HandleHelp(Player* master, const std::string param, AccountTypes security)
{
    std::list<std::string> messages;
    
    if (param.empty())
    {
        messages.push_back("Available commands: list, reload, tweak, always, self, debug, c, do, record, read, clear");
        messages.push_back("Type 'help <command>' for more information on a specific command.");
        return messages;
    }

    if (param == "commands")
    {
        std::string commands = "Commands: ";
        for (auto& [command, help] : GetCommandTexts())
        {
            commands += command + ", ";
        }

        commands = commands.substr(0, commands.size() - 2);
        messages.push_back(commands);
        return messages;
    }
    
    std::string helpText = GetCommandTexts(param);
    if (helpText.empty())
    {
        messages.push_back("No help available for '" + param + "'");
    }
    else
    {
        messages.push_back(helpText);
    }
    
    return messages;
}

std::list<std::string> PlayerbotHolder::HandleReload(Player* master, const std::string param, AccountTypes security)
{
    std::list<std::string> messages;
    if (security < SEC_GAMEMASTER)
    {
        messages.push_back("You do not have permission to use this command.");
        return messages;
    }
    messages.push_back("Reloading config");
    sPlayerbotAIConfig.Initialize();
    return messages;
}

std::list<std::string> PlayerbotHolder::HandleTweak(Player* master, const std::string param, AccountTypes security)
{
    std::list<std::string> messages;
    if (security < SEC_GAMEMASTER)
    {
        messages.push_back("You do not have permission to use this command.");
        return messages;
    }
    sPlayerbotAIConfig.tweakValue = sPlayerbotAIConfig.tweakValue++;
    if (sPlayerbotAIConfig.tweakValue > 2)
        sPlayerbotAIConfig.tweakValue = 0;
    messages.push_back("Set tweakvalue to " + std::to_string(sPlayerbotAIConfig.tweakValue));
    return messages;
}

std::string PlayerbotHolder::HandleBotAlways(Player* bot, Player* master, const std::string param)
{
    if (sPlayerbotAIConfig.selfBotLevel == BotSelfBotLevel::DISABLED)
    {
        return "Self-bot is disabled";
    }

    // Chat-supplied token: this stoull had no guard at all, so any non-numeric
    // or overflowing param threw out of the command handler.
    uint64 guidRaw = 0;
    if (!living::TryParseUInt64(param, guidRaw))
        return "Always: Error parsing " + param;

    ObjectGuid guid = ObjectGuid(guidRaw);
    uint32 accountId = sObjectMgr.GetPlayerAccountIdByGUID(guid);
    std::string alwaysName;    

    if (!sObjectMgr.GetPlayerNameByGUID(guid, alwaysName))
        return "Unable to find player.";


    // TYPED prior-state read: an untrusted read (failed bulk load, dirty row)
    // or an invalid stored value used to collapse to DISABLED and flip the
    // durable row - and the bot's login - against unknown state. Refuse the
    // toggle instead; nothing durable or runtime changes.
    uint32 alwaysRaw = 0;
    bool const alwaysKnown = sRandomPlayerbotMgr.TryGetEventValue(guid.GetCounter(), "always", alwaysRaw);
    living::AlwaysToggleDecision const decision = living::PlanAlwaysToggle(alwaysKnown, alwaysRaw);

    if (decision == living::AlwaysToggleDecision::RefuseUnknown)
        return "Always-online state for " + alwaysName + " could not be read; nothing changed, try again.";

    if (decision == living::AlwaysToggleDecision::Enable)
    {
        // Persist FIRST and gate every runtime side effect on a CONFIRMED write:
        // a rejected/ambiguous `always` write must leave freeAltBots and the
        // login untouched, or the running state diverges from what a restart
        // would rebuild from the durable row.
        if (!living::AlwaysToggleMayApply(
                sRandomPlayerbotMgr.SetValue(guid.GetCounter(), "always", (uint32)BotAlwaysOnline::ACTIVE)))
            return "Could not persist always-online for " + alwaysName + "; state unchanged, try again.";

        // Deduplicated insert: the guid may already be scheduled (a finalized
        // auto-add, or a repeated command) and a duplicate entry would make
        // every LoginFreeBots pass process the bot twice.
        if (!sPlayerbotAIConfig.IsFreeAltBot(guid.GetCounter()))
            sPlayerbotAIConfig.freeAltBots.push_back(std::make_pair(accountId, guid.GetCounter()));

        Player* existingBot = sRandomPlayerbotMgr.GetPlayerBot(guid);
        if (existingBot)
        {
            if (master)
            {
                ProcessBotCommand("add", guid, master->GetObjectGuid(), false, master->GetSession()->GetAccountId(), master->GetGuildId());
            }
        }
        else
        {
            Player* player = sObjectMgr.GetPlayer(guid, false);
            if (player)
                OnBotLogin(player);
        }

        return "Enabled offline player ai for " + alwaysName;
    }
    else
    {
        // Same durable-write gate for disable: an unconfirmed write must not log
        // the bot out or drop it from freeAltBots.
        if (!living::AlwaysToggleMayApply(
                sRandomPlayerbotMgr.SetValue(guid.GetCounter(), "always", (uint32)BotAlwaysOnline::DISABLED_BY_COMMAND)))
            return "Could not persist always-online change for " + alwaysName + "; state unchanged, try again.";

        Player* onlineBot = sObjectMgr.GetPlayer(guid, false);
        if (onlineBot && onlineBot->GetPlayerbotAI())
        {
            if (!master || guid != master->GetObjectGuid())
            {
                if (sPlayerbotAIConfig.IsFreeAltBot(onlineBot))
                    sRandomPlayerbotMgr.LogoutPlayerBot(guid);
                else
                    DisablePlayerBot(guid, false);
            }
            else if (master)
            {
                DisablePlayerBot(guid, false);
            }
        }

        auto it = std::remove_if(sPlayerbotAIConfig.freeAltBots.begin(), sPlayerbotAIConfig.freeAltBots.end(), [guid](std::pair<uint32, uint32> i) { return i.second == guid.GetCounter(); });
        sPlayerbotAIConfig.freeAltBots.erase(it, sPlayerbotAIConfig.freeAltBots.end());

        return "Disabled offline player ai for " + alwaysName;
    }
}

std::list<std::string> PlayerbotHolder::HandleSelf(Player* master, const std::string param, AccountTypes security)
{
    std::list<std::string> messages;
    if (!master)
    {
        messages.push_back("self requires a master (in-game)");
        return messages;
    }

    if (master->GetPlayerbotAI())
    {
        DisablePlayerBot(master->GetGUIDLow(), false);

        uint32 selfbot = 0;
        if (!sRandomPlayerbotMgr.TryGetEventValue(master->GetGUIDLow(), "selfbot", selfbot))
        {
            messages.push_back("Disable player ai; login preference is unavailable and was not changed");
        }
        else if (selfbot)
        {
            if (sRandomPlayerbotMgr.SetValue(master->GetGUIDLow(), "selfbot", (uint32)BotAlwaysOnline::DISABLED))
                messages.push_back("Disable player ai (on login)");
            else
                messages.push_back("Disable player ai; login preference could not be cleared");
        }
        else
            messages.push_back("Disable player ai");
    }
    else if (sPlayerbotAIConfig.selfBotLevel == BotSelfBotLevel::DISABLED)
        messages.push_back("Self-bot is disabled");
    else if (sPlayerbotAIConfig.selfBotLevel == BotSelfBotLevel::GM_ONLY && security < SEC_GAMEMASTER)
        messages.push_back("You do not have permission to enable player ai");
    else
    {
        OnBotLogin(master);

        if (!param.empty() && param == "login")
        {
            messages.push_back("Enable player ai (on login)");
            sRandomPlayerbotMgr.SetValue(master->GetObjectGuid().GetCounter(), "selfbot", 1);
        }
        else
            messages.push_back("Enable player ai");
    }
   return messages;
}

std::string PlayerbotHolder::HandleBotDebug(Player* bot, Player* master, const std::string param)
{
    if (!bot)
        return "debug requires a bot";

    PlayerbotAI* ai = bot->GetPlayerbotAI();
    if (!ai)
        return "Bot has no AI";

    ai->RecordMessages(true, true);

    std::string command = param;

    if(!ai->DoSpecificAction("cdebug", Event(".bot", command, master ? master : bot), true))
    {
        return "debug failed";
    }

    std::vector<std::string> output = ai->GetRecordedMessages();
    if (output.empty())
        return "(no output)";

    std::string result;
    for (const auto& line : output)
    {
        result += line + "\n";
    }
    return result;
}

std::string PlayerbotHolder::HandleBotC(Player* bot, Player* master, const std::string param)
{
    if (!bot)
        return "c requires a bot";

    PlayerbotAI* ai = bot->GetPlayerbotAI();
    if (!ai)
        return "Bot has no AI";

    ai->DoSpecificAction("cdebug", Event(".bot", "monstertalk " + param, master ? master : bot), true);
    return "ok";
}

std::string PlayerbotHolder::HandleConsoleWhisper(Player* bot, Player* master, const std::string param)
{
    Player* sender = master;
    Player* reciever = bot;


    if (!reciever)
        return "d requires a bot";

    PlayerbotAI* ai = bot->GetPlayerbotAI();
    if (!ai)
        return "Bot has no AI";

    std::string message = param;

    if (!sender)
    {
        //Try format .(rnd)bot w <sender> <reciever> <message>

        std::string botName = param.substr(0, param.find(" "));

        master = sObjectAccessor.FindPlayerByName(botName.c_str());

        if (master)
        {
            if (message.size() > param.find(" ") + 1)
                message = param.substr(param.find(" ") + 1);
            else
                message = "";

            sender = bot; //Switch sender reciever
            reciever = master; 
        }
    }

    if (!sender)
        sender = bot;

    if (message.empty())
    {
        std::ostringstream out;
        if (!sender->GetPlayerbotAI())
            out << "Player ";
        if (!sender->GetPlayerbotAI()->IsRealPlayer())
            out << "Player bot ";
        else if (sRandomPlayerbotMgr.IsRandomBot(sender))
            out << "Random bot ";
        else if (sPlayerbotAIConfig.IsFreeAltBot(sender))
            out << "Free alt bot ";
        else
            out << "Bot ";

        out << reciever->GetName();
        out << " level " << std::to_string(reciever->GetLevel());
        out << " " << ChatHelper::formatRace(reciever->getRace());
        out << " " << ChatHelper::formatClass(reciever->getClass());

        if (sender->GetPlayerbotAI() && sender->GetPlayerbotAI()->GetMaster())
            out << " (master " << sender->GetPlayerbotAI()->GetMaster()->GetName() << ")";

        return out.str(); 
    }

    WorldPacket packet_template(CMSG_MESSAGECHAT);

    packet_template << CHAT_MSG_WHISPER;
    packet_template << LANG_UNIVERSAL;
    packet_template << reciever->GetName();
    packet_template << message;

    std::unique_ptr<WorldPacket> packetPtr(new WorldPacket(packet_template));

    sender->GetSession()->QueuePacket(std::move(packetPtr));

    std::string msg = "Sending whisper " + message + " to player " + reciever->GetName() + " from " + sender->GetName();

    return msg;
}

std::string PlayerbotHolder::HandleConsoleCmd(Player* bot, Player* master, const std::string param)
{
    if (!bot)
        return "do requires a bot";

    PlayerbotAI* ai = bot->GetPlayerbotAI();
    if (!ai)
        return "Bot has no AI";

    ExternalEventHelper helper(ai->GetAiObjectContext());

    std::string msg = "Sending command " + param + " to player " + bot->GetName();

    if (!helper.ParseChatCommand(param, master ? master : bot))
    {
        return "command failed";
    }    

    return msg;
}

std::string PlayerbotHolder::HandleBotTest(Player* bot, Player* master, const std::string param)
{
    if (!bot)
        return "test requires a bot";

    PlayerbotAI* ai = bot->GetPlayerbotAI();
    if (!ai)
        return "Bot has no AI";

    if (param.empty())
    {
        return "Usage: test <testName>. Available tests: walk_to_ironforge, flight_ratchet_to_booty_bay";
    }

    // Activate test strategy which will run the test over multiple ticks
    std::string strategyName = "test::" + param;
    ai->ChangeStrategy("+" + strategyName, BotState::BOT_STATE_NON_COMBAT);
    
    return "Test '" + param + "' started for bot " + bot->GetName();
}

std::string PlayerbotHolder::HandleBotDo(Player* bot, Player* master, const std::string param)
{
    if (!bot)
        return "do requires a bot";

    PlayerbotAI* ai = bot->GetPlayerbotAI();
    if (!ai)
        return "Bot has no AI";

    std::string actionName = param;
    std::string subparam = "";

    Action* action = nullptr;

    size_t i = std::string::npos;
    while (true)
    {
        action = ai->GetAiObjectContext()->GetAction(param);

        if (action)
            break;

        size_t found = param.rfind(" ", i);
        if (found == std::string::npos || !found)
            break;

        actionName = param.substr(0, found);
        subparam = param.substr(found + 1);

        i = found - 1;
    }

    if (!action)
        return "action not found";

    ai->RecordMessages(true, true);

    std::vector<std::string> output;

    if (!ai->DoSpecificAction(actionName, Event(".bot", subparam, master ? master : bot), true))
    {
        output = GetBotErrors(bot->GetName());

        if (output.empty())
            return "action failed";

        std::string result;
        for (const auto& line : output)
        {
            result += line + "\n";
        }
        return result;
    }

    output = ai->GetRecordedMessages();
    if (output.empty())
        return "(no output)";

    std::string result;
    for (const auto& line : output)
    {
        result += line + "\n";
    }
    return result;
}

std::string PlayerbotHolder::HandleBotRecord(Player* bot, Player* master, const std::string param)
{
    if (!bot)
        return "record requires a bot";

    PlayerbotAI* ai = bot->GetPlayerbotAI();
    if (!ai)
        return "Bot has no AI";

    ai->RecordMessages(true, !param.empty());
    return "Recording enabled on " + std::string(bot->GetName());
}

std::string PlayerbotHolder::HandleBotRead(Player* bot, Player* master, const std::string param)
{
    if (!bot)
        return "read requires a bot";

    PlayerbotAI* ai = bot->GetPlayerbotAI();
    if (!ai)
        return "Bot has no AI";

    std::vector<std::string> output = ai->GetRecordedMessages();

    if (output.empty())
        return "(no messages)";

    std::string result;
    for (const auto& line : output)
    {
        result += line + "\n";
    }
    return result;
}

std::string PlayerbotHolder::HandleBotClear(Player* bot, Player* master, const std::string param)
{
    if (!bot)
        return "clear requires a bot";

    PlayerbotAI* ai = bot->GetPlayerbotAI();
    if (!ai)
        return "Bot has no AI";

    ai->ClearRecordedMessages();
    return "Messages cleared";
}

std::list<std::string> PlayerbotHolder::HandleParty(Player* master, const std::string param, AccountTypes security)
{
    std::string message;
    std::string botName;

    if (!master)
    {
        botName = param.substr(0, param.find(" "));
        master = sObjectAccessor.FindPlayerByName(botName.c_str());
    }

    if (!master)
        return {"No sender found"};

    if (param.find(" ") == std::string::npos)
        message = param;
    else if (param.size() > param.find(" ") + 1)
        message = param.substr(param.find(" ") + 1);

    if (!master->GetGroup())
        return {"Sender is not in a group"};

    if (message.empty())
    {
        Group* group = master->GetGroup();
        Group::MemberSlotList const& members = group->GetMemberSlots();
        Player* leader = sObjectMgr.GetPlayer(group->GetLeaderGuid());

        std::string leaderName = leader ? leader->GetName() : "Unknown";
        std::string otherMembers;

        for (auto const& slot : members)
        {
            if (slot.guid == master->GetObjectGuid())
                continue;

            Player* member = sObjectMgr.GetPlayer(slot.guid);
            if (member)
            {
                if (!otherMembers.empty())
                    otherMembers += ", ";
                otherMembers += member->GetName();
            }
        }

        return {"Party with " + leaderName + " as leader" + (otherMembers.empty() ? "" : " and " + otherMembers)};
    }

    WorldPacket packet_template(CMSG_MESSAGECHAT);
    packet_template << CHAT_MSG_PARTY;
    packet_template << LANG_UNIVERSAL;
    packet_template << message;

    std::unique_ptr<WorldPacket> packetPtr(new WorldPacket(packet_template));
    master->GetSession()->QueuePacket(std::move(packetPtr));
    return {"Sent party message \"" + message + "\" as " + master->GetName()};
}

std::list<std::string> PlayerbotHolder::HandleGuild(Player* master, const std::string param, AccountTypes security)
{
    std::string message;
    std::string botName;

    if (!master)
    {
        botName = param.substr(0, param.find(" "));
        master = sObjectAccessor.FindPlayerByName(botName.c_str());
    }

    if (!master)
        return {"No sender found"};

    if (param.find(" ") == std::string::npos)
        message = param;
    else if (param.size() > param.find(" ") + 1)
        message = param.substr(param.find(" ") + 1);

    if (!master->GetGuildId())
        return {"Sender is not in a guild"};

    if (message.empty())
    {
        Guild* guild = sGuildMgr.GetGuildById(master->GetGuildId());
        if (!guild)
            return {"Guild info not found"};

        std::string guildName = guild->GetName();
        std::string guildLeader;
        sObjectMgr.GetPlayerNameByGUID(guild->GetLeaderGuid(), guildLeader);
        uint32 memberCount = guild->GetMemberSize();

        return {"Guild: " + guildName + ", Leader: " + guildLeader + ", Members: " + std::to_string(memberCount)};
    }

    WorldPacket packet_template(CMSG_MESSAGECHAT);
    packet_template << CHAT_MSG_GUILD;
    packet_template << LANG_UNIVERSAL;
    packet_template << message;

    std::unique_ptr<WorldPacket> packetPtr(new WorldPacket(packet_template));
    master->GetSession()->QueuePacket(std::move(packetPtr));
    return {"Sent guild message \"" + message + "\" as " + master->GetName()};
}

std::list<std::string> PlayerbotHolder::HandleRaid(Player* master, const std::string param, AccountTypes security)
{
    std::string message = param;

    if (!master)
    {
        std::string botName = param.substr(0, param.find(" "));

        master = sObjectAccessor.FindPlayerByName(botName.c_str());
        if (message.size() > param.find(" ") + 1)
            message = param.substr(param.find(" ") + 1);
    }

    if (!master)
        return {"No sender found"};

    if (!master->GetGroup() || !master->GetGroup()->IsRaidGroup())
        return {"Sender is not in a raid group"};

    if (message.empty())
    {
        Group* group = master->GetGroup();
        Group::MemberSlotList const& members = group->GetMemberSlots();
        Player* leader = sObjectMgr.GetPlayer(group->GetLeaderGuid());

        std::string leaderName = leader ? leader->GetName() : "Unknown";
        std::string otherMembers;

        for (auto const& slot : members)
        {
            if (slot.guid == master->GetObjectGuid())
                continue;

            Player* member = sObjectMgr.GetPlayer(slot.guid);
            if (member)
            {
                if (!otherMembers.empty())
                    otherMembers += ", ";
                otherMembers += member->GetName();
            }
        }

        return {"Raid with " + leaderName + " as leader" + (otherMembers.empty() ? "" : " and " + otherMembers)};
    }

    WorldPacket packet_template(CMSG_MESSAGECHAT);
    packet_template << CHAT_MSG_RAID;
    packet_template << LANG_UNIVERSAL;
    packet_template << message;

    std::unique_ptr<WorldPacket> packetPtr(new WorldPacket(packet_template));
    master->GetSession()->QueuePacket(std::move(packetPtr));
    return {"Sent raid message \"" + message + "\" as " + master->GetName()};
}

std::list<std::string> PlayerbotHolder::HandleRaidLeader(Player* master, const std::string param, AccountTypes security)
{
    std::string message = param;

    if (!master)
    {
        std::string botName = param.substr(0, param.find(" "));

        master = sObjectAccessor.FindPlayerByName(botName.c_str());
        if (message.size() > param.find(" ") + 1)
            message = param.substr(param.find(" ") + 1);
    }

    if (!master)
        return {"No sender found"};

    if (!master->GetGroup() || !master->GetGroup()->IsRaidGroup())
        return {"Sender is not in a raid group"};

    WorldPacket packet_template(CMSG_MESSAGECHAT);
    packet_template << CHAT_MSG_RAID;
    packet_template << LANG_UNIVERSAL;
    packet_template << message;

    std::unique_ptr<WorldPacket> packetPtr(new WorldPacket(packet_template));
    master->GetSession()->QueuePacket(std::move(packetPtr));
    std::string result = "Sent raid leader transfer request as " + std::string(master->GetName());
    return {result};
}

std::string PlayerbotHolder::HandleBotAddLogin(Player* bot, Player* master, const std::string param)
{
    if (bot)
        return "Player already logged in";

    // Chat-supplied token: the old isValidNumberString + stoull pair accepted a
    // lone sign and threw on overflow.
    uint64 guidRaw = 0;
    if (!living::TryParseUInt64(param, guidRaw))
        return "Add: Error parsing " + param;

    ObjectGuid guid = ObjectGuid(guidRaw);

    uint32 guildId = Player::GetGuildIdFromDB(guid);
    uint32 masterAccountId = master ? master->GetSession()->GetAccountId() : 0;
    uint32 masterGuildId = master ? master->GetGuildId() : 0;
    uint32 botAccount = sObjectMgr.GetPlayerAccountIdByGUID(guid);
    bool isMasterAccount = (masterAccountId == botAccount);
    bool isRandomAccount = sPlayerbotAIConfig.IsInRandomAccountList(botAccount);

    // Every add/login path must recheck the deletion state: a
    // deletion-pending character may never log in.
    if (PlayerbotHolder::IsDeletionPending(guid.GetCounter()))
        return "Add refused: a character deletion is pending for this guid";

    if (isRandomAccount)
        sRandomPlayerbotMgr.AddRandomBot(guid);
    else if (isMasterAccount || sPlayerbotAIConfig.allowMultiAccountAltBots)
        AddPlayerBot(guid, masterAccountId);
    else
        return "Not in your account";

    return "ok";
}

std::string PlayerbotHolder::HandleBotRemoveLogout(Player* bot, Player* master, const std::string param)
{
    if (!bot)
        return "Player is offline";

    uint32 guildId = Player::GetGuildIdFromDB(bot->GetObjectGuid());
    uint32 masterAccountId = master ? master->GetSession()->GetAccountId() : 0;
    uint32 masterGuildId = master ? master->GetGuildId() : 0;
    uint32 botAccount = sObjectMgr.GetPlayerAccountIdByGUID(bot->GetObjectGuid());
    bool isMasterAccount = (masterAccountId == botAccount);
    bool isRandomAccount = sPlayerbotAIConfig.IsInRandomAccountList(botAccount);

    if (isRandomAccount)
        sRandomPlayerbotMgr.Remove(bot);
    else if (GetPlayerBot(bot->GetGUIDLow()))
        LogoutPlayerBot(bot->GetGUIDLow());
    else
        return "Not your bot";

    return "ok";
}

namespace
{
    // Strict boolean for chat-supplied flags: the loose (value == "1" || ...)
    // idiom silently turned a typo like "temporary=tru" into false and created a
    // permanent character. Malformed values fail the parse instead.
    bool ParseExplicitBool(std::string const& key, std::string const& value, bool& out, std::string& error)
    {
        switch (living::ParseLivingRealmBool(value))
        {
            case living::LivingRealmBool::True:
                out = true;
                return true;
            case living::LivingRealmBool::False:
                out = false;
                return true;
            case living::LivingRealmBool::Malformed:
            default:
                error = "Invalid boolean for " + key + ": " + value;
                return false;
        }
    }
}

// Every CreateBot argument, parsed and validated BEFORE any account or
// character mutation. Absent optional fields keep the legacy default/random
// behavior; an explicitly supplied but invalid field - including unknown keys,
// bare tokens, duplicates, empty values and malformed booleans - fails the
// parse, so nothing invalid can reach GetOrCreateAccount, stat initialization
// or SaveToDB.
bool CreateBotOptions::Parse(Player* master, std::string const& param, std::string& error)
{
    autoAdd = master;
    groupWith = master ? master->GetName() : "";

    static std::set<std::string> const allowedKeys = {
        "name", "faction", "race", "class", "gender", "level", "role",
        "login", "group", "gear", "test", "temporary"
    };

    std::map<std::string, std::string> args;
    if (!living::TryParseKeyValueArgs(param, allowedKeys, args, error))
        return false;

    auto const get = [&args](char const* key) -> std::string const*
    {
        auto const it = args.find(key);
        return it == args.end() ? nullptr : &it->second;
    };

    if (std::string const* value = get("name"))
    {
        // Mirror the cores' character-creation handler exactly: normalization,
        // then the shared syntax/length/language rules, then reserved names.
        // An absent name means "generate one"; "name=" was already rejected as
        // an empty explicit value by the argument parser.
        std::string candidate = *value;
        if (!normalizePlayerName(candidate))
        {
            error = "Invalid name: " + *value;
            return false;
        }

        if (ObjectMgr::CheckPlayerName(candidate, true) != CHAR_NAME_SUCCESS)
        {
            error = "Invalid name: " + *value;
            return false;
        }

        if (sObjectMgr.IsReservedName(candidate))
        {
            error = "Reserved name: " + *value;
            return false;
        }

        name = candidate;
    }

    if (std::string const* value = get("faction"))
    {
        team = ChatHelper::parseTeam(*value);
        if (team == Team::TEAM_BOTH_ALLOWED)
        {
            error = "Invalid faction: " + *value;
            return false;
        }
    }

    if (std::string const* value = get("race"))
    {
        uint32 parsedRace = ChatHelper::parseRace(*value);
        if (!parsedRace)
        {
            error = "Invalid race: " + *value;
            return false;
        }
        race = static_cast<uint8>(parsedRace);
    }

    if (std::string const* value = get("class"))
    {
        uint32 parsedClass = ChatHelper::parseClass(*value);
        if (!parsedClass)
        {
            error = "Invalid class: " + *value;
            return false;
        }
        cls = static_cast<uint8>(parsedClass);
    }

    if (std::string const* value = get("gender"))
    {
        uint32 parsedGender = ChatHelper::parseGender(*value);
        if (parsedGender == GENDER_NONE)
        {
            error = "Invalid gender: " + *value;
            return false;
        }
        gender = static_cast<uint8>(parsedGender);
    }

    if (std::string const* value = get("level"))
    {
        // The old std::stoul had no catch in holder-command dispatch and no
        // domain check, so "level=4294967295" reached SetLevel, stat
        // initialization and SaveToDB.
        if (!living::TryParseUInt32InRange(*value, 1, sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL), level))
        {
            error = "Invalid level: " + *value;
            return false;
        }
    }

    if (std::string const* value = get("role"))
    {
        BotRoles parsedRole = ChatHelper::parseRole(*value);
        if (parsedRole == BotRoles::BOT_ROLE_NONE)
        {
            error = "Invalid role: " + *value;
            return false;
        }
        role = static_cast<uint8>(parsedRole);
        // Every explicit role request is verified against the spec that talent
        // selection actually produces - it is never silently approximated.
        requireRoleMatch = true;
    }

    if (std::string const* value = get("login"))
    {
        bool parsed = false;
        if (!ParseExplicitBool("login", *value, parsed, error))
            return false;
        autoAdd = parsed;
    }

    if (std::string const* value = get("group"))
    {
        groupWith = *value;
        explicitGroup = true;
    }

    if (std::string const* value = get("gear"))
    {
        // Finite whitelist BEFORE any mutation: an arbitrary payload used to
        // be accepted and silently act as "default", and an oversized one
        // cannot fit the durable event envelope once a captured target level
        // is stamped onto it.
        if (!living::IsSupportedGearValue(*value))
        {
            error = "Unsupported gear value '" + *value
                + "' (supported: default, empty, green/uncommon, blue/rare, purple/epic, upgrade, sync, best, partial)";
            return false;
        }
        gear = *value;
    }

    if (std::string const* value = get("temporary"))
    {
        bool parsed = false;
        if (!ParseExplicitBool("temporary", *value, parsed, error))
            return false;
        temporary = parsed;
    }

    // Deliberately last and order-independent: a test bot is always auto-added.
    if (std::string const* value = get("test"))
    {
        testName = *value;
        autoAdd = true;
    }

    // Cross-field compatibility, still before any mutation.
    if (race && team != Team::TEAM_BOTH_ALLOWED && Player::TeamForRace(race) != team)
    {
        error = "Race does not match the requested faction";
        return false;
    }

    if (race && cls && !sObjectMgr.GetPlayerInfo(race, cls))
    {
        error = "Invalid race/class combination";
        return false;
    }

    return true;
}

namespace ai
{
    // Asynchronous creation finalizer. In every pinned core Player::SaveToDB
    // (and Player::DeleteFromDB) commits through the async transaction queue:
    // returning from them confirms ENQUEUEING, not execution, and no
    // module-accessible hook reports the transaction result directly. The one
    // execution-ordered signal available is an AsyncQuery on the SAME FIFO
    // delay thread: it executes after the queued transaction, and its callback
    // (delivered by CharacterDatabase.ProcessResultQueue on the world thread)
    // observes the durable outcome.
    //
    // CRITICAL constraint (verified against all three pinned cores): the SQL
    // worker still holds the async DB connection when it calls
    // SqlResultQueue::Add, and ProcessResultQueue holds the result-queue mutex
    // while executing callbacks. A callback that performs ANY database work
    // (DirectPExecute, another query) can therefore deadlock the world thread
    // against the worker. HandleVerify/HandleCleanupVerify below only parse
    // the result into a small in-memory event, delete the QueryResult and
    // return; every lifecycle transition, metadata write, cleanup, retry and
    // follow-up query runs in PumpBotCreation(), executed from the module's
    // world-update hook - which all three cores call BEFORE
    // World::UpdateResultQueue in a tick, so a queued event is always pumped
    // on a LATER tick, strictly after the result-queue mutex was released.
    //
    // The lifecycle itself lives in living::CreationFinalizer:
    //   SaveToDB queued -> PendingPersistence (no metadata, no quota, no GUID
    //   exposure) -> row + identity verified -> execution-confirmed metadata
    //   writes -> Created (GUID exposed: freeAltBots). Metadata failure ->
    //   PendingCleanup -> confirmed character+event removal -> FailedRetryable;
    //   anything unknown -> Quarantined, permanently.
    class BotCreationFinalizer
    {
    public:
        // Since every creation metadata row (markers, spec events, the
        // durable intent) is execution-confirmed BEFORE the character save,
        // the finalizer's remaining metadata duty is the intent transition:
        // finalized-with-obligations (retaining the restart login
        // authorization) or an immediate clear for markerless creations.
        struct CreationPayload
        {
            std::string intentData;     // the encoded intent, preserved on the 2-write
            bool hasObligations = false;
        };

        living::CreationFinalizer core;
        std::map<uint32, CreationPayload> payloads;

        uint64 Begin(living::CreationFinalizer::Record record, CreationPayload payload)
        {
            payloads[record.guid] = std::move(payload);
            return core.Begin(std::move(record), Ops());
        }

        // SQL result callbacks: parse + copy + return. Nothing here may touch
        // the database or the lifecycle.
        void HandleVerify(QueryResult* result, uint32 guid)
        {
            living::RowVerifyOutcome outcome = living::RowVerifyOutcome::QueryFailed;
            if (result)
            {
                // Identity fields are compared here (pure in-memory reads of
                // the already-materialized result) so the queued event stays
                // small; the decision what to DO with the outcome is deferred.
                Field* fields = result->Fetch();
                if (fields[0].GetUInt32() == 0)
                    outcome = living::RowVerifyOutcome::Absent;
                else
                {
                    uint32 expectedAccount = 0;
                    std::string expectedName;
                    if (ExpectedIdentity(guid, expectedAccount, expectedName)
                        && (fields[1].GetUInt32() != expectedAccount || fields[2].GetCppString() != expectedName))
                        outcome = living::RowVerifyOutcome::IdentityMismatch;
                    else
                        outcome = living::RowVerifyOutcome::Verified;
                }
            }
            delete result;

            core.OnCallbackResult(guid, outcome, living::CreationCallbackKind::Verify);
        }

        void HandleCleanupVerify(QueryResult* result, uint32 guid)
        {
            living::RowVerifyOutcome outcome = living::RowVerifyOutcome::QueryFailed;
            if (result)
                outcome = result->Fetch()[0].GetUInt32() == 0
                    ? living::RowVerifyOutcome::Absent
                    : living::RowVerifyOutcome::Verified; // still present
            delete result;

            core.OnCallbackResult(guid, outcome, living::CreationCallbackKind::CleanupVerify);
        }

        living::CreationFinalizerOps Ops()
        {
            living::CreationFinalizerOps ops;
            ops.requestVerify = [this](uint32 guid) -> bool
            {
                // COUNT + identity aggregates always yield one row on success,
                // so a null callback result is a FAILED query, never absence.
                // AsyncPQuery is the one async-query form all three pinned
                // cores share; its return value reports whether the query was
                // actually ENQUEUED.
                return CharacterDatabase.AsyncPQuery(this, &BotCreationFinalizer::HandleVerify, guid,
                    "SELECT COUNT(*), MIN(account), MIN(name) FROM characters WHERE guid = '%u'", guid);
            };
            ops.requestCleanupVerify = [this](uint32 guid) -> bool
            {
                return CharacterDatabase.AsyncPQuery(this, &BotCreationFinalizer::HandleCleanupVerify, guid,
                    "SELECT COUNT(*) FROM characters WHERE guid = '%u'", guid);
            };
            ops.writeMetadata = [this](uint32 guid)
            {
                auto it = payloads.find(guid);
                if (it == payloads.end())
                    return false;

                // All heavyweight metadata (markers, spec events) was
                // execution-confirmed BEFORE the character save; only the
                // durable intent transitions here - and EVERY creation
                // transitions to Finalized, markerless ones included: the
                // durable login authorization must outlive the crash window
                // between this transition and the memory-only exposure in
                // onCreated. Markerless intents are cleared AFTER exposure
                // (idempotent), never before.
                CreationPayload const& payload = it->second;
                return sRandomPlayerbotMgr.SetValue(guid, "create pending",
                    living::kCreationIntentFinalized, payload.intentData);
            };
            ops.deleteEventRows = [](uint32 guid)
            {
                bool const eventsGone = CharacterDatabase.DirectPExecute(
                    "DELETE FROM ai_playerbot_random_bots WHERE owner = 0 AND bot = '%u'", guid);
                sRandomPlayerbotMgr.ForgetEventCache(guid);
                return eventsGone;
            };
            ops.deleteCharacter = [](uint32 guid, uint32 accountId)
            {
                Player::DeleteFromDB(ObjectGuid(HIGHGUID_PLAYER, guid), accountId, true, true);
            };
            ops.onCreated = [this](uint32 guid, uint32 accountId, std::string const& name, bool autoAdd)
            {
                // Only now is the GUID exposed and the creation complete.
                // Deduplicated: a `.bot always` issued while the creation was
                // pending may already have scheduled this guid.
                if (autoAdd && !sPlayerbotAIConfig.IsFreeAltBot(guid))
                    sPlayerbotAIConfig.freeAltBots.push_back(std::make_pair(accountId, guid));

                // Lifecycle ownership is registered AT FINALIZATION and ONLY
                // when a durable obligation actually exists - a markerless
                // level-1 creation must not leak a permanent scheduler owner.
                // Login authorization (autoAdd, i.e. login=1) travels with the
                // owner AND the durable intent row, so it survives restarts.
                //
                // A markerless creation clears its (finalized) intent only
                // NOW, after the idempotent exposure above: a crash between
                // the finalized transition and this point recovers by
                // REPLAYING the exposure from the intent, then clearing. An
                // unconfirmed clear leaves the row for that same recovery.
                auto payloadIt = payloads.find(guid);
                bool const hasObligations = payloadIt != payloads.end() && payloadIt->second.hasObligations;
                if (hasObligations)
                    sRandomPlayerbotMgr.RegisterPostCreateOwner(guid, accountId, autoAdd);
                else if (!sRandomPlayerbotMgr.SetValue(guid, "create pending", 0))
                    sLog.outError("Bot creation for guid %u: markerless intent clear unconfirmed; "
                        "the next startup replays the exposure and clears it", guid);

                sLog.outDetail("Bot creation finalized: '%s' (guid %u)", name.c_str(), guid);
            };
            return ops;
        }

    private:
        bool ExpectedIdentity(uint32 guid, uint32& accountId, std::string& name) const;
    };

    static BotCreationFinalizer botCreationFinalizer;
    static living::CreationBatchRegistry botCreationBatches;
    // Owns creation tokens abandoned by a test-context reset until they reach a
    // terminal state, deleting each finalized temporary character exactly once
    // (see LivingCreationCleanup.h). Pumped from PumpBotCreation, it survives
    // TestContext::Reset - which the old poll-once-and-clear did not.
    static living::AbandonedCreationCleanup botCreationCleanup;

    // Durable per-GUID deletion owner (see DurableCharacterDeletions): every
    // DeleteBot adoption is confirmed by an execution-ordered absence readback
    // before its metadata is cleared or its account cleanup runs. The same
    // callback contract as the creation finalizer applies: HandleAbsenceVerify
    // only parses into the mailbox; all decisions run in PumpBotCreation.
    class BotDeletionConfirmer
    {
    public:
        living::DurableCharacterDeletions core;

        // The AsyncPQuery parameter packs (generation << 32 | guid) so a late
        // callback from a superseded request generation is distinguishable.
        void HandleAbsenceVerify(QueryResult* result, uint64 packed)
        {
            uint32 const guid = static_cast<uint32>(packed & 0xFFFFFFFFull);
            uint32 const generation = static_cast<uint32>(packed >> 32);

            living::RowVerifyOutcome outcome = living::RowVerifyOutcome::QueryFailed;
            if (result)
            {
                Field* fields = result->Fetch();
                if (fields[0].GetUInt32() == 0)
                    outcome = living::RowVerifyOutcome::Absent;
                else
                {
                    // Identity check against the RECORDED (name, account):
                    // only a full match proves the present row is the
                    // intended character (Verified -> the idempotent deletion
                    // may be re-issued). Anything else - renamed, moved,
                    // reused, or never recorded - is unproven identity and
                    // FAILS CLOSED (quarantine; never delete).
                    std::string expectedName;
                    uint32 expectedAccount = 0;
                    bool const identityKnown = core.TryGetExpectedIdentity(guid, expectedName, expectedAccount);
                    bool const identityProven = identityKnown
                        && !expectedName.empty() && fields[1].GetCppString() == expectedName
                        && expectedAccount != 0 && fields[2].GetUInt32() == expectedAccount;
                    outcome = identityProven ? living::RowVerifyOutcome::Verified
                                             : living::RowVerifyOutcome::IdentityMismatch;
                }
            }
            delete result;

            core.OnAbsenceVerify(guid, generation, outcome);
        }

        living::CharacterDeletionOps Ops()
        {
            living::CharacterDeletionOps ops;
            ops.deleteCharacter = [](uint32 guid, uint32 accountId)
            {
                Player::DeleteFromDB(ObjectGuid(HIGHGUID_PLAYER, guid), accountId, true, true);
            };
            ops.requestAbsenceVerify = [this](uint32 guid, uint32 generation) -> bool
            {
                // COUNT + MIN(name) always yields one row on success, so a
                // null callback result is a FAILED query, never absence. The
                // query is queued on the same FIFO thread as the deletion
                // transaction, so its execution is ordered behind it.
                uint64 const packed = (static_cast<uint64>(generation) << 32) | guid;
                return CharacterDatabase.AsyncPQuery(this, &BotDeletionConfirmer::HandleAbsenceVerify, packed,
                    "SELECT COUNT(*), MIN(name), MIN(account) FROM characters WHERE guid = '%u'", guid);
            };
            ops.clearMetadata = [](uint32 guid) -> bool
            {
                // Absence is re-proven BY THE STATEMENT ITSELF: the predicate
                // makes this a no-op if a replacement character claimed the
                // guid between the absence readback and this clear - a stale
                // absence observation can then never erase the replacement's
                // fresh creation metadata. (No rows deleted still executes
                // successfully: the pending deletion completes either way.)
                bool const cleared = CharacterDatabase.DirectPExecute(
                    "DELETE FROM ai_playerbot_random_bots WHERE owner = 0 AND bot = '%u' "
                    "AND '%u' NOT IN (SELECT guid FROM characters WHERE guid = '%u')", guid, guid, guid);
                sRandomPlayerbotMgr.ForgetEventCache(guid);
                return cleared;
            };
            ops.revokeLogin = [](uint32 guid)
            {
                auto it = std::remove_if(sPlayerbotAIConfig.freeAltBots.begin(), sPlayerbotAIConfig.freeAltBots.end(),
                    [guid](std::pair<uint32, uint32> entry) { return entry.second == guid; });
                sPlayerbotAIConfig.freeAltBots.erase(it, sPlayerbotAIConfig.freeAltBots.end());
                sRandomPlayerbotMgr.ForgetPostCreateOwner(guid);
            };
            ops.onConfirmedDeleted = [](uint32 guid, uint32 accountId)
            {
                PlayerbotHolder::OnCharacterDeletionConfirmed(guid, accountId);
            };
            ops.onQuarantined = [](uint32 guid)
            {
                sLog.outError("Character deletion for guid %u could not be confirmed (bounded attempts exhausted, "
                    "or the present row's identity does not match the recorded intent); its rows are kept "
                    "fail-closed - inspect and resolve manually or let a later restart retry", guid);
            };
            return ops;
        }
    };

    static BotDeletionConfirmer botDeletionConfirmer;

    bool BotCreationFinalizer::ExpectedIdentity(uint32 guid, uint32& accountId, std::string& name) const
    {
        // Reading the in-memory record from the callback is safe (world
        // thread, in-memory map only); this const helper exists so the
        // callback cannot accidentally reach mutating members.
        return core.WithRecord(guid, [&](living::CreationFinalizer::Record const& record)
        {
            accountId = record.accountId;
            name = record.name;
        });
    }
}

living::CreationPollResult PlayerbotHolder::PollBotCreation(uint64 creationToken, bool acknowledgeTerminal)
{
    return ai::botCreationFinalizer.core.Poll(creationToken, acknowledgeTerminal);
}

living::BatchPollResult PlayerbotHolder::PollBotCreationBatch(uint64 batchToken, bool acknowledgeComplete)
{
    return ai::botCreationBatches.Poll(batchToken, acknowledgeComplete);
}

void PlayerbotHolder::AbandonCreationToken(uint64 creationToken)
{
    // Transfer a still-pending single-creation token to the deferred owner so a
    // later finalization still has a cleanup owner (no leaked temporary bot).
    ai::botCreationCleanup.AdoptSingle(creationToken);
}

void PlayerbotHolder::AbandonBatchToken(uint64 batchToken)
{
    // Transfer a still-pending group-batch token to the deferred owner; it will
    // delete the finalized members only once the batch is Complete.
    ai::botCreationCleanup.AdoptBatch(batchToken);
}

void PlayerbotHolder::AdoptCharacterDeletion(uint32 guid, uint32 accountId, std::string const& expectedName,
    bool identityProvenInProcess)
{
    ai::botDeletionConfirmer.core.Adopt(guid, accountId, expectedName, identityProvenInProcess,
        ai::botDeletionConfirmer.Ops());
}

namespace ai
{
    // Bounded-backoff retry state for the deletion-intent scan (armed by
    // ReadoptPendingDeletions itself, driven from PumpBotCreation). STARTS
    // failed: until the first scan SUCCEEDS, IsDeletionPending fails closed
    // for every guid.
    static bool deletionIntentScanFailed = true;
    static uint32 deletionIntentScanRetryPasses = 0;
    static constexpr uint32 kDeletionScanRetryPasses = 600;

    // Same pattern for the creation-intent scan.
    static bool creationIntentScanFailed = false;
    static uint32 creationIntentScanRetryPasses = 0;
}

bool PlayerbotHolder::ReadoptPendingDeletions()
{
    // Durable deletion intents ('delete' event rows, written by DeleteBot
    // BEFORE the deletion is queued) are re-adopted at startup/reload, so a
    // crash between queueing DeleteFromDB and its confirmed absence can never
    // silently lose an ordinary deletion. A character still present under its
    // FULL recorded identity (name AND account) re-runs the deletion path;
    // anything else is adopted for confirmation only - absence confirms with
    // the RECORDED account for empty-account cleanup, unproven identities
    // fail closed. COUNT-first: an empty scan is a success, a failed one
    // arms the bounded-backoff retry in the creation pump.
    ai::deletionIntentScanFailed = true;
    ai::deletionIntentScanRetryPasses = 0;

    auto countResult = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM ai_playerbot_random_bots WHERE owner = 0 AND event = 'delete'");
    if (!countResult)
        return false;
    if (countResult->Fetch()[0].GetUInt32() == 0)
    {
        ai::deletionIntentScanFailed = false;
        return true;
    }

    auto result = CharacterDatabase.Query(
        "SELECT r.bot, r.data, c.name, c.account FROM ai_playerbot_random_bots r "
        "LEFT JOIN characters c ON c.guid = r.bot WHERE r.owner = 0 AND r.event = 'delete'");
    if (!result)
        return false;

    ai::deletionIntentScanFailed = false;
    do
    {
        Field* fields = result->Fetch();
        uint32 const guid = fields[0].GetUInt32();
        std::string recordedName;
        uint32 recordedAccount = 0;
        living::DecodeDeletionIntent(fields[1].GetCppString(), recordedName, recordedAccount);
        bool const present = !fields[2].IsNULL();
        std::string const currentName = present ? fields[2].GetCppString() : std::string();
        uint32 const currentAccount = present ? fields[3].GetUInt32() : 0;

        // Re-adoption is idempotent (Adopt deduplicates). Absent rows adopt
        // with the RECORDED account so empty-account cleanup still runs. A
        // PRESENT row is NEVER automatically re-deleted after a restart:
        // (guid, name, account) is not immutable across restarts (same-guid
        // reuse under the same name/account is possible and no immutable
        // nonce exists in the schema), so it is adopted pre-quarantined -
        // login stays blocked, everything retained for manual resolution.
        (void)currentName;
        (void)currentAccount;
        switch (living::PlanDeletionIntentRecovery(present))
        {
            case living::DeletionIntentRecovery::QuarantinePresent:
                sLog.outError("Deletion intent for guid %u ('%s') found a PRESENT character after restart; "
                    "identity cannot be proven immutable - quarantined for manual resolution, login blocked",
                    guid, recordedName.c_str());
                ai::botDeletionConfirmer.core.AdoptQuarantined(guid, ai::botDeletionConfirmer.Ops());
                break;
            case living::DeletionIntentRecovery::AdoptForCleanup:
                AdoptCharacterDeletion(guid, recordedAccount, recordedName, false);
                break;
        }
    } while (result->NextRow());

    return true;
}

bool PlayerbotHolder::ReadoptPendingCreations()
{
    // Durable creation intents ('create pending', execution-confirmed BEFORE
    // the character save) are re-adopted at startup/reload, closing the crash
    // window between the character commit and finalizer registration:
    //  - character ABSENT: the save never committed (or rolled back) - the
    //    residue rows are discarded;
    //  - pre-persistence intent + character PRESENT: finalization is resumed
    //    through the normal finalizer (verify -> intent transition ->
    //    exposure/owner), with the payload decoded from the intent;
    //  - finalized intent + PRESENT: the post-create owner is reconstructed
    //    with the intent's persisted login authorization.
    // COUNT-first; a failed scan arms the bounded-backoff retry in the pump.
    ai::creationIntentScanFailed = true;
    ai::creationIntentScanRetryPasses = 0;

    auto countResult = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM ai_playerbot_random_bots WHERE owner = 0 AND event = 'create pending'");
    if (!countResult)
        return false;
    if (countResult->Fetch()[0].GetUInt32() == 0)
    {
        ai::creationIntentScanFailed = false;
        return true;
    }

    auto result = CharacterDatabase.Query(
        "SELECT r.bot, r.value, r.data, c.account, c.name FROM ai_playerbot_random_bots r "
        "LEFT JOIN characters c ON c.guid = r.bot WHERE r.owner = 0 AND r.event = 'create pending'");
    if (!result)
        return false;

    ai::creationIntentScanFailed = false;
    do
    {
        Field* fields = result->Fetch();
        uint32 const guid = fields[0].GetUInt32();
        uint32 const value = fields[1].GetUInt32();
        std::string recordedName;
        uint32 recordedAccount = 0;
        uint32 level = 0;
        bool autoAdd = false;
        bool hasObligations = false;
        bool const intentValid = living::DecodeCreationIntent(fields[2].GetCppString(), recordedName,
            recordedAccount, level, autoAdd, hasObligations);
        bool const present = !fields[4].IsNULL();
        uint32 const currentAccount = present ? fields[3].GetUInt32() : 0;
        std::string const currentName = present ? fields[4].GetCppString() : std::string();

        if (!intentValid)
        {
            sRandomPlayerbotMgr.QuarantinePostCreateOwner(guid);
            sLog.outError("ReadoptPendingCreations: guid %u has a malformed creation intent; retained QUARANTINED - resolve manually", guid);
            continue;
        }

        // The RECORDED identity is the anchor: a present row whose identity
        // does not match the intent belongs to a reused GUID (the original
        // save rolled back and a legacy/normal character took the id). Such a
        // row is NEVER finalized, exposed, mutated, or auto-logged-in - the
        // intent row is retained quarantined for manual resolution.
        bool const identityMatches = present && living::CreationIdentityMatches(
            recordedName, recordedAccount, currentName, currentAccount);

        switch (living::PlanCreationIntentRecovery(present, value))
        {
            case living::CreationIntentRecovery::Quarantine:
                sRandomPlayerbotMgr.QuarantinePostCreateOwner(guid);
                sLog.outError("ReadoptPendingCreations: guid %u has undefined creation-intent phase %u; retained QUARANTINED - resolve manually",
                    guid, value);
                break;
            case living::CreationIntentRecovery::DiscardResidue:
                // Absence proven by this scan; nothing else can own the rows.
                // The discard must be CONFIRMED - an unconfirmed delete keeps
                // the intent as the durable owner and re-arms the retry, so
                // residue can never linger silently into GUID reuse.
                if (CharacterDatabase.DirectPExecute(
                        "DELETE FROM ai_playerbot_random_bots WHERE owner = 0 AND bot = '%u'", guid))
                    sRandomPlayerbotMgr.ForgetEventCache(guid);
                else
                    ai::creationIntentScanFailed = true;
                break;
            case living::CreationIntentRecovery::ResumeFinalization:
            {
                if (!identityMatches)
                {
                    sRandomPlayerbotMgr.QuarantinePostCreateOwner(guid);
                    sLog.outError("ReadoptPendingCreations: guid %u holds a pre-persistence intent for '%s' but the "
                        "present character is '%s' (reused id); intent retained QUARANTINED - resolve manually",
                        guid, recordedName.c_str(), currentName.c_str());
                    break; // never mutate the unrelated row; intent stays inert
                }
                if (IsDeletionPending(guid))
                {
                    // Fail closed while deletion state is unknown/owned - and
                    // RE-ARM this scan so the skipped intent is retried once
                    // the deletion scan recovers, instead of being stranded
                    // until a manual reload.
                    ai::creationIntentScanFailed = true;
                    break;
                }
                bool alreadyOwned = ai::botCreationFinalizer.core.WithRecord(guid,
                    [](living::CreationFinalizer::Record const&) {});
                if (alreadyOwned)
                    break; // idempotent reload: the record already exists
                if (!ai::botCreationFinalizer.core.CanAdmit())
                {
                    sLog.outError("ReadoptPendingCreations: finalizer registry full; intent for guid %u retries later", guid);
                    ai::creationIntentScanFailed = true; // re-arm the bounded retry
                    break;
                }

                sLog.outDetail("Resuming interrupted creation finalization for guid %u ('%s')", guid, recordedName.c_str());
                living::CreationFinalizer::Record record;
                record.guid = guid;
                // The RECORDED identity, never the current row: the
                // finalizer's identity verification then re-proves the row
                // against the intent (a mismatch quarantines) instead of
                // tautologically confirming whatever occupies the id now.
                record.accountId = recordedAccount;
                record.name = recordedName;
                record.autoAdd = autoAdd;
                record.holdsAccountReservation = false; // reservations are process-local
                ai::BotCreationFinalizer::CreationPayload payload;
                payload.intentData = fields[2].GetCppString();
                payload.hasObligations = hasObligations;
                ai::botCreationFinalizer.Begin(std::move(record), std::move(payload));
                break;
            }
            case living::CreationIntentRecovery::ReconstructOwner:
                if (!identityMatches)
                {
                    sRandomPlayerbotMgr.QuarantinePostCreateOwner(guid);
                    sLog.outError("ReadoptPendingCreations: guid %u holds a finalized intent for '%s' but the present "
                        "character is '%s'; intent retained QUARANTINED - resolve manually",
                        guid, recordedName.c_str(), currentName.c_str());
                    break;
                }
                if (hasObligations)
                    sRandomPlayerbotMgr.RegisterPostCreateOwner(guid, recordedAccount, autoAdd);
                else
                {
                    // Markerless finalized intent: the crash hit between the
                    // finalized transition and exposure. REPLAY the idempotent
                    // exposure first (restoring login=1 where authorized),
                    // then clear with confirmation.
                    if (autoAdd && !sPlayerbotAIConfig.IsFreeAltBot(guid))
                        sPlayerbotAIConfig.freeAltBots.push_back(std::make_pair(recordedAccount, guid));
                    if (!sRandomPlayerbotMgr.SetValue(guid, "create pending", 0))
                        ai::creationIntentScanFailed = true; // retried; exposure replay is idempotent
                }
                break;
        }
    } while (result->NextRow());

    return true;
}

bool PlayerbotHolder::DeletionStateKnown()
{
    return !ai::deletionIntentScanFailed;
}

bool PlayerbotHolder::IsDeletionPending(uint32 guid)
{
    // GLOBALLY fail closed while the durable deletion-intent scan has not
    // SUCCEEDED (including before the first startup scan): an empty in-memory
    // confirmer proves nothing about durable 'delete' rows the scan could not
    // read, so every login/add/post-create path treats every guid as
    // possibly deletion-pending until the scan lands.
    return ai::deletionIntentScanFailed || ai::botDeletionConfirmer.core.Owns(guid);
}

void PlayerbotHolder::OnCharacterDeletionConfirmed(uint32 guid, uint32 accountId)
{
    // Absence is confirmed: the durable character count now reflects the
    // deletion, so the account-cleanup hook can act on truthful occupancy.
    static_cast<PlayerbotHolder&>(sRandomPlayerbotMgr).OnBotDeleted(guid, accountId);
}

uint32 PlayerbotHolder::MaxCharsPerAccount()
{
#ifdef MANGOSBOT_TWO
    return 10;
#else
    return 9;
#endif
}

bool PlayerbotHolder::TryGetDurableCharacterCount(uint32 accountId, uint32& count)
{
    // Own COUNT query instead of sAccountMgr.GetCharactersCount: the cores
    // collapse a failed query to zero, which would admit a full account as
    // empty. COUNT always yields one row on success, so null = failed.
    auto result = CharacterDatabase.PQuery("SELECT COUNT(guid) FROM characters WHERE account = '%u'", accountId);
    if (!result)
        return false;

    count = result->Fetch()[0].GetUInt32();
    return true;
}

bool PlayerbotHolder::TryGetEffectiveCharacterCount(uint32 accountId, uint32& count)
{
    if (!TryGetDurableCharacterCount(accountId, count))
        return false;

    count += ai::botCreationFinalizer.core.ReservedCount(accountId);
    return true;
}

living::SlotReservationOutcome PlayerbotHolder::TryReserveCharacterSlot(uint32 accountId)
{
    uint32 durableCount = 0;
    if (!TryGetDurableCharacterCount(accountId, durableCount))
        return living::SlotReservationOutcome::DatabaseUnavailable;

    return TryReserveCharacterSlot(accountId, durableCount);
}

living::SlotReservationOutcome PlayerbotHolder::TryReserveCharacterSlot(uint32 accountId, uint32 durableCount)
{
    // The DURABLE count goes in; the ledger adds its own reservations exactly
    // once inside TryReserveAccountSlot - passing an already-effective count
    // here would double-charge existing reservations.
    return ai::botCreationFinalizer.core.TryReserveAccountSlot(accountId, durableCount, MaxCharsPerAccount())
        ? living::SlotReservationOutcome::Reserved
        : living::SlotReservationOutcome::AtCapacity;
}

void PlayerbotHolder::ReleaseCharacterSlot(uint32 accountId)
{
    ai::botCreationFinalizer.core.ReleaseAccountSlot(accountId);
}

AccountLookupOutcome PlayerbotHolder::TryLookupAccountId(std::string const& accountName, uint32& accountId)
{
    // COUNT + MIN aggregate: it always yields one row on success, so a null
    // result is a FAILED query - never a missing account. Only a confirmed
    // zero count is Missing (the sole outcome that may trigger account
    // creation). The name is escaped here, once.
    std::string escapedName = accountName;
    LoginDatabase.escape_string(escapedName);

    auto result = LoginDatabase.PQuery(
        "SELECT COUNT(*), MIN(id) FROM account WHERE username = '%s'", escapedName.c_str());
    if (!result)
        return AccountLookupOutcome::DatabaseUnavailable;

    Field* fields = result->Fetch();
    if (fields[0].GetUInt32() == 0)
        return AccountLookupOutcome::Missing;

    accountId = fields[1].GetUInt32();
    return AccountLookupOutcome::Found;
}

namespace ai
{
    static void NotifyBatchInitiator(living::CreationBatchRegistry::Batch const& batch, std::string const& message, bool isError = true)
    {
        if (isError)
            sLog.outError("Group creation for %s: %s", batch.initiatorName.c_str(), message.c_str());
        else
            sLog.outString("Group creation for %s: %s", batch.initiatorName.c_str(), message.c_str());

        if (batch.initiatorGuid)
            if (Player* initiator = sObjectMgr.GetPlayer(ObjectGuid(HIGHGUID_PLAYER, batch.initiatorGuid)))
                ChatHandler(initiator).PSendSysMessage("Group creation: %s", message.c_str());
    }

    // Drains the deferred SQL-callback events and drives ALL creation
    // lifecycle work (metadata writes, cleanup, retries, follow-up queries),
    // then propagates terminal results to group/test batches - including
    // bounded replacement of retryably failed members. Runs from
    // PlayerbotHolder::UpdateSessions: a normal world update that all three
    // pinned cores execute BEFORE World::UpdateResultQueue, so nothing here
    // ever runs inside the SQL result-queue mutex.
    static uint64 creationPumpTick = 0;

    uint64 CreationPumpTick()
    {
        return creationPumpTick;
    }

    void PumpBotCreation()
    {
        uint64 const pumpCount = ++creationPumpTick;

        botCreationFinalizer.core.Pump(botCreationFinalizer.Ops(),
            [pumpCount](living::CreationCompletion const& completion)
        {
            switch (completion.status)
            {
                case living::CreationPollStatus::Created:
                    break; // onCreated already logged
                case living::CreationPollStatus::FailedRetryable:
                case living::CreationPollStatus::FailedTerminal:
                case living::CreationPollStatus::Quarantined:
                default:
                    sLog.outError("Bot creation for '%s' (guid %u): %s",
                        completion.name.c_str(), completion.guid, completion.message.c_str());
                    break;
            }

            botCreationFinalizer.payloads.erase(completion.guid);

            // Publish to the batch coordinator BEFORE the finalizer removes
            // the record: the member transitions in place (Finalized,
            // back to AwaitingAttempt as an in-slot replacement, or a
            // recorded TerminalFailure).
            botCreationBatches.OnCreationTerminal(completion, pumpCount);
        });

        // Paced attempt loop: every planned slot whose backoff elapsed -
        // in-slot replacements, initial transient shortfalls, and transient
        // retries all flow through here, so pacing and budgets are enforced
        // in ONE place. Attempts run AFTER the pump drained (CreateBot
        // re-enters the finalizer via Begin).
        for (living::DueAttempt const& attempt : botCreationBatches.TakeDueAttempts(pumpCount))
        {
            living::CreationBatchRegistry::Batch const* batch = botCreationBatches.Find(attempt.batchToken);
            if (!batch)
                continue;

            Player* master = batch->initiatorGuid
                ? sObjectMgr.GetPlayer(ObjectGuid(HIGHGUID_PLAYER, batch->initiatorGuid)) : nullptr;
            if (!master)
            {
                botCreationBatches.OnAttemptResult(attempt.batchToken, attempt.memberIndex,
                    living::BotCreateStatus::TerminalFailure, 0, 0, pumpCount);
                botCreationBatches.RecordFailure(attempt.batchToken,
                    "member attempt skipped: initiator is offline");
                continue;
            }

            // The slot's PLANNED role/class are preserved (per-class quotas
            // were charged for them); an unplanned slot (initial transient
            // shortfall) draws its role now.
            uint8 role = attempt.role;
            if (!role)
            {
                static BotRoles const groupRoles[3] = { BotRoles::BOT_ROLE_TANK, BotRoles::BOT_ROLE_HEALER, BotRoles::BOT_ROLE_DPS };
                role = static_cast<uint8>(groupRoles[urand(0, 2)]);
            }

            uint8 race = 0;
            uint8 cls = 0;
            if (!RandomPlayerbotFactory::GetRandomTuple(master->GetTeam(), static_cast<BotRoles>(role),
                    0, attempt.cls, race, cls))
            {
                botCreationBatches.OnAttemptResult(attempt.batchToken, attempt.memberIndex,
                    living::BotCreateStatus::TerminalFailure, 0, 0, pumpCount);
                botCreationBatches.RecordFailure(attempt.batchToken,
                    "member attempt skipped: no compatible race/class tuple");
                continue;
            }

            CreateBotOptions options;
            options.level = master->GetLevel();
            options.race = race;
            options.cls = cls;
            options.role = role;
            options.requireRoleMatch = true;
            options.groupWith = master->GetName();
            options.explicitGroup = true;
            options.gear = batch->memberGear;
            options.autoAdd = batch->memberAutoAdd;
            options.temporary = batch->memberTemporary;

            // The attempt allocates on the SAME holder mode as the original
            // run, recorded in the batch: a `.rndbot group` run through the
            // random manager must keep allocating on random accounts even
            // when the initiating master also owns a personal PlayerbotMgr.
            PlayerbotHolder* holder = nullptr;
            if (batch->useRandomAccounts)
                holder = static_cast<PlayerbotHolder*>(&sRandomPlayerbotMgr);
            else if (master->GetPlayerbotMgr())
                holder = static_cast<PlayerbotHolder*>(master->GetPlayerbotMgr());

            if (!holder)
            {
                botCreationBatches.OnAttemptResult(attempt.batchToken, attempt.memberIndex,
                    living::BotCreateStatus::TerminalFailure, 0, 0, pumpCount);
                botCreationBatches.RecordFailure(attempt.batchToken,
                    "member attempt skipped: initiator has no bot manager");
                continue;
            }

            // The typed result flows back verbatim: transient failures keep
            // the slot with paced bounded retries (never consuming the
            // replacement budget), retryable failures consume it, terminal
            // failures are recorded.
            BotCreationResult result = holder->CreateBot(master, options);
            botCreationBatches.OnAttemptResult(attempt.batchToken, attempt.memberIndex,
                result.status, result.creationToken, result.createdClass, pumpCount, result.createdRole);
        }

        // Surface completed batches exactly once - the requested group is
        // never left silently undersized.
        for (living::CreationBatchRegistry::Batch const* batch : botCreationBatches.TakeNewlyCompleted())
        {
            size_t finalized = 0;
            for (living::CreationBatchMember const& member : batch->members)
                if (member.state == living::BatchMemberState::Finalized)
                    ++finalized;

            std::ostringstream summary;
            summary << "batch complete: " << finalized << " member(s) finalized";
            uint32 const baseline = batch->EffectivePreexisting();
            uint32 const wanted = batch->desiredSize > baseline
                ? batch->desiredSize - baseline : 0;
            if (finalized < wanted)
                summary << " of " << wanted << " requested";
            if (!batch->failures.empty())
                summary << ", " << batch->failures.size() << " failure(s), first: " << batch->failures.front();
            NotifyBatchInitiator(*batch, summary.str(), !batch->failures.empty());
        }

        botCreationBatches.PruneExpired(pumpCount);

        // Drive deferred cleanup of tokens abandoned by a test-context reset:
        // each is polled to terminal and every finalized temporary character is
        // deleted exactly once (a still-live batch's members are never touched).
        living::CreationCleanupOps cleanupOps;
        cleanupOps.pollSingle = [](uint64 token, bool ack) { return PlayerbotHolder::PollBotCreation(token, ack); };
        cleanupOps.pollBatch = [](uint64 token, bool ack) { return PlayerbotHolder::PollBotCreationBatch(token, ack); };
        cleanupOps.deleteCharacter = [](uint32 guid) -> bool
        {
            // DeleteBot revokes login eligibility and ADOPTS the deletion into
            // the durable confirmer. Its result is DURABLE OWNERSHIP: false
            // (fail-closed intent refusal / unknown identity) must keep the
            // cleanup token alive - acknowledging would orphan the character.
            return sRandomPlayerbotMgr.DeleteBot(ObjectGuid(HIGHGUID_PLAYER, guid), true);
        };
        botCreationCleanup.Pump(cleanupOps);

        // Drive the durable deletion confirmations (absence readbacks, bounded
        // re-issues, metadata clears, account cleanup).
        botDeletionConfirmer.core.Pump(botDeletionConfirmer.Ops());

        // Bounded-backoff retry of failed intent scans: a durable intent must
        // never wait for a manual reload just because the scan raced a
        // database blip. Deletion first - creation resumption defers to
        // deletion-pending state.
        if (deletionIntentScanFailed && ++deletionIntentScanRetryPasses >= kDeletionScanRetryPasses)
        {
            if (PlayerbotHolder::ReadoptPendingDeletions())
            {
                // Deletion state just became KNOWN: the dependent scans ran
                // (or skipped rows) under the fail-closed assumption and must
                // rerun against real deletion ownership.
                PlayerbotHolder::ReadoptPendingCreations();
                sRandomPlayerbotMgr.ReconstructPostCreateOwners();
            }
        }
        if (creationIntentScanFailed && ++creationIntentScanRetryPasses >= kDeletionScanRetryPasses)
            PlayerbotHolder::ReadoptPendingCreations();
    }
}

BotCreationResult PlayerbotHolder::CreateBot(Player* master, const std::string param)
{
    // Allow null master for RA/console usage
    // Player* master can be null when called via .rndbot commands

    CreateBotOptions options;
    std::string error;
    if (!options.Parse(master, param, error))
    {
        BotCreationResult result;
        result.status = living::BotCreateStatus::TerminalFailure;
        result.messages.push_back(error);
        return result;
    }

    return CreateBot(master, options);
}

BotCreationResult PlayerbotHolder::CreateBot(Player* master, CreateBotOptions const& options)
{
    BotCreationResult result;

    // One documented rule for unverifiable roles: roles are talent-derived, and
    // below level 10 a character has no talent points, so a role-constrained
    // creation cannot be verified there. It fails HERE - before any account,
    // DB, or character mutation - instead of persisting a member with a
    // fictional role counted against a quota.
    if (options.requireRoleMatch && options.role && options.level < 10)
    {
        result.status = living::BotCreateStatus::TerminalFailure;
        result.messages.push_back("Roles are talent-derived and cannot be verified below level 10; use level=10 or higher with role=");
        return result;
    }

    // Resolve the EFFECTIVE race/class/faction/gender/role tuple first and
    // validate it before any allocation or mutation. Explicit fields were
    // already validated in Parse; resolution covers the randomly chosen rest
    // (e.g. Classic "faction=horde class=paladin" used to fall back to a Human
    // and persist a team-violating character).
    Team team = options.team;
    if (team == TEAM_BOTH_ALLOWED && master)
        team = master->GetTeam();

    uint8 gender = options.gender;
    if (gender == GENDER_NONE)
        gender = urand(GENDER_MALE, GENDER_FEMALE);

    // Joint constrained selection: any unresolved race/class is drawn as one
    // race+class tuple filtered by team, role, explicit constraints, template
    // existence and nonzero weight, so a random draw can never pick a class the
    // team cannot play (Classic Horde drawing paladin) and then fail although
    // compatible alternatives exist.
    uint8 cls = options.cls;
    uint8 race = options.race;
    if (cls == 0 || race == 0)
    {
        if (!RandomPlayerbotFactory::GetRandomTuple(team, static_cast<BotRoles>(options.role), options.race, options.cls, race, cls))
        {
            result.status = living::BotCreateStatus::TerminalFailure;
            result.messages.push_back("No compatible race/class combination exists for the requested faction/role");
            return result;
        }
    }

    // Backstop validation of the effective tuple (also covers the fully
    // explicit case, which skips the selector).
    if (options.role && !RandomPlayerbotFactory::isAvailableRole(cls, static_cast<BotRoles>(options.role)))
    {
        result.status = living::BotCreateStatus::TerminalFailure;
        result.messages.push_back("Class " + ChatHelper::formatClass(cls) + " cannot fill the requested role");
        return result;
    }

    if (!sObjectMgr.GetPlayerInfo(race, cls))
    {
        result.status = living::BotCreateStatus::TerminalFailure;
        result.messages.push_back("Invalid race/class combination");
        return result;
    }

    if (team != TEAM_BOTH_ALLOWED && Player::TeamForRace(race) != team)
    {
        result.status = living::BotCreateStatus::TerminalFailure;
        result.messages.push_back("Race does not match the requested faction");
        return result;
    }

    // Name collision is checked BEFORE account allocation, through the object
    // manager's escaped lookup - the raw name is never interpolated into SQL.
    // An explicit name can never succeed on retry, so this is terminal.
    if (!options.name.empty() && sObjectMgr.GetPlayerGuidByName(options.name))
    {
        result.status = living::BotCreateStatus::TerminalFailure;
        result.messages.push_back("Name already exists");
        return result;
    }

    // Bounded pending registry: while any creation's durability or cleanup is
    // unknown its record occupies capacity, and a full registry refuses new
    // work instead of reusing uncertain state.
    if (!ai::botCreationFinalizer.core.CanAdmit())
    {
        result.status = living::BotCreateStatus::TerminalFailure;
        result.messages.push_back("Bot creation pipeline is at capacity (pending/quarantined creations); try again later");
        return result;
    }

    // Every event value this creation may persist must fit the real schema
    // limits BEFORE any account allocation, GUID allocation, or SaveToDB. A
    // 256-byte gear/group/test value must fail here, creating nothing - not
    // after the character already exists.
    {
        std::string const oversized = living::FindOversizedCreationValue(options.gear, options.groupWith, options.testName, options.name);
        if (!oversized.empty())
        {
            result.status = living::BotCreateStatus::TerminalFailure;
            result.messages.push_back("Value for '" + oversized + "' exceeds the " + std::to_string(living::EVENT_DATA_MAX_BYTES) + "-byte limit");
            return result;
        }
    }

    // An explicit group target must resolve BEFORE creation, and is stored as
    // a stable GUID instead of a mutable name: renames can no longer orphan
    // the join, and the login path can distinguish "target offline" (retry)
    // from "target deleted" (terminal).
    bool const wantsGroupEvent = !options.groupWith.empty() && (options.explicitGroup || options.level > 1);
    std::string groupTargetData;
    if (wantsGroupEvent)
    {
        ObjectGuid const groupTarget = sObjectMgr.GetPlayerGuidByName(options.groupWith);
        if (!groupTarget)
        {
            result.status = living::BotCreateStatus::TerminalFailure;
            result.messages.push_back("Group target '" + options.groupWith + "' does not exist");
            return result;
        }

        groupTargetData = std::to_string(groupTarget.GetCounter());
    }

    std::string error;
    uint32 accountId = 0;
    switch (GetOrCreateAccount(master, accountId, error))
    {
        case AccountSelectOutcome::Found:
            break;
        case AccountSelectOutcome::DatabaseUnavailable:
            // Transient: the database could not answer. NOT terminal (capacity
            // is unknown, not exhausted) and NOT retried inside this call -
            // coordinators back off across ticks.
            result.status = living::BotCreateStatus::TransientFailure;
            result.messages.push_back("Database unavailable while selecting a bot account; try again later");
            return result;
        case AccountSelectOutcome::CapacityExhausted:
        case AccountSelectOutcome::AccountCreationFailed:
        default:
            result.status = living::BotCreateStatus::TerminalFailure;
            result.messages.push_back(error.empty() ? "No bot account available" : error);
            return result;
    }

    // Reserve one character slot BEFORE any GUID allocation or SaveToDB,
    // through the ONE shared reservation API (typed durable count + finalizer
    // ledger, combined exactly once). A burst of creations (group loop, test
    // runner) cannot observe the same stale count and overfill the account; a
    // FAILED count query is transient unavailability - never "empty" (fail
    // open) and never terminal. The reservation is bound to the pending
    // record by Begin and released only on a CONFIRMED terminal outcome;
    // every failure path between here and Begin must release it explicitly
    // because no save was queued.
    switch (TryReserveCharacterSlot(accountId))
    {
        case living::SlotReservationOutcome::Reserved:
            break;
        case living::SlotReservationOutcome::DatabaseUnavailable:
            result.status = living::BotCreateStatus::TransientFailure;
            result.messages.push_back("Database unavailable while verifying account capacity; try again later");
            return result;
        case living::SlotReservationOutcome::AtCapacity:
        default:
            result.status = living::BotCreateStatus::TerminalFailure;
            result.messages.push_back("Account has max characters");
            return result;
    }

    uint8 skin = 0, face = 0, hairStyle = 0, hairColor = 0, facialHair = 0;

    bool const explicitName = !options.name.empty();
    std::string name = options.name;

    if (name.empty())
    {
        RandomPlayerbotFactory::NameRaceAndGender raceAndGender = RandomPlayerbotFactory::CombineRaceAndGender(gender, race);
        name = RandomPlayerbotFactory::CreateRandomBotName(raceAndGender);
    }

    // A name whose creation is still pending durability confirmation is not
    // reusable: the durable row may materialize at any moment.
    if (ai::botCreationFinalizer.core.HasPendingName(name))
    {
        ai::botCreationFinalizer.core.ReleaseAccountSlot(accountId); // pre-save failure
        result.status = explicitName
            ? living::BotCreateStatus::TerminalFailure
            : living::BotCreateStatus::RetryableFailure;
        result.messages.push_back("A creation for the name '" + name + "' is still pending confirmation");
        return result;
    }

    WorldSession* botSession = new WorldSession(accountId, NULL, SEC_PLAYER,
#ifdef MANGOSBOT_TWO
        2,
        0,
        LOCALE_enUS,
        "",
        0,
        0,
        false);
#endif
#ifdef MANGOSBOT_ONE
        2, 0, LOCALE_enUS, "", 0, 0, false);
#endif
#ifdef MANGOSBOT_ZERO
        0, LOCALE_enUS, "", 0);
#endif

        botSession->SetNoAnticheat();

        Player* newBot = new Player(botSession);
        if (!newBot->Create(sObjectMgr.GeneratePlayerLowGuid(), name, race, cls, gender, skin, face, hairStyle, hairColor, facialHair, 0))
        {
            delete botSession;
            delete newBot;
            ai::botCreationFinalizer.core.ReleaseAccountSlot(accountId); // pre-save failure
            // Only a randomly GENERATED name/appearance may roll differently on a
            // retry; an explicit name fails deterministically and retrying it
            // would just burn account capacity on the same failure.
            result.status = explicitName
                ? living::BotCreateStatus::TerminalFailure
                : living::BotCreateStatus::RetryableFailure;
            result.messages.push_back("Failed to create character");
            return result;
        }

        newBot->setCinematic(2);
        newBot->SetAtLoginFlag(AT_LOGIN_NONE);
        sObjectAccessor.AddObject(newBot);

        uint32 botGuid = newBot->GetGUIDLow();
        result.guid = newBot->GetObjectGuid();

        // Stages, in order: (1) apply the talent plan to the TRANSIENT player,
        // (2) derive the resulting roles, (3) verify the requested role,
        // (4) persist the character, (5) persist spec/creation metadata.
        // Nothing before stage 4 may write a DB row or cache entry - a rejected
        // creation must leave no state for a GUID that was never saved.
        ChangeTalentsAction::TalentSelectionResult talents;

        if (options.level > 1)
        {
            newBot->SetLevel(options.level);
            newBot->SetUInt32Value(PLAYER_XP, 0);
            newBot->InitStatsForLevel(true);
#ifdef MANGOSBOT_ZERO
            newBot->InitTaxiNodes();
#else
        newBot->InitTaxiNodesForLevel();
#endif
            newBot->InitTalentForLevel();
            newBot->InitPrimaryProfessions();
            newBot->learnDefaultSpells();

            std::ostringstream out;
            talents = ChangeTalentsAction::SelectTalents(newBot, &out, static_cast<BotRoles>(options.role));

            // Stage 3: verify the requested role BEFORE any persistence, from
            // the SELECTED SPEC's intended role mask - never from runtime
            // roles, which depend on transient combat auras (a fresh Feral
            // druid has no bear-form aura yet is a tank). The rejected
            // transient player and session are destroyed, and the only cache
            // touched so far (the empty event-cache entry created by the spec
            // lookup) is dropped, so the discarded GUID leaves no DB row or
            // cache state at all.
            if (options.requireRoleMatch && options.role)
            {
                // An explicit role is satisfiable only by an ACTUALLY APPLIED
                // talent path whose capability mask contains the request. No
                // path applied (none configured, or several merely listed with
                // auto-pick disabled) never passes - a blank-talent default
                // tab is not a role.
                if (!talents.applied || (static_cast<uint32>(talents.selectedRoles) & options.role) == 0)
                {
                    sObjectAccessor.RemoveObject(newBot);
                    delete newBot;
                    delete botSession;
                    sRandomPlayerbotMgr.ForgetEventCache(botGuid);
                    ai::botCreationFinalizer.core.ReleaseAccountSlot(accountId); // pre-save failure
                    result.guid = ObjectGuid();
                    result.status = living::BotCreateStatus::RetryableFailure;
                    result.messages.push_back("Talent selection did not produce the requested role");
                    return result;
                }
            }

            // Spec metadata is part of the required creation payload: a link
            // that cannot fit the schema must fail before the character is
            // persisted, exactly like every other creation event value.
            if (!living::EventValueFitsSchema("specLink", talents.specLink))
            {
                sObjectAccessor.RemoveObject(newBot);
                delete newBot;
                delete botSession;
                sRandomPlayerbotMgr.ForgetEventCache(botGuid);
                ai::botCreationFinalizer.core.ReleaseAccountSlot(accountId); // pre-save failure
                result.guid = ObjectGuid();
                result.status = living::BotCreateStatus::RetryableFailure;
                result.messages.push_back("Selected talent link exceeds the metadata schema limit");
                return result;
            }
        }
        else
            newBot->SetLevel(1);

        if (master)
        {
            newBot->SetMap(master->GetMap());
            newBot->SetPosition(master->GetPositionX(), master->GetPositionY(), master->GetPositionZ(), master->GetOrientation());
        }

        // Stage 3.4: the allocated GUID must be provably CLEAN before any
        // metadata is written. A GUID still owned by the deletion confirmer
        // (or unknown while the deletion scan has not succeeded) is refused
        // outright - its metadata would race the pending blanket clear. Any
        // lifecycle residue rows from an earlier failed cleanup are removed
        // with a CONFIRMED delete first, or the creation is refused: writing
        // fresh metadata over residue would let this character inherit an
        // older creation's gear/group/test/spec work.
        auto tearDownTransient = [&](living::BotCreateStatus status, char const* message)
        {
            sObjectAccessor.RemoveObject(newBot);
            delete newBot;
            delete botSession;
            sRandomPlayerbotMgr.ForgetEventCache(botGuid);
            ai::botCreationFinalizer.core.ReleaseAccountSlot(accountId); // pre-save failure
            result.guid = ObjectGuid();
            result.status = status;
            result.messages.push_back(message);
            return result;
        };

        if (IsDeletionPending(botGuid))
            return tearDownTransient(living::BotCreateStatus::TransientFailure,
                "Allocated character id is owned by a pending deletion (or deletion state is not yet known); "
                "creation refused and retried with pacing");

        {
            auto residue = CharacterDatabase.PQuery(
                "SELECT COUNT(*) FROM ai_playerbot_random_bots WHERE owner = 0 AND bot = '%u'", botGuid);
            if (!residue)
                return tearDownTransient(living::BotCreateStatus::TransientFailure,
                    "Lifecycle residue for the allocated character id could not be checked; creation refused");
            if (residue->Fetch()[0].GetUInt32() > 0
                && !CharacterDatabase.DirectPExecute(
                    "DELETE FROM ai_playerbot_random_bots WHERE owner = 0 AND bot = '%u'", botGuid))
                return tearDownTransient(living::BotCreateStatus::TransientFailure,
                    "Lifecycle residue for the allocated character id could not be removed; creation refused");
        }

        // Stage 3.5: the durable creation INTENT is written FIRST - it is the
        // restart-discoverable owner of everything that follows, so a failure
        // in any later metadata write can never leave ownerless residue (the
        // startup intent scan finds the intent, sees no character, and
        // discards with a confirmed delete). The intent records the IDENTITY
        // (name, account) this creation is bound to: recovery verifies the
        // present row against exactly these values, never against itself.
        // FAIL CLOSED: any unconfirmed write tears the transient character
        // down before anything was queued.
        bool const hasObligations = options.level > 1
            || (wantsGroupEvent && !groupTargetData.empty())
            || !options.testName.empty()
            || options.temporary;

        std::string const intentData = living::EncodeCreationIntent(name, accountId,
            options.level, options.autoAdd, hasObligations);
        bool metadataOk = sRandomPlayerbotMgr.SetValue(botGuid, "create pending",
            living::kCreationIntentPrePersistence, intentData);

        if (metadataOk && talents.evaluated)
            metadataOk = ai::ChangeTalentsAction::PersistTalentSpec(botGuid, talents);

        if (metadataOk && options.level > 1)
        {
            metadataOk = sRandomPlayerbotMgr.SetValue(botGuid, "create levelup", 1);
            metadataOk = sRandomPlayerbotMgr.SetValue(botGuid, "create gear", 1, options.gear) && metadataOk;
        }
        if (metadataOk && wantsGroupEvent && !groupTargetData.empty())
            metadataOk = sRandomPlayerbotMgr.SetValue(botGuid, "create group", 1, groupTargetData);
        if (metadataOk && !options.testName.empty())
            metadataOk = sRandomPlayerbotMgr.SetValue(botGuid, "test", 1, options.testName);
        if (metadataOk && options.temporary)
            metadataOk = sRandomPlayerbotMgr.SetValue(botGuid, "temporary", 1, name);

        if (!metadataOk)
        {
            // Confirmed cleanup, or the INTENT stays as the durable owner of
            // the residue: the startup/retry scan discards it once the
            // absent character proves nothing committed. Never an unchecked
            // blanket delete with released ownership.
            if (!CharacterDatabase.DirectPExecute(
                    "DELETE FROM ai_playerbot_random_bots WHERE owner = 0 AND bot = '%u'", botGuid))
                sLog.outError("CreateBot: metadata cleanup for guid %u could not be confirmed; "
                    "the creation intent remains as the durable owner until the intent scan discards it", botGuid);
            return tearDownTransient(living::BotCreateStatus::RetryableFailure,
                "Creation metadata could not be persisted; nothing was created");
        }

        // Stage 4: the character itself. SaveToDB commits through the async
        // transaction queue in every pinned core: returning from it confirms
        // ENQUEUEING only. Everything dependent on durability - Created
        // status, GUID exposure, freeAltBots - is deferred to the
        // asynchronous finalizer, which verifies the durable row first.
        newBot->SaveToDB();

        // Report the PLANNED composition (class from the character, role from
        // the CONCRETE applied talent build - never the capability mask, which
        // would let a DPS Feral pass as a tank, and never aura-dependent runtime
        // roles). For group planning this is a reservation, not a persistence
        // claim: the status below stays PendingPersistence.
        result.createdClass = newBot->getClass();
        result.createdRole = talents.evaluated
            ? static_cast<uint8>(talents.selectedRoles)
            : static_cast<uint8>(AiFactory::GetAppliedSpecRole(newBot));

        botSession->LogoutPlayer();
        sObjectAccessor.RemoveObject(newBot);
        delete newBot;
        delete botSession;

        // Stage 5: register the bounded pending record (GUID, account,
        // normalized name, deferred metadata and composition context) and let
        // the finalizer confirm the durable row before any dependent write -
        // from the pump, never from the SQL result callback. The record
        // adopts the account reservation taken above.
        living::CreationFinalizer::Record record;
        record.guid = botGuid;
        record.accountId = accountId;
        record.name = name;
        record.autoAdd = options.autoAdd;
        record.holdsAccountReservation = true;

        ai::BotCreationFinalizer::CreationPayload payload;
        payload.intentData = intentData;
        payload.hasObligations = hasObligations;

        result.creationToken = ai::botCreationFinalizer.Begin(std::move(record), std::move(payload));

        // The GUID is NOT exposed while durability is unknown; callers observe
        // the creation through the opaque token.
        result.guid = ObjectGuid();
        result.status = living::BotCreateStatus::PendingPersistence;
        result.messages.push_back("Bot creation queued: " + name + " (finalized once the character transaction is confirmed)");
        if (options.autoAdd)
            result.messages.push_back(name + " will come online after confirmation");
        else
            result.messages.push_back("After confirmation, use '.rndbot add " + name + "' to bring this bot online");

        return result;
}

std::list<std::string> PlayerbotHolder::HandleCreate(Player* master, const std::string param, AccountTypes security)
{
    return CreateBot(master, param).messages;
}

std::list<std::string> PlayerbotHolder::HandleGroup(Player* master, const std::string param, AccountTypes security)
{
    uint64 batchToken = 0;
    return HandleGroup(master, param, security, batchToken);
}

std::list<std::string> PlayerbotHolder::HandleGroup(Player* master, const std::string param, AccountTypes security, uint64& batchToken)
{
    std::list<std::string> messages;
    batchToken = 0;

    if (!master)
    {
        messages.push_back("group command requires a master (in-game)");
        return messages;
    }

    uint32 masterLevel = master->GetLevel();
    uint8 masterClass = master->getClass();
    Team team = master->GetTeam();
    BotRoles masterRole = AiFactory::GetPlayerRoles(master);
    uint32 groupSize = 5;
    uint32 currentGroupSize = 1;
    Group* group = master->GetGroup();
    if (group)
        currentGroupSize = group->GetMembersCount();

    // Group composition is role-quota driven, and roles are talent-derived:
    // below level 10 no member's role could be verified, so the command errors
    // out instead of persisting members with fictional roles (same rule as
    // CreateBot's role verification).
    if (masterLevel < 10)
    {
        messages.push_back("group requires a level 10+ master: member roles are talent-derived and cannot be verified below level 10");
        return messages;
    }

    // Group-level options are parsed ONCE into typed values. Composition fields
    // (class, role, race, faction, level, group target, name, test) cannot be
    // overridden: the old flow appended the raw user text AFTER the generated
    // "class=..." so a user parameter silently replaced the selected class while
    // the quotas were still charged for the selection.
    static std::set<std::string> const allowedGroupKeys = { "size", "gear", "login", "temporary" };

    std::map<std::string, std::string> groupArgs;
    std::string groupError;
    if (!living::TryParseKeyValueArgs(param, allowedGroupKeys, groupArgs, groupError))
    {
        messages.push_back(groupError + " (group composition fields such as class/role/race/faction/level/name are selected automatically and cannot be overridden)");
        return messages;
    }

    std::string gear = "default";
    bool autoAdd = true;
    bool temporary = false;

    if (auto it = groupArgs.find("size"); it != groupArgs.end())
    {
        // Validate against the core raid maximum BEFORE any narrowing or bot
        // mutation: "size=256" used to wrap a uint8 to 0 and "size=4294967295"
        // to 255, driving hundreds of unexpected creation attempts.
        uint32 sizeValue = 0;
        if (!living::TryParseUInt32InRange(it->second, 1, MAX_RAID_SIZE, sizeValue))
        {
            messages.push_back("Invalid group size: " + it->second + " (expected 1.." + std::to_string(MAX_RAID_SIZE) + ")");
            return messages;
        }
        groupSize = sizeValue;
    }

    if (auto it = groupArgs.find("gear"); it != groupArgs.end())
    {
        if (!living::IsSupportedGearValue(it->second))
        {
            messages.push_back("Unsupported gear value '" + it->second
                + "' (supported: default, empty, green/uncommon, blue/rare, purple/epic, upgrade, sync, best, partial)");
            return messages;
        }
        gear = it->second;
    }

    std::string boolError;
    if (auto it = groupArgs.find("login"); it != groupArgs.end())
    {
        if (!ParseExplicitBool("login", it->second, autoAdd, boolError))
        {
            messages.push_back(boolError);
            return messages;
        }
    }

    if (auto it = groupArgs.find("temporary"); it != groupArgs.end())
    {
        if (!ParseExplicitBool("temporary", it->second, temporary, boolError))
        {
            messages.push_back(boolError);
            return messages;
        }
    }

    // groupSize was validated against 1..MAX_RAID_SIZE above, so this narrowing
    // cannot alias.
    std::unordered_map<uint8, std::unordered_map<BotRoles, uint32>> allowedClassNr = LfgAction::AllowedClassRoleNr(master, static_cast<uint8>(groupSize));

    uint32 maxTries = 10*groupSize;

    // ONE consistent ownership/accounting rule for repeated requests, decided
    // by the shared planner: outstanding dependencies - planned, pending, or
    // finalized-but-unjoined members of ANY of this initiator's batches,
    // including completed retained ones - are credited as effective
    // membership, so a request whose options differ from theirs is refused
    // explicitly (the old flow checked only the ACTIVE batch and could
    // silently queue nothing under changed options), and a fresh batch
    // explicitly credits them in its completion baseline.
    bool const wantRandomAccounts = dynamic_cast<RandomPlayerbotMgr*>(this) != nullptr;
    auto const isJoined = [group](uint32 guid) { return group && group->IsMember(ObjectGuid(HIGHGUID_PLAYER, guid)); };
    uint32 const liveMembers = currentGroupSize;

    living::GroupBatchOptions requestedOptions;
    requestedOptions.gear = gear;
    requestedOptions.autoAdd = autoAdd;
    requestedOptions.temporary = temporary;
    requestedOptions.useRandomAccounts = wantRandomAccounts;

    living::GroupBatchRequestPlan const requestPlan = living::PlanGroupBatchRequest(
        ai::botCreationBatches, master->GetGUIDLow(), isJoined, liveMembers, groupSize, requestedOptions);

    if (requestPlan.action == living::GroupBatchRequestAction::RejectIncompatible)
    {
        messages.push_back("A group creation with different options still owns "
            + std::to_string(requestPlan.outstandingSlots)
            + " outstanding member slot(s); wait for those members to join or the batch to expire before requesting different options");
        return messages;
    }

    if (requestPlan.action == living::GroupBatchRequestAction::AlreadyCovered
        && requestPlan.outstandingSlots > 0)
    {
        // Queueing nothing here is SUCCESS, not an admission failure, so the
        // caller still gets a token IT OWNS - a fresh, empty tracking batch
        // recording the covered request under its own ledger. NEVER a
        // predecessor's token: the caller acknowledges its token when done,
        // and acknowledging a batch this request did not create would erase
        // it out from under its real owner (and judge this request against
        // the other request's size and failures).
        //
        // The tracking batch has no members, so it polls Complete immediately
        // and (effective baseline >= requested size) not undersized; it is
        // never returned as extendable, and its credited count feeds no
        // outstanding accounting. If a credited member ultimately never
        // joins, the group is simply short and the user re-runs the command.
        living::CreationBatchRegistry::Batch tracking;
        tracking.initiatorName = master->GetName();
        tracking.initiatorGuid = master->GetGUIDLow();
        tracking.desiredSize = groupSize;
        tracking.preexistingMembers = liveMembers;
        tracking.creditedMembers = requestPlan.outstandingSlots;
        tracking.memberGear = gear;
        tracking.memberAutoAdd = autoAdd;
        tracking.memberTemporary = temporary;
        tracking.useRandomAccounts = wantRandomAccounts;
        // The user is told synchronously below; the pump must not surface a
        // second, empty completion summary for this batch.
        tracking.completionReported = true;
        batchToken = ai::botCreationBatches.Begin(std::move(tracking));
        if (!batchToken)
            messages.push_back("Warning: creation batch registry is full; the covered request cannot be tracked");

        messages.push_back(std::to_string(requestPlan.outstandingSlots)
            + " outstanding member slot(s) from a previous group creation already cover the requested size; "
            "no additional members queued - completion is confirmed asynchronously");
        return messages;
    }

    // The credited slots also count toward effective membership, so this run
    // enqueues only the residual deficit; their planned classes/roles consume
    // the composition quotas below, so older allocations constrain this
    // selection. A slot stays owned until membership is verified in the live
    // group or its batch expires (the bounded terminal path for joins that
    // never happen).
    uint64 const existingToken = requestPlan.existingToken;
    uint32 const outstandingSlots = requestPlan.outstandingSlots;
    currentGroupSize += outstandingSlots;

    ai::botCreationBatches.ForEachOutstandingMember(master->GetGUIDLow(), isJoined,
        [&allowedClassNr](living::CreationBatchMember const& member)
        {
            if (!member.role)
                return; // unplanned slot: constrains size only, not quotas

            living::TryConsumeQuota(allowedClassNr[0][static_cast<BotRoles>(member.role)]);
            if (member.cls)
            {
                auto classQuota = allowedClassNr[member.cls].find(static_cast<BotRoles>(member.role));
                if (classQuota != allowedClassNr[member.cls].end())
                    living::TryConsumeQuota(classQuota->second);
            }
        });

    if (existingToken)
    {
        batchToken = existingToken;
        // The extension refreshes the batch's live-membership baseline from
        // THIS observation: unrelated members who joined since the batch began
        // no longer leave a stale baseline that completion reports as a false
        // undersize.
        ai::botCreationBatches.ExtendDesiredSize(existingToken, groupSize, liveMembers, isJoined);

        if (currentGroupSize >= groupSize)
        {
            // The requested size is already covered by live members plus the
            // batch's outstanding slots: coalesce, enqueue nothing more.
            messages.push_back("Group creation already in progress covers the requested size ("
                + std::to_string(outstandingSlots) + " outstanding member slot(s)); no additional members queued");
            return messages;
        }
    }

    living::GroupCreationLedger ledger;
    std::vector<living::CreationBatchMember> batchMembers;
    // A fresh batch's baseline credits the outstanding dependencies it
    // coalesced over: live members plus older batches' unjoined slots. The
    // credit is a plain count, so if a credited member ultimately never joins
    // the group simply completes undersized and the user re-runs the command.
    uint32 const preexistingMembers = requestPlan.effectiveMembers;
    uint32 continue_role = 0, continue_race = 0, continue_class = 0;
    bool stoppedOnTerminalFailure = false;
    bool stoppedOnTransientFailure = false;

    while (currentGroupSize < groupSize)
    {
        maxTries--;
        if (!maxTries)
            break;

        // BotRoles are bit flags (1/2/4): the old urand(TANK, DPS) sampled the
        // integer 3 (TANK|HEALER), which is no role and wasted the attempt.
        static BotRoles const groupRoles[3] = { BotRoles::BOT_ROLE_TANK, BotRoles::BOT_ROLE_HEALER, BotRoles::BOT_ROLE_DPS };
        BotRoles role = groupRoles[urand(0, 2)];

        if (allowedClassNr[0][role] == 0)
        {
            continue_role++;
            continue;
        }

        // The same joint team/role-constrained selector direct creation uses:
        // it can only produce tuples the master's faction can actually play, so
        // the old Classic paladin/shaman faction skips are unnecessary.
        uint8 race = 0;
        uint8 cls = 0;
        if (!RandomPlayerbotFactory::GetRandomTuple(team, role, 0, 0, race, cls))
        {
            // No tuple can ever satisfy this role for this faction: stop
            // drawing the role instead of burning attempts on it.
            allowedClassNr[0][role] = 0;
            continue_role++;
            continue;
        }

        if (allowedClassNr[cls].find(role) != allowedClassNr[cls].end() && allowedClassNr[cls][role] == 0)
        {
            continue_class++;
            continue;
        }

        // The typed options are constructed DIRECTLY with the selected
        // composition - no argument string is assembled and reparsed, so nothing
        // can override the selected class, and the selected role now reaches
        // talent/spec selection. requireRoleMatch makes creation verify the
        // produced spec BEFORE persistence: a bot that cannot fill the selected
        // role slot is torn down and reported retryable instead of persisted.
        // (Level-1 members have no spec; the verification applies to leveled
        // creations, which is every case with role quotas in play.)
        CreateBotOptions options;
        options.level = masterLevel;
        options.race = race;
        options.cls = cls;
        options.role = static_cast<uint8>(role);
        options.requireRoleMatch = true;
        options.groupWith = master->GetName();
        options.explicitGroup = true;
        options.gear = gear;
        options.autoAdd = autoAdd;
        options.temporary = temporary;

        // Success is decided by THIS attempt's typed result. A pending
        // creation (character transaction queued, durability confirmed
        // asynchronously) fills a composition slot for PLANNING - its
        // class/role were verified before the queue - but is never counted or
        // reported as a persisted member; the finalizer confirms or discards
        // it later.
        BotCreationResult result = CreateBot(master, options);
        bool const terminal = ledger.Record(result.status, result.createdClass);
        messages.splice(messages.end(), result.messages);

        // A pending member joins the run's completion batch: its terminal
        // result (confirmed, rolled back, quarantined) is propagated back to
        // this run instead of dying in a log line.
        if (result.status == living::BotCreateStatus::PendingPersistence && result.creationToken)
        {
            living::CreationBatchMember member;
            member.creationToken = result.creationToken;
            member.cls = result.createdClass;
            member.role = result.createdRole; // the VERIFIED role (== requested, enforced by requireRoleMatch)
            batchMembers.push_back(member);
        }

        if (living::GroupCreationLedger::CountsTowardComposition(result.status))
        {
            currentGroupSize++;

            // Quotas shrink only when a slot was filled (persisted or
            // pending-verified). Class is the planned class; the role slot is
            // the selected role, which requireRoleMatch verified against the
            // applied build's CONCRETE role (not the capability mask) before
            // SaveToDB was queued - so the drawn role equals the stored role.
            uint8 const actualClass = result.createdClass;

            living::TryConsumeQuota(allowedClassNr[0][role]);

            if (allowedClassNr[actualClass].find(role) != allowedClassNr[actualClass].end())
                living::TryConsumeQuota(allowedClassNr[actualClass][role]);
        }

        // A terminal failure (invalid arguments) cannot succeed on retry; a
        // TRANSIENT failure (database unavailable) must not hammer the DB
        // inside this call - the remaining deficit becomes paced planned
        // slots below instead of terminal shortfall.
        if (terminal)
        {
            stoppedOnTerminalFailure = result.status == living::BotCreateStatus::TerminalFailure;
            stoppedOnTransientFailure = result.status == living::BotCreateStatus::TransientFailure;
            break;
        }
    }

    // Register/extend the completion batch: the finalizer publishes each
    // member's terminal result to it, retryable failures are replaced in
    // place within the bounded budget, transient shortfalls are retried with
    // pacing, and the initiator/test queue is told the real outcome instead
    // of the group staying silently undersized.
    uint32 const transientShortfall = stoppedOnTransientFailure && groupSize > currentGroupSize
        ? groupSize - currentGroupSize : 0;

    if (existingToken)
    {
        // Extension: the members join the EXISTING batch's shared ledger.
        for (living::CreationBatchMember const& member : batchMembers)
            ai::botCreationBatches.AddPendingMember(existingToken, member);
        if (transientShortfall)
            ai::botCreationBatches.AddPlannedSlots(existingToken, transientShortfall, ai::CreationPumpTick());
        messages.push_back("Extended the in-progress group creation; members are confirmed asynchronously");
    }
    else if (!batchMembers.empty() || transientShortfall)
    {
        living::CreationBatchRegistry::Batch batch;
        batch.initiatorName = master->GetName();
        batch.initiatorGuid = master->GetGUIDLow();
        batch.desiredSize = groupSize;
        // The two halves of the baseline are stored SEPARATELY: a later
        // extension refreshes the live count from its own observation, and
        // must not discard the credited slots while doing so.
        batch.preexistingMembers = liveMembers;
        batch.creditedMembers = requestPlan.outstandingSlots;
        batch.members = batchMembers;
        // Bounded replacement: at most one replacement per requested slot.
        batch.replacementBudget = groupSize;
        batch.memberGear = gear;
        batch.memberAutoAdd = autoAdd;
        batch.memberTemporary = temporary;
        // Replacements must allocate exactly like this run did.
        batch.useRandomAccounts = wantRandomAccounts;
        // The initial run itself can fall short TERMINALLY (terminal stop,
        // maxTries exhaustion): record that shortfall NOW so batch completion
        // reports it - a partial group must never poll as an unqualified
        // success. A TRANSIENT shortfall becomes paced planned slots instead.
        if (!stoppedOnTransientFailure && currentGroupSize < groupSize)
        {
            std::ostringstream shortfall;
            // preexistingMembers already credits the outstanding slots, so the
            // residual this run owed is measured against it alone.
            shortfall << "initial run queued only " << (currentGroupSize - preexistingMembers)
                << " of " << (groupSize - preexistingMembers) << " requested members";
            if (stoppedOnTerminalFailure)
                shortfall << " (stopped on a terminal failure)";
            else if (maxTries == 0)
                shortfall << " (attempt budget exhausted)";
            batch.failures.push_back(shortfall.str());
        }
        batchToken = ai::botCreationBatches.Begin(std::move(batch));
        if (!batchToken)
            messages.push_back("Warning: creation batch registry is full; asynchronous member failures will be logged but not replaced");
        else if (transientShortfall)
        {
            ai::botCreationBatches.AddPlannedSlots(batchToken, transientShortfall, ai::CreationPumpTick());
            messages.push_back("Group creation queued; the database is transiently unavailable, remaining members will be created with paced retries");
        }
        else
            messages.push_back("Group creation queued; members are confirmed asynchronously and failures are replaced or reported");
    }

    std::ostringstream debugInfo;
    debugInfo << "DEBUG group: target=" << groupSize
        << ", created=" << ledger.created
        << ", pending confirmation=" << ledger.pendingPersistence;
    if (maxTries == 0)
        debugInfo << " (maxTries exhausted)";
    if (stoppedOnTerminalFailure)
        debugInfo << " (stopped on terminal failure)";
    debugInfo << ", continues: role=" << continue_role << ", race=" << continue_race << ", class=" << continue_class;
    debugInfo << ", classes: ";
    for (auto& kv : ledger.createdByClass)
        debugInfo << ChatHelper::formatClass(kv.first) << "=" << kv.second << ",";
    sLog.outString("%s", debugInfo.str().c_str());

    return messages;
}

#ifdef GenerateBotTests
std::list<std::string> PlayerbotHolder::HandleRunTest(Player* master, const std::string param, AccountTypes security)
{    
    std::list<std::string> messages;
    static constexpr size_t maxListLines = 200;

    if (param.empty())
    {
        messages.push_back("Usage: .rndbot runtest <testnamepart> [count]");
        messages.push_back("Available tests:");
        std::vector<std::string> availableTests = TestRegistry::GetAvailableTests();
        size_t shown = 0;
        for (const auto& test : availableTests)
        {
            if (shown >= maxListLines)
                break;
            messages.push_back("  " + test);
            ++shown;
        }

        if (availableTests.size() > shown)
        {
            std::ostringstream out;
            out << "... " << (availableTests.size() - shown) << " more tests not shown. Use '.rndbot runtest ?<namepart> [count]' to narrow results.";
            messages.push_back(out.str());
        }

        return messages;
    }

    std::string testNamePart = param;
    size_t maxTests = 0;

    size_t lastSpace = param.find_last_of(" \t");
    if (lastSpace != std::string::npos)
    {
        std::string maybeLimit = param.substr(lastSpace + 1);
        if (!maybeLimit.empty())
        {
            bool numericLimit = std::all_of(maybeLimit.begin(), maybeLimit.end(), ::isdigit);
            if (numericLimit)
            {
                try
                {
                    maxTests = std::stoul(maybeLimit);
                }
                catch (...)
                {
                    messages.push_back("Invalid count '" + maybeLimit + "'");
                    return messages;
                }

                if (!maxTests)
                {
                    messages.push_back("Count must be greater than 0");
                    return messages;
                }

                testNamePart = param.substr(0, lastSpace);
                size_t nameEnd = testNamePart.find_last_not_of(" \t");
                testNamePart = (nameEnd == std::string::npos) ? "" : testNamePart.substr(0, nameEnd + 1);
            }
        }
    }

    if (testNamePart.empty())
    {
        messages.push_back("Usage: .rndbot runtest <testnamepart> [count]");
        return messages;
    }

    bool listTests = false;

    if (testNamePart[0] == '?')
    {
        listTests = true;
        testNamePart = testNamePart.substr(2);
    }

    std::transform(testNamePart.begin(), testNamePart.end(), testNamePart.begin(), ::tolower);

    std::vector<std::string> matchingTests;
    std::vector<std::string> allTests = TestRegistry::GetAvailableTests();
    for (const auto& test : allTests)
    {
        std::string lowerTest = test;
        std::transform(lowerTest.begin(), lowerTest.end(), lowerTest.begin(), ::tolower);
        if (lowerTest.find(testNamePart) == 0 || testNamePart == "*")
        {
            matchingTests.push_back(test);
        }
    }

    if (maxTests && matchingTests.size() > maxTests)
    {
        std::shuffle(matchingTests.begin(), matchingTests.end(), *GetRandomGenerator());
        matchingTests.resize(maxTests);
    }

    if (matchingTests.empty())
    {
        messages.push_back("No tests matching '" + param + "' found");
        return messages;
    }

    if (listTests)
    {
        messages.push_back("Tests matching '" + param + "':");
        size_t shown = 0;
        for (const auto& test : matchingTests)
        {
            if (shown >= maxListLines)
                break;
            messages.push_back("  " + test);
            ++shown;
        }

        if (matchingTests.size() > shown)
        {
            std::ostringstream out;
            out << "... " << (matchingTests.size() - shown) << " more tests not shown. Add [count] to limit, e.g. '.rndbot runtest " << testNamePart << " 20'.";
            messages.push_back(out.str());
        }

        return messages;
    }

    {
        std::lock_guard<std::mutex> lock(testResultsMutex);
        for (const auto& test : matchingTests)
        {
            PendingTest pt;
            pt.testName = test;
            pt.result = "";
            pt.pending = false;
            pt.completed = false;
            pt.expectedBotSpawnCount = TestRegistry::ExpectedBotSpawnCount(test);
            pt.retry = 0;
            pendingTests.push_back(pt);
        }
    }

    std::ostringstream out;
    out << "Queued " << matchingTests.size() << " test(s): ";
    for (size_t i = 0; i < matchingTests.size() && i < 3; i++)
        out << matchingTests[i] << (i < matchingTests.size() - 1 && i < 2 ? ", " : "");
    if (matchingTests.size() > 3)
        out << "...";
    messages.push_back(out.str());

    return messages;
}

void PlayerbotHolder::UpdatePendingTests(uint32 elapsed)
{
    std::lock_guard<std::mutex> lock(testResultsMutex);

    for (auto& pt : pendingTests)
    {
        if (pt.pending)
            continue;

        if (pt.completed)
            continue;

        // Paced transient backoff: during a database outage the runner skips
        // whole update passes instead of issuing a synchronous capacity count
        // (or creation attempt) every tick.
        if (pt.transientBackoff)
        {
            --pt.transientBackoff;
            continue;
        }

        // Poll an outstanding asynchronous creation to a REAL terminal result
        // before anything else: enqueueing was never success. A retryable
        // failure re-attempts within a bounded budget; terminal/quarantined
        // failure fails the test with the real reason instead of leaving it
        // pending forever.
        if (pt.creationToken)
        {
            living::CreationPollResult poll = PollBotCreation(pt.creationToken, true);
            switch (poll.status)
            {
                case living::CreationPollStatus::Pending:
                    continue;
                case living::CreationPollStatus::Created:
                    pt.creationToken = 0;
                    // The finalized bot carries the test event and deposits
                    // its result after login.
                    pt.pending = true;
                    continue;
                case living::CreationPollStatus::FailedRetryable:
                    pt.creationToken = 0;
                    if (++pt.creationRetries >= 3)
                    {
                        pt.result = "IMPOSSIBLE: bot creation kept failing retryably (" + poll.message + ")";
                        pt.completed = true;
                    }
                    continue; // else fall through to a fresh attempt next update
                default: // FailedTerminal, Quarantined, Unknown
                    pt.creationToken = 0;
                    pt.result = "IMPOSSIBLE: bot creation failed (" + poll.message + ")";
                    pt.completed = true;
                    continue;
            }
        }

        if (!TestRegistry::HasTest(pt.testName))
        {
            pt.result = "Test not found";
            pt.completed = true;
            continue;
        }

        if (dynamic_cast<PlayerbotMgr*>(this))
        {
            uint32 accountId = sObjectMgr.GetPlayerAccountIdByGUID((dynamic_cast<PlayerbotMgr*>(this))->GetMaster()->GetObjectGuid());
                if (accountId == 0) continue;

            // Effective occupancy: durable characters plus pending/quarantined
            // creation reservations - several queued tests must not all admit
            // against the same stale durable count. An UNKNOWN count (failed
            // query) is a TRANSIENT deferral with the same bounded paced
            // schedule as transient creation - never a per-tick synchronous
            // retry, never an assumed capacity.
            uint32 effectiveCount = 0;
            if (!TryGetEffectiveCharacterCount(accountId, effectiveCount))
            {
                if (++pt.transientRetries >= 60)
                {
                    pt.result = "IMPOSSIBLE: database unavailable for the capacity precheck (deferred attempts exhausted)";
                    pt.completed = true;
                }
                else
                    pt.transientBackoff = 5;
                continue;
            }

            if (effectiveCount >= MaxCharsPerAccount())
                continue;
        }

        static constexpr uint32 maxActiveTestBots = 50;
        uint32 activeTestBots = 0;
        for (const auto& test : pendingTests)
        {
            // A test whose creation is still in flight occupies its bots too.
            if ((test.pending || test.creationToken) && !test.completed)
                activeTestBots += std::max<uint32>(1, test.expectedBotSpawnCount);
        }

        uint32 newTestBotCount = std::max<uint32>(1, pt.expectedBotSpawnCount);
        if (activeTestBots + newTestBotCount >= maxActiveTestBots)
            continue;

        std::string createParams = TestRegistry::GetBotCreationRequirement(pt.testName);

        createParams += " login=0 temporary=1 test=" + pt.testName;

        BotCreationResult createResult = CreateBot(nullptr, createParams);
        switch (createResult.status)
        {
            case living::BotCreateStatus::PendingPersistence:
                pt.creationToken = createResult.creationToken;
                break;
            case living::BotCreateStatus::Created:
                pt.pending = true; // defensive: creation is normally asynchronous
                break;
            case living::BotCreateStatus::RetryableFailure:
                if (++pt.creationRetries >= 3)
                {
                    pt.result = "IMPOSSIBLE: bot creation kept failing ("
                        + (createResult.messages.empty() ? std::string("unknown") : createResult.messages.front()) + ")";
                    pt.completed = true;
                }
                break;
            case living::BotCreateStatus::TransientFailure:
                // Transient database unavailability: defer with the bounded
                // PACED schedule (separate from the creation retry budget)
                // instead of failing the test or hammering the DB every tick.
                if (++pt.transientRetries >= 60)
                {
                    pt.result = "IMPOSSIBLE: database unavailable for bot creation (deferred attempts exhausted)";
                    pt.completed = true;
                }
                else
                    pt.transientBackoff = 5;
                break;
            case living::BotCreateStatus::TerminalFailure:
            default:
                // An immediate parse/admission failure fails the test now.
                pt.result = "IMPOSSIBLE: bot creation rejected ("
                    + (createResult.messages.empty() ? std::string("unknown") : createResult.messages.front()) + ")";
                pt.completed = true;
                break;
        }
    }
}

void PlayerbotHolder::DepositTestResult(const std::string& testName, const std::string& result)
{
    std::lock_guard<std::mutex> lock(testResultsMutex);

    for (auto& pt : pendingTests)
    {
        if (!pt.pending)
            continue;

        if (pt.testName == testName && !pt.completed)
        {
            pt.pending = false;
            if (result == "ABORT") //Failed this time but might work next time.
            {
                pt.result = result;
                pt.retry++;
                break;
            }

            pt.result = result;
            pt.completed = true;
            testResults.push_back(pt);
            break;
        }
    }
}
#endif

AccountSelectOutcome PlayerbotHolder::GetOrCreateAccount(Player* master, uint32& accountId, std::string& error)
{
    if (!master)
    {
        // For console/RA usage without master - delegate to derived class
        error = "GetOrCreateAccount requires master or override in derived class";
        return AccountSelectOutcome::AccountCreationFailed;
    }

    accountId = master->GetSession()->GetAccountId();
    return AccountSelectOutcome::Found;
}

void PlayerbotHolder::OnBotDeleted(uint32 botGuid, uint32 accountId)
{
}

bool PlayerbotHolder::DeleteBot(ObjectGuid guid, bool allowInstant)
{
    // ONE typed identity preflight: COUNT + name + account by guid. The
    // legacy GetPlayerNameByGUID/GetPlayerAccountIdByGUID pair conflated
    // "missing row" with "query failed" and accepted account 0, so a
    // destructive deletion could be queued for an unknown identity with
    // broken realm/account bookkeeping. Unknown or incomplete identity
    // writes NO intent and queues NO deletion (fail closed).
    std::optional<uint64> identityCount;
    std::string expectedName;
    uint32 botAccount = 0;
    if (auto preflight = CharacterDatabase.PQuery(
            "SELECT COUNT(*), MIN(name), MIN(account) FROM characters WHERE guid = '%u'", guid.GetCounter()))
    {
        Field* fields = preflight->Fetch();
        identityCount = fields[0].GetUInt32();
        if (*identityCount > 0)
        {
            expectedName = fields[1].GetCppString();
            botAccount = fields[2].GetUInt32();
        }
    }

    switch (living::ClassifyDeletionPreflight(identityCount, expectedName, botAccount))
    {
        case living::DeletionPreflight::Unknown:
            sLog.outError("DeleteBot: identity for guid %u could not be verified (query failed or row "
                "incomplete); NOT queueing the deletion (fail closed) - retry when the database recovers",
                guid.GetCounter());
            return false;
        case living::DeletionPreflight::ConfirmedAbsent:
            // Nothing destructive to queue: adopt for metadata cleanup only
            // (the readback confirms the absence and clears the event rows).
            // No durable intent is written - no destructive work is pending.
            sRandomPlayerbotMgr.CancelPendingRelocation(guid.GetCounter(), living::RelocationCancelMode::Force);
            AdoptCharacterDeletion(guid.GetCounter(), 0, std::string(), false);
            return true;
        case living::DeletionPreflight::VerifiedPresent:
            break; // proceed with the verified (name, account) tuple below
    }

    // Persist the deletion INTENT - the VERIFIED identity name plus ORIGINAL
    // account (for empty-account cleanup when the row is absent at recovery) -
    // BEFORE anything destructive. FAIL CLOSED: an unconfirmed intent queues
    // NO deletion at all; the caller may retry.
    if (!sRandomPlayerbotMgr.SetValue(guid.GetCounter(), "delete", 1,
            living::EncodeDeletionIntent(expectedName, botAccount)))
    {
        sLog.outError("DeleteBot: the deletion intent for guid %u could not be persisted; "
            "NOT queueing the deletion (fail closed) - retry when the database recovers", guid.GetCounter());
        return false;
    }

    // Force-cancel any tracked relocation BY GUID before the deletion: an
    // OFFLINE deletion has no live Player, so the logout path below never runs
    // and a retained Finalizing record (with its destination density
    // reservation) would outlive the character. The tracker ignores stale
    // verification callbacks by relocation token, so a cancelled record cannot
    // be resurrected or mutated by an in-flight result.
    sRandomPlayerbotMgr.CancelPendingRelocation(guid.GetCounter(), living::RelocationCancelMode::Force);

    if (Player* player = sObjectMgr.GetPlayer(guid, true))
    {
        //Attempt instant logout.
        player->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING);
        LogoutPlayerBot(guid, allowInstant, true);
    }

    Player::DeleteFromDB(guid, botAccount, true, true);

    // DeleteFromDB only QUEUED the transaction. Adopt the deletion into the
    // durable confirmer: login eligibility (freeAltBots) is revoked NOW, and
    // event rows/markers are cleared - and account cleanup runs - only after
    // the execution-ordered readback confirms the character row absent (or the
    // guid is proven reused by a new identity). Still-present and failed
    // readbacks retry bounded; a readback whose callback never arrives is
    // bounded by a pump watchdog. Every exhaustion path fails closed - rows
    // kept, login still blocked, a restart's scan retries.
    AdoptCharacterDeletion(guid.GetCounter(), botAccount, expectedName, true);

    OnBotDeleted(guid, botAccount);

    return true;
}

std::string PlayerbotHolder::HandleBotDelete(Player* bot, Player* master, const std::string param)
{
    ObjectGuid guid;
    if (!bot)
    {
        uint64 guidRaw = 0;
        if (!living::TryParseUInt64(param, guidRaw))
            return "Add: Error parsing " + param;

        guid = ObjectGuid(guidRaw);
    }
    else
    {
        guid = bot->GetObjectGuid();
    }

    uint32 masterAccountId = master ? master->GetSession()->GetAccountId() : 0;
    PlayerbotMgr* mgr = master ? master->GetPlayerbotMgr() : nullptr;
    
    uint32 botAccount = sObjectMgr.GetPlayerAccountIdByGUID(guid);
    bool isRandomAccount = sPlayerbotAIConfig.IsInRandomAccountList(botAccount);

    if (!isRandomAccount && masterAccountId != botAccount)
        return "Not your bot";

    if (isRandomAccount && mgr == this)
        return "Not your bot";

    if (!DeleteBot(guid))
        return "Deletion refused: the durable deletion intent could not be persisted (fail closed); try again";

    return "ok";
}

std::string PlayerbotHolder::HandleBotGear(Player* bot, Player* master, const std::string param)
{
    if (param.empty())
    {
        PlayerbotFactory factory(bot, bot->GetLevel());
        factory.EquipGear();
        return "random gear equipped";
    }
    if (param == "green" || param == "uncommon")
    {
        PlayerbotFactory factory(bot, bot->GetLevel(), ITEM_QUALITY_UNCOMMON);
        factory.EquipGear();
        return "random green gear equipped";
    }
    if (param == "blue" || param == "rare")
    {
        PlayerbotFactory factory(bot, bot->GetLevel(), ITEM_QUALITY_RARE);
        factory.EquipGear();
        return "random blue gear equipped";
    }
    if (param == "purple" || param == "epic")
    {
        PlayerbotFactory factory(bot, bot->GetLevel(), ITEM_QUALITY_EPIC);
        factory.EquipGear();
        return "random epic gear equipped";
    }
    if (param == "upgrade")
    {
        PlayerbotFactory factory(bot, master ? master->GetLevel() : bot->GetLevel(), ITEM_QUALITY_NORMAL);
        factory.UpgradeGear(false);
        return "gear upgraded";
    }
    if (param == "sync")
    {
        PlayerbotFactory factory(bot, master ? master->GetLevel() : bot->GetLevel(), ITEM_QUALITY_NORMAL);
        factory.UpgradeGear(true);
        return "gear upgraded";
    }
    if (param == "best")
    {
        PlayerbotFactory factory(bot, bot->GetLevel());
        factory.EquipGearBest();
        return "random best gear equipped";
    }
    if (param == "partial")
    {
        PlayerbotFactory factory(bot, bot->GetLevel());
        factory.EquipGearPartialUpgrade();
        return "random gear upgraded to some slots";
    }

    return "unknown gear command";
}

std::string PlayerbotHolder::HandleBotTrainLearn(Player* bot, Player* master, const std::string param)
{
#ifndef MANGOSBOT_ONE
    bot->learnClassLevelSpells();
#endif
    return "class level spells learned";
}

std::string PlayerbotHolder::HandleBotFoodDrink(Player* bot, Player* master, const std::string param)
{
    uint32 level = master ? master->GetLevel() : bot->GetLevel();
    PlayerbotFactory factory(bot, level, ITEM_QUALITY_NORMAL);
    factory.AddFood();
    return "food added";
}

std::string PlayerbotHolder::HandleBotPotions(Player* bot, Player* master, const std::string param)
{
    uint32 level = master ? master->GetLevel() : bot->GetLevel();
    PlayerbotFactory factory(bot, level, ITEM_QUALITY_NORMAL);
    factory.AddPotions();
    return "potions added";
}

std::string PlayerbotHolder::HandleBotConsumes(Player* bot, Player* master, const std::string param)
{
    uint32 level = master ? master->GetLevel() : bot->GetLevel();
    PlayerbotFactory factory(bot, level, ITEM_QUALITY_NORMAL);
    factory.AddConsumes();
    return "consumables added";
}

std::string PlayerbotHolder::HandleBotReagents(Player* bot, Player* master, const std::string param)
{
    uint32 level = master ? master->GetLevel() : bot->GetLevel();
    PlayerbotFactory factory(bot, level, ITEM_QUALITY_NORMAL);
    factory.AddReagents();
    return "reagents added";
}

std::string PlayerbotHolder::HandleBotPrepare(Player* bot, Player* master, const std::string param)
{
    uint32 level = master ? master->GetLevel() : bot->GetLevel();
    PlayerbotFactory factory(bot, level, ITEM_QUALITY_NORMAL);
    factory.Refresh();
    return "consumes/regs added";
}

std::string PlayerbotHolder::HandleBotInit(Player* bot, Player* master, const std::string param)
{
    uint32 level = master ? master->GetLevel() : bot->GetLevel();

    if (param.empty())
    {
        PlayerbotFactory factory(bot, level, ITEM_QUALITY_NORMAL);
        factory.Randomize(true, false);
    }
    else if (param == "white" || param == "common")
    {
        PlayerbotFactory factory(bot, level, ITEM_QUALITY_NORMAL);
        factory.Randomize(false, false);
    }
    else if (param == "green" || param == "uncommon")
    {
        PlayerbotFactory factory(bot, level, ITEM_QUALITY_UNCOMMON);
        factory.Randomize(false, false);
    }
    else if (param == "blue" || param == "rare")
    {
        PlayerbotFactory factory(bot, level, ITEM_QUALITY_RARE);
        factory.Randomize(false, false);
    }
    else if (param == "epic" || param == "purple")
    {
        PlayerbotFactory factory(bot, level, ITEM_QUALITY_EPIC);
        factory.Randomize(false, false);
    }
    else if (param == "legendary" || param == "yellow")
    {
        PlayerbotFactory factory(bot, level, ITEM_QUALITY_LEGENDARY);
        factory.Randomize(false, false);
    }
    else if (param == "sync")
    {
        PlayerbotFactory factory(bot, level, ITEM_QUALITY_LEGENDARY);
        factory.Randomize(false, true);
    }

    return "ok";
}

std::string PlayerbotHolder::HandleBotEnchants(Player* bot, Player* master, const std::string param)
{
    PlayerbotFactory factory(bot, bot->GetLevel(), ITEM_QUALITY_LEGENDARY);
    factory.EnchantEquipment();
    return "ok";
}

std::string PlayerbotHolder::HandleBotAmmo(Player* bot, Player* master, const std::string param)
{
    PlayerbotFactory factory(bot, bot->GetLevel(), ITEM_QUALITY_LEGENDARY);
    factory.InitAmmo();
    return "ok";
}

std::string PlayerbotHolder::HandleBotPet(Player* bot, Player* master, const std::string param)
{
    PlayerbotFactory factory(bot, bot->GetLevel(), ITEM_QUALITY_LEGENDARY);
    factory.InitPet();
    factory.InitPetSpells();
    return "ok";
}

std::string PlayerbotHolder::HandleBotLevelUp(Player* bot, Player* master, const std::string param)
{
    PlayerbotFactory factory(bot, bot->GetLevel());
    factory.Randomize(true, false);
    return "ok";
}

std::string PlayerbotHolder::HandleBotRefresh(Player* bot, Player* master, const std::string param)
{
    PlayerbotFactory factory(bot, bot->GetLevel());
    factory.Refresh();
    return "ok";
}

std::string PlayerbotHolder::HandleBotRandom(Player* bot, Player* master, const std::string param)
{
    sRandomPlayerbotMgr.Randomize(bot);
    return "ok";
}

std::string PlayerbotHolder::GetCommandTexts(const std::string& command)
{
    auto texts = GetCommandTexts();
    auto it = texts.find(command);
    if (it != texts.end())
        return it->second;
    return "";
}

std::unordered_map<std::string, std::string> PlayerbotHolder::GetCommandTexts()
{
    return std::unordered_map<std::string, std::string>
    {
        // Holder commands (used with .(rnd)bot)
        {"list", "List all active player bots.\nUsage: .(rnd)bot list"},
        {"help", "Show help for commands.\nUsage: .(rnd)bot help <command>"},
        {"reload", "Reload the playerbot config (GM only).\nUsage: .(rnd)bot reload"},
        {"tweak", "Adjust the tweak value for testing (GM only).\nUsage: .(rnd)bot tweak"},
        {"self", "Enable self-bot mode for a player.\nUsage: .(rnd)bot self <playername>"},
        {"group", "Create 4 bots with complementary classes at master's level.\nUsage: .(rnd)bot group"},
        {"create", "Create a new bot character.\nUsage: .(rnd)bot create level=<n> class=<class> race=<race>"},
        {"spoof", "Spoof as another bot for command routing.\nUsage: .(rnd)bot spoof <botname>"},
        {"runtest", "Run bot tests.\nUsage: .rndbot runtest <testnamepart> [count]"},
        
        // Bot commands (used with .(rnd)bot <bot> ...)
        {"add", "Add a bot to the player's group.\nUsage: .(rnd)bot add <playername>"},
        {"login", "Add a bot to the player's group.\nUsage: .(rnd)bot login <playername>"},
        {"remove", "Remove a bot from the player's group.\nUsage: .(rnd)bot remove <botname>"},
        {"logout", "Remove a bot from the player's group.\nUsage: .(rnd)bot logout <botname>"},
        {"rm", "Remove a bot from the player's group.\nUsage: .(rnd)bot rm <botname>"},
        {"delete", "Delete a bot character.\nUsage: .(rnd)bot delete <botname>"},
        
        {"gear", "Equip best gear on bot.\nUsage: .(rnd)bot gear <bot> "},
        {"equip", "Equip best gear on bot.\nUsage: .(rnd)bot equip  <bot> "},
        
        {"train", "Train bot spells at trainer.\nUsage: .(rnd)bot train <bot> "},
        {"learn", "Train bot spells at trainer.\nUsage: .(rnd)bot learn <bot> "},
        
        {"food", "Buy food/drink for bot.\nUsage: .(rnd)bot food <bot> "},
        {"drink", "Buy food/drink for bot.\nUsage: .(rnd)bot drink <bot> "},
        
        {"potions", "Buy potions for bot.\nUsage: .(rnd)bot potions <bot> "},
        {"pots", "Buy potions for bot.\nUsage: .(rnd)bot pots <bot> "},
        
        {"consumes", "Buy all consumables for bot.\nUsage: .(rnd)bot consumes <bot> "},
        {"consumables", "Buy all consumables for bot.\nUsage: .(rnd)bot consumables <bot> "},
        
        {"regs", "Buy reagents for bot.\nUsage: .(rnd)bot regs <bot> "},
        {"reg", "Buy reagents for bot.\nUsage: .(rnd)bot reg <bot> "},
        {"reagents", "Buy reagents for bot.\nUsage: .(rnd)bot reagents  <bot> "},
        
        {"prepare", "Prepare bot (gear, food, pots, etc).\nUsage: .(rnd)bot prepare <bot> "},
        {"prep", "Prepare bot (gear, food, pots, etc).\nUsage: .(rnd)bot prep <bot>"},
        {"refresh", "Refresh bot gear and items.\nUsage: .(rnd)bot refresh <bot> "},
        
        {"init", "Initialize bot with default actions.\nUsage: .(rnd)bot init <bot> "},
        
        {"enchants", "Apply enchants to bot's gear.\nUsage: .(rnd)bot enchants <bot> "},
        
        {"ammo", "Buy ammo for bot.\nUsage: .(rnd)bot ammo <bot> "},
        
        {"pet", "Summon/dismiss pet for bot.\nUsage: .(rnd)bot pet <bot> "},
        
        {"levelup", "Level up bot.\nUsage: .(rnd)bot levelup <bot>"},
        {"level", "Level up bot.\nUsage: .(rnd)bot level <bot>"},
        
        {"random", "Randomize bot appearance and gear.\nUsage: .(rnd)bot random <bot>"},
        
        {"always", "Enable offline AI for a player.\nUsage: .(rnd)bot always <playername>"},
        
        {"debug", "Run debug commands on the bot (GM only).\nUsage: .(rnd)bot debug <bot> <command>"},
        
        {"c", "Execute a chat command on the bot.\nUsage: .(rnd)bot c <bot> <command>"},
        
        {"w", "Send a whisper.\nUsage: .(rnd)bot w <bot> <message> (while spoofing as sender)\nUsage: .(rnd)bot <sender> <reciever> "},
        
        {"p", "Send a party message as the bot.\nUsage: .(rnd)bot p <message> (while spoofing as sender)\n .(rnd)bot p <botname> <message>\nNote: No message = party info.\nExample: .rndbot p Dunpriest (shows party info)"},
        
        {"g", "Send a guild message as the bot.\nUsage: .(rnd)bot g <message> (while spoofing as sender)\n .(rnd)bot g <botname> <message>\nNote: No message = guild info.\nExample: .rndbot g Dunpriest (shows guild info)"},
        
        {"r", "Send a raid message as the bot.\nUsage: .(rnd)bot r <message> (while spoofing as sender)\n .(rnd)bot r <botname> <message>"},
        
        {"rl", "Transfer raid leadership.\nUsage: .(rnd)bot rl <message> (while spoofing as sender)\n .(rnd)bot rl <botname>"},
        
        {"do", "Execute a bot action (sync, immediate response).\nUsage: .(rnd)bot do <bot> <action>\nExample: .(rnd)bot do <bot> stats, where, quests, who"},
        
        {"cmd", "Execute a bot action (async, queued).\nUsage: .(rnd)bot cmd <bot> do <action>\nNote: Use with record to capture output."},
        
        {"record", "Enable message recording for async commands.\nUsage: .(rnd)bot record <bot> enable\nUsage: .(rnd)bot record <bot> disable"},
        
        {"read", "Get recorded async command output.\nUsage: .(rnd)bot read <bot>"},
        
        {"clear", "Clear recorded messages without retrieving.\nUsage: .(rnd)bot clear <bot>"},
        
        {"spoof", "Spoof as another bot for command routing.\nUsage: .(rnd)bot spoof <botname>\nUsage: .(rnd)bot spoof (to clear)"}
    };
}

std::list<std::string> PlayerbotHolder::HandleSpoof(Player* master, const std::string param, AccountTypes security)
{
    std::list<std::string> messages;
    
    if (param.empty())
    {
        // Clear the spoof
        if (m_spoofGuid)
        {
            std::string playerName;
            if (sObjectMgr.GetPlayerNameByGUID(m_spoofGuid, playerName))
            {
                messages.push_back("Spoof cleared. Was spoofing: " + playerName);
            }
            else
            {
                messages.push_back("Spoof cleared.");
            }
            m_spoofGuid = ObjectGuid();
        }
        else
        {
            messages.push_back("Spoof is not set.");
        }
        return messages;
    }
    
    // Look up player by name
    ObjectGuid guid = sObjectMgr.GetPlayerGuidByName(param);
    if (!guid)
    {
        messages.push_back("Player '" + param + "' not found.");
        return messages;
    }
    
    // Get the player to verify they exist
    Player* player = sObjectMgr.GetPlayer(guid, false);
    if (!player)
    {
        messages.push_back("Player '" + param + "' found but is not online.");
        return messages;
    }
    
    std::string playerName;
    sObjectMgr.GetPlayerNameByGUID(guid, playerName);
    m_spoofGuid = guid;
    
    messages.push_back("Spoof set to: " + playerName + " (" + std::to_string(guid.GetCounter()) + ")");
    return messages;
}
