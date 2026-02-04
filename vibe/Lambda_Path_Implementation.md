# Lambda Path Implementation Proposal

> **Version**: Draft 0.1
> **Status**: Proposal
> **Last Updated**: February 2026

## Summary

Implement Lambda paths as **segmented symbols** by extending the existing `Symbol` type with a `parent` pointer. Paths become linked chains of symbols, sharing the existing name pool infrastructure.

---

## 1. Core Concept

A path is a symbol with ancestry:

```
file.etc.hosts
     ↓
┌─────────┐    ┌─────────┐    ┌─────────┐    ┌──────┐
│ "hosts" │───▶│ "etc"   │───▶│ "file"  │───▶│ ROOT │
│ parent  │    │ parent  │    │ parent  │    │ null │
└─────────┘    └─────────┘    └─────────┘    └──────┘
```

**Key insight**: No new data type needed—just add `parent` to Symbol.

---

## 2. Changes Required

### 2.1 Extend Symbol Structure

**File**: `lambda/lambda-data.hpp`

```cpp
struct Symbol {
    const char* name;      // Interned via name_pool
    Symbol* parent;        // NULL = normal symbol, non-NULL = path segment
};
```

### 2.2 Add Root Scheme Symbols

**File**: `lambda/lambda-data.cpp` (or new `lambda/path.cpp`)

```cpp
static Symbol ROOT_SENTINEL = { nullptr, nullptr };

// Predefined scheme roots (initialized once at startup)
Symbol* SYM_FILE;   // file://
Symbol* SYM_HTTP;   // http://
Symbol* SYM_HTTPS;  // https://
Symbol* SYM_SYS;    // sys://
Symbol* SYM_DOT;    // . (relative)
Symbol* SYM_DDOT;   // .. (parent)

void init_path_roots() {
    SYM_FILE  = make_root_symbol("file");
    SYM_HTTP  = make_root_symbol("http");
    SYM_HTTPS = make_root_symbol("https");
    SYM_SYS   = make_root_symbol("sys");
    SYM_DOT   = make_root_symbol(".");
    SYM_DDOT  = make_root_symbol("..");
}

Symbol* make_root_symbol(const char* name) {
    Symbol* s = arena_alloc(sizeof(Symbol));
    s->name = name_pool_intern(name);
    s->parent = &ROOT_SENTINEL;
    return s;
}
```

### 2.3 Path Construction API

**File**: `lambda/path.cpp` (new)

```cpp
// Append segment to path
Symbol* path_append(Symbol* parent, const char* segment) {
    Symbol* s = arena_alloc(sizeof(Symbol));
    s->name = name_pool_intern(segment);
    s->parent = parent;
    return s;
}

// Build path from string: "file.etc.hosts"
Symbol* path_from_string(const char* path_str);

// Convert path to OS path: "/etc/hosts"
void path_to_os_string(Symbol* path, StrBuf* out);

// Get scheme: file, http, sys, etc.
const char* path_get_scheme(Symbol* path);

// Check if symbol is a path
bool is_path(Symbol* sym) {
    return sym->parent != nullptr;
}
```

### 2.4 Parser Integration

**File**: `lambda/build_ast.cpp`

Update path literal parsing to construct Symbol chains:

```cpp
// When parsing: file.etc.hosts
// Build: Symbol("hosts", parent=Symbol("etc", parent=SYM_FILE))

Item parse_path_literal(TSNode node) {
    // 1. Get first segment, look up root scheme
    // 2. Chain remaining segments via path_append()
    // 3. Return as Item with TypeId::Symbol
}
```

### 2.5 Path Operations

**File**: `lambda/path.cpp`

```cpp
// Concatenation: path ++ "segment"
Symbol* path_concat(Symbol* base, const char* segment);
Symbol* path_concat_path(Symbol* base, Symbol* suffix);

// Decomposition
const char* path_basename(Symbol* path);      // Last segment
Symbol* path_parent(Symbol* path);            // Parent path
Symbol* path_scheme_root(Symbol* path);       // Root scheme symbol

// Comparison
bool path_equals(Symbol* a, Symbol* b);
bool path_starts_with(Symbol* path, Symbol* prefix);
```

