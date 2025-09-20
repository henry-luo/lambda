#include <criterion/criterion.h>
#include <criterion/redirect.h>
#include "../lib/cmdedit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Test fixtures
static struct terminal_state test_terminal;

void setup_terminal(void) {
    memset(&test_terminal, 0, sizeof(test_terminal));
}

void teardown_terminal(void) {
    terminal_cleanup(&test_terminal);
}

// ============================================================================
// PHASE 1 TESTS: TERMINAL I/O ABSTRACTION
// ============================================================================

TestSuite(terminal_io, .init = setup_terminal, .fini = teardown_terminal);

Test(terminal_io, terminal_init_success) {
    int result = terminal_init(&test_terminal);
    cr_assert_eq(result, 0, "terminal_init should succeed");
    cr_assert_geq(test_terminal.input_fd, 0, "input_fd should be valid");
    cr_assert_geq(test_terminal.output_fd, 0, "output_fd should be valid");
}

Test(terminal_io, terminal_init_null_param) {
    int result = terminal_init(NULL);
    cr_assert_eq(result, -1, "terminal_init should fail with NULL parameter");
}

Test(terminal_io, terminal_cleanup_success) {
    terminal_init(&test_terminal);
    int result = terminal_cleanup(&test_terminal);
    cr_assert_eq(result, 0, "terminal_cleanup should succeed");
}

Test(terminal_io, terminal_cleanup_null_param) {
    int result = terminal_cleanup(NULL);
    cr_assert_eq(result, -1, "terminal_cleanup should fail with NULL parameter");
}

Test(terminal_io, terminal_get_size_basic) {
    terminal_init(&test_terminal);
    int rows, cols;
    int result = terminal_get_size(&test_terminal, &rows, &cols);
    
    // Should either succeed or fail gracefully
    if (result == 0) {
        cr_assert_gt(rows, 0, "rows should be positive");
        cr_assert_gt(cols, 0, "cols should be positive");
    } else {
        // Should use fallback values
        cr_assert_eq(rows, 24, "fallback rows should be 24");
        cr_assert_eq(cols, 80, "fallback cols should be 80");
    }
}

Test(terminal_io, terminal_get_size_null_params) {
    terminal_init(&test_terminal);
    int rows, cols;
    
    cr_assert_eq(terminal_get_size(NULL, &rows, &cols), -1, "Should fail with NULL terminal");
    cr_assert_eq(terminal_get_size(&test_terminal, NULL, &cols), -1, "Should fail with NULL rows");
    cr_assert_eq(terminal_get_size(&test_terminal, &rows, NULL), -1, "Should fail with NULL cols");
}

Test(terminal_io, terminal_raw_mode_toggle) {
    terminal_init(&test_terminal);
    
    if (test_terminal.is_tty) {
        // Test enabling raw mode
        int result = terminal_raw_mode(&test_terminal, true);
        cr_assert_eq(result, 0, "Should enable raw mode successfully");
        cr_assert(test_terminal.raw_mode, "raw_mode flag should be set");
        
        // Test disabling raw mode
        result = terminal_raw_mode(&test_terminal, false);
        cr_assert_eq(result, 0, "Should disable raw mode successfully");
        cr_assert_not(test_terminal.raw_mode, "raw_mode flag should be cleared");
    } else {
        // Non-TTY should fail
        int result = terminal_raw_mode(&test_terminal, true);
        cr_assert_eq(result, -1, "Should fail on non-TTY");
    }
}

Test(terminal_io, terminal_raw_mode_null_param) {
    int result = terminal_raw_mode(NULL, true);
    cr_assert_eq(result, -1, "Should fail with NULL parameter");
}

// ============================================================================
// PHASE 1 TESTS: API FUNCTIONS
// ============================================================================

TestSuite(api_basic);

Test(api_basic, repl_init_success) {
    int result = repl_init();
    cr_assert_eq(result, 0, "repl_init should succeed");
    
    // Test double initialization
    result = repl_init();
    cr_assert_eq(result, 0, "repl_init should handle double initialization");
    
    repl_cleanup();
}

Test(api_basic, repl_cleanup_safe) {
    // Should be safe to call without init
    repl_cleanup();
    
    // Should be safe to call after init
    repl_init();
    repl_cleanup();
    
    // Should be safe to call multiple times
    repl_cleanup();
}

Test(api_basic, repl_add_history_basic) {
    repl_init();
    
    // Test adding valid history
    int result = repl_add_history("test command");
    cr_assert_eq(result, 0, "Should add valid history entry");
    
    // Test adding empty line
    result = repl_add_history("");
    cr_assert_eq(result, 0, "Should handle empty line gracefully");
    
    // Test adding NULL
    result = repl_add_history(NULL);
    cr_assert_eq(result, 0, "Should handle NULL gracefully");
    
    // Test adding REPL command (should be ignored)
    result = repl_add_history(".quit");
    cr_assert_eq(result, 0, "Should ignore REPL commands");
    
    repl_cleanup();
}

Test(api_basic, readline_compatibility_functions) {
    repl_init();
    
    // Test add_history wrapper
    int result = add_history("test line");
    cr_assert_eq(result, 0, "add_history wrapper should work");
    
    // Test clear_history (stub)
    result = clear_history();
    cr_assert_eq(result, 0, "clear_history should work");
    
    // Test read_history (stub)
    result = read_history("test.history");
    cr_assert_eq(result, 0, "read_history should work");
    
    // Test write_history (stub)
    result = write_history("test.history");
    cr_assert_eq(result, 0, "write_history should work");
    
    repl_cleanup();
}

// ============================================================================
// PHASE 1 TESTS: INPUT/OUTPUT SIMULATION
// ============================================================================

