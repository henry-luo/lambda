# Transpile_Js26: LambdaJS Performance Optimization Proposal

## Overview

This document analyzes the current LambdaJS benchmark performance against V8 (Node.js 22.13.0) and proposes targeted optimizations to close the gap. Benchmarks were run on Apple Silicon M4 (release build, 3 runs averaged).

**Current status (2026-04-12, after P1+P2+P4b):** Overall geo mean improved from ~8× to **~5×** across all suites. AWFY improved from 25.82× to **17.64×** (still the weakest suite, dominated by method dispatch overhead in richards/deltablue). nbody improved from 235× to **66×** (3.6× faster). R7RS and BENG are now near-competitive at 2.26× and 2.48×.

**Update (2026-06-19, after P2b+P4b-of+P4h):** AWFY geo mean improved from 17.64× to **16.27×** (−7.8%). Key wins: storage −12.2% (INT compound assignment), richards −8.4%, json −8.2%, nbody −7.0%, towers −5.8%. Phase 2 is now effectively complete; remaining gains require Phase 3 (method dispatch).

**Key finding (original):** LambdaJS shows a **massive OOP benchmark regression** since the Js11 era (6.3× geo mean → 25.82× for AWFY). The test262 compliance work (Js16–Js25) introduced correctness overhead that negated earlier performance gains. The regression is **localized to object/property access paths**.

---

## 1. Current Benchmark Results (LambdaJS vs V8/Node.js)

### Summary by Suite (Geometric Mean LJS/V8)

| Suite | Original | After P1+P2+P4b | After P2b+P4h | Tests | LJS Wins | Verdict |
|-------|----------|-----------------|---------------|-------|----------|---------|
| R7RS | 3.12× | **2.26×** | **2.26×** | 9/10 | tak, ack, nqueens, paraffins, primes | Good |
| AWFY | 25.82× | **17.64×** | **16.27×** | 12/14 | sieve (0.26×) | Needs P3 method dispatch |
| BENG | 3.65× | **2.48×** | **2.48×** | 7/10 | fannkuch (0.15×), pidigits (0.17×) | Good |
| KOSTYA | 6.91× | **4.96×** | **~4.9×** | 7/7 | primes (0.85×) | Needs work |
| LARCENY | 5.28× | **3.80×** | **~3.7×** | 12/12 | array1, paraffins, primes | Improved |

### Tier Classification (Updated 2026-04-12)

**A-Tier: LambdaJS FASTER than V8 (< 1×)**

| Benchmark | LJS (ms) | V8 (ms) | Ratio | Pattern |
|-----------|----------|---------|-------|---------|
| fannkuch | 1.04 | 6.88 | 0.15× | Pure integer recursion |
| pidigits | 0.49 | 2.83 | 0.17× | BigInt/integer math |
| array1 | 0.82 | 2.65 | 0.31× | Array fill + access |
| sieve | 0.13 | 0.51 | 0.26× | Simple array + loop |
| ack | 8.14 | 18.55 | 0.44× | Deep recursion |
| paraffins | 1.02 | 1.33 | 0.77× | Tree recursion |
| tak | 0.84 | 1.06 | 0.79× | Integer recursion |
| primes-L | 5.07 | 6.14 | 0.83× | Integer sieve |
| nqueens | 2.13 | 2.50 | 0.85× | Backtracking |
| primes-K | 5.13 | 6.06 | 0.85× | Integer sieve |

**B-Tier: Near Parity (1–2×)**

| Benchmark | LJS (ms) | V8 (ms) | Ratio | Pattern |
|-----------|----------|---------|-------|---------|
| cpstak | 1.64 | 1.37 | 1.20× | CPS recursion |
| fasta | 12.85 | 9.15 | 1.40× | String building |
| base64 | 47.40 | 26.77 | 1.77× | String/byte ops |
| divrec | 18.53 | 10.17 | 1.82× | Integer division |

**C-Tier: Moderate Slowdown (2–5×)**

| Benchmark | LJS (ms) | V8 (ms) | Ratio | Pattern |
|-----------|----------|---------|-------|---------|
| ray | 10.76 | 5.22 | 2.06× | Float math + objects |
| sumfp | 3.04 | 1.26 | 2.41× | Float accumulation |
| json_gen | 23.45 | 9.08 | 2.58× | Object creation |
| puzzle | 14.69 | 5.22 | 2.81× | Array + integer math |
| collatz | 5596.07 | 1849.04 | 3.03× | Integer loop |
| mandelbrot-B | 73.03 | 21.23 | 3.44× | Float math in loop |
| mandelbrot-A | 143.85 | 41.65 | 3.45× | Float math in loop |
| fib | 10.71 | 2.81 | 3.81× | Integer recursion |
| quicksort | 11.13 | 2.42 | 4.60× | Array swap + recursion |

**D-Tier: Slow (5–20×)**

| Benchmark | LJS (ms) | V8 (ms) | Ratio | Pattern |
|-----------|----------|---------|-------|---------|
| spectralnorm | 27.73 | 4.76 | 5.83× | Float nested loops |
| fibfp | 15.43 | 2.46 | 6.27× | Float recursion |
| brainfuck | 450.80 | 66.20 | 6.81× | Interpreter pattern |
| binarytrees | 59.29 | 7.63 | 7.77× | Alloc-heavy trees |
| triangl | 759.85 | 96.75 | 7.85× | Array + integer ops |
| mbrot | 22.68 | 2.68 | 8.46× | Float complex math |
| bounce | 7.33 | 0.85 | 8.62× | OOP class methods |
| sum | 17.29 | 1.95 | 8.87× | Integer accumulation |
| levenshtein | 50.80 | 5.37 | 9.46× | 2D array + loops |
| list | 7.81 | 0.76 | 10.28× | Linked list OOP |
| pnpoly | 84.28 | 7.72 | 10.92× | Array + float math |
| storage | 12.41 | 0.90 | 13.79× | Array alloc OOP |
| diviter | 9088.09 | 615.44 | 14.77× | Long integer loop |

