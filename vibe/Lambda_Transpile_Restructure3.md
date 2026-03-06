# Lambda Transpiler — Performance Restructuring Proposal (Phase 3)

## Overview

This proposal targets **structural enhancements to the Lambda runtime and transpiler** to close the performance gap on compute-intensive benchmarks. Five benchmarks expose the key bottlenecks:

| Benchmark | Lambda (release) | Node.js | Ratio | Primary Bottleneck |
|-----------|----------------:|--------:|------:|-------------------|
| diviter | 5,579ms | 488ms | **11.4x** | `_store_i64` in tight loop + function call overhead |
| triangl | 1,416ms | 70ms | **20.2x** | Array indexing returns boxed `Item` + bounds/type checks |
| collatz | 2,185ms | 1,459ms | **1.5x** | `_store_i64` in loop + `shr` non-inlinable |
| gcbench | 2,560ms | 27ms | **94.8x** | 2 GC allocs per map literal + `p2it` field reads |
| gcbench2 | 2,079ms | 27ms | **77.0x** | Same as gcbench, typed maps |

The geometric mean slowdown vs Node.js across these five benchmarks is **~17x**. This proposal aims to bring the geometric mean to **<5x** through six structural optimizations, prioritized by expected impact.

### Prior Art

- [Lambda_Transpile_Restructure.md](Lambda_Transpile_Restructure.md) — Dual-version function generation (`_n`/`_b`), structured returns (`RetItem`)
- [Lambda_Transpile_Restructure2.md](Lambda_Transpile_Restructure2.md) — JIT header diet, O(1) sys func lookup, config-driven code gen
- [Lambda_Box_Unbox.md](Lambda_Box_Unbox.md) / [Lambda_Box_Unbox2.md](Lambda_Box_Unbox2.md) — Boxing/unboxing uniformity

This Phase 3 focuses exclusively on **JIT code quality and runtime call elimination** — the hot-path optimizations that directly affect compute-bound benchmarks.

---

## Root Cause Analysis

### Why Lambda is 11–95x Slower on These Benchmarks

Before proposing fixes, it's important to understand *exactly* what code the JIT produces and where time is spent.

#### 1. diviter — The `_store_i64` Tax (11.4x slower)

The core loop:
```lambda
pn diviter_div(x, y) {
    var q = 0
    var r = x
    while (r >= y) {
        r = r - y
        q = q + 1
    }
    return q
}
```

**Generated C (C2MIR path):**
```c
Item _f12_diviter_div(Item _x, Item _y) {
    int64_t _q = 0;
    int64_t _r = it2i(_x);
    while (_r >= _y_unboxed) {
        _store_i64(&_r, _r - _y_unboxed);  // ← FUNCTION CALL per iteration
        _store_i64(&_q, _q + 1);           // ← FUNCTION CALL per iteration
    }
    return (Item){.item = i2it(_q)};
}
```

`_store_i64` is a trivial `*dst = val` function that exists **solely** to work around a MIR JIT SSA "lost-copy" bug. It prevents MIR from reordering variable assignments inside loops. But since MIR cannot inline external functions, every scalar assignment in a while loop becomes a function call — wiping out the benefit of native arithmetic.

**Impact**: diviter performs ~500M iterations across 1000 calls to `diviter_div`. Each iteration has 2 `_store_i64` calls = 1 billion unnecessary function calls. At ~3ns per call (function call ABI overhead on Apple Silicon), that's **~3 seconds** of pure overhead — exactly matching the observed gap.

Additionally, `pn` functions are excluded from `can_use_unboxed_call` (transpile.cpp line 334), so even though `x` and `y` are inferred as `int`, the function signature uses `Item` parameters requiring box/unbox at every call boundary (1000 calls × 2 params × box + unbox).

#### 2. collatz — Same `_store_i64` + Non-Inlined Function Call (1.5x slower)

```lambda
pn collatz_len(n) {
    var steps = 1
    var x = n
    while (x != 1) {
        if (x % 2 == 0) { x = shr(x, 1) }
        else { x = 3 * x + 1 }
        steps = steps + 1
    }
    return steps
}
```

