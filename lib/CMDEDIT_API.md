# Command Line Editor (cmdedit) - API Reference

## Core API Functions

### Initialization and Cleanup

#### `int repl_init(void)`
Initializes the command line editor system.

**Returns:**
- `0` on success
- `-1` on failure (terminal not available, signal setup failed)

**Notes:**
- Must be called before using other functions
- Safe to call multiple times (idempotent)
- Automatically called by `repl_readline()` if needed

---

#### `void repl_cleanup(void)`
Cleans up the command line editor system.

**Notes:**
- Releases all allocated memory
- Restores terminal settings
- Restores signal handlers
- Safe to call multiple times

---

### Line Input

#### `char *repl_readline(const char *prompt)`
Reads a line of input with full editing capabilities.

**Parameters:**
- `prompt`: String to display as prompt (can be NULL)

**Returns:**
- Allocated string containing user input (caller must free)
- `NULL` on EOF, error, or signal interruption

**Features:**
- Full line editing with cursor movement
- History navigation
- Tab completion (if function set)
- UTF-8 Unicode support
- Signal handling

**Example:**
```c
char *line = repl_readline("Enter command: ");
if (line) {
    printf("Got: %s\n", line);
    free(line);
}
```

---

#### `char *readline(const char *prompt)`
GNU Readline compatibility function. Identical to `repl_readline()`.

---

### History Management

#### `int repl_add_history(const char *line)`
Adds a line to the command history.

**Parameters:**
- `line`: String to add to history

**Returns:**
- `0` on success
- `-1` on failure (memory allocation error)

**Notes:**
- Automatically filters empty lines
- Manages history size limits
- Initializes editor if needed

---

#### `int add_history(const char *line)`
GNU Readline compatibility function. Identical to `repl_add_history()`.

---

#### `int read_history(const char *filename)`
Loads command history from a file.

**Parameters:**
- `filename`: Path to history file

**Returns:**
- `0` on success
- `-1` on failure (file not found, permission denied)

**Notes:**
- Appends to existing in-memory history
- File format: one command per line
- Creates file if it doesn't exist

---

#### `int write_history(const char *filename)`
Saves command history to a file.

**Parameters:**
- `filename`: Path to history file

**Returns:**
- `0` on success  
- `-1` on failure (permission denied, disk full)

**Notes:**
- Overwrites existing file
- Creates directories if needed
- Atomic write (temporary file + rename)

---

#### `int clear_history(void)`
Clears all command history.

**Returns:**
- `0` on success

**Notes:**
- Frees all history memory
- Resets history position

---

### Tab Completion

#### `typedef char **(*rl_completion_func_t)(const char *, int, int)`
Function pointer type for tab completion callbacks.

**Parameters:**
- `text`: Text being completed
- `start`: Start position in buffer
- `end`: End position in buffer

**Returns:**
- Array of string matches (NULL-terminated)
- `NULL` if no matches

**Memory Management:**
- Caller must free returned array and all strings
- Each string in array must be individually allocated

**Example:**
```c
char **my_completer(const char *text, int start, int end) {
    char **matches = malloc(sizeof(char*) * 2);
    matches[0] = strdup("hello");
    matches[1] = NULL;
    return matches;
}
```

---

#### `extern rl_completion_func_t *rl_attempted_completion_function`
Global function pointer for tab completion.

**Usage:**
```c
rl_attempted_completion_function = &my_completion_function;
```

**Notes:**
- Set to `NULL` to disable completion
- When `NULL`, TAB inserts a tab character
- Called when user presses TAB key

---

### Readline Compatibility Variables

#### `extern char *rl_line_buffer`
Current input buffer (read-only).

**Notes:**
- Valid only during completion callbacks
- Do not modify or free
- May contain partial input

---

#### `extern int rl_point`
Current cursor position in buffer.

**Notes:**
- Byte offset, not character offset
- Valid during completion callbacks
- Read-only

---

#### `extern int rl_end`
Length of current buffer.

**Notes:**
- Byte length, not character count
- Valid during completion callbacks
- Read-only

---

#### `extern char *rl_prompt`
Current prompt string.

**Notes:**
- Valid during editing session
- Read-only
- May be NULL

---

### Key Binding

#### `int rl_bind_key(int key, int (*function)(int, int))`
Binds a key to a handler function.

**Parameters:**
- `key`: Key code to bind
- `function`: Handler function

**Returns:**
- `0` on success
- `-1` on failure

**Notes:**
- Currently limited implementation
- Handler signature may change
- See key codes in `cmdedit.h`

---

## Advanced API (Testing Interface)

These functions are available when compiled with `-DCMDEDIT_TESTING`:

### Editor Management

#### `int editor_init(struct line_editor *ed, const char *prompt)`
Initializes a line editor instance.

**Parameters:**
- `ed`: Editor instance to initialize
- `prompt`: Prompt string

**Returns:**
- `0` on success
- `-1` on failure

---

#### `void editor_cleanup(struct line_editor *ed)`
Cleans up a line editor instance.

**Parameters:**
- `ed`: Editor instance to clean up

---

#### `void editor_refresh_display(struct line_editor *ed)`
Forces a display refresh.

**Parameters:**
- `ed`: Editor instance

**Notes:**
- Usually called automatically
- Use for custom display updates

---

### Character Operations

#### `int editor_insert_char(struct line_editor *ed, char c)`
Inserts a character at cursor position.

**Parameters:**
- `ed`: Editor instance
- `c`: Character to insert

