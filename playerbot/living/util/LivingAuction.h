#pragma once

#include <cstdint>

namespace living
{
    // Checked 95% opening bid for an auction posting. The multiplication is done
    // in uint64 (a valid buyout near the core money cap wrapped `price * 95` in
    // uint32, turning a ~214,748g buyout into a ~2,147g opening bid), the floor
    // division is preserved, and the result is validated against `maxCopper`
    // before narrowing. Returns false when the price itself is out of range.
    bool TryComputeOpeningBid(uint32_t buyoutPrice, uint64_t maxCopper, uint32_t& outOpeningBid);

    // The exact minimum acceptable bid for one auction, mirroring the pinned
    // cores' HandleAuctionPlaceBid rules:
    //   - price must exceed the current bid;
    //   - below buyout, price must be at least currentBid + outBidIncrement
    //     (the caller passes the core's own AuctionEntry::GetAuctionOutBid());
    //   - price must be at least the start bid (and at least 1: the core drops
    //     zero-price bid packets outright);
    //   - a no-buyout auction (buyout == 0) uses this bid cost - it must never
    //     become min(0, ...) = 0;
    //   - a minimum bid that reaches a nonzero buyout is the buyout.
    // All arithmetic is checked in uint64; returns false when no legal bid
    // exists within `maxCopper`.
    bool TryComputeAuctionBidCost(uint32_t startBid, uint32_t currentBid, uint32_t outBidIncrement,
        uint32_t buyout, uint64_t maxCopper, uint32_t& outCost);
}
