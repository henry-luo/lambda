#include "include/criterion/criterion.h"
#include <stdio.h>

// Initialize the test registry before any tests register
__attribute__((constructor(101)))
static void init_tests(void) {
    test_registry_init();
    printf("Test registry initialized\n");
}

// Global variables for setup/teardown testing
static int setup_called = 0;
static int teardown_called = 0;

void setup(void) {
    setup_called++;
    printf("Setup called (count: %d)\n", setup_called);
}

void teardown(void) {
    teardown_called++;
    printf("Teardown called (count: %d)\n", teardown_called);
}

// Basic test without setup/teardown
Test(basic, simple_assertion) {
    cr_assert_eq(1 + 1, 2, "Basic math should work");
    cr_assert_neq(1, 2, "1 should not equal 2");
    cr_assert_gt(5, 3, "5 should be greater than 3");
}

// Test with setup/teardown
Test(basic, with_setup_teardown, .init = setup, .fini = teardown) {
    cr_assert_eq(setup_called, 1, "Setup should have been called once");
    cr_expect_not_null(&setup_called, "setup_called should not be null");
}

// String comparison test
Test(strings, string_equality) {
    const char* str1 = "hello";
    const char* str2 = "hello";
    const char* str3 = "world";
    
    cr_assert_str_eq(str1, str2, "Identical strings should be equal");
    cr_assert_str_neq(str1, str3, "Different strings should not be equal");
}

// Floating point test
Test(math, floating_point) {
    double a = 1.0;
    double b = 1.0000001;
    double c = 1.1;
    
    cr_assert_float_eq(a, b, 0.001, "Close floats should be equal within epsilon");
    cr_expect_float_eq(a, c, 0.01, "This expect should fail but not stop the test");
    cr_assert_lt(a, c, "1.0 should be less than 1.1");
}

// Pointer tests
Test(pointers, null_checks) {
    int value = 42;
    int* ptr = &value;
    int* null_ptr = NULL;
    
    cr_assert_not_null(ptr, "Valid pointer should not be null");
    cr_assert_null(null_ptr, "Null pointer should be null");
    cr_assert_eq(*ptr, 42, "Dereferenced pointer should equal 42");
}

// Test that should fail (for demonstration)
Test(demo, intentional_failure) {
    cr_assert_eq(2 + 2, 5, "This test should fail intentionally");
}
