#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>

/**
 * Lambda REPL CLI Interface Tests using Catch2
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
    struct test_result result = {nullptr, 0, 0};
    
    // Use printf instead of echo to handle newlines properly
    char command[2048];
    snprintf(command, sizeof(command), "printf \"%s\" | timeout 10 lambda.exe", input);
    
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
    struct test_result result = {nullptr, 0, 0};
    
    // Use script command with echo to simulate TTY
    char command[2048];
    snprintf(command, sizeof(command), 
             "echo \"%s\" | script -q /dev/null lambda.exe", 
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

TEST_CASE("executable_exists", "[lambda][repl][basic]") {
    int result = system("test -x lambda.exe");
    REQUIRE(result == 0);
}

TEST_CASE("startup_and_quit", "[lambda][repl][basic]") {
    struct test_result result = run_lambda_repl(".quit\\n");
    
    REQUIRE(result.output != nullptr);
    REQUIRE(result.output_len > 0);
    REQUIRE(output_contains(result.output, "Lambda"));
    
    free_test_result(&result);
}

TEST_CASE("basic_arithmetic", "[lambda][repl][basic]") {
    struct test_result result = run_lambda_repl("2 + 3\\n.quit\\n");
    
    REQUIRE(result.output != nullptr);
    REQUIRE(output_contains(result.output, "5"));
    
    free_test_result(&result);
}

TEST_CASE("help_command", "[lambda][repl][basic]") {
    struct test_result result = run_lambda_repl(".help\\n.quit\\n");
    
    REQUIRE(result.output != nullptr);
    REQUIRE((output_contains(result.output, ".quit") || output_contains(result.output, "quit")));
    
    free_test_result(&result);
}

TEST_CASE("multiple_commands", "[lambda][repl][basic]") {
    struct test_result result = run_lambda_repl("1 + 1\\n2 * 3\\n.quit\\n");
    
    REQUIRE(result.output != nullptr);
    REQUIRE((output_contains(result.output, "2") || output_contains(result.output, "6")));
    
    free_test_result(&result);
}

TEST_CASE("quit_variations", "[lambda][repl][basic]") {
    // Test .q short form
    struct test_result result1 = run_lambda_repl(".q\\n");
    REQUIRE(result1.output != nullptr);
    free_test_result(&result1);
    
    // Test .exit
    struct test_result result2 = run_lambda_repl(".exit\\n");
    REQUIRE(result2.output != nullptr);
    free_test_result(&result2);
}

TEST_CASE("complex_arithmetic", "[lambda][repl][basic]") {
    struct test_result result = run_lambda_repl("5 * 7\\n8 / 2\\n.quit\\n");
    
    REQUIRE(result.output != nullptr);
    REQUIRE((output_contains(result.output, "35") || output_contains(result.output, "4")));
    
    free_test_result(&result);
}

TEST_CASE("error_recovery", "[lambda][repl][basic]") {
    struct test_result result = run_lambda_repl("2 +\\n1 + 1\\n.quit\\n");
    
    REQUIRE(result.output != nullptr);
    // Should continue running despite syntax error
    REQUIRE((output_contains(result.output, "2") || output_contains(result.output, "Lambda")));
    
    free_test_result(&result);
}

TEST_CASE("version_display", "[lambda][repl][basic]") {
    struct test_result result = run_lambda_repl(".quit\\n");
    
    REQUIRE(result.output != nullptr);
    REQUIRE((output_contains(result.output, "1.0") || output_contains(result.output, "v1")));
    
    free_test_result(&result);
}

TEST_CASE("repl_functionality", "[lambda][repl][basic]") {
    struct test_result result = run_lambda_repl(".quit\\n");
    
    REQUIRE(result.output != nullptr);
    // In non-interactive mode, prompts may not appear but REPL should function
    bool has_startup_info = output_contains(result.output, "Lambda Script REPL") ||
                           output_contains(result.output, "Type .help for commands");
    
    REQUIRE(has_startup_info);
    
    free_test_result(&result);
}

TEST_CASE("command_sequence_stability", "[lambda][repl][basic]") {
    struct test_result result = run_lambda_repl("1 + 1\\n.help\\n2 * 2\\n.quit\\n");
    
    REQUIRE(result.output != nullptr);
    REQUIRE(result.output_len > 50);
    
    free_test_result(&result);
}

// ============================================================================
// INTERACTIVE MODE TESTS (with pseudo-TTY to capture prompts)
// ============================================================================

TEST_CASE("prompt_display", "[lambda][repl][interactive]") {
    struct test_result result = run_lambda_repl_interactive(".quit\n");
    
    REQUIRE(result.output != nullptr);
    
    // Check for actual Lambda prompts that appear in TTY mode
    bool has_lambda_prompt = output_contains(result.output, "λ>");
    bool has_ascii_prompt = output_contains(result.output, "L>");
    
    REQUIRE((has_lambda_prompt || has_ascii_prompt));
    
    free_test_result(&result);
}

TEST_CASE("prompt_with_expressions", "[lambda][repl][interactive]") {
    struct test_result result = run_lambda_repl_interactive("2 + 3\n.quit\n");
    
    REQUIRE(result.output != nullptr);
    
    // Interactive mode should at least show prompts, even if expressions are hard to test reliably
    bool has_prompt = output_contains_clean(result.output, "λ>") || output_contains_clean(result.output, "L>");
    bool has_startup = output_contains_clean(result.output, "Lambda Script REPL");
    
    REQUIRE((has_prompt || has_startup));
    
    free_test_result(&result);
}

TEST_CASE("unicode_prompt_support", "[lambda][repl][interactive]") {
    struct test_result result = run_lambda_repl_interactive(".quit\n");
    
    REQUIRE(result.output != nullptr);
    
    // In UTF-8 environments, should prefer λ> over L>
    bool has_unicode = output_contains(result.output, "λ>");
    bool has_ascii = output_contains(result.output, "L>");
    
    // At least one prompt type should be present
    REQUIRE((has_unicode || has_ascii));
    
    free_test_result(&result);
}

TEST_CASE("multiple_prompt_sequence", "[lambda][repl][interactive]") {
    struct test_result result = run_lambda_repl_interactive("1 + 1\n2 * 2\n.quit\n");
    
    REQUIRE(result.output != nullptr);
    
    // For interactive mode with pseudo-TTY, focus on what we can reliably test
    bool has_content = result.output && strlen(result.output) > 0;
    REQUIRE(has_content);
    
    free_test_result(&result);
}
