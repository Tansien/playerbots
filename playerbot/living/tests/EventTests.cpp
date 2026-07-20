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

LIVING_TEST(event_value_known_fails_closed_while_load_state_unknown)
{
    // The typed-read rule behind TryGetEventValue: a cached entry is always
    // known (confirmed write or successful load); an ABSENT entry is
    // confirmed absent only after a completed bulk load. Absent while
    // Unloaded/Unknown reads as zero but is NOT knowledge - the destructive
    // callers (timed-logout deactivation clearing `add`, removing the bot
    // from currentBots and logging it out) skip their mutation instead of
    // consuming a transient load failure as an expired activation.
    LIVING_CHECK(!EventValueKnown(EventCacheLoadState::Unloaded, false));
    LIVING_CHECK(!EventValueKnown(EventCacheLoadState::Unknown, false));
    LIVING_CHECK(EventValueKnown(EventCacheLoadState::Loaded, false));  // confirmed absence
    LIVING_CHECK(EventValueKnown(EventCacheLoadState::Unloaded, true)); // cached entry
    LIVING_CHECK(EventValueKnown(EventCacheLoadState::Unknown, true));  // prior known value
    LIVING_CHECK(EventValueKnown(EventCacheLoadState::Loaded, true));

    // The failed-first-load deactivation scenario as a sequence: load fails
    // (Unknown), `add` absent -> not known -> NO deactivation; the DB
    // recovers, the load succeeds, and only a then-confirmed zero (Loaded +
    // absent) may deactivate.
    EventCacheLoadState state = ResolveEventCacheBulkLoad(ClassifyCountedLoad(std::nullopt, false));
    LIVING_CHECK(state == EventCacheLoadState::Unknown);
    bool deactivate = EventValueKnown(state, false); // value would read 0
    LIVING_CHECK(!deactivate);

    state = ResolveEventCacheBulkLoad(ClassifyCountedLoad(0, false)); // recovered: zero rows confirmed
    LIVING_CHECK(state == EventCacheLoadState::Loaded);
    LIVING_CHECK(EventValueKnown(state, false)); // now a KNOWN zero may deactivate
}

// ---- Finding 1: reported-failed writes are confirmed against the COMPLETE
// durable row (time, validIn, value, data), never the value alone. ----

LIVING_TEST(event_failed_write_applied_but_reported_failed_is_confirmed)
{
    // The UPDATE/INSERT reported failure but actually landed: the reloaded row
    // equals the complete requested row, so the write is confirmed and the
    // caller (AddPlayerBot/activation) may proceed.
    living::EventRow const requested{2, 100, 3600, "gear"};
    LIVING_CHECK(living::ClassifyFailedWriteReload(
        living::EventReloadOutcome::Found, /*isDeletion=*/false,
        requested, /*durable=*/requested,
        /*priorKnown=*/true, /*priorPresent=*/true, /*prior=*/living::EventRow{1, 10, 50, ""})
        == living::EventWriteResult::DesiredStateConfirmed);
}

LIVING_TEST(event_failed_write_same_value_different_validity_not_confirmed)
{
    // Same raw value 1, but a new time and validIn: the failed write did NOT
    // apply and the reload finds the unchanged prior row. Comparing the value
    // alone would falsely confirm; the full-row compare must not.
    living::EventRow const prior{1, 10, 50, ""};        // unchanged durable row
    living::EventRow const requested{1, 100, 3600, ""}; // same value, fresh lifetime
    living::EventWriteResult const r = living::ClassifyFailedWriteReload(
        living::EventReloadOutcome::Found, /*isDeletion=*/false,
        requested, /*durable=*/prior, /*priorKnown=*/true, /*priorPresent=*/true, prior);
    LIVING_CHECK(r != living::EventWriteResult::DesiredStateConfirmed);
    LIVING_CHECK(r == living::EventWriteResult::DefinitelyNotApplied);
}

LIVING_TEST(event_failed_write_same_value_different_data_not_confirmed)
{
    // Same value/time/validIn but a different data payload: still a different
    // durable state, so a stale-data row cannot confirm the refresh.
    living::EventRow const prior{1, 100, 3600, "old-group"};
    living::EventRow const requested{1, 100, 3600, "new-group"};
    living::EventWriteResult const r = living::ClassifyFailedWriteReload(
        living::EventReloadOutcome::Found, /*isDeletion=*/false,
        requested, /*durable=*/prior, /*priorKnown=*/true, /*priorPresent=*/true, prior);
    LIVING_CHECK(r != living::EventWriteResult::DesiredStateConfirmed);
    LIVING_CHECK(r == living::EventWriteResult::DefinitelyNotApplied);
}

