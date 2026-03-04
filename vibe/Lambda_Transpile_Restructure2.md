# Lambda Transpiler — Structural Efficiency Proposal

## Overview

This document proposes structural improvements to the Lambda transpiler pipeline to make it leaner, more configuration-driven, and more efficient. The three primary objectives are:

1. **Reduce `lambda.h` size** — minimize the header embedded in JIT-compiled code
2. **O(1) system function lookup** — replace linear scan with hashmap
3. **Configuration-driven code generation** — replace inline-coded dispatch tables with data-driven metadata

These changes complement the earlier refactoring proposal ([Lambda_Transpile_Restructure.md](Lambda_Transpile_Restructure.md)) which focused on box/unbox uniformity and dual-version function generation.

### Current State

| File | Lines | Role |
|------|-------|------|
| `lambda/lambda.h` | 1,061 | C header embedded into every JIT module via `lambda-embed.h` (3,393 lines xxd) |
| `lambda/lambda.hpp` | 423 | C++ Item class, extends `lambda.h` + moved type/target/name definitions |
| `lambda/lambda-path.h` | 84 | Full Path/PathMeta struct definitions, path enums, macros, and Path API (new, Phase 2) |
| `lambda/lambda-data.hpp` | 650 | Full data structures, used by C++ runtime only |
| `lambda/build_ast.cpp` | ~7,200 | AST builder with 144 sys_func entries, **hashmap lookup (O(1))** |
| `lambda/transpile.cpp` | 8,002 | C code generator with scattered dispatch tables |
| `lambda/mir.c` | ~990 | JIT compiler with ~417-entry import resolver, **hashmap lookup (O(1))** |

---

## 1. Reduce `lambda.h` — JIT Header Diet

### Motivation

`lambda.h` is embedded byte-for-byte into every JIT module via `xxd -i lambda.h > lambda-embed.h`. C2MIR parses this header before compiling any transpiled code. A smaller header means:

- **Faster JIT startup** — less text for C2MIR to parse per module
- **Reduced memory** — embedded array is 4,224 lines / ~50KB currently
- **Simpler JIT ABI surface** — only what JIT code actually needs is exposed
- **Fewer C2MIR compatibility issues** — C2MIR is a simplified C compiler; less surface area = fewer edge cases

### What JIT Code Actually Needs

The transpiled C code (`transpile_ast_root` output) uses `lambda.h` for:

| Category | Needed by JIT | Examples |
|----------|---------------|---------|
| TypeId enum | Yes | `LMD_TYPE_INT`, `LMD_TYPE_STRING`, etc. |
| Item boxing macros | Yes | `i2it()`, `s2it()`, `d2it()`, `ITEM_NULL`, etc. |
| Forward type declarations | Yes | `typedef struct Map Map;` — JIT only passes opaque pointers |
| `Context` struct | Yes | Accessed via `rt->consts[index]` |
| Runtime function declarations | Yes | `extern Item fn_add(Item a, Item b);` etc. |
| Ret* structs + constructors | Yes | `RetItem`, `RetBool`, `ri_ok()`, etc. |
| `BoolEnum` / `Bool` | Yes | `BOOL_FALSE`, `BOOL_TRUE`, `BOOL_ERROR` |
| Math `extern` declarations | Yes | `extern double sin(double)` etc. |
| Container struct **full definitions** | **No** — JIT code never accesses fields of `Range`, `List`, `Map`, etc. directly (but the unbox macros cast `Item` → pointer) | `struct Range { ... }`, `struct List { ... }`, etc. |
| `String` / `Symbol` full definitions | **Partial** — JIT accesses `String.chars` and `String.len` in a few places | `str->chars`, `str->len` |
| `Function` struct definition | **No** — JIT passes `Function*` opaquely to `fn_call*` | `struct Function { ... }` |
| `Path` / `Target` / `Name` full definitions | **No** — JIT never accesses path internals | `struct Path { ... }`, `struct Target { ... }` |
| `PathMeta` / `LPathSegmentType` / `PathScheme` | **No** — only used by path.c runtime | Full enum + struct |
| `get_type_name()` inline function | **No** — never called from JIT code | 30-line switch statement |
| C++ `#ifdef __cplusplus` blocks | **No** — JIT compiles C only | Various C++ guards |
| `Target` / `Url` / `Pool` / `Arena` types | **Partial** — only forward decls needed | `typedef struct Target Target;` |

### Proposed Split

