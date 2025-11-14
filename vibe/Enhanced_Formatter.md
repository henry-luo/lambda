# Enhanced Formatter Architecture - Incremental Refactoring Plan

## Executive Summary

This document outlines an incremental plan to improve Lambda Script's formatter architecture by:
1. Extracting common utilities to eliminate ~2,000-3,000 lines of duplication
2. Standardizing patterns across all 20 formatters
3. Completing the MarkReader migration for format-md.cpp
4. Improving maintainability, consistency, and performance

**Current State**: 19/20 formatters fully migrated to MarkReader API, with format-md.cpp requiring specialized conversion work.

---

## Phase 1: Complete MarkReader Migration

### Task 1.1: Convert format-md.cpp Helper Functions
**Goal**: Convert all helper functions in format-md.cpp to accept `ElementReader` instead of `Element*`

**Files**: `lambda/format/format-md.cpp`

**Changes**:
- Convert function signatures:
  - `format_heading(StringBuf* sb, Element* elem)` → `format_heading_reader(StringBuf* sb, const ElementReader& elem)`
  - `format_emphasis(StringBuf* sb, Element* elem)` → `format_emphasis_reader(StringBuf* sb, const ElementReader& elem)`
  - `format_code(StringBuf* sb, Element* elem)` → `format_code_reader(StringBuf* sb, const ElementReader& elem)`
  - `format_link(StringBuf* sb, Element* elem)` → `format_link_reader(StringBuf* sb, const ElementReader& elem)`
  - `format_list(StringBuf* sb, Element* elem)` → `format_list_reader(StringBuf* sb, const ElementReader& elem)`
  - `format_table(StringBuf* sb, Element* elem)` → `format_table_reader(StringBuf* sb, const ElementReader& elem)`
  - `format_paragraph(StringBuf* sb, Element* elem)` → `format_paragraph_reader(StringBuf* sb, const ElementReader& elem)`
  - `format_blockquote(StringBuf* sb, Element* elem)` → `format_blockquote_reader(StringBuf* sb, const ElementReader& elem)`
  - `format_thematic_break(StringBuf* sb)` → No change needed (no element parameter)

- Replace all `get_attribute()` calls with `get_attribute_reader()`
- Update `format_element_reader()` to call the new `_reader` helper functions
- Remove all old pointer-based helper functions and their forward declarations

**Testing**: Run `make test` - maintain 15/15 tests passing

**Estimated Effort**: 4-6 hours

**Success Criteria**: 
- format-md.cpp reduced from 1192 → ~600 lines (50% reduction)
- All tests passing
- 100% MarkReader API usage across all 20 formatters

---

## Phase 2: Extract Common Text Processing Utilities

### Task 2.1: Create format-utils.cpp Foundation
**Goal**: Establish shared utilities file with common text processing functions

**New Files**: 
- `lambda/format/format-utils.cpp`
- `lambda/format/format-utils.h`

**Implementation**:

```c
// format-utils.h
#ifndef FORMAT_UTILS_H
#define FORMAT_UTILS_H

#include "../../lib/stringbuf.h"
#include "../lambda-data.hpp"

// Text escaping configuration
typedef struct {
    const char* chars_to_escape;      // Characters needing escape
    bool use_backslash_escape;        // true for \*, false for &amp;
    const char* (*escape_fn)(char c); // Custom escape sequence generator
} TextEscapeConfig;

// Common text formatting functions
void format_raw_text_common(StringBuf* sb, String* str);
void format_text_with_escape(StringBuf* sb, String* str, const TextEscapeConfig* config);

// Predefined escape configs
extern const TextEscapeConfig MARKDOWN_ESCAPE_CONFIG;
extern const TextEscapeConfig RST_ESCAPE_CONFIG;
extern const TextEscapeConfig WIKI_ESCAPE_CONFIG;

#endif // FORMAT_UTILS_H
```

**Testing**: Unit tests for escape functions with edge cases

**Estimated Effort**: 3-4 hours

---

### Task 2.2: Migrate format_raw_text() to Shared Utility
**Goal**: Replace duplicate `format_raw_text()` in md, wiki formatters

**Files Modified**:
- `lambda/format/format-md.cpp` - remove local `format_raw_text()`, use `format_raw_text_common()`
- `lambda/format/format-wiki.cpp` - remove local `format_raw_text()`, use `format_raw_text_common()`

**Lines Eliminated**: ~40 lines

**Testing**: Verify markdown and wiki roundtrip tests pass

**Estimated Effort**: 1 hour

