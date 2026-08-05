#include "playerbot/playerbot.h"
#include "playerbot/LivingRealmObserver.h"

#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/living/events/LivingEvents.h"
#include "playerbot/living/testing/LivingDeterminism.h"

#include <mutex>

namespace
{
    // Intercepted fabrication runs on the world thread and on map-update worker
    // threads, so the process sink synchronizes itself rather than assuming a
    // single writer.
    class LogTelemetrySink final : public living::LivingRealmTelemetrySink
    {
    public:
        void Emit(living::LivingEvent const& event) override
        {
            living::OrganicActionMetadata const* row = living::TryGetOrganicActionMetadata(event.action);

            std::lock_guard<std::mutex> guard(m_mutex);
            sLog.outDetail("LivingObserve: action=%s decision=%s reason=%s guid=%u",
                row ? row->name : "INVALID_ACTION",
                living::ToString(event.decision),
                living::ToString(event.reason),
                event.identity.characterGuid);
        }

    private:
        std::mutex m_mutex;
    };

    living::LivingRealmTelemetrySink& Sink()
    {
        static LogTelemetrySink sink;
        return sink;
    }

    // Not named Clock(): the cores' Common.h typedefs a global-scope Clock,
    // and unqualified lookup here would be ambiguous.
    living::LivingClock& ObserverClock()
    {
        static living::SystemLivingClock clock;
        return clock;
    }
}

namespace living_observer
{
    void RecordDecision(living::OrganicActionKind kind, living::OrganicSourceKind source, uint32 characterGuid)
    {
        try
        {
            living::OrganicRequest request;
            request.characterGuid = characterGuid;
            request.kind = kind;
            request.source = source;
            // No bot carries a Living identity yet, so identityNonce keeps its
            // all-zero "absent identity" value and the evaluator answers for the
            // state that actually exists. Most rows therefore come back
            // Deny/IdentityRequired: that is the honest observe-phase reading,
            // not a missing input.

            living::LivingIdentitySnapshot identity;
            identity.characterGuid = characterGuid;

            Sink().Emit(living::MakeMutationDecisionEvent(identity, ObserverClock().UtcNowMs(), kind,
                living::Evaluate(request)));
        }
        catch (...)
        {
            // Telemetry is observation, never authority (0002 section 5): a
            // failed observation must not disturb the path it observes.
        }
    }

    void RecordDecision(living::OrganicActionKind kind, living::OrganicSourceKind source, Player* bot)
    {
        if (!bot)
            return;

        WorldSession* session = bot->GetSession();
        if (!session)
            return;

        // Living Realm governs the managed random-bot population; player-owned
        // alts are outside its scope (0002 section 2) and are never recorded.
        if (!sPlayerbotAIConfig.IsInRandomAccountList(session->GetAccountId()))
            return;

        RecordDecision(kind, source, bot->GetGUIDLow());
    }
}
