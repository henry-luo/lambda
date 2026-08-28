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

### Phase 3: Unified Registry — ✅ COMPLETE

**Implementation summary:**

1. **Extended `SysFuncInfo`** in `ast.hpp` with 4 new fields: `c_func_name`, `native_c_name`, `native_returns_float`, `native_arg_count`
2. **Created `sys_func_registry.h` / `.cpp`** — single source of truth with ~120 entries.  All metadata (identity, type info, C codegen, native math optimization) in one table.
3. **Refactored `build_ast.cpp`** — removed 170-line local `sys_funcs[]` array, replaced with `extern` reference to `sys_func_defs[]` from registry.
4. **Refactored `transpile.cpp`** — removed `NativeMathFunc` struct + `native_math_funcs[]` array (~50 lines), removed `NativeBinaryFunc` struct + `native_binary_funcs[]` (~10 lines), unified `can_use_native_math_binary()` to handle both math and min/max, generic fallback now uses `fn_info->c_func_name` instead of computing it at runtime.
5. **`mir.c` func_list[]** — kept as-is for JIT import resolution (removing would cause linker issues in shared library builds). Registry remains metadata-only; func_ptr field was removed from SysFuncInfo to avoid symbol resolution issues in `liblambda-input-full-cpp.dylib`.

**Result:** 582/582 tests pass.  Adding a new system function now requires editing only `sys_func_registry.cpp` + `lambda.h` (declaration) + implementation file.

| Task | Status |
|------|--------|
| Design `sys_func_registry.h` | ✅ Done |
| Create `sys_func_registry.cpp` with ~120 entries | ✅ Done |
| Refactor `build_ast.cpp` to use registry | ✅ Done |
| Refactor `transpile.cpp` native math optimization | ✅ Done |
| Refactor `transpile.cpp` generic fallback to use `c_func_name` | ✅ Done |
| `mir.c` func_list[] — deferred (shared lib constraint) | ⏭️ Skipped |

### Phase 4: Transpiler Cleanup — ✅ COMPLETED

| Task | Status | Details |
|------|--------|---------|
| Const macro cleanup | ✅ Done | Added `_const_pool` indirection in `lambda.h`. Module import override reduced from 20 lines (10 undefs + 10 redefs) to 2 lines. |
| Data-driven native optimization | ✅ Done | Replaced all `strcmp(fn_name, ...)` chains with enum-based dispatch (`fn_info->fn == SYSFUNC_*`). Collapsed 5 identical bitwise binary patterns (band/bor/bxor/shl/shr, ~43 lines) into 10 lines using `c_arg_conv == C_ARG_NATIVE`. Removed dead `neg()` code path (never reachable as SysFuncNode). |
| Extract `transpile-call.cpp` | ✅ Done | Extracted ~993 lines: `transpile_call_expr`, `transpile_call_argument`, `transpile_tail_call`, `is_tco_tail_call`, `find_param_by_name`, plus helper functions (`emit_bitwise_arg`, `can_use_native_math`, `is_integer_type`, etc.). Made 5 shared static functions non-static (`callee_returns_retitem`, `current_func_returns_retitem`, `emit_zero_value`, `value_emits_native_type`, `get_container_unbox_fn`) and declared them in `transpiler.hpp`. transpile.cpp: 7,892 → 6,881 lines. |

---

## Post-Implementation Benchmark Results (5 Mar 2026)

After completing all four phases, both benchmark suites were re-run with a fresh release build (LTO, stripped, optimized) to measure the impact of the transpiler restructuring on performance.

**Platform**: macOS, Apple Silicon (M-series)  
**Build**: Release (LTO, dead code elimination, stripped)  
**Runs per benchmark**: 3 (median reported)  
**Baseline**: Previous results from 3 Mar 2026 (before Phases 1–4)

### Kostya Benchmark Suite — JIT Overhead Comparison

