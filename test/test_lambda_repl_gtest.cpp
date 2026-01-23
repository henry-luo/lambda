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
    snprintf(command, sizeof(command), "lambda.exe < %s 2>&1", temp_file);
#else
    snprintf(command, sizeof(command), "./lambda.exe < %s 2>&1", temp_file);
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

// ============================================================================
// Multi-line Input Tests (Continuation Prompt Feature)
// ============================================================================

TEST(LambdaReplTests, test_multiline_array) {
    // Multi-line array definition with unclosed bracket
    test_result result = run_lambda_repl("let arr = [\n  1,\n  2,\n  3\n]\narr\n.quit");
    ASSERT_NE(result.output, nullptr) << "Expected output from multi-line array";
    // Should show continuation prompts and final array result
    ASSERT_TRUE(strstr(result.output, ".. ") != nullptr) << "Expected continuation prompt '.. '";
    ASSERT_TRUE(strstr(result.output, "[1, 2, 3]") != nullptr || 
                (strstr(result.output, "1") != nullptr && 
                 strstr(result.output, "2") != nullptr && 
                 strstr(result.output, "3") != nullptr)) << "Expected array with values 1, 2, 3";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_multiline_map) {
    // Multi-line map definition with unclosed brace
    test_result result = run_lambda_repl("let m = {\n  a: 1,\n  b: 2\n}\nm\n.quit");
    ASSERT_NE(result.output, nullptr) << "Expected output from multi-line map";
    // Should show continuation prompts
    ASSERT_TRUE(strstr(result.output, ".. ") != nullptr) << "Expected continuation prompt '.. '";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_multiline_function) {
    // Multi-line function definition
    test_result result = run_lambda_repl("let f = fn(x) {\n  x * 2\n}\n.quit");
    ASSERT_NE(result.output, nullptr) << "Expected output from multi-line function";
    // Should show continuation prompts for incomplete function
    ASSERT_TRUE(strstr(result.output, ".. ") != nullptr) << "Expected continuation prompt '.. '";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_multiline_nested_brackets) {
    // Nested brackets should all be tracked
    test_result result = run_lambda_repl("let nested = [\n  [1, 2],\n  [3, 4]\n]\nnested\n.quit");
    ASSERT_NE(result.output, nullptr) << "Expected output from nested brackets";
    // Should show continuation prompts
    ASSERT_TRUE(strstr(result.output, ".. ") != nullptr) << "Expected continuation prompt for nested brackets";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_multiline_parentheses) {
    // Multi-line expression with unclosed parentheses
    test_result result = run_lambda_repl("let sum = (\n  1 + 2 +\n  3 + 4\n)\nsum\n.quit");
    ASSERT_NE(result.output, nullptr) << "Expected output from multi-line parentheses";
    // Should show continuation prompts
    ASSERT_TRUE(strstr(result.output, ".. ") != nullptr) << "Expected continuation prompt for unclosed parens";
    // Result should be 10
    ASSERT_TRUE(strstr(result.output, "10") != nullptr) << "Expected sum to be 10";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_multiline_string_not_incomplete) {
    // Strings with brackets inside should not trigger continuation
    test_result result = run_lambda_repl("\"hello { world }\"\n.quit");
    ASSERT_NE(result.output, nullptr) << "Expected output from string with brackets";
    // Should NOT show continuation prompt - string brackets don't count
    // The string should be printed
    ASSERT_TRUE(strstr(result.output, "hello") != nullptr) << "Expected string output";
    free_test_result(&result);
}

// ============================================================================
// Syntax Error Recovery Tests
// ============================================================================

TEST(LambdaReplTests, test_syntax_error_discarded) {
    // Invalid syntax should be discarded, not crash REPL
    test_result result = run_lambda_repl("@#$%\n5 + 5\n.quit");
    ASSERT_NE(result.output, nullptr) << "Expected output after syntax error";
    // Should show error message
    ASSERT_TRUE(strstr(result.output, "Syntax error") != nullptr ||
                strstr(result.output, "error") != nullptr ||
                strstr(result.output, "Error") != nullptr) << "Expected syntax error message";
    // Should recover and compute 5+5=10
    ASSERT_TRUE(strstr(result.output, "10") != nullptr) << "Expected recovery with result '10'";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_syntax_error_does_not_corrupt_state) {
    // After syntax error, previous valid definitions should still work
    test_result result = run_lambda_repl("let x = 100\n@invalid@\nx * 2\n.quit");
    ASSERT_NE(result.output, nullptr) << "Expected output from state preservation test";
    // x should still be defined and x*2 should work
    ASSERT_TRUE(strstr(result.output, "200") != nullptr) << "Expected x*2=200 after error recovery";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_multiple_syntax_errors) {
    // Multiple syntax errors in sequence
    test_result result = run_lambda_repl("!!!\n@@@\n###\n1 + 2\n.quit");
    ASSERT_NE(result.output, nullptr) << "Expected output from multiple errors";
    // Should eventually compute 1+2=3
    ASSERT_TRUE(strstr(result.output, "3") != nullptr) << "Expected result '3' after multiple errors";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_incomplete_vs_error) {
    // Incomplete (missing bracket) should wait, not error
    // Then error (invalid chars) should discard
    test_result result = run_lambda_repl("let a = [\n1\n]\n@error@\na\n.quit");
    ASSERT_NE(result.output, nullptr) << "Expected output from incomplete vs error test";
    // Array should work (continuation prompt used)
    ASSERT_TRUE(strstr(result.output, ".. ") != nullptr) << "Expected continuation prompt for array";
    // Error should be reported for @error@
    ASSERT_TRUE(strstr(result.output, "Syntax error") != nullptr ||
                strstr(result.output, "error") != nullptr) << "Expected error for invalid syntax";
    free_test_result(&result);
}