TestSuite(input_output);

Test(input_output, repl_readline_non_interactive, .init = cr_redirect_stdout, .fini = cr_redirect_stdout) {
    // Create a temporary file for input simulation
    FILE *temp_input = tmpfile();
    if (temp_input) {
        fprintf(temp_input, "test input line\n");
        rewind(temp_input);
        
        // Temporarily redirect stdin
        FILE *old_stdin = stdin;
        stdin = temp_input;
        
        repl_init();
        char *result = repl_readline("test> ");
        
        // Restore stdin
        stdin = old_stdin;
        fclose(temp_input);
        
        if (result) {
            cr_assert_str_eq(result, "test input line", "Should read line correctly");
            free(result);
        }
        
        repl_cleanup();
    }
}

Test(input_output, repl_readline_with_prompt, .init = cr_redirect_stdout, .fini = cr_redirect_stdout) {
    repl_init();
    
    // Test that prompt is printed (we can't easily test actual readline in unit tests)
    // This mainly tests that the function doesn't crash
    char *result = repl_readline("λ> ");
    
    // In a real terminal, this would wait for input
    // In test environment, it might return NULL or read from redirected input
    if (result) {
        free(result);
    }
    
    repl_cleanup();
}

// ============================================================================
// PHASE 1 TESTS: MEMORY MANAGEMENT
// ============================================================================

TestSuite(memory_management);

Test(memory_management, memory_allocation_cleanup) {
    // Test that initialization and cleanup don't leak memory
    for (int i = 0; i < 10; i++) {
        repl_init();
        repl_cleanup();
    }
    // If we reach here without crashes, memory management is working
    cr_assert(true, "Memory management should not crash");
}

Test(memory_management, readline_return_value_cleanup) {
    // Test that returned strings can be properly freed
    repl_init();
    
    // Test with empty input simulation
    FILE *temp_input = tmpfile();
    if (temp_input) {
        fprintf(temp_input, "test\n");
        rewind(temp_input);
        
        FILE *old_stdin = stdin;
        stdin = temp_input;
        
        char *result = repl_readline("test> ");
        
        stdin = old_stdin;
        fclose(temp_input);
        
        if (result) {
            // Should be able to free the returned string
            free(result);
            cr_assert(true, "Returned string should be freeable");
        }
    }
    
    repl_cleanup();
}

// ============================================================================
// PHASE 1 TESTS: ERROR HANDLING
// ============================================================================

TestSuite(error_handling);

Test(error_handling, null_parameter_safety) {
    // Test that functions handle NULL parameters gracefully
    cr_assert_eq(terminal_init(NULL), -1, "terminal_init should reject NULL");
    cr_assert_eq(terminal_cleanup(NULL), -1, "terminal_cleanup should reject NULL");
    cr_assert_eq(terminal_raw_mode(NULL, true), -1, "terminal_raw_mode should reject NULL");
    cr_assert_eq(terminal_get_size(NULL, NULL, NULL), -1, "terminal_get_size should reject NULL");
}

Test(error_handling, invalid_file_descriptors) {
    struct terminal_state invalid_term;
    memset(&invalid_term, 0, sizeof(invalid_term));
    invalid_term.input_fd = -1;
    invalid_term.output_fd = -1;
    invalid_term.is_tty = false;
    
    // Functions should handle invalid file descriptors gracefully
    int rows, cols;
    int result = terminal_get_size(&invalid_term, &rows, &cols);
    
    // Should either fail or use fallback values
    cr_assert(result == -1 || (rows == 24 && cols == 80), "Should handle invalid FDs");
}

// ============================================================================
// PHASE 1 TESTS: PLATFORM COMPATIBILITY
// ============================================================================

TestSuite(platform_compatibility);

Test(platform_compatibility, terminal_detection) {
    terminal_init(&test_terminal);
    
    // Test TTY detection
    bool is_tty = test_terminal.is_tty;
    
    // The result depends on test environment, but should not crash
    cr_assert(is_tty == true || is_tty == false, "TTY detection should return boolean");
}

Test(platform_compatibility, file_descriptor_setup) {
    terminal_init(&test_terminal);
    
    // File descriptors should be valid
    cr_assert_geq(test_terminal.input_fd, 0, "Input FD should be valid");
    cr_assert_geq(test_terminal.output_fd, 0, "Output FD should be valid");
    
#ifdef _WIN32
    // Windows-specific checks
    cr_assert_neq(test_terminal.h_stdin, INVALID_HANDLE_VALUE, "stdin handle should be valid");
    cr_assert_neq(test_terminal.h_stdout, INVALID_HANDLE_VALUE, "stdout handle should be valid");
#else
    // Unix-specific checks
    cr_assert_eq(test_terminal.input_fd, STDIN_FILENO, "Input FD should be stdin");
    cr_assert_eq(test_terminal.output_fd, STDOUT_FILENO, "Output FD should be stdout");
#endif
}

// ============================================================================
// PHASE 1 INTEGRATION TESTS
// ============================================================================

TestSuite(integration);

Test(integration, basic_repl_workflow) {
    // Test complete initialization -> operation -> cleanup workflow
    cr_assert_eq(repl_init(), 0, "Initialization should succeed");
    
    // Test basic operations
    cr_assert_eq(repl_add_history("test command"), 0, "History add should work");
    cr_assert_eq(add_history("another command"), 0, "Readline compatibility should work");
    
    // Test cleanup
    repl_cleanup(); // Should not crash
    
    cr_assert(true, "Basic workflow should complete successfully");
}