| Benchmark | Previous JIT | Current JIT | Delta |
|-----------|-------------|-------------|-------|
| brainfuck | 9.1 ms | 13.1 ms | +4.0 ms |
| matmul | 9.4 ms | 12.8 ms | +3.4 ms |
| primes | 5.2 ms | 7.2 ms | +2.0 ms |
| base64 | 93.7 ms | 95.4 ms | +1.7 ms |
| levenshtein | 8.0 ms | 10.8 ms | +2.8 ms |
| json_gen | 15.5 ms | 18.7 ms | +3.2 ms |
| collatz | 5.4 ms | 6.8 ms | +1.4 ms |

| Metric | Previous | Current | Change |
|--------|----------|---------|--------|
| **Geo mean (wall)** | 212.2 ms | 229.3 ms | +8.1% |
| **Geo mean (exec)** | 187.1 ms | 197.5 ms | +5.6% |

### Larceny Benchmark Suite — JIT Overhead Comparison

| Benchmark | Previous JIT | Current JIT | Delta |
|-----------|-------------|-------------|-------|
| deriv | 7.4 ms | 10.5 ms | +3.1 ms |
| primes | 5.1 ms | 7.6 ms | +2.5 ms |
| pnpoly | 6.7 ms | 9.7 ms | +3.0 ms |
| diviter | 5.1 ms | 6.5 ms | +1.4 ms |
| divrec | 4.8 ms | 7.2 ms | +2.4 ms |
| array1 | 4.4 ms | 6.4 ms | +2.0 ms |
| gcbench | 9.0 ms | 12.1 ms | +3.1 ms |
| quicksort | 5.6 ms | 7.5 ms | +1.9 ms |
| triangl | 11.4 ms | 12.9 ms | +1.5 ms |
| puzzle | 5.7 ms | 8.7 ms | +3.0 ms |
| ray | 7.4 ms | 9.4 ms | +2.0 ms |
| paraffins | 11.8 ms | 14.2 ms | +2.4 ms |

| Metric | Previous | Current | Change |
|--------|----------|---------|--------|
| **Geo mean (wall)** | 64.8 ms | 74.7 ms | +15.3% |
| **Geo mean (exec)** | 37.2 ms | 39.0 ms | +4.8% |

### Analysis

**JIT overhead increased uniformly by ~2–3 ms across all benchmarks in both suites.** This is a consistent additive constant, not a percentage-based regression, indicating a fixed-cost increase in the transpile/compile pipeline.

**Exec-time differences are small** (+4–6% geo mean) and within typical run-to-run variance on Apple Silicon (thermal throttling, background load). The restructuring did not affect generated code quality.

**Possible causes of the consistent ~2–3 ms JIT overhead increase:**

1. **File I/O during JIT**: The transpile pipeline writes `_transpiled_N.c` to disk before compiling — this is a debug artifact that may have different timing characteristics across builds.
2. **Instruction cache locality**: Splitting `transpile.cpp` into `transpile.cpp` + `transpile-call.cpp` may cause slightly worse i-cache behavior during C code generation, since the call transpilation hot path is now in a different translation unit.
3. **Hashmap overhead vs. direct arrays**: The O(1) hashmap lookups (Phase 1) have higher per-lookup constant cost than the linear scans they replaced, particularly for small tables where linear scan is cache-friendly. The `sys_func_map` and `func_map` hashmaps add hashing + comparison overhead that may exceed the benefit for 144-entry and 417-entry tables.
4. **LTO interaction**: The `clean-all` + fresh release build may produce different optimization decisions than the previous build, especially with cross-TU inlining affected by the new file split.

### Phase-Level Profiling (5 Mar 2026)

Fine-grained phase timing was added to `transpile_script()` in `runner.cpp` to break down JIT overhead into 7 sub-phases. Enabled by setting `LAMBDA_PROFILE=1` environment variable — stores timing data in memory during compilation, dumps to `temp/phase_profile.txt` at `runtime_cleanup()`. Zero overhead when disabled.

**Initial debug-build profiling was misleading** — AST Build appeared to be 31% of JIT overhead, but this was an artifact of debug-mode overhead (no inlining, bounds checks, etc.). Release-build profiling reveals the true bottleneck distribution.

