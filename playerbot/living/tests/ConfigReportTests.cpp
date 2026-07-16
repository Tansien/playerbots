#include "LivingTest.h"

#include "../config/EffectiveConfigReport.h"

#include <cmath>
#include <cstring>
#include <string>

using namespace living;

namespace
{
    LegacyCompatibilityInputs CleanLegacyInputs()
    {
        LegacyCompatibilityInputs inputs;
        inputs.asyncBotLogin = true;
        inputs.populationInspection = PopulationInspection::Clean;
        inputs.livingSchemaPresent = true;
        return inputs;
    }

    LegacyCompatibilityInputs ConflictingLegacyInputs()
    {
        LegacyCompatibilityInputs inputs;
        inputs.instantRandomize = true;
        inputs.randomGearUpgradeEnabled = true;
        inputs.rndBotCheatMask = 0x25; // taxi|item|breath, the shipped default
        inputs.xpRate = 2.0f;
        inputs.syncQuestWithPlayer = true;
        inputs.syncQuestForPlayer = true;
        inputs.preQuests = true;
        inputs.configuredQuestRewards = true;
        inputs.syncLevelWithPlayers = true;
        inputs.syncAltLevelToMaster = true;
        inputs.autoTrainSpells = "free";
        inputs.autoLearnTrainerSpells = true;
        inputs.autoLearnQuestSpells = true;
        inputs.autoLearnDroppedSpells = true;
        inputs.autoEnchantUpgradeLoot = true;
        inputs.worldBuffCount = 3;
        inputs.respawnModHostile = 5.0f;
        inputs.respawnModNeutral = 10.0f;
        inputs.respawnModForPlayerBots = true;
        inputs.respawnModForInstances = true;
        inputs.enableRandomTeleports = true;
        inputs.randomBotTeleportNearPlayer = true;
        inputs.enableMinimalMove = true;
        inputs.transportTeleportType = 2;
        inputs.randomBotTimedLogout = true;
        inputs.randomBotTimedOffline = true;
        inputs.nonGmFreeSummon = true;
        inputs.summonAtInnkeepersEnabled = true;
        inputs.asyncBotLogin = false;
        inputs.populationInspection = PopulationInspection::MixedDetected;
        inputs.livingSchemaPresent = false;
        return inputs;
    }

    std::string FormatReport(EffectiveConfigReport const& report)
    {
        std::string text;
        for (EffectiveConfigEntry const& entry : report.entries)
        {
            text += FormatEffectiveConfigEntry(entry);
            text += '\n';
        }
        return text;
    }

    size_t CountReason(EffectiveConfigReport const& report, ConfigReasonCode reason)
    {
        size_t count = 0;
        for (EffectiveConfigEntry const& entry : report.entries)
            if (entry.reason == reason)
                ++count;
        return count;
    }
}

LIVING_TEST(config_parses_profiles_case_insensitively)
{
    LIVING_CHECK(ParseLivingRealmProfile("organic") == LivingRealmProfile::Organic);
    LIVING_CHECK(ParseLivingRealmProfile("Organic") == LivingRealmProfile::Organic);
    LIVING_CHECK(ParseLivingRealmProfile("ORGANIC") == LivingRealmProfile::Organic);
    LIVING_CHECK(ParseLivingRealmProfile("chaotic") == LivingRealmProfile::Unknown);
    LIVING_CHECK(ParseLivingRealmProfile("") == LivingRealmProfile::Unknown);

    LivingRealmConfig const config = LivingRealmConfig::FromValues(true, "Organic", false);
    LIVING_CHECK(config.enabled);
    LIVING_CHECK(config.profile == LivingRealmProfile::Organic);
    LIVING_CHECK(config.profileName == "Organic");
    LIVING_CHECK(!config.strict);

    LivingRealmConfig const defaults;
    LIVING_CHECK(!defaults.enabled);
    LIVING_CHECK(defaults.profile == LivingRealmProfile::Organic);
    LIVING_CHECK(defaults.strict);
}

