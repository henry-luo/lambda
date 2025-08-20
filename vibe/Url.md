# URL Parser Implementation Plan

## Executive Summary

This document outlines the incremental development plan for implementing a custom URL parser in C to replace the current lexbor URL parser dependency. The implementation will follow WHATWG URL Standard specifications while incorporating best practices from lexbor's design patterns.

## Analysis of Existing lexbor Integration

### Current Usage Patterns
From studying the codebase, lexbor URL parser is used in:
- `lib/url.c` - Core URL parsing and manipulation
- `lambda/input/input.cpp` - File URL processing for document loading
- `lambda/input/input-css.cpp` - CSS URL parsing
- Test files for validation

### Key API Points
```c
// Core types (from lexbor)
typedef struct lxb_url lxb_url_t;
typedef struct lxb_url_parser lxb_url_parser_t;

// Main functions we need to replace
lxb_url_parser_t* lxb_url_parser_create(void);
lxb_status_t lxb_url_parser_init(lxb_url_parser_t *parser, void *allocator);
lxb_url_t* lxb_url_parse(lxb_url_parser_t *parser, lxb_url_t *base, const lxb_char_t *input, size_t length);
void lxb_url_parser_destroy(lxb_url_parser_t *parser, bool self_destroy);
void lxb_url_destroy(lxb_url_t *url);

// Utility functions
char* url_to_local_path(lxb_url_t *url);
lxb_status_t lxb_url_serialize(lxb_url_t *url, callback_fn, void *ctx, bool exclude_fragment);
```

## Design Principles

### 1. Simplified Memory Management
- **malloc-based allocation**: Use standard malloc for both UrlParser and Url objects
- **No external dependencies**: Remove VariableMemPool dependency entirely
- **Reference Counting**: Maintain reference counting for URL objects
- **Cleanup**: Proper resource deallocation with simple malloc/free patterns

### 2. Single Header Design
- **Consolidated API**: All types and functions in `./lib/url.h`
- **No compatibility layer**: Refactor existing code to use new API directly
- **Clean interface**: Single include for all URL functionality

### 3. Error Handling
- **Status Codes**: Use project's existing status code system
- **Validation**: Comprehensive input validation with helpful error messages
- **Graceful Degradation**: Handle malformed URLs without crashes

### 4. Performance Characteristics
- **Zero-Copy Parsing**: Minimize string copying where possible
- **Lazy Evaluation**: Parse components on-demand
- **Small Memory Footprint**: Efficient internal representation

### 4. API Compatibility
- **Direct replacement**: Refactor existing lexbor usage to new API
- **Migration strategy**: Systematic replacement of lexbor calls
- **Extended Functionality**: Add convenience functions specific to project needs
- **Thread Safety**: Consider concurrent usage patterns

## URL Structure Representation

### Core URL Components (WHATWG Standard)
```c
typedef enum {
    URL_SCHEME_HTTP,
    URL_SCHEME_HTTPS,
    URL_SCHEME_FTP,
    URL_SCHEME_FILE,
    URL_SCHEME_WS,
    URL_SCHEME_WSS,
    URL_SCHEME_DATA,
    URL_SCHEME_MAILTO,
    URL_SCHEME_JAVASCRIPT,
    URL_SCHEME_ABOUT,
    URL_SCHEME_BLOB,
    URL_SCHEME_CUSTOM,       // For non-standard schemes
    URL_SCHEME_UNKNOWN
} url_scheme_t;

typedef struct {
    // Scheme components
    url_scheme_t scheme_type; // Enum for fast scheme identification
    String* scheme;           // String representation (always lowercase)
    
    // Authority components  
    String* username;         // Percent-encoded username (can be empty)
    String* password;         // Percent-encoded password (can be empty)
    String* host;            // Domain, IPv4, IPv6, or opaque host
    uint16_t port;           // Port number (0 if default for scheme)
    bool has_port;           // Whether port was explicitly specified
    
    // Path component
    String** path_segments;   // Array of path segments (for hierarchical URLs)
    size_t path_count;       // Number of path segments
    String* opaque_path;     // For non-hierarchical URLs (e.g., mailto:)
    bool is_opaque;          // Whether path is opaque
    
    // Query and fragment
    String* query;           // Query string without '?' prefix
    String* fragment;        // Fragment without '#' prefix
    
    // Metadata
    bool is_special;         // Whether scheme is "special" (has default port)
    bool has_credentials;    // Whether username or password present
    
    // Memory management
    uint32_t ref_count;      // Reference counter
} Url;

typedef struct {
    // Parser state
    const char* input;       // Current input string
    size_t input_length;     // Total input length
    size_t position;         // Current parsing position
    
    // Error reporting
    int error_code;          // Last error encountered
    size_t error_position;   // Position of last error
    
    // Configuration
    bool strict_mode;        // Whether to be strict about validation
    bool normalize_case;     // Whether to normalize case
} UrlParser;
```

