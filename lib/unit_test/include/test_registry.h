#ifndef TEST_REGISTRY_H
#define TEST_REGISTRY_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Test case structure
typedef struct TestCase {
    const char* suite_name;
    const char* test_name;
    void (*test_func)(void);
    void (*setup_func)(void);
    void (*teardown_func)(void);
    bool enabled;
    struct TestCase* next;
} TestCase;

// Test registry functions
void test_registry_init(void);
void test_registry_cleanup(void);
void test_registry_register(TestCase* test);
TestCase* test_registry_get_tests(void);
TestCase* test_registry_filter_tests(const char* filter);
size_t test_registry_count_tests(void);

// Test case creation helper
TestCase* test_case_create(const char* suite_name, const char* test_name, 
                          void (*test_func)(void), void (*setup_func)(void), 
                          void (*teardown_func)(void));

#ifdef __cplusplus
}
#endif

#endif // TEST_REGISTRY_H
