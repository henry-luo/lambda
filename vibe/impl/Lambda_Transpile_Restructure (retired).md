# Lambda C Transpiler — Structured Refactoring Proposal

## Overview

This document proposes a structured refactoring of the Lambda C transpiler (`transpile.cpp`) to address four categories of architectural issues:

1. **Dual-version function generation** — systematic unboxed/boxed versions for all user and system functions
2. **Structured return types** — replacing sentinel-value error returns with `Result` structs
3. **Idempotent boxing** — safe boxing helpers for all numeric types
4. **Additional transpiling enhancements** — data-driven metadata, centralized type narrowing, debug validation

The overarching goal: make the transpiler's generated code **structurally correct by construction**, rather than relying on ad-hoc special cases and caller-side knowledge of return conventions.

### References

- [Lambda_Box_Unbox.md](Lambda_Box_Unbox.md) — Dual version optimization proposal (original)
- [Lambda_Box_Unbox2.md](Lambda_Box_Unbox2.md) — Container unboxing fix (correctness)
- [Lamdba_Runtime.md §Suggestions](../doc/dev/Lamdba_Runtime.md) — Runtime improvement list (10 items)

---

## 1. Dual-Version Function Generation

### Problem Statement

The current transpiler generates up to three versions of typed user functions:

| Version | Suffix | Params | Return | Status |
|---------|--------|--------|--------|--------|
| Main | (none) | Mixed (native typed + Item optional) | Mixed | Generated always |
| Unboxed | `_u` | Native types | Native return | Only for INT/INT64 return |
| Wrapper | `_w` | All Item | Item | Generated for typed params |

**Issues:**
- `_u` is only generated for INT/INT64 returns — missing FLOAT, BOOL, STRING, containers
- The main function's signature is inconsistent: closures return Item, `can_raise` returns Item, typed returns native, untyped returns Item
- `_w` uses blind `(void*)` casts for container params — crashes on null/type-mismatch (see [Lambda_Box_Unbox2.md](Lambda_Box_Unbox2.md))
- System functions have no dual-version scheme at all — each has a single `fn_*` function with ad-hoc return conventions
- `transpile_box_item()` has ~340 lines of special-case switch logic to determine how to box return values
- `transpile_call_argument()` has ~160 lines of type-coercion logic spread across nested if/else chains

### Proposed Architecture

Generate exactly **two versions** for every callable function (user and system):

| Version | Suffix | ABI | Purpose |
|---------|--------|-----|---------|
| **Native** | `_n` | Native C types (`int64_t`, `double`, `Map*`, etc.) | Core logic, maximum performance |
| **Boxed** | `_b` | All `Item` params, returns `RetItem` (see §2) | Safe trampoline for dynamic dispatch (`fn_call*`) |

The current three versions (main, `_u`, `_w`) are consolidated into these two cleanly separated versions.

#### Current `_w` vs. Proposed `_b` — What Changes

The **`_w` (wrapper)** is the current implementation's boxed trampoline. It exists because `fn_call*` dynamic dispatch casts all function pointers to `Item(*)(Item,...)` ABI, but typed user functions have native-typed parameters. The `_w` function accepts `Item` params, converts them, and calls the main function.

**Why `_w` is being replaced, not kept:**

| Aspect | Current `_w` | Proposed `_b` |
|--------|-------------|---------------|
| **Parameter unboxing** | Blind `(void*)` cast for containers — **crashes on null/type-mismatch** | Type-checking helpers (`it2map`, `it2list`, etc.) — safe |
| **Return value** | Returns `Item` — ambiguous (value? error? null?) | Returns typed `Ret*` struct — explicit success/failure |
| **Return boxing** | Ad-hoc `switch` on return type, ~30 cases | Uses `TypeBoxInfo` table lookup — uniform |
| **Error propagation** | Returns `ITEM_ERROR` sentinel — no detail | Returns `RetItem` with structured error — rich info |
| **Code generation** | Complex logic in `define_func_call_wrapper()` | Simple: emit `unbox(param)` per param, `box(result)` once |
| **Coverage** | Only generated for typed-param functions | Generated for ALL functions that need dynamic dispatch |

The `_b` version is structurally identical in purpose to `_w` — it's a boxed ABI trampoline — but with correct, safe, and uniform implementation.

The **`_u` (unboxed)** suffix is also retired. It was a partial optimization: only generated for INT/INT64 return types, with the function body duplicated. In the new scheme, `_n` IS the unboxed version by definition — it always uses native types for both params and return. No body duplication needed.

#### 1.1 Native Version (`_n`)

The native version is the **canonical implementation**. It uses C-native types for all parameters and return values.

```c
// fn list_length(node: map) int
int64_t _list_length_n42(Map* _node) {
    if (_node == NULL) return 0;
    // ... core logic ...
}

// fn add_floats(a: float, b: float) float
double _add_floats_n55(double _a, double _b) {
    return _a + _b;
}

// fn transform(data: map) map
Map* _transform_n60(Map* _data) {
    // ... returns Map* or NULL ...
}
```

**Rules for the native version:**
- All typed parameters use their C-native type (per `write_type()` mapping)
- Return type is the C-native type of the declared/inferred return
- NULL handling is the caller's responsibility (functions should handle NULL gracefully)
- No Item boxing or unboxing in the function body — all operations are native
- For `can_raise` functions, native version returns the appropriate typed `Ret*` struct (see §2) — e.g., `RetInt56` for an `int`-returning function that can raise, `RetInt64` for `int64`, `RetMap` for a `map`-returning function that can raise

#### 1.2 Boxed Version (`_b`)

The boxed version is a **thin trampoline** that unboxes parameters, calls the native version, and boxes the result. It matches the `fn_call*` ABI (`Item(*)(Item, ...)`) for dynamic dispatch. It always returns `RetItem` — the universal boxed return type.

```c
// fn list_length(node: map) int  →  boxed trampoline
RetItem _list_length_b42(Item _node) {
    return ri_ok(i2it(_list_length_n42(it2map(_node))));
}

// fn transform(data: map) map  →  boxed trampoline
RetItem _transform_b60(Item _data) {
    Map* result = _transform_n60(it2map(_data));
    return ri_ok(p2it(result));
}

// fn read_data(path: string) map^  →  can_raise, boxed trampoline
RetItem _read_data_b80(Item _path) {
    RetMap rm = _read_data_n80(it2s(_path));  // native version returns RetMap
    if (rm.err) return ri_err(rm.err);
    return ri_ok(p2it(rm.value));
}
```

**Rules for the boxed version:**
- All parameters are `Item`
- Return type is always `RetItem` (uniform ABI for `fn_call*`)
- Unboxing uses type-checking helpers (`it2map`, `it2list`, `it2i`, etc.) — never blind `(void*)` casts
- Boxing uses the standard helpers (`i2it`, `push_d`, `s2it`, `p2it`, etc.)
- For non-error functions: body is a single expression — unbox → call native → box → wrap in `ri_ok()`
- For `can_raise` functions: call native → check `Ret*.err` → convert to `RetItem`

#### 1.3 Call-Site Resolution

The transpiler selects the version at each call site:

| Caller context | Callee version | Rationale |
|----------------|----------------|-----------|
| Direct call, all arg types known and match | `_n` | No boxing overhead |
| Direct call, arg types known, coercion needed | `_n` with coercion | e.g., `int` → `double` widening |
| Direct call, some arg types unknown (`ANY`) | `_b` | Must go through type-checking unbox |
| Dynamic dispatch (`fn_call*`) | `_b` | ABI compatibility |
| Closure call | `_b` | Closures pass Items |

**Generated code at call sites:**

```c
// Direct call with known types → native version (zero boxing overhead)
int64_t result = _list_length_n42(my_map);

// Direct call, can_raise, known types → native version returns typed Ret*
RetMap rm = _read_data_n80(my_path_str);
if (rm.err) return ...; // propagate error
Map* data = rm.value;

// Direct call with ANY-typed argument → boxed version
RetItem ri = _list_length_b42(some_item);
if (ri.err) { /* handle error */ }
Item result = ri.value;

// Coercion call (int → float)
double result = _add_floats_n55((double)my_int, my_float);
```

#### 1.4 System Function Dual Versions

System functions currently have a single `fn_*` implementation with inconsistent return conventions. Introduce dual versions for performance-critical system functions:

```c
// Current (inconsistent):
Item  fn_add(Item a, Item b);        // returns Item, but might be raw int64 for INT+INT
Item  fn_len(Item container);         // returns int64_t pretending to be Item

// Proposed:
// Native versions (type-specialized):
int64_t fn_add_ii(int64_t a, int64_t b);   // int + int → int
double  fn_add_dd(double a, double b);      // float + float → float
int64_t fn_len_l(List* list);               // list length → int64
int64_t fn_len_m(Map* map);                 // map length → int64

// Boxed version (generic, safe):
RetItem fn_add_b(Item a, Item b);           // any + any → RetItem
RetItem fn_len_b(Item container);           // any container → RetItem
```

**Naming convention for system function native variants:**

| Pattern | Suffix | Meaning |
|---------|--------|---------|
| `fn_foo_i` | `_i` | int64_t specialization |
| `fn_foo_d` | `_d` | double specialization |
| `fn_foo_ii` | `_ii` | (int64_t, int64_t) specialization |
| `fn_foo_dd` | `_dd` | (double, double) specialization |
| `fn_foo_id` | `_id` | (int64_t, double) specialization |
| `fn_foo_s` | `_s` | String* specialization |
| `fn_foo_m` | `_m` | Map* specialization |
| `fn_foo_l` | `_l` | List* specialization |
| `fn_foo_b` | `_b` | Boxed Item generic version (returns `RetItem`) |

