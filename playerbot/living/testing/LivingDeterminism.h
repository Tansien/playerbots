#pragma once

// Deterministic time, nonce, and fault seams (Phase 0, 0001 section 7).
// Production Living Realm code takes these interfaces instead of reading the
// wall clock, ambient randomness, or performing fallible work directly, so
// host tests replay exact schedules and inject faults deterministically
// (AGENTS.md: deterministic code stays independent of wall-clock time and
// ambient randomness).

#include <array>
#include <cstdint>
#include <map>
#include <string>

namespace living
{
    // UTC epoch milliseconds are the durable time base; monotonic milliseconds
    // are only ever meaningful inside one process (0001A A.3).
    class LivingClock
    {
    public:
        virtual ~LivingClock() = default;

        virtual uint64_t UtcNowMs() = 0;
        virtual uint64_t MonotonicNowMs() = 0;
    };

    // Production clock over std::chrono.
    class SystemLivingClock final : public LivingClock
    {
    public:
        uint64_t UtcNowMs() override;
        uint64_t MonotonicNowMs() override;
    };

    // Test clock: starts where constructed and moves only when advanced.
    class ManualLivingClock final : public LivingClock
    {
    public:
        ManualLivingClock(uint64_t utcMs, uint64_t monotonicMs);

        uint64_t UtcNowMs() override;
        uint64_t MonotonicNowMs() override;

        // Advances both timelines by the same delta.
        void AdvanceMs(uint64_t deltaMs);

    private:
        uint64_t m_utcMs;
        uint64_t m_monotonicMs;
    };

    using IdentityNonce = std::array<uint8_t, 16>;

    // The all-zero nonce means "absent identity" everywhere; a provider must
    // never produce it.
    class LivingNonceProvider
    {
    public:
        virtual ~LivingNonceProvider() = default;

        virtual IdentityNonce NextNonce() = 0;
    };

    // Test provider: a seed-keyed deterministic sequence. Two providers with
    // one seed produce the same sequence; distinct seeds diverge.
    class SequentialNonceProvider final : public LivingNonceProvider
    {
    public:
        explicit SequentialNonceProvider(uint64_t seed);

        IdentityNonce NextNonce() override;

    private:
        uint64_t m_seed;
        uint64_t m_counter = 0;
    };

    // Deterministic fault plan: a test arms a number of failures for a named
    // site; the production seam asks ShouldFail at the fault point. Unarmed
    // sites never fail; armed sites fail exactly the armed number of times,
    // then recover. Site names are free-form and owned by the seam that
    // queries them.
    class LivingFaultPlan
    {
    public:
        void ArmFailures(std::string const& site, uint32_t failures);

        // Consumes and reports one armed failure, if any remain.
        bool ShouldFail(std::string const& site);

        uint32_t RemainingFailures(std::string const& site) const;

    private:
        std::map<std::string, uint32_t> m_armed;
    };
}
