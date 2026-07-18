#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace living
{
    // Column limits of the ai_playerbot_random_bots schema (event varchar(45),
    // data varchar(255)). Values are validated against these BEFORE any DELETE,
    // INSERT, or cache mutation: strict SQL would otherwise delete the old row
    // and reject the insert, and permissive SQL would truncate the stored value
    // while the cache kept the original - either way DB and cache diverge.
    inline constexpr size_t EVENT_NAME_MAX_BYTES = 45;
    inline constexpr size_t EVENT_DATA_MAX_BYTES = 255;

    inline bool EventValueFitsSchema(std::string const& event, std::string const& data)
    {
        return !event.empty()
            && event.size() <= EVENT_NAME_MAX_BYTES
            && data.size() <= EVENT_DATA_MAX_BYTES;
    }

    // The single execution-confirmed statement one event write must run.
    enum class EventWriteKind { Delete, Update, Insert };

    // Typed outcome of reloading one durable event row. A null query result
    // can mean "query failed", not only "no row": treating it as absence
    // turned unknown DB state into a confirmed zero in the cache. Absence is
    // confirmed only through a successful COUNT.
    enum class EventReloadOutcome
    {
        Found,           // row(s) read; cache now mirrors durable state
        ConfirmedAbsent, // COUNT succeeded and returned zero rows
        QueryFailed,     // durable state unknown; prior cache value must be
                         // preserved and the entry marked dirty for retry
    };

    // Typed outcome of a COUNT-first bulk load. A plain row query returns a
    // null result BOTH for "no rows" and for "query failed"; only an aggregate
    // COUNT (which always yields one row on success) can separate a confirmed
    // empty set from an unanswerable one. Used by the per-bot event-cache bulk
    // load and by currentBots reconciliation.
    enum class CountedLoadOutcome
    {
        // COUNT succeeded and reported zero rows: confirmed empty.
        SuccessEmpty,
        // COUNT reported rows and the row query loaded them.
        SuccessRows,
        // The COUNT or the row query failed: state is UNKNOWN, never empty.
        QueryFailed,
    };

    // Classifies one COUNT-first load attempt. `count` is nullopt when the
    // COUNT query itself failed; `rowsLoaded` reports the row query (only
    // consulted when count > 0).
    inline CountedLoadOutcome ClassifyCountedLoad(std::optional<uint64_t> count, bool rowsLoaded)
    {
        if (!count)
            return CountedLoadOutcome::QueryFailed;
        if (*count == 0)
            return CountedLoadOutcome::SuccessEmpty;
        return rowsLoaded ? CountedLoadOutcome::SuccessRows : CountedLoadOutcome::QueryFailed;
    }

    // Explicit initial-load state of one bot's event cache. The legacy check
    // ("is the per-bot map empty?") conflated "never loaded", "load failed"
    // and "loaded but empty", and the first default-inserted zero entry made
    // the map nonempty - permanently suppressing the bulk load while durable
    // add/logout/specNo state stayed hidden.
    enum class EventCacheLoadState
    {
        // No bulk load has been attempted.
        Unloaded,
        // A bulk load completed (possibly with zero rows): the cache is
        // authoritative modulo individually dirty events.
        Loaded,
        // The last bulk load FAILED: durable state is unknown. Reads keep
        // retrying the bulk load - a sibling cached event must not stop the
        // retry - and absent entries are served as zero WITHOUT being treated
        // as confirmed absence.
        Unknown,
    };

    // Whether a read must (re)attempt the bulk load in this state.
    inline bool EventCacheNeedsBulkLoad(EventCacheLoadState state)
    {
        return state != EventCacheLoadState::Loaded;
    }

    // Folds one bulk-load attempt outcome into the load state.
    inline EventCacheLoadState ResolveEventCacheBulkLoad(CountedLoadOutcome outcome)
    {
        return outcome == CountedLoadOutcome::QueryFailed
            ? EventCacheLoadState::Unknown
            : EventCacheLoadState::Loaded;
    }

    // Whether a read of ONE event yields a KNOWN value. A cached entry is
    // always known (it came from a confirmed write or a successful load); an
    // ABSENT entry is confirmed absent (zero) only when the bulk-load state is
    // Loaded. Absent + Unloaded/Unknown is NOT knowledge: destructive callers
    // (timed-logout deactivation, marker clearing) must skip their mutation
    // instead of consuming the default zero as an expired event.
    inline bool EventValueKnown(EventCacheLoadState state, bool hasCachedEntry)
    {
        return hasCachedEntry || state == EventCacheLoadState::Loaded;
    }

    enum class EventPersistOutcome
    {
        // The value violates the schema; nothing was attempted.
        Rejected,
        // The synchronous existence probe failed; nothing was written.
        ProbeFailed,
        // The statement ran and reported failure; durable state is unknown to
        // the caller, which must reload it rather than publish the intended
        // value.
        ExecuteFailed,
        // The statement ran and reported success; the caller may now (and only
        // now) publish the value to its cache.
        Persisted,
    };

    // Drives the execution-confirmed persistence flow for one event write:
    // schema validation first, then value == 0 -> Delete; otherwise a
    // synchronous existence probe decides Update (matching rows exist) vs
    // Insert - the schema has no unique key on (owner, bot, event), so a blind
    // upsert is not available, and DELETE-then-INSERT loses the row when the
    // INSERT fails. `probe` returns whether matching rows exist, or nullopt
    // when the probe itself failed; `execute` runs the one statement
    // synchronously and returns its actual execution result. Queued SQL is not
    // durable success: both callables must be backed by synchronous,
    // result-returning paths (DirectPExecute), never by transaction queueing.
    template <typename ProbeFn, typename ExecuteFn>
    EventPersistOutcome PersistEventValue(std::string const& event, std::string const& data, uint32_t value,
        ProbeFn&& probe, ExecuteFn&& execute)
    {
        if (!EventValueFitsSchema(event, data))
            return EventPersistOutcome::Rejected;

        EventWriteKind kind = EventWriteKind::Delete;
        if (value)
        {
            std::optional<bool> const exists = probe();
            if (!exists)
                return EventPersistOutcome::ProbeFailed;

            kind = *exists ? EventWriteKind::Update : EventWriteKind::Insert;
        }

        return execute(kind) ? EventPersistOutcome::Persisted : EventPersistOutcome::ExecuteFailed;
    }
}
