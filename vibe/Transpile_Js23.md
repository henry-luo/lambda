# JavaScript Transpiler v23: Performance Tuning — Transpile & Execution Optimization

## 1. Executive Summary

This phase focuses on **reducing per-test transpilation cost and improving execution throughput** of the Lambda JS engine. The primary strategies are:

1. **C Runtime Facade** — Define compact C wrapper modules that combine multiple runtime calls into single MIR imports, reducing emitted MIR instruction count and MIR compilation/linking overhead.
2. **Native System Function Promotion** — Follow the Lambda transpiler's two-tier pattern (`fn_wrapper` + `native_c_func`) to call C library and runtime functions directly for known-type operands, bypassing boxed dispatch.
3. **Inline MIR Expansion** — Emit inline MIR sequences for frequent micro-operations (type checks, truthiness, tag extraction) instead of calling into runtime.
4. **MIR Module Reduction** — Reduce the number of MIR proto/import items per compilation to speed up MIR linking and code generation.

### Baseline (v22 — Run 48)

| Metric | Value |
|--------|-------|
| Passing tests | 13,414 / 27,089 (49.5%) |
| Median per-test time | 7.9 ms |
| p99 per-test time | 137 ms |
| MIR opt level (batch) | O0 |
| Phase 2 wall clock | ~71 s |

### Targets

| Metric | Target |
|--------|--------|
| Median per-test time | ≤ 5 ms (−37%) |
| Phase 2 wall clock | ≤ 55 s (−23%) |
| Passing tests | ≥ 13,414 (no regressions) |

Correctness must not regress. All optimizations must be semantics-preserving.

---

## 2. Architecture Context

### Current Cost Breakdown (per test, median ~8 ms)

| Phase | Approx % | Notes |
|-------|----------|-------|
| Tree-sitter parse | ~5% | Already fast (C, incremental) |
| AST build | ~5% | Single pass over CST |
| **MIR emission** | **~25%** | Traverses AST, emits MIR insns + proto/import items |
| **MIR link + JIT** | **~40%** | `MIR_link()` resolves imports; `MIR_gen()` does regalloc + codegen |
| **Execution** | **~25%** | JIT-compiled code calling runtime C functions |

The MIR link+JIT phase dominates because:
- Each test creates **80–200 import items** (one per unique runtime function referenced), each requiring hashmap lookup during linking.
- Each test creates a corresponding **proto item** per import (function signature).
- MIR O0 still does register allocation, which is O(n) in MIR instruction count.

### Existing Optimization Infrastructure

The JS transpiler already has a multi-tier optimization framework:

| Phase | Optimization | Status |
|-------|-------------|--------|
| P3 | Shape-aware constructor slot access | ✅ Done |
| P3.5 | `typeof x === "type"` narrowing | ✅ Done |
| P4 | Native function parameter/return type inference | ✅ Done |
| P5 | Math method compile-time resolution (sqrt, sin, etc.) | ✅ Done |
| P7 | Typed receiver native method calls | ✅ Done |
| P9 | Typed array inline loads | ✅ Done |
| A2 | Known-array inline bounds check | ✅ Done |
| A4 | `js_array_get_int()` fast path | ✅ Done |

**Arithmetic already has native fast paths** when both operands are typed:
- INT+INT → `MIR_ADD`, INT<INT → `MIR_LTS`, FLOAT*FLOAT → `MIR_DMUL`, etc.

**The problem**: Most test262 code has untyped variables (no annotation, no inference), so the transpiler falls through to boxed runtime dispatch (e.g., `js_add()`, `js_less_than()`, `js_property_access()`).

---

## 3. Stage 1: C Runtime Facade Functions

### 3.1 Problem

For untyped code, a simple expression like `a + b > 0` emits:

```
call js_property_access(env, "a")    → import + proto + call insn
call js_property_access(env, "b")    → import + proto + call insn
call js_add(reg_a, reg_b)           → import + proto + call insn
call js_make_int_item(0)            → import + proto + call insn
call js_greater_than(reg_sum, reg_0) → import + proto + call insn
call js_is_truthy(reg_gt)           → import + proto + call insn
```

That's **6 MIR call instructions** with **6 import+proto pairs** (12 items total). Each `MIR_new_call_insn` also has 4+ operands (proto ref, import ref, result reg, args). The MIR instruction stream is bulky, and linking must resolve all 6 imports.

### 3.2 Proposal: Compound Facade Functions

Define a small set of **C facade functions** that bundle frequent runtime call sequences into a single call, registered in `jit_runtime_imports[]` for MIR linking.

#### Category A: Comparison-with-Coerce Facades

These combine the pattern `js_xxx(js_to_number(a), js_to_number(b))`:

