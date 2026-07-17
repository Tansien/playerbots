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

    // Checked automatic-sell pricing. Both products that used to run in uint32
    // (base price x percentage, per-item price x stack count) can wrap for
    // cap-adjacent bases or large stacks, letting an overflowed tiny price reach
    // the posting path as if it were valid.
    //
    // Per-item: floor(basePerItem * percentage / 100), minimum 1 copper
    // (preserving the legacy floor), computed in uint64 and validated against
    // maxCopper before narrowing.
    bool TryComputePerItemSellPrice(uint32_t basePerItem, uint32_t percentage, uint64_t maxCopper, uint32_t& outPerItem);

    // Stack total: perItem * stackCount in uint64, validated against maxCopper
    // before narrowing. Rejects a zero stack.
    bool TryComputeSellTotal(uint32_t perItem, uint32_t stackCount, uint64_t maxCopper, uint32_t& outTotal);

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