**E-Tier: Critical (>20×)**

| Benchmark | LJS (ms) | V8 (ms) | Ratio | Pattern |
|-----------|----------|---------|-------|---------|
| queens | 16.29 | 0.97 | 16.79× | OOP class methods |
| deriv | 122.37 | 6.14 | 19.93× | Recursive structure |
| permute | 25.71 | 1.06 | 24.25× | OOP + array |
| towers | 48.32 | 1.65 | 29.28× | OOP linked list |
| gcbench | 1368.78 | 36.72 | 37.28× | GC allocation |
| json | 191.83 | 3.70 | 51.85× | OOP + hash map |
| nbody-A | 453.14 | 7.43 | 60.99× | Float + class field |
| richards-A | 5726.52 | 66.43 | 86.20× | OOP method dispatch |
| matmul | 2144.68 | 21.95 | 97.71× | Float 2D array |
| nbody-B | 1194.46 | 11.83 | 100.97× | Float + object |
| deltablue | 1843.05 | 18.93 | 97.37× | OOP constraints |

### Failing Tests (8 of 62)

| Test | Reason | Fix Feasibility |
|------|--------|-----------------|
| fft2.js | Float64Array not implemented | Medium (typed array support needed) |
| cd2_bundle.js | `static with(...)` — `with` is reserved keyword | Low (parser limitation) |
| havlak2_bundle.js | Computation error + TypeError | Medium (runtime bug) |
| knucleotide.js | `require('fs')` | Low (Node.js-only) |
| regexredux.js | `require('fs')` | Low (Node.js-only) |
| revcomp.js | `require('fs')` | Low (Node.js-only) |
| splay.js (JetStream) | Raw JetStream file needs wrapper | Low (test harness issue) |
| deltablue.js (JetStream) | Raw JetStream file needs wrapper | Low (test harness issue) |

---

## 2. Root Cause Analysis

### 2.1 Performance Regression Since Js11 (AWFY: 6.3× → 25.82×)

The test262 compliance work in Js16–Js25 introduced multiple overhead layers:

1. **Null/Undefined TypeError in property access** — `js_property_access()` now checks for null/undefined receivers and throws TypeError (test262 compliance). This adds a branch to every property read, and the error object allocation path can corrupt heap state in hot loops. (Partially fixed in this session for `process.argv`.)

2. **Property descriptor compliance** — Property reads/writes now respect `[[Get]]`/`[[Set]]`/`[[Writable]]`/`[[Configurable]]` descriptors. Each property access potentially checks descriptor flags.

3. **TDZ enforcement** — Temporal Dead Zone checks add overhead to variable access in `let`/`const` scopes.

4. **Strict mode overhead** — Additional error checking for assignments to read-only properties, arguments validation.

5. **Prototype chain completeness** — `for-in` now walks full prototype chain, `hasOwnProperty` checks are more thorough.

### 2.2 Structural Bottleneck Analysis

Based on transpiler code review and benchmark profiling:

| Bottleneck | Impact | Affected Benchmarks |
|-----------|--------|---------------------|
| **Property access via hash table** | Every `this.x` or `obj.field` goes through `js_property_access()` → hash lookup + null check + descriptor check | ALL OOP benchmarks (E-tier) |
| **Float boxing in hot loops** | Each float operation boxes result to heap Item, then unboxes for next operation | nbody, matmul, mandelbrot, spectralnorm |
| **Shaped slot linked-list walk** | `js_get_shaped_slot()` walks O(slot_index) linked list instead of O(1) array offset | Class-heavy benchmarks |
| **Method dispatch overhead** | `obj.method()` = property_access + function_call (2 runtime calls) | richards, deltablue, bounce, queens |
| **GC allocation pressure** | High-rate object creation triggers frequent GC pauses | gcbench, binarytrees, json, storage |
| **Array bounds check overhead** | Per-access bounds check + deleted sentinel check, even in tight loops | matmul, navier_stokes, levenshtein |

### 2.3 Why Some Benchmarks are Fast

The A-tier and B-tier benchmarks share these characteristics:
- **No object property access** — Pure integer/float arithmetic in local variables
- **Simple recursion** — Function call overhead is well-optimized (native MIR calls)
- **No heap allocation in hot loop** — All values fit in native registers
- **Type inference works** — Transpiler correctly identifies INT/FLOAT types and emits native MIR instructions

V8's JIT warmup overhead (~1ms) makes LambdaJS competitive on short-running benchmarks (< 5ms).

---

## 3. Proposed Optimizations

### P1: Inline Property Access for Known Shapes (Priority: CRITICAL) — ✅ IMPLEMENTED

**Problem:** Every `this.x` compiles to a `js_property_access(obj, "x")` runtime call even when the transpiler knows the class shape at compile time. This call performs: null check → type dispatch → hash table lookup → descriptor check → value return. For benchmarks like richards (125×), this overhead dominates.

**Current state:** The P4 shaped-slot optimization exists (`js_get_shaped_slot(obj, slot_index)`) but uses an O(n) linked-list walk through `ShapeEntry` nodes. For a class with 10 fields, accessing field 8 requires 8 pointer dereferences.

**Proposed fix:** Replace `ShapeEntry` linked-list with a **flat array of byte offsets** computed at shape creation time. `js_get_shaped_slot(obj, slot_index)` becomes:
```c
Item js_get_shaped_slot_fast(Item obj, int slot_index) {
    Container* c = item_to_container(obj);
    int offset = c->shape->offsets[slot_index]; // O(1) array lookup
    return *(Item*)((char*)c + offset);          // direct memory read
}
```

Better yet, **inline the access in MIR** — emit the offset computation directly in the generated code:
```mir
// this.x where x is slot 3 at byte offset 48
slot_val = load_i64(this_ptr, 48)   // single MIR instruction, no function call
```

**Expected impact:** 10–50× speedup on property access → AWFY OOP benchmarks from 25× to ~5× geo mean.

