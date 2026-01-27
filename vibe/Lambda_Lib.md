# Lambda Standard Library Migration Proposal

**Goal**: Remove `std::*` (std::string, std::vector, std::map, etc.) from Lambda runtime code and use only C types and data structures from `./lib`.

**Rationale**: Tight control over basic data structures and types used in Lambda runtime for:
- Consistent and full **control over memory management**
	- e.g. with pool/arena allocation, reference counting, etc.
- **Reduced binary size** and startup time
- Better and easier integration with the MIR JIT compiler (which is a C compiler)

---

## Current State Analysis

### Already Clean (No std::* usage)
These core Lambda runtime files are already free of std::* dependencies:
- `lambda/lambda-data.cpp` - Runtime data manipulation
- `lambda/lambda-eval.cpp` - Interpreter execution
- `lambda/lambda-mem.cpp` - Memory allocation
- `lambda/lambda-proc.cpp` - Built-in procedures
- `lambda/lambda-stack.cpp` - Stack management
- `lambda/build_ast.cpp` - AST construction
- `lambda/transpile.cpp` - C code transpilation
- `lambda/transpile-mir.cpp` - MIR JIT compilation
- `lambda/runner.cpp` - Function registration
- `lambda/mark_builder.cpp` - Data construction
- `lambda/mark_reader.cpp` - Data traversal

### Files Requiring Migration

#### Phase 1: Lambda Script Core (Low Impact) ✅ COMPLETED
| File | std::* Usage | Replacement Strategy |
|------|--------------|---------------------|
| `lambda/safety_analyzer.cpp` | `std::string`, `std::vector`, `std::unordered_map`, `std::unordered_set` | ✅ Static const array, `const char*` |
| `lambda/mark_editor.cpp` | `std::vector` (2 uses) | ✅ Stack arrays (`MAX_BATCH_UPDATES=64`) |
| `lambda/main.cpp` | `std::string` (2 uses) | ✅ `StrBuf*` |

**Test Results:** All 154 Lambda baseline tests pass

#### Phase 2: Input Parsers (Medium Impact)
| File | std::* Usage | Replacement Strategy |
|------|--------------|---------------------|
| `lambda/input/input-json.cpp` | `std::string` (2 uses) | Use `StrBuf` for error messages |
| `lambda/input/input-context.cpp` | `std::string` (15+ uses) | Use `StrBuf` |
| `lambda/input/input-context.hpp` | `std::string` | Use `char*` / `StrBuf` |
| `lambda/input/source_tracker.cpp` | `std::string`, `std::vector` | Use `StrBuf`, `ArrayList` |
| `lambda/input/source_tracker.hpp` | `std::string`, `std::vector` | Use `char*`, `ArrayList` |
| `lambda/input/parse_error.cpp` | `std::string`, `std::vector` | Use `StrBuf`, `ArrayList` |
| `lambda/input/parse_error.hpp` | `std::string`, `std::vector` | Use `char*`, `ArrayList` |
| `lambda/input/input-latex-ts.cpp` | `std::string`, `std::unordered_map` | Use `StrBuf`, `HashMap` |
| `lambda/input/input-graph-mermaid.cpp` | `std::string` | Use `StrBuf` |
| `lambda/input/markup/markup_parser.cpp` | `std::string` | Use `StrBuf` |
| `lambda/input/markup/block/block_quote.cpp` | `std::vector` | Use `ArrayList` |

#### Phase 3: Format/Output Code (Medium Impact)
| File | std::* Usage | Replacement Strategy |
|------|--------------|---------------------|
| `lambda/format/format-utils.cpp` | `std::unordered_map` | Use `HashMap` |
| `lambda/format/html_encoder.cpp` | `std::string`, `std::string_view` | Use `StrBuf`, `StrView` |
| `lambda/format/html_writer.cpp` | `std::string`, `std::vector` | Use `StrBuf`, `ArrayList` |
| `lambda/format/html_generator.cpp` | `std::string`, `std::vector` | Use `StrBuf`, `ArrayList` |

#### Phase 4: Radiant Engine (Separate Consideration)
| File | std::* Usage | Notes |
|------|--------------|-------|
| `radiant/cmd_layout.cpp` | `std::vector`, `std::unordered_map`, `std::string` | Critical layout code |
| `radiant/layout_multicol.cpp` | `std::vector` | Column layout |
| `radiant/font_face.cpp` | `std::string` | WOFF2 decompression |

