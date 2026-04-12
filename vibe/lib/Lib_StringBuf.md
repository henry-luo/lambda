# StringBuf Refactoring Plan

## Overview

The current `StrBuf` implementation in Lambda contains two mixed versions:
1. **Non-pooled version**: Uses `char*` with standard malloc/realloc
2. **Pooled version**: Uses `String*` structure with memory pool allocation

This refactoring splits the pooled version into a new `StringBuf` type that properly handles `String*` structures, leaving the original `StrBuf` for non-pooled usage.

## Current Implementation Analysis

### StrBuf Structure (lib/strbuf.h)
```c
typedef struct {
    char* str;              // pointer to string data (may or may-not be null-terminated)
    size_t length;          // length excluding null terminator    
    size_t capacity;
    VariableMemPool *pool;  // memory pool for the string buffer
} StrBuf;
```

### Key Issues Identified
1. **Mixed semantics**: `str` field holds either `char*` (non-pooled) or `String*` (pooled)
2. **Complex capacity management**: Pooled version reserves `sizeof(uint32_t)` for String length
3. **Manual String conversion**: Parsers manually cast `sb->str` to `String*` and set length/ref_cnt
4. **Inconsistent initialization**: Pooled version starts with `length = sizeof(uint32_t)`

### Usage Patterns in Input Parsers
- **Universal usage**: All 21 input parsers use `strbuf_new_pooled(input->pool)`
- **String conversion**: Common pattern: `String *string = (String*)sb->str; string->len = sb->length - sizeof(uint32_t); string->ref_cnt = 0;`
- **Helper function**: `strbuf_to_string()` in input.cpp performs the conversion
- **Reset pattern**: `strbuf_full_reset(sb)` after String conversion

## New StringBuf Design

### StringBuf Structure
```c
typedef struct {
    String* str;            // pointer to string data (may or may-not be null-terminated)
    size_t length;          // length excluding null terminator    
    size_t capacity;
    VariableMemPool *pool;  // memory pool for the string buffer
} StringBuf;
```

### Key Design Decisions
1. **Type safety**: `str` field is explicitly `String*` instead of `char*`
2. **Clean semantics**: Length tracks actual string content, not including String header
3. **Pool-only**: StringBuf is always pooled (no non-pooled version)
4. **Direct String access**: No manual casting required

## Implementation Plan

### Phase 1: Core Implementation
1. **Create stringbuf.h and stringbuf.c**
   - Define StringBuf structure
   - Implement core functions (new, free, reset, ensure_cap)
   - Implement append functions (str, char, format, etc.)
   - Implement String conversion functions

2. **Key Functions to Implement**
   ```c
   StringBuf* stringbuf_new(VariableMemPool *pool);
   void stringbuf_free(StringBuf *sb);
   void stringbuf_reset(StringBuf *sb);
   bool stringbuf_ensure_cap(StringBuf *sb, size_t min_capacity);
   void stringbuf_append_str(StringBuf *sb, const char *str);
   void stringbuf_append_char(StringBuf *sb, char c);
   void stringbuf_append_format(StringBuf *sb, const char *format, ...);
   String* stringbuf_to_string(StringBuf *sb);
   ```

### Phase 2: Unit Testing
1. **Create test/test_stringbuf.cpp**
   - Test basic operations (new, append, reset)
   - Test capacity management and growth
   - Test String conversion
   - Test memory pool integration
   - Test edge cases (empty strings, large strings)

### Phase 3: Parser Migration (One at a Time)
Migration order based on complexity and usage patterns:

#### Tier 1: Simple Parsers (Low Risk)
1. **input-csv.cpp** (6 matches) - Simple structure parsing
2. **input-ini.cpp** (12 matches) - Key-value parsing
3. **input-json.cpp** (18 matches) - Well-structured parsing

#### Tier 2: Medium Complexity
4. **input-eml.cpp** (19 matches) - Email parsing
5. **input-vcf.cpp** (25 matches) - Contact parsing
6. **input-rtf.cpp** (24 matches) - Rich text parsing
7. **input-pdf.cpp** (25 matches) - PDF metadata parsing