#### `lambda.h` (JIT-facing, minimal — target: ~500 lines)

Keep only:
```c
// 1. Primitive type setup (size_t, bool for C2MIR)
// 2. Math extern declarations
// 3. EnumTypeId (the full enum, ~30 values)
// 4. TypeId typedef, BoolEnum, Bool
// 5. Item typedef (uint64_t), ITEM_* constants, boxing macros (i2it, s2it, etc.)
// 6. Forward type declarations ONLY:
typedef struct String String;
typedef struct Symbol Symbol;
typedef struct Binary Binary;  // alias for String
typedef struct Range Range;
typedef struct List List;
typedef struct Array Array;
typedef struct ArrayInt ArrayInt;
typedef struct ArrayInt64 ArrayInt64;
typedef struct ArrayFloat ArrayFloat;
typedef struct Map Map;
typedef struct VMap VMap;
typedef struct Element Element;
typedef struct Object Object;
typedef struct Function Function;
typedef struct Decimal Decimal;
typedef struct Type Type;
typedef struct TypePattern TypePattern;
typedef struct Path Path;
typedef struct LambdaError LambdaError;
typedef struct Pool Pool;
typedef struct Url Url;
typedef struct Arena Arena;
typedef struct _ArrayList ArrayList;

// 7. DateTime typedef + error macros
// 8. Container unboxing macros (it2map, it2list, etc. — just casts, don't need struct defs)
// 9. p2it, err2it, it2err helpers
// 10. Ret* structs + constructor helpers
// 11. Context struct (needed for rt->consts access)
// 12. Runtime function declarations (fn_*, pn_*, list_*, map_*, etc.)
// 13. const_* macros
```

Remove from `lambda.h`:
- **Full struct definitions** for `Range`, `List`, `ArrayInt`, `ArrayInt64`, `ArrayFloat`, `Map`, `Object`, `Element`, `Container`, `Function` — replace with forward declarations
- **`String` and `Symbol` full struct definitions** — move to `lambda-jit.h` or keep minimal accessor (see below)
- **`Path` full struct definition** and all path API functions
- **`PathMeta`, `LPathSegmentType`, path macros** (`PATH_GET_SEG_TYPE`, etc.)
- **`Target` full struct + all target API functions**
- **`Name` struct + `name_equal` inline function**
- **`get_type_name()` inline function** — make it an `extern` declaration, implement in `lambda-data.cpp`
- **`Type` struct definition** — forward declare only; JIT never inspects `Type` fields
- **C++ conditional blocks** (`#ifdef __cplusplus`) — entire blocks can be stripped since JIT is C-only

#### `String` / `Symbol` Access from JIT

The transpiled code does access `str->chars` and `str->len` in a few places (string constants, `fn_to_cstr`). Two options:

**Option A (Recommended): Keep minimal String/Symbol in `lambda.h`**
```c
// Minimal definition for JIT access to string data
typedef struct String {
    uint32_t len;
    uint8_t is_ascii;
    char chars[];
} String;

typedef struct Symbol {
    uint32_t len;
    void* ns;  // opaque to JIT
    char chars[];
} Symbol;
```
This is only ~10 lines and avoids needing accessor functions.

**Option B: Opaque + accessor functions**
```c
typedef struct String String;
extern uint32_t string_len(String* s);
extern const char* string_chars(String* s);
```
Cleaner abstraction but adds function call overhead for every string length check.

#### `get_type_name()` — Move to Implementation File

Currently a 30-line `static inline` function in `lambda.h`. JIT code never calls it. Move to `lambda-data.cpp` as a regular function, declare as `extern` in `lambda.h` only for C++ callers:

```c
// lambda.h (JIT header)
// nothing — JIT doesn't need get_type_name

// lambda-data.hpp or lambda-data.cpp
const char* get_type_name(TypeId type_id);  // regular function
```

For C++ code that currently calls `get_type_name()`, the declaration moves to `lambda-data.hpp` or a new `lambda-types.h`.

#### Move Full Definitions to `lambda.hpp` (existing file)

`lambda.hpp` (currently 342 lines) already extends `lambda.h` with the C++ `Item` class. It is the natural home for full struct definitions that C++ runtime code needs but JIT code does not:

