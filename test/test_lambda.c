#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>  // for getcwd and chdir
#include <assert.h>
#include <string.h>

// Include C header and declare extern C functions from the lambda project
#include "../lambda/lambda.h"
#include "../lib/strbuf.h"

// Explicitly declare string functions to avoid macro interference
extern size_t strlen(const char *s);
extern int strcmp(const char *s1, const char *s2);
extern char *strstr(const char *haystack, const char *needle);

// Forward declarations for C interface functions from the lambda runtime
typedef struct Runtime Runtime;

extern void runtime_init(Runtime* runtime);
extern void runtime_cleanup(Runtime* runtime);
extern uint64_t run_script_at(Runtime *runtime, char* script_path, bool transpile_only);
extern void format_item(StrBuf *strbuf, Item item, int depth, char* indent);

// Simple C-compatible Runtime structure definition
// This should match the actual Runtime structure
typedef struct Runtime {
    void* scripts;     // ArrayList* scripts
    void* parser;      // TSParser* parser
    char* current_dir;
} Runtime;

// Setup and teardown functions
void setup(void) {
    // Runs before each test
}

void teardown(void) {
    // Runs after each test
}

TestSuite(lambda_tests, .init = setup, .fini = teardown);

// Function to read file contents into a string
char* read_file_to_string(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        printf("Error: Could not open file %s\n", filename);
        return NULL;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // Allocate buffer and read file
    char* buffer = malloc(file_size + 1);
    if (!buffer) {
        printf("Error: Could not allocate memory for file %s\n", filename);
        fclose(file);
        return NULL;
    }
    
    size_t bytes_read = fread(buffer, 1, file_size, file);
    buffer[bytes_read] = '\0';
    fclose(file);
    
    return buffer;
}

// Function to trim whitespace from the end of a string
void trim_trailing_whitespace(char* str) {
    if (!str) return;
    
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r' || str[len - 1] == ' ' || str[len - 1] == '\t')) {
        str[len - 1] = '\0';
        len--;
    }
}

// Helper function to test a lambda script against expected output
void test_lambda_script_against_file(const char* script_path, const char* expected_output_path) { 
    // Initialize runtime
    Runtime runtime;
    runtime_init(&runtime);  runtime.current_dir = "";
    
    // Run the script
    uint64_t ret = run_script_at(&runtime, (char*)script_path, false);
    printf("TRACE: test runner - ret: %llu\n", ret);
    
    StrBuf* output_buf = strbuf_new_cap(1024);
    // Cast uint64_t to Item (which is uint64_t in C)
    format_item(output_buf, (Item)ret, 0, " ");
    printf("TRACE: test runner - formatted output: '%s'\n", output_buf->str);
        
    // Read expected output to verify the file exists
    char* expected_output = read_file_to_string(expected_output_path);
    
    cr_assert_neq(expected_output, NULL, "Failed to read expected output file: %s", expected_output_path);
    
    // Check that the script ran without error (assuming 0 means error)
    cr_assert_neq(ret, 0, "Lambda script returned error. Script: %s", script_path);

    // Verify the expected output matches the actual output
    cr_assert_eq(strcmp(expected_output, output_buf->str), 0,
                 "Output does not match expected output for script: %s\nExpected:\n%s\nGot:\n%s",
                 script_path, expected_output, output_buf->str);
    printf("expect length: %d, got length: %d\n", (int)strlen(expected_output), (int)strlen(output_buf->str));
    assert(strlen(expected_output) == output_buf->length);
    
    free(expected_output);  strbuf_free(output_buf);
    runtime_cleanup(&runtime);
}

Test(lambda_tests, test_single_ls) {
    test_lambda_script_against_file("test/lambda/single.ls", "test/lambda/single.txt");
}

Test(lambda_tests, test_value_ls) {
    test_lambda_script_against_file("test/lambda/value.ls", "test/lambda/value.txt");
}

Test(lambda_tests, test_simple_expr_ls) {
    test_lambda_script_against_file("test/lambda/simple_expr.ls", "test/lambda/simple_expr.txt");
}

Test(lambda_tests, test_sys_fn_ls) {
    test_lambda_script_against_file("test/lambda/sys_fn.ls", "test/lambda/sys_fn.txt");
}

// Additional test cases for other lambda scripts
Test(lambda_tests, test_expr_ls) {
    // For expr.ls, we'll test if it runs without errors
    // Since there's no expected output file, we just verify it doesn't crash
    // Save current directory
    char original_cwd[1024];
    getcwd(original_cwd, sizeof(original_cwd));
    
    // Check if we're already in project root or in test directory
    if (strstr(original_cwd, "/test") && original_cwd[strlen(original_cwd)-5] == '/' && 
        strcmp(original_cwd + strlen(original_cwd)-4, "test") == 0) {
        chdir("..");
    }
    
    Runtime runtime;
    runtime_init(&runtime);
    runtime.current_dir = "";
    
    uint64_t ret = run_script_at(&runtime, (char*)"test/lambda/expr.ls", false);
    
    // Restore original directory
    chdir(original_cwd);
    
    // Just verify the script runs without crashing
    // We assume non-zero means success and 0 means error
    cr_assert_neq(ret, 0, "expr.ls script should not return an error");
    
    runtime_cleanup(&runtime);
}

Test(lambda_tests, test_box_unbox_ls) {
    // Test box_unbox.ls script
    // Save current directory
    char original_cwd[1024];
    getcwd(original_cwd, sizeof(original_cwd));
    
    // Check if we're already in project root or in test directory
    if (strstr(original_cwd, "/test") && original_cwd[strlen(original_cwd)-5] == '/' && 
        strcmp(original_cwd + strlen(original_cwd)-4, "test") == 0) {
        chdir("..");
    }
    
    Runtime runtime;
    runtime_init(&runtime);
    runtime.current_dir = "";
    
    uint64_t ret = run_script_at(&runtime, (char*)"test/lambda/box_unbox.ls", false);
    
    // Restore original directory
    chdir(original_cwd);
    
    // Verify the script runs without errors
    cr_assert_neq(ret, 0, "box_unbox.ls script should not return an error");
    
    runtime_cleanup(&runtime);
}

Test(lambda_tests, test_csv_test_ls) {
    // Test csv_test.ls script which tests various CSV parsing scenarios
    // Save current directory
    char original_cwd[1024];
    getcwd(original_cwd, sizeof(original_cwd));
    
    // Check if we're already in project root or in test directory
    if (strstr(original_cwd, "/test") && original_cwd[strlen(original_cwd)-5] == '/' && 
        strcmp(original_cwd + strlen(original_cwd)-4, "test") == 0) {
        chdir("..");
    }
    
    Runtime runtime;
    runtime_init(&runtime);
    runtime.current_dir = "";
    
    uint64_t ret = run_script_at(&runtime, (char*)"test/lambda/csv_test.ls", false);
    
    // Restore original directory
    chdir(original_cwd);
    
    // Verify the script runs without errors
    cr_assert_neq(ret, 0, "csv_test.ls script should not return an error");
    
    printf("CSV test completed successfully\n");
    
    runtime_cleanup(&runtime);
}
