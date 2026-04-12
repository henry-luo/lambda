# Transpile_Js26: LambdaJS Performance Optimization Proposal

## Overview

This document analyzes the current LambdaJS benchmark performance against V8 (Node.js 22.13.0) and proposes targeted optimizations to close the gap. Benchmarks were run on Apple Silicon M4 (release build, 3 runs averaged).

**Key finding:** LambdaJS shows a **massive OOP benchmark regression** since the Js11 era (6.3× geo mean → 25.82× for AWFY). The test262 compliance work (Js16–Js25) introduced correctness overhead that negated earlier performance gains. Numeric and function-call benchmarks remain competitive (R7RS 3.12×, BENG 3.65×), confirming the regression is **localized to object/property access paths**.

---

## 1. Current Benchmark Results (LambdaJS vs V8/Node.js)

### Summary by Suite (Geometric Mean LJS/V8)

| Suite | Geo Mean | Tests | LJS Wins | Verdict |
|-------|----------|-------|----------|---------|
| R7RS | **3.12×** | 9/10 | ack (0.60×) | Acceptable |
| AWFY | **25.82×** | 12/14 | sieve (0.32×) | Critical regression |
| BENG | **3.65×** | 7/10 | fannkuch (0.21×), pidigits (0.26×) | Acceptable |
| KOSTYA | **6.91×** | 7/7 | — | Needs work |
| LARCENY | **5.28×** | 12/12 | array1 (0.41×) | Needs work |

### Tier Classification

**A-Tier: LambdaJS FASTER than V8 (< 1×)**

| Benchmark | LJS (ms) | V8 (ms) | Ratio | Pattern |
|-----------|----------|---------|-------|---------|
| fannkuch | 1.47 | 6.88 | 0.21× | Pure integer recursion |
| pidigits | 0.73 | 2.83 | 0.26× | BigInt/integer math |
| sieve | 0.16 | 0.51 | 0.32× | Simple array + loop |
| array1 | 1.09 | 2.65 | 0.41× | Array fill + access |
| ack | 11.19 | 18.55 | 0.60× | Deep recursion |

**B-Tier: Near Parity (1–2×)**

| Benchmark | LJS (ms) | V8 (ms) | Ratio | Pattern |
|-----------|----------|---------|-------|---------|
| tak | 1.10 | 1.06 | 1.03× | Integer recursion |
| paraffins | 1.38 | 1.33 | 1.04× | Tree recursion |
| primes-K | 7.03 | 6.06 | 1.16× | Integer sieve |
| primes-L | 7.40 | 6.14 | 1.21× | Integer sieve |
| nqueens | 3.03 | 2.50 | 1.21× | Backtracking |
| cpstak | 2.30 | 1.37 | 1.68× | CPS recursion |

**C-Tier: Moderate Slowdown (2–5×)**

| Benchmark | LJS (ms) | V8 (ms) | Ratio | Pattern |
|-----------|----------|---------|-------|---------|
| fasta | 19.05 | 9.15 | 2.08× | String building |
| divrec | 24.80 | 10.17 | 2.44× | Integer division |
| base64 | 66.85 | 26.77 | 2.50× | String/byte ops |
| ray | 15.96 | 5.22 | 3.06× | Float math + objects |
| sumfp | 4.44 | 1.26 | 3.53× | Float accumulation |
| json_gen | 33.25 | 9.08 | 3.66× | Object creation |
| puzzle | 20.23 | 5.22 | 3.87× | Array + integer math |
| collatz | 7643.68 | 1849.04 | 4.13× | Integer loop |
| mandelbrot-A | 195.07 | 41.65 | 4.68× | Float math in loop |
| fib | 13.32 | 2.81 | 4.74× | Integer recursion |
| mandelbrot-B | 103.99 | 21.23 | 4.90× | Float math in loop |

**D-Tier: Slow (5–20×)**

| Benchmark | LJS (ms) | V8 (ms) | Ratio | Pattern |
|-----------|----------|---------|-------|---------|
| quicksort | 15.84 | 2.42 | 6.54× | Array swap + recursion |
| spectralnorm | 41.13 | 4.76 | 8.64× | Float nested loops |
| fibfp | 21.82 | 2.46 | 8.86× | Float recursion |
| brainfuck | 646.15 | 66.20 | 9.76× | Interpreter pattern |
| triangl | 1027.60 | 96.75 | 10.62× | Array + integer ops |
| binarytrees | 85.78 | 7.63 | 11.24× | Alloc-heavy trees |
| sum | 24.51 | 1.95 | 12.55× | Integer accumulation |
| mbrot | 32.06 | 2.68 | 11.96× | Float complex math |
| levenshtein | 69.55 | 5.37 | 12.96× | 2D array + loops |
| list-A | 10.44 | 0.76 | 13.68× | Linked list OOP |
| pnpoly | 120.37 | 7.72 | 15.59× | Array + float math |
| bounce | 14.70 | 0.85 | 17.27× | OOP class methods |
| crypto_sha1 | 235.72 | 12.41 | 19.00× | Bitwise + array ops |
| diviter | 12259.43 | 615.44 | 19.92× | Long integer loop |

