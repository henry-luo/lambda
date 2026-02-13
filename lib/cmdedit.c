#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <string.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>
#include "cmdedit.h"
#include "cmdedit_utf8.h"
#include "log.h"
#include "str.h"

// Platform-specific includes
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <conio.h>
#define isatty _isatty
#define STDIN_FILENO _fileno(stdin)
#define STDOUT_FILENO _fileno(stdout)
// Windows doesn't have ssize_t
typedef intptr_t ssize_t;
#else
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>
#endif

// Key codes are now defined in cmdedit.h

#ifndef CMDEDIT_TESTING
struct terminal_state {
    int input_fd;
    int output_fd;
    bool is_tty;
    bool raw_mode;

#ifdef _WIN32
    HANDLE h_stdin;
    HANDLE h_stdout;
    DWORD orig_input_mode;
    DWORD orig_output_mode;
#else
#ifndef CMDEDIT_TESTING
    struct termios orig_termios;
#else
    char orig_termios_data[256]; // Storage for struct termios (platform-specific size)
#endif
#endif
};

#endif // CMDEDIT_TESTING

// Helper macros for termios access when testing
#ifdef CMDEDIT_TESTING
#define GET_TERMIOS(term) ((struct termios *)(term)->orig_termios_data)
#else
#define GET_TERMIOS(term) (&(term)->orig_termios)
#endif

// Line editor structure
#ifndef CMDEDIT_TESTING
struct line_editor {
    char *buffer;           // Current line buffer
    size_t buffer_size;     // Allocated size
    size_t buffer_len;      // Current length
    size_t cursor_pos;      // Cursor position in buffer
    size_t display_pos;     // Cursor position on screen
    char *prompt;           // Current prompt string
    size_t prompt_len;      // Display width of prompt
    struct terminal_state *term;
    bool needs_refresh;     // Display needs updating
};
#endif // CMDEDIT_TESTING

// History entry structure
#ifndef CMDEDIT_TESTING
struct history_entry {
    char *line;
    struct history_entry *next;
    struct history_entry *prev;
};

// History structure
struct history {
    struct history_entry *head;
    struct history_entry *tail;
    struct history_entry *current;
    size_t count;
    size_t max_size;
    char *filename;
};
#endif // CMDEDIT_TESTING

// Global state
static struct terminal_state g_terminal = {0};
static struct line_editor g_editor = {0};
static struct history g_history = {0};
static bool g_initialized = false;

// Signal handling state
#ifndef _WIN32
static volatile sig_atomic_t g_signal_received = 0;
static volatile sig_atomic_t g_winch_received = 0;
static struct sigaction g_old_sigint;
static struct sigaction g_old_sigterm;
static struct sigaction g_old_sigwinch;
static bool g_signals_installed = false;
#endif

// Readline compatibility variables
char *rl_line_buffer = NULL;
int rl_point = 0;
int rl_end = 0;
char *rl_prompt = NULL;
rl_completion_func_t *rl_attempted_completion_function = NULL;

// Forward declarations
static int terminal_write(struct terminal_state *term, const char *data, size_t len);
static char *editor_readline(const char *prompt);
int history_add_entry(struct history *hist, const char *line);
void history_cleanup(struct history *hist);
#ifdef CMDEDIT_TESTING
int terminal_raw_mode(struct terminal_state *term, bool enable);
#else
static int terminal_raw_mode(struct terminal_state *term, bool enable);
#endif
static void kill_ring_cleanup(void);

// ============================================================================
// SIGNAL HANDLING
// ============================================================================

#ifndef _WIN32
static void signal_handler(int sig) {
    switch (sig) {
        case SIGINT:
            g_signal_received = SIGINT;
            break;
        case SIGTERM:
            g_signal_received = SIGTERM;
            break;
#if !defined(_WIN32) && defined(SIGWINCH)
        case SIGWINCH:
            g_winch_received = 1;
            break;
#endif
#if !defined(_WIN32) && defined(SIGPIPE)
        case SIGPIPE:
            // Ignore SIGPIPE - handle broken pipes gracefully
            break;
#endif
        default:
            break;
    }
}

static int install_signal_handlers(void) {
    if (g_signals_installed) return 0;

    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;  // Restart interrupted system calls

    // Install SIGINT handler
    if (sigaction(SIGINT, &sa, &g_old_sigint) != 0) {
        return -1;
    }

    // Install SIGTERM handler
    if (sigaction(SIGTERM, &sa, &g_old_sigterm) != 0) {
        sigaction(SIGINT, &g_old_sigint, NULL);  // Restore SIGINT
        return -1;
    }

#if !defined(_WIN32) && defined(SIGPIPE)
    // Install SIGPIPE handler (ignore broken pipes)
    if (sigaction(SIGPIPE, &sa, NULL) != 0) {
        sigaction(SIGINT, &g_old_sigint, NULL);
        sigaction(SIGTERM, &g_old_sigterm, NULL);
        return -1;
    }
#endif

#if !defined(_WIN32) && defined(SIGWINCH)
    // Install SIGWINCH handler (window resize)
    if (sigaction(SIGWINCH, &sa, &g_old_sigwinch) != 0) {
        sigaction(SIGINT, &g_old_sigint, NULL);
        sigaction(SIGTERM, &g_old_sigterm, NULL);
        return -1;
    }
#endif

    g_signals_installed = true;
    return 0;
}

static int restore_signal_handlers(void) {
    if (!g_signals_installed) return 0;

    int result = 0;

    if (sigaction(SIGINT, &g_old_sigint, NULL) != 0) {
        result = -1;
    }

    if (sigaction(SIGTERM, &g_old_sigterm, NULL) != 0) {
        result = -1;
    }

#if !defined(_WIN32) && defined(SIGWINCH)
    if (sigaction(SIGWINCH, &g_old_sigwinch, NULL) != 0) {
        result = -1;
    }
#endif

    g_signals_installed = false;
    return result;
}

static bool check_signals(void) {
    // Check for window resize
    if (g_winch_received) {
        g_winch_received = 0;
        // TODO: Handle window resize - refresh display
        // For now, just return false to continue normal operation
    }

    // Check for termination signals
    if (g_signal_received) {
        return true;  // Signal received, should exit
    }

    return false;  // No termination signal
}
#else
// Windows stubs for signal handling
static inline int install_signal_handlers(void) { return 0; }
static inline int restore_signal_handlers(void) { return 0; }
static inline bool check_signals(void) { return false; }
#endif

// ============================================================================
// PHASE 1: TERMINAL I/O ABSTRACTION
// ============================================================================