#### Phase 5: LaTeX Formatting (Lower Priority)
| File | std::* Usage | Replacement Strategy |
|------|--------------|---------------------|
| `lambda/format/latex_packages.cpp` | `std::map`, `std::string` | Sorted array + bsearch for static symbols |
| `lambda/format/latex_packages.hpp` | `std::map`, `std::unordered_map` | `HashMap` or ArrayList |
| `lambda/format/latex_generator.cpp` | `std::map`, `std::string`, `std::vector` | ArrayList for small maps |
| `lambda/format/latex_generator.hpp` | `std::map`, `std::string`, `std::vector` | ArrayList for counters/lengths/labels |
| `lambda/format/latex_docclass.cpp` | `std::map`, `std::string`, `std::vector` | ArrayList for counters/lengths |
| `lambda/format/latex_docclass.hpp` | `std::map`, `std::string`, `std::vector` | ArrayList |
| `lambda/format/latex_assets.cpp` | `std::string`, `std::vector` | `StrBuf`, `ArrayList` |
| `lambda/format/latex_hyphenation.cpp` | `std::string`, `std::vector`, `std::unordered_map` | `HashMap` for patterns |
| `lambda/format/latex_picture.cpp` | `std::string`, `std::vector` | `StrBuf`, `ArrayList` |

---

## `std::map` Usage Analysis

**Key Finding**: None of the `std::map` usages require ordered iteration. All are simple key-value lookups.

### Usage Breakdown

| Location | Data | Count | Best Replacement |
|----------|------|-------|------------------|
| `latex_packages.cpp` | Symbol tables (BASE_SYMBOLS, DIACRITICS, etc.) | ~200 entries | **Sorted array + bsearch** (static data) |
| `latex_generator.cpp` | Counters | ~25 | **ArrayList + linear search** |
| `latex_generator.cpp` | Lengths | ~40 | **ArrayList + linear search** |
| `latex_generator.cpp` | Labels | varies | **HashMap** |
| `latex_docclass.cpp` | Counters/Lengths init | ~30 | **ArrayList + linear search** |
| `latex_packages.cpp` | Package factories | ~10 | **ArrayList + linear search** |

### Recommended Replacements

**For static symbol tables** (compile-time constant):
```c
// Sorted array + binary search - perfect for static data
typedef struct { const char* key; const char* value; } SymbolEntry;

static const SymbolEntry BASE_SYMBOLS[] = {
    {"$", "$"},
    {"AA", "Å"},
    // ... sorted alphabetically
};
#define BASE_SYMBOLS_COUNT (sizeof(BASE_SYMBOLS)/sizeof(BASE_SYMBOLS[0]))

const char* symbol_lookup(const char* cmd);  // Uses bsearch()
```

**For small dynamic maps** (counters, lengths):
```c
// Simple key-value array - linear search is fine for ~30 items
typedef struct { const char* key; Counter value; } CounterEntry;

typedef struct {
    CounterEntry* entries;
    int count;
    int capacity;
} CounterMap;

Counter* countermap_get(CounterMap* map, const char* key);  // O(n) but n≤30
void countermap_set(CounterMap* map, const char* key, Counter value);
```

**For larger dynamic maps** (labels, hyphenation patterns): Use `HashMap` from `lib/hashmap.h`

**No AvlTree needed** - these are all simple lookups without ordering requirements.

---

## Available Library Components (`./lib`)

### String Types
| Type | Header | Description |
|------|--------|-------------|
| `StrView` | `strview.h` | Read-only string view (like std::string_view) |
| `StrBuf` | `strbuf.h` | Growable string buffer (like std::string) |
| `StringBuf` | `stringbuf.h` | Pool-allocated string buffer |
| `String` | `string.h` | Lambda's immutable ref-counted string |

### Collections
| Type | Header | Description |
|------|--------|-------------|
| `ArrayList` | `arraylist.h` | Dynamic array of pointers (like std::vector) |
| `HashMap` | `hashmap.h` | Robin hood hash map (like std::unordered_map) |
| `PriorityQueue` | `priority_queue.h` | Min-heap priority queue |

**Note**: `AvlTree` (avl_tree.h) exists but is overkill for most uses. See `std::map` analysis below.

### Memory Management
| Type | Header | Description |
|------|--------|-------------|
| `Pool` | `mempool.h` | Variable-size memory pool |
| `Arena` | `arena.h` | Linear/bump allocator |