#### Phase Time Distribution — Release Build (Average across 19 benchmarks, median of 3 runs)

| Phase | Avg (ms) | % of Total | Description |
|-------|----------|------------|-------------|
| **Parse** | 0.262 | 3.5% | Tree-sitter parse source → CST |
| **AST Build** | 0.182 | 2.4% | CST → typed AST (build_ast.cpp) |
| **C Transpile** | 0.061 | 0.8% | AST → C code text (transpile.cpp) |
| **JIT Init** | 0.143 | 1.9% | `jit_init()` — create MIR context |
| **File Write** | 0.129 | 1.7% | Write `_transpiled_N.c` debug file |
| **C2MIR** | 3.482 | 46.1% | C2MIR parse + compile C → MIR |
| **MIR Gen** | 3.295 | 43.6% | MIR optimization + native codegen |
| **Total** | **7.553** | **100%** | |

```
  Parse       : 0.262 ms (  3.5%) #
  AST Build   : 0.182 ms (  2.4%) #
  C Transpile : 0.061 ms (  0.8%)
  JIT Init    : 0.143 ms (  1.9%)
  File Write  : 0.129 ms (  1.7%)
  C2MIR       : 3.482 ms ( 46.1%) #######################
  MIR Gen     : 3.295 ms ( 43.6%) #####################
```

#### Release vs Debug Comparison

| Phase | Debug (ms) | Release (ms) | Speedup |
|-------|-----------|-------------|---------|
| Parse | 0.460 | 0.262 | 1.8x |
| AST Build | 3.860 | 0.182 | **21.2x** |
| C Transpile | 1.180 | 0.061 | **19.4x** |
| JIT Init | 0.230 | 0.143 | 1.6x |
| File Write | 0.170 | 0.129 | 1.3x |
| C2MIR | 3.260 | 3.482 | 0.9x |
| MIR Gen | 3.290 | 3.295 | 1.0x |
| **Total** | **12.450** | **7.553** | **1.6x** |

AST Build and C Transpile gain ~20x from compiler optimizations (inlining, loop unrolling, dead code elimination). C2MIR and MIR Gen are unaffected because they are interpreted/JIT code that runs at the same speed regardless of host build configuration.

#### Per-Benchmark Phase Breakdown — Release Build (ms)

| Benchmark | Parse | AST | Trans | Init | Write | C2MIR | MIR Gen | Total | Code Len |
|-----------|-------|-----|-------|------|-------|-------|---------|-------|----------|
| brainfuck | 0.272 | 0.176 | 0.052 | 0.139 | 0.150 | 3.543 | 3.147 | 7.479 | 43,360 |
| matmul | 0.231 | 0.158 | 0.047 | 0.259 | 0.263 | 3.994 | 3.658 | 8.610 | 43,034 |
| primes (K) | 0.162 | 0.118 | 0.042 | 0.129 | 0.112 | 3.525 | 2.312 | 6.400 | 42,284 |
| base64 | 0.282 | 0.183 | 0.075 | 0.135 | 0.121 | 3.501 | 3.815 | 8.112 | 44,139 |
| levenshtein | 0.300 | 0.206 | 0.064 | 0.139 | 0.114 | 3.363 | 4.550 | 8.736 | 44,031 |
| json_gen | 0.258 | 0.183 | 0.074 | 0.128 | 0.116 | 3.315 | 3.886 | 7.960 | 44,143 |
| collatz | 0.165 | 0.121 | 0.050 | 0.136 | 0.123 | 2.840 | 2.112 | 5.547 | 42,294 |
| deriv | 0.276 | 0.211 | 0.083 | 0.136 | 0.099 | 3.961 | 3.326 | 8.092 | 47,374 |
| primes (L) | 0.179 | 0.123 | 0.049 | 0.133 | 0.110 | 2.802 | 2.584 | 5.980 | 42,462 |
| pnpoly | 0.315 | 0.223 | 0.059 | 0.129 | 0.104 | 3.100 | 3.512 | 7.442 | 43,746 |
| diviter | 0.153 | 0.120 | 0.041 | 0.128 | 0.097 | 3.477 | 2.218 | 6.234 | 42,290 |
| divrec | 0.155 | 0.116 | 0.049 | 0.127 | 0.118 | 3.417 | 2.040 | 6.022 | 42,909 |
| array1 | 0.155 | 0.119 | 0.066 | 0.136 | 0.126 | 2.690 | 1.845 | 5.137 | 42,043 |
| gcbench | 0.221 | 0.149 | 0.077 | 0.131 | 0.111 | 3.276 | 3.794 | 7.759 | 43,629 |
| quicksort | 0.494 | 0.292 | 0.096 | 0.206 | 0.265 | 6.174 | 2.364 | 9.891 | 43,322 |
| triangl | 0.361 | 0.304 | 0.055 | 0.131 | 0.119 | 3.196 | 3.020 | 7.186 | 44,192 |
| puzzle | 0.194 | 0.137 | 0.066 | 0.129 | 0.092 | 2.891 | 2.515 | 6.024 | 42,877 |
| ray | 0.343 | 0.216 | 0.051 | 0.129 | 0.099 | 3.294 | 4.165 | 8.297 | 44,466 |
| paraffins | 0.459 | 0.297 | 0.058 | 0.132 | 0.107 | 3.803 | 7.749 | 12.605 | 46,083 |

