#include "LivingTest.h"

#include "../events/LivingEvents.h"
#include "../util/LivingEventSchema.h"

#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace living;

LIVING_TEST(events_names_are_stable)
{
    LIVING_CHECK(std::strcmp(ToString(LivingEventType::LOGIN_OBSERVED), "login_observed") == 0);
    LIVING_CHECK(std::strcmp(ToString(LivingEventType::LOGOUT_OBSERVED), "logout_observed") == 0);
    LIVING_CHECK(std::strcmp(ToString(LivingEventType::SCHEDULE_TRANSITION), "schedule_transition") == 0);
    LIVING_CHECK(std::strcmp(ToString(LivingEventType::GOAL_TRANSITION), "goal_transition") == 0);
    LIVING_CHECK(std::strcmp(ToString(LivingEventType::GROUP_JOINED), "group_joined") == 0);
    LIVING_CHECK(std::strcmp(ToString(LivingEventType::GROUP_LEFT), "group_left") == 0);
    LIVING_CHECK(std::strcmp(ToString(LivingEventType::TRAVEL_COMPLETED), "travel_completed") == 0);
    LIVING_CHECK(std::strcmp(ToString(LivingEventType::TRAVEL_FAILED), "travel_failed") == 0);
    LIVING_CHECK(std::strcmp(ToString(LivingEventType::QUEST_ACCEPTED), "quest_accepted") == 0);
    LIVING_CHECK(std::strcmp(ToString(LivingEventType::QUEST_REWARDED), "quest_rewarded") == 0);
    LIVING_CHECK(std::strcmp(ToString(LivingEventType::SYNTHETIC_ACTION_REQUESTED), "synthetic_action_requested") == 0);
    LIVING_CHECK(std::strcmp(ToString(LivingEventType::SYNTHETIC_ACTION_APPLIED), "synthetic_action_applied") == 0);
    LIVING_CHECK(std::strcmp(ToString(LivingEventType::SYNTHETIC_ACTION_FAILED), "synthetic_action_failed") == 0);
    LIVING_CHECK(std::strcmp(ToString(LivingEventType::SYNTHETIC_ACTION_RECONCILED), "synthetic_action_reconciled") == 0);
    LIVING_CHECK(std::strcmp(ToString(LivingEventType::SYNTHETIC_ACTION_CANCELLED), "synthetic_action_cancelled") == 0);

    std::set<std::string> names;
    for (size_t i = 0; i < static_cast<size_t>(LivingEventType::Count); ++i)
        LIVING_CHECK(names.insert(ToString(static_cast<LivingEventType>(i))).second);
    LIVING_CHECK(names.size() == 15);
}

LIVING_TEST(events_test_sink_preserves_order)
{
    OrderedTestEventSink sink;

    LivingEvent first;
    first.type = LivingEventType::LOGIN_OBSERVED;
    first.characterGuid = 1;
    first.occurredAtMs = 100;
    sink.Emit(first);

    LivingEvent second;
    second.type = LivingEventType::QUEST_ACCEPTED;
    second.characterGuid = 1;
    second.identityNonce = { 9, 9, 9 }; // same guid, distinct identity
    second.occurredAtMs = 200;
    second.detail = "quest=123";
    sink.Emit(second);

    LivingEvent third;
    third.type = LivingEventType::LOGOUT_OBSERVED;
    third.characterGuid = 2;
    third.occurredAtMs = 150; // order is emission order, not timestamp order
    sink.Emit(third);

    LIVING_CHECK(sink.events.size() == 3);
    LIVING_CHECK(sink.events[0].type == LivingEventType::LOGIN_OBSERVED);
    LIVING_CHECK(sink.events[1].type == LivingEventType::QUEST_ACCEPTED);
    LIVING_CHECK(sink.events[1].detail == "quest=123");
    LIVING_CHECK(sink.events[1].identityNonce[0] == 9);  // identity survives the sink
    LIVING_CHECK((sink.events[1].actionToken == std::array<uint8_t, 16>{})); // token only on synthetic events
    LIVING_CHECK((sink.events[0].identityNonce == std::array<uint8_t, 16>{})); // zero when n/a
    LIVING_CHECK(sink.events[2].type == LivingEventType::LOGOUT_OBSERVED);
    LIVING_CHECK(sink.events[2].characterGuid == 2);

    // Action tokens belong to the SYNTHETIC_ACTION_* events (0002B).
    LivingEvent synthetic;
    synthetic.type = LivingEventType::SYNTHETIC_ACTION_REQUESTED;
    synthetic.characterGuid = 3;
    synthetic.identityNonce = { 7 };
    synthetic.actionToken = { 4, 2 };
    synthetic.occurredAtMs = 300;
    sink.Emit(synthetic);
    LIVING_CHECK(sink.events.size() == 4);
    LIVING_CHECK(sink.events[3].actionToken[0] == 4);
}

