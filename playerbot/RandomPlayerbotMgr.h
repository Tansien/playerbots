#ifndef _RandomPlayerbotMgr_H
#define _RandomPlayerbotMgr_H

#include "Common.h"
#include "PlayerbotAIBase.h"
#include "PlayerbotMgr.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/living/util/LivingActivation.h"
#include "playerbot/living/util/LivingCreationLifecycle.h"
#include "playerbot/living/util/LivingEventSchema.h"
#include "playerbot/living/util/LivingRelocation.h"
#include "WorldPosition.h"
#include <map>
#include <list>
#include <set>

class WorldPacket;
class Player;
class Unit;
class Object;
class Item;

class CachedEvent
{
public:
    CachedEvent() : value(0), lastChangeTime(0), validIn(0), data("") {}
    CachedEvent(const CachedEvent& other) : value(other.value), lastChangeTime(other.lastChangeTime), validIn(other.validIn), data(other.data) {}
    CachedEvent(uint32 value, uint32 lastChangeTime, uint32 validIn, std::string data = "") : value(value), lastChangeTime(lastChangeTime), validIn(validIn), data(data) {}

public:
    bool IsEmpty() { return !lastChangeTime; }

public:
    uint32 value, lastChangeTime, validIn;
    std::string data;
};

class PerformanceMonitorOperation;

//https://gist.github.com/bradley219/5373998

class botPIDImpl;
class botPID
{
public:
    // Kp -  proportional gain
    // Ki -  Integral gain
    // Kd -  derivative gain
    // dt -  loop interval time
    // max - maximum value of manipulated variable
    // min - minimum value of manipulated variable
    botPID(double dt, double max, double min, double Kp, double Ki, double Kd);
    void adjust(double Kp, double Ki, double Kd);
    void reset();
   
    double calculate(double setpoint, double pv);
    ~botPID();

private:
    botPIDImpl* pimpl;
};

class RandomPlayerbotMgr : public PlayerbotHolder
{
    public:
        RandomPlayerbotMgr();
        virtual ~RandomPlayerbotMgr() override;
        static RandomPlayerbotMgr& instance()
        {
            static RandomPlayerbotMgr instance;
            return instance;
        }

        virtual void UpdateAIInternal(uint32 elapsed, bool minimal = false) override;
private:
        void ScaleBotActivity();
        void LogPlayerLocation();
        void DelayedFacingFix();
        void LoginFreeBots();
public:
        // Rebuilds the TRANSIENT post-create scheduler owners (guid ->
        // account) from the durable unsettled markers (create levelup/gear/
        // group, test) at startup/config reload: creation persists those
        // markers durably, but the always-online list they used to ride on is
        // rebuilt from always-state only, which orphaned unfinished work. A
        // failed scan keeps the existing owners - unknown state never removes
        // an owner.
        void ReconstructPostCreateOwners();
        // Drops a guid from the transient owner set (its deletion was adopted).
        void ForgetPostCreateOwner(uint32 botGuid) { postCreateOwners.erase(botGuid); }
        static void DatabasePing(QueryResult* result, uint32 pingStart, std::string db);
        void SetDatabaseDelay(std::string db, uint32 delay) {databaseDelay[db] = delay;}
        uint32 GetDatabaseDelay(std::string db) {if(databaseDelay.find(db) == databaseDelay.end()) return 0; return databaseDelay[db];}

        void LoadNamedLocations();
        bool AddNamedLocation(std::string const& name, WorldLocation const& location);
        bool GetNamedLocation(std::string const& name, WorldLocation& location);

