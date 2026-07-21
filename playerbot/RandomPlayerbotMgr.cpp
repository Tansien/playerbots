#include "Config/Config.h"

#include "playerbot/playerbot.h"
#include "playerbot/living/util/LivingBotCreation.h"
#include "playerbot/living/util/LivingCommandSplit.h"
#include "playerbot/living/util/LivingEventSchema.h"
#include "playerbot/living/util/LivingStrategyCommand.h"
#include "playerbot/living/util/LivingNumericParse.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "Maps/MapManager.h"
#include "playerbot/PlayerbotFactory.h"
#include "strategy/values/LastMovementValue.h"
#include "Accounts/AccountMgr.h"
#include "Globals/ObjectMgr.h"
#include "Database/DatabaseEnv.h"
#include "Database/DatabaseImpl.h"
#include "PlayerbotAI.h"
#include "Entities/Player.h"
#include "playerbot/AiFactory.h"
#include "PlayerbotCommandServer.h"
#include "MemoryMonitor.h"

#include "Grids/GridNotifiers.h"
#include "Grids/GridNotifiersImpl.h"
#include "Grids/CellImpl.h"
#include "FleeManager.h"
#include "playerbot/ServerFacade.h"

#include "BattleGround/BattleGround.h"
#include "BattleGround/BattleGroundMgr.h"
#include "Chat/ChannelMgr.h"
#include "Guilds/GuildMgr.h"
#include "World/WorldState.h"
#include "PlayerbotLoginMgr.h"
#include "Entities/Transports.h"

#ifndef MANGOSBOT_ZERO
#ifdef CMANGOS
#include "Arena/ArenaTeam.h"
#endif
#ifdef MANGOS
#include "ArenaTeam.h"
#endif
#endif

#include "playerbot/TravelMgr.h"
#include <iomanip>
#include <float.h>

#if PLATFORM == PLATFORM_WINDOWS
#include "windows.h"
#include "psapi.h"
#endif

using namespace ai;
using namespace MaNGOS;

INSTANTIATE_SINGLETON_1(RandomPlayerbotMgr);

#ifdef CMANGOS
#include <boost/thread/thread.hpp>
#endif

#ifdef MANGOS
class PrintStatsThread: public ACE_Task <ACE_MT_SYNCH>
{
public:
    int svc(void) { sRandomPlayerbotMgr.PrintStats(); return 0; }
};
#endif
#ifdef CMANGOS
void PrintStatsThread(uint32 requesterGuid)
{
    sRandomPlayerbotMgr.PrintStats(requesterGuid);
}
#endif

void activatePrintStatsThread(uint32 requesterGuid)
{
#ifdef MANGOS
    PrintStatsThread *thread = new PrintStatsThread();
    thread->activate();
#endif
#ifdef CMANGOS
    boost::thread t(PrintStatsThread, requesterGuid);
    t.detach();
#endif
}

#ifdef MANGOS
class CheckBgQueueThread : public ACE_Task <ACE_MT_SYNCH>
{
public:
    int svc(void) { sRandomPlayerbotMgr.CheckBgQueue(); return 0; }
};
#endif
#ifdef CMANGOS
void CheckBgQueueThread()
{
    sRandomPlayerbotMgr.CheckBgQueue();
}
#endif

void activateCheckBgQueueThread()
{
#ifdef MANGOS
    CheckBgQueueThread *thread = new CheckBgQueueThread();
    thread->activate();
#endif
#ifdef CMANGOS
    boost::thread t(CheckBgQueueThread);
    t.detach();
#endif
}

#ifdef MANGOS
class CheckLfgQueueThread : public ACE_Task <ACE_MT_SYNCH>
{
public:
    int svc(void) { sRandomPlayerbotMgr.CheckLfgQueue(); return 0; }
};
#endif
#ifdef CMANGOS
void CheckLfgQueueThread()
{
    sRandomPlayerbotMgr.CheckLfgQueue();
}
#endif

void activateCheckLfgQueueThread()
{
#ifdef MANGOS
    CheckLfgQueueThread *thread = new CheckLfgQueueThread();
    thread->activate();
#endif
#ifdef CMANGOS
    boost::thread t(CheckLfgQueueThread);
    t.detach();
#endif
}

#ifdef MANGOS
class CheckPlayersThread : public ACE_Task <ACE_MT_SYNCH>
{
public:
    int svc(void) { sRandomPlayerbotMgr.CheckPlayers(); return 0; }
};
#endif
#ifdef CMANGOS
void CheckPlayersThread()
{
    sRandomPlayerbotMgr.CheckPlayers();
}
#endif

void activateCheckPlayersThread()
{
#ifdef MANGOS
    CheckPlayersThread *thread = new CheckPlayersThread();
    thread->activate();
#endif
#ifdef CMANGOS
    boost::thread t(CheckPlayersThread);
    t.detach();
#endif
}

class botPIDImpl
{
public:
    botPIDImpl(double dt, double max, double min, double Kp, double Ki, double Kd);
    ~botPIDImpl();
    double calculate(double setpoint, double pv);
    void adjust(double Kp, double Ki, double Kd) { _Kp = Kp; _Ki = Ki; _Kd = Kd; }
    void reset() { _integral = 0; }

private:
    double _dt;
    double _max;
    double _min;
    double _Kp;
    double _Ki;
    double _Kd;
    double _pre_error;
    double _integral;
};


botPID::botPID(double dt, double max, double min, double Kp, double Ki, double Kd)
{
    pimpl = new botPIDImpl(dt, max, min, Kp, Ki, Kd);
}
void botPID::adjust(double Kp, double Ki, double Kd)
{
    pimpl->adjust(Kp, Ki, Kd);
}
void botPID::reset()
{
    pimpl->reset();
}
double botPID::calculate(double setpoint, double pv)
{
    return pimpl->calculate(setpoint, pv);
}
botPID::~botPID()
{
    delete pimpl;
}


/**
 * Implementation
 */
botPIDImpl::botPIDImpl(double dt, double max, double min, double Kp, double Ki, double Kd) :
    _dt(dt),
    _max(max),
    _min(min),
    _Kp(Kp),
    _Ki(Ki),
    _Kd(Kd),
    _pre_error(0),
    _integral(0)
{
}

double botPIDImpl::calculate(double setpoint, double pv)
{

    // Calculate error
    double error = setpoint - pv;

    // Proportional term
    double Pout = _Kp * error;

    // Integral term
    _integral += error * _dt;

    double Iout = _Ki * _integral;

    // Derivative term
    double derivative = (error - _pre_error) / _dt;
    double Dout = _Kd * derivative;

    // Calculate total output
    double output = Pout + Iout + Dout;

    // Restrict to max/min
    if (output > _max)
    {
        output = _max;
        _integral -= error * _dt; //Stop integral buildup at max
    }
    else if (output < _min)
    {
        output = _min;
        _integral -= error * _dt; //Stop integral buildup at min
    }

    // Save error to previous error
    _pre_error = error;

    return output;
}

botPIDImpl::~botPIDImpl()
{
}

RandomPlayerbotMgr::RandomPlayerbotMgr() 
: PlayerbotHolder()
, processTicks(0)
, loginProgressBar(NULL)
{
    if (sPlayerbotAIConfig.enabled && sPlayerbotAIConfig.randomBotAutologin)
    {
        eventTimersSynchronized = SyncEventTimers();
        if (!eventTimersSynchronized)
            sLog.outError("RandomPlayerbotMgr: event timer synchronization failed; bot event loading deferred");

        sPlayerbotCommandServer.Start();
        PrepareTeleportCache();

        for (int i = BG_BRACKET_ID_FIRST; i < MAX_BATTLEGROUND_BRACKETS; ++i)
        {
            for (int j = BATTLEGROUND_QUEUE_AV; j < MAX_BATTLEGROUND_QUEUE_TYPES; ++j)
            {
                BgPlayers[j][i][0] = 0;
                BgPlayers[j][i][1] = 0;
                BgBots[j][i][0] = 0;
                BgBots[j][i][1] = 0;
                ArenaBots[j][i][0][0] = 0;
                ArenaBots[j][i][0][1] = 0;
                ArenaBots[j][i][1][0] = 0;
                ArenaBots[j][i][1][1] = 0;
                NeedBots[j][i][0] = false;
                NeedBots[j][i][1] = false;
            }
        }

        //1) Proportional: Amount activity is adjusted based on diff being above or below wanted diff. (100 wanted diff & 0.1 p = 150 diff = -5% activity)
        //2) Integral: Same as proportional but builds up each tick. (100 wanted diff & 0.01 i = 150 diff = -0.5% activity each tick)
        //3) Derative: Based on speed of diff. (+5 diff last tick & 0.05 d = -0.25% activity)
        pid.adjust(0.05,0.001,0.05);
        BgCheckTimer = 0;
        LfgCheckTimer = 0;
        PlayersCheckTimer = 0;
        EventTimeSyncTimer = 0;
        OfflineGroupBotsTimer = 0;
        guildsDeleted = false;
        arenaTeamsDeleted = false;

        if (eventTimersSynchronized)
        {
            for (uint32 bot : GetBots())
            {
                // Typed gate: only a KNOWN set login flag is swept; an unknown
                // read must not trigger the write.
                uint32 loginFlag = 0;
                if (TryGetEventValue(bot, "login", loginFlag) && loginFlag)
                    SetEventValue(bot, "login", 0, 0);
            }
        }

#ifndef MANGOSBOT_ZERO
        // load random bot team members
        auto results = CharacterDatabase.PQuery("SELECT guid FROM arena_team_member");
        if (results)
        {
            sLog.outString("Loading arena team bot members...");
            do
            {
                Field* fields = results->Fetch();
                uint32 lowguid = fields[0].GetUInt32();
                arenaTeamMembers.push_back(lowguid);
            } while (results->NextRow());
        }
#endif
        for (uint32 i = 0; i < sMapStore.GetNumRows(); ++i)
        {
            if (!sMapStore.LookupEntry(i))
                continue;

            uint32 mapId = sMapStore.LookupEntry(i)->MapID;
            facingFix[mapId] = {};
        }

        showLoginWarning = true;
    }
}

RandomPlayerbotMgr::~RandomPlayerbotMgr()
{
}

int RandomPlayerbotMgr::GetMaxAllowedBotCount()
{
    return GetEventValue(0, "bot_count");
}

inline void print_line(Unit* bot, const std::vector<std::pair<int, int>> line, bool is_sqDist_greater_200)
{
    std::ostringstream out;
    out << bot->GetName() << ",";
    out << std::fixed << std::setprecision(1);
    out << "\"LINESTRING(";
    for (auto& p : line)
    {
        out << p.first << " " << p.second << (&p == &line.back() ? "" : ",");
    }    
    out << ")\",";
    out << bot->GetOrientation() << ",";
    out << std::to_string(bot->getRace()) << ",";
    out << std::to_string(bot->getClass()) << ",";
    out << (is_sqDist_greater_200 ? "1" : "0");
    sPlayerbotAIConfig.log("player_paths.csv", out.str().c_str());
}

inline void print_path(Unit* bot, std::vector<std::pair<int, int>>& log)
{
    std::vector<std::pair<int, int>> line;

    std::pair<int, int> lastP = {0, 0};

    for (auto& p : log)
    {
        if (lastP.first && lastP.second && pow(lastP.first - p.first, 2) + pow(lastP.second - p.second, 2) > 200 * 200)
        {
            if (line.size()>1)
                print_line(bot, line, false);      //Print previous path.
            print_line(bot, {lastP, p}, true); //Print jump.
            line.clear();
        }
        line.push_back(p);
        lastP = p;
    }
    if (line.size() > 1)
        print_line(bot, line, false); //Print remaining path.
}

void RandomPlayerbotMgr::LogPlayerLocation()
{
    botCount = 0;
    activeBots = 0;
    if (sPlayerbotAIConfig.randomBotAutologin)
    {
        ForEachPlayerbot([&](Player* bot) {
            if (bot->GetPlayerbotAI())
            {

                botCount++;
                if (bot->GetPlayerbotAI()->AllowActivity(ALL_ACTIVITY))
                {
                    activeBots++;
                }
            }
        });
    }

    for (auto i : GetPlayers())
    {
        Player* bot = i.second;
        if (!bot)
            continue;
        if (bot->GetPlayerbotAI())
        {
            botCount++;
            if (bot->GetPlayerbotAI()->AllowActivity(ALL_ACTIVITY))
                activeBots++;
        }
    }

    if (sPlayerbotAIConfig.hasLog("player_location.csv"))
    {
        try
        {
            sPlayerbotAIConfig.openLog("player_location.csv", "w");

            if (sPlayerbotAIConfig.hasLog("player_route.csv"))
                sPlayerbotAIConfig.openLog("player_route.csv", "w");

            if (sPlayerbotAIConfig.randomBotAutologin)
            {
                ForEachPlayerbot([&](Player* bot) {
                    std::ostringstream out;
                    out << sPlayerbotAIConfig.GetTimestampStr() << "+00,";
                    out << "RND" << ",";
                    out << bot->GetName() << ",";
                    out << std::fixed << std::setprecision(2);
                    WorldPosition(bot).printWKT(out);
                    out << bot->GetOrientation() << ",";
                    out << std::to_string(bot->getRace()) << ",";
                    out << std::to_string(bot->getClass()) << ",";
                    out << bot->GetMapId() << ",";
                    out << bot->GetLevel() << ",";
                    out << bot->GetHealth() << ",";
                    out << bot->GetPowerPercent() << ",";
                    out << bot->GetMoney() << ",";

                    if (bot->GetPlayerbotAI())
                    {
                        out << std::to_string(uint8(bot->GetPlayerbotAI()->GetGrouperType())) << ",";
                        out << std::to_string(uint8(bot->GetPlayerbotAI()->GetGuilderType())) << ",";
                        out << (bot->GetPlayerbotAI()->AllowActivity(ALL_ACTIVITY) ? "active" : "inactive") << ",";
                        out << (bot->GetPlayerbotAI()->IsActive() ? "active" : "delay") << ",";
                        out << bot->GetPlayerbotAI()->HandleRemoteCommand("state") << ",";
                        PlayerbotAI* ai = bot->GetPlayerbotAI();
                        AiObjectContext* context = ai->GetAiObjectContext();

                        out << (AI_VALUE(bool, "should get money") ? "should get money" : "has enough money") << ",";

                        if (sPlayerbotAIConfig.hasLog("player_route.csv") && WorldPosition(bot))
                        {
                            LastMovement& lastMove = AI_VALUE(LastMovement&, "last movement");

                            std::vector<PathNodePoint> fullPath = lastMove.lastPath.getPath();

                            if (!fullPath.empty())
                            {
                                std::vector<std::pair<std::vector<WorldPosition>, bool>> splitPath;

                                bool currentWalkable = fullPath[0].isWalkable();
                                std::vector<WorldPosition> currentSegment;
                                currentSegment.push_back(fullPath[0].point);

                                for (size_t i = 1; i < fullPath.size(); i++)
                                {
                                    bool walkable = fullPath[i].isWalkable();

                                    if (walkable != currentWalkable)
                                    {
                                        // End current segment, start new one beginning with the last point
                                        splitPath.push_back({currentSegment, currentWalkable});
                                        currentSegment.clear();
                                        currentSegment.push_back(fullPath[i - 1].point); // shared junction point
                                        currentWalkable = walkable;
                                    }

                                    currentSegment.push_back(fullPath[i].point);
                                }

                                splitPath.push_back({currentSegment, currentWalkable});

                                uint32 segmentNr = 0;

                                for (auto& [segement, walkable] : splitPath)
                                {
                                    segmentNr++;
                                    std::ostringstream out;
                                    out << bot->GetName() << ",";
                                    out << std::fixed << std::setprecision(1);

                                    out << segmentNr << ",";

                                    WorldPosition().printWKT(segement, out, 1, false);

                                    out << bot->GetOrientation() << ",";
                                    out << std::to_string(bot->getRace()) << ",";
                                    out << std::to_string(bot->getClass()) << ",";
                                    out << (walkable ? "1" : "0") << ",";
                                    out << lastMove.moveEvent.getSource();
                                    sPlayerbotAIConfig.log("player_route.csv", out.str().c_str());
                                }
                            }
                        }
                    }
                    else
                    {
                        out << 0 << "," << 0 << ",err,err,err,err,";
                    }

                    out << (bot->IsInCombat() ? "combat" : "safe") << ",";
                    out << (bot->IsDead() ? (bot->GetCorpse() ? "ghost" : "dead") : "alive") << ",";

                    if (bot->GetGroup())
                        WorldPosition(bot).printWKT({bot, sObjectMgr.GetPlayer(bot->GetGroup()->GetLeaderGuid())}, out, 1);

                    sPlayerbotAIConfig.log("player_location.csv", out.str().c_str());

                    if (sPlayerbotAIConfig.hasLog("player_paths.csv") && WorldPosition(bot))
                    {
                        auto& botMoveLog = playerBotMoveLog[bot->GetObjectGuid().GetCounter()];

                        std::pair<int32, int32> curDisplayPos = std::make_pair(WorldPosition(bot).getDisplayX(), WorldPosition(bot).getDisplayY());

                        botMoveLog.push_back(curDisplayPos);

                        if (botMoveLog.size() > 100)
                        {
                            print_path(bot, botMoveLog);
                            botMoveLog.clear();
                            botMoveLog.push_back(curDisplayPos); //Start next path at current position.
                        }
                    }
                });
            }

            for (auto i : GetPlayers())
            {
                Player* bot = i.second;
                if (!bot)
                    continue;

                std::ostringstream out;
                out << sPlayerbotAIConfig.GetTimestampStr() << "+00,";
                out << "PLR" << ",";
                out << bot->GetName() << ",";
                out << std::fixed << std::setprecision(2);
                WorldPosition(bot).printWKT(out);
                out << bot->GetOrientation() << ",";
                out << std::to_string(bot->getRace()) << ",";
                out << std::to_string(bot->getClass()) << ",";
                out << bot->GetMapId() << ",";
                out << bot->GetLevel() << ",";
                out << bot->GetHealth() << ",";
                out << bot->GetPowerPercent() << ",";
                out << bot->GetMoney() << ",";
                if (bot->GetPlayerbotAI())
                {
                    out << std::to_string(uint8(bot->GetPlayerbotAI()->GetGrouperType())) << ",";
                    out << std::to_string(uint8(bot->GetPlayerbotAI()->GetGuilderType())) << ",";
                    out << (bot->GetPlayerbotAI()->AllowActivity(ALL_ACTIVITY) ? "active" : "inactive") << ",";
                    out << (bot->GetPlayerbotAI()->IsActive() ? "active" : "delay") << ",";
                    out << bot->GetPlayerbotAI()->HandleRemoteCommand("state") << ",";
                    PlayerbotAI* ai = bot->GetPlayerbotAI();
                    AiObjectContext* context = ai->GetAiObjectContext();

                    out << (AI_VALUE(bool, "should get money") ? "should get money" : "has enough money") << ",";
                }
                else
                {
                    out << 0 << "," << 0 << ",player,player,player,player,";
                }

                out << (bot->IsInCombat() ? "combat" : "safe") << ",";
                out << (bot->IsDead() ? (bot->GetCorpse() ? "ghost" : "dead") : "alive") << ",";

                if (bot->GetGroup())
                    WorldPosition(bot).printWKT({bot, sObjectMgr.GetPlayer(bot->GetGroup()->GetLeaderGuid())}, out, 1);

                sPlayerbotAIConfig.log("player_location.csv", out.str().c_str());

                if (sPlayerbotAIConfig.hasLog("player_paths.csv") && WorldPosition(bot))
                {
                    auto& botMoveLog = playerBotMoveLog[bot->GetObjectGuid().GetCounter()];

                    std::pair<int32, int32> curDisplayPos = std::make_pair(WorldPosition(bot).getDisplayX(), WorldPosition(bot).getDisplayY());

                    botMoveLog.push_back(curDisplayPos);

                    if (botMoveLog.size() > 100)
                    {
                        print_path(bot, botMoveLog);
                        botMoveLog.clear();
                        botMoveLog.push_back(curDisplayPos); //Start next path at current position.
                    }
                }
            }
        }
        catch (...)
        {
            return;
            //This is to prevent some thread-unsafeness. Crashes would happen if bots get added or removed.
            //We really don't care here. Just skip a log. Making this thread-safe is not worth the effort.
        }
    }
    if (sPlayerbotAIConfig.hasLog("transport.csv"))
    {
        sPlayerbotAIConfig.openLog("transport.csv", "w");
        for (auto& [mapId, map] : sMapMgr.Maps())
        {
            for (auto& transport : WorldPosition(map->GetId(), 1, 1).getTransports())
            {
                std::ostringstream out;
                out << sPlayerbotAIConfig.GetTimestampStr() << "+00,";
                if (transport->GetName() == nullptr || transport->GetName()[0] == '\0')
                {
                    GameObjectInfo const* data = sGOStorage.LookupEntry<GameObjectInfo>(transport->GetEntry());
                    out << data->name << ",";
                }
                else
                    out << transport->GetName() << ",";

                out << transport->GetEntry() << ",";
                out << std::fixed << std::setprecision(2);
                WorldPosition(transport).printWKT(out);
                out << transport->GetOrientation();

                sPlayerbotAIConfig.log("transport.csv", out.str().c_str());
            }
        }
    }
}

void RandomPlayerbotMgr::UpdateAIInternal(uint32 elapsed, bool minimal)
{
#ifdef MEMORY_MONITOR
    sMemoryMonitor.Print();
    sMemoryMonitor.LogCount(sConfig.GetStringDefault("LogsDir") + "/" + "memory.csv");
#endif

    if (!sPlayerbotAIConfig.randomBotAutologin || !sPlayerbotAIConfig.enabled)
        return;

    // Timer rows must be shifted before any per-bot event cache is loaded.
    // A failed confirmed write retries on a later update and admits no bot
    // work from unsynchronized deadlines.
    if (!eventTimersSynchronized)
    {
        if (!SyncEventTimers())
        {
            SetAIInternalUpdateDelay(sPlayerbotAIConfig.randomBotUpdateInterval);
            return;
        }

        eventTimersSynchronized = true;
        for (uint32 bot : GetBots())
        {
            uint32 loginFlag = 0;
            if (TryGetEventValue(bot, "login", loginFlag) && loginFlag)
                SetEventValue(bot, "login", 0, 0);
        }
    }

    RetryFailedLoginCleanups();

#ifdef GenerateBotTests
    if (sPlayerbotAIConfig.startupRunTestsPending)
    {
        sPlayerbotAIConfig.startupRunTestsPending = false;

        for (const std::string& runTestParam : sPlayerbotAIConfig.startupRunTests)
        {
            std::list<std::string> messages = HandlePlayerbotCommand("runtest " + runTestParam, nullptr, SEC_PLAYER);
            for (const std::string& message : messages)
                sLog.outString("[Config RunTest] %s", message.c_str());
        }
    }
#endif

    if (!playersLevel)
        playersLevel = sPlayerbotAIConfig.syncLevelNoPlayer;

    ScaleBotActivity();
    if (sPlayerbotAIConfig.asyncBotLogin)
    {
        auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "AsyncBotLogin");
        sPlayerBotLoginMgr.Update(players);
        pmo.reset();
    }

    // Typed read: an unknown bot_count must not be rewritten (the failed
    // read is not an expired value) - the bot-count-driven cycle defers.
    uint32 maxAllowedBotCount = 0;
    if (!TryGetEventValue(0, "bot_count", maxAllowedBotCount))
    {
        SetAIInternalUpdateDelay(sPlayerbotAIConfig.randomBotUpdateInterval);
        return;
    }

    if (!maxAllowedBotCount || ((uint32)maxAllowedBotCount < sPlayerbotAIConfig.minRandomBots || (uint32)maxAllowedBotCount > sPlayerbotAIConfig.maxRandomBots))
    {
        maxAllowedBotCount = urand(sPlayerbotAIConfig.minRandomBots, sPlayerbotAIConfig.maxRandomBots);
        SetEventValue(0, "bot_count", maxAllowedBotCount,
            urand(sPlayerbotAIConfig.randomBotCountChangeMinInterval, sPlayerbotAIConfig.randomBotCountChangeMaxInterval));
    }

    std::list<uint32> availableBots = GetBots();    
    uint32 availableBotCount = availableBots.size();
    uint32 onlineBotCount = GetPlayerbotsAmount();
    
    SetAIInternalUpdateDelay(sPlayerbotAIConfig.randomBotUpdateInterval);

    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT,
        onlineBotCount < maxAllowedBotCount ? "RandomPlayerbotMgr::Login" : "RandomPlayerbotMgr::UpdateAIInternal");

    if (time(nullptr) > (EventTimeSyncTimer + 30))
        SaveCurTime();

    if (availableBotCount < maxAllowedBotCount && !sWorld.IsShutdowning())
    {
        bool logInAllowed = true;
        if (sPlayerbotAIConfig.randomBotLoginWithPlayer)
        {
            logInAllowed = !players.empty();
        }

        if (logInAllowed)
        {
            AddRandomBots();
        }
    }

    if (sPlayerbotAIConfig.syncLevelWithPlayers && players.size())
    {
        if (time(nullptr) > (PlayersCheckTimer + 60))
            CheckPlayers();
    }

    if (sPlayerbotAIConfig.randomBotJoinLfg && players.size())
    {
        if (time(nullptr) > (LfgCheckTimer + 30))
            CheckLfgQueue();
    }

    if (sPlayerbotAIConfig.randomBotJoinBG/* && players.size()*/)
    {
        if (time(nullptr) > (BgCheckTimer + 30))
            CheckBgQueue();
    }

    if (time(nullptr) > (OfflineGroupBotsTimer + 5) && players.size())
        AddOfflineGroupBots();

    uint32 updateBots = sPlayerbotAIConfig.randomBotsPerInterval == 0 ? UINT32_MAX : sPlayerbotAIConfig.randomBotsPerInterval;

    //Update bots
    for (auto bot : availableBots)
    {
        if (GetPlayerBot(bot))
        {
            if (ProcessBot(bot))
                updateBots--;

            if (!updateBots)
                break;
        }
    }

    uint32 maxLogins = sPlayerbotAIConfig.randomBotsMaxLoginsPerInterval;

    //Log in bots
    if (sRandomPlayerbotMgr.GetDatabaseDelay("CharacterDatabase") < 10 * IN_MILLISECONDS && !sPlayerbotAIConfig.asyncBotLogin && onlineBotCount < maxAllowedBotCount && maxLogins > 0)
    {
        for (auto bot : availableBots)
        {
            if (GetPlayerBot(bot))
                continue;   

            // find() instead of operator[]: probing must not default-insert an
            // empty per-bot map (that poisoned the explicit load-state
            // bookkeeping the bulk loader relies on).
            auto cachedBot = eventCache.find(bot);
            if (cachedBot != eventCache.end() && !cachedBot->second.empty() && GetEventValue(bot, "login"))
            {
                onlineBotCount++;
                continue;
            }

            if (GetEventValue(bot, "login"))
                onlineBotCount++;

            if (onlineBotCount >= maxAllowedBotCount)
                break;

            if (ProcessBot(bot)) {
                --maxLogins;
            }

            if (maxLogins == 0)
                break;
        }
    }

    LoginFreeBots();

    //sLog.outString("[char %d, bot %d]", CharacterDatabase.m_threadBody->m_sqlQueue.size(), CharacterDatabase.m_threadBody->m_sqlQueue.size());
   
    LogPlayerLocation();

    DelayedFacingFix();

    MirrorAh();

    for (auto& [mapId, map] : sMapMgr.Maps())
    {
        sPerformanceMonitor.Init(map->GetId(), map->GetInstanceId());
    }

    //Ping character database.
    CharacterDatabase.AsyncPQuery(&RandomPlayerbotMgr::DatabasePing, sWorld.GetCurrentMSTime(), std::string("CharacterDatabase"), "SELECT 1");

    PlayerbotHolder::UpdateAIInternal(elapsed, minimal);
}

void RandomPlayerbotMgr::ScaleBotActivity()
{
    float activityPercentage = getActivityPercentage();

    //if (activityPercentage >= 100.0f || activityPercentage <= 0.0f) pid.reset(); //Stop integer buildup during max/min activity

    //    % increase/decrease                   wanted diff                                         , avg diff
    float activityPercentageMod = pid.calculate(sRandomPlayerbotMgr.GetPlayers().empty() ? sPlayerbotAIConfig.diffEmpty : sPlayerbotAIConfig.diffWithPlayer, sWorld.GetAverageDiff());

    activityPercentage = activityPercentageMod + 50;

    //Cap the percentage between 0 and 100.
    activityPercentage = std::max(0.0f, std::min(100.0f, activityPercentage));

    setActivityPercentage(activityPercentage);

    if (sPlayerbotAIConfig.hasLog("activity_pid.csv"))
    {
        double virtualMemUsedByMe = 0;
#if PLATFORM == PLATFORM_WINDOWS
        PROCESS_MEMORY_COUNTERS_EX pmc;
        GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
        virtualMemUsedByMe = pmc.PrivateUsage;
#endif

        std::ostringstream out;
        out << sWorld.GetCurrentMSTime() << ", ";

        out << sWorld.GetCurrentDiff() << ",";
        out << sWorld.GetAverageDiff() << ",";
        out << sWorld.GetMaxDiff() << ",";
        out << virtualMemUsedByMe << ",";
        out << activityPercentage << ",";
        out << activityPercentageMod << ",";
        out << activeBots << ",";
        out << GetPlayerbotsAmount() << ",";

        float totalLevel = 0, totalGold = 0, totalGearscore = 0;

        if (sPlayerbotAIConfig.randomBotAutologin)
        {
            ForEachPlayerbot([&](Player* bot)
            {
                if (bot->GetPlayerbotAI()->AllowActivity())
                {
                    std::string bracket = "level:" + std::to_string(bot->GetLevel() / 10);

                    float level = bot->GetPlayerbotAI()->GetLevelFloat();
                    totalLevel += level;
                    float gold = bot->GetMoney() / 10000;
                    totalGold += gold;
                    float gearscore = bot->GetPlayerbotAI()->GetEquipGearScore(bot, false, false);
                    totalGearscore += gearscore;

                    const uint32 botGuid = bot->GetObjectGuid().GetCounter();
                    PushMetric(botPerformanceMetrics[bracket], botGuid, level);
                    PushMetric(botPerformanceMetrics["gold"], botGuid, gold);
                    PushMetric(botPerformanceMetrics["gearscore"], botGuid, gearscore);
                }
            });
        }

        out << std::fixed << std::setprecision(4);
        out << totalLevel << ",";

        for (uint8 i = 0; i < (DEFAULT_MAX_LEVEL / 10) + 1; i++)
        {
            out << GetMetricDelta(botPerformanceMetrics["level:" + std::to_string(i)]) * 12 * 60 << ",";
        }

        out << totalGold << ",";
        out << GetMetricDelta(botPerformanceMetrics["gold"]) * 12 * 60 << ",";
        out << totalGearscore << ",";
        out << GetMetricDelta(botPerformanceMetrics["gearscore"]) * 12 * 60 << ",";
        //out << CharacterDatabase.m_threadBody->m_sqlQueue.size();

        sPlayerbotAIConfig.log("activity_pid.csv", out.str().c_str());
    }
}

