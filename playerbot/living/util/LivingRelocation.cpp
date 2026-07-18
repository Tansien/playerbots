#include "LivingRelocation.h"

namespace living
{
    uint64_t RelocationTracker::Begin(uint32_t botGuid, PendingRelocation record)
    {
        // Begin never silently replaces an active record: a Finalizing record
        // still owes runtime/durable side effects, and even a PendingAck
        // record represents an accepted transfer plus a density reservation.
        // The caller's pump resolves stale PendingAck records (a finished
        // landing either finalizes them or terminally mismatches), so this
        // refusal cannot deadlock a bot whose acknowledgement was lost.
        if (pending.find(botGuid) != pending.end())
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

        // The bot is present and advancing this sweep: reset the offline
        // watchdog so a later absence starts its bounded count fresh.
        record.offlineSweeps = 0;

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
            if (record.homebindVerify == HomebindVerifyState::NotStarted && ops.applyHomebind)
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
                    record.homebindVerifyRequests = 0;
                    record.homebindVerify = HomebindVerifyState::WriteApplied;
                }
                else
                {
                    // Nothing to write (no eligible inn): the operation is
                    // complete by absence, not by failure.
                    record.homebindHandled = true;
                }
            }

            // The verification query enters flight only after a SUCCESSFUL
            // enqueue - all three pinned cores report enqueue failure via a
            // false return, and treating that as in-flight stranded the
            // record in AwaitingResult forever. Failed requests retry on
            // later advances within a bounded budget.
            if (record.homebindVerify == HomebindVerifyState::WriteApplied && ops.requestHomebindVerify)
            {
                if (record.homebindVerifyRequests >= kMaxHomebindVerifyRequests)
                {
                    record.homebindVerify = HomebindVerifyState::GaveUp;
                    if (ops.onHomebindGaveUp)
                        ops.onHomebindGaveUp();
                }
                else
                {
                    ++record.homebindVerifyRequests;
                    if (ops.requestHomebindVerify())
                    {
                        record.homebindVerify = HomebindVerifyState::AwaitingResult;
                        record.awaitingResultAdvances = 0;
                    }
                }
            }
            else if (record.homebindVerify == HomebindVerifyState::AwaitingResult)
            {
                // Watchdog: a result that never arrives (event dropped by the
                // bounded queue, callback lost) re-arms the request instead of
                // holding the record - and its relocation block and density
                // reservation - forever.
                if (++record.awaitingResultAdvances > kAwaitingResultTimeoutAdvances)
                    record.homebindVerify = HomebindVerifyState::WriteApplied;
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

    void RelocationTracker::OnHomebindVerifyResult(uint64_t relocationToken, HomebindVerifyOutcome outcome)
    {
        // SQL-callback context: enqueue only. A full queue drops the event;
        // the awaiting-result watchdog in Advance then re-requests, so a
        // dropped result delays - never strands - the record.
        if (homebindVerifyEvents.size() >= kMaxHomebindVerifyEvents)
            return;

        homebindVerifyEvents.push_back(HomebindVerifyEvent{ relocationToken, outcome });
    }

    std::vector<RelocationTracker::HomebindVerifyEvent> RelocationTracker::DrainHomebindVerifyEvents()
    {
        std::vector<HomebindVerifyEvent> drained(homebindVerifyEvents.begin(), homebindVerifyEvents.end());
        homebindVerifyEvents.clear();
        return drained;
    }

    HomebindVerifyAction RelocationTracker::ApplyHomebindVerify(uint64_t relocationToken, HomebindVerifyOutcome outcome,
        uint32_t maxWriteAttempts)
    {
        // Token-addressed lookup: a stale result from a superseded or
        // cancelled generation matches no live AwaitingResult record and is
        // ignored - it can never consume a later generation's retry budget.
        PendingRelocation* record = nullptr;
        for (auto& [guid, candidate] : pending)
        {
            if (candidate.token == relocationToken)
            {
                record = &candidate;
                break;
            }
        }

        if (!record || record->homebindVerify != HomebindVerifyState::AwaitingResult)
            return HomebindVerifyAction::None;

        if (outcome == HomebindVerifyOutcome::Match)
        {
            record->homebindVerify = HomebindVerifyState::Confirmed;
            return HomebindVerifyAction::Confirmed;
        }

        if (record->homebindWriteAttempts >= maxWriteAttempts)
        {
            // Bounded give-up: the record completes with an explicit error
            // (the caller logs it) instead of blocking the bot's relocation
            // pipeline forever on a write that keeps failing. The in-memory
            // homebind fields are set either way; only restart durability is
            // at risk, and that risk is now logged, not silent.
            record->homebindVerify = HomebindVerifyState::GaveUp;
            return HomebindVerifyAction::GaveUp;
        }

        // Re-arm the write: the next advance re-issues SetHomebindToLocation
        // and a fresh verification.
        record->homebindTargetKnown = false;
        record->homebindVerify = HomebindVerifyState::NotStarted;
        return HomebindVerifyAction::Reissue;
    }

    bool RelocationTracker::Cancel(uint32_t botGuid, RelocationCancelMode mode)
    {
        auto const it = pending.find(botGuid);
        if (it == pending.end())
            return false;

        // Force, or a PendingAck record (nothing durable owed yet): drop it.
        // Ordinary cancel of a Finalizing record RETAINS the ledger so its owed
        // durable work resumes when the bot returns.
        if (mode == RelocationCancelMode::Force || it->second.stage != RelocationStage::Finalizing)
        {
            pending.erase(it);
            return true;
        }

        return false;
    }

    bool RelocationTracker::NoteBotAbsent(uint32_t botGuid)
    {
        auto const it = pending.find(botGuid);
        if (it == pending.end())
            return false;

        // A PendingAck record has no durable debt: drop it as soon as the bot is
        // absent (event-driven retry markers own re-attempting). A Finalizing
        // record is retained across the absence and only dropped once the
        // bounded offline watchdog expires.
        if (it->second.stage != RelocationStage::Finalizing)
        {
            pending.erase(it);
            return true;
        }

        if (++it->second.offlineSweeps > kMaxOfflineSweeps)
        {
            pending.erase(it);
            return true;
        }

        return false;
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

    std::vector<uint32_t> RelocationTracker::TrackedBots() const
    {
        std::vector<uint32_t> bots;
        bots.reserve(pending.size());
        for (auto const& [guid, record] : pending)
            bots.push_back(guid);
        return bots;
    }

    PendingRelocation const* RelocationTracker::FindByToken(uint64_t relocationToken) const
    {
        for (auto const& [guid, record] : pending)
            if (record.token == relocationToken)
                return &record;
        return nullptr;
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
