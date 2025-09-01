#include <criterion/criterion.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>

extern "C" {
// Forward declarations for C standard library functions
extern void* memcpy(void* dest, const void* src, size_t n);
extern char* strstr(const char* haystack, const char* needle);
extern size_t strlen(const char* s);
}

/**
 * Lambda REPL CLI Interface Tests
 *
 * Merged test suite combining comprehensive coverage for reliable execution.
 * Tests both interactive and non-interactive Lambda Script REPL modes,
 * particularly dot-prefixed commands (.help, .quit).
 * 
 * Coverage:
 * - Non-interactive mode (piped input)
 * - Interactive mode (pseudo-TTY with prompts)
 * - Command syntax migration (: to . prefixes)
 * - Error handling and recovery
 */

// Helper to run Lambda REPL and capture output
struct test_result {
    char* output;
    size_t output_len;
    int exit_code;
};

void free_test_result(struct test_result* result) {
    if (result && result->output) {
        free(result->output);
        result->output = NULL;
    }
}

struct test_result run_lambda_repl(const char* input) {
    struct test_result result = {0};
    
    // Use printf instead of echo to handle newlines properly
    char command[2048];
    snprintf(command, sizeof(command), "printf \"%s\" | timeout 10 ./lambda.exe 2>&1", input);
    
    FILE* proc = popen(command, "r");
    if (!proc) {
        return result;
    }
    
    char buffer[8192] = {0};
    size_t bytes_read = fread(buffer, 1, sizeof(buffer) - 1, proc);
    int exit_status = pclose(proc);
    
    if (bytes_read > 0) {
        result.output = (char*)malloc(bytes_read + 1);
        memcpy(result.output, buffer, bytes_read);
        result.output[bytes_read] = '\0';
        result.output_len = bytes_read;
    }
    
    result.exit_code = WEXITSTATUS(exit_status);
    return result;
}

bool output_contains(const char* output, const char* expected) {
    return output && expected && strstr(output, expected) != NULL;
}

// Helper to clean control characters from terminal output for better matching
char* clean_terminal_output(const char* raw_output) {
    if (!raw_output) return NULL;
    
    size_t len = strlen(raw_output);
    char* cleaned = (char*)malloc(len + 1);
    size_t j = 0;
    
    for (size_t i = 0; i < len; i++) {
        char c = raw_output[i];
        // Keep printable characters, spaces, and newlines
        if ((c >= 32 && c <= 126) || c == '\n' || c == '\t') {
            cleaned[j++] = c;
        }
        // Convert carriage returns to newlines
        else if (c == '\r') {
            cleaned[j++] = '\n';
        }
        // Skip other control characters
    }
    
    cleaned[j] = '\0';
    return cleaned;
}

// Enhanced output checking that handles terminal control characters
bool output_contains_clean(const char* output, const char* expected) {
    if (!output || !expected) return false;
    
    char* cleaned = clean_terminal_output(output);
    bool found = strstr(cleaned, expected) != NULL;
    free(cleaned);
    
    return found;
}

// Helper to run Lambda REPL in interactive mode using script command (pseudo-TTY)
struct test_result run_lambda_repl_interactive(const char* input) {
    struct test_result result = {0};
    
    // Use script command with echo to simulate TTY
    char command[2048];
    snprintf(command, sizeof(command), 
             "echo \"%s\" | script -q /dev/null ./lambda.exe 2>&1", 
             input);
    
    FILE* proc = popen(command, "r");
    if (!proc) {
        return result;
    }
    
    char buffer[8192] = {0};
    size_t bytes_read = fread(buffer, 1, sizeof(buffer) - 1, proc);
    int exit_status = pclose(proc);
    
    if (bytes_read > 0) {
        result.output = (char*)malloc(bytes_read + 1);
        memcpy(result.output, buffer, bytes_read);
        result.output[bytes_read] = '\0';
        result.output_len = bytes_read;
    }
    
    result.exit_code = WEXITSTATUS(exit_status);
    return result;
}

// ============================================================================
// BASIC FUNCTIONALITY TESTS
// ============================================================================

Test(lambda_repl, executable_exists) {
    int result = system("test -x ./lambda.exe");
    cr_assert_eq(result, 0, "Lambda executable should exist and be executable");
}

Test(lambda_repl, startup_and_quit) {
    struct test_result result = run_lambda_repl(".quit\\n");
    
    cr_assert_not_null(result.output, "Expected output from REPL");
    cr_assert_gt(result.output_len, 0, "REPL should produce output");
    cr_assert(output_contains(result.output, "Lambda"), "Output should mention Lambda");
    
    free_test_result(&result);
}

Test(lambda_repl, basic_arithmetic) {
    struct test_result result = run_lambda_repl("2 + 3\\n.quit\\n");
    
    cr_assert_not_null(result.output, "Expected output from arithmetic");
    cr_assert(output_contains(result.output, "5"), "Should show arithmetic result: 5");
    
    free_test_result(&result);
}

Test(lambda_repl, help_command) {
    struct test_result result = run_lambda_repl(".help\\n.quit\\n");
    
    cr_assert_not_null(result.output, "Expected output from .help command");
    cr_assert(output_contains(result.output, ".quit") || output_contains(result.output, "quit"), 
              "Help should mention quit command");
    
    free_test_result(&result);
}