```c
// js_facade.c (new file, or section in js_runtime.cpp)

extern "C" Item js_add_items(Item a, Item b);
// Equivalent to js_add(a, b) — already exists, keep as-is

extern "C" int64_t js_lt_ii(Item a, Item b);
// If both items are tagged ints, compare directly (no boxing result).
// Returns 1/0 as raw int64 for direct use in MIR_BF/MIR_BT.
// Fallback: call js_less_than() and extract boolean.

extern "C" int64_t js_gt_ii(Item a, Item b);
extern "C" int64_t js_eq_ii(Item a, Item b);    // strict equal
extern "C" int64_t js_seq_ii(Item a, Item b);   // abstract equal
extern "C" int64_t js_truthy(Item a);
// Returns raw int64 1/0 instead of Item(true)/Item(false).
// Saves the box+unbox cycle for if/for/while conditions.
```

**Key insight**: The current `js_less_than()` returns a boxed `Item` (ItemTrue/ItemFalse), then `js_is_truthy()` unboxes it. Returning a raw `int64_t` eliminates an entire call.

#### Category B: Property Access + Method Call Facades

Many test262 patterns look like:
```js
assert.sameValue(x, y);
// Transpiles to: get 'assert' → get 'sameValue' → call_function
```

This generates 3+ runtime calls. A facade can collapse common patterns:

```c
extern "C" Item js_method_call_2(Item object, const char* method_name,
                                  Item arg1, Item arg2);
// Equivalent to:
//   Item method = js_property_get(object, js_make_string(method_name));
//   return js_call_function(method, object, &args, 2);

extern "C" Item js_method_call_1(Item object, const char* method_name, Item arg1);
extern "C" Item js_method_call_0(Item object, const char* method_name);
```

**Benefit**: Each collapses 2-3 MIR call instructions + 2-3 import/proto pairs into 1.

#### Category C: Compound Truthiness Tests

```c
extern "C" int64_t js_typeof_is(Item value, const char* type_str);
// Returns 1 if typeof(value) === type_str, 0 otherwise.
// Avoids: js_typeof() → string creation → strcmp.

extern "C" int64_t js_is_null_or_undefined(Item value);
// Single bit-pattern check. Avoids calling js_strict_equal twice + js_logical_or.

extern "C" int64_t js_instanceof_check(Item object, Item constructor);
// Combines prototype chain walk into single call.
```

### 3.3 Implementation Plan

1. **Define facades** in a new section of `lambda/js/js_runtime.cpp` (or `js_facade.cpp` if preferred) with `extern "C"` linkage.
2. **Register** in `jit_runtime_imports[]` array in `lambda/mir.c`. No changes to `sys_func_registry.c` needed (these are JS-only).
3. **Emit** from transpiler: in `jm_transpile_binary()`, detect the boxed fallback path and emit the facade call instead:

```cpp
// Before (boxed path):
case JS_OP_LT: fn_name = "js_less_than"; break;
// ... then later: jm_call_2(mt, fn_name, MIR_T_I64, ...) → returns Item
// ... then: jm_call_1(mt, "js_is_truthy", ...) → returns Item
// ... then: unbox for BF/BT

// After (facade path, when result used as branch condition):
MIR_reg_t cond = jm_call_2(mt, "js_lt_ii", MIR_T_I64,
    MIR_T_I64, left_op, MIR_T_I64, right_op);
// cond is already raw int64 0/1, use directly in MIR_BF
jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF, label_op, MIR_new_reg_op(mt->ctx, cond)));
```

### 3.4 Expected Savings

| Pattern | Before (calls) | After (calls) | Import reduction |
|---------|---------------|---------------|-----------------|
| `a < b` in `if`/`for` | 2 (js_less_than + js_is_truthy) | 1 (js_lt_ii) | −2 items |
| `typeof x === "string"` | 3 (js_typeof + string_create + js_strict_equal) | 1 (js_typeof_is) | −4 items |
| `x == null` | 2 (js_equal + js_is_truthy) | 1 (js_is_null_or_undefined) | −2 items |
| `obj.method(a, b)` | 3 (get_prop + get_prop + call_function) | 1 (js_method_call_2) | −4 items |

Estimated: **20–40% reduction in emitted MIR items** for typical test262 tests.

---

## 4. Stage 2: Inline MIR Micro-Operations

### 4.1 Problem

Several frequent runtime calls perform trivially simple operations that cost more in call overhead (push/pop frame, argument passing) than in actual work:

| Function | Work done | Call overhead vs work |
|----------|-----------|----------------------|
| `js_is_truthy(item)` | Extract tag bits, compare | 5× |
| `js_make_int_item(n)` | Shift + OR tag | 10× |
| `js_typeof(item)` | Extract tag, switch | 3× |
| `it2i(item)` / `i2it(n)` | Shift or mask | 10× |

