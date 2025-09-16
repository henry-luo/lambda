#include "../include/test_registry.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fnmatch.h>

// Global test registry
static TestCase* g_test_head = NULL;
static size_t g_test_count = 0;

void test_registry_init(void) {
    g_test_head = NULL;
    g_test_count = 0;
}

void test_registry_cleanup(void) {
    TestCase* current = g_test_head;
    while (current) {
        TestCase* next = current->next;
        free(current);
        current = next;
    }
    g_test_head = NULL;
    g_test_count = 0;
}

void test_registry_register(TestCase* test) {
    if (!test) return;
    
    test->next = g_test_head;
    g_test_head = test;
    g_test_count++;
}

TestCase* test_registry_get_tests(void) {
    return g_test_head;
}

TestCase* test_registry_filter_tests(const char* filter) {
    if (!filter || strlen(filter) == 0) {
        return g_test_head;
    }
    
    // Create a new filtered list
    TestCase* filtered_head = NULL;
    TestCase* filtered_tail = NULL;
    
    TestCase* current = g_test_head;
    while (current) {
        // Create test name pattern: suite.test
        char test_pattern[512];
        snprintf(test_pattern, sizeof(test_pattern), "%s.%s", 
                current->suite_name, current->test_name);
        
        // Check if filter matches (support wildcards)
        if (fnmatch(filter, test_pattern, 0) == 0 ||
            fnmatch(filter, current->test_name, 0) == 0 ||
            fnmatch(filter, current->suite_name, 0) == 0) {
            
            // Create a copy for the filtered list
            TestCase* filtered_test = test_case_create(
                current->suite_name, current->test_name,
                current->test_func, current->setup_func, current->teardown_func);
            
            if (!filtered_head) {
                filtered_head = filtered_test;
                filtered_tail = filtered_test;
            } else {
                filtered_tail->next = filtered_test;
                filtered_tail = filtered_test;
            }
        }
        current = current->next;
    }
    
    return filtered_head;
}

size_t test_registry_count_tests(void) {
    return g_test_count;
}

TestCase* test_case_create(const char* suite_name, const char* test_name,
                          void (*test_func)(void), void (*setup_func)(void),
                          void (*teardown_func)(void)) {
    TestCase* test = malloc(sizeof(TestCase));
    if (!test) return NULL;
    
    test->suite_name = suite_name;
    test->test_name = test_name;
    test->test_func = test_func;
    test->setup_func = setup_func;
    test->teardown_func = teardown_func;
    test->enabled = true;
    test->next = NULL;
    
    return test;
}
