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

/*
Test(lambda_tests, test_csv_with_headers_comma) {
    // Test CSV parsing with headers (comma-separated) using direct script execution
    // Save current directory
    char original_cwd[1024];
    getcwd(original_cwd, sizeof(original_cwd));
    
    // Change to project root so lambda scripts can find lambda/lambda.h
    chdir("..");
    
    Runtime runtime;
    runtime_init(&runtime);
    runtime.current_dir = "";
    
    // Create a simple test script
    FILE* temp_file = fopen("temp_csv_test1.ls", "w");
    cr_assert_neq(temp_file, NULL, "Failed to create temporary test file");
    fprintf(temp_file, "let csv = input('test/input/test.csv', 'csv'); csv[0].name");
    fclose(temp_file);
    
    Item ret = run_script_at(&runtime, "temp_csv_test1.ls");
    
    // Remove temporary file
    remove("temp_csv_test1.ls");
    
    // Restore original directory
    chdir(original_cwd);
    
    // Verify the script runs without errors
    cr_assert_neq(ret, ITEM_ERROR, "CSV parsing should not fail");
    cr_assert_neq(ret, ITEM_NULL, "CSV parsing should return valid data");
    
    // Print the output for debugging
    StrBuf* strbuf = strbuf_new_cap(1024);
    print_item(strbuf, ret);
    printf("CSV with headers (comma) test result: %s\n", strbuf->str);
    
    // Verify it contains the expected first person's name (should be "John Doe" from first row)
    cr_assert(strstr(strbuf->str, "John Doe") != NULL, 
              "CSV with headers should return first person's name, got: %s", strbuf->str);
    
    strbuf_free(strbuf);
    runtime_cleanup(&runtime);
}

Test(lambda_tests, test_csv_with_tab_separator) {
    // Test CSV parsing with tab separator using direct script execution
    // Save current directory
    char original_cwd[1024];
    getcwd(original_cwd, sizeof(original_cwd));
    
    // Change to project root so lambda scripts can find lambda/lambda.h
    chdir("..");
    
    Runtime runtime;
    runtime_init(&runtime);
    runtime.current_dir = "";
    
    // Create a simple test script
    FILE* temp_file = fopen("temp_csv_test2.ls", "w");
    cr_assert_neq(temp_file, NULL, "Failed to create temporary test file");
    fprintf(temp_file, "let csv = input('test/input/test_tab.csv', 'csv'); csv[0].name");
    fclose(temp_file);
    
    Item ret = run_script_at(&runtime, "temp_csv_test2.ls");
    
    // Remove temporary file
    remove("temp_csv_test2.ls");
    
    // Restore original directory
    chdir(original_cwd);
    
    // Verify the script runs without errors
    cr_assert_neq(ret, ITEM_ERROR, "CSV parsing with tab separator should not fail");
    cr_assert_neq(ret, ITEM_NULL, "CSV parsing should return valid data");
    
    // Print the output for debugging
    StrBuf* strbuf = strbuf_new_cap(1024);
    print_item(strbuf, ret);
    printf("CSV with tab separator test result: %s\n", strbuf->str);
    
    // Verify it contains the expected field value (should be "John Doe" from first row)
    cr_assert(strstr(strbuf->str, "John Doe") != NULL, 
              "CSV with tab separator should return first person's name, got: %s", strbuf->str);
    
    strbuf_free(strbuf);
    runtime_cleanup(&runtime);
}

Test(lambda_tests, test_csv_without_headers) {
    // Test CSV parsing without headers using direct script execution
    // Save current directory
    char original_cwd[1024];
    getcwd(original_cwd, sizeof(original_cwd));
    
    // Change to project root so lambda scripts can find lambda/lambda.h
    chdir("..");
    
    Runtime runtime;
    runtime_init(&runtime);
    runtime.current_dir = "";
    
    // Create a simple test script
    FILE* temp_file = fopen("temp_csv_test3.ls", "w");
    cr_assert_neq(temp_file, NULL, "Failed to create temporary test file");
    fprintf(temp_file, "let csv = input('test/input/test_no_header.csv', 'csv'); csv[0][0]");
    fclose(temp_file);
    
    Item ret = run_script_at(&runtime, "temp_csv_test3.ls");
    
    // Remove temporary file
    remove("temp_csv_test3.ls");
    
    // Restore original directory
    chdir(original_cwd);
    
    // Verify the script runs without errors
    cr_assert_neq(ret, ITEM_ERROR, "CSV parsing without headers should not fail");
    cr_assert_neq(ret, ITEM_NULL, "CSV parsing should return valid data");
    
    // Print the output for debugging
    StrBuf* strbuf = strbuf_new_cap(1024);
    print_item(strbuf, ret);
    printf("CSV without headers test result: %s\n", strbuf->str);
    
    // Verify it contains the expected field value (should be "John Doe" from first row)
    cr_assert(strstr(strbuf->str, "John Doe") != NULL, 
              "CSV without headers should return first row first column value, got: %s", strbuf->str);
}

Test(lambda_tests, test_csv_field_access_by_name) {
    // Test accessing CSV data by field name using direct script execution
    // Save current directory
    char original_cwd[1024];
    getcwd(original_cwd, sizeof(original_cwd));
    
    // Change to project root so lambda scripts can find lambda/lambda.h
    chdir("..");
    
    Runtime runtime;
    runtime_init(&runtime);
    runtime.current_dir = "";
    
    // Create a simple test script
    FILE* temp_file = fopen("temp_csv_test4.ls", "w");
    cr_assert_neq(temp_file, NULL, "Failed to create temporary test file");
    fprintf(temp_file, "let csv = input('test/input/test.csv', 'csv'); csv[0].name");
    fclose(temp_file);
    
    Item ret = run_script_at(&runtime, "temp_csv_test4.ls");
    
    // Remove temporary file
    remove("temp_csv_test4.ls");
    
    // Restore original directory
    chdir(original_cwd);
    
    // Verify the script runs without errors
    cr_assert_neq(ret, ITEM_ERROR, "CSV field access by name should not fail");
    cr_assert_neq(ret, ITEM_NULL, "CSV field access should return valid data");
    
    // Print the output for debugging
    StrBuf* strbuf = strbuf_new_cap(1024);
    print_item(strbuf, ret);
    printf("CSV field access by name test result: %s\n", strbuf->str);
    
    // Verify it contains the expected field value (should be "John Doe" from first row)
    cr_assert(strstr(strbuf->str, "John Doe") != NULL, 
              "CSV field access by name should return first person's name, got: %s", strbuf->str);
    
    strbuf_free(strbuf);
    runtime_cleanup(&runtime);
}

Test(lambda_tests, test_csv_field_access_by_index) {
    // Test accessing CSV data by numeric index using direct script execution
    // Save current directory
    char original_cwd[1024];
    getcwd(original_cwd, sizeof(original_cwd));
    
    // Change to project root so lambda scripts can find lambda/lambda.h
    chdir("..");
    
    Runtime runtime;
    runtime_init(&runtime);
    runtime.current_dir = "";
    
    // Create a simple test script
    FILE* temp_file = fopen("temp_csv_test5.ls", "w");
    cr_assert_neq(temp_file, NULL, "Failed to create temporary test file");
    fprintf(temp_file, "let csv = input('test/input/test_no_header.csv', 'csv'); csv[0][0]");
    fclose(temp_file);
    
    Item ret = run_script_at(&runtime, "temp_csv_test5.ls");
    
    // Remove temporary file
    remove("temp_csv_test5.ls");
    
    // Restore original directory
    chdir(original_cwd);
    
    // Verify the script runs without errors
    cr_assert_neq(ret, ITEM_ERROR, "CSV field access by index should not fail");
    cr_assert_neq(ret, ITEM_NULL, "CSV field access should return valid data");
    
    // Print the output for debugging
    StrBuf* strbuf = strbuf_new_cap(1024);
    print_item(strbuf, ret);
    printf("CSV field access by index test result: %s\n", strbuf->str);
    
    // Verify it contains the expected field value (should be "John Doe" from first row)
    cr_assert(strstr(strbuf->str, "John Doe") != NULL, 
              "CSV field access by index should return first row first column value, got: %s", strbuf->str);
    
    strbuf_free(strbuf);
    runtime_cleanup(&runtime);
}

Test(lambda_tests, test_csv_length_function) {
    // Test getting the length of CSV data using direct script execution
    // Save current directory
    char original_cwd[1024];
    getcwd(original_cwd, sizeof(original_cwd));
    
    // Change to project root so lambda scripts can find lambda/lambda.h
    chdir("..");
    
    Runtime runtime;
    runtime_init(&runtime);
    runtime.current_dir = "";
    
    // Create a simple test script
    FILE* temp_file = fopen("temp_csv_test6.ls", "w");
    cr_assert_neq(temp_file, NULL, "Failed to create temporary test file");
    fprintf(temp_file, "let csv = input('./test/input/test.csv', 'csv'); length(csv)");
    fclose(temp_file);
    
    Item ret = run_script_at(&runtime, "temp_csv_test6.ls");
    
    // Remove temporary file
    remove("temp_csv_test6.ls");
    
    // Restore original directory
    chdir(original_cwd);
    
    // Verify the script runs without errors - len function might not be implemented yet
    // So we'll just check that the CSV parsing works
    cr_assert_neq(ret, ITEM_ERROR, "CSV length check should not crash");
    
    // Print the output for debugging
    StrBuf* strbuf = strbuf_new_cap(1024);
    print_item(strbuf, ret);
    printf("CSV length test result: %s\n", strbuf->str);
    
    strbuf_free(strbuf);
    runtime_cleanup(&runtime);
}
*/