The transpiler selects the variant based on compile-time argument types. When types are unknown (`ANY`), it falls back to the `_b` variant.

#### 1.5 Minimizing Generated Code Size

To keep generated C code small, the boxed version should be **generated inline as a one-liner trampoline**, not a duplicated function body:

```c
// Compact boxed trampoline (single line):
RetItem _foo_b42(Item a, Item b) { return ri_ok(i2it(_foo_n42(it2i(a), it2i(b)))); }
```

**Code size reduction strategies:**

1. **Never duplicate function bodies** — the boxed version always delegates to the native version
2. **Use macro helpers** for common unbox/box patterns:
   ```c
   #define UNBOX_CALL_1(ret_box, fn, unbox1, a) \
       ri_ok(ret_box(fn(unbox1(a))))
   #define UNBOX_CALL_2(ret_box, fn, unbox1, a, unbox2, b) \
       ri_ok(ret_box(fn(unbox1(a), unbox2(b))))
   ```
3. **Inline the trampoline** — for simple functions, the C compiler will inline the boxed version when called directly, eliminating the trampoline overhead entirely
4. **Omit the boxed version** when it's never needed — if a function is never passed to `fn_call*`, never stored as a closure, and never called with `ANY`-typed arguments, skip generating `_b`

#### 1.6 Structurally Safe Unboxing

Replace all blind `(void*)` casts with type-checking unbox helpers (from [Lambda_Box_Unbox2.md](Lambda_Box_Unbox2.md)):

```c
// Scalar unboxing (already exists):
int64_t  it2i(Item item);     // INT/INT64/FLOAT/BOOL → int64
double   it2d(Item item);     // INT/INT64/FLOAT/DECIMAL → double
bool     it2b(Item item);     // any → bool
int64_t  it2l(Item item);     // INT/INT64 → int64
String*  it2s(Item item);     // STRING → String*

// Container unboxing (NEW — needed for correctness):
Map*     it2map(Item item);   // MAP/VMAP → Map*, NULL if mismatch
List*    it2list(Item item);  // LIST → List*, NULL if mismatch
Element* it2elmt(Item item);  // ELEMENT → Element*, NULL if mismatch
Object*  it2obj(Item item);   // OBJECT → Object*, NULL if mismatch
Array*   it2arr(Item item);   // ARRAY variants → Array*, NULL if mismatch
Range*   it2range(Item item); // RANGE → Range*, NULL if mismatch
Path*    it2path(Item item);  // PATH → Path*, NULL if mismatch
void*    it2p(Item item);     // Any container → void*, NULL if tagged scalar

// Container boxing (safe NULL handling):
static inline Item p2it(void* ptr) {
    return ptr ? (Item)(uint64_t)ptr : ITEM_NULL;
}
```

**Implementation pattern** (from Box_Unbox2):

```c
Map* it2map(Item item) {
    if (item.item >> 56 != 0) return NULL;  // tagged scalar (not container)
    if (item.item == 0) return NULL;         // null pointer
    TypeId tid = *(TypeId*)(void*)item.item;  // read Container.type_id
    if (tid == LMD_TYPE_MAP || tid == LMD_TYPE_VMAP) return (Map*)(void*)item.item;
    return NULL;                              // wrong container type
}
```

#### 1.7 Performance Considerations

The dual-version scheme **improves** performance over the current system:

| Aspect | Current | Proposed |
|--------|---------|----------|
| **Direct typed call** | Calls main function (native types), sometimes wraps return | Calls `_n` directly (zero overhead) |
| **Dynamic dispatch** | Calls `_w` with blind `(void*)` casts | Calls `_b` with safe unboxing (~1 branch per container param) |
| **System function call (typed)** | Calls `fn_*` (always Item ABI), boxes/unboxes at call site | Calls `fn_*_ii` etc. (native ABI, no boxing overhead) |
| **Return value handling** | Ad-hoc boxing in `transpile_box_item()` | Caller knows if native or `RetItem` — no ambiguity |
| **Container param passing** | Blind cast (fast but crashes on mismatch) | `it2map()` — one branch check (negligible cost) |

The critical insight: **the hot path (direct typed calls) has zero boxing overhead**. The boxing cost only appears in the cold path (dynamic dispatch), where it's already negligible compared to the function pointer indirection.

---

## 2. Structured Return Types: Per-Type `Ret*` Structs

### Problem Statement

Currently, error-returning functions and boxed wrappers all return `Item`. Error detection requires inspecting the Item's type tag:

```c
Item result = fn_read_file(path_item);
if (item_type_id(result) == LMD_TYPE_ERROR) {
    // handle error...
}
```

**Issues:**
- **Ambiguity**: A function returning `Item` — is it a normal value, an error, or null? The caller must always check.
- **Sentinel reuse**: `ITEM_ERROR` is a single 64-bit value with no error detail. The structured `LambdaError` system exists but is passed out-of-band.
- **Type mismatch errors are silent**: When `it2i()` receives a Map, it returns 0 — indistinguishable from a real 0.
- **Native return functions can't signal errors**: A function returning `int64_t` has no way to indicate failure (0 could be a valid result).
- **`?` propagation is fragile**: The transpiler generates `item_type_id(ep)==LMD_TYPE_ERROR` checks — these only work for Item-returning functions and miss native-return errors.
- **A single `LResult{Item, bool}` would force boxing**: If the error-returning native version had to pack its value into `Item`, we'd pay boxing cost on the hot path (success case) — defeating the purpose of the native version.

### Proposed Solution: Per-Type `Ret*` Struct Family

Define a **separate result struct for each native return type**, so that error-returning functions preserve their native return type without boxing.

#### Design Evolution: 3-Field vs 2-Field

Two designs were considered for the `Ret*` structs:

**Option A — 3-field: `{value, ok, err}`**

```c
typedef Item Error;  // Error = LMD_TYPE_ERROR-tagged Item
typedef struct RetMap { Map* value; bool ok; Error err; } RetMap;
// ok check:    if (!r.ok) { /* error: r.err */ }
// success:     r.value
```

**Option B — 2-field: `{value, err}` (chosen)**

```c
typedef struct RetMap { Map* value; LambdaError* err; } RetMap;
// error check: if (r.err) { /* error: r.err->code, r.err->message */ }
// success:     r.value
```

**Comparison:**

| Aspect | Option A: 3-field | Option B: 2-field (chosen) |
|--------|-------------------|----------------------------|
| **Fields** | `value` + `ok` + `err` | `value` + `err` |
| **Size** | 24 bytes (8 + 1 + 7 padding + 8) | **16 bytes** (8 + 8) |
| **x86-64 ABI** | Returned via **hidden pointer** (>16B) | Returned in **`rax`+`rdx` registers** — zero memory indirection |
| **ARM64 ABI** | `x0`/`x1`/`x2` (3 registers) | `x0`/`x1` (2 registers) |
| **Error check** | `if (!r.ok)` — test bool | `if (r.err)` — NULL pointer test (same speed) |
| **Error detail** | `r.err` is an Item — must untag to get `LambdaError*` | `r.err->code`, `r.err->message` — **direct access** |
| **Redundancy** | `ok` and `err` can desync (e.g., `ok=false` but `err` unset) | **Single source of truth** — `NULL` = success, non-NULL = error |
| **Error type** | `Error` (Item) — needs `err2it()` / tag manipulation | `LambdaError*` — native C pointer, direct field access |

**Decision: Option B (2-field).** The 16-byte struct enables register-return ABI on x86-64 — a significant performance win for the hot success path. The `LambdaError*` error field provides richer, more direct access to error details. The `ok` field is redundant since `err == NULL` encodes success.

> **Note**: `LambdaErrorCode` already defines `ERR_OK = 0`, and `LambdaError*` with `NULL` = no error is the natural C convention for optional error output. The 2-field design aligns with both conventions.

#### Struct Definitions (2-field, chosen design)

```c
// Per-type result structs (2-field: value + err):
typedef struct RetBool   { bool     value; LambdaError* err; } RetBool;
typedef struct RetInt56  { int64_t  value; LambdaError* err; } RetInt56;  // Lambda int (56-bit inline)
typedef struct RetInt64  { int64_t  value; LambdaError* err; } RetInt64;  // int64 (heap-allocated)
typedef struct RetFloat  { double   value; LambdaError* err; } RetFloat;
typedef struct RetString { String*  value; LambdaError* err; } RetString;
typedef struct RetSymbol { Symbol*  value; LambdaError* err; } RetSymbol;
typedef struct RetMap    { Map*     value; LambdaError* err; } RetMap;
typedef struct RetList   { List*    value; LambdaError* err; } RetList;
typedef struct RetElmt   { Element* value; LambdaError* err; } RetElmt;
typedef struct RetObj    { Object*  value; LambdaError* err; } RetObj;
typedef struct RetArray  { Array*   value; LambdaError* err; } RetArray;
typedef struct RetRange  { Range*   value; LambdaError* err; } RetRange;
typedef struct RetPath   { Path*    value; LambdaError* err; } RetPath;
typedef struct RetItem   { Item     value; LambdaError* err; } RetItem;
```

**Why per-type structs, not a single `LResult{Item, LambdaError*}`?**

| Approach | Pros | Cons |
|----------|------|------|
| **Sentinel `ITEM_ERROR`** | Zero overhead | No error detail, can't distinguish "value 0" from "error" |
| **Single `LResult{Item, LambdaError*}`** | One type for everything | Forces boxing native return values — **kills `_n` performance for `can_raise` functions** |
| **Per-type `Ret*` structs** | Native values stay native on success path; rich error detail via `LambdaError*`; 16 bytes = register return | Many struct types; requires transpiler to select correct `Ret*` |

