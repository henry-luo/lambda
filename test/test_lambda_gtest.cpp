#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <cerrno>
#include <string>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOGDI
    #define NOUSER
    #define NOHELP
    #define NOMCX
    #include <windows.h>
    #include <process.h>
    #include <io.h>
    #include <direct.h>
    #define getcwd _getcwd
    #define chdir _chdir
    #define unlink _unlink
    #define access _access
    #define popen _popen
    #define pclose _pclose
    #define WEXITSTATUS(status) (status)
#else
    #include <unistd.h>
    #include <sys/wait.h>
#endif

// Helper function to execute a lambda script and capture output
char* execute_lambda_script(const char* script_path) {
    char command[512];
#ifdef _WIN32
    snprintf(command, sizeof(command), "lambda.exe \"%s\"", script_path);
#else
    snprintf(command, sizeof(command), "./lambda.exe \"%s\"", script_path);
#endif

    FILE* pipe = popen(command, "r");
    if (!pipe) {
        fprintf(stderr, "Error: Could not execute command: %s\n", command);
        return nullptr;
    }

    // Read output in chunks
    char buffer[1024];
    size_t total_size = 0;
    char* full_output = nullptr;

    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        size_t len = strlen(buffer);
        char* new_output = (char*)realloc(full_output, total_size + len + 1);
        if (!new_output) {
            free(full_output);
            pclose(pipe);
            return nullptr;
        }
        full_output = new_output;
        strcpy(full_output + total_size, buffer);
        total_size += len;
    }

    int exit_code = pclose(pipe);
    if (WEXITSTATUS(exit_code) != 0) {
        fprintf(stderr, "Error: lambda.exe exited with code %d for script: %s\n",
                WEXITSTATUS(exit_code), script_path);
        free(full_output);
        return nullptr;
    }

    // Extract result from "##### Script" marker
    char* marker = strstr(full_output, "##### Script");
    if (marker) {
        char* result_start = strchr(marker, '\n');
        if (result_start) {
            result_start++; // Skip the newline
            // Create a copy of the result
            char* result = strdup(result_start);
            free(full_output);
            return result;
        }
    }

    return full_output;
}

// Helper function to trim trailing whitespace
void trim_trailing_whitespace(char* str) {
    if (!str) return;
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[--len] = '\0';
    }
}