---

## New Library Extensions Needed

### 1. `ArrayList` Enhancements (Optional)
Current `ArrayList` stores `void*` pointers. For type safety, consider adding typed macros:

```c
// In arraylist.h - type-safe wrappers
#define DECLARE_ARRAYLIST(TYPE, NAME) \
    typedef struct { ArrayList base; } NAME##List;

#define arraylist_typed_append(list, item) \
    arraylist_append(&(list)->base, (void*)(item))

#define arraylist_typed_get(list, index, TYPE) \
    ((TYPE*)arraylist_get(&(list)->base, index))
```

### 2. String Formatting Helper
Add formatting utilities to `strbuf.h`:

```c
// Already exists: strbuf_append_format()
// May want to add convenience functions:
void strbuf_append_char_escaped(StrBuf* sb, char c);  // For JSON/HTML escaping
void strbuf_append_html_escaped(StrBuf* sb, const char* str);
```

### 3. HashMap C++ Wrapper Improvements
The existing `lib/hashmap.hpp` uses std::* internally. Create a pure C version or extend `hashmap.h`:

```c
// Add string-keyed convenience functions to hashmap.h
HashMap* hashmap_new_string_keyed(size_t value_size);
void* hashmap_get_str(HashMap* map, const char* key);
void* hashmap_set_str(HashMap* map, const char* key, void* value);
```

---

## Migration Strategy

### Phase 1: Lambda Script Core (Week 1)

**Priority**: High (affects Lambda script compilation)

#### 1.1 `lambda/mark_editor.cpp`
Replace `std::vector<UpdateEntry>` with stack-allocated array or `ArrayList`:

```cpp
// Before
std::vector<UpdateEntry> updates;
updates.reserve(count);

// After - Option A: Stack array for small counts
#define MAX_BATCH_UPDATES 32
UpdateEntry updates[MAX_BATCH_UPDATES];
int update_count = 0;

// After - Option B: ArrayList
ArrayList* updates = arraylist_new(count);
// ... use arraylist_append/arraylist_get
arraylist_free(updates);
```

#### 1.2 `lambda/safety_analyzer.cpp`
Replace std containers with lib equivalents:

```cpp
// Before
std::unordered_map<std::string, FunctionCallInfo> functions_;
std::unordered_set<std::string> safe_builtins_;

// After
HashMap* functions_;  // key: char*, value: FunctionCallInfo*
HashMap* safe_builtins_;  // key: char*, value: bool*
```

#### 1.3 `lambda/main.cpp`
Replace `std::string` with `StrBuf`:

```cpp
// Before
std::string full_doc_output;
full_doc_output = std::string(html_buf->str, html_buf->length);

// After
StrBuf* full_doc_output = strbuf_new();
strbuf_append_str_n(full_doc_output, html_buf->str, html_buf->length);
// ... use full_doc_output->str
strbuf_free(full_doc_output);
```

### Phase 2: Input Parsers (Week 2-3)

**Priority**: Medium (affects all input parsing)

#### 2.1 Error Handling Infrastructure
Refactor `ParseError` to use C strings:

```cpp
// Before (parse_error.hpp)
struct ParseError {
    std::string message;
    std::string context_line;
    std::string hint;
};

// After
struct ParseError {
    char* message;        // Allocated from pool
    char* context_line;   // Allocated from pool
    char* hint;           // Allocated from pool
    Pool* pool;           // Memory owner
};
```

#### 2.2 Source Tracker
Replace std::vector with ArrayList:

```cpp
// Before (source_tracker.hpp)
std::vector<size_t> line_starts_;

// After
ArrayList* line_starts_;  // Stores size_t* values
```

#### 2.3 Input Context
Replace std::string with StrBuf:

```cpp
// Before (input-context.hpp)
std::string owned_source_;

// After
char* owned_source_;
size_t owned_source_len_;
Pool* pool_;
```

### Phase 3: Format/Output Code (Week 4)

**Priority**: Medium (HTML output formatting)

This phase covers non-LaTeX formatting code:
- `format-utils.cpp` - Element type dispatcher
- `html_encoder.cpp` - HTML escaping
- `html_writer.cpp` - HTML tag generation
- `html_generator.cpp` - HTML document generation

### Phase 4: Radiant Engine (Week 5-6)

**Priority**: Separate track (CSS layout engine)

