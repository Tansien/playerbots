#include "LivingTest.h"

#include "../util/LivingRetryBudget.h"

using namespace living;

LIVING_TEST(transient_budget_does_not_leak_across_independent_spawns)
{
    // Finding N7: a hard spawn that burns most of its budget must NOT shorten a
    // later independent spawn's budget. A terminal outcome resets the counter,
    // so the next command starts fresh.
    TransientRetryBudget budget;

    // Spawn #1: 25 transient failures, then a terminal success resets the budget.
    for (uint32_t i = 0; i < 25; ++i)
        LIVING_CHECK(!budget.RecordFailureExhausted(30));
    budget.Reset(); // terminal success (pre-fix: NOT reset)

    // Spawn #2 (independent) gets a FRESH 30, not the leaked 5.
    uint32_t survived = 0;
    while (!budget.RecordFailureExhausted(30))
        ++survived;
    LIVING_CHECK(survived == 29); // 29 deferrals + the 30th exhausts
}

LIVING_TEST(transient_budget_exhausts_at_bound)
{
    TransientRetryBudget budget;
    for (uint32_t i = 0; i < 29; ++i)
        LIVING_CHECK(!budget.RecordFailureExhausted(30));
    LIVING_CHECK(budget.RecordFailureExhausted(30)); // the 30th failure is terminal
}
