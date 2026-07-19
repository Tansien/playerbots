#ifndef _PLAYERBOTMGR_H
#define _PLAYERBOTMGR_H

#include "Common.h"
#include "PlayerbotAIBase.h"
#include "Entities/ObjectGuid.h"
#include "Database/DatabaseEnv.h"
#include "Globals/SharedDefines.h"
#include "playerbot/living/util/LivingBotCreation.h"
#include "playerbot/living/util/LivingCreationBatch.h"


class WorldPacket;
class Player;
class Unit;
class Object;
class Item;

typedef std::map<uint32, Player*> PlayerBotMap;
typedef std::map<std::string, std::set<std::string> > PlayerBotErrorMap;

// Typed outcome of bot-account selection. A failed character-count query is
// transient DATABASE unavailability: the scan must abort on the first failure
// (never walk thousands of further synchronous queries into the outage) and
// the caller reports a transient - not terminal - creation failure.
enum class AccountSelectOutcome
{
    Found,
    CapacityExhausted,
    DatabaseUnavailable,
    AccountCreationFailed,
};

// Typed account-existence lookup. A failed query is NOT a missing account:
// only a confirmed Missing may trigger account creation - creating (or
// scanning past) an account because the lookup query failed duplicates
// accounts and amplifies the outage.
enum class AccountLookupOutcome
{
    Found,
    Missing,
    DatabaseUnavailable,
};

// Typed outcome of PlayerbotHolder::CreateBot. Callers must branch on `status`
// (and use `guid` for the created character); `messages` is display text only
// and must never be parsed to infer success. `createdClass`/`createdRole` are
// the values the creation ACTUALLY persisted - group accounting must use these,
// never its preselected assumptions. A PendingPersistence result carries the
// OPAQUE `creationToken` (never a GUID): callers poll it through
// PlayerbotHolder::PollBotCreation until a terminal result arrives.
struct BotCreationResult
{
    living::BotCreateStatus status = living::BotCreateStatus::TerminalFailure;
    ObjectGuid guid;
    uint64 creationToken = 0; // nonzero for PendingPersistence
    uint8 createdClass = 0;
    uint8 createdRole = 0; // ai::BotRoles derived from the created character
    std::list<std::string> messages;
};

// Typed, fully validated CreateBot arguments. The chat-command entry point
// builds this via Parse (which rejects unknown keys, bare tokens, duplicates,
// empty explicit values and malformed booleans BEFORE any mutation); internal
// callers such as HandleGroup construct it directly instead of assembling and
// reparsing an argument string.
struct CreateBotOptions
{
    std::string name;                  // empty = generate a random name; an explicit empty "name=" is rejected by Parse
    std::string testName;
    uint8 race = 0;                    // 0 = absent -> random
    uint8 cls = 0;                     // 0 = absent -> random
    uint32 level = 0;                  // 0 = absent -> level 1
    uint8 gender = GENDER_NONE;        // absent -> random
    Team team = TEAM_BOTH_ALLOWED;     // absent -> master's team
    uint8 role = 0;                    // ai::BotRoles; BOT_ROLE_NONE = auto talents
    // When set (group composition), the role produced by talent selection is
    // verified BEFORE any persistence; a mismatch is cleaned up unpersisted and
    // reported as a retryable failure. Only meaningful for level > 1 creations
    // (a level-1 character has no spec to verify).
    bool requireRoleMatch = false;
    bool autoAdd = false;
    bool temporary = false;
    std::string groupWith;
    // True when group= was explicitly requested (or set by HandleGroup), as
    // opposed to the legacy master-name default. An explicit group request
    // writes the group-join event at ANY level; the defaulted value keeps the
    // legacy level>1-only behavior.
    bool explicitGroup = false;
    std::string gear = "default";

    // Parses and validates a chat-supplied argument string. Explicit names are
    // normalized and checked with the same rules the cores' character-creation
    // handler uses (syntax, length, language, reserved names). Returns false
    // with `error` set and no partial state the caller may act on.
    bool Parse(Player* master, std::string const& param, std::string& error);
};

class PlayerbotHolder : public PlayerbotAIBase
{
public:
    PlayerbotHolder();
    virtual ~PlayerbotHolder();

