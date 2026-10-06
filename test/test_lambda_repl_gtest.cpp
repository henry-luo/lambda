#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "../lib/shell.h"
}

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <io.h>
    #include <direct.h>
    #define access _access
    #define X_OK 0
    #define popen _popen
    #define pclose _pclose
    #define WEXITSTATUS(status) (status)
    #define LAMBDA_EXE "lambda.exe"
#else
    #include <unistd.h>
    #include <signal.h>
    #include <sys/wait.h>
    #define LAMBDA_EXE "./lambda.exe"
#endif

// Helper to run Lambda REPL and capture output
struct test_result {
    char* output;
    int exit_code;
};

// `session_args` (NULL-terminated, may be NULL) go after --no-log, so `run`
// opens a procedural session and `--tier=...` selects the satellite tier.
test_result run_lambda_repl_with(const char* input, const char* const* session_args) {
    test_result result = {nullptr, -1};

    // Create a temporary file for input
    const char* temp_file = "temp/temp_repl_input.txt";
    FILE* temp = fopen(temp_file, "w");
    if (!temp) {
        return result;
    }

    fprintf(temp, "%s\n", input);
    fclose(temp);

    const char* args[8] = {LAMBDA_EXE, "--no-log", NULL};
    int argc = 2;
    for (int i = 0; session_args && session_args[i] && argc < 7; i++) {
        args[argc++] = session_args[i];
    }
    args[argc] = NULL;
    ShellOptions options = {0};
    options.stdin_path = temp_file;
    options.merge_stderr = true;
    // File-backed stdin preserves REPL input without invoking a redirection shell.
    ShellResult shell_result = shell_exec(LAMBDA_EXE, args, &options);
    result.exit_code = shell_result.exit_code;
    result.output = shell_result.stdout_buf ? strdup(shell_result.stdout_buf) : strdup("");
    shell_result_free(&shell_result);

    unlink(temp_file);
    return result;
}

test_result run_lambda_repl(const char* input) {
    return run_lambda_repl_with(input, NULL);
}

void free_test_result(test_result* result) {
    if (result->output) {
        free(result->output);
        result->output = nullptr;
    }
}

// Test basic REPL functionality
TEST(LambdaReplTests, test_help_command) {
    test_result result = run_lambda_repl("help");
    ASSERT_NE(result.output, nullptr);
    ASSERT_TRUE(strstr(result.output, "help") != nullptr || strstr(result.output, "Lambda") != nullptr);
    free_test_result(&result);
}

TEST(LambdaReplTests, test_quit_command) {
    test_result result = run_lambda_repl("quit");
    ASSERT_NE(result.output, nullptr);
    // Should exit cleanly
    ASSERT_EQ(result.exit_code, 0);
    free_test_result(&result);
}

