#include "include/unit_test.h"
#include "include/test_registry.h"
#include "include/test_runner.h"
#include "include/assertions.h"
#include <stdio.h>
#include <string.h>

// Simple test function
void test_basic_math(void) {
    printf("Running basic math test\n");
    if (1 + 1 != 2) {
        printf("FAIL: 1 + 1 should equal 2\n");
        return;
    }
    printf("PASS: Basic math works\n");
}

void test_string_compare(void) {
    printf("Running string compare test\n");
    const char* a = "hello";
    const char* b = "hello";
    if (strcmp(a, b) != 0) {
        printf("FAIL: Strings should be equal\n");
        return;
    }
    printf("PASS: String comparison works\n");
}

int main(void) {
    printf("=== Simple Unit Test Framework Demo ===\n");
    
    // Initialize registry
    test_registry_init();
    
    // Manually register tests
    TestCase* test1 = test_case_create("math", "basic", test_basic_math, NULL, NULL);
    TestCase* test2 = test_case_create("strings", "compare", test_string_compare, NULL, NULL);
    
    test_registry_register(test1);
    test_registry_register(test2);
    
    printf("Registered %zu tests\n", test_registry_count_tests());
    
    // Get and run tests
    TestCase* tests = test_registry_get_tests();
    TestSummary* summary = test_runner_execute(tests, NULL);
    
    if (summary) {
        test_runner_print_summary(summary);
        test_runner_cleanup(summary);
    }
    
    test_registry_cleanup();
    return 0;
}
