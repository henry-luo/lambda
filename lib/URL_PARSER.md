# URL Parser Implementation

This directory contains a modern C URL parser implementation designed to replace the lexbor URL parser in the Lambda Script project.

## Features

### Phase 1: Basic URL Parsing
- ✅ Complete URL scheme detection and parsing
- ✅ Memory-safe string handling with custom String type
- ✅ Comprehensive error handling and validation
- ✅ WHATWG URL Standard compliance foundation

### Phase 2: Component Parsing  
- ✅ Full component extraction (protocol, username, password, host, port, pathname, search, hash)
- ✅ Advanced host parsing (hostname vs host with port)
- ✅ Query parameter and fragment handling
- ✅ Port number parsing and validation

### Phase 3: Relative URL Resolution
- ✅ Relative URL resolution against base URLs
- ✅ Path normalization (removes `.` and `..` segments)
- ✅ Absolute and relative path handling
- ✅ Query and fragment preservation in relative resolution

## API Overview

### Core Functions

```c
// Create and destroy URLs
Url* url_create(void);
void url_destroy(Url* url);

// Parse URLs
Url* url_parse(const char* url_string);
Url* url_parse_with_base(const char* url_string, const Url* base_url);

// Component access
const char* url_get_href(const Url* url);
const char* url_get_protocol(const Url* url);
const char* url_get_hostname(const Url* url);
// ... other getters

// Component modification
UrlError url_set_href(Url* url, const char* href);
UrlError url_set_protocol(Url* url, const char* protocol);
// ... other setters

// Utility functions
bool url_equals(const Url* a, const Url* b);
Url* url_clone(const Url* url);
```

### URL Structure

```c
typedef struct {
    UrlScheme scheme;
    int port_number;
    
    // String components
    String* href;      // Complete URL
    String* protocol;  // e.g., "https:"
    String* username;  // User authentication
    String* password;  // Password authentication  
    String* host;      // Host with port (if non-standard)
    String* hostname;  // Host without port
    String* port;      // Port as string
    String* pathname;  // Path component
    String* search;    // Query string with '?'
    String* hash;      // Fragment with '#'
} Url;
```

## Usage Examples

### Basic Parsing
```c
Url* url = url_parse("https://user:pass@example.com:8080/path?query=value#fragment");
if (url) {
    printf("Protocol: %s\n", url->protocol->chars);  // "https:"
    printf("Hostname: %s\n", url->hostname->chars);  // "example.com"
    printf("Port: %s\n", url->port->chars);          // "8080"
    printf("Path: %s\n", url->pathname->chars);      // "/path"
    url_destroy(url);
}
```

### Relative URL Resolution
```c
Url* base = url_parse("https://example.com/dir1/dir2/file.html");
Url* resolved = url_parse_with_base("../other.html", base);
// Result: https://example.com/dir1/other.html

url_destroy(base);
url_destroy(resolved);
```

### Path Normalization
```c
Url* base = url_parse("https://example.com/a/b/c/d.html");
Url* resolved = url_parse_with_base("./sub/file.html", base);
// Result: https://example.com/a/b/c/sub/file.html (. segment removed)

url_destroy(base);
url_destroy(resolved);
```

## Testing

### Quick Test
```bash
./run_url_tests.sh
```

### Comprehensive Test Suite
```bash
# Run the complete test suite with modular compilation
./run_url_tests.sh

# Manual compilation (if needed)
clang -std=c99 -I. -Ilib/mem-pool/include \
  test_url_complete.c \
  lib/url.c \
  lib/url_parser.c \
  lib/url_compat.c \
  lib/string.c \
  lib/mem-pool/src/buffer.c \
  lib/mem-pool/src/utils.c \
  lib/mem-pool/src/variable.c \
  -o test_url_complete && ./test_url_complete

# Phase 3 Criterion tests (requires criterion framework)
clang -std=c99 -I. -Ilib/mem-pool/include $(pkg-config --cflags criterion) \
  test/test_url_phase3.c lib/url.c lib/url_parser.c lib/url_compat.c lib/string.c \
  lib/mem-pool/src/buffer.c lib/mem-pool/src/utils.c lib/mem-pool/src/variable.c \
  $(pkg-config --libs criterion) -o test_url_phase3 && ./test_url_phase3
```

## Implementation Details

### Memory Management
- Uses custom String type with reference counting
- Pool-based memory allocation for performance
- Automatic cleanup and leak prevention
- Safe string operations with bounds checking

### Standards Compliance
- Based on WHATWG URL Standard
- Handles edge cases and malformed URLs gracefully
- Comprehensive validation and normalization
- Cross-platform compatibility (macOS, Linux, Windows)

### Performance Features
- Efficient string copying and manipulation
- Minimal memory allocations
- Fast path normalization algorithms
- Optimized for common URL patterns

## Architecture

The URL parser has been modularized into separate files for better maintainability and separation of concerns:

```
lib/url.h          - Public API and type definitions
lib/url.c          - Core URL structure, creation, destruction, getters, setters
lib/url_parser.c   - Parsing logic, normalization, and path handling  
lib/url_compat.c   - Compatibility layer for legacy lexbor API functions
lib/string.h       - Custom String type for memory safety
lib/string.c       - String implementation
test/test_url_phase3.c - Comprehensive test suite
test_url_complete.c    - Simple validation tests
run_url_tests.sh       - Test runner script
```

### Modular Design Benefits

1. **Separation of Concerns**: Core data structures, parsing logic, and compatibility layers are clearly separated
2. **Maintainability**: Each module has a focused responsibility making code easier to understand and modify
3. **Testability**: Individual components can be tested in isolation
4. **Legacy Support**: Compatibility layer ensures smooth migration from lexbor without breaking existing code

## Integration

This URL parser is designed as a drop-in replacement for lexbor's URL functionality in the Lambda Script project. It provides:

1. **Better Memory Safety**: Custom String type prevents buffer overflows
2. **Standards Compliance**: Full WHATWG URL Standard support  
3. **Performance**: Optimized for common parsing patterns
4. **Maintainability**: Clean, well-documented C code
5. **Testing**: Comprehensive test coverage with Criterion framework

## Status

✅ **Ready for Production**: All phases implemented, tested, and modularized
- ✅ Phase 1: Basic parsing and scheme detection
- ✅ Phase 2: Complete component parsing  
- ✅ Phase 3: Relative URL resolution and normalization
- ✅ **Modular Architecture**: Split into focused, maintainable components
- ✅ **Legacy Compatibility**: Seamless migration from lexbor with compatibility layer
- ✅ **Build Integration**: Successfully integrated into main project build system

### Recent Updates (August 2025)

**Codebase Modularization Completed**
- Split monolithic `url.c` into three focused modules:
  - `url.c`: Core URL data structures and basic operations
  - `url_parser.c`: All parsing logic and path normalization
  - `url_compat.c`: Legacy lexbor API compatibility functions
- Updated build configuration to include all new modules
- Fixed compilation issues and resolved function redefinitions
- Updated test runner to compile with new modular structure
- ✅ All tests pass with the new modular architecture
- ✅ Main project builds successfully with 0 errors

The parser successfully handles the infinite loop bug that was fixed in the path normalization logic and passes all test cases including complex relative path resolution scenarios. The modular design makes the codebase more maintainable while preserving full backward compatibility.