The key insight: A function like `fn read_line() string^` should return `RetString{String*, LambdaError*}` from its native version — the `String*` stays as a raw pointer, never boxed into Item. Only the boxed `_b` trampoline converts to `RetItem`.

### 2.1 Struct Layout and ABI

Each `Ret*` struct has two fields:

| Field | Type | Purpose |
|-------|------|---------|
| `value` | Native type (`int64_t`, `Map*`, etc.) | The successful return value (undefined on error) |
| `err` | `LambdaError*` | `NULL` = success; non-NULL = error with code, message, stack trace |

**Size**: 16 bytes for all variants (8 + 8). Both fields are pointer-sized or smaller.

**x86-64 ABI**: Structs with two integer/pointer fields ≤ 16 bytes are returned in registers `rax` + `rdx`. This means **zero memory indirection** on the success path — the value comes back in `rax` and the NULL error in `rdx`.

**ARM64 ABI**: Returned in `x0` + `x1` (or `x0` + `d0` for `RetFloat`). Two registers, no memory.

**Optimization**: For non-error functions, the transpiler does NOT use `Ret*` structs — it returns the raw native type directly. `Ret*` is only used when `can_raise` is true. This means:

- **Non-error function, native call**: returns `int64_t` / `Map*` / etc. directly — zero overhead
- **Error function, native call**: returns `RetInt56` / `RetInt64` / `RetMap` / etc. — one pointer extra (NULL on success)
- **Any function, boxed call**: returns `RetItem` — uniform ABI for `fn_call*`

### 2.2 Error ↔ Item Conversion Helper

Since `fn_call*` dispatch and `let a^err` destructuring may need the error as an Item, we provide a conversion helper:

```c
// Convert LambdaError* → Error Item (LMD_TYPE_ERROR-tagged pointer)
static inline Item err2it(LambdaError* err) {
    if (!err) return ITEM_NULL;
    return (Item){ .item = ((uint64_t)LMD_TYPE_ERROR << 56) | (uint64_t)(uintptr_t)err };
}

// Convert Error Item → LambdaError* (extract pointer from tagged Item)
static inline LambdaError* it2err(Item error_item) {
    if (item_type_id(error_item) != LMD_TYPE_ERROR) return NULL;
    return (LambdaError*)(uintptr_t)(error_item.item & 0x00FFFFFFFFFFFFFFULL);
}
```

These are used in:
- **`_b` trampolines**: `if (r.err) return (RetItem){ ITEM_ERROR, r.err };` — `r.err` is already `LambdaError*`, no conversion needed in the `RetItem`.
  When `fn_call*` needs to return a plain `Item` error: `return err2it(ri.err);`
- **`let a^err` in boxed context**: `Item _err = r.err ? err2it(r.err) : ITEM_NULL;`
- **Error unpacking from Item**: `LambdaError* lerr = it2err(error_item);`

### 2.3 Constructor Helpers

```c
// RetItem constructors (used by boxed _b versions):
static inline RetItem ri_ok(Item value) {
    return (RetItem){ .value = value, .err = NULL };
}
static inline RetItem ri_err(LambdaError* error) {
    return (RetItem){ .value = ITEM_ERROR, .err = error };
}

// RetInt56 constructors (Lambda int, 56-bit inline):
static inline RetInt56 ri56_ok(int64_t value) {
    return (RetInt56){ .value = value, .err = NULL };
}
static inline RetInt56 ri56_err(LambdaError* error) {
    return (RetInt56){ .value = 0, .err = error };
}

// RetInt64 constructors (int64, heap-allocated):
static inline RetInt64 ri64_ok(int64_t value) {
    return (RetInt64){ .value = value, .err = NULL };
}
static inline RetInt64 ri64_err(LambdaError* error) {
    return (RetInt64){ .value = 0, .err = error };
}

// RetFloat constructors:
static inline RetFloat rf_ok(double value) {
    return (RetFloat){ .value = value, .err = NULL };
}
static inline RetFloat rf_err(LambdaError* error) {
    return (RetFloat){ .value = 0.0, .err = error };
}

// RetString constructors:
static inline RetString rs_ok(String* value) {
    return (RetString){ .value = value, .err = NULL };
}
static inline RetString rs_err(LambdaError* error) {
    return (RetString){ .value = NULL, .err = error };
}

// RetMap constructors:
static inline RetMap rm_ok(Map* value) {
    return (RetMap){ .value = value, .err = NULL };
}
static inline RetMap rm_err(LambdaError* error) {
    return (RetMap){ .value = NULL, .err = error };
}

// RetBool constructors:
static inline RetBool rb_ok(bool value) {
    return (RetBool){ .value = value, .err = NULL };
}
static inline RetBool rb_err(LambdaError* error) {
    return (RetBool){ .value = false, .err = error };
}

// ... same pattern for RetList, RetElmt, RetObj, RetArray, RetRange, RetPath

// Error creation helper (allocates LambdaError on heap):
static inline LambdaError* make_err(LambdaErrorCode code, const char* msg) {
    return err_create_simple(code, msg);
}
```

**Naming convention**: `r` + type abbreviation + `_ok` / `_err`:

| Type | Prefix | Ok | Err |
|------|--------|-----|-----|
| `RetItem` | `ri` | `ri_ok(item)` | `ri_err(lerr)` |
| `RetInt56` | `ri56` | `ri56_ok(val)` | `ri56_err(lerr)` |
| `RetInt64` | `ri64` | `ri64_ok(val)` | `ri64_err(lerr)` |
| `RetFloat` | `rf` | `rf_ok(val)` | `rf_err(lerr)` |
| `RetString` | `rs` | `rs_ok(str)` | `rs_err(lerr)` |
| `RetBool` | `rb` | `rb_ok(val)` | `rb_err(lerr)` |
| `RetMap` | `rm` | `rm_ok(map)` | `rm_err(lerr)` |
| `RetList` | `rl` | `rl_ok(list)` | `rl_err(lerr)` |

### 2.3 Applying `Ret*` to Function Versions

**Native version (`_n`)**: Non-error functions return raw native types. Error functions return the appropriate typed `Ret*`:

```c
// Non-error function: native return, no Ret* overhead
int64_t _list_length_n42(Map* _node) {
    if (_node == NULL) return 0;
    return fn_len_m(_node);
}

// Error-raising function: returns RetString (native String* + Error)
RetString _read_file_n70(String* _path) {
    if (_path == NULL) return rs_err(make_err(ERR_NULL_REFERENCE, "path is null"));
    String* content = /* ... read file ... */;
    if (!content) return rs_err(make_err(ERR_FILE_READ_ERROR, "read failed"));
    return rs_ok(content);
}

// Error-raising function returning Lambda int: returns RetInt56
RetInt56 _parse_int_n90(String* _str) {
    if (_str == NULL) return ri56_err(make_err(ERR_NULL_REFERENCE, "str is null"));
    int64_t val = /* ... parse ... */;
    return ri56_ok(val);
}
```

**Boxed version (`_b`)**: Always returns `RetItem` — the universal boxed return type for `fn_call*`:

```c
// Non-error function: wrap native result in RetItem
RetItem _list_length_b42(Item _node) {
    return ri_ok(i2it(_list_length_n42(it2map(_node))));
}

// Error-raising function: convert RetString → RetItem
RetItem _read_file_b70(Item _path) {
    RetString rs = _read_file_n70(it2s(_path));
    if (rs.err) return ri_err(rs.err);
    return ri_ok(s2it(rs.value));
}

// Error-raising function: convert RetInt56 → RetItem
RetItem _parse_int_b90(Item _str) {
    RetInt56 ri = _parse_int_n90(it2s(_str));
    if (ri.err) return ri_err(ri.err);
    return ri_ok(i2it(ri.value));  // i2it packs 56-bit int inline
}
```

### 2.4 `Ret*` Mapping Table

The transpiler selects the correct `Ret*` struct based on the function's return TypeId:

| Return TypeId | `Ret*` Struct | Applies to `_n` when `can_raise` |
|---------------|---------------|----------------------------------|
| `LMD_TYPE_INT` | `RetInt56` | Yes — value is `int64_t`, boxes via `i2it()` (inline packing) |
| `LMD_TYPE_INT64` | `RetInt64` | Yes — value is `int64_t`, boxes via `l2it()` (nursery allocation) |
| `LMD_TYPE_FLOAT` | `RetFloat` | Yes |
| `LMD_TYPE_BOOL` | `RetBool` | Yes |
| `LMD_TYPE_STRING` / `LMD_TYPE_BINARY` | `RetString` | Yes |
| `LMD_TYPE_MAP` | `RetMap` | Yes |
| `LMD_TYPE_LIST` | `RetList` | Yes |
| `LMD_TYPE_ELEMENT` | `RetElmt` | Yes |
| `LMD_TYPE_OBJECT` | `RetObj` | Yes |
| `LMD_TYPE_ARRAY` | `RetArray` | Yes |
| `LMD_TYPE_ANY` / unknown | `RetItem` | Yes |
| (any, boxed `_b` version) | `RetItem` | Always |

### 2.5 Error Propagation with `?`

The `?` propagation operator works naturally with typed `Ret*` structs:

**Current generated code:**
```c
({Item ep0 = fn_read_file(path_item);
  if(item_type_id(ep0)==LMD_TYPE_ERROR) return ep0; ep0;})
```

**Proposed — in a native `_n` context (caller also returns `Ret*`):**
```c
// Caller: fn process(path: string) map^   →   RetMap _process_n(String* _path)
// Callee: fn read_file(path: string) string^  →  RetString _read_file_n(String* _path)
({RetString _rs0 = _read_file_n70(_path);
  if(_rs0.err) return rm_err(_rs0.err); _rs0.value;})
```