**E-Tier: Critical (>20×)**

| Benchmark | LJS (ms) | V8 (ms) | Ratio | Pattern |
|-----------|----------|---------|-------|---------|
| storage | 20.40 | 0.90 | 22.72× | Array alloc OOP |
| queens-A | 22.69 | 0.97 | 23.36× | OOP class methods |
| deriv | 168.80 | 6.14 | 27.48× | Recursive structure |
| permute | 37.09 | 1.06 | 34.91× | OOP + array |
| towers | 69.13 | 1.65 | 41.92× | OOP linked list |
| navier_stokes | 1019.00 | 21.63 | 47.12× | Float array ops |
| gcbench | 1957.34 | 36.72 | 53.31× | GC allocation |
| richards-J | 735.89 | 12.25 | 60.08× | OOP method dispatch |
| json-A | 299.05 | 3.70 | 80.90× | OOP + hash map |
| richards-A | 8332.02 | 66.43 | 125.43× | OOP method dispatch |
| deltablue | 2514.22 | 18.93 | 132.82× | OOP constraints |
| matmul | 2989.24 | 21.95 | 136.19× | Float 2D array |
| nbody-B | 1886.97 | 11.83 | 159.53× | Float + object |
| nbody-A | 1749.21 | 7.43 | 235.48× | Float + class field |

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

### P1: Inline Property Access for Known Shapes (Priority: CRITICAL)

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

### P3: Inline Method Dispatch for Monomorphic Call Sites (Priority: HIGH)

**Problem:** `obj.method(args)` compiles to two runtime calls: `js_property_access(obj, "method")` to fetch the function, then `js_call_function(fn, this, args, argc)` to invoke it. For benchmarks like richards (125×) and deltablue (133×), the inner loop is dominated by virtual method dispatch.

**Current state:** The P7 optimization exists for direct native calls where the transpiler can resolve the method at compile time. But this requires the transpiler to know both the class of the receiver AND that no subclass overrides the method.

**Proposed fix — Inline Cache (IC) at call site:**
```c
// Monomorphic IC: cache the last shape → function pointer mapping
static Shape* cached_shape = NULL;
static FuncPtr cached_fn = NULL;

if (obj->shape == cached_shape) {
    result = cached_fn(obj, args...);  // fast path: direct call
} else {
    fn = js_property_access(obj, "method");
    cached_shape = obj->shape;
    cached_fn = extract_func(fn);
    result = js_call_function(fn, obj, args, argc);
}
```

In MIR, this translates to a shape-check guard + direct function call.

For class hierarchies with known subclasses (common in AWFY), emit a **switch on shape**:
```mir
// Virtual dispatch for node.process() where Node has 3 subclasses
shape = load_i64(node, 0)  // shape pointer at offset 0
if shape == WorkerNode_shape: call WorkerNode_process(node)
elif shape == HandlerNode_shape: call HandlerNode_process(node)
else: call generic_dispatch(node, "process", ...)
```

**Expected impact:** 5–20× speedup on method-heavy benchmarks → richards from 125× to ~10×, deltablue from 133× to ~15×.

**Effort:** Medium-High. Requires:
1. Shape comparison in generated MIR code
2. Per-call-site IC slot allocation (MIR global variables)
3. Polymorphic fallback for megamorphic sites

---

### P4: Direct Array Access in Typed Loops (Priority: HIGH)

**Problem:** Array access in tight loops (`arr[i]` where both arr and i are known types) still goes through bounds checking + deleted-sentinel checking per access. For `matmul` (136×), the inner loop does 3 array reads and 1 array write per iteration, each with full runtime overhead.

**Current state:** The A2 optimization provides inline bounds checking, but still requires:
- Loading `arr->length` every iteration (cache-miss potential)
- Checking `arr->items[idx] != JS_DELETED_SENTINEL` every access
- Function call fallback for out-of-bounds

**Proposed fix — Loop-hoisted bounds check:**
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

**Expected impact:** 5–10× speedup on array-intensive loops → matmul from 136× to ~15×, navier_stokes from 47× to ~8×.

