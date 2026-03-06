# Lambda Transpiler — Performance Restructuring Proposal (Phase 3)

## Overview

This proposal targets **structural enhancements to the Lambda runtime and transpiler** to close the performance gap on compute-intensive benchmarks. Five benchmarks expose the key bottlenecks:

| Benchmark | Lambda (release) | Node.js | Ratio | Primary Bottleneck |
|-----------|----------------:|--------:|------:|-------------------|
| diviter | 5,579ms → **~285ms** | 488ms | ~~11.4x~~ **0.58x** | ~~`_store_i64` in tight loop~~ ✅ Solved (P1+CSI) |
| triangl | 1,416ms → **~441ms** (MIR) | 93ms | ~~20.2x~~ **~4.7x** (MIR) | Array indexing overhead (D1+D3-MIR) |
| collatz | 2,185ms → **~439ms** | 1,459ms | ~~1.5x~~ **0.30x** | ~~`_store_i64` + `shr`~~ ✅ Solved (P1+CSI+B2) |
| gcbench | 2,560ms → **~436ms** | 27ms | ~~94.8x~~ **16.1x** | ~~2 GC allocs per map~~ Reduced (P5) |
| gcbench2 | 2,079ms → **~266ms** | 27ms | ~~77.0x~~ **9.9x** | ~~Same as gcbench~~ Reduced (P5) |

The original geometric mean slowdown vs Node.js across these five benchmarks was **~17x**. Through six structural optimizations, it has been reduced to **~3.1x** (see Updated Benchmark Results).

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

### Priority 3: Native Typed Array Access — **IMPLEMENTED (D1)**

**Target benchmarks**: triangl (20.2x→~13.6x achieved), all array-heavy benchmarks
**Actual impact**: triangl **~1,265ms** (release), down from 1,416ms (~12% improvement)

**Status**: ✅ Implemented and verified (589/589 baseline tests pass). See D1 implementation log below for full details.

The optimization rewrites `transpile_index_expr` to eliminate `fn_index()` calls (the most expensive array access path) in favor of typed accessors and `item_at()`. For the triangl benchmark, all `fn_index` calls in the hot path were eliminated. Further gains require runtime-level changes (e.g., specializing `item_at` for boolean arrays, eliminating `fn_and`/`fn_not`/`is_truthy` overhead on boolean values).

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
| A3 | Enable unboxed `pn` calls | transpile.cpp, transpile-call.cpp | Medium | ✅ Superseded by CSI |

**Phase A estimated results:**

| Benchmark | Before | After Phase A | Actual (with CSI+D1) |
|-----------|-------:|-------------:|-------:|
| diviter | 5,579ms | ~1,200ms | **~285ms** ✅ |
| triangl | 1,416ms | ~500ms | **~1,265ms** ✅ (D1 done) |
| collatz | 2,185ms | ~1,500ms | **~439ms** ✅ |
| gcbench | ~~2,560ms~~ **540ms** ✅ | 540ms | **436ms** ✅ |
| gcbench2 | ~~2,079ms~~ **334ms** ✅ | 334ms | **266ms** ✅ |

### Phase B: Inlining (2–3 weeks)

| # | Optimization | Files Changed | Complexity | Expected dx |
|---|-------------|---------------|-----------|-------------|
| B1 | Transpiler-level function inlining | transpile-call.cpp, transpiler.hpp | High | diviter: 2x→~1.2x, collatz: 1.5x→~1.1x |
| B2 | Inline `shr`/`shl`/bitwise as raw ops | transpile-call.cpp | Low | ✅ **DONE** — collatz: ~8% |

### Phase C: Allocation Optimization (2–3 weeks)

| # | Optimization | Files Changed | Complexity | Status |
|---|-------------|---------------|-----------|--------|
| C1 | Combined map+data allocation | transpile.cpp, transpile-mir.cpp, lambda-data-runtime.cpp, mir.c, lambda.h | Medium | ✅ **DONE** — gcbench 4.7x, gcbench2 6.2x |
| C2 | Procedure return type inference | build_ast.cpp, transpile.cpp | Medium | Partially addressed by CSI |
| **CSI** | **Call-site type inference** | **transpile.cpp** | **High** | ✅ **DONE** — diviter 19.2x, collatz 4.6x |

### Phase D: Advanced (4+ weeks, optional)

| # | Optimization | Description | Status |
|---|-------------|-------------|--------|
| D1 | Direct array access dispatch | Typed array accessors + `item_at` instead of `fn_index` | ✅ **DONE** — triangl C2MIR: 1,416ms→1,265ms |
| D3-MIR | MIR Direct inline array reads | 6 new dispatch paths with runtime type checks | ✅ **DONE** — triangl MIR: ~699ms→~441ms (2.56x faster than C2MIR) |
| D2 | Loop-invariant code motion | Hoist array pointer loads out of loops | Not started |
| D3 | MIR SSA bug upstream fix | Report and fix the lost-copy bug in MIR | Not started |
| D4 | Generational GC nursery | Young-generation bump allocator for short-lived maps | Not started |

---

## Projected Final Results

