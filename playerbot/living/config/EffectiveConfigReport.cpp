#include "EffectiveConfigReport.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <locale>
#include <sstream>

namespace living
{
    namespace
    {
        std::string FormatFloatStable(float value)
        {
            // Locale-independent, round-trip-faithful text: the classic locale keeps
            // the decimal point a '.', and max_digits10 makes values that merely
            // differ from 1.0 by one ulp visibly different from "1" in the report.
            std::ostringstream out;
            out.imbue(std::locale::classic());
            out.precision(std::numeric_limits<float>::max_digits10);
            out << value;
            return out.str();
        }

        void AddEntry(EffectiveConfigReport& report, std::string key, std::string configured,
            std::string effective, std::string enforcementPoint, ConfigClassification classification,
            ConfigSeverity severity, ConfigReasonCode reason, bool blocking)
        {
            EffectiveConfigEntry entry;
            entry.key = std::move(key);
            entry.configuredValue = std::move(configured);
            entry.effectiveValue = std::move(effective);
            entry.enforcementPoint = std::move(enforcementPoint);
            entry.classification = classification;
            entry.severity = severity;
            entry.reason = reason;
            entry.blocking = blocking;
            report.entries.push_back(std::move(entry));
        }

        // A conflicting legacy value can be overridden fail-closed at runtime, so
        // strict mode decides whether it blocks (0002 section 3). Phase 0 applies
        // no override in either mode; it only reports.
        void AddConflict(EffectiveConfigReport& report, bool strict, std::string key,
            std::string configured, std::string effective, std::string enforcementPoint,
            ConfigReasonCode reason)
        {
            if (strict)
                AddEntry(report, std::move(key), std::move(configured), std::move(effective),
                    std::move(enforcementPoint), ConfigClassification::Conflict,
                    ConfigSeverity::Blocking, reason, true);
            else
                AddEntry(report, std::move(key), std::move(configured), std::move(effective),
                    std::move(enforcementPoint), ConfigClassification::OverrideRequired,
                    ConfigSeverity::Warning, reason, false);
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
            AddEntry(report, LIVING_REALM_ENABLED_KEY, "0", "0", "none",
                ConfigClassification::Informational, ConfigSeverity::Info,
                ConfigReasonCode::LivingRealmDisabled, false);
            return report;
        }

        if (config.profile == LivingRealmProfile::Organic)
            AddEntry(report, LIVING_REALM_PROFILE_KEY, config.profileName, "organic",
                "startup validation", ConfigClassification::Compatible, ConfigSeverity::Info,
                ConfigReasonCode::OrganicProfileActive, false);
        else
            // An unknown profile cannot be reasoned about; it blocks regardless of
            // strict mode.
            AddEntry(report, LIVING_REALM_PROFILE_KEY, config.profileName, "organic",
                "startup validation", ConfigClassification::Conflict, ConfigSeverity::Blocking,
                ConfigReasonCode::UnknownProfile, true);

        // Legacy fabrication settings the Organic profile forces to their
        // fail-closed values (0002 section 3 mandatory outcomes). Enforcement
        // points quote the 0002A matrix column.
        if (legacy.instantRandomize)
            AddConflict(report, config.strict, "AiPlayerbot.InstantRandomize", "1", "0",
                "manager/factory entry", ConfigReasonCode::RandomizationConflict);

        if (legacy.randomGearUpgradeEnabled)
            AddConflict(report, config.strict, "AiPlayerbot.RandomGearUpgradeEnabled", "1", "0",
                "manager/actions/commands", ConfigReasonCode::RandomizationConflict);

        if (legacy.rndBotCheatMask != 0)
            AddConflict(report, config.strict, "AiPlayerbot.RndBotCheats",
                std::to_string(legacy.rndBotCheatMask), "0", "AI init/config resolution",
                ConfigReasonCode::CheatMaskConflict);

        if (legacy.xpRate != 1.0f)
            AddConflict(report, config.strict, "AiPlayerbot.XPRate",
                FormatFloatStable(legacy.xpRate), "1", "config + XP action",
                ConfigReasonCode::XpRateConflict);

