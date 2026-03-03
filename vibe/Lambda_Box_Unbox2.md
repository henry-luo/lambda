# Lambda Box/Unbox — Structural Fix Proposal

## Overview

This proposal addresses a structural defect in the C transpiler's handling of typed function parameters and return values. The current implementation blindly casts Items to native container pointers (`Map*`, `List*`, etc.) without runtime type checks, causing **segfaults** when null or type-mismatched Items are passed to container-typed parameters.

The fix: replace blind pointer casts with proper **unbox-with-typecheck** helpers for all types, making the wrapper (`_w`) a correct type-checking trampoline between the boxed (Item ABI) and unboxed (native-typed) calling conventions.

See [Lambda_Box_Unbox.md](Lambda_Box_Unbox.md) for the broader dual-version optimization proposal.

---

## Current Architecture

### Three Function Versions

For a typed function like `fn list_length(node: map) int`, the transpiler generates up to three versions:

| Version | Suffix | Params | Return | Purpose |
|---------|--------|--------|--------|---------|
| **Main** | (none) | Native types (`Map*`) | Native (`int64_t`) | Core logic, called by direct callers with known types |
| **Wrapper** | `_w` | All `Item` | `Item` | Adaptor for `fn_call*` dynamic dispatch (accepts Items, calls Main) |
| **Unboxed** | `_u` | Native types | Native return | Avoids return-value boxing when caller can use native return directly |

### Generated Code (Current)

For `fn list_length(node: map) int`:

```c
// Main: native params, native return
int64_t _list_length42(Map* _node) {
    // ... body using _node as Map* ...
}

// Wrapper (_w): Item → Main → Item
Item _list_length_w42(Item _node) {
    return i2it(_list_length42((void*)_node));  // ← BLIND CAST: crashes on null
}

// Unboxed (_u): same as Main but only generated for INT/INT64 return
int64_t _list_length_u42(Map* _node) {
    // ... body (duplicate of Main) ...
}
```

### Code Locations

| Component | File | Lines | Function |
|-----------|------|-------|----------|
| Main function generation | transpile.cpp | 6148–6470 | `define_func()` |
| Wrapper (`_w`) generation | transpile.cpp | 6587–6698 | `define_func_call_wrapper()` |
| Unboxed (`_u`) generation | transpile.cpp | 6485–6584 | `define_func_unboxed()` |
| Call-site version selection | transpile.cpp | 319–410 | `can_use_unboxed_call()` |
| Per-argument type conversion | transpile.cpp | ~4400–4560 | `transpile_call_argument()` |
| C type mapping | print.cpp | 59–140 | `write_type()` |
| Scalar unboxing helpers | lambda-data.cpp | 202–300 | `it2d`, `it2b`, `it2i`, `it2l`, `it2s` |
| Scalar boxing helpers | lambda.h | 636–651 | `i2it`, `b2it`, `s2it`, `d2it`, etc. |

---

## Root Cause Analysis

### The Blind Cast Problem

The wrapper (`_w`) unboxes container-typed parameters with a bare `(void*)` cast:

```c
// define_func_call_wrapper(), transpile.cpp:6670-6679
default:
    // pointer types: cast from Item
    strbuf_append_str(tp->code_buf, "(void*)_");
    strbuf_append_str_n(tp->code_buf, param->name->chars, param->name->len);
```

This emits `(void*)_node` — reinterpreting the raw 64-bit Item value as a pointer with no type check.

The same pattern exists at **direct call sites** (`transpile_call_argument()`, line 4544–4556):

```c
else if (param_type->type_id == LMD_TYPE_MAP || ...) {
    strbuf_append_str(tp->code_buf, "(");
    write_type(tp->code_buf, (Type*)param_type);   // "Map*"
    strbuf_append_str(tp->code_buf, ")");
    transpile_box_item(tp, value);                    // raw Item → blind cast
}
```

### Why This Crashes

Lambda's `Item` is a 64-bit tagged value. The encoding differs between scalars and containers:

**Scalars** (int, bool, null, float, string, ...): type tag in top 8 bits, payload in lower 56 bits.
```
ItemNull  = 0x01_00000000000000   (LMD_TYPE_NULL=1 in top byte)
int 42    = 0x03_0000000000002A   (LMD_TYPE_INT=3 in top byte, value in lower bits)
```

**Containers** (Map, List, Element, ...): raw heap pointer, top byte = 0x00 (valid heap pointers are in low address space). The type_id is stored in the `Container` struct's first byte, read by dereferencing.
```
Map* ptr  = 0x00_007F81A0009C00   (top byte 0x00, rest is pointer)
```