### 4.2 Proposal: Emit Inline MIR Sequences

Instead of calling `js_is_truthy(item)`, emit the equivalent MIR directly:

```cpp
// Inline js_is_truthy for common fast paths:
// Most Items: truthy if not 0, not ItemNull, not ItemFalse, not ItemUndefined
//
// Fast check: value != ItemNull && value != ItemFalse && value != ItemUndefined && value != 0
// These are all known 64-bit constants.

static MIR_reg_t jm_emit_inline_truthy(JsMirTranspiler* mt, MIR_reg_t item_reg) {
    MIR_reg_t r = jm_new_reg(mt, "truthy", MIR_T_I64);
    MIR_label_t l_false = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);

    // Check: item == ItemNull (known constant)
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BEQ, MIR_new_label_op(mt->ctx, l_false),
        MIR_new_reg_op(mt->ctx, item_reg), MIR_new_int_op(ITEM_NULL_BITS)));
    // Check: item == ItemFalse
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BEQ, MIR_new_label_op(mt->ctx, l_false),
        MIR_new_reg_op(mt->ctx, item_reg), MIR_new_int_op(ITEM_FALSE_BITS)));
    // Check: item == ItemUndefined
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BEQ, MIR_new_label_op(mt->ctx, l_false),
        MIR_new_reg_op(mt->ctx, item_reg), MIR_new_int_op(ITEM_UNDEFINED_BITS)));
    // Check: item == 0 (int zero)
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BEQ, MIR_new_label_op(mt->ctx, l_false),
        MIR_new_reg_op(mt->ctx, item_reg), MIR_new_int_op(ITEM_INT_ZERO_BITS)));

    // True path
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, r), MIR_new_int_op(1)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));

    // False path
    jm_emit_label(mt, l_false);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, r), MIR_new_int_op(0)));

    jm_emit_label(mt, l_end);
    return r;  // raw int64 0/1
}
```

**Caveat**: This inline version handles the **fast falsy paths** (null, false, undefined, int-zero) but misses edge cases like `NaN`, empty string `""`, and `-0`. Two strategies:
- **Conservative**: Only inline when the item is known NOT to be a string/float (via type inference). Fall back to `js_is_truthy()` call otherwise.
- **Aggressive**: Inline with a fallback branch — if the tag indicates string/float, call the full `js_is_truthy()`. This is still profitable if >80% of truthiness checks hit the fast path.

### 4.3 Inline int-item boxing/unboxing

```cpp
// i2it(n): int64 → Item (tagged)
// Item encoding: (value << TAG_BITS) | INT_TAG
static MIR_reg_t jm_emit_inline_i2it(JsMirTranspiler* mt, MIR_reg_t int_reg) {
    MIR_reg_t r = jm_new_reg(mt, "i2it", MIR_T_I64);
    MIR_reg_t shifted = jm_new_reg(mt, "shl", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_LSH,
        MIR_new_reg_op(mt->ctx, shifted),
        MIR_new_reg_op(mt->ctx, int_reg),
        MIR_new_int_op(TAG_SHIFT)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_OR,
        MIR_new_reg_op(mt->ctx, r),
        MIR_new_reg_op(mt->ctx, shifted),
        MIR_new_int_op(INT_TYPE_TAG)));
    return r;
}

// it2i(item): Item (tagged int) → int64
static MIR_reg_t jm_emit_inline_it2i(JsMirTranspiler* mt, MIR_reg_t item_reg) {
    MIR_reg_t r = jm_new_reg(mt, "it2i", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_RSH,
        MIR_new_reg_op(mt->ctx, r),
        MIR_new_reg_op(mt->ctx, item_reg),
        MIR_new_int_op(TAG_SHIFT)));
    return r;
}
```

This eliminates the most frequent import: `it2i` and `i2it` appear in virtually every compiled function.

### 4.4 Expected Savings

| Operation | Before | After | Saving per occurrence |
|-----------|--------|-------|-----------------------|
| `js_is_truthy()` | 1 call (4 MIR ops) | 6-8 inline MIR insns | Eliminates 1 import+proto + call overhead |
| `i2it()` / `it2i()` | 1 call each | 2-3 inline insns each | Eliminates 2 import+proto pairs |
| `js_make_int_item(n)` | 1 call | 2 inline insns | Eliminates 1 import+proto |

Per typical function: eliminates **4-8 import/proto pairs** and **4-8 call instructions**.

---

## 5. Stage 3: Import Deduplication and Caching

### 5.1 Problem

The current `jm_ensure_import()` creates import+proto items per **function per module**. Since each test compiles as its own MIR module, the same runtime functions (js_add, js_property_access, js_call_function, etc.) are re-imported for every test.