int terminal_init(struct terminal_state *term) {
    if (!term) return -1;

    memset(term, 0, sizeof(*term));

#ifdef _WIN32
    term->h_stdin = GetStdHandle(STD_INPUT_HANDLE);
    term->h_stdout = GetStdHandle(STD_OUTPUT_HANDLE);

    if (term->h_stdin == INVALID_HANDLE_VALUE || term->h_stdout == INVALID_HANDLE_VALUE) {
        return -1;
    }

    term->input_fd = _fileno(stdin);
    term->output_fd = _fileno(stdout);
    term->is_tty = _isatty(term->input_fd) && _isatty(term->output_fd);

    if (term->is_tty) {
        // Set console to UTF-8 for proper Unicode display
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);

        // Save original console modes
        if (!GetConsoleMode(term->h_stdin, &term->orig_input_mode) ||
            !GetConsoleMode(term->h_stdout, &term->orig_output_mode)) {
            return -1;
        }
    }
#else
    term->input_fd = STDIN_FILENO;
    term->output_fd = STDOUT_FILENO;
    term->is_tty = isatty(term->input_fd) && isatty(term->output_fd);

    if (term->is_tty) {
        // Save original terminal settings
        if (tcgetattr(term->input_fd, GET_TERMIOS(term)) != 0) {
            return -1;
        }
    }
#endif

    term->raw_mode = false;
    return 0;
}

int terminal_cleanup(struct terminal_state *term) {
    if (!term) return -1;

    // Restore terminal to original state
    if (term->raw_mode) {
        terminal_raw_mode(term, false);
    }

    return 0;
}

#ifdef CMDEDIT_TESTING
int terminal_raw_mode(struct terminal_state *term, bool enable) {
#else
static int terminal_raw_mode(struct terminal_state *term, bool enable) {
#endif
    if (!term || !term->is_tty) return -1;

#ifdef _WIN32
    if (enable) {
        // Enable raw input mode
        DWORD input_mode = term->orig_input_mode;
        input_mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
        // Note: Don't enable ENABLE_VIRTUAL_TERMINAL_INPUT - we want native Windows key events

        DWORD output_mode = term->orig_output_mode;
        output_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;

        if (!SetConsoleMode(term->h_stdin, input_mode) ||
            !SetConsoleMode(term->h_stdout, output_mode)) {
            return -1;
        }
    } else {
        // Restore original modes
        if (!SetConsoleMode(term->h_stdin, term->orig_input_mode) ||
            !SetConsoleMode(term->h_stdout, term->orig_output_mode)) {
            return -1;
        }
    }
#else
    if (enable) {
        struct termios raw = *GET_TERMIOS(term);

        // Disable canonical mode, echo, and signals
        raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
        raw.c_iflag &= ~(IXON | ICRNL | INPCK | ISTRIP);
        raw.c_oflag &= ~(OPOST);
        raw.c_cflag |= (CS8);

        // Set minimum characters and timeout
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;

        if (tcsetattr(term->input_fd, TCSAFLUSH, &raw) != 0) {
            return -1;
        }
    } else {
        // Restore original settings
        if (tcsetattr(term->input_fd, TCSAFLUSH, GET_TERMIOS(term)) != 0) {
            return -1;
        }
    }
#endif

    term->raw_mode = enable;
    return 0;
}

int terminal_get_size(struct terminal_state *term, int *rows, int *cols) {
    if (!term || !rows || !cols) return -1;

#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(term->h_stdout, &csbi)) {
        *cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        return 0;
    }
#else
    struct winsize ws;
    if (ioctl(term->output_fd, TIOCGWINSZ, &ws) == 0) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
        return 0;
    }
#endif

    // Fallback defaults
    *rows = 24;
    *cols = 80;
    return -1;
}

static int terminal_write(struct terminal_state *term, const char *data, size_t len) {
    if (!term || !data) return -1;

#ifdef _WIN32
    DWORD written;
    if (WriteConsoleA(term->h_stdout, data, (DWORD)len, &written, NULL)) {
        return (int)written;
    }
#else
    ssize_t written = write(term->output_fd, data, len);
    if (written >= 0) {
        return (int)written;
    }
#endif

    return -1;
}

int terminal_read_key(struct terminal_state *term) {
    if (!term || !term->is_tty) return KEY_ERROR;

#ifdef _WIN32
    INPUT_RECORD record;
    DWORD events_read;

    while (true) {
        if (!ReadConsoleInput(term->h_stdin, &record, 1, &events_read) || events_read == 0) {
            return KEY_ERROR;
        }

        if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown) {
            KEY_EVENT_RECORD *key = &record.Event.KeyEvent;

            // Handle special keys
            if (key->wVirtualKeyCode == VK_UP) return KEY_UP;
            if (key->wVirtualKeyCode == VK_DOWN) return KEY_DOWN;
            if (key->wVirtualKeyCode == VK_LEFT) return KEY_LEFT;
            if (key->wVirtualKeyCode == VK_RIGHT) return KEY_RIGHT;
            if (key->wVirtualKeyCode == VK_HOME) return KEY_HOME;
            if (key->wVirtualKeyCode == VK_END) return KEY_END;
            if (key->wVirtualKeyCode == VK_PRIOR) return KEY_PAGE_UP;
            if (key->wVirtualKeyCode == VK_NEXT) return KEY_PAGE_DOWN;
            if (key->wVirtualKeyCode == VK_DELETE) return KEY_DELETE;

            // Handle ASCII characters
            char ch = key->uChar.AsciiChar;
            if (ch != 0) {
                return (unsigned char)ch;
            }
        }
    }
#else
    char c;
    ssize_t result = read(term->input_fd, &c, 1);

    if (result <= 0) {
        return (result == 0) ? KEY_EOF : KEY_ERROR;
    }

    // Handle escape sequences
    if (c == 27) { // ESC
        char seq[3];

        // Use select() to timeout on escape sequence reading
        fd_set readfds;
        struct timeval timeout;

        FD_ZERO(&readfds);
        FD_SET(term->input_fd, &readfds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000; // 100ms timeout

        // Try to read the next character with timeout
        if (select(term->input_fd + 1, &readfds, NULL, NULL, &timeout) <= 0 ||
            read(term->input_fd, &seq[0], 1) != 1) {
            return KEY_ESC;
        }

        if (seq[0] == '[') {
            // ANSI escape sequence - read next character with timeout
            FD_ZERO(&readfds);
            FD_SET(term->input_fd, &readfds);
            timeout.tv_sec = 0;
            timeout.tv_usec = 100000; // 100ms timeout

            if (select(term->input_fd + 1, &readfds, NULL, NULL, &timeout) <= 0 ||
                read(term->input_fd, &seq[1], 1) != 1) {
                return KEY_ESC;
            }

            switch (seq[1]) {
                case 'A': return KEY_UP;
                case 'B': return KEY_DOWN;
                case 'C': return KEY_RIGHT;
                case 'D': return KEY_LEFT;
                case 'H': return KEY_HOME;
                case 'F': return KEY_END;
                case '1':
                case '7': return KEY_HOME;
                case '4':
                case '8': return KEY_END;
                case '5': return KEY_PAGE_UP;
                case '6': return KEY_PAGE_DOWN;
                case '3':
                    // Delete key (ESC[3~) - handles both regular delete and fn+delete on Mac/Linux
                    if (read(term->input_fd, &seq[2], 1) == 1 && seq[2] == '~') {
                        return KEY_DELETE;
                    }
                    return KEY_ESC;
            }
        }

        return KEY_ESC;
    }

    return (unsigned char)c;
#endif
}

