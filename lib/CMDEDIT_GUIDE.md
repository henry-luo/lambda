# Command Line Editor (cmdedit) - User Guide

## Overview

The Lambda Script command line editor (`cmdedit`) is a full-featured, cross-platform line editing library that provides:

- **UTF-8 Support**: Proper handling of Unicode characters with display width calculation
- **Tab Completion**: Extensible completion system with callback support
- **Signal Handling**: Robust handling of SIGINT, SIGTERM, and SIGWINCH
- **History Management**: Command history with file persistence
- **Advanced Editing**: Kill ring, word operations, transpose, and more
- **Cross-Platform**: Works on macOS, Linux, and Windows

## Quick Start

### Basic Usage

```c
#include "cmdedit.h"

int main() {
    // Initialize the editor
    if (repl_init() != 0) {
        fprintf(stderr, "Failed to initialize editor\n");
        return 1;
    }
    
    char *line;
    while ((line = repl_readline("λ> ")) != NULL) {
        printf("You entered: %s\n", line);
        
        // Add to history (automatically handles duplicates)
        if (strlen(line) > 0) {
            repl_add_history(line);
        }
        
        free(line);
    }
    
    // Cleanup
    repl_cleanup();
    return 0;
}
```

### Readline Compatibility

For applications using GNU Readline, cmdedit provides a compatible API:

```c
#include "cmdedit.h"

int main() {
    char *line = readline("Enter command: ");
    if (line) {
        if (*line) {
            add_history(line);
        }
        printf("Command: %s\n", line);
        free(line);
    }
    return 0;
}
```

## Features

### UTF-8 Unicode Support

The editor properly handles UTF-8 encoded text with correct:
- Display width calculation for East Asian wide characters
- Character-based cursor movement (not byte-based)
- Word boundary detection for Unicode text

```c
// Handles Unicode input correctly
char *line = repl_readline("Enter text (Unicode OK): ");
// Input like "こんにちは world 🌍" works perfectly
```

### Tab Completion

Set up custom completion functions:

```c
// Example completion function
char **my_completion_function(const char *text, int start, int end) {
    // Static list of commands
    static const char *commands[] = {
        "help", "exit", "load", "save", "run", NULL
    };
    
    char **matches = malloc(sizeof(char*) * 10);
    int count = 0;
    
    for (int i = 0; commands[i]; i++) {
        if (strncmp(commands[i], text, strlen(text)) == 0) {
            matches[count] = strdup(commands[i]);
            count++;
        }
    }
    
    if (count == 0) {
        free(matches);
        return NULL;
    }
    
    matches[count] = NULL;  // Null-terminate
    return matches;
}

int main() {
    repl_init();
    
    // Set completion function
    rl_attempted_completion_function = &my_completion_function;
    
    char *line = repl_readline("cmd> ");
    // Now pressing TAB will complete commands
    
    repl_cleanup();
    return 0;
}
```

### History Management

```c
// Load history from file
read_history(".my_app_history");

// Normal editing session
char *line = repl_readline(">> ");
if (line && *line) {
    repl_add_history(line);
}

// Save history on exit
write_history(".my_app_history");
```

### Signal Handling

The editor automatically handles:
- **Ctrl+C (SIGINT)**: Returns NULL to allow graceful exit
- **Ctrl+D (SIGTERM)**: Returns NULL for EOF handling  
- **Window Resize (SIGWINCH)**: Automatically refreshes display

```c
char *line = repl_readline("prompt> ");
if (line == NULL) {
    // User pressed Ctrl+C or Ctrl+D, or signal received
    printf("Interrupted or EOF\n");
    return 0;
}
```

## Key Bindings

### Movement
- **Ctrl+A / Home**: Move to beginning of line
- **Ctrl+E / End**: Move to end of line  
- **Ctrl+B / Left Arrow**: Move cursor left
- **Ctrl+F / Right Arrow**: Move cursor right
- **Alt+B**: Move backward one word
- **Alt+F**: Move forward one word

### Editing
- **Ctrl+H / Backspace**: Delete character before cursor
- **Delete**: Delete character under cursor
- **Ctrl+K**: Kill (cut) from cursor to end of line
- **Ctrl+U**: Kill from beginning of line to cursor
- **Ctrl+Y**: Yank (paste) most recent kill
- **Ctrl+T**: Transpose characters
- **Tab**: Trigger completion (if function set)

