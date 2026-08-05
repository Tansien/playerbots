#pragma once

// Minimal Phase 0 event vocabulary and the replaceable telemetry sink
// (0002 section 5). Phase 0 emits observations that already exist -- login,
// logout, group membership, travel, quests, and intercepted mutation
// decisions; schedule and goal transitions arrive with their 0.1 components.
// Telemetry is observation, never durable truth: no policy or lifecycle
// decision may depend on a sink having seen an event.

#include "playerbot/living/policy/OrganicPolicy.h"
#include "playerbot/living/snapshots/LivingSnapshots.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace living
{
    enum class LivingEventKind : uint8_t
    {
        BotLogin = 0,
        BotLogout,
        GroupJoin,
        GroupLeave,
        TravelCompleted,
        TravelFailed,
        QuestAccepted,
        QuestRewarded,
        MutationDecision,

        Count
    };

    char const* ToString(LivingEventKind value);

    // One fixed-size observation. Fields that are not meaningful for a kind
    // keep their inert defaults -- zero for plain values, the Count sentinel
    // for every enum, so an unpopulated field can never read as a real
    // outcome. subjectId carries the kind-specific id (quest id, group id,
    // travel destination id); the action/source/decision/reason group is
    // meaningful only for MutationDecision.
    struct LivingEvent
    {
        LivingEventKind kind = LivingEventKind::Count;
        LivingIdentitySnapshot identity;
        uint64_t utcMs = 0;
        uint32_t subjectId = 0;

        // MutationDecision payload: what was asked (action, source) and what
        // the evaluator answered (decision, reason). OrganicSourceKind arrives
        // with the OrganicPolicy.h include above. The sentinel defaults render
        // as INVALID_* rather than resembling a genuine fail-closed denial.
        OrganicActionKind action = OrganicActionKind::Count;
        OrganicSourceKind source = OrganicSourceKind::Count;
        OrganicDecision decision = OrganicDecision::Count;
        OrganicReasonCode reason = OrganicReasonCode::Count;

        friend bool operator==(LivingEvent const&, LivingEvent const&) = default;
    };

    static_assert(std::is_trivially_copyable_v<LivingEvent>,
        "events are observations passed by value; keep them trivially copyable");
    // Same two-layer enforcement as the snapshot types: is_trivially_copyable
    // does not stop raw pointers, so the size pin makes any new member -- e.g.
    // a Player* that would dangle inside a sink outliving the emitting frame --
    // a conscious, reviewable edit.
    // 48, not 41: the trailing enum run (uint16_t action, then three uint8_t
    // enums) exactly filled the previous 40 bytes, so the source byte rounds
    // the whole event up to the next multiple of its 8-byte alignment.
    static_assert(sizeof(LivingEvent) == 48,
        "new LivingEvent member: verify it is pure value state, then update this pin");

    // Builds the composite mutation-decision observation from an evaluation.
    LivingEvent MakeMutationDecisionEvent(LivingIdentitySnapshot const& identity, uint64_t utcMs,
        OrganicActionKind action, OrganicSourceKind source, OrganicEvaluation evaluation);

    // Replaceable Phase 0 telemetry surface.
    class LivingRealmTelemetrySink
    {
    public:
        virtual ~LivingRealmTelemetrySink() = default;

        virtual void Emit(LivingEvent const& event) = 0;
    };

    // In-memory sink for host tests. Not thread-safe by design: production
    // emission can occur on the world thread AND on map-update worker threads,
    // so any production sink must synchronize itself; this test sink is for
    // single-threaded host suites only and must not be swapped into runtime use.
    class InMemoryTelemetrySink final : public LivingRealmTelemetrySink
    {
    public:
        void Emit(LivingEvent const& event) override;

        std::vector<LivingEvent> const& Events() const;
        size_t CountOf(LivingEventKind kind) const;
        void Clear();

    private:
        std::vector<LivingEvent> m_events;
    };
}