### Special Schemes and Default Ports
```c
typedef struct {
    url_scheme_t scheme_type;
    const char* scheme_str;
    uint16_t default_port;
    bool requires_host;
} scheme_info_t;

static const scheme_info_t SPECIAL_SCHEMES[] = {
    {URL_SCHEME_HTTP,  "http",  80,   true},
    {URL_SCHEME_HTTPS, "https", 443,  true},
    {URL_SCHEME_FTP,   "ftp",   21,   true},
    {URL_SCHEME_FILE,  "file",  0,    false},  // Special case: can have empty host
    {URL_SCHEME_WS,    "ws",    80,   true},
    {URL_SCHEME_WSS,   "wss",   443,  true},
    {URL_SCHEME_UNKNOWN, NULL,  0,    false}   // Sentinel
};

// Helper function to get scheme info from enum
static inline const scheme_info_t* get_scheme_info(url_scheme_t scheme_type) {
    for (int i = 0; SPECIAL_SCHEMES[i].scheme_str != NULL; i++) {
        if (SPECIAL_SCHEMES[i].scheme_type == scheme_type) {
            return &SPECIAL_SCHEMES[i];
        }
    }
    return NULL;
}

// Helper function to parse scheme string to enum
static inline url_scheme_t parse_scheme_type(const char* scheme_str) {
    for (int i = 0; SPECIAL_SCHEMES[i].scheme_str != NULL; i++) {
        if (strcmp(SPECIAL_SCHEMES[i].scheme_str, scheme_str) == 0) {
            return SPECIAL_SCHEMES[i].scheme_type;
        }
    }
    return URL_SCHEME_CUSTOM;  // For non-standard schemes
}
```

## Implementation Phases

### Phase 1: Foundation (Week 1-2)
**Goal**: Basic URL structure and memory management

#### Deliverables:
1. **Core Data Structures**
   - Define `Url` structure with scheme enum
   - Define `UrlParser` structure
   - Simplified memory management without pools in Url

2. **Memory Management**
   ```c
   Url* url_create(void);                    // Simple malloc-based allocation
   void url_destroy(Url* url);               // Free all components
   Url* url_ref(Url* url);                   // Increment reference count
   void url_unref(Url* url);                 // Decrement and free if needed
   ```

3. **Scheme Management**
   ```c
   url_scheme_t url_get_scheme_type(const Url* url);
   const char* url_get_scheme_string(const Url* url);
   bool url_is_special_scheme(url_scheme_t scheme_type);
   uint16_t url_get_default_port(url_scheme_t scheme_type);
   ```

4. **Parser Lifecycle**
   ```c
   UrlParser* url_parser_create(void);
   int url_parser_init(UrlParser* parser);
   void url_parser_clean(UrlParser* parser);
   void url_parser_destroy(UrlParser* parser);
   ```

5. **Basic Tests**
   - Memory allocation/deallocation without pools
   - Reference counting
   - Scheme enum functionality

### Phase 2: Core Parsing Engine (Week 3-4)
**Goal**: Implement basic URL parsing according to WHATWG specification

