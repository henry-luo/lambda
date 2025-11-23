# Lambda Script Log Library

A lightweight, zlog-compatible logging library for the Lambda Script project with convenient `log_xxx` and category-specific `clog_xxx` APIs.

## Features

- **Dual API design**: Convenient `log_xxx()` functions for default category and `clog_xxx(category, ...)` for specific categories
- **Multiple log levels**: FATAL, ERROR, WARN, NOTICE, INFO, DEBUG
- **Timestamp support** with configurable formatting
- **Color output** for terminal displays (auto-detected TTY)
- **Multiple categories** for organized logging
- **Configuration support** via strings or files
- **Thread-safe** design with minimal dependencies

## API Overview

### Convenient Default Category Functions

```c
// Simple logging to default category
int log_debug(const char *format, ...);
int log_info(const char *format, ...);
int log_notice(const char *format, ...);
int log_warn(const char *format, ...);
int log_error(const char *format, ...);
int log_fatal(const char *format, ...);

// Variadic versions
int log_vdebug(const char *format, va_list args);
int log_vinfo(const char *format, va_list args);
// ... etc
```

### Category-Specific Functions

```c
// Logging with explicit category
int clog_debug(log_category_t *category, const char *format, ...);
int clog_info(log_category_t *category, const char *format, ...);
int clog_notice(log_category_t *category, const char *format, ...);
int clog_warn(log_category_t *category, const char *format, ...);
int clog_error(log_category_t *category, const char *format, ...);
int clog_fatal(log_category_t *category, const char *format, ...);

// Variadic versions
int clog_vdebug(log_category_t *category, const char *format, va_list args);
// ... etc
```

### Core Functions

```c
// Initialization
int log_init(const char *config);
void log_fini(void);

// Category management
log_category_t* log_get_category(const char *name);
void log_set_level(log_category_t *category, int level);
void log_set_output(log_category_t *category, FILE *output);

// Default category (zlog dzlog_init compatible)
int log_default_init(const char *config, const char *category_name);
void log_default_fini(void);
```

### Configuration

Configuration can be passed as a string with key=value pairs:

```c
log_init("level=debug;timestamps=true;colors=true");
```

Available options:
- `level`: debug, info, notice, warn, error, fatal
- `timestamps`: true/false or 1/0
- `colors`: true/false or 1/0

## Usage Examples

### Basic Usage (Convenient API)

```c
#include "lib/log.h"

int main() {
    // Initialize with debug level
    log_init("level=debug;timestamps=true");
    
    // Simple logging (uses default category)
    log_info("Application started");
    log_debug("Debug info: %d", 42);
    log_warn("Warning: %s", "something happened");
    
    // Cleanup
    log_fini();
    return 0;
}
```

### Category-Specific Logging

```c
log_category_t *db_log = log_get_category("database");
log_category_t *net_log = log_get_category("network");

// Use category-specific API
clog_info(db_log, "Connected to database");
clog_error(net_log, "Network timeout");

// Or mix with convenient API
log_info("General application message");
clog_debug(db_log, "SQL query executed in %d ms", 45);
```

### Migration from zlog/dzlog

Replace:
- `#include <zlog.h>` → `#include "lib/log.h"`
- `dzlog_init()` → `log_default_init()`
- `dzlog_debug()` → `log_debug()` (now uses default category automatically)
- `zlog_debug(category, ...)` → `clog_debug(category, ...)`

## API Design Benefits

### Convenient API (`log_xxx`)
- **Simpler calls**: `log_info("message")` vs `clog_info(log_default_category, "message")`
- **Less typing**: Perfect for most logging scenarios
- **Backward compatibility**: Easy migration from dzlog

### Category API (`clog_xxx`)
- **Explicit control**: Clear which category is being used
- **Multiple categories**: Different components can have separate log categories
- **Flexible configuration**: Each category can have different levels and outputs

## Build Integration

Add to your build configuration:

```json
"source_files": [
    "lib/log.c",
    // ... other files
]
```

## License

Part of the Lambda Script project. Same license as the main project.
