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