void RandomPlayerbotMgr::ReconstructPostCreateOwners()
{
    // COUNT-first over finalized intents so "no unsettled owners" is
    // distinguishable from "could not ask": the pinned cores return a null
    // result for BOTH an empty row set and a failed query. A marker row alone
    // cannot own a character: only its exact finalized creation intent can.
    static char const* kScanCondition =
        "i.owner = 0 AND i.event = 'create pending' AND i.value = 2 "
        "AND i.data LIKE 'name:%%|account:%%|level:%%|login:%%|obligations:1' "
        "AND i.bot NOT IN "
        "(SELECT d.bot FROM (SELECT bot FROM ai_playerbot_random_bots WHERE owner = 0 AND event = 'delete') d)";

    // While deletion state is UNKNOWN, this scan cannot be authoritative: it
    // would replace the owner map having skipped rows it could not classify.
    // Keep existing owners and let the deletion-scan recovery rerun us.
    if (!PlayerbotHolder::DeletionStateKnown())
    {
        postCreateScanFailed = true;
        postCreateScanRetryPasses = 0;
        sLog.outDetail("ReconstructPostCreateOwners deferred: deletion-intent state is not yet known");
        return;
    }

    std::map<uint32, PostCreateOwner> scanned;
    bool scanSucceeded = false;
    if (auto countResult = CharacterDatabase.PQuery(
            "SELECT COUNT(*) FROM ai_playerbot_random_bots i "
            "JOIN characters c ON c.guid = i.bot WHERE %s", kScanCondition))
    {
        if (countResult->Fetch()[0].GetUInt32() == 0)
            scanSucceeded = true; // confirmed: nothing unsettled
        else if (auto rows = CharacterDatabase.PQuery(
            "SELECT i.bot, i.data, c.account, c.name FROM ai_playerbot_random_bots i "
            "JOIN characters c ON c.guid = i.bot WHERE %s", kScanCondition))
        {
            do
            {
                Field* fields = rows->Fetch();
                uint32 const guid = fields[0].GetUInt32();
                if (PlayerbotHolder::IsDeletionPending(guid))
                    continue; // in-memory adoption not yet durably visible

                std::string recordedName;
                uint32 recordedAccount = 0;
                uint32 level = 0;
                bool autoAdd = false;
                bool hasObligations = false;
                uint32 const currentAccount = fields[2].GetUInt32();
                std::string const currentName = fields[3].GetCppString();
                if (!living::DecodeCreationIntent(fields[1].GetCppString(), recordedName, recordedAccount,
                        level, autoAdd, hasObligations)
                    || !hasObligations
                    || !living::CreationIdentityMatches(recordedName, recordedAccount,
                        currentName, currentAccount))
                {
                    sLog.outError("ReconstructPostCreateOwners: malformed or identity-mismatched finalized intent for guid %u; owner retained QUARANTINED", guid);
                    postCreateQuarantined.insert(guid);
                    continue;
                }

                postCreateQuarantined.erase(guid);
                scanned[guid] = PostCreateOwner{ currentAccount,
                    sPlayerbotAIConfig.IsFreeAltBot(guid) || autoAdd };
            } while (rows->NextRow());
            scanSucceeded = true;
        }
    }

    if (!scanSucceeded)
        sLog.outError("ReconstructPostCreateOwners: durable intent scan failed; existing owners are kept and the scan retries with backoff");
    else if (!scanned.empty())
        sLog.outString("Reconstructed %zu post-create scheduler owner(s) from durable intents", scanned.size());

    postCreateScanFailed = !scanSucceeded;
    postCreateScanRetryPasses = 0;
    postCreateOwners = living::ReconcilePostCreateOwners(postCreateOwners, scanSucceeded, scanned);
}

void RandomPlayerbotMgr::LoginFreeBots()
{
    // Bounded-backoff retry of a failed owner-reconstruction scan: transient
    // database failure must not orphan durable obligations until the next
    // manual reload.
    if (postCreateScanFailed && ++postCreateScanRetryPasses >= kOwnerScanRetryPasses)
        ReconstructPostCreateOwners();

    if (!sPlayerbotAIConfig.freeAltBots.empty() || !postCreateOwners.empty())
    {
        // LOGIN_ONLY_ALWAYS_ACTIVE suppresses only the LOGINS this sweep
        // would initiate; lifecycle obligations of bots that are already
        // online are still serviced (they used to starve in that mode).
        bool const autoLoginAllowed =
            sPlayerbotAIConfig.botAutologin != BotAutoLogin::LOGIN_ONLY_ALWAYS_ACTIVE;

        std::vector<std::pair<uint32, uint32>> botsToRemove;

        // The sweep services the always-online membership AND the transient
        // post-create owners: a bot whose durable markers are unsettled keeps
        // its scheduler even when it is not (or no longer) always-online.
        // Owner entries carry their own login authorization; membership
        // entries are authorized by definition.
        struct SweepEntry
        {
            uint32 accountId;
            uint32 botGuid;
            bool mayAutoLogin;
        };
        std::vector<SweepEntry> sweep;
        for (auto const& [entryAccount, entryGuid] : sPlayerbotAIConfig.freeAltBots)
            sweep.push_back({ entryAccount, entryGuid, true });
        for (auto const& [ownerGuid, owner] : postCreateOwners)
            if (!sPlayerbotAIConfig.IsFreeAltBot(ownerGuid))
                sweep.push_back({ owner.accountId, ownerGuid, owner.mayAutoLogin });

        for (auto [accountId, botGuid, mayAutoLogin] : sweep)
        {
            if (IsLifecycleLoginBlocked(botGuid))
                continue;

            ObjectGuid guid(ObjectGuid(HIGHGUID_PLAYER, botGuid));
            Player* bot = sObjectMgr.GetPlayer(guid, false);

            // A deletion-pending character must never be logged in or receive
            // post-create mutations, whatever list it is still riding.
            if (PlayerbotHolder::IsDeletionPending(botGuid))
                continue;

            if (!bot)
            {
                // Login only when the mode AND the entry's own authorization
                // allow it (login=0 lifecycle owners wait for an authorized
                // login instead of forcing one). Deletion-pending was already
                // excluded above; the shared predicate keeps the policy
                // testable in one place.
                if (living::MayAutoLoginPostCreateOwner(autoLoginAllowed, mayAutoLogin,
                        PlayerbotHolder::IsDeletionPending(botGuid)))
                {
                    sLog.outDetail("Add player %d", botGuid);
                    AddPlayerBot(botGuid, accountId);
                }
            }
            else if (!bot->IsBeingTeleported())
            {
                // Transient post-create ownership, tracked separately from the
                // always-online membership: the bot may leave the schedule
                // only when every marker below is authoritatively absent or
                // confirmed-cleared and the group join reached a confirmed
                // terminal persist. Unknown (typed-untrusted) reads keep the
                // bot scheduled and mutate nothing.
                bool postCreateSettled = true;

                // One-shot marker consume: run the runtime effect EXACTLY
                // ONCE per process, then retry only the durable clear -
                // unbounded and self-healing (the ledger is retained, so a
                // stuck clear can never re-license the effect; it settles the
                // moment the database recovers, and is disclosed once
                // meanwhile). The whole protocol, including the ledger
                // lifecycle, lives in living:: so the host suite exercises
                // exactly what runs here.
                auto consumeOneShot = [&](char const* marker, auto&& applyEffect) -> bool
                {
                    uint32 present = 0;
                    bool const known = sRandomPlayerbotMgr.TryGetEventValue(botGuid, marker, present);
                    return living::ConsumeOneShotMarker(oneShotMarkers, botGuid, marker,
                        known, present != 0, applyEffect,
                        [&]() { return sRandomPlayerbotMgr.SetValue(botGuid, marker, 0); },
                        [&](uint32 attempts)
                        {
                            sLog.outError("Bot %u: post-create marker '%s' clear has not confirmed after %u attempts; "
                                "retrying every pass until it lands (the effect ran once and is never replayed)",
                                botGuid, marker, attempts);
                        });
                };

                // create levelup / create gear are the same one-shot consume:
                // apply once per process, retry only the confirmed clear.
                //
                // The effect must be QUEUED FOR PERSISTENCE before the marker
                // is cleared. Randomize saves only when the character DB is
                // not backed up, and the gear effects never save at all, so
                // the consume queues the save itself - otherwise the clear (a
                // synchronous DELETE, durable the instant it returns) would
                // discharge an obligation that existed only in memory. When
                // Randomize also saved (healthy DB) this is a second,
                // redundant save - ACCEPTED: it happens once per consume, not
                // per pass, and every conditional variant that mirrors the
                // factory's delay gate reintroduces a rare zero-saves race,
                // which is the exact loss this save exists to close.
                //
                // Two crash windows remain, both accepted for a bot: before
                // the clear, the randomization replays once on the next pass;
                // after the clear but before the queued save drains, that one
                // randomization is lost. Neither is worth a durable
                // applied-phase protocol - see the OneShotMarker disclosure.
                bool const levelupSettled = consumeOneShot("create levelup", [&]()
                {
                    PlayerbotFactory factory(bot, bot->GetLevel());
                    factory.Randomize(true, false);
                    bot->SaveToDB();
                });
                postCreateSettled &= levelupSettled;

                Player* master = nullptr;

                // TYPED join-intent read: an untrusted read must not be
                // interpreted as "no join owed" - it keeps the bot scheduled
                // and attempts nothing.
                uint32 joinAttempts = 0;
                bool const joinKnown = sRandomPlayerbotMgr.TryGetEventValue(botGuid, "create group", joinAttempts);
                bool groupJoinSettled = joinKnown && joinAttempts == 0;
                if (!joinKnown)
                    joinAttempts = 0;

                if (joinAttempts && time(0) >= groupJoinBackoffUntil[botGuid])
                {
                    // The stored data is a stable target GUID (creation
                    // resolves it before any mutation); legacy rows may still
                    // hold a name. The event is cleared ONLY on verified
                    // membership or a terminal outcome - offline targets, full
                    // groups and failed invites keep it for a bounded retry.
                    std::string const groupWith = sRandomPlayerbotMgr.GetData(botGuid, "create group");

                    // COUNT-first typed existence: GetPlayerAccountIdByGUID and
                    // GetPlayerGuidByName both return zero/empty for BOTH "row
                    // absent" and "query failed", so a bool would let a transient
                    // outage look like a deletion and terminally clear the join
                    // intent. A COUNT yields exactly one row on success, so a null
                    // result is UNKNOWN (retry later), zero is confirmed missing.
                    living::TargetExistence existence = living::TargetExistence::Unavailable;
                    Player* target = nullptr;
                    uint32 targetGuidLow = 0;
                    if (living::TryParseUInt32InRange(groupWith, 1, 0xFFFFFFFFu, targetGuidLow))
                    {
                        ObjectGuid const targetGuid(HIGHGUID_PLAYER, targetGuidLow);
                        std::optional<uint64> count;
                        if (auto result = CharacterDatabase.PQuery(
                                "SELECT COUNT(*) FROM characters WHERE guid = '%u'", targetGuidLow))
                            count = result->Fetch()[0].GetUInt32();
                        existence = living::ClassifyTargetExistence(count);
                        target = sObjectMgr.GetPlayer(targetGuid);
                    }
                    else if (!groupWith.empty())
                    {
                        std::string escapedName = groupWith;
                        CharacterDatabase.escape_string(escapedName);
                        std::optional<uint64> count;
                        if (auto result = CharacterDatabase.PQuery(
                                "SELECT COUNT(*) FROM characters WHERE name = '%s'", escapedName.c_str()))
                            count = result->Fetch()[0].GetUInt32();
                        existence = living::ClassifyTargetExistence(count);
                        target = sObjectAccessor.FindPlayerByName(groupWith.c_str());
                    }

                    bool membershipVerified = false;
                    if (target)
                    {
                        bot->GetPlayerbotAI()->DoSpecificAction("join", Event("create group", "", target));
                        // Verify the RESULTING membership; the action's return
                        // value alone cannot prove the invite stuck. Only a
                        // VERIFIED membership promotes the candidate target to
                        // the post-create master - a failed/full-group attempt
                        // must not teleport the bot to the target or leak the
                        // target's level into gear processing on every retry.
                        membershipVerified = bot->GetGroup() && bot->GetGroup() == target->GetGroup();
                        if (membershipVerified)
                            master = target;
                    }

                    living::GroupJoinPlan const plan = living::PlanGroupJoinAttempt(
                        existence, target != nullptr, membershipVerified,
                        joinAttempts - 1, /*maxAttempts*/ 10, /*baseDelaySeconds*/ 30);

                    // Advance the retry/backoff bookkeeping ONLY when the durable
                    // write is confirmed: a lost/ambiguous write used to advance
                    // the in-memory backoff while the durable attempt count never
                    // moved, replaying the join work and unbounding the budget.
                    // SetValue's bool maps to the typed result the persist gate
                    // consumes (unconfirmed collapses to a short holdoff).
                    auto persistResult = [](bool confirmed)
                    {
                        return confirmed ? living::EventWriteResult::DesiredStateConfirmed
                                         : living::EventWriteResult::StateUnknown;
                    };
                    if (plan.decision == living::GroupJoinDecision::RetryLater)
                    {
                        bool const confirmed = sRandomPlayerbotMgr.SetValue(botGuid, "create group", plan.attemptNumber + 1, groupWith);
                        living::GroupJoinPersist const persist = living::PlanGroupJoinPersist(plan.decision, persistResult(confirmed));
                        groupJoinBackoffUntil[botGuid] = time(0) + (persist.advanceBackoff ? plan.retryDelaySeconds : 30u);
                    }
                    else
                    {
                        if (plan.decision == living::GroupJoinDecision::ClearTerminal)
                            sLog.outDetail("Bot %s: giving up on group target '%s' (%s)", bot->GetName(), groupWith.c_str(),
                                existence == living::TargetExistence::ConfirmedMissing ? "target deleted" : "retry budget exhausted");

                        // Before the ONLY durable copy of the verified target
                        // is cleared, copy the verified target LEVEL into a
                        // dependent, still-unapplied sync/upgrade gear
                        // obligation (confirmed write): a crash after this
                        // clear but before the gear effect must recover with
                        // the original verified target, never silently fall
                        // back to the bot's own level.
                        bool targetCaptured = true;
                        if (plan.decision == living::GroupJoinDecision::ClearJoined && master)
                        {
                            uint32 gearPhaseNow = 0;
                            if (!sRandomPlayerbotMgr.TryGetEventValue(botGuid, "create gear", gearPhaseNow))
                                targetCaptured = false;
                            else if (gearPhaseNow)
                            {
                                std::string gearBaseNow;
                                uint32 capturedLevel = 0;
                                living::SplitGearTarget(sRandomPlayerbotMgr.GetData(botGuid, "create gear"),
                                    gearBaseNow, capturedLevel);
                                if ((gearBaseNow == "sync" || gearBaseNow == "upgrade") && capturedLevel == 0)
                                    targetCaptured = sRandomPlayerbotMgr.SetValue(botGuid, "create gear", 1,
                                        living::StampGearTarget(gearBaseNow, master->GetLevel()));
                            }
                        }

                        if (!targetCaptured)
                        {
                            // The dependent gear target could not be durably
                            // captured: keep the group marker (the only durable
                            // target) and retry the terminal step next pass.
                            groupJoinBackoffUntil[botGuid] = time(0) + 30;
                        }
                        else
                        {
                        bool const cleared = sRandomPlayerbotMgr.SetValue(botGuid, "create group", 0);
                        living::GroupJoinPersist const persist = living::PlanGroupJoinPersist(plan.decision, persistResult(cleared));
                        if (persist.consumed)
                        {
                            groupJoinBackoffUntil.erase(botGuid);
                            // Only a CONFIRMED terminal clear settles the join
                            // ownership; RetryLater and unconfirmed clears keep
                            // the bot scheduled for the next pass.
                            groupJoinSettled = true;
                        }
                        else
                            groupJoinBackoffUntil[botGuid] = time(0) + 30; // clear not confirmed: retry it, keep the marker
                        }
                    }
                }
                postCreateSettled &= groupJoinSettled;

                // gear=sync/upgrade is DEFINED against the verified master's
                // level: once the join verifies, that level is captured into
                // the gear payload ("sync@<level>"), so a captured obligation
                // no longer needs a live master. While the join is still
                // retryable an uncaptured marker is retained (deferred, not
                // consumed); a terminal join settles it with the bot's own
                // level as the documented fallback.
                //
                // SERIALIZED behind the levelup marker: gear must not mutate
                // state while the levelup effect is still owed.
                uint32 gearPhase = 0;
                bool const gearKnown = sRandomPlayerbotMgr.TryGetEventValue(botGuid, "create gear", gearPhase);
                std::string gearBase;
                uint32 gearCapturedLevel = 0;
                if (gearKnown && gearPhase)
                    living::SplitGearTarget(sRandomPlayerbotMgr.GetData(botGuid, "create gear"),
                        gearBase, gearCapturedLevel);
                bool const gearNeedsMaster = gearKnown && gearPhase != 0
                    && (gearBase == "sync" || gearBase == "upgrade") && gearCapturedLevel == 0;
                if (!gearKnown || !levelupSettled
                    || (gearPhase != 0
                        && !living::MayApplyMasterDerivedGear(gearNeedsMaster, groupJoinSettled, master != nullptr)))
                {
                    postCreateSettled = false; // unknown read, levelup unsettled, or join not settled
                }
                else
                {
                postCreateSettled &= consumeOneShot("create gear", [&]()
                {
                    std::string gear;
                    uint32 capturedLevel = 0;
                    living::SplitGearTarget(sRandomPlayerbotMgr.GetData(botGuid, "create gear"),
                        gear, capturedLevel);
                    if (gear == "empty")
                    {
                        for (uint8 slot = 0; slot < EQUIPMENT_SLOT_END; ++slot)
                        {
                            bot->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
                        }
                    }

                    else if (gear == "green" || gear == "uncommon")
                    {
                        PlayerbotFactory factory(bot, bot->GetLevel(), ITEM_QUALITY_UNCOMMON);
                        factory.EquipGear();
                    }
                    else if (gear == "blue" || gear == "rare")
                    {
                        PlayerbotFactory factory(bot, bot->GetLevel(), ITEM_QUALITY_RARE);
                        factory.EquipGear();
                    }
                    else if (gear == "purple" || gear == "epic")
                    {
                        PlayerbotFactory factory(bot, bot->GetLevel(), ITEM_QUALITY_EPIC);
                        factory.EquipGear();
                    }
                    else if (gear == "upgrade")
                    {
                        // Captured verified-target level first; live master
                        // second (same-pass verification); own level only as
                        // the terminal-join fallback.
                        PlayerbotFactory factory(bot, capturedLevel ? capturedLevel
                            : (master ? master->GetLevel() : bot->GetLevel()), ITEM_QUALITY_NORMAL);
                        factory.UpgradeGear(false);
                    }
                    else if (gear == "sync")
                    {
                        PlayerbotFactory factory(bot, capturedLevel ? capturedLevel
                            : (master ? master->GetLevel() : bot->GetLevel()), ITEM_QUALITY_NORMAL);
                        factory.UpgradeGear(true);
                    }
                    else if (gear == "best")
                    {
                        PlayerbotFactory factory(bot, bot->GetLevel());
                        factory.EquipGearBest();
                    }
                    else if (gear == "partial")
                    {
                        PlayerbotFactory factory(bot, bot->GetLevel());
                        factory.EquipGearPartialUpgrade();
                    }
                    else
                    {
                        PlayerbotFactory factory(bot, bot->GetLevel());
                        factory.EquipGear();
                    }

                    // The gear effects mutate equipment in memory only; queue
                    // the save before the consume clears the marker.
                    bot->SaveToDB();
                });
                }

                postCreateSettled &= consumeOneShot("test", [&]()
                {
                    PlayerbotAI* ai = bot->GetPlayerbotAI();
                    AiObjectContext* context = ai->GetAiObjectContext();
                    (void)context;
                    std::string testName = GetEventData(botGuid, "test");
                    testName = std::regex_replace(testName, std::regex("\\'"), "'");
                    std::string strategyName = "test::" + testName;
                    ai->ChangeStrategy("+" + strategyName, BotState::BOT_STATE_NON_COMBAT);
                    SET_AI_VALUE2(bool, "manual bool", "is running test", true);
                });

                if (!IsRandomBot(bot) && GetPlayerBot(guid)) //Place bot in player manager.
                {
                    for (auto& [mGuid, master] : players)
                    {
                        ObjectGuid masterGuid(ObjectGuid(HIGHGUID_PLAYER, mGuid));
                        if (accountId == sObjectMgr.GetPlayerAccountIdByGUID(masterGuid))
                        {
                            PlayerbotMgr* mgr = master->GetPlayerbotMgr();
                            if (mgr)
                            {
                                MovePlayerBot(guid, mgr);
                            }
                        }
                    }
                }

                if (master)
                    bot->TeleportTo(WorldPosition(master));

                // The finalized creation intent (value 2) is itself a
                // settlement obligation: it carries the restart login
                // authorization, so it is cleared (confirmed) only once every
                // OTHER obligation settled; a pre-persistence intent means
                // finalization still owns the bot.
                uint32 creationIntent = 0;
                if (!sRandomPlayerbotMgr.TryGetEventValue(botGuid, "create pending", creationIntent))
                    postCreateSettled = false;
                else if (creationIntent == living::kCreationIntentPrePersistence)
                    postCreateSettled = false;
                else if (creationIntent == living::kCreationIntentFinalized && postCreateSettled
                    && !sRandomPlayerbotMgr.SetValue(botGuid, "create pending", 0))
                    postCreateSettled = false;
                else if (creationIntent != living::kCreationIntentFinalized)
                    postCreateSettled = false;

                // Release from the schedule only from a KNOWN, valid always
                // state AND with every transient post-create obligation
                // settled: an unreadable `always` row must not unschedule a
                // possibly-active bot, and unfinished markers/joins must keep
                // their scheduler owner.
                uint32 alwaysRaw = 0;
                living::AlwaysOnlineState alwaysState = living::AlwaysOnlineState::Disabled;
                bool const alwaysKnown = sRandomPlayerbotMgr.TryGetEventValue(botGuid, "always", alwaysRaw)
                    && living::TryClassifyAlwaysOnline(alwaysRaw, alwaysState);
                bool const alwaysActive = alwaysKnown && alwaysState == living::AlwaysOnlineState::Active;
                if (living::MayReleasePostCreateOwner(alwaysKnown, alwaysActive, !postCreateSettled))
                {
                    botsToRemove.push_back({accountId, botGuid});
                }

                // The transient owner releases on its own condition: all
                // post-create work settled or explicitly quarantined,
                // regardless of always-online membership.
                if (postCreateSettled)
                    postCreateOwners.erase(botGuid);
            }
        }

        sPlayerbotAIConfig.freeAltBots.remove_if([&](const std::pair<uint32, uint32>& entry) {
            return std::find(botsToRemove.begin(), botsToRemove.end(), entry) != botsToRemove.end();
        });
    }
}

void RandomPlayerbotMgr::DelayedFacingFix()
{
    if (!sPlayerbotAIConfig.turnInRpg)
        return;

    for (auto& fMap : facingFix) {
        for (auto& fInstance : fMap.second) {
            for (auto obj : fInstance.second) {
                if (time(0) - obj.second > 5)
                {
                    if (!obj.first.IsCreature())
                        continue;

                    GuidPosition guidP(obj.first, WorldPosition(fMap.first, 0, 0, 0));

                    Creature* unit = guidP.GetCreature(fInstance.first);

                    if (!unit)
                        continue;

                    CreatureData* data = guidP.GetCreatureData();

                    if (!data)
                        continue;

                    if (unit->GetOrientation() == data->orientation)
                        continue;

                    unit->SetFacingTo(data->orientation);
                }
            }
        }
        facingFix[fMap.first].clear();
    }
}

void RandomPlayerbotMgr::DatabasePing(QueryResult* result, uint32 pingStart, std::string db)
{
    sRandomPlayerbotMgr.SetDatabaseDelay(db, sWorld.GetCurrentMSTime() - pingStart);
    delete result;
}

void RandomPlayerbotMgr::LoadNamedLocations()
{
    namedLocations.clear();

    auto result = WorldDatabase.Query("SELECT `name`, `map_id`, `position_x`, `position_y`, `position_z`, `orientation` FROM `ai_playerbot_named_location` WHERE `name` NOT LIKE 'FISH_LOCATION%'");

    if (!result)
    {
        sLog.outString(">> Loaded 0 named locations - table is empty!");
        sLog.outString();
        return;
    }

    uint32 count = 0;
    do
    {
        ++count;

        Field* fields = result->Fetch();

        std::string name = fields[0].GetCppString();
        uint32 mapId = fields[1].GetUInt32();
        float positionX = fields[2].GetFloat();
        float positionY = fields[3].GetFloat();
        float positionZ = fields[4].GetFloat();
        float orientation = fields[5].GetFloat();

        AddNamedLocation(name, WorldLocation(mapId, positionX, positionY, positionZ, orientation));
    } while (result->NextRow());

    sLog.outString(">> Loaded %u named locations", count);
    sLog.outString();
}

bool RandomPlayerbotMgr::AddNamedLocation(std::string const& name, WorldLocation const& location)
{
    if (namedLocations.find(name) != namedLocations.end())
    {
        sLog.outError("RandomPlayerbotMgr::AddNamedLocation: Failed to add named location '%s' - already exists!", name.c_str());
        return false;
    }

    namedLocations[name] = location;

    return true;
}

bool RandomPlayerbotMgr::GetNamedLocation(std::string const& name, WorldLocation& location)
{
    auto itr = namedLocations.find(name);
    if (itr == namedLocations.end())
    {
        sLog.outError("RandomPlayerbotMgr::GetNamedLocation: Named location '%s' not found! Please ensure that your ai_playerbot_named_location table is up to date.", name.c_str());
        return false;
    }

    location = itr->second;

    return true;
}