    void AddPlayerBot(uint32 guid, uint32 masterAccountId);
	void HandlePlayerBotLoginCallback(QueryResult * dummy, SqlQueryHolder * holder);

    void LogoutPlayerBot(uint32 guid, bool allowInstant = true, bool forDelete = false);
    void DisablePlayerBot(uint32 guid, bool logOutPlayer = true);
    Player* GetPlayerBot (uint32 guid) const;

    virtual void UpdateAIInternal(uint32 elapsed, bool minimal = false) override;
    void UpdateSessions(uint32 elapsed);

    void ForEachPlayerbot(std::function<void(Player*)> fct) const;

    void LogoutAllBots();
    void JoinChatChannels(Player* bot);
    void OnBotLogin(Player* bot);
    virtual void MovePlayerBot(uint32 guid, PlayerbotHolder* newHolder);

    std::list<std::string> HandlePlayerbotCommand(const std::string args, Player* master = NULL, AccountTypes security = SEC_PLAYER);
    std::string ProcessBotCommand(const std::string cmd, ObjectGuid guid, ObjectGuid masterguid, bool admin, uint32 masterAccountId, uint32 masterGuildId, const std::string param = "");
    uint32 GetAccountId(std::string name);
    std::string ListBots(Player* master, const std::string param);
    PlayerBotMap& GetAllBots() { return playerBots; }
    uint32 GetPlayerbotsAmount() const;

    static std::string GetCommandTexts(const std::string& command);
    static std::unordered_map<std::string, std::string> GetCommandTexts();

    // String entry point: parses/validates into CreateBotOptions, then defers
    // to the typed entry point below.
    BotCreationResult CreateBot(Player* master, const std::string param);
    // Typed entry point: options are assumed validated (Parse or an internal
    // constructor like HandleGroup). Performs the name-collision lookup and all
    // account/character mutation.
    BotCreationResult CreateBot(Player* master, CreateBotOptions const& options);
    bool DeleteBot(ObjectGuid guid, bool allowInstant = true);
#ifdef GenerateBotTests
    void DepositTestResult(const std::string& testName, const std::string& result);
#endif

    // Polls one PendingPersistence creation token. Pending until the
    // finalizer confirms a terminal outcome; a terminal result is retained
    // (bounded) until acknowledged. Enqueueing was never success - callers
    // must poll to a real terminal result.
    static living::CreationPollResult PollBotCreation(uint64 creationToken, bool acknowledgeTerminal);
    // Polls one HandleGroup batch token (asynchronous group completion).
    static living::BatchPollResult PollBotCreationBatch(uint64 batchToken, bool acknowledgeComplete);
    // Transfer a still-pending creation token to the deferred cleanup owner
    // (TestContext::Reset): it survives the reset, polls the token to terminal,
    // and deletes any finalized temporary character exactly once.
    static void AbandonCreationToken(uint64 creationToken);
    static void AbandonBatchToken(uint64 batchToken);
    // Adopts durable ownership of an ALREADY-QUEUED character deletion: login
    // eligibility (freeAltBots) is revoked immediately, and the character's
    // event rows/markers are cleared only after an execution-ordered readback
    // confirms the row absent (DeleteFromDB returning is never success) or the
    // guid proven reused by a different identity (`expectedName` records the
    // identity for that comparison). DeleteBot calls this for every deletion
    // path, live or offline.
    static void AdoptCharacterDeletion(uint32 guid, uint32 accountId, std::string const& expectedName);
    // Re-adopts every durable deletion intent ('delete' event rows) at
    // startup/config reload: a lost queued deletion is re-run, an absent or
    // guid-reused character is adopted for confirmation/cleanup only. A
    // failed scan leaves the intent rows untouched.
    static void ReadoptPendingDeletions();
    // Internal surface for the deletion confirmer: fires the OnBotDeleted
    // account-cleanup hook once absence is CONFIRMED (only then is the
    // durable character count truthful about the deletion).
    static void OnCharacterDeletionConfirmed(uint32 guid, uint32 accountId);
    // Typed durable character count. The cores' GetCharactersCount collapses
    // a FAILED query to zero, which fails admission OPEN (a full account
    // admitted as empty); this returns false on query failure so admission
    // can fail closed instead.
    static bool TryGetDurableCharacterCount(uint32 accountId, uint32& count);
    // Effective per-account occupancy: durable characters plus the
    // finalizer's pending/quarantined reservations. Admission must use this,
    // never the durable count alone - a queued-but-unexecuted SaveToDB is
    // invisible to the durable count. Returns false when the durable count
    // could not be established (callers reject or defer, never assume empty).
    static bool TryGetEffectiveCharacterCount(uint32 accountId, uint32& count);
    // THE shared character-slot reservation API: obtains the typed durable
    // count, combines it with the finalizer reservation ledger exactly once,
    // and reserves atomically on the world thread. Every queued character -
    // manual, group, test, or legacy bulk - occupies exactly one reservation
    // in this single ledger. The second overload reuses a count the caller
    // already fetched (bulk loops), avoiding per-character query
    // amplification.
    static living::SlotReservationOutcome TryReserveCharacterSlot(uint32 accountId);
    static living::SlotReservationOutcome TryReserveCharacterSlot(uint32 accountId, uint32 durableCount);
    // Releases one reservation that never queued a save (pre-save failure).
    static void ReleaseCharacterSlot(uint32 accountId);
    // Registers one GENERATION-scoped execution-ordered bulk-reservation
    // readback (issued after that generation's saves were queued on the same
    // FIFO thread); the pump releases exactly that generation's reservations
    // when its own barrier returns, keeps them on bounded failure. Never
    // merges with earlier in-flight generations.
    static void BeginBulkReservationVerify(uint32 accountId, uint32 reservedSlots);
    // Typed account-existence lookup by name (COUNT + MIN aggregate: a
    // missing account is distinguishable from a failed query; the name is
    // escaped here, once).
    static AccountLookupOutcome TryLookupAccountId(std::string const& accountName, uint32& accountId);
    static uint32 MaxCharsPerAccount();