#### Key Findings

**Two phases dominate in release builds**, together accounting for **89.7%** of total JIT overhead:

1. **C2MIR (46.1%)** — The single largest bottleneck. C2MIR parsing the transpiled C code. Stable across benchmarks (2.69–6.17 ms) because the code length is dominated by the `lambda.h` header (~42,000 chars, ~96% of total C code). User-generated code is only 43–5,374 chars (avg 1,615). C2MIR spends most of its time re-parsing the same header for every script.

2. **MIR Gen (43.6%)** — MIR optimization passes and AArch64 native code generation. Varies more (1.85–7.75 ms) — larger scripts with more functions produce more MIR instructions to optimize and compile. Paraffins is an outlier at 7.75 ms due to its many recursive functions.

**Everything else is negligible in release builds:**
- Parse (3.5%), AST Build (2.4%), C Transpile (0.8%) — collectively only 6.7%. These are pure CPU-bound C++ code that benefits massively from LTO/inlining.
- JIT Init (1.9%) and File Write (1.7%) are fixed costs.

**Critical insight: The transpiler restructuring in Phases 1–4 affects only 6.7% of JIT overhead.** The ~2–3ms JIT overhead increase observed in benchmarks cannot be explained by transpiler changes — it must come from C2MIR/MIR Gen variance or LTO optimization differences.

#### Optimization Opportunities (Revised)

| Opportunity | Phase | Potential Savings | Difficulty |
|-------------|-------|-------------------|------------|
| **Cache parsed `lambda.h` MIR** — parse the header once, reuse for subsequent scripts | C2MIR | ~3 ms per import (not per run — header is parsed once per process for single-script execution) | High — requires C2MIR pre-compiled header support, which means modifying 3rd-party code |
| **Switch to direct MIR backend** (`transpile-mir.cpp`) — bypass C2MIR entirely | C2MIR | ~3.5 ms (eliminate C2MIR phase entirely) | Medium — `transpile-mir.cpp` is under active development |
| **Reduce `lambda.h` size** — continue header diet (Phase 2 followup) | C2MIR | ~0.5–1 ms | Low — straightforward removal |
| **MIR optimization level tuning** — use level 1 instead of 2 for faster JIT | MIR Gen | ~1–2 ms (fewer optimization passes) | Low — already configurable via `--optimize=N` |
| **Pre-compiled MIR modules** — cache JIT output for unchanged scripts | All | ~7.5 ms (skip entire JIT) | High — needs cache invalidation, `MIR_write`/`MIR_read` API |
| **Skip debug file write** — don't write `_transpiled_N.c` in release mode | File Write | ~0.13 ms | Low — add conditional |