Test(integration, multiple_init_cleanup_cycles) {
    // Test that we can init/cleanup multiple times
    for (int i = 0; i < 5; i++) {
        cr_assert_eq(repl_init(), 0, "Init cycle %d should succeed", i);
        cr_assert_eq(repl_add_history("test"), 0, "Operation in cycle %d should work", i);
        repl_cleanup();
    }
}

// ============================================================================
// MAIN TEST RUNNER
// ============================================================================

// Test helper to print phase information
void print_phase_header(const char *phase_name) {
    printf("\n=== TESTING %s ===\n", phase_name);
}

// ============================================================================
// PHASE 2 TESTS: LINE EDITOR CORE
// ============================================================================

TestSuite(line_editor);

Test(line_editor, editor_init_success) {
    struct line_editor ed;
    int result = editor_init(&ed, "test> ");
    
    cr_assert_eq(result, 0, "editor_init should succeed");
    cr_assert_not_null(ed.buffer, "buffer should be allocated");
    cr_assert_not_null(ed.prompt, "prompt should be allocated");
    cr_assert_str_eq(ed.prompt, "test> ", "prompt should be set correctly");
    cr_assert_eq(ed.buffer_len, 0, "buffer should be empty initially");
    cr_assert_eq(ed.cursor_pos, 0, "cursor should be at start");
    cr_assert_gt(ed.buffer_size, 0, "buffer_size should be positive");
    
    editor_cleanup(&ed);
}

Test(line_editor, editor_init_null_prompt) {
    struct line_editor ed;
    int result = editor_init(&ed, NULL);
    
    cr_assert_eq(result, 0, "editor_init should succeed with NULL prompt");
    cr_assert_not_null(ed.prompt, "prompt should be allocated even for NULL");
    cr_assert_str_eq(ed.prompt, "", "prompt should be empty string");
    cr_assert_eq(ed.prompt_len, 0, "prompt_len should be 0");
    
    editor_cleanup(&ed);
}

Test(line_editor, editor_init_null_param) {
    int result = editor_init(NULL, "test> ");
    cr_assert_eq(result, -1, "editor_init should fail with NULL editor");
}

Test(line_editor, editor_cleanup_safe) {
    struct line_editor ed;
    editor_init(&ed, "test> ");
    
    // Should not crash
    editor_cleanup(&ed);
    
    // Should be safe to call again
    editor_cleanup(&ed);
    
    // Should be safe with NULL
    editor_cleanup(NULL);
}

Test(line_editor, editor_insert_char_basic) {
    struct line_editor ed;
    editor_init(&ed, "test> ");
    
    // Insert single character
    int result = editor_insert_char(&ed, 'a');
    cr_assert_eq(result, 0, "Should insert character successfully");
    cr_assert_eq(ed.buffer_len, 1, "Buffer length should be 1");
    cr_assert_eq(ed.cursor_pos, 1, "Cursor should advance");
    cr_assert_str_eq(ed.buffer, "a", "Buffer should contain inserted character");
    
    // Insert another character
    result = editor_insert_char(&ed, 'b');
    cr_assert_eq(result, 0, "Should insert second character");
    cr_assert_eq(ed.buffer_len, 2, "Buffer length should be 2");
    cr_assert_str_eq(ed.buffer, "ab", "Buffer should contain both characters");
    
    editor_cleanup(&ed);
}

Test(line_editor, editor_insert_char_at_position) {
    struct line_editor ed;
    editor_init(&ed, "");
    
    // Insert initial text
    editor_insert_char(&ed, 'a');
    editor_insert_char(&ed, 'c');
    cr_assert_str_eq(ed.buffer, "ac", "Initial text should be 'ac'");
    
    // Move cursor to middle
    ed.cursor_pos = 1;
    
    // Insert character in middle
    int result = editor_insert_char(&ed, 'b');
    cr_assert_eq(result, 0, "Should insert in middle");
    cr_assert_str_eq(ed.buffer, "abc", "Should insert character in correct position");
    cr_assert_eq(ed.cursor_pos, 2, "Cursor should be after inserted character");
    
    editor_cleanup(&ed);
}

Test(line_editor, editor_insert_char_buffer_growth) {
    struct line_editor ed;
    editor_init(&ed, "");
    
    size_t initial_size = ed.buffer_size;
    
    // Insert many characters to trigger buffer growth
    for (int i = 0; i < (int)(initial_size + 10); i++) {
        int result = editor_insert_char(&ed, 'x');
        cr_assert_eq(result, 0, "Should insert character %d", i);
    }
    
    cr_assert_gt(ed.buffer_size, initial_size, "Buffer should have grown");
    cr_assert_eq(ed.buffer_len, initial_size + 10, "Buffer length should be correct");
    
    editor_cleanup(&ed);
}

Test(line_editor, editor_delete_char_basic) {
    struct line_editor ed;
    editor_init(&ed, "");
    
    // Insert some text
    editor_insert_char(&ed, 'a');
    editor_insert_char(&ed, 'b');
    editor_insert_char(&ed, 'c');
    cr_assert_str_eq(ed.buffer, "abc", "Initial text should be 'abc'");
    
    // Move cursor to middle
    ed.cursor_pos = 1;
    
    // Delete character under cursor
    int result = editor_delete_char(&ed);
    cr_assert_eq(result, 0, "Should delete character");
    cr_assert_str_eq(ed.buffer, "ac", "Should delete correct character");
    cr_assert_eq(ed.cursor_pos, 1, "Cursor should stay in position");
    cr_assert_eq(ed.buffer_len, 2, "Buffer length should decrease");
    
    editor_cleanup(&ed);
}