// ============================================================================
// PHASE 1: BASIC API IMPLEMENTATION
// ============================================================================

int repl_init(void) {
    if (g_initialized) {
        return 0; // Already initialized
    }

    if (terminal_init(&g_terminal) != 0) {
        return -1;
    }

    // Install signal handlers
    if (install_signal_handlers() != 0) {
        terminal_cleanup(&g_terminal);
        return -1;
    }

    // Initialize history with default size
    g_history.max_size = 100;
    g_history.head = NULL;
    g_history.tail = NULL;
    g_history.current = NULL;
    g_history.count = 0;
    g_history.filename = NULL;

    g_initialized = true;
    return 0;
}

void repl_cleanup(void) {
    if (!g_initialized) { return; }

    log_debug("Cleaning up command line editor");
    terminal_cleanup(&g_terminal);

    // Cleanup editor
    if (g_editor.buffer) {
        free(g_editor.buffer);
        g_editor.buffer = NULL;
    }
    if (g_editor.prompt) {
        free(g_editor.prompt);
        g_editor.prompt = NULL;
    }

    // Cleanup history (will be implemented in Phase 3)
    history_cleanup(&g_history);

    // Cleanup kill ring
    kill_ring_cleanup();

    // Restore signal handlers
    restore_signal_handlers();

    g_initialized = false;
}

char *repl_readline(const char *prompt) {
    return editor_readline(prompt);
}

int repl_add_history(const char *line) {
    if (!g_initialized) {
        repl_init();
    }

    return history_add_entry(&g_history, line);
}

// Readline compatibility functions
char *readline(const char *prompt) {
    return repl_readline(prompt);
}

int add_history(const char *line) {
    return repl_add_history(line);
}

int rl_bind_key(int key, int (*function)(int, int)) {
    (void)key;
    (void)function; // Will be implemented in Phase 4
    return 0;
}

// ============================================================================
// PHASE 2: LINE EDITOR CORE
// ============================================================================

// Initial buffer size for line editing
#define INITIAL_BUFFER_SIZE 256
#define BUFFER_GROW_SIZE 256

// ANSI escape sequences for terminal control
#define ANSI_CLEAR_LINE "\033[K"
#define ANSI_CURSOR_LEFT "\033[D"
#define ANSI_CURSOR_RIGHT "\033[C"
#define ANSI_CURSOR_UP "\033[A"
#define ANSI_CURSOR_DOWN "\033[B"
#define ANSI_CURSOR_HOME "\033[H"
#define ANSI_CLEAR_SCREEN "\033[2J\033[H"
#define ANSI_SAVE_CURSOR "\033[s"
#define ANSI_RESTORE_CURSOR "\033[u"

// Key binding function type
typedef int (*key_handler_t)(struct line_editor *ed, int key, int count);

// Key binding structure
typedef struct {
    int key;
    key_handler_t handler;
} key_binding_t;

// Forward declarations for key handlers
static int handle_char_insert(struct line_editor *ed, int key, int count);
static int handle_backspace(struct line_editor *ed, int key, int count);
static int handle_delete(struct line_editor *ed, int key, int count);
static int handle_move_left(struct line_editor *ed, int key, int count);
static int handle_move_right(struct line_editor *ed, int key, int count);
static int handle_move_home(struct line_editor *ed, int key, int count);
static int handle_move_end(struct line_editor *ed, int key, int count);
static int handle_enter(struct line_editor *ed, int key, int count);
static int handle_ctrl_c(struct line_editor *ed, int key, int count);
static int handle_ctrl_d(struct line_editor *ed, int key, int count);
static int handle_ctrl_l(struct line_editor *ed, int key, int count);
static int handle_history_prev(struct line_editor *ed, int key, int count);
static int handle_history_next(struct line_editor *ed, int key, int count);
static int handle_tab_completion(struct line_editor *ed, int key, int count);

// Advanced editing function declarations
#ifdef CMDEDIT_TESTING
int handle_kill_line(struct line_editor *ed, int key, int count);
int handle_kill_whole_line(struct line_editor *ed, int key, int count);
int handle_backward_kill_word(struct line_editor *ed, int key, int count);
int handle_yank(struct line_editor *ed, int key, int count);
int handle_transpose_chars(struct line_editor *ed, int key, int count);
#else
static int handle_kill_line(struct line_editor *ed, int key, int count);
static int handle_kill_whole_line(struct line_editor *ed, int key, int count);
static int handle_backward_kill_word(struct line_editor *ed, int key, int count);
static int handle_yank(struct line_editor *ed, int key, int count);
static int handle_transpose_chars(struct line_editor *ed, int key, int count);
#endif

// Default key bindings
static key_binding_t default_bindings[] = {
    {KEY_CTRL_A, handle_move_home},
    {KEY_CTRL_E, handle_move_end},
    {KEY_CTRL_B, handle_move_left},
    {KEY_CTRL_F, handle_move_right},
    {KEY_CTRL_H, handle_backspace},  // Ctrl+H is backspace
    {KEY_CTRL_C, handle_ctrl_c},
    {KEY_CTRL_D, handle_ctrl_d},
    {KEY_CTRL_L, handle_ctrl_l},
    {KEY_CTRL_P, handle_history_prev},  // Previous history
    {KEY_CTRL_N, handle_history_next},  // Next history
    {KEY_TAB, handle_tab_completion},   // Tab completion
    {KEY_BACKSPACE, handle_backspace},
    {KEY_DELETE, handle_delete},
    {KEY_LEFT, handle_move_left},
    {KEY_RIGHT, handle_move_right},
    {KEY_UP, handle_history_prev},      // Up arrow = previous history
    {KEY_DOWN, handle_history_next},    // Down arrow = next history
    {KEY_HOME, handle_move_home},
    {KEY_END, handle_move_end},
    {KEY_ENTER, handle_enter},
    {0, NULL} // Sentinel
};

