#ifndef CMDEDIT_H
#define CMDEDIT_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Core API functions (readline compatibility)
char *readline(const char *prompt);
int add_history(const char *line);
int clear_history(void);

// Lambda-specific API (for compatibility)
int repl_init(void);
void repl_cleanup(void);
char *repl_readline(const char *prompt);
int repl_add_history(const char *line);

// Configuration variables
extern char *rl_line_buffer;
extern int rl_point;
extern int rl_end;
extern char *rl_prompt;

// History management
int read_history(const char *filename);
int write_history(const char *filename);

// Completion (basic framework)
typedef char **(*rl_completion_func_t)(const char *, int, int);
extern rl_completion_func_t *rl_attempted_completion_function;

// Key binding functions
int rl_bind_key(int key, int (*function)(int, int));

// Key codes
enum {
    KEY_ENTER = 13,
    KEY_BACKSPACE = 8,
    KEY_TAB = 9,
    KEY_ESC = 27,
    KEY_DELETE = 127,
    
    // Extended keys (> 256)
    KEY_UP = 256,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_PAGE_UP,
    KEY_PAGE_DOWN,
    
    // Control combinations
    KEY_CTRL_A = 1,
    KEY_CTRL_B = 2,
    KEY_CTRL_C = 3,
    KEY_CTRL_D = 4,
    KEY_CTRL_E = 5,
    KEY_CTRL_F = 6,
    KEY_CTRL_G = 7,
    KEY_CTRL_H = 8,
    KEY_CTRL_I = 9,   // TAB
    KEY_CTRL_J = 10,  // LF
    KEY_CTRL_K = 11,
    KEY_CTRL_L = 12,
    KEY_CTRL_M = 13,  // CR
    KEY_CTRL_N = 14,
    KEY_CTRL_O = 15,
    KEY_CTRL_P = 16,
    KEY_CTRL_Q = 17,
    KEY_CTRL_R = 18,
    KEY_CTRL_S = 19,
    KEY_CTRL_T = 20,
    KEY_CTRL_U = 21,
    KEY_CTRL_V = 22,
    KEY_CTRL_W = 23,
    KEY_CTRL_X = 24,
    KEY_CTRL_Y = 25,
    KEY_CTRL_Z = 26,
    
    KEY_ERROR = -1,
    KEY_EOF = -2
};

// Internal functions exposed for testing
#ifdef CMDEDIT_TESTING

struct terminal_state {
    int input_fd;
    int output_fd;
    bool is_tty;
    bool raw_mode;
    
#ifdef _WIN32
    void *h_stdin;  // HANDLE
    void *h_stdout; // HANDLE
    unsigned long orig_input_mode;  // DWORD
    unsigned long orig_output_mode; // DWORD
#else
    char orig_termios_data[256]; // Storage for struct termios (platform-specific size)
#endif
};

struct line_editor {
    char *buffer;
    size_t buffer_size;
    size_t buffer_len;
    size_t cursor_pos;
    size_t display_pos;
    char *prompt;
    size_t prompt_len;
    struct terminal_state *term;
    bool needs_refresh;     // Display needs updating
};

struct history_entry {
    char *line;
    struct history_entry *next;
    struct history_entry *prev;
};

struct history {
    struct history_entry *head;
    struct history_entry *tail;
    struct history_entry *current;
    size_t count;
    size_t max_size;
    char *filename;
};

typedef struct terminal_state terminal_state_t;
typedef struct line_editor line_editor_t;
typedef struct history history_t;

// Terminal operations
int terminal_init(struct terminal_state *term);
int terminal_cleanup(struct terminal_state *term);
int terminal_raw_mode(struct terminal_state *term, bool enable);
int terminal_get_size(struct terminal_state *term, int *rows, int *cols);
int terminal_read_key(struct terminal_state *term);

// Line editor operations
int editor_init(struct line_editor *ed, const char *prompt);
void editor_cleanup(struct line_editor *ed);
int editor_insert_char(struct line_editor *ed, char c);
int editor_delete_char(struct line_editor *ed);
int editor_backspace_char(struct line_editor *ed);
int editor_move_cursor(struct line_editor *ed, int offset);
void editor_refresh_display(struct line_editor *ed);

// History operations
int history_init(struct history *hist, size_t max_size);
void history_cleanup(struct history *hist);
int history_add_entry(struct history *hist, const char *line);
const char *history_get_entry(struct history *hist, int offset);
const char *history_search_prefix(struct history *hist, const char *prefix);
int history_save_to_file(struct history *hist, const char *filename);
int history_load_from_file(struct history *hist, const char *filename);

// Advanced editing operations (Phase 4)
int handle_kill_line(struct line_editor *ed, int key, int count);
int handle_kill_whole_line(struct line_editor *ed, int key, int count);
int handle_yank(struct line_editor *ed, int key, int count);
int handle_transpose_chars(struct line_editor *ed, int key, int count);
int handle_word_forward(struct line_editor *ed, int key, int count);
int handle_word_backward(struct line_editor *ed, int key, int count);
int handle_kill_word(struct line_editor *ed, int key, int count);
int handle_backward_kill_word(struct line_editor *ed, int key, int count);

#else

typedef struct terminal_state terminal_state_t;
typedef struct line_editor line_editor_t;
typedef struct history history_t;

#endif

#ifdef __cplusplus
}
#endif

#endif // CMDEDIT_H
