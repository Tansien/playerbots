#include "LivingTest.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <vector>

namespace living
{
namespace test
{
    namespace
    {
        struct TestCase
        {
            char const* name;
            TestFunction function;
        };

        std::vector<TestCase>& Registry()
        {
            // Function-local static: registration happens during static
            // initialization of other translation units, whose order relative
            // to a namespace-scope vector here would be unspecified.
            static std::vector<TestCase> registry;
            return registry;
        }
    }

    Registrar::Registrar(char const* name, TestFunction function)
    {
        Registry().push_back({ name, function });
    }

    void FailCheck(char const* file, int line, char const* expression)
    {
        throw CheckFailure{ file, line, expression };
    }

    namespace
    {
        void DeliberatelyFailingTest()
        {
            FailCheck(__FILE__, __LINE__, "deliberate failure injected by --verify-failure-detection");
        }
    }

    int RunAllTests(bool injectDeliberateFailure)
    {
        std::vector<TestCase> tests = Registry();

        if (injectDeliberateFailure)
            tests.push_back({ "VerifyFailureDetection_DeliberateFailure", &DeliberatelyFailingTest });

        // Registration order follows link order; sort by name so execution and
        // output order are deterministic.
        std::sort(tests.begin(), tests.end(),
            [](TestCase const& a, TestCase const& b) { return std::strcmp(a.name, b.name) < 0; });

        int failures = 0;

        if (tests.empty())
        {
            // A suite that silently stopped registering tests must not pass.
            std::printf("FAIL <registry>: no tests registered\n");
            ++failures;
        }

        for (size_t i = 0; i < tests.size(); ++i)
        {
            if (i > 0 && std::strcmp(tests[i - 1].name, tests[i].name) == 0)
            {
                // Two LIVING_TESTs with the same name link fine (the bodies are
                // translation-unit-local) but make every report ambiguous.
                ++failures;
                std::printf("FAIL %s\n     duplicate test name\n", tests[i].name);
                continue;
            }

            try
            {
                tests[i].function();
                std::printf("PASS %s\n", tests[i].name);
            }
            catch (CheckFailure const& failure)
            {
                ++failures;
                std::printf("FAIL %s\n     %s:%d: LIVING_CHECK(%s)\n",
                    tests[i].name, failure.file, failure.line, failure.expression);
            }
            catch (std::exception const& e)
            {
                ++failures;
                std::printf("FAIL %s\n     unexpected exception: %s\n", tests[i].name, e.what());
            }
            catch (...)
            {
                ++failures;
                std::printf("FAIL %s\n     unexpected non-standard exception\n", tests[i].name);
            }
        }

        std::printf("%zu tests, %d failed\n", tests.size(), failures);
        return failures == 0 ? 0 : 1;
    }
}
}

int main(int argc, char** argv)
{
    bool injectDeliberateFailure = false;

    if (argc > 1)
    {
        if (argc == 2 && std::strcmp(argv[1], "--verify-failure-detection") == 0)
        {
            injectDeliberateFailure = true;
        }
        else
        {
            std::fprintf(stderr, "usage: %s [--verify-failure-detection]\n", argv[0]);
            return 2;
        }
    }

    return living::test::RunAllTests(injectDeliberateFailure);
}
