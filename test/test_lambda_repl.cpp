#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <errno.h>

// Test helper function to run lambda executable with input and capture output
#include <criterion/criterion.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <ctype.h>

// Test result structure to hold output from Lambda REPL
struct test_result {
    char* stdout_data;
    char* stderr_data;
    size_t stdout_len;
    size_t stderr_len;
    int exit_code;
};

void free_test_result(struct test_result* result) {
    if (result) {
        free(result->stdout_data);
        free(result->stderr_data);
        result->stdout_data = NULL;
        result->stderr_data = NULL;
    }
}

struct test_result run_lambda_with_input(const char* input) {
    struct test_result result = {0};
    
    int stdin_pipe[2];
    int stdout_pipe[2];
    int stderr_pipe[2];
    
    if (pipe(stdin_pipe) == -1 || pipe(stdout_pipe) == -1 || pipe(stderr_pipe) == -1) {
        perror("pipe failed");
        return result;
    }
    
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        close(stdin_pipe[1]);   // Close write end of stdin pipe
        close(stdout_pipe[0]);  // Close read end of stdout pipe
        close(stderr_pipe[0]);  // Close read end of stderr pipe
        
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        
        execl("./lambda.exe", "lambda.exe", (char*)NULL);
        perror("execl failed");
        exit(1);
    } else if (pid > 0) {
        // Parent process
        close(stdin_pipe[0]);   // Close read end of stdin pipe
        close(stdout_pipe[1]);  // Close write end of stdout pipe
        close(stderr_pipe[1]);  // Close write end of stderr pipe
        
        // Write input to child's stdin
        write(stdin_pipe[1], input, strlen(input));
        close(stdin_pipe[1]);
        
        // Read stdout
        char stdout_buffer[4096] = {0};
        ssize_t stdout_bytes = read(stdout_pipe[0], stdout_buffer, sizeof(stdout_buffer) - 1);
        if (stdout_bytes > 0) {
            result.stdout_data = (char*)malloc(stdout_bytes + 1);
            memcpy(result.stdout_data, stdout_buffer, stdout_bytes);
            result.stdout_data[stdout_bytes] = '\0';
            result.stdout_len = stdout_bytes;
        }
        
        // Read stderr
        char stderr_buffer[4096] = {0};
        ssize_t stderr_bytes = read(stderr_pipe[0], stderr_buffer, sizeof(stderr_buffer) - 1);
        if (stderr_bytes > 0) {
            result.stderr_data = (char*)malloc(stderr_bytes + 1);
            memcpy(result.stderr_data, stderr_buffer, stderr_bytes);
            result.stderr_data[stderr_bytes] = '\0';
            result.stderr_len = stderr_bytes;
        }
        
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        
        // Wait for child to complete
        int status;
        waitpid(pid, &status, 0);
        result.exit_code = WEXITSTATUS(status);
    } else {
        perror("fork failed");
    }
    
    return result;
}

// Test basic REPL startup and exit
Test(lambda_repl, startup_and_exit) {
    struct test_result result = run_lambda_with_input(".quit\n");
    
    cr_assert_not_null(result.stdout_data, "Expected stdout output");
    cr_assert(strstr(result.stdout_data, "Lambda Script REPL") != NULL, 
              "Expected REPL startup message");
    cr_assert(strstr(result.stdout_data, ".help for commands") != NULL,
              "Expected REPL help message with dot commands");
    cr_assert_eq(result.exit_code, 0, "Expected clean exit");
    
    free_test_result(&result);
}

// Test .help command
Test(lambda_repl, help_command) {
    struct test_result result = run_lambda_with_input(".help\n.quit\n");
    
    cr_assert_not_null(result.stdout_data, "Expected stdout output");
    cr_assert(strstr(result.stdout_data, "Lambda Script") != NULL,
              "Expected help content");
    cr_assert(strstr(result.stdout_data, ".quit") != NULL,
              "Expected dot-prefixed quit commands");
    cr_assert(strstr(result.stdout_data, ".help") != NULL,
              "Expected dot-prefixed help commands");
    cr_assert(strstr(result.stdout_data, ".clear") != NULL,
              "Expected dot-prefixed clear command");
    
    free_test_result(&result);
}

// Test short help command
Test(lambda_repl, help_short_command) {
    struct test_result result = run_lambda_with_input(".h\n.quit\n");
    
    cr_assert_not_null(result.stdout_data, "Expected stdout output");
    cr_assert(strstr(result.stdout_data, "Lambda Script Interpreter v1.0") != NULL,
              "Expected help header from .h command");
    
    free_test_result(&result);
}

// Test basic arithmetic evaluation
Test(lambda_repl, basic_arithmetic) {
    struct test_result result = run_lambda_with_input("2 + 3\n.quit\n");
    
    cr_assert_not_null(result.stdout_data, "Expected stdout output");
    cr_assert(strstr(result.stdout_data, "5") != NULL,
              "Expected arithmetic result");
    
    free_test_result(&result);
}

// Test multiple expressions
Test(lambda_repl, multiple_expressions) {
    struct test_result result = run_lambda_with_input("5 * 7\n10 - 4\n.quit\n");
    
    cr_assert_not_null(result.stdout_data, "Expected stdout output");
    cr_assert(strstr(result.stdout_data, "35") != NULL,
              "Expected first arithmetic result");
    cr_assert(strstr(result.stdout_data, "6") != NULL,
              "Expected second arithmetic result");
    
    free_test_result(&result);
}

