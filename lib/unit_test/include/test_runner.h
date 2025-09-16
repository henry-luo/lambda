#ifndef TEST_RUNNER_H
#define TEST_RUNNER_H

#include "test_registry.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Test result enumeration
typedef enum {
    TEST_RESULT_PASS,
    TEST_RESULT_FAIL,
    TEST_RESULT_SKIP
} TestResult;

// Test report structure
typedef struct TestReport {
    TestResult result;
    const char* suite_name;
    const char* test_name;
    char* message;
    const char* file;
    int line;
    double execution_time;
} TestReport;

// Test summary structure
typedef struct TestSummary {
    int total_tests;
    int passed_tests;
    int failed_tests;
    int skipped_tests;
    double total_time;
    TestReport* reports;
} TestSummary;

// Test runner functions
TestSummary* test_runner_execute(TestCase* tests, const char* filter);
void test_runner_print_summary(const TestSummary* summary);
void test_runner_cleanup(TestSummary* summary);

// Command line argument parsing
typedef struct {
    const char* filter;
    bool verbose;
    bool list_tests;
    bool help;
} TestArgs;

TestArgs test_args_parse(int argc, char** argv);
void test_args_print_help(const char* program_name);

#ifdef __cplusplus
}
#endif

#endif // TEST_RUNNER_H
