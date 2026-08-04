#pragma once

// Host-side test harness for the playerbots_living_tests target
// (docs/design/0001-living-realm-implementation-contract.md A.4).
//
// Living Realm host tests compile against standard C++ only: no CMaNGOS core
// headers, no legacy playerbot headers, no external test framework. A test is
// a void() function registered with LIVING_TEST; LIVING_CHECK is the assertion.
// A failing check throws CheckFailure, the runner in LivingTestMain.cpp records
// every failure with its location, and the process exits non-zero.

namespace living
{
namespace test
{
    struct CheckFailure
    {
        char const* file;
        int line;
        char const* expression;
    };

    using TestFunction = void (*)();

    // Adds a test to the registry during static initialization (via LIVING_TEST).
    struct Registrar
    {
        Registrar(char const* name, TestFunction function);
    };

    // Throws CheckFailure carrying the failing check's location.
    [[noreturn]] void FailCheck(char const* file, int line, char const* expression);

    // Runs every registered test in deterministic (name) order and reports each
    // result on stdout. Returns the process exit code: 0 only when at least one
    // test ran and none failed, 1 otherwise. With injectDeliberateFailure the
    // runner adds one always-failing test, so a correct run reports and counts
    // that failure and exits exactly 1; the CTest companion
    // playerbots_living_tests_detect_failure runs this mode through
    // VerifyFailureDetection.cmake, which requires that exact contract to prove
    // end to end that failure accounting still turns a run red.
    int RunAllTests(bool injectDeliberateFailure);
}
}

#define LIVING_TEST(name) \
    static void LivingTestBody_##name(); \
    static ::living::test::Registrar livingTestRegistrar_##name(#name, &LivingTestBody_##name); \
    static void LivingTestBody_##name()

#define LIVING_CHECK(expression) \
    do \
    { \
        if (!(expression)) \
            ::living::test::FailCheck(__FILE__, __LINE__, #expression); \
    } while (0)