The `type_id()` method uses this distinction:
```cpp
inline TypeId type_id() {
    if (this->_type_id) return this->_type_id;     // tagged scalar
    if (this->item) return *((TypeId*)this->item);  // container: deref pointer
    return LMD_TYPE_NULL;                            // null pointer (item == 0)
}
```

**When `(void*)ItemNull` is executed**: ItemNull = `0x0100000000000000`. Cast to pointer → address `0x0100000000000000` → invalid address → **segfault** on first dereference.

**When `(void*)int_item` is executed**: An int Item like `0x030000000000002A` becomes pointer `0x030000000000002A` → similarly invalid → **segfault**.

### Contrast with Scalars

Scalar unboxing helpers (`it2i`, `it2d`, `it2b`, `it2s`) all perform runtime type checks and return safe defaults on mismatch:

```c
int64_t it2i(Item itm) {
    if (itm._type_id == LMD_TYPE_INT) return itm.get_int56();
    if (itm._type_id == LMD_TYPE_INT64) return itm.get_int64();
    if (itm._type_id == LMD_TYPE_FLOAT) return (int64_t)itm.get_double();
    if (itm._type_id == LMD_TYPE_BOOL) return itm.bool_val ? 1 : 0;
    return 0;  // safe default
}
```

**No equivalent helpers exist for container types.** That's the structural gap.

---

## Affected Types

All container/pointer types that `write_type()` maps to native pointer types:

| Lambda Type | C Type | Has Unbox Helper? | Status |
|-------------|--------|-------------------|--------|
| `int` | `int64_t` | `it2i()` ✅ | Safe |
| `int64` | `int64_t` | `it2l()` ✅ | Safe |
| `float` | `double` | `it2d()` ✅ | Safe |
| `bool` | `bool` | `it2b()` ✅ | Safe |
| `string` | `String*` | `it2s()` ✅ | Safe |
| `map` | `Map*` | ❌ `(void*)` cast | **CRASHES** |
| `object` | `Object*` | ❌ `(void*)` cast | **CRASHES** |
| `element` | `Element*` | ❌ `(void*)` cast | **CRASHES** |
| `list` | `List*` | ❌ `(void*)` cast | **CRASHES** |
| `array` | `Array*` | ❌ `(void*)` cast | **CRASHES** |
| `symbol` | `Symbol*` | ❌ (not in wrapper) | Untested |
| `decimal` | `Decimal*` | ❌ (not in wrapper) | Untested |
| `datetime` | `DateTime` | ❌ (not in wrapper) | Untested |
| `path` | `Path*` | ❌ (not in wrapper) | Untested |

---

## Proposed Solution

### Design Principle

The wrapper (`_w`) should follow the same pattern as the existing scalar helpers:

1. **Unbox** each Item parameter to its native type with a runtime type check
2. **Call** the main function with native-typed arguments
3. **Box** the return value back to Item