The `shr(x, 1)` call compiles to `fn_shr((int64_t)_x, 1)` — a runtime function call that simply does `a >> b` with a bounds check. While efficient for a function call, it could be a single `SHR` instruction if inlined. Each of the ~1M calls to `collatz_len` averages ~130 loop iterations, so `fn_shr` is called ~65M times.

The `_store_i64` workaround adds 2 more function calls per iteration (for `x` and `steps`). Combined with the `fn_shr` call = 3 function calls per iteration × ~130M iterations ≈ 390M function calls of overhead.

Collatz is only 1.5x slower because Node.js also can't fold `shr` into a single instruction (JS doesn't know the value fits in 32 bits, uses generic paths). The gap is narrower here because both runtimes have overhead.

#### 3. triangl — Array Indexing Returns Boxed `Item` (20.2x slower)

The hot loop accesses arrays like `board[mfrom[mi]]`:
```c
// Generated: nested array access, each returns Item with full checks
array_int_get(_mfrom, _mi)  // → null check, type_id check, bounds check, i2it boxing
```

**`array_int_get` implementation** (lambda-data-runtime.cpp line 123):
```c
Item array_int_get(ArrayInt *array, int index) {
    if (!array || ((uintptr_t)array >> 56)) { return ItemNull; }  // null/tag check
    if (array->type_id != LMD_TYPE_ARRAY_INT)                     // runtime type check
        return array_get((Array*)array, index);                    // fallback
    if (index < 0 || index >= array->length) { return ItemNull; } // bounds check
    int64_t val = array->items[index];
    Item item = (Item){.item = i2it(val)};                         // BOXING
    return item;
}
```

For `board[mfrom[mi]]`, this is **2 function calls** (inner + outer), each with 3 checks + boxing. The inner result is boxed into `Item` just to be immediately unboxed by the outer call's index parameter. Pure waste.

The `board` array uses generic `Array` (not `ArrayInt`) because it holds `true`/`false` values, so access goes through `array_get` which has an additional `switch` on `_type_id` and may call `push_l`/`push_d` (nursery allocation).

**Node.js comparison**: V8's optimizing compiler (TurboFan) recognizes `board` as a Smi array, eliminates type checks after warmup, and emits direct `LDR` instructions with only a bounds check. The per-access cost is ~1 instruction vs Lambda's ~30 instructions.

#### 4. gcbench/gcbench2 — Allocation-Dominated (77–95x slower)

```lambda
pn make_tree(depth) {
    if (depth == 0) { return {left: null, right: null} }
    return {left: make_tree(depth - 1), right: make_tree(depth - 1)}
}
pn check_tree(node) {
    if (node.left == null) { return 1 }
    return 1 + check_tree(node.left) + check_tree(node.right)
}
```

**Each `{left: ..., right: ...}` map literal allocates 2 GC objects:**
1. `map(type_index)` → `heap_calloc(sizeof(Map), LMD_TYPE_MAP)` — object zone alloc (~40B struct)
2. `heap_data_calloc(byte_size)` → data zone alloc (16B for two pointer fields)

For the stretch tree of depth 15: 2^16 - 1 = 65,535 nodes = **131,070 GC allocations**.
Total across all iterations: ~1.1M nodes = ~2.2M GC allocations.

**Each field access `node.left`** generates (C2MIR fast path):
```c
p2it(*(void**)((char*)(_node)->data + 0))  // pointer→Item tag conversion
```

**Node.js comparison**: V8 allocates objects as **hidden-class instances** — a single pointer-bump allocation in the young generation (~5ns). GC is generational with parallel scavenging. Lambda's dual-allocation scheme (struct + data buffer) and mark-sweep GC are fundamentally slower for this pattern.

The 77–95x gap is the widest because it compounds:
- 2x allocation overhead (two allocs per object vs one)
- Each alloc has `memset` overhead
- Mark-sweep GC is triggered every ~3MB of data-zone usage (frequent for tree construction)
- Field access through indirection (`Map* → data → offset`) vs V8's inline properties

