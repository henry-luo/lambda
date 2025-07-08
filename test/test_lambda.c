#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  // for getcwd and chdir
#include "../lambda/transpiler.h"
#include "../lambda/lambda.h"

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
    // Save current directory
    char original_cwd[1024];
    getcwd(original_cwd, sizeof(original_cwd));
    
    // Change to project root so lambda scripts can find lambda/lambda.h
    chdir("..");
    
    // Initialize runtime
    Runtime runtime;
    runtime_init(&runtime);
    runtime.current_dir = "";
    
    // Run the script
    Item ret = run_script_at(&runtime, script_path);
    
    // Capture the output
    StrBuf* strbuf = strbuf_new_cap(1024);
    print_item(strbuf, ret);
    
    // Read expected output (using path relative to current directory, which is now project root)
    char* expected_output = read_file_to_string(expected_output_path);
    
    // Restore original directory
    chdir(original_cwd);
    
    cr_assert_neq(expected_output, NULL, "Failed to read expected output file: %s", expected_output_path);
    
    // Trim whitespace from both strings for comparison
    trim_trailing_whitespace(strbuf->str);
    trim_trailing_whitespace(expected_output);
    
    // Compare results using Criterion assertion
    cr_assert_str_eq(strbuf->str, expected_output, 
        "Lambda script output doesn't match expected output.\nScript: %s\nExpected: %s\nGot: %s", 
        script_path, expected_output, strbuf->str);
    
    free(expected_output);
    strbuf_free(strbuf);
    runtime_cleanup(&runtime);
}

Test(lambda_tests, test_value_ls) {
    test_lambda_script_against_file("test/lambda/value.ls", "test/lambda/value.txt");
}

Test(lambda_tests, test_single_ls) {
    test_lambda_script_against_file("test/lambda/single.ls", "test/lambda/single.txt");
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
    
    // Change to project root so lambda scripts can find lambda/lambda.h
    chdir("..");
    
    Runtime runtime;
    runtime_init(&runtime);
    runtime.current_dir = "";
    
    Item ret = run_script_at(&runtime, "test/lambda/expr.ls");
    
    // Restore original directory
    chdir(original_cwd);
    
    // Just verify the script runs without crashing
    // The specific output format may vary but shouldn't be an error
    cr_assert_neq(ret, ITEM_ERROR, "expr.ls script should not return an error");
    
    runtime_cleanup(&runtime);
}

Test(lambda_tests, test_box_unbox_ls) {
    // Test box_unbox.ls script
    // Save current directory
    char original_cwd[1024];
    getcwd(original_cwd, sizeof(original_cwd));
    
    // Change to project root so lambda scripts can find lambda/lambda.h
    chdir("..");
    
    Runtime runtime;
    runtime_init(&runtime);
    runtime.current_dir = "";
    
    Item ret = run_script_at(&runtime, "test/lambda/box_unbox.ls");
    
    // Restore original directory
    chdir(original_cwd);
    
    // Verify the script runs without errors
    cr_assert_neq(ret, ITEM_ERROR, "box_unbox.ls script should not return an error");
    
    runtime_cleanup(&runtime);
}

Test(lambda_tests, test_csv_test_ls) {
    // Test csv_test.ls script which tests various CSV parsing scenarios
    // Save current directory
    char original_cwd[1024];
    getcwd(original_cwd, sizeof(original_cwd));
    
    // Change to project root so lambda scripts can find lambda/lambda.h
    chdir("..");
    
    Runtime runtime;
    runtime_init(&runtime);
    runtime.current_dir = "";
    
    Item ret = run_script_at(&runtime, "test/lambda/csv_test.ls");
    
    // Restore original directory
    chdir(original_cwd);
    
    // Verify the script runs without errors
    cr_assert_neq(ret, ITEM_ERROR, "csv_test.ls script should not return an error");
    
    // Print the output for debugging
    StrBuf* strbuf = strbuf_new_cap(1024);
    print_item(strbuf, ret);
    printf("CSV test output: %s\n", strbuf->str);
    strbuf_free(strbuf);
    
    runtime_cleanup(&runtime);
}
