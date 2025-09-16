#ifndef CRITERION_CRITERION_H
#define CRITERION_CRITERION_H

// Prevent system Criterion headers from being included
#define CRITERION_NEW_ASSERT_H
#define CRITERION_INTERNAL_NEW_ASSERTS_H
#define CRITERION_LOGGING_H
#define CRITERION_PARAMETERIZED_H

// Criterion compatibility header
// This provides a drop-in replacement for <criterion/criterion.h>

// Suppress variadic macro warnings for older C++ standards
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wvariadic-macro-arguments-omitted"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wvariadic-macros"
#endif

#include "../unit_test.h"
#include "../test_registry.h"
#include "../test_runner.h"
#include "../assertions.h"

#ifdef __cplusplus
extern "C" {
#endif

// Default empty setup/teardown functions
void _empty_setup(void) {}
void _empty_teardown(void) {}

// Test definition macro with automatic registration
// Use a helper macro to handle optional arguments cleanly
#define Test(...) TEST_IMPL(__VA_ARGS__, unused, unused, unused)
#define TEST_IMPL(suite, name, arg3, arg4, ...) \
    static void _test_##suite##_##name(void); \
    __attribute__((constructor)) \
    static void _register_##suite##_##name(void) { \
        TestCase* test = test_case_create(#suite, #name, \
                                         _test_##suite##_##name, \
                                         _empty_setup, \
                                         _empty_teardown); \
        test_registry_register(test); \
    } \
    static void _test_##suite##_##name(void)

// Test suite definition (for setup/teardown inheritance)
#define TestSuite(suite, ...) \
    static void _suite_setup_##suite(void) {} \
    static void _suite_teardown_##suite(void) {}

// Setup/teardown function attributes (parsed from Test macro)
#define PARSE_INIT_FINI(...) __VA_ARGS__

// Main test runner entry point
#define CRITERION_MAIN() \
    int main(int argc, char** argv) { \
        return unit_test_run_all(argc, argv); \
    }

// Automatic main function if not defined elsewhere
#ifndef NO_CRITERION_MAIN
__attribute__((weak))
int main(int argc, char** argv) {
    return unit_test_run_all(argc, argv);
}
#endif

// Note: Removed problematic eq/ne/lt/le/gt/ge macros that conflict with C++ standard library
// Tests should use direct comparison operators or cr_assert_* macros instead

// Skip macro for test skipping
#define cr_skip(msg, ...) \
    do { \
        printf("SKIP: " msg "\n", ##__VA_ARGS__); \
        return; \
    } while(0)

// Logging macros for compatibility
#define cr_log_info(fmt, ...) printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#define cr_log_warn(fmt, ...) printf("[WARN] " fmt "\n", ##__VA_ARGS__)
#define cr_log_error(fmt, ...) printf("[ERROR] " fmt "\n", ##__VA_ARGS__)

// Skip test macro for compatibility
#define cr_skip_test(msg, ...) \
    do { \
        printf("SKIP: " msg "\n", ##__VA_ARGS__); \
        return; \
    } while(0)

// Type aliases for Criterion compatibility
#define u64 unsigned long long
#define i64 long long
#define u32 unsigned int
#define i32 int
#define u16 unsigned short
#define i16 short
#define u8 unsigned char
#define i8 char
#define f32 float
#define f64 double

#ifdef __cplusplus
}
#endif

// Override system Criterion macros to prevent conflicts
#ifdef cr_assert
#undef cr_assert
#endif
#ifdef cr_assert_eq
#undef cr_assert_eq
#endif
#ifdef cr_assert_neq
#undef cr_assert_neq
#endif
#ifdef cr_expect
#undef cr_expect
#endif
#ifdef cr_expect_eq
#undef cr_expect_eq
#endif
#ifdef cr_expect_neq
#undef cr_expect_neq
#endif

// Redefine our macros to ensure they take precedence
// Core assertion macros
#define cr_assert(condition, ...) \
    do { \
        if (!(condition)) { \
            _test_fail(__FILE__, __LINE__, "Assertion failed: " #condition); \
            return; \
        } \
    } while(0)

#define cr_expect(condition, ...) \
    do { \
        if (!(condition)) { \
            _test_expect_fail(__FILE__, __LINE__, "Expectation failed: " #condition); \
        } \
    } while(0)

#define cr_expect_not(condition, ...) \
    do { \
        if ((condition)) { \
            _test_expect_fail(__FILE__, __LINE__, "Expected NOT: " #condition); \
        } \
    } while(0)

#define cr_assert_eq(actual, expected, ...) \
    cr_assert((actual) == (expected), "Expected %s == %s", ##__VA_ARGS__)

#define cr_assert_neq(actual, expected, ...) \
    cr_assert((actual) != (expected), "Expected %s != %s", ##__VA_ARGS__)

#define cr_assert_not(condition, ...) \
    do { \
        if (condition) { \
            _test_fail(__FILE__, __LINE__, "Expected " #condition " to be false"); \
            return; \
        } \
    } while(0)

#define cr_expect_eq(actual, expected, ...) \
    cr_expect((actual) == (expected), "Expected %s == %s", ##__VA_ARGS__)

#define cr_expect_neq(actual, expected, ...) \
    cr_expect((actual) != (expected), "Expected %s != %s", ##__VA_ARGS__)

// Restore previous diagnostic settings
#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#endif // CRITERION_CRITERION_H