```cpp
// lambda.hpp — C++ runtime header (already exists)
#pragma once
#include "lambda.h"

// Full Container base
struct Container { ... };

// Full Range, List, Array*, Map, Element, Object definitions
struct Range { ... };
struct List { ... };
// ... etc.

// Full Function struct
struct Function { ... };

// Full Path, PathMeta, Target, Name definitions
struct Path { ... };
// ... etc.

// get_type_name() declaration
const char* get_type_name(TypeId type_id);

// Item class definition (already here)
struct Item { ... };
```

No new file needed — the existing `lambda.hpp` already plays this role. C files (`mir.c`, `path.c`) that need full struct definitions will need a C-compatible subset; for those, the struct definitions can be guarded with `#ifndef __cplusplus` in `lambda.h` (as they already are today) or extracted to a small C-only header `lambda-c-structs.h` only for the few `.c` files that need them. Most runtime code is C++ and already includes `lambda.hpp`.

### Size Estimate

| Section | Current lines | After cleanup |
|---------|--------------|---------------|
| Preamble (types, bool, math) | ~70 | ~70 |
| EnumTypeId + TypeId | ~40 | ~40 |
| `get_type_name()` | 30 | 0 (removed) |
| BoolEnum / Type | ~15 | ~15 |
| Forward decls (replaces full structs) | 0 | ~30 |
| Full struct definitions | ~230 | 0 (moved out) |
| Path/Target/Name definitions + API | ~200 | 0 (moved out) |
| Item macros + box/unbox | ~50 | ~50 |
| Ret* structs + helpers | ~120 | ~120 |
| Context + runtime fn decls | ~300 | ~250 (trim Path API) |
| **Total** | **~1,268** | **~550** |

Embedded `lambda-embed.h` would shrink from ~50KB to ~22KB (56% reduction).

### Migration Steps

1. Create `lambda-structs.h` with full struct definitions extracted from `lambda.h`
2. Replace full struct definitions in `lambda.h` with forward declarations
3. Move `get_type_name()` to `lambda-data.cpp`, add extern declaration where needed
4. Remove Path/Target/Name full definitions and API from `lambda.h`
5. Remove C++ conditional blocks from `lambda.h`
6. For C files (`mir.c`, `path.c`) that need full struct definitions, provide a C-only subset header or keep minimal definitions inline
7. Rebuild `lambda-embed.h` (automatic via Makefile)
8. Run full test suite to verify JIT code still compiles and executes correctly

---

## 2. O(1) System Function Lookup

### Problem

The `sys_funcs[]` table in `build_ast.cpp` has **144 entries**. Three functions scan it linearly:

| Function | Scans | Called when |
|----------|-------|------------|
| `get_sys_func_info(name, arg_count)` | 2× full scan (exact + variadic fallback) | Every function call expression |
| `get_sys_func_for_method(name, arg_count, type)` | 1× full scan | Every method-style call `obj.method()` |
| `is_sys_func_name(name, name_len)` | 1× full scan | Name resolution (shadowing check) |

For a script with N function calls, this is O(N × 144) comparisons during AST building. While not a bottleneck today, it's easy to fix and establishes a pattern for the import resolver (Issue below).

### Proposed Solution

Use `lib/hashmap.h` (already in the project and already included by `build_ast.cpp`) to build an O(1) lookup table at initialization.

#### Key Design: Composite Key

Since system functions are overloaded by name + arg_count (e.g., `"symbol"` with 1 or 2 args), the hashmap key must be a composite:

```cpp
// Composite key for sys func lookup
typedef struct SysFuncKey {
    const char* name;
    int arg_count;
} SysFuncKey;

// Hash: combine name hash with arg_count
static uint64_t sys_func_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const SysFuncKey* key = (const SysFuncKey*)item;
    uint64_t h = hashmap_xxhash3(key->name, strlen(key->name), seed0, seed1);
    // mix in arg_count
    h ^= (uint64_t)(key->arg_count + 1) * 0x9E3779B97F4A7C15ULL;
    return h;
}

// Compare: name string equality + arg count match
static int sys_func_compare(const void* a, const void* b, void* udata) {
    const SysFuncKey* ka = (const SysFuncKey*)a;
    const SysFuncKey* kb = (const SysFuncKey*)b;
    if (ka->arg_count != kb->arg_count) return 1;
    return strcmp(ka->name, kb->name);
}
```

#### Hashmap Entry

```cpp
typedef struct SysFuncEntry {
    SysFuncKey key;         // name + arg_count (hashmap key)
    SysFuncInfo* info;      // pointer into sys_funcs[] array
} SysFuncEntry;
```

#### Initialization (once at startup)

