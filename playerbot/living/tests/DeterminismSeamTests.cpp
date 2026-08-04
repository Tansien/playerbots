#include "LivingTest.h"

#include "playerbot/living/testing/LivingDeterminism.h"

#include <set>

// Host tests for the deterministic clock, nonce, and fault seams.

using living::IdentityNonce;
using living::LivingFaultPlan;
using living::ManualLivingClock;
using living::SequentialNonceProvider;
using living::SystemLivingClock;

LIVING_TEST(ManualClockMovesOnlyWhenAdvanced)
{
    ManualLivingClock clock(1'000'000, 500);

    LIVING_CHECK(clock.UtcNowMs() == 1'000'000);
    LIVING_CHECK(clock.MonotonicNowMs() == 500);
    LIVING_CHECK(clock.UtcNowMs() == 1'000'000);

    clock.AdvanceMs(250);
    LIVING_CHECK(clock.UtcNowMs() == 1'000'250);
    LIVING_CHECK(clock.MonotonicNowMs() == 750);
}

LIVING_TEST(SystemClockIsSaneAndMonotonic)
{
    SystemLivingClock clock;

    // 2020-01-01T00:00:00Z in epoch milliseconds: any earlier value means the
    // UTC lane is not wall-clock based at all.
    LIVING_CHECK(clock.UtcNowMs() > 1'577'836'800'000ULL);

    uint64_t const first = clock.MonotonicNowMs();
    uint64_t const second = clock.MonotonicNowMs();
    LIVING_CHECK(second >= first);
}

LIVING_TEST(NonceSequencesAreDeterministicPerSeed)
{
    SequentialNonceProvider a(42);
    SequentialNonceProvider b(42);
    SequentialNonceProvider other(43);

    IdentityNonce const firstA = a.NextNonce();
    LIVING_CHECK(firstA == b.NextNonce());
    LIVING_CHECK(a.NextNonce() == b.NextNonce());

    LIVING_CHECK(!(firstA == other.NextNonce()));
}

LIVING_TEST(NoncesAreNeverAbsentAndAllDistinct)
{
    // Pairwise distinctness, not just adjacent: a period-2 regression
    // (A, B, A, B, ...) would pass every neighbour comparison while handing
    // out colliding identity nonces.
    SequentialNonceProvider provider(7);

    std::set<IdentityNonce> seen;
    for (int i = 0; i < 256; ++i)
    {
        IdentityNonce const nonce = provider.NextNonce();
        LIVING_CHECK(!(nonce == IdentityNonce{}));
        LIVING_CHECK(seen.insert(nonce).second);
    }

    LIVING_CHECK(seen.size() == 256);
}

LIVING_TEST(FaultPlanFailsExactlyTheArmedCount)
{
    LivingFaultPlan plan;

    LIVING_CHECK(!plan.ShouldFail("direct-commit"));
    LIVING_CHECK(plan.RemainingFailures("direct-commit") == 0);

    plan.ArmFailures("direct-commit", 2);
    LIVING_CHECK(plan.RemainingFailures("direct-commit") == 2);

    LIVING_CHECK(plan.ShouldFail("direct-commit"));
    LIVING_CHECK(plan.ShouldFail("direct-commit"));
    LIVING_CHECK(!plan.ShouldFail("direct-commit"));
    LIVING_CHECK(plan.RemainingFailures("direct-commit") == 0);

    // Re-arming with zero disarms.
    plan.ArmFailures("re-read", 1);
    plan.ArmFailures("re-read", 0);
    LIVING_CHECK(!plan.ShouldFail("re-read"));
}

LIVING_TEST(FaultPlanSitesAreIndependent)
{
    // Two sites armed at once: consuming one must neither consume nor
    // resurrect the other (a single global failure counter would fail here).
    LivingFaultPlan plan;
    plan.ArmFailures("direct-commit", 2);
    plan.ArmFailures("re-read", 1);

    LIVING_CHECK(plan.ShouldFail("direct-commit"));
    LIVING_CHECK(plan.RemainingFailures("direct-commit") == 1);
    LIVING_CHECK(plan.RemainingFailures("re-read") == 1);

    LIVING_CHECK(plan.ShouldFail("re-read"));
    LIVING_CHECK(!plan.ShouldFail("re-read"));
    LIVING_CHECK(plan.RemainingFailures("re-read") == 0);
    LIVING_CHECK(plan.RemainingFailures("direct-commit") == 1);

    LIVING_CHECK(plan.ShouldFail("direct-commit"));
    LIVING_CHECK(!plan.ShouldFail("direct-commit"));
    LIVING_CHECK(!plan.ShouldFail("re-read"));
}