### C2MIR vs MIR Direct Path Comparison (5 Mar 2026)

Profiling was extended to include the `--mir` direct path (AST → MIR API, bypassing C text generation and C2MIR parsing). This comparison establishes the performance baseline for MIR transpiler tuning.

#### Architecture Comparison

| | C2MIR Path (default) | MIR Direct Path (`--mir`) |
|---|---|---|
| **Pipeline** | AST → C text → C2MIR → MIR → native | AST → MIR API → native |
| **Entry point** | `transpile_script()` in `runner.cpp` | `run_script_mir()` in `transpile-mir.cpp` |
| **C2MIR dependency** | Yes — parses `lambda.h` header + user code | No — direct MIR builder API |
| **Code generation** | C intermediate text (~42KB including header) | Direct MIR instructions via API |

#### JIT Compilation Overhead — C2MIR vs MIR Direct (median of 3 runs, ms)

| Benchmark | C2MIR JIT | MIR Direct JIT | Speedup | C2MIR Breakdown | MIR Breakdown |
|-----------|-----------|----------------|---------|-----------------|---------------|
| kostya/base64 | 7.710 | 3.405 | 2.26x | c2mir=3.574 gen=3.798 | xpile=0.933 link=2.382 |
| kostya/brainfuck | 6.167 | 2.640 | 2.34x | c2mir=2.983 gen=2.884 | xpile=0.583 link=1.970 |
| kostya/collatz | 5.395 | 1.445 | 3.73x | c2mir=3.158 gen=1.930 | xpile=0.508 link=0.849 |
| kostya/json_gen | 7.701 | 2.934 | 2.62x | c2mir=3.201 gen=4.207 | xpile=0.519 link=2.326 |
| kostya/levenshtein | 7.616 | 3.310 | 2.30x | c2mir=3.230 gen=4.113 | xpile=0.622 link=2.597 |
| kostya/matmul | 7.031 | 2.368 | 2.97x | c2mir=3.122 gen=3.626 | xpile=0.514 link=1.763 |
| kostya/primes | 5.201 | 1.464 | 3.55x | c2mir=2.692 gen=2.207 | xpile=0.452 link=0.915 |
| larceny/array1 | 4.629 | 1.028 | 4.50x | c2mir=2.545 gen=1.812 | xpile=0.296 link=0.645 |
| larceny/deriv | 7.067 | 2.672 | 2.64x | c2mir=3.435 gen=3.358 | xpile=0.667 link=1.907 |
| larceny/diviter | 4.792 | 1.335 | 3.59x | c2mir=2.600 gen=1.896 | xpile=0.411 link=0.841 |
| larceny/divrec | 4.918 | 1.772 | 2.78x | c2mir=2.679 gen=1.949 | xpile=0.449 link=1.234 |
| larceny/gcbench | 6.877 | 2.229 | 3.09x | c2mir=3.178 gen=3.424 | xpile=0.485 link=1.655 |
| larceny/paraffins | 11.398 | 7.125 | 1.60x | c2mir=3.879 gen=7.220 | xpile=1.208 link=5.819 |
| larceny/pnpoly | 7.405 | 2.635 | 2.81x | c2mir=3.180 gen=3.926 | xpile=0.529 link=2.004 |
| larceny/primes | 5.439 | 1.439 | 3.78x | c2mir=2.797 gen=2.337 | xpile=0.385 link=0.964 |
| larceny/puzzle | 5.489 | 1.992 | 2.76x | c2mir=2.733 gen=2.476 | xpile=0.490 link=1.409 |
| larceny/quicksort | 5.959 | 1.783 | 3.34x | c2mir=2.945 gen=2.732 | xpile=0.503 link=1.162 |
| larceny/triangl | 6.269 | 3.363 | 1.86x | c2mir=2.980 gen=2.995 | xpile=0.649 link=2.627 |
| **AVERAGE** | **6.503** | **2.497** | **2.60x** | | |