With harness pre-compilation (v21), the preamble module already imports many functions. But test modules re-import the same symbols independently.

### 5.2 Proposal: Shared Import Proto Pool

Pre-define a static array of `MIR_item_t` protos for the ~50 most commonly used runtime functions. At module creation, bulk-insert these protos and imports in one pass instead of lazily creating them per-encounter.

```c
// Pre-defined import table (compiled once, reused per module)
static const struct {
    const char* name;
    MIR_type_t ret_type;
    int arg_count;
    MIR_type_t arg_types[6];
} JS_COMMON_IMPORTS[] = {
    {"js_add",             MIR_T_I64, 2, {MIR_T_I64, MIR_T_I64}},
    {"js_subtract",        MIR_T_I64, 2, {MIR_T_I64, MIR_T_I64}},
    {"js_multiply",        MIR_T_I64, 2, {MIR_T_I64, MIR_T_I64}},
    {"js_divide",          MIR_T_I64, 2, {MIR_T_I64, MIR_T_I64}},
    {"js_less_than",       MIR_T_I64, 2, {MIR_T_I64, MIR_T_I64}},
    {"js_greater_than",    MIR_T_I64, 2, {MIR_T_I64, MIR_T_I64}},
    {"js_strict_equal",    MIR_T_I64, 2, {MIR_T_I64, MIR_T_I64}},
    {"js_equal",           MIR_T_I64, 2, {MIR_T_I64, MIR_T_I64}},
    {"js_property_access", MIR_T_I64, 2, {MIR_T_I64, MIR_T_I64}},
    {"js_property_set",    MIR_T_I64, 3, {MIR_T_I64, MIR_T_I64, MIR_T_I64}},
    {"js_call_function",   MIR_T_I64, 4, {MIR_T_I64, MIR_T_I64, MIR_T_I64, MIR_T_I64}},
    {"js_is_truthy",       MIR_T_I64, 1, {MIR_T_I64}},
    {"js_to_number",       MIR_T_I64, 1, {MIR_T_I64}},
    {"js_to_string",       MIR_T_I64, 1, {MIR_T_I64}},
    {"js_typeof",          MIR_T_I64, 1, {MIR_T_I64}},
    {"js_set_module_var",  MIR_T_V,   2, {MIR_T_I64, MIR_T_I64}},
    {"js_get_module_var",  MIR_T_I64, 1, {MIR_T_I64}},
    // ... ~35 more common imports
    {NULL, 0, 0, {}}
};
```

At module init, iterate this table and pre-populate the import hashmap. This changes import creation from **O(n) lazy per-encounter** to **O(1) batch up-front** — reducing hashmap growth/resizing during transpilation.

### 5.3 Expected Savings

- Eliminates hashmap resizing during transpilation (pre-sized correctly)
- Reduces per-function import creation overhead
- Estimated **5–10% reduction** in transpilation phase time

---

## 6. Stage 4: Speculative Type Widening for Loop Variables

### 6.1 Problem

Most `for` loops in test262 use patterns like:
```js
for (var i = 0; i < arr.length; i++) { ... }
```

The transpiler already optimizes when `i` has a known INT type. But many variables start as untyped (from `var` declarations), so the loop body falls through to boxed paths.

### 6.2 Proposal: Loop Counter Type Speculation

When the transpiler encounters a `for` statement where:
1. The init is `var i = <integer literal>` or `let i = <integer literal>`
2. The update is `i++`, `i--`, `i += <integer>`, or `i -= <integer>`
3. The test compares `i` against a bound (`i < x`, `i <= x`, `i >= 0`, etc.)

**Speculate** that `i` is INT for the scope of the loop. This is already partially done (the for-loop tier-1 optimization), but extend it to:

- Mark the variable as `LMD_TYPE_INT` in `jm_var_entries` for the loop scope
- All uses of `i` inside the loop body emit native int operations
- Emit a **type guard** at loop entry: if `i` is not an int at runtime, jump to a slow (boxed) fallback loop. This guard costs 1 branch instruction but saves N×(unbox+rebox) per iteration.

```
; Type guard at loop entry
    MOV i_raw, i_item
    AND tag, i_raw, TAG_MASK
    BNE tag, INT_TAG, L_slow_loop    ; fallback if not int

L_fast_loop:
    ; All operations on i use native int registers
    RSH i_val, i_raw, TAG_SHIFT      ; extract int value once
    ...
    ADD i_val, i_val, 1               ; i++ (native)
    LSH i_raw, i_val, TAG_SHIFT      ; retag
    OR  i_raw, i_raw, INT_TAG
    BLT i_val, bound, L_fast_loop    ; native comparison

L_slow_loop:
    ; Original boxed code (for non-int edge case)
    ...
```

### 6.3 Scope