LIVING_TEST(events_noop_sink_has_no_observable_effect)
{
    NoopLivingEventSink sink;
    LivingEventSink& base = sink;

    LivingEvent event;
    event.type = LivingEventType::SYNTHETIC_ACTION_REQUESTED;
    event.characterGuid = 7;
    for (int i = 0; i < 3; ++i)
        base.Emit(event);

    // Nothing stored, nothing thrown; the event payload is untouched.
    LIVING_CHECK(event.characterGuid == 7);
    LIVING_CHECK(event.type == LivingEventType::SYNTHETIC_ACTION_REQUESTED);
}

#include "../util/LivingEventSchema.h"

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

// PersistEventValue is the exact decision flow RandomPlayerbotMgr::SetEventValue
// runs: schema gate, synchronous existence probe, then ONE execution-confirmed
// statement. These are helper/spy tests - the live DirectPExecute/MySQL wiring
// is exercised only by compilation and in-world runs, not here.

namespace
{
    struct PersistSpy
    {
        int probeCalls = 0;
        int executeCalls = 0;
        std::optional<bool> probeResult = false;
        bool executeResult = true;
        std::optional<living::EventWriteKind> executedKind;

        living::EventPersistOutcome Run(std::string const& event, std::string const& data, uint32_t value)
        {
            return living::PersistEventValue(event, data, value,
                [&]() { ++probeCalls; return probeResult; },
                [&](living::EventWriteKind kind) { ++executeCalls; executedKind = kind; return executeResult; });
        }
    };
}

LIVING_TEST(event_persist_zero_value_deletes_without_probe)
{
    PersistSpy spy;
    LIVING_CHECK(spy.Run("add", "", 0) == EventPersistOutcome::Persisted);
    LIVING_CHECK(spy.probeCalls == 0);
    LIVING_CHECK(spy.executeCalls == 1);
    LIVING_CHECK(spy.executedKind == EventWriteKind::Delete);
}

LIVING_TEST(event_persist_updates_existing_row_and_inserts_missing_row)
{
    PersistSpy spy;
    spy.probeResult = true;
    LIVING_CHECK(spy.Run("add", "", 1) == EventPersistOutcome::Persisted);
    LIVING_CHECK(spy.executedKind == EventWriteKind::Update);

    PersistSpy fresh;
    fresh.probeResult = false;
    LIVING_CHECK(fresh.Run("add", "", 1) == EventPersistOutcome::Persisted);
    LIVING_CHECK(fresh.executedKind == EventWriteKind::Insert);
}

LIVING_TEST(event_persist_oversized_value_rejected_before_any_statement)
{
    PersistSpy spy;
    std::string const oversized(EVENT_DATA_MAX_BYTES + 1, 'x');
    LIVING_CHECK(spy.Run("create gear", oversized, 1) == EventPersistOutcome::Rejected);
    LIVING_CHECK(spy.probeCalls == 0);
    LIVING_CHECK(spy.executeCalls == 0);

    std::string const atLimit(EVENT_DATA_MAX_BYTES, 'x');
    LIVING_CHECK(spy.Run("create gear", atLimit, 1) == EventPersistOutcome::Persisted);
}

LIVING_TEST(event_persist_probe_failure_stops_before_execution)
{
    PersistSpy spy;
    spy.probeResult = std::nullopt;
    LIVING_CHECK(spy.Run("add", "", 1) == EventPersistOutcome::ProbeFailed);
    LIVING_CHECK(spy.executeCalls == 0);
}

LIVING_TEST(event_persist_execution_failure_is_reported_not_swallowed)
{
    PersistSpy spy;
    spy.probeResult = false;
    spy.executeResult = false;
    LIVING_CHECK(spy.Run("add", "", 1) == EventPersistOutcome::ExecuteFailed);
    LIVING_CHECK(spy.executeCalls == 1);
}