#### Deliverables:
1. **Parsing State Machine**
   ```c
   typedef enum {
       URL_STATE_SCHEME_START,
       URL_STATE_SCHEME,
       URL_STATE_NO_SCHEME,
       URL_STATE_SPECIAL_RELATIVE_OR_AUTHORITY,
       URL_STATE_PATH_OR_AUTHORITY,
       URL_STATE_RELATIVE,
       URL_STATE_RELATIVE_SLASH,
       URL_STATE_SPECIAL_AUTHORITY_SLASHES,
       URL_STATE_SPECIAL_AUTHORITY_IGNORE_SLASHES,
       URL_STATE_AUTHORITY,
       URL_STATE_HOST,
       URL_STATE_HOSTNAME,
       URL_STATE_PORT,
       URL_STATE_FILE,
       URL_STATE_FILE_SLASH,
       URL_STATE_FILE_HOST,
       URL_STATE_PATH_START,
       URL_STATE_PATH,
       URL_STATE_OPAQUE_PATH,
       URL_STATE_QUERY,
       URL_STATE_FRAGMENT
   } url_parser_state_t;
   ```

2. **Core Parsing Function**
   ```c
   Url* url_parse(UrlParser* parser, 
                  Url* base_url, 
                  const char* input, 
                  size_t length);
   ```

3. **Percent-Encoding Utilities**
   ```c
   char* url_percent_decode(VariableMemPool* pool, const char* input, size_t length);
   char* url_percent_encode(VariableMemPool* pool, const char* input, size_t length, percent_encode_set_t set);
   ```

4. **Tests**
   - Basic URL parsing (absolute URLs)
   - Scheme recognition
   - Authority parsing
   - Path parsing
   - Query and fragment extraction

### Phase 3: Host Parsing (Week 5)
**Goal**: Robust host parsing including domains, IPv4, and IPv6

#### Deliverables:
1. **Host Parser**
   ```c
   typedef enum {
       HOST_TYPE_DOMAIN,
       HOST_TYPE_IPV4,
       HOST_TYPE_IPV6,
       HOST_TYPE_OPAQUE,
       HOST_TYPE_EMPTY
   } host_type_t;
   
   typedef struct {
       host_type_t type;
       union {
           String* domain;          // For domain names
           uint32_t ipv4;          // For IPv4 addresses
           uint16_t ipv6[8];       // For IPv6 addresses
           String* opaque;         // For opaque hosts
       } data;
   } host_t;
   
   host_t* parse_host(VariableMemPool* pool, const char* input, size_t length, bool is_special);
   ```

2. **IPv4 Address Parser**
   - Decimal, hexadecimal, and octal number parsing
   - Validation and range checking
   - Compatibility with web standards

3. **IPv6 Address Parser**
   - Full IPv6 syntax support
   - Compression handling (`::`)
   - IPv4-in-IPv6 support

4. **Domain Name Processing**
   - ASCII case normalization
   - Basic IDNA support (or graceful fallback)
   - Invalid character detection

5. **Tests**
   - IPv4 address parsing (various formats)
   - IPv6 address parsing 
   - Domain name parsing
   - Host validation edge cases

### Phase 4: Relative URL Resolution (Week 6)
**Goal**: Support relative URLs and base URL resolution

#### Deliverables:
1. **Base URL Resolution**
   ```c
   Url* url_resolve(UrlParser* parser, 
                    Url* base_url, 
                    const char* relative_url, 
                    size_t length);
   ```

2. **Path Resolution Algorithm**
   - Dot-segment removal (`.` and `..`)
   - Path normalization
   - Authority inheritance

3. **Special Cases**
   - File URLs with Windows drive letters
   - Empty base URLs
   - Fragment-only URLs

4. **Tests**
   - Relative URL resolution
   - Base URL inheritance
   - Path normalization
   - Edge cases and malformed inputs

### Phase 5: Serialization (Week 7)
**Goal**: Convert URLs back to string representation

#### Deliverables:
1. **URL Serialization**
   ```c
   String* url_serialize(Url* url, VariableMemPool* pool, bool exclude_fragment);
   char* url_to_string(Url* url, VariableMemPool* pool);
   ```

2. **Component Serialization**
   ```c
   String* url_serialize_scheme(Url* url, VariableMemPool* pool);
   String* url_serialize_host(Url* url, VariableMemPool* pool);
   String* url_serialize_path(Url* url, VariableMemPool* pool);
   ```

3. **Host Serialization**
   - IPv4 dotted decimal notation
   - IPv6 compressed notation
   - Domain name case preservation
   - Bracket notation for IPv6

4. **Tests**
   - Roundtrip parsing and serialization
   - Component serialization
   - Format compliance