int editor_init(struct line_editor *ed, const char *prompt) {
    if (!ed) return -1;

    memset(ed, 0, sizeof(*ed));

    // Allocate initial buffer
    ed->buffer = malloc(INITIAL_BUFFER_SIZE);
    if (!ed->buffer) {
        return -1;
    }
    ed->buffer_size = INITIAL_BUFFER_SIZE;
    ed->buffer_len = 0;
    ed->cursor_pos = 0;
    ed->display_pos = 0;
    ed->buffer[0] = '\0';

    // Set prompt
    if (prompt) {
        ed->prompt = strdup(prompt);
        if (!ed->prompt) {
            free(ed->buffer);
            return -1;
        }
        ed->prompt_len = cmdedit_utf8_display_width(prompt, strlen(prompt));
    } else {
        ed->prompt = strdup("");
        ed->prompt_len = 0;
    }

    // Set terminal reference
    ed->term = &g_terminal;

    return 0;
}

void editor_cleanup(struct line_editor *ed) {
    if (!ed) return;

    if (ed->buffer) {
        free(ed->buffer);
        ed->buffer = NULL;
    }
    if (ed->prompt) {
        free(ed->prompt);
        ed->prompt = NULL;
    }

    memset(ed, 0, sizeof(*ed));
}

static int editor_ensure_buffer_size(struct line_editor *ed, size_t needed) {
    if (!ed || needed <= ed->buffer_size) {
        return 0;
    }

    // Use exponential growth for better performance with large inputs
    size_t new_size = ed->buffer_size;
    if (new_size == 0) {
        new_size = BUFFER_GROW_SIZE;
    }

    while (new_size < needed) {
        if (new_size > SIZE_MAX / 2) {
            // Avoid overflow, fall back to exact size
            new_size = needed;
            break;
        }
        new_size *= 2;
    }

    char *new_buffer = realloc(ed->buffer, new_size);
    if (!new_buffer) {
        return -1;
    }

    ed->buffer = new_buffer;
    ed->buffer_size = new_size;
    return 0;
}

int editor_insert_char(struct line_editor *ed, char c) {
    if (!ed) return -1;

    // Ensure we have space for the new character plus null terminator
    if (editor_ensure_buffer_size(ed, ed->buffer_len + 2) != 0) {
        return -1;
    }

    // Move characters after cursor to make room
    if (ed->cursor_pos < ed->buffer_len) {
        memmove(ed->buffer + ed->cursor_pos + 1,
                ed->buffer + ed->cursor_pos,
                ed->buffer_len - ed->cursor_pos);
    }

    // Insert the character
    ed->buffer[ed->cursor_pos] = c;
    ed->cursor_pos++;
    ed->buffer_len++;
    ed->buffer[ed->buffer_len] = '\0';

    return 0;
}

int editor_delete_char(struct line_editor *ed) {
    if (!ed || ed->cursor_pos >= ed->buffer_len) {
        return -1; // Nothing to delete - cursor is at end of line
    }

    // Get the UTF-8 character at cursor position
    utf8_char_t utf8_char;
    if (!cmdedit_utf8_get_char_at_byte(ed->buffer, ed->buffer_len, ed->cursor_pos, &utf8_char)) {
        return -1; // Invalid UTF-8 character
    }

    size_t char_bytes = utf8_char.byte_length;
    if (char_bytes == 0 || ed->cursor_pos + char_bytes > ed->buffer_len) {
        return -1; // Invalid character or would go past end
    }

    // Move characters after the deleted character to close the gap
    memmove(ed->buffer + ed->cursor_pos,
            ed->buffer + ed->cursor_pos + char_bytes,
            ed->buffer_len - ed->cursor_pos - char_bytes);

    ed->buffer_len -= char_bytes;
    ed->buffer[ed->buffer_len] = '\0';

    return 0;
}

int editor_backspace_char(struct line_editor *ed) {
    if (!ed || ed->cursor_pos == 0) {
        return -1; // Nothing to delete - cursor is at beginning
    }

    // Move cursor back to the previous character
    size_t prev_pos = cmdedit_utf8_move_cursor_left(ed->buffer, ed->buffer_len, ed->cursor_pos);
    if (prev_pos == ed->cursor_pos) {
        return -1; // Couldn't move back
    }

    // Calculate bytes to delete
    size_t char_bytes = ed->cursor_pos - prev_pos;

    // Move characters after cursor to close the gap
    memmove(ed->buffer + prev_pos,
            ed->buffer + ed->cursor_pos,
            ed->buffer_len - ed->cursor_pos);

    ed->buffer_len -= char_bytes;
    ed->cursor_pos = prev_pos;
    ed->buffer[ed->buffer_len] = '\0';

    return 0;
}

int editor_move_cursor(struct line_editor *ed, int offset) {
    if (!ed) return -1;

    if (offset == 0) return 0;

    if (offset < 0) {
        // Move left by characters
        for (int i = 0; i < -offset && ed->cursor_pos > 0; i++) {
            ed->cursor_pos = cmdedit_utf8_move_cursor_left(ed->buffer, ed->buffer_len, ed->cursor_pos);
        }
    } else {
        // Move right by characters
        for (int i = 0; i < offset && ed->cursor_pos < ed->buffer_len; i++) {
            ed->cursor_pos = cmdedit_utf8_move_cursor_right(ed->buffer, ed->buffer_len, ed->cursor_pos);
        }
    }

    return 0;
}

void editor_refresh_display(struct line_editor *ed) {
    if (!ed || !ed->term || !ed->term->is_tty) {
        return;
    }

    // Move cursor to beginning of line
    terminal_write(ed->term, "\r", 1);

    // Clear the line
    terminal_write(ed->term, ANSI_CLEAR_LINE, strlen(ANSI_CLEAR_LINE));

    // Print prompt
    if (ed->prompt) {
        terminal_write(ed->term, ed->prompt, strlen(ed->prompt));
    }

    // Print buffer content
    if (ed->buffer_len > 0) {
        terminal_write(ed->term, ed->buffer, ed->buffer_len);
    }

    // Calculate cursor display position using cached prompt width
    int cursor_display_width = cmdedit_utf8_display_width(ed->buffer, ed->cursor_pos);
    int total_display_width = cmdedit_utf8_display_width(ed->buffer, ed->buffer_len);

    size_t total_pos = ed->prompt_len + cursor_display_width;
    size_t current_pos = ed->prompt_len + total_display_width;

    if (current_pos > total_pos) {
        // Move cursor left to correct position
        char move_cmd[32];
        int move_count = (int)(current_pos - total_pos);
        snprintf(move_cmd, sizeof(move_cmd), "\033[%dD", move_count);
        terminal_write(ed->term, move_cmd, strlen(move_cmd));
    }
}