This benefits any `for` loop with an integer-initialized counter — estimated ~60% of `for` loops in test262.

---

## 7. Stage 5: String Interning for Property Names

### 7.1 Problem

Property access `obj.foo` currently:
1. Transpiler emits `js_make_string("foo")` → allocates/interns a string Item
2. Calls `js_property_access(obj, string_item)`
3. Inside `js_property_access`, extracts the `char*` from the string Item to do a hash lookup

The string creation + extraction is pure overhead when the property name is a compile-time constant.

### 7.2 Proposal: Pre-Interned Property Name Constants

During transpilation, collect all string-literal property names used. Emit them as MIR `BSS` or `DATA` items (module-level constant slots). At module init, call a batch interning function:

```c
extern "C" void js_intern_prop_names(const char** names, Item* slots, int count);
// Pre-interns all property name strings into Items stored in BSS slots.
// Called once at module init. Each slot is a valid Item for the lifetime of the module.
```

Then property access becomes:
```
; Before:
call js_make_string("length")       → import + call
call js_property_access(obj, str)   → import + call

; After:
LOAD str_item, [bss_slot_for_length]  → 1 MIR insn (no call)
call js_property_access(obj, str_item) → 1 call
```

This eliminates one call per property access for compile-time-known property names.

### 7.3 Further: Direct C-String Property Access

Even better, define a specialized function:

```c
extern "C" Item js_property_get_str(Item object, const char* key);
// Accepts raw C string, does hash lookup directly without Item boxing.
// Eliminates both js_make_string AND the string-to-char* extraction inside property_access.
```

The transpiler emits the string literal as a MIR data item and passes the pointer directly. This is safe because MIR data items live for the module lifetime.

---

## 8. Stage 6: Reduce Runtime Function Dispatch Cost

### 8.1 Problem: Built-in Method Dispatch

`js_dispatch_builtin()` is a giant switch with 100+ cases. Every `Array.prototype.push()`, `String.prototype.indexOf()`, etc. goes through:

1. `js_call_function()` → checks func type, extracts builtin_id
2. `js_dispatch_builtin(builtin_id, this, args, argc)` → switch on 100+ cases
3. Actual method implementation

### 8.2 Proposal: Direct Method Pointer Tables

For known receiver types (detected at transpile time or via inline cache), bypass the dispatch:

```c
// Method pointer tables indexed by builtin_id
// These are already registered as extern "C" — just need transpiler to call them directly.

// Example: Array.prototype.push is builtin_id = JS_BUILTIN_ARRAY_PUSH
// Instead of: js_call_function(push_func, arr, args, 1)
// Emit:       js_array_push(arr, args[0])
```

The transpiler already does this for Math methods (Phase 5). Extend the pattern to:
- **Array methods**: `push`, `pop`, `shift`, `unshift`, `indexOf`, `includes`, `slice`, `splice`, `map`, `filter`, `forEach`, `reduce`, `join`
- **String methods**: `indexOf`, `includes`, `slice`, `substring`, `charAt`, `charCodeAt`, `split`, `trim`, `toLowerCase`, `toUpperCase`, `replace`
- **Object static methods**: `Object.keys`, `Object.values`, `Object.entries`, `Object.assign`, `Object.create`, `Object.defineProperty`

Detection pattern:
```cpp
// In jm_transpile_call_expr(), when callee is member expression:
if (callee->type == JS_AST_MEMBER && callee->is_dot) {
    const char* method = callee->property_name;
    TypeId receiver_type = jm_get_effective_type(mt, callee->object);

    // Known array receiver
    if (receiver_type == LMD_TYPE_ARRAY) {
        if (strcmp(method, "push") == 0 && arg_count == 1) {
            return jm_call_2(mt, "js_array_push", MIR_T_I64,
                MIR_T_I64, obj_reg, MIR_T_I64, arg0_reg);
        }
        if (strcmp(method, "indexOf") == 0 && arg_count == 1) {
            return jm_call_2(mt, "js_array_index_of", MIR_T_I64,
                MIR_T_I64, obj_reg, MIR_T_I64, arg0_reg);
        }
        // ...
    }
}
```

### 8.3 Expected Savings

Eliminates 2 levels of indirection (js_call_function → js_dispatch_builtin → actual method) for ~40% of method calls in typical test262 code.

---

## 9. Stage 7: MIR Code Size Reduction

### 9.1 Problem

Large MIR functions slow down register allocation (even at O0). The main sources of code bloat are:

1. **Repeated env variable loads**: `js_get_module_var(N)` called every time a module-scope variable is read, even within the same basic block.
2. **Redundant type conversions**: `i2it` → `it2i` round-trips when an int result is immediately used in another int operation.
3. **Exception check scaffolding**: After every runtime call that might throw, the transpiler emits `js_has_pending_exception()` check + branch to error handler.

