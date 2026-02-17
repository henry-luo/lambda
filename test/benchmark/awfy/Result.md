# AWFY Benchmark Results — Round 1

**Date:** February 16, 2026
**Lambda Script:** C2MIR JIT (`pn` procedural mode)
**Node.js:** v24.7.0 (V8 JIT)
**Platform:** macOS, Apple Silicon (Mac Mini)
**Runs per benchmark:** 3 (median reported)

## All 14 AWFY Benchmarks Implemented

| # | Micro Benchmarks | Status | # | Macro Benchmarks | Status |
|---|-----------------|--------|---|-----------------|--------|
| 1 | Sieve           | PASS   | 10 | Richards       | PASS   |
| 2 | Permute         | PASS   | 11 | Json           | PASS   |
| 3 | Queens          | PASS   | 12 | DeltaBlue      | PASS   |
| 4 | Towers          | PASS   | 13 | Havlak         | PASS   |
| 5 | Bounce          | PASS   | 14 | CD             | PASS   |
| 6 | List            | PASS   |    |                |        |
| 7 | Storage         | PASS   |    |                |        |
| 8 | Mandelbrot      | PASS   |    |                |        |
| 9 | NBody           | PASS   |    |                |        |

## Wall-Clock Timing: Lambda vs Node.js

Median of 3 runs. Includes full process lifecycle: startup + parsing + JIT compilation + execution.

| Benchmark    | Lambda    | Node.js  | Ratio     | Category |
|-------------|-----------|----------|-----------|----------|
| Sieve       | 251 ms    | 35 ms    | 7.2x      | micro    |
| Permute     | 331 ms    | 37 ms    | 9.1x      | micro    |
| Queens      | 578 ms    | 35 ms    | 16.4x     | micro    |
| Towers      | 385 ms    | 35 ms    | 11.1x     | micro    |
| Bounce      | 409 ms    | 36 ms    | 11.4x     | micro    |
| List        | 392 ms    | 36 ms    | 10.8x     | micro    |
| Storage     | 448 ms    | 43 ms    | 10.6x     | micro    |
| Mandelbrot  | 37.6 s    | 55 ms    | 685x      | micro    |
| NBody       | 2.64 s    | 33 ms    | 80x       | micro    |
| Richards    | 10.8 s    | 36 ms    | 303x      | macro    |
| Json        | 2.31 s    | 34 ms    | 67x       | macro    |
| DeltaBlue   | 2.73 s    | 35 ms    | 79x       | macro    |
| Havlak      | 31.2 s    | 130 ms   | 241x      | macro    |
| CD          | 297 s     | 65 ms    | 4598x     | macro    |
| **Geo Mean**|           |          | **53x**   |          |

> Ratio = Lambda / Node.js. Lower is better for Lambda. Values > 1.0 mean Node.js is faster.

## Node.js Internal Timing (Benchmark-Only, No Startup)

Node.js internal timing measured inside the process via `process.hrtime()`, excluding startup overhead.

| Benchmark    | Node.js (internal) | Lambda (total) | Ratio     |
|-------------|-------------------|----------------|-----------|
| Sieve       | 1.0 ms            | 251 ms         | 247x      |
| Permute     | 146 us            | 331 ms         | 2270x     |
| Queens      | 180 us            | 578 ms         | 3208x     |
| Towers      | 310 us            | 385 ms         | 1241x     |
| Bounce      | 380 us            | 409 ms         | 1075x     |
| List        | 112 us            | 392 ms         | 3499x     |
| Storage     | 726 us            | 448 ms         | 617x      |
| Mandelbrot  | 21.6 ms           | 37.6 s         | 1739x     |
| Richards    | 1.4 ms            | 10.8 s         | 7752x     |
| Json        | 1.3 ms            | 2.31 s         | 1717x     |
| DeltaBlue   | 1.6 ms            | 2.73 s         | 1708x     |
| Havlak      | 80.4 ms           | 31.2 s         | 388x      |
| CD          | 24.4 ms           | 297 s          | 12153x    |