The error is **forwarded without re-wrapping** — `_rs0.err` (a `LambdaError*`) is passed directly to `rm_err()`. Only the `Ret*` struct type changes (from `RetString` to `RetMap` in this case), the error payload pointer is preserved.

**Proposed — in a boxed `_b` context:**
```c
({RetItem _ri0 = _read_file_b70(path_item);
  if(_ri0.err) return ri_err(_ri0.err); _ri0.value;})
```

### 2.6 `let value^err` Destructuring

```c
// let content^err = read_file("data.json")
// In native context:
RetString _rs0 = _read_file_n70(path_str);
String* _content = _rs0.value;   // native type preserved — no boxing
Item _err = _rs0.err ? err2it(_rs0.err) : ITEM_NULL;  // convert LambdaError* → Error Item

// In boxed context:
RetItem _ri0 = _read_file_b70(s2it(path_str));
Item _content = _ri0.value;
Item _err = _ri0.err ? err2it(_ri0.err) : ITEM_NULL;
```

### 2.7 Rich Errors via `LambdaError*`

With the 2-field design, `Ret*` structs carry `LambdaError*` directly — no tagging/untagging overhead. The `LambdaError` struct from `lambda-error.h` provides:

| Field | Type | Purpose |
|-------|------|`--------|
| `code` | `LambdaErrorCode` | Error category (e.g., `ERR_NULL_REFERENCE = 301`) |
| `message` | `char*` | Human-readable description |
| `location` | `SourceLocation` | File, line, column of error site |
| `stack_trace` | `StackFrame*` | Call stack for debugging |
| `help` | `char*` | Optional suggestion text |
| `cause` | `LambdaError*` | Chained inner error |

Accessing error details in generated C code:
```c
RetString rs = _read_file_n70(path);
if (rs.err) {
    // Direct access — no untagging needed:
    log_error("error %d: %s", rs.err->code, rs.err->message);
    // Forward to caller:
    return rm_err(rs.err);
}
```

When the error needs to be passed as a Lambda `Item` (e.g., for `fn_call*` dispatch or variable binding), use `err2it()`:
```c
Item error_item = err2it(rs.err);  // LambdaError* → Error Item
```

### 2.8 Migration Strategy

The `Ret*` adoption can be **incremental**:

**Phase 1**: Define all `Ret*` structs, constructor helpers, and `err2it()`/`it2err()` converters. Use `RetItem` in new boxed wrappers (`_b`).
**Phase 2**: Migrate `can_raise` user functions' native versions to return typed `Ret*`.
**Phase 3**: Migrate system functions — start with I/O functions (`fn_read_file`, `fn_http_get`, etc.) that already have `can_raise=true`.
**Phase 4**: Migrate `fn_call*` dispatch to expect `RetItem` from `_b` functions.

During migration, compatibility shims convert between `Item` and `RetItem`:

```c
// Compatibility: wrap legacy Item-returning function into RetItem
static inline RetItem item_to_ri(Item item) {
    if (item_type_id(item) == LMD_TYPE_ERROR)
        return (RetItem){ .value = ITEM_ERROR, .err = it2err(item) };
    return (RetItem){ .value = item, .err = NULL };
}

// Compatibility: extract Item from RetItem (for legacy callers)
static inline Item ri_to_item(RetItem ri) {
    return ri.err ? err2it(ri.err) : ri.value;
}
```

### 2.9 Performance Impact

| Path | Current | With Ret* | Delta |
|------|---------|-----------|-------|
| **Direct typed call, non-error** | Returns `int64_t` | Returns `int64_t` (unchanged) | **0** |
| **Direct typed call, can_raise** | Returns `Item` (must box native value) | Returns `RetInt56`/`RetInt64` (native value, no boxing!) | **Faster** |
| **Dynamic dispatch (`_b`)** | Returns `Item` (8 bytes) | Returns `RetItem` (16 bytes) | **+8 bytes** |
| **Error check** | `item_type_id(x)==LMD_TYPE_ERROR` (load + shift + compare) | `if (r.err)` (NULL pointer test) | **Faster** |
| **Error propagation** | Wrap in `ITEM_ERROR` + `return` | `return r*_err(r.err)` (forward `LambdaError*`) | **Richer info** |

The critical win: **`can_raise` functions on the hot path (success) no longer pay boxing cost.** A `fn parse_int(s: string) int^` returns `RetInt56{42, NULL}` — the `int64_t 42` is never boxed into an `Item`, and `NULL` means no error. Only the cold error path allocates a `LambdaError`.

---

## 3. Idempotent Boxing for All Numeric Types

### Problem Statement

`push_l_safe()` was created as a workaround for INT64 double-boxing in the MIR transpiler. The same class of bug can affect FLOAT and DATETIME boxing:

```c
// push_l_safe exists:
Item push_l_safe(int64_t val) {
    uint8_t tag = (uint64_t)val >> 56;
    if (tag == LMD_TYPE_INT64) return (Item){.item = (uint64_t)val};  // already boxed
    if (tag == LMD_TYPE_INT)   { /* extract and rebox */ }
    return push_l(val);  // raw value, box normally
}

// push_d_safe and push_k_safe DO NOT EXIST yet
```

### Proposed Safe Boxing Helpers

#### 3.1 `push_d_safe()` — Idempotent Float Boxing

```c
Item push_d_safe(double val) {
    // Reinterpret double as uint64_t to check for pre-existing type tag
    uint64_t bits;
    memcpy(&bits, &val, sizeof(bits));
    uint8_t tag = bits >> 56;

    if (tag == LMD_TYPE_FLOAT) {
        // Already a boxed FLOAT Item — return as-is
        log_debug("push_d_safe: already boxed FLOAT");
        return (Item){ .item = bits };
    }

    // Check for edge case: double whose bit pattern collides with tagged form.
    // LMD_TYPE_FLOAT tag value occupies the exponent range for extreme NaN encodings.
    // In practice, IEEE 754 NaN payloads rarely collide with our tag values.
    // The standard push_d() allocates in nursery and tags the pointer — safe.
    return push_d(val);
}
```

**NaN collision risk**: The top byte of a double encodes the sign + upper 7 exponent bits. For tag `LMD_TYPE_FLOAT` (value ~7–8 range), this corresponds to very small denormalized doubles or specific NaN encodings. Collision is theoretically possible but astronomically rare in practice. If needed, add a secondary check:

```c
// Paranoid version: verify the "pointer" in lower 56 bits is actually a nursery pointer
if (tag == LMD_TYPE_FLOAT) {
    void* ptr = (void*)(bits & 0x00FFFFFFFFFFFFFF);
    if (is_nursery_pointer(ptr)) return (Item){ .item = bits };
    // Not a nursery pointer — this is a raw double with a coincidental tag
}
```

#### 3.2 `push_k_safe()` — Idempotent DateTime Boxing

```c
Item push_k_safe(DateTime dtval) {
    // DateTime is likely passed as a struct on the stack.
    // Check if the raw int64 representation has an existing type tag.
    uint64_t bits;
    memcpy(&bits, &dtval, sizeof(bits));
    uint8_t tag = bits >> 56;

    if (tag == LMD_TYPE_DTIME) {
        // Already a boxed DTIME Item
        log_debug("push_k_safe: already boxed DTIME");
        return (Item){ .item = bits };
    }
    return push_k(dtval);
}
```

#### 3.3 Debug-Mode Boxing Validation

In addition to safe boxing, add **debug assertions** to catch double-boxing at the point of occurrence:

```c
#ifdef LAMBDA_DEBUG
Item push_l_debug(int64_t val) {
    uint8_t tag = (uint64_t)val >> 56;
    if (tag != 0) {
        log_error("push_l: called with already-tagged value 0x%016llx (tag=%d)",
                  (uint64_t)val, tag);
        assert(false && "push_l called with already-tagged value");
    }
    return push_l(val);
}
#define push_l(val) push_l_debug(val)
#endif
```

This makes the transpiler's type tracking bugs manifest immediately during testing rather than producing silent corruption downstream.

#### 3.4 Idempotent Container Boxing: `p2it()`

For containers, the analogous issue is passing `NULL` or a non-container Item where a `Map*` is expected. The `p2it()` helper handles this:

```c
static inline Item p2it(void* ptr) {
    if (!ptr) return ITEM_NULL;           // NULL → null (not undefined)
    // Container pointers have 0x00 in top byte — they ARE valid Items already
    return (Item){ .item = (uint64_t)(uintptr_t)ptr };
}
```

This is safe because container pointers are heap addresses with the top byte `0x00`, which makes them valid Items when reinterpreted as `uint64_t`.

---

## 4. Additional Transpiling Enhancements

### 4.1 Data-Driven SysFuncInfo Metadata

**Problem**: The transpiler uses hardcoded switches in `transpile_box_item()` and `transpile_call_argument()` to determine boxing/unboxing behavior for each system function. Adding or modifying a system function requires updating multiple switch statements across ~500 lines of transpiler code.

**Solution**: Extend `SysFuncInfo` with C-level ABI metadata:

```c
typedef enum CRetType {
    C_RET_ITEM,      // returns boxed Item (default, safe)
    C_RET_INT64,     // returns raw int64_t
    C_RET_DOUBLE,    // returns raw double
    C_RET_BOOL,      // returns raw bool (int)
    C_RET_STRING,    // returns String*
    C_RET_CONTAINER, // returns container pointer (Map*, List*, etc.)
    C_RET_ADAPTIVE,  // return type depends on argument types
} CRetType;

typedef enum CArgConvention {
    C_ARG_ITEM,      // arguments are boxed Items (default)
    C_ARG_NATIVE,    // arguments are native C types (int64_t, double)
} CArgConvention;

