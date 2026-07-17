
#include "playerbot/playerbot.h"
#include "AhAction.h"
#include "playerbot/living/util/LivingAuction.h"

#include <algorithm>
#include <random>
#include "playerbot/strategy/values/ItemCountValue.h"
#include "playerbot/RandomItemMgr.h"
#include "playerbot/strategy/values/BudgetValues.h"
#include "playerbot/strategy/values/ItemUsageValue.h"

using namespace ai;

bool AhAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    std::string text = event.getParam();

    std::list<ObjectGuid> npcs = AI_VALUE(std::list<ObjectGuid>, "nearest npcs");
    for (std::list<ObjectGuid>::iterator i = npcs.begin(); i != npcs.end(); i++)
    {
        Unit* npc = bot->GetNPCIfCanInteractWith(*i, UNIT_NPC_FLAG_AUCTIONEER);
        if (!npc)
            continue;

        if (!sRandomPlayerbotMgr.m_ahActionMutex.try_lock()) //Another bot is using the Auction right now. Try again later.
            return false;

        bool doneAuction = ExecuteCommand(requester, text, npc);

        sRandomPlayerbotMgr.m_ahActionMutex.unlock();

        return doneAuction;
    }

    ai->TellPlayerNoFacing(requester, "Cannot find auctioneer nearby");
    return false;
}

bool AhAction::ExecuteCommand(Player* requester, std::string text, Unit* auctioneer)
{
    uint32 time;
#ifdef MANGOSBOT_ZERO
    time = 8 * HOUR / MINUTE;
#else
    time = 12 * HOUR / MINUTE;
#endif

    if (text == "vendor")
    {
        AuctionHouseEntry const* auctionHouseEntry = bot->GetSession()->GetCheckedAuctionHouseForAuctioneer(auctioneer->GetObjectGuid());
        if (!auctionHouseEntry)
            return false;

        std::list<Item*> items = AI_VALUE2(std::list<Item*>, "inventory items", "usage " + std::to_string((uint8)ItemUsage::ITEM_USAGE_AH));

        bool postedItem = false;

        std::map<uint32, uint32> pricePerItemCache;

        //resulting undercut value for reporting
        uint32 resultingUndercut = 0;
        uint32 postedItems = 0;

        for (auto item : items)
        {
            RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(item).GetQualifier());
            if(AI_VALUE2(ItemUsage, "item usage", ItemQualifier(item).GetQualifier()) != ItemUsage::ITEM_USAGE_AH)
                continue;

            auto pmo = sPerformanceMonitor.start(PERF_MON_VALUE, "IsMoreProfitableToSellToAHThanToVendor", ai);
            bool isMoreProfitableToSellToAHThanToVendor = ItemUsageValue::IsMoreProfitableToSellToAHThanToVendor(item->GetProto(), bot);
            pmo.reset();

            if (!isMoreProfitableToSellToAHThanToVendor)
                continue;

            uint32 deposit = AuctionHouseMgr::GetAuctionDeposit(auctionHouseEntry, time * MINUTE, item);

            RESET_AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::ah);
            uint32 freeMoney = AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::ah);

            if (deposit > freeMoney)
                return false;

            const ItemPrototype* proto = item->GetProto();

            // Both products are computed checked in uint64 against the core
            // money cap: base x percentage and per-item x stack count each
            // wrapped in uint32 for cap-adjacent prices/large stacks, letting an
            // overflowed tiny price reach the posting path as if it were valid.
            if (!pricePerItemCache[proto->ItemId])
            {
                uint32 basePerItem = ItemUsageValue::GetBotSellPrice(proto, bot);
                uint32 initialPricePercentage = urand(75, 100);
                uint32 pricePerItem = 0;
                if (!living::TryComputePerItemSellPrice(basePerItem, initialPricePercentage, MAX_MONEY_AMOUNT, pricePerItem))
                    continue;
                pricePerItemCache[proto->ItemId] = pricePerItem;
            }

            uint32 stackTotal = 0;
            if (!living::TryComputeSellTotal(pricePerItemCache[proto->ItemId], item->GetCount(), MAX_MONEY_AMOUNT, stackTotal))
                continue;

            bool didPost = PostItem(requester, item, stackTotal, auctioneer, time);

            if (didPost)
            {
                    postedItem |= true;
                    postedItems++;
            }

            if (!urand(0, 5 + (items.size()- postedItems)/10))
                break;
        }

        return postedItem;
    }

    // Documented syntax: ah <itemlink> <money>. The buyout price is the
    // trailing money token (the old parse read the FIRST token as money, so
    // only the undocumented reversed form ever worked); everything before it is
    // the item to post.
    size_t const lastSpace = text.rfind(' ');
    if (lastSpace == std::string::npos)
    {
        ai->TellError(requester, "Usage: ah <itemlink> <price>");
        return false;
    }

    std::string const priceStr = text.substr(lastSpace + 1);
    std::string itemQuery = text.substr(0, lastSpace);
    while (!itemQuery.empty() && itemQuery.back() == ' ')
        itemQuery.pop_back();

    uint32 price = 0;
    if (ChatHelper::parseMoney(priceStr, price) != living::MoneyParseStatus::Ok || itemQuery.empty())
    {
        // A malformed or out-of-range price must not post the auction at a zero
        // or wrapped buyout.
        ai->TellError(requester, "Usage: ah <itemlink> <price> (a money amount like 5g)");
        return false;
    }

    std::list<Item*> found = ai->InventoryParseItems(itemQuery, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
    if (found.empty())
        return false;

    Item* item = *found.begin();

    return PostItem(requester, item, price, auctioneer, time);
}