### Phase 6: API Compatibility Layer (Week 8)
**Goal**: Provide drop-in replacement for lexbor API

#### Deliverables:
1. **Compatibility Types**
   ```c
   // Compatibility typedefs
   typedef Url lxb_url_t;
   typedef UrlParser lxb_url_parser_t;
   typedef int lxb_status_t;  // Or whatever status type lexbor uses
   ```

2. **Compatibility Functions**
   ```c
   // Drop-in replacements for existing lexbor API
   lxb_url_parser_t* lxb_url_parser_create(void);
   lxb_status_t lxb_url_parser_init(lxb_url_parser_t *parser, void *allocator);
   lxb_url_t* lxb_url_parse(lxb_url_parser_t *parser, lxb_url_t *base, 
                           const lxb_char_t *input, size_t length);
   void lxb_url_parser_destroy(lxb_url_parser_t *parser, bool self_destroy);
   void lxb_url_destroy(lxb_url_t *url);
   ```

3. **File System Integration**
   ```c
   char* url_to_local_path(lxb_url_t *url);  // Existing function signature
   lxb_url_t* local_path_to_url(VariableMemPool* pool, const char* path);
   ```

4. **Migration Support**
   - Header file with compatibility macros
   - Gradual migration path
   - Debug/logging integration

### Phase 7: Optimization and Polish (Week 9-10)
**Goal**: Performance optimization and production readiness

#### Deliverables:
1. **Performance Optimizations**
   - String interning for common schemes
   - Fast path for common URL patterns
   - Memory pool optimization for Url objects
   - Reduced allocation overhead

2. **Advanced Features**
   ```c
   // URL manipulation functions
   int url_set_scheme(Url* url, const char* scheme);
   int url_set_host(Url* url, const char* host);
   int url_set_path(Url* url, const char* path);
   int url_set_query(Url* url, const char* query);
   int url_set_fragment(Url* url, const char* fragment);
   ```

3. **Error Reporting**
   - Detailed error messages
   - Error position tracking
   - Validation warnings

4. **Security Features**
   - Input length limits
   - Recursion depth limits
   - Buffer overflow protection

## Testing Strategy

### Test Categories

#### 1. Unit Tests (Criterion Framework)
- **Component Parsing**: Test each URL component parser independently
- **Memory Management**: Verify allocation/deallocation patterns
- **Edge Cases**: Boundary conditions, malformed input
- **Performance**: Benchmarking against lexbor

#### 2. Integration Tests
- **File System Integration**: Test file:// URL handling
- **Document Loading**: Verify integration with input parsers
- **CSS URL Processing**: Test CSS-specific URL handling

#### 3. Compliance Tests
- **WHATWG Test Suite**: Run official URL specification tests
- **Web Platform Tests**: Cross-browser compatibility test suite
- **RFC Compliance**: Basic URI/IRI standard compliance

#### 4. Regression Tests
- **Lexbor Compatibility**: Ensure identical behavior for existing code paths
- **Performance Regression**: Benchmark against previous versions

### Test Data Sources
1. **WHATWG URL Test Data**: Official specification test cases
2. **Web Platform Tests**: Browser compatibility test suite
3. **Real-world URLs**: Common URL patterns from web crawls
4. **Malformed URLs**: Fuzzing and error case generation

### Performance Benchmarks
1. **Parsing Speed**: URLs/second for various URL types
2. **Memory Usage**: Peak and average memory consumption
3. **Cache Performance**: URL parsing cache hit rates
4. **Comparison Baseline**: Performance relative to lexbor

## File Structure

```
lib/
├── url.h                      # Complete URL API (consolidated header)
├── url.c                      # Core URL structure and basic functions
├── url_parser.c               # URL parsing implementation
├── url_host.c                 # Host parsing (IPv4/IPv6/domain)
├── url_percent.c              # Percent-encoding utilities
└── url_serialize.c            # URL serialization

test/
├── test_url.c                 # Comprehensive URL tests (already exists)
└── test_url_performance.c     # Performance benchmarks
```

### Simplified Structure Benefits
1. **Single Include**: Only need `#include "lib/url.h"` 
2. **No Compatibility Layer**: Direct API usage, cleaner code
3. **Easier Maintenance**: All declarations in one place
4. **Faster Compilation**: Fewer header dependencies

