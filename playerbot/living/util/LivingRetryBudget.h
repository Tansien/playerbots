#pragma once

#include <cstdint>

namespace living
{
    // A bounded per-command transient-retry budget. One spawn command owns one
    // budget: it counts consecutive transient failures of THAT command and, at
    // the bound, fails it terminally. The test-DSL spawn path used a single
    // context-lifetime counter reset only by a whole-context reset, so a hard
    // spawn left the counter high and a later independent spawn exhausted
    // immediately. Owning the budget per command and Reset()-ing it on every
    // terminal outcome (and at a new spawn) keeps each independent command's
    // budget fresh without weakening the per-command bound.
    struct TransientRetryBudget
    {
        uint32_t attempts = 0;

        // Records one transient failure; returns true when the bound is reached
        // and the command must fail terminally.
        bool RecordFailureExhausted(uint32_t maxAttempts)
        {
            return ++attempts >= maxAttempts;
        }

        void Reset() { attempts = 0; }
    };
}