        static bool HandlePlayerbotConsoleCommand(ChatHandler* handler, char const* args);
        bool IsRandomBot(Player* bot);
        bool IsRandomBot(uint32 bot);
        bool IsFreeBot(Player* bot) { return IsRandomBot(bot) || sPlayerbotAIConfig.IsFreeAltBot(bot); }
        bool IsFreeBot(uint32 bot) { return IsRandomBot(bot) || sPlayerbotAIConfig.IsFreeAltBot(bot); }
        void InstaRandomize(Player* bot);
        void Randomize(Player* bot);
        void RandomizeFirst(Player* bot);
        void UpdateGearSpells(Player* bot);
        // Returns whether the schedule event was execution-confirmed persisted
        // (relocation finalization retries a failed write; other callers may
        // ignore the result as before).
        bool ScheduleTeleport(uint32 bot, uint32 time = 0);
        void ScheduleChangeStrategy(uint32 bot, uint32 time = 0);
        void HandleCommand(uint32 type, const std::string& text, Player& fromPlayer, std::string channelName = "", Team team = TEAM_BOTH_ALLOWED, uint32 lang = LANG_UNIVERSAL);
        std::string HandleRemoteCommand(std::string request);
        void OnPlayerLogout(Player* player);
        void OnPlayerLogin(Player* player);
        void OnPlayerLoginError(uint32 bot);
        Player* GetRandomPlayer();
        PlayerBotMap& GetPlayers() { return players; };
        Player* GetPlayer(uint32 playerGuid);
        void PrintStats(uint32 requesterGuid);
        double GetBuyMultiplier(Player* bot);
        double GetSellMultiplier(Player* bot);
        void AddTradeDiscount(Player* bot, Player* master, int32 value);
        void SetTradeDiscount(Player* bot, Player* master, uint32 value);
        uint32 GetTradeDiscount(Player* bot, Player* master);
        // resetAi = false lets a caller that already performed the full
        // Reset(true) (FinalizeRelocation) skip the plain internal reset.
        void Refresh(Player* bot, bool resetAi = true);
        // Relocation/recovery entry points return a typed outcome. In the pinned
        // cores TeleportTo QUEUES a near/far transfer, so acceptance is Pending -
        // Refresh, homebind, inn binding, revive-marker clearing and scheduling
        // are all deferred to FinalizeRelocation, which runs from the teleport
        // acknowledgement. Rejected means nothing was mutated.
        living::RelocationOutcome RandomTeleportForLevel(Player* bot, bool activeOnly, bool scheduleNextOnCompletion = false, bool reviveRecovery = false);
        living::RelocationOutcome RandomTeleportForLevel(Player* bot) { return RandomTeleportForLevel(bot, true); }
        living::RelocationOutcome RandomTeleportForRpg(Player* bot, bool activeOnly, bool scheduleNextOnCompletion = false);
        living::RelocationOutcome RandomTeleportForRpg(Player* bot) { return RandomTeleportForRpg(bot, true); }
        int GetMaxAllowedBotCount();
        bool ProcessBot(Player* player);
        living::RelocationOutcome Revive(Player* player);
        // Resolves the bot's pending relocation after its teleport
        // acknowledgement: verifies the bot is in-world, no longer teleporting,
        // and standing on the EXACT accepted destination, then moves the
        // record to Finalizing and advances its owed completion work (full AI
        // reset and Refresh exactly once; revive-marker clearing; homebind
        // write plus execution-ordered durability verification; RPG cooldown;
        // next-teleport scheduling). The record is released only when EVERY
        // owed operation is confirmed - failed writes are retried from
        // PumpPendingRelocations, and while the record exists no new random
        // relocation may start for the bot. A finished acknowledgement that
        // landed anywhere else terminally cancels the obsolete record (retry
        // markers stay) so no stale work remains armed for a later landing.
        living::RelocationAdvanceResult FinalizeRelocation(Player* bot);
        // Retries owed completion work of Finalizing relocations and applies
        // drained homebind-verification callback events. Runs from the
        // module's world-update hook, never from a SQL result callback.
        void PumpPendingRelocations();
        // Drops a pending relocation without finalizing it (logout/removal/
        // relogin). Retry markers are untouched.
        void CancelPendingRelocation(uint32 botGuid, living::RelocationCancelMode mode = living::RelocationCancelMode::Ordinary);
        // In-flight relocation reservations near a destination (density
        // admission includes bots that ACCEPTED a move there but have not
        // landed/completed yet).
        uint32 CountPendingRelocationsNear(uint32 mapId, float x, float y, float radius, uint32 excludeBotGuid) const
        {
            return relocations.CountReservedDestinationsNear(mapId, x, y, radius, excludeBotGuid);
        }
        // Whether the bot has an in-flight relocation record (either stage);
        // the live density scan skips such bots - they are counted through
        // their reservation instead, so each bot contributes exactly once.
        bool HasPendingRelocation(uint32 botGuid) const { return relocations.HasPending(botGuid); }
        // Drops the in-memory event cache for a GUID whose creation was
        // rejected before persistence, so a discarded transient character
        // leaves no cache entry behind (load state included - the next read
        // reloads from durable truth).
        void ForgetEventCache(uint32 bot) { eventCache.erase(bot); eventCacheLoadState.erase(bot); oneShotMarkers.erase(bot); durableOneShotMarkers.erase(bot); }
        void ChangeStrategy(Player* player);
        uint32 GetValue(Player* bot, std::string type);
        uint32 GetValue(uint32 bot, std::string type);
        // Typed read: returns whether the value is KNOWN (cached entry, or a
        // completed bulk load confirming absence). While the bulk-load state
        // is Unloaded/Unknown an absent event reads as value 0 but NOT known -
        // destructive callers (`.bot always`, the always-online loader, the
        // post-create marker consume) must skip their mutation instead of
        // consuming a load failure as confirmed state.
        bool TryGetEventValue(uint32 bot, std::string const& event, uint32& value);
        int32 GetValueValidTime(uint32 bot, std::string event);
        std::string GetData(uint32 bot, std::string type);
        // Returns false when the value could not be persisted (schema limits or
        // DB failure); the cache is not touched in that case. Callers that
        // require metadata persistence must check the result.
        bool SetValue(uint32 bot, std::string type, uint32 value, std::string data = "", int32 validIn = -1);
        bool SetValue(Player* bot, std::string type, uint32 value, std::string data = "", int32 validIn = -1);
        void Remove(Player* bot);
        void Hotfix(Player* player, uint32 version);
        uint32 GetBattleMasterEntry(Player* bot, BattleGroundTypeId bgTypeId, bool fake = false);
        const CreatureDataPair* GetCreatureDataByEntry(uint32 entry);
        uint32 GetCreatureGuidByEntry(uint32 entry);
        void LoadBattleMastersCache();
        std::map<uint32, std::map<uint32, std::map<uint32, bool> > > NeedBots;
        std::map<uint32, std::map<uint32, std::map<uint32, uint32> > > BgBots;
        std::map<uint32, std::map<uint32, std::map<uint32, uint32> > > VisualBots;
        std::map<uint32, std::map<uint32, std::map<uint32, uint32> > > BgPlayers;
        std::map<uint32, std::map<uint32, std::map<uint32, std::map<uint32, uint32> > > > ArenaBots;
        std::map<uint32, std::map<uint32, std::map<uint32, uint32> > > Rating;
        std::map<uint32, std::map<uint32, std::map<uint32, uint32> > > Supporters;
        std::map<Team, std::vector<uint32>> LfgDungeons;
        void CheckBgQueue();
        void CheckLfgQueue();
        void CheckPlayers();
        void SaveCurTime();
        void SyncEventTimers();
        void AddOfflineGroupBots();
        static Item* CreateTempItem(uint32 item, uint32 count, Player const* player, uint32 randomPropertyId = 0);
        static InventoryResult CanEquipUnseenItem(Player* player, uint8 slot, uint16& dest, uint32 item);