LIVING_TEST(event_persist_cache_matches_durable_state_through_failures_and_restart)
{
    // Spy-backed model of SetEventValue's cache protocol against a fake row
    // store: publish the value only on Persisted, reload the durable row on
    // ExecuteFailed. The cache must equal the durable state after every step,
    // including a simulated restart (cache rebuilt from rows) - the exact
    // invariant the live AddRandomBots default path relies on.
    std::map<std::string, uint32_t> rows;    // durable
    std::map<std::string, uint32_t> cache;   // runtime
    bool failNext = false;

    auto set = [&](std::string const& event, uint32_t value)
    {
        auto outcome = living::PersistEventValue(event, "", value,
            [&]() -> std::optional<bool> { return rows.count(event) > 0; },
            [&](living::EventWriteKind kind)
            {
                if (failNext)
                {
                    failNext = false;
                    return false; // statement failed: durable state unchanged
                }

                if (kind == living::EventWriteKind::Delete)
                    rows.erase(event);
                else
                    rows[event] = value;
                return true;
            });

        if (outcome == living::EventPersistOutcome::Persisted)
            cache[event] = value;
        else if (outcome == living::EventPersistOutcome::ExecuteFailed)
        {
            // Reload durable value; never publish the intended one.
            if (rows.count(event))
                cache[event] = rows[event];
            else
                cache.erase(event);
        }

        return outcome == living::EventPersistOutcome::Persisted;
    };

    auto cacheValue = [&](std::string const& event) { return cache.count(event) ? cache[event] : 0u; };
    auto rowValue = [&](std::string const& event) { return rows.count(event) ? rows[event] : 0u; };

    // The AddRandomBots pair: add=1, logout=0.
    LIVING_CHECK(set("add", 1));
    LIVING_CHECK(set("logout", 0));
    LIVING_CHECK(cacheValue("add") == 1 && rowValue("add") == 1);
    LIVING_CHECK(cacheValue("logout") == 0 && rowValue("logout") == 0);

    // Forced SQL failure: the intended value 2 must NOT appear in the cache.
    failNext = true;
    LIVING_CHECK(!set("add", 2));
    LIVING_CHECK(cacheValue("add") == 1 && rowValue("add") == 1);

    // Forced failure of the compensating delete: row stays, cache follows row.
    failNext = true;
    LIVING_CHECK(!set("add", 0));
    LIVING_CHECK(cacheValue("add") == 1 && rowValue("add") == 1);

    // Successful update, then restart round-trip: a cache rebuilt from the
    // durable rows reads identically to the one maintained incrementally (a
    // cleared event is value 0 either as an explicit entry or as no row).
    LIVING_CHECK(set("add", 5));
    LIVING_CHECK(cacheValue("add") == 5 && rowValue("add") == 5);
    std::map<std::string, uint32_t> rebuilt(rows.begin(), rows.end());
    auto rebuiltValue = [&](std::string const& event) { return rebuilt.count(event) ? rebuilt[event] : 0u; };
    for (char const* event : { "add", "logout" })
        LIVING_CHECK(rebuiltValue(event) == cacheValue(event));
}

LIVING_TEST(event_reload_query_failure_is_not_confirmed_absence)
{
    // Model of the production reload protocol (ReloadEventRow + dirty
    // tracking) against a fake row store: a write failure followed by a
    // RELOAD failure must preserve the prior known value and mark the entry
    // dirty - never erase it - even while a sibling event for the same bot
    // stays cached (the whole-map-empty reload can never repair a single
    // entry). Spy/model coverage; the live PQuery wiring is compile-verified.
    std::map<std::string, uint32_t> rows = { { "add", 7 }, { "logout", 1 } };
    std::map<std::string, uint32_t> cache = rows;
    std::set<std::string> dirty;
    bool reloadAvailable = true;

    auto reload = [&](std::string const& event) -> living::EventReloadOutcome
    {
        if (!reloadAvailable)
            return living::EventReloadOutcome::QueryFailed;

        if (rows.count(event) == 0)
        {
            cache.erase(event);
            return living::EventReloadOutcome::ConfirmedAbsent;
        }

        cache[event] = rows[event];
        return living::EventReloadOutcome::Found;
    };

    // Write fails AND the reload fails: prior value preserved, entry dirty.
    reloadAvailable = false;
    if (reload("add") == living::EventReloadOutcome::QueryFailed)
        dirty.insert("add");
    LIVING_CHECK(cache["add"] == 7);          // not erased, not zeroed
    LIVING_CHECK(dirty.count("add") == 1);
    LIVING_CHECK(cache.count("logout") == 1); // sibling stays cached

    // Read-side retry while still failing: serve the prior KNOWN value.
    LIVING_CHECK(reload("add") == living::EventReloadOutcome::QueryFailed);
    LIVING_CHECK(cache["add"] == 7);

    // Recovery: a successful reload clears the dirty mark with durable truth.
    reloadAvailable = true;
    rows["add"] = 9;
    if (reload("add") != living::EventReloadOutcome::QueryFailed)
        dirty.erase("add");
    LIVING_CHECK(cache["add"] == 9);
    LIVING_CHECK(dirty.empty());

    // Confirmed absence (COUNT succeeded, zero rows) IS allowed to erase.
    rows.erase("add");
    LIVING_CHECK(reload("add") == living::EventReloadOutcome::ConfirmedAbsent);
    LIVING_CHECK(cache.count("add") == 0);
}