On type mismatch: return `NULL` pointer (for containers) or `ItemError` (for the wrapper's Item return). The main function must handle NULL gracefully — this is consistent with how `it2s()` returns `nullptr` for non-string Items.

### Step 1: Add Container Unboxing Helpers

Add to `lambda.h` (for C/MIR JIT access) and implement in `lambda-data.cpp`:

```c
// Extract container pointer from Item, with type checking.
// Returns NULL if the Item is not the expected container type or is null.
// Safe: never dereferences invalid pointers.

static inline void* it2p(Item item) {
    // Container pointers have 0x00 in top byte (valid heap address).
    // Tagged scalars (int, bool, null, etc.) have non-zero top byte.
    if (item >> 56 != 0) return NULL;  // tagged type → not a container
    return (void*)item;                 // valid pointer or NULL (when item == 0)
}

// Type-specific variants (with container type_id check):
Map*     it2map(Item item);     // checks LMD_TYPE_MAP or LMD_TYPE_VMAP
List*    it2list(Item item);    // checks LMD_TYPE_LIST
Element* it2elmt(Item item);    // checks LMD_TYPE_ELEMENT
Object*  it2obj(Item item);     // checks LMD_TYPE_OBJECT
Array*   it2arr(Item item);     // checks LMD_TYPE_ARRAY (+ typed array variants)
Range*   it2range(Item item);   // checks LMD_TYPE_RANGE
Path*    it2path(Item item);    // checks LMD_TYPE_PATH
```

Implementation pattern:

```c
Map* it2map(Item item) {
    if (item >> 56 != 0) return NULL;  // tagged scalar (includes ItemNull)
    if (item == 0) return NULL;         // null pointer
    TypeId tid = *(TypeId*)item;        // read Container.type_id
    if (tid == LMD_TYPE_MAP || tid == LMD_TYPE_VMAP) return (Map*)item;
    return NULL;                         // wrong container type
}
```

The generic `it2p()` skips the container type_id check — useful when the param accepts multiple container types or when the function body already handles type dispatch.

### Step 2: Fix Wrapper Generation (`define_func_call_wrapper`)

Replace the blind `(void*)` cast in the `default:` case with proper unbox calls:

**Before** (transpile.cpp:6670–6679):
```c
default:
    strbuf_append_str(tp->code_buf, "(void*)_");
    strbuf_append_str_n(tp->code_buf, param->name->chars, param->name->len);
```

**After**:
```c
case LMD_TYPE_MAP:     unbox_fn = "it2map("; break;
case LMD_TYPE_OBJECT:  unbox_fn = "it2obj("; break;
case LMD_TYPE_ELEMENT: unbox_fn = "it2elmt("; break;
case LMD_TYPE_LIST:    unbox_fn = "it2list("; break;
case LMD_TYPE_ARRAY:
case LMD_TYPE_ARRAY_INT:
case LMD_TYPE_ARRAY_INT64:
case LMD_TYPE_ARRAY_FLOAT:
                       unbox_fn = "it2arr("; break;
case LMD_TYPE_RANGE:   unbox_fn = "it2range("; break;
case LMD_TYPE_PATH:    unbox_fn = "it2path("; break;
case LMD_TYPE_SYMBOL:  unbox_fn = "it2sym("; break;
case LMD_TYPE_DTIME:   unbox_fn = "it2k("; break;
case LMD_TYPE_DECIMAL: unbox_fn = "it2c("; break;
default:
    // Unknown type: pass Item through (fallback)
    strbuf_append_char(tp->code_buf, '_');
    strbuf_append_str_n(tp->code_buf, param->name->chars, param->name->len);
    break;
```

This makes the wrapper generate:
```c
Item _list_length_w42(Item _node) {
    return i2it(_list_length42(it2map(_node)));  // safe: returns NULL for non-map
}
```

### Step 3: Fix Call-Site Conversion (`transpile_call_argument`)

Replace the blind cast at direct call sites (transpile.cpp:4544–4556):

**Before**:
```c
else if (param_type->type_id == LMD_TYPE_MAP || ...) {
    if (value_emits_native_type(tp, value, param_type->type_id)) {
        transpile_expr(tp, value);
    } else {
        strbuf_append_str(tp->code_buf, "(");
        write_type(tp->code_buf, (Type*)param_type);
        strbuf_append_str(tp->code_buf, ")");
        transpile_box_item(tp, value);
    }
}
```

**After**:
```c
else if (param_type->type_id == LMD_TYPE_MAP || ...) {
    if (value_emits_native_type(tp, value, param_type->type_id)) {
        transpile_expr(tp, value);
    } else {
        // Use type-checking unbox helper instead of blind cast
        const char* unbox = get_container_unbox_fn(param_type->type_id);
        strbuf_append_str(tp->code_buf, unbox);
        transpile_box_item(tp, value);
        strbuf_append_char(tp->code_buf, ')');
    }
}
```

Where `get_container_unbox_fn()` maps type_id → helper name:
```c
const char* get_container_unbox_fn(TypeId tid) {
    switch (tid) {
    case LMD_TYPE_MAP:     return "it2map(";
    case LMD_TYPE_OBJECT:  return "it2obj(";
    case LMD_TYPE_ELEMENT: return "it2elmt(";
    case LMD_TYPE_LIST:    return "it2list(";
    case LMD_TYPE_ARRAY:   return "it2arr(";
    case LMD_TYPE_RANGE:   return "it2range(";
    case LMD_TYPE_PATH:    return "it2path(";
    default:               return "it2p(";  // generic fallback
    }
}
```

### Step 4: Add Container Boxing Helpers (Return Path)

The wrapper's return-boxing `switch` (transpile.cpp:6630–6650) already handles scalars but falls through to `default: break` for containers. This is actually **correct** for containers because a `Map*` return value IS a valid Item (top byte 0x00, type_id in the Container struct). No explicit boxing needed.

However, for completeness and to handle potential NULL returns, we may want to add:

```c
case LMD_TYPE_MAP:
case LMD_TYPE_OBJECT:
case LMD_TYPE_ELEMENT:
case LMD_TYPE_LIST:
case LMD_TYPE_ARRAY:
    // Container pointers are valid Items (top byte 0x00).
    // But if the function returns NULL, we should return ItemNull not 0.
    box_prefix = "p2it("; break;  // p2it: NULL→ItemNull, else pass-through
```

Where:
```c
static inline Item p2it(void* ptr) {
    return ptr ? (Item)(uint64_t)ptr : ITEM_NULL;
}
```

This prevents a NULL Map* from becoming Item value 0 (ITEM_UNDEFINED) instead of ITEM_NULL.

### Step 5: Register Helpers in MIR

Add all new helpers to the MIR function table in `mir.c` so the JIT can resolve them:

```c
// In the func_list array:
{"it2map",   (void*)it2map},
{"it2list",  (void*)it2list},
{"it2elmt",  (void*)it2elmt},
{"it2obj",   (void*)it2obj},
{"it2arr",   (void*)it2arr},
{"it2range", (void*)it2range},
{"it2path",  (void*)it2path},
{"it2p",     (void*)it2p},
{"p2it",     (void*)p2it},
```

---

## Generated Code After Fix

### Example: `fn list_length(node: map) int`

```c
// Main: native params, native return (unchanged)
int64_t _list_length42(Map* _node) {
    if (_node == NULL) return 0;  // null check (original logic)
    // ... body ...
}

// Wrapper (_w): proper unboxing with type check
Item _list_length_w42(Item _node) {
    return i2it(_list_length42(it2map(_node)));
}
```

### Example: `fn tail(x: map, y: map, z: map) map`

```c
// Main
Map* _tail55(Map* _x, Map* _y, Map* _z) { ... }

// Wrapper: all params properly unboxed, return properly boxed
Item _tail_w55(Item _x, Item _y, Item _z) {
    return p2it(_tail55(it2map(_x), it2map(_y), it2map(_z)));
}
```

### What Happens on Null/Mismatch

| Input Item | `it2map(item)` returns | Behavior |
|-----------|----------------------|----------|
| Valid `Map*` (0x00007F...) | `Map*` pointer | Normal execution |
| `ItemNull` (0x0100000000000000) | `NULL` | Safe — function gets NULL, handles gracefully |
| `int 42` (0x030000000000002A) | `NULL` | Safe — type mismatch returns NULL |
| `List*` pointer | `NULL` | Safe — wrong container type |
| `0` (ITEM_UNDEFINED) | `NULL` | Safe — null pointer |

---

## The `_u` Version: Current Limitations

### Current State

`define_func_unboxed()` only generates `_u` for functions with `INT` or `INT64` return types. `can_use_unboxed_call()` only selects `_u` at call sites for INT returns.

### Why This is Separate

The `_u` version is an **optimization** (avoid return-value boxing) while the wrapper fix is a **correctness** issue. They should be addressed independently.

### Future Extension

Once the wrapper is fixed, the `_u` version could be extended to more return types — but the main function already uses native types for params AND return, so `_u` would be identical to main for most cases. The real value of `_u` is when the main function returns `Item` (inferred ANY) but the body actually produces a specific native type — then `_u` avoids intermediate boxing.

---

## Implementation Plan

### Phase 1: Correctness Fix (Critical)

1. **Add container unboxing helpers** (`it2map`, `it2list`, etc.) to `lambda-data.cpp` / `lambda.h`
2. **Fix `define_func_call_wrapper()`** — replace `(void*)` cast with proper unbox calls
3. **Fix `transpile_call_argument()`** — replace blind container cast with unbox calls
4. **Add `p2it()` boxing helper** for container return values
5. **Register new helpers in `mir.c`** function table
6. **Test**: `list2.ls` benchmark should work with `node: map` (not `map?`)

### Phase 2: MIR Path Alignment

The MIR transpiler (`transpile-mir.cpp`) happens to be safer because it keeps container params as boxed Items (LMD_TYPE_ANY). However, this means it misses the optimization opportunity. Once the C transpiler is fixed, align the MIR path to use the same unboxing helpers.

### Phase 3: Broader Optimization (from Lambda_Box_Unbox.md)

With the correctness foundation in place, implement the full `_b`/`_u` dual-version optimization:
- Boxed `_b` version with full type checking (extends current `_w`)
- Call-site resolution: static match → `_u`, unknown type → `_b`

---

## Files to Modify

| File | Change |
|------|--------|
| `lambda/lambda.h` | Add `it2map`, `it2list`, `it2elmt`, `it2obj`, `it2arr`, `it2range`, `it2path`, `it2p`, `p2it` declarations |
| `lambda/lambda-data.cpp` | Implement container unboxing helpers |
| `lambda/transpile.cpp` | Fix `define_func_call_wrapper()` and `transpile_call_argument()` |
| `lambda/mir.c` | Register new helpers in func_list |
| `test/benchmark/awfy/list2.ls` | Revert `map?` back to `map` to validate the fix |

---

## Risk Assessment

- **Low risk**: Unboxing helpers are simple, self-contained functions with no side effects
- **Backward compatible**: Only changes the generated wrapper code — untyped functions are unaffected
- **Performance**: `it2map()` adds a branch (check top byte) vs the blind cast — negligible cost since the wrapper is already a trampoline
- **NULL handling**: Functions receiving NULL from `it2map()` must handle it. This is the same contract as `it2s()` returning `nullptr` — callers already deal with NULL strings
