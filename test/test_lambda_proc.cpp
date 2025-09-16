#include "../lib/unit_test/include/criterion/criterion.h"
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

// Execute lambda.exe run and capture its output
char* execute_lambda_proc_script(const char* script_path) {
    char command[512];
    snprintf(command, sizeof(command), "./lambda.exe run %s 2>&1", script_path);
    
    FILE* pipe = popen(command, "r");
    if (!pipe) {
        fprintf(stderr, "Error: Failed to execute lambda.exe run with script: %s\n", script_path);
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
        fprintf(stderr, "Error: lambda.exe run exited with code %d for script: %s\n", exit_code, script_path);
        free(full_output);
        return NULL;
    }
    
    // Extract only the actual script output after "Executing JIT compiled code..."
    char* marker = strstr(full_output, "Executing JIT compiled code...");
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

TestSuite(lambda_proc_tests, .init = setup, .fini = teardown);

// Function to trim whitespace from the end of a string
void trim_trailing_whitespace(char* str) {
    if (!str) return;
    
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r' || str[len - 1] == ' ' || str[len - 1] == '\t')) {
        str[len - 1] = '\0';
        len--;
    }
}

// Helper function to test a lambda procedural script against expected output using lambda.exe run
void test_lambda_proc_script_against_file(const char* script_path, const char* expected_output_path) {
    // Execute lambda procedural script via lambda.exe run
    char* actual_output = execute_lambda_proc_script(script_path);
    
    cr_assert_neq(actual_output, NULL, "Failed to execute lambda.exe run with script: %s", script_path);
    
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

Test(lambda_proc_tests, test_proc1) {
    test_lambda_proc_script_against_file("test/lambda/proc1.ls", "test/lambda/proc1.txt");
}

Test(lambda_proc_tests, test_proc2) {
    test_lambda_proc_script_against_file("test/lambda/proc2.ls", "test/lambda/proc2.txt");
}

Test(lambda_proc_tests, test_proc_fetch) {
    test_lambda_proc_script_against_file("test/lambda/proc_fetch.ls", "test/lambda/proc_fetch.txt");
}