uint32 RandomPlayerbotMgr::AddRandomBots()
{
    // Typed read: an unknown bot_count defers additions instead of treating
    // the failed read as zero.
    uint32 maxAllowedBotCount = 0;
    if (!TryGetEventValue(0, "bot_count", maxAllowedBotCount))
        return currentBots.size();

    uint32 currentAllowedBotCount = maxAllowedBotCount;

    uint32 maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);
    float currentAvgLevel = 0, wantedAvgLevel = 0, randomAvgLevel = 0;

    if(sPlayerbotAIConfig.asyncBotLogin)
        return 0;

    // Reconcile the bot list from durable truth first if a prior batch left it
    // dirty; a new batch must not build on an untrusted vector.
    GetBots();
    if (currentBotsDirty)
        return currentBots.size();

    if (currentBots.size() < currentAllowedBotCount)
    {
        if (sPlayerbotAIConfig.syncLevelWithPlayers)
        {
            maxLevel = std::max(sPlayerbotAIConfig.randomBotMinLevel, std::min(playersLevel + sPlayerbotAIConfig.syncLevelMaxAbove, sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL)));

            wantedAvgLevel = maxLevel / 2;
            uint32 botsAmount = 0;
            ForEachPlayerbot([&](Player* bot)
            {
                currentAvgLevel += bot->GetLevel();
                botsAmount++;
            });
                

            if(currentAvgLevel)
            {
                currentAvgLevel = currentAvgLevel / botsAmount;
            }

            randomAvgLevel = (sPlayerbotAIConfig.randomBotMinLevel + std::max(sPlayerbotAIConfig.randomBotMinLevel, std::min(playersLevel+ sPlayerbotAIConfig.syncLevelMaxAbove, sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL)))) / 2;
        }

        currentAllowedBotCount -= currentBots.size();

        int32 neededAddBots = currentAllowedBotCount;

        currentAllowedBotCount = currentAllowedBotCount*2;      

        // Legacy global switch kept: other paths still rely on async execution.
        CharacterDatabase.AllowAsyncTransactions();

        // No transaction here: SetEventValue persists each event write through
        // its own execution-confirmed synchronous statement, and the pinned
        // cores assert on nested BeginTransaction. Selection state (currentBots,
        // quotas, counters) changes only after both writes for a bot succeeded.
        bool enoughBotsForCriteria = true;

        for (uint32 noCriteria = 0; noCriteria < 3; noCriteria++)
        {
            int32  classRaceAllowed[MAX_CLASSES][MAX_RACES] = { 0 };

            for (uint32 race = 1; race < MAX_RACES; ++race)
            {
                for (uint32 cls = 1; cls < MAX_CLASSES; ++cls)
                {
                    if (sPlayerbotAIConfig.useFixedClassRaceCounts)
                    {
                        classRaceAllowed[cls][race] = sPlayerbotAIConfig.fixedClassRaceCounts[{cls, race}];
                    }
                    else
                    {
                        if (sPlayerbotAIConfig.classRaceProbability[cls][race])
                            classRaceAllowed[cls][race] = ((sPlayerbotAIConfig.classRaceProbability[cls][race] * maxAllowedBotCount / sPlayerbotAIConfig.classRaceProbabilityTotal) + 1) * (noCriteria + 1);
                    }
                }
            }

            for (std::list<uint32>::iterator i = sPlayerbotAIConfig.randomBotAccounts.begin(); i != sPlayerbotAIConfig.randomBotAccounts.end(); i++)
            {
                uint32 accountId = *i;

                std::unique_ptr<QueryResult> result;

                if (noCriteria == 2)
                {
                    result = CharacterDatabase.PQuery("SELECT guid, level, totaltime, race, class FROM characters WHERE account = '%u'", accountId);
                }
                else
                {
                    bool needToIncrease = wantedAvgLevel && currentAvgLevel + 1 < wantedAvgLevel;
                    bool needToLower = wantedAvgLevel && currentAvgLevel > wantedAvgLevel + 1;
                    bool rndCanIncrease = !sPlayerbotAIConfig.disableRandomLevels && randomAvgLevel > currentAvgLevel;
                    bool rndCanLower = !sPlayerbotAIConfig.disableRandomLevels && randomAvgLevel < currentAvgLevel;

                    std::string query = "SELECT guid, level, totaltime, race, class FROM characters WHERE account = '%u' AND level <= %u";
                    std::string wasRand = sPlayerbotAIConfig.instantRandomize ? "totaltime" : "(level > 1)";

                    if (needToIncrease) //We need more higher level bots.
                    {
                        query += " AND (level > %u";
                        if (rndCanIncrease) //Log in higher level bots or bots that will be randomized.
                            query += " OR !" + wasRand;
                        query += ")";

                        result = CharacterDatabase.PQuery(query.c_str(), accountId, maxLevel, (uint32)wantedAvgLevel);
                    }
                    else
                    {
                        if (needToLower && !rndCanLower) //Do not load unrandomized if it'll only increase level.
                            query += " AND " + wasRand;

                        result = CharacterDatabase.PQuery(query.c_str(), accountId, maxLevel);
                    }
                }

                if (!result)
                    continue;

                do
                {
                    Field* fields = result->Fetch();
                    uint32 guid = fields[0].GetUInt32();
                    uint32 level = fields[1].GetUInt32();
                    uint32 totaltime = fields[2].GetUInt32();
                    uint32 race = fields[3].GetUInt32();
                    uint32 cls = fields[4].GetUInt32();

                    // Typed reads: unknown activation state neither skips nor
                    // activates this candidate - it ABORTS the whole batch.
                    // The database is unavailable, every further probe is a
                    // synchronous world-thread query, and (worse) a read that
                    // fails here but recovers before the write below could
                    // reactivate a durably logged-out bot.
                    uint32 addActive = 0, logoutActive = 0;
                    if (!TryReadRequiredEvents(guid, { {"add", &addActive}, {"logout", &logoutActive} }))
                    {
                        sLog.outError("AddRandomBots: activation state for bot %u unavailable; stopping the batch", guid);
                        return currentBots.size();
                    }

                    if (addActive)
                    {
                        if (!noCriteria)
                            classRaceAllowed[cls][race]--;
                        continue;
                    }

                    if (logoutActive)
                        continue;

                    if (GetPlayerBot(guid))
                    {
                        if (!noCriteria)
                            classRaceAllowed[cls][race]--;
                        continue;
                    }

                    if (std::find(currentBots.begin(), currentBots.end(), guid) != currentBots.end())
                    {
                        if (!noCriteria)
                            classRaceAllowed[cls][race]--;
                        continue;
                    }

                    if (classRaceAllowed[cls][race] <= 0)
                        continue;

                    // Persist the add/logout pair BEFORE any cache/quota/counter
                    // update through the activation-plan executor: the pair is
                    // ONE logical activation, the priors are KNOWN (typed reads
                    // above: both zero), a failed write compensates the written
                    // prefix, and an uncertain compensation dirties currentBots
                    // and stops the batch.
                    std::vector<living::PlannedEventWrite> activationPlan = {
                        { "add", 1, urand(sPlayerbotAIConfig.minRandomBotInWorldTime, sPlayerbotAIConfig.maxRandomBotInWorldTime), 0, 0 },
                        { "logout", 0, 0, 0, 0 },
                    };

                    if (!RunActivationPlan(guid, activationPlan))
                    {
                        if (currentBotsDirty)
                        {
                            // Uncertain compensation: the in-memory vector can
                            // no longer be trusted, and continuing the batch
                            // would compound the divergence.
                            sLog.outError("AddRandomBots: durable activation state for bot %u unknown; stopping the batch", guid);
                            return currentBots.size();
                        }

                        // Compensated back to the known inactive priors: this
                        // candidate is skipped, the batch may continue.
                        continue;
                    }

                    currentBots.push_back(guid);

                    if(!noCriteria)
                        classRaceAllowed[cls][race]--;

                    if (wantedAvgLevel)
                    {
                        if (sPlayerbotAIConfig.instantRandomize ? totaltime : level > 1)
                            currentAvgLevel += (float)level / currentBots.size();
                        else
                            currentAvgLevel += (float)level + randomAvgLevel; //Use predicted randomized level. This will be wrong but avarage out correct.
                    }

                    currentAllowedBotCount--;
                    neededAddBots--;

                    if (!currentAllowedBotCount)
                        break;

                } while (result->NextRow());

                if (!currentAllowedBotCount)
                    break;
            }

            if (!currentAllowedBotCount)
                break;

            if (showLoginWarning && neededAddBots > 0)
            {
                sLog.outError("Not enough accounts to meet selection criteria. A random selection of bots was activated to fill the server.");

                if (sPlayerbotAIConfig.syncLevelWithPlayers)
                    sLog.outError("Only bots between level %d and %d are selected to sync with player level", uint32((currentAvgLevel + 1 < wantedAvgLevel) ? wantedAvgLevel : 1), maxLevel);

                ChatHelper chat(nullptr);

                for (uint32 race = 1; race < MAX_RACES; ++race)
                {
                    for (uint32 cls = 1; cls < MAX_CLASSES; ++cls)
                    {

                            int32 moreWanted = classRaceAllowed[cls][race];
                            if (moreWanted > 0)
                            {
                                if (sPlayerbotAIConfig.useFixedClassRaceCounts)
                                {
                                    int32 totalWanted = sPlayerbotAIConfig.fixedClassRaceCounts[{cls, race}];
                                    sLog.outError("%d %s %ss needed but only %d found.", totalWanted, chat.formatRace(race).c_str(), chat.formatClass(cls).c_str(), totalWanted - moreWanted);
                                }
                                else
                                {
                                    int32 totalWanted = ((sPlayerbotAIConfig.classRaceProbability[cls][race] * maxAllowedBotCount / sPlayerbotAIConfig.classRaceProbabilityTotal) + 1);
                                    float percentage = float(sPlayerbotAIConfig.classRaceProbability[cls][race]) * 100.0f / sPlayerbotAIConfig.classRaceProbabilityTotal;
                                    sLog.outError("%d %s %ss needed to get %3.2f%% of total but only %d found.", totalWanted, chat.formatRace(race).c_str(), chat.formatClass(cls).c_str(), percentage, totalWanted - moreWanted);
                                }
                            }
                        
                    }
                }

                showLoginWarning = false;
            }
        }

        if (currentAllowedBotCount)
            currentAllowedBotCount = std::max(int64(GetEventValue(0, "bot_count")) - int64(currentBots.size()), int64(0));

        if(currentAllowedBotCount && sPlayerbotAIConfig.randomBotAutoCreate && !sPlayerbotAIConfig.useFixedClassRaceCounts)
#ifdef MANGOSBOT_TWO
            sLog.outError("Not enough random bot accounts available. Need %d more!!", (uint32)ceil(currentAllowedBotCount / 10));
#else
            sLog.outError("Not enough random bot accounts available. Need %d more!!", (uint32)ceil(currentAllowedBotCount / 9));
#endif
      
    }

    return currentBots.size();
}

void RandomPlayerbotMgr::LoadBattleMastersCache()
{
    BattleMastersCache.clear();

    sLog.outString("---------------------------------------");
    sLog.outString("          Loading BattleMasters Cache  ");
    sLog.outString("---------------------------------------");
    sLog.outString();

    auto result = WorldDatabase.Query("SELECT `entry`,`bg_template` FROM `battlemaster_entry`");

    uint32 count = 0;

    if (!result)
    {
        sLog.outString(">> Loaded 0 battlemaster entries - table is empty!");
        sLog.outString();
        return;
    }

    do
    {
        ++count;

        Field* fields = result->Fetch();

        uint32 entry = fields[0].GetUInt32();
        uint32 bgTypeId = fields[1].GetUInt32();

        CreatureInfo const* bmaster = sObjectMgr.GetCreatureTemplate(entry);
        if (!bmaster)
            continue;

#ifdef MANGOS
        FactionTemplateEntry const* bmFaction = sFactionTemplateStore.LookupEntry(bmaster->FactionAlliance);
#endif
#ifdef CMANGOS
        FactionTemplateEntry const* bmFaction = sFactionTemplateStore.LookupEntry(bmaster->Faction);
#endif
        uint32 bmFactionId = bmFaction->faction;
#ifdef MANGOS
        FactionEntry const* bmParentFaction = sFactionStore.LookupEntry(bmFactionId);
#endif
#ifdef CMANGOS
#ifdef MANGOSBOT_ONE
        FactionEntry const* bmParentFaction = sFactionStore.LookupEntry<FactionEntry>(bmFactionId);
#else
        FactionEntry const* bmParentFaction = sFactionStore.LookupEntry(bmFactionId);
#endif
#endif
        uint32 bmParentTeam = bmParentFaction->team;
        Team bmTeam = TEAM_BOTH_ALLOWED;
        if (bmParentTeam == 891)
            bmTeam = ALLIANCE;
        if (bmFactionId == 189)
            bmTeam = ALLIANCE;
        if (bmParentTeam == 892)
            bmTeam = HORDE;
        if (bmFactionId == 66)
            bmTeam = HORDE;

        BattleMastersCache[bmTeam][BattleGroundTypeId(bgTypeId)].insert(BattleMastersCache[bmTeam][BattleGroundTypeId(bgTypeId)].end(), entry);
        sLog.outDetail("Cached Battmemaster #%d for BG Type %d (%s)", entry, bgTypeId, bmTeam == ALLIANCE ? "Alliance" : bmTeam == HORDE ? "Horde" : "Neutral");

    } while (result->NextRow());

    sLog.outString(">> Loaded %u battlemaster entries", count);
    sLog.outString();
}