LIVING_TEST(config_booleans_are_tri_state_and_malformed_values_block)
{
    // Every accepted spelling, ASCII case-insensitive, whitespace tolerated.
    for (char const* text : { "1", "true", "TRUE", "Yes", "on", " 1 " })
        LIVING_CHECK(ParseLivingRealmBool(text) == LivingRealmBool::True);
    for (char const* text : { "0", "false", "FALSE", "No", "off", " 0 " })
        LIVING_CHECK(ParseLivingRealmBool(text) == LivingRealmBool::False);
    // The core's GetBoolDefault would silently map all of these to false.
    for (char const* text : { "", "2", "tru", "yess", "-1", "enabled", "y" })
        LIVING_CHECK(ParseLivingRealmBool(text) == LivingRealmBool::Malformed);

    // Only the ENDS are trimmed: embedded whitespace stays malformed. Stripping
    // whitespace everywhere turned "o f f" into a valid "off" and silently
    // disabled Living Realm (or strict enforcement).
    for (char const* text : { "o f f", "o n", "t rue", "1 0", "y es", "of f", "on\toff" })
        LIVING_CHECK(ParseLivingRealmBool(text) == LivingRealmBool::Malformed);
    LIVING_CHECK(ParseLivingRealmBool("\t off \r\n") == LivingRealmBool::False); // ends only

    // A malformed Enabled blocks and does not silently switch the realm off.
    LivingRealmConfig const badEnabled = LivingRealmConfig::FromRawValues("tru", "organic", "1");
    LIVING_CHECK(badEnabled.enabledRaw == LivingRealmBool::Malformed);
    LIVING_CHECK(badEnabled.enabled); // fail-closed interpretation while the report blocks
    EffectiveConfigReport const enabledReport = BuildEffectiveConfigReport(badEnabled, CleanLegacyInputs());
    LIVING_CHECK(CountReason(enabledReport, ConfigReasonCode::MalformedBoolean) == 1);
    LIVING_CHECK(enabledReport.HasBlockingEntry());
    // The entry names the operator's actual token, not the word "malformed".
    for (EffectiveConfigEntry const& entry : enabledReport.entries)
        if (entry.reason == ConfigReasonCode::MalformedBoolean)
            LIVING_CHECK(entry.configuredValue == "tru");

    // An embedded-space value blocks rather than resolving to "off".
    LivingRealmConfig const spaced = LivingRealmConfig::FromRawValues("o f f", "organic", "1");
    LIVING_CHECK(spaced.enabledRaw == LivingRealmBool::Malformed);
    LIVING_CHECK(spaced.enabled); // not silently disabled
    LIVING_CHECK(BuildEffectiveConfigReport(spaced, CleanLegacyInputs()).HasBlockingEntry());

    // A malformed Strict blocks and does not downgrade conflicts to warnings.
    LivingRealmConfig const badStrict = LivingRealmConfig::FromRawValues("1", "organic", "2");
    LIVING_CHECK(badStrict.strictRaw == LivingRealmBool::Malformed);
    LIVING_CHECK(badStrict.strict);
    EffectiveConfigReport const strictReport = BuildEffectiveConfigReport(badStrict, CleanLegacyInputs());
    LIVING_CHECK(CountReason(strictReport, ConfigReasonCode::MalformedBoolean) == 1);
    LIVING_CHECK(strictReport.HasBlockingEntry());

    // A VALID disabled Enabled validates nothing else, including a malformed
    // Strict: the public contract is exactly one informational entry.
    LivingRealmConfig const disabledBadStrict = LivingRealmConfig::FromRawValues("0", "bogus", "nonsense");
    EffectiveConfigReport const disabledReport =
        BuildEffectiveConfigReport(disabledBadStrict, ConflictingLegacyInputs());
    LIVING_CHECK(disabledReport.entries.size() == 1);
    LIVING_CHECK(disabledReport.entries[0].reason == ConfigReasonCode::LivingRealmDisabled);
    LIVING_CHECK(!disabledReport.HasBlockingEntry());

    // Well-formed raw values behave exactly like the typed constructor.
    LivingRealmConfig const off = LivingRealmConfig::FromRawValues("0", "organic", "1");
    LIVING_CHECK(!off.enabled);
    LIVING_CHECK(!BuildEffectiveConfigReport(off, ConflictingLegacyInputs()).HasBlockingEntry());
}