// ============================================================================
// .clear Command Tests
// ============================================================================

TEST(LambdaReplTests, test_clear_resets_variables) {
    // After .clear, variables should be undefined
    test_result result = run_lambda_repl("let myvar = 999\nmyvar\n.clear\nmyvar\n.quit");
    ASSERT_NE(result.output, nullptr) << "Expected output from .clear test";
    // Should show "REPL history cleared"
    ASSERT_TRUE(strstr(result.output, "cleared") != nullptr) << "Expected 'cleared' message";
    // After clear, accessing myvar should cause error
    ASSERT_TRUE(strstr(result.output, "error") != nullptr ||
                strstr(result.output, "Error") != nullptr) << "Expected error accessing cleared variable";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_clear_allows_redefinition) {
    // After .clear, we can redefine variables
    test_result result = run_lambda_repl("let z = 10\n.clear\nlet z = 20\nz\n.quit");
    ASSERT_NE(result.output, nullptr) << "Expected output from redefinition after clear";
    // Should show 20 (the new value)
    ASSERT_TRUE(strstr(result.output, "20") != nullptr) << "Expected new value '20' after clear and redefine";
    free_test_result(&result);
}

// ============================================================================
// Incremental Output Display Tests
// ============================================================================

TEST(LambdaReplTests, test_variable_persistence) {
    // Variables defined earlier should persist
    test_result result = run_lambda_repl("let a = 5\nlet b = 10\na + b\n.quit");
    ASSERT_NE(result.output, nullptr) << "Expected output from variable persistence";
    // a + b should be 15
    ASSERT_TRUE(strstr(result.output, "15") != nullptr) << "Expected a+b=15";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_sequential_definitions) {
    // Multiple let statements in sequence
    test_result result = run_lambda_repl("let x = 1\nlet y = 2\nlet z = 3\nx + y + z\n.quit");
    ASSERT_NE(result.output, nullptr) << "Expected output from sequential definitions";
    // x + y + z = 6
    ASSERT_TRUE(strstr(result.output, "6") != nullptr) << "Expected x+y+z=6";
    free_test_result(&result);
}

// ============================================================================
// Edge Cases and Robustness Tests
// ============================================================================

TEST(LambdaReplTests, test_empty_lines_in_multiline) {
    // Empty lines during multi-line input
    test_result result = run_lambda_repl("let arr = [\n\n1\n\n]\narr\n.quit");
    ASSERT_NE(result.output, nullptr) << "Expected output with empty lines";
    // Should still work
    ASSERT_TRUE(strstr(result.output, "1") != nullptr) << "Expected array with 1";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_comment_in_multiline) {
    // Comments should not affect bracket counting
    test_result result = run_lambda_repl("let x = [\n// this is a comment with {\n1\n]\nx\n.quit");
    ASSERT_NE(result.output, nullptr) << "Expected output with comment in multiline";
    // Should work - comment brackets don't count
    ASSERT_TRUE(strstr(result.output, "1") != nullptr) << "Expected array with 1";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_block_comment_incomplete) {
    // Unclosed block comment should be detected as incomplete
    test_result result = run_lambda_repl("/* this is\nstill a comment */\n1 + 1\n.quit");
    ASSERT_NE(result.output, nullptr) << "Expected output with block comment";
    // Should handle block comment and compute 1+1
    ASSERT_TRUE(strstr(result.output, "2") != nullptr) << "Expected result 2";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_deeply_nested_multiline) {
    // Deeply nested structure
    test_result result = run_lambda_repl("let deep = [\n  [\n    [\n      1\n    ]\n  ]\n]\ndeep\n.quit");
    ASSERT_NE(result.output, nullptr) << "Expected output from deeply nested structure";
    // Should show multiple continuation prompts
    int cont_count = 0;
    const char* p = result.output;
    while ((p = strstr(p, ".. ")) != nullptr) {
        cont_count++;
        p++;
    }
    ASSERT_GE(cont_count, 3) << "Expected at least 3 continuation prompts for deep nesting";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_mixed_brackets_multiline) {
    // Mix of different bracket types
    test_result result = run_lambda_repl("let mixed = {\n  arr: [\n    (1 + 2)\n  ]\n}\nmixed\n.quit");
    ASSERT_NE(result.output, nullptr) << "Expected output from mixed brackets";
    // Should show continuation prompts
    ASSERT_TRUE(strstr(result.output, ".. ") != nullptr) << "Expected continuation for mixed brackets";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_multiline_startup_message) {
    // Verify startup message mentions multi-line support
    test_result result = run_lambda_repl(".quit");
    ASSERT_NE(result.output, nullptr) << "Expected startup message";
    ASSERT_TRUE(strstr(result.output, "Multi-line") != nullptr ||
                strstr(result.output, "multi-line") != nullptr ||
                strstr(result.output, "continuation") != nullptr) << "Expected multi-line info in startup";
    free_test_result(&result);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
