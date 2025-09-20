# Lambda Cmdline Library Implementation Plan

## Executive Summary

This document outlines the plan to implement a custom command-line editing library (`lib/cmdedit.c` and `lib/cmdedit.h`) to replace the current dependencies on libreadline/libeditline in the Lambda scripting language. The library will be cross-platform (macOS/Linux/Windows), API-compatible with readline, and require no additional dependencies.

## Current State Analysis

### Current Usage in Lambda
From analyzing `lambda/main-repl.cpp`, the Lambda REPL currently uses:

1. **Core Functions**:
   - `repl_readline(const char *prompt)` - Get input line with editing support
   - `repl_add_history(const char *line)` - Add line to history
   - `repl_init()` - Initialize readline system
   - `repl_cleanup()` - Clean up resources

2. **Platform-Specific Behavior**:
   - **Unix/Linux/macOS**: Uses `editline/readline.h` and `histedit.h` (BSD libedit)
   - **Windows**: Falls back to simple `fgets()` to avoid DLL dependencies

3. **Features Currently Used**:
   - Line editing with emacs key bindings
   - Command history with configurable size (100 entries)
   - Prompt customization
   - Signal handling for crash recovery
   - Terminal detection (interactive vs non-interactive)
   - UTF-8 prompt support (`λ> ` vs `> `)

## API Compatibility Requirements

### Primary API Functions
The library must implement these functions for drop-in compatibility:

```c
// Core readline functions
char *readline(const char *prompt);
int add_history(const char *line);

// Library-specific functions (current Lambda API)
int repl_init(void);
void repl_cleanup(void);
char *repl_readline(const char *prompt);
int repl_add_history(const char *line);
```

### Additional Readline Functions (Standard Compatibility)
For broader compatibility, implement these standard readline functions:

```c
// History management
int clear_history(void);
int read_history(const char *filename);
int write_history(const char *filename);
int history_expand(char *string, char **output);

// Configuration
int rl_bind_key(int key, int (*function)(int, int));
char *rl_line_buffer;
int rl_point;
int rl_end;
char *rl_prompt;

// Completion (basic support)
typedef char **rl_completion_func_t(const char *, int, int);
rl_completion_func_t *rl_attempted_completion_function;
```

## Architecture Design

### Overall Structure

```
lib/cmdedit.h    - Public API header
lib/cmdedit.c    - Main implementation

Internal Components:
- Terminal I/O abstraction layer
- Line editor with key binding system  
- History management
- Platform-specific terminal control
- UTF-8 and Unicode support
- Signal handling framework
```

### Platform Abstraction Layer

#### Terminal I/O Interface
```c
typedef struct {
    int input_fd;
    int output_fd;
    bool is_tty;
    bool raw_mode;
    
    // Platform-specific terminal state
#ifdef _WIN32
    HANDLE h_stdin;
    HANDLE h_stdout;
    DWORD orig_input_mode;
    DWORD orig_output_mode;
#else
    struct termios orig_termios;
#endif
} terminal_state_t;

// Core terminal operations
int terminal_init(terminal_state_t *term);
int terminal_cleanup(terminal_state_t *term);
int terminal_raw_mode(terminal_state_t *term, bool enable);
int terminal_get_size(terminal_state_t *term, int *rows, int *cols);
```

#### Platform-Specific Implementation

**Unix/Linux/macOS**:
- Use `termios` for raw mode control
- Use `read()/write()` for I/O
- ANSI escape sequences for cursor control
- UTF-8 support via locale detection

**Windows**:
- Use Windows Console API (`SetConsoleMode`, `ReadConsoleInputW`, `WriteConsoleW`)
- UTF-16 to UTF-8 conversion for Unicode support
- Windows-specific key code handling
- Virtual terminal sequences (Windows 10+) or fallback console manipulation

### Line Editor Core

#### Data Structures
```c
typedef struct {
    char *buffer;           // Current line buffer
    size_t buffer_size;     // Allocated size
    size_t buffer_len;      // Current length
    size_t cursor_pos;      // Cursor position in buffer
    size_t display_pos;     // Cursor position on screen
    char *prompt;           // Current prompt string
    size_t prompt_len;      // Display width of prompt
} line_editor_t;

typedef struct {
    int key;                // Key code
    int (*handler)(line_editor_t *ed, int key, int count);
} key_binding_t;
```

