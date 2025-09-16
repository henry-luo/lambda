#ifndef UNIT_TEST_H
#define UNIT_TEST_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct TestRegistry TestRegistry;

// Test execution context
typedef struct {
    bool test_failed;
    bool in_expect;
    const char* current_file;
    int current_line;
    char* failure_message;
} TestContext;

// Global test context
extern TestContext* g_test_context;

// Core framework functions
void unit_test_init(void);
void unit_test_cleanup(void);
int unit_test_run_all(int argc, char** argv);

// Test context management
TestContext* test_context_create(void);
void test_context_destroy(TestContext* ctx);
void test_context_set_current(TestContext* ctx);

// Internal failure handling
void _test_fail(const char* file, int line, const char* format, ...);
void _test_expect_fail(const char* file, int line, const char* format, ...);

#ifdef __cplusplus
}
#endif

#endif // UNIT_TEST_H