typedef struct SysFuncInfo {
    SysFunc fn;
    const char* name;
    int arg_count;
    Type* return_type;
    bool is_proc;
    bool is_overloaded;
    bool is_method_eligible;
    TypeId first_param_type;
    bool can_raise;
    // NEW:
    CRetType c_ret_type;          // C-level return convention
    CArgConvention c_arg_conv;    // C-level argument convention
    const char* native_variant;   // name of type-specialized native function (or NULL)
} SysFuncInfo;
```

The transpiler's boxing logic becomes a **data lookup** instead of a code switch:

```c
void transpile_sysfunc_result_boxing(Transpiler* tp, SysFuncInfo* info) {
    switch (info->c_ret_type) {
    case C_RET_ITEM:      /* already boxed, nothing to do */ break;
    case C_RET_INT64:     strbuf_append_str(tp->code_buf, "i2it("); break;
    case C_RET_DOUBLE:    strbuf_append_str(tp->code_buf, "push_d("); break;
    case C_RET_BOOL:      strbuf_append_str(tp->code_buf, "b2it("); break;
    case C_RET_STRING:    strbuf_append_str(tp->code_buf, "s2it("); break;
    case C_RET_CONTAINER: strbuf_append_str(tp->code_buf, "p2it("); break;
    case C_RET_ADAPTIVE:  transpile_adaptive_boxing(tp, info); break;
    }
}
```

**Sample registration table updates:**

```c
// Before:
{FN_LEN,  "len",   1, &TYPE_INT64, false, true, true, LMD_TYPE_ANY, false},
{FN_BAND, "band",  2, &TYPE_INT,   false, false, false, LMD_TYPE_ANY, false},
{FN_ADD,  "add",   2, &TYPE_ANY,   false, true, false, LMD_TYPE_ANY, false},

// After:
{FN_LEN,  "len",   1, &TYPE_INT64, false, true, true, LMD_TYPE_ANY, false,
    C_RET_INT64, C_ARG_ITEM, NULL},
{FN_BAND, "band",  2, &TYPE_INT,   false, false, false, LMD_TYPE_ANY, false,
    C_RET_INT64, C_ARG_NATIVE, NULL},
{FN_ADD,  "add",   2, &TYPE_ANY,   false, true, false, LMD_TYPE_ANY, false,
    C_RET_ADAPTIVE, C_ARG_ITEM, NULL},
```

### 4.2 Centralized Type Narrowing Table

**Problem**: Type narrowing logic ("if both args to `fn_add` are INT, result is INT") is scattered across both transpilers in ad-hoc if/else chains.

**Solution**: A single declaration table:

```c
typedef struct TypeNarrowEntry {
    SysFunc func_id;
    TypeId arg1_type;
    TypeId arg2_type;     // LMD_TYPE_ANY for unary functions
    TypeId result_type;   // narrowed result type
    CRetType c_ret;       // C ABI for this specialization
    const char* native_fn; // native function name (e.g., "fn_add_ii")
} TypeNarrowEntry;

static const TypeNarrowEntry type_narrow_table[] = {
    // Arithmetic
    {FN_ADD, LMD_TYPE_INT,   LMD_TYPE_INT,   LMD_TYPE_INT,   C_RET_INT64, "fn_add_ii"},
    {FN_ADD, LMD_TYPE_INT,   LMD_TYPE_FLOAT, LMD_TYPE_FLOAT, C_RET_DOUBLE, "fn_add_id"},
    {FN_ADD, LMD_TYPE_FLOAT, LMD_TYPE_FLOAT, LMD_TYPE_FLOAT, C_RET_DOUBLE, "fn_add_dd"},
    {FN_ADD, LMD_TYPE_INT64, LMD_TYPE_INT64, LMD_TYPE_INT64, C_RET_INT64, "fn_add_ll"},

    // Comparison
    {FN_EQ,  LMD_TYPE_INT,   LMD_TYPE_INT,   LMD_TYPE_BOOL,  C_RET_BOOL, NULL},
    {FN_LT,  LMD_TYPE_INT,   LMD_TYPE_INT,   LMD_TYPE_BOOL,  C_RET_BOOL, NULL},

    // String operations
    {FN_LEN, LMD_TYPE_STRING, LMD_TYPE_ANY,  LMD_TYPE_INT,   C_RET_INT64, "fn_len_s"},
    {FN_LEN, LMD_TYPE_LIST,   LMD_TYPE_ANY,  LMD_TYPE_INT,   C_RET_INT64, "fn_len_l"},
    {FN_LEN, LMD_TYPE_MAP,    LMD_TYPE_ANY,  LMD_TYPE_INT,   C_RET_INT64, "fn_len_m"},

    // Sentinel
    {(SysFunc)0, LMD_TYPE_ANY, LMD_TYPE_ANY, LMD_TYPE_ANY, C_RET_ITEM, NULL},
};
```

Both transpilers (C and MIR) consult this table via a lookup function:

```c
const TypeNarrowEntry* narrow_lookup(SysFunc func, TypeId arg1, TypeId arg2) {
    for (int i = 0; type_narrow_table[i].func_id; i++) {
        const TypeNarrowEntry* e = &type_narrow_table[i];
        if (e->func_id == func &&
            (e->arg1_type == LMD_TYPE_ANY || e->arg1_type == arg1) &&
            (e->arg2_type == LMD_TYPE_ANY || e->arg2_type == arg2)) {
            return e;
        }
    }
    return NULL;  // no specialization — use generic
}
```

**Benefits:**
- Single source of truth for type narrowing rules
- Both C and MIR transpilers stay in sync automatically
- Adding a new narrowing rule = one table row, not code changes in two transpilers

### 4.3 Unified Unbox/Box Helper Registry

Centralize the mapping from `TypeId` to unbox/box function names for use across all transpiler code paths:

```c
typedef struct TypeBoxInfo {
    TypeId type_id;
    const char* unbox_fn;     // Item → native (e.g., "it2i")
    const char* box_fn;       // native → Item (e.g., "i2it")
    const char* safe_box_fn;  // native → Item, idempotent (e.g., "push_l_safe")
    const char* c_type;       // C type string (e.g., "int64_t")
    const char* zero_value;   // default/zero value (e.g., "0")
} TypeBoxInfo;

static const TypeBoxInfo type_box_table[] = {
    {LMD_TYPE_INT,     "it2i",    "i2it",     NULL,           "int64_t",  "0"},
    {LMD_TYPE_INT64,   "it2l",    "push_l",   "push_l_safe",  "int64_t",  "0"},
    {LMD_TYPE_FLOAT,   "it2d",    "push_d",   "push_d_safe",  "double",   "0.0"},
    {LMD_TYPE_BOOL,    "it2b",    "b2it",     NULL,           "bool",     "false"},
    {LMD_TYPE_STRING,  "it2s",    "s2it",     NULL,           "String*",  "NULL"},
    {LMD_TYPE_SYMBOL,  "it2sym",  "y2it",     NULL,           "Symbol*",  "NULL"},
    {LMD_TYPE_BINARY,  "it2x",    "x2it",     NULL,           "String*",  "NULL"},
    {LMD_TYPE_DECIMAL, "it2c",    "c2it",     NULL,           "Decimal*", "NULL"},
    {LMD_TYPE_DTIME,   "it2k",    "k2it",     "push_k_safe",  "DateTime", "{0}"},
    {LMD_TYPE_MAP,     "it2map",  "p2it",     NULL,           "Map*",     "NULL"},
    {LMD_TYPE_LIST,    "it2list", "p2it",     NULL,           "List*",    "NULL"},
    {LMD_TYPE_ELEMENT, "it2elmt", "p2it",     NULL,           "Element*", "NULL"},
    {LMD_TYPE_OBJECT,  "it2obj",  "p2it",     NULL,           "Object*",  "NULL"},
    {LMD_TYPE_ARRAY,   "it2arr",  "p2it",     NULL,           "Array*",   "NULL"},
    {LMD_TYPE_RANGE,   "it2range","p2it",     NULL,           "Range*",   "NULL"},
    {LMD_TYPE_PATH,    "it2path", "p2it",     NULL,           "Path*",    "NULL"},
};

const TypeBoxInfo* get_box_info(TypeId tid) {
    for (int i = 0; i < (int)(sizeof(type_box_table)/sizeof(type_box_table[0])); i++) {
        if (type_box_table[i].type_id == tid) return &type_box_table[i];
    }
    return NULL;
}
```

This replaces scattered switch/if chains in `transpile_box_item()`, `define_func_call_wrapper()`, `transpile_call_argument()`, and `write_type()` with uniform table lookups.

### 4.4 Transpile-Time Unbox/Box Emission API

Instead of each transpiler code path manually emitting unbox/box calls, provide a high-level emission API:

```c
// Emit: unbox_fn(expr)  — e.g., "it2i(" + expr + ")"
void emit_unbox(Transpiler* tp, TypeId target_tid, AstNode* expr) {
    const TypeBoxInfo* info = get_box_info(target_tid);
    if (info) {
        strbuf_append_str(tp->code_buf, info->unbox_fn);
        strbuf_append_char(tp->code_buf, '(');
        transpile_expr(tp, expr);
        strbuf_append_char(tp->code_buf, ')');
    } else {
        // ANY type — no unboxing needed
        transpile_expr(tp, expr);
    }
}

// Emit: box_fn(expr)  — e.g., "i2it(" + expr + ")"
void emit_box(Transpiler* tp, TypeId source_tid, AstNode* expr) {
    const TypeBoxInfo* info = get_box_info(source_tid);
    if (info) {
        strbuf_append_str(tp->code_buf, info->box_fn);
        strbuf_append_char(tp->code_buf, '(');
        transpile_expr(tp, expr);
        strbuf_append_char(tp->code_buf, ')');
    } else {
        transpile_expr(tp, expr);
    }
}