LIVING_TEST(event_failed_write_expired_add_refresh_not_confirmed)
{
    // The finding's exact repro: durable add=1 is EXPIRED (old time, short
    // validIn); activation re-schedules add=1 with a fresh lifetime; the UPDATE
    // reports failure and did NOT apply; the reload finds the unchanged expired
    // raw 1. A value-only compare would confirm an expired durable activation.
    living::EventRow const expiredAdd{1, 10, 5, ""};      // long expired
    living::EventRow const freshAdd{1, 1000, 3600, ""};   // requested refresh
    living::EventWriteResult const r = living::ClassifyFailedWriteReload(
        living::EventReloadOutcome::Found, /*isDeletion=*/false,
        freshAdd, /*durable=*/expiredAdd, /*priorKnown=*/true, /*priorPresent=*/true, expiredAdd);
    LIVING_CHECK(r != living::EventWriteResult::DesiredStateConfirmed);
    LIVING_CHECK(r == living::EventWriteResult::DefinitelyNotApplied);
}

LIVING_TEST(event_failed_write_expired_teleport_refresh_not_confirmed)
{
    // Same shape for a teleport marker: an expired teleport=1 refreshed to the
    // same raw value must not be accepted as freshly scheduled.
    living::EventRow const expiredTeleport{1, 20, 5, ""};
    living::EventRow const freshTeleport{1, 2000, 600, ""};
    living::EventWriteResult const r = living::ClassifyFailedWriteReload(
        living::EventReloadOutcome::Found, /*isDeletion=*/false,
        freshTeleport, /*durable=*/expiredTeleport, /*priorKnown=*/true, /*priorPresent=*/true, expiredTeleport);
    LIVING_CHECK(r != living::EventWriteResult::DesiredStateConfirmed);
    LIVING_CHECK(r == living::EventWriteResult::DefinitelyNotApplied);
}

LIVING_TEST(event_delete_confirmed_only_by_confirmed_absence)
{
    living::EventRow const prior{1, 10, 50, ""};
    living::EventRow const requestedDelete{0, 100, 0, ""}; // value 0 => deletion
    // A still-present row (even one holding value 0) never confirms a delete.
    LIVING_CHECK(living::ClassifyFailedWriteReload(
        living::EventReloadOutcome::Found, /*isDeletion=*/true,
        requestedDelete, /*durable=*/prior, /*priorKnown=*/true, /*priorPresent=*/true, prior)
        == living::EventWriteResult::DefinitelyNotApplied); // prior row remained: delete did not land
    living::EventRow const stillZero{0, 10, 50, ""};
    LIVING_CHECK(living::ClassifyFailedWriteReload(
        living::EventReloadOutcome::Found, /*isDeletion=*/true,
        requestedDelete, /*durable=*/stillZero, /*priorKnown=*/false, /*priorPresent=*/false, living::EventRow{})
        != living::EventWriteResult::DesiredStateConfirmed); // present value-0 row is NOT absence
    // Only a confirmed-absent row proves the delete landed.
    LIVING_CHECK(living::ClassifyFailedWriteReload(
        living::EventReloadOutcome::ConfirmedAbsent, /*isDeletion=*/true,
        requestedDelete, /*durable=*/living::EventRow{}, /*priorKnown=*/true, /*priorPresent=*/true, prior)
        == living::EventWriteResult::DesiredStateConfirmed);
}

LIVING_TEST(event_failed_write_untrusted_prior_stays_unknown)
{
    // Prior state is NOT known (e.g. the event was already dirty): a row that
    // matches that untrusted prior cannot prove DefinitelyNotApplied, and a
    // same-value-different-lifetime row does not match the request, so the
    // result is StateUnknown - the caller keeps the event dirty.
    living::EventRow const prior{1, 10, 5, ""};
    living::EventRow const requested{1, 100, 3600, ""};
    LIVING_CHECK(living::ClassifyFailedWriteReload(
        living::EventReloadOutcome::Found, /*isDeletion=*/false,
        requested, /*durable=*/prior, /*priorKnown=*/false, /*priorPresent=*/true, prior)
        == living::EventWriteResult::StateUnknown);
    // A failed reload is always StateUnknown regardless of prior knownness.
    LIVING_CHECK(living::ClassifyFailedWriteReload(
        living::EventReloadOutcome::QueryFailed, /*isDeletion=*/false,
        requested, /*durable=*/living::EventRow{}, /*priorKnown=*/true, /*priorPresent=*/true, prior)
        == living::EventWriteResult::StateUnknown);
}