    std::list<std::string> HandleGroup(Player* master, const std::string param, AccountTypes security);
    // Batch-token surface: identical to HandleGroup but exposes the creation
    // batch registered for the run (0 when nothing was enqueued or the batch
    // registry is full). The test DSL polls this token for real completion.
    std::list<std::string> HandleGroup(Player* master, const std::string param, AccountTypes security, uint64& batchToken);
protected:
    virtual void OnBotLoginInternal(Player * const bot) = 0;
    virtual void OnBotDeleted(uint32 botGuid, uint32 accountId);
    virtual AccountSelectOutcome GetOrCreateAccount(Player* master, uint32& accountId, std::string& error);
    void Cleanup();
private:
    typedef std::list<std::string> (PlayerbotHolder::*HolderCommandHandler)(Player* master, const std::string param, AccountTypes security);
    typedef std::string (PlayerbotHolder::*BotCommandHandler)(Player* bot, Player* master, const std::string param);

    ObjectGuid GetSpoofGuid() const { return m_spoofGuid; }

    virtual std::vector<std::string> GetBotErrors(std::string botName) { return {}; }
 

    std::list<std::string> HandleList(Player* master, const std::string param, AccountTypes security);
    std::list<std::string> HandleHelp(Player* master, const std::string param, AccountTypes security);
    std::list<std::string> HandleReload(Player* master, const std::string param, AccountTypes security);
    std::list<std::string> HandleTweak(Player* master, const std::string param, AccountTypes security);
    std::list<std::string> HandleSelf(Player* master, const std::string param, AccountTypes security);
    std::list<std::string> HandleSpoof(Player* master, const std::string param, AccountTypes security);
    std::list<std::string> HandleParty(Player* master, const std::string param, AccountTypes security);
    std::list<std::string> HandleGuild(Player* master, const std::string param, AccountTypes security);
    std::list<std::string> HandleRaid(Player* master, const std::string param, AccountTypes security);
    std::list<std::string> HandleRaidLeader(Player* master, const std::string param, AccountTypes security);
    std::list<std::string> HandleCreate(Player* master, const std::string param, AccountTypes security);
#ifdef GenerateBotTests
    std::list<std::string> HandleRunTest(Player* master, const std::string param, AccountTypes security);

