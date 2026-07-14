#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace living
{
    // Minimal Phase 0 event vocabulary (0002 section 5). Events are telemetry and
    // hints only: no policy or lifecycle decision may depend on them, and they are
    // never authoritative for online/group/quest/goal state (0001 section 4).
    enum class LivingEventType : uint8_t
    {
        LOGIN_OBSERVED = 0,
        LOGOUT_OBSERVED,
        SCHEDULE_TRANSITION,
        GOAL_TRANSITION,
        GROUP_JOINED,
        GROUP_LEFT,
        TRAVEL_COMPLETED,
        TRAVEL_FAILED,
        QUEST_ACCEPTED,
        QUEST_REWARDED,
        SYNTHETIC_ACTION_REQUESTED,
        SYNTHETIC_ACTION_APPLIED,
        SYNTHETIC_ACTION_FAILED,
        SYNTHETIC_ACTION_RECONCILED,
        SYNTHETIC_ACTION_CANCELLED,

        Count
    };

    // Stable names used by structured sinks; changing one breaks consumers.
    char const* ToString(LivingEventType value);

    struct LivingEvent
    {
        LivingEventType type = LivingEventType::Count;
        uint32_t characterGuid = 0;
        // Identity nonce: a reused low guid with a different nonce is a different
        // identity (0003 section 2), so events carry both. Zero when not applicable.
        std::array<uint8_t, 16> identityNonce{};
        // 0002B action token for the SYNTHETIC_ACTION_* events; zero otherwise.
        std::array<uint8_t, 16> actionToken{};
        uint64_t occurredAtMs = 0; // UTC epoch ms from an injected clock
        std::string detail;        // bounded free-form context; never parsed for decisions
    };

    class LivingEventSink
    {
    public:
        virtual ~LivingEventSink() = default;

        virtual void Emit(LivingEvent const& event) = 0;
    };

    // Production default until a structured sink ships: accepts and drops events
    // with no observable side effect.
    class NoopLivingEventSink final : public LivingEventSink
    {
    public:
        void Emit(LivingEvent const& /*event*/) override {}
    };

    // Deterministic in-memory sink for host-side tests; preserves emission order.
    class OrderedTestEventSink final : public LivingEventSink
    {
    public:
        void Emit(LivingEvent const& event) override { events.push_back(event); }

        std::vector<LivingEvent> events;
    };
}