#### Key Handling System
```c
// Key codes (normalized across platforms)
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
    // ... etc
};

// Default emacs-style key bindings
static key_binding_t default_bindings[] = {
    {KEY_CTRL_A, move_to_beginning},
    {KEY_CTRL_E, move_to_end},
    {KEY_CTRL_B, move_backward_char},
    {KEY_CTRL_F, move_forward_char},
    {KEY_CTRL_P, previous_history},
    {KEY_CTRL_N, next_history},
    {KEY_CTRL_K, kill_line},
    {KEY_CTRL_U, unix_line_discard},
    {KEY_CTRL_W, unix_word_rubout},
    {KEY_CTRL_L, clear_screen},
    {KEY_BACKSPACE, backward_delete_char},
    {KEY_DELETE, delete_char},
    {KEY_TAB, complete_line},
    // ...
};
```

### History Management

#### History Structure
```c
typedef struct history_entry {
    char *line;
    struct history_entry *next;
    struct history_entry *prev;
} history_entry_t;

typedef struct {
    history_entry_t *head;
    history_entry_t *tail;
    history_entry_t *current;  // For navigation
    size_t count;
    size_t max_size;
    char *filename;             // For persistent history
} history_t;

// History operations
int history_add(history_t *hist, const char *line);
const char *history_get(history_t *hist, int offset);
int history_search(history_t *hist, const char *prefix);
int history_save(history_t *hist, const char *filename);
int history_load(history_t *hist, const char *filename);
```

### Unicode and UTF-8 Support

#### String Handling
```c
// UTF-8 aware string operations
size_t utf8_strlen(const char *str);
size_t utf8_char_len(const char *str);
int utf8_char_width(uint32_t codepoint);
int utf8_validate(const char *str, size_t len);

// Display width calculation (accounting for wide characters)
size_t display_width(const char *str, size_t len);
```

## Implementation Phases

### Phase 1: Basic Terminal I/O (Week 1)
**Goal**: Get basic line input working on all platforms

- Implement terminal abstraction layer
- Raw mode control for Unix/Windows
- Basic key reading without editing
- Simple prompt display
- Exit on Enter/Ctrl+C

**Deliverables**:
- `terminal_init()`, `terminal_cleanup()`, `terminal_raw_mode()`
- Basic `cmdline_readline()` with no editing
- Platform detection and fallback mechanisms

### Phase 2: Core Line Editing (Week 2)
**Goal**: Basic editing capabilities

- Line buffer management with dynamic allocation
- Character insertion and deletion
- Cursor movement (left/right/home/end)
- Backspace and delete functionality
- Display refresh and cursor positioning

**Deliverables**:
- Complete line editor core
- Basic key bindings (Ctrl+A, Ctrl+E, Ctrl+B, Ctrl+F)
- Buffer resize and overflow protection

### Phase 3: History System (Week 3)
**Goal**: Command history navigation

- History data structure implementation
- History navigation (up/down arrows, Ctrl+P/Ctrl+N)
- History search (Ctrl+R)
- Persistent history save/load
- History size limits and cleanup

**Deliverables**:
- Complete history management
- `add_history()`, `clear_history()` functions
- History file I/O

### Phase 4: Advanced Editing (Week 4)
**Goal**: Full readline compatibility

- Word-based navigation (Alt+B, Alt+F)
- Kill ring (Ctrl+K, Ctrl+U, Ctrl+W, Ctrl+Y)
- Line transpose operations
- Kill/yank functionality
- Advanced cursor movements

**Deliverables**:
- Complete emacs-style key bindings
- Kill ring implementation
- Word boundary detection

### Phase 5: Polish and Integration (Week 5)
**Goal**: Production ready

- UTF-8 support and wide character handling
- Signal handling (Ctrl+C, window resize)
- Tab completion framework (basic)
- Configuration and customization options
- Error handling and edge cases
- Performance optimization

**Deliverables**:
- UTF-8 string operations
- Signal handling framework
- Basic completion infrastructure
- Comprehensive error handling