// Emit: box(native_fn(unbox1(arg1), unbox2(arg2)))
void emit_boxed_call(Transpiler* tp, const char* fn_name,
                     TypeId ret_tid, AstNode** args, TypeId* arg_tids, int argc) {
    const TypeBoxInfo* ret_info = get_box_info(ret_tid);
    if (ret_info) {
        strbuf_append_str(tp->code_buf, ret_info->box_fn);
        strbuf_append_char(tp->code_buf, '(');
    }
    strbuf_append_str(tp->code_buf, fn_name);
    strbuf_append_char(tp->code_buf, '(');
    for (int i = 0; i < argc; i++) {
        if (i > 0) strbuf_append_char(tp->code_buf, ',');
        emit_unbox(tp, arg_tids[i], args[i]);
    }
    strbuf_append_char(tp->code_buf, ')');
    if (ret_info) strbuf_append_char(tp->code_buf, ')');
}
```

This collapses hundreds of lines of per-type switch logic into compositional helper calls.

### 4.5 First-Class Variable Type Tracking

**Problem**: After type widening (loop variable goes from INT to ANY), the AST's type becomes stale. The transpiler makes boxing decisions based on stale types, producing double-boxing.

**Solution**: Track variable types explicitly in the transpiler context:

```c
typedef struct VarTypeInfo {
    TypeId declared_type;   // original type from AST
    TypeId current_type;    // updated after each assignment
    bool is_widened;        // set true when widened (e.g., INT → ANY)
    bool is_captured;       // part of a closure env
} VarTypeInfo;
```

All boxing decisions consult `current_type`, not the AST node's type. When a variable is widened, `current_type` is set to `LMD_TYPE_ANY` and `is_widened = true`, signaling that the value is already boxed and should not be re-boxed.

### 4.6 MIR Function Table Alignment

All new helpers must be registered in `mir.c`'s function table for JIT resolution:

```c
// In func_list[]:
// Container unboxing
{"it2map",   (void*)it2map},
{"it2list",  (void*)it2list},
{"it2elmt",  (void*)it2elmt},
{"it2obj",   (void*)it2obj},
{"it2arr",   (void*)it2arr},
{"it2range", (void*)it2range},
{"it2path",  (void*)it2path},
{"it2p",     (void*)it2p},

// Container boxing
{"p2it",     (void*)p2it},

// Error ↔ Item conversion
{"err2it",   (void*)err2it},
{"it2err",   (void*)it2err},

// Safe boxing
{"push_d_safe", (void*)push_d_safe},
{"push_k_safe", (void*)push_k_safe},

// Ret* helpers
{"ri_ok",    (void*)ri_ok},
{"ri_err",   (void*)ri_err},
{"ri56_ok",  (void*)ri56_ok},
{"ri56_err", (void*)ri56_err},
{"ri64_ok",  (void*)ri64_ok},
{"ri64_err", (void*)ri64_err},
{"rf_ok",    (void*)rf_ok},
{"rf_err",   (void*)rf_err},
{"rs_ok",    (void*)rs_ok},
{"rs_err",   (void*)rs_err},
{"rm_ok",    (void*)rm_ok},
{"rm_err",   (void*)rm_err},
{"make_err", (void*)make_err},
```

---

## Implementation Plan

> **Status**: Phase 1 (steps 1.1–1.3, 1.5–1.7) and Phase 2 (steps 2.1–2.7) are **complete** as of 2026-03-04.
> All 572/572 Lambda baseline tests pass.
> All 2418 Radiant baseline tests pass.

### Phase 1: Foundation (Correctness)

**Goal**: Fix crashes and establish the structural primitives.

| Step | Change | Files | Status |
|------|--------|-------|--------|
| 1.1 | Add container unboxing helpers (`it2map`, `it2list`, etc.) | lambda.h, lambda.hpp | ✅ Done |
| 1.2 | Add `p2it()` container boxing helper | lambda.h, lambda.hpp | ✅ Done |
| 1.3 | Add `push_d_safe()`, `push_k_safe()` idempotent boxing | lambda-mem.cpp, lambda.h | ✅ Done |
| 1.4 | Add debug-mode boxing assertions | lambda.h (conditional) | Not started |
| 1.5 | Register all new helpers in MIR function table | mir.c | ✅ Done |
| 1.6 | Fix `define_func_call_wrapper()` — replace `(void*)` casts | transpile.cpp | ✅ Done |
| 1.7 | Fix `transpile_call_argument()` — replace blind container casts | transpile.cpp | ✅ Done |

**Validation**: Existing test suite passes 100% (572/572 Lambda, 2418/2418 Radiant).

### Phase 2: Ret* Types (Error Structure)

**Goal**: Introduce per-type `Ret*` structs and migrate error-returning functions.

| Step | Change | Files | Status |
|------|--------|-------|--------|
| 2.1 | Define all `Ret*` structs (2-field) and constructor helpers | lambda.h, lambda.hpp | ✅ Done |
| 2.2 | Add `err2it()`/`it2err()` converters and `item_to_ri()`/`ri_to_item()` shims | lambda.h, lambda.hpp | ✅ Done |
| 2.3 | Migrate `can_raise` user function native versions to return `RetItem` | transpile.cpp | ✅ Done |
| 2.4 | Migrate `can_raise` user function boxed wrappers (`_w`) to handle `RetItem` | transpile.cpp | ✅ Done |
| 2.5 | Update `?` propagation codegen to use `RetItem` | transpile.cpp | ✅ Done |
| 2.6 | Update `let a^err` destructuring codegen | transpile.cpp | ✅ Done |
| 2.7 | Migrate `can_raise` system functions to `Ret*` | lambda-eval.cpp, lambda-proc.cpp, transpile.cpp, path.c, mir.c | ✅ Done |

### Phase 3: Dual Version Generation

**Goal**: Two-version generation for user functions: **native** (typed params, native return) + **`_b` boxed** (Item params, RetItem return).

**Design decisions**:
- The **native function** keeps its current name (no `_n` suffix). E.g. `_square_15(int64_t x)` stays as-is.
- The **`_b` boxed wrapper** uses `_b` suffix. E.g. `_square_b_15(Item x)` → RetItem. Used by `fn_call*` dispatch.
- The **procedural `main()` function** only generates one boxed version (Item params, RetItem return). No unboxed/native variant needed for the entry point.
- The **functional script `_mod_main()`** likewise only generates one boxed version. It's the hidden entry point for top-level script evaluation.
- All other user functions (`fn` and `pn`) generate both native + `_b` versions.

| Step | Change                                                                                            | Files                            | Status              |
| ---- | ------------------------------------------------------------------------------------------------- | -------------------------------- | ------------------- |
| 3.1  | Clarify naming: native function keeps current name (no `_n` suffix); procedural `main()` is boxed-only | transpile.cpp                    | ✅ Done (doc only)  |
| 3.2  | Rewrite `define_func_call_wrapper()` as `define_func_boxed()` returning `RetItem`                 | transpile.cpp, transpile-mir.cpp | ✅ Done              |
| 3.3  | Fix `can_use_unboxed_call()`: return false when declared return is ANY (native main returns Item, not native scalar); update comments | transpile.cpp                    | ✅ Done              |
| 3.4  | Remove `define_func_unboxed()` (subsumed by native version); remove `in_unboxed_body` flag; change `_u` suffix to NULL in direct call path | transpile.cpp, ast.hpp           | ✅ Done              |
| 3.5  | Update `fn_call*` dispatch to use `_b` functions returning `RetItem` via `FN_FLAG_BOXED_RET` flag | lambda-eval.cpp, lambda.h        | ✅ Done              |

**Implementation notes (Phase 3)**:
- Added `resolve_native_ret_type()` helper in transpile.cpp to centralize return type resolution for native functions. Uses `fn_type->returned` if set, falls back to `body->type`.
- Content-block body inference (inferring `int64_t` return from last expression of `{ a+b }` when `fn_type->returned == ANY`) was investigated but **deferred** — changing the C return type creates a mismatch with callers that still see `ANY` (e.g. `transpile_call_argument` fast-path passes raw int64_t where Item is expected). Requires updating all call-site code paths first (Phase 4/5 candidate).
- `can_use_unboxed_call()` is currently defined but unused — retained for future content-block inference work.

### Phase 4: Data-Driven Metadata

**Goal**: Replace hardcoded switches with table lookups.

| Step | Change | Files | Status |
|------|--------|-------|--------|
| 4.1 | Add `CRetType`, `CArgConvention` to `SysFuncInfo` | ast.hpp | ✅ Done |
| 4.2 | Populate metadata in `sys_funcs[]` table | build_ast.cpp | ✅ Done |
| 4.3 | Create `TypeBoxInfo` table and `get_box_info()` | transpile.cpp | ✅ Done |
| 4.4 | Create `TypeNarrowEntry` table | transpile.cpp | Deferred (Phase 5 handles inline) |
| 4.5 | Refactor `transpile_box_item()` to use `try_box_scalar()` table lookup | transpile.cpp | ✅ Done |
| 4.6 | Create `emit_unbox_open()` / `emit_box_open()` / `emit_zero_value()` API | transpile.cpp | ✅ Done |
| 4.7 | Refactor `transpile_call_argument()` zero-default + param unboxing via table | transpile.cpp | ✅ Done |

### Phase 5: System Function Native Variants

**Goal**: Type-specialized system functions for hot paths.

| Step | Change | Files | Status |
|------|--------|-------|--------|
| 5.1 | Native `fn_mod_i`/`fn_idiv_i` for typed integer `%` and `//` operators | lambda-eval-num.cpp, transpile.cpp, build_ast.cpp | ✅ Done |
| 5.2 | Native `fn_len_l/a/s/e` for typed collections/strings | lambda-eval.cpp, transpile.cpp | ✅ Done |
| 5.3 | Wire transpiler to select native variants when types are known | transpile.cpp | ✅ Done |
| 5.4 | Register native variants in MIR table | mir.c | ✅ Done |

