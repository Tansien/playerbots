#pragma once

#include <array>
#include <cstdint>

namespace living
{
    // Immutable POD snapshot types for cross-thread proposals (0001 invariant 8,
    // 0003 section 8, 0005A A.1). Safety invariants: no live Player/Group/Map/
    // Quest/Item/TravelTarget pointers, no database-result objects, and no methods
    // that query mutable global state. Workers receive copies and return proposals;
    // the world thread revalidates against a fresh snapshot before acting.

    // A character identity is the low guid PLUS the identity nonce: a reused low
    // guid with a different nonce is a different identity (0003 section 2).
    struct LivingIdentity
    {
        uint32_t characterGuid = 0;
        std::array<uint8_t, 16> identityNonce{};

        bool operator==(LivingIdentity const&) const = default;
    };

    enum class LivingMapKind : uint8_t
    {
        OpenWorld,
        Dungeon,
        Battleground,
        Arena
    };

    char const* ToString(LivingMapKind value);

    struct LivingLocationSummary
    {
        uint32_t mapId = 0;
        uint32_t zoneId = 0;
        uint32_t instanceId = 0;
        LivingMapKind mapKind = LivingMapKind::OpenWorld;

        bool operator==(LivingLocationSummary const&) const = default;
    };

    struct LivingGroupSummary
    {
        bool inGroup = false;
        uint8_t memberCount = 0;
        bool protectedRealPlayerCommitment = false;
        uint32_t commitmentOwnerGuid = 0; // real player's low guid; 0 when unprotected

        bool operator==(LivingGroupSummary const&) const = default;
    };

    struct LivingCharacterSnapshot
    {
        LivingIdentity identity;
        uint64_t stateVersion = 0;
        uint64_t scheduleGeneration = 0;
        uint64_t goalGeneration = 0;
        uint64_t snapshotGeneration = 0;
        bool desiredOnline = false;
        bool actualObservedOnline = false;
        LivingGroupSummary group;
        LivingLocationSummary location;
        uint64_t proposalExpiresAtMs = 0; // UTC epoch ms; proposals from this snapshot expire here

        bool operator==(LivingCharacterSnapshot const&) const = default;
    };

    inline bool SameIdentity(LivingIdentity const& a, LivingIdentity const& b)
    {
        return a == b;
    }

    // Mandatory full validator: a proposal computed from `basis` may act on
    // `current` only when EVERY field still matches - identity (guid AND nonce),
    // versions, generations, desired/observed online state, group and commitment
    // summary, and location - and the proposal has not expired (0005A A.1 requires
    // actual online/group/map state to be revalidated, not just versions). Anything
    // else is stale and must be discarded without side effects.
    inline bool IsSnapshotStale(LivingCharacterSnapshot const& basis,
        LivingCharacterSnapshot const& current, uint64_t nowUtcMs)
    {
        return !(basis == current) || nowUtcMs >= basis.proposalExpiresAtMs;
    }
}
