#include "LivingTest.h"

#include "../events/LivingEvents.h"

#include <cstring>
#include <set>
#include <string>

using namespace living;

LIVING_TEST(events_names_are_stable)
{
    LIVING_CHECK(std::strcmp(ToString(LivingEventType::LOGIN_OBSERVED), "login_observed") == 0);
    LIVING_CHECK(std::strcmp(ToString(LivingEventType::LOGOUT_OBSERVED), "logout_observed") == 0);
    LIVING_CHECK(std::strcmp(ToString(LivingEventType::SCHEDULE_TRANSITION), "schedule_transition") == 0);
    LIVING_CHECK(std::strcmp(ToString(LivingEventType::GOAL_TRANSITION), "goal_transition") == 0);
    LIVING_CHECK(std::strcmp(ToString(LivingEventType::GROUP_JOINED), "group_joined") == 0);
    LIVING_CHECK(std::strcmp(ToString(LivingEventType::GROUP_LEFT), "group_left") == 0);
    LIVING_CHECK(std::strcmp(ToString(LivingEventType::TRAVEL_COMPLETED), "travel_completed") == 0);
    LIVING_CHECK(std::strcmp(ToString(LivingEventType::TRAVEL_FAILED), "travel_failed") == 0);
    LIVING_CHECK(std::strcmp(ToString(LivingEventType::QUEST_ACCEPTED), "quest_accepted") == 0);
    LIVING_CHECK(std::strcmp(ToString(LivingEventType::QUEST_REWARDED), "quest_rewarded") == 0);
    LIVING_CHECK(std::strcmp(ToString(LivingEventType::SYNTHETIC_ACTION_REQUESTED), "synthetic_action_requested") == 0);
    LIVING_CHECK(std::strcmp(ToString(LivingEventType::SYNTHETIC_ACTION_APPLIED), "synthetic_action_applied") == 0);
    LIVING_CHECK(std::strcmp(ToString(LivingEventType::SYNTHETIC_ACTION_FAILED), "synthetic_action_failed") == 0);
    LIVING_CHECK(std::strcmp(ToString(LivingEventType::SYNTHETIC_ACTION_RECONCILED), "synthetic_action_reconciled") == 0);
    LIVING_CHECK(std::strcmp(ToString(LivingEventType::SYNTHETIC_ACTION_CANCELLED), "synthetic_action_cancelled") == 0);

    std::set<std::string> names;
    for (size_t i = 0; i < static_cast<size_t>(LivingEventType::Count); ++i)
        LIVING_CHECK(names.insert(ToString(static_cast<LivingEventType>(i))).second);
    LIVING_CHECK(names.size() == 15);
}

LIVING_TEST(events_test_sink_preserves_order)
{
    OrderedTestEventSink sink;

    LivingEvent first;
    first.type = LivingEventType::LOGIN_OBSERVED;
    first.characterGuid = 1;
    first.occurredAtMs = 100;
    sink.Emit(first);

    LivingEvent second;
    second.type = LivingEventType::QUEST_ACCEPTED;
    second.characterGuid = 1;
    second.identityNonce = { 9, 9, 9 }; // same guid, distinct identity
    second.occurredAtMs = 200;
    second.detail = "quest=123";
    sink.Emit(second);

    LivingEvent third;
    third.type = LivingEventType::LOGOUT_OBSERVED;
    third.characterGuid = 2;
    third.occurredAtMs = 150; // order is emission order, not timestamp order
    sink.Emit(third);

    LIVING_CHECK(sink.events.size() == 3);
    LIVING_CHECK(sink.events[0].type == LivingEventType::LOGIN_OBSERVED);
    LIVING_CHECK(sink.events[1].type == LivingEventType::QUEST_ACCEPTED);
    LIVING_CHECK(sink.events[1].detail == "quest=123");
    LIVING_CHECK(sink.events[1].identityNonce[0] == 9);  // identity survives the sink
    LIVING_CHECK((sink.events[1].actionToken == std::array<uint8_t, 16>{})); // token only on synthetic events
    LIVING_CHECK((sink.events[0].identityNonce == std::array<uint8_t, 16>{})); // zero when n/a
    LIVING_CHECK(sink.events[2].type == LivingEventType::LOGOUT_OBSERVED);
    LIVING_CHECK(sink.events[2].characterGuid == 2);

    // Action tokens belong to the SYNTHETIC_ACTION_* events (0002B).
    LivingEvent synthetic;
    synthetic.type = LivingEventType::SYNTHETIC_ACTION_REQUESTED;
    synthetic.characterGuid = 3;
    synthetic.identityNonce = { 7 };
    synthetic.actionToken = { 4, 2 };
    synthetic.occurredAtMs = 300;
    sink.Emit(synthetic);
    LIVING_CHECK(sink.events.size() == 4);
    LIVING_CHECK(sink.events[3].actionToken[0] == 4);
}

LIVING_TEST(events_noop_sink_has_no_observable_effect)
{
    NoopLivingEventSink sink;
    LivingEventSink& base = sink;

    LivingEvent event;
    event.type = LivingEventType::SYNTHETIC_ACTION_REQUESTED;
    event.characterGuid = 7;
    for (int i = 0; i < 3; ++i)
        base.Emit(event);

    // Nothing stored, nothing thrown; the event payload is untouched.
    LIVING_CHECK(event.characterGuid == 7);
    LIVING_CHECK(event.type == LivingEventType::SYNTHETIC_ACTION_REQUESTED);
}
