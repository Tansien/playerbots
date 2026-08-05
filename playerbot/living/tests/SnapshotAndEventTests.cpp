#include "LivingTest.h"

#include "playerbot/living/events/LivingEvents.h"
#include "playerbot/living/snapshots/LivingSnapshots.h"

#include <cstring>

// Host tests for the immutable snapshot foundation and the Phase 0 event
// vocabulary + telemetry sink.

using living::Evaluate;
using living::InMemoryTelemetrySink;
using living::LivingEvent;
using living::LivingEventKind;
using living::LivingIdentitySnapshot;
using living::LivingProposalHeader;
using living::LivingSnapshotHeader;
using living::MakeMutationDecisionEvent;
using living::OrganicActionKind;
using living::OrganicDecision;
using living::OrganicReasonCode;
using living::OrganicRequest;
using living::OrganicSourceKind;
using living::ProposalIsCurrent;
using living::TryGetOrganicActionMetadata;

namespace
{
    LivingIdentitySnapshot Identity(uint32_t guid)
    {
        LivingIdentitySnapshot identity;
        identity.characterGuid = guid;
        identity.identityNonce = { 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9 };
        return identity;
    }
}

LIVING_TEST(SnapshotCopiesAreIndependentValues)
{
    LivingIdentitySnapshot original = Identity(77);
    LivingIdentitySnapshot copy = original;
    LIVING_CHECK(copy == original);

    copy.identityNonce[0] = 0xAA;
    LIVING_CHECK(!(copy == original));
    LIVING_CHECK(original.identityNonce[0] == 9);
}

LIVING_TEST(ProposalsApplyOnlyAgainstTheirExactLiveGeneration)
{
    LivingSnapshotHeader snapshot;
    snapshot.generation = 41;
    snapshot.capturedAtUtcMs = 123456;

    LivingProposalHeader proposal;
    proposal.basedOnGeneration = snapshot.generation;

    LIVING_CHECK(ProposalIsCurrent(proposal, 41));
    LIVING_CHECK(!ProposalIsCurrent(proposal, 42)); // world moved on
    LIVING_CHECK(!ProposalIsCurrent(proposal, 40)); // generation moved backwards
    LIVING_CHECK(!ProposalIsCurrent(LivingProposalHeader{}, 41));

    // Generation 0 is reserved as "never populated": a default-constructed or
    // zero-filled proposal is not current even against a writer whose counter
    // has not been bumped past its own zero initialization yet.
    LIVING_CHECK(!ProposalIsCurrent(LivingProposalHeader{}, 0));
}

LIVING_TEST(EventKindStringsAreStable)
{
    // These literals are stable telemetry identifiers (0002 section 5): every
    // enumerator is pinned exactly, so an edited or misrouted ToString case
    // cannot slip through as a warning.
    struct EventName { LivingEventKind value; char const* name; };
    static EventName const EVENTS[] = {
        { LivingEventKind::BotLogin, "BotLogin" },
        { LivingEventKind::BotLogout, "BotLogout" },
        { LivingEventKind::GroupJoin, "GroupJoin" },
        { LivingEventKind::GroupLeave, "GroupLeave" },
        { LivingEventKind::TravelCompleted, "TravelCompleted" },
        { LivingEventKind::TravelFailed, "TravelFailed" },
        { LivingEventKind::QuestAccepted, "QuestAccepted" },
        { LivingEventKind::QuestRewarded, "QuestRewarded" },
        { LivingEventKind::MutationDecision, "MutationDecision" },
    };
    static_assert(sizeof(EVENTS) / sizeof(EVENTS[0])
        == static_cast<size_t>(LivingEventKind::Count), "pin every event kind");
    for (size_t i = 0; i < static_cast<size_t>(LivingEventKind::Count); ++i)
    {
        // Bijective coverage, not just table length: a duplicated row would
        // satisfy the length static_assert while leaving some kind unpinned.
        LIVING_CHECK(EVENTS[i].value == static_cast<LivingEventKind>(i));
        LIVING_CHECK(std::strcmp(ToString(EVENTS[i].value), EVENTS[i].name) == 0);
    }

    LIVING_CHECK(std::strcmp(ToString(LivingEventKind::Count), "INVALID_EVENT") == 0);
    LIVING_CHECK(std::strcmp(ToString(static_cast<LivingEventKind>(0xFF)), "INVALID_EVENT") == 0);
}

