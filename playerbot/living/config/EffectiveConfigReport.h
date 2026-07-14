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
        RuntimeOverride,     // non-strict mode: value would be overridden to the fail-closed effective value
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
        FreeLearningConflict,
        TeleportConflict,
        TransportConflict,
        AsyncBotLoginRequired,
        LivingSchemaMissing
    };

    struct EffectiveConfigEntry
    {
        std::string key;             // source configuration key (or prerequisite identifier)
        std::string configuredValue;
        std::string effectiveValue;  // value the Organic profile requires/would enforce
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
        uint32_t rndBotCheatMask = 0;            // parsed AiPlayerbot.RndBotCheats
        float xpRate = 1.0f;                     // AiPlayerbot.XPRate
        bool syncQuestWithPlayer = false;        // AiPlayerbot.SyncQuestWithPlayer
        bool syncQuestForPlayer = false;         // AiPlayerbot.SyncQuestForPlayer
        std::string autoTrainSpells = "no";      // AiPlayerbot.AutoTrainSpells ("free" conflicts)
        bool autoLearnTrainerSpells = false;     // AiPlayerbot.AutoLearnTrainerSpells
        bool autoLearnQuestSpells = false;       // AiPlayerbot.AutoLearnQuestSpells
        bool autoLearnDroppedSpells = false;     // AiPlayerbot.AutoLearnDroppedSpells
        bool enableRandomTeleports = false;      // AiPlayerbot.EnableRandomTeleports
        bool randomBotTeleportNearPlayer = false;// AiPlayerbot.RandomBotTeleportNearPlayer
        uint32_t transportTeleportType = 0;      // AiPlayerbot.TransportTeleportType (modes 1/2 conflict)
        bool asyncBotLogin = false;              // AiPlayerbot.AsyncBotLogin (0.1 prerequisite)
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
