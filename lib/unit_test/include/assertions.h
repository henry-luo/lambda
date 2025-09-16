#ifndef ASSERTIONS_H
#define ASSERTIONS_H

#include "unit_test.h"
#include <stdbool.h>
#include <string.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

// Helper functions for assertions
bool _test_float_eq(double a, double b, double epsilon);
bool _test_str_eq(const char* a, const char* b);

// Core assertion macros with optional format string
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

// Equality assertions - compatible with older C++ standards
#define cr_assert_eq(actual, expected, ...) \
    cr_assert((actual) == (expected), "Expected %s == %s", ##__VA_ARGS__)

#define cr_assert_neq(actual, expected, ...) \
    cr_assert((actual) != (expected), "Expected %s != %s", ##__VA_ARGS__)

#define cr_expect_eq(actual, expected, ...) \
    cr_expect((actual) == (expected), "Expected %s == %s", ##__VA_ARGS__)

#define cr_expect_neq(actual, expected, ...) \
    cr_expect((actual) != (expected), "Expected %s != %s", ##__VA_ARGS__)

// Comparison assertions
#define cr_assert_gt(actual, expected, ...) \
    cr_assert((actual) > (expected), "Expected %s > %s", ##__VA_ARGS__)

#define cr_assert_lt(actual, expected, ...) \
    cr_assert((actual) < (expected), "Expected %s < %s", ##__VA_ARGS__)

#define cr_assert_geq(actual, expected, ...) \
    cr_assert((actual) >= (expected), "Expected %s >= %s", ##__VA_ARGS__)

#define cr_assert_leq(actual, expected, ...) \
    cr_assert((actual) <= (expected), "Expected %s <= %s", ##__VA_ARGS__)

#define cr_expect_gt(actual, expected, ...) \
    cr_expect((actual) > (expected), "Expected %s > %s", ##__VA_ARGS__)

#define cr_expect_lt(actual, expected, ...) \
    cr_expect((actual) < (expected), "Expected %s < %s", ##__VA_ARGS__)

#define cr_expect_geq(actual, expected, ...) \
    cr_expect((actual) >= (expected), "Expected %s >= %s", ##__VA_ARGS__)

#define cr_expect_leq(actual, expected, ...) \
    cr_expect((actual) <= (expected), "Expected %s <= %s", ##__VA_ARGS__)

// Pointer assertions
#define cr_assert_null(ptr, ...) \
    cr_assert((ptr) == NULL, "Expected NULL pointer", ##__VA_ARGS__)

#define cr_assert_not_null(ptr, ...) \
    cr_assert((ptr) != NULL, "Expected non-NULL pointer", ##__VA_ARGS__)

#define cr_expect_null(ptr, ...) \
    cr_expect((ptr) == NULL, "Expected NULL pointer", ##__VA_ARGS__)

#define cr_expect_not_null(ptr, ...) \
    cr_expect((ptr) != NULL, "Expected non-NULL pointer", ##__VA_ARGS__)

// Floating-point assertions
#define cr_assert_float_eq(actual, expected, epsilon, ...) \
    cr_assert(_test_float_eq((double)(actual), (double)(expected), (double)(epsilon)), \
              "Expected %f == %f (±%f)", ##__VA_ARGS__)

#define cr_expect_float_eq(actual, expected, epsilon, ...) \
    cr_expect(_test_float_eq((double)(actual), (double)(expected), (double)(epsilon)), \
              "Expected %f == %f (±%f)", ##__VA_ARGS__)

// String assertions
#define cr_assert_str_eq(actual, expected, ...) \
    cr_assert(_test_str_eq((actual), (expected)), \
              "Expected strings to be equal", ##__VA_ARGS__)

#define cr_assert_str_neq(actual, expected, ...) \
    cr_assert(!_test_str_eq((actual), (expected)), \
              "Expected strings to be different", ##__VA_ARGS__)

#define cr_expect_str_eq(actual, expected, ...) \
    cr_expect(_test_str_eq((actual), (expected)), \
              "Expected strings to be equal", ##__VA_ARGS__)

#define cr_expect_str_neq(actual, expected, ...) \
    cr_expect(!_test_str_eq((actual), (expected)), \
              "Expected strings to be different", ##__VA_ARGS__)

// Compatibility aliases for common variations
// (removed duplicate macro definitions)

#ifdef __cplusplus
}
#endif

#endif // ASSERTIONS_H