// Enhanced key bindings with advanced features
static key_binding_t enhanced_bindings[] = {
    // Basic movement
    {KEY_CTRL_A, handle_move_home},
    {KEY_CTRL_E, handle_move_end},
    {KEY_CTRL_B, handle_move_left},
    {KEY_CTRL_F, handle_move_right},
    {KEY_LEFT, handle_move_left},
    {KEY_RIGHT, handle_move_right},
    {KEY_HOME, handle_move_home},
    {KEY_END, handle_move_end},

    // Word movement (Alt/Meta keys - typically ESC sequences)
    // Note: These would need platform-specific handling for Alt key detection

    // History
    {KEY_CTRL_P, handle_history_prev},
    {KEY_CTRL_N, handle_history_next},
    {KEY_UP, handle_history_prev},
    {KEY_DOWN, handle_history_next},

    // Editing
    {KEY_CTRL_H, handle_backspace},
    {KEY_BACKSPACE, handle_backspace},
    {KEY_DELETE, handle_delete},
    {KEY_CTRL_D, handle_ctrl_d},

    // Kill ring operations
    {KEY_CTRL_K, handle_kill_line},
    {KEY_CTRL_U, handle_kill_whole_line},
    {KEY_CTRL_W, handle_backward_kill_word},
    {KEY_CTRL_Y, handle_yank},

    // Advanced editing
    {KEY_CTRL_T, handle_transpose_chars},

    // Control
    {KEY_CTRL_L, handle_ctrl_l},
    {KEY_CTRL_C, handle_ctrl_c},
    {KEY_ENTER, handle_enter},

    {0, NULL} // Sentinel
};

static key_handler_t find_key_handler(int key) {
    for (int i = 0; enhanced_bindings[i].handler; i++) {
        if (enhanced_bindings[i].key == key) {
            return enhanced_bindings[i].handler;
        }
    }
    return NULL;
}

// Key handler implementations
static int handle_char_insert(struct line_editor *ed, int key, int count) {
    (void)count; // Not used for character insertion

    if (key < 32 || key > 126) {
        return 0; // Ignore non-printable characters
    }

    if (editor_insert_char(ed, (char)key) == 0) {
        editor_refresh_display(ed);
        return 0;
    }
    return -1;
}

static int handle_backspace(struct line_editor *ed, int key, int count) {
    (void)key;
    (void)count;

    if (editor_backspace_char(ed) == 0) {
        editor_refresh_display(ed);
        return 0;
    }
    return 0; // Don't treat as error if nothing to delete
}

static int handle_delete(struct line_editor *ed, int key, int count) {
    (void)key;
    (void)count;

    if (editor_delete_char(ed) == 0) {
        editor_refresh_display(ed);
        return 0;
    }
    return 0; // Don't treat as error if nothing to delete
}

static int handle_move_left(struct line_editor *ed, int key, int count) {
    (void)key;
    if (count <= 0) count = 1;

    if (editor_move_cursor(ed, -count) == 0) {
        editor_refresh_display(ed);
        return 0;
    }
    return -1;
}

static int handle_move_right(struct line_editor *ed, int key, int count) {
    (void)key;
    if (count <= 0) count = 1;

    if (editor_move_cursor(ed, count) == 0) {
        editor_refresh_display(ed);
        return 0;
    }
    return -1;
}

static int handle_move_home(struct line_editor *ed, int key, int count) {
    (void)key;
    (void)count;

    ed->cursor_pos = 0;
    editor_refresh_display(ed);
    return 0;
}

static int handle_move_end(struct line_editor *ed, int key, int count) {
    (void)key;
    (void)count;

    ed->cursor_pos = ed->buffer_len;
    editor_refresh_display(ed);
    return 0;
}

static int handle_enter(struct line_editor *ed, int key, int count) {
    (void)key;
    (void)count;

    // Move to end of line and print newline
    terminal_write(ed->term, "\n", 1);
    return 1; // Signal that we're done editing
}

static int handle_ctrl_c(struct line_editor *ed, int key, int count) {
    (void)key;
    (void)count;

    // Clear the line and show newline
    terminal_write(ed->term, "^C\n", 3);

    // Clear the buffer
    ed->buffer_len = 0;
    ed->cursor_pos = 0;
    ed->buffer[0] = '\0';

    return -2; // Signal interrupt
}

static int handle_ctrl_d(struct line_editor *ed, int key, int count) {
    (void)key;
    (void)count;

    if (ed->buffer_len == 0) {
        // Empty line - signal EOF
        return -3;
    } else {
        // Non-empty line - delete character under cursor
        return handle_delete(ed, KEY_DELETE, 1);
    }
}

static int handle_ctrl_l(struct line_editor *ed, int key, int count) {
    (void)key;
    (void)count;

    // Clear screen and redraw
    terminal_write(ed->term, ANSI_CLEAR_SCREEN, strlen(ANSI_CLEAR_SCREEN));
    editor_refresh_display(ed);
    return 0;
}

// Enhanced readline implementation with line editing
static char *editor_readline(const char *prompt) {
    if (!g_initialized) {
        repl_init();
    }

    // Fall back to simple input for non-TTY
    if (!g_terminal.is_tty) {
        // Simple fallback for non-interactive input (pipes, redirects)
        if (prompt) {
            printf("%s", prompt);
            fflush(stdout);
        }

        char *line = NULL;
        size_t len = 0;
#ifdef _WIN32
        // Windows doesn't have getline, so implement a simple version
        size_t capacity = 128;
        line = malloc(capacity);
        if (!line) return NULL;

        char c;
        while ((c = getchar()) != EOF && c != '\n') {
            if (len >= capacity - 1) {
                capacity *= 2;
                char *new_line = realloc(line, capacity);
                if (!new_line) {
                    free(line);
                    return NULL;
                }
                line = new_line;
            }
            line[len++] = c;
        }

        if (c == EOF && len == 0) {
            free(line);
            return NULL;
        }

        line[len] = '\0';
        ssize_t nread = len;
#else
        ssize_t nread = getline(&line, &len, stdin);

        if (nread == -1) {
            if (line) free(line);
            return NULL; // EOF or error
        }

        // Remove trailing newline
        if (nread > 0 && line[nread-1] == '\n') {
            line[nread-1] = '\0';
        }
#endif

        return line;
    }

    // Initialize editor
    if (editor_init(&g_editor, prompt) != 0) {
        return NULL;
    }

    // Enable raw mode
    if (terminal_raw_mode(&g_terminal, true) != 0) {
        editor_cleanup(&g_editor);
        return repl_readline(prompt); // Fall back to simple input
    }

    // Display initial prompt
    editor_refresh_display(&g_editor);

    int done = 0;
    char *result = NULL;

    while (!done) {
        // Check for signals before reading input
        if (check_signals()) {
            done = -2; // Signal received
            break;
        }

        int key = terminal_read_key(&g_terminal);
        if (key == KEY_ERROR || key == KEY_EOF) {
            done = -1; // Error or EOF
            break;
        }

        // Platform-specific key mapping for delete/backspace
        #ifdef _WIN32
            // Windows: key 8 = backspace, key 127 = delete
            if (key == 8) {
                key = KEY_BACKSPACE;
            } else if (key == 127) {
                key = KEY_DELETE;
            }
        #else
            // Mac/Linux: key 127 = backspace, delete/fn+delete send ESC[3~ sequence (handled above)
            if (key == 127) {
                key = KEY_BACKSPACE;
            } else if (key == 8) {
                key = KEY_BACKSPACE; // Ctrl+H also acts as backspace
            }
        #endif

        // Find handler for this key
        key_handler_t handler = find_key_handler(key);

        if (handler) {
            int handler_result = handler(&g_editor, key, 1);
            if (handler_result > 0) {
                done = 1; // Normal completion (Enter pressed)
            } else if (handler_result < -1) {
                done = handler_result; // Special exit codes (Ctrl+C, Ctrl+D)
            }
        } else {
            // Default action - try to insert printable characters
            if (key >= 32 && key <= 126) {
                handle_char_insert(&g_editor, key, 1);
            }
        }
    }

    // Disable raw mode
    terminal_raw_mode(&g_terminal, false);

    if (done == 1) {
        // Normal completion - return the line
        result = strdup(g_editor.buffer);

        // Add to history if it's not empty
        if (result && strlen(result) > 0) {
            history_add_entry(&g_history, result);
        }
    }
    // For other exit codes (Ctrl+C, Ctrl+D, error), return NULL

    editor_cleanup(&g_editor);
    return result;
}