        if (legacy.syncQuestWithPlayer)
            AddConflict(report, config.strict, "AiPlayerbot.SyncQuestWithPlayer", "1", "0",
                "quest-giver action", ConfigReasonCode::QuestSyncConflict);

        if (legacy.syncQuestForPlayer)
            AddConflict(report, config.strict, "AiPlayerbot.SyncQuestForPlayer", "1", "0",
                "quest-giver action", ConfigReasonCode::QuestSyncConflict);

        if (legacy.preQuests)
            AddConflict(report, config.strict, "AiPlayerbot.PreQuests", "1", "0",
                "bootstrap/factory", ConfigReasonCode::QuestFabricationConflict);

        if (legacy.configuredQuestRewards)
            AddConflict(report, config.strict, "AiPlayerbot.RandomBotQuestIds", "set", "empty",
                "bootstrap/factory", ConfigReasonCode::QuestFabricationConflict);

        if (legacy.syncLevelWithPlayers)
            AddConflict(report, config.strict, "AiPlayerbot.SyncLevelWithPlayers", "1", "0",
                "login/AI update", ConfigReasonCode::LevelSyncConflict);

        if (legacy.syncAltLevelToMaster)
            AddConflict(report, config.strict, "AiPlayerbot.SyncAltLevelToMaster", "1", "0",
                "login/AI update", ConfigReasonCode::LevelSyncConflict);

        // Exact match: TrainerAction recognizes only the literal lowercase "free",
        // so the report must not flag values the runtime would not treat as free.
        if (legacy.autoTrainSpells == "free")
            AddConflict(report, config.strict, "AiPlayerbot.AutoTrainSpells",
                legacy.autoTrainSpells, "no", "trainer action/config",
                ConfigReasonCode::FreeLearningConflict);

        if (legacy.autoLearnTrainerSpells)
            AddConflict(report, config.strict, "AiPlayerbot.AutoLearnTrainerSpells", "1", "0",
                "auto-learn actions", ConfigReasonCode::FreeLearningConflict);

        if (legacy.autoLearnQuestSpells)
            AddConflict(report, config.strict, "AiPlayerbot.AutoLearnQuestSpells", "1", "0",
                "auto-learn actions", ConfigReasonCode::FreeLearningConflict);

        if (legacy.autoLearnDroppedSpells)
            AddConflict(report, config.strict, "AiPlayerbot.AutoLearnDroppedSpells", "1", "0",
                "auto-learn actions", ConfigReasonCode::FreeLearningConflict);

        if (legacy.autoEnchantUpgradeLoot)
            AddConflict(report, config.strict, "AiPlayerbot.AutoEnchantUpgradeLoot", "1", "0",
                "equip/repair/buy/RPG hooks", ConfigReasonCode::EnchantConflict);

        if (legacy.worldBuffCount != 0)
            AddConflict(report, config.strict, "AiPlayerbot.WorldBuff*",
                std::to_string(legacy.worldBuffCount), "0", "world-buff action",
                ConfigReasonCode::WorldBuffConflict);

        if (legacy.enableRandomTeleports)
            AddConflict(report, config.strict, "AiPlayerbot.EnableRandomTeleports", "1", "0",
                "manager/teleport", ConfigReasonCode::TeleportConflict);

        if (legacy.randomBotTeleportNearPlayer)
            AddConflict(report, config.strict, "AiPlayerbot.RandomBotTeleportNearPlayer", "1", "0",
                "manager", ConfigReasonCode::TeleportConflict);

        if (legacy.enableMinimalMove)
            AddConflict(report, config.strict, "AiPlayerbot.EnableMinimalMove", "1", "0",
                "movement action", ConfigReasonCode::TeleportConflict);

        if (legacy.transportTeleportType != 0)
            AddConflict(report, config.strict, "AiPlayerbot.TransportTeleportType",
                std::to_string(legacy.transportTeleportType), "0", "transport action",
                ConfigReasonCode::TransportConflict);

        if (legacy.randomBotTimedLogout)
            AddConflict(report, config.strict, "AiPlayerbot.RandomBotTimedLogout", "1", "0",
                "login/logout callbacks and criteria", ConfigReasonCode::TimedRotationConflict);

