#include "LivingRelocation.h"

namespace living
{
    uint64_t RelocationTracker::Begin(uint32_t botGuid, PendingRelocation record)
    {
        // A Finalizing record still owes runtime/durable side effects; a new
        // relocation must not start until they are confirmed (or the bot logs
        // out and Cancel drops the record). A PendingAck record is superseded
        // as before: a bot that never acknowledges leaves a record the next
        // attempt overwrites, so event-driven retry doubles as the timeout.
        if (auto it = pending.find(botGuid); it != pending.end()
            && it->second.stage == RelocationStage::Finalizing)
            return 0;

        record.token = nextToken++;
        record.stage = RelocationStage::PendingAck;
        pending[botGuid] = record;
        return record.token;
    }

    RelocationAckResult RelocationTracker::Acknowledge(uint32_t botGuid, uint32_t mapId, float x, float y,
        float z, float orientation, PendingRelocation& out)
    {
        auto const it = pending.find(botGuid);
        if (it == pending.end())
            return RelocationAckResult::NoPending;

        if (it->second.stage == RelocationStage::Finalizing)
            return RelocationAckResult::AlreadyFinalizing;

        // Exact equality: the acknowledgement handlers relocate to the exact
        // stored destination floats, so any difference means a different
        // teleport landed.
        bool const exactMatch = it->second.mapId == mapId
            && it->second.x == x && it->second.y == y && it->second.z == z
            && it->second.orientation == orientation;

        if (!exactMatch)
        {
            // Copy out BEFORE erasing - the map node dies with the erase.
            out = it->second;
            pending.erase(it);
            return RelocationAckResult::TerminalMismatch;
        }

        // The record is RETAINED: it now tracks the owed completion work and
        // keeps blocking new relocations (and reserving the destination) until
        // everything is confirmed.
        it->second.stage = RelocationStage::Finalizing;
        out = it->second;
        return RelocationAckResult::Landed;
    }

    RelocationAdvanceResult RelocationTracker::Advance(uint32_t botGuid, RelocationAdvanceOps const& ops)
    {
        auto const it = pending.find(botGuid);
        if (it == pending.end() || it->second.stage != RelocationStage::Finalizing)
            return RelocationAdvanceResult::NoPending;

        PendingRelocation& record = it->second;

        // Runtime operations run exactly once; they are in-memory and cannot
        // fail, so their flags flip unconditionally.
        if (!record.runtimeResetDone)
        {
            if (ops.runtimeReset)
                ops.runtimeReset();
            record.runtimeResetDone = true;
        }

        if (record.rpgTravelCooldown && !record.rpgCooldownDone)
        {
            if (ops.applyRpgCooldown)
                ops.applyRpgCooldown();
            record.rpgCooldownDone = true;
        }

        // Durable operations flip their flag only on confirmation and are
        // retried on later advances otherwise (SetEventValue and the homebind
        // write are idempotent).
        if (record.reviveRecovery && !record.reviveMarkersHandled && ops.clearReviveMarkers)
        {
            switch (ops.clearReviveMarkers())
            {
                case MarkerClearOutcome::Cleared:
                case MarkerClearOutcome::KeptStillDead:
                    // KeptStillDead is deliberate completion: the bot did not
                    // actually recover, the markers stay, and the event-driven
                    // revive retry owns the next attempt.
                    record.reviveMarkersHandled = true;
                    break;
                case MarkerClearOutcome::WriteFailed:
                    break; // retry on a later advance
            }
        }

        if ((record.setHomebind || record.bindInn) && !record.homebindHandled)
        {
            if (!record.homebindTargetKnown && record.homebindVerify == HomebindVerifyState::NotStarted
                && ops.applyHomebind)
            {
                if (std::optional<HomebindWrite> const written = ops.applyHomebind())
                {
                    record.homebindTargetKnown = true;
                    record.homebindTargetMap = written->mapId;
                    record.homebindTargetX = written->x;
                    record.homebindTargetY = written->y;
                    record.homebindTargetZ = written->z;
                    record.homebindTargetArea = written->areaId;
                    ++record.homebindWriteAttempts;
                    record.homebindVerify = HomebindVerifyState::AwaitingResult;
                    if (ops.requestHomebindVerify)
                        ops.requestHomebindVerify();
                }
                else
                {
                    // Nothing to write (no eligible inn): the operation is
                    // complete by absence, not by failure.
                    record.homebindHandled = true;
                }
            }

            if (record.homebindVerify == HomebindVerifyState::Confirmed
                || record.homebindVerify == HomebindVerifyState::GaveUp)
                record.homebindHandled = true;
        }

        if (record.scheduleNextTeleport && !record.nextTeleportScheduled && ops.scheduleNextTeleport)
        {
            if (ops.scheduleNextTeleport())
                record.nextTeleportScheduled = true;
            // A failed schedule write keeps the record armed: the bot cannot
            // be re-relocated by the old expired event while this record
            // exists, and the write is retried on a later advance.
        }

        bool const allDone = record.runtimeResetDone
            && (!record.rpgTravelCooldown || record.rpgCooldownDone)
            && (!record.reviveRecovery || record.reviveMarkersHandled)
            && ((!record.setHomebind && !record.bindInn) || record.homebindHandled)
            && (!record.scheduleNextTeleport || record.nextTeleportScheduled);

        if (!allDone)
            return RelocationAdvanceResult::Finalizing;

        pending.erase(it);
        return RelocationAdvanceResult::Completed;
    }