LIVING_TEST(config_disabled_realm_validates_nothing_and_needs_no_schema)
{
    // LR-001: disabled mode produces one informational entry even when every legacy
    // setting conflicts and no Living Realm schema exists.
    LivingRealmConfig const config = LivingRealmConfig::FromValues(false, "organic", true);
    EffectiveConfigReport const report = BuildEffectiveConfigReport(config, ConflictingLegacyInputs());

    LIVING_CHECK(report.entries.size() == 1);
    LIVING_CHECK(report.entries[0].reason == ConfigReasonCode::LivingRealmDisabled);
    LIVING_CHECK(report.entries[0].severity == ConfigSeverity::Info);
    LIVING_CHECK(!report.entries[0].blocking);
    LIVING_CHECK(!report.HasBlockingEntry());
    LIVING_CHECK(CountReason(report, ConfigReasonCode::LivingSchemaMissing) == 0);
}

LIVING_TEST(config_unknown_profile_blocks_only_when_enabled)
{
    LegacyCompatibilityInputs const clean = CleanLegacyInputs();

    EffectiveConfigReport const disabled = BuildEffectiveConfigReport(
        LivingRealmConfig::FromValues(false, "chaotic", true), clean);
    LIVING_CHECK(!disabled.HasBlockingEntry());
    LIVING_CHECK(CountReason(disabled, ConfigReasonCode::UnknownProfile) == 0);

    EffectiveConfigReport const enabled = BuildEffectiveConfigReport(
        LivingRealmConfig::FromValues(true, "chaotic", true), clean);
    LIVING_CHECK(enabled.HasBlockingEntry());
    LIVING_CHECK(CountReason(enabled, ConfigReasonCode::UnknownProfile) == 1);
    LIVING_CHECK(enabled.entries[0].configuredValue == "chaotic");
    LIVING_CHECK(enabled.entries[0].blocking);
}

LIVING_TEST(config_valid_organic_configuration_has_no_blocking_entries)
{
    EffectiveConfigReport const report = BuildEffectiveConfigReport(
        LivingRealmConfig::FromValues(true, "organic", true), CleanLegacyInputs());

    LIVING_CHECK(!report.HasBlockingEntry());
    LIVING_CHECK(CountReason(report, ConfigReasonCode::OrganicProfileActive) == 1);
    LIVING_CHECK(report.entries.size() == 1);
}

LIVING_TEST(config_report_represents_every_required_conflict)
{
    EffectiveConfigReport const report = BuildEffectiveConfigReport(
        LivingRealmConfig::FromValues(true, "organic", true), ConflictingLegacyInputs());

    LIVING_CHECK(CountReason(report, ConfigReasonCode::RandomizationConflict) == 2);
    LIVING_CHECK(CountReason(report, ConfigReasonCode::CheatMaskConflict) == 1);
    LIVING_CHECK(CountReason(report, ConfigReasonCode::XpRateConflict) == 1);
    LIVING_CHECK(CountReason(report, ConfigReasonCode::QuestSyncConflict) == 2);
    LIVING_CHECK(CountReason(report, ConfigReasonCode::QuestFabricationConflict) == 2);
    LIVING_CHECK(CountReason(report, ConfigReasonCode::LevelSyncConflict) == 2);
    LIVING_CHECK(CountReason(report, ConfigReasonCode::FreeLearningConflict) == 4);
    LIVING_CHECK(CountReason(report, ConfigReasonCode::EnchantConflict) == 1);
    LIVING_CHECK(CountReason(report, ConfigReasonCode::WorldBuffConflict) == 1);
    LIVING_CHECK(CountReason(report, ConfigReasonCode::RespawnAccelerationConflict) == 4);

    // With BOTH respawn modifiers zero, AccelerateRespawn cannot mutate anything:
    // the scope flags alone are inert and must not block an effectively disabled
    // configuration in strict mode.
    {
        LegacyCompatibilityInputs inert = CleanLegacyInputs();
        inert.respawnModHostile = 0.0f;
        inert.respawnModNeutral = 0.0f;
        inert.respawnModForPlayerBots = true;
        inert.respawnModForInstances = true;
        EffectiveConfigReport const inertReport = BuildEffectiveConfigReport(
            LivingRealmConfig::FromValues(true, "organic", true), inert);
        LIVING_CHECK(CountReason(inertReport, ConfigReasonCode::RespawnAccelerationConflict) == 0);
        LIVING_CHECK(!inertReport.HasBlockingEntry());

        // One nonzero modifier reactivates the scope-flag conflicts.
        inert.respawnModHostile = 5.0f;
        EffectiveConfigReport const activeReport = BuildEffectiveConfigReport(
            LivingRealmConfig::FromValues(true, "organic", true), inert);
        LIVING_CHECK(CountReason(activeReport, ConfigReasonCode::RespawnAccelerationConflict) == 3);
    }
    LIVING_CHECK(CountReason(report, ConfigReasonCode::TeleportConflict) == 3);
    LIVING_CHECK(CountReason(report, ConfigReasonCode::TransportConflict) == 1);
    LIVING_CHECK(CountReason(report, ConfigReasonCode::TimedRotationConflict) == 2);
    LIVING_CHECK(CountReason(report, ConfigReasonCode::FreeSummonConflict) == 2);
    LIVING_CHECK(CountReason(report, ConfigReasonCode::AsyncBotLoginRequired) == 1);
    LIVING_CHECK(CountReason(report, ConfigReasonCode::MixedPopulationDetected) == 1);
    LIVING_CHECK(CountReason(report, ConfigReasonCode::PopulationNotInspected) == 0);
    LIVING_CHECK(CountReason(report, ConfigReasonCode::LivingSchemaMissing) == 1);

    // Every entry names its 0.1 enforcement point (design 0002 section 3).
    for (EffectiveConfigEntry const& entry : report.entries)
        LIVING_CHECK(!entry.enforcementPoint.empty());
}

