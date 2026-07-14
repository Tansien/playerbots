#include "EffectiveConfigReport.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace living
{
    namespace
    {
        std::string FormatFloatStable(float value)
        {
            // Deterministic short form ("1", "1.5") so report text is stable across
            // platforms; std::to_string's fixed six decimals are noisy in reports.
            std::ostringstream out;
            out << value;
            return out.str();
        }

        void AddEntry(EffectiveConfigReport& report, std::string key, std::string configured,
            std::string effective, ConfigClassification classification, ConfigSeverity severity,
            ConfigReasonCode reason, bool blocking)
        {
            EffectiveConfigEntry entry;
            entry.key = std::move(key);
            entry.configuredValue = std::move(configured);
            entry.effectiveValue = std::move(effective);
            entry.classification = classification;
            entry.severity = severity;
            entry.reason = reason;
            entry.blocking = blocking;
            report.entries.push_back(std::move(entry));
        }

        // A conflicting legacy value can be overridden fail-closed at runtime, so
        // strict mode decides whether it blocks (0002 section 3).
        void AddConflict(EffectiveConfigReport& report, bool strict, std::string key,
            std::string configured, std::string effective, ConfigReasonCode reason)
        {
            if (strict)
                AddEntry(report, std::move(key), std::move(configured), std::move(effective),
                    ConfigClassification::Conflict, ConfigSeverity::Blocking, reason, true);
            else
                AddEntry(report, std::move(key), std::move(configured), std::move(effective),
                    ConfigClassification::RuntimeOverride, ConfigSeverity::Warning, reason, false);
        }
    }

    bool EffectiveConfigReport::HasBlockingEntry() const
    {
        for (EffectiveConfigEntry const& entry : entries)
            if (entry.blocking)
                return true;

        return false;
    }

    EffectiveConfigReport BuildEffectiveConfigReport(LivingRealmConfig const& config,
        LegacyCompatibilityInputs const& legacy)
    {
        EffectiveConfigReport report;

        // Disabled Living Realm validates nothing: no schema, no legacy conflict, no
        // blocking entry. This is the LR-001 parity guarantee.
        if (!config.enabled)
        {
            AddEntry(report, LIVING_REALM_ENABLED_KEY, "0", "0",
                ConfigClassification::Informational, ConfigSeverity::Info,
                ConfigReasonCode::LivingRealmDisabled, false);
            return report;
        }

        if (config.profile == LivingRealmProfile::Organic)
            AddEntry(report, LIVING_REALM_PROFILE_KEY, config.profileName, "organic",
                ConfigClassification::Compatible, ConfigSeverity::Info,
                ConfigReasonCode::OrganicProfileActive, false);
        else
            // An unknown profile cannot be reasoned about; it blocks regardless of
            // strict mode.
            AddEntry(report, LIVING_REALM_PROFILE_KEY, config.profileName, "organic",
                ConfigClassification::Conflict, ConfigSeverity::Blocking,
                ConfigReasonCode::UnknownProfile, true);

        // Legacy fabrication settings the Organic profile forces to their
        // fail-closed values (0002 section 3 mandatory outcomes).
        if (legacy.instantRandomize)
            AddConflict(report, config.strict, "AiPlayerbot.InstantRandomize", "1", "0",
                ConfigReasonCode::RandomizationConflict);

        if (legacy.rndBotCheatMask != 0)
            AddConflict(report, config.strict, "AiPlayerbot.RndBotCheats",
                std::to_string(legacy.rndBotCheatMask), "0", ConfigReasonCode::CheatMaskConflict);

        if (legacy.xpRate != 1.0f)
            AddConflict(report, config.strict, "AiPlayerbot.XPRate",
                FormatFloatStable(legacy.xpRate), "1", ConfigReasonCode::XpRateConflict);

        if (legacy.syncQuestWithPlayer)
            AddConflict(report, config.strict, "AiPlayerbot.SyncQuestWithPlayer", "1", "0",
                ConfigReasonCode::QuestSyncConflict);

        if (legacy.syncQuestForPlayer)
            AddConflict(report, config.strict, "AiPlayerbot.SyncQuestForPlayer", "1", "0",
                ConfigReasonCode::QuestSyncConflict);

        {
            std::string lowered = legacy.autoTrainSpells;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lowered == "free")
                AddConflict(report, config.strict, "AiPlayerbot.AutoTrainSpells",
                    legacy.autoTrainSpells, "no", ConfigReasonCode::FreeLearningConflict);
        }

        if (legacy.autoLearnTrainerSpells)
            AddConflict(report, config.strict, "AiPlayerbot.AutoLearnTrainerSpells", "1", "0",
                ConfigReasonCode::FreeLearningConflict);

        if (legacy.autoLearnQuestSpells)
            AddConflict(report, config.strict, "AiPlayerbot.AutoLearnQuestSpells", "1", "0",
                ConfigReasonCode::FreeLearningConflict);

        if (legacy.autoLearnDroppedSpells)
            AddConflict(report, config.strict, "AiPlayerbot.AutoLearnDroppedSpells", "1", "0",
                ConfigReasonCode::FreeLearningConflict);

        if (legacy.enableRandomTeleports)
            AddConflict(report, config.strict, "AiPlayerbot.EnableRandomTeleports", "1", "0",
                ConfigReasonCode::TeleportConflict);

        if (legacy.randomBotTeleportNearPlayer)
            AddConflict(report, config.strict, "AiPlayerbot.RandomBotTeleportNearPlayer", "1", "0",
                ConfigReasonCode::TeleportConflict);

        if (legacy.transportTeleportType != 0)
            AddConflict(report, config.strict, "AiPlayerbot.TransportTeleportType",
                std::to_string(legacy.transportTeleportType), "0", ConfigReasonCode::TransportConflict);

        // Hard 0.1 prerequisites: these cannot be overridden fail-closed, so they
        // block regardless of strict mode. Phase 0 models them without enforcing.
        if (!legacy.asyncBotLogin)
            AddEntry(report, "AiPlayerbot.AsyncBotLogin", "0", "1",
                ConfigClassification::MissingPrerequisite, ConfigSeverity::Blocking,
                ConfigReasonCode::AsyncBotLoginRequired, true);

        if (!legacy.livingSchemaPresent)
            AddEntry(report, "LivingRealm.Schema", "missing", "clean",
                ConfigClassification::MissingPrerequisite, ConfigSeverity::Blocking,
                ConfigReasonCode::LivingSchemaMissing, true);

        return report;
    }

    char const* ToString(ConfigClassification value)
    {
        switch (value)
        {
            case ConfigClassification::Informational: return "Informational";
            case ConfigClassification::Compatible: return "Compatible";
            case ConfigClassification::RuntimeOverride: return "RuntimeOverride";
            case ConfigClassification::Conflict: return "Conflict";
            case ConfigClassification::MissingPrerequisite: return "MissingPrerequisite";
        }

        return "INVALID_CLASSIFICATION";
    }

    char const* ToString(ConfigSeverity value)
    {
        switch (value)
        {
            case ConfigSeverity::Info: return "Info";
            case ConfigSeverity::Warning: return "Warning";
            case ConfigSeverity::Blocking: return "Blocking";
        }

        return "INVALID_SEVERITY";
    }

    char const* ToString(ConfigReasonCode value)
    {
        switch (value)
        {
            case ConfigReasonCode::LivingRealmDisabled: return "LIVING_REALM_DISABLED";
            case ConfigReasonCode::OrganicProfileActive: return "ORGANIC_PROFILE_ACTIVE";
            case ConfigReasonCode::UnknownProfile: return "UNKNOWN_PROFILE";
            case ConfigReasonCode::RandomizationConflict: return "RANDOMIZATION_CONFLICT";
            case ConfigReasonCode::CheatMaskConflict: return "CHEAT_MASK_CONFLICT";
            case ConfigReasonCode::XpRateConflict: return "XP_RATE_CONFLICT";
            case ConfigReasonCode::QuestSyncConflict: return "QUEST_SYNC_CONFLICT";
            case ConfigReasonCode::FreeLearningConflict: return "FREE_LEARNING_CONFLICT";
            case ConfigReasonCode::TeleportConflict: return "TELEPORT_CONFLICT";
            case ConfigReasonCode::TransportConflict: return "TRANSPORT_CONFLICT";
            case ConfigReasonCode::AsyncBotLoginRequired: return "ASYNC_BOT_LOGIN_REQUIRED";
            case ConfigReasonCode::LivingSchemaMissing: return "LIVING_SCHEMA_MISSING";
        }

        return "INVALID_REASON";
    }

    std::string FormatEffectiveConfigEntry(EffectiveConfigEntry const& entry)
    {
        std::string text = entry.key;
        text += " configured=";
        text += entry.configuredValue;
        text += " effective=";
        text += entry.effectiveValue;
        text += " classification=";
        text += ToString(entry.classification);
        text += " severity=";
        text += ToString(entry.severity);
        text += " reason=";
        text += ToString(entry.reason);
        text += entry.blocking ? " blocking=1" : " blocking=0";
        return text;
    }
}