**Effort:** Medium. Requires:
1. Change `ShapeEntry` to include a pre-computed `offsets[]` array
2. Emit inline MIR load/store with constant byte offsets in transpiler
3. Guard with a single shape-check (`obj->shape == expected_shape`) at method entry

---

### P2: Float Unboxing in Hot Loops (Priority: HIGH)

**Problem:** Float values in tight loops are boxed on the heap after every arithmetic operation, then immediately unboxed for the next operation. For `nbody` (235×), each body has 7 float fields (x, y, z, vx, vy, vz, mass), and the inner loop performs ~25 float operations per pair, each boxing/unboxing the result.

**Current state:** The transpiler has native float paths for local variables (`jm_transpile_as_native()` with `LMD_TYPE_FLOAT`), but property access always returns boxed `Item` values. So `this.x + this.vx * dt` becomes:
```
val1 = js_property_access(this, "x")    // returns boxed Item
val2 = js_property_access(this, "vx")   // returns boxed Item
f1 = unbox_float(val1)                  // heap read
f2 = unbox_float(val2)                  // heap read
f3 = unbox_float(dt)
result = f1 + f2 * f3                   // native
boxed = box_float(result)               // heap alloc!
js_property_set(this, "x", boxed)       // hash lookup + write
```

**Proposed fix:** For shaped class instances with known float fields, **keep field values in native registers** across the loop iteration:
1. At loop entry, load all accessed fields into native MIR doubles
2. Perform all arithmetic in native registers
3. At loop exit (or method return), write back to object fields

This is essentially **scalar replacement of aggregates (SRA)** — a well-known compiler optimization.

For simpler cases: emit direct float store into the shape slot:
```mir
// this.x = this.x + this.vx * dt (all float, shape known)
x = load_f64(this_ptr, offset_x)
vx = load_f64(this_ptr, offset_vx)
x = x + vx * dt
store_f64(this_ptr, offset_x, x)   // no boxing, straight memory write
```

**Expected impact:** 20–100× speedup on float-heavy loops → nbody from 235× to ~5×, matmul from 136× to ~3×.

**Effort:** High. Requires P1 (inline property access) as prerequisite, plus:
1. Field type inference (track that `this.x` is always float)
2. Native register allocation for object fields in loop scope
3. Write-back at loop exit and method boundaries

---

### P3: Inline Method Dispatch for Known Class Instances ✅ IMPLEMENTED

**Problem:** `obj.method(args)` compiles to two runtime calls: `js_property_access(obj, "method")` to fetch the function, then `js_call_function(fn, this, args, argc)` to invoke it. The full dispatch chain is: `js_map_method` (670 lines of type checks) → `js_property_access` (hash lookup + prototype chain walk) → `js_call_function` (type check, this binding, builtin check) → `js_invoke_fn` (arity switch, fn ptr cast) — **6+ function calls per method invocation**.

**Implementation (2025-06-22):** Direct MIR CALL bypassing all runtime dispatch.

When the receiver's class is known at compile time:
- `this.method(args)` inside a class method: uses `mt->current_class`
- `obj.method(args)` where `obj` was assigned from `new ClassName()`: uses `obj->class_entry`

The transpiler resolves the method by walking the class + superclass chain at compile time, then emits a direct `MIR_CALL` to the method's `func_item` with individually transpiled boxed arguments.

**Key implementation details:**
1. **Override safety:** For `this` receiver, scans ALL known classes to detect subclass overrides. If any subclass overrides the target method, P3 falls back to runtime dispatch. Named vars from `new ClassName()` have exact types — no override check needed.
2. **Argument evaluation order:** Arguments are transpiled BEFORE setting `this` to the receiver. This prevents a bug where `object.add(name, this.readValue())` would see the wrong `this` during argument evaluation.
3. **Guards:** Method must have `func_item` (compiled), `capture_count == 0` (no closures), and no spread arguments.
4. **This save/restore:** Saves `js_get_this()`, sets `js_set_this(recv)`, clears `new.target`, calls method, restores both.
5. **Closure readback:** Calls `jm_readback_closure_env()` after the direct call in case the method modified closure state.

**Insertion point:** Between spread-arg check and `jm_build_args_array()` in the CALL_EXPRESSION handler for method calls.

**Results — AWFY benchmarks (5-run medians, release build, Apple Silicon M4):**

| Benchmark | Pre-P3 (ms) | Post-P3 (ms) | Speedup | vs V8 |
|-----------|-------------|--------------|---------|-------|
| sieve | 0.131 | 0.134 | — | **0.26×** |
| permute | 25.71 | 0.625 | **41.1×** | **0.59×** |
| queens | 16.29 | 0.482 | **33.8×** | **0.50×** |
| towers | 48.32 | 4.511 | **10.7×** | 2.73× |
| bounce | 7.33 | 7.746 | — | 9.11× |
| list | 7.81 | 0.673 | **11.6×** | **0.89×** |
| storage | 12.41 | 5.478 | **2.3×** | 6.09× |
| mandelbrot | 143.85 | 147.03 | — | 3.53× |
| nbody | 453.14 | 440.22 | — | 59.25× |
| richards | 5726.52 | 4778.47 | **1.2×** | 71.93× |
| json | 191.83 | 17.01 | **11.3×** | 4.60× |
| deltablue | 1843.05 | 1906.63 | — | 100.72× |

**AWFY Geometric Mean: 16.27× → 4.52× (−72.2%, 3.6× overall improvement)**

Four benchmarks now **faster than V8**: sieve (0.26×), permute (0.59×), queens (0.50×), list (0.89×).

**Why some benchmarks don't improve:**
- **bounce/sieve/mandelbrot/nbody:** Dominated by numeric computation, not method dispatch — P3 doesn't fire on hot paths
- **richards/deltablue:** Use deep polymorphic hierarchies where `this` receiver has subclass overrides → P3 correctly falls back to runtime dispatch. These need P3b (shape-based polymorphic dispatch) for further improvement