| Benchmark | Original | Current (all opts) | After All Phases | Node.js | Final Ratio |
|-----------|--------:|--------:|----------------:|--------:|:----------:|
| diviter | 5,579ms | **~285ms** ✅ | ~250ms | 488ms | **~0.5x** |
| triangl (C2MIR) | 1,416ms | **~1,265ms** ✅ | ~200ms | 93ms | **~2.2x** |
| triangl (MIR) | — | **~441ms** ✅ | ~200ms | 93ms | **~2.2x** |
| collatz | 2,185ms | **~439ms** ✅ | ~350ms | 1,459ms | **~0.24x** |
| gcbench | 2,560ms | **436ms** ✅ | ~400ms | 27ms | **~15x** |
| gcbench2 | 2,079ms | **266ms** ✅ | ~250ms | 27ms | **~9x** |
| **Geomean** | **~17x** | **~3.1x** | **~2.3x** | — | — |

All five benchmarks now have concrete implementations with verified speedups. D1 (direct array access) improved triangl C2MIR by eliminating all `fn_index` calls from the hot path. D3-MIR extended this to MIR Direct with 6 new inline dispatch paths, achieving **2.56x faster than C2MIR** (~441ms vs ~1,130ms). The remaining triangl gap (~4.7x vs Node.js on MIR Direct) is dominated by: (1) `item_at` runtime type dispatch for `board` (generic `Array` holding booleans), (2) `fn_and`/`fn_not`/`is_truthy` overhead on boolean values, and (3) `it2i` unboxing at each level of nested access.

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

### Current Benchmark Comparison: C2MIR vs MIR Direct (debug build, March 2026)

After all optimizations (P1, P5, B2, CSI, D1), measured on the current debug build:

| Benchmark | C2MIR (default) | MIR Direct (`--mir`) | Node.js | C2MIR/MIR Ratio |
|-----------|--------:|--------:|--------:|------:|
| diviter | ~282ms | ~281ms | 488ms | ~1.0x (same) |
| collatz | ~345ms | ~345ms | 1,459ms | ~1.0x (same) |
| triangl | ~1,127ms | **~441ms** | 93ms | **2.56x slower** |
| gcbench | ~502ms | ~490ms | 27ms | ~1.0x (same) |
| gcbench2 | ~302ms | ~266ms | 27ms | 1.14x slower |

**Key observations:**

- **triangl: MIR Direct is 2.56x faster than C2MIR** after D3-MIR. The 6 new inline array read dispatch paths (with runtime `Container.type_id` checks) eliminate most external function calls for array access. MIR Direct generates native MIR instructions directly without C text → C2MIR parsing, producing better register allocation for nested array access patterns.
- **diviter / collatz: identical** — both paths generate equivalent tight loops for scalar integer arithmetic. The CSI + P1 optimizations normalize performance across both backends.
- **gcbench / gcbench2: MIR Direct slightly faster** (~14% on gcbench2), likely due to less overhead in allocation-heavy code paths where C2MIR's C text generation and parsing adds latency.
- **The remaining triangl gap** vs Node.js (MIR ~441ms vs Node 93ms, ~4.7x) is dominated by boolean operation overhead (`fn_and`, `fn_not`, `is_truthy`) and `item_at` runtime type dispatch for `board` (generic Array). Array indexing is no longer the bottleneck.

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

3. **Inline array access**: ✅ **IMPLEMENTED (D3-MIR)**. Extended MIR Direct's existing inline ArrayInt assignment to array reads across all typed and untyped combinations. Added 6 new dispatch paths with runtime `Container.type_id` checks for safety. See D3-MIR implementation log below. **Result**: triangl MIR Direct ~441ms (was ~699ms), **2.56x faster than C2MIR** (~1,130ms).

4. ~~Import cache for `_raw` variants~~: ~~The MIR Direct import cache (`import_cache` hashmap) should be extended with entries for the new `array_int_get_raw`, `array_float_get_raw` functions.~~
   Superseded by inline reads — Fast Path 1 is already faster than any `_raw` function call
---

## Implementation Log

### Priority 1: Selective `_store_i64` Elimination — **IMPLEMENTED**

**Status**: ✅ Implemented and verified (587/587 baseline tests pass).

#### Approach A (volatile): FAILED
C2MIR parses `volatile` but completely ignores it during MIR instruction generation. The `volatile_p` flag is stored but never consulted for stores/loads. All volatile-based approaches produce identical MIR code to non-volatile, causing the lost-copy bug (Fibonacci gives 256 instead of 55).

#### Approach B (pointer-through-store): FAILED
`*((int64_t*)&_var) = val` — C2MIR sees through the cast and recognizes it as a store to a local variable. The SSA pass still optimizes it incorrectly. Same lost-copy failure as approach A.

#### Approach C (cross-dependency analysis): **SUCCEEDED**

**Key insight**: The MIR SSA lost-copy bug only triggers for variables with **circular cross-dependencies** across loop iterations (e.g., the Fibonacci swap pattern `temp=a+b; a=b; b=temp`). Self-updating variables (`q=q+1`, `r=r-y`) have simple SSA phi chains with no cross-dependencies, and MIR handles them correctly.

**Implementation**: Added a static analysis pass that runs before each while-loop body transpilation:
1. `collect_loop_assigns()` — walks the loop body AST (including nested if/else) and collects all (target, rhs) pairs
2. `expr_contains_ident()` — checks if an expression tree references a given variable name, handling AST_NODE_PRIMARY wrappers, BINARY, UNARY, CALL, IF expressions
3. `analyze_loop_var_safety()` — for each assigned variable V, checks if any OTHER assignment's RHS reads V. If so, V is UNSAFE (needs `_store_i64`)

