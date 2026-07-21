#include "LivingTest.h"

#include "../util/LivingCreationLifecycle.h"

using namespace living;

// CreationLifecycle is the exact transition table the asynchronous creation
// finalizer drives from execution-ordered verify callbacks and
// execution-confirmed write results. These are fault-injection tests of that
// production decision core; the live async-queue/MySQL wiring is
// compile-verified only.

LIVING_TEST(creation_lifecycle_character_save_failure_is_retryable_only_when_confirmed)
{
    // The character INSERT transaction rolled back: the verify observes a
    // confirmed absence, and since no dependent state was ever written the
    // attempt is retryable.
    CreationLifecycle rolledBack;
    LIVING_CHECK(rolledBack.OnCharacterVerify(RowVerifyOutcome::Absent, 5) == CreationStage::FailedRetryable);

    // A FAILED verify query is not absence: bounded retries, then quarantine -
    // unknown durability never becomes a retryable claim.
    CreationLifecycle unknown;
    LIVING_CHECK(unknown.OnCharacterVerify(RowVerifyOutcome::QueryFailed, 3) == CreationStage::PendingPersistence);
    LIVING_CHECK(unknown.OnCharacterVerify(RowVerifyOutcome::QueryFailed, 3) == CreationStage::PendingPersistence);
    LIVING_CHECK(unknown.OnCharacterVerify(RowVerifyOutcome::QueryFailed, 3) == CreationStage::Quarantined);

    // A row with the wrong identity is never adopted.
    CreationLifecycle mismatch;
    LIVING_CHECK(mismatch.OnCharacterVerify(RowVerifyOutcome::IdentityMismatch, 5) == CreationStage::Quarantined);
}

LIVING_TEST(creation_lifecycle_created_only_after_verified_row_and_metadata)
{
    CreationLifecycle lifecycle;

    // Verified keeps the stage: metadata has not run yet, so nothing may be
    // exposed or counted.
    LIVING_CHECK(lifecycle.OnCharacterVerify(RowVerifyOutcome::Verified, 5) == CreationStage::PendingPersistence);

    // Every required metadata write execution-confirmed -> Created.
    LIVING_CHECK(lifecycle.OnMetadataResult(true) == CreationStage::Created);

    // Terminal stages ignore later events (no resurrection of a record).
    LIVING_CHECK(lifecycle.OnCharacterVerify(RowVerifyOutcome::Absent, 5) == CreationStage::Created);
}

LIVING_TEST(creation_lifecycle_metadata_failure_requires_confirmed_cleanup)
{
    CreationLifecycle lifecycle;
    lifecycle.OnCharacterVerify(RowVerifyOutcome::Verified, 5);

    // A metadata write failed after character durability: cleanup begins;
    // nothing is retryable yet.
    LIVING_CHECK(lifecycle.OnMetadataResult(false) == CreationStage::PendingCleanup);

    // Character absence is established before its durable event owner clears.
    LIVING_CHECK(lifecycle.OnEventCleanupResult(true) == CreationStage::PendingCleanup);
    LIVING_CHECK(lifecycle.OnCleanupVerify(RowVerifyOutcome::Absent, 3) == CreationStage::FailedRetryable);
}

LIVING_TEST(creation_lifecycle_uncertain_cleanup_stays_quarantined)
{
    // Event-state cleanup failure after absence: uncertain cleanup, quarantined.
    CreationLifecycle eventsFail;
    eventsFail.OnCharacterVerify(RowVerifyOutcome::Verified, 5);
    eventsFail.OnMetadataResult(false);
    LIVING_CHECK(eventsFail.OnEventCleanupResult(false) == CreationStage::Quarantined);

    // Character deletion that never confirms: bounded attempts, then
    // quarantine - never a retryable claim while rows may exist.
    CreationLifecycle deleteFails;
    deleteFails.OnCharacterVerify(RowVerifyOutcome::Verified, 5);
    deleteFails.OnMetadataResult(false);
    deleteFails.OnEventCleanupResult(true);
    LIVING_CHECK(deleteFails.OnCleanupVerify(RowVerifyOutcome::Verified, 3) == CreationStage::PendingCleanup);
    LIVING_CHECK(deleteFails.OnCleanupVerify(RowVerifyOutcome::QueryFailed, 3) == CreationStage::PendingCleanup);
    LIVING_CHECK(deleteFails.OnCleanupVerify(RowVerifyOutcome::Verified, 3) == CreationStage::Quarantined);

    // Quarantined is permanent: no later callback revives the record, so its
    // GUID/name capacity is never reused.
    LIVING_CHECK(deleteFails.OnCleanupVerify(RowVerifyOutcome::Absent, 3) == CreationStage::Quarantined);
    LIVING_CHECK(deleteFails.OnMetadataResult(true) == CreationStage::Quarantined);
}

