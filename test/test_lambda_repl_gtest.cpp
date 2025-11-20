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
    test_result result = run_lambda_repl("1 + 1\n.quit");
    ASSERT_NE(result.output, nullptr);
    // Should contain the result "2"
    ASSERT_TRUE(strstr(result.output, "2") != nullptr) << "Expected to find result '2' in output";
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

// Additional tests migrated from Criterion version to achieve full parity

TEST(LambdaReplTests, test_executable_exists) {
#ifdef _WIN32
    int result = system("where lambda.exe > nul 2>&1");
#else
    int result = system("test -x ./lambda.exe");
#endif
    ASSERT_EQ(result, 0) << "Lambda executable should exist and be executable";
}

TEST(LambdaReplTests, test_startup_and_quit) {
    test_result result = run_lambda_repl(".quit");
    ASSERT_NE(result.output, nullptr) << "Expected output from REPL";
    ASSERT_GT(strlen(result.output), 0) << "REPL should produce output";
    ASSERT_TRUE(strstr(result.output, "Lambda") != nullptr || 
                strstr(result.output, "λ") != nullptr) << "Output should mention Lambda";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_multiple_commands) {
    test_result result = run_lambda_repl("1 + 1\n2 * 3\n.quit");
    ASSERT_NE(result.output, nullptr) << "Expected output from multiple commands";
    // Should contain both results
    ASSERT_TRUE(strstr(result.output, "2") != nullptr) << "Expected to find result '2' for 1+1";
    ASSERT_TRUE(strstr(result.output, "6") != nullptr) << "Expected to find result '6' for 2*3";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_quit_variations) {
    // Test .q short form
    test_result result1 = run_lambda_repl(".q");
    ASSERT_NE(result1.output, nullptr) << "Expected output from .q";
    free_test_result(&result1);
    
    // Test .exit
    test_result result2 = run_lambda_repl(".exit");
    ASSERT_NE(result2.output, nullptr) << "Expected output from .exit";
    free_test_result(&result2);
}

TEST(LambdaReplTests, test_complex_arithmetic) {
    test_result result = run_lambda_repl("5 * 7\n8 / 2\n.quit");
    ASSERT_NE(result.output, nullptr) << "Expected output from complex arithmetic";
    // Should contain both results
    ASSERT_TRUE(strstr(result.output, "35") != nullptr) << "Expected to find result '35' for 5*7";
    ASSERT_TRUE(strstr(result.output, "4") != nullptr) << "Expected to find result '4' for 8/2";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_error_recovery) {
    test_result result = run_lambda_repl("2 +\n1 + 1\n.quit");
    ASSERT_NE(result.output, nullptr) << "Expected output from error recovery test";
    // Should continue running despite syntax error and compute 1+1=2
    ASSERT_TRUE(strstr(result.output, "error") != nullptr || 
                strstr(result.output, "Error") != nullptr ||
                strstr(result.output, "ERROR") != nullptr) << "Should show error for incomplete expression";
    ASSERT_TRUE(strstr(result.output, "2") != nullptr) << "Should recover and compute 1+1=2";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_version_display) {
    test_result result = run_lambda_repl(".quit");
    ASSERT_NE(result.output, nullptr) << "Expected output from REPL";
    // Should show version information or Lambda branding
    ASSERT_GT(strlen(result.output), 0) << "Should show version/startup information";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_repl_functionality) {
    test_result result = run_lambda_repl(".quit");
    ASSERT_NE(result.output, nullptr) << "Expected output to check REPL behavior";
    // In non-interactive mode, prompts may not appear but REPL should function
    bool has_startup_info = strstr(result.output, "Lambda") != nullptr ||
                           strstr(result.output, "help") != nullptr ||
                           strstr(result.output, "λ") != nullptr;
    ASSERT_TRUE(has_startup_info) << "Should show REPL startup information";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_command_sequence_stability) {
    test_result result = run_lambda_repl("1 + 1\n.help\n2 * 2\n.quit");
    ASSERT_NE(result.output, nullptr) << "Expected output from command sequence";
    // Should contain help text and both computation results
    ASSERT_TRUE(strstr(result.output, "help") != nullptr || 
                strstr(result.output, "REPL") != nullptr ||
                strstr(result.output, "Commands") != nullptr) << "Expected help output";
    ASSERT_TRUE(strstr(result.output, "2") != nullptr) << "Expected to find result '2' for 1+1";
    ASSERT_TRUE(strstr(result.output, "4") != nullptr) << "Expected to find result '4' for 2*2";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_prompt_display) {
    test_result result = run_lambda_repl(".quit");
    ASSERT_NE(result.output, nullptr) << "Expected output from REPL";
    // Check for Lambda prompts or startup messages
    bool has_lambda_content = strstr(result.output, "λ") != nullptr ||
                             strstr(result.output, "Lambda") != nullptr ||
                             strstr(result.output, "L>") != nullptr;
    ASSERT_TRUE(has_lambda_content) << "Should show Lambda prompt or content";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_prompt_with_expressions) {
    test_result result = run_lambda_repl("2 + 3\n.quit");
    ASSERT_NE(result.output, nullptr) << "Expected output from expressions";
    // Should compute 2+3=5
    ASSERT_TRUE(strstr(result.output, "5") != nullptr) << "Expected to find result '5' for 2+3";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_unicode_prompt_support) {
    test_result result = run_lambda_repl(".quit");
    ASSERT_NE(result.output, nullptr) << "Expected output from REPL";
    // Unicode support test - just ensure REPL handles input/output properly
    ASSERT_GT(strlen(result.output), 0) << "Should handle unicode input properly";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_multiple_prompt_sequence) {
    test_result result = run_lambda_repl("1\n2\n3\n.quit");
    ASSERT_NE(result.output, nullptr) << "Expected output from multiple prompts";
    // Should echo back all three values
    ASSERT_TRUE(strstr(result.output, "1") != nullptr) << "Expected to find value '1'";
    ASSERT_TRUE(strstr(result.output, "2") != nullptr) << "Expected to find value '2'";
    ASSERT_TRUE(strstr(result.output, "3") != nullptr) << "Expected to find value '3'";
    free_test_result(&result);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
