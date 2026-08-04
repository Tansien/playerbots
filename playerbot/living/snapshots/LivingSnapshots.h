#pragma once

// Immutable snapshot and proposal foundation (0001 invariant 8): workers
// receive trivially copyable value snapshots -- never pointers to live game
// objects -- and return generation-checked proposals that apply only if the
// world state they were computed from is still current. Enforcement below is
// two-layered and deliberately honest about its limits: is_trivially_copyable
// keeps out owning and non-trivial members but NOT raw pointers or references
// (both are trivially copyable), so each type's size is also pinned -- any new
// member, including a smuggled Player* or Map&, must consciously edit the size
// static_assert, where review catches it.

#include <array>
#include <cstdint>
#include <type_traits>

namespace living
{
    // Generation 0 is reserved as "never populated", mirroring the all-zero
    // identity nonce: live generations start at 1, so a zero-filled or
    // default-constructed proposal can never pass the staleness gate, not even
    // against a writer that has not bumped its counter yet.
    using SnapshotGeneration = uint64_t;

    // The (guid, nonce) identity pair every Living Realm record binds to
    // (0001 section 4). The all-zero nonce means "absent identity".
    struct LivingIdentitySnapshot
    {
        uint32_t characterGuid = 0;
        std::array<uint8_t, 16> identityNonce{};

        friend bool operator==(LivingIdentitySnapshot const&, LivingIdentitySnapshot const&) = default;
    };

    struct LivingSnapshotHeader
    {
        SnapshotGeneration generation = 0;
        uint64_t capturedAtUtcMs = 0;

        friend bool operator==(LivingSnapshotHeader const&, LivingSnapshotHeader const&) = default;
    };

    struct LivingProposalHeader
    {
        SnapshotGeneration basedOnGeneration = 0;

        friend bool operator==(LivingProposalHeader const&, LivingProposalHeader const&) = default;
    };

    // A proposal applies only against the exact live generation it was
    // computed from; any other current generation makes it stale, including a
    // current generation that moved backwards (a restart). The reserved
    // generation 0 never matches anything: an unpopulated proposal is not
    // current even against an unstarted (still-zero) writer counter.
    constexpr bool ProposalIsCurrent(LivingProposalHeader proposal, SnapshotGeneration currentGeneration)
    {
        return proposal.basedOnGeneration != 0
            && proposal.basedOnGeneration == currentGeneration;
    }

    static_assert(std::is_trivially_copyable_v<LivingIdentitySnapshot>,
        "snapshots cross thread boundaries by value; keep them trivially copyable");
    static_assert(std::is_trivially_copyable_v<LivingSnapshotHeader>,
        "snapshots cross thread boundaries by value; keep them trivially copyable");
    static_assert(std::is_trivially_copyable_v<LivingProposalHeader>,
        "proposals cross thread boundaries by value; keep them trivially copyable");

    // Size pins: every member of these types is fixed-width with natural
    // alignment, so the sizes are identical across the supported compilers.
    // Growing a type (even by a trivially copyable pointer or reference)
    // fails here; update the pin only together with review of the new member.
    static_assert(sizeof(LivingIdentitySnapshot) == 20,
        "new LivingIdentitySnapshot member: verify it is pure value state, then update this pin");
    static_assert(sizeof(LivingSnapshotHeader) == 16,
        "new LivingSnapshotHeader member: verify it is pure value state, then update this pin");
    static_assert(sizeof(LivingProposalHeader) == 8,
        "new LivingProposalHeader member: verify it is pure value state, then update this pin");
}