        bool AddRandomBot(uint32 bot);
        bool CreateRandomBot(const std::string& name, uint8 race, uint8 cls, uint32 level);
        bool DeleteRandomBot(ObjectGuid guid);
        virtual void MovePlayerBot(uint32 guid, PlayerbotHolder* newHolder) override;

        std::map<Team, std::map<BattleGroundTypeId, std::list<uint32> > > getBattleMastersCache() { return BattleMastersCache; }

        float getActivityMod() { return activityMod; }
        float getActivityPercentage() { return activityMod * 100.0f; }
        void setActivityPercentage(float percentage) { activityMod = percentage / 100.0f; }

        void PrintTeleportCache();

        void AddFacingFix(uint32 mapId, uint32 instanceId, ObjectGuid guid) { facingFix[mapId][instanceId].push_back(std::make_pair(guid,time(0))); }

        bool arenaTeamsDeleted, guildsDeleted = false;

        std::mutex m_ahActionMutex;

        const std::vector<AuctionEntry>& GetAhPrices(uint32 itemId) {
            static const std::vector<AuctionEntry> emptyVector; // Avoid returning dangling refs
            auto it = ahMirror.find(itemId);
            return (it != ahMirror.end()) ? it->second : emptyVector;}
        uint32 GetPlayersLevel() { return playersLevel; }
    protected:
        virtual void OnBotLoginInternal(Player * const bot) override;
    private:
        //pid values are set in constructor
        botPID pid = botPID(1, 50, -50, 0, 0, 0);
        float activityMod = 0.25;
        std::map<std::string, uint32> databaseDelay;
        uint32 GetEventValue(uint32 bot, std::string event);
        // All-or-nothing multi-event read: false when ANY value is unknown,
        // so a multi-key mutation decision can never partially proceed on a
        // mix of known and unknown state.
        bool TryReadRequiredEvents(uint32 bot, std::initializer_list<std::pair<char const*, uint32*>> reads);
        std::string GetEventData(uint32 bot, std::string event);
        bool SetEventValue(uint32 bot, std::string event, uint32 value, uint32 validIn, std::string data = "");
        // Typed durability result RELATIVE TO THE REQUESTED STATE: a reported
        // execution failure is not proof of non-mutation, so this reloads and
        // classifies (DesiredStateConfirmed / DefinitelyNotApplied /
        // StateUnknown). Compensation logic must use THIS - the bool wrapper
        // above returns true only for DesiredStateConfirmed.
        living::EventWriteResult SetEventValueEx(uint32 bot, std::string const& event, uint32 value, uint32 validIn, std::string const& data = "");
        // Reloads ONE durable event row into the cache with a typed outcome:
        // only a successful COUNT confirms absence; a failed query preserves
        // the prior cached value (the caller marks the entry dirty).
        living::EventReloadOutcome ReloadEventRow(uint32 bot, std::string const& event);
        bool IsEventDirty(uint32 bot, std::string const& event) const;
        // COUNT-first canonical currentBots load (`add` active AND `logout`
        // inactive): SuccessEmpty/SuccessRows replace the vector; QueryFailed
        // leaves it untouched (a null row query is never confirmed absence).
        living::CountedLoadOutcome LoadCurrentBotsFromDb();
        // Runs one complete durable activation plan (checked writes with
        // compensation to known priors) through the pure executor. Returns
        // true only when EVERY write is execution-confirmed - only then may a
        // login start or the in-memory list change. Uncertain compensation
        // marks currentBots dirty.
        bool RunActivationPlan(uint32 bot, std::vector<living::PlannedEventWrite> const& plan);
        // Remaining validity of a cached event (compensation restores the
        // prior value with its remaining window), zero when absent/expired.
        uint32 RemainingValidity(uint32 bot, std::string const& event);
        // SQL result callback for the relocation homebind verification: parses
        // the row into a match/mismatch outcome and enqueues it on the
        // relocation tracker - no database work, no decisions. The parameter
        // is the RELOCATION TOKEN of the requesting generation, so a stale
        // callback can never be applied to a later record.
        void HandleHomebindVerify(QueryResult* result, uint64 relocationToken);
        // Advances one Finalizing relocation's owed completion work.
        living::RelocationAdvanceResult AdvanceRelocation(Player* bot);
        std::list<uint32> GetBots();
        std::list<uint32> GetBgBots(uint32 bracket);
        time_t BgCheckTimer;
        time_t LfgCheckTimer;
        time_t PlayersCheckTimer;
        time_t EventTimeSyncTimer;
        time_t OfflineGroupBotsTimer;
        uint32 AddRandomBots();
        bool ProcessBot(uint32 bot);
        void ScheduleRandomize(uint32 bot, uint32 time);
        living::RelocationOutcome RandomTeleport(Player* bot, bool reviveRecovery = false);
        living::RelocationOutcome RandomTeleport(Player* bot, std::vector<WorldLocation> &locs, living::PendingRelocation flags, bool activeOnly = false);
        living::RelocationTracker relocations;
        uint32 GetZoneLevel(uint16 mapId, float teleX, float teleY, float teleZ);
        void PrepareTeleportCache();
        typedef std::list<std::string> (RandomPlayerbotMgr::*ConsoleCommandHandler) (std::string param);
        typedef std::list<std::string> (RandomPlayerbotMgr::*ConsolePlayerCommandHandler) (Player*);
        // For player commands whose documented contract carries an operand
        // (e.g. change_strategy <bot> <strategy>): the parsed operand is threaded
        // to the handler instead of being parsed and discarded.
        typedef std::list<std::string> (RandomPlayerbotMgr::*ConsolePlayerCommandParamHandler) (Player*, std::string const&);