### 9.2 Proposals

#### A. Module Variable Caching

For module variables referenced multiple times in a function, load once into a local register at function entry (or at first use) and reuse:

```cpp
// Before: each reference to 'assert' emits:
call js_get_module_var(3)  // load assert
// ... use ...
call js_get_module_var(3)  // load assert again (redundant)

// After: cache in local register:
MOV _cached_assert, call js_get_module_var(3)  // load once
// ... use _cached_assert everywhere ...
```

**Important**: Must invalidate cache after any function call that might modify globals (conservative: after any call).

#### B. Exception Check Batching

Currently after each runtime call:
```
call js_add(a, b)
call js_has_pending_exception()
BT exception_label
```

For sequences of pure operations (arithmetic, comparisons) that cannot throw user-visible side effects between them, batch the exception check:

```
call js_add(a, b)           ; might set exception flag
call js_subtract(c, d)      ; might set exception flag
call js_has_pending_exception()  ; check once for both
BT exception_label
```

This requires classifying runtime functions as "definitely-no-throw" (e.g., `js_make_int_item`, `it2i`, `i2it`) vs "maybe-throw" (most others). Only batch across definitely-no-throw calls.

#### C. Strength Reduction for Common Constants

`ItemNull`, `ItemUndefined`, `ItemTrue`, `ItemFalse` are 64-bit constants loaded via `js_make_null()`, `js_make_undefined()`, etc. Emit them as inline `MIR_MOV` with immediate values instead of function calls.

---

## 10. Implementation Priority

| Stage | Description | Impact | Risk | Effort | Status |
|-------|-------------|--------|------|--------|--------|
| **1** | C Runtime Facade Functions | High (−20-40% MIR items) | Low | Medium | ✅ Done |
| **2** | Inline MIR Micro-Operations | Medium (−5-10% call overhead) | Low | Low | ✅ Done |
| **3** | Import Deduplication/Caching | Low-Medium (−5-10% link time) | Low | Low | 🟡 Partial |
| **4** | Loop Variable Type Speculation | High (tight loops much faster) | Medium (correctness) | Medium | ✅ Done (pre-v23) |
| **5** | String Interning for Properties | Medium (−1 call per prop access) | Low | Low | ⏭ Skipped (already covered) |
| **6** | Direct Method Dispatch | High (bypass 2-level indirection) | Medium (must match semantics) | High | ✅ Done (pre-v23) |
| **7** | MIR Code Size Reduction | Medium (faster regalloc) | Medium | Medium | 🟡 Partial |

**Recommended order**: Stage 2 → Stage 1 → Stage 5 → Stage 3 → Stage 7 → Stage 4 → Stage 6

Start with low-risk, quick-win stages (inline micro-ops, facades, property interning) before the higher-risk type speculation and method dispatch changes.

---

## 11. Measurement Plan

### A/B Benchmarking

All stages must be measured with the test262 timing sweep (v22 infrastructure):

```bash
# Baseline timing
./test/test_js_test262_gtest.exe --opt-level=0 2>&1 | tee temp/_t262_timing_baseline.log

# After each stage
./test/test_js_test262_gtest.exe --opt-level=0 2>&1 | tee temp/_t262_timing_stageN.log
```

Extract per-test microsecond data from `BATCH_END` protocol and compare distributions.

### Metrics to Track

| Metric | Source | Target |
|--------|--------|--------|
| Median per-test µs | BATCH_END timing | ≤ 5000 |
| p99 per-test µs | BATCH_END timing | ≤ 100000 |
| Phase 2 wall clock | GTest harness | ≤ 55s |
| MIR items per test (avg) | `MIR_output()` counting | −30% |
| Passing tests | GTest summary | ≥ 13,414 |
| Regressions | Baseline comparison | 0 |

### Profiling Tools

- `instruments -t "Time Profiler"` for hot function identification
- Per-test timing TSV for distribution analysis
- `MIR_output(ctx, stderr)` with item counting for MIR size metrics
- `log.txt` with `MIR-TIMING:` prefixed lines for transpile/link/exec phase breakdown

---

## 12. Implementation Progress

### Stage Status Summary

