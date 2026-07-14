#include "LivingTest.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>

namespace living { namespace test
{
    std::vector<TestCase>& Registry()
    {
        static std::vector<TestCase> registry;
        return registry;
    }

    Registrar::Registrar(char const* name, TestFn fn)
    {
        Registry().push_back({ name, fn });
    }

    void Fail(char const* file, int line, char const* expression)
    {
        Failure failure;
        failure.file = file;
        failure.line = line;
        failure.expression = expression;
        throw failure;
    }
}}

int main()
{
    using namespace living::test;

    std::vector<TestCase> tests = Registry();
    if (tests.empty())
    {
        std::fprintf(stderr, "playerbots_living_tests: no tests registered\n");
        return 1;
    }

    // Static-initialization order across translation units is unspecified; sort by
    // name so the run order (and output) is deterministic.
    std::sort(tests.begin(), tests.end(),
        [](TestCase const& a, TestCase const& b) { return std::strcmp(a.name, b.name) < 0; });

    int failed = 0;
    for (TestCase const& test : tests)
    {
        try
        {
            test.fn();
            std::printf("PASS %s\n", test.name);
        }
        catch (Failure const& failure)
        {
            ++failed;
            std::printf("FAIL %s\n     %s:%d: LIVING_CHECK(%s)\n",
                test.name, failure.file.c_str(), failure.line, failure.expression.c_str());
        }
        catch (std::exception const& e)
        {
            ++failed;
            std::printf("FAIL %s\n     unexpected exception: %s\n", test.name, e.what());
        }
    }

    std::printf("%zu tests, %d failed\n", tests.size(), failed);
    return failed == 0 ? 0 : 1;
}