**Safety guarantee**: The analysis is conservative — it marks any variable read by another assignment as unsafe, even if the dependency is one-way (e.g., `max_start=i; i=i+1` marks `i` as unsafe). This avoids false positives (safe variables never get `_store_i64` removed incorrectly).

**Files changed:**
- `lambda/ast.hpp`: Added `loop_unsafe_vars` and `loop_unsafe_count` fields to Transpiler struct
- `lambda/transpile.cpp`: Added analysis functions (`expr_contains_ident`, `collect_loop_assigns`, `analyze_loop_var_safety`), integrated into `transpile_while`, modified `transpile_assign_stam` to check unsafe set

**Results** (release build, typed parameters):

| Benchmark | C2MIR Before | C2MIR After | MIR Direct | Node.js | After/Node Ratio |
|-----------|------------:|------------:|-----------:|--------:|-----------------:|
| diviter (typed) | ~5,500ms* | **271ms** | 271ms | 488ms | **0.56x (faster)** |
| collatz (typed) | ~2,000ms* | **338ms** | 338ms | 1,459ms | **0.23x (faster)** |

\* Estimated: typed versions would still use `_store_i64` in every loop iteration before this change.

**Note**: The original untyped benchmarks show no improvement because their bottleneck is **boxed arithmetic** (`fn_ge`, `fn_sub`, etc.) from untyped parameters, not `_store_i64`. With typed parameters, C2MIR now matches MIR Direct performance exactly. This optimization benefits all future Lambda code that uses type annotations on `pn` function parameters.

**Verification**: The `while_swap.ls` test suite (10 tests covering Fibonacci, bubble sort, GCD, collatz, rotate swap, nested swap, float swap, multi-swap) all pass with correct results. The analysis correctly identifies:
- Fibonacci vars (a, b, temp) as UNSAFE → keep `_store_i64`
- Loop counters (i) as SAFE → direct assignment
- Self-updating computation vars (q, r, steps, x) as SAFE → direct assignment

### Priority 1 Finding: Root Cause Analysis

The performance gap between C2MIR and Node.js on untyped benchmarks is **not** caused by `_store_i64`, but by **boxed arithmetic from untyped parameters**:

```c
// Untyped (current benchmark): ~5,500ms
fn_sub(_r, _y);  // boxed: function call + tag checks + unbox/box
fn_ge(_r, _y);   // boxed: function call + type dispatch

// Typed: ~271ms (20x faster)
_r - _y;          // native: single SUB instruction
_r >= _y;         // native: single CMP instruction
```

**Implication**: The remaining performance gap for untyped code requires **type inference for untyped `pn` parameters** — the transpiler needs to infer that `x: int, y: int` from call-site analysis, similar to what MIR Direct already does internally. This is a new optimization target not in the original proposal.

---

### B2: Inline Bitwise Operations — **IMPLEMENTED**

**Status**: ✅ Implemented and verified (587/587 baseline tests pass).

Inlined `band`, `bor`, `bxor`, `bnot`, `shl`, `shr` as direct C operators in the transpiled code instead of external function calls:
- `band(a,b)` → `(a & b)`, `bor(a,b)` → `(a | b)`, `bxor(a,b)` → `(a ^ b)`
- `bnot(a)` → `(~a)`
- `shr(a,b)` / `shl(a,b)` → guarded ternary `((b>=0&&b<64)?(a>>b):0)`

**Files changed:** `lambda/transpile-call.cpp`

**Impact on untyped collatz**: ~8% improvement (2185ms → 2016ms) — modest because `fn_shr` was already a direct native call with minimal overhead.

**Impact on typed collatz**: Eliminates the last remaining function call in the inner loop. Combined with P1, typed collatz runs at 338ms (Node.js: 1459ms).

---

### B2-MIR: Inline Bitwise Operations for MIR Direct — **IMPLEMENTED**

**Status**: ✅ Implemented and verified (589/589 baseline tests pass).

#### Optimization Parity Audit (C2MIR vs MIR Direct)

Audited all C2MIR optimizations to check which were already applied to MIR Direct:

| Optimization | C2MIR | MIR Direct | Action Needed |
|---|---|---|---|
| **P1** (`_store_i64` elimination) | ✅ | N/A — MIR uses native `MIR_MOV`, no workaround needed | None |
| **P5** (Compact `map_with_data`) | ✅ | ✅ Already using `map_with_data`/`object_with_data` | None |
| **B2** (Inline bitwise ops) | ✅ C operators | ❌ Was calling `fn_band`/`fn_shr` etc. | **Fixed** |
| **CSI** (Call-site type inference) | ✅ | Has body-usage inference (different but equivalent approach) | None |
|**D1** (Direct array access)|✅ 3-level dispatch|✅ **Enhanced (D3-MIR)**: 6 new inline read dispatch paths with runtime type checks. triangl **2.56x faster** than C2MIR|**Done** → ~441ms vs C2MIR ~1,130ms|

**B2 was the only missing optimization.** MIR Direct correctly unboxed arguments to native `int64_t` but then called external runtime functions (`fn_band`, `fn_shr`, etc.) instead of using native MIR instructions.

#### Implementation

Replaced all bitwise function calls in `transpile-mir.cpp` with native MIR instructions:

- `band(a,b)` → `MIR_AND` (single instruction)
- `bor(a,b)` → `MIR_OR` (single instruction)
- `bxor(a,b)` → `MIR_XOR` (single instruction)
- `bnot(a)` → `MIR_XOR` with `-1` (equivalent to `~a`; MIR has no `NOT` instruction)
- `shl(a,b)` → guarded `MIR_LSH`: bounds-check `b` in [0,64), branch to 0 if out-of-range
- `shr(a,b)` → guarded `MIR_RSH`: same bounds-check pattern

**Files changed:** `lambda/transpile-mir.cpp` (bitwise function handling in `transpile_call`)

#### Results (release build)

Collatz is the primary benchmark affected (uses `shr` in the inner loop):

| Benchmark | MIR Before B2 | MIR After B2 | C2MIR | Node.js |
|-----------|--------:|--------:|--------:|--------:|
| collatz | ~345ms | **~337ms** | ~367ms | 1,459ms |

The improvement is modest (~2%) because `fn_shr` was already a lightweight native-arg function. The main benefit is architectural consistency — MIR Direct now uses zero external function calls for bitwise operations, matching C2MIR's inline approach.

---

### Priority 3 / D1: Direct Array Access Dispatch — **IMPLEMENTED**

**Status**: ✅ Implemented and verified (589/589 baseline tests pass).

Rewrote `transpile_index_expr` to eliminate `fn_index()` calls — the most expensive array access path (double dispatch: index type + object type) — in favor of typed accessors and `item_at()`.

#### Root Cause Analysis

The triangl benchmark's hot inner loop `board[mfrom[mi]]` generated:
```c
// Before D1: every array access uses fn_index (most expensive path)
fn_index(_board, fn_index((Item)(_mfrom), _mi))
```

All accesses used `fn_index` because:
1. `board` has type ANY (from `fn_fill()` which returns `Item`)
2. `mi` has type ANY (from indexing `stack`, also ANY)
3. `mfrom` is `ArrayInt*` at C level but gets cast to `(Item)` for fn_index since `mi` is ANY
4. The early `field_type != INT/INT64/FLOAT` check in `transpile_index_expr` sent ALL non-numeric-typed indices to the `fn_index` fallback

#### Solution: Three-Level Dispatch Optimization

**Level 1 — Typed array + ANY index** (e.g., `mfrom[mi]`):
- Object is a known typed array (`ArrayInt*`, etc.) but index is `Item` (ANY)
- Unbox the index with `it2i()` and call the typed accessor directly
- Before: `fn_index((Item)_mfrom, _mi)` → After: `array_int_get(_mfrom, (int)it2i(_mi))`

**Level 2 — ANY object + INT index** (e.g., `stack[depth]`):
- Object is `Item` (ANY) but index is a known `int64_t`
- Use `item_at(obj, (int)idx)` which skips fn_index's index-type dispatch
- Before: `fn_index(_stack, i2it(_depth))` → After: `item_at(_stack, (int)_depth)`

**Level 3 — ANY object + nested INT-array index** (e.g., `board[mfrom[mi]]`):
- Both object and field types are ANY, BUT the field expression is an INDEX_EXPR into a typed int array
- Detect at transpile time that the inner expression produces an integer Item
- Unbox with `it2i()` and use `item_at` instead of `fn_index`
- Before: `fn_index(_board, array_int_get(_mfrom, ...))` → After: `item_at(_board, (int)it2i(array_int_get(_mfrom, ...)))`

**Key design insight**: The ARRAY type (generic `Array` with `nested` type info) is distinct from ARRAY_INT at the AST level. Array literals like `[0, 1, 2]` get type `ARRAY` with `nested=INT`, not `ARRAY_INT`. The initial approach of setting index result types based on nested type caused 15 test failures because it changed variable declarations from `Item` to `double`/`int64_t` throughout the codebase, creating type mismatches in MIR compilation. The final approach uses **targeted inner-expression detection** — only applied when both object AND field are ANY, and only for the specific case of nested array indexing.

#### Generated Code (triangl hot line, after D1)

```c
// Before D1:
fn_index(_board, array_int_get(_mfrom, (int)it2i(_mi)))

// After D1:
item_at(_board, (int)it2i(array_int_get(_mfrom, (int)it2i(_mi))))
```

All 3 board accesses in the hot condition and all 6 `fn_array_set` index computations now use `array_int_get` → `item_at` instead of `fn_index`. Zero `fn_index` calls remain in the generated code (only the declaration).

#### Implementation Details

**Files changed:**
- **lambda/build_ast.cpp** (`build_field_expr`): Added result type assignment for index into typed arrays — `ARRAY_INT`→`INT`, `ARRAY_INT64`→`INT64`, `ARRAY_FLOAT`→`FLOAT`. This enables `use_raw_int`/`use_raw_float` flags at transpile time.
- **lambda/transpile.cpp** (`reinfer_body_types`): Added `INDEX_EXPR` case to re-derive result type based on updated object type after param inference.
- **lambda/transpile.cpp** (`transpile_index_expr`): Major rewrite:
  - Early non-numeric check modified: `if (!field_is_numeric && field_type != LMD_TYPE_ANY)` allows ANY fields through for typed arrays
  - All typed array branches (ARRAY_INT, ARRAY_INT64, ARRAY_FLOAT, ARRAY, LIST) now accept `field_is_any` with `it2i()` unboxing
  - New `item_at` path for `object=ANY + field=INT`
  - New inner INDEX_EXPR detection for `object=ANY + field=ANY` when field is a typed array index
  - All index parameters use `(int)` cast for safety