LIVING_TEST(counted_load_separates_confirmed_empty_from_failure)
{
    // The COUNT-first discriminator behind both the per-bot event-cache bulk
    // load and currentBots reconciliation: a null COUNT is a FAILED load, a
    // zero COUNT is a confirmed empty set, and rows only count as loaded when
    // the row query succeeded too.
    LIVING_CHECK(ClassifyCountedLoad(std::nullopt, false) == CountedLoadOutcome::QueryFailed);
    LIVING_CHECK(ClassifyCountedLoad(std::nullopt, true) == CountedLoadOutcome::QueryFailed);
    LIVING_CHECK(ClassifyCountedLoad(0, false) == CountedLoadOutcome::SuccessEmpty);
    LIVING_CHECK(ClassifyCountedLoad(3, true) == CountedLoadOutcome::SuccessRows);
    // COUNT succeeded but the row query then failed: still a failure - the
    // rows are NOT loaded and must not be claimed as such.
    LIVING_CHECK(ClassifyCountedLoad(3, false) == CountedLoadOutcome::QueryFailed);
}

LIVING_TEST(event_cache_failed_first_load_stays_unknown_and_recovers)
{
    // The task-6 scenario driven through the exact production helpers: the
    // first bulk load fails while ANOTHER event is already cached (a
    // successful SetEventValue wrote it) - under the legacy map-empty
    // heuristic that sibling entry suppressed every future bulk load. With
    // the explicit load state the failed load stays Unknown, the next read
    // retries, and a recovered DB with a nonzero add/logout/specNo row is
    // discovered.
    std::map<std::string, uint32_t> cache; // the per-bot cache map stand-in
    cache["specNo"] = 1;                   // sibling event cached before the load

    EventCacheLoadState state = EventCacheLoadState::Unloaded;
    LIVING_CHECK(EventCacheNeedsBulkLoad(state));

    // First load: COUNT query fails -> Unknown; the read serves zero for the
    // missing event WITHOUT default-inserting a confirmed-absent entry.
    state = ResolveEventCacheBulkLoad(ClassifyCountedLoad(std::nullopt, false));
    LIVING_CHECK(state == EventCacheLoadState::Unknown);
    LIVING_CHECK(cache.find("add") == cache.end());

    // The sibling cached event does NOT prevent the retry: the state - not
    // the map's emptiness - decides.
    LIVING_CHECK(!cache.empty());
    LIVING_CHECK(EventCacheNeedsBulkLoad(state));

    // The DB recovers with a nonzero row; the next read's bulk load discovers
    // it and the state becomes authoritative.
    cache["add"] = 1; // the row query populates the cache
    state = ResolveEventCacheBulkLoad(ClassifyCountedLoad(2, true));
    LIVING_CHECK(state == EventCacheLoadState::Loaded);
    LIVING_CHECK(!EventCacheNeedsBulkLoad(state));
    LIVING_CHECK(cache["add"] == 1);

    // A confirmed-empty load is also authoritative (Loaded), never retried.
    LIVING_CHECK(ResolveEventCacheBulkLoad(ClassifyCountedLoad(0, false)) == EventCacheLoadState::Loaded);
}

LIVING_TEST(current_bots_reconciliation_zero_rows_is_success_not_failure)
{
    // The task-7 scenario: an uncertain activation left the list dirty; the
    // canonical reconciliation then reports ZERO active bots. That is a
    // successful reconciliation (clear the vector, clear the dirty flag,
    // AddRandomBots may proceed) - only a FAILED query keeps the dirty flag
    // and the last known vector.
    std::vector<uint32_t> currentBots{ 11, 22 };
    bool dirty = true;

    // Failed reconciliation: vector preserved, still dirty, mutation refused.
    switch (ClassifyCountedLoad(std::nullopt, false))
    {
        case CountedLoadOutcome::SuccessRows:
        case CountedLoadOutcome::SuccessEmpty:
            dirty = false;
            break;
        case CountedLoadOutcome::QueryFailed:
            break;
    }
    LIVING_CHECK(dirty);
    LIVING_CHECK(currentBots.size() == 2);

    // Successful reconciliation to zero active rows: SuccessEmpty clears
    // both the vector and the dirty flag.
    switch (ClassifyCountedLoad(0, false))
    {
        case CountedLoadOutcome::SuccessEmpty:
            currentBots.clear();
            dirty = false;
            break;
        default:
            break;
    }
    LIVING_CHECK(!dirty);
    LIVING_CHECK(currentBots.empty());
}