**Returns:**
- `0` on success
- `-1` on failure

---

#### `int editor_delete_char(struct line_editor *ed)`
Deletes character at cursor position.

**Parameters:**
- `ed`: Editor instance

**Returns:**
- `0` on success
- `-1` on failure

---

#### `int editor_backspace_char(struct line_editor *ed)`
Deletes character before cursor position.

**Parameters:**
- `ed`: Editor instance

**Returns:**
- `0` on success
- `-1` on failure

---

#### `int editor_move_cursor(struct line_editor *ed, int offset)`
Moves cursor by character count.

**Parameters:**
- `ed`: Editor instance
- `offset`: Characters to move (negative = left, positive = right)

**Returns:**
- `0` on success
- `-1` on failure

**Notes:**
- UTF-8 aware (moves by characters, not bytes)
- Bounded by buffer limits

---

### Advanced Editing Operations

#### `int handle_kill_line(struct line_editor *ed, int key, int count)`
Kills from cursor to end of line.

**Parameters:**
- `ed`: Editor instance
- `key`: Key that triggered (usually Ctrl+K)
- `count`: Repeat count

**Returns:**
- `0` to continue editing
- `1` to finish editing
- negative for error

---

#### `int handle_kill_whole_line(struct line_editor *ed, int key, int count)`
Kills entire line.

---

#### `int handle_yank(struct line_editor *ed, int key, int count)`
Yanks (pastes) from kill ring.

---

#### `int handle_transpose_chars(struct line_editor *ed, int key, int count)`
Transposes character at cursor with previous character.

---

#### `int handle_word_forward(struct line_editor *ed, int key, int count)`
Moves cursor forward by words.

---

#### `int handle_word_backward(struct line_editor *ed, int key, int count)`
Moves cursor backward by words.

---

#### `int handle_kill_word(struct line_editor *ed, int key, int count)`
Kills word forward.

---

#### `int handle_backward_kill_word(struct line_editor *ed, int key, int count)`
Kills word backward.

---

### Terminal Operations

#### `int terminal_init(struct terminal_state *term)`
Initializes terminal state.

---

#### `int terminal_cleanup(struct terminal_state *term)`
Cleans up terminal state.

---

#### `int terminal_raw_mode(struct terminal_state *term, bool enable)`
Enables/disables raw mode.

---

## UTF-8 Support Functions

These functions are in `cmdedit_utf8.h`:

#### `int utf8_char_count(const char* str, size_t byte_len)`
Counts UTF-8 characters in string.

**Parameters:**
- `str`: UTF-8 string
- `byte_len`: Length in bytes

**Returns:**
- Number of Unicode characters

---

#### `int utf8_display_width(const char* str, size_t byte_len)`
Calculates display width of UTF-8 string.

**Parameters:**
- `str`: UTF-8 string  
- `byte_len`: Length in bytes

**Returns:**
- Display width (accounts for wide characters)

---

#### `size_t utf8_move_cursor_left(const char* str, size_t byte_len, size_t current_byte_offset)`
#### `size_t utf8_move_cursor_right(const char* str, size_t byte_len, size_t current_byte_offset)`
UTF-8 aware cursor movement.

**Returns:**
- New byte offset after movement

---

#### `bool utf8_is_valid(const char* str, size_t byte_len)`
Validates UTF-8 string.

**Returns:**
- `true` if valid UTF-8
- `false` if invalid

---

## Key Codes

Defined in `cmdedit.h`:

### Basic Keys
```c
KEY_ENTER = 13
KEY_BACKSPACE = 8  
KEY_TAB = 9
KEY_ESC = 27
KEY_DELETE = 127
```

### Extended Keys
```c
KEY_UP = 256
KEY_DOWN, KEY_LEFT, KEY_RIGHT
KEY_HOME, KEY_END
KEY_PAGE_UP, KEY_PAGE_DOWN
```

### Control Keys
```c
KEY_CTRL_A = 1  // through KEY_CTRL_Z = 26
```

## Data Structures

### `struct line_editor`
```c
struct line_editor {
    char *buffer;           // Current line buffer
    size_t buffer_size;     // Allocated size
    size_t buffer_len;      // Current length  
    size_t cursor_pos;      // Cursor position (bytes)
    char *prompt;           // Prompt string
    size_t prompt_len;      // Prompt display width
    struct terminal_state *term;
    bool needs_refresh;     // Display optimization flag
};
```

### `struct terminal_state`
Platform-specific terminal state (opaque).

### `struct history`
Command history state (opaque).

## Error Codes

Functions return:
- `0`: Success
- `-1`: General error
- `-2`: Signal received (SIGINT/SIGTERM)
- `1`: Normal completion (Enter pressed)

## Memory Management

- All returned strings must be freed by caller
- Library manages internal memory automatically
- Call `repl_cleanup()` to free all library memory
- Safe to call functions after cleanup (will re-initialize)

## Thread Safety

The library is **not thread-safe**. Use external synchronization if calling from multiple threads.

## Dependencies

- `utf8proc`: Unicode support
- Standard C library
- Platform-specific terminal libraries (termios on Unix, Windows Console API)

## Compilation

```bash
# Basic compilation
gcc -std=gnu99 -DUTF8PROC_STATIC myapp.c lib/cmdedit.c lib/cmdedit_utf8.c -lutf8proc

# With testing interface
gcc -std=gnu99 -DCMDEDIT_TESTING -DUTF8PROC_STATIC myapp.c lib/cmdedit.c lib/cmdedit_utf8.c -lutf8proc
```