// ============================================================================
// PHASE 3: HISTORY SYSTEM
// ============================================================================

int history_init(struct history *hist, size_t max_size) {
    if (!hist) return -1;

    memset(hist, 0, sizeof(*hist));
    hist->max_size = max_size > 0 ? max_size : 100; // Default to 100
    hist->head = NULL;
    hist->tail = NULL;
    hist->current = NULL;
    hist->count = 0;
    hist->filename = NULL;

    return 0;
}

void history_cleanup(struct history *hist) {
    if (!hist) return;

    // Free all history entries
    struct history_entry *entry = hist->head;
    while (entry) {
        struct history_entry *next = entry->next;
        if (entry->line) {
            free(entry->line);
        }
        free(entry);
        entry = next;
    }

    // Free filename if set
    if (hist->filename) {
        free(hist->filename);
    }

    memset(hist, 0, sizeof(*hist));
}

int history_add_entry(struct history *hist, const char *line) {
    if (!hist || !line || strlen(line) == 0) {
        return 0; // Don't add empty lines
    }

    // Don't add lines starting with '.' (REPL commands)
    if (line[0] == '.') {
        return 0;
    }

    // Don't add duplicate of the last entry
    if (hist->tail && hist->tail->line && strcmp(hist->tail->line, line) == 0) {
        return 0;
    }

    // Create new entry
    struct history_entry *entry = malloc(sizeof(struct history_entry));
    if (!entry) {
        return -1;
    }

    entry->line = strdup(line);
    if (!entry->line) {
        free(entry);
        return -1;
    }

    entry->next = NULL;
    entry->prev = hist->tail;

    // Add to list
    if (hist->tail) {
        hist->tail->next = entry;
    } else {
        hist->head = entry;
    }
    hist->tail = entry;
    hist->count++;

    // Enforce size limit
    while (hist->count > hist->max_size && hist->head) {
        struct history_entry *old_head = hist->head;
        hist->head = old_head->next;
        if (hist->head) {
            hist->head->prev = NULL;
        } else {
            hist->tail = NULL;
        }

        if (old_head->line) {
            free(old_head->line);
        }
        free(old_head);
        hist->count--;
    }

    // Reset current pointer to end
    hist->current = NULL;

    return 0;
}

const char *history_get_entry(struct history *hist, int offset) {
    if (!hist || hist->count == 0) {
        return NULL;
    }

    struct history_entry *entry;

    if (offset == 0) {
        // Return current position or end of history
        return hist->current ? hist->current->line : NULL;
    } else if (offset > 0) {
        // Move forward in history
        if (!hist->current) {
            return NULL; // Already at end
        }

        entry = hist->current;
        for (int i = 0; i < offset && entry; i++) {
            entry = entry->next;
        }

        hist->current = entry;
        return entry ? entry->line : NULL;
    } else {
        // Move backward in history
        if (!hist->current) {
            // Start from end
            hist->current = hist->tail;
        } else {
            entry = hist->current;
            for (int i = 0; i < -offset && entry; i++) {
                entry = entry->prev;
            }
            hist->current = entry;
        }

        return hist->current ? hist->current->line : NULL;
    }
}

const char *history_search_prefix(struct history *hist, const char *prefix) {
    if (!hist || !prefix || hist->count == 0) {
        return NULL;
    }

    size_t prefix_len = strlen(prefix);
    if (prefix_len == 0) {
        return NULL;
    }

    // Start from current position or end
    struct history_entry *entry = hist->current ? hist->current->prev : hist->tail;

    while (entry) {
        if (entry->line && strncmp(entry->line, prefix, prefix_len) == 0) {
            hist->current = entry;
            return entry->line;
        }
        entry = entry->prev;
    }

    return NULL;
}

int history_save_to_file(struct history *hist, const char *filename) {
    if (!hist || !filename) {
        return -1;
    }

    FILE *file = fopen(filename, "w");
    if (!file) {
        return -1;
    }

    struct history_entry *entry = hist->head;
    while (entry) {
        if (entry->line) {
            fprintf(file, "%s\n", entry->line);
        }
        entry = entry->next;
    }

    fclose(file);
    return 0;
}

int history_load_from_file(struct history *hist, const char *filename) {
    if (!hist || !filename) {
        return -1;
    }

    FILE *file = fopen(filename, "r");
    if (!file) {
        return -1; // File doesn't exist or can't be opened
    }

    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        // Remove trailing newline
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
        }

        history_add_entry(hist, line);
    }

    fclose(file);
    return 0;
}

// History navigation key handlers
static int handle_history_prev(struct line_editor *ed, int key, int count) {
    (void)key;
    (void)count;

    const char *line = history_get_entry(&g_history, -1);
    if (line) {
        // Replace current buffer with history line
        ed->buffer_len = 0;
        ed->cursor_pos = 0;

        size_t line_len = strlen(line);
        if (editor_ensure_buffer_size(ed, line_len + 1) == 0) {
            str_copy(ed->buffer, ed->buffer_size, line, line_len);
            ed->buffer_len = line_len;
            ed->cursor_pos = line_len;
            editor_refresh_display(ed);
        }
    }

    return 0;
}