LIVING_TEST(UnpopulatedEventPayloadCannotResembleARealDecision)
{
    // A default-constructed event's payload must render as INVALID_*, never as
    // a genuine evaluation outcome (an unset payload that reads
    // "Deny/UnknownAction" would be indistinguishable from a real fail-closed
    // denial in serialized telemetry).
    LivingEvent const event;
    LIVING_CHECK(std::strcmp(ToString(event.kind), "INVALID_EVENT") == 0);
    LIVING_CHECK(std::strcmp(living::ToString(event.decision), "INVALID_DECISION") == 0);
    LIVING_CHECK(std::strcmp(living::ToString(event.reason), "INVALID_REASON") == 0);
    LIVING_CHECK(std::strcmp(living::ToString(event.source), "INVALID_SOURCE") == 0);
    LIVING_CHECK(TryGetOrganicActionMetadata(event.action) == nullptr);
}

LIVING_TEST(InMemorySinkRecordsEventsInOrder)
{
    InMemoryTelemetrySink sink;
    LIVING_CHECK(sink.Events().empty());

    LivingEvent login;
    login.kind = LivingEventKind::BotLogin;
    login.identity = Identity(5);
    login.utcMs = 100;
    sink.Emit(login);

    LivingEvent quest;
    quest.kind = LivingEventKind::QuestAccepted;
    quest.identity = Identity(5);
    quest.utcMs = 200;
    quest.subjectId = 1234; // quest id
    sink.Emit(quest);

    LIVING_CHECK(sink.Events().size() == 2);
    LIVING_CHECK(sink.Events()[0] == login);
    LIVING_CHECK(sink.Events()[1] == quest);
    LIVING_CHECK(sink.CountOf(LivingEventKind::BotLogin) == 1);
    LIVING_CHECK(sink.CountOf(LivingEventKind::BotLogout) == 0);

    sink.Clear();
    LIVING_CHECK(sink.Events().empty());
}

LIVING_TEST(MutationDecisionEventCarriesTheEvaluationFaithfully)
{
    // The event must carry the action, the requesting source, and the exact
    // decision/reason pair the evaluator returned -- across DISTINCT actions,
    // sources and outcomes, so a builder hardcoding any one of them cannot pass
    // (0002 section 5: intercepted mutation decisions are the observation
    // Phase 0 most needs to see). The source is what attributes an intercepted
    // row to the call site that asked, so it has to survive the trip to a sink.
    LivingIdentitySnapshot const identity = Identity(88);

    struct Expectation
    {
        OrganicActionKind action;
        OrganicSourceKind source;
        OrganicDecision decision;
        OrganicReasonCode reason;
    };
    static Expectation const CASES[] = {
        { OrganicActionKind::RANDOM_TELEPORT, OrganicSourceKind::AiUpdate, OrganicDecision::Deny, OrganicReasonCode::DeniedByClassification },
        { OrganicActionKind::TALENT_SPEND_EARNED, OrganicSourceKind::PlayerChatCommand, OrganicDecision::AllowAutomation, OrganicReasonCode::AllowedAutomation },
        { OrganicActionKind::STUCK_EMERGENCY_TELEPORT, OrganicSourceKind::RandomManager, OrganicDecision::RequireAudit, OrganicReasonCode::AuditRequired },
    };

    for (Expectation const& expected : CASES)
    {
        OrganicRequest request;
        request.characterGuid = identity.characterGuid;
        request.identityNonce = identity.identityNonce;
        request.kind = expected.action;
        request.source = expected.source;

        living::OrganicEvaluation const evaluation = Evaluate(request);
        LIVING_CHECK(evaluation.decision == expected.decision);
        LIVING_CHECK(evaluation.reason == expected.reason);

        LivingEvent const event = MakeMutationDecisionEvent(identity, 777, request.kind, request.source, evaluation);

        LIVING_CHECK(event.kind == LivingEventKind::MutationDecision);
        LIVING_CHECK(event.identity == identity);
        LIVING_CHECK(event.utcMs == 777);
        LIVING_CHECK(event.action == expected.action);
        LIVING_CHECK(event.source == expected.source);
        LIVING_CHECK(event.decision == evaluation.decision);
        LIVING_CHECK(event.reason == evaluation.reason);

        // Equality is memberwise over the whole payload: two rows that differ
        // only in their source must not compare equal, or a misattributed
        // observation would be indistinguishable from a correct one.
        LivingEvent misattributed = event;
        misattributed.source = (expected.source == OrganicSourceKind::ConsoleCommand)
            ? OrganicSourceKind::RecoveryService
            : OrganicSourceKind::ConsoleCommand;
        LIVING_CHECK(!(misattributed == event));
    }
}