LIVING_TEST(one_shot_marker_applies_effect_once_and_retries_only_the_clear)
{
    // A one-shot post-create marker (create gear/levelup/test) must apply its
    // runtime effect EXACTLY ONCE per process; if the durable clear fails or
    // is ambiguous, only the CLEAR is retried - the mutation is never
    // replayed within the process. (A crash between effect and clear replays
    // once on the next pass; every marker effect is a harmless-to-repeat bot
    // randomization, which is the documented crash-window trade.)
    OneShotMarker marker;
    int effects = 0;
    int clears = 0;

    auto pump = [&](bool present, EventWriteResult clearResult) -> bool
    {
        MarkerConsumeStep const step = marker.Plan(present);
        if (step == MarkerConsumeStep::ApplyThenClear)
        {
            ++effects;
            marker.OnEffectApplied();
        }
        if (step == MarkerConsumeStep::Idle)
            return false;
        ++clears;
        return marker.OnClearResult(clearResult);
    };

    // Present: apply the effect once, then the clear fails.
    LIVING_CHECK(!pump(true, EventWriteResult::DefinitelyNotApplied));
    LIVING_CHECK(effects == 1 && clears == 1);

    // Still present (clear did not land): retry the CLEAR only - no replay.
    LIVING_CHECK(!pump(true, EventWriteResult::StateUnknown));
    LIVING_CHECK(effects == 1 && clears == 2); // effect count unchanged: the replay guard

    // Confirmed clear consumes the marker.
    LIVING_CHECK(pump(true, EventWriteResult::DesiredStateConfirmed));
    LIVING_CHECK(effects == 1 && clears == 3);

    // Gone: nothing to do.
    LIVING_CHECK(!pump(false, EventWriteResult::DesiredStateConfirmed));
    LIVING_CHECK(effects == 1 && clears == 3);

    // A fresh marker for the same slot (e.g. a re-add wrote it again) applies once more.
    OneShotMarker readded;
    LIVING_CHECK(readded.Plan(true) == MarkerConsumeStep::ApplyThenClear);
}

LIVING_TEST(one_shot_consume_protocol_never_replays_while_the_clear_is_stuck)
{
    // The PROTOCOL - including the ledger lifecycle - is what production runs
    // (living::ConsumeOneShotMarker), so this exercises the exact boundary
    // that matters: a clear that never confirms must retry the CLEAR forever,
    // never the effect. Erasing the ledger on any kind of give-up would
    // default-construct a fresh marker against the still-present durable row
    // and replay the destructive effect - the regression this test pins.
    OneShotLedgers ledgers;
    int effects = 0;
    int clears = 0;
    int warns = 0;
    bool durablePresent = true;
    bool clearSucceeds = false;

    auto pass = [&]() -> bool
    {
        return ConsumeOneShotMarker(ledgers, 42u, "create gear", /*known*/ true, durablePresent,
            [&]() { ++effects; },
            [&]() { ++clears; if (clearSucceeds) durablePresent = false; return clearSucceeds; },
            [&](uint32_t) { ++warns; });
    };

    // Far past the warn threshold: effect once, clears retried, warned once.
    for (int i = 0; i < 50; ++i)
        LIVING_CHECK(!pass());
    LIVING_CHECK(effects == 1); // NEVER replayed, no matter how long the clear is stuck
    LIVING_CHECK(clears == 50);
    LIVING_CHECK(warns == 1);   // disclosed exactly once

    // The database recovers: the very next pass settles, self-healing.
    clearSucceeds = true;
    LIVING_CHECK(pass());
    LIVING_CHECK(effects == 1);
    LIVING_CHECK(ledgers.empty()); // no retained per-bot maps

    // Settled durable state: idle passes track nothing and insert nothing.
    LIVING_CHECK(pass());
    LIVING_CHECK(ledgers.empty());
}

LIVING_TEST(one_shot_consume_protocol_boundaries)
{
    OneShotLedgers ledgers;
    int effects = 0;

    // Unknown durable state: mutate nothing, keep ownership, track nothing.
    LIVING_CHECK(!ConsumeOneShotMarker(ledgers, 7u, "test", /*known*/ false, true,
        [&]() { ++effects; }, [&]() { return true; }, [](uint32_t) {}));
    LIVING_CHECK(effects == 0);
    LIVING_CHECK(ledgers.empty());

    // Present and healthy: apply once, clear confirmed, fully settled.
    LIVING_CHECK(ConsumeOneShotMarker(ledgers, 7u, "test", true, true,
        [&]() { ++effects; }, [&]() { return true; }, [](uint32_t) {}));
    LIVING_CHECK(effects == 1);
    LIVING_CHECK(ledgers.empty());

    // A marker written AGAIN later (fresh durable row) applies once more -
    // the consumed ledger never suppresses a genuinely new obligation.
    LIVING_CHECK(ConsumeOneShotMarker(ledgers, 7u, "test", true, true,
        [&]() { ++effects; }, [&]() { return true; }, [](uint32_t) {}));
    LIVING_CHECK(effects == 2);

    // A transient absent read while a clear was pending drops the tracked
    // state (the row is authoritatively gone) without running anything.
    ConsumeOneShotMarker(ledgers, 8u, "create levelup", true, true,
        [&]() { ++effects; }, [&]() { return false; }, [](uint32_t) {});
    LIVING_CHECK(!ledgers.empty());
    LIVING_CHECK(ConsumeOneShotMarker(ledgers, 8u, "create levelup", true, /*present*/ false,
        [&]() { ++effects; }, [&]() { return true; }, [](uint32_t) {}));
    LIVING_CHECK(ledgers.empty());
}
