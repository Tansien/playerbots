#include "LivingDeterminism.h"

#include <chrono>

namespace living
{
    uint64_t SystemLivingClock::NowUtcMs()
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    }

    uint64_t SystemLivingClock::MonotonicMs()
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    LivingToken RandomLivingTokenProvider::NextToken()
    {
        std::random_device device;
        LivingToken token{};
        for (size_t i = 0; i < token.size(); i += 4)
        {
            uint32_t const value = device();
            token[i + 0] = static_cast<uint8_t>(value >> 24);
            token[i + 1] = static_cast<uint8_t>(value >> 16);
            token[i + 2] = static_cast<uint8_t>(value >> 8);
            token[i + 3] = static_cast<uint8_t>(value);
        }

        return token;
    }

    LivingToken DeterministicLivingTokenProvider::NextToken()
    {
        ++counter;

        LivingToken token{};
        for (size_t i = 0; i < 8; ++i)
        {
            token[i] = static_cast<uint8_t>(seed >> (56 - 8 * i));
            token[8 + i] = static_cast<uint8_t>(counter >> (56 - 8 * i));
        }

        return token;
    }

    uint32_t SeededLivingRandom::NextUInt32()
    {
        return static_cast<uint32_t>(engine());
    }

    uint32_t SeededLivingRandom::NextUInt32(uint32_t boundExclusive)
    {
        if (boundExclusive == 0)
            return 0;

        // std::uniform_int_distribution is not portable across standard libraries.
        // Rejection sampling keeps the result exactly uniform (no modulo/multiply
        // bias) while staying reproducible: the loop consumes engine outputs
        // deterministically.
        uint32_t const rejectBelow = static_cast<uint32_t>(0u - boundExclusive) % boundExclusive;
        uint32_t value = NextUInt32();
        while (value < rejectBelow)
            value = NextUInt32();

        return value % boundExclusive;
    }

    uint64_t SeededLivingRandom::NextUInt64()
    {
        return engine();
    }

    void TestLivingFaultInjector::ArmFault(std::string const& point, uint32_t count)
    {
        armed[point] += count;
    }

    bool TestLivingFaultInjector::ShouldFail(char const* point)
    {
        if (!point)
            return false;

        auto it = armed.find(point);
        if (it == armed.end() || it->second == 0)
            return false;

        --it->second;
        ++fired[point];
        return true;
    }

    uint32_t TestLivingFaultInjector::FiredCount(std::string const& point) const
    {
        auto it = fired.find(point);
        return it == fired.end() ? 0 : it->second;
    }
}