---

### P4: Direct Array Access in Typed Loops (Priority: HIGH) — ✅ PARTIALLY DONE

**Problem:** Array access in tight loops (`arr[i]` where both arr and i are known types) still goes through bounds checking + deleted-sentinel checking per access. For `matmul` (136×), the inner loop does 3 array reads and 1 array write per iteration, each with full runtime overhead.

**Current state:** The A2 optimization provides inline bounds checking, but still requires:
- Loading `arr->length` every iteration (cache-miss potential)
- Checking `arr->items[idx] != JS_DELETED_SENTINEL` every access
- Function call fallback for out-of-bounds

**Implemented (in Phase 2):**
- ✅ P4b: Array element class type propagation (`bodies[i]` → Body via field-access inference)
- ✅ P4b-of: For-of loop variable class inference (same field-access inference as P4b)
- ✅ P4h: Loop-invariant array pointer hoisting (typed + regular arrays in for/while loops)
- ~~ Bounds check elimination — deferred (diminishing returns; hot loops use function params)

**Remaining (deferred):**

Loop-hoisted bounds check (full elimination):
```mir
// Before loop: verify array won't be resized
arr_len = load_i64(arr, 16)    // arr->length
arr_items = load_i64(arr, 8)   // arr->items
assert(loop_max < arr_len)     // single check for entire loop

// Inside loop: raw pointer arithmetic, no bounds check
val = load_i64(arr_items, i * 8)   // direct memory load
```

For 2D arrays (`matrix[i][j]`):
1. Hoist outer array access (`row = matrix[i]`) to outer loop
2. Hoist inner array pointer (`row_items = row->items`) to inner loop entry
3. Inner load becomes single `load_i64(row_items, j * 8)`

**Expected impact (remaining):** Bounds check elimination could yield additional 1.5–2× on array-intensive loops, but was deferred as diminishing returns.

**Effort:** Medium. Requires:
1. Loop analysis to identify array access patterns
2. Bounds-check hoisting (prove loop bounds ≤ array length)
3. Array items pointer hoisting (prove no resize in loop body) — ✅ done (P4h)

---

### P5: Reduce GC Allocation Pressure (Priority: MEDIUM)

**Problem:** Benchmarks like gcbench (53×), binarytrees (11×), json (81×), and storage (23×) create many short-lived objects. Each allocation goes through the GC nursery with bump allocation, but frequent GC cycles add overhead.

**Current state:** Lambda uses three-tier allocation (namepool, arena, GC heap). Object creation goes through `heap_create()` which bump-allocates in the nursery and triggers collection when full.

**Proposed optimizations:**

1. **Nursery size tuning** — Increase nursery from current size to 4–8 MB for allocation-heavy benchmarks. Fewer collections = less overhead.

2. **Object pooling for typed constructors** — For `new Node(left, right, val)` in a loop, pre-allocate a free-list of Node-shaped objects. Reuse dead objects without GC involvement.

3. **Stack allocation for short-lived objects** — If escape analysis shows an object doesn't escape the current function, allocate it on the MIR stack frame instead of the heap. No GC tracking needed.

**Expected impact:** 2–5× speedup on allocation-heavy benchmarks → gcbench from 53× to ~15×, binarytrees from 11× to ~4×.

**Effort:** Medium-High. Nursery tuning is trivial; escape analysis is complex.

---

### P6: Integer Loop Counter Optimization (Priority: MEDIUM)

**Problem:** Simple counting loops like `for (let i = 0; i < n; i++)` where the computation inside is integer-only still have overhead from boxing `i` at phi-merge points or when used in property access indices.

Benchmarks affected: sum (12.55×), diviter (19.92×), collatz (4.13×), triangl (10.62×).

**Current state:** The transpiler tracks variable types and uses native int64_t for typed loop counters, but:
- Type inference sometimes fails for complex loop bodies
- Phi-merge at loop header may force re-boxing
- Comparison `i < n` where `n` is untyped forces unboxing

**Proposed fix:**
1. **Aggressive loop counter typing** — If a variable is initialized to `0` or an integer literal and incremented by `1` or an integer, force it to native int64_t regardless of other uses.
2. **Speculative widening** — Start with int, promote to float only if overflow/non-integer detected. Add a deopt check.
3. **Range propagation** — If `i < 1000000`, prove `i` fits in int32_t and use 32-bit MIR ops.

**Expected impact:** 2–5× speedup on integer-loop benchmarks → sum from 12.55× to ~3×, diviter from 19.92× to ~5×.

**Effort:** Medium. Mostly transpiler-side analysis improvements.

---

### P7: String Operation Fast Path (Priority: LOW)

**Problem:** String-building benchmarks (fasta 2.08×, base64 2.50×, json_gen 3.66×) are moderately slow due to string concatenation creating intermediate heap strings.

**Proposed fix:** Use `StrBuf` (mutable string buffer) for string concatenation in recognized patterns:
- `s += "..."` → `strbuf_append(buf, ...)`
- Template literals → pre-compute total length and single allocation

**Expected impact:** 1.5–2× speedup on string benchmarks.

**Effort:** Low-Medium.

---

## 4. Implementation Roadmap

### Phase 1: Property Access Fix (P1) — ✅ DONE (2025-04-12)

1. ✅ Added `slot_entries[]` O(1) array to TypeMap alongside existing `ShapeEntry` linked list
2. ✅ Populated at object creation (one-time cost, ≤16 fields)
3. ✅ `js_get/set_shaped_slot()` use O(1) array lookup with fallback
4. ✅ Emit `js_get_slot_f/i` native calls for known-type shaped fields
5. ✅ Constructor field type detection (literals + binary arithmetic)
6. ✅ P1 native reads for both named vars and `this.prop` in methods

**Result:** 7–26% improvement on OOP benchmarks. Modest vs target because method dispatch and array element type propagation are larger bottlenecks. See §7 for detailed analysis.