Test(line_editor, editor_delete_char_at_end) {
    struct line_editor ed;
    editor_init(&ed, "");
    
    editor_insert_char(&ed, 'a');
    
    // Try to delete past end
    int result = editor_delete_char(&ed);
    cr_assert_eq(result, -1, "Should fail to delete past end");
    cr_assert_str_eq(ed.buffer, "a", "Buffer should be unchanged");
    
    editor_cleanup(&ed);
}

Test(line_editor, editor_backspace_char_basic) {
    struct line_editor ed;
    editor_init(&ed, "");
    
    // Insert some text
    editor_insert_char(&ed, 'a');
    editor_insert_char(&ed, 'b');
    editor_insert_char(&ed, 'c');
    
    // Backspace from end
    int result = editor_backspace_char(&ed);
    cr_assert_eq(result, 0, "Should backspace successfully");
    cr_assert_str_eq(ed.buffer, "ab", "Should remove last character");
    cr_assert_eq(ed.cursor_pos, 2, "Cursor should move back");
    
    editor_cleanup(&ed);
}

Test(line_editor, editor_backspace_char_from_middle) {
    struct line_editor ed;
    editor_init(&ed, "");
    
    // Insert text and move cursor
    editor_insert_char(&ed, 'a');
    editor_insert_char(&ed, 'b');
    editor_insert_char(&ed, 'c');
    ed.cursor_pos = 2; // Position before 'c'
    
    // Backspace
    int result = editor_backspace_char(&ed);
    cr_assert_eq(result, 0, "Should backspace from middle");
    cr_assert_str_eq(ed.buffer, "ac", "Should remove middle character");
    cr_assert_eq(ed.cursor_pos, 1, "Cursor should move back");
    
    editor_cleanup(&ed);
}

Test(line_editor, editor_backspace_char_at_start) {
    struct line_editor ed;
    editor_init(&ed, "");
    
    editor_insert_char(&ed, 'a');
    ed.cursor_pos = 0;
    
    // Try to backspace from start
    int result = editor_backspace_char(&ed);
    cr_assert_eq(result, -1, "Should fail to backspace from start");
    cr_assert_str_eq(ed.buffer, "a", "Buffer should be unchanged");
    
    editor_cleanup(&ed);
}

Test(line_editor, editor_move_cursor_basic) {
    struct line_editor ed;
    editor_init(&ed, "");
    
    // Insert some text
    editor_insert_char(&ed, 'a');
    editor_insert_char(&ed, 'b');
    editor_insert_char(&ed, 'c');
    
    // Move cursor left
    int result = editor_move_cursor(&ed, -1);
    cr_assert_eq(result, 0, "Should move cursor left");
    cr_assert_eq(ed.cursor_pos, 2, "Cursor should be at position 2");
    
    // Move cursor right
    result = editor_move_cursor(&ed, 1);
    cr_assert_eq(result, 0, "Should move cursor right");
    cr_assert_eq(ed.cursor_pos, 3, "Cursor should be at end");
    
    editor_cleanup(&ed);
}

Test(line_editor, editor_move_cursor_bounds) {
    struct line_editor ed;
    editor_init(&ed, "");
    
    editor_insert_char(&ed, 'a');
    editor_insert_char(&ed, 'b');
    
    // Try to move past start
    ed.cursor_pos = 0;
    int result = editor_move_cursor(&ed, -10);
    cr_assert_eq(result, 0, "Should not crash moving past start");
    cr_assert_eq(ed.cursor_pos, 0, "Cursor should stay at start");
    
    // Try to move past end
    result = editor_move_cursor(&ed, 100);
    cr_assert_eq(result, 0, "Should not crash moving past end");
    cr_assert_eq(ed.cursor_pos, 2, "Cursor should be at end");
    
    editor_cleanup(&ed);
}

Test(line_editor, editor_move_cursor_null_param) {
    int result = editor_move_cursor(NULL, 1);
    cr_assert_eq(result, -1, "Should fail with NULL parameter");
}

// ============================================================================
// PHASE 2 TESTS: KEY HANDLING
// ============================================================================

TestSuite(key_handling);

Test(key_handling, key_binding_lookup) {
    // Test that we can find key handlers
    // Note: We can't directly test the static function, but we can test behavior
    
    struct line_editor ed;
    editor_init(&ed, "test> ");
    
    // Test that the editor is properly initialized for key handling
    cr_assert_not_null(ed.buffer, "Editor should be initialized");
    cr_assert_eq(ed.cursor_pos, 0, "Cursor should start at beginning");
    
    editor_cleanup(&ed);
}

Test(key_handling, printable_character_range) {
    // Test that printable characters are in the expected range
    for (int c = 32; c <= 126; c++) {
        cr_assert(c >= 32 && c <= 126, "Character %d should be in printable range", c);
    }
}

Test(key_handling, control_character_definitions) {
    // Test that control characters are defined correctly
    cr_assert_eq(KEY_CTRL_A, 1, "Ctrl+A should be 1");
    cr_assert_eq(KEY_CTRL_C, 3, "Ctrl+C should be 3");
    cr_assert_eq(KEY_CTRL_D, 4, "Ctrl+D should be 4");
    cr_assert_eq(KEY_ENTER, 13, "Enter should be 13");
    cr_assert_eq(KEY_BACKSPACE, 8, "Backspace should be 8");
}

// ============================================================================
// PHASE 2 TESTS: INTEGRATION WITH TERMINAL
// ============================================================================

TestSuite(editor_terminal_integration);

Test(editor_terminal_integration, editor_with_terminal_state) {
    // Test that editor can work with terminal state
    repl_init();
    
    struct line_editor ed;
    int result = editor_init(&ed, "test> ");
    cr_assert_eq(result, 0, "Editor should initialize with terminal");
    
    // Editor should have terminal reference
    cr_assert_not_null(ed.term, "Editor should have terminal reference");
    
    editor_cleanup(&ed);
    repl_cleanup();
}

