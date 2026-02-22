# C+ Coding Convention

Lambda adopts a **C+** coding convention — a pragmatic subset of C++ that enhances C with selected C++ features while preserving C's simplicity, control, and ABI compatibility.

The guiding philosophy: **use C++ where it makes C better, but never surrender control over memory, error flow, or binary layout.**

A key practical driver: Lambda uses **MIR** (Medium Internal Representation) for JIT compilation, and MIR only supports C. All runtime functions callable from JIT-compiled code must have a C-compatible ABI, making `extern "C"` boundaries and C-safe data layouts a hard requirement — not just a stylistic choice.

---

## 1. Use C++ Features to Enhance C

Lambda freely uses C++ features that improve safety, readability, or ergonomics without imposing runtime costs or hidden complexity.

### Inline member functions on structs

Structs gain lightweight accessor methods — no vtable, no overhead:

```cpp
struct Item {
    union { ... };

    inline TypeId type_id() {
        if (this->_type_id) { return this->_type_id; }
        if (this->item) { return *((TypeId*)this->item); }
        return LMD_TYPE_NULL;
    }

    inline double get_double() { ... }
    inline int64_t get_int64() { ... }
};
```

```cpp
// lambda/lambda.hpp — Map struct with member functions
struct Map : Container {
    Item get(Item key);
    bool has_field(const char* name);
};
```

### Struct inheritance as structural extension

C++ `struct : Base` is used for layout-compatible field extension — not for OOP hierarchies:

```cpp
struct Container { TypeId type_id; uint16_t ref_cnt; ... };
struct List   : Container { Item* items; int count; ... };
struct Map    : Container { ShapeEntry* shape; ... };
struct Element: List      { ... };   // element is a list + attributes
```

```cpp
// radiant/view.hpp
struct DomNode    { ... };
struct DomText    : DomNode { ... };
struct DomElement : DomNode { ... };
struct ViewBlock  : ViewSpan { ... };
```

### Templates (sparingly, utility-only)

Templates are used only for type-safe replacements of C macros — never for generic programming:

```cpp
// radiant/view.hpp
template<typename T, typename U>
inline auto max(T a, U b) -> typename std::common_type<T,U>::type {
    return a > b ? a : b;
}
```

### Other C++ features used

| Feature            | Usage                                                                                 |
| ------------------ | ------------------------------------------------------------------------------------- |
| `auto`             | Return types in template helpers, range-for loops                                     |
| `constexpr`        | Compile-time constants                                                                |
| `static_assert`    | Compile-time assertions                                                               |
| `initializer_list` | Fluent builder APIs                                                                   |
| References (`&`)   | Function parameters where pointer would be cumbersome                                 |
| Namespaces         | Sparingly, mainly for forward-declaring external libs: `namespace re2 { class RE2; }` |
| RAII destructors   | Stack-allocated builders/contexts with automatic cleanup (no `new`/`delete`)          |

---

## 2. Maintain C-Compatible ABI

All public APIs and data structures maintain a C-compatible binary interface. This is essential because MIR JIT-compiled code calls into the runtime via C function signatures.

### `extern "C"` on all C-facing APIs

```cpp
// lambda/lambda-mem.cpp — callable from MIR JIT C code
extern "C" void* heap_calloc(size_t size, TypeId type_id);
extern "C" String* heap_strcpy(char* src, int len);
extern "C" void set_runtime_error_no_trace(...);
```

### `.h` vs `.hpp` file convention

| Extension | Semantics |
|-----------|-----------|
| `.h` | Pure C header — safe for MIR JIT compilation and C callers |
| `.hpp` | C++ header — may use member functions, inheritance, templates |

The `.h` headers use the standard guard:

```c
// lib/str.h, lib/arraylist.h, lib/hashmap.h, lib/mempool.h, lambda/lambda.h, etc.
#ifdef __cplusplus
extern "C" {
#endif

// ... C declarations ...

#ifdef __cplusplus
}
#endif
```

### Dual struct definitions for C/C++ interop

`lambda.h` provides C-only struct layouts, `lambda.hpp` provides C++ struct layouts with member functions — both share the same binary layout:

```c
// lambda/lambda.h — C view
#ifndef __cplusplus
typedef uint64_t Item;      // plain 64-bit value
struct List { TypeId type_id; ...; Item* items; int count; };
#endif
```