**Effort:** Medium. Requires:
1. Loop analysis to identify array access patterns
2. Bounds-check hoisting (prove loop bounds ≤ array length)
3. Array items pointer hoisting (prove no resize in loop body)

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

### Phase 1: Property Access Fix (P1) — Impact: 10–50× on OOP benchmarks

1. Add `offsets[]` flat array to Shape struct alongside existing `ShapeEntry` linked list
2. Populate offsets at shape creation (one-time cost)
3. Emit `js_get_shaped_slot_fast(obj, slot_index)` using O(1) array lookup
4. Emit inline MIR load/store for known shape + known slot index
5. Add shape-guard at method entry: `if (obj->shape != expected) goto slow_path`

**Validation:** Re-run AWFY suite. Target: geo mean from 25× to ≤ 8×.

### Phase 2: Float Unboxing (P2) + Array Hoisting (P4)

1. Implement field type tracking (float vs int vs mixed) in shape metadata
2. Emit native float load/store for shaped float fields
3. Implement loop-invariant array pointer hoisting
4. Eliminate per-iteration bounds checks where provably safe

**Validation:** nbody < 10×, matmul < 10×, mandelbrot < 3×.

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

| Suite | Current Geo Mean | Target Geo Mean | Improvement |
|-------|-----------------|----------------|-------------|
| R7RS | 3.12× | **2.0×** | 1.5× |
| AWFY | 25.82× | **5.0×** | 5× |
| BENG | 3.65× | **2.0×** | 1.8× |
| KOSTYA | 6.91× | **3.0×** | 2.3× |
| LARCENY | 5.28× | **3.0×** | 1.8× |
| **Overall** | **~8× geo mean** | **~3× geo mean** | **2.7×** |

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

## Appendix: Raw Benchmark Data

```
Benchmark               LambdaJS (ms)   Node.js (ms)   Ratio
─── R7RS ───
fib                         13.32           2.81        4.74×
fibfp                       21.82           2.46        8.86×
tak                          1.10           1.06        1.03×
cpstak                       2.30           1.37        1.68×
sum                         24.51           1.95       12.55×
sumfp                        4.44           1.26        3.53×
nqueens                      3.03           2.50        1.21×
fft                           ---           2.31        FAIL
mbrot                       32.06           2.68       11.96×
ack                         11.19          18.55        0.60×

─── AWFY ───
sieve                        0.16           0.51        0.32×
permute                     37.09           1.06       34.91×
queens                      22.69           0.97       23.36×
towers                      69.13           1.65       41.92×
bounce                      14.70           0.85       17.27×
list                        10.44           0.76       13.68×
storage                     20.40           0.90       22.72×
mandelbrot                 195.07          41.65        4.68×
nbody                     1749.21           7.43      235.48×
richards                  8332.02          66.43      125.43×
json                       299.05           3.70       80.90×
deltablue                 2514.22          18.93      132.82×
havlak                        ---         182.63        FAIL
cd                            ---          61.96        FAIL

─── BENG ───
binarytrees                 85.78           7.63       11.24×
fannkuch                     1.47           6.88        0.21×
fasta                       19.05           9.15        2.08×
knucleotide                   ---           6.92        FAIL
mandelbrot                 103.99          21.23        4.90×
nbody                     1886.97          11.83      159.53×
pidigits                     0.73           2.83        0.26×
regexredux                    ---           3.69        FAIL
revcomp                       ---           4.80        FAIL
spectralnorm                41.13           4.76        8.64×

─── KOSTYA ───
brainfuck                  646.15          66.20        9.76×
matmul                    2989.24          21.95      136.19×
primes                       7.03           6.06        1.16×
base64                      66.85          26.77        2.50×
levenshtein                 69.55           5.37       12.96×
json_gen                    33.25           9.08        3.66×
collatz                   7643.68        1849.04        4.13×

─── LARCENY ───
triangl                   1027.60          96.75       10.62×
array1                       1.09           2.65        0.41×
deriv                      168.80           6.14       27.48×
diviter                  12259.43         615.44       19.92×
divrec                      24.80          10.17        2.44×
gcbench                   1957.34          36.72       53.31×
paraffins                    1.38           1.33        1.04×
pnpoly                     120.37           7.72       15.59×
primes                       7.40           6.14        1.21×
puzzle                      20.23           5.22        3.87×
quicksort                   15.84           2.42        6.54×
ray                         15.96           5.22        3.06×

─── JETSTREAM ───
navier_stokes             1019.00          21.63       47.12×
richards                   735.89          12.25       60.08×
crypto_sha1                235.72          12.41       19.00×
```