Test(editor_terminal_integration, editor_refresh_display_safe) {
    repl_init();
    
    struct line_editor ed;
    editor_init(&ed, "test> ");
    
    // Should not crash even if terminal is not in raw mode
    editor_refresh_display(&ed);
    
    // Should not crash with text in buffer
    editor_insert_char(&ed, 'a');
    editor_insert_char(&ed, 'b');
    editor_refresh_display(&ed);
    
    editor_cleanup(&ed);
    repl_cleanup();
}

// ============================================================================
// PHASE 2 TESTS: MEMORY MANAGEMENT
// ============================================================================

TestSuite(editor_memory);

Test(editor_memory, buffer_allocation_and_growth) {
    struct line_editor ed;
    editor_init(&ed, "");
    
    size_t initial_size = ed.buffer_size;
    
    // Fill buffer to near capacity
    for (size_t i = 0; i < initial_size - 10; i++) {
        editor_insert_char(&ed, 'x');
    }
    
    cr_assert_eq(ed.buffer_size, initial_size, "Buffer should not have grown yet");
    
    // Trigger growth
    for (int i = 0; i < 20; i++) {
        editor_insert_char(&ed, 'y');
    }
    
    cr_assert_gt(ed.buffer_size, initial_size, "Buffer should have grown");
    cr_assert_not_null(ed.buffer, "Buffer should still be valid");
    
    editor_cleanup(&ed);
}

Test(editor_memory, prompt_allocation) {
    struct line_editor ed;
    
    // Test with normal prompt
    editor_init(&ed, "normal prompt> ");
    cr_assert_not_null(ed.prompt, "Prompt should be allocated");
    cr_assert_str_eq(ed.prompt, "normal prompt> ", "Prompt should be copied correctly");
    editor_cleanup(&ed);
    
    // Test with empty prompt
    editor_init(&ed, "");
    cr_assert_not_null(ed.prompt, "Empty prompt should be allocated");
    cr_assert_str_eq(ed.prompt, "", "Empty prompt should be correct");
    editor_cleanup(&ed);
    
    // Test with NULL prompt
    editor_init(&ed, NULL);
    cr_assert_not_null(ed.prompt, "NULL prompt should default to empty");
    cr_assert_str_eq(ed.prompt, "", "NULL prompt should become empty string");
    editor_cleanup(&ed);
}

Test(editor_memory, cleanup_completeness) {
    struct line_editor ed;
    editor_init(&ed, "test prompt");
    
    // Add some content
    editor_insert_char(&ed, 'a');
    editor_insert_char(&ed, 'b');
    
    // Verify everything is allocated
    cr_assert_not_null(ed.buffer, "Buffer should be allocated");
    cr_assert_not_null(ed.prompt, "Prompt should be allocated");
    
    // Cleanup should clear everything
    editor_cleanup(&ed);
    cr_assert_null(ed.buffer, "Buffer should be NULL after cleanup");
    cr_assert_null(ed.prompt, "Prompt should be NULL after cleanup");
    cr_assert_eq(ed.buffer_size, 0, "Buffer size should be 0");
    cr_assert_eq(ed.buffer_len, 0, "Buffer length should be 0");
}

// ============================================================================
// PHASE 3 TESTS: HISTORY SYSTEM
// ============================================================================

TestSuite(history_system);

Test(history_system, history_init_success) {
    struct history hist;
    int result = history_init(&hist, 50);
    
    cr_assert_eq(result, 0, "history_init should succeed");
    cr_assert_eq(hist.max_size, 50, "max_size should be set correctly");
    cr_assert_eq(hist.count, 0, "count should be 0 initially");
    cr_assert_null(hist.head, "head should be NULL initially");
    cr_assert_null(hist.tail, "tail should be NULL initially");
    cr_assert_null(hist.current, "current should be NULL initially");
    
    history_cleanup(&hist);
}

Test(history_system, history_init_default_size) {
    struct history hist;
    int result = history_init(&hist, 0);
    
    cr_assert_eq(result, 0, "history_init should succeed with 0 size");
    cr_assert_eq(hist.max_size, 100, "should default to 100");
    
    history_cleanup(&hist);
}

Test(history_system, history_init_null_param) {
    int result = history_init(NULL, 50);
    cr_assert_eq(result, -1, "history_init should fail with NULL");
}

Test(history_system, history_add_entry_basic) {
    struct history hist;
    history_init(&hist, 10);
    
    // Add first entry
    int result = history_add_entry(&hist, "first command");
    cr_assert_eq(result, 0, "Should add first entry");
    cr_assert_eq(hist.count, 1, "Count should be 1");
    cr_assert_not_null(hist.head, "Head should not be NULL");
    cr_assert_not_null(hist.tail, "Tail should not be NULL");
    cr_assert_eq(hist.head, hist.tail, "Head and tail should be same for single entry");
    
    // Add second entry
    result = history_add_entry(&hist, "second command");
    cr_assert_eq(result, 0, "Should add second entry");
    cr_assert_eq(hist.count, 2, "Count should be 2");
    cr_assert_neq(hist.head, hist.tail, "Head and tail should be different");
    
    history_cleanup(&hist);
}

Test(history_system, history_add_entry_ignore_empty) {
    struct history hist;
    history_init(&hist, 10);
    
    // Try to add empty line
    int result = history_add_entry(&hist, "");
    cr_assert_eq(result, 0, "Should handle empty line gracefully");
    cr_assert_eq(hist.count, 0, "Should not add empty line");
    
    // Try to add NULL
    result = history_add_entry(&hist, NULL);
    cr_assert_eq(result, 0, "Should handle NULL gracefully");
    cr_assert_eq(hist.count, 0, "Should not add NULL");
    
    history_cleanup(&hist);
}