> Note: This comparison is unfair to Lambda — Lambda's total includes startup (~200ms) while Node.js internal excludes startup entirely.

## Analysis

### Startup Overhead

- **Lambda startup:** ~200 ms (parse + tree-sitter CST → AST → C transpile → C2MIR JIT compile)
- **Node.js startup:** ~33 ms
- Lambda startup is ~6x slower. For micro benchmarks with trivial workload (e.g., Permute at 146us in Node.js), Lambda's 200ms startup dominates the total time.

### Execution Performance Bottlenecks

After subtracting startup, Lambda is still **50–12000x slower** than V8 on pure benchmark execution. Root causes:

1. **No type specialization.** Every arithmetic operation goes through generic runtime calls (`fn_add`, `fn_mul`, `fn_div`) that unbox the 64-bit tagged `Item`, perform the operation, and rebox the result. V8 generates type-specialized native machine code after profiling.

2. **No loop optimization.** C2MIR does not hoist loop-invariant computations, unroll loops, or perform strength reduction. V8 applies aggressive loop optimizations via TurboFan.

3. **No inline caching.** Array access (`arr[i]`) and map field access go through `fn_array_get`/`fn_map_get` runtime calls every time. V8 caches field offsets and array element kinds for near-zero-cost access.

4. **Tagged pointer overhead for floats.** Lambda stores floats as pointers to `double` values (with type tag bits), requiring dereferencing on every read. V8 can keep floats unboxed in registers.

5. **Extreme cases — Mandelbrot (685x) and CD (4598x).** These are tight floating-point computation loops where V8's register allocation and SIMD-capable codegen dominate. Lambda's per-operation function-call overhead is maximally exposed.

### Micro vs Macro Pattern

- **Micro benchmarks (7–16x):** Dominated by Lambda's startup overhead. Actual compute is tiny.
- **Macro benchmarks (67–4598x):** Dominated by execution inefficiency. Startup is negligible relative to seconds/minutes of runtime.

## Issues Identified During Implementation

### Transpiler Bug #21: FLOAT Reassignment Inside `if` Blocks

**Symptom:** Assigning a float expression result to a variable inside an `if` block produces garbage values (the raw 64-bit tagged `Item` bits interpreted as a `double`, e.g. `3.60288e+17`).

```javascript
pn broken(x) {
  var v = 0.0
  if (x != 0) {
    v = x / 2.0       // BUG: v gets tagged Item bits, not the double value
  }
  return v
}
```

**Root cause:** The transpiler's code generation for variable assignment inside conditional blocks skips the `it2d()` extraction when the target variable is typed as `double`. Top-level assignments and `return` statements handle this correctly.

**What works inside `if` blocks:**
- `return expr` (correct extraction path)
- `v = 3.14` (literal constant, no Item extraction needed)
- `v = intVar` (INT is packed inline, trivial extraction)
- `arr[i] = val` with `var _d = 0` trick (uses `fn_array_set` function call)

**What fails inside `if` blocks:**
- `v = expr / y` (float result from runtime division)
- `v = fnCall()` (float result from function return)
- Any runtime float expression assigned to a variable

**Workarounds used in CD benchmark:**
```javascript
// Helper functions — return works correctly inside if
pn safe_div(a, b) { if (b == 0) { return 0.0 }; return a / b }
pn min_f(a, b) { if (a < b) { return a }; return b }
pn max_f(a, b) { if (a > b) { return a }; return b }

// Instead of: if (x != 0) { lowX = expr / x }
// Use:        var lowX = safe_div(expr, x)

// Instead of: var v = t1; if (t1 <= 0) { v = 0.0 }
// Use:        var v = max_f(t1, 0.0)

// Instead of: if (old != null) { x = old[0] }
// Use:        var src = get_old_or_new(old, fallback); var x = src[0]
```

### Other Transpiler Limitations Encountered

