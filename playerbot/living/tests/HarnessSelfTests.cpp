#include "LivingTest.h"

#include <cstring>

// Self-checks for the harness: every other Living Realm host test depends on
// LIVING_CHECK both passing and failing correctly.

LIVING_TEST(LivingCheckPassesOnTrueCondition)
{
    LIVING_CHECK(2 + 2 == 4);
}

LIVING_TEST(LivingCheckFailureCarriesLocationAndExpression)
{
    bool verified = false;
    int expectedLine = 0;

    try
    {
        expectedLine = __LINE__; LIVING_CHECK(2 + 2 == 5);
    }
    catch (living::test::CheckFailure const& failure)
    {
        verified = failure.file != nullptr
            && std::strstr(failure.file, "HarnessSelfTests.cpp") != nullptr
            && failure.line == expectedLine
            && std::strcmp(failure.expression, "2 + 2 == 5") == 0;
    }

    // Deliberately not LIVING_CHECK: this test is what proves a failing
    // LIVING_CHECK still throws, so its own verdict cannot rely on the macro.
    if (!verified)
        throw living::test::CheckFailure{ __FILE__, __LINE__,
            "a failing LIVING_CHECK must throw a CheckFailure carrying this file, its line, and its expression" };
}