---

## 3. Integration Points

| Component | Change |
|-----------|--------|
| `lambda-data.hpp` | Add `parent` field to Symbol |
| `lambda-data.cpp` | Initialize root scheme symbols |
| `build_ast.cpp` | Parse path literals into Symbol chains |
| `lambda-eval.cpp` | Handle path operations (`++`, member access) |
| `transpile-mir.cpp` | Generate code for path operations |
| `input/*.cpp` | Use path symbols for file/URL resolution |
| `print.cpp` | Format paths for output |

---

## 4. Memory Model

| Allocation | Strategy |
|------------|----------|
| Symbol structs | Arena (long-lived) |
| Name strings | Name pool (interned, shared) |
| Temp path buffers | Stack `StrBuf` |

**No `std::string` or `std::vector`**—use `lib/strbuf.h` and `lib/arraylist.h`.

---

## 5. Example Transformations

### Source → Runtime Representation

```lambda
let p = file.etc.hosts;
```

Becomes:

```
Item {
    type: Symbol,
    value: Symbol {
        name: "hosts",
        parent: Symbol {
            name: "etc",
            parent: Symbol {
                name: "file",
                parent: &ROOT_SENTINEL
            }
        }
    }
}
```

### Path Concatenation

```lambda
let base = file.home.user;
let full = base ++ "documents" ++ "file.txt";
```

Runtime:

```
path_append(path_append(base, "documents"), "file.txt")
→ Symbol("file.txt", parent=Symbol("documents", parent=base))
```

---

## 6. Implementation Order

1. **Phase 1**: Extend Symbol with `parent` field
2. **Phase 2**: Add root scheme symbols and `path_append()`
3. **Phase 3**: Update parser to build path chains
4. **Phase 4**: Implement `++` operator for paths
5. **Phase 5**: Add path decomposition functions
6. **Phase 6**: Integrate with input system for file/URL access
7. **Phase 7**: Update print/format for path display

---

## 7. Testing Strategy

| Test | Description |
|------|-------------|
| `test_path_construction` | Build paths, verify parent chains |
| `test_path_concat` | `++` operator with strings and paths |
| `test_path_decomposition` | `basename`, `parent`, `scheme` |
| `test_path_to_os` | Convert to OS paths |
| `test_path_parsing` | Parser creates correct Symbol chains |
| `test_path_io` | Read files via path notation |

---

## 8. Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| Breaking existing Symbol usage | Keep `parent=NULL` for non-path symbols |
| Memory overhead | Symbols are small; arena allocation is cheap |
| Name pool pressure | Paths share interned names; minimal overhead |
| Circular references | Impossible—paths are singly-linked |

---

## 9. Files to Create/Modify

| File | Action |
|------|--------|
| `lambda/path.hpp` | **New**: Path API declarations |
| `lambda/path.cpp` | **New**: Path implementation |
| `lambda/lambda-data.hpp` | **Modify**: Add `parent` to Symbol |
| `lambda/lambda-data.cpp` | **Modify**: Init root symbols |
| `lambda/build_ast.cpp` | **Modify**: Parse path literals |
| `lambda/lambda-eval.cpp` | **Modify**: Eval path operations |
| `lambda/print.cpp` | **Modify**: Format paths |
| `test/test_path.cpp` | **New**: Path unit tests |

---

## 10. Estimated Effort

| Phase | Effort |
|-------|--------|
| Symbol extension + roots | 2 hours |
| Path construction API | 4 hours |
| Parser integration | 4 hours |
| Concatenation operator | 2 hours |
| Decomposition functions | 2 hours |
| Input system integration | 4 hours |
| Testing | 4 hours |
| **Total** | ~22 hours |

---

## See Also

- [Lambda Path Mapping](Lambda_Path.md) - Full design document
- [Lambda System Info](Lambda_Sysinfo.md) - `sys` scheme reference
