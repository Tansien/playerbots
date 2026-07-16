#include "LivingAuction.h"

#include <algorithm>

namespace living
{
    bool TryComputeOpeningBid(uint32_t buyoutPrice, uint64_t maxCopper, uint32_t& outOpeningBid)
    {
        if (buyoutPrice > maxCopper)
            return false;

        uint64_t const openingBid = static_cast<uint64_t>(buyoutPrice) * 95 / 100;
        if (openingBid > maxCopper)
            return false;

        outOpeningBid = static_cast<uint32_t>(openingBid);
        return true;
    }

    bool TryComputeAuctionBidCost(uint32_t startBid, uint32_t currentBid, uint32_t outBidIncrement,
        uint32_t buyout, uint64_t maxCopper, uint32_t& outCost)
    {
        // With no bids yet the core requires price >= startBid and price > 0.
        // With a standing bid it additionally requires price >= bid + outbid.
        uint64_t minimum = std::max<uint64_t>(startBid, 1);
        if (currentBid > 0)
            minimum = std::max<uint64_t>(minimum, static_cast<uint64_t>(currentBid) + outBidIncrement);

        // Reaching (or passing) a nonzero buyout means buying out.
        if (buyout > 0 && minimum >= buyout)
            minimum = buyout;

        if (minimum == 0 || minimum > maxCopper)
            return false;

        outCost = static_cast<uint32_t>(minimum);
        return true;
    }
}
