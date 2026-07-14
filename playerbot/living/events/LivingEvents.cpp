#include "LivingEvents.h"

namespace living
{
    char const* ToString(LivingEventType value)
    {
        switch (value)
        {
            case LivingEventType::LOGIN_OBSERVED: return "login_observed";
            case LivingEventType::LOGOUT_OBSERVED: return "logout_observed";
            case LivingEventType::SCHEDULE_TRANSITION: return "schedule_transition";
            case LivingEventType::GOAL_TRANSITION: return "goal_transition";
            case LivingEventType::GROUP_JOINED: return "group_joined";
            case LivingEventType::GROUP_LEFT: return "group_left";
            case LivingEventType::TRAVEL_COMPLETED: return "travel_completed";
            case LivingEventType::TRAVEL_FAILED: return "travel_failed";
            case LivingEventType::QUEST_ACCEPTED: return "quest_accepted";
            case LivingEventType::QUEST_REWARDED: return "quest_rewarded";
            case LivingEventType::SYNTHETIC_ACTION_REQUESTED: return "synthetic_action_requested";
            case LivingEventType::SYNTHETIC_ACTION_APPLIED: return "synthetic_action_applied";
            case LivingEventType::SYNTHETIC_ACTION_FAILED: return "synthetic_action_failed";
            case LivingEventType::SYNTHETIC_ACTION_RECONCILED: return "synthetic_action_reconciled";
            case LivingEventType::SYNTHETIC_ACTION_CANCELLED: return "synthetic_action_cancelled";
            case LivingEventType::Count: break;
        }

        return "invalid_event";
    }
}