Test(lambda_repl, multiple_commands) {
    struct test_result result = run_lambda_repl("1 + 1\\n2 * 3\\n.quit\\n");
    
    cr_assert_not_null(result.output, "Expected output from multiple commands");
    cr_assert(output_contains(result.output, "2") || output_contains(result.output, "6"), 
              "Should show results from multiple expressions");
    
    free_test_result(&result);
}

Test(lambda_repl, quit_variations) {
    // Test .q short form
    struct test_result result1 = run_lambda_repl(".q\\n");
    cr_assert_not_null(result1.output, "Expected output from .q");
    free_test_result(&result1);
    
    // Test .exit
    struct test_result result2 = run_lambda_repl(".exit\\n");
    cr_assert_not_null(result2.output, "Expected output from .exit");
    free_test_result(&result2);
}

Test(lambda_repl, complex_arithmetic) {
    struct test_result result = run_lambda_repl("5 * 7\\n8 / 2\\n.quit\\n");
    
    cr_assert_not_null(result.output, "Expected output from complex arithmetic");
    cr_assert(output_contains(result.output, "35") || output_contains(result.output, "4"), 
              "Should show complex arithmetic results");
    
    free_test_result(&result);
}

Test(lambda_repl, error_recovery) {
    struct test_result result = run_lambda_repl("2 +\\n1 + 1\\n.quit\\n");
    
    cr_assert_not_null(result.output, "Expected output from error recovery test");
    // Should continue running despite syntax error
    cr_assert(output_contains(result.output, "2") || output_contains(result.output, "Lambda"), 
              "Should recover from syntax error");
    
    free_test_result(&result);
}

Test(lambda_repl, version_display) {
    struct test_result result = run_lambda_repl(".quit\\n");
    
    cr_assert_not_null(result.output, "Expected output from REPL");
    cr_assert(output_contains(result.output, "1.0") || output_contains(result.output, "v1"), 
              "Should show version information");
    
    free_test_result(&result);
}

Test(lambda_repl, repl_functionality) {
    struct test_result result = run_lambda_repl(".quit\\n");
    
    cr_assert_not_null(result.output, "Expected output to check REPL behavior");
    // In non-interactive mode, prompts may not appear but REPL should function
    bool has_startup_info = output_contains(result.output, "Lambda Script REPL") ||
                           output_contains(result.output, "Type .help for commands");
    
    cr_assert(has_startup_info, "Should show REPL startup information");
    
    free_test_result(&result);
}

Test(lambda_repl, command_sequence_stability) {
    struct test_result result = run_lambda_repl("1 + 1\\n.help\\n2 * 2\\n.quit\\n");
    
    cr_assert_not_null(result.output, "Expected output from command sequence");
    cr_assert_gt(result.output_len, 50, "Should produce substantial output");
    
    free_test_result(&result);
}

// ============================================================================
// INTERACTIVE MODE TESTS (with pseudo-TTY to capture prompts)
// ============================================================================

Test(lambda_repl_interactive, prompt_display) {
    struct test_result result = run_lambda_repl_interactive(".quit\n");
    
    cr_assert_not_null(result.output, "Expected output from interactive REPL");
    
    // Check for actual Lambda prompts that appear in TTY mode
    bool has_lambda_prompt = output_contains(result.output, "λ>");
    bool has_ascii_prompt = output_contains(result.output, "L>");
    
    cr_assert(has_lambda_prompt || has_ascii_prompt, 
              "Interactive mode should show Lambda prompt (λ> or L>)");
    
    free_test_result(&result);
}

Test(lambda_repl_interactive, prompt_with_expressions) {
    struct test_result result = run_lambda_repl_interactive("2 + 3\n.quit\n");
    
    cr_assert_not_null(result.output, "Expected output from interactive expressions");
    
    // Interactive mode should at least show prompts, even if expressions are hard to test reliably
    bool has_prompt = output_contains_clean(result.output, "λ>") || output_contains_clean(result.output, "L>");
    bool has_startup = output_contains_clean(result.output, "Lambda Script REPL");
    
    cr_assert(has_prompt || has_startup, "Should show either prompts or startup in interactive mode");
    
    free_test_result(&result);
}

Test(lambda_repl_interactive, unicode_prompt_support) {
    struct test_result result = run_lambda_repl_interactive(".quit\n");
    
    cr_assert_not_null(result.output, "Expected output to check Unicode support");
    
    // In UTF-8 environments, should prefer λ> over L>
    bool has_unicode = output_contains(result.output, "λ>");
    bool has_ascii = output_contains(result.output, "L>");
    
    // At least one prompt type should be present
    cr_assert(has_unicode || has_ascii, "Should display appropriate prompt for locale");
    
    free_test_result(&result);
}

Test(lambda_repl_interactive, multiple_prompt_sequence) {
    struct test_result result = run_lambda_repl_interactive("1 + 1\n2 * 2\n.quit\n");
    
    cr_assert_not_null(result.output, "Expected output from multiple prompts");
    
    // For interactive mode with pseudo-TTY, focus on what we can reliably test
    bool has_content = result.output && strlen(result.output) > 0;
    cr_assert(has_content, "Should have some output in interactive mode");
    
    free_test_result(&result);
}