void RandomPlayerbotMgr::CheckBgQueue()
{
    if (!BgCheckTimer)
        BgCheckTimer = time(nullptr);

    if (time(nullptr) < (BgCheckTimer + 30))
    {
        return;
    }
    else
    {
        BgCheckTimer = time(nullptr);
    }

    sLog.outDetail("Checking BG Queue...");

    for (int i = BG_BRACKET_ID_FIRST; i < MAX_BATTLEGROUND_BRACKETS; ++i)
    {
        for (int j = BATTLEGROUND_QUEUE_AV; j < MAX_BATTLEGROUND_QUEUE_TYPES; ++j)
        {
            BgPlayers[j][i][0] = 0;
            BgPlayers[j][i][1] = 0;
            BgBots[j][i][0] = 0;
            BgBots[j][i][1] = 0;
            ArenaBots[j][i][0][0] = 0;
            ArenaBots[j][i][0][1] = 0;
            ArenaBots[j][i][1][0] = 0;
            ArenaBots[j][i][1][1] = 0;
            NeedBots[j][i][0] = false;
            NeedBots[j][i][1] = false;
        }
    }

    for (auto i : players)
    {
        Player* player = i.second;

        if (!player || !player->IsInWorld())
            continue;

        if (!player->InBattleGroundQueue())
            continue;

        if (player->InBattleGround() && player->GetBattleGround()->GetStatus() == STATUS_WAIT_LEAVE)
            continue;

        for (int i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
        {
            BattleGroundQueueTypeId queueTypeId = player->GetBattleGroundQueueTypeId(i);
            if (queueTypeId == BATTLEGROUND_QUEUE_NONE)
                continue;

            uint32 TeamId = player->GetTeam() == ALLIANCE ? 0 : 1;

            BattleGroundTypeId bgTypeId = sServerFacade.BgTemplateId(queueTypeId);
#ifndef MANGOSBOT_TWO
            BattleGroundBracketId bracketId = sBattleGroundMgr.GetBattleGroundBracketIdFromLevel(bgTypeId, player->GetLevel());
#endif
#ifdef MANGOSBOT_TWO
            BattleGround* bg = sBattleGroundMgr.GetBattleGroundTemplate(bgTypeId);
            uint32 mapId = bg->GetMapId();
            PvPDifficultyEntry const* pvpDiff = GetBattlegroundBracketByLevel(mapId, player->GetLevel());
            if (!pvpDiff)
                continue;

            BattleGroundBracketId bracketId = pvpDiff->GetBracketId();
#endif
#ifdef MANGOSBOT_TWO
            /* to fix
            if (ArenaType arenaType = sServerFacade.BgArenaType(queueTypeId))
            {
                BattleGroundQueue& bgQueue = sServerFacade.bgQueue(queueTypeId);
                GroupQueueInfo ginfo;
                uint32 tempT = TeamId;

                if (bgQueue.GetPlayerGroupInfoData(player->GetObjectGuid(), &ginfo))
                {
                    if (ginfo.isRated)
                    {
                        for (uint32 arena_slot = 0; arena_slot < MAX_ARENA_SLOT; ++arena_slot)
                        {
                            uint32 arena_team_id = player->GetArenaTeamId(arena_slot);
                            ArenaTeam* arenateam = sObjectMgr.GetArenaTeamById(arena_team_id);
                            if (!arenateam)
                                continue;
                            if (arenateam->GetType() != arenaType)
                                continue;

                            Rating[queueTypeId][bracketId][1] = arenateam->GetRating();
                        }
                    }
                    TeamId = ginfo.isRated ? 1 : 0;
                }
                if (player->InArena())
                {
                    if (player->GetBattleGround()->IsRated())
                        TeamId = 1;
                    else
                        TeamId = 0;
                }
                ArenaBots[queueTypeId][bracketId][TeamId][tempT]++;
            }
         */
#endif
#ifdef MANGOSBOT_ONE
            if (ArenaType arenaType = sServerFacade.BgArenaType(queueTypeId))
            {
                sWorld.GetBGQueue().GetMessager().AddMessage([queueTypeId, playerId = player->GetObjectGuid(), arenaType = arenaType, bracketId = bracketId, tempT = TeamId](BattleGroundQueue* bgQueue)
                    {
                        uint32 TeamId;
                        GroupQueueInfo ginfo;

                        BattleGroundQueueItem* queueItem = &bgQueue->GetBattleGroundQueue(queueTypeId);
                        Player *player = RandomPlayerbotMgr::instance().GetPlayer(playerId);

                        if (!player)
                            return;

                        if (queueItem->GetPlayerGroupInfoData(player->GetObjectGuid(), &ginfo))
                        {
                            if (ginfo.isRated)
                            {
                                for (uint32 arena_slot = 0; arena_slot < MAX_ARENA_SLOT; ++arena_slot)
                                {
                                    uint32 arena_team_id = player->GetArenaTeamId(arena_slot);
                                    ArenaTeam* arenateam = sObjectMgr.GetArenaTeamById(arena_team_id);
                                    if (!arenateam)
                                        continue;
                                    if (arenateam->GetType() != arenaType)
                                        continue;

                                    sRandomPlayerbotMgr.Rating[queueTypeId][bracketId][1] = arenateam->GetRating();
                                }
                            }
                            TeamId = ginfo.isRated ? 1 : 0;
                        }
                        if (player->InArena())
                        {
                            if (player->GetBattleGround()->IsRated()/* && (ginfo.isRated && ginfo.arenaTeamId && ginfo.arenaTeamRating && ginfo.opponentsTeamRating)*/)
                                TeamId = 1;
                            else
                                TeamId = 0;
                        }
                        sRandomPlayerbotMgr.ArenaBots[queueTypeId][bracketId][TeamId][tempT]++;

                    }
                );
            }
#endif
            if (player->GetPlayerbotAI())
                BgBots[queueTypeId][bracketId][TeamId]++;
            else
                BgPlayers[queueTypeId][bracketId][TeamId]++;

            if (!player->IsInvitedForBattleGroundQueueType(queueTypeId) && (!player->InBattleGround() || player->GetBattleGround()->GetTypeId() != sServerFacade.BgTemplateId(queueTypeId)))
            {
#ifndef MANGOSBOT_ZERO
                if (ArenaType arenaType = sServerFacade.BgArenaType(queueTypeId))
                {
                    NeedBots[queueTypeId][bracketId][TeamId] = true;
                }
                else
                {
                    NeedBots[queueTypeId][bracketId][0] = true;
                    NeedBots[queueTypeId][bracketId][1] = true;
                }
#else
                NeedBots[queueTypeId][bracketId][0] = true;
                NeedBots[queueTypeId][bracketId][1] = true;
#endif
            }
        }
    }

    ForEachPlayerbot([&](Player* bot)
    {
        if (!bot || !bot->IsInWorld())
            return;

        if (!bot->InBattleGroundQueue())
            return;

        if (!IsFreeBot(bot))
            return;

        if (bot->InBattleGround() && bot->GetBattleGround() && bot->GetBattleGround()->GetStatus() == STATUS_WAIT_LEAVE)
            return;

        for (int i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
        {
            BattleGroundQueueTypeId queueTypeId = bot->GetBattleGroundQueueTypeId(i);
            if (queueTypeId == BATTLEGROUND_QUEUE_NONE)
                continue;

            uint32 TeamId = bot->GetTeam() == ALLIANCE ? 0 : 1;

            BattleGroundTypeId bgTypeId = sServerFacade.BgTemplateId(queueTypeId);

#ifndef MANGOSBOT_TWO
            BattleGroundBracketId bracketId = sBattleGroundMgr.GetBattleGroundBracketIdFromLevel(bgTypeId, bot->GetLevel());;
#endif
#ifdef MANGOSBOT_TWO
            BattleGround* bg = sBattleGroundMgr.GetBattleGroundTemplate(bgTypeId);
            uint32 mapId = bg->GetMapId();
            PvPDifficultyEntry const* pvpDiff = GetBattlegroundBracketByLevel(mapId, bot->GetLevel());
            if (!pvpDiff)
                continue;

            BattleGroundBracketId bracketId = pvpDiff->GetBracketId();
#endif
#ifdef MANGOSBOT_TWO
            /* to fix
            ArenaType arenaType = sServerFacade.BgArenaType(queueTypeId);
            if (arenaType != ARENA_TYPE_NONE)
            {
                BattleGroundQueue& bgQueue = sServerFacade.bgQueue(queueTypeId);
                GroupQueueInfo ginfo;
                uint32 tempT = TeamId;
                if (bgQueue.GetPlayerGroupInfoData(bot->GetObjectGuid(), &ginfo))
                {
                    TeamId = ginfo.isRated ? 1 : 0;
                }
                if (bot->InArena())
                {
                    if (bot->GetBattleGround()->IsRated())
                        TeamId = 1;
                    else
                        TeamId = 0;
                }
                ArenaBots[queueTypeId][bracketId][TeamId][tempT]++;
            }
        */
#endif
#ifdef MANGOSBOT_ONE
            ArenaType arenaType = sServerFacade.BgArenaType(queueTypeId);
            if (arenaType != ARENA_TYPE_NONE)
            {
                sWorld.GetBGQueue().GetMessager().AddMessage([queueTypeId, botId = bot->GetObjectGuid(), arenaType = arenaType, bracketId = bracketId, tempT = TeamId](BattleGroundQueue* bgQueue)
                    {
                        uint32 TeamId;
                        GroupQueueInfo ginfo;

                        BattleGroundQueueItem* queueItem = &bgQueue->GetBattleGroundQueue(queueTypeId);
                        Player *bot = RandomPlayerbotMgr::instance().GetPlayer(botId);
                        if (!bot)
                            return;

                        if (queueItem->GetPlayerGroupInfoData(bot->GetObjectGuid(), &ginfo))
                        {
                            TeamId = ginfo.isRated ? 1 : 0;
                        }
                        if (bot->InArena())
                        {
                            if (bot->GetBattleGround()->IsRated()/* && (ginfo.isRated && ginfo.arenaTeamId && ginfo.arenaTeamRating && ginfo.opponentsTeamRating)*/)
                                TeamId = 1;
                            else
                                TeamId = 0;
                        }

                        

                        sRandomPlayerbotMgr.ArenaBots[queueTypeId][bracketId][TeamId][tempT]++;

                    }
                );
            }
#endif
            BgBots[queueTypeId][bracketId][TeamId]++;
        }
    });

    for (int i = BG_BRACKET_ID_FIRST; i < MAX_BATTLEGROUND_BRACKETS; ++i)
    {
        for (int j = BATTLEGROUND_QUEUE_AV; j < MAX_BATTLEGROUND_QUEUE_TYPES; ++j)
        {
            BattleGroundQueueTypeId queueTypeId = BattleGroundQueueTypeId(j);

            if ((BgPlayers[j][i][0] + BgBots[j][i][0] + BgPlayers[j][i][1] + BgBots[j][i][1]) == 0)
                continue;

#ifndef MANGOSBOT_ZERO
            if (ArenaType type = sServerFacade.BgArenaType(queueTypeId))
            {
                sLog.outDetail("ARENA:%s %s: Plr (Skirmish:%d, Rated:%d) Bot (Skirmish:%d, Rated:%d) Total (Skirmish:%d Rated:%d)",
                    type == ARENA_TYPE_2v2 ? "2v2" : type == ARENA_TYPE_3v3 ? "3v3" : "5v5",
                    i == 0 ? "10-19" : i == 1 ? "20-29" : i == 2 ? "30-39" : i == 3 ? "40-49" : i == 4 ? "50-59" : (i == 5 && MAX_BATTLEGROUND_BRACKETS == 6) ? "60" : (i == 5 && MAX_BATTLEGROUND_BRACKETS == 7) ? "60-69" : i == 6 ? (i == 6 && MAX_BATTLEGROUND_BRACKETS == 16) ? "70-79" : "70" : "80",
                    BgPlayers[j][i][0],
                    BgPlayers[j][i][1],
                    BgBots[j][i][0],
                    BgBots[j][i][1],
                    BgPlayers[j][i][0] + BgBots[j][i][0],
                    BgPlayers[j][i][1] + BgBots[j][i][1]
                );
                continue;
            }
#endif
            BattleGroundTypeId bgTypeId = sServerFacade.BgTemplateId(queueTypeId);
            std::string _bgType;
            switch (bgTypeId)
            {
            case BATTLEGROUND_AV:
                _bgType = "AV";
                break;
            case BATTLEGROUND_WS:
                _bgType = "WSG";
                break;
            case BATTLEGROUND_AB:
                _bgType = "AB";
                break;
#ifndef MANGOSBOT_ZERO
            case BATTLEGROUND_EY:
                _bgType = "EotS";
                break;
#endif
#ifdef MANGOSBOT_TWO
            case BATTLEGROUND_RB:
                _bgType = "Random";
                break;
            case BATTLEGROUND_SA:
                _bgType = "SotA";
                break;
            case BATTLEGROUND_IC:
                _bgType = "IoC";
                break;
#endif
            default:
                _bgType = "Other";
                break;
            }
            sLog.outDetail("BG:%s %s: Plr (%d:%d) Bot (%d:%d) Total (A:%d H:%d)",
                _bgType.c_str(),
                i == 0 ? "10-19" : i == 1 ? "20-29" : i == 2 ? "30-39" : i == 3 ? "40-49" : i == 4 ? "50-59" : (i == 5 && MAX_BATTLEGROUND_BRACKETS == 6) ? "60" : (i == 5 && MAX_BATTLEGROUND_BRACKETS == 7) ? "60-69" : i == 6 ? (i == 6 && MAX_BATTLEGROUND_BRACKETS == 16) ? "70-79" : "70" : "80",
                BgPlayers[j][i][0],
                BgPlayers[j][i][1],
                BgBots[j][i][0],
                BgBots[j][i][1],
                BgPlayers[j][i][0] + BgBots[j][i][0],
                BgPlayers[j][i][1] + BgBots[j][i][1]
            );
        }
    }

    sLog.outDetail("BG Queue check finished");
    return;
}

void RandomPlayerbotMgr::CheckLfgQueue()
{
    if (!LfgCheckTimer || time(NULL) > (LfgCheckTimer + 30))
        LfgCheckTimer = time(NULL);

    if (sPlayerbotAIConfig.logRandomBotJoinLfg)
    {
        sLog.outDetail("Checking LFG Queue...");
    }

    // Clear LFG list
    LfgDungeons[HORDE].clear();
    LfgDungeons[ALLIANCE].clear();

    for (auto i : players)
    {
        Player* player = i.second;

        if (!player || !player->IsInWorld())
            continue;

        bool isLFG = false;

#ifdef MANGOSBOT_ZERO
        WorldSafeLocsEntry const* ClosestGrave = player->GetMap()->GetGraveyardManager().GetClosestGraveYard(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetMapId(), player->GetTeam());
        uint32 zoneId = ClosestGrave ? ClosestGrave->ID : 0;

        Group* group = player->GetGroup();
        if (group)
        {
            if (sWorld.GetLFGQueue().IsGroupInQueue(group->GetId()))
            {
                isLFG = true;
                LFGGroupQueueInfo lfgInfo;
                sWorld.GetLFGQueue().GetGroupQueueInfo(&lfgInfo, group->GetId());
                uint32 lfgType = (zoneId << 16) | lfgInfo.areaId;
                LfgDungeons[player->GetTeam()].push_back(lfgType);
            }
        }
        else
        {
            if (sWorld.GetLFGQueue().IsPlayerInQueue(player->GetObjectGuid()))
            {
                isLFG = true;
                LFGPlayerQueueInfo lfgInfo;
                sWorld.GetLFGQueue().GetPlayerQueueInfo(&lfgInfo, player->GetObjectGuid());
                uint32 lfgType = (zoneId << 16) | lfgInfo.areaId;
                LfgDungeons[player->GetTeam()].push_back(lfgType);
            }
        }
#endif

#ifdef MANGOSBOT_ONE
        /* todo: Fix with new system
        WorldSafeLocsEntry const* ClosestGrave = player->GetMap()->GetGraveyardManager().GetClosestGraveYard(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetMapId(), player->GetTeam());
        uint32 zoneId = ClosestGrave ? ClosestGrave->ID : 0;

        Group* group = player->GetGroup();
        if (group && !group->IsFull())
        {
            if (group->IsLeader(player->GetObjectGuid()))
            {
                if (player->GetSession()->m_lfgInfo.queued && player->GetSession()->LookingForGroup_auto_add && player->m_lookingForGroup.more.isAuto())
                {
                    uint32 lfgType = (zoneId << 16) | ((1 << 8) | uint8(player->m_lookingForGroup.more.entry));
                    LfgDungeons[player->GetTeam()].push_back(lfgType);
                    isLFG = true;
                }
            }
        }
        else if (!group)
        {
            for (int i = 0; i < MAX_LOOKING_FOR_GROUP_SLOT; ++i)
                if (!player->m_lookingForGroup.group[i].empty() && player->GetSession()->LookingForGroup_auto_join && player->m_lookingForGroup.group[i].isAuto())
                {
                    isLFG = true;
                    uint32 lfgType = (zoneId << 16) | ((0 << 8) | uint8(player->m_lookingForGroup.group[i].entry));
                    LfgDungeons[player->GetTeam()].push_back(lfgType);
                }

            if (!player->m_lookingForGroup.more.empty() && player->GetSession()->LookingForGroup_auto_add && player->m_lookingForGroup.more.isAuto())
            {
                uint32 lfgType = (zoneId << 16) | ((1 << 8) | uint8(player->m_lookingForGroup.more.entry));
                LfgDungeons[player->GetTeam()].push_back(lfgType);
                isLFG = true;
            }
        }
        */
#endif

#ifdef MANGOSBOT_TWO
        Group* group = player->GetGroup();
        if (group)
        {
            if (group->IsLFGGroup())
            {
                isLFG = true;
                LFGQueueData& lfgData = sWorld.GetLFGQueue().GetQueueData(group->GetObjectGuid());
                if (lfgData.GetState() != LFG_STATE_NONE && lfgData.GetState() < LFG_STATE_DUNGEON)
                {
                    LfgDungeonSet dList = lfgData.GetDungeons();
                    for (auto dungeon : dList)
                    {
                        LfgDungeons[player->GetTeam()].push_back(dungeon);
                    }
                }
            }
        }
        else
        {
            if (player->GetLfgData().GetState() != LFG_STATE_NONE)
            {
                LFGQueueData& lfgData = sWorld.GetLFGQueue().GetQueueData(player->GetObjectGuid());
                isLFG = true;
                if (lfgData.GetState() < LFG_STATE_DUNGEON)
                {
                    LfgDungeonSet dList = lfgData.GetDungeons();
                    for (auto dungeon : dList)
                    {
                        LfgDungeons[player->GetTeam()].push_back(dungeon);
                    }
                }
            }
        }
#endif
    }

#ifdef MANGOSBOT_ONE
    /* todo: Fix with new system
    ForEachPlayerbot([&](Player* bot)
    {
        if (!bot || !bot->IsInWorld())
            return;

        if (LfgDungeons[bot->GetTeam()].empty())
            return;

        WorldSafeLocsEntry const* ClosestGrave = bot->GetMap()->GetGraveyardManager().GetClosestGraveYard(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId(), bot->GetTeam());
        uint32 zoneId = ClosestGrave ? ClosestGrave->ID : 0;

        Group* group = bot->GetGroup();
        if (group && !group->IsFull())
        {
            if (group->IsLeader(bot->GetObjectGuid()))
            {
                if (bot->GetSession()->m_lfgInfo.queued && bot->GetSession()->m_lfgInfo.autofill)
                {
                    uint32 lfgType = (zoneId << 16) | ((1 << 8) | uint8(bot->m_lookingForGroup.more.entry));
                    LfgDungeons[bot->GetTeam()].push_back(lfgType);
                }
            }
        }
        else if (!group)
        {
            if (!bot->m_lookingForGroup.more.empty() && bot->GetSession()->LookingForGroup_auto_add && bot->m_lookingForGroup.more.isAuto())
            {
                uint32 lfgType = (zoneId << 16) | ((1 << 8) | uint8(bot->m_lookingForGroup.more.entry));
                LfgDungeons[bot->GetTeam()].push_back(lfgType);
            }
        }
    });
    */
#endif

    if (sPlayerbotAIConfig.logRandomBotJoinLfg)
    {
       if (LfgDungeons[ALLIANCE].size() || LfgDungeons[HORDE].size())
            sLog.outDetail("LFG Queue check finished. There are real players in queue.");
       else
           sLog.outDetail("LFG Queue check finished. No real players in queue.");
    }
    return;
}

void RandomPlayerbotMgr::AddOfflineGroupBots()
{
    if (!OfflineGroupBotsTimer || time(NULL) > (OfflineGroupBotsTimer + 5))
        OfflineGroupBotsTimer = time(NULL);

    uint32 totalCounter = 0;
    for (const auto& i : players)
    {
        Player* player = i.second;

        if (!player || !player->IsInWorld() || !player->GetGroup())
            continue;

        Group* group = player->GetGroup();
        if (group && group->IsLeader(player->GetObjectGuid()))
        {
            std::vector<uint32> botsToAdd;
            Group::MemberSlotList const& slots = group->GetMemberSlots();
            for (Group::MemberSlotList::const_iterator i = slots.begin(); i != slots.end(); ++i)
            {
                ObjectGuid member = i->guid;
                if (member == player->GetObjectGuid())
                    continue;

                if (!IsFreeBot(member.GetCounter()))
                    continue;

                if (sObjectMgr.GetPlayer(member))
                    continue;

                if (GetPlayerBot(member))
                    continue;

                botsToAdd.push_back(member.GetCounter());
            }

            if (botsToAdd.empty())
                return;

            uint32 maxToAdd = urand(1, 5);
            uint32 counter = 0;
            for (auto& guid : botsToAdd)
            {
                if (counter >= maxToAdd)
                    break;

                if (sPlayerbotAIConfig.IsFreeAltBot(guid))
                {
                    for (auto& bot : sPlayerbotAIConfig.freeAltBots)
                    {
                        if (bot.second == guid)
                        {
                            Player* player = GetPlayerBot(bot.second);
                            if (!player && !PlayerbotHolder::IsDeletionPending(bot.second)
                                && !IsLifecycleLoginBlocked(bot.second))
                            {
                                AddPlayerBot(bot.second, bot.first);
                            }
                        }
                    }
                }
                else
                    AddRandomBot(guid);

                counter++;
                totalCounter++;
            }
        }
    }

    if (totalCounter)
        sLog.outDetail("Added %u offline bots from groups", totalCounter);
}

Item* RandomPlayerbotMgr::CreateTempItem(uint32 item, uint32 count, Player const* player, uint32 randomPropertyId)
{
    if (count < 1)
        return nullptr;                                        // don't create item at zero count

    if (ItemPrototype const* pProto = ObjectMgr::GetItemPrototype(item))
    {
        if (count > pProto->GetMaxStackSize())
            count = pProto->GetMaxStackSize();

        MANGOS_ASSERT(count != 0 && "pProto->Stackable == 0 but checked at loading already");

        Item* pItem = NewItemOrBag(pProto);
        if (pItem->Create(0, item, player))
        {
            pItem->SetCount(count);
            if (int32 randId = randomPropertyId ? randomPropertyId : Item::GenerateItemRandomPropertyId(item))
                pItem->SetItemRandomProperties(randId);

            return pItem;
        }
        delete pItem;
    }
    return nullptr;
}

InventoryResult RandomPlayerbotMgr::CanEquipUnseenItem(Player* player, uint8 slot, uint16& dest, uint32 item)
{
    dest = 0;
    Item* pItem = RandomPlayerbotMgr::CreateTempItem(item, 1, player);

    if (pItem)
    {
        InventoryResult result = player->CanEquipItem(slot, dest, pItem, true, false);

        pItem->RemoveFromUpdateQueueOf(player);

        if (!player->GetItemUpdateQueue().empty() && !player->GetItemUpdateQueue().back()) //Prevent queue overflow.
            player->GetItemUpdateQueue().pop_back();

        delete pItem;
        return result;
    }

    return EQUIP_ERR_ITEM_NOT_FOUND;
}

void RandomPlayerbotMgr::SaveCurTime()
{
    if (!EventTimeSyncTimer || time(NULL) > (EventTimeSyncTimer + 60))
        EventTimeSyncTimer = time(NULL);

    SetValue(uint32(0), "current_time", uint32(time(nullptr)));
}

bool RandomPlayerbotMgr::SyncEventTimers()
{
    // Fixed across retries: bootstrap may continue after the first failed
    // read/write and create fresh event rows. Those rows must never receive
    // the preceding server-downtime shift on a later retry.
    if (!eventTimerSyncCutoff)
        eventTimerSyncCutoff = static_cast<uint32>(time(nullptr));

    uint32 oldTime = 0;
    if (!TryGetEventValue(0, "current_time", oldTime))
        return false;

    if (!oldTime || oldTime >= eventTimerSyncCutoff)
        return true;

    // Advance the restart sentinel in the SAME atomic statement as every
    // shifted deadline. If execution is reported uncertain but actually
    // landed, the retry reloads the new sentinel and cannot double-shift.
    bool const synchronized = CharacterDatabase.DirectPExecute(
        "UPDATE ai_playerbot_random_bots SET `time` = CASE "
        "WHEN bot = 0 AND event = 'current_time' THEN %u ELSE `time` + %u END, "
        "`value` = CASE WHEN bot = 0 AND event = 'current_time' THEN %u ELSE `value` END "
        "WHERE owner = 0 AND ((bot <> 0 AND `time` < %u) OR (bot = 0 AND event = 'current_time'))",
        eventTimerSyncCutoff, eventTimerSyncCutoff - oldTime,
        eventTimerSyncCutoff, eventTimerSyncCutoff);

    // Any cache loaded before this boundary describes pre-shift timestamps.
    // Force the next typed read to reload durable truth on both success and
    // an uncertain result (whose statement may still have landed).
    eventCache.clear();
    eventCacheLoadState.clear();
    return synchronized;
}

void RandomPlayerbotMgr::CheckPlayers()
{
    if (!PlayersCheckTimer || time(NULL) > (PlayersCheckTimer + 60))
        PlayersCheckTimer = time(NULL);

    sLog.outDetail("Checking Players...");

    uint32 newPlayersLevel = 0;

    for (auto i : players)
    {
        Player* player = i.second;

        if (player->IsGameMaster())
            continue;

        //if (player->GetSession()->GetSecurity() > SEC_PLAYER)
        //    continue;

        if (player->GetLevel() > newPlayersLevel)
            newPlayersLevel = player->GetLevel();
    }

    if(playersLevel!= newPlayersLevel)
        sLog.outDetail("Max player level is %d, max bot level changed from %d to %d", newPlayersLevel, playersLevel, newPlayersLevel);
    else
        sLog.outDetail("Max player level is %d, max bot level set to %d", newPlayersLevel, newPlayersLevel);

    playersLevel = newPlayersLevel;

    return;
}

bool RandomPlayerbotMgr::ScheduleRandomize(uint32 bot, uint32 time)
{
    return SetEventValue(bot, "randomize", 1, time);
}

bool RandomPlayerbotMgr::ScheduleTeleport(uint32 bot, uint32 time)
{
    if (!time)
        time = 60 + urand(sPlayerbotAIConfig.randomBotTeleportMinInterval, sPlayerbotAIConfig.randomBotTeleportMaxInterval);
    return SetEventValue(bot, "teleport", 1, time);
}

bool RandomPlayerbotMgr::ScheduleChangeStrategy(uint32 bot, uint32 time)
{
    if (!time)
        time = urand(sPlayerbotAIConfig.minRandomBotChangeStrategyTime, sPlayerbotAIConfig.maxRandomBotChangeStrategyTime);
    return SetEventValue(bot, "change_strategy", 1, time);
}

bool RandomPlayerbotMgr::AddRandomBot(uint32 bot)
{
    if (IsLifecycleLoginBlocked(bot))
        return false;

    Player* player = GetPlayerBot(bot);
    if (player)
        return true;

    uint32 accountId = sObjectMgr.GetPlayerAccountIdByGUID(ObjectGuid(HIGHGUID_PLAYER, bot));

    if (!sPlayerbotAIConfig.IsInRandomAccountList(accountId))
    {
        sLog.outError("Bot #%d login fail: Not random bot!", bot);
        return false;
    }

    // Typed reads for the login gate AND every prior value the activation
    // plan may need to restore. Unknown state starts nothing and writes
    // nothing - a transient load failure must not trigger a (possibly
    // duplicate) login.
    uint32 loginValue = 0, priorAdd = 0, priorLogout = 0, priorUpdate = 0;
    if (!TryReadRequiredEvents(bot, { {"login", &loginValue}, {"add", &priorAdd},
            {"logout", &priorLogout}, {"update", &priorUpdate} }))
        return false;

    if (loginValue)
        return true; // a login is already in progress; duplicate attempt is a no-op

    // The COMPLETE durable activation plan - add, logout, login, update - is
    // persisted and checked BEFORE the login starts or the in-memory list
    // changes. A failed write compensates the written prefix back to the
    // known priors; an uncertain compensation dirties currentBots. Either
    // way, NO login was started by a failure.
    std::vector<living::PlannedEventWrite> plan = {
        { "add", 1, urand(sPlayerbotAIConfig.minRandomBotInWorldTime, sPlayerbotAIConfig.maxRandomBotInWorldTime),
          priorAdd, RemainingValidity(bot, "add") },
        { "logout", 0, 0, priorLogout, RemainingValidity(bot, "logout") },
        { "login", 1, static_cast<uint32>(-1), 0, 0 },
        { "update", 1, urand(sPlayerbotAIConfig.minRandomBotReviveTime, sPlayerbotAIConfig.maxRandomBotReviveTime),
          priorUpdate, RemainingValidity(bot, "update") },
    };

    if (PlayerbotHolder::IsDeletionPending(bot) || IsLifecycleLoginBlocked(bot))
    {
        sLog.outDetail("AddRandomBot: refusing guid %u - lifecycle state is quarantined or deletion-pending", bot);
        return false;
    }

    if (!RunActivationPlan(bot, plan))
        return false;

    // Only after the full plan is execution-confirmed: start the login and
    // insert into the list exactly once.
    AddPlayerBot(bot, 0);
    if (std::find(currentBots.begin(), currentBots.end(), bot) == currentBots.end())
        currentBots.push_back(bot);

    sLog.outDetail("Random bot added #%d", bot);
    return true;
}

void RandomPlayerbotMgr::MovePlayerBot(uint32 guid, PlayerbotHolder* newHolder)
{
    if (!sPlayerbotAIConfig.enabled)
        return;

    players[guid] = this->GetPlayerBot(guid);
    PlayerbotHolder::MovePlayerBot(guid, newHolder);
}

bool RandomPlayerbotMgr::ProcessBot(uint32 bot)
{
    if (IsLifecycleLoginBlocked(bot))
        return false;

    Player* player = GetPlayerBot(bot);
    if (player && sPlayerbotAIConfig.IsFreeAltBot(player))
    {
        return false;
    }

    PlayerbotAI* ai = player ? player->GetPlayerbotAI() : NULL;

    bool botsAllowedInWorld = !sPlayerbotAIConfig.randomBotLoginWithPlayer || (!players.empty() && sWorld.GetActiveSessionCount() > 0);

    bool isValid = true;

    // Typed read: a KNOWN zero means the in-world window genuinely expired.
    // While the event-cache load state is unknown (failed bulk load) the read
    // reports NOT KNOWN and this deactivation is SKIPPED - a transient read
    // failure must never remove a durably active bot from currentBots, clear
    // its add marker and log it out as if the activation had expired.
    uint32 addValue = 0;
    bool const addKnown = TryGetEventValue(bot, "add", addValue);

    if (sPlayerbotAIConfig.randomBotTimedLogout && addKnown && !addValue && !sPlayerbotAIConfig.asyncBotLogin) // RandomBotInWorldTime is expired.
        isValid = false;
    else if(!botsAllowedInWorld)                                               // Logout if all players logged out
        isValid = false;

    //Log out bot
    if (!isValid)
    {
        if (botsAllowedInWorld && player && player->GetGroup())
        {
            SetEventValue(bot, "add", 1, 120);                                 // Delay logout for 2 minutes while in group.
            return false;
        }

        if (!player || !player->IsInWorld())
            sLog.outDetail("Bot #%d: log out", bot);
        else
            sLog.outDetail("Bot #%d %s:%d <%s>: log out", bot, IsAlliance(player->getRace()) ? "A" : "H", player->GetLevel(), player->GetName());

        currentBots.remove(bot);
        // Half of the activation pair: if the durable clear failed, the
        // in-memory removal above diverged from durable truth - reconcile
        // before the vector is trusted again.
        if (!SetEventValue(bot, "add", 0, 0))
        {
            currentBotsDirty = true;
            sLog.outError("ProcessBot: clearing 'add' for bot %u not confirmed; bot list marked dirty", bot);
        }

        if (!player)
        {
            return false;
        }

        LogoutPlayerBot(bot);

        if (sPlayerbotAIConfig.randomBotTimedOffline)
        {
            // Typed read: an unknown logout state must not trigger the write
            // (the failed read is not a confirmed "no logout scheduled").
            uint32 logout = 0;
            if (TryGetEventValue(bot, "logout", logout) && !logout
                && !SetEventValue(bot, "logout", 1, urand(sPlayerbotAIConfig.minRandomBotInWorldTime, sPlayerbotAIConfig.maxRandomBotInWorldTime)))
            {
                currentBotsDirty = true;
                sLog.outError("ProcessBot: setting 'logout' for bot %u not confirmed; bot list marked dirty", bot);
            }
        }

        return false;
    }

    //Log in bot (Added in AddRandomBots)
    if (!player)
    {
        if (!botsAllowedInWorld)
            return false;

        // Typed reads: unknown login state must not start a login, and the
        // full durable plan (login + update, with known priors) is confirmed
        // BEFORE AddPlayerBot - the same activation boundary AddRandomBot
        // uses.
        uint32 loginValue = 0, priorUpdate = 0;
        if (!TryReadRequiredEvents(bot, { {"login", &loginValue}, {"update", &priorUpdate} }))
            return false;

        if (loginValue)
            return true;

        std::vector<living::PlannedEventWrite> plan = {
            // Reset to 0 on server startup - see the RandomPlayerbotMgr
            // constructor's login sweep.
            { "login", 1, static_cast<uint32>(-1), 0, 0 },
            { "update", 1, urand(sPlayerbotAIConfig.minRandomBotReviveTime, sPlayerbotAIConfig.maxRandomBotReviveTime),
              priorUpdate, RemainingValidity(bot, "update") },
        };

        if (PlayerbotHolder::IsDeletionPending(bot) || IsLifecycleLoginBlocked(bot))
            return false; // lifecycle-quarantined/deletion-pending characters never log in

        if (!RunActivationPlan(bot, plan))
            return false;

        AddPlayerBot(bot, 0);
        return true;
    }

    if (!player->IsInWorld() || player->IsBeingTeleported() || player->GetSession()->isLogingOut()) //Skip bots that are in limbo.
        return false;

    uint32 loginFlag = 0;
    if (TryGetEventValue(bot, "login", loginFlag) && loginFlag)
        SetEventValue(bot, "login", 0, 0); //Bot is no longer loggin in.

    // Typed read: an unknown update state defers the AI update instead of
    // treating the failed read as "due now" and rewriting the schedule.
    uint32 update = 0;
    if (!TryGetEventValue(bot, "update", update))
        return false;

    //Update the bot
    if (!update)
    {
        bool shouldProcess = false;
        if (!sPlayerbotAIConfig.disableRandomLevels)
        {
            if (player->GetGroup() || player->IsTaxiFlying())
                return false;

            shouldProcess = true;
            if (ai)
            {
                if (!sRandomPlayerbotMgr.IsRandomBot(player))
                    shouldProcess = false;

                if (player->GetGroup() && ai->GetGroupMaster() && (!ai->GetGroupMaster()->GetPlayerbotAI() || ai->GetGroupMaster()->GetPlayerbotAI()->IsRealPlayer()))
                    shouldProcess = false;

                if (ai->HasPlayerNearby())
                    shouldProcess = false;
            }
        }

        uint32 randomTime = urand(sPlayerbotAIConfig.minRandomBotReviveTime, sPlayerbotAIConfig.maxRandomBotReviveTime * 5);
        if (!SetEventValue(bot, "update", 1, randomTime))
            return false;

        // No due-work side effect runs until the next manager deadline is
        // execution-confirmed. A failed lease leaves the expired state to retry.
        if (ai && !ai->HasStrategy("debug", BotState::BOT_STATE_NON_COMBAT))
            ai->GetAiObjectContext()->ClearExpiredValues();
        if (shouldProcess)
            ProcessBot(player);
        return true;
    }

    return false;
}

bool RandomPlayerbotMgr::ProcessBot(Player* player)
{
    if (!player || !player->IsInWorld() || player->IsBeingTeleported() || player->GetSession()->isLogingOut())
        return false;

    uint32 bot = player->GetGUIDLow();

    if (player->InBattleGround())
        return false;

    if (player->InBattleGroundQueue())
        return false;

    // only teleport idle bots
    bool idleBot = false;
    TravelTarget* target = player->GetPlayerbotAI()->GetAiObjectContext()->GetValue<TravelTarget*>("travel target")->Get();
    if (target)
    {
        if (target->GetTravelState() == TravelState::TRAVEL_STATE_IDLE)
            idleBot = true;
    }
    else
        idleBot = true;

    if (idleBot)
    {
        // All-or-nothing typed read of every event this decision consumes: a
        // failed load must not randomize, change strategy, relocate or write
        // any schedule - the whole idle step defers until the state is known.
        uint32 randomize = 0, changeStrategyValue = 0, teleportValue = 0;
        if (!TryReadRequiredEvents(bot, { {"randomize", &randomize},
                {"change_strategy", &changeStrategyValue}, {"teleport", &teleportValue} }))
            return false;

        if (!randomize)
        {
            bool randomiser = true;
            if (player->GetGuildId())
            {
                Guild* guild = sGuildMgr.GetGuildById(player->GetGuildId());
                uint32 accountId = sObjectMgr.GetPlayerAccountIdByGUID(guild->GetLeaderGuid());
                if (!sPlayerbotAIConfig.IsInRandomAccountList(accountId))
                {
                    int32 rank = guild->GetRank(player->GetObjectGuid());
                    randomiser = rank < 4 ? false : true;
                }
            }

            if (randomiser)
            {
                uint32 const randomTime = urand(sPlayerbotAIConfig.minRandomBotRandomizeTime,
                    sPlayerbotAIConfig.maxRandomBotRandomizeTime);
                if (!ScheduleRandomize(bot, randomTime))
                    return false;
                Randomize(player);
                return true;
            }
        }

        if (!changeStrategyValue)
        {
            if (sPlayerbotAIConfig.enableRandomTeleports)
            {
                if (!ScheduleChangeStrategy(bot))
                    return false;
                sLog.outDetail("Changing strategy for bot #%d %s:%d <%s>", bot, player->GetTeam() == ALLIANCE ? "A" : "H", player->GetLevel(), player->GetName());
                ChangeStrategy(player);
            }
            else
            {
                sLog.outDetail("Changing strategy for bot #%d %s:%d <%s> is supposed to happen, but enableRandomTeleports = false", bot, player->GetTeam() == ALLIANCE ? "A" : "H", player->GetLevel(), player->GetName());
            }
            return true;
        }

        if (!teleportValue && players.size())
        {
            if (sPlayerbotAIConfig.enableRandomTeleports)
            {
                sLog.outDetail("Bot #%d %s:%d <%s>: sent to grind", bot, player->GetTeam() == ALLIANCE ? "A" : "H", player->GetLevel(), player->GetName());
                // The next teleport is scheduled in FinalizeRelocation, only once
                // this one actually completed: accepted asynchronous work is not
                // completed work. On rejection (or an acknowledgement that never
                // arrives) the event value stays 0 and the next cycle retries.
                RandomTeleportForLevel(player, true, /*scheduleNextOnCompletion*/ true);
            }
            else
            {
                sLog.outDetail("Bot #%d %s:%d <%s>: supposed to be sent to grind, but enableRandomTeleports = false", bot, player->GetTeam() == ALLIANCE ? "A" : "H", player->GetLevel(), player->GetName());
            }
            return true;
        }
    }

    return false;
}

living::RelocationOutcome RandomPlayerbotMgr::Revive(Player* player)
{
    // Recovery is acknowledgement-driven: the dead/revive markers are cleared in
    // FinalizeRelocation, and only once the bot has actually been resurrected
    // there. The markers used to be cleared up front (later, on acceptance), so
    // a refused - or accepted-but-never-completed - relocation left the bot dead
    // with its retry bookkeeping gone and the command still reporting success.
    if (sServerFacade.GetDeathState(player) == CORPSE)
        return RandomTeleport(player, /*reviveRecovery*/ true);

    return RandomTeleportForLevel(player, false, false, /*reviveRecovery*/ true);
}

living::RelocationAdvanceResult RandomPlayerbotMgr::FinalizeRelocation(Player* bot)
{
    if (!bot)
        return living::RelocationAdvanceResult::NoPending;

    uint32 const botGuid = bot->GetGUIDLow();
    if (!relocations.HasPending(botGuid))
        return living::RelocationAdvanceResult::NoPending;

    if (!relocations.IsFinalizing(botGuid))
    {
        // The acknowledgement is complete only when the bot is back in-world
        // and no longer teleporting; until then the record stays armed.
        if (!bot->IsInWorld() || bot->IsBeingTeleported())
            return living::RelocationAdvanceResult::NoPending;

        // Acknowledge() verifies the bot is standing on the EXACT destination
        // the accepted TeleportTo was given. A finished acknowledgement
        // anywhere else means the tracked teleport chain is dead (redirected,
        // clobbered, or superseded mid-chain): the obsolete record is erased -
        // never left armed for a later unrelated landing - while revive/retry
        // markers stay, so the recovery is retried rather than falsely
        // completed. An exact landing moves the record to Finalizing; it is
        // NOT erased - it now tracks the owed completion work.
        living::PendingRelocation record;
        switch (relocations.Acknowledge(botGuid, bot->GetMapId(),
            bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetOrientation(), record))
        {
            case living::RelocationAckResult::TerminalMismatch:
                sLog.outDetail("Relocation of bot %s terminally cancelled: acknowledged landing does not match the accepted destination (token " UI64FMTD ")",
                    bot->GetName(), record.token);
                return living::RelocationAdvanceResult::NoPending;
            case living::RelocationAckResult::NoPending:
                return living::RelocationAdvanceResult::NoPending;
            default:
                break; // Landed / AlreadyFinalizing: advance below
        }
    }

    return AdvanceRelocation(bot);
}

living::RelocationAdvanceResult RandomPlayerbotMgr::AdvanceRelocation(Player* bot)
{
    uint32 const botGuid = bot->GetGUIDLow();
    living::PendingRelocation const* liveRecord = relocations.Find(botGuid);
    if (!liveRecord || liveRecord->stage != living::RelocationStage::Finalizing)
        return living::RelocationAdvanceResult::NoPending;

    // Copy the request out: Advance may erase the live record on completion.
    living::PendingRelocation const record = *liveRecord;

    living::RelocationAdvanceOps ops;

    ops.runtimeReset = [this, bot]()
    {
        // Exactly one FULL AI reset per completed relocation, unconditionally:
        // Reset(true) clears old-map movement/travel/spell state and
        // reinitializes the engines. Refresh's own internal reset is both
        // plain (Reset(false)) and skipped entirely when random levels are
        // disabled, so relying on it let bots resume stale cross-map
        // navigation. Refresh resurrects/repairs/refills - exactly once, and
        // only now that the bot demonstrably arrived (it exits early during a
        // far transfer, which is why running it on acceptance silently did
        // nothing). The done-flag in the record guarantees neither re-runs on
        // later persistence-retry advances.
        bot->GetPlayerbotAI()->Reset(true);
        Refresh(bot, /*resetAi*/ false);
    };

    ops.clearReviveMarkers = [this, bot, botGuid]() -> living::MarkerClearOutcome
    {
        // Clear the retry markers only after ACTUAL recovery. If the bot is
        // somehow still dead (Refresh is guarded against battlegrounds), the
        // markers stay - deliberately - and the event-driven revive retry owns
        // the next attempt; a failed WRITE, in contrast, is retried here.
        if (sServerFacade.UnitIsDead(bot))
        {
            sLog.outDetail("Relocation of bot %s completed but the bot is still dead; revive markers kept", bot->GetName());
            return living::MarkerClearOutcome::KeptStillDead;
        }

        bool cleared = SetEventValue(botGuid, "dead", 0, 0);
        cleared = SetEventValue(botGuid, "revive", 0, 0) && cleared;
        return cleared ? living::MarkerClearOutcome::Cleared : living::MarkerClearOutcome::WriteFailed;
    };

    ops.applyHomebind = [this, bot, &record]() -> std::optional<living::HomebindWrite>
    {
        // The homebind reuses the exact accepted destination and the area
        // resolved from those FINAL coordinates (post-jitter,
        // post-terrain-height), never a cached pre-adjustment location.
        if (record.setHomebind)
        {
            bot->SetHomebindToLocation(WorldLocation(record.mapId, record.x, record.y, record.z, record.orientation),
                record.homebindAreaId);
            return living::HomebindWrite{ record.mapId, record.x, record.y, record.z, record.homebindAreaId };
        }

        // Closest-inn selection runs from the post-acknowledgement position.
        // Only inns on the bot's CURRENT map are candidates: sqDistance has no
        // map awareness, so a foreign-map inn with numerically similar
        // coordinates could otherwise win and persist a homebind on the wrong
        // continent.
        WorldPosition botPos(bot);
        ObjectGuid closestInn;
        WorldPosition closestInnPos;
        living::MapLocalMinimum closest(bot->GetMapId());
        for (auto& [innGuid, innPosition] : innCacheLevel[bot->getRace()][bot->GetLevel()])
        {
            if (closest.Consider(innPosition.mapid, botPos.sqDistance(innPosition)))
            {
                closestInn = innGuid;
                closestInnPos = innPosition;
            }
        }

        if (!closestInn)
            return std::nullopt; // no eligible inn: nothing to write

        // Perform the REAL bind: the state change is the homebind fields (the
        // innkeeper creature may not even be loaded this far away, so the
        // SendBindPoint interaction path is not available).
        uint32 const innAreaId = sTerrainMgr.GetAreaId(closestInnPos.getMapId(),
            closestInnPos.getX(), closestInnPos.getY(), closestInnPos.getZ());
        bot->SetHomebindToLocation(
            WorldLocation(closestInnPos.getMapId(), closestInnPos.getX(), closestInnPos.getY(), closestInnPos.getZ(), 0.0f),
            innAreaId);

        // Legacy visual confirmation for nearby observers.
        WorldPacket data(SMSG_TRAINER_BUY_SUCCEEDED, (8 + 4));
        data << closestInn;
        data << uint32(3286);                               // Bind
        bot->GetSession()->SendPacket(data);

        return living::HomebindWrite{ closestInnPos.getMapId(),
            closestInnPos.getX(), closestInnPos.getY(), closestInnPos.getZ(), innAreaId };
    };

    ops.requestHomebindVerify = [this, botGuid, &record]() -> bool
    {
        // SetHomebindToLocation queues an async UPDATE and returns void in
        // every pinned core; this execution-ordered query (same FIFO delay
        // thread) observes the row AFTER that write executed. Its callback
        // only parses and enqueues - the outcome is processed by
        // PumpPendingRelocations. The callback parameter is the relocation
        // TOKEN, so a stale result from a superseded generation can never be
        // applied to a later record. AsyncPQuery reports ENQUEUE failure via
        // its return value in all three cores - propagated so the tracker
        // retries instead of waiting for a result that was never queued.
        return CharacterDatabase.AsyncPQuery(this, &RandomPlayerbotMgr::HandleHomebindVerify, record.token,
            "SELECT map, zone, position_x, position_y, position_z FROM character_homebind WHERE guid = '%u'", botGuid);
    };

    ops.onHomebindGaveUp = [bot]()
    {
        sLog.outError("Relocation homebind for bot %s could not be verified (request budget exhausted); completing with an UNVERIFIED homebind (may revert on restart)",
            bot->GetName());
    };

    ops.applyRpgCooldown = [bot]()
    {
        //Travel cooldown for 10 minutes.
        AiObjectContext* context = bot->GetPlayerbotAI()->GetAiObjectContext();
        TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target");

        sTravelMgr.SetNullTravelTarget(travelTarget);
        travelTarget->SetStatus(TravelStatus::TRAVEL_STATUS_COOLDOWN);
        travelTarget->SetExpireIn(10 * MINUTE * IN_MILLISECONDS);
    };

    ops.scheduleNextTeleport = [this, botGuid]()
    {
        // Follow-up scheduling records completed work only now that the work
        // is actually complete - and a FAILED write keeps the record armed,
        // so the old expired event cannot trigger an immediate second
        // teleport; the write is retried from the pump.
        return ScheduleTeleport(botGuid);
    };

    living::RelocationAdvanceResult const result = relocations.Advance(botGuid, ops);
    if (result == living::RelocationAdvanceResult::Completed)
        sLog.outDetail("Relocation of bot %s to map %u completed (token " UI64FMTD ")",
            bot->GetName(), record.mapId, record.token);

    return result;
}

void RandomPlayerbotMgr::HandleHomebindVerify(QueryResult* result, uint64 relocationToken)
{
    // SQL result callback: parse the row into a typed outcome, enqueue it on
    // the tracker, return. No database work, no lifecycle decisions - those
    // run in PumpPendingRelocations, outside the result-queue mutex.
    living::HomebindVerifyOutcome outcome = living::HomebindVerifyOutcome::QueryFailed;
    if (result)
    {
        Field* fields = result->Fetch();
        uint32 const mapId = fields[0].GetUInt32();
        uint32 const zone = fields[1].GetUInt32();
        float const x = fields[2].GetFloat();
        float const y = fields[3].GetFloat();
        float const z = fields[4].GetFloat();

        outcome = living::HomebindVerifyOutcome::Mismatch;
        // Token-addressed lookup: a stale callback from a superseded or
        // cancelled generation finds no record and its event is simply
        // ignored by the pump.
        if (living::PendingRelocation const* record = relocations.FindByToken(relocationToken))
        {
            // The core's homebind UPDATE serializes coordinates through "%f"
            // (6 decimals), so the readback is not bit-exact: compare with a
            // tolerance far above serialization noise and far below any real
            // displacement.
            auto withinTolerance = [](float a, float b) { return std::fabs(a - b) <= 0.1f; };
            if (record->homebindTargetKnown
                && record->homebindTargetMap == mapId
                && record->homebindTargetArea == zone
                && withinTolerance(record->homebindTargetX, x)
                && withinTolerance(record->homebindTargetY, y)
                && withinTolerance(record->homebindTargetZ, z))
                outcome = living::HomebindVerifyOutcome::Match;
        }
    }
    delete result;

    relocations.OnHomebindVerifyResult(relocationToken, outcome);
}

void RandomPlayerbotMgr::PumpPendingRelocations()
{
    // Apply drained homebind-verification callback events first (pump
    // context: the result-queue mutex is not held here). Events are
    // token-addressed: results from superseded/cancelled generations match
    // nothing and are dropped.
    for (living::RelocationTracker::HomebindVerifyEvent const& event : relocations.DrainHomebindVerifyEvents())
    {
        switch (relocations.ApplyHomebindVerify(event.relocationToken, event.outcome,
            living::RelocationTracker::kMaxHomebindWriteAttempts))
        {
            case living::HomebindVerifyAction::GaveUp:
                sLog.outError("Relocation homebind (token " UI64FMTD ") could not be durably verified after %u attempts; completing with an UNVERIFIED homebind (may revert on restart)",
                    event.relocationToken, living::RelocationTracker::kMaxHomebindWriteAttempts);
                break;
            case living::HomebindVerifyAction::Reissue:
                sLog.outDetail("Relocation homebind (token " UI64FMTD ") not confirmed; reissuing the write", event.relocationToken);
                break;
            default:
                break;
        }
    }

    // Sweep every tracked record. Finalizing records advance their owed
    // durable writes (revive markers, homebind chain, next-teleport
    // scheduling) until confirmed - only then is the record and its
    // destination reservation released. STALE PendingAck records (an
    // acknowledgement the ack hook never resolved: bot back in-world, not
    // teleporting) are resolved through the normal acknowledgement path -
    // an exact landing finalizes, anything else terminally mismatches - which
    // is what makes RelocationTracker::Begin's refusal of duplicate records
    // deadlock-free.
    for (uint32 const botGuid : relocations.TrackedBots())
    {
        Player* bot = GetPlayerBot(botGuid);
        if (!bot)
        {
            // The player is temporarily absent (logging out/in): a PendingAck
            // record is released, but a Finalizing record's owed durable work is
            // RETAINED across the gap and only dropped once the bounded offline
            // watchdog expires, so a relogin resumes it exactly once.
            relocations.NoteBotAbsent(botGuid);
            continue;
        }

        if (!bot->IsInWorld() || !bot->GetPlayerbotAI() || bot->IsBeingTeleported())
            continue; // genuinely in flight (or in limbo): leave the record armed

        if (relocations.IsFinalizing(botGuid))
            AdvanceRelocation(bot);
        else
            FinalizeRelocation(bot);
    }
}

void RandomPlayerbotMgr::CancelPendingRelocation(uint32 botGuid, living::RelocationCancelMode mode)
{
    relocations.Cancel(botGuid, mode);
}

// Returns Rejected when no candidate was accepted (nothing mutated) and Pending
// when TeleportTo QUEUED a transfer - completion work (Refresh, homebind, inn
// binding, marker clearing, scheduling) runs only in FinalizeRelocation once the
// bot's teleport acknowledgement lands on the accepted destination.
namespace
{
    // A random teleport may only move free synthetic random bots. A connected real
    // player can carry a NON-NULL PlayerbotAI in this codebase and is identified by
    // IsRealPlayer() (the manager relies on exactly that elsewhere), so a null-AI
    // check alone still relocated real players; a bot owned by a real player must
    // stay with its owner rather than be moved away from that commitment.
    bool IsFreeSyntheticRandomBot(Player* player)
    {
        if (!player || !player->GetSession())
            return false;

        PlayerbotAI* playerAi = player->GetPlayerbotAI();
        if (!playerAi)                       // real player with no AI at all
            return false;
        if (playerAi->IsRealPlayer())        // connected real player, AI or not
            return false;
        if (playerAi->HasRealPlayerMaster()) // player-owned/protected bot
            return false;
        if (sPlayerbotAIConfig.IsFreeAltBot(player)) // player alt bot
            return false;

        return sPlayerbotAIConfig.IsInRandomAccountList(player->GetSession()->GetAccountId());
    }

    // The ONE complete destination-eligibility validator. It runs both as the
    // candidate pre-filter AND - decisively - on the FINAL x/y/z produced by
    // Dark Portal overrides, retry jitter and terrain-height adjustment:
    // zone/area are recomputed from the final coordinates and EVERY policy a
    // mutable coordinate can invalidate reruns here - configured map
    // allowlist, expansion rules, faction/capital/starter zone policy,
    // active-zone requirement and destination density (when activeOnly), and
    // coordinate validity. The resolved final area is returned for the
    // relocation record so the persisted homebind zone always matches the
    // landing coordinates.
    bool IsEligibleTeleportDestination(Player* bot, uint32 mapId, float x, float y, float z, bool activeOnly, AreaTableEntry const*& outArea)
    {
        outArea = nullptr;

        // Configured map allowlist: also a FINAL check - the Dark Portal
        // override can substitute a different map after candidate admission.
        if (std::find(sPlayerbotAIConfig.randomBotMaps.begin(), sPlayerbotAIConfig.randomBotMaps.end(), mapId)
            == sPlayerbotAIConfig.randomBotMaps.end())
            return false;

        if (!MapManager::IsValidMapCoord(mapId, x, y, z, 0))
            return false;

        uint32 zoneId, areaId;
        sTerrainMgr.GetZoneAndAreaId(zoneId, areaId, mapId, x, y, z);
        AreaTableEntry const* area = GetAreaEntryByAreaID(areaId);
        if (!area)
            return false;

#ifndef MANGOSBOT_ZERO
        // Do not teleport to outland before portal opening (allow new races zones)
        if (sWorldState.GetExpansion() == EXPANSION_NONE && (mapId == 571 || (mapId == 530 && area->team != 2 && area->team != 4)))
            return false;
#endif

        bool const expansionZones =
#ifdef MANGOSBOT_ZERO
            false;
#else
            true;
#endif

        if (zoneId)
        {
            // Top-level areas ARE their own zone (zoneId == areaId): zone
            // policy runs on the area record itself in that case - skipping it
            // let low-level bots into foreign starter zones and enemy capitals
            // whenever the destination was a top-level tuple.
            AreaTableEntry const* zone = zoneId == areaId ? area : GetAreaEntryByAreaID(zoneId);
            if (!zone)
                return false;

            bool const zoneIsEnemy = (zone->team == AREATEAM_ALLY && bot->GetTeam() != ALLIANCE)
                || (zone->team == AREATEAM_HORDE && bot->GetTeam() != HORDE);

            if (living::DestinationBlockedByEnemyZone(zoneIsEnemy, (zone->flags & AREA_FLAG_CAPITAL) != 0, bot->GetLevel()))
                return false;

            if (living::DestinationBlockedByStarterZone(zoneId, bot->getRace(), bot->GetTeam() == ALLIANCE, bot->GetLevel(), expansionZones))
                return false;
        }

        bool const areaIsEnemy = (area->team == AREATEAM_ALLY && bot->GetTeam() != ALLIANCE)
            || (area->team == AREATEAM_HORDE && bot->GetTeam() != HORDE);

        if (living::DestinationBlockedByEnemyArea(areaIsEnemy, bot->GetLevel()))
            return false;

        // Active-zone requirement: a mutable-coordinate policy (jitter can
        // cross into an inactive zone), rerun on the final tuple exactly like
        // the pre-filter - but only while activeOnly; the wrappers' fallback
        // deliberately relaxes it.
        if (activeOnly && sPlayerbotAIConfig.randomBotTeleportNearPlayer)
        {
            Map* tMap = sMapMgr.FindMap(mapId, 0);
            if (!tMap || !tMap->IsContinent() || !tMap->HasActiveZones())
                return false;

            if (!tMap->HasActiveZone(zoneId ? zoneId : areaId))
                return false;
        }

        // Destination density is INDEPENDENT of the active-zone relaxation:
        // the wrappers retry every rejection with activeOnly=false, and if
        // density only ran on the first pass, the second pass would happily
        // overfill the exact point another bot already reserved.
        if (sPlayerbotAIConfig.randomBotTeleportNearPlayer
            && sPlayerbotAIConfig.randomBotTeleportNearPlayerMaxAmount > 0
            && sPlayerbotAIConfig.randomBotTeleportNearPlayerMaxAmountRadius > 0.0f)
        {
            WorldPosition const destination(mapId, x, y, z);
            uint32 botsNearTeleportPoint = 0;
            sRandomPlayerbotMgr.ForEachPlayerbot([&](Player* otherBot)
            {
                // ONE spatial predicate for live occupants and reservations:
                // same map + configured 2D radius, self excluded - the
                // reservation scan uses exactly this, so an identical
                // placement counts the same before and after finalization
                // (the old zone-equality gate made a bot across a zone
                // boundary count while pending and vanish once finalized).
                // The RELOCATING bot itself never counts against its own
                // destination; a bot with an in-flight relocation record is
                // skipped here and counted through its destination
                // reservation below, so each bot contributes exactly once.
                if (otherBot && otherBot != bot && !otherBot->IsBeingTeleported()
                    && !sRandomPlayerbotMgr.HasPendingRelocation(otherBot->GetGUIDLow())
                    && otherBot->GetMapId() == mapId)
                {
                    if (destination.fDist(WorldPosition(otherBot)) <= sPlayerbotAIConfig.randomBotTeleportNearPlayerMaxAmountRadius)
                        botsNearTeleportPoint++;
                }
            });

            // In-flight reservations: bots whose accepted relocation
            // targets this vicinity but who have not landed/completed yet
            // (PendingAck and Finalizing stages). Without them two bots
            // could pass admission for the same empty destination and both
            // land, exceeding the cap. The bot's own record is excluded.
            botsNearTeleportPoint += sRandomPlayerbotMgr.CountPendingRelocationsNear(mapId, x, y,
                sPlayerbotAIConfig.randomBotTeleportNearPlayerMaxAmountRadius, bot->GetGUIDLow());

            if (botsNearTeleportPoint >= sPlayerbotAIConfig.randomBotTeleportNearPlayerMaxAmount)
                return false;
        }

        outArea = area;
        return true;
    }
}

living::RelocationOutcome RandomPlayerbotMgr::RandomTeleport(Player* bot, std::vector<WorldLocation> &locs, living::PendingRelocation flags, bool activeOnly)
{
    // Complete preflight BEFORE any mutation (taxi, homebind, motion, position,
    // heartbeat, AI reset).
    if (!IsFreeSyntheticRandomBot(bot))
    {
        sLog.outDetail("Random teleport skipped for bot %s: not a free synthetic random bot",
            bot ? bot->GetName() : "<null>");
        return living::RelocationOutcome::Rejected;
    }

    // A grouped bot is never randomly relocated (standalone legacy safety fix).
    // There is no core-backed way to relocate a group atomically: once one member
    // has teleported or had its homebind rewritten, a later member's TeleportTo
    // failure leaves the group split with no way back. Sequential "best effort"
    // relocation is therefore not attempted at all; a core-backed group relocation
    // is future design work.
    if (bot->GetGroup())
    {
        sLog.outDetail("Random teleport skipped for bot %s: bot is in a group", bot->GetName());
        return living::RelocationOutcome::Rejected;
    }

    if (bot->IsBeingTeleported())
        return living::RelocationOutcome::Rejected;

    // ANY tracked relocation (awaiting its acknowledgement, or finalizing
    // owed side effects with failed schedule/homebind writes retrying from
    // the pump) blocks a new random relocation: an old expired teleport event
    // must not trigger an immediate second teleport while the first one is
    // unresolved, and Begin never silently replaces an active record. Stale
    // PendingAck records are resolved by the pump sweep (finalized or
    // terminally mismatched), so this refusal cannot wedge the bot. This runs
    // BEFORE TeleportTo so the tracker can never refuse a registration for an
    // already-queued transfer.
    if (relocations.HasPending(bot->GetGUIDLow()))
    {
        sLog.outDetail("Random teleport skipped for bot %s: a previous relocation is still pending/finalizing", bot->GetName());
        return living::RelocationOutcome::Rejected;
    }

    if (bot->InBattleGround())
        return living::RelocationOutcome::Rejected;

    if (bot->InBattleGroundQueue())
        return living::RelocationOutcome::Rejected;

    if (bot->GetLevel() < 5)
        return living::RelocationOutcome::Rejected;

    // The remaining core TeleportTo rejection paths (verified against all three
    // pinned cores) are preflighted here so a rejection can never happen after
    // destructive preparation:
    // - a charmed player is rejected inside TeleportTo (HasCharmer());
    // - a taxi-flying bot needs its taxi path and motion destroyed BEFORE
    //   TeleportTo can succeed, and that state cannot be restored if the call
    //   then fails, so taxi-flying bots are conservatively refused;
    // - invalid coordinates are rejected per candidate below.
    if (bot->HasCharmer())
    {
        sLog.outDetail("Random teleport skipped for bot %s: bot is charmed", bot->GetName());
        return living::RelocationOutcome::Rejected;
    }

    if (bot->IsTaxiFlying())
    {
        sLog.outDetail("Random teleport skipped for bot %s: bot is taxi flying", bot->GetName());
        return living::RelocationOutcome::Rejected;
    }

    if (locs.empty())
    {
        sLog.outError("Cannot teleport bot %s - no locations available", bot->GetName());
        return living::RelocationOutcome::Rejected;
    }

    std::vector<WorldPosition> tlocs;

    for (auto& loc : locs)
    {
        tlocs.push_back(WorldPosition(loc));
    }

    // Candidate pre-filter: the SAME complete eligibility validator that later
    // decides the final tuple (map allowlist, expansion, zone/area policy,
    // active-zone requirement and density when activeOnly), run here on the
    // raw candidate to avoid burning attempts on hopeless locations.
    tlocs.erase(std::remove_if(tlocs.begin(), tlocs.end(), [bot, activeOnly](const WorldPosition& l)
    {
        AreaTableEntry const* area = nullptr;
        return !IsEligibleTeleportDestination(bot, l.getMapId(), l.coord_x, l.coord_y, l.coord_z, activeOnly, area);
    }), tlocs.end());

    //Random shuffle based on distance. Closer distances are more likely (but not exclusively) to be at the begin of the list.
    tlocs = WorldPosition(bot).GetNextPoint(tlocs, 0);

    //5% + 0.1% per level chance node on different map in selection.
    //tlocs.erase(std::remove_if(tlocs.begin(), tlocs.end(), [bot](WorldLocation const& l) {return l.mapid != bot->GetMapId() && urand(1, 100) > 0.5 * bot->GetLevel(); }), tlocs.end());

    //Continent is about 20.000 large
    //Bot will travel 0-5000 units + 75-150 units per level.
    //tlocs.erase(std::remove_if(tlocs.begin(), tlocs.end(), [bot](WorldLocation const& l) {return l.mapid == bot->GetMapId() && sServerFacade.GetDistance2d(bot, l.coord_x, l.coord_y) > urand(0, 5000) + bot->GetLevel() * 15 * urand(5, 10); }), tlocs.end());

    if (tlocs.empty())
    {
        // The activeOnly fallback (retry without the active-zone restriction)
        // lives in the public helpers now, so this helper reports plain
        // outcomes and never hides a fallback's own result.
        if (!activeOnly)
            sLog.outError("Cannot teleport bot %s - no locations available", bot->GetName());

        return living::RelocationOutcome::Rejected;
    }

    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "RandomTeleportByLocations");

    int index = 0;

    for (int i = 0; i < tlocs.size(); i++)
    {
        for (int attemtps = 0; attemtps < 3; ++attemtps)
        {
            WorldLocation const original = tlocs[i];

            bool overrideDrawn = false;
            WorldLocation overrideLoc = original;
#ifdef MANGOSBOT_ONE
            // Optional Dark Portal override while the opening event is in
            // progress: drawn per attempt and tried FIRST, but it never
            // REPLACES the raw candidate - when the portal tuple is rejected
            // (map 0 not in the allowlist, inactive, over-density, bad
            // terrain) the original candidate is tried in the SAME attempt,
            // so only genuine exhaustion of the raw candidates can reach the
            // wrappers' activeOnly=false fallback.
            if (sWorldState.GetExpansion() == EXPANSION_NONE && bot->GetLevel() > 54 && urand(0, 100) > 20)
            {
                overrideDrawn = true;
                if (urand(0, 1))
                    overrideLoc = WorldLocation(uint32(0), -11772.43f, -3272.84f, -17.9f, 3.32447f);
                else
                    overrideLoc = WorldLocation(uint32(0), -11741.70f, -3130.3f, -11.7936f, 3.32447f);
            }
#endif

            for (WorldLocation const& loc : living::OverrideThenOriginal(overrideDrawn, overrideLoc, original))
            {
            float x = loc.coord_x + (attemtps > 0 ? urand(0, sPlayerbotAIConfig.grindDistance) - sPlayerbotAIConfig.grindDistance / 2 : 0);
            float y = loc.coord_y + (attemtps > 0 ? urand(0, sPlayerbotAIConfig.grindDistance) - sPlayerbotAIConfig.grindDistance / 2 : 0);
            float z = loc.coord_z;

            Map* map = sMapMgr.FindMap(loc.mapid, 0);
            if (!map)
                continue;

#ifdef MANGOSBOT_TWO
            float ground = map->GetHeight(bot->GetPhaseMask(), x, y, z + 0.5f);
#else
            float ground = map->GetHeight(x, y, z + 0.5f);
#endif
            if (ground <= INVALID_HEIGHT)
                continue;

            z = 0.05f + ground;

            // The FINAL tuple is now known (jitter applied, z replaced by the
            // terrain height). Rerun the complete eligibility validation on
            // exactly these coordinates: the override tuple can name a
            // disabled map, and jitter or vertical area resolution can cross
            // expansion/faction/starter/capital, active-zone or density
            // boundaries the raw candidate satisfied. A rejected override
            // falls through to the original tuple of the SAME attempt - it is
            // never confused with candidate exhaustion. The area recorded for
            // the homebind is the area of the LANDING spot. This also
            // preflights the core's own coordinate rejection so TeleportTo
            // cannot fail for a reason known in advance.
            AreaTableEntry const* area = nullptr;
            if (!IsEligibleTeleportDestination(bot, loc.mapid, x, y, z, activeOnly, area))
                continue;

            sLog.outDetail("Random teleporting bot %s to %s %f,%f,%f (%u/%zu locations)",
                bot->GetName(), area->area_name[0], x, y, z, attemtps, tlocs.size());

            // NO state is mutated before TeleportTo: taxi-flying bots were already
            // refused in the preflight (their taxi path cannot be restored on
            // failure). A rejected call moves on with everything untouched.
            if (!bot->TeleportTo(loc.mapid, x, y, z, 0))
            {
                sLog.outDetail("Random teleport of bot %s to map %u failed; trying next location",
                    bot->GetName(), loc.mapid);
                continue;
            }

            // Acceptance means the core QUEUED a near/far transfer - it is NOT
            // completion. Record the one exact final destination (reused verbatim
            // for the homebind so the bind can never diverge from the landing
            // spot) plus the work owed, and defer everything observable - Refresh,
            // homebind, inn binding, marker clearing, scheduling, heartbeat, AI
            // reset - to FinalizeRelocation on the teleport acknowledgement.
            // Clearing the motion stack is the one transient exception: the old
            // generator must not keep steering a bot whose transfer is queued.
            bot->GetMotionMaster()->Clear();

            flags.mapId = loc.mapid;
            flags.x = x;
            flags.y = y;
            flags.z = z;
            flags.orientation = 0.0f;
            flags.homebindAreaId = area->ID;
            if (!relocations.Begin(bot->GetGUIDLow(), flags))
                // Cannot happen: Finalizing was preflighted before any
                // TeleportTo. Logged in case a future path breaks the order.
                sLog.outError("Relocation tracker refused registration for bot %s AFTER TeleportTo; completion work will not run", bot->GetName());

            // Some core paths complete a same-map transfer synchronously; if no
            // acknowledgement is owed, finalize right here (persistence
            // retries continue from the pump while the record is Finalizing).
            if (!bot->IsBeingTeleported()
                && FinalizeRelocation(bot) == living::RelocationAdvanceResult::Completed)
                return living::RelocationOutcome::Completed;

            return living::RelocationOutcome::Pending;
            }
        }
    }

    sLog.outError("Cannot teleport bot %s - no locations available", bot->GetName());
    return living::RelocationOutcome::Rejected;
}

std::vector<std::pair<uint32, uint32>> RandomPlayerbotMgr::RpgLocationsNear(WorldLocation pos, const std::map<uint32, std::map<uint32, std::vector<std::string>>>& areaNames, uint32 radius)
{
    std::vector<std::pair<uint32, uint32>> results;
    float minDist = FLT_MAX;
    WorldPosition areaPos(pos);
    std::string hasZone = "-", wantZone = areaPos.getAreaName(true, true);

    for (uint32 level = 1; level < sPlayerbotAIConfig.randomBotMaxLevel + 1; level++)
    {
        for (uint32 r = 1; r < MAX_RACES; r++)
        {
            uint32 i = 0;
            for (auto p : rpgLocsCacheLevel[r][level])
            {
                std::string currentZone = areaNames.at(level).at(r)[i];
                i++;

                if (currentZone != wantZone && hasZone == wantZone) //If we already have the right id but this location isn't in the right id. Skip it.
                    continue;

                if (currentZone == wantZone && hasZone != wantZone) //If this is the first spot with a good area id use this now.
                    minDist = FLT_MAX;

                float dist = WorldPosition(pos).fDist(p);

                if (dist > radius || dist > minDist)
                    continue;

                if (dist < minDist)
                    results.clear();

                results.push_back(std::make_pair(r, level));

                hasZone = currentZone;

                minDist = dist;
            }
        }
    }

    return results;
}

void RandomPlayerbotMgr::PrepareTeleportCache()
{
    uint32 maxLevel = sPlayerbotAIConfig.randomBotMaxLevel;
    if (maxLevel > sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
        maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);

    auto results = CharacterDatabase.PQuery("SELECT `map_id`, `x`, `y`, `z`, `level` FROM `ai_playerbot_tele_cache`");
    if (results)
    {
        sLog.outString("Loading random teleport caches for %d levels...", maxLevel);
        do
        {
            Field* fields = results->Fetch();
            uint16 mapId = fields[0].GetUInt16();
            float x = fields[1].GetFloat();
            float y = fields[2].GetFloat();
            float z = fields[3].GetFloat();
            uint16 level = fields[4].GetUInt16();
            WorldLocation loc(mapId, x, y, z, 0);
            locsPerLevelCache[level].push_back(loc);
        } while (results->NextRow());
    }
    else
    {
        sLog.outString("Preparing random teleport caches for %d levels...", maxLevel);
        BarGoLink bar(maxLevel);
        for (uint8 level = 1; level <= maxLevel; level++)
        {
            auto results = WorldDatabase.PQuery("SELECT `map`, `position_x`, `position_y`, `position_z` "
                "FROM (SELECT `map`, `position_x`, `position_y`, `position_z`, t.maxlevel, t.minlevel, "
                "%u - (t.maxlevel + t.minlevel) / 2 delta "
                "FROM creature c INNER JOIN creature_template t ON c.id = t.entry WHERE t.CreatureType != 8 AND t.NpcFlags = 0 AND t.Rank = 0 AND NOT (t.extraFlags & 1024 OR t.extraFlags & 65536 OR t.extraflags & 64 OR t.unitFlags & 256 OR t.unitFlags & 512) AND t.lootid != 0) q "
                "WHERE delta >= 0 AND delta <= %u AND map in (%s)",
                level,
                sPlayerbotAIConfig.randomBotTeleLevel,
                sPlayerbotAIConfig.randomBotMapsAsString.c_str()
            );
            if (results)
            {
                CharacterDatabase.BeginTransaction();
                do
                {
                    Field* fields = results->Fetch();
                    uint16 mapId = fields[0].GetUInt16();
                    float x = fields[1].GetFloat();
                    float y = fields[2].GetFloat();
                    float z = fields[3].GetFloat();
                    WorldLocation loc(mapId, x, y, z, 0);
                    locsPerLevelCache[level].push_back(loc);

                    CharacterDatabase.PExecute("INSERT INTO `ai_playerbot_tele_cache` (`level`, `map_id`, `x`, `y`, `z`) VALUES (%u, %u, %f, %f, %f)",
                        level, mapId, x, y, z);
                } while (results->NextRow());
                CharacterDatabase.CommitTransaction();
            }
            bar.step();
        }
    }

    sLog.outString("Preparing RPG teleport caches for %d factions...", sFactionTemplateStore.GetNumRows());

    results = WorldDatabase.PQuery("SELECT map, position_x, position_y, position_z, "
        "r.race, r.minl, r.maxl "
        "FROM creature c INNER JOIN ai_playerbot_rpg_races r ON c.id = r.entry "
        "WHERE r.race < 15");

    if (results)
    {
        do
        {
            for (uint32 level = 1; level < sPlayerbotAIConfig.randomBotMaxLevel + 1; level++)
            {
                Field* fields = results->Fetch();
                uint16 mapId = fields[0].GetUInt16();
                float x = fields[1].GetFloat();
                float y = fields[2].GetFloat();
                float z = fields[3].GetFloat();
                //uint32 faction = fields[4].GetUInt32();
                //string name = fields[5].GetCppString();
                uint32 race = fields[4].GetUInt32();
                uint32 minl = fields[5].GetUInt32();
                uint32 maxl = fields[6].GetUInt32();

                if (level > maxl || level < minl) continue;

                WorldLocation loc(mapId, x, y, z, 0);
                for (uint32 r = 1; r < MAX_RACES; r++)
                {
                    if (race == r || race == 0) rpgLocsCacheLevel[r][level].push_back(loc);
                }
            }
            //bar.step();
        } while (results->NextRow());
    }

    sLog.outString("Enhancing RPG teleport cache");

    std::map<uint32, std::map<uint32, std::vector<std::string>>> areaNames;

    for (uint32 level = 1; level < sPlayerbotAIConfig.randomBotMaxLevel + 1; level++)
    {
        for (uint32 r = 1; r < MAX_RACES; r++)
        {
            for (auto p : rpgLocsCacheLevel[r][level])
            {
                areaNames[level][r].push_back(WorldPosition(p).getAreaName(true, true));
            }
        }
    }

    std::vector<std::pair<std::pair<uint32, uint32>, WorldPosition>> newPoints;
    std::vector<std::pair<std::pair<uint32, uint32>, GuidPosition>> innPoints;

    //Static portals.
    for (auto& goData : WorldPosition().getGameObjectsNear(0, 0))
    {
        GuidPosition go(goData);

        auto data = sGOStorage.LookupEntry<GameObjectInfo>(go.GetEntry());

        if (!data)
            continue;

        if (data->type != GAMEOBJECT_TYPE_SPELLCASTER)
            continue;

        const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(data->spellcaster.spellId);

        if (pSpellInfo->EffectTriggerSpell[0])
            pSpellInfo = sServerFacade.LookupSpellInfo(pSpellInfo->EffectTriggerSpell[0]);

        if (pSpellInfo->Effect[0] != SPELL_EFFECT_TELEPORT_UNITS && pSpellInfo->Effect[1] != SPELL_EFFECT_TELEPORT_UNITS && pSpellInfo->Effect[2] != SPELL_EFFECT_TELEPORT_UNITS)
            continue;

        SpellTargetPosition const* pos = sSpellMgr.GetSpellTargetPosition(pSpellInfo->Id);

        if (!pos)
            continue;

        std::vector<std::pair<uint32, uint32>> ranges = RpgLocationsNear(WorldPosition(pos), areaNames);

        for (auto& range : ranges)
            newPoints.push_back(std::make_pair(std::make_pair(range.first, range.second), pos));
    }

    //Creatures.
    for (auto& creatureData : WorldPosition().getCreaturesNear(0, 0))
    {
        CreatureInfo const* cInfo = ObjectMgr::GetCreatureTemplate(creatureData->second.id);

        if (!cInfo)
            continue;

        if (cInfo->ExtraFlags & CREATURE_EXTRA_FLAG_INVISIBLE)
            continue;

        std::vector<uint32> allowedNpcFlags;

        allowedNpcFlags.push_back(UNIT_NPC_FLAG_BATTLEMASTER);
        allowedNpcFlags.push_back(UNIT_NPC_FLAG_BANKER);
        allowedNpcFlags.push_back(UNIT_NPC_FLAG_AUCTIONEER);
        allowedNpcFlags.push_back(UNIT_NPC_FLAG_TRAINER);
        allowedNpcFlags.push_back(UNIT_NPC_FLAG_VENDOR);
        allowedNpcFlags.push_back(UNIT_NPC_FLAG_REPAIR);
        allowedNpcFlags.push_back(UNIT_NPC_FLAG_INNKEEPER);

        for (auto flag : allowedNpcFlags)
        {          
            if ((cInfo->NpcFlags & flag) != 0)
            {
                std::vector<std::pair<uint32, uint32>> ranges = RpgLocationsNear(WorldPosition(creatureData), areaNames);

                if (cInfo->NpcFlags & UNIT_NPC_FLAG_INNKEEPER)
                {
                    for (auto& range : ranges)
                        innPoints.push_back(std::make_pair(std::make_pair(range.first, range.second), creatureData));
                }
                else
                {
                    for (auto& range : ranges)
                        newPoints.push_back(std::make_pair(std::make_pair(range.first, range.second), creatureData));
                }
                break;
            }
        }
    }

    for (auto newPoint : newPoints)
        rpgLocsCacheLevel[newPoint.first.first][newPoint.first.second].push_back(newPoint.second);
    
    for (auto innPoint : innPoints)
        innCacheLevel[innPoint.first.first][innPoint.first.second].push_back(std::make_pair(innPoint.second, innPoint.second));
}

void RandomPlayerbotMgr::PrintTeleportCache()
{
    sPlayerbotAIConfig.openLog("telecache.csv", "w");

    for (auto& l : sRandomPlayerbotMgr.locsPerLevelCache)
    {
        uint32 level = l.first;
        for (auto& p : l.second)
        {
            std::ostringstream out;
            out << level << ",";
            WorldPosition(p).printWKT(out);
            out << "LEVEL" << ",0," << WorldPosition(p).getAreaName(true, true);
            sPlayerbotAIConfig.log("telecache.csv", out.str().c_str());
        }
    }

    for (auto r : sRandomPlayerbotMgr.rpgLocsCacheLevel)
    {
        uint32 race =  r.first;
        for (auto& l : r.second)
        {
            uint32 level = l.first;
            for (auto& p : l.second)
            {
                std::ostringstream out;
                out << level << ",";
                WorldPosition(p).printWKT(out);
                out << "RPG" << "," << race << "," << WorldPosition(p).getAreaName(true, true);
                sPlayerbotAIConfig.log("telecache.csv", out.str().c_str());
            }
        }
    }
}

living::RelocationOutcome RandomPlayerbotMgr::RandomTeleportForLevel(Player* bot, bool activeOnly, bool scheduleNextOnCompletion, bool reviveRecovery)
{
    if (bot->InBattleGround())
        return living::RelocationOutcome::Rejected;

    living::PendingRelocation flags;
    flags.bindInn = true;
    flags.reviveRecovery = reviveRecovery;
    flags.scheduleNextTeleport = scheduleNextOnCompletion;

    sLog.outDetail("Preparing location to random teleporting bot %s for level %u", bot->GetName(), bot->GetLevel());
    living::RelocationOutcome outcome = RandomTeleport(bot, locsPerLevelCache[bot->GetLevel()], flags, activeOnly);
    if (outcome == living::RelocationOutcome::Rejected && activeOnly)
    {
        // Legacy fallback: when the active-zone restriction leaves nothing, retry
        // without it.
        outcome = RandomTeleport(bot, locsPerLevelCache[bot->GetLevel()], flags, false);
    }

    // Refresh and closest-inn binding are owed on COMPLETION and run in
    // FinalizeRelocation from the post-acknowledgement position.
    return outcome;
}

living::RelocationOutcome RandomPlayerbotMgr::RandomTeleport(Player* bot, bool reviveRecovery)
{
    if (bot->InBattleGround())
        return living::RelocationOutcome::Rejected;

    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "RandomTeleport");
    std::vector<WorldLocation> locs;

    std::list<Unit*> targets;
    float range = sPlayerbotAIConfig.randomBotTeleportDistance;
    MaNGOS::AnyUnitInObjectRangeCheck u_check(bot, range);
    MaNGOS::UnitListSearcher<MaNGOS::AnyUnitInObjectRangeCheck> searcher(targets, u_check);
    Cell::VisitAllObjects(bot, searcher, range);

    if (!targets.empty())
    {
        for (std::list<Unit *>::iterator i = targets.begin(); i != targets.end(); ++i)
        {
            Unit* unit = *i;
            // Calculate from the unit's position WITHOUT moving the live player:
            // this used to SetPosition() the bot onto each candidate in turn, so a
            // failed or empty calculation left it silently relocated to the last
            // unit, and the following Refresh() could resurrect it there.
            // FleeManager takes the start position explicitly for exactly this.
            FleeManager manager(bot, sPlayerbotAIConfig.sightDistance, 0, true, WorldPosition(unit));
            float rx, ry, rz;
            if (manager.CalculateDestination(&rx, &ry, &rz))
            {
                WorldLocation loc(bot->GetMapId(), rx, ry, rz);
                locs.push_back(loc);
            }
        }

        pmo.reset();

        // Actually consume the candidates: they were computed and then dropped, so
        // this path never relocated the bot at all. Refresh is owed on completion
        // (FinalizeRelocation), not on acceptance.
        if (!locs.empty())
        {
            living::PendingRelocation flags;
            flags.reviveRecovery = reviveRecovery;

            living::RelocationOutcome outcome = RandomTeleport(bot, locs, flags, true);
            if (outcome != living::RelocationOutcome::Rejected)
                return outcome;
        }

        // Fall through to the level-based relocation when no flee spot worked.
    }

    pmo.reset();

    return RandomTeleportForLevel(bot, true, false, reviveRecovery);
}