```cpp
// lambda/lambda.hpp — C++ view (same binary layout, plus methods)
struct Item {
    union { ...; uint64_t _type_id:8; ... };
    inline TypeId type_id() { ... }
    inline double get_double() { ... }
};
```

---

## 3. Custom Memory Management — No `new`/`delete`

Lambda uses its own allocators for total control over memory usage, allocation patterns, and lifecycle. **C++ `new` and `delete` are never used.**

### Three-tier allocation

| Tier | Allocator | Purpose | Speed |
|------|-----------|---------|-------|
| **Pool** | `pool_alloc()`, `pool_calloc()` | Runtime heap objects (containers, strings) | Fast (rpmalloc-backed) |
| **Arena** | `arena_alloc()` | Input parsing data (bump-pointer, zero per-allocation overhead) | O(1) |
| **NamePool** | `name_pool_create_len()` | Deduplicated structural identifiers (field names, symbols) | Amortized O(1) |

```cpp
// pool allocation — lambda/lambda-mem.cpp
context->heap = (Heap*)calloc(1, sizeof(Heap));   // top-level only
context->heap->pool = pool_create();
void* data = pool_alloc(heap->pool, size);         // all runtime objects

// arena allocation — lambda/mark_builder.hpp
// "Arena allocation is FAST (bump-pointer, O(1)) with zero per-allocation
//  overhead. All arena data lives until the arena is reset/destroyed."
List*    list_arena(Arena* arena);
Map*     map_arena(Arena* arena);
Element* elmt_arena(Arena* arena);
```

### Reference counting (not GC, not RAII smart pointers)

Ref counts are embedded directly in data structs as bitfields:

```c
// lambda/lambda.h
struct String    { uint32_t len:22; uint32_t ref_cnt:10; char chars[]; };
struct Container { TypeId type_id; ...; uint16_t ref_cnt; };
```

Manual increment/decrement with recursive free on zero:

```cpp
// lambda/lambda-mem.cpp
void free_map_item(Map* map) {
    // decrement ref_cnt, recursively free children when zero
}
```

### What to use instead of C++ allocation

| Don't | Do |
|-------|-----|
| `new MyStruct()` | `pool_calloc(pool, sizeof(MyStruct))` or stack-allocate |
| `delete obj` | `pool_free(pool, obj)` or let arena/pool lifetime manage it |
| `std::make_shared<T>()` | Embed `ref_cnt` in the struct, manage manually |
| `std::unique_ptr<T>` | Stack-allocate with RAII destructor, or arena-allocate |

---

## 4. No C++ Exception Handling

Errors are handled as **return values**, not exceptions. There is no `try`/`catch`/`throw` anywhere in the codebase. Error propagation requires explicit checking and unwinding through the call stack.

### Error sentinel values

```cpp
// lambda/lambda.hpp
extern Item ItemNull;    // null/absent value
extern Item ItemError;   // error sentinel

// Functions return ItemError on failure
Item fn_error(EvalContext* ctx, ...) {
    set_runtime_error(ctx, ...);
    return ItemError;
}
```

### `GUARD_ERROR` macros for propagation

Instead of exceptions bubbling up automatically, errors are checked and propagated explicitly:

```cpp
// lambda/lambda.hpp
#define GUARD_ERROR1(a) \
    if (get_type_id(a) == LMD_TYPE_ERROR) return (a)

#define GUARD_ERROR2(a, b) \
    if (get_type_id(a) == LMD_TYPE_ERROR) return (a); \
    if (get_type_id(b) == LMD_TYPE_ERROR) return (b)

// Usage — lambda/lambda-eval.cpp
Item fn_join(EvalContext* ctx, Item left, Item right) {
    GUARD_ERROR2(left, right);
    // ... proceed with operation
}
```

### Three-state `Bool` (not C++ `bool`)

```c
// lambda/lambda.h
typedef enum { BOOL_FALSE=0, BOOL_TRUE=1, BOOL_ERROR=2 } BoolEnum;
typedef uint8_t Bool;

// lambda/lambda-eval.cpp
Bool is_truthy(Item item);   // can return BOOL_FALSE, BOOL_TRUE, or BOOL_ERROR
```

### Structured error codes