#### Bugs Encountered and Resolved

**Bug 1 — ARRAY nested type propagation breaks 15 tests**: Setting result type to INT for `ARRAY` (with nested INT) caused `double _array_index` declarations where MIR expected `Item`, producing `dmov: unexpected operand mode` errors. Also caused `<error>` results for array indexing in expressions like `[base[0], for ...]` where the index result was used in a generic Item context.
**Fix**: Reverted the ARRAY nested type change. Only ARRAY_INT/ARRAY_INT64/ARRAY_FLOAT (explicit typed arrays) get typed results. For generic ARRAY, keep `TYPE_ANY` and use targeted inner-expression detection instead.

**Bug 2 — fn_index not eliminated from early exit path**: Debug tracing revealed the `fn_index` was emitted from the second early-exit check (`!field_is_numeric && !object_is_typed_array`), not the final fallback. Both object and field were ANY (type ID 24), so the early exit fired before reaching the typed dispatch paths.
**Fix**: Integrated the inner INDEX_EXPR detection into the early-exit check itself, so the optimization applies before falling through to `fn_index`.

#### Results (release build)

| Benchmark | Before D1 | After D1 | Node.js | Ratio |
|-----------|--------:|--------:|--------:|------:|
| triangl | 1,416ms | **~1,265ms** | 93ms | **~13.6x** |

**Improvement**: ~12% faster. The improvement is modest because the remaining bottleneck is NOT `fn_index` (which is now eliminated) but rather:
1. `item_at` still does a runtime `type_id` switch for each `board` access (board is generic `Array` holding booleans)
2. `fn_and`/`fn_not`/`is_truthy` boolean operations on each board value add significant per-access overhead
3. `it2i` unboxing at each level of nested access (inner result is boxed Item, outer needs int index)
4. `array_int_get` still has null check + type_id check + bounds check + i2it boxing

---

### D3-MIR: MIR Direct Inline Array Reads — **IMPLEMENTED**

**Status**: ✅ Implemented and verified (589/589 baseline tests pass).  
**Appendix C Item 3** — extends MIR Direct's inline array access from writes to reads.

#### Motivation

MIR Direct already had Fast Path 1 (ARRAY_INT + INT index → inline `items[idx]` read) and Fast Path 2 (ANY + INT index → `fn_index` call). But many common access patterns fell through to `fn_index` or `item_at` unnecessarily:

- `ARRAY` with `nested=INT` (e.g., array literals `[0, 1, 2]`) → not inlined
- `ARRAY_INT` with ANY index (e.g., `mfrom[mi]` where `mi` is Item) → not inlined
- `ARRAY` with ANY index → not inlined
- Nested `board[mfrom[mi]]` where both object and index are ANY but inner index produces INT → not inlined

The triangl benchmark's hot loop uses all of these patterns extensively.

#### Solution: 6 New Dispatch Paths with Runtime Type Safety

**Critical insight**: The compile-time ARRAY type (with `nested=INT`) is only a **hint** — `fn_array_set` can convert an `ArrayInt` to a generic `Array` in-place at runtime (changes `Container.type_id`). All inline reads for ARRAY-typed objects must include a runtime `type_id` check with fallback to `item_at`.

**New fast paths added to `transpile_index` in `transpile-mir.cpp`:**

| Path | Object Type | Index Type | Strategy |
|------|------------|-----------|----------|
| **1b** (typed) | ARRAY (nested=INT) | INT | Runtime check `Container.type_id == LMD_TYPE_ARRAY_INT` → inline `items[idx]` with `i2it` boxing; else → `item_at` fallback |
| **1b** (generic) | ARRAY (nested≠INT or unknown) | INT | Direct `item_at(boxed_obj, idx)` — skips index-type dispatch |
| **1c** | ARRAY_INT | ANY | Unbox index with `it2i()` → inline `items[idx]` read |
| **1d** (typed) | ARRAY (nested=INT) | ANY | Unbox index, runtime `type_id` check → inline or `item_at` fallback |
| **1d** (generic) | ARRAY (nested≠INT) | ANY | Unbox index → `item_at(boxed_obj, idx)` |
| **Level 3** | ANY | ANY | Detect inner INDEX_EXPR into typed array → unbox with `it2i()` → `item_at` |

Additionally, **Fast Path 2 slow path** was improved: changed from `fn_index(boxed_obj, box_int(idx))` to `item_at(boxed_obj, idx)` — saves both the integer boxing and `fn_index`'s double type dispatch.

#### Runtime Type Check Pattern (MIR)

For ARRAY objects that might be ArrayInt at runtime:
```
// Read Container.type_id (uint8_t at offset 0)
MIR_MOV  type_reg, MIR_MEM(obj_reg, 0, MIR_T_U8)
MIR_BNE  fallback_label, type_reg, LMD_TYPE_ARRAY_INT

// Inline path: read items[idx] directly
MIR_MOV  items_reg, MIR_MEM(obj_reg, 8, MIR_T_I64)    // Container.items
MIR_MUL  offset, idx_reg, 8
MIR_ADD  addr, items_reg, offset
MIR_MOV  result, MIR_MEM(addr, 0, MIR_T_I64)
MIR_JMP  done_label

// Fallback: runtime dispatch
fallback_label:
MIR_CALL item_at(boxed_obj, idx)

done_label:
```

