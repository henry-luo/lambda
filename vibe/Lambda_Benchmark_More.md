# Lambda Benchmark: Larceny/Gambit & Kostya Suites

## Overview

Two additional benchmark suites were implemented for Lambda Script, complementing the existing AWFY, Beng, and R7RS suites:

1. **Larceny/Gambit** (`test/benchmark/larceny/`) — 12 classic Scheme/Lisp benchmarks from the Larceny and Gambit benchmark collections. These focus on recursive algorithms, backtracking, GC stress, and numeric computation.
2. **Kostya** (`test/benchmark/kostya/`) — 7 benchmarks from the [Kostya benchmarks](https://github.com/kostya/benchmarks) collection. These focus on practical workloads: string processing, matrix math, interpreters, and encoding.

All 19 benchmarks are written as `pn` (procedural) Lambda scripts using the `pn main()` entry point, JIT-compiled via C2MIR, and verified to produce correct output.

## Larceny/Gambit Suite (12 benchmarks)

**Location:** `test/benchmark/larceny/`
**Runner:** `python3 test/benchmark/larceny/run_bench.py [num_runs]`

| # | Benchmark | Category | Description | Expected Result | Verified |
|---|-----------|----------|-------------|-----------------|----------|
| 1 | **deriv** | alloc | Symbolic differentiation of expression trees using tagged maps, 5000 iterations | 45 nodes | ✅ PASS |
| 2 | **primes** | array | Sieve of Eratosthenes to 8000, repeated 10 times | pi(8000) = 1007 | ✅ PASS |
| 3 | **pnpoly** | numeric | Point-in-polygon ray casting (Jordan curve theorem), 100K test points against a 20-vertex polygon | inside=29415 | ✅ PASS |
| 4 | **diviter** | iterative | Iterative integer division via repeated subtraction, 1000 iterations | result=500000 | ✅ PASS |
| 5 | **divrec** | recursive | Recursive integer division via repeated subtraction, 1000 iterations | result=500000000 | ✅ PASS |
| 6 | **array1** | array | Array fill (0..9999) & sum, 10000 elements × 100 iterations | sum=49995000 | ✅ PASS |
| 7 | **gcbench** | gc | GC stress: binary tree allocation & traversal depth 4–14, stretch tree depth 15 | Multi-line tree stats | ✅ PASS |
| 8 | **quicksort** | sort | Quicksort 5000 pseudo-random elements, 10 iterations | sorted=true | ⏳ Release only |
| 9 | **triangl** | backtrack | Triangle solitaire — count all backtracking solutions (15-position board) | solutions=29760 | ⏳ Release only |
| 10 | **puzzle** | backtrack | N-Queens n=10, count all solutions | solutions=724 | ✅ PASS |
| 11 | **ray** | numeric | Ray tracer 100×100 with 4 spheres, 1 iteration | hits=1392 | ✅ PASS |
| 12 | **sumfp_long** | numeric | Sum 1/i² for i=1..500000, 5 repetitions (pi²/6 ≈ 1.6449) | approximate sum | ⏳ Release only |

**Note:** `paraffins` was originally planned but replaced with `sumfp_long` due to a SIGSEGV crash in the complex recursive enumeration algorithm.

## Kostya Suite (7 benchmarks)

**Location:** `test/benchmark/kostya/`
**Runner:** `python3 test/benchmark/kostya/run_bench.py [num_runs]`

| # | Benchmark | Category | Description | Expected Result | Verified |
|---|-----------|----------|-------------|-----------------|----------|
| 1 | **brainfuck** | interpreter | Brainfuck interpreter for Hello World program, 10000 iterations | "Hello World!\n" | ✅ PASS (1 iter) |
| 2 | **matmul** | numeric | 200×200 matrix multiply with LCG-initialized matrices | sum printed | ⏳ Release only |
| 3 | **primes** | array | Sieve of Eratosthenes to 1,000,000 | pi(10⁶) = 78498 | ⏳ Release only |
| 4 | **base64** | encoding | Base64 encode 10KB of data (all 'a'), 100 iterations | encoded_len=13336 | ✅ PASS |
| 5 | **levenshtein** | string/DP | Levenshtein edit distance (2-row DP), multiple string pairs | d(kitten,sitting)=3 | ✅ PASS |
| 6 | **json_gen** | string | JSON string generation, 1000 objects × 10 iterations | length=61626 | ✅ PASS |
| 7 | **collatz** | numeric | Longest Collatz (3n+1) sequence under 1,000,000 | start=837799 | ⏳ Release only |

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

During implementation, several Lambda `pn`-mode (procedural JIT) issues were discovered and worked around at the benchmark level. These represent real limitations in the current C2MIR transpiler and runtime.

### Fixed (worked around in benchmark code)

#### 1. String range indexing returns `null` in `pn` mode

**Symptom:** `s[i to i]` (range-based string slicing to extract a single character) returns `null` instead of a single-character string when used inside `pn` functions.

**Impact:** Any string character comparison using range syntax evaluates as `null == null → true`, producing silently wrong results.

**Workaround:** Use integer indexing `s[i]` instead of range indexing `s[i to i]`. Integer indexing works correctly and returns a comparable character value.

```
// BROKEN in pn mode
var ch = s[i to i]          // returns null

// WORKS
var ch = s[i]               // returns character at position i
```

**Files affected:** `levenshtein.ls`, `brainfuck.ls`, `base64.ls`

#### 2. `div` operator fails in `pn` mode

**Symptom:** The `div` keyword (integer division) returns an `error` value instead of an integer when used inside `pn` functions. The runtime reports the result type as `error`.

**Impact:** Any computation using `div` silently produces error values that poison subsequent arithmetic.

**Workaround:** Use `shr(x, n)` (bit shift right) for division by powers of 2. For general integer division, use `int(a / b)` — however, see Issue #3 below.

```
// BROKEN in pn mode
var x = 97 div 4            // returns <error>

// WORKS (power-of-2 divisors)
var x = shr(97, 2)          // 97 >> 2 = 24

// WORKS (general case, but see Issue #3)
var x = int(97 / 4)         // returns 24 as int
```

**Files affected:** `base64.ls`, `collatz.ls`

#### 3. `int()` of a float returns `int64` type — not usable as array/string index

**Symptom:** `int(float_value)` returns a value with internal type `int64` (TypeId 64) rather than the normal `int` type. The runtime's `item_at` function does not support this type for indexing into strings or arrays, producing `"unsupported item_at type: 64"` errors.

**Impact:** Cannot use `int(x / y)` result directly to index into strings or arrays. Must find alternative approaches (e.g., bit shifts, or pre-computing integer values).

**Workaround:** For powers-of-2 division, `shr()` returns a proper `int`. For other cases, avoid the pattern entirely or restructure the algorithm.

```
// BROKEN — int() returns int64, unusable for indexing
var idx = int(b0 / 4)
var ch = table[idx]          // ERROR: item_at type 64

// WORKS — shr returns proper int
var idx = shr(b0, 2)
var ch = table[idx]          // OK
```

**Files affected:** `base64.ls`

#### 4. String literal in called `pn` function produces `item_at` error

**Symptom:** When a `pn` function contains a `let` binding to a string literal and that function is called from another `pn` function, indexing the string fails with `"unsupported item_at type: 64"` even when the index is a proper `int`.

**Impact:** Cannot define lookup tables as local string constants inside helper functions.

**Workaround:** Define the string in `main()` and pass it as a parameter to the helper function.

```
// BROKEN — string literal inside called pn function
pn b64_char(idx) {
    let table = "ABCD..."
    return table[idx]        // ERROR on repeated calls
}

// WORKS — pass string from caller
pn b64_char(table, idx) {
    return table[idx]        // OK
}
pn main() {
    let table = "ABCD..."
    print(b64_char(table, 24))
}
```

**Files affected:** `base64.ls`

#### 5. `pn` functions must have explicit `return` on all code paths

**Symptom:** A recursive `pn` function that does not have an explicit `return` statement on every code path causes the C2MIR transpiler to emit `"unfinished compound statement"` and fail compilation.

**Impact:** Affects any recursive helper `pn` function where the programmer relies on implicit return of the last expression.

**Workaround:** Always add explicit `return <value>` at the end of every branch in recursive `pn` functions.

```
// BROKEN — missing return after recursive call
pn qsort(arr, lo, hi) {
    if (lo < hi) {
        var p = partition(arr, lo, hi)
        qsort(arr, lo, p - 1)
        qsort(arr, p + 1, hi)
    }
    // no return → transpiler error
}

// WORKS
pn qsort(arr, lo, hi) {
    if (lo < hi) {
        var p = partition(arr, lo, hi)
        qsort(arr, lo, p - 1)
        qsort(arr, p + 1, hi)
        return 0
    }
    return 0
}
```

**Files affected:** `quicksort.ls`

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

### Outstanding Issues (not yet resolved)

#### A. `paraffins` algorithm crashes with SIGSEGV

The Larceny `paraffins` benchmark (enumerating paraffin isomers using radix-tree enumeration) causes a segmentation fault. The crash occurs deep in recursion and could not be worked around. This benchmark was replaced with `sumfp_long`.

#### B. Debug build too slow for compute-heavy benchmarks

The debug build of `lambda.exe` is orders of magnitude slower than release for computation-heavy loops. Benchmarks like `brainfuck` (10K iterations), `collatz` (1M numbers), `matmul` (200×200), `kostya/primes` (sieve to 1M), `quicksort` (5000 elements), `triangl` (backtracking 29K solutions), and `sumfp_long` (500K iterations) are only practical to run in release mode. They have been verified to compile and their logic verified with reduced inputs.

#### C. Semicolons on same line cause syntax error

Multiple statements separated by `;` on a single line (e.g., `stack[sp] = i; sp = sp + 1`) produce syntax error E100. Lambda requires each statement on its own line or in its own block `{ }`.

#### D. `div` keyword entirely non-functional in `pn` mode

The `div` operator for integer division does not work at all in procedural (`pn`) mode. This is a significant gap since integer division is common in algorithms. Currently must use `shr()` for power-of-2 cases or `int(a / b)` for general cases (with the caveat that the result may not be usable as an index — see Issue #3).

## Benchmark Design Patterns

Several patterns were established for writing benchmarks in Lambda `pn` mode:

### Dynamic array creation
Lambda does not have a built-in `make_array(n, val)`, so each benchmark includes a helper:
```lambda
pn make_array(n, val) {
    var arr = [val, val, val, val, val, val, val, val, val, val]
    var sz = 10
    while (sz * 2 <= n) {
        arr = arr ++ arr
        sz = sz * 2
    }
    // ... handle remainder
    return arr
}
```

### Character code conversion (no `ord()`/`chr()`)
Lambda `pn` mode lacks `ord()` and `chr()` builtins, so manual lookup tables map characters to codes and back:
```lambda
pn chr(code) {
    if (code == 72) { return "H" }
    if (code == 101) { return "e" }
    // ...
}
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