```c
// lambda/lambda-error.h — HTTP-inspired error code ranges
enum LambdaErrorCode {
    // 1xx — Syntax errors
    ERR_SYNTAX_BASE = 100,
    // 2xx — Semantic errors
    ERR_SEMANTIC_BASE = 200,
    // 3xx — Runtime errors
    ERR_RUNTIME_ERROR = 300,
    ERR_NULL_REFERENCE,
    ERR_INDEX_OUT_OF_BOUNDS,
    // 4xx — I/O errors
    ERR_IO_BASE = 400,
    // 5xx — Internal errors
    ERR_INTERNAL_BASE = 500,
};

struct LambdaError {
    LambdaErrorCode code;
    const char* message;
    SourceLocation location;
    StackFrame* stack_trace;
    const char* help;
    LambdaError* cause;           // chained errors
};
```

### Error handling patterns

| Don't | Do |
|-------|-----|
| `throw std::runtime_error(msg)` | `return fn_error(ctx, ERR_RUNTIME_ERROR, msg)` |
| `try { ... } catch (...)` | `GUARD_ERROR1(result); // propagate error up` |
| `throw` in constructor | Return `NULL` or set error on context |
| Exception-based stack unwinding | Walk the call stack via FP chain for trace |

---

## 5. No Virtual Member Functions (No vtable)

C++ virtual functions inject a hidden vtable pointer into every object and require heap allocation. Lambda avoids this entirely.

### TypeId-based dispatch

Every container begins with a `TypeId` field at offset 0. Runtime dispatch is an explicit switch:

```c
// lambda/lambda.h
enum EnumTypeId {
    LMD_TYPE_NULL, LMD_TYPE_BOOL, LMD_TYPE_INT, LMD_TYPE_FLOAT,
    LMD_TYPE_STRING, LMD_TYPE_LIST, LMD_TYPE_MAP, LMD_TYPE_ELEMENT,
    // ... 30+ types
};

struct Container { TypeId type_id; ... };   // first field = type tag
```

```c
// lambda/lambda.h
static inline const char* get_type_name(TypeId type_id) {
    switch (type_id) {
        case LMD_TYPE_NULL:    return "null";
        case LMD_TYPE_BOOL:    return "bool";
        case LMD_TYPE_STRING:  return "string";
        // ... all 30+ types
    }
}
```

### Tagged union for value representation

The core `Item` type is a 64-bit tagged union — type tag + value in a single word:

```cpp
// lambda/lambda.hpp
struct Item {
    union {
        void*      item;
        int64_t    int_val:56;
        uint64_t   _type_id:8;
        Container* container;
        List*      list;
        Map*       map;
        Element*   element;
        // ...
    };
};
```

### Manual function-pointer vtable (rare, explicit)

The **only** vtable-like structure in the entire codebase is `VMapVtable` — an explicit, manually-managed function pointer table for polymorphic map backends:

```cpp
// lambda/lambda.hpp
struct VMapVtable {
    Item       (*get)(void* data, Item key);
    void       (*set)(void* data, Item key, Item value);
    int64_t    (*count)(void* data);
    ArrayList* (*keys)(void* data);
    // ...
};
```

This is C-style polymorphism: visible, controllable, and without hidden costs.

---

## 6. Custom Lib Types Instead of C++ STL

Lambda provides its own collection and string types under `lib/`. These are C-compatible, use custom allocators, and have no hidden allocations.

| Custom Type | Replaces | Header | Notes |
|-------------|----------|--------|-------|
| `str_*()` functions | `<cstring>` | `lib/str.h` | Length-bounded, NULL-tolerant, C99 |
| `StrBuf` | `std::string` (mutable) | `lib/strbuf.h` | Growable buffer with pool allocator |
| `StrView` | `std::string_view` | `lib/strview.h` | Non-owning `(ptr, len)` pair |
| `String` | `std::string` (immutable) | `lambda/lambda.h` | Ref-counted, length-prefixed, flexible array member |
| `ArrayList` | `std::vector<void*>` | `lib/arraylist.h` | `void** data`, auto-resizing |
| `HashMap` | `std::unordered_map` | `lib/hashmap.h` | Robin Hood hashing, custom allocator slots |
| `Pool` | `std::pmr::memory_resource` | `lib/mempool.h` | Opaque pool via rpmalloc |
| `Arena` | — | `lib/arena.h` | Bump-pointer allocator |

### C++ STL usage (permitted where conforming)

C++ STL **can** be used when it conforms to all the rules above:

- Does not trigger `new`/`delete` behind the scenes in hot paths
- Does not introduce exception-based control flow
- Does not require virtual dispatch
- Does not break C ABI for exposed interfaces

