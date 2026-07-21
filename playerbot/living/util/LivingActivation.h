#pragma once

#include "LivingEventSchema.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace living
{
    // Durable activation boundary for one random-bot login.
    //
    // Activation used to start the login first and scatter its event writes
    // afterwards, checking only some of them: a failed `login`/`update` write
    // left a login already running with no durable trace, and a failed
    // `add`/`logout` write could only be flagged, not undone. The rule now:
    // the COMPLETE required durable plan is persisted (and checked) BEFORE
    // any login starts or any in-memory list changes; a partial plan is
    // compensated back to the known prior values, and only an UNCERTAIN
    // compensation forces the dirty-reconciliation path.
    //
    // Writes report the typed EventWriteResult, never a bool: a write that
    // REPORTS failure may still have applied durably (the setter reloads and
    // classifies), so compensation must also restore the FAILED index itself
    // whenever its durable state is unknown - the old bool contract silently
    // assumed a failed writer never mutated.

    // One planned event write plus the KNOWN prior state to restore on
    // compensation (value 0 deletes the row, matching SetEventValue).
    struct PlannedEventWrite
    {
        std::string event;
        uint32_t value = 0;
        uint32_t validIn = 0;
        uint32_t priorValue = 0;
        uint32_t priorValidIn = 0;
    };

    enum class ActivationOutcome
    {
        // Every write confirmed at the requested state: the caller may start
        // the login and mutate its in-memory state.
        Persisted,
        // A write failed and every value that may have changed - including
        // the failed write itself when its durable state was unknown - was
        // CONFIRMED restored to its known prior state: nothing durable
        // changed, nothing may start.
        FailedCompensated,
        // A write failed AND at least one restore could not be CONFIRMED at
        // the prior state: durable state is uncertain - the caller must mark
        // its in-memory view dirty and reconcile before trusting it again.
        // Nothing may start.
        FailedCompensationUncertain,
    };

    // Executes the plan in order through `write(event, value, validIn) ->
    // EventWriteResult`. On the first non-confirmed write, restores in
    // reverse order: the failed index itself is restored too when its result
    // was StateUnknown (it may have mutated despite reporting failure); a
    // DefinitelyNotApplied failure provably left its own event untouched, so
    // only the confirmed earlier writes are restored. A restore counts only
    // when the prior state is CONFIRMED - anything else is uncertain.
    template <typename WriteFn>
    ActivationOutcome ExecuteActivationPlan(std::vector<PlannedEventWrite> const& plan, WriteFn&& write)
    {
        size_t failedIndex = plan.size();
        EventWriteResult failure = EventWriteResult::DefinitelyNotApplied;
        for (size_t i = 0; i < plan.size(); ++i)
        {
            EventWriteResult const result = write(plan[i].event, plan[i].value, plan[i].validIn);
            if (result == EventWriteResult::DesiredStateConfirmed)
                continue;

            failedIndex = i;
            failure = result;
            break;
        }

        if (failedIndex == plan.size())
            return ActivationOutcome::Persisted;

        // StateUnknown: the failed write may have applied - restore it too.
        // DefinitelyNotApplied: provably untouched - restore only the prefix.
        size_t const restoreFrom = failure == EventWriteResult::StateUnknown ? failedIndex + 1 : failedIndex;

        bool restoresConfirmed = true;
        for (size_t i = restoreFrom; i-- > 0;)
        {
            restoresConfirmed = write(plan[i].event, plan[i].priorValue, plan[i].priorValidIn)
                    == EventWriteResult::DesiredStateConfirmed
                && restoresConfirmed;
        }

        return restoresConfirmed ? ActivationOutcome::FailedCompensated
                                 : ActivationOutcome::FailedCompensationUncertain;
    }

    // A `.bot always` toggle may mutate freeAltBots and start/stop the bot's
    // login ONLY after its durable `always` write is CONFIRMED. An unconfirmed
    // write - rejected (DefinitelyNotApplied) or ambiguous (StateUnknown), both
    // collapsed into `false` by SetValue - must leave runtime state unchanged
    // and report failure, so a restart cannot diverge from the running state.
    inline bool AlwaysToggleMayApply(bool alwaysWriteConfirmed)
    {
        return alwaysWriteConfirmed;
    }

    inline bool TimedLogoutDue(bool online, bool addKnown, uint32_t addValue)
    {
        return online && addKnown && !addValue;
    }

    inline bool TimedOfflineBlocksLogin(bool online, bool logoutKnown, uint32_t logoutValue)
    {
        return !online && (!logoutKnown || logoutValue);
    }

    // Typed always-online state. The durable value space is exactly
    // {0 disabled, 1 active, 2 disabled-by-command} (BotAlwaysOnline in the
    // module); any other stored value is invalid, never coerced.
    enum class AlwaysOnlineState : uint32_t
    {
        Disabled = 0,
        Active = 1,
        DisabledByCommand = 2,
    };

    inline bool TryClassifyAlwaysOnline(uint32_t raw, AlwaysOnlineState& out)
    {
        if (raw > static_cast<uint32_t>(AlwaysOnlineState::DisabledByCommand))
            return false;

        out = static_cast<AlwaysOnlineState>(raw);
        return true;
    }

    // What the `.bot always` command may do given the TYPED read of the prior
    // durable state. An untrusted read (failed bulk load, unreconciled dirty
    // row) or an invalid stored value produces NO durable or runtime mutation:
    // toggling against a fabricated zero could flip an active bot's durable
    // row - and its login - based on state that was never confirmed.
    enum class AlwaysToggleDecision
    {
        RefuseUnknown,
        Enable,
        Disable,
    };

    inline AlwaysToggleDecision PlanAlwaysToggle(bool readTrusted, uint32_t raw)
    {
        AlwaysOnlineState state;
        if (!readTrusted || !TryClassifyAlwaysOnline(raw, state))
            return AlwaysToggleDecision::RefuseUnknown;

        return state == AlwaysOnlineState::Active
            ? AlwaysToggleDecision::Disable
            : AlwaysToggleDecision::Enable;
    }

    // Startup-loader decision for one character: schedule always-online ONLY
    // from a trusted, valid read. Unknown state schedules nothing (fail
    // closed) - in particular a config reload during a database blip can never
    // re-include a DISABLED_BY_COMMAND character whose read collapsed to zero.
    // `configuredOnline` is the config-side default after account/character
    // toggles (selfBotLevel == ALWAYS_ACTIVE xor toggle lists).
    inline bool ShouldScheduleAlwaysOnline(bool readTrusted, uint32_t raw, bool configuredOnline)
    {
        AlwaysOnlineState state;
        if (!readTrusted || !TryClassifyAlwaysOnline(raw, state))
            return false;

        if (state == AlwaysOnlineState::DisabledByCommand)
            return false;

        return configuredOnline || state == AlwaysOnlineState::Active;
    }
}
