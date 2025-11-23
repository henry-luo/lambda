# Lambda Script Logging System

## Enhanced Logger Features ✅

The logging system has been enhanced with improved formatting, zlog-compatible configuration, and indentation support:

### Improved Log Format
**Before:**
```
2025-08-27 12:24:40[DEBUG] [default] transpile box item: 10
```

**After:**
```
12:24:40 [DEBUG]  transpile box item: 10
```

**Improvements:**
- Time-only timestamps (`HH:MM:SS`) instead of full date
- Space between time and log level
- Hidden `[default]` category for cleaner output
- Custom categories still shown: `[parser]`, `[memory]`, etc.

### Format Patterns
- `%T` - Time (HH:MM:SS)
- `%F` - Full timestamp with date  
- `%L` - Log level ([DEBUG], [INFO], etc.)
- `%C` - Category name (hidden for "default" by default)
- `%I` - **Indentation spaces** (new in Phase 3)
- `%m` - Message content
- `%n` - Newline

### Indentation Support (Phase 3 ✅)
**Thread-local indentation** for hierarchical debugging:
```cpp
log_info("Function start");
log_enter();                    // Increase indentation by 2 spaces
log_debug("Processing...");     // Indented output
log_enter();                    // Nested level
log_debug("Deep processing");   // More indented
log_leave();                    // Decrease indentation
log_leave();                    // Back to original level
log_info("Function end");
```

**Output:**
```
12:24:40 [INFO]  Function start
12:24:40 [DEBUG]    Processing...
12:24:40 [DEBUG]      Deep processing
12:24:40 [INFO]  Function end
```

**Indentation API:**
- `log_enter()` - Increase indentation by 2 spaces
- `log_leave()` - Decrease indentation by 2 spaces  
- `log_set_indent(n)` - Set indentation to n spaces
- `log_get_indent()` - Get current indentation level
- `log_reset_indent()` - Reset to no indentation
- Max indentation: 40 spaces (20 levels)

### Smart Color Management (Phase 3 ✅)
**File extension-based coloring** with VS Code integration:
- **`.log` files**: ANSI color codes for syntax highlighting in VS Code
- **`.txt` files**: Plain text without color codes  
- **Console output**: Colors enabled when output is a terminal (TTY)
- **VS Code setup**: ANSI Colors extension auto-configured for `.log` files

**Configuration:**
```ini
[rules]
default.DEBUG "lambda.log"; simple    # Colored output for VS Code viewing
default.DEBUG "output.txt"; simple    # Plain text output
```

Log files are automatically cleared on program startup for fresh debugging sessions.

### Configuration File Support
**zlog-compatible** `log.conf` format:
```ini
[formats]
simple = "%T %L %C %I%m%n"         # Default: time, level, category, indentation, message
compact = "%T %L %I%m%n"           # Compact: time, level, indentation, message only
detailed = "%F %L %C %I%m%n"       # Detailed: full timestamp with date and indentation

[rules]
default.DEBUG "log.txt"; simple     # Default category to file with simple format
parser.INFO ""; compact             # Parser category to console with compact format
```

## Requirements

### Phase 1: Core Lambda Files ✅
Clean up console print statements (printf, fprintf, perror) in these core files:
1. `lambda/lambda-mem.cpp`
2. `lambda/lambda-data.cpp` 
3. `lambda/lambda-eval.cpp`
4. `lambda/print.cpp`
5. `lambda/build_ast.cpp`
6. `lambda/transpile.cpp`

### Phase 2: Remaining Files
Extend logging cleanup to all other files in the codebase.

### Phase 3: Indentation Support ✅
Add thread-local indentation for hierarchical debugging and function tracing.

## Logging Level Mapping Strategy

### Output Behavior Overview
**All log messages** (when enabled by log level) are **always written to the log file** for complete debugging history. **Additionally**, certain levels are also sent to console streams:

- **error/warn** → **stderr** (+ log file)
- **notice** → **stdout** (+ log file)  
- **info/debug** → **log file only** (no console output for cleaner interface)

### 1. `log_error()` - Error Messages
- **Output**: **Log file + stderr**
- **Usage**: Critical errors that prevent normal operation
- **Examples**:
  - Memory allocation failures
  - Invalid type operations
  - Parse errors
  - File I/O errors
  - NULL pointer dereferences

### 2. `log_warn()` - Warning Messages  
- **Output**: **Log file + stderr**
- **Usage**: Recoverable issues or potential problems
- **Examples**:
  - Type coercion warnings
  - Deprecated feature usage
  - Configuration fallbacks
  - Performance concerns