In practice, this means utility headers like `<type_traits>`, `<initializer_list>`, `<cstdint>`, `<cstring>`, and `<utility>` are fine. Standard containers (`std::vector`, `std::string`, `std::map`) are **not** used.

---

## 7. Naming Conventions

| Element | Convention | Examples |
|---------|-----------|----------|
| Functions | `snake_case` | `pool_alloc()`, `heap_strcpy()`, `is_truthy()`, `fn_join()` |
| Types / Structs / Classes | `PascalCase` | `EvalContext`, `MarkBuilder`, `ViewElement`, `LambdaError` |
| Constants / Enums / Macros | `UPPER_SNAKE_CASE` | `LMD_TYPE_NULL`, `ERR_RUNTIME_ERROR`, `GUARD_ERROR1`, `STR_NPOS` |
| Member variables (private) | `snake_case_` (trailing underscore) | `input_`, `pool_`, `arena_`, `name_pool_` |
| Member functions (C++ classes) | `camelCase` | `createName()`, `createElement()`, `createString()` |
| File names | `snake_case` | `lambda_eval.cpp`, `mark_builder.hpp`, `shape_pool.cpp` |
| Inline comments | Start lowercase | `// process the next token` |

---

## Summary: What C++ Features Are Used vs. Avoided

| **Used** | **Avoided** |
|----------|-------------|
| Inline member functions on structs | Virtual member functions / vtable |
| Struct inheritance (layout extension) | Deep OOP class hierarchies |
| Templates (utility only) | Generic / template metaprogramming |
| `auto`, `constexpr` | `new` / `delete` |
| Move semantics, `= delete` | Smart pointers (`shared_ptr`, `unique_ptr`) |
| RAII destructors (stack-allocated) | `try` / `catch` / `throw` |
| References (`&`) | RTTI (`dynamic_cast`, `typeid`) |
| `initializer_list` | STL containers (`std::vector`, `std::string`, `std::map`) |
| Namespaces (sparingly) | Operator overloading (sparingly) |
| C++ `static_cast` | C++ streams (`iostream`, `cout`) |

---

## Prior Art

Lambda's C+ convention is not unique — it draws from a well-established tradition of projects that use C++ as "a better C" rather than embracing the full language.

| Project / Guide | Author / Org | Key Overlap | Link |
|----------------|-------------|-------------|------|
| **Orthodox C++** | Branimir Karadžić (bgfx) | Near-identical: no exceptions, no RTTI, no STL, no virtuals, custom allocators. The closest published manifesto to Lambda's approach. | [gist.github.com/bkaradzic/2e39896bc7d8c34e042b](https://gist.github.com/bkaradzic/2e39896bc7d8c34e042b) |
| **Handmade Hero** | Casey Muratori | "C-style C++" — structs with methods, no exceptions, no STL, manual memory management, data-oriented design. | [handmadehero.org](https://handmadehero.org/) |
| **id Software** (Doom, Quake) | John Carmack | C++ as "better C", custom allocators, no exceptions, minimal STL. Carmack's coding style influenced a generation of game engines. | [fabiensanglard.net/doom3](https://fabiensanglard.net/doom3/) |
| **EASTL** | Electronic Arts | EA replaced the STL entirely because standard allocator model lacked control — the same motivation behind Lambda's `ArrayList`, `HashMap`, etc. | [github.com/electronicarts/EASTL](https://github.com/electronicarts/EASTL) |
| **LLVM / Clang** | LLVM Project | No exceptions, no RTTI, custom bump-pointer allocators (`BumpPtrAllocator`), custom ADT containers. Uses more templates than Lambda. | [llvm.org/docs/CodingStandards.html](https://llvm.org/docs/CodingStandards.html) |
| **Google C++ Style Guide** | Google | Bans exceptions, restricts some features, though still uses STL containers. | [google.github.io/styleguide/cppguide.html](https://google.github.io/styleguide/cppguide.html) |
| **Linux kernel** | Linus Torvalds | Pure C, but uses the same struct-embedding pattern for "inheritance" (`container_of` macro) and TypeId-like dispatch. | [kernel.org/doc/html/latest/process/coding-style.html](https://www.kernel.org/doc/html/latest/process/coding-style.html) |
| **SQLite** | D. Richard Hipp | Pure C, clean ABI, custom allocators (`sqlite3_malloc`), error codes instead of exceptions. A model for C-based runtime design. | [sqlite.org/codeofethics.html](https://www.sqlite.org/codeofethics.html) |