bool AhAction::PostItem(Player* requester, Item* item, uint32 price, Unit* auctioneer, uint32 time)
{
    ObjectGuid itemGuid = item->GetObjectGuid();
    ItemPrototype const* proto = item->GetProto();

    ItemQualifier itemQualifier(item);

    uint32 cnt = item->GetCount();

    // Checked 95% opening bid: `price * 95` in uint32 wrapped for any valid
    // buyout above ~45,193g, turning a cap-adjacent buyout into a tiny opening
    // bid on a real posted auction.
    uint32 openingBid = 0;
    if (!living::TryComputeOpeningBid(price, MAX_MONEY_AMOUNT, openingBid))
    {
        ai->TellError(requester, "Invalid price: " + ChatHelper::formatMoney(price));
        return false;
    }

    WorldPacket packet;
    packet << auctioneer->GetObjectGuid();
#ifdef MANGOSBOT_TWO
    packet << (uint32)1;
#endif
    packet << itemGuid;
#ifdef MANGOSBOT_TWO
    packet << cnt;
#endif
    packet << openingBid; //bid price
    packet << price; //buyout price
    packet << time;

    bot->GetSession()->HandleAuctionSellItem(packet);

    if (bot->GetItemByGuid(itemGuid))
        return false;

    sPlayerbotAIConfig.logEvent(ai, "AhAction", proto->Name1, std::to_string(proto->ItemId));

    std::ostringstream out;
    out << "Posting " << ChatHelper::formatItem(itemQualifier, cnt) << " for " << ChatHelper::formatMoney(price) << " to the AH";
    ai->TellPlayerNoFacing(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
    return true;
}

bool AhBidAction::ExecuteCommand(Player* requester, std::string text, Unit* auctioneer)
{
    AuctionHouseEntry const* auctionHouseEntry = bot->GetSession()->GetCheckedAuctionHouseForAuctioneer(auctioneer->GetObjectGuid());
    if (!auctionHouseEntry)
        return false;

    // always return pointer
    AuctionHouseObject* auctionHouse = sAuctionMgr.GetAuctionsMap(auctionHouseEntry);

    if (!auctionHouse)
        return false;

    AuctionHouseObject::AuctionEntryMap const& map = auctionHouse->GetAuctions();

    if (map.empty())
        return false;

    AuctionEntry* auction = nullptr;

    // Candidates hold the auction ID, never the entry pointer: an earlier
    // buyout in the same batch removes (frees) the won auction, so a stored
    // pointer to a later candidate could dangle by the time it is revisited.
    struct BidCandidate
    {
        uint32 auctionId;
        uint32 cost;
        bool isBuyout;
        uint32 power;
    };

    if (text == "vendor")
    {
        ItemUsage usage;
        auto data = WorldPacket();
        uint32 count, totalcount = 0;
        auctionHouse->BuildListBidderItems(data, bot, 9999, count, totalcount);

        // Standing-bid budget: >= 10 existing bids is AT capacity (the old
        // `> 10` permitted an eleventh). The cap gates each NON-buyout bid
        // right before it is sent; buyouts never consume a slot, so a bot at
        // the cap may still buy out.
        living::StandingBidCap bidCap(totalcount, 10);

        std::unordered_map <ItemUsage, int32> freeMoney;

        freeMoney[ItemUsage::ITEM_USAGE_EQUIP] = freeMoney[ItemUsage::ITEM_USAGE_BAD_EQUIP] = (uint32)NeedMoneyFor::gear;
        freeMoney[ItemUsage::ITEM_USAGE_USE] = (uint32)NeedMoneyFor::consumables;
        freeMoney[ItemUsage::ITEM_USAGE_SKILL] = freeMoney[ItemUsage::ITEM_USAGE_DISENCHANT] =(uint32)NeedMoneyFor::tradeskill;
        freeMoney[ItemUsage::ITEM_USAGE_AMMO] = (uint32)NeedMoneyFor::ammo;
        freeMoney[ItemUsage::ITEM_USAGE_QUEST] = freeMoney[ItemUsage::ITEM_USAGE_AH] = freeMoney[ItemUsage::ITEM_USAGE_VENDOR] = freeMoney[ItemUsage::ITEM_USAGE_FORCE_NEED] = freeMoney[ItemUsage::ITEM_USAGE_FORCE_GREED] = (uint32)NeedMoneyFor::anything;

        std::vector<BidCandidate> candidates;

        // BotCheckAllAuctionListings=1 scans EVERY auction exactly once. The
        // old code performed map.size() random draws WITH replacement, which
        // visits only ~63% of a large house on average. The bounded path
        // (setting disabled) samples without replacement, so no draw is wasted
        // on a duplicate either.
        std::vector<AuctionEntry*> toScan;
        toScan.reserve(map.size());
        for (auto const& [auctionId, mapEntry] : map)
            if (mapEntry)
                toScan.push_back(mapEntry);

        if (!sPlayerbotAIConfig.botCheckAllAuctionListings)
        {
            uint32 const sampleCount = std::min<uint32>(urand(50, 250), static_cast<uint32>(toScan.size()));
            std::vector<AuctionEntry*> sampled;
            sampled.reserve(sampleCount);
            std::mt19937 gen(urand());
            std::sample(toScan.begin(), toScan.end(), std::back_inserter(sampled), sampleCount, gen);
            toScan = std::move(sampled);
        }

        for (AuctionEntry* scanEntry : toScan)
        {
            auction = auctionHouse->GetAuction(scanEntry->Id);

            if (!auction)
                continue;

            if (auction->owner == bot->GetGUIDLow())
                continue;

            // Also skip auctions the bot is already winning: the core accepts a
            // same-bidder raise and charges the delta, so repeated automatic
            // cycles would outbid the bot against itself toward the buyout.
            if (auction->bidder == bot->GetGUIDLow())
                continue;

            usage = AI_VALUE2(ItemUsage, "item usage", ItemQualifier(auction).GetQualifier());

            if (freeMoney.find(usage) == freeMoney.end())
                continue;

            uint32 const allowance = AI_VALUE2(uint32, "free money for", freeMoney[usage]);

            // ONE exact legal cost per candidate, mirroring the core handler's
            // minimum-bid/outbid rules. The old min(buyout, randomized bid)
            // made every no-buyout candidate cost zero and could emit bids the
            // core rejects (price <= bid, missing outbid increment). Items the
            // bot does not keep (vendor/greed) only bid; wanted items buy out
            // when the buyout fits the allowance, else fall back to bidding.
            // This scan-time cost RANKS the candidate only - it is never
            // authorization: everything is revalidated against current state
            // immediately before any bid is sent.
            uint32 bidCost = 0;
            if (!living::TryComputeAuctionBidCost(auction->startbid, auction->bid, auction->GetAuctionOutBid(),
                    auction->buyout, MAX_MONEY_AMOUNT, bidCost))
                continue;

            bool const bidOnly = usage == ItemUsage::ITEM_USAGE_VENDOR || usage == ItemUsage::ITEM_USAGE_FORCE_GREED;

            uint32 cost;
            bool isBuyout;
            if (!bidOnly && auction->buyout > 0 && auction->buyout <= allowance)
            {
                cost = auction->buyout;
                isBuyout = true;
            }
            else
            {
                cost = bidCost;
                isBuyout = auction->buyout > 0 && bidCost == auction->buyout;
            }

            if (cost > allowance || cost > bot->GetMoney())
                continue;

            uint32 power = 1;

            switch (usage)
            {
            case ItemUsage::ITEM_USAGE_EQUIP:
            case ItemUsage::ITEM_USAGE_BAD_EQUIP:
                power = sRandomItemMgr.GetLiveStatWeight(bot, auction->itemTemplate);
                break;
            case ItemUsage::ITEM_USAGE_AH:
            {
                auto pmo = sPerformanceMonitor.start(PERF_MON_VALUE, "IsWorthBuyingFromAhToResellAtAH", ai);
                bool isWorthBuyingFromAhToResellAtAH = ItemUsageValue::IsWorthBuyingFromAhToResellAtAH(sObjectMgr.GetItemPrototype(auction->itemTemplate), cost, auction->itemCount);
                pmo.reset();

                if (!isWorthBuyingFromAhToResellAtAH)
                    continue;
                power = 1000;
                break;
            }
            case ItemUsage::ITEM_USAGE_VENDOR:
                //basically if AH price is lower than vendor sell price then it's worth it
                if (cost / auction->itemCount >= (int32)sObjectMgr.GetItemPrototype(auction->itemTemplate)->SellPrice)
                    continue;
                power = 1000;
                break;
            case ItemUsage::ITEM_USAGE_FORCE_NEED:
            case ItemUsage::ITEM_USAGE_FORCE_GREED:
                power = 1000;
                break;
            }

            power *= 1000;
            power /= (cost + 1);

            candidates.push_back({ auction->Id, cost, isBuyout, power });
        }

        // Sort by POWER: the old pair<pointer, power> comparison with `i > j`
        // ranked candidates by allocation address first.
        std::sort(candidates.begin(), candidates.end(),
            [](BidCandidate const& a, BidCandidate const& b) { return a.power > b.power; });

        bool anyBid = false;

        for (BidCandidate const& candidate : candidates)
        {
            // Batched transactions revalidate CURRENT state immediately before
            // each mutation: earlier bids in this batch have already spent
            // money, so the scan-time snapshot authorizes nothing.
            auction = auctionHouse->GetAuction(candidate.auctionId);

            // Gone, changed hands, or the bot became high bidder meanwhile.
            if (!auction || auction->owner == bot->GetGUIDLow() || auction->bidder == bot->GetGUIDLow())
                continue;

            usage = AI_VALUE2(ItemUsage, "item usage", ItemQualifier(auction).GetQualifier());

            if (freeMoney.find(usage) == freeMoney.end())
                continue;

            // Reset and re-read this usage's CURRENT free-money allowance - the
            // cached value predates the earlier bids in this batch. Two
            // individually affordable candidates must not cumulatively dip into
            // money reserved for repairs/spells/consumables.
            RESET_AI_VALUE2(uint32, "free money for", freeMoney[usage]);
            uint32 const allowance = AI_VALUE2(uint32, "free money for", freeMoney[usage]);

            // Recompute ONE exact legal cost from the auction's current state.
            uint32 bidCost = 0;
            if (!living::TryComputeAuctionBidCost(auction->startbid, auction->bid, auction->GetAuctionOutBid(),
                    auction->buyout, MAX_MONEY_AMOUNT, bidCost))
                continue;

            bool const bidOnly = usage == ItemUsage::ITEM_USAGE_VENDOR || usage == ItemUsage::ITEM_USAGE_FORCE_GREED;

            uint32 cost;
            bool isBuyout;
            if (!bidOnly && auction->buyout > 0 && auction->buyout <= allowance)
            {
                cost = auction->buyout;
                isBuyout = true;
            }
            else
            {
                cost = bidCost;
                isBuyout = auction->buyout > 0 && bidCost == auction->buyout;
            }

            if (cost > allowance || cost > bot->GetMoney())
                continue;

            // Enforce the standing-bid cap immediately before each NON-buyout
            // bid; a buyout never consumes a slot and stays allowed at the cap.
            if (!isBuyout && !bidCap.CanPlaceStandingBid())
                continue;

            std::string reason = ItemUsageValue::ReasonForNeed(usage, auction, auction->itemCount, bot);

            bool const didBid = BidItem(requester, auction, cost, auctioneer, isBuyout, reason);

            // Accumulate: a later stale/rejected bid must not erase an earlier
            // successful money mutation from the result.
            anyBid |= didBid;

            // Only a successful STANDING bid occupies one of the 10 slots.
            if (didBid && !isBuyout)
                bidCap.RecordStandingBid();

            if (!urand(0, 5))
                break;
        }

        return anyBid;
    }

    // Documented syntax: ah bid <item link or item name> <budget>. The budget is
    // the trailing money token; everything before it is the item query. The old
    // parse read the FIRST token as money and matched names against the whole
    // text (budget included), so the documented forms could not match, an exact
    // item name with no budget matched with price 0, and a normal no-buyout
    // auction produced cost = min(0, ...) = 0 and divided by it.
    size_t const lastSpace = text.rfind(' ');
    if (lastSpace == std::string::npos)
    {
        ai->TellError(requester, "Usage: ah bid <item link or name> <budget>");
        return false;
    }

    std::string const budgetStr = text.substr(lastSpace + 1);
    std::string itemQuery = text.substr(0, lastSpace);
    while (!itemQuery.empty() && itemQuery.back() == ' ')
        itemQuery.pop_back();

    // A valid nonzero budget is required: NoMoney is not an unlimited bid.
    uint32 budget = 0;
    if (ChatHelper::parseMoney(budgetStr, budget) != living::MoneyParseStatus::Ok || budget == 0 || itemQuery.empty())
    {
        ai->TellError(requester, "Usage: ah bid <item link or name> <budget> (a nonzero amount like 5g)");
        return false;
    }

    // Links match by item template ID; plain text matches item names
    // case-insensitively.
    std::set<uint32> const queryItemIds = ChatHelper::ExtractAllItemIds(itemQuery);

    std::vector<BidCandidate> candidates;

    for (auto curAuction : map)
    {
        auction = curAuction.second;

        // Skip own auctions and auctions the bot is already winning (the core
        // accepts a same-bidder raise and charges the delta).
        if (!auction || auction->owner == bot->GetGUIDLow() || auction->bidder == bot->GetGUIDLow())
            continue;

        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(auction->itemTemplate);

        if (!proto || !proto->Name1)
            continue;

        if (!queryItemIds.empty())
        {
            if (queryItemIds.find(auction->itemTemplate) == queryItemIds.end())
                continue;
        }
        else if (!strstri(proto->Name1, itemQuery.c_str()))
            continue;

        // ONE exact bid cost per candidate, mirroring the core handler's
        // minimum-bid/outbid rules (start bid, current bid + core outbid
        // increment, buyout). The same value is stored with the candidate and
        // later sent in the packet - never recomputed.
        uint32 cost = 0;
        if (!living::TryComputeAuctionBidCost(auction->startbid, auction->bid, auction->GetAuctionOutBid(),
                auction->buyout, MAX_MONEY_AMOUNT, cost))
            continue;

        if (cost > budget || cost > bot->GetMoney())
            continue;

        uint32 const power = auction->itemCount * 1000 / cost; // cost >= 1 by contract

        candidates.push_back({ auction->Id, cost, auction->buyout > 0 && cost == auction->buyout, power });
    }

    if (candidates.empty())
        return false;

    std::sort(candidates.begin(), candidates.end(),
        [](BidCandidate const& a, BidCandidate const& b) { return a.power > b.power; });

    // Revalidate the chosen auction's CURRENT state immediately before the bid:
    // the scan-time cost ranked candidates, it does not authorize spending.
    BidCandidate const& best = candidates.front();
    auction = auctionHouse->GetAuction(best.auctionId);
    if (!auction || auction->owner == bot->GetGUIDLow() || auction->bidder == bot->GetGUIDLow())
        return false;

    uint32 currentCost = 0;
    if (!living::TryComputeAuctionBidCost(auction->startbid, auction->bid, auction->GetAuctionOutBid(),
            auction->buyout, MAX_MONEY_AMOUNT, currentCost))
        return false;

    if (currentCost > budget || currentCost > bot->GetMoney())
        return false;

    return BidItem(requester, auction, currentCost, auctioneer,
        auction->buyout > 0 && currentCost == auction->buyout);
}

bool AhBidAction::BidItem(Player* requester, AuctionEntry* auction, uint32 price, Unit* auctioneer, bool isBuyout, std::string reason)
{
    AuctionHouseEntry const* auctionHouseEntry = bot->GetSession()->GetCheckedAuctionHouseForAuctioneer(auctioneer->GetObjectGuid());
    if (!auctionHouseEntry)
        return false;

    // always return pointer
    AuctionHouseObject* auctionHouse = sAuctionMgr.GetAuctionsMap(auctionHouseEntry);

    if (!auctionHouse)
        return false;

    auction = auctionHouse->GetAuction(auction->Id);

    if (!auction)
        return false;

    WorldPacket packet;
    packet << auctioneer->GetObjectGuid();
    packet << auction->Id;
    packet << price;

    uint32 oldMoney = bot->GetMoney();
    ItemQualifier itemQualifier(auction);
    uint32 count = auction->itemCount;

    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(auction->itemTemplate);

    bot->GetSession()->HandleAuctionPlaceBid(packet);

    if (bot->GetMoney() < oldMoney)
    {
        sPlayerbotAIConfig.logEvent(ai, "AhBidAction", proto->Name1, std::to_string(proto->ItemId));
        std::ostringstream out;
        if (isBuyout)
        {
            out << "Buying out " << ChatHelper::formatItem(itemQualifier, count) << " for " << ChatHelper::formatMoney(price) << " on the AH";
        }
        else
        {
            out << "Bidding " << ChatHelper::formatMoney(price) << " on " << ChatHelper::formatItem(itemQualifier, count) << " on the AH";
        }
        if (!reason.empty())
            out << " " << reason;
        ai->TellPlayerNoFacing(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        return true;
    }
    return false;
}