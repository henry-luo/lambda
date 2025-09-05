#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>  // for getcwd and chdir
#include <assert.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

// Utility functions for file I/O and process execution
char* read_text_file(const char* file_path) {
    FILE* file = fopen(file_path, "r");
    if (!file) {
        return NULL;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // Allocate buffer and read file
    char* content = (char*)malloc(size + 1);
    if (!content) {
        fclose(file);
        return NULL;
    }
    
    size_t read_size = fread(content, 1, size, file);
    content[read_size] = '\0';
    fclose(file);
    
    return content;
}

void write_text_file(const char* file_path, const char* content) {
    FILE* file = fopen(file_path, "w");
    if (!file) {
        return;
    }
    
    fputs(content, file);
    fclose(file);
}

// Execute lambda.exe and capture its output
char* execute_lambda_script(const char* script_path) {
    char command[512];
    snprintf(command, sizeof(command), "./lambda.exe %s 2>/dev/null", script_path);
    
    FILE* pipe = popen(command, "r");
    if (!pipe) {
        fprintf(stderr, "Error: Failed to execute lambda.exe with script: %s\n", script_path);
        return NULL;
    }
    
    // Read output from pipe
    char* full_output = (char*)malloc(8192);
    if (!full_output) {
        pclose(pipe);
        return NULL;
    }
    
    size_t total_read = 0;
    size_t buffer_size = 8192;
    char buffer[256];
    
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        size_t len = strlen(buffer);
        
        // Resize output buffer if needed
        if (total_read + len + 1 > buffer_size) {
            buffer_size *= 2;
            full_output = (char*)realloc(full_output, buffer_size);
            if (!full_output) {
                pclose(pipe);
                return NULL;
            }
        }
        
        strcpy(full_output + total_read, buffer);
        total_read += len;
    }
    
    full_output[total_read] = '\0';
    
    int exit_code = pclose(pipe);
    if (exit_code != 0) {
        fprintf(stderr, "Error: lambda.exe exited with code %d for script: %s\n", exit_code, script_path);
        free(full_output);
        return NULL;
    }
    
    // Extract only the actual script output after the marker line
    char* marker = strstr(full_output, "##### Script");
    if (marker) {
        // Find the end of the marker line
        char* result_start = strchr(marker, '\n');
        if (result_start) {
            result_start++; // Skip the newline
            
            // Create a new string with just the result
            char* result = strdup(result_start);
            free(full_output);
            return result;
        }
    }
    
    // If no marker found, return the full output (fallback)
    return full_output;
}

// Setup and teardown functions
void setup(void) {
    // Runs before each test
}

void teardown(void) {
    // Runs after each test
}

TestSuite(lambda_tests, .init = setup, .fini = teardown);

// Function to trim whitespace from the end of a string
void trim_trailing_whitespace(char* str) {
    if (!str) return;
    
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r' || str[len - 1] == ' ' || str[len - 1] == '\t')) {
        str[len - 1] = '\0';
        len--;
    }
}

