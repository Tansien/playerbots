#pragma once

#include "LivingRealmConfig.h"

#include <cstdint>
#include <string>
#include <vector>

namespace living
{
    enum class ConfigClassification : uint8_t
    {
        Informational,       // no Organic consequence
        Compatible,          // configured value is valid for the Organic profile
        OverrideRequired,    // non-strict mode: 0.1 must override this to the fail-closed effective
                             // value at runtime; Phase 0 applies no override and only reports
        Conflict,            // strict mode: value conflicts and blocks Organic startup
        MissingPrerequisite  // hard 0.1 requirement absent; cannot be overridden fail-closed
    };

    enum class ConfigSeverity : uint8_t
    {
        Info,
        Warning,
        Blocking
    };

    enum class ConfigReasonCode : uint8_t
    {
        LivingRealmDisabled,
        OrganicProfileActive,
        UnknownProfile,
        RandomizationConflict,
        CheatMaskConflict,
        XpRateConflict,
        QuestSyncConflict,
        QuestFabricationConflict,
        LevelSyncConflict,
        FreeLearningConflict,
        TeleportConflict,
        TransportConflict,
        TimedRotationConflict,
        FreeSummonConflict,
        EnchantConflict,
        WorldBuffConflict,
        AsyncBotLoginRequired,
        PopulationNotInspected,
        MixedPopulationDetected,
        LivingSchemaMissing
    };

    // Provenance state of the existing random-bot population under the managed
    // account prefix. Detection requires database inspection (0.1 startup work), so
    // "not inspected" is an explicit state: it must never be reported as clean.
    enum class PopulationInspection : uint8_t
    {
        NotInspected,
        Clean,
        MixedDetected
    };

    struct EffectiveConfigEntry
    {
        std::string key;             // source configuration key (or prerequisite identifier)
        std::string configuredValue;
        std::string effectiveValue;  // value the Organic profile requires/would enforce
        std::string enforcementPoint;// where 0.1 enforces the effective value (0002A column)
        ConfigClassification classification = ConfigClassification::Informational;
        ConfigSeverity severity = ConfigSeverity::Info;
        ConfigReasonCode reason = ConfigReasonCode::LivingRealmDisabled;
        bool blocking = false;
    };

    // Pure-data snapshot of the already-parsed legacy settings the Organic profile
    // validates against (0002 section 3). Passed in explicitly so the report builder
    // reads no global configuration.
    struct LegacyCompatibilityInputs
    {
        bool instantRandomize = false;           // AiPlayerbot.InstantRandomize
        bool randomGearUpgradeEnabled = false;   // AiPlayerbot.RandomGearUpgradeEnabled
        uint32_t rndBotCheatMask = 0;            // parsed AiPlayerbot.RndBotCheats
        float xpRate = 1.0f;                     // AiPlayerbot.XPRate
        bool syncQuestWithPlayer = false;        // AiPlayerbot.SyncQuestWithPlayer
        bool syncQuestForPlayer = false;         // AiPlayerbot.SyncQuestForPlayer
        bool preQuests = false;                  // AiPlayerbot.PreQuests
        bool configuredQuestRewards = false;     // AiPlayerbot.RandomBotQuestIds non-empty
        bool syncLevelWithPlayers = false;       // AiPlayerbot.SyncLevelWithPlayers
        bool syncAltLevelToMaster = false;       // AiPlayerbot.SyncAltLevelToMaster
        std::string autoTrainSpells = "no";      // AiPlayerbot.AutoTrainSpells ("free" conflicts)
        bool autoLearnTrainerSpells = false;     // AiPlayerbot.AutoLearnTrainerSpells
        bool autoLearnQuestSpells = false;       // AiPlayerbot.AutoLearnQuestSpells
        bool autoLearnDroppedSpells = false;     // AiPlayerbot.AutoLearnDroppedSpells
        bool autoEnchantUpgradeLoot = false;     // AiPlayerbot.AutoEnchantUpgradeLoot
        uint32_t worldBuffCount = 0;             // number of parsed AiPlayerbot.WorldBuff* entries
        bool enableRandomTeleports = false;      // AiPlayerbot.EnableRandomTeleports
        bool randomBotTeleportNearPlayer = false;// AiPlayerbot.RandomBotTeleportNearPlayer
        bool enableMinimalMove = false;          // AiPlayerbot.EnableMinimalMove (direct teleport moves)
        uint32_t transportTeleportType = 0;      // AiPlayerbot.TransportTeleportType (modes 1/2 conflict)
        bool randomBotTimedLogout = false;       // AiPlayerbot.RandomBotTimedLogout
        bool randomBotTimedOffline = false;      // AiPlayerbot.RandomBotTimedOffline
        bool nonGmFreeSummon = false;            // AiPlayerbot.NonGmFreeSummon
        bool summonAtInnkeepersEnabled = false;  // AiPlayerbot.SummonAtInnkeepersEnabled
        bool asyncBotLogin = false;              // AiPlayerbot.AsyncBotLogin (0.1 prerequisite)
        PopulationInspection populationInspection =
            PopulationInspection::NotInspected;  // Phase 0 cannot inspect; never claim clean
        bool livingSchemaPresent = false;        // future global prerequisite; always false in Phase 0
    };

    struct EffectiveConfigReport
    {
        std::vector<EffectiveConfigEntry> entries;

        bool HasBlockingEntry() const;
    };

    // Pure and deterministic. Phase 0 only builds and reports; nothing enforces the
    // result against runtime behavior or startup yet (enforcement is 0.1 work).
    // Disabled Living Realm yields a single informational entry and validates
    // nothing else, preserving legacy behavior exactly.
    EffectiveConfigReport BuildEffectiveConfigReport(LivingRealmConfig const& config,
        LegacyCompatibilityInputs const& legacy);

    // Stable string forms, independent of any log sink formatting.
    char const* ToString(ConfigClassification value);
    char const* ToString(ConfigSeverity value);
    char const* ToString(ConfigReasonCode value);
    std::string FormatEffectiveConfigEntry(EffectiveConfigEntry const& entry);
}
