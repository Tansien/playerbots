#include "LivingTest.h"

#include "../util/LivingAuction.h"

using namespace living;

namespace
{
    constexpr uint64_t kCoreMoneyCap = 0x7FFFFFFFull - 1; // MAX_MONEY_AMOUNT

    // The cores' AuctionEntry::GetAuctionOutBid: 5% of the current bid, minimum 1.
    uint32_t CoreOutBid(uint32_t bid)
    {
        uint32_t outbid = (bid / 100) * 5;
        return outbid ? outbid : 1;
    }
}

LIVING_TEST(auction_opening_bid_is_exact_at_the_money_cap)
{
    uint32_t openingBid = 0;

    // The exact maximum accepted price: 95% must arrive intact in the packet.
    // uint32 `price * 95` wrapped this to 21,474,834 instead of 2,040,109,463.
    LIVING_CHECK(TryComputeOpeningBid(2147483646u, kCoreMoneyCap, openingBid));
    LIVING_CHECK(openingBid == 2040109463u);

    // Cap-adjacent and small values keep the floor semantics.
    LIVING_CHECK(TryComputeOpeningBid(2147483645u, kCoreMoneyCap, openingBid) && openingBid == 2040109462u);
    LIVING_CHECK(TryComputeOpeningBid(100, kCoreMoneyCap, openingBid) && openingBid == 95);
    LIVING_CHECK(TryComputeOpeningBid(99, kCoreMoneyCap, openingBid) && openingBid == 94);  // floor(94.05)
    LIVING_CHECK(TryComputeOpeningBid(2, kCoreMoneyCap, openingBid) && openingBid == 1);    // smallest legal posting

    // Prices whose 95% floor is zero cannot be posted: every pinned core's
    // HandleAuctionSellItem rejects a zero start bid.
    LIVING_CHECK(!TryComputeOpeningBid(1, kCoreMoneyCap, openingBid));
    LIVING_CHECK(!TryComputeOpeningBid(0, kCoreMoneyCap, openingBid));

    // A price above the cap is rejected outright, never wrapped.
    LIVING_CHECK(!TryComputeOpeningBid(2147483647u, kCoreMoneyCap, openingBid));
    LIVING_CHECK(!TryComputeOpeningBid(4294967295u, kCoreMoneyCap, openingBid));
}

LIVING_TEST(auction_sell_price_products_are_checked)
{
    uint32_t perItem = 0;
    uint32_t total = 0;

    // Normal pricing keeps the floor and the 1-copper minimum.
    LIVING_CHECK(TryComputePerItemSellPrice(100, 75, kCoreMoneyCap, perItem) && perItem == 75);
    LIVING_CHECK(TryComputePerItemSellPrice(1, 75, kCoreMoneyCap, perItem) && perItem == 1); // floor(0.75) -> min 1
    LIVING_CHECK(TryComputePerItemSellPrice(0, 100, kCoreMoneyCap, perItem) && perItem == 1);

    // base x percentage wrapped in uint32 for cap-adjacent bases: 2147483646*75
    // is far beyond uint32. The checked product stays exact.
    LIVING_CHECK(TryComputePerItemSellPrice(2147483646u, 75, kCoreMoneyCap, perItem) && perItem == 1610612734u);
    LIVING_CHECK(TryComputePerItemSellPrice(2147483646u, 100, kCoreMoneyCap, perItem) && perItem == 2147483646u);
    // 101% of the cap exceeds it: rejected, not wrapped.
    LIVING_CHECK(!TryComputePerItemSellPrice(2147483646u, 101, kCoreMoneyCap, perItem));

    // perItem x stack count: exact maximum passes, one more copper fails, and
    // large stacks cannot wrap into a tiny accepted total.
    LIVING_CHECK(TryComputeSellTotal(1073741823u, 2, kCoreMoneyCap, total) && total == 2147483646u);
    LIVING_CHECK(!TryComputeSellTotal(1073741824u, 2, kCoreMoneyCap, total)); // cap + 2
    LIVING_CHECK(TryComputeSellTotal(100, 20, kCoreMoneyCap, total) && total == 2000);
    LIVING_CHECK(!TryComputeSellTotal(214748365u, 1000, kCoreMoneyCap, total)); // wraps in uint32
    LIVING_CHECK(!TryComputeSellTotal(100, 0, kCoreMoneyCap, total));
}

LIVING_TEST(auction_bid_cost_mirrors_core_minimums)
{
    uint32_t cost = 0;

    // No bids yet: the start bid is the minimum (and the core requires > 0).
    LIVING_CHECK(TryComputeAuctionBidCost(500, 0, CoreOutBid(0), 0, kCoreMoneyCap, cost));
    LIVING_CHECK(cost == 500);

    // No bids and no start bid: 1 copper is the smallest bid the core accepts.
    LIVING_CHECK(TryComputeAuctionBidCost(0, 0, CoreOutBid(0), 0, kCoreMoneyCap, cost));
    LIVING_CHECK(cost == 1);

    // A standing bid requires bid + the core outbid increment (5%, min 1).
    LIVING_CHECK(TryComputeAuctionBidCost(500, 1000, CoreOutBid(1000), 0, kCoreMoneyCap, cost));
    LIVING_CHECK(cost == 1050);
    LIVING_CHECK(TryComputeAuctionBidCost(5, 10, CoreOutBid(10), 0, kCoreMoneyCap, cost));
    LIVING_CHECK(cost == 11); // 5% of 10 floors to 0 -> minimum increment 1

    // The start bid still applies as a floor alongside the increment rule.
    LIVING_CHECK(TryComputeAuctionBidCost(5000, 4900, CoreOutBid(4900), 0, kCoreMoneyCap, cost));
    LIVING_CHECK(cost == 5145); // 4900 + 245 > startbid 5000
}

LIVING_TEST(auction_bid_cost_handles_buyout_and_no_buyout)
{
    uint32_t cost = 0;

    // The review's crash case: a normal no-buyout auction. The old code took
    // min(buyout=0, ...) = 0 as the cost and divided by it. The valid bid cost
    // is used instead.
    LIVING_CHECK(TryComputeAuctionBidCost(200, 0, CoreOutBid(0), 0, kCoreMoneyCap, cost));
    LIVING_CHECK(cost == 200);

    // A minimum bid that reaches the buyout IS the buyout.
    LIVING_CHECK(TryComputeAuctionBidCost(90, 100, CoreOutBid(100), 104, kCoreMoneyCap, cost));
    LIVING_CHECK(cost == 104); // 100 + max(5,1)=105 >= buyout 104 -> buyout

    // Below the buyout the increment rules stand.
    LIVING_CHECK(TryComputeAuctionBidCost(90, 100, CoreOutBid(100), 500, kCoreMoneyCap, cost));
    LIVING_CHECK(cost == 105);

    // Increment arithmetic near the cap cannot wrap: it fails instead.
    LIVING_CHECK(!TryComputeAuctionBidCost(0, 2147483646u, CoreOutBid(2147483646u), 0, kCoreMoneyCap, cost));

    // A start bid above the cap has no legal bid.
    LIVING_CHECK(!TryComputeAuctionBidCost(2147483647u, 0, 1, 0, kCoreMoneyCap, cost));
}