// Helper function to test a lambda script against expected output using lambda.exe
void test_lambda_script_against_file(const char* script_path, const char* expected_output_path) {
    // Execute lambda script via lambda.exe
    char* actual_output = execute_lambda_script(script_path);
    
    cr_assert_neq(actual_output, NULL, "Failed to execute lambda.exe with script: %s", script_path);
    
    // Trim trailing whitespace from actual output
    trim_trailing_whitespace(actual_output);
    
    printf("TRACE: test runner - actual output: '%s'\n", actual_output);
    
    // Extract script name from path for output file
    const char* script_name = strrchr(script_path, '/');
    script_name = script_name ? script_name + 1 : script_path;
    
    // Create output filename by replacing .ls with .txt
    char output_filename[512];
    snprintf(output_filename, sizeof(output_filename), "test_output/%s", script_name);
    char* dot = strrchr(output_filename, '.');
    if (dot) { strcpy(dot, ".txt"); }
    
    // Save actual output to test_output directory
    write_text_file(output_filename, actual_output);
    printf("TRACE: Saved actual output to %s\n", output_filename);
        
    // Read expected output to verify the file exists
    char* expected_output = read_text_file(expected_output_path);
    
    cr_assert_neq(expected_output, NULL, "Failed to read expected output file: %s", expected_output_path);
    
    // Trim trailing whitespace from expected output
    trim_trailing_whitespace(expected_output);

    // Verify the expected output matches the actual output
    cr_assert_eq(strcmp(expected_output, actual_output), 0,
                 "Output does not match expected output for script: %s\nExpected:\n'%s'\nGot:\n'%s'",
                 script_path, expected_output, actual_output);
    printf("Expected length: %d, got length: %d\n", (int)strlen(expected_output), (int)strlen(actual_output));
    
    free(expected_output);
    free(actual_output);
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

Test(lambda_tests, test_expr_ls) {
    test_lambda_script_against_file("test/lambda/expr.ls", "test/lambda/expr.txt");
}

Test(lambda_tests, test_decimal_ls) {
    test_lambda_script_against_file("test/lambda/decimal.ls", "test/lambda/decimal.txt");
}

Test(lambda_tests, test_box_unbox_ls) {
    test_lambda_script_against_file("test/lambda/box_unbox.ls", "test/lambda/box_unbox.txt");
}

Test(lambda_tests, test_sys_fn_ls) {
    test_lambda_script_against_file("test/lambda/sys_fn.ls", "test/lambda/sys_fn.txt");
}

Test(lambda_tests, test_expr_stam_ls) {
    test_lambda_script_against_file("test/lambda/expr_stam.ls", "test/lambda/expr_stam.txt");
}

Test(lambda_tests, test_numeric_expr_ls) {
    test_lambda_script_against_file("test/lambda/numeric_expr.ls", "test/lambda/numeric_expr.txt");
}

Test(lambda_tests, test_array_float_ls) {
    test_lambda_script_against_file("test/lambda/array_float.ls", "test/lambda/array_float.txt");
}

Test(lambda_tests, test_comp_expr_ls) {
    test_lambda_script_against_file("test/lambda/comp_expr.ls", "test/lambda/comp_expr.txt");
}

Test(lambda_tests, test_comp_expr_edge_ls) {
    test_lambda_script_against_file("test/lambda/comp_expr_edge.ls", "test/lambda/comp_expr_edge.txt");
}

// Test(lambda_tests, test_unicode_ls) {
//     test_lambda_script_against_file("test/lambda/unicode.ls", "test/lambda/unicode.txt");
// }

Test(lambda_tests, test_type_ls) {
    test_lambda_script_against_file("test/lambda/type.ls", "test/lambda/type.txt");
}

Test(lambda_tests, test_func_ls) {
    test_lambda_script_against_file("test/lambda/func.ls", "test/lambda/func.txt");
}

Test(lambda_tests, test_int64_ls) {
    test_lambda_script_against_file("test/lambda/int64.ls", "test/lambda/int64.txt");
}

Test(lambda_tests, test_csv_ls) {
    test_lambda_script_against_file("test/lambda/input_csv.ls", "test/lambda/input_csv.txt");
}

Test(lambda_tests, test_input_dir_ls) {
    test_lambda_script_against_file("test/lambda/input_dir.ls", "test/lambda/input_dir.txt");
}

Test(lambda_tests, test_complex_report_ls) {
    test_lambda_script_against_file("test/lambda/complex_report.ls", "test/lambda/complex_report.txt");
}

Test(lambda_tests, test_import_ls) {
    test_lambda_script_against_file("test/lambda/import.ls", "test/lambda/import.txt");
}

Test(lambda_tests, test_numeric_sys_func) {
    test_lambda_script_against_file("test/lambda/numeric_sys_func.ls", "test/lambda/numeric_sys_func.txt");
}