LIVING_TEST(config_strict_mode_conflicts_are_deterministic)
{
    LegacyCompatibilityInputs const conflicting = ConflictingLegacyInputs();

    EffectiveConfigReport const strict = BuildEffectiveConfigReport(
        LivingRealmConfig::FromValues(true, "organic", true), conflicting);
    for (EffectiveConfigEntry const& entry : strict.entries)
    {
        if (entry.classification == ConfigClassification::Conflict)
        {
            LIVING_CHECK(entry.severity == ConfigSeverity::Blocking);
            LIVING_CHECK(entry.blocking);
        }
        LIVING_CHECK(entry.classification != ConfigClassification::OverrideRequired);
    }
    LIVING_CHECK(strict.HasBlockingEntry());

    // Non-strict mode marks overridable conflicts as overrides 0.1 must apply
    // fail-closed at runtime (Phase 0 applies nothing), but hard prerequisites
    // still block.
    EffectiveConfigReport const lenient = BuildEffectiveConfigReport(
        LivingRealmConfig::FromValues(true, "organic", false), conflicting);
    for (EffectiveConfigEntry const& entry : lenient.entries)
    {
        LIVING_CHECK(entry.classification != ConfigClassification::Conflict);
        if (entry.classification == ConfigClassification::OverrideRequired)
        {
            LIVING_CHECK(entry.severity == ConfigSeverity::Warning);
            LIVING_CHECK(!entry.blocking);
        }
        if (entry.classification == ConfigClassification::MissingPrerequisite)
            LIVING_CHECK(entry.blocking);
    }
    LIVING_CHECK(lenient.HasBlockingEntry());

    // Same inputs, same report, every time.
    LIVING_CHECK(FormatReport(strict) == FormatReport(BuildEffectiveConfigReport(
        LivingRealmConfig::FromValues(true, "organic", true), conflicting)));
}