---

## Proposed Optimizations

### Priority 1: Eliminate `_store_i64` / `_store_f64` — Replace with `volatile` Qualifier

**Target benchmarks**: diviter (11.4x→~2x), collatz (1.5x→~1.1x), triangl (partial)
**Expected overall impact**: ~3x improvement on loop-heavy benchmarks

#### Problem

`_store_i64(&_var, value)` is an *external* function call that MIR cannot inline, costing ~3–5ns per call on Apple Silicon. It exists to prevent MIR's SSA pass from reordering/eliminating variable updates inside `while` loops (a "lost-copy" bug).

#### Solution A: Use `volatile` Qualifier (Preferred)

Instead of calling an external function, declare loop-mutated variables as `volatile`:

```c
// Current (function call per assignment):
int64_t _r = ...;
while (...) {
    _store_i64(&_r, _r - _y);  // external call, ~3-5ns
}

// Proposed (volatile prevents reordering):
volatile int64_t _r = ...;
while (...) {
    _r = _r - _y;  // direct store, ~0.3ns, no function call
}
```

C2MIR should support `volatile` since it implements a subset of C11. The `volatile` qualifier tells the compiler that the variable may be modified by external factors, preventing:
- Dead store elimination
- Store reordering across sequence points
- Register caching across iteration boundaries

**Implementation**: In `transpile.cpp`, when `while_depth > 0`, instead of emitting `_store_i64(&_var, value)`, emit the variable declaration with `volatile` and use direct assignment:

```cpp
// In transpile_let_stam / variable declaration:
if (tp->while_depth > 0 || var_is_loop_mutated) {
    strbuf_append_str(tp->code_buf, "volatile ");
}
// Then in transpile_assign_stam, always use direct assignment:
// _var = value;  (no _store_i64 wrapper needed)
```

**Validation needed**: Test that C2MIR correctly handles `volatile int64_t` and `volatile double`. If C2MIR doesn't support `volatile`, fall back to Solution B.

#### Solution B: MIR Memory Operands (Fallback)

If `volatile` doesn't work in C2MIR, an alternative: instead of `_store_i64(&_var, value)`, emit the store as a compound expression that reads through a pointer:

```c
// pointer-through-store — forces memory round-trip without external call
*((int64_t*)&_r) = _r - _y;
```

This is still cheaper than an external function call because C2MIR can see it's a store to a known address.

#### Solution C: Fix MIR SSA Pass (Long-term)

The correct long-term fix is to report and fix the lost-copy bug in the MIR compiler. However, this depends on the MIR upstream maintainer.

**Estimated speedup on diviter**: From 5,579ms to ~1,200ms (eliminating ~1B unnecessary function calls).

---

### Priority 2: Inline Short Functions at the Transpiler Level

**Target benchmarks**: diviter (11.4x→~2x), collatz (1.5x→~1x), all recursive benchmarks
**Expected overall impact**: ~2–5x improvement on call-heavy benchmarks

#### Problem

Every function call in Lambda goes through the MIR function call ABI:
1. Box all arguments to `Item` (for `pn` functions without type annotations)
2. Push arguments to registers/stack per ABI
3. Branch to function entry
4. Unbox parameters (if typed)
5. Execute function body
6. Box return value to `Item`
7. Return through ABI
8. Unbox returned `Item` at call site

For small functions like `diviter_div` (5 lines of logic), steps 1–4 and 6–8 dominate the actual computation. MIR JIT **cannot** inline across function boundaries — it only optimizes within a single function.

#### Solution: Transpiler-Level Function Inlining

Implement inlining as an **AST-level transformation** before transpilation. When a call expression targets a function that qualifies for inlining, substitute the function body directly at the call site.

**Inlining eligibility criteria:**
1. Function body is "small" — heuristic: ≤ 20 AST nodes or ≤ 5 statements
2. Function is **not recursive** (no self-calls in body)
3. Function is **not a closure** (no captured variables)
4. Function is defined in the same module (local function, not imported)
5. Call site argument count matches parameter count (no variadic)
6. Function is called more than once OR is in a hot loop (inside `while`/`for`)