// Helper function to read expected output from file
char* read_expected_output(const char* expected_file_path) {
    FILE* file = fopen(expected_file_path, "r");
    if (!file) {
        return nullptr;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* content = (char*)malloc(file_size + 1);
    if (!content) {
        fclose(file);
        return nullptr;
    }

    size_t read_size = fread(content, 1, file_size, file);
    content[read_size] = '\0';
    fclose(file);

    trim_trailing_whitespace(content);
    return content;
}

// Helper function to test lambda script against expected output file
void test_lambda_script_against_file(const char* script_path, const char* expected_file_path) {
    // Get script name for better error messages
    const char* script_name = strrchr(script_path, '/');
    script_name = script_name ? script_name + 1 : script_path;

    // Create expected output file path if it doesn't exist
    char expected_path[512];
    strncpy(expected_path, script_path, sizeof(expected_path) - 1);
    expected_path[sizeof(expected_path) - 1] = '\0';
    char* dot = strrchr(expected_path, '.');
    if (dot) { strcpy(dot, ".txt"); }

    char* expected_output = read_expected_output(expected_file_path);
    ASSERT_NE(expected_output, nullptr) << "Could not read expected output file: " << expected_file_path;

    char* actual_output = execute_lambda_script(script_path);
    ASSERT_NE(actual_output, nullptr) << "Could not execute lambda script: " << script_path;

    // Trim whitespace from actual output
    trim_trailing_whitespace(actual_output);

    // Compare outputs
    ASSERT_STREQ(expected_output, actual_output)
        << "Output mismatch for script: " << script_path
        << " (expected " << strlen(expected_output) << " chars, got " << strlen(actual_output) << " chars)";

    free(expected_output);
    free(actual_output);
}

// Test cases
TEST(LambdaTests, test_single) {
    test_lambda_script_against_file("test/lambda/single.ls", "test/lambda/single.txt");
}

TEST(LambdaTests, test_value) {
    test_lambda_script_against_file("test/lambda/value.ls", "test/lambda/value.txt");
}

TEST(LambdaTests, test_simple_expr_ls) {
    test_lambda_script_against_file("test/lambda/simple_expr.ls", "test/lambda/simple_expr.txt");
}

TEST(LambdaTests, test_expr_ls) {
    test_lambda_script_against_file("test/lambda/expr.ls", "test/lambda/expr.txt");
}

TEST(LambdaTests, test_decimal) {
    test_lambda_script_against_file("test/lambda/decimal.ls", "test/lambda/decimal.txt");
}

TEST(LambdaTests, test_box_unbox) {
    test_lambda_script_against_file("test/lambda/box_unbox.ls", "test/lambda/box_unbox.txt");
}

TEST(LambdaTests, test_sys_fn) {
    test_lambda_script_against_file("test/lambda/sys_fn.ls", "test/lambda/sys_fn.txt");
}

TEST(LambdaTests, test_expr_stam) {
    test_lambda_script_against_file("test/lambda/expr_stam.ls", "test/lambda/expr_stam.txt");
}

TEST(LambdaTests, test_numeric_expr) {
    test_lambda_script_against_file("test/lambda/numeric_expr.ls", "test/lambda/numeric_expr.txt");
}

TEST(LambdaTests, test_array_float) {
    test_lambda_script_against_file("test/lambda/array_float.ls", "test/lambda/array_float.txt");
}

TEST(LambdaTests, test_comp_expr_ls) {
    test_lambda_script_against_file("test/lambda/comp_expr.ls", "test/lambda/comp_expr.txt");
}

TEST(LambdaTests, test_comp_expr_edge_ls) {
    test_lambda_script_against_file("test/lambda/comp_expr_edge.ls", "test/lambda/comp_expr_edge.txt");
}

TEST(LambdaTests, test_type) {
    test_lambda_script_against_file("test/lambda/type.ls", "test/lambda/type.txt");
}

TEST(LambdaTests, test_func) {
    test_lambda_script_against_file("test/lambda/func.ls", "test/lambda/func.txt");
}

TEST(LambdaTests, test_func_param) {
    test_lambda_script_against_file("test/lambda/func_param.ls", "test/lambda/func_param.txt");
}

TEST(LambdaTests, test_int64) {
    test_lambda_script_against_file("test/lambda/int64.ls", "test/lambda/int64.txt");
}

TEST(LambdaTests, test_input_csv) {
    test_lambda_script_against_file("test/lambda/input_csv.ls", "test/lambda/input_csv.txt");
}

TEST(LambdaTests, test_input_dir) {
#ifdef _WIN32
    GTEST_SKIP() << "Skipping on Windows: directory listing results differ (size, symlinks, ordering)";
#endif
    test_lambda_script_against_file("test/lambda/input_dir.ls", "test/lambda/input_dir.txt");
}

TEST(LambdaTests, test_complex_report) {
    test_lambda_script_against_file("test/lambda/complex_report.ls", "test/lambda/complex_report.txt");
}

TEST(LambdaTests, test_import) {
    test_lambda_script_against_file("test/lambda/import.ls", "test/lambda/import.txt");
}

TEST(LambdaTests, test_numeric_sys_func) {
    test_lambda_script_against_file("test/lambda/numeric_sys_func.ls", "test/lambda/numeric_sys_func.txt");
}

TEST(LambdaTests, test_complex_data_science_report) {
    test_lambda_script_against_file("test/lambda/complex_data_science_report.ls", "test/lambda/complex_data_science_report.txt");
}

TEST(LambdaTests, test_complex_iot_report) {
    test_lambda_script_against_file("test/lambda/complex_iot_report.ls", "test/lambda/complex_iot_report.txt");
}

TEST(LambdaTests, test_single_let) {
    test_lambda_script_against_file("test/lambda/single_let.ls", "test/lambda/single_let.txt");
}

// Negative tests - verify transpiler reports errors gracefully without crashing
// These scripts contain intentional type errors and should fail with proper error messages

// Helper to test that a script reports type errors but doesn't crash
// Note: Lambda currently exits with code 0 even on type errors (errors are reported to stderr)
void test_lambda_script_expects_error(const char* script_path) {
    char command[512];
#ifdef _WIN32
    snprintf(command, sizeof(command), "lambda.exe \"%s\" 2>&1", script_path);
#else
    snprintf(command, sizeof(command), "./lambda.exe \"%s\" 2>&1", script_path);
#endif

    FILE* pipe = popen(command, "r");
    ASSERT_NE(pipe, nullptr) << "Failed to execute command: " << command;

    char buffer[4096];
    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }

    int exit_code = pclose(pipe);
    (void)exit_code;  // exit code may be 0 even with errors
    
    // Should contain error messages (type_error or [ERR!])
    bool has_error_msg = output.find("type_error") != std::string::npos ||
                         output.find("[ERR!]") != std::string::npos;
    EXPECT_TRUE(has_error_msg) << "Expected error messages in output for: " << script_path
                               << "\nOutput was: " << output;
    
    // Should NOT contain crash indicators
    EXPECT_EQ(output.find("Segmentation fault"), std::string::npos) 
        << "Transpiler crashed on: " << script_path;
    EXPECT_EQ(output.find("SIGABRT"), std::string::npos) 
        << "Transpiler aborted on: " << script_path;
}

TEST(LambdaNegativeTests, test_func_param_type_errors) {
    test_lambda_script_expects_error("test/lambda/negative/func_param_negative.ls");
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