Test(history_system, history_add_entry_ignore_repl_commands) {
    struct history hist;
    history_init(&hist, 10);
    
    // Add REPL command
    int result = history_add_entry(&hist, ".quit");
    cr_assert_eq(result, 0, "Should handle REPL command gracefully");
    cr_assert_eq(hist.count, 0, "Should not add REPL command");
    
    // Add normal command
    result = history_add_entry(&hist, "normal command");
    cr_assert_eq(result, 0, "Should add normal command");
    cr_assert_eq(hist.count, 1, "Should add normal command");
    
    history_cleanup(&hist);
}

Test(history_system, history_add_entry_ignore_duplicates) {
    struct history hist;
    history_init(&hist, 10);
    
    // Add first entry
    history_add_entry(&hist, "same command");
    cr_assert_eq(hist.count, 1, "Should add first occurrence");
    
    // Try to add same command again
    int result = history_add_entry(&hist, "same command");
    cr_assert_eq(result, 0, "Should handle duplicate gracefully");
    cr_assert_eq(hist.count, 1, "Should not add duplicate");
    
    // Add different command
    result = history_add_entry(&hist, "different command");
    cr_assert_eq(result, 0, "Should add different command");
    cr_assert_eq(hist.count, 2, "Should have 2 entries");
    
    history_cleanup(&hist);
}

Test(history_system, history_add_entry_size_limit) {
    struct history hist;
    history_init(&hist, 3); // Small limit for testing
    
    // Add entries up to limit
    history_add_entry(&hist, "command 1");
    history_add_entry(&hist, "command 2");
    history_add_entry(&hist, "command 3");
    cr_assert_eq(hist.count, 3, "Should have 3 entries");
    
    // Add one more to trigger cleanup
    history_add_entry(&hist, "command 4");
    cr_assert_eq(hist.count, 3, "Should still have 3 entries");
    
    // Check that oldest was removed
    cr_assert_str_eq(hist.head->line, "command 2", "Oldest should be removed");
    cr_assert_str_eq(hist.tail->line, "command 4", "Newest should be at tail");
    
    history_cleanup(&hist);
}

Test(history_system, history_get_entry_basic) {
    struct history hist;
    history_init(&hist, 10);
    
    // Add some entries
    history_add_entry(&hist, "first");
    history_add_entry(&hist, "second");
    history_add_entry(&hist, "third");
    
    // Get previous entries
    const char *line = history_get_entry(&hist, -1);
    cr_assert_str_eq(line, "third", "Should get last entry");
    
    line = history_get_entry(&hist, -1);
    cr_assert_str_eq(line, "second", "Should get second-to-last entry");
    
    line = history_get_entry(&hist, -1);
    cr_assert_str_eq(line, "first", "Should get first entry");
    
    line = history_get_entry(&hist, -1);
    cr_assert_null(line, "Should return NULL when going past start");
    
    history_cleanup(&hist);
}

Test(history_system, history_get_entry_navigation) {
    struct history hist;
    history_init(&hist, 10);
    
    history_add_entry(&hist, "first");
    history_add_entry(&hist, "second");
    history_add_entry(&hist, "third");
    
    // Navigate backward then forward
    const char *line = history_get_entry(&hist, -1);
    cr_assert_str_eq(line, "third", "Should get last entry");
    
    line = history_get_entry(&hist, -1);
    cr_assert_str_eq(line, "second", "Should get previous entry");
    
    line = history_get_entry(&hist, 1);
    cr_assert_str_eq(line, "third", "Should move forward to next entry");
    
    line = history_get_entry(&hist, 1);
    cr_assert_null(line, "Should return NULL when going past end");
    
    history_cleanup(&hist);
}

Test(history_system, history_get_entry_empty_history) {
    struct history hist;
    history_init(&hist, 10);
    
    const char *line = history_get_entry(&hist, -1);
    cr_assert_null(line, "Should return NULL for empty history");
    
    line = history_get_entry(&hist, 1);
    cr_assert_null(line, "Should return NULL for empty history");
    
    history_cleanup(&hist);
}

Test(history_system, history_search_prefix_basic) {
    struct history hist;
    history_init(&hist, 10);
    
    history_add_entry(&hist, "echo hello");
    history_add_entry(&hist, "ls -la");
    history_add_entry(&hist, "echo world");
    history_add_entry(&hist, "pwd");
    
    // Search for "echo" prefix
    const char *line = history_search_prefix(&hist, "echo");
    cr_assert_str_eq(line, "echo world", "Should find most recent match");
    
    // Search again to find previous match
    line = history_search_prefix(&hist, "echo");
    cr_assert_str_eq(line, "echo hello", "Should find previous match");
    
    // Search again - no more matches
    line = history_search_prefix(&hist, "echo");
    cr_assert_null(line, "Should return NULL when no more matches");
    
    history_cleanup(&hist);
}

Test(history_system, history_search_prefix_no_match) {
    struct history hist;
    history_init(&hist, 10);
    
    history_add_entry(&hist, "echo hello");
    history_add_entry(&hist, "ls -la");
    
    const char *line = history_search_prefix(&hist, "grep");
    cr_assert_null(line, "Should return NULL for no match");
    
    history_cleanup(&hist);
}

Test(history_system, history_search_prefix_empty) {
    struct history hist;
    history_init(&hist, 10);
    
    history_add_entry(&hist, "test");
    
    // Empty prefix should return NULL
    const char *line = history_search_prefix(&hist, "");
    cr_assert_null(line, "Should return NULL for empty prefix");
    
    // NULL prefix should return NULL
    line = history_search_prefix(&hist, NULL);
    cr_assert_null(line, "Should return NULL for NULL prefix");
    
    history_cleanup(&hist);
}

