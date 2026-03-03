# Lambda Benchmark: Larceny/Gambit & Kostya Suites

## Overview

Two additional benchmark suites were implemented for Lambda Script, complementing the existing AWFY, Beng, and R7RS suites:

1. **Larceny/Gambit** (`test/benchmark/larceny/`) — 12 classic Scheme/Lisp benchmarks from the Larceny and Gambit benchmark collections. These focus on recursive algorithms, backtracking, GC stress, and numeric computation.
2. **Kostya** (`test/benchmark/kostya/`) — 7 benchmarks from the [Kostya benchmarks](https://github.com/kostya/benchmarks) collection. These focus on practical workloads: string processing, matrix math, interpreters, and encoding.

All 19 benchmarks are written as `pn` (procedural) Lambda scripts using the `pn main()` entry point, JIT-compiled via C2MIR, and verified to produce correct output.

## Larceny/Gambit Suite (12 benchmarks)

**Location:** `test/benchmark/larceny/`
**Runner:** `python3 test/benchmark/larceny/run_bench.py [num_runs]`

| #   | Benchmark     | Category  | Description                                                                                       | Expected Result       | Verified |
| --- | ------------- | --------- | ------------------------------------------------------------------------------------------------- | --------------------- | -------- |
| 1   | **deriv**     | alloc     | Symbolic differentiation of expression trees using tagged maps, 5000 iterations                   | 45 nodes              | ✅ PASS   |
| 2   | **primes**    | array     | Sieve of Eratosthenes to 8000, repeated 10 times                                                  | pi(8000) = 1007       | ✅ PASS   |
| 3   | **pnpoly**    | numeric   | Point-in-polygon ray casting (Jordan curve theorem), 100K test points against a 20-vertex polygon | inside=29415          | ✅ PASS   |
| 4   | **diviter**   | iterative | Iterative integer division via repeated subtraction, 1000 iterations                              | result=500000         | ✅ PASS   |
| 5   | **divrec**    | recursive | Recursive integer division via repeated subtraction, 1000 iterations                              | result=500000000      | ✅ PASS   |
| 6   | **array1**    | array     | Array fill (0..9999) & sum, 10000 elements × 100 iterations                                       | sum=49995000          | ✅ PASS   |
| 7   | **gcbench**   | gc        | GC stress: binary tree allocation & traversal depth 4–14, stretch tree depth 15                   | Multi-line tree stats | ✅ PASS   |
| 8   | **quicksort** | sort      | Quicksort 5000 pseudo-random elements, 10 iterations                                              | sorted=true           | ✅ PASS   |
| 9   | **triangl**   | backtrack | Triangle solitaire — count all backtracking solutions (15-position board)                         | solutions=29760       | ✅ PASS   |
| 10  | **puzzle**    | backtrack | N-Queens n=10, count all solutions                                                                | solutions=724         | ✅ PASS   |
| 11  | **ray**       | numeric   | Ray tracer 100×100 with 4 spheres, 1 iteration                                                    | hits=1392             | ✅ PASS   |
| 12  | **paraffins** | recursive | Paraffin isomer counting (OEIS A000602) nb(1..23) × 10 iters                                      | nb(23)=5731580        | ✅ PASS   |

## Kostya Suite (7 benchmarks)

**Location:** `test/benchmark/kostya/`
**Runner:** `python3 test/benchmark/kostya/run_bench.py [num_runs]`

| # | Benchmark | Category | Description | Expected Result | Verified |
|---|-----------|----------|-------------|-----------------|----------|
| 1 | **brainfuck** | interpreter | Brainfuck interpreter for Hello World program, 10000 iterations | "Hello World!\n" | ✅ PASS |
| 2 | **matmul** | numeric | 200×200 matrix multiply with LCG-initialized matrices | sum printed | ✅ PASS |
| 3 | **primes** | array | Sieve of Eratosthenes to 1,000,000 | pi(10⁶) = 78498 | ✅ PASS |
| 4 | **base64** | encoding | Base64 encode 10KB of data (all 'a'), 100 iterations | encoded_len=13336 | ✅ PASS |
| 5 | **levenshtein** | string/DP | Levenshtein edit distance (2-row DP), multiple string pairs | d(kitten,sitting)=3 | ✅ PASS |
| 6 | **json_gen** | string | JSON string generation, 1000 objects × 10 iterations | length=61626 | ✅ PASS |
| 7 | **collatz** | numeric | Longest Collatz (3n+1) sequence under 1,000,000 | start=837799 | ✅ PASS |

## File Structure

Each suite contains:
```
test/benchmark/<suite>/
├── run_bench.py       # Runner with wall-clock timing and CSV output
├── Result.md          # Placeholder for performance results
├── <name>.ls          # Lambda benchmark script
└── <name>.txt         # Expected output for verification
```

## Lambda Language Issues Discovered & Fixed

During implementation, several Lambda `pn`-mode (procedural JIT) issues were discovered. All have been fixed in the engine and workarounds removed from benchmark code.

### All Issues — FIXED

#### ~~1. String range indexing returns `null` in `pn` mode~~ — FIXED in engine

**Status:** ✅ **Resolved.** Two bugs were fixed in the engine:

1. **C transpiler boxing bug** (`transpile.cpp`, `transpile_index_expr`): When the index expression is non-numeric (e.g., a Range from `i to i`), the object was passed to `fn_index()` as a raw pointer via `transpile_expr()` instead of a boxed Item via `transpile_box_item()`. This caused `get_type_id()` to misread the string's `len` field as its TypeId.
2. **Missing Range handler in `fn_index`** (`lambda-eval.cpp`): `fn_index()` had no code path for Range-typed index values. Added handling that extracts `start`/`end` from the Range and delegates to `fn_slice()` (with inclusive→exclusive end conversion).

`s[i to i]` now works correctly in `pn` mode. Note: `s[i]` (integer indexing) is still more efficient for single-character access since it avoids Range allocation and substring overhead.

**Files affected by fix:** `lambda/transpile.cpp`, `lambda/lambda-eval.cpp`

#### 2. `div` operator fails in `pn` mode — FIXED

**Symptom:** The `div` keyword (integer division) returned an `error` value instead of an integer when used inside `pn` functions. The bug was **pn-only** — `div` worked correctly in `fn` mode.

**Why pn-only:** The transpiler generates different C code for `fn` vs `pn` variable bindings:

- **`fn` mode** — All `let` bindings use the generic `Item` type (a tagged 64-bit value). Since `fn_idiv()` returns `Item`, there is no type mismatch:
  ```c
  // fn: let q = a div b → everything is Item, no mismatch
  Item result = fn_idiv(i2it(97), i2it(4));   // Item → Item ✓
  ```

- **`pn` mode** — The transpiler infers concrete C types for `var` bindings based on AST type analysis. Since `int div int` infers to `int`, the variable is declared as native `int64_t`. But `fn_idiv()` still returns a **boxed `Item`** (to handle div-by-zero → `ItemError`), creating a type mismatch:
  ```c
  // pn: var q = a div b → variable typed as int64_t, but fn_idiv returns Item
  int64_t _q = fn_idiv(i2it(_a), i2it(_b));   // Item crammed into int64_t ✗
  pn_print(i2it(_q));                          // re-boxes garbage → <error>
  ```

**Root Cause:** `fn_idiv()` returns a boxed `Item`, but the pn transpiler declared result variables as native `int64_t` (because AST type inference says `int div int = int`). The boxed Item was stored raw, then re-boxed with `i2it()` at use, creating a garbage value.

**Fix:** Added targeted unboxing (`it2i()`) in `transpile_assign_expr` and `transpile_assign_stam` when the RHS is an IDIV binary expression. The generated code now correctly unwraps:
```c
// After fix: unbox Item → native int at assignment
int64_t _q = it2i(fn_idiv(i2it(_a), i2it(_b)));   // Item → int64_t ✓
pn_print(i2it(_q));                                 // boxes native int → correct Item ✓
```

**Files affected by fix:** `lambda/transpile.cpp`, `lambda/build_ast.cpp`

#### 3. `int()` of a float returns `int64` type — not usable as array/string index — NO LONGER REPRODUCIBLE

**Original Symptom:** `int(float_value)` returned a value with internal type `int64` (TypeId 64) that could not be used for indexing into strings or arrays.

**Current Status:** This issue is no longer reproducible. The runtime's `fn_index()` handles `LMD_TYPE_INT64` correctly (extracts via `get_int64()` and passes to `item_at()`), and the transpiler now keeps `fn_int()` results as `Item` type rather than misboxing them. The original failure was likely a manifestation of the same pn boxing bug family as Issue #2 (`div`).

```
// Now works correctly in pn mode:
var idx = int(b0 / 4)
var ch = table[idx]          // OK — fn_index handles INT64
```

#### 4. String literal in called `pn` function produces `item_at` error — NO LONGER REPRODUCIBLE

**Original Symptom:** When a `pn` function contains a `let` binding to a string literal and that function is called from another `pn` function, indexing the string failed with `"unsupported item_at type: 64"`.

**Current Status:** This issue is no longer reproducible. String literals in called `pn` functions now work correctly, including repeated calls (tested with 64 consecutive calls). The original failure was likely related to the same pn transpiler boxing issues fixed in Issue #2 (`div`).

#### 5. `pn` functions must have explicit `return` on all code paths — FIXED

**Status:** ✅ **Resolved.** The transpiler now correctly handles `pn` functions where the body is a single control flow statement (if/while/for/match) without wrapping content. When the body is a single `AST_NODE_IF_EXPR`, the transpiler routes to `transpile_if_stam` and appends `return ITEM_NULL;` as a fallthrough, instead of generating an invalid nested return in ternary expression.

**Files affected by fix:** `lambda/transpile.cpp`

#### 6. Large integer overflow in LCG random number generators

**Symptom:** The standard LCG multiplier `seed * 1103515245` overflows Lambda's safe integer range (53-bit mantissa), producing an error value. The runtime reports `"unknown comparing type: error"` when subsequent comparisons are attempted.

**Impact:** Any benchmark using the standard glibc LCG (`seed * 1103515245 + 12345`) fails.

**Workaround:** Use a smaller multiplier that stays within safe range: `(seed * 1664525 + 1013904223) % 1000000`.

```
// BROKEN — overflows safe int range
pn next_rand(seed) { return seed * 1103515245 + 12345 }

// WORKS — stays within safe range
pn next_rand(seed) { return (seed * 1664525 + 1013904223) % 1000000 }
```

**Files affected:** `quicksort.ls`, `matmul.ls`, `json_gen.ls`

#### 7. Modulo (`%`) on float values returns error — FIXED

**Symptom:** `float_value % int_value` returned `<error>` instead of computing the remainder. Since `/` always returns float, any value derived from division that is subsequently used with `%` produced an error, which silently poisoned all downstream arithmetic.

**Root cause:** `fn_mod()` in `lambda-eval-num.cpp` explicitly rejected float operands with `"modulo not supported for float types"` → `ItemError`. This was a **runtime limitation** (not a transpiler bug like Issue #2), affecting both `fn` and `pn` modes equally.

**Fix:** Replaced the error branch in `fn_mod()` with proper `fmod()` computation from `<cmath>`. When either operand is `LMD_TYPE_FLOAT`, both operands are promoted to `double` (handling `INT`, `INT64`, and `FLOAT` sources) and `fmod(a, b)` is called. Division by zero returns `ItemError`.

**Verification:**
- `10.5 % 3` → `1.5` (fn and pn modes)
- `17.0 % 5` → `2`, `10 % 3.5` → `3`, `7.5 % 2.5` → `0`
- `10.5 % 0` → `<error>` (zero check works)
- 570/572 baseline tests pass (2 pre-existing failures unrelated)
- Existing `numeric_expr` and `cross_type_arithmetic` tests updated to expect float mod results instead of errors

### Resolved Issues

#### A. `paraffins` SIGSEGV — fixed

The original `paraffins` implementation (building explicit tree structures) caused a SIGSEGV crash deep in recursion. **Fixed** by rewriting with a counting-only approach: instead of allocating radical tree nodes, it counts radicals at each size using multiset combination formulas (ms2, ms3, ms4). A critical algorithmic fix was adding a `max_rad = floor((n-1)/2)` constraint in the CCP (carbon-centered paraffin) enumeration to prevent overlap with BCP (bond-centered) counting and array out-of-bounds access. Now uses the `div` operator directly for integer division.

### Remaining Limitations

#### B. Debug build too slow for compute-heavy benchmarks

The debug build of `lambda.exe` is orders of magnitude slower than release for computation-heavy loops. Benchmarks like `pnpoly`, `brainfuck`, `collatz`, `matmul`, `kostya/primes`, and `triangl` are only practical to run in release mode (`make release`). All 19 benchmarks pass in release mode.

## Node.js Comparison

All 19 benchmarks were also implemented in JavaScript and timed with Node.js v22.13.0 (V8 JIT) on the same machine. Both Lambda and Node.js scripts include in-script timing (`clock()` and `process.hrtime.bigint()` respectively) to separate JIT/startup overhead from pure execution time. Three runs per benchmark, median reported.

### Summary — Total Time (Wall-Clock)

| Suite | Lambda Geo Mean | Node.js Geo Mean | Ratio |
|-------|----------------|-----------------|-------|
| Kostya (7) | 212.2 ms | 29.2 ms | 7.3× |
| Larceny (12) | 64.8 ms | 8.9 ms | 7.3× |

### Summary — Exec Time Only (Excluding JIT/Startup)

| Suite | Lambda Exec Geo Mean | Node.js Exec Geo Mean | Exec Ratio |
|-------|---------------------|----------------------|------------|
| Kostya (7) | 187.1 ms | 22.0 ms | 8.5× |
| Larceny (12) | 37.2 ms | 6.6 ms | 5.6× |

### JIT Overhead

| | Lambda JIT (median) | Node.js JIT (median) |
|---|--------------------|--------------------|
| Kostya | ~9 ms | ~3 ms |
| Larceny | ~6 ms | ~1.2 ms |

Lambda's C2MIR JIT overhead is 4–15 ms per benchmark (parsing + AST + MIR transpilation + compilation). This is negligible for long-running benchmarks but adds 20–80% overhead for sub-10ms benchmarks.

Lambda is roughly **7× slower** than Node.js (V8 JIT) on wall-clock, and **6–9× slower** on pure execution time across these suites. The gap varies from 0.9× (Larceny primes — Lambda exec is *faster*!) to 105× (gcbench — GC stress).

### Key Observations

- **Lambda exec beats Node.js** on one benchmark: Larceny `primes` (0.9× — Lambda 1.5ms vs Node 1.7ms for sieve to 8000). On `paraffins` (1.1×) and `divrec` (1.2×), they are nearly tied.
- **Closest performance** (2–4×): Simple loop/recursion benchmarks (collatz, ray, puzzle, quicksort) where Lambda's C2MIR JIT generates efficient native code.
- **Medium gap** (5–15×): Array-heavy and string/DP benchmarks (array1, levenshtein, deriv, diviter) where V8's typed-array and string optimizations provide an advantage.
- **Largest gap** (25–105×): GC stress (gcbench) and backtracking with array copies (triangl) — Lambda's garbage collector and object allocation are the primary bottleneck in these workloads.

JS benchmark files: `test/benchmark/kostya/*.js`, `test/benchmark/larceny/*.js`
Timing runner: `temp/run_node_bench.py`

## Benchmark Design Patterns

Several patterns were established for writing benchmarks in Lambda `pn` mode:

### Dynamic array creation
Lambda has a built-in `fill(n, val)` function that creates an array of `n` elements initialized to `val`:
```lambda
var arr = fill(1000, 0)         // [0, 0, 0, ..., 0] — 1000 zeros
var flags = fill(8001, true)    // [true, true, ..., true] — 8001 bools
var strs = fill(3, "hello")    // ["hello", "hello", "hello"] — 3 strings
```

### Character code conversion — `ord()` and `chr()`
Lambda has built-in `ord(str)` and `chr(int)` functions for Unicode code point conversion:
```lambda
ord("A")    // 65
chr(65)     // "A"
ord("é")    // 233
chr(128512) // "😀"
```

### Safe LCG random numbers
```lambda
pn next_rand(seed) {
    return (seed * 1664525 + 1013904223) % 1000000
}
```

## Running the Benchmarks

```bash
# Run Larceny suite (default 3 runs each)
python3 test/benchmark/larceny/run_bench.py

# Run Kostya suite (5 runs each)
python3 test/benchmark/kostya/run_bench.py 5

# Run a single benchmark directly
./lambda.exe run test/benchmark/larceny/deriv.ls
./lambda.exe run test/benchmark/kostya/levenshtein.ls
```

## All Benchmark Suites

| Suite | Location | Count | Focus |
|-------|----------|-------|-------|
| AWFY | `test/benchmark/awfy/` | — | OOP patterns, classic CS algorithms |
| Beng | `test/benchmark/beng/` | — | General-purpose |
| R7RS | `test/benchmark/r7rs/` | 10 | Functional/recursive (Scheme ports) |
| **Larceny** | `test/benchmark/larceny/` | 12 | Classic Scheme: recursion, backtracking, GC, numeric |
| **Kostya** | `test/benchmark/kostya/` | 7 | Practical: strings, math, interpreters, encoding |
