#pragma once
#include "GenericActions.h"
#include "playerbot/living/util/LivingAuction.h"

namespace ai
{
    class AhAction : public ChatCommandAction
    {
    public:
        AhAction(PlayerbotAI* ai, std::string name = "ah") : ChatCommandAction(ai, name) {}
        virtual bool Execute(Event& event) override;

    protected:
        // Action-scoped preparation, run BEFORE the AH mutex is taken (so a DB
        // round trip never happens under the mutex). Returning false fails the
        // whole action closed. The bid action loads its sibling set here.
        virtual bool PrepareAction() { return true; }

    private:
        virtual bool ExecuteCommand(Player* requester, std::string text, Unit* auctioneer);
        bool PostItem(Player* requester, Item* item, uint32 price, Unit* auctioneer, uint32 time);

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "ah"; } //Must equal iternal name
        virtual std::string GetHelpDescription()
        {
            return "This command will make bots auction items to a nearby auction houses.\n"
                "Usage: ah [itemlink] <money>\n"
                "Example: ah vendor (post items based on item use)\n"
                "Example: ah [itemlink] 5g\n";
        }
        virtual std::vector<std::string> GetUsedActions() { return {}; }
        virtual std::vector<std::string> GetUsedValues() { return { "nearest npcs", "inventory items", "item usage", "free money for" }; }
#endif
    };

    class AhBidAction : public AhAction
    {
    public:
        AhBidAction(PlayerbotAI* ai) : AhAction(ai, "ah bid") {}

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "ah bid"; } //Must equal iternal name
        virtual std::string GetHelpDescription()
        {
            return "This command will make bots bid on a specific item with a specific budget on a nearby auctionhouse.\n"
                "The highest item/gold auction will be used that falls below the given budget.\n"
                "Usage: ah bid <itemlink or item name> <money>\n"
                "The trailing money amount is a required nonzero budget; the bid uses the auction's exact minimum acceptable cost within it.\n"
                "Example: ah bid vendor (bid on items based on item use)\n"
                "Example: ah bid [itemlink] 5g\n"
                "Example: ah bid copper ore 20s\n";
        }
        virtual std::vector<std::string> GetUsedActions() { return {}; }
        virtual std::vector<std::string> GetUsedValues() { return { "nearest npcs", "item usage", "free money for" }; }
#endif 
    protected:
        virtual bool PrepareAction() override;

    private:
        virtual bool ExecuteCommand(Player* requester, std::string text, Unit* auctioneer);
        bool BidItem(Player* requester, AuctionEntry* auction, uint32 price, Unit* auctioneer, bool isBuyout, std::string reason = "");

        // The bidder account's character GUIDs, loaded by PrepareAction and
        // reused by every attempt, scan, pre-bid revalidation and BidItem
        // packet boundary within the refresh window - admission is a pure set
        // membership check, never a per-auction account lookup.
        living::AuctionBidderSiblings bidderSiblings;
        time_t siblingsLoadedAt = 0;

        // Bots are packed several to an account and character creation can put
        // a new one on this account mid-session, so the set is re-read on this
        // interval. The core only rejects a same-account bid when the owner is
        // OFFLINE, which makes this set the only thing standing between an
        // ONLINE sibling and a self-deal - it must not go stale for a whole
        // session. One query a minute per bidding bot, versus one per attempt.
        static constexpr time_t siblingsRefreshSeconds = 60;
    };
}