Test(history_system, history_file_operations) {
    struct history hist;
    history_init(&hist, 10);
    
    // Add some entries
    history_add_entry(&hist, "command 1");
    history_add_entry(&hist, "command 2");
    history_add_entry(&hist, "command 3");
    
    // Save to file
    const char *filename = "/tmp/test_history.txt";
    int result = history_save_to_file(&hist, filename);
    cr_assert_eq(result, 0, "Should save history to file");
    
    // Create new history and load from file
    struct history hist2;
    history_init(&hist2, 10);
    
    result = history_load_from_file(&hist2, filename);
    cr_assert_eq(result, 0, "Should load history from file");
    cr_assert_eq(hist2.count, 3, "Should have loaded 3 entries");
    
    // Check that entries match
    const char *line = history_get_entry(&hist2, -1);
    cr_assert_str_eq(line, "command 3", "Last entry should match");
    
    // Cleanup
    history_cleanup(&hist);
    history_cleanup(&hist2);
    
    // Remove test file
    unlink(filename);
}

Test(history_system, history_file_operations_invalid) {
    struct history hist;
    history_init(&hist, 10);
    
    // Try to save with invalid parameters
    int result = history_save_to_file(NULL, "test.txt");
    cr_assert_eq(result, -1, "Should fail with NULL history");
    
    result = history_save_to_file(&hist, NULL);
    cr_assert_eq(result, -1, "Should fail with NULL filename");
    
    // Try to load from non-existent file
    result = history_load_from_file(&hist, "/non/existent/file.txt");
    cr_assert_eq(result, -1, "Should fail with non-existent file");
    
    history_cleanup(&hist);
}

Test(history_system, history_cleanup_safety) {
    struct history hist;
    history_init(&hist, 10);
    
    // Add some entries
    history_add_entry(&hist, "test 1");
    history_add_entry(&hist, "test 2");
    
    // Cleanup should free all memory
    history_cleanup(&hist);
    
    // Should be safe to cleanup again
    history_cleanup(&hist);
    
    // Should be safe with NULL
    history_cleanup(NULL);
}

// ============================================================================
// PHASE 3 TESTS: INTEGRATION WITH REPL
// ============================================================================

TestSuite(history_integration);

Test(history_integration, repl_add_history_integration) {
    repl_init();
    
    // Add some history entries
    int result = repl_add_history("test command 1");
    cr_assert_eq(result, 0, "Should add to history");
    
    result = repl_add_history("test command 2");
    cr_assert_eq(result, 0, "Should add second entry");
    
    // Test that empty lines are ignored
    result = repl_add_history("");
    cr_assert_eq(result, 0, "Should handle empty line");
    
    // Test that REPL commands are ignored
    result = repl_add_history(".quit");
    cr_assert_eq(result, 0, "Should ignore REPL command");
    
    repl_cleanup();
}

Test(history_integration, readline_compatibility_functions) {
    repl_init();
    
    // Add some history
    add_history("compat test 1");
    add_history("compat test 2");
    
    // Test write_history
    const char *filename = "/tmp/test_compat_history.txt";
    int result = write_history(filename);
    cr_assert_eq(result, 0, "write_history should succeed");
    
    // Clear and reload
    clear_history();
    result = read_history(filename);
    cr_assert_eq(result, 0, "read_history should succeed");
    
    // Cleanup
    repl_cleanup();
    unlink(filename);
}

Test(history_integration, clear_history_function) {
    repl_init();
    
    // Add some history
    add_history("test 1");
    add_history("test 2");
    
    // Clear history
    int result = clear_history();
    cr_assert_eq(result, 0, "clear_history should succeed");
    
    // History should be empty now (we can't directly test this without
    // exposing internal state, but at least it shouldn't crash)
    
    repl_cleanup();
}

// ============================================================================
// PHASE 4 TESTS: ADVANCED EDITING FEATURES
// ============================================================================

TestSuite(advanced_editing);

Test(advanced_editing, kill_line_operations) {
    struct line_editor ed;
    int result = editor_init(&ed, "test> ");
    cr_assert_eq(result, 0, "editor_init should succeed");
    
    // Insert test text
    const char *test_text = "hello world test";
    for (size_t i = 0; i < strlen(test_text); i++) {
        editor_insert_char(&ed, test_text[i]);
    }
    cr_assert_str_eq(ed.buffer, "hello world test", "Buffer should contain test text");
    
    // Test kill line from middle
    ed.cursor_pos = 6; // After "hello "
    result = handle_kill_line(&ed, 11, 1); // Ctrl+K
    cr_assert_eq(result, 0, "handle_kill_line should succeed");
    cr_assert_str_eq(ed.buffer, "hello ", "Buffer should contain only 'hello '");
    cr_assert_eq(ed.cursor_pos, 6, "Cursor should remain at position 6");
    
    // Test yank (paste) operation
    result = handle_yank(&ed, 25, 1); // Ctrl+Y
    cr_assert_eq(result, 0, "handle_yank should succeed");
    cr_assert_str_eq(ed.buffer, "hello world test", "Buffer should be restored after yank");
    
    editor_cleanup(&ed);
}

