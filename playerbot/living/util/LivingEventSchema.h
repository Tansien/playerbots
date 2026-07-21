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

    // Events carrying lifecycle CONTROL state: unfinished creation
    // obligations, deletion intents, and temporary identity. A console reset
    // (and any other bulk event wipe) must never delete these - they are the
    // only durable record their crash-safe owners recover from. The reset's
    // SQL exclusion list mirrors exactly this predicate.
    inline bool IsLifecycleControlEvent(std::string const& event)
    {
        return event == "temporary" || event == "delete" || event == "test"
            || event == "create pending"
            || event == "create levelup" || event == "create gear" || event == "create group";
    }

    // Events whose cached value must NEVER be zeroed by validIn expiry: the
    // long-lived configuration/spec rows the production reader already
    // exempted, plus every lifecycle-CONTROL row - an unsettled creation or
    // deletion obligation must stay authoritative until it is explicitly and
    // successfully cleared, no matter how long the server was down or how
    // late a login=0 bot first logs in.
    inline bool IsNonExpiringEvent(std::string const& event)
    {
        return event == "specNo" || event == "specLink" || event == "init"
            || event == "current_time" || event == "always" || event == "selfbot"
            || IsLifecycleControlEvent(event);
    }

    inline bool IsEventValueActive(std::string const& event, uint32_t value,
        uint32_t lastChangeTime, uint32_t validIn, uint32_t now)
    {
        return value && (IsNonExpiringEvent(event) || now < lastChangeTime
            || now - lastChangeTime < validIn);
    }

    inline bool EventValueFitsSchema(std::string const& event, std::string const& data)
    {
        return !event.empty()
            && event.size() <= EVENT_NAME_MAX_BYTES
            && data.size() <= EVENT_DATA_MAX_BYTES;
    }

    // specNo selects the active representation: zero uses specLink, nonzero
    // uses the premade spec. Persist the payload before activating a custom
    // link, and activate a premade spec before clearing its now-dormant link.
    // Short-circuiting keeps the previously active representation intact when
    // the first write fails.
    template <typename WriteFn>
    bool PersistTalentMetadata(int32_t specId, std::string const& specLink, WriteFn&& write)
    {
        if (specId == -1)
            return write("specLink", specLink.empty() ? 0u : 1u, specLink)
                && write("specNo", 0u, std::string());

        return write("specNo", static_cast<uint32_t>(specId + 1), std::string())
            && write("specLink", 0u, std::string());
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

    // Whether a read of ONE event yields a TRUSTED (known) value once dirtiness
    // is folded in. An event is marked dirty when a write REPORTED failure and
    // the follow-up reload could not confirm the durable state - the cached
    // value may already be stale relative to a write that actually landed. Such
    // a value must never gate a lifecycle mutation: a dirty entry reads as NOT
    // trusted regardless of the load state or a lingering cached value (which is
    // retained only for diagnostics) until a reload reconciles it. A confirmed
    // write or a successful reload clears the dirty mark and restores trust.
    inline bool EventValueTrusted(EventCacheLoadState state, bool hasCachedEntry, bool isDirty)
    {
        return !isDirty && EventValueKnown(state, hasCachedEntry);
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

    // Detailed durability result of one event write RELATIVE TO THE REQUESTED
    // STATE. A reported execution failure is NOT proof that nothing mutated:
    // the statement may have applied and the failure been reported anyway, so
    // the setter reloads and classifies. Compensation logic must branch on
    // this - the old bool collapsed "definitely untouched" and "possibly
    // applied" into the same false.
    enum class EventWriteResult
    {
        // Durable state provably equals the requested value (the write
        // succeeded, or a reload after a reported failure proved the value
        // landed anyway).
        DesiredStateConfirmed,
        // The failure occurred BEFORE any mutation (schema rejection, failed
        // probe), or a reload proved the known prior value remained: durable
        // state is confirmed unchanged.
        DefinitelyNotApplied,
        // Execution was attempted and the resulting durable state cannot be
        // confirmed (reload failed, or shows neither the requested nor the
        // prior value).
        StateUnknown,
    };

    // The complete durable state of one event row. A reported-failed write is
    // confirmed against the WHOLE row, never the value alone: the durable row
    // carries time, validIn, value AND data, and a same-value schedule refresh
    // with a new expiry or payload is a DIFFERENT durable state. Two rows are
    // equal only when every column matches.
    struct EventRow
    {
        uint32_t value = 0;
        uint32_t time = 0;
        uint32_t validIn = 0;
        std::string data;

        bool operator==(EventRow const& other) const
        {
            return value == other.value
                && time == other.time
                && validIn == other.validIn
                && data == other.data;
        }
        bool operator!=(EventRow const& other) const { return !(*this == other); }
    };

    // Classifies the durability of an event write that REPORTED failure, after
    // its row was reloaded, RELATIVE TO THE COMPLETE requested and prior rows.
    // A reported failure is not proof nothing mutated, so the reload is compared
    // as a whole tuple, not by value:
    //   - DesiredStateConfirmed : a non-deletion write whose complete durable
    //     row equals the requested row landed despite the report. A DELETION is
    //     confirmed ONLY by a confirmed-absent row - a still-present row (even
    //     one holding value 0) never confirms a delete.
    //   - DefinitelyNotApplied  : the complete KNOWN prior row remained (the
    //     present row is byte-for-byte the prior row, or the row is confirmed
    //     absent and the prior was known-absent).
    //   - StateUnknown          : the reload failed, or the row matched neither
    //     the requested nor the prior state; the caller marks the event dirty.
    // `priorKnown` MUST be false when the prior state is not trustworthy (for
    // example the event was already dirty before this write): an untrusted prior
    // can never prove DefinitelyNotApplied. `requested`/`prior` are consulted
    // only in the branches noted above.
    inline EventWriteResult ClassifyFailedWriteReload(
        EventReloadOutcome reload,
        bool isDeletion,
        EventRow const& requested,
        EventRow const& durable,
        bool priorKnown,
        bool priorPresent,
        EventRow const& prior)
    {
        switch (reload)
        {
            case EventReloadOutcome::Found:
                if (!isDeletion && durable == requested)
                    return EventWriteResult::DesiredStateConfirmed; // landed despite the report
                if (priorKnown && priorPresent && durable == prior)
                    return EventWriteResult::DefinitelyNotApplied;  // prior row remained intact
                return EventWriteResult::StateUnknown;
            case EventReloadOutcome::ConfirmedAbsent:
                if (isDeletion)
                    return EventWriteResult::DesiredStateConfirmed; // the delete landed
                if (priorKnown && !priorPresent)
                    return EventWriteResult::DefinitelyNotApplied;  // confirmed-absent before AND after
                return EventWriteResult::StateUnknown;
            case EventReloadOutcome::QueryFailed:
            default:
                return EventWriteResult::StateUnknown;
        }
    }

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