---

### Task 2.3: Migrate format_text() to Shared Utility
**Goal**: Replace duplicate `format_text()` implementations with config-based approach

**Files Modified**:
- `lambda/format/format-md.cpp` - use `format_text_with_escape(sb, str, &MARKDOWN_ESCAPE_CONFIG)`
- `lambda/format/format-wiki.cpp` - use `format_text_with_escape(sb, str, &WIKI_ESCAPE_CONFIG)`
- `lambda/format/format-rst.cpp` - use `format_text_with_escape(sb, str, &RST_ESCAPE_CONFIG)`

**Lines Eliminated**: ~150 lines across 3 files

**Testing**: Comprehensive escaping tests for special characters in each format

**Estimated Effort**: 2-3 hours

---

## Phase 3: Standardize Attribute Access

### Task 3.1: Create Typed Attribute Accessors
**Goal**: Provide type-safe attribute access helpers

**Implementation in format-utils.h**:

```c
#include "../mark_reader.hpp"

// Type-safe attribute accessors
static inline String* get_string_attr(const ElementReader& elem, const char* attr_name) {
    ItemReader attr = elem.get_attr(attr_name);
    return attr.isString() ? attr.asString() : NULL;
}

static inline int get_int_attr(const ElementReader& elem, const char* attr_name, int default_val) {
    ItemReader attr = elem.get_attr(attr_name);
    return attr.isInt() ? attr.asInt() : default_val;
}

static inline bool get_bool_attr(const ElementReader& elem, const char* attr_name, bool default_val) {
    ItemReader attr = elem.get_attr(attr_name);
    return attr.isBool() ? attr.asBool() : default_val;
}
```

**Estimated Effort**: 1 hour

---

### Task 3.2: Migrate format-md.cpp to Typed Accessors
**Goal**: Replace all `get_attribute_reader()` calls with type-safe accessors

**Files Modified**: `lambda/format/format-md.cpp`

**Pattern**:
```c
// Before:
String* level_attr = get_attribute_reader(elem, "level");
if (level_attr) { ... }

// After:
String* level_attr = get_string_attr(elem, "level");
if (level_attr) { ... }
```

**Lines Eliminated**: ~10 lines (remove local `get_attribute_reader()`)

**Estimated Effort**: 1 hour

---

### Task 3.3: Migrate Other Formatters to Typed Accessors
**Goal**: Standardize attribute access across all formatters

**Files Modified**:
- `lambda/format/format-wiki.cpp`
- `lambda/format/format-rst.cpp`
- `lambda/format/format-jsx.cpp`
- `lambda/format/format-mdx.cpp`
- `lambda/format/format-graph.cpp`
- 5-10 other formatters with attribute access

**Lines Eliminated**: ~50 lines across all files

**Testing**: All roundtrip tests pass

**Estimated Effort**: 2-3 hours

---

## Phase 4: Standardize Element Child Iteration

### Task 4.1: Create Parameterized Child Iteration Helper
**Goal**: Extract common child iteration pattern

**Implementation in format-utils.h**:

```c
typedef void (*TextProcessor)(StringBuf* sb, String* str);
typedef void (*ItemProcessor)(StringBuf* sb, const ItemReader& item);

// Process element children with custom text and item handlers
void format_element_children_with_processors(
    StringBuf* sb,
    const ElementReader& elem,
    TextProcessor text_proc,
    ItemProcessor item_proc
);
```

**Implementation in format-utils.cpp**:

```c
void format_element_children_with_processors(
    StringBuf* sb,
    const ElementReader& elem,
    TextProcessor text_proc,
    ItemProcessor item_proc
) {
    auto it = elem.children();
    ItemReader child;
    while (it.next(&child)) {
        if (child.isString()) {
            if (text_proc) {
                text_proc(sb, child.asString());
            }
        } else if (item_proc) {
            item_proc(sb, child);
        }
    }
}
```

**Estimated Effort**: 2 hours

---

### Task 4.2: Refactor format_element_children_reader() Functions
**Goal**: Replace local implementations with shared helper

**Files Modified**:
- `lambda/format/format-md.cpp`
- `lambda/format/format-wiki.cpp`
- `lambda/format/format-rst.cpp`
- 7-10 other formatters

**Pattern**:
```c
// Before:
static void format_element_children_reader(StringBuf* sb, const ElementReader& elem) {
    auto it = elem.children();
    ItemReader child;
    while (it.next(&child)) {
        if (child.isString()) {
            format_text(sb, child.asString());
        } else {
            format_item_reader(sb, child);
        }
    }
}

// After:
static void format_element_children_reader(StringBuf* sb, const ElementReader& elem) {
    format_element_children_with_processors(sb, elem, format_text, format_item_reader);
}
```