// Test .clear command
Test(lambda_repl, clear_command) {
    struct test_result result = run_lambda_with_input("1 + 1\n.clear\n2 * 2\n.quit\n");
    
    cr_assert_not_null(result.stdout_data, "Expected stdout output");
    cr_assert(strstr(result.stdout_data, "REPL history cleared") != NULL,
              "Expected clear confirmation message");
    cr_assert(strstr(result.stdout_data, "2") != NULL,
              "Expected first result");
    cr_assert(strstr(result.stdout_data, "4") != NULL,
              "Expected second result after clear");
    
    free_test_result(&result);
}

// Test .q (short quit) command
Test(lambda_repl, quit_short_command) {
    struct test_result result = run_lambda_with_input("3 + 4\n.q\n");
    
    cr_assert_not_null(result.stdout_data, "Expected stdout output");
    cr_assert(strstr(result.stdout_data, "7") != NULL,
              "Expected arithmetic result before quit");
    cr_assert_eq(result.exit_code, 0, "Expected clean exit with .q");
    
    free_test_result(&result);
}

// Test .exit command
Test(lambda_repl, exit_command) {
    struct test_result result = run_lambda_with_input("8 / 2\n.exit\n");
    
    cr_assert_not_null(result.stdout_data, "Expected stdout output");
    cr_assert(strstr(result.stdout_data, "4") != NULL,
              "Expected arithmetic result before exit");
    cr_assert_eq(result.exit_code, 0, "Expected clean exit with .exit");
    
    free_test_result(&result);
}

// Test old colon commands are rejected
Test(lambda_repl, old_colon_commands_rejected) {
    struct test_result result = run_lambda_with_input(":help\n.quit\n");
    
    cr_assert_not_null(result.stderr_data, "Expected stderr output for syntax error");
    cr_assert(strstr(result.stderr_data, "ERROR") != NULL,
              "Expected error for old colon command");
    cr_assert(strstr(result.stdout_data, "null") != NULL,
              "Expected null result for failed parse");
    
    free_test_result(&result);
}

// Test string expressions
Test(lambda_repl, string_expressions) {
    struct test_result result = run_lambda_with_input("let text = \"Hello, Lambda!\"; text\n.quit\n");
    
    cr_assert_not_null(result.stdout_data, "Expected stdout output");
    cr_assert(strstr(result.stdout_data, "Hello, Lambda!") != NULL,
              "Expected string result");
    
    free_test_result(&result);
}

// Test empty lines are handled gracefully
Test(lambda_repl, empty_lines) {
    struct test_result result = run_lambda_with_input("\n\n1 + 1\n\n.quit\n");
    
    cr_assert_not_null(result.stdout_data, "Expected stdout output");
    cr_assert(strstr(result.stdout_data, "2") != NULL,
              "Expected arithmetic result despite empty lines");
    cr_assert_eq(result.exit_code, 0, "Expected clean exit");
    
    free_test_result(&result);
}

// Test complex expressions
Test(lambda_repl, complex_expressions) {
    struct test_result result = run_lambda_with_input("let x = 5; let y = 10; x * y + 2\n.quit\n");
    
    cr_assert_not_null(result.stdout_data, "Expected stdout output");
    cr_assert(strstr(result.stdout_data, "52") != NULL,
              "Expected complex expression result");
    
    free_test_result(&result);
}

// Test REPL prompt appears correctly
Test(lambda_repl, prompt_detection) {
    struct test_result result = run_lambda_with_input(".quit\n");
    
    cr_assert_not_null(result.stdout_data, "Expected stdout output");
    // Check for either λ> or L> prompt depending on system capabilities
    bool has_lambda_prompt = strstr(result.stdout_data, "λ>") != NULL;
    bool has_fallback_prompt = strstr(result.stdout_data, "L>") != NULL;
    cr_assert(has_lambda_prompt || has_fallback_prompt,
              "Expected either Unicode lambda or ASCII L prompt");
    
    free_test_result(&result);
}

// Test error handling for invalid syntax
Test(lambda_repl, invalid_syntax_handling) {
    struct test_result result = run_lambda_with_input("2 +\n.quit\n");
    
    cr_assert_not_null(result.stderr_data, "Expected stderr output for syntax error");
    cr_assert(strstr(result.stderr_data, "ERROR") != NULL,
              "Expected error message for invalid syntax");
    
    free_test_result(&result);
}

// Test case insensitive commands don't work (commands should be exact)
Test(lambda_repl, case_sensitive_commands) {
    struct test_result result = run_lambda_with_input(".QUIT\n.quit\n");
    
    cr_assert_not_null(result.stderr_data, "Expected stderr output for invalid command");
    // .QUIT should be treated as invalid Lambda syntax, not as a command
    cr_assert(strstr(result.stderr_data, "ERROR") != NULL,
              "Expected error for case-insensitive command");
    
    free_test_result(&result);
}

// Test command history accumulation (basic test)
Test(lambda_repl, history_accumulation) {
    struct test_result result = run_lambda_with_input("let a = 1\nlet b = 2\na + b\n.quit\n");
    
    cr_assert_not_null(result.stdout_data, "Expected stdout output");
    cr_assert(strstr(result.stdout_data, "3") != NULL,
              "Expected result from accumulated history");
    
    free_test_result(&result);
}

// Test version information in startup
Test(lambda_repl, version_information) {
    struct test_result result = run_lambda_with_input(".quit\n");
    
    cr_assert_not_null(result.stdout_data, "Expected stdout output");
    cr_assert(strstr(result.stdout_data, "v1.0") != NULL,
              "Expected version number in startup message");
    
    free_test_result(&result);
}