        std::list<std::string> HandleHelp(std::string param);
        std::list<std::string> HandleConsoleReset(std::string param);
        std::list<std::string> HandleConsoleStats(std::string param);
        std::list<std::string> HandleConsoleReload(std::string param);
        std::list<std::string> HandleConsoleUpdate(std::string param);
        std::list<std::string> HandleConsolePid(std::string param);
        std::list<std::string> HandleConsoleDiff(std::string param);
        std::list<std::string> HandleConsoleCleanMap(std::string param);
        std::list<std::string> HandleConsoleLoginDebug(std::string param);
        std::list<std::string> HandleConsolePathCheck(std::string param);
        // Override virtual methods from PlayerbotHolder
        virtual AccountSelectOutcome GetOrCreateAccount(Player* master, uint32& accountId, std::string& error) override;
        virtual void OnBotDeleted(uint32 botGuid, uint32 accountId) override;

    public:
        static std::string GetCommandTexts(const std::string& command);
        static std::unordered_map<std::string, std::string> GetCommandTexts();
        std::list<std::string> HandleRandomizeFirst(Player* bot);
        std::list<std::string> HandleUpdateGearSpells(Player* bot);
        std::list<std::string> HandleRefresh(Player* bot);
        std::list<std::string> HandleRandomTeleportForLevel(Player* bot);
        std::list<std::string> HandleRandomTeleportForRpg(Player* bot);
        std::list<std::string> HandleRevive(Player* bot);
        std::list<std::string> HandleRandomTeleport(Player* bot);
        std::list<std::string> HandleChangeStrategy(Player* bot, std::string const& strategySpec);
        std::list<std::string> HandleRemove(Player* bot);