### Phase 2: Float Unboxing (P2) + Array Hoisting (P4) — ✅ COMPLETE (2025-06-19)

1. ✅ Field type tracking implemented in P1 (`ctor_prop_types[]`)
2. ✅ Native float load/store for shaped float fields (P1 `js_get/set_slot_f`)
3. ✅ Compound assignment native path (`this.x += val` and `obj.x -= expr` in methods)
4. ✅ Array element class type propagation (`bodies[i]` → Body via P4b field-access inference)
5. ✅ P2b: INT compound/simple assignment (`js_get/set_slot_i` for INT-typed shaped fields)
6. ✅ P4b-of: For-of loop variable class inference (same field-access inference as P4b)
7. ✅ Inheritance guard fix (only disable child `ctor_prop_count` when parent has constructor fields)
8. ✅ P4h: Loop-invariant array pointer hoisting (typed + regular arrays in for/while loops)
9. ~~ Bounds check elimination — deferred (diminishing returns; hot loops use function params)

**Validation (2025-06-19):** AWFY geo mean 17.64× → 16.27× (−7.8%). Key: storage 14.14→12.41ms (−12.2%), richards 6252→5727ms (−8.4%), json 209→192ms (−8.2%), nbody 487→453ms (−7.0%).

### Phase 3: Method Dispatch (P3) + Loop Counter (P6)

1. Implement monomorphic inline cache for method calls
2. Add polymorphic dispatch for class hierarchies (2–4 shapes)
3. Improve loop counter type inference

**Validation:** richards < 15×, deltablue < 20×, sum < 4×.

### Phase 4: GC Pressure (P5) + String (P7)

1. Tune nursery size
2. Implement constructor object pooling
3. String concatenation buffer optimization

**Validation:** gcbench < 20×, binarytrees < 5×.

---

## 5. Target Performance After Optimization

| Suite | Original | After P1+P2 | After P2b | Target | Remaining |
|-------|----------|-------------|-----------|--------|-----------|
| R7RS | 3.12× | **2.26×** | **2.26×** | **2.0×** | Close — fibfp, sum, mbrot need work |
| AWFY | 25.82× | **17.64×** | **16.27×** | **5.0×** | P3 method dispatch critical |
| BENG | 3.65× | **2.48×** | **2.48×** | **2.0×** | Nearly there — spectralnorm, nbody |
| KOSTYA | 6.91× | **4.96×** | **~4.9×** | **3.0×** | matmul needs param type propagation |
| LARCENY | 5.28× | **3.80×** | **~3.7×** | **3.0×** | Close — diviter, gcbench, triangl |
| **Overall** | **~8×** | **~5×** | **~4.8×** | **~3×** | P3 + P6 loop counter needed |

### Test Fixes (8 failing → 3 failing)

| Test | Fix | Priority |
|------|-----|----------|
| fft2.js | Implement Float64Array typed array | Medium |
| havlak2_bundle.js | Debug computation error | Medium |
| cd2_bundle.js | Handle `with` as method name (not keyword) | Low |
| knucleotide/regexredux/revcomp | Require `fs` module — skip | N/A |
| JetStream wrapper tests | Generate proper LambdaJS wrappers | Low |

---

## 6. Risk Assessment

| Risk | Mitigation |
|------|-----------|
| Inline property access breaks when object is reshaped at runtime (add/delete properties) | Shape guard at method entry; deopt to slow path |
| Float unboxing produces wrong results for NaN/Infinity edge cases | Preserve JS IEEE-754 semantics in native ops |
| Inline caches add memory overhead (per-call-site cached shape) | Use polymorphic ICs with bounded cache size (4 entries) |
| Optimizations regress test262 compliance | Run test262 suite after each phase; maintain pass count ≥ current |
| P1 changes `ShapeEntry` struct layout → breaks existing code | Keep linked list for backward compat; add offsets[] alongside |

---

## 7. Implementation Progress

### Phase 1 (P1): Inline Property Access — ✅ IMPLEMENTED (2025-04-12)

**What was built:**

1. **O(1) slot lookup via `slot_entries[]` array** — Added `ShapeEntry** slot_entries` and `int slot_count` to `TypeMap`. Populated at object creation time in `js_new_object_with_shape()` for objects with ≤16 fields. `js_get_shaped_slot()` and `js_set_shaped_slot()` use O(1) array indexing with fallback to O(n) linked-list walk.

2. **Native typed slot functions** — 4 new runtime functions that bypass Item boxing entirely:
   - `js_get_slot_f(obj, byte_offset)` → returns raw `double` from data buffer
   - `js_get_slot_i(obj, byte_offset)` → returns raw `int64_t` from data buffer
   - `js_set_slot_f(obj, byte_offset, value)` → writes raw double + updates ShapeEntry type
   - `js_set_slot_i(obj, byte_offset, value)` → writes raw int64 + updates ShapeEntry type
   - Type guards in getters handle runtime type changes (INT↔FLOAT conversion)

3. **Constructor field type detection** — `jm_detect_ctor_field_type()` infers field types from constructor init expressions:
   - Number literals: `0` → INT, `0.0` → FLOAT
   - Unary minus: `-0.0` → FLOAT, `-1` → INT
   - Binary arithmetic (`+`, `-`, `*`, `/`, `%`) → FLOAT (JS numbers are IEEE-754 doubles)
   - Types stored in `JsFuncCollected.ctor_prop_types[16]`

4. **P1 native reads in transpiler** — `jm_transpile_as_native()` MEMBER_EXPRESSION handler emits direct `js_get_slot_f/i` calls when:
   - Object is a named variable with known `class_entry` (e.g., `body.x`)
   - Object is `this` in a class method (e.g., `this.vx` via `mt->current_class`)
   - Field type is known FLOAT or INT from constructor scan
   - Returns native MIR register (MIR_T_D or MIR_T_I64), no boxing

5. **P3 native writes in constructors** — Constructor `this.prop = expr` assignments use `js_set_slot_f/i` when both the field type and RHS type are known numeric, bypassing Item boxing. Falls back to `js_set_shaped_slot` for unknown types.