// ---- Finding 2: a dirty event whose reload cannot reconcile reads as NOT
// trusted, so typed mutation gates defer until reconciliation succeeds. ----

LIVING_TEST(event_value_trusted_is_false_while_dirty)
{
    // Dirtiness overrides every otherwise-known state: a cached value or a
    // completed load does not make a dirty entry trustworthy.
    LIVING_CHECK(!living::EventValueTrusted(living::EventCacheLoadState::Loaded, true, /*dirty=*/true));
    LIVING_CHECK(!living::EventValueTrusted(living::EventCacheLoadState::Unknown, true, /*dirty=*/true));
    LIVING_CHECK(!living::EventValueTrusted(living::EventCacheLoadState::Loaded, false, /*dirty=*/true));
    // Clean entries keep the EventValueKnown semantics unchanged.
    LIVING_CHECK(living::EventValueTrusted(living::EventCacheLoadState::Loaded, true, /*dirty=*/false));
    LIVING_CHECK(living::EventValueTrusted(living::EventCacheLoadState::Unknown, true, /*dirty=*/false));
    LIVING_CHECK(living::EventValueTrusted(living::EventCacheLoadState::Loaded, false, /*dirty=*/false)); // confirmed absent
    LIVING_CHECK(!living::EventValueTrusted(living::EventCacheLoadState::Unloaded, false, /*dirty=*/false));
}

LIVING_TEST(event_dirty_typed_read_untrusted_until_reconciled)
{
    // The full lifecycle from finding 2: an ambiguous write (ExecuteFailed +
    // failed reload) marks the event dirty; the possibly-stale cached value must
    // report NOT trusted through repeated reload failures - so no typed caller
    // mutates - and only a successful reload restores trust.
    living::EventCacheLoadState const loaded = living::EventCacheLoadState::Loaded;
    bool dirty = true;      // marked dirty by the ambiguous write
    bool reloadAvailable = false;

    auto reloadAttempt = [&]() -> living::EventReloadOutcome
    {
        living::EventReloadOutcome const outcome = reloadAvailable
            ? living::EventReloadOutcome::Found
            : living::EventReloadOutcome::QueryFailed;
        if (outcome != living::EventReloadOutcome::QueryFailed)
            dirty = false; // successful reload clears the dirty mark
        return outcome;
    };

    // First typed read: reload fails again -> still dirty -> NOT trusted.
    reloadAttempt();
    LIVING_CHECK(dirty);
    LIVING_CHECK(!living::EventValueTrusted(loaded, /*hasCachedEntry=*/true, dirty));

    // Repeated failure: still not trusted (no lifecycle mutation may proceed).
    reloadAttempt();
    LIVING_CHECK(!living::EventValueTrusted(loaded, true, dirty));

    // Reconciliation: a successful reload clears dirty and restores knownness.
    reloadAvailable = true;
    reloadAttempt();
    LIVING_CHECK(!dirty);
    LIVING_CHECK(living::EventValueTrusted(loaded, true, dirty));
}

LIVING_TEST(lifecycle_control_events_survive_console_reset)
{
    // The console reset's SQL exclusion list mirrors exactly this predicate:
    // active deletion intents and unfinished creation obligations are the
    // ONLY durable record their crash-safe owners recover from, so a reset
    // must never delete them.
    for (char const* event : { "temporary", "delete", "test",
        "create levelup", "create gear", "create group" })
        LIVING_CHECK(living::IsLifecycleControlEvent(event));

    for (char const* event : { "add", "logout", "login", "update", "always",
        "bot_count", "teleport", "specNo" })
        LIVING_CHECK(!living::IsLifecycleControlEvent(event));
}

LIVING_TEST(lifecycle_control_events_never_expire)
{
    // The typed-read expiry consults exactly this predicate: an unsettled
    // creation/deletion obligation must stay authoritative however long the
    // server was down or however late a login=0 bot first logs in (the
    // 15-day default validIn used to zero it silently).
    for (char const* event : { "create pending", "create levelup", "create gear",
        "create group", "test", "delete", "temporary",
        "specNo", "specLink", "init", "current_time", "always", "selfbot" })
        LIVING_CHECK(living::IsNonExpiringEvent(event));

    // Scheduling events still expire as before.
    for (char const* event : { "add", "logout", "login", "update", "teleport", "bot_count" })
        LIVING_CHECK(!living::IsNonExpiringEvent(event));
}
