#pragma once

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
        // Every write execution-confirmed: the caller may start the login and
        // mutate its in-memory state.
        Persisted,
        // A write failed and every already-written value was restored to its
        // known prior state: nothing durable changed, nothing may start.
        FailedCompensated,
        // A write failed AND compensation failed: durable state is uncertain -
        // the caller must mark its in-memory view dirty and reconcile before
        // trusting it again. Nothing may start.
        FailedCompensationUncertain,
    };

    // Executes the plan in order through `write(event, value, validIn) ->
    // bool` (an execution-confirmed setter). On the first failure the
    // already-written prefix is restored in reverse order.
    template <typename WriteFn>
    ActivationOutcome ExecuteActivationPlan(std::vector<PlannedEventWrite> const& plan, WriteFn&& write)
    {
        size_t written = 0;
        for (; written < plan.size(); ++written)
        {
            if (!write(plan[written].event, plan[written].value, plan[written].validIn))
                break;
        }

        if (written == plan.size())
            return ActivationOutcome::Persisted;

        bool compensated = true;
        for (size_t i = written; i-- > 0;)
            compensated = write(plan[i].event, plan[i].priorValue, plan[i].priorValidIn) && compensated;

        return compensated ? ActivationOutcome::FailedCompensated
                           : ActivationOutcome::FailedCompensationUncertain;
    }
}