void RandomPlayerbotMgr::InstaRandomize(Player* bot)
{
    sRandomPlayerbotMgr.Randomize(bot);

    if(bot->GetLevel() > sWorld.getConfig(CONFIG_UINT32_START_PLAYER_LEVEL))
        sRandomPlayerbotMgr.RandomTeleportForLevel(bot, false);
}

void RandomPlayerbotMgr::Randomize(Player* bot)
{
    if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported() || bot->GetSession()->isLogingOut())
        return;

    bool initialRandom = false;
    if (bot->GetLevel() <= sPlayerbotAIConfig.randombotStartingLevel)
        initialRandom = true;
#ifdef MANGOSBOT_TWO
    else if (bot->GetLevel() < 60 && bot->getClass() == CLASS_DEATH_KNIGHT)
        initialRandom = true;
#endif

    // give bot random level if is above or below level sync
    if (!initialRandom && players.size() && sPlayerbotAIConfig.syncLevelWithPlayers)
    {
        uint32 maxLevel = std::max(sPlayerbotAIConfig.randomBotMinLevel, std::min(playersLevel + sPlayerbotAIConfig.syncLevelMaxAbove, sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL)));
        if (bot->GetLevel() > maxLevel || (bot->GetLevel() + sPlayerbotAIConfig.syncLevelMaxAbove) < playersLevel)
            initialRandom = true;
    }

    if (initialRandom)
    {
        RandomizeFirst(bot);
        sLog.outDetail("Bot #%d %s:%d <%s>: gear/level randomised", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName());
    }
    else if (sPlayerbotAIConfig.randomGearUpgradeEnabled)
    {
        UpdateGearSpells(bot);
        sLog.outDetail("Bot #%d %s:%d <%s>: gear upgraded", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName());
    }
    else
    {
        // schedule randomise
        uint32 randomTime = urand(sPlayerbotAIConfig.minRandomBotRandomizeTime, sPlayerbotAIConfig.maxRandomBotRandomizeTime);
        SetEventValue(bot->GetGUIDLow(), "randomize", 1, randomTime);
    }

    //SetValue(bot, "version", MANGOSBOT_VERSION);
}