static int handle_history_next(struct line_editor *ed, int key, int count) {
    (void)key;
    (void)count;

    const char *line = history_get_entry(&g_history, 1);
    if (line) {
        // Replace current buffer with history line
        ed->buffer_len = 0;
        ed->cursor_pos = 0;

        size_t line_len = strlen(line);
        if (editor_ensure_buffer_size(ed, line_len + 1) == 0) {
            str_copy(ed->buffer, ed->buffer_size, line, line_len);
            ed->buffer_len = line_len;
            ed->cursor_pos = line_len;
            editor_refresh_display(ed);
        }
    } else {
        // No more history, clear buffer
        ed->buffer_len = 0;
        ed->cursor_pos = 0;
        ed->buffer[0] = '\0';
        editor_refresh_display(ed);
    }

    return 0;
}

static int handle_tab_completion(struct line_editor *ed, int key, int count) {
    (void)key;
    (void)count;

    if (!ed || !rl_attempted_completion_function) {
        // No completion function available, insert a tab character
        return editor_insert_char(ed, '\t');
    }

    // Find the start of the word to complete
    size_t word_start = cmdedit_utf8_find_word_start(ed->buffer, ed->buffer_len, ed->cursor_pos);

    // Extract the prefix to complete
    size_t prefix_len = ed->cursor_pos - word_start;
    char *prefix = NULL;
    if (prefix_len > 0) {
        prefix = malloc(prefix_len + 1);
        if (!prefix) return -1;
        memcpy(prefix, ed->buffer + word_start, prefix_len);
        prefix[prefix_len] = '\0';
    } else {
        prefix = malloc(1);
        if (!prefix) return -1;
        prefix[0] = '\0';
    }

    // Call the completion function
    char **completions = (*rl_attempted_completion_function)(prefix, word_start, ed->cursor_pos);

    if (completions && completions[0]) {
        // We have at least one completion
        const char *completion = completions[0];
        size_t completion_len = strlen(completion);

        // Check if we need to replace the prefix or just append
        size_t common_prefix = 0;
        while (common_prefix < prefix_len &&
               common_prefix < completion_len &&
               prefix[common_prefix] == completion[common_prefix]) {
            common_prefix++;
        }

        // Remove the old prefix beyond the common part
        if (prefix_len > common_prefix) {
            size_t remove_count = prefix_len - common_prefix;
            memmove(ed->buffer + word_start + common_prefix,
                   ed->buffer + ed->cursor_pos,
                   ed->buffer_len - ed->cursor_pos);
            ed->buffer_len -= remove_count;
            ed->cursor_pos -= remove_count;
        }

        // Insert the new completion part
        const char *insert_text = completion + common_prefix;
        size_t insert_len = completion_len - common_prefix;

        if (editor_ensure_buffer_size(ed, ed->buffer_len + insert_len + 1) == 0) {
            // Make space for the new text
            memmove(ed->buffer + ed->cursor_pos + insert_len,
                   ed->buffer + ed->cursor_pos,
                   ed->buffer_len - ed->cursor_pos);

            // Insert the completion text
            memcpy(ed->buffer + ed->cursor_pos, insert_text, insert_len);
            ed->buffer_len += insert_len;
            ed->cursor_pos += insert_len;
            ed->buffer[ed->buffer_len] = '\0';

            editor_refresh_display(ed);
        }

        // Free the completions array
        for (int i = 0; completions[i]; i++) {
            free(completions[i]);
        }
        free(completions);
    }

    free(prefix);
    return 0;
}

// Enhanced repl_add_history implementation
// (This replaces the basic implementation above)

// Enhanced readline compatibility functions
int clear_history(void) {
    if (!g_initialized) {
        return 0;
    }

    history_cleanup(&g_history);
    history_init(&g_history, 100); // Reset with default size
    return 0;
}

int read_history(const char *filename) {
    if (!g_initialized) {
        repl_init();
    }

    // For readline compatibility, return 0 even if file doesn't exist
    int result = history_load_from_file(&g_history, filename);
    return (result == -1 && filename) ? 0 : result; // Convert file-not-found to success
}

int write_history(const char *filename) {
    if (!g_initialized) {
        return -1;
    }

    return history_save_to_file(&g_history, filename);
}

// ============================================================================
// PHASE 4: ADVANCED EDITING FEATURES
// ============================================================================

// Kill ring for cut/copy/paste functionality
#define KILL_RING_SIZE 10

struct kill_ring {
    char *entries[KILL_RING_SIZE];
    int current;
    int count;
};

static struct kill_ring g_kill_ring = {0};

// Word boundary detection
static bool is_word_char(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           c == '_';
}

static size_t find_word_start(const char *buffer, size_t pos, size_t len) {
    return cmdedit_utf8_find_word_start(buffer, len, pos);
}

static size_t find_word_end(const char *buffer, size_t pos, size_t len) {
    return cmdedit_utf8_find_word_end(buffer, len, pos);
}

static size_t find_next_word_start(const char *buffer, size_t pos, size_t len) {
    // Skip current word
    while (pos < len && is_word_char(buffer[pos])) {
        pos++;
    }

    // Skip whitespace
    while (pos < len && !is_word_char(buffer[pos])) {
        pos++;
    }

    return pos;
}

static size_t find_prev_word_start(const char *buffer, size_t pos, size_t len) {
    (void)len; // Unused parameter
    if (pos == 0) return 0;

    // Move back one to start from previous character
    pos--;

    // Skip whitespace backwards
    while (pos > 0 && !is_word_char(buffer[pos])) {
        pos--;
    }

    // Skip word characters backwards to find start
    while (pos > 0 && is_word_char(buffer[pos - 1])) {
        pos--;
    }

    return pos;
}

// Kill ring operations
static void kill_ring_add(const char *text) {
    if (!text || strlen(text) == 0) {
        return;
    }

    // Free old entry if ring is full
    if (g_kill_ring.count == KILL_RING_SIZE && g_kill_ring.entries[g_kill_ring.current]) {
        free(g_kill_ring.entries[g_kill_ring.current]);
    }

    // Add new entry
    g_kill_ring.entries[g_kill_ring.current] = strdup(text);

    // Update counters
    g_kill_ring.current = (g_kill_ring.current + 1) % KILL_RING_SIZE;
    if (g_kill_ring.count < KILL_RING_SIZE) {
        g_kill_ring.count++;
    }
}

static const char *kill_ring_get(int offset) {
    if (g_kill_ring.count == 0) {
        return NULL;
    }

    int index = (g_kill_ring.current - 1 - offset + KILL_RING_SIZE) % KILL_RING_SIZE;
    if (offset >= g_kill_ring.count) {
        return NULL;
    }

    return g_kill_ring.entries[index];
}