        if (legacy.randomBotTimedOffline)
            AddConflict(report, config.strict, "AiPlayerbot.RandomBotTimedOffline", "1", "0",
                "login/logout callbacks and criteria", ConfigReasonCode::TimedRotationConflict);

        if (legacy.nonGmFreeSummon)
            AddConflict(report, config.strict, "AiPlayerbot.NonGmFreeSummon", "1", "0",
                "summon action", ConfigReasonCode::FreeSummonConflict);

        if (legacy.summonAtInnkeepersEnabled)
            AddConflict(report, config.strict, "AiPlayerbot.SummonAtInnkeepersEnabled", "1", "0",
                "summon action", ConfigReasonCode::FreeSummonConflict);

        // Hard 0.1 prerequisites: these cannot be overridden fail-closed, so they
        // block regardless of strict mode. Phase 0 models them without enforcing.
        if (!legacy.asyncBotLogin)
            AddEntry(report, "AiPlayerbot.AsyncBotLogin", "0", "1",
                "manager dispatch/startup validation", ConfigClassification::MissingPrerequisite,
                ConfigSeverity::Blocking, ConfigReasonCode::AsyncBotLoginRequired, true);

        // Population provenance is a hard prerequisite with an explicit unknown
        // state. Only a verified Clean value passes silently; "not inspected" and
        // any out-of-range value block, so corruption can never read as clean.
        switch (legacy.populationInspection)
        {
            case PopulationInspection::Clean:
                break;
            case PopulationInspection::MixedDetected:
                AddEntry(report, "LivingRealm.Population", "mixed", "managed-only",
                    "startup validation", ConfigClassification::MissingPrerequisite,
                    ConfigSeverity::Blocking, ConfigReasonCode::MixedPopulationDetected, true);
                break;
            case PopulationInspection::NotInspected:
            default:
                AddEntry(report, "LivingRealm.Population", "not-inspected", "managed-only",
                    "startup validation", ConfigClassification::MissingPrerequisite,
                    ConfigSeverity::Blocking, ConfigReasonCode::PopulationNotInspected, true);
                break;
        }

        if (!legacy.livingSchemaPresent)
            AddEntry(report, "LivingRealm.Schema", "missing", "clean",
                "schema validation", ConfigClassification::MissingPrerequisite,
                ConfigSeverity::Blocking, ConfigReasonCode::LivingSchemaMissing, true);

        return report;
    }

    char const* ToString(ConfigClassification value)
    {
        switch (value)
        {
            case ConfigClassification::Informational: return "Informational";
            case ConfigClassification::Compatible: return "Compatible";
            case ConfigClassification::OverrideRequired: return "OverrideRequired";
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
            case ConfigReasonCode::QuestFabricationConflict: return "QUEST_FABRICATION_CONFLICT";
            case ConfigReasonCode::LevelSyncConflict: return "LEVEL_SYNC_CONFLICT";
            case ConfigReasonCode::FreeLearningConflict: return "FREE_LEARNING_CONFLICT";
            case ConfigReasonCode::TeleportConflict: return "TELEPORT_CONFLICT";
            case ConfigReasonCode::TransportConflict: return "TRANSPORT_CONFLICT";
            case ConfigReasonCode::TimedRotationConflict: return "TIMED_ROTATION_CONFLICT";
            case ConfigReasonCode::FreeSummonConflict: return "FREE_SUMMON_CONFLICT";
            case ConfigReasonCode::EnchantConflict: return "ENCHANT_CONFLICT";
            case ConfigReasonCode::WorldBuffConflict: return "WORLD_BUFF_CONFLICT";
            case ConfigReasonCode::AsyncBotLoginRequired: return "ASYNC_BOT_LOGIN_REQUIRED";
            case ConfigReasonCode::PopulationNotInspected: return "POPULATION_NOT_INSPECTED";
            case ConfigReasonCode::MixedPopulationDetected: return "MIXED_POPULATION_DETECTED";
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
        text += " enforcement=";
        text += entry.enforcementPoint;
        text += entry.blocking ? " blocking=1" : " blocking=0";
        return text;
    }
}