    struct PendingTest
    {
        std::string testName;
        std::string result;
        bool pending;
        bool completed;
        uint32 expectedBotSpawnCount = 1;
        uint8 retry = 0;
        // Outstanding asynchronous creation for this test's bot: the runner
        // polls it to a REAL terminal result instead of assuming enqueueing
        // succeeded (a parse/admission failure fails the test immediately, an
        // async failure retries within the bounded budget).
        uint64 creationToken = 0;
        uint8 creationRetries = 0;
        // Transient database-unavailability deferrals (bounded tick-based
        // backoff, separate from the creation retry budget).
        uint8 transientRetries = 0;
        // Update passes to skip before the next attempt (paces the capacity
        // precheck and creation retries instead of issuing a synchronous
        // count every update tick during an outage).
        uint8 transientBackoff = 0;
    };

    void UpdatePendingTests(uint32 elapsed);
    std::mutex testResultsMutex;
    std::vector<PendingTest> pendingTests;
    std::vector<PendingTest> testResults;
#endif

    std::string HandleBotAlways(Player* bot, Player* master, const std::string param);
    std::string HandleBotDebug(Player* bot, Player* master, const std::string param);
    std::string HandleBotC(Player* bot, Player* master, const std::string param);
    std::string HandleConsoleWhisper(Player* bot, Player* master, const std::string param);
    std::string HandleConsoleCmd(Player* bot, Player* master, const std::string param);
    std::string HandleBotTest(Player* bot, Player* master, const std::string param);
    std::string HandleBotDo(Player* bot, Player* master, const std::string param);
    std::string HandleBotRecord(Player* bot, Player* master, const std::string param);
    std::string HandleBotRead(Player* bot, Player* master, const std::string param);
    std::string HandleBotClear(Player* bot, Player* master, const std::string param);

    std::string HandleBotAddLogin(Player* bot, Player* master, const std::string param);
    std::string HandleBotRemoveLogout(Player* bot, Player* master, const std::string param);
    std::string HandleBotCreate(Player* bot, Player* master, const std::string param);
    std::string HandleBotDelete(Player* bot, Player* master, const std::string param);
    std::string HandleBotGear(Player* bot, Player* master, const std::string param);
    std::string HandleBotTrainLearn(Player* bot, Player* master, const std::string param);
    std::string HandleBotFoodDrink(Player* bot, Player* master, const std::string param);
    std::string HandleBotPotions(Player* bot, Player* master, const std::string param);
    std::string HandleBotConsumes(Player* bot, Player* master, const std::string param);
    std::string HandleBotReagents(Player* bot, Player* master, const std::string param);
    std::string HandleBotPrepare(Player* bot, Player* master, const std::string param);
    std::string HandleBotInit(Player* bot, Player* master, const std::string param);
    std::string HandleBotEnchants(Player* bot, Player* master, const std::string param);
    std::string HandleBotAmmo(Player* bot, Player* master, const std::string param);
    std::string HandleBotPet(Player* bot, Player* master, const std::string param);
    std::string HandleBotLevelUp(Player* bot, Player* master, const std::string param);
    std::string HandleBotRefresh(Player* bot, Player* master, const std::string param);
    std::string HandleBotRandom(Player* bot, Player* master, const std::string param);

    PlayerBotMap playerBots;
    std::map<std::string, HolderCommandHandler> m_holderHandlers;
    std::map<std::string, BotCommandHandler> m_botCommandHandlers;
    ObjectGuid m_spoofGuid;
};

class PlayerbotMgr : public PlayerbotHolder
{
public:
    PlayerbotMgr(Player* const master);
    virtual ~PlayerbotMgr() override;

    static bool HandlePlayerbotMgrCommand(ChatHandler* handler, char const* args);
    void HandleMasterIncomingPacket(const WorldPacket& packet);
    void HandleMasterOutgoingPacket(const WorldPacket& packet);
    void HandleCommand(uint32 type, const std::string& text, uint32 lang = LANG_UNIVERSAL);
    void OnPlayerLogin(Player* player);
    void CancelLogout();

    virtual void UpdateAIInternal(uint32 elapsed, bool minimal = false) override;
    void TellError(std::string botName, std::string text);
    virtual std::vector<std::string> GetBotErrors(std::string botName) override;

    Player* GetMaster() const { return master; };

    void SaveToDB();

protected:
    virtual void OnBotLoginInternal(Player * const bot) override;
    void CheckTellErrors(uint32 elapsed);

private:
    Player* const master;
    PlayerBotErrorMap errors;
    time_t lastErrorTell;
};

#endif