## Integration Points

### 1. Simplified Memory Management
```c
// Both Url and UrlParser use simple malloc/free
// String components within Url are malloc'd individually
typedef struct {
    // No pool needed - direct malloc/free approach
} url_allocator_t;

// Memory management functions
Url* url_create(void);                           // malloc-based allocation
void url_destroy(Url* url);                     // Free all String components
String* url_create_string(const char* str);     // Helper for String allocation
void url_free_string(String* str);              // Helper for String deallocation
```

### 2. String Integration
```c
// Use project's String type throughout
typedef struct {
    uint32_t len;              // String length
    uint32_t ref_cnt;          // Reference count
    char chars[];              // Character data
} String;
```

### 3. Error Code Integration
```c
// Use project's existing error code system
typedef enum {
    STATUS_OK = 0,
    STATUS_ERROR_MEMORY_ALLOCATION,
    STATUS_ERROR_INVALID_URL,
    STATUS_ERROR_INVALID_HOST,
    STATUS_ERROR_INVALID_PORT,
    // ... other project error codes
} status_t;
```

### 4. Logging Integration
```c
// Use project's logging system for debugging
#define URL_LOG_DEBUG(fmt, ...) LOG_DEBUG("URL", fmt, ##__VA_ARGS__)
#define URL_LOG_ERROR(fmt, ...) LOG_ERROR("URL", fmt, ##__VA_ARGS__)
```

## Risk Assessment and Mitigation

### High Risks
1. **Specification Complexity**: WHATWG URL spec is complex
   - *Mitigation*: Incremental implementation with comprehensive testing
   
2. **Performance Regression**: Custom implementation might be slower than lexbor
   - *Mitigation*: Continuous benchmarking and optimization

3. **Compatibility Issues**: Breaking existing functionality
   - *Mitigation*: Compatibility layer and extensive regression testing

### Medium Risks
1. **Security Vulnerabilities**: URL parsing is security-critical
   - *Mitigation*: Security review and fuzzing
   
2. **Memory Leaks**: Complex memory management
   - *Mitigation*: Valgrind testing and reference counting

3. **Platform Differences**: Different behavior on Windows/Linux/macOS
   - *Mitigation*: Cross-platform testing

### Low Risks
1. **Maintenance Burden**: Additional code to maintain
   - *Mitigation*: Good documentation and modular design

## Success Criteria

### Phase Completion Criteria
- [ ] All unit tests pass
- [ ] WHATWG compliance test suite passes (>95%)
- [ ] Performance within 10% of lexbor
- [ ] Zero memory leaks in Valgrind
- [ ] Cross-platform compatibility
- [ ] API compatibility with existing code

### Overall Success Metrics
1. **Functionality**: 100% feature parity with lexbor usage
2. **Performance**: ≤10% performance regression
3. **Reliability**: Zero crashes in 1M URL parsing operations
4. **Maintainability**: <1000 lines of code per major component
5. **Security**: Pass security audit and fuzzing tests

## Future Enhancements

### Post-MVP Features
1. **IDNA Support**: Full internationalized domain name processing
2. **Punycode**: Unicode domain name encoding/decoding
3. **URL Templates**: RFC 6570 URI template support
4. **Caching**: Parsed URL caching for repeated operations
5. **Streaming Parser**: Parse URLs from streams without full buffering

### Integration Opportunities
1. **Schema Validation**: URL validation against schemas
2. **Security Policies**: CSP and CORS URL validation
3. **Performance Profiling**: URL parsing performance metrics
4. **Error Recovery**: Better handling of malformed URLs

## Conclusion

This plan provides a structured approach to implementing a robust, spec-compliant URL parser that can replace lexbor while maintaining compatibility and performance. The incremental development strategy allows for early feedback and reduces implementation risks.

The key to success will be:
1. **Thorough Testing**: Comprehensive test coverage at every phase
2. **Performance Focus**: Continuous benchmarking and optimization
3. **Compatibility**: Maintaining existing API compatibility
4. **Security**: Security-first approach to parsing untrusted input

With this plan, we can deliver a production-ready URL parser that serves the project's needs while reducing external dependencies.
