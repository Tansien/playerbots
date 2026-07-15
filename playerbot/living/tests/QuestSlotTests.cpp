#include "LivingTest.h"

#include "../util/LivingQuestSlots.h"

#include <cstdint>
#include <vector>

using namespace living;

namespace
{
    // A quest-log slot as the runtime sees it: the stored quest ID, plus whether a
    // template still resolves for it.
    struct Slot
    {
        uint32_t questId;
        bool hasTemplate;
    };

    constexpr uint8_t MAX_SLOTS = 25; // mirrors MAX_QUEST_LOG_SIZE

    // The occupancy rule both CleanQuestLogAction and FreeQuestLogSlotValue now use.
    uint8_t CountOccupied(std::vector<Slot> const& log)
    {
        uint8_t occupied = 0;
        for (Slot const& slot : log)
            if (IsQuestSlotOccupied(slot.questId))
                ++occupied;
        return occupied;
    }

    uint8_t FreeSlots(std::vector<Slot> const& log)
    {
        return uint8_t(MAX_SLOTS - CountOccupied(log));
    }

    // The old rule, kept only to prove the two truths actually differed.
    uint8_t CountOccupiedByTemplate(std::vector<Slot> const& log)
    {
        uint8_t occupied = 0;
        for (Slot const& slot : log)
            if (slot.questId != 0 && slot.hasTemplate)
                ++occupied;
        return occupied;
    }
}

LIVING_TEST(quest_slots_occupancy_counts_every_nonzero_slot)
{
    // Mixed log: valid quests, a failed quest, and two orphans (template removed
    // from the DB). Every nonzero slot occupies the log.
    std::vector<Slot> const mixed = {
        { 100, true }, { 101, true }, { 4242, false }, { 102, true }, { 9999, false }
    };

    LIVING_CHECK(CountOccupied(mixed) == 5);
    LIVING_CHECK(FreeSlots(mixed) == MAX_SLOTS - 5);

    // The template-based rule under-counted by exactly the orphans, which is how
    // capacity ended up meaning two different things in two places.
    LIVING_CHECK(CountOccupiedByTemplate(mixed) == 3);
    LIVING_CHECK(CountOccupied(mixed) != CountOccupiedByTemplate(mixed));

    LIVING_CHECK(IsQuestSlotOccupied(1));
    LIVING_CHECK(!IsQuestSlotOccupied(0)); // an empty slot is the only free slot
}

LIVING_TEST(quest_slots_all_orphan_log_is_full_and_repairable)
{
    // A log made entirely of orphans is FULL, not empty: the old rule reported it
    // as completely free, so acceptance wedged against a core that refused to add.
    std::vector<Slot> allOrphans;
    for (uint8_t i = 0; i < MAX_SLOTS; ++i)
        allOrphans.push_back({ uint32_t(5000 + i), false });

    LIVING_CHECK(CountOccupied(allOrphans) == MAX_SLOTS);
    LIVING_CHECK(FreeSlots(allOrphans) == 0);
    LIVING_CHECK(CountOccupiedByTemplate(allOrphans) == 0); // the old, wrong answer

    // Every one of them is identified as repairable, so cleanup can free the log
    // instead of being unable to remove anything.
    for (Slot const& slot : allOrphans)
        LIVING_CHECK(IsOrphanedQuestSlot(slot.questId, slot.hasTemplate));

    // Clearing them (SetQuestSlot(slot, 0) in the action) frees real capacity.
    std::vector<Slot> repaired;
    for (Slot const& slot : allOrphans)
        repaired.push_back(IsOrphanedQuestSlot(slot.questId, slot.hasTemplate) ? Slot{ 0, false } : slot);
    LIVING_CHECK(FreeSlots(repaired) == MAX_SLOTS);
}

LIVING_TEST(quest_slots_orphan_rule_never_targets_valid_quests)
{
    // Only template-less occupied slots are orphans. A valid quest is never
    // repairable, so cleanup can no longer discard valid quests while keeping the
    // bad entry.
    LIVING_CHECK(IsOrphanedQuestSlot(4242, false));
    LIVING_CHECK(!IsOrphanedQuestSlot(100, true));  // valid quest
    LIVING_CHECK(!IsOrphanedQuestSlot(0, false));   // empty slot is not an orphan
    LIVING_CHECK(!IsOrphanedQuestSlot(0, true));

    // In a mixed log, exactly the orphans are repaired and the valid quests survive.
    std::vector<Slot> const mixed = {
        { 100, true }, { 4242, false }, { 101, true }, { 9999, false }
    };

    uint8_t repairs = 0;
    uint8_t survivors = 0;
    for (Slot const& slot : mixed)
    {
        if (IsOrphanedQuestSlot(slot.questId, slot.hasTemplate))
            ++repairs;
        else if (IsQuestSlotOccupied(slot.questId))
            ++survivors;
    }

    LIVING_CHECK(repairs == 2);
    LIVING_CHECK(survivors == 2);
}
