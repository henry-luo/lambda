#include "../include/unit_test.h"
#include "../include/test_registry.h"
#include "../include/test_runner.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

// Global test context
TestContext* g_test_context = NULL;

void unit_test_init(void) {
    test_registry_init();
}

void unit_test_cleanup(void) {
    test_registry_cleanup();
    if (g_test_context) {
        test_context_destroy(g_test_context);
        g_test_context = NULL;
    }
}

int unit_test_run_all(int argc, char** argv) {
    // Don't reinitialize - tests are already registered via constructors
    
    // Parse command line arguments
    TestArgs args = test_args_parse(argc, argv);
    
    if (args.help) {
        test_args_print_help(argv[0]);
        unit_test_cleanup();
        return 0;
    }
    
    // Get tests (filtered if needed)
    TestCase* tests = test_registry_get_tests();
    size_t test_count = test_registry_count_tests();
    
    printf("Found %zu registered tests\n", test_count);
    
    if (args.list_tests) {
        printf("Available tests:\n");
        TestCase* current = tests;
        while (current) {
            printf("  %s.%s\n", current->suite_name, current->test_name);
            current = current->next;
        }
        unit_test_cleanup();
        return 0;
    }
    
    // Run tests
    printf("About to execute tests...\n");
    TestSummary* summary = test_runner_execute(tests, args.filter);
    
    printf("Tests executed, printing results...\n");
    // Print results
    test_runner_print_summary(summary);
    
    int exit_code = (summary->failed_tests > 0) ? 1 : 0;
    
    // Cleanup
    test_runner_cleanup(summary);
    unit_test_cleanup();
    
    return exit_code;
}

TestContext* test_context_create(void) {
    TestContext* ctx = malloc(sizeof(TestContext));
    if (!ctx) return NULL;
    
    ctx->test_failed = false;
    ctx->in_expect = false;
    ctx->current_file = NULL;
    ctx->current_line = 0;
    ctx->failure_message = NULL;
    
    return ctx;
}

void test_context_destroy(TestContext* ctx) {
    if (!ctx) return;
    
    if (ctx->failure_message) {
        free(ctx->failure_message);
    }
    free(ctx);
}

void test_context_set_current(TestContext* ctx) {
    g_test_context = ctx;
}

void _test_fail(const char* file, int line, const char* format, ...) {
    if (!g_test_context) return;
    
    g_test_context->test_failed = true;
    g_test_context->current_file = file;
    g_test_context->current_line = line;
    
    if (format) {
        va_list args;
        va_start(args, format);
        
        // Calculate required buffer size
        int size = vsnprintf(NULL, 0, format, args);
        va_end(args);
        
        if (size > 0) {
            g_test_context->failure_message = malloc(size + 1);
            if (g_test_context->failure_message) {
                va_start(args, format);
                vsnprintf(g_test_context->failure_message, size + 1, format, args);
                va_end(args);
            }
        }
    }
}

void _test_expect_fail(const char* file, int line, const char* format, ...) {
    if (!g_test_context) return;
    
    g_test_context->in_expect = true;
    
    // For expect, we don't fail the test immediately, just record the failure
    if (format) {
        va_list args;
        va_start(args, format);
        
        printf("EXPECTATION FAILED at %s:%d: ", file, line);
        vprintf(format, args);
        printf("\n");
        
        va_end(args);
    }
}