LIVING_TEST(config_population_inspection_never_reads_clean_by_default)
{
    // "Not inspected" is an explicit blocking state, distinct from a verified
    // managed-only population; the default input never claims clean.
    LegacyCompatibilityInputs inputs = CleanLegacyInputs();
    inputs.populationInspection = PopulationInspection::NotInspected;
    EffectiveConfigReport const notInspected = BuildEffectiveConfigReport(
        LivingRealmConfig::FromValues(true, "organic", true), inputs);
    LIVING_CHECK(CountReason(notInspected, ConfigReasonCode::PopulationNotInspected) == 1);
    LIVING_CHECK(notInspected.HasBlockingEntry());

    inputs.populationInspection = PopulationInspection::MixedDetected;
    EffectiveConfigReport const mixed = BuildEffectiveConfigReport(
        LivingRealmConfig::FromValues(true, "organic", true), inputs);
    LIVING_CHECK(CountReason(mixed, ConfigReasonCode::MixedPopulationDetected) == 1);
    LIVING_CHECK(CountReason(mixed, ConfigReasonCode::PopulationNotInspected) == 0);
    LIVING_CHECK(mixed.HasBlockingEntry());

    inputs.populationInspection = PopulationInspection::Clean;
    EffectiveConfigReport const clean = BuildEffectiveConfigReport(
        LivingRealmConfig::FromValues(true, "organic", true), inputs);
    LIVING_CHECK(CountReason(clean, ConfigReasonCode::PopulationNotInspected) == 0);
    LIVING_CHECK(CountReason(clean, ConfigReasonCode::MixedPopulationDetected) == 0);
    LIVING_CHECK(!clean.HasBlockingEntry());

    LIVING_CHECK(LegacyCompatibilityInputs().populationInspection == PopulationInspection::NotInspected);

    // An out-of-range value must block like NotInspected, never pass like Clean.
    inputs.populationInspection = static_cast<PopulationInspection>(0xFF);
    EffectiveConfigReport const garbage = BuildEffectiveConfigReport(
        LivingRealmConfig::FromValues(true, "organic", true), inputs);
    LIVING_CHECK(CountReason(garbage, ConfigReasonCode::PopulationNotInspected) == 1);
    LIVING_CHECK(garbage.HasBlockingEntry());
}

LIVING_TEST(config_float_reporting_is_locale_free_and_faithful)
{
    // One ulp above 1.0 must not be reported as "1" while flagging a conflict.
    LegacyCompatibilityInputs inputs = CleanLegacyInputs();
    inputs.xpRate = std::nextafter(1.0f, 2.0f);
    EffectiveConfigReport const report = BuildEffectiveConfigReport(
        LivingRealmConfig::FromValues(true, "organic", true), inputs);

    LIVING_CHECK(CountReason(report, ConfigReasonCode::XpRateConflict) == 1);
    for (EffectiveConfigEntry const& entry : report.entries)
        if (entry.reason == ConfigReasonCode::XpRateConflict)
        {
            LIVING_CHECK(entry.configuredValue != "1");
            LIVING_CHECK(entry.configuredValue.find(',') == std::string::npos);
        }
}

LIVING_TEST(config_report_output_is_stable)
{
    EffectiveConfigReport const report = BuildEffectiveConfigReport(
        LivingRealmConfig::FromValues(false, "organic", true), CleanLegacyInputs());
    LIVING_CHECK(FormatEffectiveConfigEntry(report.entries[0]) ==
        "AiPlayerbot.LivingRealm.Enabled configured=0 effective=0 "
        "classification=Informational severity=Info reason=LIVING_REALM_DISABLED "
        "enforcement=none blocking=0");

    LegacyCompatibilityInputs inputs = CleanLegacyInputs();
    inputs.xpRate = 2.5f;
    EffectiveConfigReport const conflict = BuildEffectiveConfigReport(
        LivingRealmConfig::FromValues(true, "organic", true), inputs);
    LIVING_CHECK(FormatReport(conflict) ==
        "AiPlayerbot.LivingRealm.Profile configured=organic effective=organic "
        "classification=Compatible severity=Info reason=ORGANIC_PROFILE_ACTIVE "
        "enforcement=startup validation blocking=0\n"
        "AiPlayerbot.XPRate configured=2.5 effective=1 "
        "classification=Conflict severity=Blocking reason=XP_RATE_CONFLICT "
        "enforcement=config + XP action blocking=1\n");

    LIVING_CHECK(std::strcmp(ToString(ConfigSeverity::Blocking), "Blocking") == 0);
    LIVING_CHECK(std::strcmp(ToString(ConfigClassification::MissingPrerequisite), "MissingPrerequisite") == 0);
    LIVING_CHECK(std::strcmp(ToString(ConfigClassification::OverrideRequired), "OverrideRequired") == 0);
    LIVING_CHECK(std::strcmp(ToString(ConfigReasonCode::AsyncBotLoginRequired), "ASYNC_BOT_LOGIN_REQUIRED") == 0);
    LIVING_CHECK(std::strcmp(ToString(ConfigReasonCode::MixedPopulationDetected), "MIXED_POPULATION_DETECTED") == 0);
}