### 3. `log_info()` - Informational Messages
- **Output**: **Log file only**
- **Usage**: Important operational information
- **Examples**:
  - Successful compilation phases
  - Major operation completions
  - Configuration loading
  - Module imports

### 4. `log_notice()` - Major Status/Progress Messages
- **Output**: **Log file + stdout**
- **Usage**: High-level progress and status updates
- **Examples**:
  - "Transpiling Lambda script..."
  - "Building AST..."
  - "Loading input data..."
  - "Generating output..."

### 5. `log_debug()` - Debug/Trace Messages
- **Output**: **Log file only**
- **Usage**: Detailed execution flow and debugging information
- **Examples**:
  - Function entry/exit traces
  - Variable value dumps
  - Internal state information
  - Memory pool operations
  - Type inference details

## Design Principles

### 1. Dual Output System
- **Complete Log File**: All enabled log messages are written to the log file for complete debugging history
- **Console Separation**: 
  - **Error/Warning messages**: Go to **stderr** (+ log file) via `log_error()` and `log_warn()`
  - **Notice messages**: Go to **stdout** (+ log file) for major status updates
  - **Info/Debug messages**: Go to **log file only** (no console output for cleaner interface)
- **Final script output**: Continue printing directly to stdout (no change)

### 2. Log Level Configuration
- **Production**: Default to LOG_LEVEL_NOTICE (hide debug/info, show notices and above)
- **Development**: Default to LOG_LEVEL_DEBUG (show all messages)
- **Quiet mode**: Set to LOG_LEVEL_ERROR (only errors and warnings)

### 3. File Logging Setup
To enable file logging with enhanced configuration:

**Basic setup:**
```cpp
log_parse_config_file("log.conf");  // Load enhanced configuration
log_init("");                       // Initialize with config
```

**Manual setup:**
```cpp
log_category_t *file_cat = log_get_category("main");
FILE *log_file = fopen("lambda.log", "a");
log_set_output(file_cat, log_file);
```

**Custom formats:**
```cpp
log_add_format("custom", "%T [%L] %m%n");
log_set_default_format("%T %L %m%n");
```

### 4. Performance Considerations
- Use log level checks for expensive debug operations:
  ```cpp
  if (log_level_enabled(log_default_category, LOG_LEVEL_DEBUG)) {
      log_debug("expensive debug info: %s", expensive_function());
  }
  ```

### 4. Error Handling Enhancement
Current error patterns like:
```cpp
fprintf(stderr, "Error: %s\n", error_msg);
perror("operation failed");
```

Should become:
```cpp
log_error("Error: %s", error_msg);
log_error("operation failed: %s", strerror(errno));
```

**Note**: With the dual-output system, these will automatically go to both the log file and stderr.

### 5. Debug Tracing Enhancement
Current debug patterns like:
```cpp
printf("build array expr\n");
printf("DEBUG: Final array nested_type_id: %d\n", type_id);
```

Should become:
```cpp
log_debug("build array expr");
log_debug("Final array nested_type_id: %d", type_id);
```

**With indentation for function tracing:**
```cpp
void build_array_expr() {
    log_debug("build array expr");
    log_enter();
    
    // processing...
    log_debug("Final array nested_type_id: %d", type_id);
    
    log_leave();
}
```

## Implementation Strategy

### 1. Header Inclusion
Add to each file:
```cpp
#include "../lib/log.h"  // Adjust path as needed
```

### 2. Initialization
Initialize logging system with enhanced configuration:

**With config file:**
```cpp
log_parse_config_file("log.conf");  // Enhanced zlog-compatible format
log_init("");
```

**Legacy simple config:**
```cpp
log_init("level=debug;colors=true;timestamps=true");
```

**Custom categories:**
```cpp
log_category_t *parser = log_get_category("parser");
clog_debug(parser, "Parsing expression");  // Output: 12:24:40 [DEBUG] [parser] Parsing expression
```

### 3. Cleanup
Add cleanup in exit paths:
```cpp
// In cleanup functions
log_finish();
```

### 4. Migration Pattern
For each printf/fprintf/perror:

1. **Identify the purpose**: Error, warning, info, notice, or debug?
2. **Choose appropriate log level**: Based on the mapping above
3. **Update the call**: Replace with corresponding log_*() function
4. **Remove manual formatting**: Let log library handle timestamps, colors, etc.

### 5. Special Cases

#### Memory Allocation Errors
```cpp
// Before
fprintf(stderr, "Failed to allocate memory for %s\n", context);

// After  
log_error("Failed to allocate memory for %s", context);
```