| Stage | Description | Status | Notes |
|-------|-------------|--------|-------|
| **1** | C Runtime Facade Functions | ✅ Done | 8 comparison `_raw` facades + `js_typeof_is`. `js_property_get_str` defined but intentionally not wired. Method call facades (`js_method_call_0/1/2`) skipped. |
| **2** | Inline MIR Micro-Operations | ✅ Done | `jm_emit_is_truthy` (bool fast-path), `jm_transpile_condition` (unified condition → raw 0/1), constant strength reduction for null/undefined/true/false. |
| **3** | Import Pre-population | 🟡 Partial | `jit_runtime_imports[]` bulk hashmap init exists. Cross-module shared import proto pool not done. |
| **4** | Loop Variable Type Speculation | ✅ Done | Semi-native for-loop tier with cached bounds (pre-v23 work). |
| **5** | String Interning for Properties | 🟡 Skipped | `jm_box_string_literal` already interns at transpile time via `name_pool_create_len()`. `js_property_get_str` defined but not wired — no benefit over pre-interned path. |
| **6** | Direct Method Dispatch | ✅ Done | `js_array_push`, `js_object_keys`, `js_object_values` direct-called (pre-v23 work). |
| **7** | MIR Code Size Reduction | 🟡 Partial | Constant strength reduction done. Module variable caching and exception check batching not done. |

### What Was Implemented in v23

#### Stage 1: Comparison Facades (`js_runtime.cpp`, `sys_func_registry.c`)
- **8 raw comparison functions** returning `int64_t` (0/1) instead of boxed `Item`:
  - `js_lt_raw`, `js_gt_raw`, `js_le_raw`, `js_ge_raw` — relational comparisons
  - `js_eq_raw`, `js_ne_raw` — strict equality
  - `js_loose_eq_raw`, `js_loose_ne_raw` — abstract equality
- `js_lt_raw` inlines fast numeric path (both INT/FLOAT → compare as doubles), falls back to full `js_less_than` for non-numeric
- All 8 registered in `jit_runtime_imports[]` via `sys_func_registry.c`
- **`js_typeof_is(Item, const char*)`** — returns 1/0 directly, avoids string creation + strcmp

#### Stage 2: Inline Micro-Ops (`transpile_js_mir.cpp`)
- **`jm_emit_is_truthy(mt, reg, expr)`** — bool fast-path: for known-BOOL expressions, emits `MIR_AND reg, 1` instead of calling `js_is_truthy()`
- **`jm_transpile_condition(mt, expr)`** — unified condition evaluator returning raw int64 0/1:
  - Case 1: Binary comparison with both operands typed numeric → `jm_transpile_expression` (native path, already 0/1)
  - Case 2: Untyped binary comparison → calls `_raw` facade directly (saves box+unbox+is_truthy)
  - Case 3: Logical NOT (`!expr`) → recursive + XOR with 1
  - Case 4: Fallback → `jm_transpile_box_item` + `jm_emit_is_truthy`
  - NULL expr → constant 1 (for `for(;;)`)
- **Wired into all 5 condition sites**: `if`, `while`, `for`, `do-while`, `ternary (?:)`
- **Switch-case comparison** inlined as direct `MIR_AND eq, 1` for BOOL result

#### What Was Intentionally Skipped
- **`js_property_get_str`**: Not wired because `jm_box_string_literal` already pre-interns string keys at transpile time. Adding a separate C-string path would add a new import without eliminating any existing one.
- **`js_method_call_0/1/2`**: General method call facades — high implementation complexity, moderate benefit. Would need to handle prototype chain, getter interception, etc.
- **Stage 3 cross-module dedup**: MIR modules are created per-test and discarded. Sharing protos across modules would require architectural changes to MIR context lifecycle.
- **Stage 7 module variable caching**: Requires invalidation after any function call that might modify globals. Conservative invalidation would negate most benefit.
- **Stage 7 exception check batching**: Requires classifying all runtime functions as throw/no-throw. Medium risk for correctness.

### Timing Results

#### Baseline (v22 — Run 48, commit 7c93ba85a)

| Phase | Time |
|-------|------|
| Phase 1 (prepare) | 0.1s |
| Phase 2 (execute) | ~71s (historical) |
| Passing tests | 13,414 / 27,089 |

#### Latest Run (v23b — April 2026)

| Phase | Time |
|-------|------|
| Phase 1 (prepare) | 0.1s — 27,089 scripts (26,854 clean, 235 quarantined) |
| Phase 2 (execute) | 113.1s — 26,834 results collected |
| Phase 2a (crashers) | 7.5s — 235 quarantined individually |
| Phase 2b (retry) | 0.4s — recovered 19 of 20 lost tests |
| **Total** | **~121s** |
| Passing tests | **13,416** / 27,089 (net +2 vs baseline) |
| Regressions | 35 (flaky, different set each run) |
| Improvements | 37 |

#### JS Unit Tests

| Run | Passed | Failed | Total |
|-----|--------|--------|-------|
| Baseline (v22) | 68 | 10 | 78 |
| v23b | 69 | 9 | 78 |

#### Observations