```cpp
static HashMap* sys_func_map = NULL;
static HashMap* sys_func_name_set = NULL;  // name-only set for is_sys_func_name()

void init_sys_func_map() {
    int count = sizeof(sys_funcs) / sizeof(sys_funcs[0]);
    sys_func_map = hashmap_new(sizeof(SysFuncEntry), count * 2, 0, 0,
                               sys_func_hash, sys_func_compare, NULL, NULL);

    for (int i = 0; i < count; i++) {
        SysFuncEntry entry = {
            .key = { .name = sys_funcs[i].name, .arg_count = sys_funcs[i].arg_count },
            .info = &sys_funcs[i]
        };
        hashmap_set(sys_func_map, &entry);
    }

    // Separate name-only set for is_sys_func_name()
    sys_func_name_set = hashmap_new(sizeof(const char*), count, 0, 0,
                                     name_hash, name_compare, NULL, NULL);
    for (int i = 0; i < count; i++) {
        hashmap_set(sys_func_name_set, &sys_funcs[i].name);
    }
}
```

#### Lookup Functions (O(1))

```cpp
SysFuncInfo* get_sys_func_info(StrView* name, int arg_count) {
    // Stack-allocate a null-terminated copy for hashmap lookup
    char name_buf[64];
    int len = name->length < 63 ? name->length : 63;
    memcpy(name_buf, name->str, len);
    name_buf[len] = '\0';

    SysFuncEntry lookup = { .key = { .name = name_buf, .arg_count = arg_count } };
    const SysFuncEntry* found = hashmap_get(sys_func_map, &lookup);
    if (found) return found->info;

    // Fallback: variadic functions (arg_count == -1)
    lookup.key.arg_count = -1;
    found = hashmap_get(sys_func_map, &lookup);
    return found ? found->info : NULL;
}

bool is_sys_func_name(const char* name, int name_len) {
    char name_buf[64];
    int len = name_len < 63 ? name_len : 63;
    memcpy(name_buf, name, len);
    name_buf[len] = '\0';
    const char* key = name_buf;
    return hashmap_get(sys_func_name_set, &key) != NULL;
}
```

#### Method Lookup

`get_sys_func_for_method()` needs name + arg_count + type filtering. The hashmap handles name + arg_count; the method-eligibility and type-compatibility checks remain as post-lookup validation:

```cpp
SysFuncInfo* get_sys_func_for_method(StrView* method_name, int method_arg_count, TypeId obj_type_id) {
    int total_arg_count = method_arg_count + 1;
    SysFuncInfo* info = get_sys_func_info(method_name, total_arg_count);
    if (!info) return NULL;
    if (!info->is_method_eligible) return NULL;
    if (info->first_param_type != LMD_TYPE_ANY && obj_type_id != LMD_TYPE_ANY) {
        if (info->first_param_type != obj_type_id) {
            // Check numeric type compatibility
            if (info->first_param_type == LMD_TYPE_NUMBER) {
                if (obj_type_id != LMD_TYPE_INT && obj_type_id != LMD_TYPE_INT64 &&
                    obj_type_id != LMD_TYPE_FLOAT && obj_type_id != LMD_TYPE_DECIMAL) {
                    return NULL;
                }
            } else {
                return NULL;
            }
        }
    }
    return info;
}
```

### Bonus: `func_list[]` in `mir.c` — Same Treatment

The MIR import resolver (`mir.c`) has a `func_list[]` with ~417 entries, also linearly scanned on every unresolved symbol during JIT linking. Apply the same hashmap pattern:

```c
static HashMap* jit_func_map = NULL;

void init_jit_func_map() {
    int len = sizeof(func_list) / sizeof(func_list[0]);
    jit_func_map = hashmap_new(sizeof(func_obj_t), len * 2, 0, 0,
                                func_name_hash, func_name_compare, NULL, NULL);
    for (int i = 0; i < len; i++) {
        hashmap_set(jit_func_map, &func_list[i]);
    }
}

fn_ptr resolve_func(const char* name) {
    func_obj_t lookup = { .name = (char*)name };
    const func_obj_t* found = hashmap_get(jit_func_map, &lookup);
    return found ? found->func : NULL;
}
```

This makes JIT linking O(1) per symbol instead of O(N × 417).

---

## 3. Configuration-Driven Code Generation

### Problem

The transpiler has several parallel dispatch tables that must be kept in manual sync:

| Table | File | Entries | Purpose |
|-------|------|---------|---------|
| `sys_funcs[]` | `build_ast.cpp` | 144 | Name → SysFuncInfo (AST building) |
| `type_box_table[]` | `transpile.cpp` | 22 | TypeId → C type, box/unbox functions |
| `native_math_funcs[]` | `transpile.cpp` | ~30 | Lambda name → C math function |
| `native_binary_funcs[]` | `transpile.cpp` | 2 | Lambda name → C binary func variant |
| `func_list[]` | `mir.c` | ~417 | Symbol name → function pointer (JIT import) |
| `get_type_name()` | `lambda.h` | ~30 | TypeId → display name (inline switch) |
| `get_container_unbox_fn()` | `transpile.cpp` | ~10 | TypeId → unbox function name |
| Runtime `fn_*`/`pn_*` declarations | `lambda.h` | ~200 | Extern function declarations |

Adding a new system function requires editing **at least 3 files** (`build_ast.cpp`, `lambda.h`, `mir.c`) and often 5+ locations. Adding a new type requires editing 6+ tables. This is error-prone — the Runtime Issues audit found multiple inconsistencies (e.g., `type_info[LMD_TYPE_RANGE]` having wrong name).

### Proposed Architecture

#### 3.1 Unified System Function Registry

Consolidate `sys_funcs[]` metadata and `func_list[]` import entries into a single data structure from which both the AST builder and JIT import resolver derive their tables.

**New file: `lambda/sys_func_registry.h`**

```c
// sys_func_registry.h — single source of truth for all system functions
#pragma once
#include "lambda.h"

// Complete metadata for a system function
typedef struct SysFuncDef {
    // Identity
    SysFunc id;                 // enum value (SYSFUNC_LEN, SYSPROC_PRINT, etc.)
    const char* lambda_name;    // Lambda-facing name ("len", "print", etc.)
    int arg_count;              // -1 for variadic

    // Type info
    Type* return_type;
    TypeId first_param_type;    // for method eligibility (LMD_TYPE_ANY = any)

    // Flags
    bool is_proc;
    bool is_overloaded;
    bool is_method_eligible;
    bool can_raise;

    // C code generation metadata
    const char* c_func_name;    // C function name ("fn_len", "pn_print", etc.)
    CRetType c_ret_type;
    CArgConvention c_arg_conv;

    // JIT import: pointer to actual runtime function
    void* func_ptr;             // same as func_list entry — resolved at compile time

    // Native math optimization (NULL if not applicable)
    const char* native_c_name;  // direct C math equivalent ("sin", "floor", etc.)
    bool native_returns_float;
} SysFuncDef;
```

**Registration file: `lambda/sys_func_registry.cpp`**

```cpp
#include "sys_func_registry.h"

// Single source of truth:
SysFuncDef sys_func_defs[] = {
    {SYSFUNC_LEN, "len", 1, &TYPE_INT64, LMD_TYPE_ANY,
     false, false, true, false,
     "fn_len", C_RET_INT64, C_ARG_ITEM,
     (void*)fn_len, NULL, false},

    {SYSFUNC_ABS, "abs", 1, &TYPE_ANY, LMD_TYPE_ANY,
     false, false, true, false,
     "fn_abs", C_RET_ITEM, C_ARG_ITEM,
     (void*)fn_abs, "fabs", true},  // native math: fabs()

    {SYSFUNC_SIN, "math_sin", 1, &TYPE_ANY, LMD_TYPE_ANY,
     false, false, true, false,
     "fn_math_sin", C_RET_ITEM, C_ARG_ITEM,
     (void*)fn_math_sin, "sin", true},  // native math: sin()

    // ... all 144 entries ...
};
```

**Benefits:**
- Adding a system function = adding one entry to one table
- `func_list[]` in `mir.c` is auto-derived from `sys_func_defs[].func_ptr`
- `native_math_funcs[]` is subsumed by `native_c_name` field
- AST builder, transpiler, and JIT resolver all read from the same source

#### 3.2 Unified Type Metadata Registry

Similarly, consolidate type-related tables into a single registry:

**New file: `lambda/type_registry.h`**