**Lines Eliminated**: ~80 lines per formatter × 10 formatters = ~800 lines

**Testing**: All roundtrip tests pass

**Estimated Effort**: 3-4 hours

---

## Phase 5: Create Element Type Dispatcher

### Task 5.1: Implement Dispatcher Infrastructure
**Goal**: Create hash-based element type routing system

**Implementation in format-utils.h**:

```c
#include "../../lib/hashmap.h"

typedef void (*ElementFormatterFunc)(StringBuf* sb, const ElementReader& elem);

typedef struct {
    HashMap* type_handlers;           // tag_name -> ElementFormatterFunc
    ElementFormatterFunc default_handler;
    Pool* pool;
} FormatterDispatcher;

// Dispatcher lifecycle
FormatterDispatcher* dispatcher_create(Pool* pool);
void dispatcher_register(FormatterDispatcher* d, const char* type, ElementFormatterFunc fn);
void dispatcher_set_default(FormatterDispatcher* d, ElementFormatterFunc fn);
void dispatcher_format(FormatterDispatcher* d, StringBuf* sb, const ElementReader& elem);
void dispatcher_destroy(FormatterDispatcher* d);
```

**Estimated Effort**: 4-5 hours

---

### Task 5.2: Refactor format-md.cpp to Use Dispatcher
**Goal**: Replace large if-else chain with dispatcher

**Changes in format-md.cpp**:

```c
// Setup dispatcher (once at initialization)
static FormatterDispatcher* md_dispatcher = NULL;

static void init_markdown_dispatcher(Pool* pool) {
    if (md_dispatcher) return;
    
    md_dispatcher = dispatcher_create(pool);
    dispatcher_register(md_dispatcher, "h1", format_heading_reader);
    dispatcher_register(md_dispatcher, "h2", format_heading_reader);
    // ... register all types
    dispatcher_set_default(md_dispatcher, format_element_children_reader);
}

// In format_element_reader():
static void format_element_reader(StringBuf* sb, const ElementReader& elem) {
    dispatcher_format(md_dispatcher, sb, elem);
}
```

**Lines Eliminated**: ~300 lines of if-else chains

**Testing**: All markdown tests pass

**Estimated Effort**: 4-5 hours

---

### Task 5.3: Migrate Other Formatters to Dispatcher Pattern
**Goal**: Apply dispatcher pattern to wiki, rst, html, jsx formatters

**Files Modified**:
- `lambda/format/format-wiki.cpp`
- `lambda/format/format-rst.cpp`
- `lambda/format/format-html.cpp`
- `lambda/format/format-jsx.cpp`

**Lines Eliminated**: ~200 lines per formatter × 4 = ~800 lines

**Estimated Effort**: 6-8 hours

---

## Phase 6: Consolidate HTML Entity Handling

### Task 6.1: Extract HTML Entity Detection Logic
**Goal**: Share sophisticated entity handling from format-html.cpp

**Implementation in format-utils.h**:

```c
// Check if position in string is start of HTML entity
// Returns true and sets *entity_end if entity found
bool is_html_entity(const char* str, size_t len, size_t pos, size_t* entity_end);

// Format string with HTML entity escaping (prevents double-encoding)
void format_html_string_safe(StringBuf* sb, String* str, bool is_attribute);
```

**Estimated Effort**: 2-3 hours

---

### Task 6.2: Migrate HTML-Family Formatters
**Goal**: Use shared entity handling in jsx, mdx formatters

**Files Modified**:
- `lambda/format/format-jsx.cpp` - replace `format_jsx_text_content()` with `format_html_string_safe()`
- `lambda/format/format-mdx.cpp` - use for text content escaping

**Lines Eliminated**: ~100 lines

**Testing**: JSX/MDX roundtrip tests with entities like `&nbsp;`, `&#123;`, `&frac12;`

**Estimated Effort**: 2 hours

---

## Phase 7: Standardize Table Processing

### Task 7.1: Create Table Analysis Utilities
**Goal**: Extract common table structure analysis

**Implementation in format-utils.h**:

```c
typedef struct {
    int row_count;
    int column_count;
    bool has_header;
    const char** alignments;  // Array of: NULL, "left", "center", "right"
} TableInfo;

// Analyze table structure
TableInfo* analyze_table(Pool* pool, const ElementReader& table_elem);
void free_table_info(TableInfo* info);

// Iterate table with callbacks
typedef void (*TableRowHandler)(StringBuf* sb, const ElementReader& row, int row_idx, bool is_header, void* ctx);
void iterate_table_rows(const ElementReader& table_elem, TableRowHandler handler, void* context);
```

**Estimated Effort**: 3-4 hours

---

### Task 7.2: Refactor Table Formatting in Markdown
**Goal**: Use shared table utilities in format-md.cpp

**Changes**: Replace custom table iteration with `iterate_table_rows()` and `analyze_table()`

**Lines Eliminated**: ~150 lines

**Testing**: Markdown table tests with various alignments and edge cases

**Estimated Effort**: 3-4 hours

---

### Task 7.3: Migrate Table Handling to Other Formatters
**Goal**: Standardize table processing in rst, wiki, html

**Files Modified**:
- `lambda/format/format-rst.cpp`
- `lambda/format/format-wiki.cpp`
- `lambda/format/format-html.cpp`

**Lines Eliminated**: ~400 lines total

**Estimated Effort**: 4-5 hours

---

## Phase 8: Create Formatter Context Infrastructure

### Task 8.1: Define FormatterContext Structure
**Goal**: Create shared state management for formatters

**Implementation in format-utils.h**:

```c
typedef struct {
    StringBuf* output;
    Pool* pool;
    int recursion_depth;
    int indent_level;
    bool compact_mode;
    void* format_specific_state;  // Opaque pointer for formatter-specific data
} FormatterContext;

#define MAX_RECURSION_DEPTH 50

// Recursion control macros
#define CHECK_RECURSION(ctx) \
    if ((ctx)->recursion_depth >= MAX_RECURSION_DEPTH) return; \
    (ctx)->recursion_depth++

#define END_RECURSION(ctx) (ctx)->recursion_depth--

// Context lifecycle
FormatterContext* formatter_context_create(Pool* pool, StringBuf* output);
void formatter_context_destroy(FormatterContext* ctx);
```

**Estimated Effort**: 2 hours

---

### Task 8.2: Migrate Formatters to Use Context
**Goal**: Replace thread_local recursion tracking with context passing

**Files Modified**: All 20 formatters

**Pattern**:
```c
// Before:
static thread_local int recursion_depth = 0;
void format_item(StringBuf* sb, Item item) {
    if (recursion_depth >= MAX_RECURSION_DEPTH) return;
    recursion_depth++;
    // ... processing
    recursion_depth--;
}

// After:
void format_item(FormatterContext* ctx, Item item) {
    CHECK_RECURSION(ctx);
    // ... processing
    END_RECURSION(ctx);
}
```

**Lines Eliminated**: ~11 formatters × 5 lines = ~55 lines

**Benefits**: Better testability, no thread_local issues

**Estimated Effort**: 6-8 hours (touches all formatters)

---

## Phase 9: Standardize Format Entry Points

### Task 9.1: Create Entry Point Macros
**Goal**: Ensure consistent API across all formatters

**Implementation in format-utils.h**:

```c
// Macro to generate standard String* wrapper for formatters
#define DEFINE_FORMAT_STRING_WRAPPER(format_name) \
String* format_##format_name##_string(Pool* pool, Item root_item) { \
    if (!pool) return NULL; \
    StringBuf* sb = stringbuf_new(pool); \
    if (!sb) return NULL; \
    format_##format_name(sb, root_item); \
    String* result = stringbuf_to_string(sb); \
    stringbuf_free(sb); \
    return result; \
}
```

**Estimated Effort**: 2 hours

---

### Task 9.2: Apply Standard Entry Points
**Goal**: Ensure all formatters have both StringBuf and String* versions

**Files Modified**: Any formatter missing String* wrapper

**Changes**: Replace hand-written wrappers with `DEFINE_FORMAT_STRING_WRAPPER(format_name)`

**Lines Eliminated**: ~8 lines per formatter × 15 formatters = ~120 lines

**Estimated Effort**: 2-3 hours

---

## Phase 10: Extract Element Type Predicates

### Task 10.1: Create Type Checking Utilities
**Goal**: Centralize element type name comparisons

**Implementation in format-utils.h**:

```c
// Common element type predicates
static inline bool is_heading_element(const ElementReader& elem) {
    const char* type = elem.type().chars;
    return type && strncmp(type, "h", 1) == 0 && isdigit(type[1]);
}

static inline bool is_list_element(const ElementReader& elem) {
    const char* type = elem.type().chars;
    return type && (strcmp(type, "ul") == 0 || strcmp(type, "ol") == 0);
}

static inline bool is_emphasis_element(const ElementReader& elem) {
    const char* type = elem.type().chars;
    return type && (strcmp(type, "em") == 0 || strcmp(type, "strong") == 0);
}

static inline bool is_code_element(const ElementReader& elem) {
    const char* type = elem.type().chars;
    return type && (strcmp(type, "code") == 0 || strcmp(type, "pre") == 0);
}

static inline bool is_block_element(const char* type) {
    return strcmp(type, "p") == 0 || strcmp(type, "div") == 0 ||
           strcmp(type, "blockquote") == 0 || strcmp(type, "pre") == 0;
}
```

**Estimated Effort**: 2 hours

---

### Task 10.2: Migrate Formatters to Use Predicates
**Goal**: Replace inline type comparisons with predicates

**Files Modified**: All formatters with element type checking

**Lines Eliminated**: ~100 lines across all formatters

**Benefits**: Self-documenting, easier to refactor type system

**Estimated Effort**: 3-4 hours

---

## Phase 11: Shared Math Expression Handling

### Task 11.1: Create Math Element Processor
**Goal**: Standardize math rendering across formatters

**Implementation in format-utils.h**:

```c
typedef enum {
    MATH_LATEX,
    MATH_TYPST,
    MATH_ASCII,
    MATH_MATHML
} MathFlavor;

// Format math element with automatic flavor detection or explicit flavor
void format_math_element(
    StringBuf* sb,
    const ElementReader& elem,
    MathFlavor flavor,
    bool inline_mode
);

// Detect math flavor from element attributes or content
MathFlavor detect_math_flavor(const ElementReader& elem);
```

**Estimated Effort**: 4-5 hours

---

### Task 11.2: Integrate Math Handling in Formatters
**Goal**: Use shared math processor in md, html, mdx formatters

**Files Modified**:
- `lambda/format/format-md.cpp` - replace custom LaTeX detection
- `lambda/format/format-html.cpp` - use for `<math>` elements
- `lambda/format/format-mdx.cpp` - use for math expressions

**Lines Eliminated**: ~150 lines

**Testing**: Math expression tests across all flavors

**Estimated Effort**: 3-4 hours

---

## Phase 12: Performance Optimization

### Task 12.1: Benchmark Current Performance
**Goal**: Establish baseline metrics

**Activities**:
- Create benchmark suite for each formatter
- Measure formatting time for various document sizes
- Profile memory allocation patterns
- Identify hot paths

**Estimated Effort**: 4-5 hours

---

### Task 12.2: Optimize Dispatcher Lookups
**Goal**: Improve element type routing performance

**Changes**:
- Pre-compute hash keys for common element types
- Implement inline cache for most frequent types
- Consider perfect hash functions for known type sets

**Expected Improvement**: 10-20% faster element dispatch

**Estimated Effort**: 3-4 hours

---

### Task 12.3: Optimize Text Escaping
**Goal**: Improve string processing performance

**Changes**:
- Batch character writes where possible
- Pre-allocate StringBuf capacity based on input size
- Optimize common case (no escaping needed)

**Expected Improvement**: 5-15% faster text formatting

**Estimated Effort**: 3-4 hours

---

## Phase 13: Documentation and Testing

### Task 13.1: Document Shared Utilities
**Goal**: Comprehensive API documentation

**Deliverables**:
- API reference for format-utils.h
- Usage examples for each major utility
- Migration guide for adding new formatters

**Estimated Effort**: 4-5 hours

---

### Task 13.2: Create Unit Test Suite
**Goal**: Test shared utilities independently

**Coverage**:
- Text escaping with all escape configs
- Attribute accessor edge cases
- Element iteration patterns
- Dispatcher registration and lookup
- HTML entity detection
- Table structure analysis

**Estimated Effort**: 6-8 hours

---

### Task 13.3: Integration Testing
**Goal**: Verify all formatters work correctly with shared utilities

**Testing Strategy**:
- All existing roundtrip tests must pass
- Add new tests for edge cases discovered during refactoring
- Performance regression tests
- Memory leak detection with valgrind

**Estimated Effort**: 4-5 hours

---

## Summary Timeline