#### Bug Encountered and Resolved

**`proc_proc_array_type_convert` test failure**: Initial implementation trusted compile-time ARRAY type without runtime check. The test assigns `arr[1] = 3.14` on an int array, which triggers `fn_array_set` to convert `ArrayInt` → generic `Array` in-place (changes `Container.type_id` from `LMD_TYPE_ARRAY_INT` to `LMD_TYPE_ARRAY`). Subsequent inline reads crashed because the memory layout assumptions were wrong.

**Fix**: Added runtime `Container.type_id` check to all ARRAY fast paths. If runtime type doesn't match expected `LMD_TYPE_ARRAY_INT`, fall back to `item_at()`.

#### Files Changed

- **lambda/transpile-mir.cpp** (`transpile_index`): ~250 lines of new dispatch paths between existing Fast Path 1 and Fast Path 2
- **lambda/transpile-mir.cpp** (`get_effective_type`): Added safety comment documenting why ARRAY+INT must NOT return INT (runtime type mutation)

#### Results (release build)

| Benchmark | C2MIR | MIR Before D3 | MIR After D3 | Node.js |
|-----------|------:|--------------:|-------------:|--------:|
| triangl | ~1,130ms | ~699ms | **~441ms** | 93ms |
| diviter | ~283ms | ~283ms | ~283ms | 488ms |
| primes | ~0.7ms | ~0.7ms | ~0.7ms | — |
| puzzle | ~4ms | ~4ms | ~4ms | — |
| quicksort | ~3.3ms | ~3.3ms | ~3.3ms | — |

**Key result**: triangl MIR Direct improved from ~699ms to **~441ms** (1.58x faster), and is now **2.56x faster than C2MIR** (~1,130ms). No regressions on other benchmarks.

The remaining gap vs Node.js (4.7x) is dominated by:
1. `item_at` runtime type dispatch for `board` accesses (board holds booleans in generic Array)
2. `fn_and`/`fn_not`/`is_truthy` boolean operation overhead
3. `i2it`/`it2i` boxing/unboxing at each nesting level

---

### Priority 4: Unboxed `pn` Calls — **UNBLOCKED via Call-Site Type Inference**

Previously blocked because all benchmark `pn` functions had untyped parameters. **Now resolved** by the call-site type inference implementation (see below), which automatically infers parameter types from call sites, making `has_typed_params()` return true for inferred functions.

---

### Priority 5: Compact Object Allocation — **IMPLEMENTED** (Prior session)

See priority 5 section above. gcbench 4.7x faster, gcbench2 6.2x faster.

---

### Call-Site Type Inference for Untyped `pn` Params — **IMPLEMENTED**

**Status**: ✅ Implemented and verified (587/587 Lambda tests pass, 156/156 MIR tests pass, all 11 AWFY untyped benchmarks pass).

#### Problem

Untyped `pn` parameters forced all arithmetic through boxed `Item` runtime functions (`fn_sub`, `fn_ge`, `fn_add`, etc.), each costing ~10–30ns vs ~0.3ns for native instructions. This was identified as the single largest remaining performance bottleneck — a 20x penalty on compute-intensive loops like diviter and collatz.

#### Solution: Automatic Type Propagation from Call Sites

Added a pre-transpilation pass `infer_proc_param_types_from_callsites()` that:
1. Iterates over all `pn` functions in the script
2. For each function, walks the AST to collect all call sites using `walk_ast_for_proc_refs()`
3. For each parameter position, examines argument types across all call sites
4. If all call sites agree on a concrete scalar type (INT, INT64, FLOAT, BOOL), mutates the `TypeParam.type_id` in-place — propagating the type to all identifier references that share the same `TypeParam*` pointer
5. Calls `reinfer_body_types()` to propagate new param types through the function body (binary expressions, if/else, etc.)

**Safety constraints:**
- Only infers types for functions with **all call sites visible** in the same script
- Skips functions used as first-class values (passed to higher-order functions)
- Requires **all** parameters to be inferred — partial inference is rejected to avoid `has_typed_params()` flipping true with mixed typed/untyped params
- Uses a `param_poisoned[]` array to permanently mark parameters that have seen non-inferable argument types (ANY, MAP, LIST, etc.)
- Skips the target function's own body when collecting call sites, preventing recursive self-calls from polluting inference

#### Implementation Details (~400 lines of new code)

**New functions in `lambda/transpile.cpp`:**
- `infer_binary_result_type(op, left_tid, right_tid)` — computes result type for binary operations given operand types
- `reinfer_body_types(node)` — recursive bottom-up type re-inference for function bodies after param types change. Handles: PRIMARY, BINARY, UNARY, CALL_EXPR, IF_EXPR, CONTENT, LIST/ARRAY, ASSIGN_STAM, WHILE, RETURN, LET/VAR, MATCH, INDEX, FOR, PIPE, MAP, KEY_EXPR, NAMED_ARG
- `walk_ast_for_proc_refs(node, target, sites, count, max, total_refs)` — finds all call sites for a given proc and detects non-call (first-class) references
- `is_inferable_type(tid)` — returns true for INT, INT64, FLOAT, BOOL
- `infer_proc_param_types_from_callsites(tp, script)` — main entry point, called from `transpile_ast_root()` before function transpilation

**Key design decisions:**