### Phase 6: MIR Transpiler Alignment

**Goal**: Port all improvements to the MIR direct transpiler.

| Step | Change | Files | Status |
|------|--------|-------|--------|
| 6.1 | Use container unboxing helpers in MIR path | transpile-mir.cpp | ✅ Done |
| 6.2 | Use `Ret*` for error-returning MIR functions | transpile-mir.cpp | Deferred (major arch change; tests pass with current `ITEM_ERROR` sentinel) |
| 6.3 | Use type narrowing table for MIR code emission | transpile-mir.cpp | ✅ Done |
| 6.4 | Port typed array construction (from C2MIR) | transpile-mir.cpp | ✅ Done (ArrayInt/ArrayFloat already implemented; ArrayInt64 gap is non-critical) |

---

## Implementation Notes

### Files Modified (Phase 6)

| File | What was changed |
|------|-----------------|
| `lambda/transpile-mir.cpp` | **6.1**: Added `emit_unbox_container()` and `emit_unbox_int_mask()` helpers (AND-mask with `0x00FFFFFFFFFFFFFF` to strip upper 8 type-tag bits). Extended `emit_unbox()` with cases for all container types (ARRAY, LIST, MAP, ELEMENT, OBJECT, RANGE, FUNC, TYPE, PATH, VMAP). Replaced 3 manual AND-mask patterns: index-assign (`arr[i] = val`), for-range offset/limit extraction. **6.3**: Updated `get_effective_type()` with IDIV/MOD early-return block — both-INT operands return `LMD_TYPE_INT`, float MOD returns `LMD_TYPE_FLOAT`. Rewrote `transpile_binary()` IDIV/MOD: 3-tier dispatch (native int via `fn_idiv_i`/`fn_mod_i`, native float via `fmod`, boxed fallback via `fn_idiv`/`fn_mod`). Updated `transpile_box_item()` boxing switches for BINARY nodes: added `OPERATOR_IDIV`/`OPERATOR_MOD` → `emit_box_int` in both_int case, added `OPERATOR_MOD` → `emit_box_float` in both_float and int_float cases. Added native `len()` dispatch: `fn_len_l` for LIST, `fn_len_a` for ARRAY variants, `fn_len_s` for STRING/SYMBOL, `fn_len_e` for ELEMENT (each passes unboxed pointer and returns native int64_t). |

### Phase 6 Boxing Correctness (Learned During Phase 6)

When adding native fast paths in the MIR direct transpiler, **three code locations** must agree on which operations return native values vs boxed Items:

1. **`get_effective_type()`**: Determines compile-time type of an expression result. Must return native TypeId (e.g., `LMD_TYPE_INT`) for operations that return native values.
2. **`transpile_binary()`**: Emits the actual MIR instructions. Must match `get_effective_type()` — if it returns a native register, `get_effective_type()` must return the corresponding native type.
3. **`transpile_box_item()`**: Decides whether to box the result. Must enumerate all cases where `transpile_binary()` returns native values and apply the appropriate `emit_box_*()`.

**INT64 exclusion**: INT64 is deliberately excluded from native IDIV/MOD paths because `transpile_expr()` returns inconsistent values for INT64 operands — raw int64 from literals but boxed Items from generic binary fallback. All INT64 arithmetic goes through the generic boxed path. See the existing `Note:` comment in `transpile_binary()` near the arithmetic ops section.

**MIR register types**: MIR enforces strict type checking at module finalization. If `fmod()` returns `MIR_T_D` (double) but the function signature expects `MIR_T_I64` return, MIR reports: `"unexpected operand mode for operand #1. Got 'double', expected 'int'"`. The fix is ensuring the result gets boxed via `emit_box_float()` before use as an Item.

### C/C++ Dual Compilation Constraint (Learned During Phase 1–2)

`lambda.h` is compiled in **two modes**: as C by the C2MIR JIT runtime, and as C++ by the regular compiler (via `lambda.hpp`). In C++ mode, `Item` is only forward-declared in `lambda.h` as `typedef struct Item Item;` — the full struct definition lives in `lambda.hpp`.

This means **any inline function in `lambda.h` that takes or returns `Item` by value will fail in C++ mode** with "incomplete type 'Item'" errors.

**Solution adopted**: All `Item`-using inline functions are guarded with `#ifndef __cplusplus` in `lambda.h` (C-only path for MIR JIT), with corresponding C++ versions defined in `lambda.hpp` after the full `Item` struct definition. Specifically:

| Function | `lambda.h` (C mode) | `lambda.hpp` (C++ mode) |
|----------|---------------------|-------------------------|
| `it2map`, `it2list`, `it2elmt`, etc. | `#ifndef __cplusplus` — uses `(uint64_t)item >> 56` for tag | After `struct Item` — uses `item._type_id` |
| `p2it`, `err2it`, `it2err` | `#ifndef __cplusplus` — returns `(Item)(uint64_t)` | After `struct Item` — returns `Item{.item = ...}` |
| `RetItem` typedef | `#ifndef __cplusplus` | After `struct Item` |
| `ri_ok`, `ri_err`, `item_to_ri`, `ri_to_item` | `#ifndef __cplusplus` | After `struct Item` |
| Non-Item `Ret*` types (`RetBool`, `RetMap`, etc.) | Unconditional (no `Item` field) | Same definitions visible to both |
| Non-Item constructors (`rb_ok`, `rm_ok`, etc.) | Unconditional | Same definitions visible to both |

**Key rule**: If a `static inline` function in `lambda.h` uses `Item` as a parameter type, return type, or local variable, it **must** be inside `#ifndef __cplusplus` with a C++ counterpart in `lambda.hpp`.

### MIR JIT `memcpy` Resolution (Learned During Phase 1–2)

C2MIR's compilation of struct-returning `static inline` functions (the `Ret*` constructors) generates implicit `memcpy` calls for struct copies. The MIR JIT linker requires all referenced symbols to be in the `func_list[]` resolution table.

**Fix**: Added `{"memcpy", (fn_ptr) memcpy}` to `func_list[]` in `mir.c`. Without this, **all** MIR JIT tests fail with `"failed to resolve native fn/pn: memcpy"`.

### Error Item Sentinel Approach (Learned During Phase 2.3–2.6)

Lambda's `fn_error()` returns `ItemError = {._type_id = LMD_TYPE_ERROR}` — a tagged zero with **no embedded `LambdaError*` pointer**. Error details are stored in the runtime context via `set_runtime_error()`, not in the Item itself.

This means `item_to_ri()` cannot extract a real `LambdaError*` from error Items (the lower 56 bits are 0). Initial implementations using `ri_err(it2err(fn_error(...)))` silently produced `RetItem{.err = NULL}`, losing the error.

**Solution adopted**: Use `.err` as a **boolean sentinel** (non-null = error, NULL = success):

```c
static inline RetItem item_to_ri(Item item) {
    RetItem r;
    r.value = item;  // ALWAYS store the original Item
    r.err = ((uint64_t)item >> 56 == LMD_TYPE_ERROR) ? (LambdaError*)1 : null;
    return r;
}

static inline Item ri_to_item(RetItem ri) {
    return ri.value;  // .value holds the actual value (error or normal)
}
```

This has cascading implications:
- **`^` propagation**: Forward the entire `RetItem` (`return _ri;`) instead of extracting `.err`
- **`let a^err` destructuring**: Error variable reads `_ri.value` (the error Item), not `err2it(_ri.err)`
- **`_w` wrapper**: `ri_to_item()` simply returns `.value` — works for both error and success

**Key rule**: `.err` is a flag, not a usable pointer. Never dereference it. Never pass it to `err2it()`.

### `transpile_box_item` Double-Wrapping (Learned During Phase 2.3–2.6)

When a `can_raise` function call has `^` propagation AND appears inside `transpile_box_item()`, both paths try to handle `RetItem → Item` conversion. The `^` propagation yields `Item` (via `_ri.value`), but `transpile_box_item()` also wraps in `ri_to_item()` — causing a type conflict (passing `Item` where `RetItem` is expected).

**Fix**: In `transpile_box_item()`, check `call_node->propagate` before applying `ri_to_item()`. When propagation is active, the `^` pattern already extracts `.value` as `Item`, so no wrapping is needed.

### Files Modified (Phase 1–2)

| File | What was added |
|------|---------------|
| `lambda/lambda.h` | `LambdaError` forward decl; C-only container unboxing (`it2map`–`it2p`); C-only `p2it`, `err2it`, `it2err`; all `Ret*` struct typedefs (non-Item ones unconditional, `RetItem` C-only); all non-Item constructors (`rb_ok`–`rp_err`); C-only `ri_ok`/`ri_err`/`item_to_ri`/`ri_to_item`; `push_d_safe`/`push_k_safe` declarations |
| `lambda/lambda.hpp` | C++ versions of: `it2map`–`it2p`, `p2it`, `err2it`, `it2err`, `RetItem` typedef, `ri_ok`, `ri_err`, `item_to_ri`, `ri_to_item` |
| `lambda/lambda-mem.cpp` | `push_d_safe()` and `push_k_safe()` implementations |
| `lambda/mir.c` | `memcpy` registration; `push_d_safe`/`push_k_safe`; all container unboxing/boxing helpers; all `Ret*` constructors; compatibility shims (~55 new entries) |

### Files Modified (Phase 1.6–1.7, Phase 2.3–2.6)

