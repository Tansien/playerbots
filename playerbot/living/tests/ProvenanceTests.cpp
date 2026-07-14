#include "LivingTest.h"

#include "../policy/BotProvenance.h"
#include "../policy/OrganicPolicy.h"

#include <cstring>

using namespace living;

namespace
{
    OrganicRequest EnabledOrganicRequest(OrganicActionKind kind, BotProvenance provenance)
    {
        OrganicRequest request;
        request.kind = kind;
        request.characterGuid = 2000;
        request.provenance = provenance;
        request.livingRealmEnabled = true;
        request.organicProfile = true;
        return request;
    }
}

LIVING_TEST(provenance_names_are_stable)
{
    LIVING_CHECK(std::strcmp(ToString(BotProvenance::ORGANIC_CREATED), "ORGANIC_CREATED") == 0);
    LIVING_CHECK(std::strcmp(ToString(BotProvenance::FIXTURE), "FIXTURE") == 0);
    LIVING_CHECK(std::strcmp(ToString(BotProvenance::LEGACY_UNMANAGED), "LEGACY_UNMANAGED") == 0);
}

LIVING_TEST(provenance_fixture_cannot_satisfy_organic_provenance)
{
    LIVING_CHECK(SatisfiesOrganicProvenance(BotProvenance::ORGANIC_CREATED));
    LIVING_CHECK(!SatisfiesOrganicProvenance(BotProvenance::FIXTURE));
    LIVING_CHECK(!SatisfiesOrganicProvenance(BotProvenance::LEGACY_UNMANAGED));
}

LIVING_TEST(provenance_transitions_never_relabel_identities)
{
    // LEGACY_UNMANAGED cannot silently become ORGANIC_CREATED, and FIXTURE can
    // never be promoted; a new provenance requires a new identity nonce.
    LIVING_CHECK(!IsAllowedProvenanceTransition(BotProvenance::LEGACY_UNMANAGED, BotProvenance::ORGANIC_CREATED));
    LIVING_CHECK(!IsAllowedProvenanceTransition(BotProvenance::FIXTURE, BotProvenance::ORGANIC_CREATED));
    LIVING_CHECK(!IsAllowedProvenanceTransition(BotProvenance::ORGANIC_CREATED, BotProvenance::FIXTURE));
    LIVING_CHECK(!IsAllowedProvenanceTransition(BotProvenance::ORGANIC_CREATED, BotProvenance::LEGACY_UNMANAGED));

    LIVING_CHECK(IsAllowedProvenanceTransition(BotProvenance::ORGANIC_CREATED, BotProvenance::ORGANIC_CREATED));
    LIVING_CHECK(IsAllowedProvenanceTransition(BotProvenance::FIXTURE, BotProvenance::FIXTURE));

    LIVING_CHECK(!IsAllowedProvenanceTransition(BotProvenance::Count, BotProvenance::Count));
}

LIVING_TEST(provenance_fixture_actions_denied_outside_test_profile)
{
    // Production Organic realm: fixture provisioning is denied no matter the
    // provenance or source.
    OrganicRequest production = EnabledOrganicRequest(OrganicActionKind::FIXTURE_PROVISION,
        BotProvenance::ORGANIC_CREATED);
    production.source = OrganicSourceKind::TestFixture;
    OrganicPolicyResult const denied = EvaluateOrganicPolicy(production);
    LIVING_CHECK(denied.decision == OrganicDecision::Deny);
    LIVING_CHECK(denied.reason == OrganicReasonCode::FixtureOutsideTestProfile);

    // Test profile, but a production Organic identity: still denied. Fixture-only
    // actions can never be authorized for production Organic identities.
    OrganicRequest organicInTestProfile = production;
    organicInTestProfile.fixtureTestProfile = true;
    OrganicPolicyResult const wrongProvenance = EvaluateOrganicPolicy(organicInTestProfile);
    LIVING_CHECK(wrongProvenance.decision == OrganicDecision::Deny);
    LIVING_CHECK(wrongProvenance.reason == OrganicReasonCode::FixtureProvenanceRequired);

    // Test profile with a FIXTURE identity: authorized as automation.
    OrganicRequest fixture = EnabledOrganicRequest(OrganicActionKind::FIXTURE_PROVISION, BotProvenance::FIXTURE);
    fixture.source = OrganicSourceKind::TestFixture;
    fixture.fixtureTestProfile = true;
    OrganicPolicyResult const allowed = EvaluateOrganicPolicy(fixture);
    LIVING_CHECK(allowed.decision == OrganicDecision::AllowAutomation);
    LIVING_CHECK(allowed.reason == OrganicReasonCode::FixtureAuthorized);

    // The same boundary holds for the other fixture-only rows.
    OrganicRequest cheatOverride = EnabledOrganicRequest(OrganicActionKind::CHEAT_RUNTIME_OVERRIDE,
        BotProvenance::ORGANIC_CREATED);
    LIVING_CHECK(EvaluateOrganicPolicy(cheatOverride).decision == OrganicDecision::Deny);

    OrganicRequest maintenance = EnabledOrganicRequest(OrganicActionKind::BROAD_MAINTENANCE_COMMAND,
        BotProvenance::ORGANIC_CREATED);
    LIVING_CHECK(EvaluateOrganicPolicy(maintenance).decision == OrganicDecision::Deny);
}