The Radiant CSS engine uses std::* for:
- Rule indexing (`std::unordered_map<std::string, std::vector<IndexedRule>>`)
- Candidate collection during matching

Consider:
1. Creating specialized `CssRuleIndex` structure using `HashMap` and `ArrayList`
2. Linear search for small rule sets, HashMap for larger ones

### Phase 5: LaTeX Formatting (Week 7-8)

**Priority**: Lower (LaTeX→HTML conversion)

This phase involves heavier refactoring of LaTeX-related code:
- `latex_packages.cpp/hpp` - Symbol tables and package system
- `latex_generator.cpp/hpp` - Counter/label/length management  
- `latex_docclass.cpp/hpp` - Document class configuration
- `latex_assets.cpp/hpp` - Asset file handling
- `latex_hyphenation.cpp/hpp` - Hyphenation patterns
- `latex_picture.cpp/hpp` - Picture environment rendering

**Strategy**:
1. Static symbol tables → Sorted arrays with bsearch
2. Small dynamic maps (counters, lengths) → ArrayList with linear search
3. Larger maps (hyphenation patterns) → HashMap

---

## Testing Strategy

### Unit Tests
Keep `std::*` usage in test code (`test/*.cpp`) for convenience:
- Test files are not part of the runtime
- std::* provides easier test assertion and debugging
- GTest framework already uses std::* heavily

### Regression Testing
After each phase:
```bash
make test-lambda-baseline    # Lambda core functionality
make test-radiant-baseline   # Radiant layout (after Phase 4)
```

### Memory Testing
Run with memory tracking enabled:
```bash
# Add to test runs
LAMBDA_MEM_TRACK=1 ./lambda.exe test.ls
```

---

## Risk Assessment

| Risk | Mitigation |
|------|------------|
| Memory leaks from manual management | Use pool/arena allocation; add cleanup in destructors |
| Performance regression | Profile before/after; ArrayList and HashMap are O(1) average |
| API breakage | Keep function signatures stable; change only internal implementation |
| Increased code complexity | Use consistent patterns; document memory ownership |

---

## Success Criteria

1. **No std::* includes** in files under `lambda/` (except test utilities)
2. **All tests pass** after migration
3. **No memory leaks** detected by tracking
4. **Binary size reduction** (expected ~5-10% from removing libstdc++ dependency)
5. **Performance maintained** or improved

---

## Appendix: Quick Reference

### StrBuf vs std::string
```cpp
// std::string
std::string s = "hello";
s += " world";
s.length();
s.c_str();

// StrBuf
StrBuf* s = strbuf_create("hello");
strbuf_append_str(s, " world");
s->length;
s->str;
strbuf_free(s);
```

### ArrayList vs std::vector
```cpp
// std::vector<int*>
std::vector<int*> v;
v.push_back(new int(42));
v[0];
v.size();

// ArrayList
ArrayList* v = arraylist_new();
int* val = malloc(sizeof(int)); *val = 42;
arraylist_append(v, val);
arraylist_get(v, 0);
v->length;
arraylist_free(v);
```

### HashMap vs std::unordered_map
```cpp
// std::unordered_map<std::string, int>
std::unordered_map<std::string, int> m;
m["key"] = 42;
m.find("key");

// HashMap (string-keyed)
typedef struct { const char* key; int value; } Entry;
HashMap* m = hashmap_new(sizeof(Entry), 0, 0, 0, hash_fn, cmp_fn, NULL, NULL);
Entry e = {"key", 42};
hashmap_set(m, &e);
Entry* found = (Entry*)hashmap_get(m, &(Entry){"key", 0});
hashmap_free(m);
```

---

## Timeline Summary

| Phase | Scope | Duration | Priority |
|-------|-------|----------|----------|
| 1 | Lambda Script Core | 1 week | High |
| 2 | Input Parsers | 2 weeks | Medium |
| 3 | Format/Output (non-LaTeX) | 1 week | Medium |
| 4 | Radiant Engine | 2 weeks | Separate |
| 5 | LaTeX Formatting | 2 weeks | Lower |

**Total estimated effort**: 8-10 weeks for full migration (Phases 1-5)

**Recommended approach**: Complete Phase 1 first, then Phase 2. Phases 3-5 can be done incrementally as part of other improvements. Phase 5 (LaTeX) can be deferred indefinitely if LaTeX formatting is not performance-critical.
