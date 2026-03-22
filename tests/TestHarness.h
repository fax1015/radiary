#pragma once

#include <cmath>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace radiary::test {

struct TestCase {
    std::string name;
    std::function<void()> run;
};

inline std::vector<TestCase>& Registry() {
    static std::vector<TestCase> registry;
    return registry;
}

struct TestRegistration {
    TestRegistration(std::string testName, std::function<void()> testRun) {
        Registry().push_back({std::move(testName), std::move(testRun)});
    }
};

[[noreturn]] inline void Fail(const char* file, int line, const std::string& message) {
    std::ostringstream stream;
    stream << file << ':' << line << ": " << message;
    throw std::runtime_error(stream.str());
}

inline bool AlmostEqual(const double left, const double right, const double epsilon = 1e-9) {
    return std::fabs(left - right) <= epsilon;
}

}  // namespace radiary::test

#define RADIARY_TEST(name) \
    static void name(); \
    static ::radiary::test::TestRegistration name##_registration(#name, &name); \
    static void name()

#define RADIARY_CHECK(condition) \
    do { \
        if (!(condition)) { \
            ::radiary::test::Fail(__FILE__, __LINE__, "CHECK failed: " #condition); \
        } \
    } while (false)

#define RADIARY_CHECK_EQ(left, right) \
    do { \
        const auto& leftValue = (left); \
        const auto& rightValue = (right); \
        if (!(leftValue == rightValue)) { \
            std::ostringstream stream; \
            stream << "CHECK_EQ failed: " #left " != " #right; \
            ::radiary::test::Fail(__FILE__, __LINE__, stream.str()); \
        } \
    } while (false)

#define RADIARY_CHECK_NEAR(left, right, epsilon) \
    do { \
        const double leftValue = static_cast<double>(left); \
        const double rightValue = static_cast<double>(right); \
        if (!::radiary::test::AlmostEqual(leftValue, rightValue, static_cast<double>(epsilon))) { \
            std::ostringstream stream; \
            stream << "CHECK_NEAR failed: " #left " != " #right; \
            ::radiary::test::Fail(__FILE__, __LINE__, stream.str()); \
        } \
    } while (false)
