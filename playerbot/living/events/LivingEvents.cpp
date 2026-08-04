#include "LivingEvents.h"

namespace living
{
    char const* ToString(LivingEventKind value)
    {
        switch (value)
        {
            case LivingEventKind::BotLogin: return "BotLogin";
            case LivingEventKind::BotLogout: return "BotLogout";
            case LivingEventKind::GroupJoin: return "GroupJoin";
            case LivingEventKind::GroupLeave: return "GroupLeave";
            case LivingEventKind::TravelCompleted: return "TravelCompleted";
            case LivingEventKind::TravelFailed: return "TravelFailed";
            case LivingEventKind::QuestAccepted: return "QuestAccepted";
            case LivingEventKind::QuestRewarded: return "QuestRewarded";
            case LivingEventKind::MutationDecision: return "MutationDecision";
            case LivingEventKind::Count: break;
        }

        return "INVALID_EVENT";
    }

    LivingEvent MakeMutationDecisionEvent(LivingIdentitySnapshot const& identity, uint64_t utcMs,
        OrganicActionKind action, OrganicEvaluation evaluation)
    {
        LivingEvent event;
        event.kind = LivingEventKind::MutationDecision;
        event.identity = identity;
        event.utcMs = utcMs;
        event.action = action;
        event.decision = evaluation.decision;
        event.reason = evaluation.reason;
        return event;
    }

    void InMemoryTelemetrySink::Emit(LivingEvent const& event)
    {
        m_events.push_back(event);
    }

    std::vector<LivingEvent> const& InMemoryTelemetrySink::Events() const
    {
        return m_events;
    }

    size_t InMemoryTelemetrySink::CountOf(LivingEventKind kind) const
    {
        size_t count = 0;
        for (LivingEvent const& event : m_events)
            if (event.kind == kind)
                ++count;

        return count;
    }

    void InMemoryTelemetrySink::Clear()
    {
        m_events.clear();
    }
}