| Phase | Description | Estimated Effort | Priority |
|-------|-------------|------------------|----------|
| 1 | Complete MarkReader Migration (format-md.cpp) | 4-6 hours | **Critical** |
| 2 | Extract Text Processing Utilities | 6-8 hours | **High** |
| 3 | Standardize Attribute Access | 4-5 hours | **High** |
| 4 | Standardize Child Iteration | 5-6 hours | **High** |
| 5 | Create Element Type Dispatcher | 14-18 hours | **Medium** |
| 6 | Consolidate HTML Entity Handling | 4-5 hours | **Medium** |
| 7 | Standardize Table Processing | 10-13 hours | **Medium** |
| 8 | Create Formatter Context | 8-10 hours | **Medium** |
| 9 | Standardize Entry Points | 4-5 hours | **Low** |
| 10 | Extract Type Predicates | 5-6 hours | **Low** |
| 11 | Shared Math Handling | 7-9 hours | **Low** |
| 12 | Performance Optimization | 10-13 hours | **Optional** |
| 13 | Documentation & Testing | 14-18 hours | **Critical** |

**Total Estimated Effort**: 95-122 hours (~12-15 working days)

---

## Expected Outcomes

### Code Quality Metrics
- **Lines Eliminated**: 2,000-3,000 lines across all formatters
- **Code Duplication**: Reduced from ~40% to <10%
- **Cyclomatic Complexity**: Reduced by 30-40% per formatter
- **Test Coverage**: Increased from ~70% to >85%

### Maintainability Improvements
- Single point of maintenance for common operations
- Consistent patterns reduce cognitive load
- Easier to add new formatters (4-6 hours vs 2-3 days)
- Bug fixes benefit all formatters simultaneously

### Performance Improvements
- 10-20% faster element dispatching (Phase 12)
- 5-15% faster text processing (Phase 12)
- Reduced memory allocations through context reuse
- Better cache locality with dispatcher pattern

### Architecture Benefits
- Clear separation of concerns (traversal vs. formatting logic)
- Type-safe APIs reduce runtime errors
- Extensible dispatcher allows format-specific customization
- Testable components enable higher confidence in changes

---

## Risk Mitigation

### Risk: Breaking Existing Tests
**Mitigation**: 
- Run full test suite after each phase
- Keep backup copies of working formatters
- Use feature flags to toggle between old/new implementations

### Risk: Performance Regression
**Mitigation**:
- Establish benchmarks in Phase 12
- Monitor performance after each major change
- Profile before and after to identify regressions
- Keep critical paths optimized (inline small helpers)

### Risk: Over-Engineering
**Mitigation**:
- Implement only utilities used by 3+ formatters
- Avoid premature abstraction
- Keep format-specific logic in formatters, not utilities
- Regular code reviews to maintain simplicity

### Risk: Integration Complexity
**Mitigation**:
- Incremental migration (one formatter at a time)
- Maintain both old and new implementations during transition
- Clear rollback plan for each phase
- Comprehensive integration testing

---

## Success Criteria

### Phase Completion
✓ All tests pass (15/15 roundtrip tests)  
✓ No memory leaks detected  
✓ Build completes without warnings  
✓ Performance within 5% of baseline  

### Project Completion
✓ All 20 formatters use shared utilities  
✓ format-md.cpp fully migrated to MarkReader  
✓ Code duplication reduced to <10%  
✓ Documentation complete and accurate  
✓ Test coverage >85%  
✓ Performance improved or maintained  

---

## Maintenance Plan

### Post-Refactoring
1. **Code Freeze**: 1 week stabilization period
2. **Regression Testing**: Comprehensive test across all formats
3. **Performance Validation**: Verify benchmarks meet targets
4. **Documentation Review**: Ensure all changes documented

### Long-Term
1. **Quarterly Review**: Assess if new common patterns emerge
2. **Performance Monitoring**: Track formatter performance over time
3. **API Stability**: Maintain backward compatibility for 2 major versions
4. **Community Feedback**: Incorporate user-reported issues into future phases

---

## Conclusion

This incremental refactoring plan will transform Lambda Script's formatter architecture from 20 independent implementations into a cohesive system built on shared infrastructure. By completing the MarkReader migration and extracting common utilities, we'll achieve:

- **Better Code Quality**: Less duplication, more consistency
- **Easier Maintenance**: Fix once, benefit everywhere
- **Faster Development**: Adding formatters becomes straightforward
- **Higher Reliability**: Shared code means shared testing and validation

The phased approach allows for continuous integration and testing, minimizing risk while delivering incremental value. Priority phases (1-4) deliver 70% of the benefits in 40% of the time, making them ideal candidates for immediate implementation.