| # | Issue | Workaround |
|---|-------|-----------|
| 4 | `obj.x` without parentheses → path literal | Always use `(obj.x)` |
| 6 | No `for` loops, no nested `pn`, no closures in `pn` | Use `while` loops |
| 7 | `/` division always returns FLOAT | Use `shr(x, 1)` for integer divide-by-2 |
| 8 | Subscript assignment sole stmt in `if` dropped | Add `var _d = 0` before it |
| 10 | `fill(n, val)` arrays segfault on set | Use literal arrays |
| 14 | `%` results can't index arrays from fn returns | Launder through map |
| 15 | Map `==` comparison broken | Use integer ID fields |

## Benchmark Runner

```bash
# Run all benchmarks (3 iterations each)
python3 test/benchmark/awfy/run_bench.py 3

# Run with more iterations for stability
python3 test/benchmark/awfy/run_bench.py 10
```

Results saved to `temp/bench_results.csv`.

---

# AWFY Benchmark Results — Round 2 (Type Annotations)

**Date:** February 17, 2026
**Change:** Added `: int` and `: float` type annotations to all benchmark scripts (`*2.ls` variants)
**Goal:** Bypass generic `fn_add`/`fn_mul`/`fn_div` runtime calls — typed variables produce native C `+`/`*`/`/` operators in transpiled code

## Approach

For each benchmark `foo.ls`, a typed version `foo2.ls` was created with:

1. **`: int` on all integer variables** — loop counters, array indices, node IDs, colors, sizes, flags
2. **`: int` on function parameters** — enables native arithmetic inside the function body AND auto-inserts `it2i()` at call sites for Item→int conversion
3. **`: float` on function parameters** — enables native `double` arithmetic inside the function body AND auto-inserts `it2d()` at call sites; transpiler generates native `sqrt()`, `sin()`, `cos()` calls when args are typed
4. **Refactored hot loops** — e.g., CD's `find_intersection` was split into 3 functions (`find_intersection` → `fi_compute(12 float params)` → `fi_collide(15 float params)`) so all float math stays in native `double` registers

### What CAN be typed
- `var i: int = 0` — loop counters, arithmetic operands
- `var x: int = arr[idx]` — works for int values from generic arrays (int bits truncate correctly)
- `pn foo(a: int, b: float)` — typed params produce native C types; call sites auto-convert
- `var r: float = a * b` where `a`, `b` are already typed float

### What CANNOT be typed (transpiler bug #21)
- `var x: float = arr[idx]` — produces garbage (tagged Item bits interpreted as double)
- Float variable reassignment inside `if` blocks — same underlying bug

### Workaround for float from array
Pass array element values through **typed function parameters** at call boundaries:
```javascript
// Array float values → typed params → native arithmetic
pn compute(a: float, b: float) { return a * b + a }  // native double ops
pn main() {
    var arr = [3.14, 2.71]
    compute(arr[0], arr[1])  // transpiler inserts it2d() at call site
}
```

## Round 2 Results: Untyped vs Typed Lambda

Median wall-clock time. Runs: 3 for fast benchmarks, 1 for slow (CD, Havlak, Mandelbrot, Richards).

