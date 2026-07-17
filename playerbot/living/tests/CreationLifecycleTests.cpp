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

    // Event cleanup confirmed, deletion verified absent -> retryable.
    LIVING_CHECK(lifecycle.OnEventCleanupResult(true) == CreationStage::PendingCleanup);
    LIVING_CHECK(lifecycle.OnCleanupVerify(RowVerifyOutcome::Absent, 3) == CreationStage::FailedRetryable);
}

LIVING_TEST(creation_lifecycle_uncertain_cleanup_stays_quarantined)
{
    // Event-state cleanup failure: uncertain cleanup, quarantined.
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