        void MirrorAh();
    private:
        PlayerBotMap players;
        int processTicks;
        std::unordered_map<std::string, WorldLocation> namedLocations;
        std::map<uint8, std::vector<WorldLocation> > locsPerLevelCache;
        std::map<uint32, std::vector<WorldLocation> > rpgLocsCache;
		std::map<uint32, std::map<uint32, std::vector<WorldLocation> > > rpgLocsCacheLevel;
        std::map<uint32, std::map<uint32, std::vector<std::pair<ObjectGuid, WorldLocation>> > > innCacheLevel;
        std::map<Team, std::map<BattleGroundTypeId, std::list<uint32> > > BattleMastersCache;
        std::map<uint32, std::map<std::string, CachedEvent> > eventCache;
        // Explicit per-bot bulk-load state: Unloaded (never attempted),
        // Loaded (authoritative, possibly empty) or Unknown (last load
        // FAILED - retried on later reads; a sibling cached event or a
        // default-inserted zero can no longer suppress the retry).
        std::map<uint32, living::EventCacheLoadState> eventCacheLoadState;
        // Events whose durable state could not be re-established after a
        // failed write (reload query also failed): the cache keeps the prior
        // KNOWN value and GetEventValue retries the reload per event - the
        // whole-map-empty reload alone cannot fix a single stale entry while
        // sibling events stay cached.
        std::map<uint32, std::set<std::string> > dirtyEvents;
        // Per-(bot, marker) one-shot completion ledger for the post-create
        // markers (create gear/levelup/test): the runtime effect runs EXACTLY
        // ONCE and only the durable clear is retried, so an always-online bot
        // no longer replays the mutation (e.g. gear=empty destroying items)
        // every manager pass. In-memory only; a confirmed clear removes the
        // durable marker, and ForgetEventCache drops the ledger on guid reuse.
        std::map<uint32, std::map<std::string, living::OneShotMarker> > oneShotMarkers;
        // Per-(bot, marker) ledger for the DESTRUCTIVE post-create markers
        // (create gear / create levelup): the effect runs first, the phase-2
        // record (with PRE/POST equipment fingerprints) is execution-confirmed
        // after it, and the durable intent is cleared only once an
        // execution-ordered fingerprint readback PROVES the intended
        // postcondition landed. Ambiguity quarantines with an error instead of
        // clearing. ForgetEventCache drops the ledger on guid reuse.
        std::map<uint32, std::map<std::string, living::DurableOneShotMarker> > durableOneShotMarkers;
        // Transient post-create scheduler owners (guid -> account), SEPARATE
        // from the always-online freeAltBots membership: reconstructed from
        // durable markers by ReconstructPostCreateOwners and released only
        // when a bot's post-create work is settled or quarantined.
        std::map<uint32, uint32> postCreateOwners;
        // Save-verification bookkeeping: token -> (bot, marker, expected
        // fingerprint, request generation) for the async equipment readback
        // queued BEHIND the effect's SaveToDB on the same FIFO thread. Results
        // are only enqueued by the SQL callback and drained from LoginFreeBots
        // on the world thread; stale generations (a late callback after a
        // watchdog re-arm) are dropped there.
        struct PostCreateSaveVerify
        {
            uint32 botGuid = 0;
            std::string marker;
            uint64 expectedHash = 0;
            uint32 generation = 0;
        };
        std::map<uint64, PostCreateSaveVerify> saveVerifyTokens;
        uint64 nextSaveVerifyToken = 1;
        struct PostCreateSaveVerifyResult
        {
            uint64 token = 0;
            bool queryOk = false;
            uint64 actualHash = 0;
        };
        std::vector<PostCreateSaveVerifyResult> saveVerifyResults;
        // Enqueues the execution-ordered equipment readback for (bot, marker);
        // returns whether it was actually enqueued.
        bool RequestPostCreateSaveVerify(uint32 botGuid, std::string const& marker,
            uint64 expectedHash, uint32 generation);
        // SQL result callback: parse + enqueue only (deadlock contract).
        void HandlePostCreateSaveVerify(QueryResult* result, uint64 verifyToken);
        // Applies drained verification results to the durable marker ledgers.
        void DrainPostCreateSaveVerifies();
        // True after an activation pair (add/logout) whose durable state could
        // not be established: GetBots must reconcile from durable truth before
        // trusting the in-memory vector again.
        bool currentBotsDirty = false;
        // Next-allowed group-join attempt per bot (bounded retry backoff for
        // the `create group` event; in-memory only - a restart simply retries
        // sooner).
        std::map<uint32, time_t> groupJoinBackoffUntil;
        BarGoLink* loginProgressBar;
        std::list<uint32> currentBots;
        std::list<uint32> arenaTeamMembers;
        uint32 bgBotsCount;
        uint32 playersLevel = 0;
        uint32 botCount = 0;
        uint32 activeBots = 0;        

        std::unordered_map<uint32, std::vector<std::pair<int32,int32>>> playerBotMoveLog;
        typedef std::unordered_map <uint32, std::list<float>> botPerformanceMetric;
        std::unordered_map<std::string, botPerformanceMetric> botPerformanceMetrics;
        
        std::vector<std::pair<uint32, uint32>> RpgLocationsNear(const WorldLocation pos, const std::map<uint32, std::map<uint32, std::vector<std::string>>>& areaNames, uint32 radius = 2000);
        void PushMetric(botPerformanceMetric& metric, const uint32 bot, const float value, const uint32 maxNum = 60) const;
        float GetMetricDelta(botPerformanceMetric& metric) const;

        bool showLoginWarning;
        std::unordered_map<uint32, std::unordered_map<uint32, std::vector<std::pair<ObjectGuid, time_t>>>> facingFix;

        //                   itemId,             buyout, count
        std::unordered_map < uint32, std::vector<AuctionEntry>> ahMirror;
};

#define sRandomPlayerbotMgr RandomPlayerbotMgr::instance()

#endif