| Benchmark    | Untyped   | Typed     | Speedup | Node.js  | Typed/Node.js |
|-------------|-----------|-----------|---------|----------|---------------|
| Sieve       | 192 ms    | 153 ms    | **1.25x** | 45 ms  | 3.4x          |
| Permute     | 285 ms    | 248 ms    | **1.15x** | 31 ms  | 8.1x          |
| Queens      | 404 ms    | 301 ms    | **1.34x** | 31 ms  | 9.8x          |
| Towers      | 359 ms    | 254 ms    | **1.41x** | 30 ms  | 8.3x          |
| Bounce      | 359 ms    | 356 ms    | 1.01x   | 31 ms    | 11.6x         |
| List        | 374 ms    | 368 ms    | 1.02x   | 31 ms    | 12.1x         |
| Storage     | 429 ms    | 383 ms    | **1.12x** | 31 ms  | 12.2x         |
| Mandelbrot  | 37.4 s    | 35.1 s    | 1.07x   | 53 ms    | 660x          |
| NBody       | 2.61 s    | 2.54 s    | 1.02x   | 34 ms    | 75x           |
| Richards    | 10.3 s    | 10.2 s    | 1.01x   | 34 ms    | 300x          |
| Json        | 2.27 s    | 2.19 s    | 1.04x   | 33 ms    | 67x           |
| DeltaBlue   | 2.69 s    | 2.69 s    | 1.00x   | 33 ms    | 83x           |
| Havlak      | 31.9 s    | 31.1 s    | 1.03x   | 119 ms   | 261x          |
| CD          | 295 s     | 208 s     | **1.42x** | 61 ms  | 3409x         |
| **Geo Mean**|           |           | **1.13x** |        | **47x**       |

## Round 1 → Round 2 Comparison

| Metric                      | Round 1 | Round 2 | Improvement |
|-----------------------------|---------|---------|-------------|
| Lambda vs Node.js (geo mean) | 53x     | **47x** | 11% closer  |
| CD (worst case)             | 4598x   | 3409x   | 26% better  |
| Typing speedup (geo mean)   | —       | 1.13x   | 13% faster  |

## Analysis

### Where Typing Helps Most

| Benchmark | Speedup | Why |
|-----------|---------|-----|
| **CD** | **1.42x** | Massive float computation refactored into typed-param functions (`fi_compute`, `fi_collide` with 12–15 `double` params). All vector math, discriminant, collision-point calculations run as native `double` arithmetic. RBT integer comparisons also benefit. |
| **Towers** | **1.41x** | Tight loops with typed `int` counters and comparisons. Disk push/pop operations use typed node IDs. |
| **Queens** | **1.34x** | Backtracking with typed `int` arrays and loop counters. Each queen placement check becomes native `int` comparison. |
| **Sieve** | **1.25x** | Boolean array sieving with typed `int` index — tight inner loop becomes native `while (i < n) { i = i + prime }`. |

### Where Typing Doesn't Help

| Benchmark | Speedup | Why |
|-----------|---------|-----|
| **DeltaBlue** | 1.00x | Bottleneck is polymorphic constraint dispatch via `match` and map field access (`fn_map_get`/`fn_map_set`). Scalar arithmetic is a tiny fraction of runtime. |
| **Bounce** | 1.01x | Most time in map field reads/writes for ball positions. Only loop counter benefits from typing. |
| **List** | 1.02x | Recursive linked-list traversal. Bottleneck is map construction (`{val:, next:}`) and field access, not arithmetic. |
| **Mandelbrot** | 1.07x | Despite heavy float math, the bottleneck is the untyped outer loop and `array_get`/`fn_array_set` for pixel storage. Inner complex-number arithmetic can't easily be typed because intermediate values come from array reads (bug #21). |

### Remaining Performance Gap

Even with typing, Lambda is **47x slower** than Node.js (geometric mean). The dominant bottlenecks that typing cannot address:

1. **Map field access** — `fn_map_get(obj, key)` runtime call for every `(obj.field)` read. V8 uses hidden classes + inline caches for O(1) field access.
2. **Array element access** — `array_get(arr, i)` returns a tagged `Item` requiring unboxing. V8 keeps typed arrays unboxed.
3. **Function call overhead** — Every `pn` call goes through the C calling convention with `LAMBDA_STACK_CHECK`. V8 can inline small functions.
4. **No loop-invariant code motion** — C2MIR doesn't hoist repeated map lookups out of loops.
5. **Transpiler bug #21** — Prevents typing float variables initialized from array access, limiting optimization in Mandelbrot, NBody, and other float-heavy code.

## Benchmark Runner (Round 2)

```bash
# Run typed vs untyped comparison (3 iterations)
python3 test/benchmark/awfy/run_bench_typed.py 3

# Results saved to temp/bench_typed_results.csv
```
