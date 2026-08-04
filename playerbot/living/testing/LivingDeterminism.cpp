#include "LivingDeterminism.h"

#include <chrono>

namespace living
{
    uint64_t SystemLivingClock::UtcNowMs()
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    }

    uint64_t SystemLivingClock::MonotonicNowMs()
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    ManualLivingClock::ManualLivingClock(uint64_t utcMs, uint64_t monotonicMs)
        : m_utcMs(utcMs), m_monotonicMs(monotonicMs)
    {
    }

    uint64_t ManualLivingClock::UtcNowMs()
    {
        return m_utcMs;
    }

    uint64_t ManualLivingClock::MonotonicNowMs()
    {
        return m_monotonicMs;
    }

    void ManualLivingClock::AdvanceMs(uint64_t deltaMs)
    {
        m_utcMs += deltaMs;
        m_monotonicMs += deltaMs;
    }

    namespace
    {
        // splitmix64: a small, platform-stable mixing step. Statistical quality
        // is irrelevant here; the sequence only has to be deterministic per
        // seed and free of the all-zero value.
        uint64_t Mix(uint64_t value)
        {
            value += 0x9e3779b97f4a7c15ULL;
            value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
            value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
            return value ^ (value >> 31);
        }
    }

    SequentialNonceProvider::SequentialNonceProvider(uint64_t seed)
        : m_seed(seed)
    {
    }

    IdentityNonce SequentialNonceProvider::NextNonce()
    {
        IdentityNonce nonce{};

        while (nonce == IdentityNonce{})
        {
            uint64_t const high = Mix(m_seed ^ Mix(m_counter * 2));
            uint64_t const low = Mix(m_seed ^ Mix(m_counter * 2 + 1));
            ++m_counter;

            for (size_t i = 0; i < 8; ++i)
            {
                nonce[i] = static_cast<uint8_t>(high >> (8 * i));
                nonce[8 + i] = static_cast<uint8_t>(low >> (8 * i));
            }
        }

        return nonce;
    }

    void LivingFaultPlan::ArmFailures(std::string const& site, uint32_t failures)
    {
        if (failures == 0)
            m_armed.erase(site);
        else
            m_armed[site] = failures;
    }

    bool LivingFaultPlan::ShouldFail(std::string const& site)
    {
        auto entry = m_armed.find(site);
        if (entry == m_armed.end())
            return false;

        if (--entry->second == 0)
            m_armed.erase(entry);

        return true;
    }

    uint32_t LivingFaultPlan::RemainingFailures(std::string const& site) const
    {
        auto entry = m_armed.find(site);
        return entry == m_armed.end() ? 0 : entry->second;
    }
}