```c
typedef struct TypeDef {
    TypeId type_id;
    const char* display_name;   // "int", "string", "range", etc.
    const char* c_type;         // "int64_t", "String*", "Range*", etc.

    // Boxing/unboxing metadata
    const char* unbox_fn;       // "it2i", "it2map", etc.
    const char* box_fn;         // "i2it", "push_d", etc.
    const char* const_box_fn;   // "const_l2it", etc.
    const char* zero_value;     // "0", "NULL", etc.

    // Runtime info
    size_t struct_size;         // sizeof(Range), sizeof(List), etc. (0 for scalars)
    bool is_container;          // heap-allocated container
    bool is_numeric;            // INT, INT64, FLOAT, DECIMAL
    bool is_gc_managed;         // tracked by GC
} TypeDef;
```

This replaces:
- `type_box_table[]` in `transpile.cpp`
- `type_info[]` in `lambda-data.cpp`
- `get_type_name()` in `lambda.h`
- `get_container_unbox_fn()` in `transpile.cpp`

#### 3.3 Native Optimization Table (Extended)

Currently, native math optimization is scattered across multiple if/else chains in `transpile.cpp` (~200 lines of special-case code for abs, sign, floor, ceil, round, neg, min, max). Consolidate into the `SysFuncDef` with richer metadata:

```c
typedef struct NativeOptInfo {
    const char* c_float_fn;     // native C function for float args (e.g., "fabs")
    const char* c_int_fn;       // native C function for int args (e.g., "fn_abs_i"), or NULL
    const char* c_binary_fn;    // native C function for 2-arg (e.g., "fn_pow_u"), or NULL
    bool returns_float;         // true if native version returns double
    bool int_is_identity;       // true if integer version is identity (floor, ceil, round for int)
} NativeOptInfo;
```

The transpiler then replaces ~100 lines of case-by-case `strcmp()` checks with a single generic routine:

```cpp
void transpile_sys_func_call(Transpiler* tp, AstSysFuncNode* sys_fn, AstNode* args) {
    SysFuncDef* def = sys_fn->def;  // already resolved during AST building

    // Try native optimization
    if (def->native_opt && args && args->type && is_numeric_type(args->type->type_id)) {
        NativeOptInfo* opt = def->native_opt;
        TypeId arg_type = args->type->type_id;

        // Integer identity optimization (floor/ceil/round on int)
        if (opt->int_is_identity && is_integer_type(arg_type)) {
            emit_box_open(tp, LMD_TYPE_INT);
            strbuf_append_str(tp->code_buf, "(int64_t)(");
            transpile_expr(tp, args);
            strbuf_append_str(tp->code_buf, "))");
            return;
        }

        // Integer-specific function
        if (opt->c_int_fn && is_integer_type(arg_type)) {
            strbuf_append_str(tp->code_buf, "i2it(");
            strbuf_append_str(tp->code_buf, opt->c_int_fn);
            strbuf_append_str(tp->code_buf, "((int64_t)(");
            transpile_expr(tp, args);
            strbuf_append_str(tp->code_buf, ")))");
            return;
        }

        // Float-specific function (direct C math)
        if (opt->c_float_fn) {
            strbuf_append_str(tp->code_buf, "push_d(");
            strbuf_append_str(tp->code_buf, opt->c_float_fn);
            strbuf_append_str(tp->code_buf, "((double)(");
            transpile_expr(tp, args);
            strbuf_append_str(tp->code_buf, ")))");
            return;
        }
    }

    // Generic fallback: fn_name(arg1, arg2, ...)
    emit_sys_func_call_generic(tp, def, args);
}
```

This is the same logic as the current scattered `strcmp()` chains, but driven from data rather than code.

---

## 4. Additional Suggestions

### 4.1 `transpile-mir.cpp` — Direct MIR Transpiler

`lambda/transpile-mir.cpp` (6,732 lines) is the **direct AST→MIR transpiler** — an alternative compilation path that bypasses C code generation entirely. Instead of Lambda → C text → C2MIR → MIR, it goes Lambda → AST → MIR instructions directly via the MIR API (`MIR_new_insn`, `MIR_new_call_insn`, etc.).

This is a substantial, actively developed component with:
- Full variable scoping with hashmap-based scope stacks
- Closure support with environment capture
- Import resolution and cross-module linking
- Box/unbox helpers, TCO, pipe context
- `run_script_mir()` entry point that handles the complete execution pipeline

