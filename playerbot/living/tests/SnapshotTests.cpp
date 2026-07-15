#include "LivingTest.h"

#include "../snapshots/LivingSnapshots.h"

#include <type_traits>

using namespace living;

// The snapshot contract forbids live pointers, database results, and hidden
// state; trivially-copyable standard-layout types make that structural.
static_assert(std::is_trivially_copyable_v<LivingCharacterSnapshot>);
static_assert(std::is_standard_layout_v<LivingCharacterSnapshot>);
static_assert(std::is_trivially_copyable_v<LivingIdentity>);

namespace
{
    LivingCharacterSnapshot BaseSnapshot()
    {
        LivingCharacterSnapshot snapshot;
        snapshot.identity.characterGuid = 1234;
        snapshot.identity.identityNonce = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
        snapshot.stateVersion = 7;
        snapshot.scheduleGeneration = 3;
        snapshot.goalGeneration = 5;
        snapshot.snapshotGeneration = 11;
        snapshot.desiredOnline = true;
        snapshot.actualObservedOnline = false;
        snapshot.group.inGroup = true;
        snapshot.group.memberCount = 4;
        snapshot.group.protectedRealPlayerCommitment = true;
        snapshot.group.commitmentOwnerGuid = 42;
    snapshot.group.commitmentGeneration = 6;
    snapshot.queueState = LivingQueueState::Offline;
        snapshot.location.mapId = 0;
        snapshot.location.zoneId = 12;
        snapshot.location.instanceId = 0;
        snapshot.location.mapKind = LivingMapKind::OpenWorld;
        snapshot.proposalExpiresAtMs = 10000;
        return snapshot;
    }
}

LIVING_TEST(snapshot_equality_covers_every_field)
{
    LivingCharacterSnapshot const a = BaseSnapshot();
    LivingCharacterSnapshot b = BaseSnapshot();
    LIVING_CHECK(a == b);

    b.group.memberCount = 5;
    LIVING_CHECK(!(a == b));

    b = BaseSnapshot();
    b.location.mapKind = LivingMapKind::Battleground;
    LIVING_CHECK(!(a == b));

    b = BaseSnapshot();
    b.desiredOnline = false;
    LIVING_CHECK(!(a == b));
}

LIVING_TEST(snapshot_same_guid_different_nonce_is_a_different_identity)
{
    LivingIdentity a;
    a.characterGuid = 1234;
    a.identityNonce = { 1, 2, 3 };

    LivingIdentity b = a;
    LIVING_CHECK(SameIdentity(a, b));

    b.identityNonce[15] = 0xFF;
    LIVING_CHECK(!SameIdentity(a, b)); // reused low guid, new nonce: new identity

    LivingIdentity c = a;
    c.characterGuid = 9999;
    LIVING_CHECK(!SameIdentity(a, c));
}

LIVING_TEST(snapshot_staleness_is_detected_per_dimension)
{
    LivingCharacterSnapshot const basis = BaseSnapshot();
    uint64_t const now = 500; // well before expiry at 10000

    LIVING_CHECK(!IsSnapshotStale(basis, BaseSnapshot(), now));

    LivingCharacterSnapshot changed = BaseSnapshot();
    changed.identity.identityNonce[0] = 0xAA;
    LIVING_CHECK(IsSnapshotStale(basis, changed, now)); // identity nonce

    changed = BaseSnapshot();
    changed.identity.characterGuid = 4321;
    LIVING_CHECK(IsSnapshotStale(basis, changed, now)); // guid

    changed = BaseSnapshot();
    changed.stateVersion += 1;
    LIVING_CHECK(IsSnapshotStale(basis, changed, now)); // state version

    changed = BaseSnapshot();
    changed.scheduleGeneration += 1;
    LIVING_CHECK(IsSnapshotStale(basis, changed, now)); // schedule generation

    changed = BaseSnapshot();
    changed.goalGeneration += 1;
    LIVING_CHECK(IsSnapshotStale(basis, changed, now)); // goal generation

    changed = BaseSnapshot();
    changed.snapshotGeneration += 1;
    LIVING_CHECK(IsSnapshotStale(basis, changed, now)); // snapshot generation

    // Expiry boundary: valid strictly before, stale at and after.
    LIVING_CHECK(!IsSnapshotStale(basis, BaseSnapshot(), 9999));
    LIVING_CHECK(IsSnapshotStale(basis, BaseSnapshot(), 10000));
    LIVING_CHECK(IsSnapshotStale(basis, BaseSnapshot(), 20000));

    // Observed state is part of the mandatory revalidation (0005A A.1): actual
    // online, group, and location changes invalidate a proposal too.
    changed = BaseSnapshot();
    changed.actualObservedOnline = true;
    LIVING_CHECK(IsSnapshotStale(basis, changed, now));

    changed = BaseSnapshot();
    changed.group.memberCount += 1;
    LIVING_CHECK(IsSnapshotStale(basis, changed, now));

    changed = BaseSnapshot();
    changed.location.zoneId = 999;
    LIVING_CHECK(IsSnapshotStale(basis, changed, now));

    // Login-queue attempt state and commitment generation are revalidated too
    // (0005A A.1): a proposal must not act on a stale queue or commitment view.
    changed = BaseSnapshot();
    changed.queueState = LivingQueueState::OnLoginQueue;
    LIVING_CHECK(IsSnapshotStale(basis, changed, now));

    changed = BaseSnapshot();
    changed.group.commitmentGeneration += 1;
    LIVING_CHECK(IsSnapshotStale(basis, changed, now));

    // A freshly captured snapshot carries its own new proposal expiry; that is
    // bookkeeping, not character state, and must not make a live proposal stale.
    changed = BaseSnapshot();
    changed.proposalExpiresAtMs = 99999;
    LIVING_CHECK(!IsSnapshotStale(basis, changed, now));
    LIVING_CHECK(IsSnapshotStale(basis, changed, 10000)); // basis' own expiry still applies
}