#### Tier 3: Complex Parsers (High Risk)
8. **input-ics.cpp** (40 matches) - Calendar parsing
9. **input-html.cpp** (41 matches) - HTML parsing
10. **input-mark.cpp** (43 matches) - Markdown parsing
11. **input-toml.cpp** (54 matches) - TOML parsing
12. **input-xml.cpp** (61 matches) - XML parsing
13. **input-math.cpp** (65 matches) - Math expression parsing
14. **input-markup.cpp** (72 matches) - Generic markup
15. **input-css.cpp** (79 matches) - CSS parsing
16. **input-latex.cpp** (80 matches) - LaTeX parsing

#### Tier 4: Minimal Usage
17. **input-adoc.cpp** (1 match) - AsciiDoc
18. **input-man.cpp** (1 match) - Man pages
19. **input-org.cpp** (1 match) - Org mode
20. **input-yaml.cpp** (1 match) - YAML parsing
21. **input.cpp** (4 matches) - Core input functions

### Phase 4: Integration and Cleanup
1. **Update build configuration** - Add stringbuf.c to compilation
2. **Update Input structure** - Change `StrBuf* sb` to `StringBuf* sb`
3. **Remove old patterns** - Clean up manual String casting
4. **Performance testing** - Ensure no regressions

## Migration Strategy Per Parser

### Standard Migration Pattern
1. **Replace includes**: `#include "strbuf.h"` → `#include "stringbuf.h"`
2. **Replace initialization**: `strbuf_new_pooled()` → `stringbuf_new_pooled()`
3. **Replace operations**: `strbuf_*()` → `stringbuf_*()`
4. **Simplify String conversion**: Replace manual casting with `stringbuf_to_string()`
5. **Update variable types**: `StrBuf*` → `StringBuf*`

### Example Transformation
**Before:**
```c
StrBuf* sb = strbuf_new_pooled(input->pool);
strbuf_append_str(sb, "content");
String *string = (String*)sb->str;
string->len = sb->length - sizeof(uint32_t);
string->ref_cnt = 0;
strbuf_full_reset(sb);
```

**After:**
```c
StringBuf* sb = stringbuf_new(input->pool);
stringbuf_append_str(sb, "content");
String *string = stringbuf_to_string(sb);
```

## Testing Strategy

### Unit Test Coverage
1. **Basic Operations**
   - Creation and destruction
   - Append operations (string, char, format)
   - Reset and capacity management

2. **String Integration**
   - String conversion accuracy
   - Length and ref_cnt handling
   - Memory pool integration

3. **Edge Cases**
   - Empty strings
   - Large strings (>4MB)
   - Capacity growth patterns
   - Pool allocation failures

### Integration Testing
1. **Parser Testing**: Run existing input parser tests after each migration
2. **Roundtrip Testing**: Ensure format/input roundtrips still work
3. **Memory Testing**: Verify no memory leaks with pool allocation

## Risk Assessment

### Low Risk
- Simple parsers (CSV, INI, JSON) have straightforward usage patterns
- Well-defined String conversion points
- Existing test coverage for input parsers

### Medium Risk  
- Complex parsers (CSS, LaTeX, Math) have many StringBuf operations
- Multiple temporary StringBuf instances per parser
- Nested parsing contexts

### High Risk
- Input.cpp changes affect all parsers
- Memory pool integration changes
- Performance regressions in hot parsing paths

## Success Criteria

1. **Functional**: All existing input parser tests pass
2. **Performance**: No significant performance regression (<5%)
3. **Memory**: No memory leaks or pool corruption
4. **Maintainability**: Cleaner, more type-safe code
5. **Compatibility**: No breaking changes to Lambda Script API

## Timeline Estimate

- **Phase 1 (Implementation)**: 2-3 days
- **Phase 2 (Unit Testing)**: 1-2 days  
- **Phase 3 (Parser Migration)**: 2-3 weeks (1-2 parsers per day)
- **Phase 4 (Integration)**: 2-3 days
- **Total**: 3-4 weeks

## Dependencies

1. **String.h**: Ensure String structure is properly defined
2. **Memory Pool**: VariableMemPool must support String allocation
3. **Build System**: Update Makefile/premake5.lua to include stringbuf.c
4. **Testing Framework**: Existing test infrastructure for validation

## Notes

- Keep original StrBuf for non-pooled usage (may be used elsewhere)
- Maintain backward compatibility during transition
- Consider adding StringBuf validation/debugging functions
- Document the new API thoroughly
- Monitor memory usage patterns during migration
