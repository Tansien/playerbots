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

LIVING_TEST(quest_slots_all_orphan_log_is_full_and_quarantined)
{
    // A log made entirely of orphans is FULL, not empty: the old rule reported it
    // as completely free, so acceptance wedged against a core that refused to add.
    std::vector<Slot> allOrphans;
    for (uint8_t i = 0; i < MAX_SLOTS; ++i)
        allOrphans.push_back({ uint32_t(5000 + i), false });

    LIVING_CHECK(CountOccupied(allOrphans) == MAX_SLOTS);
    LIVING_CHECK(FreeSlots(allOrphans) == 0);
    LIVING_CHECK(CountOccupiedByTemplate(allOrphans) == 0); // the old, wrong answer

    // Every orphan is identified and the whole log is quarantined: automated
    // cleanup is denied outright. Clearing slots is NOT a repair - it leaves quest
    // status, source items, and timers behind.
    QuestLogPreflight preflight;
    for (Slot const& slot : allOrphans)
        LIVING_CHECK(preflight.Observe(slot.questId, slot.hasTemplate)); // each one logged
    LIVING_CHECK(preflight.occupied == MAX_SLOTS);
    LIVING_CHECK(preflight.orphans == MAX_SLOTS);
    LIVING_CHECK(!preflight.CleanupPermitted());
}

LIVING_TEST(quest_slots_cleanup_preflight_quarantines_before_any_pruning)
{
    // The exact production decision CleanQuestLogAction runs before its first
    // pruning pass. The critical case: a full log of 24 orphans plus ONE valid
    // quest. Pruning "around" the orphans would drop the only valid quest to gain
    // capacity the orphans still hold, so the preflight must deny cleanup entirely
    // - zero drops, zero slot mutations.
    QuestLogPreflight mixedFull;
    for (uint8_t i = 0; i < 24; ++i)
        mixedFull.Observe(uint32_t(6000 + i), false); // orphans
    mixedFull.Observe(777, true);                     // the one valid quest

    LIVING_CHECK(mixedFull.occupied == 25);
    LIVING_CHECK(mixedFull.orphans == 24);
    LIVING_CHECK(!mixedFull.CleanupPermitted()); // nothing may be dropped

    // ONE orphan anywhere is enough to quarantine the whole log.
    QuestLogPreflight oneOrphan;
    oneOrphan.Observe(100, true);
    oneOrphan.Observe(4242, false);
    oneOrphan.Observe(101, true);
    LIVING_CHECK(!oneOrphan.CleanupPermitted());

    // A healthy log (any mix of valid quests and empty slots) permits cleanup.
    QuestLogPreflight healthy;
    healthy.Observe(100, true);
    healthy.Observe(0, false); // empty slot, ignored
    healthy.Observe(101, true);
    LIVING_CHECK(healthy.occupied == 2);
    LIVING_CHECK(healthy.orphans == 0);
    LIVING_CHECK(healthy.CleanupPermitted());

    // An empty log trivially permits cleanup (there is nothing to protect).
    LIVING_CHECK(QuestLogPreflight{}.CleanupPermitted());
}

LIVING_TEST(quest_slots_orphan_fingerprint_dedupes_recurring_logs)
{
    // Cleanup runs from a recurring trigger; the caller logs the quarantine only
    // when this fingerprint changes, so an unchanged orphan set is logged once.
    QuestLogPreflight healthy;
    healthy.Observe(100, true);
    LIVING_CHECK(healthy.OrphanFingerprint() == 0); // no orphans -> sentinel 0

    QuestLogPreflight first;
    first.Observe(100, true);
    first.Observe(4242, false);
    first.Observe(9999, false);
    LIVING_CHECK(first.OrphanFingerprint() != 0);

    // The same orphan set observed on a later attempt fingerprints identically.
    QuestLogPreflight repeat;
    repeat.Observe(100, true);
    repeat.Observe(4242, false);
    repeat.Observe(9999, false);
    LIVING_CHECK(repeat.OrphanFingerprint() == first.OrphanFingerprint());

    // Valid quests do not contribute: only the orphan set identifies the log.
    QuestLogPreflight differentValid;
    differentValid.Observe(555, true);
    differentValid.Observe(4242, false);
    differentValid.Observe(9999, false);
    LIVING_CHECK(differentValid.OrphanFingerprint() == first.OrphanFingerprint());

    // A changed orphan set (grown, shrunk, or different IDs) logs again.
    QuestLogPreflight grown;
    grown.Observe(4242, false);
    grown.Observe(9999, false);
    grown.Observe(1234, false);
    LIVING_CHECK(grown.OrphanFingerprint() != first.OrphanFingerprint());

    QuestLogPreflight shrunk;
    shrunk.Observe(4242, false);
    LIVING_CHECK(shrunk.OrphanFingerprint() != first.OrphanFingerprint());

    QuestLogPreflight otherIds;
    otherIds.Observe(4243, false);
    otherIds.Observe(9999, false);
    LIVING_CHECK(otherIds.OrphanFingerprint() != first.OrphanFingerprint());
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

    // In a mixed log, exactly the orphans are detected; valid quests are never
    // classified as orphans, and detection is observation only - no repair exists.
    std::vector<Slot> const mixed = {
        { 100, true }, { 4242, false }, { 101, true }, { 9999, false }
    };

    uint8_t orphans = 0;
    uint8_t survivors = 0;
    for (Slot const& slot : mixed)
    {
        if (IsOrphanedQuestSlot(slot.questId, slot.hasTemplate))
            ++orphans;
        else if (IsQuestSlotOccupied(slot.questId))
            ++survivors;
    }

    LIVING_CHECK(orphans == 2);
    LIVING_CHECK(survivors == 2);
}
