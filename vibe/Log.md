# Lambda Script Logging System Design

## Requirements

### Phase 1: Core Lambda Files
Clean up console print statements (printf, fprintf, perror) in these core files:
1. `lambda/lambda-mem.cpp`
2. `lambda/lambda-data.cpp` 
3. `lambda/lambda-eval.cpp`
4. `lambda/print.cpp`
5. `lambda/build_ast.cpp`
6. `lambda/transpile.cpp`

### Phase 2: Remaining Files
Extend logging cleanup to all other files in the codebase.

## Logging Level Mapping Strategy

### Output Behavior Overview
**All log messages** (when enabled by log level) are **always written to the log file** for complete debugging history. **Additionally**, certain levels are also sent to console streams:

- **error/warn** → **stderr** (+ log file)
- **notice** → **stdout** (+ log file)  
- **info/debug** → **stdout** (+ log file, configurable via category output)

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
- **Output**: **Log file + stdout** (configurable)
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
- **Output**: **Log file + stdout** (configurable)
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
  - **Info/Debug messages**: Go to **stdout** (+ log file, configurable per category)
- **Final script output**: Continue printing directly to stdout (no change)

### 2. Log Level Configuration
- **Production**: Default to LOG_LEVEL_NOTICE (hide debug/info, show notices and above)
- **Development**: Default to LOG_LEVEL_DEBUG (show all messages)
- **Quiet mode**: Set to LOG_LEVEL_ERROR (only errors and warnings)

### 3. File Logging Setup
To enable file logging, configure category output:
```cpp
log_category_t *file_cat = log_get_category("main");
FILE *log_file = fopen("lambda.log", "a");
log_set_output(file_cat, log_file);
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

## Implementation Strategy

### 1. Header Inclusion
Add to each file:
```cpp
#include "../lib/log.h"  // Adjust path as needed
```

### 2. Initialization
Initialize logging system in main entry points:
```cpp
// In main() or init functions
log_init("level=debug;colors=true;timestamps=true");
```

### 3. Cleanup
Add cleanup in exit paths:
```cpp
// In cleanup functions
log_fini();
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
```

## Configuration

### Default Configuration
```
level=notice
colors=true  
timestamps=true
```

### Development Configuration
```
level=debug
colors=true
timestamps=true
```

### Production Configuration
```
level=error
colors=false
timestamps=true
```

## Benefits

1. **Consistent Output**: All logging goes through a unified system
2. **Configurable Verbosity**: Users can control what they see
3. **Proper Error Handling**: Errors go to stderr, output to stdout
4. **Better Debugging**: Structured debug information with levels
5. **Professional Appearance**: Timestamps, colors, and consistent formatting
6. **Performance**: Can disable expensive debug operations
7. **Maintainability**: Centralized logging configuration and formatting

## Testing Strategy

1. **Verify error messages**: Ensure all errors go to stderr
2. **Test log levels**: Confirm filtering works correctly
3. **Check output separation**: Final results still go to stdout
4. **Performance impact**: Measure logging overhead
5. **Configuration parsing**: Test various log configurations

## Future Enhancements

1. **File logging**: Add support for logging to files
2. **Rotation**: Implement log file rotation
3. **Categories**: Use different categories for different modules
4. **JSON output**: Support structured logging formats
5. **Remote logging**: Send logs to external systems
