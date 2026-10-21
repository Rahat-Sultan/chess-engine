#pragma once
/**
 * test_framework.hpp — Minimal self-contained test framework
 *
 * Include this header in every test TU (test_*.cpp) and in test_main.cpp.
 * test_main.cpp must also define TEST_MAIN to emit the global state and main().
 *
 * Usage in test_main.cpp:
 *   #define TEST_MAIN
 *   #include "test_framework.hpp"
 *
 * Usage in test_*.cpp:
 *   #include "test_framework.hpp"
 */

#include <functional>
#include <iostream>
#include <string>
#include <vector>

// ─── Shared state (defined once via TEST_MAIN, declared extern elsewhere) ─────

#ifdef TEST_MAIN
bool g_current_test_failed = false;
#else
extern bool g_current_test_failed;
#endif

// ─── Test registry ────────────────────────────────────────────────────────────

struct TestCase {
    std::string           name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& test_registry() {
    static std::vector<TestCase> reg;
    return reg;
}

struct TestRegistrar {
    TestRegistrar(const char* name, std::function<void()> fn) {
        test_registry().push_back({name, std::move(fn)});
    }
};

// ─── TEST macro ───────────────────────────────────────────────────────────────

#define TEST(suite, name) \
    static void test_##suite##_##name(); \
    static TestRegistrar reg_##suite##_##name(#suite "." #name, test_##suite##_##name); \
    static void test_##suite##_##name()

// ─── Assertion macros ─────────────────────────────────────────────────────────

#define EXPECT_EQ(a, b) do { \
    auto _a = (a); auto _b = (b); \
    if (!(_a == _b)) { \
        std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ \
                  << "  expected [" << _b << "] got [" << _a << "]\n"; \
        g_current_test_failed = true; \
    } \
} while(0)

#define EXPECT_NE(a, b) do { \
    auto _a = (a); auto _b = (b); \
    if (_a == _b) { \
        std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ \
                  << "  expected values to differ, both = [" << _a << "]\n"; \
        g_current_test_failed = true; \
    } \
} while(0)

#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { \
        std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ \
                  << "  expected true: " #expr "\n"; \
        g_current_test_failed = true; \
    } \
} while(0)

#define EXPECT_FALSE(expr) do { \
    if ((expr)) { \
        std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ \
                  << "  expected false: " #expr "\n"; \
        g_current_test_failed = true; \
    } \
} while(0)

#define EXPECT_THROW(expr, ExcType) do { \
    bool _threw = false; \
    try { (expr); } catch (const ExcType&) { _threw = true; } catch (...) {} \
    if (!_threw) { \
        std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ \
                  << "  expected exception " #ExcType " from: " #expr "\n"; \
        g_current_test_failed = true; \
    } \
} while(0)

// ─── main() — emitted only when TEST_MAIN is defined ─────────────────────────

#ifdef TEST_MAIN
int main() {
    auto& tests = test_registry();
    int passed = 0;
    int failed = 0;

    for (auto& tc : tests) {
        g_current_test_failed = false;
        std::cerr << "[ RUN  ] " << tc.name << "\n";
        try {
            tc.fn();
        } catch (const std::exception& e) {
            std::cerr << "  FAIL  uncaught exception: " << e.what() << "\n";
            g_current_test_failed = true;
        } catch (...) {
            std::cerr << "  FAIL  unknown exception\n";
            g_current_test_failed = true;
        }
        if (g_current_test_failed) {
            std::cerr << "[ FAIL ] " << tc.name << "\n";
            ++failed;
        } else {
            std::cerr << "[ PASS ] " << tc.name << "\n";
            ++passed;
        }
    }

    std::cerr << "\n=== Results: " << passed << " passed, "
              << failed << " failed ===\n";
    return (failed == 0) ? 0 : 1;
}
#endif // TEST_MAIN
