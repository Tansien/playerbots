#pragma once

#include <string>
#include <vector>

// Minimal host-side test harness for the playerbots_living_tests CTest target.
// Standard C++ only: no core headers, no live game objects, no framework
// dependency (0001A A.4). Tests self-register via LIVING_TEST and fail by
// throwing; the runner in LivingTestMain.cpp reports and returns non-zero.

namespace living { namespace test
{
    struct Failure
    {
        std::string file;
        int line = 0;
        std::string expression;
    };

    using TestFn = void (*)();

    struct TestCase
    {
        char const* name;
        TestFn fn;
    };

    std::vector<TestCase>& Registry();

    struct Registrar
    {
        Registrar(char const* name, TestFn fn);
    };

    [[noreturn]] void Fail(char const* file, int line, char const* expression);
}}

#define LIVING_TEST(testName) \
    static void testName(); \
    static living::test::Registrar livingTestRegistrar_##testName(#testName, &testName); \
    static void testName()

#define LIVING_CHECK(expression) \
    do { if (!(expression)) living::test::Fail(__FILE__, __LINE__, #expression); } while (0)