#### Type System Debug
```cpp
// Before
printf("transpile box item: %d\n", item->type->type_id);

// After
log_debug("transpile box item: %d", item->type->type_id);
```

#### Progress Updates
```cpp
// Before
printf("building identifier\n");

// After
log_debug("building identifier");
```

#### Major Operations
```cpp
// Before
printf("Transpiling Lambda script to C...\n");

// After
log_notice("Transpiling Lambda script to C...");
// Output: 12:24:40 [NOTICE] Transpiling Lambda script to C...
```

### 6. Enhanced Format Examples

#### Clean Debug Output
```cpp
// Before
printf("transpile box item: %d\n", item->type->type_id);

// After
log_debug("transpile box item: %d", item->type->type_id);
// Output: 12:24:40 [DEBUG]  transpile box item: 10  (note: no [default] category)
```

#### Custom Category Usage
```cpp
log_category_t *memory = log_get_category("memory");
clog_debug(memory, "Memory pool allocated: %dKB", size);
// Output: 12:24:40 [DEBUG] [memory] Memory pool allocated: 1024KB
```

#### Function Tracing with Indentation
```cpp
void transpile_function() {
    log_info("transpile_function() starting");
    log_enter();
    
    log_debug("parsing parameters");
    log_debug("building function body");
    
    log_leave();
    log_info("transpile_function() completed");
}
// Output:
// 12:24:40 [INFO]  transpile_function() starting
// 12:24:40 [DEBUG]    parsing parameters  
// 12:24:40 [DEBUG]    building function body
// 12:24:40 [INFO]  transpile_function() completed
```

## Configuration

### Enhanced Configuration (log.conf)
**Recommended approach** using zlog-compatible format with indentation:
```ini
[formats]
simple = "%T %L %C %I%m%n"       # Time, level, category (hidden for default), indentation, message
compact = "%T %L %I%m%n"         # Time, level, indentation, message only
detailed = "%F %L %C %I%m%n"     # Full timestamp with date and indentation

[rules]
default.DEBUG "lambda.log"; simple    # All levels to file
parser.INFO ""; compact               # Parser category to console
memory.WARN ""; detailed              # Memory warnings with full timestamp
```

### Legacy Configuration
```ini
# Simple key=value format (backward compatible)
level=notice
colors=true  
timestamps=true
```

### Runtime Configuration
```cpp
// Load configuration file
log_parse_config_file("log.conf");

// Or use simple string config
log_init("level=debug;colors=true");

// Custom format
log_set_default_format("%T [%L] %m%n");
```

## Benefits

1. **Enhanced Output Format**: Cleaner timestamps and hidden default categories
2. **Flexible Configuration**: Multiple formats and zlog-compatible config files
3. **Consistent Output**: All logging goes through a unified system
4. **Configurable Verbosity**: Users can control what they see
5. **Proper Error Handling**: Errors go to stderr, output to stdout
6. **Better Debugging**: Structured debug information with levels and categories
7. **Professional Appearance**: Timestamps, colors, and consistent formatting
8. **Performance**: Can disable expensive debug operations
9. **Maintainability**: Centralized logging configuration and formatting
10. **Industry Standards**: zlog-compatible configuration format
11. **Clean Console Output**: Debug/info messages go to log file only, keeping console uncluttered

## Implementation Status

### ✅ Completed Features
- Enhanced format patterns (`%T %L %C %I%m%n`)
- zlog-compatible configuration files
- Cleaner output (time-only timestamps, hidden default category)
- Multiple output destinations (files, stdout, stderr)
- Per-category configuration
- Pattern-based formatting system
- **Thread-local indentation support** (Phase 3)

### 📁 Modified Files
- `lib/log.h` - Enhanced structures, thread-local indentation, and function declarations
- `lib/log.c` - Complete format system, config parsing, and indentation implementation
- `log.conf` - Updated with enhanced configuration format and indentation support

## Testing Strategy

1. **Verify error messages**: Ensure all errors go to stderr
2. **Test log levels**: Confirm filtering works correctly  
3. **Check output separation**: Final results still go to stdout
4. **Performance impact**: Measure logging overhead
5. **Configuration parsing**: Test various log configurations
6. **Format validation**: Test different format patterns
7. **Category behavior**: Verify default category hiding and custom category display

## Future Enhancements

1. **Log rotation**: Implement log file rotation
2. **JSON output**: Support structured logging formats  
3. **Remote logging**: Send logs to external systems
4. **Compression**: Compress rotated log files
5. **Real-time filtering**: Dynamic log level changes