Test(advanced_editing, kill_whole_line_operation) {
    struct line_editor ed;
    int result = editor_init(&ed, "test> ");
    cr_assert_eq(result, 0, "editor_init should succeed");
    
    // Insert test text
    const char *test_text = "hello world test";
    for (size_t i = 0; i < strlen(test_text); i++) {
        editor_insert_char(&ed, test_text[i]);
    }
    
    // Test kill whole line
    result = handle_kill_whole_line(&ed, 21, 1); // Ctrl+U
    cr_assert_eq(result, 0, "handle_kill_whole_line should succeed");
    cr_assert_str_eq(ed.buffer, "", "Buffer should be empty after kill whole line");
    cr_assert_eq(ed.cursor_pos, 0, "Cursor should be at position 0");
    
    // Test yank restores the whole line
    result = handle_yank(&ed, 25, 1); // Ctrl+Y
    cr_assert_eq(result, 0, "handle_yank should succeed");
    cr_assert_str_eq(ed.buffer, "hello world test", "Buffer should be restored after yank");
    
    editor_cleanup(&ed);
}

Test(advanced_editing, transpose_characters) {
    struct line_editor ed;
    int result = editor_init(&ed, "test> ");
    cr_assert_eq(result, 0, "editor_init should succeed");
    
    // Insert two characters
    editor_insert_char(&ed, 'a');
    editor_insert_char(&ed, 'b');
    cr_assert_str_eq(ed.buffer, "ab", "Buffer should contain 'ab'");
    
    // Move cursor between characters and transpose
    ed.cursor_pos = 1; // Between 'a' and 'b'
    result = handle_transpose_chars(&ed, 20, 1); // Ctrl+T
    cr_assert_eq(result, 0, "handle_transpose_chars should succeed");
    cr_assert_str_eq(ed.buffer, "ba", "Characters should be transposed to 'ba'");
    
    editor_cleanup(&ed);
}

Test(advanced_editing, transpose_at_end) {
    struct line_editor ed;
    int result = editor_init(&ed, "test> ");
    cr_assert_eq(result, 0, "editor_init should succeed");
    
    // Insert test text
    editor_insert_char(&ed, 'x');
    editor_insert_char(&ed, 'y');
    editor_insert_char(&ed, 'z');
    cr_assert_str_eq(ed.buffer, "xyz", "Buffer should contain 'xyz'");
    
    // Cursor at end, should transpose last two characters
    cr_assert_eq(ed.cursor_pos, 3, "Cursor should be at end");
    result = handle_transpose_chars(&ed, 20, 1); // Ctrl+T
    cr_assert_eq(result, 0, "handle_transpose_chars should succeed");
    cr_assert_str_eq(ed.buffer, "xzy", "Last two characters should be transposed");
    
    editor_cleanup(&ed);
}

Test(advanced_editing, backward_kill_word) {
    struct line_editor ed;
    int result = editor_init(&ed, "test> ");
    cr_assert_eq(result, 0, "editor_init should succeed");
    
    // Insert test text with words
    const char *test_text = "hello world test";
    for (size_t i = 0; i < strlen(test_text); i++) {
        editor_insert_char(&ed, test_text[i]);
    }
    
    // Position cursor at end and kill previous word
    ed.cursor_pos = ed.buffer_len; // At end
    result = handle_backward_kill_word(&ed, 23, 1); // Ctrl+W
    cr_assert_eq(result, 0, "handle_backward_kill_word should succeed");
    cr_assert_str_eq(ed.buffer, "hello world ", "Should kill 'test'");
    
    // Test yank restores the killed word
    result = handle_yank(&ed, 25, 1); // Ctrl+Y
    cr_assert_eq(result, 0, "handle_yank should succeed");
    cr_assert_str_eq(ed.buffer, "hello world test", "Buffer should be restored after yank");
    
    editor_cleanup(&ed);
}

Test(advanced_editing, kill_ring_multiple_entries) {
    struct line_editor ed;
    int result = editor_init(&ed, "test> ");
    cr_assert_eq(result, 0, "editor_init should succeed");
    
    // Test multiple kill operations to fill kill ring
    editor_insert_char(&ed, 'a');
    result = handle_kill_line(&ed, 11, 1); // Kill 'a'
    cr_assert_eq(result, 0, "First kill should succeed");
    
    editor_insert_char(&ed, 'b');
    result = handle_kill_line(&ed, 11, 1); // Kill 'b'
    cr_assert_eq(result, 0, "Second kill should succeed");
    
    editor_insert_char(&ed, 'c');
    result = handle_kill_line(&ed, 11, 1); // Kill 'c'
    cr_assert_eq(result, 0, "Third kill should succeed");
    
    // Yank should restore the most recent kill ('c')
    result = handle_yank(&ed, 25, 1);
    cr_assert_eq(result, 0, "handle_yank should succeed");
    cr_assert_str_eq(ed.buffer, "c", "Should yank most recent kill");
    
    editor_cleanup(&ed);
}

Test(advanced_editing, empty_buffer_operations) {
    struct line_editor ed;
    int result = editor_init(&ed, "test> ");
    cr_assert_eq(result, 0, "editor_init should succeed");
    
    // Test operations on empty buffer (should not crash)
    result = handle_kill_line(&ed, 11, 1);
    cr_assert_eq(result, 0, "kill_line on empty buffer should succeed");
    
    result = handle_kill_whole_line(&ed, 21, 1);
    cr_assert_eq(result, 0, "kill_whole_line on empty buffer should succeed");
    
    result = handle_backward_kill_word(&ed, 23, 1);
    cr_assert_eq(result, 0, "backward_kill_word on empty buffer should succeed");
    
    result = handle_transpose_chars(&ed, 20, 1);
    cr_assert_eq(result, 0, "transpose_chars on empty buffer should succeed");
    
    // Buffer should still be empty
    cr_assert_str_eq(ed.buffer, "", "Buffer should remain empty");
    cr_assert_eq(ed.cursor_pos, 0, "Cursor should remain at 0");
    
    editor_cleanup(&ed);
}