void RandomPlayerbotMgr::UpdateGearSpells(Player* bot)
{
    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "UpgradeGear");

    uint32 maxLevel = sPlayerbotAIConfig.randomBotMaxLevel;
    if (maxLevel > sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
        maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);

    uint32 lastLevel = GetValue(bot, "level");
    uint32 level = bot->GetLevel();
    PlayerbotFactory factory(bot, level);
    factory.Randomize(true, false);

    if (lastLevel != level)
        SetValue(bot, "level", level);

    // schedule randomise
    uint32 randomTime = urand(sPlayerbotAIConfig.minRandomBotRandomizeTime, sPlayerbotAIConfig.maxRandomBotRandomizeTime);
    SetEventValue(bot->GetGUIDLow(), "randomize", 1, randomTime);
}

void RandomPlayerbotMgr::RandomizeFirst(Player* bot)
{
    uint32 maxLevel = sPlayerbotAIConfig.randomBotMaxLevel;
    if (maxLevel > sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
        maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);

    // if lvl sync is enabled, max level is limited by online players lvl
    if (sPlayerbotAIConfig.syncLevelWithPlayers)
        maxLevel = std::max(sPlayerbotAIConfig.randomBotMinLevel, std::min(playersLevel+ sPlayerbotAIConfig.syncLevelMaxAbove, sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL)));

    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "RandomizeFirst");
    uint32 level = urand(std::max(uint32(sWorld.getConfig(CONFIG_UINT32_START_PLAYER_LEVEL)), sPlayerbotAIConfig.randomBotMinLevel), maxLevel);

#ifdef MANGOSBOT_TWO
    if (bot->getClass() == CLASS_DEATH_KNIGHT)
        level = urand(std::max(bot->GetLevel(), sWorld.getConfig(CONFIG_UINT32_START_HEROIC_PLAYER_LEVEL)), std::max(sWorld.getConfig(CONFIG_UINT32_START_HEROIC_PLAYER_LEVEL), maxLevel));
#endif

    if (urand(0, 100) < 100 * sPlayerbotAIConfig.randomBotMaxLevelChance && level < maxLevel)
        level = maxLevel;

#ifndef MANGOSBOT_ZERO
    if (sWorldState.GetExpansion() == EXPANSION_NONE && level > 60)
        level = 60;
#endif

#ifdef MANGOSBOT_TWO
    // do not allow level down death knights
    if (bot->getClass() == CLASS_DEATH_KNIGHT && level < sWorld.getConfig(CONFIG_UINT32_START_HEROIC_PLAYER_LEVEL))
        return;

    // only randomise death knights to min lvl 60
    if (bot->getClass() == CLASS_DEATH_KNIGHT && level < 60)
        level = 60;
#endif

    if (level == sWorld.getConfig(CONFIG_UINT32_START_PLAYER_LEVEL))
        return;

    SetValue(bot, "level", level);
    PlayerbotFactory factory(bot, level);
    factory.Randomize(false, false);

    // schedule randomise
    uint32 randomTime = urand(sPlayerbotAIConfig.minRandomBotRandomizeTime, sPlayerbotAIConfig.maxRandomBotRandomizeTime);
    SetEventValue(bot->GetGUIDLow(), "randomize", 1, randomTime);

    bool hasPlayer = bot->GetPlayerbotAI()->HasRealPlayerMaster();
    bot->GetPlayerbotAI()->Reset(!hasPlayer);

    if (bot->GetGroup() && !hasPlayer)
        bot->RemoveFromGroup();
}

uint32 RandomPlayerbotMgr::GetZoneLevel(uint16 mapId, float teleX, float teleY, float teleZ)
{
	uint32 maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);

	uint32 level;
    auto results = WorldDatabase.PQuery("SELECT AVG(t.minlevel) minlevel, AVG(t.maxlevel) maxlevel FROM creature c "
            "INNER JOIN creature_template t ON c.id = t.entry "
            "WHERE map = '%u' AND minlevel > 1 AND abs(position_x - '%f') < '%u' AND abs(position_y - '%f') < '%u'",
            mapId, teleX, sPlayerbotAIConfig.randomBotTeleportDistance / 2, teleY, sPlayerbotAIConfig.randomBotTeleportDistance / 2);

    if (results)
    {
        Field* fields = results->Fetch();
        uint8 minLevel = fields[0].GetUInt8();
        uint8 maxLevel = fields[1].GetUInt8();
        level = urand(minLevel, maxLevel);
        if (level > maxLevel)
            level = maxLevel;
    }
    else
    {
        level = urand(1, maxLevel);
    }

    return level;
}

void RandomPlayerbotMgr::Refresh(Player* bot, bool resetAi)
{
    if (bot->IsBeingTeleportedFar() || !bot->IsInWorld())
        return;

    if (sServerFacade.UnitIsDead(bot))
    {
        bot->ResurrectPlayer(1.0f);
        bot->SpawnCorpseBones();
        bot->GetPlayerbotAI()->ResetStrategies();
    }

    if (sPlayerbotAIConfig.disableRandomLevels)
        return;

    if (bot->InBattleGround())
        return;

    sLog.outDetail("Refreshing bot #%d <%s>", bot->GetGUIDLow(), bot->GetName());
    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "Refresh");

    if (resetAi)
        bot->GetPlayerbotAI()->Reset();

    bot->DurabilityRepairAll(false, 1.0f
#ifndef MANGOSBOT_ZERO
        , false
#endif
    );
	bot->SetHealthPercent(100);
	bot->SetPvP(true);

    PlayerbotFactory factory(bot, bot->GetLevel());
    factory.Refresh();

    if (bot->GetMaxPower(POWER_MANA) > 0)
        bot->SetPower(POWER_MANA, bot->GetMaxPower(POWER_MANA));

    if (bot->GetMaxPower(POWER_ENERGY) > 0)
        bot->SetPower(POWER_ENERGY, bot->GetMaxPower(POWER_ENERGY));

    // Checked money grant, mirroring the cores' own AddMoney clamp: an
    // unchecked add can wrap past MAX_MONEY_AMOUNT.
    uint32 money = bot->GetMoney();
    uint32 bonus = static_cast<uint32>(500 * sqrt(urand(1, bot->GetLevel() * 5)));
    bot->SetMoney(money < uint32(MAX_MONEY_AMOUNT) - bonus ? money + bonus : uint32(MAX_MONEY_AMOUNT));
}

bool RandomPlayerbotMgr::IsRandomBot(Player* bot)
{
    if (bot && bot->GetPlayerbotAI())
    {
        if (bot->GetPlayerbotAI()->IsRealPlayer())
            return false;
    }
    if (bot)
    {
        if (sPlayerbotAIConfig.IsInRandomAccountList(bot->GetSession()->GetAccountId()))
            return true;

        return IsRandomBot(bot->GetGUIDLow());
    }

    return false;
}

bool RandomPlayerbotMgr::IsRandomBot(uint32 bot)
{
    ObjectGuid guid = ObjectGuid(HIGHGUID_PLAYER, bot);
    if (sPlayerbotAIConfig.IsInRandomAccountList(sObjectMgr.GetPlayerAccountIdByGUID(guid)))
        return true;

    return GetEventValue(bot, "add");
}

// The ONE canonical currentBots loader, COUNT-first so a confirmed empty set
// is distinguishable from a failed query (a plain row query returns null for
// BOTH). The canonical activation requires `add` active AND `logout`
// inactive - the `add` marker alone also matched bots already marked logged
// out. SuccessRows/SuccessEmpty replace the vector; QueryFailed leaves it
// untouched for the caller's dirty handling.
living::CountedLoadOutcome RandomPlayerbotMgr::LoadCurrentBotsFromDb()
{
    static char const* activeBotsWhere =
        "FROM ai_playerbot_random_bots WHERE owner = 0 AND event = 'add' AND `value` > 0 "
        "AND bot NOT IN (SELECT bot FROM ai_playerbot_random_bots WHERE owner = 0 AND event = 'logout' AND `value` > 0)";

    std::optional<uint64> count;
    if (auto countResult = CharacterDatabase.PQuery("SELECT COUNT(*) %s", activeBotsWhere))
        count = countResult->Fetch()[0].GetUInt32();

    bool rowsLoaded = false;
    std::list<uint32> loaded;
    if (count && *count > 0)
    {
        if (auto rows = CharacterDatabase.PQuery("SELECT bot %s", activeBotsWhere))
        {
            do
            {
                loaded.push_back(rows->Fetch()[0].GetUInt32());
            } while (rows->NextRow());

            rowsLoaded = true;
        }
    }

    living::CountedLoadOutcome const outcome = living::ClassifyCountedLoad(count, rowsLoaded);
    if (outcome != living::CountedLoadOutcome::QueryFailed)
        currentBots = std::move(loaded);

    return outcome;
}

std::list<uint32> RandomPlayerbotMgr::GetBots()
{
    // After an uncertain activation the nonempty in-memory vector is NOT
    // trusted: reconcile from canonical durable state. A confirmed EMPTY
    // result is a valid reconciliation (zero active bots) and clears the
    // dirty flag - AddRandomBots may proceed; only a FAILED query keeps the
    // flag and serves the last known vector rather than fabricating an empty
    // one.
    if (currentBotsDirty)
    {
        switch (LoadCurrentBotsFromDb())
        {
            case living::CountedLoadOutcome::SuccessRows:
            case living::CountedLoadOutcome::SuccessEmpty:
                currentBotsDirty = false;
                break;
            case living::CountedLoadOutcome::QueryFailed:
                sLog.outError("GetBots: reconciliation query failed; bot list stays dirty");
                break;
        }

        return currentBots;
    }

    if (!currentBots.empty()) return currentBots;

    // Initial load through the same canonical loader. A failure marks the
    // list dirty: mutation (AddRandomBots) stays refused until a load
    // succeeds - query failure is never treated as "no bots exist".
    if (LoadCurrentBotsFromDb() == living::CountedLoadOutcome::QueryFailed)
    {
        currentBotsDirty = true;
        sLog.outError("GetBots: initial bot list load failed; bot list marked dirty");
    }

    return currentBots;
}

std::list<uint32> RandomPlayerbotMgr::GetBgBots(uint32 bracket)
{
    //if (!currentBgBots.empty()) return currentBgBots;

    auto results = CharacterDatabase.PQuery(
        "SELECT bot FROM ai_playerbot_random_bots WHERE event = 'bg' AND value = '%d'", bracket);
    std::list<uint32> BgBots;
    if (results)
    {
        do
        {
            Field* fields = results->Fetch();
            uint32 bot = fields[0].GetUInt32();
            BgBots.push_back(bot);
        } while (results->NextRow());
    }

    return BgBots;
}

bool RandomPlayerbotMgr::TryGetEventValue(uint32 bot, std::string const& event, uint32& value)
{
    // Bulk-load all durable events on the first read - with an EXPLICIT load
    // state instead of the "per-bot map is empty" heuristic. That heuristic
    // treated a failed query as confirmed absence: the first default-inserted
    // zero made the map nonempty, permanently suppressing the bulk load while
    // durable add/logout/specNo state stayed hidden. Here a COUNT first
    // separates "confirmed empty" from "could not ask"; a failed load leaves
    // the state Unknown and every later read retries - a sibling cached event
    // cannot stop the retry.
    living::EventCacheLoadState& loadState = eventCacheLoadState[bot];
    if (living::EventCacheNeedsBulkLoad(loadState))
    {
        living::EventCacheLoadState const previousState = loadState;
        std::optional<uint64> count;
        if (auto countResult = CharacterDatabase.PQuery(
            "SELECT COUNT(*) FROM ai_playerbot_random_bots WHERE owner = 0 AND bot = '%u'", bot))
            count = countResult->Fetch()[0].GetUInt32();

        // Rows are loaded into a FRESH map and swapped in only on success:
        // merging into the existing map would let a stale cached sibling that
        // is absent from durable rows survive a recovered load that then
        // marks the cache authoritative. On failure the old map (prior KNOWN
        // values) is preserved untouched.
        bool rowsLoaded = false;
        std::map<std::string, CachedEvent> loaded;
        if (count && *count > 0)
        {
            auto results = CharacterDatabase.PQuery("SELECT `event`, `value`, `time`, validIn, `data` FROM ai_playerbot_random_bots WHERE owner = 0 AND bot = '%u'", bot);
            if (results)
            {
                do
                {
                    Field* fields = results->Fetch();
                    std::string eventName = fields[0].GetString();
                    CachedEvent e;
                    e.value = fields[1].GetUInt32();
                    e.lastChangeTime = fields[2].GetUInt32();
                    e.validIn = fields[3].GetUInt32();
                    e.data = fields[4].GetString();
                    loaded[eventName] = e;
                } while (results->NextRow());

                rowsLoaded = true;
            }
        }

        living::CountedLoadOutcome const outcome = living::ClassifyCountedLoad(count, rowsLoaded);
        loadState = living::ResolveEventCacheBulkLoad(outcome);
        if (outcome != living::CountedLoadOutcome::QueryFailed)
        {
            // The load is authoritative: replace the cache (SuccessEmpty
            // clears it) and drop any per-event dirty marks - durable truth
            // has just been re-established for the whole bot.
            eventCache[bot] = std::move(loaded);
            dirtyEvents.erase(bot);
        }
        // Log the transition once, not every retried read.
        else if (previousState != living::EventCacheLoadState::Unknown)
            sLog.outError("GetEventValue: bulk event load failed for bot %u; state stays unknown and will be retried", bot);
    }

    // Retry the reload of an individually dirty event even while sibling
    // events keep the per-bot map populated (the bulk load above only runs
    // while the load state demands it). Until a reload succeeds, the prior
    // KNOWN value is served - a failed query is not absence.
    bool eventStillDirty = false;
    if (auto botDirty = dirtyEvents.find(bot); botDirty != dirtyEvents.end() && botDirty->second.count(event))
    {
        if (ReloadEventRow(bot, event) != living::EventReloadOutcome::QueryFailed)
        {
            botDirty->second.erase(event);
            if (botDirty->second.empty())
                dirtyEvents.erase(botDirty);
        }
        else
        {
            // The dirty reload failed again: durable state stays unknown. The
            // write that marked this event dirty may already have landed, so the
            // still-cached value cannot be trusted to gate a lifecycle mutation.
            // Report NOT trusted (the stale value remains in `value` for
            // diagnostics) until a later reload reconciles the row.
            eventStillDirty = true;
        }
    }

    // Plain lookup WITHOUT default-inserting: a fabricated zero entry is what
    // used to turn "load failed" into permanent confirmed absence.
    CachedEvent e;
    bool hasCachedEntry = false;
    if (auto botCache = eventCache.find(bot); botCache != eventCache.end())
    {
        if (auto entry = botCache->second.find(event); entry != botCache->second.end())
        {
            e = entry->second;
            hasCachedEntry = true;
        }
    }

    // Expiry interpretation is shared with the host tests: lifecycle-control
    // rows (creation/deletion obligations) are non-expiring - a first login
    // more than 15 days after creation must still see its owed markers.
    if ((time(0) - e.lastChangeTime) >= e.validIn && !living::IsNonExpiringEvent(event))
        e.value = 0;

    value = e.value;

    // Known: a cached entry (confirmed write or successful load), or a
    // completed bulk load confirming absence. Absent + Unloaded/Unknown reads
    // as zero but reports NOT KNOWN - destructive callers skip. A still-dirty
    // event whose reload could not reconcile also reports NOT trusted.
    return living::EventValueTrusted(loadState, hasCachedEntry, eventStillDirty);
}

uint32 RandomPlayerbotMgr::GetEventValue(uint32 bot, std::string event)
{
    uint32 value = 0;
    TryGetEventValue(bot, event, value);
    return value;
}

bool RandomPlayerbotMgr::TryReadRequiredEvents(uint32 bot, std::initializer_list<std::pair<char const*, uint32*>> reads)
{
    for (auto const& read : reads)
        if (!TryGetEventValue(bot, read.first, *read.second))
            return false;

    return true;
}

uint32 RandomPlayerbotMgr::RemainingValidity(uint32 bot, std::string const& event)
{
    int32 const remaining = GetValueValidTime(bot, event);
    return remaining > 0 ? static_cast<uint32>(remaining) : 0;
}

bool RandomPlayerbotMgr::RunActivationPlan(uint32 bot, std::vector<living::PlannedEventWrite> const& plan)
{
    switch (living::ExecuteActivationPlan(plan,
        [this, bot](std::string const& event, uint32 value, uint32 validIn)
        {
            // TYPED durability result: the plan executor must distinguish "the
            // failed write provably did not mutate" from "it may have applied
            // despite reporting failure" - the latter restores the failed
            // event itself during compensation.
            return SetEventValueEx(bot, event, value, validIn);
        }))
    {
        case living::ActivationOutcome::Persisted:
            return true;
        case living::ActivationOutcome::FailedCompensated:
            sLog.outError("Activation plan for bot %u failed; written values compensated - nothing was started", bot);
            return false;
        case living::ActivationOutcome::FailedCompensationUncertain:
        default:
            currentBotsDirty = true;
            sLog.outError("Activation plan for bot %u failed AND compensation is uncertain; bot list marked dirty for reconciliation", bot);
            return false;
    }
}

int32 RandomPlayerbotMgr::GetValueValidTime(uint32 bot, std::string event)
{
    if (eventCache.find(bot) == eventCache.end())
        return 0;

    if (eventCache[bot].find(event) == eventCache[bot].end())
        return 0;

    CachedEvent e = eventCache[bot][event];

    return e.validIn-(time(0) - e.lastChangeTime);
}

std::string RandomPlayerbotMgr::GetEventData(uint32 bot, std::string event)
{
    std::string data = "";
    if (GetEventValue(bot, event))
    {
        CachedEvent e = eventCache[bot][event];
        data = e.data;
    }
    return data;
}

bool RandomPlayerbotMgr::SetEventValue(uint32 bot, std::string event, uint32 value, uint32 validIn, std::string data)
{
    return SetEventValueEx(bot, event, value, validIn, data) == living::EventWriteResult::DesiredStateConfirmed;
}