## File Structure

### Public API (`lib/cmdedit.h`)
```c
#ifndef CMDEDIT_H
#define CMDEDIT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Core API functions
char *readline(const char *prompt);
int add_history(const char *line);
int clear_history(void);

// Lambda-specific API (for compatibility)
int repl_init(void);
void repl_cleanup(void);
char *repl_readline(const char *prompt);
int repl_add_history(const char *line);

// Configuration
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

#ifdef __cplusplus
}
#endif

#endif // CMDEDIT_H
```

### Implementation (`lib/cmdedit.c`)
```c
// File structure:
// 1. Includes and platform detection
// 2. Data structures and state
// 3. Platform abstraction layer
// 4. UTF-8 utilities
// 5. Terminal I/O operations
// 6. Line editor core
// 7. Key binding system
// 8. History management
// 9. Public API implementation
// 10. Initialization and cleanup
```

## Integration Strategy

### Makefile Changes
Update build system to:
- Add `lib/cmdedit.c` to source files
- Remove readline/editline dependencies
- Add platform-specific compile flags
- Update link flags to remove readline libraries

### Code Changes
Minimal changes required:
1. Replace `#include <editline/readline.h>` with `#include "lib/cmdedit.h"`
2. Remove `#include <histedit.h>`
3. Remove EditLine-specific code blocks
4. The existing `repl_*` function calls remain unchanged

### Testing Strategy

#### Unit Tests
- Terminal I/O operations
- Line buffer manipulation
- History operations
- UTF-8 string functions
- Key binding resolution

#### Integration Tests  
- REPL functionality
- Cross-platform compatibility
- Performance benchmarks
- Memory leak detection
- Signal handling

#### Compatibility Tests
- Drop-in replacement verification
- Feature parity with current implementation
- Edge case handling

## Performance Considerations

### Memory Management
- Efficient buffer growth strategy (exponential resize)
- Pool allocation for history entries
- String interning for repeated prompts
- Lazy initialization of optional features

### I/O Optimization
- Minimize system calls
- Batch screen updates
- Efficient cursor movement calculations
- Terminal capability caching

### Unicode Performance
- Fast UTF-8 validation
- Character width caching
- Efficient string scanning
- Locale-aware optimizations

## Risk Mitigation

### Platform Compatibility Risks
- **Mitigation**: Extensive testing on all target platforms
- **Fallback**: Graceful degradation to basic line input
- **Testing**: Automated CI/CD testing on multiple platforms

### Performance Risks
- **Mitigation**: Benchmark against existing readline performance
- **Optimization**: Profile-guided optimization for hot paths
- **Monitoring**: Performance regression testing

### API Compatibility Risks
- **Mitigation**: Comprehensive compatibility test suite
- **Validation**: Test against existing Lambda REPL usage
- **Documentation**: Clear migration guide and API differences

## Success Criteria

1. **Functional**: 100% drop-in replacement for current readline usage
2. **Performance**: Within 10% of libreadline performance
3. **Compatibility**: Works on macOS, Linux, and Windows
4. **Reliability**: No memory leaks, proper error handling
5. **Size**: Minimal binary size increase
6. **Dependencies**: Zero additional dependencies

## Timeline

- **Week 1**: Basic terminal I/O and platform abstraction
- **Week 2**: Core line editing functionality
- **Week 3**: History system implementation
- **Week 4**: Advanced editing features
- **Week 5**: Polish, optimization, and integration

**Total Estimated Time**: 5 weeks for full implementation and testing

## Future Enhancements

### Optional Advanced Features
- Syntax highlighting support
- Advanced completion (file/command completion)
- Custom key binding configuration
- Vi mode support
- Multi-byte character set support beyond UTF-8
- Asynchronous I/O support

### Integration Opportunities
- Integration with Lambda's type system for intelligent completion
- Lambda script syntax-aware editing
- Integration with Lambda's error reporting system
- Custom Lambda REPL commands and shortcuts

---

This implementation plan provides a comprehensive roadmap for creating a production-ready, cross-platform command-line editing library that maintains full API compatibility with the existing readline/editline usage in Lambda while eliminating external dependencies.