| File | What was changed |
|------|-----------------|
| `lambda/transpile.cpp` | Phase 1.6: `define_func_call_wrapper()` — replaced `(void*)` container casts with `it2map()`/`it2list()` etc., added `p2it()` boxing for container returns. Phase 1.7: `transpile_call_argument()` — replaced blind `(Type*)` casts with safe unbox helpers in normal and TCO paths. Phase 2.3: `can_raise` non-closure/non-method functions emit `RetItem` return type, function body wrapped in `item_to_ri()`. Phase 2.4: `_w` wrapper uses `ri_to_item()` for `can_raise`. Phase 2.5: `^` propagation uses `RetItem` temp var, forwards entire struct on error. Phase 2.6: `let a^err` dual path (RetItem for user can_raise calls, legacy Item for system functions). |
| `lambda/lambda.h` | `item_to_ri()` uses sentinel approach (`.value` = original Item, `.err` = flag). `ri_to_item()` simply returns `.value`. |
| `lambda/lambda.hpp` | C++ versions updated to match sentinel approach. |
| `lambda/lambda-embed.h` | Regenerated from updated `lambda.h`. |

### Files Modified (Phase 5)

| File | What was changed |
|------|-----------------|
| `lambda/lambda-eval-num.cpp` | `fn_mod_i` and `fn_idiv_i` updated with div-by-zero checks (return `INT64_ERROR` instead of UB). |
| `lambda/lambda-eval.cpp` | Added `fn_len_l(List*)`, `fn_len_a(Array*)`, `fn_len_s(String*)`, `fn_len_e(Element*)` — all `extern "C" int64_t`. |
| `lambda/lambda.h` | Declarations for `fn_len_l/a/s/e`; `extern double fmod(double, double)` for C2MIR. |
| `lambda/lambda-embed.h` | Regenerated from updated `lambda.h`. |
| `lambda/build_ast.cpp` | Added `OPERATOR_MOD` type inference: `std::max(left, right)` for numeric operands (was falling through to `LMD_TYPE_ANY`). |
| `lambda/transpile.cpp` | `transpile_binary_expr`: native fast paths for MOD (`fn_mod_i` for int, `fmod` for float) and IDIV (`fn_idiv_i` for int), with boxed fallbacks. `transpile_call_expr`: native `len()` dispatching for typed List/Array/String/Element. Updated `binary_already_returns_item()` and `is_idiv_expr()` to account for native-returning paths (critical fix for boxing correctness). |
| `lambda/mir.c` | Registered `fn_len_l`, `fn_len_a`, `fn_len_s`, `fn_len_e`, `fmod`. |

### Phase 5 Boxing/Unboxing Correctness (Learned During Phase 5)

When binary operators (`%`, `//`) switch from always-boxed (`fn_mod`, `fn_idiv` returning `Item`) to conditionally-native (`fn_mod_i` returning `int64_t`, `fmod` returning `double`), **three separate functions** must be updated to reflect that the C return type changed:

1. **`binary_already_returns_item()`**: Tells `transpile_box_item()` whether to skip boxing. Before: `MOD/IDIV → true` (always Item). After: returns `false` for native paths so `transpile_box_item` applies `i2it()` or `push_d()`.
2. **`is_idiv_expr()`**: Special-case check in let/assignment that adds `it2i()` unboxing for IDIV results. Before: always true. After: returns `false` when both operands are INT/INT64 (native `fn_idiv_i` already returns `int64_t`).

### Files Modified (Phase 4)

| File | What was changed |
|------|-----------------|
| `lambda/ast.hpp` | Added `CRetType` enum (10 values: C_RET_ITEM, C_RET_RETITEM, C_RET_INT64, C_RET_DOUBLE, C_RET_BOOL, C_RET_STRING, C_RET_SYMBOL, C_RET_DTIME, C_RET_TYPE_PTR, C_RET_CONTAINER) and `CArgConvention` enum (C_ARG_ITEM, C_ARG_NATIVE). Extended `SysFuncInfo` struct with `c_ret_type` and `c_arg_conv` fields (default 0 = C_RET_ITEM/C_ARG_ITEM). |
| `lambda/build_ast.cpp` | Populated ~36 `sys_funcs[]` entries with non-default `CRetType`/`CArgConvention`: int64-returning (len, int64, index_of, ord, bitwise ops), bool-returning (contains, starts_with, ends_with, exists), string-returning (string, format1/2), symbol-returning (name, symbol1), datetime-returning (9 DT funcs + now/today/justnow), double-returning (clock), RetItem-returning (all can_raise: input, parse, fetch, io ops, cmd), native-arg (bitwise ops). |
| `lambda/transpile.cpp` | Added `TypeBoxInfo` struct and `type_box_table[]` (22 entries mapping TypeId → c_type/unbox_fn/box_fn/const_box_fn/zero_value). Added `get_box_info()` lookup, `emit_unbox_open()`/`emit_box_open()`/`emit_zero_value()` helpers, and `try_box_scalar()` unified scalar boxing function. Refactored: `transpile_box_item()` type switch (9 scalar type cases → single `try_box_scalar()` call), `transpile_box_item()` ANY/ERROR native_ret boxing (6-way if/else chain → `emit_box_open()`), `transpile_primary_expr()` captured-var + param unboxing (per-type if/else chains → `get_box_info()` table lookup), `transpile_call_argument()` zero-default switch (4 cases → `emit_zero_value()`). |

### Phase 4 Design Decisions

**`TypeBoxInfo` table approach**: A flat array with linear scan via `get_box_info(TypeId)` rather than a direct-indexed array. TypeId values are not dense (there are gaps), and the table has only 22 entries, so linear scan is negligible overhead compared to the code emission it gates. The table centralizes all type-specific strings (C type names, boxing/unboxing function names, literal constructors, zero values) that were previously spread across multiple switch statements.

**`try_box_scalar()` unification**: The original `transpile_box_item()` had 9 separate case blocks for scalar types, each with slightly different combinations of optional-param/closure-param/captured-var checks. The unified function applies all three checks uniformly for all scalar types. This fixes latent issues where some types (BOOL, DTIME, DECIMAL, STRING) were missing `is_optional_param_ref()` checks — previously these would incorrectly attempt native boxing on an already-Item value.

**Phase 4.4 (TypeNarrowEntry) deferred**: The original plan called for a type-narrowing table, but Phase 5's inline native variants handle the hot-path narrowing cases (MOD, IDIV, len) directly. Additional narrowing can be added later as needed.

**`CArgConvention` scope**: Currently only `C_ARG_NATIVE` is used for the 6 bitwise operators (band/bor/bxor/bnot/shl/shr) which take `int64_t` directly instead of `Item`. This enables the transpiler to skip boxing when passing typed int arguments to these functions.
3. **`value_emits_native_type()`**: Uses `binary_already_returns_item()` transitively — automatically fixed.

**Key rule**: When adding a native fast path for a binary operator, always update `binary_already_returns_item()` to return `false` for the native case. Also search for operator-specific workarounds like `is_idiv_expr()`.

---

## Files to Modify

| File | Changes | Status |
|------|---------|--------|
| `lambda/lambda.h` | `Ret*` struct family (2-field); `err2it()`/`it2err()` converters; container unbox/box (C-only); `push_d_safe`/`push_k_safe` decls; debug assertions | ✅ Partially done (all except debug assertions) |
| `lambda/lambda.hpp` | C++ versions of Item-using inline helpers: `it2map`–`it2p`, `p2it`, `err2it`, `it2err`, `RetItem`, `ri_ok`/`ri_err`, `item_to_ri`/`ri_to_item` | ✅ Done |
| `lambda/lambda-mem.cpp` | `push_d_safe()`, `push_k_safe()` implementations | ✅ Done |
| `lambda/mir.c` | Register all new helpers + `memcpy` in function table (~55 new entries) | ✅ Done |
| `lambda/lambda-data.cpp` | Container unboxing implementations (moved to inline in lambda.h/lambda.hpp instead) | ✅ Done (approach changed) |
| `lambda/ast.hpp` | Extend `SysFuncInfo` with `CRetType`, `CArgConvention`, `native_variant` | ✅ Done (CRetType + CArgConvention; native_variant deferred) |
| `lambda/build_ast.cpp` | Populate new `SysFuncInfo` fields in `sys_funcs[]` table | ✅ Done (~36 entries with non-default CRetType/CArgConvention) |
| `lambda/transpile.cpp` | Dual version generation; `Ret*` codegen; table-driven boxing; emission API | ✅ Done (Phases 1–5: container casts, RetItem codegen, TypeBoxInfo table, try_box_scalar, emit helpers, native variants) |
| `lambda/transpile-mir.cpp` | MIR alignment (container unboxing, `Ret*`, narrowing table) | Not started |
| `lambda/print.cpp` | Update `write_type()` for `Ret*` return types | Not started |
| `lambda/lambda-eval.cpp` | Update `fn_call*` dispatch for `RetItem`; migrate `can_raise` sys funcs | Not started |
| `lambda/lambda-data-runtime.cpp` | System function native variants | Not started |

---

## Risk Assessment

| Risk | Severity | Mitigation |
|------|----------|------------|
| ABI change (`Item` → `RetItem`) breaks `fn_call*` dispatch | **High** | Phase 2–3 use compatibility shims; migrate incrementally |
| Container unboxing introduces NULL where previously there was a crash | **Medium** | Functions already need NULL handling (same contract as `it2s`) |
| `push_d_safe` NaN bit-pattern collision | **Low** | Extremely rare; add nursery pointer validation if needed |
| Dual version doubles code size | **Low** | `_b` is a single-line trampoline (< 100 bytes each); `Ret*` structs are typedef-only, no runtime cost |
| System function native variants increase symbol count | **Low** | Only generate for performance-critical hot-path functions |
| MIR table needs all new symbols | **Low** | Mechanical task; fail-fast on missing symbol during JIT link |