6. **Type inference for shaped fields** — `jm_get_effective_type()` returns FLOAT/INT for shaped property reads on known-class objects (both `this.prop` and `var.prop`), enabling native arithmetic paths downstream.

**Files changed:**

| File | Changes |
|------|---------|
| `lambda/lambda-data.hpp` | Added `slot_entries`, `slot_count` to TypeMap |
| `lambda/js/js_runtime.cpp` | O(1 slot lookup, 4 native slot functions with type guards |
| `lambda/js/js_runtime.h` | Declarations for `js_get/set_slot_f/i` |
| `lambda/js/transpile_js_mir.cpp` | Field type detection, P1 native reads, P3 native writes, forward decl |
| `lambda/sys_func_registry.c` | Registered 4 new functions in `jit_runtime_imports[]` |

**Tests:** 78/78 JS tests pass, 566/566 Lambda baseline tests pass.

**Benchmark results (AWFY, release build, Apple Silicon M4, median of 3 runs):**

| Benchmark | Before (ms) | After (ms) | Speedup | Notes |
|-----------|------------|-----------|---------|-------|
| nbody | 1749 | 1290 | **1.36×** (−26%) | Float field access in tight loop |
| bounce | 11.4 | 9.6 | **1.19×** (−16%) | OOP class methods |
| deltablue | 2170 | 1848 | **1.17×** (−15%) | Constraint solving + OOP |
| richards | 6581 | 6120 | **1.08×** (−7%) | Method dispatch dominates |

**Analysis:**

The P1 implementation delivers **measurable but modest improvements** (7–26%). The gains are largest on nbody where float field access is the dominant cost. The limited impact on richards and deltablue indicates that **property access O(1) lookup is not the primary bottleneck** — these benchmarks are dominated by:

1. **Method dispatch overhead** — `obj.method()` still requires `js_property_access()` to fetch the function, then `js_call_function()` to invoke it. P1 only optimizes data field reads, not method lookups. (→ P3 inline method dispatch needed)
2. **Boxed return values from method calls** — Even when P1 reads fields natively, method return values are still boxed Items. An expression like `this.bodies[i]` returns a boxed Item, so the subsequent `iBody.x` access can't use the P1 native path (no `class_entry` on the variable).
3. **Array element access has no class info** — `let iBody = bodies[i]` loses the class type information. The transpiler doesn't know `iBody` is a `Body` instance, so falls back to generic property access.

**Remaining gaps for further P1 improvement:**
- Array element access type propagation (`bodies[i]` → knows it's a Body)
- Method call return type inference (class factory methods)
- Compound assignment native path (`this.x += val` in methods, not just constructors)

### Phase 2 (P2+P4b): Compound Assignment + Array Element Type Inference — ✅ IMPLEMENTED (2025-06-18)

**What was built:**

1. **P2: Native compound assignment for shaped class instances** — For `obj.field op= expr` (where op is `+=`, `-=`, `*=`, `/=`) and the object has known class_entry with FLOAT field type:
   - Emits `js_get_slot_f(obj, byte_offset)` → native double read
   - Transpiles RHS as native float (or unbox if ANY type)
   - Emits native MIR arithmetic (MIR_DADD, MIR_DSUB, MIR_DMUL, MIR_DDIV)
   - Emits `js_set_slot_f(obj, byte_offset, result)` → native double write
   - Replaces 3 boxed runtime calls (property_access + js_subtract + property_set) with 2 native slot calls + 1 MIR instruction
   - Also handles simple assignment (`obj.field = expr`) in method bodies for float fields
   - Boxed fallback (`js_set_shaped_slot`) for known-slot non-float simple assignments

2. **P4b: Array element class type inference** — When a variable is assigned from a subscript access (`const x = arr[i]`):
   - `jm_collect_var_fields_walk()` recursively walks the function body AST to collect all unique field names accessed as `x.field`
   - `jm_match_class_from_fields()` scans all known `JsClassEntry` objects and finds the unique class whose constructor has ALL collected field names
   - Requires ≥2 unique field accesses for reliable inference (avoids false positives)
   - Tags `var_entry->class_entry` enabling P1 native reads and P2 native writes on the variable
   - Works for both `this.bodies[i]` (via `mt->current_class`) and named array variables

**Key design decisions:**
- P4b uses **usage-based inference** rather than array element type tracking: it doesn't need to know the array's element type, just that the variable is used with field names matching exactly one class
- The AST walker handles 20+ node types to cover common code patterns (blocks, for loops, if statements, assignments, binary expressions, function calls, switch, try/catch, etc.)
- Functions/class bodies are NOT recursed into (different scope — the variable isn't accessible there)
- The "exactly one class matches" constraint ensures soundness: if two classes share all accessed field names, inference conservatively returns NULL

**Files changed:**

| File | Changes |
|------|---------|
| `lambda/js/transpile_js_mir.cpp` | P2 compound assignment block (~70 lines), P4b walker + matcher + integration (~150 lines) |

**Tests:** 78/78 JS tests pass, 566/566 Lambda baseline tests pass, 11/11 passing AWFY benchmarks still pass.

**Benchmark results (debug build, Apple Silicon M4):**

| Benchmark | Before (ms) | After (ms) | Speedup |
|-----------|------------|-----------|---------|
| nbody | 3772 | 1234 | **3.06×** |
| bounce | 51 | 31 | **1.63×** |
| deltablue | 7095 | 7160 | ~1.00× |
| richards | 23357 | 23977 | ~1.00× |

**Release build (Apple Silicon M4):**

| Benchmark | Release (ms) |
|-----------|-------------|
| nbody | 474 |
| bounce | 8.5 |
| mandelbrot | 149 |
| permute | 30 |
| queens | 17.5 |

**What fires on nbody hot loop:**
- P4b: `_js_iBody` and `_js_jBody` inferred as `Body` from 7 field accesses each
- P1: 8 native float loads (`iBody.mass`, `iBody.vx`×2, `iBody.vy`×2, `iBody.vz`×2, `jBody.mass`)
- P2: 6 native compound assignments (`iBody.vx -= ...`, `iBody.vy -= ...`, `iBody.vz -= ...`, `jBody.vx += ...`, `jBody.vy += ...`, `jBody.vz += ...`)

**Remaining gaps:**
- deltablue/richards dominated by method dispatch (→ P3 needed)
- forEach callback parameters (e.g., `body` in `bodies.forEach((body) => { body.x += ... })`) not yet inferred — P4b only works with subscript-initialized variables
- matmul/navier_stokes need array pointer hoisting and bounds check elimination

### Phase 2b (P2b+P4b-of+P4h): INT Assignment + For-of Inference + Array Hoisting — ✅ IMPLEMENTED (2025-06-19)

**What was built:**

1. **P2b: INT compound/simple assignment for shaped class instances** — Mirrors P2 float assignment but for INT-typed fields:
   - Emits `js_get_slot_i(obj, byte_offset)` → native int64_t read
   - Emits native MIR integer arithmetic (MIR_ADD, MIR_SUB, MIR_MUL)
   - Emits `js_set_slot_i(obj, byte_offset, result)` → native int64_t write
   - Also handles simple assignment (`obj.field = expr`) for INT fields
   - Biggest winner: `storage` benchmark (−12.2%) which uses integer fields in OOP patterns

2. **P4b-of: For-of loop variable class inference** — Extends P4b to `for (const x of arr)` loops:
   - Reuses `jm_collect_var_fields_walk()` and `jm_match_class_from_fields()` from P4b
   - The for-of iteration variable gets class_entry tagged, enabling P1 native reads and P2 native writes
   - Works alongside the existing for-of semi-native optimization

3. **Inheritance guard fix** — Fixed false negative in constructor field type detection for class hierarchies:
   - Previously: if a class extends another, child's `ctor_prop_count` was unconditionally zeroed
   - Fix: only disable child `ctor_prop_count` when parent class actually has constructor field assignments (`parent_has_ctor_fields` flag)
   - Enables P1/P2 native paths for more class hierarchies (e.g., AWFY benchmarks with empty base classes)

4. **P4h: Loop-invariant array pointer hoisting** — Hoists array metadata loads (items pointer, length, typed-array data pointer) out of for/while loop bodies:
   - `jm_scan_subscript_arrays()` AST walker scans loop body for computed MEMBER_EXPRESSION patterns (`arr[i]`)
   - Tracks reassigned variables as "unsafe" (prevents hoisting for mutated arrays)
   - For typed arrays: hoists 3 loads (ta_ptr, ta_len, ta_data) before loop entry
   - For regular arrays: hoists 2 loads (items pointer, length) before loop entry
   - Hoisted registers stored in `JsMirVarEntry.hoisted_data_reg` / `hoisted_len_reg`
   - All 4 array access functions (`array_get_inline`, `typed_array_get`, `typed_array_get_native`, `typed_array_set`) accept optional hoisted registers and skip redundant loads when provided
   - Cleanup: hoisted fields cleared after loop end to prevent stale pointers in nested/sequential loops
   - Fires on: matmul (3 typed arrays in main), triangl (1 typed + 3 regular in while), brainfuck (2 typed arrays)
   - **Limited impact** (~1-2%): hoisted loads are a small fraction of per-iteration work, and the hottest loops (matmul inner) take arrays as function parameters (no typed_array_type set for params)

**Files changed:**

| File | Changes |
|------|---------|
| `lambda/js/transpile_js_mir.cpp` | P2b INT assignment (~40 lines), P4b-of for-of inference (~15 lines), inheritance guard fix (~10 lines), P4h scanner + hoisting logic (~250 lines), 4 function signatures updated with hoisted params |

**Tests:** 78/78 JS tests pass, 566/566 Lambda baseline tests pass, 12/12 passing AWFY benchmarks still pass.

**Benchmark results (AWFY, release build, Apple Silicon M4, median of 3 runs):**

| Benchmark | Before (ms) | After (ms) | Δ | V8 Ratio |
|-----------|------------|-----------|---|----------|
| sieve | 0.18 | 0.131 | −27% | 0.26× |
| permute | 27.14 | 25.71 | −5.3% | 24.25× |
| queens | 16.89 | 16.29 | −3.6% | 16.79× |
| towers | 51.29 | 48.32 | −5.8% | 29.28× |
| bounce | 7.41 | 7.33 | −1.1% | 8.62× |
| list | 8.27 | 7.81 | −5.6% | 10.28× |
| storage | 14.14 | 12.41 | **−12.2%** | 13.79× |
| mandelbrot | 145.25 | 143.85 | −1.0% | 3.45× |
| nbody | 487.13 | 453.14 | **−7.0%** | 60.99× |
| richards | 6251.84 | 5726.52 | **−8.4%** | 86.20× |
| json | 208.88 | 191.83 | **−8.2%** | 51.85× |
| deltablue | 1935.33 | 1843.05 | −4.8% | 97.37× |

**AWFY Geometric Mean: 17.64× → 16.27× → 4.52× (P3: −72.2%)**

**Phase 3 Analysis:**
- P3 direct method dispatch eliminates 6+ runtime function calls per method invocation for monomorphic/known-class call sites
- Massive wins on recursive OOP benchmarks: permute (41×), queens (34×), towers (11×), list (12×), json (11×)
- richards/deltablue still limited: polymorphic hierarchies cause P3's override check to fall back to runtime dispatch
- Next step: P3b (shape-based polymorphic dispatch) for richards/deltablue

**Previous Phase 2 Analysis:**
- P2b INT assignment is the primary driver: storage (−12.2%), richards (−8.4%), json (−8.2%) all use integer fields in OOP patterns
- Inheritance guard fix enables P1/P2 on more class hierarchies, contributing to richards/deltablue/json improvements
- P4h array hoisting fires correctly but provides minimal additional speedup (~1-2%) because hoisted loads are a small fraction of total work and matmul's hot inner loop takes arrays as function parameters
- Bounds check elimination (Phase 2 item 9) deferred: the hottest array-intensive loops use function parameters (matmul) or have complex control flow (brainfuck), limiting the benefit of provably-safe bounds removal

**Remaining gaps (→ Phase 3):**
- richards/deltablue/json still dominated by method dispatch overhead
- matmul needs parameter type propagation for typed arrays (new optimization, beyond Phase 2 scope)
- sum/diviter need integer loop counter optimization (P6)

---

## Appendix: Raw Benchmark Data

### Current Results (2026-06-19, release build, Apple Silicon M4, after P1+P2+P2b+P4b+P4b-of+P4h)

```
Benchmark               LambdaJS (ms)   Node.js (ms)   Ratio      Δ from original
─── R7RS ───
fib                         10.71           2.81        3.81×      (was 4.74×)
fibfp                       15.43           2.46        6.27×      (was 8.86×)
tak                          0.84           1.06        0.79×      (was 1.03×)
cpstak                       1.64           1.37        1.20×      (was 1.68×)
sum                         17.29           1.95        8.87×      (was 12.55×)
sumfp                        3.04           1.26        2.41×      (was 3.53×)
nqueens                      2.13           2.50        0.85×      (was 1.21×)
fft                           ---           2.31        FAIL
mbrot                       22.68           2.68        8.46×      (was 11.96×)
ack                          8.14          18.55        0.44×      (was 0.60×)

─── AWFY ───
sieve                        0.13           0.51        0.26×      (was 0.32×) ★
permute                      0.63           1.06        0.59×      (was 34.91×) ★★★ P3
queens                       0.48           0.97        0.50×      (was 23.36×) ★★★ P3
towers                       4.51           1.65        2.73×      (was 41.92×) ★★★ P3
bounce                       7.75           0.85        9.11×      (was 17.27×)
list                         0.67           0.76        0.89×      (was 13.68×) ★★★ P3
storage                      5.48           0.90        6.09×      (was 22.72×) ★★ P3
mandelbrot                 147.03          41.65        3.53×      (was 4.68×)
nbody                      440.22           7.43       59.25×      (was 235.48×) ★
richards                  4778.47          66.43       71.93×      (was 125.43×) ★ P3
json                        17.01           3.70        4.60×      (was 80.90×) ★★★ P3
deltablue                 1906.63          18.93      100.72×      (was 132.82×)
havlak                        ---         182.63        FAIL
cd                            ---          61.96        FAIL

─── BENG ───
binarytrees                 59.29           7.63        7.77×      (was 11.24×)
fannkuch                     1.04           6.88        0.15×      (was 0.21×)
fasta                       12.85           9.15        1.40×      (was 2.08×)
knucleotide                   ---           6.92        FAIL
mandelbrot                  73.03          21.23        3.44×      (was 4.90×)
nbody                     1194.46          11.83      100.97×      (was 159.53×) ★
pidigits                     0.49           2.83        0.17×      (was 0.26×)
regexredux                    ---           3.69        FAIL
revcomp                       ---           4.80        FAIL
spectralnorm                27.73           4.76        5.83×      (was 8.64×)

─── KOSTYA ───
brainfuck                  450.80          66.20        6.81×      (was 9.76×)
matmul                    2144.68          21.95       97.71×      (was 136.19×)
primes                       5.13           6.06        0.85×      (was 1.16×)
base64                      47.40          26.77        1.77×      (was 2.50×)
levenshtein                 50.80           5.37        9.46×      (was 12.96×)
json_gen                    23.45           9.08        2.58×      (was 3.66×)
collatz                   5596.07        1849.04        3.03×      (was 4.13×)

─── LARCENY ───
triangl                    759.85          96.75        7.85×      (was 10.62×)
array1                       0.82           2.65        0.31×      (was 0.41×)
deriv                      122.37           6.14       19.93×      (was 27.48×)
diviter                   9088.09         615.44       14.77×      (was 19.92×)
divrec                      18.53          10.17        1.82×      (was 2.44×)
gcbench                   1368.78          36.72       37.28×      (was 53.31×)
paraffins                    1.02           1.33        0.77×      (was 1.04×)
pnpoly                      84.28           7.72       10.92×      (was 15.59×)
primes                       5.07           6.14        0.83×      (was 1.21×)
puzzle                      14.69           5.22        2.81×      (was 3.87×)
quicksort                   11.13           2.42        4.60×      (was 6.54×)
ray                         10.76           5.22        2.06×      (was 3.06×)
```

★ = Major improvement from P1+P2+P4b optimizations

### Improvement Summary (Original → After P1+P2+P2b+P3+P4b+P4b-of+P4h)

| Category | Benchmarks Improved >1.5× | Key Wins |
|----------|---------------------------|----------|
| **P3 method dispatch** | permute: 35→0.59×, queens: 23→0.50×, towers: 42→2.7×, json: 81→4.6× | Direct MIR CALL bypasses 6+ runtime calls |
| Float + OOP | nbody-A: 235→59×, nbody-B: 160→101×, spectralnorm: 8.6→5.8× | P2 compound assignment + P4b class inference |
| OOP methods | bounce: 17→9.1×, list: 14→0.89×, storage: 23→6.1× | P1 shaped slot reads + P2b + P3 |
| INT + OOP | richards: 125→72×, json: 81→4.6× | P2b INT assignment + P3 dispatch |
| Pure numeric | ack: 0.60→0.44×, nqueens: 1.21→0.85×, fib: 4.74→3.81× | General transpiler improvements |
| GC/alloc | gcbench: 53→37×, binarytrees: 11→7.8× | Memory subsystem improvements |
| Still critical | richards: 72×, deltablue: 101×, matmul: 98× | Need P3b (polymorphic dispatch) and param type propagation |