TEST(LambdaReplTests, test_simple_expression) {
    test_result result = run_lambda_repl("1 + 1\nquit");
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

static int count_substr(const char* text, const char* needle) {
    int count = 0;
    const char* p = text;
    while (p && *p) {
        p = strstr(p, needle);
        if (!p) break;
        count++;
        p += strlen(needle);
    }
    return count;
}

TEST(LambdaReplTests, test_runtime_error_does_not_replay_previous_output) {
    // D8.1.1v17: an entry that completes with an error is reported and rolled
    // back; earlier entries are never re-run, so their output appears once
    test_result result = run_lambda_repl("1 + 1\n[1, 2, 3] + \"hello\"\n2 + 2\nquit");
    ASSERT_NE(result.output, nullptr);
    EXPECT_EQ(count_substr(result.output, "> 2\n"), 1) << result.output;
    EXPECT_EQ(count_substr(result.output, "Entry rolled back."), 1) << result.output;
    EXPECT_EQ(count_substr(result.output, "> 4\n"), 1) << result.output;
    free_test_result(&result);
}

TEST(LambdaReplTests, test_empty_input) {
    test_result result = run_lambda_repl("");
    ASSERT_NE(result.output, nullptr);
    free_test_result(&result);
}

// Additional tests migrated from Criterion version to achieve full parity

TEST(LambdaReplTests, test_executable_exists) {
    // Executability is filesystem state; spawning a shell made this check slower and less precise.
    ASSERT_EQ(access("./lambda.exe", X_OK), 0)
        << "Lambda executable should exist and be executable";
}

TEST(LambdaReplTests, test_startup_and_quit) {
    test_result result = run_lambda_repl("quit");
    ASSERT_NE(result.output, nullptr) << "Expected output from REPL";
    ASSERT_GT(strlen(result.output), 0) << "REPL should produce output";
    ASSERT_TRUE(strstr(result.output, "Lambda") != nullptr ||
                strstr(result.output, "λ") != nullptr) << "Output should mention Lambda";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_multiple_commands) {
    test_result result = run_lambda_repl("1 + 1\n2 * 3\nquit");
    ASSERT_NE(result.output, nullptr) << "Expected output from multiple commands";
    // Should contain both results
    ASSERT_TRUE(strstr(result.output, "2") != nullptr) << "Expected to find result '2' for 1+1";
    ASSERT_TRUE(strstr(result.output, "6") != nullptr) << "Expected to find result '6' for 2*3";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_quit_variations) {
    // Test q short form
    test_result result1 = run_lambda_repl("q");
    ASSERT_NE(result1.output, nullptr) << "Expected output from q";
    free_test_result(&result1);

    // Test exit
    test_result result2 = run_lambda_repl("exit");
    ASSERT_NE(result2.output, nullptr) << "Expected output from exit";
    free_test_result(&result2);
}

TEST(LambdaReplTests, test_complex_arithmetic) {
    test_result result = run_lambda_repl("5 * 7\n8 / 2\nquit");
    ASSERT_NE(result.output, nullptr) << "Expected output from complex arithmetic";
    // Should contain both results
    ASSERT_TRUE(strstr(result.output, "35") != nullptr) << "Expected to find result '35' for 5*7";
    ASSERT_TRUE(strstr(result.output, "4") != nullptr) << "Expected to find result '4' for 8/2";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_error_recovery) {
    // `2 +` is no longer an error: S16.2.1 continues an incomplete expression
    // across a line break unconditionally, so `2 +` / `1 + 1` is `2 + 1 + 1`.
    // Use an input that is genuinely rejected, keeping the recovery intent.
    test_result result = run_lambda_repl("\"a\" < \"b\"\n1 + 1\nquit");
    ASSERT_NE(result.output, nullptr) << "Expected output from error recovery test";
    // Should continue running despite syntax error and compute 1+1=2
    ASSERT_TRUE(strstr(result.output, "error") != nullptr ||
                strstr(result.output, "Error") != nullptr ||
                strstr(result.output, "ERROR") != nullptr) << "Should show error for incomplete expression";
    ASSERT_TRUE(strstr(result.output, "2") != nullptr) << "Should recover and compute 1+1=2";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_version_display) {
    test_result result = run_lambda_repl("quit");
    ASSERT_NE(result.output, nullptr) << "Expected output from REPL";
    // Should show version information or Lambda branding
    ASSERT_GT(strlen(result.output), 0) << "Should show version/startup information";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_repl_functionality) {
    test_result result = run_lambda_repl("quit");
    ASSERT_NE(result.output, nullptr) << "Expected output to check REPL behavior";
    // In non-interactive mode, prompts may not appear but REPL should function
    bool has_startup_info = strstr(result.output, "Lambda") != nullptr ||
                           strstr(result.output, "help") != nullptr ||
                           strstr(result.output, "λ") != nullptr;
    ASSERT_TRUE(has_startup_info) << "Should show REPL startup information";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_command_sequence_stability) {
    test_result result = run_lambda_repl("1 + 1\nhelp\n2 * 2\nquit");
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
    test_result result = run_lambda_repl("quit");
    ASSERT_NE(result.output, nullptr) << "Expected output from REPL";
    // Check for Lambda prompts or startup messages
    bool has_lambda_content = strstr(result.output, "λ") != nullptr ||
                             strstr(result.output, "Lambda") != nullptr ||
                             strstr(result.output, "L>") != nullptr;
    ASSERT_TRUE(has_lambda_content) << "Should show Lambda prompt or content";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_prompt_with_expressions) {
    test_result result = run_lambda_repl("2 + 3\nquit");
    ASSERT_NE(result.output, nullptr) << "Expected output from expressions";
    // Should compute 2+3=5
    ASSERT_TRUE(strstr(result.output, "5") != nullptr) << "Expected to find result '5' for 2+3";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_unicode_prompt_support) {
    test_result result = run_lambda_repl("quit");
    ASSERT_NE(result.output, nullptr) << "Expected output from REPL";
    // Unicode support test - just ensure REPL handles input/output properly
    ASSERT_GT(strlen(result.output), 0) << "Should handle unicode input properly";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_multiple_prompt_sequence) {
    test_result result = run_lambda_repl("1\n2\n3\nquit");
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
    test_result result = run_lambda_repl("let arr = [\n  1,\n  2,\n  3\n]\narr\nquit");
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
    test_result result = run_lambda_repl("let m = {\n  a: 1,\n  b: 2\n}\nm\nquit");
    ASSERT_NE(result.output, nullptr) << "Expected output from multi-line map";
    // Should show continuation prompts
    ASSERT_TRUE(strstr(result.output, ".. ") != nullptr) << "Expected continuation prompt '.. '";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_multiline_function) {
    // Multi-line function definition
    test_result result = run_lambda_repl("let f = fn(x) {\n  x * 2\n}\nquit");
    ASSERT_NE(result.output, nullptr) << "Expected output from multi-line function";
    // Should show continuation prompts for incomplete function
    ASSERT_TRUE(strstr(result.output, ".. ") != nullptr) << "Expected continuation prompt '.. '";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_multiline_nested_brackets) {
    // Nested brackets should all be tracked
    test_result result = run_lambda_repl("let nested = [\n  [1, 2],\n  [3, 4]\n]\nnested\nquit");
    ASSERT_NE(result.output, nullptr) << "Expected output from nested brackets";
    // Should show continuation prompts
    ASSERT_TRUE(strstr(result.output, ".. ") != nullptr) << "Expected continuation prompt for nested brackets";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_multiline_parentheses) {
    // Multi-line expression with unclosed parentheses
    test_result result = run_lambda_repl("let sum = (\n  1 + 2 +\n  3 + 4\n)\nsum\nquit");
    ASSERT_NE(result.output, nullptr) << "Expected output from multi-line parentheses";
    // Should show continuation prompts
    ASSERT_TRUE(strstr(result.output, ".. ") != nullptr) << "Expected continuation prompt for unclosed parens";
    // Result should be 10
    ASSERT_TRUE(strstr(result.output, "10") != nullptr) << "Expected sum to be 10";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_multiline_string_not_incomplete) {
    // Strings with brackets inside should not trigger continuation
    test_result result = run_lambda_repl("\"hello { world }\"\nquit");
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
    test_result result = run_lambda_repl("@#$%\n5 + 5\nquit");
    ASSERT_NE(result.output, nullptr) << "Expected output after syntax error";
    // Should show error message
    ASSERT_TRUE(strstr(result.output, "Syntax error") != nullptr ||
                strstr(result.output, "error") != nullptr ||
                strstr(result.output, "Error") != nullptr) << "Expected syntax error message";
    // Should recover and compute 5+5=10
    ASSERT_TRUE(strstr(result.output, "10") != nullptr) << "Expected recovery with result '10'";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_statement_comparison_error_has_grouping_hint) {
    test_result result = run_lambda_repl("\"a\" < \"b\"\nquit");
    ASSERT_NE(result.output, nullptr) << "Expected output after syntax error";
    ASSERT_TRUE(strstr(result.output, "'<' and '>' are ambiguous with element syntax at statement level") != nullptr)
        << "Expected statement-level comparison ambiguity hint.\nOutput: " << result.output;
    ASSERT_TRUE(strstr(result.output, "Use parentheses to group the comparison expression") != nullptr)
        << "Expected grouping help text.\nOutput: " << result.output;
    free_test_result(&result);
}

TEST(LambdaReplTests, test_syntax_error_does_not_corrupt_state) {
    // After syntax error, previous valid definitions should still work
    test_result result = run_lambda_repl("let x = 100\n@invalid@\nx * 2\nquit");
    ASSERT_NE(result.output, nullptr) << "Expected output from state preservation test";
    // x should still be defined and x*2 should work
    ASSERT_TRUE(strstr(result.output, "200") != nullptr) << "Expected x*2=200 after error recovery";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_multiple_syntax_errors) {
    // Multiple syntax errors in sequence
    test_result result = run_lambda_repl("!!!\n@@@\n###\n1 + 2\nquit");
    ASSERT_NE(result.output, nullptr) << "Expected output from multiple errors";
    // Should eventually compute 1+2=3
    ASSERT_TRUE(strstr(result.output, "3") != nullptr) << "Expected result '3' after multiple errors";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_incomplete_vs_error) {
    // Incomplete (missing bracket) should wait, not error
    // Then error (invalid chars) should discard
    test_result result = run_lambda_repl("let a = [\n1\n]\n@error@\na\nquit");
    ASSERT_NE(result.output, nullptr) << "Expected output from incomplete vs error test";
    // Array should work (continuation prompt used)
    ASSERT_TRUE(strstr(result.output, ".. ") != nullptr) << "Expected continuation prompt for array";
    // Error should be reported for @error@
    ASSERT_TRUE(strstr(result.output, "Syntax error") != nullptr ||
                strstr(result.output, "error") != nullptr) << "Expected error for invalid syntax";
    free_test_result(&result);
}

// ============================================================================
// clear Command Tests
// ============================================================================

TEST(LambdaReplTests, test_clear_resets_variables) {
    // After clear, variables should be undefined
    test_result result = run_lambda_repl("let myvar = 999\nmyvar\nclear\nmyvar\nquit");
    ASSERT_NE(result.output, nullptr) << "Expected output from clear test";
    // Should show "REPL history cleared"
    ASSERT_TRUE(strstr(result.output, "cleared") != nullptr) << "Expected 'cleared' message";
    // After clear, accessing myvar should cause error
    ASSERT_TRUE(strstr(result.output, "error") != nullptr ||
                strstr(result.output, "Error") != nullptr) << "Expected error accessing cleared variable";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_clear_allows_redefinition) {
    // After clear, we can redefine variables
    test_result result = run_lambda_repl("let z = 10\nclear\nlet z = 20\nz\nquit");
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
    test_result result = run_lambda_repl("let a = 5\nlet b = 10\na + b\nquit");
    ASSERT_NE(result.output, nullptr) << "Expected output from variable persistence";
    // a + b should be 15
    ASSERT_TRUE(strstr(result.output, "15") != nullptr) << "Expected a+b=15";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_sequential_definitions) {
    // Multiple let statements in sequence
    test_result result = run_lambda_repl("let x = 1\nlet y = 2\nlet z = 3\nx + y + z\nquit");
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
    test_result result = run_lambda_repl("let arr = [\n\n1\n\n]\narr\nquit");
    ASSERT_NE(result.output, nullptr) << "Expected output with empty lines";
    // Should still work
    ASSERT_TRUE(strstr(result.output, "1") != nullptr) << "Expected array with 1";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_comment_in_multiline) {
    // Comments should not affect bracket counting
    test_result result = run_lambda_repl("let x = [\n// this is a comment with {\n1\n]\nx\nquit");
    ASSERT_NE(result.output, nullptr) << "Expected output with comment in multiline";
    // Should work - comment brackets don't count
    ASSERT_TRUE(strstr(result.output, "1") != nullptr) << "Expected array with 1";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_block_comment_incomplete) {
    // Unclosed block comment should be detected as incomplete
    test_result result = run_lambda_repl("/* this is\nstill a comment */\n1 + 1\nquit");
    ASSERT_NE(result.output, nullptr) << "Expected output with block comment";
    // Should handle block comment and compute 1+1
    ASSERT_TRUE(strstr(result.output, "2") != nullptr) << "Expected result 2";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_deeply_nested_multiline) {
    // Deeply nested structure
    test_result result = run_lambda_repl("let deep = [\n  [\n    [\n      1\n    ]\n  ]\n]\ndeep\nquit");
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
    test_result result = run_lambda_repl("let mixed = {\n  arr: [\n    (1 + 2)\n  ]\n}\nmixed\nquit");
    ASSERT_NE(result.output, nullptr) << "Expected output from mixed brackets";
    // Should show continuation prompts
    ASSERT_TRUE(strstr(result.output, ".. ") != nullptr) << "Expected continuation for mixed brackets";
    free_test_result(&result);
}

TEST(LambdaReplTests, test_multiline_startup_message) {
    // Verify startup message mentions multi-line support
    test_result result = run_lambda_repl("quit");
    ASSERT_NE(result.output, nullptr) << "Expected startup message";
    ASSERT_TRUE(strstr(result.output, "Multi-line") != nullptr ||
                strstr(result.output, "multi-line") != nullptr ||
                strstr(result.output, "continuation") != nullptr) << "Expected multi-line info in startup";
    free_test_result(&result);
}

// ============================================================================
// Persistent interpreter session (D8.1.1v17, S16.7.4-S16.7.6)
// ============================================================================

static const char* const k_procedural[] = {"run", NULL};

TEST(LambdaReplSessionTests, declaration_only_entry_echoes_nothing) {
    // S16.7.4: `let`/`fn` produce no item; a bare `null` still echoes
    test_result result = run_lambda_repl("let x = 1\nfn f(a) { a + 1 }\nf(x)\nnull\nquit");
    ASSERT_NE(result.output, nullptr);
    EXPECT_EQ(count_substr(result.output, "> > 2\n"), 1) << result.output;
    EXPECT_EQ(count_substr(result.output, "null"), 1) << result.output;
    free_test_result(&result);
}

TEST(LambdaReplSessionTests, redefinition_is_e209_reported_once) {
    // S16.7.5: the session top level is one scope; `clear` is the reset
    test_result result = run_lambda_repl(
        "fn f(a) { a + 1 }\nfn f(a) { a + 2 }\nf(1)\nlet x = 1\nlet x = 2\nx\nquit");
    ASSERT_NE(result.output, nullptr);
    EXPECT_EQ(count_substr(result.output, "error[E209]"), 2) << result.output;
    EXPECT_EQ(count_substr(result.output, "Entry rolled back."), 2) << result.output;
    EXPECT_NE(strstr(result.output, "> 2\n"), nullptr) << result.output;
    EXPECT_NE(strstr(result.output, "> 1\n"), nullptr) << result.output;
    free_test_result(&result);
}

TEST(LambdaReplSessionTests, parser_decides_incomplete_entry) {
    // D8.1.1v17: balanced input the C parser reports INCOMPLETE continues
    test_result result = run_lambda_repl("let y =\n5\ny\n1 +\n2\nquit");
    ASSERT_NE(result.output, nullptr);
    EXPECT_GE(count_substr(result.output, ".. "), 2) << result.output;
    EXPECT_NE(strstr(result.output, "5\n"), nullptr) << result.output;
    EXPECT_NE(strstr(result.output, "3\n"), nullptr) << result.output;
    EXPECT_EQ(strstr(result.output, "rolled back"), nullptr) << result.output;
    free_test_result(&result);
}

TEST(LambdaReplSessionTests, functional_session_rejects_procedural_entries) {
    // S16.7.6: `lambda` is functional, as a file's top level is
    test_result result = run_lambda_repl("var v = 1\npn p() { 1 }\np()\nquit");
    ASSERT_NE(result.output, nullptr);
    EXPECT_GE(count_substr(result.output, "error[E224]"), 2) << result.output;
    EXPECT_EQ(strstr(result.output, "Procedural session"), nullptr) << result.output;
    free_test_result(&result);
}

TEST(LambdaReplSessionTests, procedural_session_runs_statements) {
    // S16.7.6: `lambda run` warns, keeps `var` across entries, calls `pn`,
    // and statements echo nothing
    test_result result = run_lambda_repl_with(
        "var v = 1\nv = v + 41\nv\nvar k = 0\nwhile (k < 3) { k = k + 1 }\nk\n"
        "pn p(n) { n * 10 }\np(k)\nquit", k_procedural);
    ASSERT_NE(result.output, nullptr);
    EXPECT_NE(strstr(result.output, "Procedural session"), nullptr) << result.output;
    EXPECT_NE(strstr(result.output, "> > 42\n"), nullptr) << result.output;
    EXPECT_NE(strstr(result.output, "> > > 3\n"), nullptr) << result.output;
    EXPECT_NE(strstr(result.output, "> > 30\n"), nullptr) << result.output;
    EXPECT_EQ(strstr(result.output, "rolled back"), nullptr) << result.output;
    free_test_result(&result);
}

TEST(LambdaReplSessionTests, failed_entry_restores_procedural_state) {
    // D8.1.1v17: a failing entry's writes are rolled back with it
    test_result result = run_lambda_repl_with(
        "var v = 1\n{ v = 99; error(\"boom\") }\nv\nquit", k_procedural);
    ASSERT_NE(result.output, nullptr);
    EXPECT_NE(strstr(result.output, "boom"), nullptr) << result.output;
    EXPECT_NE(strstr(result.output, "Entry rolled back."), nullptr) << result.output;
    EXPECT_NE(strstr(result.output, "> 1\n"), nullptr) << result.output;
    free_test_result(&result);
}

TEST(LambdaReplSessionTests, file_import_initializes_module) {
    // D7.2.2: an entry's import cone initializes before the entry runs
    FILE* module = fopen("temp/repl_session_import_mod.ls", "w");
    ASSERT_NE(module, nullptr);
    fputs("pub fn twice(n) { n * 2 }\npub let base = 40\n", module);
    fclose(module);
    test_result result = run_lambda_repl(
        "import m: .temp.repl_session_import_mod\nm.twice(4)\nm.base + 2\nquit");
    ASSERT_NE(result.output, nullptr);
    EXPECT_NE(strstr(result.output, "> 8\n"), nullptr) << result.output;
    EXPECT_NE(strstr(result.output, "> 42\n"), nullptr) << result.output;
    free_test_result(&result);
    unlink("temp/repl_session_import_mod.ls");
}

TEST(LambdaReplSessionTests, tiers_agree) {
    // D8.1.1v17: the tier governs satellites only, so output is identical
    const char* input = "fn sq(n) { n * n }\nsq(2)\nsq(3)\nsq(4)\nsq(5)\nsq(6)\nsq(7)\nquit";
    static const char* const k_interp[] = {"--tier=interp", NULL};
    static const char* const k_jit[] = {"--tier=jit", NULL};
    test_result auto_run = run_lambda_repl(input);
    test_result interp_run = run_lambda_repl_with(input, k_interp);
    test_result jit_run = run_lambda_repl_with(input, k_jit);
    ASSERT_NE(auto_run.output, nullptr);
    ASSERT_NE(interp_run.output, nullptr);
    ASSERT_NE(jit_run.output, nullptr);
    EXPECT_NE(strstr(auto_run.output, "> > 4\n> 9\n> 16\n> 25\n> 36\n> 49\n"), nullptr)
        << auto_run.output;
    EXPECT_NE(strstr(interp_run.output, "> > 4\n> 9\n> 16\n> 25\n> 36\n> 49\n"), nullptr)
        << interp_run.output;
    EXPECT_NE(strstr(jit_run.output, "> > 4\n> 9\n> 16\n> 25\n> 36\n> 49\n"), nullptr)
        << jit_run.output;
    free_test_result(&auto_run);
    free_test_result(&interp_run);
    free_test_result(&jit_run);
}

#ifndef _WIN32
TEST(LambdaReplSessionTests, sigint_interrupts_the_running_entry) {
    // RI4: SIGINT faults the entry, rolls it back, and the session continues
    const char* input_path = "temp/repl_session_sigint_input.txt";
    const char* output_path = "temp/repl_session_sigint_output.txt";
    FILE* input = fopen(input_path, "w");
    ASSERT_NE(input, nullptr);
    fputs("var k = 0\nwhile (true) { k = k + 1 }\nk > 0\n1 + 1\nquit\n", input);
    fclose(input);
    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        freopen(input_path, "r", stdin);
        freopen(output_path, "w", stdout);
        dup2(fileno(stdout), fileno(stderr));
        execl(LAMBDA_EXE, LAMBDA_EXE, "--no-log", "run", (char*)NULL);
        _exit(127);
    }
    usleep(1500 * 1000);
    kill(pid, SIGINT);
    int status = 0;
    pid_t done = 0;
    for (int i = 0; i < 50 && done == 0; i++) {
        done = waitpid(pid, &status, WNOHANG);
        if (done == 0) usleep(100 * 1000);
    }
    if (done == 0) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        FAIL() << "the session did not finish after SIGINT";
    }
    EXPECT_TRUE(WIFEXITED(status)) << "the session was terminated by the signal";
    FILE* output = fopen(output_path, "r");
    ASSERT_NE(output, nullptr);
    char text[8192] = {0};
    size_t length = fread(text, 1, sizeof(text) - 1, output);
    fclose(output);
    text[length] = '\0';
    EXPECT_NE(strstr(text, "Interrupted"), nullptr) << text;
    EXPECT_NE(strstr(text, "> false\n"), nullptr) << text;
    EXPECT_NE(strstr(text, "> 2\n"), nullptr) << text;
    unlink(input_path);
    unlink(output_path);
}
#endif

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