living::EventWriteResult RandomPlayerbotMgr::SetEventValueEx(uint32 bot, std::string const& event, uint32 value, uint32 validIn, std::string const& data)
{
    // This is the shared persistence boundary for event metadata, and callers
    // legitimately pass user-influenced strings (e.g. CreateBot's gear/group
    // values).
    //
    // Schema limits are validated BEFORE any mutation: an over-limit value
    // would either fail the INSERT after the DELETE (strict SQL) or be silently
    // truncated (permissive SQL) while the cache kept the original - both
    // diverge DB from cache.
    if (!living::EventValueFitsSchema(event, data))
    {
        sLog.outError("SetEventValue rejected for bot %u: event '%s' (%zu bytes, max %zu) or data (%zu bytes, max %zu) exceeds the schema",
            bot, event.c_str(), event.size(), living::EVENT_NAME_MAX_BYTES, data.size(), living::EVENT_DATA_MAX_BYTES);
        return living::EventWriteResult::DefinitelyNotApplied;
    }

    // Capture the KNOWN prior state (raw cached value, expiry not applied)
    // BEFORE any statement: a reported execution failure is classified
    // against it - "the prior value remained" is DefinitelyNotApplied, "the
    // requested value landed anyway" is DesiredStateConfirmed, anything else
    // is StateUnknown.
    living::EventRow priorRow;
    bool priorPresent = false;
    if (auto botCache = eventCache.find(bot); botCache != eventCache.end())
    {
        if (auto entry = botCache->second.find(event); entry != botCache->second.end())
        {
            priorRow = living::EventRow{entry->second.value, entry->second.lastChangeTime, entry->second.validIn, entry->second.data};
            priorPresent = true;
        }
    }
    living::EventCacheLoadState priorLoadState = living::EventCacheLoadState::Unloaded;
    if (auto stateIt = eventCacheLoadState.find(bot); stateIt != eventCacheLoadState.end())
        priorLoadState = stateIt->second;
    // A prior state that is still dirty (an earlier reported-failed write that
    // never reconciled) is NOT trustworthy: it can never prove
    // DefinitelyNotApplied, so a same-value refresh over a dirty row stays
    // StateUnknown rather than being falsely confirmed.
    bool const priorKnown = living::EventValueTrusted(priorLoadState, priorPresent, IsEventDirty(bot, event));

    // Escape here, once: an unescaped quote used to delete the prior row, fail
    // the INSERT, and leave the in-memory cache diverged from the DB. The
    // unescaped originals stay in the cache so memory matches what a round-trip
    // through the driver produces.
    std::string escapedEvent = event;
    std::string escapedData = data;
    CharacterDatabase.escape_string(escapedEvent);
    CharacterDatabase.escape_string(escapedData);

    // Execution-confirmed persistence only. In the pinned cores PExecute merely
    // queues while a transaction is open, async CommitTransaction returns true
    // after queueing, and CommitTransactionDirect discards the transaction's
    // execution result - none of those return values can confirm durability.
    // The old unconditional BeginTransaction here also nested (and asserted)
    // inside callers that already batch, so this setter owns no transaction: it
    // runs exactly one synchronous DirectPExecute whose return value is the
    // actual execution result. A nonzero value UPDATEs existing matching rows
    // or INSERTs a new one (decided by a synchronous probe); zero DELETEs.
    uint32 const now = (uint32)time(0);
    living::EventPersistOutcome const outcome = living::PersistEventValue(event, data, value,
        [&]() -> std::optional<bool>
        {
            // COUNT(*) always yields a row, so a null result is a failed probe,
            // never an empty one.
            auto result = CharacterDatabase.PQuery(
                "SELECT COUNT(*) FROM ai_playerbot_random_bots WHERE owner = 0 AND bot = '%u' AND event = '%s'",
                bot, escapedEvent.c_str());
            if (!result)
                return std::nullopt;

            return result->Fetch()[0].GetUInt32() > 0;
        },
        [&](living::EventWriteKind kind)
        {
            switch (kind)
            {
                case living::EventWriteKind::Delete:
                    return CharacterDatabase.DirectPExecute(
                        "DELETE FROM ai_playerbot_random_bots WHERE owner = 0 AND bot = '%u' AND event = '%s'",
                        bot, escapedEvent.c_str());
                case living::EventWriteKind::Update:
                    return CharacterDatabase.DirectPExecute(
                        "UPDATE ai_playerbot_random_bots SET `time` = '%u', validIn = '%u', `value` = '%u', `data` = '%s' WHERE owner = 0 AND bot = '%u' AND event = '%s'",
                        now, validIn, value, escapedData.c_str(), bot, escapedEvent.c_str());
                case living::EventWriteKind::Insert:
                default:
                    return CharacterDatabase.DirectPExecute(
                        "INSERT INTO ai_playerbot_random_bots (owner, bot, `time`, validIn, event, `value`, `data`) VALUES ('%u', '%u', '%u', '%u', '%s', '%u', '%s')",
                        0, bot, now, validIn, escapedEvent.c_str(), value, escapedData.c_str());
            }
        });

    if (outcome == living::EventPersistOutcome::Rejected || outcome == living::EventPersistOutcome::ProbeFailed)
    {
        // Both happen BEFORE any mutating statement: prior state is
        // confirmed unchanged.
        sLog.outError("SetEventValue could not persist event '%s' for bot %u (%s); cache left unchanged",
            event.c_str(), bot, outcome == living::EventPersistOutcome::Rejected ? "schema" : "probe failed");
        return living::EventWriteResult::DefinitelyNotApplied;
    }

    if (outcome == living::EventPersistOutcome::ExecuteFailed)
    {
        // The statement REPORTED failure after the probe - which is not proof
        // it did not mutate. Reload and classify against the requested and
        // prior states; a failed reload NEVER becomes confirmed absence: the
        // prior known value stays cached, the entry is marked dirty, and the
        // result is StateUnknown so compensating callers restore this event
        // too.
        sLog.outError("SetEventValue failed to persist event '%s' for bot %u; reloading durable value", event.c_str(), bot);

        // Reload and classify against the COMPLETE requested and prior rows, not
        // the value alone: a same-value refresh with a new expiry/data, and an
        // expired add/teleport re-scheduled to the same raw value, must NOT be
        // confirmed just because the stale value still matches. Deletion is
        // confirmed only by a confirmed-absent row.
        living::EventReloadOutcome const reload = ReloadEventRow(bot, event);
        living::EventRow durableRow;
        if (reload == living::EventReloadOutcome::Found)
        {
            if (auto botCache = eventCache.find(bot); botCache != eventCache.end())
                if (auto entry = botCache->second.find(event); entry != botCache->second.end())
                    durableRow = living::EventRow{entry->second.value, entry->second.lastChangeTime, entry->second.validIn, entry->second.data};
        }

        living::EventRow const requestedRow{value, now, validIn, data};
        living::EventWriteResult const classified = living::ClassifyFailedWriteReload(
            reload, /*isDeletion=*/ value == 0, requestedRow, durableRow, priorKnown, priorPresent, priorRow);

        // A failed reload leaves durable state unknown: keep the prior cached
        // value for diagnostics but mark the entry dirty so later typed reads
        // stay untrusted and keep retrying until reconciliation succeeds.
        if (reload == living::EventReloadOutcome::QueryFailed)
            dirtyEvents[bot].insert(event);

        return classified;
    }

    CachedEvent e(value, now, validIn, data);
    eventCache[bot][event] = e;

    // A confirmed write makes the cache authoritative again for this event.
    if (auto botDirty = dirtyEvents.find(bot); botDirty != dirtyEvents.end())
    {
        botDirty->second.erase(event);
        if (botDirty->second.empty())
            dirtyEvents.erase(botDirty);
    }

    return living::EventWriteResult::DesiredStateConfirmed;
}

living::EventReloadOutcome RandomPlayerbotMgr::ReloadEventRow(uint32 bot, std::string const& event)
{
    std::string escapedEvent = event;
    CharacterDatabase.escape_string(escapedEvent);

    // COUNT first: it always yields a row on success, so a null result is a
    // FAILED query, never an empty one - the only way to tell "no row" apart
    // from "could not ask".
    auto countResult = CharacterDatabase.PQuery(
        "SELECT COUNT(*) FROM ai_playerbot_random_bots WHERE owner = 0 AND bot = '%u' AND event = '%s'",
        bot, escapedEvent.c_str());
    if (!countResult)
        return living::EventReloadOutcome::QueryFailed;

    if (countResult->Fetch()[0].GetUInt32() == 0)
    {
        eventCache[bot].erase(event);
        return living::EventReloadOutcome::ConfirmedAbsent;
    }

    auto rows = CharacterDatabase.PQuery(
        "SELECT `value`, `time`, validIn, `data` FROM ai_playerbot_random_bots WHERE owner = 0 AND bot = '%u' AND event = '%s'",
        bot, escapedEvent.c_str());
    if (!rows)
        return living::EventReloadOutcome::QueryFailed;

    // Last row wins, mirroring the bulk loader in GetEventValue.
    do
    {
        Field* fields = rows->Fetch();
        eventCache[bot][event] = CachedEvent(fields[0].GetUInt32(), fields[1].GetUInt32(), fields[2].GetUInt32(), fields[3].GetString());
    } while (rows->NextRow());

    return living::EventReloadOutcome::Found;
}

bool RandomPlayerbotMgr::IsEventDirty(uint32 bot, std::string const& event) const
{
    auto botDirty = dirtyEvents.find(bot);
    return botDirty != dirtyEvents.end() && botDirty->second.count(event) > 0;
}

uint32 RandomPlayerbotMgr::GetValue(uint32 bot, std::string type)
{
    return GetEventValue(bot, type);
}

uint32 RandomPlayerbotMgr::GetValue(Player* bot, std::string type)
{
    return GetValue(bot->GetObjectGuid().GetCounter(), type);
}

std::string RandomPlayerbotMgr::GetData(uint32 bot, std::string type)
{
    return GetEventData(bot, type);
}

bool RandomPlayerbotMgr::SetValue(uint32 bot, std::string type, uint32 value, std::string data, int32 validIn)
{
    return SetEventValue(bot, type, value, validIn == -1 ? 15*24*3600 : validIn, data);
}

bool RandomPlayerbotMgr::SetValue(Player* bot, std::string type, uint32 value, std::string data, int32 validIn)
{
    return SetValue(bot->GetObjectGuid().GetCounter(), type, value, data, validIn);
}

bool RandomPlayerbotMgr::HandlePlayerbotConsoleCommand(ChatHandler* handler, char const* args)
{
    if (!sPlayerbotAIConfig.enabled)
    {
        sLog.outError("Playerbot system is currently disabled!");
        return false;
    }

    bool isRA = false;
    
    if (handler->GetSession()) //Client command
        isRA = true;
    else if (static_cast<CliHandler*>(handler) && static_cast<CliHandler*>(handler)->GetAccountId()) //RA call with account.
        isRA = true;

    if (!args || !*args)
    {
        sLog.outError("Usage: rndbot help/stats/update/reset/init/refresh/add/remove/more..");
        if (isRA)
            handler->SendSysMessage("Usage: rndbot help/stats/update/reset/init/refresh/add/remove/more..");

        std::list<std::string> messages = sRandomPlayerbotMgr.HandleHelp("");

        for (auto& msg : messages)
        {
            sLog.outString("%s", msg.c_str());
            if (isRA)
                handler->SendSysMessage(msg.c_str());
        }

        return true;
    }

    std::string cmd = args;

    std::map<std::string, ConsoleCommandHandler> handlers;
    handlers["help"] = &RandomPlayerbotMgr::HandleHelp;
    handlers["reset"] = &RandomPlayerbotMgr::HandleConsoleReset;
    handlers["stats"] = &RandomPlayerbotMgr::HandleConsoleStats;
    handlers["update"] = &RandomPlayerbotMgr::HandleConsoleUpdate;
    handlers["pid"] = &RandomPlayerbotMgr::HandleConsolePid;
    handlers["diff"] = &RandomPlayerbotMgr::HandleConsoleDiff;
    handlers["clean map"] = &RandomPlayerbotMgr::HandleConsoleCleanMap;
    handlers["login debug"] = &RandomPlayerbotMgr::HandleConsoleLoginDebug;

    for (auto& [prefix, consoleHandler] : handlers)
    {
        // Exact dispatch: "reset" or "reset ...", never "resetX"/"pidX1 2 3".
        // Prefix-only matching plus a blind one-character strip used to let a
        // typo invoke destructive handlers (reset deletes every random-bot row).
        std::string param;
        if (!living::MatchExactCommand(cmd, prefix, param))
            continue;

        if (prefix == "stats")
            param = handler->GetSession() ? std::to_string(handler->GetSession()->GetPlayer()->GetObjectGuid()) : "";

        std::list<std::string> messages = (sRandomPlayerbotMgr.*consoleHandler)(param);
        for (auto& msg : messages)
        {
            sLog.outString("%s", msg.c_str());
            if(isRA)
                handler->SendSysMessage(msg.c_str());      
        }

        if (!messages.empty() && (prefix != "help" || param != "commands"))
            return true;
    }

    // Per-command target policy: destructive commands never acquire implicit
    // bulk targeting. BulkAll is set ONLY for commands whose help documents a
    // bare bulk form; init's contract is "the first available bot", so its bare
    // form deterministically targets one bot, never every bot. `acceptsParams`
    // marks the commands whose help documents extra arguments; everything else
    // rejects unexpected arguments with usage instead of silently ignoring
    // them.
    struct PlayerCommandSpec
    {
        ConsolePlayerCommandHandler handler;
        living::CommandTargetMode mode;
        bool acceptsParams;
        // Set for commands whose operand is threaded to a param-aware handler
        // (change_strategy). When present, `handler` is unused and this is
        // invoked with the parsed operand instead.
        ConsolePlayerCommandParamHandler paramHandler = nullptr;
    };

    std::map<std::string, PlayerCommandSpec> playerHandlers;
    playerHandlers["init"] = { &RandomPlayerbotMgr::HandleRandomizeFirst, living::CommandTargetMode::FirstAvailable, false };
    playerHandlers["upgrade"] = { &RandomPlayerbotMgr::HandleUpdateGearSpells, living::CommandTargetMode::BulkAll, false };
    playerHandlers["refresh"] = { &RandomPlayerbotMgr::HandleRefresh, living::CommandTargetMode::BulkAll, false };
    playerHandlers["teleport"] = { &RandomPlayerbotMgr::HandleRandomTeleportForLevel, living::CommandTargetMode::BulkAll, false };
    playerHandlers["rpg"] = { &RandomPlayerbotMgr::HandleRandomTeleportForRpg, living::CommandTargetMode::BulkAll, false };
    playerHandlers["revive"] = { &RandomPlayerbotMgr::HandleRevive, living::CommandTargetMode::BulkAll, false };
    playerHandlers["grind"] = { &RandomPlayerbotMgr::HandleRandomTeleport, living::CommandTargetMode::BulkAll, false };
    playerHandlers["change_strategy"] = { nullptr, living::CommandTargetMode::ExactOne, true, &RandomPlayerbotMgr::HandleChangeStrategy };
    playerHandlers["remove"] = { &RandomPlayerbotMgr::HandleRemove, living::CommandTargetMode::ExactOne, false };

    for (auto& [prefix, spec] : playerHandlers)
    {
        // Exact dispatch, same rule as above: "removeBob"/"reviveX" must not
        // resolve to remove/revive.
        std::string nameAndParams;
        if (!living::MatchExactCommand(cmd, prefix, nameAndParams))
            continue;

        auto const emit = [&](std::string const& msg)
        {
            sLog.outString("%s", msg.c_str());
            if (isRA)
                handler->SendSysMessage(msg.c_str());
        };

        // Tokenize with whitespace collapsing: "remove  Bob" is the exact
        // target "Bob", never an empty bulk target.
        std::vector<std::string> tokens;
        size_t pos = 0;
        while (pos < nameAndParams.size())
        {
            size_t const end = nameAndParams.find(' ', pos);
            std::string const token = nameAndParams.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            pos = end == std::string::npos ? nameAndParams.size() : end + 1;
            if (!token.empty())
                tokens.push_back(token);
        }

        std::string const name = tokens.empty() ? "" : tokens[0];

        std::string params;
        for (size_t i = 1; i < tokens.size(); ++i)
            params += (i > 1 ? " " : "") + tokens[i];

        // Destructive/per-bot commands require one explicit exact target; bare
        // and whitespace-only forms return usage without touching any bot.
        if (name.empty() && spec.mode == living::CommandTargetMode::ExactOne)
        {
            emit(GetCommandTexts(prefix));
            return true;
        }

        // Undocumented extra arguments are rejected with usage, never silently
        // ignored or misread as part of a target name.
        if (!params.empty() && !spec.acceptsParams)
        {
            emit(GetCommandTexts(prefix));
            return true;
        }

        // Phase 1: resolve stable targets by GUID without mutating anything. A
        // named target matches EXACTLY - prefix matching made "remove Bob" also
        // remove Bobby - and cardinality is the MODE's property: bare init
        // resolves at most ONE bot (lowest online GUID, deterministically),
        // while only BulkAll commands fan out.
        std::vector<std::pair<uint32, std::string>> onlineBots;
        sRandomPlayerbotMgr.ForEachPlayerbot([&](Player* bot) {
            onlineBots.emplace_back(bot->GetGUIDLow(), bot->GetName());
        });

        std::vector<uint32> targets = living::ResolveCommandTargets(spec.mode, name, onlineBots);

        if (targets.empty())
        {
            emit(name.empty() ? "No random bots are online" : "No random bot named '" + name + "' is online");
            return true;
        }

        // Phase 2: execute against re-fetched players, never against iterators
        // of a container the handler may invalidate (removal logs the bot out
        // and erases it from the very map ForEachPlayerbot walks).
        for (uint32 targetGuid : targets)
        {
            Player* bot = sRandomPlayerbotMgr.GetPlayerBot(targetGuid);
            if (!bot)
                continue;

            std::list<std::string> messages = spec.paramHandler
                ? (sRandomPlayerbotMgr.*spec.paramHandler)(bot, params)
                : (sRandomPlayerbotMgr.*spec.handler)(bot);
            for (auto& msg : messages)
                emit(msg);
        }

        return true;
    }

    std::list<std::string> messages = sRandomPlayerbotMgr.HandlePlayerbotCommand(args, handler->GetSession() ? handler->GetSession()->GetPlayer():nullptr, static_cast<CliHandler*>(handler) ? static_cast<CliHandler*>(handler)->GetAccessLevel() : SEC_PLAYER);
    for (std::list<std::string>::iterator i = messages.begin(); i != messages.end(); ++i)
    {
        sLog.outString("%s", i->c_str());
        if (isRA)
            handler->SendSysMessage(i->c_str());
    }

    if (!messages.empty())
        return true;

    if (isRA)
        handler->SendSysMessage("usage: help/list/reload/more.. or add/init/remove/more.. PLAYERNAME");

    return true;
}

void RandomPlayerbotMgr::HandleCommand(uint32 type, const std::string& text, Player& fromPlayer, std::string channelName, Team team, uint32 lang)
{
    ForEachPlayerbot([&](Player* bot)
    {
        if (type == CHAT_MSG_SAY)
        {
            if (bot->GetMapId() != fromPlayer.GetMapId() || sServerFacade.GetDistance2d(bot, &fromPlayer) > 25)
            {
                return;
            }
        }

        if (type == CHAT_MSG_YELL)
        {
            if (bot->GetMapId() != fromPlayer.GetMapId() || sServerFacade.GetDistance2d(bot, &fromPlayer) > 300)
            {
                return;
            }
        }

        if (team != TEAM_BOTH_ALLOWED && bot->GetTeam() != team)
        {
            return;
        }

        if (type == CHAT_MSG_GUILD && bot->GetGuildId() != fromPlayer.GetGuildId())
        {
            return;
        }

        if (!channelName.empty())
        {
            if (ChannelMgr* cMgr = channelMgr(bot->GetTeam()))
            {
                Channel* chn = cMgr->GetChannel(channelName, bot);
                if (!chn)
                {
                    return;
                }
            }
        }

        bot->GetPlayerbotAI()->HandleCommand(type, text, fromPlayer, lang);
    });
}

void RandomPlayerbotMgr::OnPlayerLogout(Player* player)
{
    bool hadPlayerBot = GetPlayerBot(player->GetGUIDLow());

    DisablePlayerBot(player->GetGUIDLow());

    if (!hadPlayerBot && player->GetPlayerbotAI() && player->GetPlayerbotAI()->IsRealPlayer() && player->GetGroup() && sPlayerbotAIConfig.IsFreeAltBot(player))
        player->GetSession()->SetOffline(); //Prevent groupkick

    ForEachPlayerbot([&](Player* bot) {
        PlayerbotAI* ai = bot->GetPlayerbotAI();
        if (player == ai->GetMaster())
        {
            ai->SetMaster(NULL);
            if (!bot->InBattleGround())
            {
                ai->ResetStrategies();
            }
        }
    });

    players.erase(player->GetGUIDLow());
}

void RandomPlayerbotMgr::OnBotLoginInternal(Player * const bot)
{
    // Pending-relocation invalidation on login lives in the shared
    // PlayerbotHolder::OnBotLogin, which runs for every holder and master
    // arrangement (this hook is skipped for real-player-mastered bots).

    sLog.outDetail("%u/%d Bot %s logged in", GetPlayerbotsAmount(), sRandomPlayerbotMgr.GetMaxAllowedBotCount(), bot->GetName());
	//if (loginProgressBar && playerBots.size() < sRandomPlayerbotMgr.GetMaxAllowedBotCount()) { loginProgressBar->step(); }
	//if (loginProgressBar && playerBots.size() >= sRandomPlayerbotMgr.GetMaxAllowedBotCount() - 1) {
    //if (loginProgressBar && playerBots.size() + 1 >= sRandomPlayerbotMgr.GetMaxAllowedBotCount()) {
	//	sLog.outString("All bots logged in");
    //    delete loginProgressBar;
	//}
}

void RandomPlayerbotMgr::OnPlayerLogin(Player* player)
{
    if (!sPlayerbotAIConfig.enabled)
        return;

    ForEachPlayerbot([&](Player* bot)
    {
        if (player == bot)
            return;

        Group* group = bot->GetGroup();
        if (!group)
            return;

        for (GroupReference *gref = group->GetFirstMember(); gref; gref = gref->next())
        {
            Player* member = gref->getSource();
            PlayerbotAI* ai = bot->GetPlayerbotAI();
            if (member == player && (!ai->GetMaster() || ai->GetMaster()->GetPlayerbotAI()))
            {
                if (!bot->InBattleGround())
                {
                    ai->SetMaster(player);
                    ai->ResetStrategies();
                    ai->TellPlayer(ai->GetMaster(), BOT_TEXT("hello"));
                }
                break;
            }
        }
    });

    if (IsFreeBot(player))
    {
        uint32 guid = player->GetGUIDLow();
        if (!sPlayerbotAIConfig.IsFreeAltBot(player))
           SetEventValue(guid, "login", 0, 0);
    }
    else
    {
        players[player->GetGUIDLow()] = player;
        sLog.outDebug("Including non-random bot player %s into random bot update", player->GetName());
    }
}

void RandomPlayerbotMgr::OnPlayerLoginError(uint32 bot)
{
    bool cleared = SetEventValue(bot, "add", 0, 0);
    cleared = SetEventValue(bot, "login", 0, 0) && cleared;
    currentBots.remove(bot);
    if (!cleared)
    {
        failedLoginCleanups.insert(bot);
        currentBotsDirty = true;
        sLog.outError("OnPlayerLoginError: activation clear for bot %u not confirmed; bot list marked dirty", bot);
    }
    else
        failedLoginCleanups.erase(bot);
}

void RandomPlayerbotMgr::RetryFailedLoginCleanups()
{
    for (auto it = failedLoginCleanups.begin(); it != failedLoginCleanups.end();)
    {
        uint32 const bot = *it;
        bool cleared = SetEventValue(bot, "add", 0, 0);
        cleared = SetEventValue(bot, "login", 0, 0) && cleared;
        if (!cleared)
        {
            currentBotsDirty = true;
            ++it;
            continue;
        }

        currentBots.remove(bot);
        it = failedLoginCleanups.erase(it);
    }
}

Player* RandomPlayerbotMgr::GetRandomPlayer()
{
    if (players.empty())
        return NULL;

    uint32 index = urand(0, players.size() - 1);
    return players[index];
}

Player* RandomPlayerbotMgr::GetPlayer(uint32 playerGuid)
{
    PlayerBotMap::const_iterator it = players.find(playerGuid);
    return (it == players.end()) ? nullptr : it->second ? it->second : nullptr;
}

void RandomPlayerbotMgr::PrintStats(uint32 requesterGuid)
{
    Player* requester = GetPlayer(requesterGuid);
    std::stringstream ss; ss << GetPlayerbotsAmount() << " Random Bots online";
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    std::map<uint32, int> alliance, horde;
    for (uint32 i = 0; i < 10; ++i)
    {
        alliance[i] = 0;
        horde[i] = 0;
    }

    std::map<uint8, int> perRace, perClass;
    for (uint8 race = RACE_HUMAN; race < MAX_RACES; ++race)
    {
        perRace[race] = 0;
    }
    for (uint8 cls = CLASS_WARRIOR; cls < MAX_CLASSES; ++cls)
    {
        perClass[cls] = 0;
    }

    uint32 dps = 0, heal = 0, tank = 0, active = 0, update = 0, randomize = 0, teleport = 0, changeStrategy = 0, dead = 0, combat = 0, revive = 0, taxi = 0, moving = 0, mounted = 0, afk = 0;
    int stateCount[(uint8)TravelState::MAX_TRAVEL_STATE + 1] = { 0 };
    std::vector<std::pair<Quest const*, int32>> questCount;

    ForEachPlayerbot([this, &dps, &heal, &tank, &active, &update, &randomize, &teleport, &changeStrategy, &dead, &combat, &revive, &taxi, &moving, &mounted, &afk, &alliance, &horde, &perRace, &perClass, &stateCount, &questCount](Player* bot)
    {
        if (IsAlliance(bot->getRace()))
            alliance[bot->GetLevel() / 10]++;
        else
            horde[bot->GetLevel() / 10]++;

        perRace[bot->getRace()]++;
        perClass[bot->getClass()]++;

        if (bot->GetPlayerbotAI()->AllowActivity())
            active++;

        if (bot->GetPlayerbotAI()->GetAiObjectContext()->GetValue<bool>("random bot update")->Get())
            update++;

        uint32 botId = bot->GetGUIDLow();
        if (!GetEventValue(botId, "randomize"))
            randomize++;

        if (!GetEventValue(botId, "teleport"))
            teleport++;

        if (!GetEventValue(botId, "change_strategy"))
            changeStrategy++;

        if (bot->IsTaxiFlying())
            taxi++;

        if (bot->IsMoving() && !bot->IsTaxiFlying() && !bot->IsFlying())
            moving++;

        if (bot->IsMounted() && !bot->IsTaxiFlying())
            mounted++;

        if (bot->IsInCombat())
            combat++;

        if (bot->isAFK())
            afk++;

        if (sServerFacade.UnitIsDead(bot))
        {
            dead++;
            //if (!GetEventValue(botId, "dead"))
            //    revive++;
        }

        int spec = AiFactory::GetPlayerSpecTab(bot);
        switch (bot->getClass())
        {
        case CLASS_DRUID:
            if (spec == 2)
                heal++;
            else
                dps++;
            break;
        case CLASS_PALADIN:
            if (spec == 1)
                tank++;
            else if (spec == 0)
                heal++;
            else
                dps++;
            break;
        case CLASS_PRIEST:
            if (spec != 2)
                heal++;
            else
                dps++;
            break;
        case CLASS_SHAMAN:
            if (spec == 2)
                heal++;
            else
                dps++;
            break;
        case CLASS_WARRIOR:
            if (spec == 2)
                tank++;
            else
                dps++;
            break;
#ifdef MANGOSBOT_TWO
        case CLASS_DEATH_KNIGHT:
            if (spec == 0)
                tank++;
            else
                dps++;
            break;
#endif
        default:
            dps++;
            break;
        }

        TravelTarget* target = bot->GetPlayerbotAI()->GetAiObjectContext()->GetValue<TravelTarget*>("travel target")->Get();
        if (target)
        {
            TravelState state = target->GetTravelState();
            stateCount[(uint8)state]++;            
        }
    });

    ss.str(""); ss << "Bots level:";
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

	uint32 maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);
	for (uint32 i = 0; i < 10; ++i)
    {
        if (!alliance[i] && !horde[i])
            continue;

        uint32 from = i*10;
        uint32 to = std::min(from + 9, maxLevel);
        if (!from) from = 1;

        ss.str(""); ss << "    " << from << ".." << to << ": " << alliance[i] << " alliance, " << horde[i] << " horde";
        sLog.outString("%s", ss.str().c_str());
        if (requester) { requester->SendMessageToPlayer(ss.str()); }
    }

    ss.str(""); ss << "Bots race:";
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    for (uint8 race = RACE_HUMAN; race < MAX_RACES; ++race)
    {
        if (perRace[race])
        {
            ss.str(""); ss << "    " << ChatHelper::formatRace(race) << ": " << perRace[race];
            sLog.outString("%s", ss.str().c_str());
            if (requester) { requester->SendMessageToPlayer(ss.str()); }
        }
    }

    ss.str(""); ss << "Bots class:";
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    for (uint8 cls = CLASS_WARRIOR; cls < MAX_CLASSES; ++cls)
    {
        if (perClass[cls])
        {
            ss.str(""); ss << "    " << ChatHelper::formatClass(cls) << ": " << perClass[cls];
            sLog.outString("%s", ss.str().c_str());
            if (requester) { requester->SendMessageToPlayer(ss.str()); }
        }
    }

    ss.str(""); ss << "Bots role:";
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    tank: " << tank << ", heal: " << heal << ", dps: " << dps;
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "Bots status:";
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    Active: " << active;
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    Moving: " << moving;
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    //sLog.outString("Bots to:");
    //sLog.outString("    update: %d", update);
    //sLog.outString("    randomize: %d", randomize);
    //sLog.outString("    teleport: %d", teleport);
    //sLog.outString("    change_strategy: %d", changeStrategy);
    //sLog.outString("    revive: %d", revive);

    ss.str(""); ss << "    On taxi: " << taxi;
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    On mount: " << mounted;
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    In combat: " << combat;
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    Dead: " << dead;
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    AFK: " << afk;
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "Bots questing:";
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    Picking quests: " << stateCount[(uint8)TravelState::TRAVEL_STATE_TRAVEL_PICK_UP_QUEST] + stateCount[(uint8)TravelState::TRAVEL_STATE_WORK_PICK_UP_QUEST];
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    Doing quests: " << stateCount[(uint8)TravelState::TRAVEL_STATE_TRAVEL_DO_QUEST] + stateCount[(uint8)TravelState::TRAVEL_STATE_WORK_DO_QUEST];
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    Completing quests: " << stateCount[(uint8)TravelState::TRAVEL_STATE_TRAVEL_HAND_IN_QUEST] + stateCount[(uint8)TravelState::TRAVEL_STATE_WORK_HAND_IN_QUEST];
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    Idling: " << stateCount[(uint8)TravelState::TRAVEL_STATE_IDLE];
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }
}

double RandomPlayerbotMgr::GetBuyMultiplier(Player* bot)
{
    uint32 id = bot->GetGUIDLow();
    // Typed gate: an unknown read serves a neutral in-memory default without
    // rewriting the durable multiplier.
    uint32 value = 0;
    if (!TryGetEventValue(id, "buymultiplier", value))
        return 1.0;

    if (!value)
    {
        value = urand(50, 120);
        uint32 validIn = urand(sPlayerbotAIConfig.minRandomBotsPriceChangeInterval, sPlayerbotAIConfig.maxRandomBotsPriceChangeInterval);
        SetEventValue(id, "buymultiplier", value, validIn);
    }

    return (double)value / 100.0;
}

double RandomPlayerbotMgr::GetSellMultiplier(Player* bot)
{
    uint32 id = bot->GetGUIDLow();
    // Typed gate, same rule as the buy multiplier.
    uint32 value = 0;
    if (!TryGetEventValue(id, "sellmultiplier", value))
        return 1.0;

    if (!value)
    {
        value = urand(80, 250);
        uint32 validIn = urand(sPlayerbotAIConfig.minRandomBotsPriceChangeInterval, sPlayerbotAIConfig.maxRandomBotsPriceChangeInterval);
        SetEventValue(id, "sellmultiplier", value, validIn);
    }

    return (double)value / 100.0;
}

void RandomPlayerbotMgr::AddTradeDiscount(Player* bot, Player* master, int32 value)
{
    if (!master) return;
    uint32 discount = GetTradeDiscount(bot, master);
    int32 result = (int32)discount + value;
    discount = (result < 0 ? 0 : result);

    SetTradeDiscount(bot, master, discount);
}

void RandomPlayerbotMgr::SetTradeDiscount(Player* bot, Player* master, uint32 value)
{
    if (!master) return;
    uint32 botId =  bot->GetGUIDLow();
    uint32 masterId =  master->GetGUIDLow();
    std::ostringstream name; name << "trade_discount_" << masterId;
    SetEventValue(botId, name.str(), value, sPlayerbotAIConfig.maxRandomBotInWorldTime);
}