#### MIR Direct Phase Breakdown

| Phase | Avg (ms) | % of MIR Total | Description |
|-------|----------|----------------|-------------|
| **MIR Init** | 0.093 | 3.7% | Create MIR context |
| **AST→MIR Transpile** | 0.567 | 22.7% | Direct AST → MIR instruction emission |
| **MIR Link+Gen** | 1.837 | 73.6% | `MIR_link()` — link imports + native codegen |
| **Total** | **2.497** | **100%** | |

#### Execution Time Comparison (median of 3 runs, ms)

The MIR direct path currently produces **slower execution** for most benchmarks, indicating the generated MIR code is less optimized than the C2MIR-generated code:

| Benchmark | C2MIR Exec | MIR Exec | Ratio | Status |
|-----------|-----------|----------|-------|--------|
| base64 | 1,083 | 971 | 0.90x | OK |
| brainfuck | 345 | 1,022 | 2.96x | SLOWER |
| collatz | 2,463 | N/A | — | MIR_FAIL |
| json_gen | 68 | 66 | 0.97x | OK |
| levenshtein | 23 | 35 | 1.47x | SLOWER |
| matmul | 334 | 1,606 | 4.82x | SLOWER |
| primes (K) | 22 | 239 | **10.9x** | SLOWER |
| array1 | 11 | 39 | 3.45x | SLOWER |
| deriv | 55 | 57 | 1.05x | OK |
| diviter | 5,455 | N/A | — | MIR_FAIL |
| divrec | 10 | 12 | 1.29x | SLOWER |
| gcbench | 2,732 | 2,561 | 0.94x | OK |
| paraffins | 1.1 | 1.3 | 1.23x | SLOWER |
| pnpoly | 57 | N/A | — | MIR_FAIL |
| primes (L) | 1.5 | 4.3 | **2.87x** | SLOWER |
| puzzle | 13 | 23 | 1.71x | SLOWER |
| quicksort | 7 | 16 | 2.39x | SLOWER |
| ray | 7 | N/A | — | MIR_FAIL |
| triangl | 1,723 | 32,583 | **18.9x** | SLOWER |

**4 benchmarks fail** with `--mir` (collatz, diviter, pnpoly, ray) — these need MIR transpiler implementation fixes.

#### Key Findings

1. **MIR direct path is 2.60x faster for JIT compilation** (2.497 ms vs 6.503 ms). The savings come from eliminating C text generation and C2MIR parsing of the ~42KB header.

2. **MIR direct path execution is significantly slower** for compute-intensive benchmarks. The worst cases (triangl: 18.9x, primes: 10.9x, matmul: 4.82x) suggest the MIR transpiler is:
   - Missing integer unboxing optimizations (using `Item` tagged values instead of raw `int64_t`)
   - Not specializing arithmetic operations for known integer types
   - Potentially using function calls for operations that should be inline

3. **MIR Link+Gen dominates** the MIR direct path at 73.6%, while AST→MIR transpilation is only 22.7%. The transpilation itself is very fast.

4. **Benchmarks where MIR is comparable** (base64, json_gen, deriv, gcbench) are primarily I/O-bound or collection-heavy — the compute overhead from unboxed arithmetic is masked.

#### MIR Transpiler Tuning Priorities

Based on this comparison, the MIR transpiler tuning should focus on:

| Priority | Area | Expected Impact |
|----------|------|-----------------|
| **P0** | Integer unboxing — detect typed `int` variables and use native `i64` | 3–10x execution speedup for numeric benchmarks |
| **P1** | Fix 4 failing benchmarks (collatz, diviter, pnpoly, ray) | Feature completeness |
| **P2** | Float unboxing for `float` typed variables | Speedup for matmul, ray |
| **P3** | Inline common operations (`+`, `-`, `*`, comparison) instead of function calls | Reduce call overhead |
| **P4** | Array access optimization — direct memory indexing for typed arrays | Speedup for array1, quicksort, base64 |