LIVING_TEST(provenance_fixture_identity_cannot_enter_production_semantics)
{
    // A FIXTURE identity performing even ordinary gameplay in production Organic
    // semantics is denied: fixtures stay inside the test profile.
    OrganicPolicyResult const loot = EvaluateOrganicPolicy(
        EnabledOrganicRequest(OrganicActionKind::GAMEPLAY_LOOT, BotProvenance::FIXTURE));
    LIVING_CHECK(loot.decision == OrganicDecision::Deny);
    LIVING_CHECK(loot.reason == OrganicReasonCode::FixtureOutsideTestProfile);

    OrganicPolicyResult const trainer = EvaluateOrganicPolicy(
        EnabledOrganicRequest(OrganicActionKind::TRAINER_PURCHASE, BotProvenance::FIXTURE));
    LIVING_CHECK(trainer.decision == OrganicDecision::Deny);
}

LIVING_TEST(provenance_legacy_unmanaged_cannot_use_managed_organic_semantics)
{
    OrganicPolicyResult const result = EvaluateOrganicPolicy(
        EnabledOrganicRequest(OrganicActionKind::GAMEPLAY_LOOT, BotProvenance::LEGACY_UNMANAGED));
    LIVING_CHECK(result.decision == OrganicDecision::Deny);
    LIVING_CHECK(result.reason == OrganicReasonCode::OrganicProvenanceRequired);
}

LIVING_TEST(provenance_invalid_values_fail_closed)
{
    // Corrupted or out-of-range provenance (bad cast, corrupted persisted input) is
    // never an allow: only ORGANIC_CREATED - or FIXTURE inside the test profile -
    // reaches the classification switch.
    OrganicRequest request = EnabledOrganicRequest(OrganicActionKind::GAMEPLAY_LOOT, BotProvenance::Count);
    OrganicPolicyResult const atCount = EvaluateOrganicPolicy(request);
    LIVING_CHECK(atCount.decision == OrganicDecision::Deny);
    LIVING_CHECK(atCount.reason == OrganicReasonCode::InvalidProvenance);

    request.provenance = static_cast<BotProvenance>(0xFF);
    OrganicPolicyResult const garbage = EvaluateOrganicPolicy(request);
    LIVING_CHECK(garbage.decision == OrganicDecision::Deny);
    LIVING_CHECK(garbage.reason == OrganicReasonCode::InvalidProvenance);

    // The fixture test profile does not rescue an invalid value.
    request.fixtureTestProfile = true;
    LIVING_CHECK(EvaluateOrganicPolicy(request).decision == OrganicDecision::Deny);

    // Automation and audited kinds fail closed the same way.
    OrganicRequest trainer = EnabledOrganicRequest(OrganicActionKind::TRAINER_PURCHASE,
        static_cast<BotProvenance>(0xFF));
    LIVING_CHECK(EvaluateOrganicPolicy(trainer).decision == OrganicDecision::Deny);

    // Disabled Living Realm remains a legacy passthrough regardless (LR-001).
    request.livingRealmEnabled = false;
    OrganicPolicyResult const disabled = EvaluateOrganicPolicy(request);
    LIVING_CHECK(disabled.decision != OrganicDecision::Deny);
    LIVING_CHECK(disabled.reason == OrganicReasonCode::LegacyPassthrough);
}
