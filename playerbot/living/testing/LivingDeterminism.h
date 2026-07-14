#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <random>
#include <string>

namespace living
{
    // Injectable clock, token, randomness, and fault seams (Phase 0 requirement in
    // 0001 section 7). Production implementations wrap the standard library; test
    // implementations are deterministic instances with no global mutable state, so
    // test sequences are reproducible.

    class LivingClock
    {
    public:
        virtual ~LivingClock() = default;

        virtual uint64_t NowUtcMs() = 0;      // UTC epoch milliseconds (persistable, 0001A A.3)
        virtual uint64_t MonotonicMs() = 0;   // process-local only; never persisted
    };

    class SystemLivingClock final : public LivingClock
    {
    public:
        uint64_t NowUtcMs() override;
        uint64_t MonotonicMs() override;
    };

    class TestLivingClock final : public LivingClock
    {
    public:
        explicit TestLivingClock(uint64_t startUtcMs = 0, uint64_t startMonotonicMs = 0)
            : utcMs(startUtcMs), monotonicMs(startMonotonicMs) {}

        uint64_t NowUtcMs() override { return utcMs; }
        uint64_t MonotonicMs() override { return monotonicMs; }

        void Advance(uint64_t deltaMs) { utcMs += deltaMs; monotonicMs += deltaMs; }
        void SetUtcMs(uint64_t value) { utcMs = value; }

    private:
        uint64_t utcMs;
        uint64_t monotonicMs;
    };

    // 16-byte identity nonces and operation/action tokens (0001A A.3, 0002B B.5).
    using LivingToken = std::array<uint8_t, 16>;

    class LivingTokenProvider
    {
    public:
        virtual ~LivingTokenProvider() = default;

        virtual LivingToken NextToken() = 0;
    };

    // Production tokens draw every byte from std::random_device. Phase 0 creates no
    // durable identity or audit rows; before 0.1 persists production nonces this
    // provider must be validated as cryptographically strong per 0003 section 2.
    class RandomLivingTokenProvider final : public LivingTokenProvider
    {
    public:
        LivingToken NextToken() override;
    };

    // Deterministic tokens: bytes 0-7 hold the seed, bytes 8-15 a strictly
    // increasing counter (both big-endian). Uniqueness within a sequence is
    // guaranteed by construction; equal seeds reproduce equal sequences.
    class DeterministicLivingTokenProvider final : public LivingTokenProvider
    {
    public:
        explicit DeterministicLivingTokenProvider(uint64_t seed) : seed(seed), counter(0) {}

        LivingToken NextToken() override;

    private:
        uint64_t seed;
        uint64_t counter;
    };

    // Seeded pseudo-randomness for deterministic tests (and any later production
    // caller that supplies its own seed source).
    class SeededLivingRandom
    {
    public:
        explicit SeededLivingRandom(uint64_t seed) : engine(seed) {}

        uint32_t NextUInt32();
        // Uniform in [0, boundExclusive); boundExclusive == 0 returns 0.
        uint32_t NextUInt32(uint32_t boundExclusive);
        uint64_t NextUInt64();

    private:
        std::mt19937_64 engine;
    };

    // Named fault-injection points for the 0002B B.10 crash windows. Production
    // code asks ShouldFail(point) at each seam; the production injector never
    // fails, the test injector fails where armed.
    namespace LivingFaultPoints
    {
        inline constexpr char const* BeforeRequestCommit = "audit.before_request_commit";
        inline constexpr char const* AfterRequestCommitBeforeVerify = "audit.after_request_commit_before_verify";
        inline constexpr char const* BeforeDispatch = "audit.before_dispatch";
        inline constexpr char const* BeforeMutation = "audit.before_mutation";
        inline constexpr char const* AfterMutationBeforeSave = "audit.after_mutation_before_save";
        inline constexpr char const* AfterSaveBeforeTerminalCommit = "audit.after_save_before_terminal_commit";
        inline constexpr char const* AfterTerminalCommitBeforeVerify = "audit.after_terminal_commit_before_verify";
        inline constexpr char const* DuringDuplicateDispatch = "audit.during_duplicate_dispatch";
        inline constexpr char const* DuringDatabaseOutage = "audit.during_database_outage";
        inline constexpr char const* DuringStartupReconciliation = "audit.during_startup_reconciliation";
    }

    class LivingFaultInjector
    {
    public:
        virtual ~LivingFaultInjector() = default;

        virtual bool ShouldFail(char const* point) = 0;
    };

    class NoopLivingFaultInjector final : public LivingFaultInjector
    {
    public:
        bool ShouldFail(char const* /*point*/) override { return false; }
    };

    class TestLivingFaultInjector final : public LivingFaultInjector
    {
    public:
        // Arms `point` to fail for the next `count` queries.
        void ArmFault(std::string const& point, uint32_t count = 1);

        bool ShouldFail(char const* point) override;

        uint32_t FiredCount(std::string const& point) const;

    private:
        std::map<std::string, uint32_t> armed;
        std::map<std::string, uint32_t> fired;
    };
}