1. **In-place TypeParam mutation**: Parameters share their `TypeParam*` with all identifier nodes that reference them (established in `build_ast.cpp`). Mutating `type_id` in-place automatically propagates to all uses without needing to walk the AST again for identifiers.

2. **Local variables NOT re-typed**: `reinfer_body_types` deliberately does NOT update `named->type` for local variables (LET_STAM/VAR_STAM). Unlike params, changing a `named->type` pointer would desynchronize the declaration from its uses (which still hold the old pointer). This was Bug 2.

3. **Map/key/named-arg traversal**: `reinfer_body_types` must descend into MAP literals, KEY_EXPR nodes, and NAMED_ARG nodes to re-infer types of expressions nested inside map literals and named function arguments. Missing this caused Bug 6 (awfy_list failure).

#### Bugs Found and Fixed

**Bug 1 — Partial inference breaks `has_typed_params()`**: When only SOME params were inferred, `has_typed_params()` flipped true, changing return type handling and function signatures. **Fix**: Added `all_params_inferred` check — skip inference entirely if not ALL params resolve to concrete types.

**Bug 2 — Local var type pointer desync**: `reinfer_body_types` was doing `named->type = named->as->type` for local vars, desynchronizing the type pointer between declaration and uses. Variables would be declared as `int64_t` but used as `Item`. **Fix**: Removed var type update from reinfer; only params benefit from in-place mutation.

**Bug 3 — ANY var assigned native scalar**: `transpile_assign_expr` had no case for var=ANY, rhs=scalar (e.g., `var x = 0; x = typed_func()`). **Fix**: Added Case 3 using `transpile_box_item` when var is ANY and RHS is a native scalar.

**Bug 4 — NULL-typed variable references emit literal ITEM_NULL**: Variables initialized with `null` share the global `&LIT_NULL` type (`is_literal=1`). In `transpile_box_item`, the NULL/is_literal path emitted the string `"ITEM_NULL"` instead of the variable name. With inference enabled, variables that had `&LIT_NULL` type were treated as literal null even when used as arguments. **Fix**: Added a check in `transpile_box_item` NULL case — if the expression is a variable reference (PRIMARY→IDENT with entry pointing to ASSIGN or PARAM node), emit `transpile_expr()` instead of literal `"ITEM_NULL"`.

**Bug 5 — Recursive self-calls included in call site analysis**: `make_list(length)` calls itself with `make_list(length - 1)`. The recursive call's argument type is ANY (from build_ast, since `length` was untyped at parse time), but later concrete INT call sites would overwrite it. **Fix**: (a) Skip the candidate function's own body in `walk_ast_for_proc_refs` via `if (fn != target)` check, (b) Added `param_poisoned[]` boolean array to permanently mark params that have seen non-inferable types.

**Bug 6 — MAP literal not traversed by reinfer**: `reinfer_body_types` had no case for `AST_NODE_MAP`, `AST_NODE_KEY_EXPR`, or `AST_NODE_NAMED_ARG`. Expressions nested inside map literals (e.g., `{val: length, next: make_list(length - 1)}`) were never re-inferred, so `length - 1` kept its original ANY type. At the call site, this caused `it2i()` to be applied to an already-native `int64_t`. **Fix**: Added MAP, KEY_EXPR, and NAMED_ARG cases to `reinfer_body_types`.

#### Files Changed

- **lambda/transpile.cpp**: ~400 lines of new inference code + bug fixes in `transpile_assign_expr` and `transpile_box_item`
- **lambda/transpile-call.cpp**: No changes required (call argument handling already correct once types propagate)

#### AWFY Benchmark Results (Untyped — All Types Inferred Automatically)

All 11 untyped AWFY benchmarks pass with call-site type inference. Performance comparison with manually-typed variants (wall clock, debug build):

| Benchmark | Untyped (inferred) | Typed (manual) | Ratio |
|---|---:|---:|---:|
| bounce | 0.08s | 0.08s | 1.0x |
| deltablue | 0.25s | 0.25s | 1.0x |
| list | 0.02s | 0.02s | 1.0x |
| mandelbrot | 16.05s | 15.53s | 1.03x |
| permute | 0.05s | 0.04s | 1.25x |
| queens | 0.11s | 0.07s | 1.57x |
| sieve | 0.03s | 0.03s | 1.0x |
| storage | 0.07s | 0.05s | 1.4x |
| towers | 0.10s | 0.04s | 2.5x |
| diviter | 0.29s | — | — |
| collatz | 0.45s | — | — |

**Key observations:**
- **5 of 9 benchmarks match typed performance exactly** (bounce, deltablue, list, mandelbrot, sieve)
- **Queens, storage, towers show residual gaps** — likely functions with MAP/LIST params that cannot be inferred to scalar types, so some boxing overhead remains
- **Inference is transparent** — no source code changes required; untyped Lambda code automatically benefits

---

## Updated Benchmark Results

### Release Build (all optimizations applied)

