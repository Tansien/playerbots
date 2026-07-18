#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <vector>

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

    // Admission rule for bidding on/buying out one auction. The pinned cores
    // reject same-account bids only when the auction owner is OFFLINE (their
    // normal invariant: one account cannot have two characters online), and
    // playerbots deliberately pack 9/10 characters per account and log several
    // in - an online sibling would bypass the core protection and spend the
    // account's budget on its own auction.
    //
    // The bidder's account membership is resolved ONCE per AH action into a
    // sibling GUID set (resolving the owner's account per listing performed a
    // synchronous CharacterDatabase round trip for every offline owner while
    // the AH mutex and world thread were held). Returns true when the bid must
    // be REJECTED:
    //   - the owner is the bidder itself;
    //   - the owner is one of the bidder account's characters (online or
    //     offline - the set covers both);
    //   - the sibling set could not be loaded (fail closed) -
    // except that ownerless listings (ownerGuidLow == 0, the auction-house-bot
    // convention) are always admissible.
    inline bool RejectAuctionBidder(uint32_t ownerGuidLow, uint32_t bidderGuidLow,
        bool siblingsKnown, bool ownerIsAccountSibling)
    {
        if (ownerGuidLow == 0)
            return false;

        if (ownerGuidLow == bidderGuidLow)
            return true;

        if (!siblingsKnown)
            return true;

        return ownerIsAccountSibling;
    }

    // The bidder account's character GUIDs, loaded once at the start of an AH
    // bid action and reused for every scan, revalidation and the BidItem
    // packet boundary - admission is a pure set check, never a per-auction
    // account query. `loader` performs the ONE lookup and returns nullopt on
    // failure, in which case the identity fails closed (every owned listing is
    // rejected) and the caller fails the whole action.
    struct AuctionBidderSiblings
    {
        uint32_t bidderGuidLow = 0;
        bool loaded = false;
        std::set<uint32_t> siblingGuidLows;

        template <typename LoaderFn>
        static AuctionBidderSiblings Load(uint32_t bidderGuidLow, LoaderFn&& loader)
        {
            AuctionBidderSiblings identity;
            identity.bidderGuidLow = bidderGuidLow;
            if (std::optional<std::vector<uint32_t>> guids = loader())
            {
                identity.loaded = true;
                identity.siblingGuidLows.insert(guids->begin(), guids->end());
            }

            return identity;
        }

        bool Rejects(uint32_t ownerGuidLow) const
        {
            return RejectAuctionBidder(ownerGuidLow, bidderGuidLow, loaded,
                siblingGuidLows.count(ownerGuidLow) > 0);
        }
    };

    // Active standing-bid budget for the automatic bid pass. A bot may hold at
    // most `cap` simultaneous standing bids: `>= cap` existing bids is AT
    // capacity (the old `> 10` checks permitted an eleventh), the cap is
    // enforced immediately before each NON-buyout bid, and only a successful
    // standing bid consumes a slot - a buyout removes the auction outright and
    // never occupies one.
    class StandingBidCap
    {
    public:
        StandingBidCap(uint32_t existingStandingBids, uint32_t capLimit)
            : active(existingStandingBids), cap(capLimit) {}

        bool CanPlaceStandingBid() const { return active < cap; }
        void RecordStandingBid() { ++active; }
        uint32_t Active() const { return active; }

    private:
        uint32_t active;
        uint32_t cap;
    };
}
