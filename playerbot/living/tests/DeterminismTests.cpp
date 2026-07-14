#include "LivingTest.h"

#include "../testing/LivingDeterminism.h"

#include <set>
#include <vector>

using namespace living;

LIVING_TEST(determinism_test_clock_is_deterministic)
{
    TestLivingClock clock(1000, 50);
    LIVING_CHECK(clock.NowUtcMs() == 1000);
    LIVING_CHECK(clock.MonotonicMs() == 50);

    clock.Advance(250);
    LIVING_CHECK(clock.NowUtcMs() == 1250);
    LIVING_CHECK(clock.MonotonicMs() == 300);

    clock.SetUtcMs(5000);
    LIVING_CHECK(clock.NowUtcMs() == 5000);
    LIVING_CHECK(clock.MonotonicMs() == 300);

    // Reading never advances anything.
    LIVING_CHECK(clock.NowUtcMs() == clock.NowUtcMs());
}

LIVING_TEST(determinism_system_clock_is_sane)
{
    SystemLivingClock clock;
    // 2020-01-01 in epoch ms; guards against seconds/milliseconds mixups.
    LIVING_CHECK(clock.NowUtcMs() > 1577836800000ull);

    uint64_t const first = clock.MonotonicMs();
    uint64_t const second = clock.MonotonicMs();
    LIVING_CHECK(second >= first);
}

LIVING_TEST(determinism_token_sequences_are_reproducible_and_unique)
{
    DeterministicLivingTokenProvider a(42);
    DeterministicLivingTokenProvider b(42);
    DeterministicLivingTokenProvider other(43);

    std::set<LivingToken> seen;
    LivingToken firstFromOther = other.NextToken();
    for (int i = 0; i < 1000; ++i)
    {
        LivingToken const token = a.NextToken();
        LIVING_CHECK(token == b.NextToken());     // same seed, same sequence
        LIVING_CHECK(seen.insert(token).second);  // unique within the sequence
        LIVING_CHECK(token != firstFromOther);    // different seed, different tokens
    }
}

LIVING_TEST(determinism_random_tokens_are_not_constant)
{
    RandomLivingTokenProvider provider;
    LIVING_CHECK(provider.NextToken() != provider.NextToken());
}

LIVING_TEST(determinism_seeded_random_is_reproducible_and_bounded)
{
    SeededLivingRandom a(7);
    SeededLivingRandom b(7);
    for (int i = 0; i < 100; ++i)
        LIVING_CHECK(a.NextUInt32() == b.NextUInt32());
    LIVING_CHECK(a.NextUInt64() == b.NextUInt64());

    SeededLivingRandom bounded(11);
    for (int i = 0; i < 1000; ++i)
        LIVING_CHECK(bounded.NextUInt32(10) < 10);
    LIVING_CHECK(bounded.NextUInt32(0) == 0);
    LIVING_CHECK(bounded.NextUInt32(1) == 0);

    SeededLivingRandom c(7);
    SeededLivingRandom d(8);
    bool anyDifference = false;
    for (int i = 0; i < 10 && !anyDifference; ++i)
        anyDifference = c.NextUInt32() != d.NextUInt32();
    LIVING_CHECK(anyDifference);
}

LIVING_TEST(determinism_fault_injector_fires_exactly_where_armed)
{
    NoopLivingFaultInjector noop;
    LIVING_CHECK(!noop.ShouldFail(LivingFaultPoints::BeforeRequestCommit));

    TestLivingFaultInjector injector;
    LIVING_CHECK(!injector.ShouldFail(LivingFaultPoints::BeforeRequestCommit));

    injector.ArmFault(LivingFaultPoints::BeforeRequestCommit, 2);
    injector.ArmFault(LivingFaultPoints::AfterMutationBeforeSave);

    LIVING_CHECK(injector.ShouldFail(LivingFaultPoints::BeforeRequestCommit));
    LIVING_CHECK(injector.ShouldFail(LivingFaultPoints::BeforeRequestCommit));
    LIVING_CHECK(!injector.ShouldFail(LivingFaultPoints::BeforeRequestCommit)); // consumed

    LIVING_CHECK(!injector.ShouldFail(LivingFaultPoints::BeforeDispatch)); // never armed
    LIVING_CHECK(injector.ShouldFail(LivingFaultPoints::AfterMutationBeforeSave));

    LIVING_CHECK(injector.FiredCount(LivingFaultPoints::BeforeRequestCommit) == 2);
    LIVING_CHECK(injector.FiredCount(LivingFaultPoints::AfterMutationBeforeSave) == 1);
    LIVING_CHECK(injector.FiredCount(LivingFaultPoints::BeforeDispatch) == 0);

    LIVING_CHECK(!injector.ShouldFail(nullptr));
}