    void RelocationTracker::OnHomebindVerifyResult(uint32_t botGuid, HomebindVerifyOutcome outcome)
    {
        // SQL-callback context: enqueue only. A full queue drops the event;
        // the record then simply stays Finalizing and a later advance's
        // re-verification path recovers (the safe direction).
        if (homebindVerifyEvents.size() >= kMaxHomebindVerifyEvents)
            return;

        homebindVerifyEvents.push_back(HomebindVerifyEvent{ botGuid, outcome });
    }

    std::vector<RelocationTracker::HomebindVerifyEvent> RelocationTracker::DrainHomebindVerifyEvents()
    {
        std::vector<HomebindVerifyEvent> drained(homebindVerifyEvents.begin(), homebindVerifyEvents.end());
        homebindVerifyEvents.clear();
        return drained;
    }

    HomebindVerifyAction RelocationTracker::ApplyHomebindVerify(uint32_t botGuid, HomebindVerifyOutcome outcome,
        uint32_t maxAttempts)
    {
        auto const it = pending.find(botGuid);
        if (it == pending.end() || it->second.homebindVerify != HomebindVerifyState::AwaitingResult)
            return HomebindVerifyAction::None;

        PendingRelocation& record = it->second;

        if (outcome == HomebindVerifyOutcome::Match)
        {
            record.homebindVerify = HomebindVerifyState::Confirmed;
            return HomebindVerifyAction::Confirmed;
        }

        if (record.homebindWriteAttempts >= maxAttempts)
        {
            // Bounded give-up: the record completes with an explicit error
            // (the caller logs it) instead of blocking the bot's relocation
            // pipeline forever on a write that keeps failing. The in-memory
            // homebind fields are set either way; only restart durability is
            // at risk, and that risk is now logged, not silent.
            record.homebindVerify = HomebindVerifyState::GaveUp;
            return HomebindVerifyAction::GaveUp;
        }

        // Re-arm the write: the next advance re-issues SetHomebindToLocation
        // and a fresh verification.
        record.homebindTargetKnown = false;
        record.homebindVerify = HomebindVerifyState::NotStarted;
        return HomebindVerifyAction::Reissue;
    }

    void RelocationTracker::Cancel(uint32_t botGuid)
    {
        pending.erase(botGuid);
    }

    bool RelocationTracker::HasPending(uint32_t botGuid) const
    {
        return pending.find(botGuid) != pending.end();
    }

    bool RelocationTracker::IsFinalizing(uint32_t botGuid) const
    {
        auto const it = pending.find(botGuid);
        return it != pending.end() && it->second.stage == RelocationStage::Finalizing;
    }

    std::vector<uint32_t> RelocationTracker::FinalizingBots() const
    {
        std::vector<uint32_t> bots;
        for (auto const& [guid, record] : pending)
            if (record.stage == RelocationStage::Finalizing)
                bots.push_back(guid);
        return bots;
    }

    uint32_t RelocationTracker::CountReservedDestinationsNear(uint32_t mapId, float x, float y, float radius,
        uint32_t excludeBotGuid) const
    {
        uint32_t count = 0;
        for (auto const& [guid, record] : pending)
        {
            if (guid == excludeBotGuid || record.mapId != mapId)
                continue;

            float const dx = record.x - x;
            float const dy = record.y - y;
            if (std::sqrt(dx * dx + dy * dy) <= radius)
                ++count;
        }

        return count;
    }

    PendingRelocation const* RelocationTracker::Find(uint32_t botGuid) const
    {
        auto const it = pending.find(botGuid);
        return it == pending.end() ? nullptr : &it->second;
    }
}
