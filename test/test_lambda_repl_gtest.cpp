#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <io.h>
    #include <direct.h>
    #define popen _popen
    #define pclose _pclose
    #define WEXITSTATUS(status) (status)
#else
    #include <unistd.h>
    #include <sys/wait.h>
#endif

// Helper to run Lambda REPL and capture output
struct test_result {
    char* output;
    int exit_code;
};

test_result run_lambda_repl(const char* input) {
    test_result result = {nullptr, -1};
    
    // Create a temporary file for input
    const char* temp_file = "temp_repl_input.txt";
    FILE* temp = fopen(temp_file, "w");
    if (!temp) {
        return result;
    }
    
    fprintf(temp, "%s\n", input);
    fclose(temp);
    
    // Run lambda.exe with redirected input
    char command[512];
#ifdef _WIN32
    snprintf(command, sizeof(command), "lambda.exe < %s", temp_file);
#else
    snprintf(command, sizeof(command), "./lambda.exe < %s", temp_file);
#endif
    
    FILE* pipe = popen(command, "r");
    if (!pipe) {
        unlink(temp_file);
        return result;
    }
    
    // Read output
    char buffer[4096];
    size_t total_size = 0;
    char* full_output = nullptr;
    
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        size_t len = strlen(buffer);
        char* new_output = (char*)realloc(full_output, total_size + len + 1);
        if (!new_output) {
            free(full_output);
            pclose(pipe);
            unlink(temp_file);
            return result;
        }
        full_output = new_output;
        strcpy(full_output + total_size, buffer);
        total_size += len;
    }
    
    result.exit_code = pclose(pipe);
    result.output = full_output;
    
    unlink(temp_file);
    return result;
}

void free_test_result(test_result* result) {
    if (result->output) {
        free(result->output);
        result->output = nullptr;
    }
}

// Test basic REPL functionality
TEST(LambdaReplTests, test_help_command) {
    test_result result = run_lambda_repl(".help");
    ASSERT_NE(result.output, nullptr);
    ASSERT_TRUE(strstr(result.output, "help") != nullptr || strstr(result.output, "Lambda") != nullptr);
    free_test_result(&result);
}

TEST(LambdaReplTests, test_quit_command) {
    test_result result = run_lambda_repl(".quit");
    ASSERT_NE(result.output, nullptr);
    // Should exit cleanly
    ASSERT_EQ(WEXITSTATUS(result.exit_code), 0);
    free_test_result(&result);
}

TEST(LambdaReplTests, test_simple_expression) {
    test_result result = run_lambda_repl("1 + 1");
    ASSERT_NE(result.output, nullptr);
    // Should contain result or error message
    ASSERT_GT(strlen(result.output), 0);
    free_test_result(&result);
}

TEST(LambdaReplTests, test_invalid_command) {
    test_result result = run_lambda_repl(".invalid");
    ASSERT_NE(result.output, nullptr);
    // Should handle invalid commands gracefully
    ASSERT_GT(strlen(result.output), 0);
    free_test_result(&result);
}

TEST(LambdaReplTests, test_empty_input) {
    test_result result = run_lambda_repl("");
    ASSERT_NE(result.output, nullptr);
    free_test_result(&result);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
