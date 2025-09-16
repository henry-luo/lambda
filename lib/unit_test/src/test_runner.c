#include "../include/test_runner.h"
#include "../include/unit_test.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fnmatch.h>

static double get_time_seconds(void) {
    // Use simpler time measurement to avoid potential issues
    return (double)clock() / CLOCKS_PER_SEC;
}

TestSummary* test_runner_execute(TestCase* tests, const char* filter) {
    printf("test_runner_execute: Starting execution\n");
    TestSummary* summary = malloc(sizeof(TestSummary));
    if (!summary) {
        printf("test_runner_execute: Failed to allocate summary\n");
        return NULL;
    }
    
    // Initialize summary
    summary->total_tests = 0;
    summary->passed_tests = 0;
    summary->failed_tests = 0;
    summary->skipped_tests = 0;
    summary->total_time = 0.0;
    summary->reports = NULL;
    
    printf("test_runner_execute: Summary initialized\n");
    
    // Count tests first
    TestCase* current = tests;
    int test_count = 0;
    printf("test_runner_execute: Starting test count, current=%p\n", (void*)current);
    
    while (current) {
        printf("test_runner_execute: Processing test %p: %s.%s\n", 
               (void*)current, 
               current->suite_name ? current->suite_name : "NULL", 
               current->test_name ? current->test_name : "NULL");
        
        // Apply filter if provided
        if (filter) {
            char test_pattern[512];
            snprintf(test_pattern, sizeof(test_pattern), "%s.%s", 
                    current->suite_name ? current->suite_name : "unknown",
                    current->test_name ? current->test_name : "unknown");
            
            if (fnmatch(filter, test_pattern, 0) != 0 &&
                fnmatch(filter, current->test_name ? current->test_name : "", 0) != 0 &&
                fnmatch(filter, current->suite_name ? current->suite_name : "", 0) != 0) {
                current = current->next;
                continue;
            }
        }
        test_count++;
        current = current->next;
    }
    
    printf("test_runner_execute: Counted %d tests\n", test_count);
    
    if (test_count == 0) {
        printf("test_runner_execute: No tests to run\n");
        return summary;
    }
    
    // Allocate reports array
    printf("test_runner_execute: Allocating reports array for %d tests\n", test_count);
    summary->reports = malloc(sizeof(TestReport) * test_count);
    if (!summary->reports) {
        printf("test_runner_execute: Failed to allocate reports array\n");
        free(summary);
        return NULL;
    }
    printf("test_runner_execute: Reports array allocated successfully\n");
    
    printf("test_runner_execute: Getting start time\n");
    double start_time = get_time_seconds();
    printf("test_runner_execute: Start time: %f\n", start_time);
    
    // Execute tests
    printf("test_runner_execute: Starting test execution loop\n");
    current = tests;
    int report_index = 0;
    printf("test_runner_execute: Initial current=%p, report_index=%d\n", (void*)current, report_index);
    
    while (current && report_index < test_count) {
        // Apply filter if provided
        if (filter) {
            char test_pattern[512];
            snprintf(test_pattern, sizeof(test_pattern), "%s.%s", 
                    current->suite_name, current->test_name);
            
            if (fnmatch(filter, test_pattern, 0) != 0 &&
                fnmatch(filter, current->test_name, 0) != 0 &&
                fnmatch(filter, current->suite_name, 0) != 0) {
                current = current->next;
                continue;
            }
        }
        
        TestReport* report = &summary->reports[report_index++];
        report->suite_name = current->suite_name;
        report->test_name = current->test_name;
        report->message = NULL;
        report->file = NULL;
        report->line = 0;
        
        // Create test context
        TestContext* ctx = test_context_create();
        test_context_set_current(ctx);
        
        double test_start = get_time_seconds();
        
        // Run setup if available
        if (current->setup_func) {
            current->setup_func();
        }
        
        // Run the test
        if (current->test_func) {
            current->test_func();
        }
        
        // Run teardown if available
        if (current->teardown_func) {
            current->teardown_func();
        }
        
        double test_end = get_time_seconds();
        report->execution_time = test_end - test_start;
        
        // Check test result
        if (ctx->test_failed) {
            report->result = TEST_RESULT_FAIL;
            report->file = ctx->current_file;
            report->line = ctx->current_line;
            if (ctx->failure_message) {
                report->message = strdup(ctx->failure_message);
            }
            summary->failed_tests++;
        } else {
            report->result = TEST_RESULT_PASS;
            summary->passed_tests++;
        }
        
        // Cleanup test context
        test_context_destroy(ctx);
        test_context_set_current(NULL);
        
        current = current->next;
    }
    
    double end_time = get_time_seconds();
    summary->total_time = end_time - start_time;
    summary->total_tests = report_index;
    
    return summary;
}

void test_runner_print_summary(const TestSummary* summary) {
    if (!summary) return;
    
    printf("\n");
    printf("================================================================================\n");
    printf("Test Results Summary\n");
    printf("================================================================================\n");
    
    // Print individual test results
    for (int i = 0; i < summary->total_tests; i++) {
        const TestReport* report = &summary->reports[i];
        
        if (report->result == TEST_RESULT_PASS) {
            printf("✓ %s.%s (%.3fs)\n", 
                   report->suite_name, report->test_name, report->execution_time);
        } else if (report->result == TEST_RESULT_FAIL) {
            printf("✗ %s.%s (%.3fs)\n", 
                   report->suite_name, report->test_name, report->execution_time);
            if (report->file && report->line > 0) {
                printf("  Failed at %s:%d\n", report->file, report->line);
            }
            if (report->message) {
                printf("  %s\n", report->message);
            }
        }
    }
    
    printf("\n");
    printf("Tests run: %d\n", summary->total_tests);
    printf("Passed: %d\n", summary->passed_tests);
    printf("Failed: %d\n", summary->failed_tests);
    printf("Skipped: %d\n", summary->skipped_tests);
    printf("Total time: %.3fs\n", summary->total_time);
    
    if (summary->failed_tests > 0) {
        printf("\n❌ SOME TESTS FAILED\n");
    } else {
        printf("\n✅ ALL TESTS PASSED\n");
    }
}

void test_runner_cleanup(TestSummary* summary) {
    if (!summary) return;
    
    if (summary->reports) {
        for (int i = 0; i < summary->total_tests; i++) {
            if (summary->reports[i].message) {
                free(summary->reports[i].message);
            }
        }
        free(summary->reports);
    }
    
    free(summary);
}

TestArgs test_args_parse(int argc, char** argv) {
    TestArgs args = {0};
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            args.help = true;
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            args.verbose = true;
        } else if (strcmp(argv[i], "--list-tests") == 0) {
            args.list_tests = true;
        } else if (strncmp(argv[i], "--filter=", 9) == 0) {
            args.filter = argv[i] + 9;
        } else if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
            args.filter = argv[++i];
        }
    }
    
    return args;
}

void test_args_print_help(const char* program_name) {
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("\n");
    printf("Options:\n");
    printf("  --help, -h          Show this help message\n");
    printf("  --verbose, -v       Enable verbose output\n");
    printf("  --list-tests        List all available tests\n");
    printf("  --filter=PATTERN    Run only tests matching PATTERN\n");
    printf("  --filter PATTERN    Run only tests matching PATTERN\n");
    printf("\n");
    printf("Filter patterns support wildcards:\n");
    printf("  *test_name          Run tests with names ending in 'test_name'\n");
    printf("  suite_name.*        Run all tests in 'suite_name' suite\n");
    printf("  *math*              Run tests with 'math' in the name\n");
    printf("\n");
}