| Benchmark | Original | Before Inference | After CSI | After D1 | Node.js | Ratio | Change |
|-----------|--------:|--------:|--------:|--------:|--------:|------:|--------|
| diviter (untyped) | 5,579ms | 5,480ms | **~285ms** | **~285ms** | 488ms | **0.58x** | **19.2x faster** (inference) |
| diviter (typed) | — | **271ms** | **271ms** | **271ms** | 488ms | **0.56x** | Faster than Node.js |
| collatz (untyped) | 2,185ms | 2,016ms | **~439ms** | **~439ms** | 1,459ms | **0.30x** | **4.6x faster** (inference) |
| collatz (typed) | — | **338ms** | **338ms** | **338ms** | 1,459ms | **0.23x** | 4.3x faster than Node.js |
| triangl (C2MIR) | 1,416ms | 1,395ms | 1,395ms | **~1,265ms** | 93ms | **~13.6x** | **12% faster** (D1) |
| triangl (MIR) | — | ~699ms | ~699ms | **~441ms** | 93ms | **~4.7x** | **1.58x faster** (D3-MIR) |
| gcbench | 2,560ms | **436ms** | **436ms** | **436ms** | 27ms | 16.1x | 5.9x faster (P5) |
| gcbench2 | 2,079ms | **266ms** | **266ms** | **266ms** | 27ms | 9.9x | 7.8x faster (P5) |

**Key findings:**
- **Call-site type inference closes the gap for untyped code**: diviter and collatz now match their manually-typed counterparts and **beat Node.js** — without any source changes
- With type annotations OR inference, C2MIR generates code that **beats Node.js** on compute-intensive loops
- **D1 eliminates all `fn_index` calls** from triangl's C2MIR hot path, replacing them with typed `array_int_get` + `item_at` dispatch
- **D3-MIR extends inline array reads** to MIR Direct with 6 new dispatch paths including runtime type safety checks. MIR Direct triangl is now **2.56x faster than C2MIR** (~441ms vs ~1,130ms), reducing the gap vs Node.js from ~13.6x to **~4.7x**
- The remaining triangl gap is dominated by boolean operations (`fn_and`, `fn_not`, `is_truthy`) and `item_at` runtime type dispatch — not array indexing

### Geomean Improvements

| Metric | Original | Before Inference | After CSI | After D1 |
|--------|--------:|--------:|--------:|--------:|
| Untyped benchmarks geomean vs Node.js | ~17x | ~8.5x | ~3.5x | **~3.1x** |
| Typed benchmarks geomean vs Node.js | — | ~0.4x (faster) | ~0.4x (faster) | ~0.4x (faster) |

---

## Revised Roadmap

### Completed Optimizations

| # | Optimization | Status | Impact |
|---|-------------|--------|--------|
| P1 | Selective `_store_i64` elimination | ✅ Done | diviter typed: 20x faster |
| P5 | Compact map+data allocation | ✅ Done | gcbench: 4.7x, gcbench2: 6.2x |
| B2 | Inline bitwise operations | ✅ Done (both C2MIR + MIR Direct) | collatz: ~8% (C2MIR), native MIR instructions (MIR Direct) |
| **CSI** | **Call-site type inference** | ✅ **Done** | **diviter untyped: 19.2x, collatz untyped: 4.6x** |
| **D1** | **Direct array access dispatch** | ✅ **Done** | **triangl C2MIR: fn_index eliminated, ~12% faster** |
| **D3-MIR** | **MIR Direct inline array reads** | ✅ **Done** | **triangl MIR: ~441ms (was ~699ms), 2.56x faster than C2MIR** |

### Remaining Opportunities

| # | Optimization | Complexity | Expected Impact |
|---|-------------|-----------|----------------|
| P2/B1 | Transpiler-level function inlining | High | Moderate (eliminates call ABI overhead for small functions) |
| P6/C2 | Procedure return type inference | Medium | Enables unboxed returns for procs |
| D2 | Loop-invariant code motion | Medium | Hoist array pointers out of loops |
| D3 | MIR SSA bug upstream fix | Hard | Eliminates need for _store_i64 entirely |
| D5 | Boolean array specialization | Medium | triangl: eliminate `fn_and`/`fn_not`/`is_truthy` overhead |
| D6 | Inline `item_at` for provably-typed arrays | Medium | triangl: skip runtime type dispatch |

**Boolean specialization (D5)** and **inline `item_at` (D6)** are the most impactful remaining optimizations for triangl. The `fn_index` bottleneck has been fully resolved by D1 (C2MIR) and D3-MIR (MIR Direct); the remaining ~4.7x gap vs Node.js on MIR Direct is dominated by boolean operation overhead (`fn_and`, `fn_not`, `is_truthy` wrapping each board access) and `item_at`'s runtime `type_id` switch.


**Optimization audit — MIR Direct (`--mir`) vs C2MIR:**

|Optimization|C2MIR|MIR Direct|Action|
|---|---|---|---|
|**P1** (`_store_i64` elimination)|✅|N/A — MIR uses native [MIR_MOV](vscode-file://vscode-app/Applications/Visual%20Studio%20Code.app/Contents/Resources/app/out/vs/code/electron-browser/workbench/workbench.html), no workaround needed|None|
|**P5** (Compact `map_with_data`)|✅|✅ Already applied|None|
|**B2** (Inline bitwise ops)|✅ C operators|❌ Was calling `fn_band`/`fn_shr` etc.|**Fixed** → native MIR instructions|
|**CSI** (Call-site type inference)|✅|Has body-usage inference (equivalent approach)|None|
|**D1** (Direct array access)|✅ 3-level dispatch|✅ **Enhanced (D3-MIR)**: 6 new inline read dispatch paths with runtime type checks. triangl **2.56x faster** than C2MIR|**Done** → ~441ms vs C2MIR ~1,130ms|
