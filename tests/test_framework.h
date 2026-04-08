#pragma once

#include <cmath>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sakura::tests
{

struct TestFailure : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

struct TestCase
{
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& Registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar
{
    Registrar(const char* name, std::function<void()> fn)
    {
        Registry().push_back(TestCase{ name, std::move(fn) });
    }
};

inline void Fail(const char* expr, const char* file, int line, const std::string& detail = {})
{
    std::ostringstream oss;
    oss << file << ':' << line << "  REQUIRE failed: " << expr;
    if (!detail.empty())
        oss << " (" << detail << ')';
    throw TestFailure(oss.str());
}

namespace Matchers
{

struct WithinAbsMatcher
{
    double expected;
    double tolerance;

    bool Match(double actual) const
    {
        return std::fabs(actual - expected) <= tolerance;
    }

    std::string Describe() const
    {
        std::ostringstream oss;
        oss << "expected " << expected << " +/- " << tolerance;
        return oss.str();
    }
};

struct WithinRelMatcher
{
    double expected;
    double tolerance;

    bool Match(double actual) const
    {
        double scale = std::max(std::fabs(expected), 1.0);
        return std::fabs(actual - expected) <= scale * tolerance;
    }

    std::string Describe() const
    {
        std::ostringstream oss;
        oss << "expected " << expected << " within relative " << tolerance;
        return oss.str();
    }
};

inline WithinAbsMatcher WithinAbs(double expected, double tolerance)
{
    return WithinAbsMatcher{ expected, tolerance };
}

inline WithinRelMatcher WithinRel(double expected, double tolerance)
{
    return WithinRelMatcher{ expected, tolerance };
}

} // namespace Matchers

template <typename TMatcher>
inline void RequireThat(double actual, const TMatcher& matcher,
                        const char* expr, const char* file, int line)
{
    if (!matcher.Match(actual))
    {
        std::ostringstream oss;
        oss << expr << " = " << actual << ", " << matcher.Describe();
        Fail(expr, file, line, oss.str());
    }
}

} // namespace sakura::tests

#define SAKURA_TEST_CONCAT_INNER(a, b) a##b
#define SAKURA_TEST_CONCAT(a, b) SAKURA_TEST_CONCAT_INNER(a, b)

#define TEST_CASE(name, tags) \
    static void SAKURA_TEST_CONCAT(TestFunc_, __LINE__)(); \
    static ::sakura::tests::Registrar SAKURA_TEST_CONCAT(TestReg_, __LINE__)( \
        name, SAKURA_TEST_CONCAT(TestFunc_, __LINE__)); \
    static void SAKURA_TEST_CONCAT(TestFunc_, __LINE__)()

#define REQUIRE(expr) \
    [&]() { \
        const bool sakuraRequirementSatisfied = static_cast<bool>(expr); \
        if (!sakuraRequirementSatisfied) \
            ::sakura::tests::Fail(#expr, __FILE__, __LINE__); \
    }()

#define REQUIRE_NOTHROW(expr) \
    [&]() { \
        try { \
            expr; \
        } catch (const std::exception& exceptionObject) { \
            ::sakura::tests::Fail(#expr, __FILE__, __LINE__, exceptionObject.what()); \
        } catch (...) { \
            ::sakura::tests::Fail(#expr, __FILE__, __LINE__, "threw unknown exception"); \
        } \
    }()

#define REQUIRE_THAT(actual, matcher) \
    [&]() { \
        ::sakura::tests::RequireThat(static_cast<double>(actual), matcher, #actual, __FILE__, __LINE__); \
    }()