- **No measurable Phase 2 speedup**: 113.1s is within the normal variance range (91–147s observed across runs on the same machine). The v23 optimizations reduce per-execution overhead (fewer calls, fewer box/unbox cycles) but test262 cost is dominated by **transpile + MIR link/compile** across 27K one-shot scripts, not by hot-loop execution.
- **Correctness neutral**: 13,416 passed (vs 13,414 baseline), with ~35 flaky regressions that differ between runs.
- **Expected benefit domain**: These optimizations would show impact in real workloads with **hot loops** and **repeated condition evaluation** (e.g., tight numeric loops, repeated comparisons), not in test262's one-shot-per-script pattern where each script is compiled, executed once, and discarded.

---

## 13. MIR Interpreter POC

### Motivation

Phase 2 (execute) at 113.1s is dominated by MIR JIT compilation overhead — register allocation and native code generation — for 27K short-lived scripts that execute once and are discarded. MIR provides a built-in **interpreter mode** (`MIR_set_interp_interface`) that skips JIT compilation entirely, executing MIR instructions directly. The hypothesis: for one-shot scripts, eliminating JIT compile cost should reduce Phase 2 wall time.

### Implementation

Added `g_mir_interp_mode` flag in `lambda/mir.c`:
- When set, `jit_init()` skips `MIR_gen_init()` (no JIT backend initialization)
- `MIR_link()` uses `MIR_set_interp_interface` instead of `MIR_set_gen_interface`
- `jit_cleanup()` skips `MIR_gen_finish()`
- `find_func()` returns an interpreter thunk via `mitem->addr` — the calling convention is unchanged

All 5 `MIR_link()` call sites in `transpile_js_mir.cpp` updated (core path + module loading + new Function + preamble).

**Activation**: `--mir-interp` CLI flag (supported in `js` subcommand, `js-test-batch`, and general args) or `JS_MIR_INTERP=1` env var (fallback).

### POC Benchmark: Single-Process Batch (1000 tests)

Ran 1000 test262 files through `lambda.exe js-test-batch` in a **single process** (no parallelism) to isolate MIR JIT vs interpreter overhead.

| Mode | 300 tests (avg 3 runs) | 1000 tests |
|------|------------------------|------------|
| **JIT -O0** | 1.112s | 4.591s |
| **JIT -O2** | 1.166s | — |
| **Interpreter** | 1.013s | 3.605s |

| Comparison | Result |
|------------|--------|
| Interp vs JIT-O0 (300 tests) | **−8.8%** (1.10x faster) |
| Interp vs JIT-O0 (1000 tests) | **−21.5%** (1.27x faster) |
| Interp vs JIT-O2 (300 tests) | **−13.1%** (1.15x faster) |
| Correctness (1000 tests) | Identical: 264 pass / 1089 total, both modes |

Projected Phase 2 from single-process data: **~88.8s** (saves ~24s from 113.1s).

### Full test262 Suite Run (27,089 tests, 8 parallel workers)

```bash
./test/test_js_test262_gtest.exe --mir-interp --gtest_print_time=0
```

| Phase | JIT (baseline) | Interpreter |
|-------|---------------|-------------|
| Phase 1 (prepare) | 0.1s | 0.1s |
| **Phase 2 (execute)** | **113.1s** | **111.0s** |
| Phase 2a (crashers) | 7.5s | 9.8s |
| **Total wall time** | **~2:01** | **~2:07** |
| Passed | 13,416 / 27,089 | 13,389 / 27,089 |
| Regressions vs baseline | — | 76 |
| Improvements vs baseline | — | 51 |

### Analysis

**Phase 2 improvement was negligible** (113.1s → 111.0s, ~2s / ~2%) despite the single-process POC showing 21% speedup. Root causes:

1. **8 parallel workers mask per-test latency**: The test262 runner spawns 8 `lambda.exe` processes via `posix_spawn`, each processing 50-test batches. With 8 cores saturated, the per-test JIT compile cost is already amortized across cores. Removing it doesn't change the parallel throughput bottleneck.
2. **Phase 2 is I/O-bound at full parallelism**: `posix_spawn` + pipe reads + manifest file writes dominate when JIT cost is distributed across workers.
3. **Interpreter execution is slower**: While JIT compile is eliminated, the interpreter dispatches each MIR instruction via a switch loop instead of executing native code. For tests with non-trivial execution (loops, recursion), interpreter overhead exceeds JIT compile savings.

**76 regressions**: Mostly generators, TypedArray, and exception-path tests. The MIR interpreter likely handles some edge cases differently (e.g., longjmp-based exception recovery, generator suspend/resume state). These are interpreter implementation limitations, not logic bugs.

### Conclusion

MIR interpreter mode is **not viable** for reducing Phase 2 test262 time in the multi-worker setup. The `--mir-interp` flag is retained for potential future use (single-process scenarios, embedded/WASM targets where JIT is unavailable).