static void kill_ring_cleanup(void) {
    for (int i = 0; i < KILL_RING_SIZE; i++) {
        if (g_kill_ring.entries[i]) {
            free(g_kill_ring.entries[i]);
            g_kill_ring.entries[i] = NULL;
        }
    }
    g_kill_ring.current = 0;
    g_kill_ring.count = 0;
}

// Advanced key handlers
#ifdef CMDEDIT_TESTING
int handle_word_forward(struct line_editor *ed, int key, int count) {
#else
static int handle_word_forward(struct line_editor *ed, int key, int count) {
#endif
    (void)key;
    if (count <= 0) count = 1;

    for (int i = 0; i < count; i++) {
        size_t new_pos = find_next_word_start(ed->buffer, ed->cursor_pos, ed->buffer_len);
        ed->cursor_pos = new_pos;
    }

    editor_refresh_display(ed);
    return 0;
}

#ifdef CMDEDIT_TESTING
int handle_word_backward(struct line_editor *ed, int key, int count) {
#else
static int handle_word_backward(struct line_editor *ed, int key, int count) {
#endif
    (void)key;
    if (count <= 0) count = 1;

    for (int i = 0; i < count; i++) {
        size_t new_pos = find_prev_word_start(ed->buffer, ed->cursor_pos, ed->buffer_len);
        ed->cursor_pos = new_pos;
    }

    editor_refresh_display(ed);
    return 0;
}

#ifdef CMDEDIT_TESTING
int handle_kill_line(struct line_editor *ed, int key, int count) {
#else
static int handle_kill_line(struct line_editor *ed, int key, int count) {
#endif
    (void)key;
    (void)count;

    if (ed->cursor_pos >= ed->buffer_len) {
        return 0; // Nothing to kill
    }

    // Kill from cursor to end of line
    char *killed_text = strdup(ed->buffer + ed->cursor_pos);
    kill_ring_add(killed_text);
    free(killed_text);

    // Truncate buffer at cursor
    ed->buffer[ed->cursor_pos] = '\0';
    ed->buffer_len = ed->cursor_pos;

    editor_refresh_display(ed);
    return 0;
}

#ifdef CMDEDIT_TESTING
int handle_kill_whole_line(struct line_editor *ed, int key, int count) {
#else
static int handle_kill_whole_line(struct line_editor *ed, int key, int count) {
#endif
    (void)key;
    (void)count;

    if (ed->buffer_len == 0) {
        return 0; // Nothing to kill
    }

    // Kill entire line
    kill_ring_add(ed->buffer);

    // Clear buffer
    ed->buffer[0] = '\0';
    ed->buffer_len = 0;
    ed->cursor_pos = 0;

    editor_refresh_display(ed);
    return 0;
}

#ifdef CMDEDIT_TESTING
int handle_kill_word(struct line_editor *ed, int key, int count) {
#else
static int handle_kill_word(struct line_editor *ed, int key, int count) {
#endif
    (void)key;
    if (count <= 0) count = 1;

    size_t start_pos = ed->cursor_pos;
    size_t end_pos = start_pos;

    for (int i = 0; i < count; i++) {
        end_pos = find_next_word_start(ed->buffer, end_pos, ed->buffer_len);
    }

    if (end_pos > start_pos) {
        // Extract killed text
        char killed_text[end_pos - start_pos + 1];
        memcpy(killed_text, ed->buffer + start_pos, end_pos - start_pos);
        killed_text[end_pos - start_pos] = '\0';
        kill_ring_add(killed_text);

        // Remove text from buffer
        memmove(ed->buffer + start_pos, ed->buffer + end_pos, ed->buffer_len - end_pos + 1);
        ed->buffer_len -= (end_pos - start_pos);

        editor_refresh_display(ed);
    }

    return 0;
}

#ifdef CMDEDIT_TESTING
int handle_backward_kill_word(struct line_editor *ed, int key, int count) {
#else
static int handle_backward_kill_word(struct line_editor *ed, int key, int count) {
#endif
    (void)key;
    if (count <= 0) count = 1;

    size_t end_pos = ed->cursor_pos;
    size_t start_pos = end_pos;

    for (int i = 0; i < count; i++) {
        start_pos = find_prev_word_start(ed->buffer, start_pos, ed->buffer_len);
    }

    if (start_pos < end_pos) {
        // Extract killed text
        char killed_text[end_pos - start_pos + 1];
        memcpy(killed_text, ed->buffer + start_pos, end_pos - start_pos);
        killed_text[end_pos - start_pos] = '\0';
        kill_ring_add(killed_text);

        // Remove text from buffer
        memmove(ed->buffer + start_pos, ed->buffer + end_pos, ed->buffer_len - end_pos + 1);
        ed->buffer_len -= (end_pos - start_pos);
        ed->cursor_pos = start_pos;

        editor_refresh_display(ed);
    }

    return 0;
}

#ifdef CMDEDIT_TESTING
int handle_yank(struct line_editor *ed, int key, int count) {
#else
static int handle_yank(struct line_editor *ed, int key, int count) {
#endif
    (void)key;
    (void)count;

    const char *text = kill_ring_get(0);
    if (!text) {
        return 0; // Nothing to yank
    }

    size_t text_len = strlen(text);
    if (editor_ensure_buffer_size(ed, ed->buffer_len + text_len + 1) != 0) {
        return -1;
    }

    // Insert text at cursor
    memmove(ed->buffer + ed->cursor_pos + text_len,
            ed->buffer + ed->cursor_pos,
            ed->buffer_len - ed->cursor_pos + 1);

    memcpy(ed->buffer + ed->cursor_pos, text, text_len);
    ed->cursor_pos += text_len;
    ed->buffer_len += text_len;

    editor_refresh_display(ed);
    return 0;
}

#ifdef CMDEDIT_TESTING
int handle_transpose_chars(struct line_editor *ed, int key, int count) {
#else
static int handle_transpose_chars(struct line_editor *ed, int key, int count) {
#endif
    (void)key;
    (void)count;

    if (ed->buffer_len < 2) {
        return 0; // Need at least 2 characters
    }

    size_t pos = ed->cursor_pos;

    // If at end, transpose last two chars
    if (pos >= ed->buffer_len) {
        pos = ed->buffer_len - 1;
    }

    // If at beginning, transpose first two chars
    if (pos == 0) {
        pos = 1;
    }

    // Transpose characters at pos-1 and pos
    if (pos > 0 && pos < ed->buffer_len) {
        char temp = ed->buffer[pos - 1];
        ed->buffer[pos - 1] = ed->buffer[pos];
        ed->buffer[pos] = temp;

        // Move cursor forward if not at end
        if (ed->cursor_pos < ed->buffer_len) {
            ed->cursor_pos++;
        }

        editor_refresh_display(ed);
    }

    return 0;
}