**Implementation approach** — two options:

##### Option A: C-Level Inlining via GCC Statement Expressions (Recommended)

At transpile time, instead of emitting a function call, emit the function body as a GCC statement expression `({ ... })` with parameter variables renamed to avoid conflicts:

```c
// Before (diviter calls diviter_div 1000 times):
_result = _f12_diviter_div(_x, _y);

// After inlining:
_result = ({
    int64_t _inline_x = _x;
    int64_t _inline_y = _y;
    int64_t _inline_q = 0;
    int64_t _inline_r = _inline_x;
    while (_inline_r >= _inline_y) {
        _inline_r = _inline_r - _inline_y;
        _inline_q = _inline_q + 1;
    }
    _inline_q;
});
```

**Key advantage**: Since the inlined body is now in the *same MIR function*, MIR's register allocator and instruction selector can optimize across the inlined code. Variables become native registers. No boxing/unboxing at call boundaries. No `_store_i64` workaround needed if we can prove the variables don't escape (though the workaround may still be needed for loops within the inlined body — TBD).

**Implementation in transpile-call.cpp**:

```cpp
void transpile_call_expr(Transpiler* tp, AstCallNode* call_node) {
    // ... existing code ...

    // NEW: Check if callee qualifies for inlining
    AstFuncNode* callee = resolve_callee(call_node);
    if (callee && should_inline(tp, callee, call_node)) {
        transpile_inlined_call(tp, callee, call_node);
        return;
    }

    // ... existing non-inlined call path ...
}

bool should_inline(Transpiler* tp, AstFuncNode* fn, AstCallNode* call) {
    if (fn->is_recursive) return false;
    if (fn->captures) return false;
    if (fn_ast_node_count(fn) > INLINE_AST_THRESHOLD) return false; // e.g., 20
    if (tp->inline_depth > MAX_INLINE_DEPTH) return false; // e.g., 3
    return true;
}
```

##### Option B: AST-Level Inlining (Transform Before Transpile)

Add an inlining pass in `build_ast.cpp` that physically clones and substitutes function bodies at call sites. More complex (requires AST cloning with proper name resolution) but enables both C2MIR and MIR-direct backends to benefit.

**Recommendation**: Start with Option A (C-level inlining) for faster iteration, then migrate to Option B if MIR-direct also needs it.