uint32 RandomPlayerbotMgr::GetTradeDiscount(Player* bot, Player* master)
{
    if (!master) return 0;
    uint32 botId =  bot->GetGUIDLow();
    uint32 masterId = master->GetGUIDLow();
    std::ostringstream name; name << "trade_discount_" << masterId;
    return GetEventValue(botId, name.str());
}

std::string RandomPlayerbotMgr::HandleRemoteCommand(std::string request)
{
    std::string::iterator pos = find(request.begin(), request.end(), ',');
    if (pos == request.end())
    {
        std::ostringstream out; out << "invalid request: " << request;
        return out.str();
    }

    std::string command = std::string(request.begin(), pos);
    uint32 guid = std::atoi(std::string(pos + 1, request.end()).c_str());
    Player* bot = GetPlayerBot(guid);
    if (!bot)
        return "invalid guid";

    PlayerbotAI *ai = bot->GetPlayerbotAI();
    if (!ai)
        return "invalid guid";

    return ai->HandleRemoteCommand(command);
}

void RandomPlayerbotMgr::ChangeStrategy(Player* player)
{
    uint32 bot = player->GetGUIDLow();

    if (urand(0, 100) > 100 * sPlayerbotAIConfig.randomBotRpgChance) // select grind / pvp
    {
        sLog.outDetail("Bot #%d %s:%d <%s>: sent to grind spot", bot, player->GetTeam() == ALLIANCE ? "A" : "H", player->GetLevel(), player->GetName());
        // teleport in different places only if players are online.
        // The next teleport is scheduled in FinalizeRelocation once this one
        // actually completed; a rejected or never-acknowledged relocation is not
        // recorded as completed work and the next update cycle retries it.
        RandomTeleportForLevel(player, !players.empty(), /*scheduleNextOnCompletion*/ true);
    }
    else
    {
        sLog.outDetail("Bot #%d %s:%d <%s>: sent to inn", bot, player->GetTeam() == ALLIANCE ? "A" : "H", player->GetLevel(), player->GetName());
        RandomTeleportForRpg(player, !players.empty(), /*scheduleNextOnCompletion*/ true);
    }
}

living::RelocationOutcome RandomPlayerbotMgr::RandomTeleportForRpg(Player* bot, bool activeOnly, bool scheduleNextOnCompletion)
{
    uint32 race = bot->getRace();
    uint32 level = bot->GetLevel();

    living::PendingRelocation flags;
    flags.setHomebind = true;         // RPG camps hearth the bot to the landing spot
    flags.rpgTravelCooldown = true;
    flags.scheduleNextTeleport = scheduleNextOnCompletion;

    sLog.outDetail("Random teleporting bot %s for RPG (%zu locations available)", bot->GetName(), rpgLocsCacheLevel[race][level].size());
    living::RelocationOutcome outcome = RandomTeleport(bot, rpgLocsCacheLevel[race][level], flags, activeOnly);
    if (outcome == living::RelocationOutcome::Rejected && activeOnly)
    {
        // Legacy fallback: when the active-zone restriction leaves nothing, retry
        // without it (see RandomTeleportForLevel).
        outcome = RandomTeleport(bot, rpgLocsCacheLevel[race][level], flags, false);
    }

    // Refresh, the homebind, and the travel cooldown are owed on COMPLETION and
    // run in FinalizeRelocation.
    return outcome;
}

void RandomPlayerbotMgr::Remove(Player* bot)
{
    uint32 owner = bot->GetGUIDLow();
    // Explicit removal (rows are about to be deleted): FORCE-cancel so even a
    // Finalizing record's owed durable work is abandoned - the bot is gone.
    CancelPendingRelocation(owner, living::RelocationCancelMode::Force);

    // Execution-confirmed delete: the cache is cleared only after the rows are
    // actually gone (a queued PExecute is not durable success, and clearing
    // first published a state the DB might never reach). The load state is
    // dropped with it so the next read re-establishes durable truth.
    if (CharacterDatabase.DirectPExecute("DELETE FROM ai_playerbot_random_bots WHERE owner = 0 AND bot = '%d'", owner))
        ForgetEventCache(owner);
    else
        sLog.outError("Remove: failed to delete random-bot events for bot %u; cache left for reload", owner);

    LogoutPlayerBot(owner);
}

const CreatureDataPair* RandomPlayerbotMgr::GetCreatureDataByEntry(uint32 entry)
{
    if (entry != 0 && ObjectMgr::GetCreatureTemplate(entry))
    {
        FindCreatureData worker(entry, NULL);
        sObjectMgr.DoCreatureData(worker);
        CreatureDataPair const* dataPair = worker.GetResult();
        return dataPair;
    }
    return NULL;
}

uint32 RandomPlayerbotMgr::GetCreatureGuidByEntry(uint32 entry)
{
    uint32 guid = 0;

    CreatureDataPair const* dataPair = sRandomPlayerbotMgr.GetCreatureDataByEntry(entry);
    guid = dataPair->first;

    return guid;
}

uint32 RandomPlayerbotMgr::GetBattleMasterEntry(Player* bot, BattleGroundTypeId bgTypeId, bool fake)
{
    Team team = bot->GetTeam();
    uint32 entry = 0;
    std::vector<uint32> Bms;

    for (auto i = begin(BattleMastersCache[team][bgTypeId]); i != end(BattleMastersCache[team][bgTypeId]); ++i)
    {
        Bms.insert(Bms.end(), *i);
    }

    for (auto i = begin(BattleMastersCache[TEAM_BOTH_ALLOWED][bgTypeId]); i != end(BattleMastersCache[TEAM_BOTH_ALLOWED][bgTypeId]); ++i)
    {
        Bms.insert(Bms.end(), *i);
    }

    if (Bms.empty())
        return entry;

    float dist1 = FLT_MAX;

    for (auto i = begin(Bms); i != end(Bms); ++i)
    {
        CreatureDataPair const* dataPair = sRandomPlayerbotMgr.GetCreatureDataByEntry(*i);
        if (!dataPair)
            continue;

        CreatureData const* data = &dataPair->second;

        Unit* Bm = sMapMgr.FindMap((uint32)data->mapid)->GetUnit(ObjectGuid(HIGHGUID_UNIT, *i, dataPair->first));
        if (!Bm)
            continue;

        if (bot->GetMapId() != Bm->GetMapId())
            continue;

        // return first available guid on map if queue from anywhere
        if (fake)
        {
            entry = *i;
            break;
        }

        AreaTableEntry const* area = GetAreaEntryByAreaID(sServerFacade.GetAreaId(Bm));
        if (!area)
            continue;

        if (area->team == 4 && bot->GetTeam() == ALLIANCE)
            continue;
        if (area->team == 2 && bot->GetTeam() == HORDE)
            continue;

        if (Bm->GetDeathState() == DEAD)
            continue;

        float dist2 = sServerFacade.GetDistance2d(bot, data->posX, data->posY);
        if (dist2 < dist1)
        {
            dist1 = dist2;
            entry = *i;
        }
    }

    return entry;
}

void RandomPlayerbotMgr::Hotfix(Player* bot, uint32 version)
{
    PlayerbotFactory factory(bot, bot->GetLevel());
    uint32 exp = bot->GetUInt32Value(PLAYER_XP);
    uint32 level = bot->GetLevel();
    uint32 id = bot->GetGUIDLow();

    for (int fix = version; fix <= MANGOSBOT_VERSION; fix++)
    {
        int count = 0;
        switch (fix)
        {
            case 1: // Apply class quests to previously made random bots

                if (level < 10)
                {
                    break;
                }

                for (std::list<uint32>::iterator i = factory.classQuestIds.begin(); i != factory.classQuestIds.end(); ++i)
                {
                    uint32 questId = *i;
                    Quest const *quest = sObjectMgr.GetQuestTemplate(questId);

                    if (!bot->SatisfyQuestClass(quest, false) ||
                        quest->GetMinLevel() > bot->GetLevel() ||
                        !bot->SatisfyQuestRace(quest, false) || bot->GetQuestStatus(questId) == QUEST_STATUS_COMPLETE)
                        continue;

                    bot->SetQuestStatus(questId, QUEST_STATUS_COMPLETE);
                    bot->RewardQuest(quest, 0, bot, false);
                    bot->SetLevel(level);
                    bot->SetUInt32Value(PLAYER_XP, exp);
                    sLog.outDetail("Bot %d rewarded quest %d",
                        bot->GetGUIDLow(), questId);
                    count++;
                }

                if (count > 0)
                {
                    sLog.outDetail("Bot %d hotfix (Class Quests), %d quests rewarded",
                        bot->GetGUIDLow(), count);
                    count = 0;
                }
                break;
            case 2: // Init Riding skill fix

                if (level < 20)
                {
                    break;
                }
                factory.InitSkills();
                sLog.outDetail("Bot %d hotfix (Riding Skill) applied",
                    bot->GetGUIDLow());
                break;

            default:
                break;
        }
    }
    SetValue(bot, "version", MANGOSBOT_VERSION);
    sLog.outDetail("Bot %d hotfix v%d applied",
        bot->GetGUIDLow(), MANGOSBOT_VERSION);
}

void RandomPlayerbotMgr::MirrorAh()
{
    sRandomPlayerbotMgr.m_ahActionMutex.lock();

    ahMirror.clear();

    std::vector<AuctionHouseType> houses = { (AuctionHouseType)0,(AuctionHouseType)1,(AuctionHouseType)2 };

    //Now loops over all houses. Can probably be faction specific later.
    for (auto house : houses)
    {
        AuctionHouseObject* auctionHouse = sAuctionMgr.GetAuctionsMap(house);

        AuctionHouseObject::AuctionEntryMap const& map = auctionHouse->GetAuctions();

        for (auto& auction : map)
        {
            if (!auction.second)
                continue;

            AuctionEntry auctionEntry = *auction.second;

            if (!auctionEntry.buyout)
                continue;

            if (!auctionEntry.itemCount)
                continue;

            ahMirror[auctionEntry.itemTemplate].push_back(auctionEntry);
        }
    }
    sRandomPlayerbotMgr.m_ahActionMutex.unlock();
}

typedef std::unordered_map <uint32, std::list<float>> botPerformanceMetric;
std::unordered_map<std::string, botPerformanceMetric> botPerformanceMetrics;

void RandomPlayerbotMgr::PushMetric(botPerformanceMetric& metric, const uint32 bot, const float value, uint32 maxNum) const
{
    metric[bot].push_back(value);

    if (metric[bot].size() > maxNum)
        metric[bot].pop_front();
}

float RandomPlayerbotMgr::GetMetricDelta(botPerformanceMetric& metric) const
{
    float deltaMetric = 0;
    for (auto& botMetric : metric)
    {
        std::list<float> values = botMetric.second;
        if (values.size() > 1)
            deltaMetric += (values.back() - values.front()) / values.size();
    }

    if (metric.empty())
        return 0;

    return deltaMetric / metric.size();
}

std::string RandomPlayerbotMgr::GetCommandTexts(const std::string& command)
{
    auto texts = GetCommandTexts();
    auto it = texts.find(command);
    if (it != texts.end())
        return it->second;
    return "";
}

std::unordered_map<std::string, std::string> RandomPlayerbotMgr::GetCommandTexts()
{
    return std::unordered_map<std::string, std::string>
    {
        {"init", "Randomize the first available bot (or one exact named bot).\nUsage: init [botname]"},
        {"upgrade", "Update gear and spells for all random bots.\nUsage: upgrade"},
        {"refresh", "Log out and log in all random bots to refresh their status.\nUsage: refresh"},
        {"teleport", "Teleport all random bots to a location suitable for their level.\nUsage: teleport"},
        {"rpg", "Teleport all random bots to a location for RPG activities.\nUsage: rpg"},
        {"revive", "Revive all dead random bots.\nUsage: revive"},
        {"grind", "Teleport all random bots to a grinding location.\nUsage: grind"},
        {"change_strategy", "Change the AI strategy for random bots.\nUsage: change_strategy <botname> <strategy>"},
        {"remove", "Remove a random bot from the server.\nUsage: remove <botname>"},
        {"reset", "Reset all random bots and clear event cache.\nUsage: reset"},
        {"diff", "Show server performance metrics.\nUsage: diff [player_diff] [empty_diff]"},
        {"stats", "Print bot statistics.\nUsage: stats"},
        {"update", "Trigger immediate bot AI update.\nUsage: update"},
        {"pid", "Adjust PID controller values.\nUsage: pid p i d"},
        {"clean map", "Unload and reload map files.\nUsage: clean map"},
        {"login debug", "Toggle login debug mode.\nUsage: login debug"},
        {"cmd", "Send command to a bot.\nUsage: cmd <botname> <command>"},
        {"help", "Show help for commands.\nUsage: help [command]"}
    };
}

std::list<std::string> RandomPlayerbotMgr::HandleHelp(std::string param)
{
    std::list<std::string> messages;
        
    if (param.empty())
    {
        messages.push_back("Type 'help commands for all available commands.");
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
    if (!helpText.empty())
    {
        messages.push_back(helpText);
    }  
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleRandomizeFirst(Player* bot)
{
    std::list<std::string> messages;
    if (!bot)
    {
        messages.push_back("Bot not found");
        return messages;
    }
    RandomizeFirst(bot);
    messages.push_back("init applied to " + std::string(bot->GetName()));
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleUpdateGearSpells(Player* bot)
{
    std::list<std::string> messages;
    if (!bot)
    {
        messages.push_back("Bot not found");
        return messages;
    }
    UpdateGearSpells(bot);
    messages.push_back("upgrade applied to " + std::string(bot->GetName()));
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleRefresh(Player* bot)
{
    std::list<std::string> messages;
    if (!bot)
    {
        messages.push_back("Bot not found");
        return messages;
    }
    Refresh(bot);
    messages.push_back("refresh applied to " + std::string(bot->GetName()));
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleRandomTeleportForLevel(Player* bot)
{
    std::list<std::string> messages;
    if (!bot)
    {
        messages.push_back("Bot not found");
        return messages;
    }
    // Pending means TeleportTo accepted the transfer; completion happens on the
    // teleport acknowledgement (never report accepted work as applied).
    switch (RandomTeleportForLevel(bot))
    {
        case living::RelocationOutcome::Completed:
            messages.push_back("teleport applied to " + std::string(bot->GetName()));
            break;
        case living::RelocationOutcome::Pending:
            messages.push_back("teleport accepted for " + std::string(bot->GetName()) + " (completes on teleport ack)");
            break;
        case living::RelocationOutcome::Rejected:
            messages.push_back("teleport failed for " + std::string(bot->GetName()));
            break;
    }
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleRandomTeleportForRpg(Player* bot)
{
    std::list<std::string> messages;
    if (!bot)
    {
        messages.push_back("Bot not found");
        return messages;
    }
    switch (RandomTeleportForRpg(bot))
    {
        case living::RelocationOutcome::Completed:
            messages.push_back("rpg applied to " + std::string(bot->GetName()));
            break;
        case living::RelocationOutcome::Pending:
            messages.push_back("rpg accepted for " + std::string(bot->GetName()) + " (completes on teleport ack)");
            break;
        case living::RelocationOutcome::Rejected:
            messages.push_back("rpg failed for " + std::string(bot->GetName()));
            break;
    }
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleRevive(Player* bot)
{
    std::list<std::string> messages;
    if (!bot)
    {
        messages.push_back("Bot not found");
        return messages;
    }
    // A refused recovery (e.g. a grouped bot that cannot be safely relocated)
    // keeps its dead/revive markers and must not report success; an accepted one
    // completes - and clears its markers - only on the teleport acknowledgement.
    switch (Revive(bot))
    {
        case living::RelocationOutcome::Completed:
            messages.push_back("revive applied to " + std::string(bot->GetName()));
            break;
        case living::RelocationOutcome::Pending:
            messages.push_back("revive accepted for " + std::string(bot->GetName()) + " (completes on teleport ack)");
            break;
        case living::RelocationOutcome::Rejected:
            messages.push_back("revive failed for " + std::string(bot->GetName()));
            break;
    }
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleRandomTeleport(Player* bot)
{
    std::list<std::string> messages;
    if (!bot)
    {
        messages.push_back("Bot not found");
        return messages;
    }
    switch (RandomTeleport(bot))
    {
        case living::RelocationOutcome::Completed:
            messages.push_back("grind applied to " + std::string(bot->GetName()));
            break;
        case living::RelocationOutcome::Pending:
            messages.push_back("grind accepted for " + std::string(bot->GetName()) + " (completes on teleport ack)");
            break;
        case living::RelocationOutcome::Rejected:
            messages.push_back("grind failed for " + std::string(bot->GetName()));
            break;
    }
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleChangeStrategy(Player* bot, std::string const& strategySpec)
{
    // Apply the REQUESTED strategy. The command used to parse the operand and
    // then discard it, calling RandomPlayerbotMgr::ChangeStrategy (a RANDOM
    // grind/inn relocation - not a strategy change at all). Now the operand is
    // validated against the bot's strategy registry (Engine::addStrategy
    // silently no-ops an unknown name) and applied to its non-combat engine.
    std::list<std::string> messages;
    if (!bot || !bot->GetPlayerbotAI())
    {
        messages.push_back("Bot not found");
        return messages;
    }

    living::StrategyDirective directive;
    switch (living::ParseStrategyDirective(strategySpec, directive))
    {
        case living::StrategyDirectiveParse::Empty:
            messages.push_back(GetCommandTexts("change_strategy"));
            return messages;
        case living::StrategyDirectiveParse::Malformed:
            messages.push_back("Invalid strategy '" + strategySpec + "'");
            return messages;
        case living::StrategyDirectiveParse::Parsed:
            break;
    }

    PlayerbotAI* ai = bot->GetPlayerbotAI();
    for (std::string const& name : directive.addedNames)
    {
        if (!ai->GetAiObjectContext()->GetStrategy(name))
        {
            messages.push_back("Unknown strategy '" + name + "'");
            return messages;
        }
    }

    ai->ChangeStrategy(directive.normalized, BotState::BOT_STATE_NON_COMBAT);
    messages.push_back("change_strategy applied '" + directive.normalized + "' to " + std::string(bot->GetName()));
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleRemove(Player* bot)
{
    std::list<std::string> messages;
    if (!bot)
    {
        messages.push_back("Bot not found");
        return messages;
    }
    // Capture everything needed for the response BEFORE removal: the default
    // logout path deletes the Player, so `bot` must never be dereferenced
    // afterward.
    std::string const removedName = bot->GetName();
    Remove(bot);
    messages.push_back("remove applied to " + removedName);
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleConsoleReset(std::string param)
{
    std::list<std::string> messages;

    // Lifecycle-CONTROL rows are never reset-deleted (see
    // living::IsLifecycleControlEvent, which this exclusion list mirrors):
    // active deletion intents and unfinished creation obligations must
    // survive a console reset, or their owners lose the only durable record.
    // DirectPExecute (synchronous, execution-confirmed): the empty
    // authoritative cache below is published ONLY once the delete provably
    // executed - a queued delete could fail after the cache was already
    // cleared and reads would then claim confirmed absence over live rows.
    if (!CharacterDatabase.DirectPExecute(
            "DELETE FROM ai_playerbot_random_bots WHERE event NOT IN "
            "('temporary', 'delete', 'create pending', 'create levelup', 'create gear', 'create group', 'test')"))
    {
        messages.push_back("Random bot reset FAILED: the delete did not execute; no cache state was changed.");
        return messages;
    }

    // The delete is confirmed: drop the cache, load states, and dirty marks
    // together so typed reads rebuild from durable truth consistently.
    sRandomPlayerbotMgr.eventCache.clear();
    sRandomPlayerbotMgr.eventCacheLoadState.clear();
    sRandomPlayerbotMgr.dirtyEvents.clear();
    std::string msg = "Random bots were reset for all players (lifecycle deletion/creation rows preserved). Please restart the Server.";
    messages.push_back(msg);
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleConsoleStats(std::string param)
{
    // Console-supplied token: the old isValidNumberString + stoull pair accepted
    // a lone sign and threw on overflow.
    uint64 guidRaw = 0;
    if (!living::TryParseUInt64(param, guidRaw))
    {
        return {"Stats: Error parsing " + param};
    }

    std::list<std::string> messages;
    std::string msg = "Stats requested.";
    messages.push_back(msg);

    ObjectGuid guid = ObjectGuid(guidRaw);
    activatePrintStatsThread(guid);
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleConsoleReload(std::string param)
{
    std::list<std::string> messages;
    sPlayerbotAIConfig.Initialize();
    std::string msg = "Playerbot config reloaded.";
    messages.push_back(msg);
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleConsoleUpdate(std::string param)
{
    std::list<std::string> messages;
    sRandomPlayerbotMgr.UpdateAIInternal(0);
    std::string msg = "Playerbot update triggered.";
    messages.push_back(msg);
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleConsolePid(std::string param)
{
    // The dispatcher already stripped the "pid " prefix; the old handler stripped
    // four more characters (dropping valid input, or throwing from substr on short
    // input) and then parsed each token twice with throwing stof.
    std::vector<std::string> pid = Qualified::getMultiQualifiers(param, " ");

    float p = 0.0f, i = 0.0f, d = 0.0f;
    if (pid.size() != 3 ||
        !living::TryParseFloatExact(pid[0], p) ||
        !living::TryParseFloatExact(pid[1], i) ||
        !living::TryParseFloatExact(pid[2], d))
    {
        return {"Usage: pid p i d (three finite numbers)"};
    }

    sRandomPlayerbotMgr.pid.adjust(p, i, d);

    std::list<std::string> messages;
    std::string msg = "Pid set to p:" + std::to_string(p) + " i:" + std::to_string(i) + " d:" + std::to_string(d);
    messages.push_back(msg);
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleConsoleDiff(std::string param)
{
    std::list<std::string> messages;
    if (param.empty())
    {
        std::stringstream ss;
        ss << "Avg diff: " << sWorld.GetAverageDiff() << "\n";
        ss << "Max diff: " << sWorld.GetMaxDiff() << "\n";
        ss << "char db ping: " << sRandomPlayerbotMgr.GetDatabaseDelay("CharacterDatabase") << "\n";
        ss << "Sessions online: " << sWorld.GetActiveSessionCount() << "\n";
        ss << "Bots online: " << sRandomPlayerbotMgr.botCount << " (active: " << sRandomPlayerbotMgr.activeBots << ")";

        messages.push_back(ss.str());
        return messages;
    }
    // Documented usage: diff [player_diff] [empty_diff]. One value sets both.
    // The old handler parsed each token twice with throwing stoi and silently
    // ignored the single-value form.
    std::vector<std::string> diff = Qualified::getMultiQualifiers(param, " ");

    uint32 diffWithPlayer = 0;
    uint32 diffEmpty = 0;
    constexpr uint32 maxDiffMs = 60000;
    if (diff.size() < 1 || diff.size() > 2 ||
        !living::TryParseUInt32InRange(diff[0], 1, maxDiffMs, diffWithPlayer) ||
        !living::TryParseUInt32InRange(diff.size() == 2 ? diff[1] : diff[0], 1, maxDiffMs, diffEmpty))
    {
        return {"Usage: diff [player_diff] [empty_diff] (1.." + std::to_string(maxDiffMs) + " ms)"};
    }

    sPlayerbotAIConfig.diffWithPlayer = diffWithPlayer;
    sPlayerbotAIConfig.diffEmpty = diffEmpty;

    std::string msg = "Diff set to " + std::to_string(diffWithPlayer) + " (player), " + std::to_string(diffEmpty) + " (empty)";
    messages.push_back(msg);
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleConsoleCleanMap(std::string param)
{
    std::list<std::string> messages;
    for (uint32 i = 0; i < sMapStore.GetNumRows(); ++i)
    {
        if (!sMapStore.LookupEntry(i))
            continue;

        uint32 mapId = sMapStore.LookupEntry(i)->MapID;
        boost::thread t([mapId]() {WorldPosition::unloadMapAndVMaps(mapId); });
        t.detach();
    }

    std::string msg = "Map cleaning initiated.";
    messages.push_back(msg);
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleConsoleLoginDebug(std::string param)
{
    std::list<std::string> messages;
    sPlayerBotLoginMgr.ToggleDebug();
    std::string msg = "Login debug toggled.";
    messages.push_back(msg);
    return messages;
}

AccountSelectOutcome RandomPlayerbotMgr::GetOrCreateAccount(Player* master, uint32& accountId, std::string& error)
{
    uint32 const maxCharsPerAccount = MaxCharsPerAccount();

    auto accountNrQr = LoginDatabase.PQuery("SELECT max(replace(lower(username), lower('%s'), '') + 1 - 1) maxAccountNr FROM account WHERE replace(lower(username), lower('%s'), '') != 0", sPlayerbotAIConfig.randomBotAccountPrefix.c_str(), sPlayerbotAIConfig.randomBotAccountPrefix.c_str());

    if (!accountNrQr)
    {
        error = "Failed to find last " + sPlayerbotAIConfig.randomBotAccountPrefix + " account nr.";
        return AccountSelectOutcome::DatabaseUnavailable;
    }

    Field* fields = accountNrQr->Fetch();
    uint32 accountNumber = sPlayerbotAIConfig.randomBotAccountCount;
    uint32 maxAccountNum = fields[0].GetUInt32();

    for (uint32 i = 0; i < 10000; i++)
    {
        std::ostringstream accountNameStr;
        accountNameStr << sPlayerbotAIConfig.randomBotAccountPrefix << accountNumber;
        std::string accountName = accountNameStr.str();

        // Typed existence lookup: only a CONFIRMED Missing may create an
        // account. A failed lookup (the cores' GetId collapses it to 0) used
        // to launch account creation as a side effect of a database outage.
        uint32 candidateId = 0;
        switch (TryLookupAccountId(accountName, candidateId))
        {
            case AccountLookupOutcome::Found:
                break; // capacity check below
            case AccountLookupOutcome::DatabaseUnavailable:
                error = "Database unavailable while looking up bot account";
                return AccountSelectOutcome::DatabaseUnavailable; // abort the scan
            case AccountLookupOutcome::Missing:
            {
                std::string password;
                if (sPlayerbotAIConfig.randomBotRandomPassword)
                {
                    for (int i = 0; i < 10; i++)
                        password += (char)urand('!', 'z');
                }
                else
                    password = accountName;

                LoginDatabase.BeginTransaction();
#ifndef MANGOSBOT_ZERO
                uint8 max_expansion = MAX_EXPANSION;
                AccountOpResult result = sAccountMgr.CreateAccount(accountName, password, max_expansion);
#else
                AccountOpResult result = sAccountMgr.CreateAccount(accountName, password);
#endif
                LoginDatabase.CommitTransactionDirect();

                if (result == AOR_OK)
                {
                    // Verify the created account through the SAME typed path.
                    uint32 createdId = 0;
                    switch (TryLookupAccountId(accountName, createdId))
                    {
                        case AccountLookupOutcome::Found:
                            sPlayerbotAIConfig.randomBotAccounts.push_back(createdId);
                            accountId = createdId;
                            return AccountSelectOutcome::Found;
                        case AccountLookupOutcome::DatabaseUnavailable:
                            error = "Database unavailable while verifying the created bot account";
                            return AccountSelectOutcome::DatabaseUnavailable;
                        case AccountLookupOutcome::Missing:
                            break; // creation claimed success but the account is confirmed absent
                    }
                }

                error = "Failed to create account";
                return AccountSelectOutcome::AccountCreationFailed;
            }
        }

        // Durable characters PLUS pending/quarantined creation reservations:
        // a group loop selecting accounts here must not admit several
        // creations against the same stale durable count. A FAILED count
        // query aborts the WHOLE scan immediately: every probe is a
        // synchronous world-thread query, and walking up to 10,000 further
        // accounts into a database outage amplifies it - the caller reports a
        // transient failure and retries with bounded tick-based backoff.
        uint32 charCount = 0;
        if (!TryGetEffectiveCharacterCount(candidateId, charCount))
        {
            error = "Database unavailable while checking bot account capacity";
            return AccountSelectOutcome::DatabaseUnavailable;
        }

        if (charCount < maxCharsPerAccount)
        {
            if (!sPlayerbotAIConfig.IsInRandomAccountList(candidateId))
            {
                sPlayerbotAIConfig.randomBotAccounts.push_back(candidateId);
            }
            accountId = candidateId;
            return AccountSelectOutcome::Found;
        }

        accountNumber++;
    }

    error = "Failed to find a suitable account.";
    return AccountSelectOutcome::CapacityExhausted;
}

void RandomPlayerbotMgr::OnBotDeleted(uint32 botGuid, uint32 accountId)
{
    if (accountId > 0 && sPlayerbotAIConfig.IsInRandomAccountList(accountId))
    {
        uint32 maxCharsPerAccount = 9;
    #ifdef MANGOSBOT_TWO
        maxCharsPerAccount = 10;
    #endif
    
        // Effective count includes pending creation reservations. The cores
        // collapse a FAILED durable query to zero, and a zero here DELETES the
        // account; unknown or reserved occupancy must never look empty.
        uint32 remainingCharacters = 0;
        if (TryGetEffectiveCharacterCount(accountId, remainingCharacters) && remainingCharacters == 0)
        {
            std::ostringstream prefix;
            prefix << sPlayerbotAIConfig.randomBotAccountPrefix;
            size_t prefixLen = prefix.str().length();
            
            auto result = LoginDatabase.PQuery("SELECT username FROM account WHERE id = '%u'", accountId);
            if (result)
            {
                std::string username = result->Fetch()[0].GetString();
                if (username.substr(0, prefixLen) == prefix.str())
                {
                    uint32 accountNum = std::stoul(username.substr(prefixLen));
                    if (accountNum >= sPlayerbotAIConfig.randomBotAccountCount)
                    {
                        sAccountMgr.DeleteAccount(accountId);
                        sLog.outString("Deleted empty random bot account: %s (id: %u)", username.c_str(), accountId);
                    }
                }
            }
        }
    }
}