**Note:** The Runtime Issues audit (Issue #15) described an earlier, much smaller version of this file (496 lines) that was a non-functional stub. The file has since been significantly expanded into a working direct MIR backend.

**Opportunity:** As the direct MIR path matures, it can benefit from the same configuration-driven approach proposed in §3 — the `SysFuncDef` registry and `TypeDef` registry would be shared between both `transpile.cpp` (C path) and `transpile-mir.cpp` (direct MIR path), ensuring consistency and reducing duplication between the two backends.

### 4.2 Separate `transpile.cpp` Sys Func Handling

The system function call transpilation in `transpile.cpp` (lines ~4900–5160) mixes generic dispatch with special-case optimizations. With the configuration-driven approach (§3), this can be cleanly separated:

| Responsibility | Current | Proposed |
|----------------|---------|----------|
| Generic `fn_name(args)` call | Inline in `transpile_expr` | `emit_sys_func_call_generic()` |
| Native math optimization | ~100 lines of `strcmp()` | Data-driven from `NativeOptInfo` |
| VMap special cases | Inline `if (SYSFUNC_VMAP_NEW)` | Move to a `transpile_vmap_call()` helper or encode in registry |
| Bitwise arg handling | Inline `emit_bitwise_arg()` | Driven by `c_arg_conv == C_ARG_NATIVE` |

### 4.3 MIR Import Resolver: Auto-Generate from Registry

Currently `func_list[]` in `mir.c` has ~417 manually maintained entries. With the unified registry, the non-system entries (C math, memset, etc.) remain static, but the ~200 `fn_*/pn_*` entries can be auto-generated:

```c
// mir.c — init function adds all sys_func_defs entries
void init_jit_imports() {
    for (int i = 0; i < sys_func_def_count; i++) {
        if (sys_func_defs[i].func_ptr) {
            register_import(sys_func_defs[i].c_func_name, sys_func_defs[i].func_ptr);
        }
    }
    // Also register C library functions (sin, cos, memset, etc.)
    register_import("sin", (void*)sin);
    register_import("cos", (void*)cos);
    // ... static entries ...
}
```

This eliminates the need to manually add a `func_list` entry every time a new system function is created.

### 4.4 Transpiler Module Split Opportunity

With configuration-driven sys func handling, `transpile.cpp` could be meaningfully split:

| New file | Lines | Content |
|----------|-------|---------|
| `transpile-call.cpp` | ~800 | Function call transpilation (user + sys + method dispatch) |
| `transpile-sys.cpp` | ~300 | System function special cases and native optimization |
| `transpile-type.cpp` | ~300 | Type boxing/unboxing, type_box_table, coercion |
| `transpile.cpp` | ~6,600 | Everything else (expressions, statements, loops, etc.) |

This makes each component independently reviewable and testable.

### 4.5 Const Pool Macro Cleanup

The `#define const_d2it(index)` macros in `lambda.h` expand to `d2it(rt->consts[index])` — which requires `rt` to be in scope. The imported module override pattern (lines 7696–7720 in `transpile.cpp`) manually `#undef`s and redefines all 10 const macros. This is fragile.

A cleaner approach: make the const accessor take an explicit context pointer:

```c
// lambda.h
#define const_get(ctx, index)  ((ctx)->consts[index])
#define const_d2it(index)      d2it(const_get(rt, index))
```

Then imported modules just redefine `rt`:
```c
#define rt _mod_rt  // point to module-local context
```

No need to redefine every individual const macro.

---

## Implementation Progress

### Phase 1: Quick Wins — ✅ COMPLETED (4 Mar 2026)

All three Phase 1 tasks have been implemented and verified (582/582 baseline tests pass).

| Task | Status | Details |
|------|--------|--------|
| Move `get_type_name()` out of `lambda.h` | ✅ Done | Replaced 30-line `static inline` with `extern "C"` declaration. Implementation moved to `lambda-data.cpp`. lambda.h: 1,268 → 1,239 lines. lambda-embed.h: 4,224 → 4,115 lines (~2.5% JIT parse reduction). |
| Hashmap for `sys_funcs[]` lookup | ✅ Done | Two hashmaps in `build_ast.cpp`: (1) `sys_func_map` with composite key (name, arg_count) for `get_sys_func_info()` and `get_sys_func_for_method()`; (2) `sys_func_name_set` with name-only key for `is_sys_func_name()`. Lazily initialized on first access. All 3 lookup functions now O(1). |
| Hashmap for `func_list[]` in `mir.c` | ✅ Done | `func_map` hashmap built from ~417 `func_list[]` entries during `jit_init()`. `import_resolver()` now does O(1) hashmap lookup instead of O(n) linear scan. Dynamic imports still use linear scan (small table, max 256). |

**Key implementation notes:**
- `get_type_name()` required `extern "C"` linkage because `lambda-data.hpp` wraps its includes in an `extern "C"` block, causing linkage mismatch if declared without explicit C linkage.
- The sys_func hashmap uses xxhash3 with arg_count mixed into the hash via golden-ratio multiplication (`h ^= (arg_count + 2) * 0x9E3779B97F4A7C15ULL`).
- The func_map in mir.c is initialized once in `jit_init()` before any JIT compilation occurs.

## Implementation Roadmap (Remaining)

### ~~Phase 1: Quick Wins~~ — Done

### ~~Phase 2: Header Diet~~ — ✅ COMPLETED (4 Mar 2026)

| Task | Status | Details |
|------|--------|---------|
| Move TypeKind enum to `lambda.hpp` | ✅ Done | Not needed by JIT, moved to runtime-only header |
| Move Path/PathMeta structs + enums/macros + Path API to `lambda-path.h` | ✅ Done | New C-compatible header for full path definitions. Included by `path.c` (C) and `lambda.hpp` (C++) |
| Move Target struct + enums + API to `lambda.hpp` | ✅ Done | JIT never accesses Target internals |
| Move Name struct + name_equal to `lambda.hpp` | ✅ Done | JIT never uses Name |
| Keep Function struct in `lambda.h` | ✅ Kept | JIT code accesses `->closure_field_count`, `->flags`, `->ptr` directly |
| Keep Container/Range/List/Array*/Map/Object/Element in `lambda.h` | ✅ Kept | JIT code accesses struct fields for for-loops and direct field access |
| Add `fn_exists` declaration back to `lambda.h` | ✅ Done | Transpiler emits `fn_exists()` calls via generic sys_func path |
| Fix `mir.c` extern declarations for `target_equal` | ✅ Done | Added extern since full Target API moved out |
| Fix `path.c` to include `lambda-path.h` + target externs | ✅ Done | `fn_exists()` in path.c calls `item_to_target`/`target_exists`/`target_free` |

**Key findings:**
- **Function struct must stay in lambda.h** — the transpiler generates code that directly accesses `_fn->closure_field_count`, `_fn->flags = FN_FLAG_BOXED_RET`, and `_fn->ptr`. This was incorrectly assumed to be opaque in the original proposal.
- **Container struct definitions must stay** — JIT code accesses `Range.start/end`, `List.items/length`, `Map.data`, `Element.items/length/data`, `String.chars/len` etc. The original proposal's claim that these were not accessed by JIT was wrong.
- **`fn_exists` is called from JIT code** — the transpiler emits `fn_exists(path)` through the generic sys_func name emission (`"fn_" + name`), requiring its declaration in lambda.h.

**Size results:**
| Metric | Before Phase 2 | After Phase 2 | Reduction |
|--------|----------------|---------------|-----------|
| `lambda.h` | 1,239 lines | 1,061 lines | 178 lines (14.4%) |
| `lambda-embed.h` (JIT payload) | 4,115 lines | 3,393 lines | 722 lines (17.5%) |
| `lambda.hpp` | 342 lines | 423 lines | +81 lines (moved in) |
| `lambda-path.h` (new) | — | 84 lines | New file |

The original proposal estimated ~56% reduction by also moving Container/Function/String structs. Since those must stay (JIT accesses their fields), the actual reduction is 14–18% for this phase. Further reduction would require changing the transpiler to use accessor functions instead of direct field access.

### Phase 3: Unified Registry (3–5 days) — Future

| Task | Effort | Impact |
|------|--------|--------|
| Design `sys_func_registry.h` | Small | API design |
| Create `sys_func_registry.cpp` with all 144 entries | Medium | Single source of truth |
| Refactor `build_ast.cpp` to use registry | Medium | Remove `sys_funcs[]` duplication |
| Refactor `mir.c` to auto-derive imports | Medium | Remove `func_list[]` duplication |
| Create `type_registry.h` / `.cpp` | Medium | Unified type metadata |
| Refactor transpiler to use type registry | Medium | Remove scattered tables |

### Phase 4: Transpiler Cleanup (2–3 days) — Future

| Task | Effort | Impact |
|------|--------|--------|
| Data-driven native optimization | Medium | Replace ~200 lines of strcmp chains |
| Extract `transpile-call.cpp` | Small | Better modularity |
| Const macro cleanup | Small | Simpler module import pattern |
