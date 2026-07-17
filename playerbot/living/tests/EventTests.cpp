#include "LivingTest.h"

#include "../events/LivingEvents.h"

#include <cstring>
#include <set>
#include <string>

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
