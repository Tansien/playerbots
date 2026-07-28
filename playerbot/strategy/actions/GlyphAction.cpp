#include "playerbot/playerbot.h"
#include "GlyphAction.h"
#include "playerbot/RandomItemMgr.h"
#include "playerbot/strategy/values/GlyphValues.h"
#include "playerbot/strategy/values/ItemUsageValue.h"

using namespace ai;

bool GlyphAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    std::string param = event.getParam();
    ItemIds ids = ChatHelper::parseItems(param);
    ProcessGlyphAction glyphAction = &GlyphAction::List;

    std::vector<uint32> glyphs;
    std::vector<uint8> glyphsSlots;

    if (param == "all")
    {
        glyphs = AI_VALUE(std::vector<uint32>, "available glyphs");

        for (auto& itemId : glyphs)
            glyphsSlots.push_back(GetEquipedGlyphSlot(itemId));
    }
    else if (param == "wanted")
    {
        glyphs = AI_VALUE(std::vector<uint32>, "wanted glyphs");

        for (auto& itemId : glyphs)
            glyphsSlots.push_back(GetEquipedGlyphSlot(itemId));
    }
    else if (param == "equiped")
    {
        glyphs = AI_VALUE(std::vector<uint32>, "equiped glyphs");

        for (uint8 glyphSlot = 0; glyphSlot < glyphs.size(); glyphSlot++)
            if (AvailableGlyphsValue::GetGlyphSlotTypeFromSlot(glyphSlot, bot->GetLevel()) != GlyphSlotType::LOCKED_SLOT)
                glyphsSlots.push_back(glyphSlot);
    }
    else if (param.find("remove") == 0)
    {
        std::vector<uint32> equipedGlyphs = AI_VALUE(std::vector<uint32>, "equiped glyphs");

        if (param.size() > 7)
        {
            std::string removeSlots = param.substr(7);

            for (auto& token : getMultiQualifiers(removeSlots, " "))
            {
                // This test used to be inverted: a well-formed slot number was
                // rejected as "not a valid slot number", and everything else
                // fell through to stoi, which throws on a non-numeric token.
                // So neither branch worked - `glyph remove 1` errored out and
                // `glyph remove x` terminated the world. Parse once, and bound
                // against the glyph vector rather than against `glyphs`, which
                // is the output being accumulated and is still empty here.
                int32 slot = 0;
                if (!parseNumberString(token, slot)
                    || slot < 0 || size_t(slot) >= equipedGlyphs.size())
                {
                    std::ostringstream out;
                    out << token << " is not a valid slot number";
                    ai->TellPlayer(requester, out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
                    return false;
                }

                glyphs.push_back(equipedGlyphs[slot]);
                glyphsSlots.push_back(uint8(slot));
            }
        }
        else //Remove all glyphs
        {
            for (uint8 glyphSlot = 0; glyphSlot < equipedGlyphs.size(); glyphSlot++)
                if (AvailableGlyphsValue::GetGlyphSlotTypeFromSlot(glyphSlot, bot->GetLevel()) != GlyphSlotType::LOCKED_SLOT)
                {
                    glyphs.push_back(equipedGlyphs[glyphSlot]);
                    glyphsSlots.push_back(glyphSlot);
                }
        }

        glyphAction = &GlyphAction::Remove;
    }
    else if (!ids.empty())
    {
        for (auto& id : ids)
        {
            if (!id)
                continue;

            glyphs.push_back(id);

            // Same reason as above: an oversized decimal passes a syntax check
            // and then throws inside stoi. 99 is the existing "no slot given"
            // sentinel, so an unusable number falls back to auto-detection
            // rather than taking the command down.
            int32 linkSlot = 0;
            size_t const slotPos = param.find("|r ");
            if (slotPos != std::string::npos
                && parseNumberString(param.substr(slotPos + 3), linkSlot)
                && linkSlot >= 0 && linkSlot < 99)
                glyphsSlots.push_back(uint8(linkSlot));
            else
                glyphsSlots.push_back(99);
        }

        glyphAction = &GlyphAction::Set;
    }
    else
    {
        ai->TellPlayer(requester, "Correct usage: glyph [glyph item link/remove] (optional slotnumber)", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        ai->TellPlayer(requester, "glyph all/wanted/equiped for list of glyphs", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        return false;
    }

    Iterate(requester, glyphAction, glyphs, glyphsSlots);

    return true;
}

void GlyphAction::Iterate(Player* requester, ProcessGlyphAction action, const std::vector<uint32> glyphs, const std::vector<uint8> glyphSlots)
{
    for (uint32 i = 0; i < glyphs.size(); i++)
    {
        std::ostringstream out;

        uint32 glyphItemId = glyphs[i];

        uint8 slotId = (glyphSlots.size() <= i ? 99 : glyphSlots[i]);

        if (action)
            (this->*action)(glyphItemId, slotId, out);

        if(!out.str().empty())
            ai->TellPlayer(requester, out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
    }
}
void GlyphAction::List(uint32 itemId, uint8 slotId, std::ostringstream& msg)
{
    GlyphSlotType slotType = EquipedGlyphsValue::GetGlyphSlotTypeFromSlot(slotId);
    std::string slotTypeName = (slotType == GlyphSlotType::MAJOR_SLOT ? "major" : "minor");

    if (itemId)
        msg << ChatHelper::formatItem(sObjectMgr.GetItemPrototype(itemId));
    else if (slotType != GlyphSlotType::LOCKED_SLOT)
        msg << "no glyph";

    if (slotType != GlyphSlotType::LOCKED_SLOT)
        msg << " (equiped in "<< slotTypeName << " slot "  << std::to_string(slotId) << ")";
}
void GlyphAction::Set(uint32 itemId, uint8 wantedSlotId, std::ostringstream& msg)
{
    const ItemPrototype* glyphProto = sObjectMgr.GetItemPrototype(itemId);

    if (!glyphProto)
        return;

    uint32 glyphId = EquipedGlyphsValue::GetGlyphIdFromProto(glyphProto);

    GlyphSlotType glyphSlotType = EquipedGlyphsValue::GetGlyphSlotTypeFromItemId(itemId);

    std::map<std::string, std::string> placeholders;
    placeholders["%glyph"] = ChatHelper::formatItem(glyphProto);

#ifdef MANGOSBOT_TWO
    if (glyphProto->Class != ITEM_CLASS_GLYPH || !glyphId)
    {
        msg << BOT_TEXT2("%glyph is not a valid glyph", placeholders);
        return;
    }
#endif

    if (glyphProto->AllowableClass != bot->getClassMask())
    {
        msg << BOT_TEXT2("%glyph is not a valid glyph for this class", placeholders);
        return;
    }

    if (glyphProto->RequiredLevel > bot->GetLevel())
    {
        msg << BOT_TEXT2("%glyph can not be equiped for this level", placeholders);
        return;
    }

    Item* glyphItem = ItemUsageValue::CurrentItem(glyphProto, bot);

    if (!glyphItem && !ai->HasCheat(BotCheatMask::glyph))
    {
        msg << BOT_TEXT2("%glyph is not found in inventory", placeholders);
        return;
    }

    std::vector<uint32> equipedGlyphs = AI_VALUE(std::vector<uint32>, "equiped glyphs");
    uint8 useSlotId = wantedSlotId;
    bool detectSlot = wantedSlotId > equipedGlyphs.size();

    if (!detectSlot && EquipedGlyphsValue::GetGlyphSlotTypeFromSlot(useSlotId, bot->GetLevel()) == GlyphSlotType::LOCKED_SLOT)
    {
        msg << BOT_TEXT2("Slot %slot is locked", placeholders);
        return;
    }

    for (uint32 slotId = 0; slotId < equipedGlyphs.size(); slotId++)
    {
        if (!detectSlot && useSlotId != slotId)
            continue;

        if (glyphSlotType != EquipedGlyphsValue::GetGlyphSlotTypeFromSlot(slotId, bot->GetLevel()))
        {
            if (detectSlot)
                continue;

            msg << BOT_TEXT2("%glyph does not fit in slot %slot", placeholders);
            return;
        }

        if (!equipedGlyphs[slotId])
        {
            useSlotId = slotId;
            break;
        }
    }

    placeholders["%slot"] = std::to_string(useSlotId);

    if (useSlotId > equipedGlyphs.size())
    {
        msg << BOT_TEXT2("No free slot available for %glyph", placeholders);
        return;
    }

    if (std::find(equipedGlyphs.begin(), equipedGlyphs.end(), itemId) != equipedGlyphs.end())
    {
        msg << BOT_TEXT2("%glyph is already active", placeholders);
        return;
    }

#ifdef MANGOSBOT_TWO
    //Refactor to use packet/use item action later.
    bot->ApplyGlyph(useSlotId, false);
    bot->SetGlyph(useSlotId, glyphId);
    bot->ApplyGlyph(useSlotId, true);
    bot->SendTalentsInfoData(false);

    uint32 count = 1;

    if (!ai->HasCheat(BotCheatMask::item))
        bot->DestroyItemCount(itemId, count, true);
#endif

    msg << BOT_TEXT2("Applied %glyph to slot %slot", placeholders);
}

void GlyphAction::Remove(uint32 itemId, uint8 removeSlotId, std::ostringstream& msg)
{
    std::map<std::string, std::string> placeholders;
    placeholders["%slot"] = std::to_string(removeSlotId);

    if (EquipedGlyphsValue::GetGlyphSlotTypeFromSlot(removeSlotId, bot->GetLevel()) == GlyphSlotType::LOCKED_SLOT)
    {
        msg << BOT_TEXT2("Slot %slot is locked", placeholders);
        return;
    }

    if (!itemId)
    {
        msg << BOT_TEXT2("No glyph in slot %slot", placeholders);
    }
        
    const ItemPrototype* glyphProto = sObjectMgr.GetItemPrototype(itemId);

    if (!glyphProto)
        return;

    placeholders["%glyph"] = ChatHelper::formatItem(glyphProto);

    std::vector<uint32> equipedGlyphs = AI_VALUE(std::vector<uint32>, "equiped glyphs");
#ifdef MANGOSBOT_TWO
    //Refactor to use packet/use item action later.
    bot->ApplyGlyph(removeSlotId, false);
    bot->SetGlyph(removeSlotId, 0);
    bot->ApplyGlyph(removeSlotId, true);
    bot->SendTalentsInfoData(false);
#endif

    msg << BOT_TEXT2("Removed %glyph from slot %slot", placeholders);
}

uint8 GlyphAction::GetEquipedGlyphSlot(uint32 itemId)
{
    std::vector<uint32> equipedGlyphs = AI_VALUE(std::vector<uint32>, "equiped glyphs");

    for (uint8 slotId = 0; slotId < equipedGlyphs.size(); slotId++)
    {
        if (itemId == equipedGlyphs[slotId])
            return slotId;
    }

    return 99;
}

bool AutoSetGlyphAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();

    std::vector<uint32> availableGlyphs = AI_VALUE(std::vector<uint32>, "available glyphs");

    std::vector<uint32> wantedGlyphs = AI_VALUE(std::vector<uint32>, "wanted glyphs");

    std::vector<uint32> equipedGlyphs = AI_VALUE(std::vector<uint32>, "equiped glyphs");

    std::vector<uint32> newGlyphs;
    std::vector<uint8> newSlots;    

    std::reverse(wantedGlyphs.begin(), wantedGlyphs.end());

    for (size_t wantedRank = 0; wantedRank < wantedGlyphs.size(); wantedRank++)
    {
        uint32 useSlot = 999;
        uint32 itemId = wantedGlyphs[wantedRank];

        if (std::find(equipedGlyphs.begin(), equipedGlyphs.end(), itemId) != equipedGlyphs.end()) //Already have glyph equiped
            continue;

        if (std::find(availableGlyphs.begin(), availableGlyphs.end(), itemId) == availableGlyphs.end()) //Do not have the glyph available
            continue;

        const ItemPrototype* glyphProto = sObjectMgr.GetItemPrototype(itemId);
        uint32 glyphId = EquipedGlyphsValue::GetGlyphIdFromProto(glyphProto);
        GlyphSlotType glyphSlotType = EquipedGlyphsValue::GetGlyphSlotTypeFromItemId(itemId);

        for (uint8 newSlot = 0; newSlot < equipedGlyphs.size(); newSlot++)
        {            
            GlyphSlotType slotType = EquipedGlyphsValue::GetGlyphSlotTypeFromSlot(newSlot, bot->GetLevel());

            if (slotType == GlyphSlotType::LOCKED_SLOT)
                continue;

            if (glyphSlotType != slotType)
                continue;

            size_t currentWantedRank = GlyphIsUpgradeValue::GetWantedRank(equipedGlyphs[newSlot], wantedGlyphs);

             if (currentWantedRank < wantedGlyphs.size() && currentWantedRank < wantedRank) //Do not replace glyph with a glyph with a better wanted rank.
                continue;

            useSlot = newSlot;
        }

        if (useSlot > equipedGlyphs.size())
            continue;

        newGlyphs.push_back(itemId);
        newSlots.push_back(useSlot);

        equipedGlyphs[useSlot] = itemId;
    }

    Iterate(requester, &GlyphAction::Set, newGlyphs, newSlots);

    return true;
}