### History
- **Ctrl+P / Up Arrow**: Previous history entry
- **Ctrl+N / Down Arrow**: Next history entry  
- **Ctrl+R**: Reverse search history (future enhancement)

### Control
- **Ctrl+L**: Clear screen and redraw
- **Ctrl+C**: Send SIGINT (usually exits)
- **Ctrl+D**: Send EOF (exits on empty line)
- **Enter**: Accept current line

## Advanced Features

### Custom Key Bindings

```c
// Bind a key to a custom function
int my_custom_function(struct line_editor *ed, int key, int count) {
    // Custom behavior
    return 0;  // Continue editing
}

// This would require extending the API
// rl_bind_key('x', my_custom_function);
```

### Error Handling

```c
if (repl_init() != 0) {
    // Initialization failed - probably not a TTY
    // Fall back to basic line input
    char buffer[1024];
    fgets(buffer, sizeof(buffer), stdin);
}
```

## Configuration

### Environment Variables

The editor respects standard environment variables:
- `TERM`: Terminal type detection
- `COLUMNS`: Terminal width (if not detectable)

### Compilation Flags

- `-DCMDEDIT_TESTING`: Enable testing interface
- `-DUTF8PROC_STATIC`: Use static utf8proc library

## Platform Notes

### macOS/Linux
- Uses termios for terminal control
- Supports all Unicode features
- Full signal handling

### Windows  
- Uses Windows Console API
- UTF-8 support via utf8proc
- Signal handling adapted for Windows

## Performance

The editor is optimized for:
- **Large Inputs**: Exponential buffer growth reduces reallocations
- **UTF-8 Text**: Efficient character width caching  
- **Display Updates**: Minimal screen refreshes
- **Memory Usage**: Automatic cleanup and leak prevention

## Integration Examples

### With Command Processors

```c
void run_command_loop() {
    repl_init();
    
    char *line;
    while ((line = repl_readline("$ ")) != NULL) {
        if (strcmp(line, "exit") == 0) {
            free(line);
            break;
        }
        
        // Process command
        execute_command(line);
        
        // Add to history
        if (*line) repl_add_history(line);
        free(line);
    }
    
    repl_cleanup();
}
```

### With Scripting Languages

```c
// Lambda Script REPL integration
void lambda_repl() {
    repl_init();
    
    // Set up Lambda-specific completions
    rl_attempted_completion_function = &lambda_completion_function;
    
    printf("Lambda Script v1.0\n");
    
    char *line;
    while ((line = repl_readline("λ> ")) != NULL) {
        if (strlen(line) > 0) {
            // Parse and execute Lambda expression
            Item result = lambda_eval(line);
            print_item(result);
            
            repl_add_history(line);
        }
        free(line);
    }
    
    repl_cleanup();
}
```

## Troubleshooting

### Common Issues

1. **No line editing**: Application not running in a TTY
   - Solution: Check `isatty(STDIN_FILENO)` before using
   
2. **UTF-8 not working**: Missing utf8proc dependency
   - Solution: Ensure utf8proc is linked (`-lutf8proc`)

3. **Completion not triggering**: Function not set or returning NULL
   - Solution: Verify `rl_attempted_completion_function` is assigned

4. **History not persisting**: File permissions or path issues
   - Solution: Check read/write access to history file

### Debug Mode

Compile with `-DCMDEDIT_TESTING` for additional debugging:

```c
#ifdef CMDEDIT_TESTING
extern struct line_editor g_editor;  // Access internal state
extern struct history g_history;     // Access history structure
#endif
```

## Building

### Dependencies
- utf8proc (Unicode support)
- Platform-specific terminal libraries

### Compile flags
```bash
gcc -std=gnu99 -DUTF8PROC_STATIC myapp.c lib/cmdedit.c lib/cmdedit_utf8.c -lutf8proc
```

## API Reference

See `cmdedit.h` for complete function documentation.

Key functions:
- `repl_init()` / `repl_cleanup()`: Initialization
- `repl_readline(prompt)`: Main input function  
- `repl_add_history(line)`: Add to history
- `read_history(file)` / `write_history(file)`: File persistence

## License

Part of the Lambda Script project. See main project LICENSE for details.