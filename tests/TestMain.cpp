#include "TestHarness.h"

#include <exception>
#include <iostream>

int main() {
    const auto& tests = radiary::test::Registry();
    int failed = 0;

    for (const auto& test : tests) {
        try {
            test.run();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& exception) {
            ++failed;
            std::cerr << "[FAIL] " << test.name << '\n';
            std::cerr << "  " << exception.what() << '\n';
        } catch (...) {
            ++failed;
            std::cerr << "[FAIL] " << test.name << '\n';
            std::cerr << "  unknown exception\n";
        }
    }

    if (failed != 0) {
        std::cerr << failed << " test(s) failed\n";
        return 1;
    }

    std::cout << tests.size() << " test(s) passed\n";
    return 0;
}