**Estimated speedup on diviter**: Combined with Priority 1, from 5,579ms to ~600ms (approaching Node.js's 488ms).

---

### Priority 3: Native Typed Array Access — Eliminate Boxing

**Target benchmarks**: triangl (20.2x→~5x), all array-heavy benchmarks
**Expected overall impact**: ~3–4x improvement on array-intensive code

#### Problem

`array_int_get(arr, index)` returns `Item` — a boxed tagged value. Even when the caller immediately uses the result as an `int64_t` (e.g., as an index into another array), the value goes through:
1. Read from `items[index]`
2. Box via `i2it(val)` (shift + OR to tag as int)
3. Return as `Item`
4. Caller unboxes via `it2i(item)` or uses as `Item`

For nested access like `board[mfrom[mi]]`, the inner result is boxed just to be unboxed by the outer access.

#### Solution: Direct-Access Functions That Return Native Types

Add new runtime functions that return the raw value, bypassing boxing:

```c
// New: return raw int64_t — no boxing
int64_t array_int_get_raw(ArrayInt* arr, int index) {
    if (index < 0 || index >= arr->length) return 0;  // or a sentinel
    return arr->items[index];
}

// New: return raw int value from generic Array (when element type is known)
int64_t array_get_int(Array* arr, int index) {
    if (index < 0 || index >= arr->length) return 0;
    return it2i(arr->items[index]);
}

// Already exists for float:
double array_float_get_value(ArrayFloat* arr, int index);
```

**Transpiler changes**: When the result of an array index expression is used in an integer context (comparison, arithmetic, another array index), emit the `_raw` variant:

```c
// Before:
array_get(_board, it2i(array_int_get(_mfrom, _mi)))

// After:
array_get_int(_board, array_int_get_raw(_mfrom, _mi))
```

For the **ideal path** (typed arrays with typed indices), emit inline access:

```c
// Best case: ArrayInt with int index, inside typed context
_mfrom->items[_mi]  // direct memory access, zero overhead
```

The transpiler already has commented-out code for this at transpile.cpp line 4523–4528:
```cpp
// For safety, we have to call array_int_get instead of:
// strbuf_append_str(tp->code_buf, "->items[");
```

**Safety concern**: The reason for not using direct `->items[idx]` is bounds checking. Proposed compromise:

```c
// Bounds-checked direct access (no boxing, no type check, no null check):
// Requires the transpiler to prove arr is non-null and ArrayInt
static inline int64_t array_int_get_fast(ArrayInt* arr, int index) {
    return (unsigned)index < (unsigned)arr->length ? arr->items[index] : 0;
}
```

The single unsigned comparison `(unsigned)index < (unsigned)arr->length` handles both negative and out-of-bounds in one branch.

**Implementation plan**:
1. Add `array_int_get_raw`, `array_get_int`, `array_float_get_raw` runtime functions
2. Register them in `mir.c` import resolver
3. In `transpile_index_expr`, when the result flows into a numeric context, use `_raw` variants
4. When both array and index types are known and the array is provably non-null, use direct `->items[idx]` access

**Estimated speedup on triangl**: From 1,416ms to ~350ms (4x improvement from eliminating ~30 instructions per array access in the inner loop).

---

### Priority 4: Enable Unboxed Calls for `pn` (Procedures)

**Target benchmarks**: diviter (further 1.5x), collatz (further 1.3x), gcbench (1.2x)
**Expected overall impact**: ~1.5x improvement on procedure-heavy benchmarks

#### Problem

Currently, `pn` functions are unconditionally excluded from unboxed calling:

```cpp
// transpile.cpp line 334:
if (fn_node->node_type == AST_NODE_PROC) return false;  // procs excluded from unboxed
```

This means every `pn` call boxes all arguments to `Item` and unboxes the returned `Item`, even when all types are known. For diviter, which calls `diviter_div` 1000 times with int arguments:
- 2 params × box (i2it) at call site
- 2 params × unbox (it2i) at function entry
- 1 return × box (i2it) at function exit
- 1 return × unbox (it2i) at call site

Total: 6 boxing operations × 1000 calls = 6,000 unnecessary operations. Each boxing/unboxing is ~1–2 instructions, but the real cost is that it prevents MIR from keeping values in registers across call boundaries.

#### Solution: Generate Unboxed `pn` Variants

Allow `pn` functions with typed parameters and typed (or inferable) return types to generate unboxed calling variants, identical to how `fn` functions do:

```c
// Unboxed variant (native types, direct call):
int64_t _f12_diviter_div_u(int64_t _x, int64_t _y) {
    int64_t _q = 0;
    volatile int64_t _r = _x;
    while (_r >= _y) { _r = _r - _y; _q = _q + 1; }
    return _q;
}

// Boxed wrapper (for dynamic dispatch):
Item _f12_diviter_div(Item _x, Item _y) {
    return (Item){.item = i2it(_f12_diviter_div_u(it2i(_x), it2i(_y)))};
}
```

**Implementation**:
1. Remove the `AST_NODE_PROC` exclusion in `can_use_unboxed_call`
2. Add return type inference for `pn` functions: analyze all `return` statements to determine the unified return type
3. Generate the `_u` variant for procs that have all-typed params AND an inferable return type
4. At call sites inside the same module, prefer the `_u` variant

**Complication**: `pn` functions can have multiple `return` statements with different types. The return type inference must find the common type (or fall back to `Item` if mixed).

---

### Priority 5: Compact Object Allocation for Small Maps — **IMPLEMENTED**

**Target benchmarks**: gcbench (94.8x→~20x), gcbench2 (77x→~15x)
**Actual impact**: gcbench **3.9–5.1x faster**, gcbench2 **6.0–6.7x faster**

**Status**: ✅ Implemented and verified (587/587 baseline tests pass).

| Benchmark | Before | After | Speedup |
|-----------|-------:|------:|--------:|
| gcbench | 2,560ms | ~540ms | **4.7x** |
| gcbench2 | 2,079ms | ~334ms | **6.2x** |

#### Problem

Every `{left: ..., right: ...}` map literal requires **two GC allocations**:

1. `heap_calloc(sizeof(Map), ...)` — 40 bytes for the Map struct (type_id, type pointer, data pointer, length, capacity, flags)
2. `heap_data_calloc(byte_size)` — 16 bytes for the data (two 8-byte pointer fields)

Total: 56 bytes across two allocations, with two `memset` operations and two GC bookkeeping entries.

For the `Node = {left: map, right: map}` pattern in gcbench, the actual payload is just 16 bytes (two pointers). The overhead-to-payload ratio is 3.5:1.

#### Solution: Combined Map+Data Allocation (Implemented)

For all static maps (where `byte_size` is statically known at transpile time), allocate the Map struct and data buffer in a **single GC allocation**. The data buffer is placed immediately after the Map struct:

```c
// New runtime function: map_with_data(type_index)
Map* map_with_data(int type_index) {
    TypeMap *map_type = ...;
    int64_t byte_size = map_type->byte_size;
    size_t total_size = sizeof(Map) + byte_size;
    Map *m = (Map *)heap_calloc(total_size, LMD_TYPE_MAP);
    m->type_id = LMD_TYPE_MAP;
    m->type = map_type;
    m->data = (char*)m + sizeof(Map);  // data immediately follows struct
    m->data_cap = byte_size;
    return m;
}
```

**Changes made:**

1. **lambda-data-runtime.cpp**: Added `map_with_data()` and `object_with_data()` — combined allocation functions for maps and objects. Modified `map_fill()`, `object_fill()`, and `elmt_fill()` to skip `heap_data_calloc` when `data` is already set.

2. **transpile.cpp (C2MIR)**: Both the fast `can_direct` path and the `map_fill` fallback now call `map_with_data(idx)` instead of `map(idx)`. The `heap_data_calloc` and `data_cap` assignments in the `can_direct` path are removed (already handled by `map_with_data`). Object expressions use `object_with_data(idx)`. Module import wrappers (`_mod_map_with_data`, `_mod_object_with_data`) and corresponding `#define` redirects were added for correct type_list scoping.

3. **transpile-mir.cpp (MIR Direct)**: Map and object allocation calls replaced with `map_with_data` / `object_with_data`.

4. **mir.c**: Registered `map_with_data` and `object_with_data` in the JIT import resolver table.

5. **lambda.h**: Declared both new functions.

**Why this works with the GC:**

- The combined allocation lives in the **object zone** (non-moving size-class allocator). The data pointer `(char*)m + sizeof(Map)` is stable — it never needs compaction.
- During GC mark phase, `gc_trace_object` reads `map->data` and walks shape entries to find contained pointers — works regardless of where `data` points.
- During GC compaction, `gc_data_zone_owns(gc->data_zone, data_slot)` returns `false` for object-zone pointers, so the compactor correctly skips inline data.
- The GC sweep frees the entire combined allocation as one unit.

**Allocation savings for `{left, right}` map (gcbench pattern):**

| | Before | After |
|-|-------:|------:|
| Allocations per map | 2 (struct + data) | **1** |
| Total bytes | 32 (Map) + 16 (data) + 2×header | 48 (Map+data) + 1×header |
| memset calls | 2 | **1** (heap_calloc zeros all) |
| GC objects tracked | 2 | **1** |

---

### Priority 6: Procedure Return Type Inference

**Target benchmarks**: All `pn` benchmarks (general improvement)
**Expected overall impact**: ~1.2x improvement across all procedural code

#### Problem

`pn` functions always return `Item`, even when all `return` paths produce the same type. This forces boxing on return and unboxing at every call site.

#### Solution: Infer Return Types from `return` Statements

During AST building, walk all `return` statements in a `pn` body and compute the meet (common supertype) of all returned expression types:

```
return <int_expr>     → int
return <int_expr>     → int
return <float_expr>   → float (widens to float if mixed with int)
return <string_expr>  → Item (mixed types → fall back to Item)
```

If all return paths produce the same type (or compatible numeric types), set the function's return type accordingly. This enables:
1. Unboxed return (Priority 4)
2. Caller-side type propagation (avoiding unbox at call site)
3. Better type inference for variables assigned from `pn` calls

**Implementation**: Add a `proc_infer_return_type()` pass in `build_ast.cpp` that runs after the function body is parsed.

---

## Implementation Roadmap

### Phase A: Quick Wins (1–2 weeks)

| # | Optimization | Files Changed | Complexity | Expected dx |
|---|-------------|---------------|-----------|-------------|
| A1 | `volatile` instead of `_store_i64` | transpile.cpp | Low | diviter: 11.4x→~3x |
| A2 | `array_int_get_raw` / native returns | lambda-data-runtime.cpp, transpile.cpp, mir.c | Low | triangl: 20.2x→~8x |
| A3 | Enable unboxed `pn` calls | transpile.cpp, transpile-call.cpp | Medium | diviter: 3x→~2x |

**Phase A estimated results:**

| Benchmark | Before | After Phase A | Target |
|-----------|-------:|-------------:|-------:|
| diviter | 5,579ms | ~1,200ms | ~600ms |
| triangl | 1,416ms | ~500ms | ~350ms |
| collatz | 2,185ms | ~1,500ms | ~1,200ms |
| gcbench | ~~2,560ms~~ **540ms** ✅ | 540ms | — |
| gcbench2 | ~~2,079ms~~ **334ms** ✅ | 334ms | — |

### Phase B: Inlining (2–3 weeks)

| # | Optimization | Files Changed | Complexity | Expected dx |
|---|-------------|---------------|-----------|-------------|
| B1 | Transpiler-level function inlining | transpile-call.cpp, transpiler.hpp | High | diviter: 2x→~1.2x, collatz: 1.5x→~1.1x |
| B2 | Inline `shr`/`shl`/bitwise as raw ops | transpile-call.cpp | Low | collatz: minor |

### Phase C: Allocation Optimization (2–3 weeks)

| # | Optimization | Files Changed | Complexity | Status |
|---|-------------|---------------|-----------|--------|
| C1 | Combined map+data allocation | transpile.cpp, transpile-mir.cpp, lambda-data-runtime.cpp, mir.c, lambda.h | Medium | ✅ **DONE** — gcbench 4.7x, gcbench2 6.2x |
| C2 | Procedure return type inference | build_ast.cpp, transpile.cpp | Medium | Not started |

### Phase D: Advanced (4+ weeks, optional)

| # | Optimization | Description |
|---|-------------|-------------|
| D1 | Direct `arr->items[idx]` access | Emit raw memory loads when array type + non-null is provable |
| D2 | Loop-invariant code motion | Hoist array pointer loads out of loops |
| D3 | MIR SSA bug upstream fix | Report and fix the lost-copy bug in MIR |
| D4 | Generational GC nursery | Young-generation bump allocator for short-lived maps |

---

## Projected Final Results

| Benchmark | Original | After C1 (done) | After All Phases | Node.js | Final Ratio |
|-----------|--------:|---------:|----------------:|--------:|:----------:|
| diviter | 5,579ms | 5,579ms | ~500ms | 488ms | **~1.0x** |
| triangl | 1,416ms | 1,416ms | ~200ms | 70ms | **~2.8x** |
| collatz | 2,185ms | 2,185ms | ~1,100ms | 1,459ms | **~0.75x** |
| gcbench | 2,560ms | **540ms** ✅ | ~400ms | 27ms | **~15x** |
| gcbench2 | 2,079ms | **334ms** ✅ | ~250ms | 27ms | **~9x** |
| **Geomean** | **~17x** | **~8x** | **~3.5x** | — | — |

The combined map+data allocation alone reduced the geomean slowdown from ~17x to ~8x vs Node.js — a significant step. The gcbench gap is now ~20x (down from ~95x), with the remaining overhead from mark-sweep GC vs V8's generational scavenger.

---

## Appendix A: C2MIR vs MIR Direct Comparison

The earlier benchmark data (from [Overall_Result.md](../test/benchmark/Overall_Result.md)) shows C2MIR is **1.24x faster** than MIR Direct in release builds. The key differences:

| Aspect | C2MIR | MIR Direct |
|--------|-------|------------|
| `_store_i64` workaround | YES — function call per loop assignment | NO — uses MIR_MOV directly |
| Typed variables | Native C types | Native MIR register types |
| Function call ABI | All params as Item (for `pn`) | All params as Item (boxed at call site) |
| Array access | Calls `array_int_get` etc. | Calls `item_at` (more generic) or inline for some cases |
| Map field access | Direct byte-offset (fast path) | Direct MIR_MOV at offset (Phase 3 "fixed shape") |
| Inlining | None | None |

**Recommendation**: Apply optimizations to C2MIR first (the primary path), then port applicable ones to MIR Direct. The `volatile` fix (Priority 1) only applies to C2MIR. MIR Direct doesn't have the `_store_i64` issue but would benefit from Priorities 2–6.

## Appendix B: Benchmark Profiles

### diviter Profile (release build)

- **Inner loop iterations**: ~500,000 per call to `diviter_div`, called 1000× from `benchmark()`
- **Total loop iterations**: ~1 billion (1000 × (500000 + 500000) for div + mod)
- **Dominant cost**: `_store_i64` × 2 per iteration + function call ABI at `diviter_div` boundary
- **Arithmetic**: Pure integer subtract/compare — should be 1 cycle per op

### collatz Profile (release build)

- **Function calls**: `collatz_len` called ~1M times
- **Average loop iterations per call**: ~130 (varies widely: 1–524 for numbers under 1M)
- **Total loop iterations**: ~130M
- **Dominant cost**: `_store_i64` × 2 per iteration + `fn_shr` external call (~65M calls)

### triangl Profile (release build)

- **Total moves examined**: ~29,760 solutions × ~36 moves tried per depth × ~14 depths ≈ millions of array accesses
- **Critical inner loop**: Scans all 36 moves checking `board[mfrom[mi]] and board[mover[mi]] and (not board[mto[mi]])`
- **Array accesses per inner iteration**: 6 (3 pairs of nested access: `mfrom[mi]`, `board[result]`, etc.)
- **Dominant cost**: 6 calls to `array_int_get`/`array_get` per inner iteration, each with full bounds/type/null checks + boxing

### gcbench Profile (release build)

- **Total nodes allocated**: ~1.1M (across stretch tree + iteration trees)
- **GC allocations**: ~2.2M (2 per node: struct + data)
- **GC collections triggered**: ~700 (every ~3MB of data-zone usage)
- **Dominant cost**: Allocation overhead + GC pause time + `memset` on zero-init

---

## Appendix C: MIR Direct Specific Improvements

For the MIR Direct transpiler path, these additional optimizations apply:

1. **Native return types for MIR functions**: Currently all MIR functions return `MIR_T_I64` (boxed Item). When return type is known, use `MIR_T_I64` with unboxed semantics or `MIR_T_D` for float returns.

2. **Typed function signatures**: Emit MIR function params with native types when all callers can be statically verified (same-module calls).

3. **Inline array access**: MIR Direct already has code for inline ArrayInt assignment (transpile-mir.cpp ~line 5900). Extend this to array reads: emit `MIR_MOV` from `items + index * 8` directly, with a bounds-check branch.

4. **Import cache for `_raw` variants**: The MIR Direct import cache (`import_cache` hashmap) should be extended with entries for the new `array_int_get_raw`, `array_float_get_raw` functions.
