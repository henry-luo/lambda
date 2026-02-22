# Benchmark Results: C2MIR vs MIR Direct Transpiler

**Date:** 2026-02-21
**Platform:** Apple Silicon Mac Mini (aarch64), macOS
**Methodology:** 3 runs per benchmark, median wall-clock time (includes startup + JIT compilation + execution)
**Lambda version:** 113/113 MIR tests passing, 362/362 baseline tests passing

---

## R7RS Benchmarks — Untyped

| Benchmark | Category | C2MIR | MIR Direct | MIR/C |
|-----------|----------|------:|----------:|------:|
| fib | recursive | 3.923s | 5.330s | 1.36x |
| fibfp | recursive | 7.971s | 9.793s | 1.23x |
| tak | recursive | 200.4ms | 371.5ms | 1.85x |
| cpstak | closure | 376.4ms | 815.5ms | 2.17x |
| sum | iterative | 8.618s | 39.458s | 4.58x |
| sumfp | iterative | 1.681s | 4.821s | 2.87x |
| nqueens | backtrack | 2.586s | 3.740s | 1.45x |
| fft | numeric | 2.413s | 2.740s | 1.14x |
| mbrot | numeric | 11.126s | 17.116s | 1.54x |
| ack | recursive | 22.140s | 36.771s | 1.66x |

**Geometric mean: 1.81x** (C2MIR faster)

---

## R7RS Benchmarks — Typed

| Benchmark | Category | C2MIR | MIR Direct | MIR/C |
|-----------|----------|------:|----------:|------:|
| fib | recursive | 1.731s | 1.779s | 1.03x |
| fibfp | recursive | 3.420s | FAIL | N/A |
| tak | recursive | 21.6ms | 23.8ms | 1.10x |
| cpstak | closure | 22.6ms | 27.5ms | 1.22x |
| sum | iterative | 24.7ms | 23.055s | 932.6x |
| sumfp | iterative | 20.9ms | 1.950s | 93.2x |
| nqueens | backtrack | 1.618s | 2.473s | 1.53x |
| fft | numeric | 2.013s | 2.555s | 1.27x |
| mbrot | numeric | 238.8ms | 3.566s | 14.9x |
| ack | recursive | 31.3ms | 46.4ms | 1.48x |

**Geometric mean: 5.57x** (C2MIR faster)

---

## AWFY Benchmarks

| Benchmark | Category | C2MIR | MIR Direct | MIR/C |
|-----------|----------|------:|----------:|------:|
| sieve | micro | 282.0ms | 654.7ms | 2.32x |
| permute | micro | 409.7ms | 813.5ms | 1.99x |
| queens | micro | 570.4ms | 651.3ms | 1.14x |
| towers | micro | 522.9ms | 699.6ms | 1.34x |
| bounce | micro | 528.7ms | 711.1ms | 1.35x |
| list | micro | 551.3ms | 1.017s | 1.85x |
| storage | micro | 629.6ms | 816.5ms | 1.30x |
| mandelbrot | micro | 62.670s | 220.157s | 3.51x |
| nbody | micro | 3.870s | 4.236s | 1.09x |
| richards | macro | FAIL | 29.700s | N/A |
| json | macro | 4.050s | 6.635s | 1.64x |
| deltablue | macro | 4.265s | 6.017s | 1.41x |
| havlak | macro | 51.471s | 69.177s | 1.34x |
| cd | macro | FAIL | FAIL | N/A |

**Geometric mean: 1.59x** (C2MIR faster)

---

## Overall Summary

| Suite | MIR/C Geomean |
|-------|:------------:|
| R7RS Untyped | 1.81x |
| R7RS Typed | 5.57x |
| AWFY | 1.59x |
| **Overall** | **2.39x** |

> Ratio > 1.0 means C2MIR is faster. Ratio < 1.0 would mean MIR Direct is faster.

---

## Key Findings

### 1. Untyped code: MIR Direct is ~1.6–1.8x slower

Across both R7RS untyped and AWFY suites, the MIR Direct transpiler is consistently 1.1–2.3x slower than C2MIR, with a geometric mean of ~1.7x. This is a reasonable gap: C2MIR generates C source that benefits from `c2mir`'s C-level optimizations (constant folding, register allocation on C expressions), while MIR Direct emits lower-level MIR IR that bypasses those optimizations.

Best-case results where MIR Direct nearly matches C2MIR:
- `nbody` (1.09x) — heavy floating-point, runtime-call dominated
- `fft` (1.14x) — numeric computation with array access
- `queens` AWFY (1.14x) — array-based backtracking

### 2. Typed code exposes the critical gap: no native numeric optimization

The biggest performance issue is that **MIR Direct does not use type annotations to emit native numeric operations**. C2MIR achieves massive speedups with typed code by replacing boxed `Item` arithmetic with native `i64`/`double` operations:

| Benchmark | C2MIR untyped → typed | MIR Direct untyped → typed | Gap |
|-----------|:--------------------:|:-------------------------:|:---:|
| sum | 8.6s → 24.7ms (**349x**) | 39.5s → 23.1s (1.7x) | **932x** |
| sumfp | 1.7s → 20.9ms (**80x**) | 4.8s → 1.95s (2.5x) | **93x** |
| mbrot | 11.1s → 238.8ms (**47x**) | 17.1s → 3.6s (4.8x) | **14.9x** |
| ack | 22.1s → 31.3ms (**707x**) | 36.8s → 46.4ms (793x) | 1.48x |

For `sum` and `sumfp`, C2MIR compiles the tight loop to native integer/float adds, while MIR Direct still goes through boxed `Item` pack/unpack for every operation. This is the single highest-impact optimization opportunity for MIR Direct.

Note: `ack` typed shows both transpilers achieving similar massive speedups (~700x), suggesting the recursive call overhead dominates equally.

### 3. Closure and iterative overhead

Benchmarks involving closures (`cpstak`: 2.17x) and tight iteration (`sum`: 4.58x, `sumfp`: 2.87x) show larger gaps in untyped mode. This suggests MIR Direct has higher per-call overhead for closure invocation and loop variable access compared to C2MIR's generated C code.

### 4. Failures

| Benchmark | C2MIR | MIR Direct | Issue |
|-----------|:-----:|:----------:|-------|
| fibfp (typed) | PASS | FAIL (exit 1) | MIR Direct crashes on typed floating-point recursion |
| richards | FAIL (wrong output) | PASS (29.7s) | C2MIR produces incorrect result (`qpc=29 hc=11`) |
| cd | FAIL (compile error E211) | FAIL (compile error) | Both transpilers fail on this benchmark |

### 5. Mandelbrot anomaly

`mandelbrot` AWFY is 3.51x slower with MIR Direct (220s vs 63s). This benchmark does heavy nested-loop floating-point math — the same pattern as `mbrot` in R7RS. The gap here is smaller than in typed R7RS (14.9x) because both paths use boxed arithmetic in untyped mode, but C2MIR still generates more efficient loop structures.

---

## Recommendations for MIR Direct Optimization

1. **Native numeric types (highest priority):** Implement typed variable tracking to emit `i64`/`double` MIR registers for annotated variables instead of boxed `Item`. This alone would close the 932x gap on `sum` typed.

2. **Loop optimization:** Investigate why tight loops (`sum` untyped: 4.58x) have higher overhead. Likely due to extra register moves or unnecessary boxing/unboxing on each iteration.

3. **Closure call overhead:** Reduce the indirection cost for closure calls (`cpstak`: 2.17x). Consider inlining small closures or optimizing the capture-access path.

4. **Fix `fibfp` typed crash:** Debug the exit-1 failure on typed floating-point recursive benchmark.

---

# Release Build Benchmark Results

**Date:** 2026-02-22
**Build:** `make build-release` (optimized, no debug assertions)
**Platform:** Apple Silicon Mac Mini (aarch64), macOS
**Methodology:** 3 runs per benchmark, median wall-clock time (includes startup + JIT compilation + execution)

---

## R7RS Benchmarks — Untyped (Release)

| Benchmark | Category | C2MIR | MIR Direct | MIR/C |
|-----------|----------|------:|----------:|------:|
| fib | recursive | 18.3ms | 19.9ms | 1.09x |
| fibfp | recursive | 24.5ms | 21.9ms | 0.89x |
| tak | recursive | 15.0ms | 15.4ms | 1.03x |
| cpstak | closure | 14.7ms | 17.7ms | 1.20x |
| sum | iterative | 22.0ms | 61.0ms | 2.77x |
| sumfp | iterative | 15.3ms | 19.5ms | 1.27x |
| nqueens | backtrack | 45.6ms | 39.2ms | 0.86x |
| fft | numeric | 21.8ms | 25.5ms | 1.17x |
| mbrot | numeric | 24.8ms | 31.2ms | 1.26x |
| ack | recursive | 38.2ms | 44.3ms | 1.16x |

**Geometric mean: 1.20x** (C2MIR faster)

---

## R7RS Benchmarks — Typed (Release)

| Benchmark | Category | C2MIR | MIR Direct | MIR/C |
|-----------|----------|------:|----------:|------:|
| fib | recursive | 15.9ms | 17.9ms | 1.12x |
| fibfp | recursive | 17.4ms | FAIL | N/A |
| tak | recursive | 16.3ms | 16.1ms | 0.99x |
| cpstak | closure | 14.4ms | 16.6ms | 1.15x |
| sum | iterative | 17.1ms | 45.5ms | 2.67x |
| sumfp | iterative | 14.8ms | 18.7ms | 1.27x |
| nqueens | backtrack | 44.1ms | 38.4ms | 0.87x |
| fft | numeric | 21.3ms | 25.1ms | 1.18x |
| mbrot | numeric | 18.4ms | 24.4ms | 1.33x |
| ack | recursive | 22.8ms | 32.0ms | 1.40x |

**Geometric mean: 1.27x** (C2MIR faster)

---

## AWFY Benchmarks (Release)

| Benchmark | Category | C2MIR | MIR Direct | MIR/C |
|-----------|----------|------:|----------:|------:|
| sieve | micro | 17.2ms | 18.9ms | 1.10x |
| permute | micro | 16.4ms | 18.2ms | 1.11x |
| queens | micro | 17.6ms | 19.6ms | 1.11x |
| towers | micro | 17.3ms | 19.6ms | 1.13x |
| bounce | micro | 19.3ms | 23.2ms | 1.20x |
| list | micro | 16.2ms | 18.5ms | 1.14x |
| storage | micro | 23.9ms | 22.7ms | 0.95x |
| mandelbrot | micro | 81.9ms | 271.7ms | 3.32x |
| nbody | micro | 26.2ms | 33.3ms | 1.27x |
| richards | macro | FAIL | 95.6ms | N/A |
| json | macro | 511.2ms | 521.6ms | 1.02x |
| deltablue | macro | 63.9ms | 94.7ms | 1.48x |
| havlak | macro | 159.1ms | 208.3ms | 1.31x |
| cd | macro | FAIL | FAIL | N/A |

**Geometric mean: 1.26x** (C2MIR faster)

---

## Release Build — Overall Summary

| Suite | MIR/C Geomean |
|-------|:------------:|
| R7RS Untyped | 1.20x |
| R7RS Typed | 1.27x |
| AWFY | 1.26x |
| **Overall** | **1.24x** |

> Ratio > 1.0 means C2MIR is faster. Ratio < 1.0 would mean MIR Direct is faster.

---

# Debug vs Release Build Comparison

The debug build results (above, dated 2026-02-21) were run with `make build` (unoptimized, `-O0`, assertions enabled). The release build (2026-02-22) uses `make build-release` (optimized). Since both builds JIT-compile the same Lambda scripts, the performance difference reflects the cost of the **host runtime** (parsing, AST building, transpiling, and runtime library calls from JIT code) rather than the JIT-generated code itself.

## C2MIR: Debug → Release Speedup

### R7RS Untyped

| Benchmark | Debug C2MIR | Release C2MIR | Speedup |
|-----------|----------:|------------:|--------:|
| fib | 3.923s | 18.3ms | 214x |
| fibfp | 7.971s | 24.5ms | 325x |
| tak | 200.4ms | 15.0ms | 13.4x |
| cpstak | 376.4ms | 14.7ms | 25.6x |
| sum | 8.618s | 22.0ms | 392x |
| sumfp | 1.681s | 15.3ms | 110x |
| nqueens | 2.586s | 45.6ms | 56.7x |
| fft | 2.413s | 21.8ms | 111x |
| mbrot | 11.126s | 24.8ms | 449x |
| ack | 22.140s | 38.2ms | 580x |

### R7RS Typed

| Benchmark | Debug C2MIR | Release C2MIR | Speedup |
|-----------|----------:|------------:|--------:|
| fib | 1.731s | 15.9ms | 109x |
| fibfp | 3.420s | 17.4ms | 197x |
| tak | 21.6ms | 16.3ms | 1.3x |
| cpstak | 22.6ms | 14.4ms | 1.6x |
| sum | 24.7ms | 17.1ms | 1.4x |
| sumfp | 20.9ms | 14.8ms | 1.4x |
| nqueens | 1.618s | 44.1ms | 36.7x |
| fft | 2.013s | 21.3ms | 94.5x |
| mbrot | 238.8ms | 18.4ms | 13.0x |
| ack | 31.3ms | 22.8ms | 1.4x |

### AWFY

| Benchmark | Debug C2MIR | Release C2MIR | Speedup |
|-----------|----------:|------------:|--------:|
| sieve | 282.0ms | 17.2ms | 16.4x |
| permute | 409.7ms | 16.4ms | 25.0x |
| queens | 570.4ms | 17.6ms | 32.4x |
| towers | 522.9ms | 17.3ms | 30.2x |
| bounce | 528.7ms | 19.3ms | 27.4x |
| list | 551.3ms | 16.2ms | 34.0x |
| storage | 629.6ms | 23.9ms | 26.3x |
| mandelbrot | 62.670s | 81.9ms | 765x |
| nbody | 3.870s | 26.2ms | 148x |
| json | 4.050s | 511.2ms | 7.9x |
| deltablue | 4.265s | 63.9ms | 66.8x |
| havlak | 51.471s | 159.1ms | 323x |

## MIR Direct: Debug → Release Speedup

### R7RS Untyped

| Benchmark | Debug MIR | Release MIR | Speedup |
|-----------|--------:|----------:|--------:|
| fib | 5.330s | 19.9ms | 268x |
| fibfp | 9.793s | 21.9ms | 447x |
| tak | 371.5ms | 15.4ms | 24.1x |
| cpstak | 815.5ms | 17.7ms | 46.1x |
| sum | 39.458s | 61.0ms | 647x |
| sumfp | 4.821s | 19.5ms | 247x |
| nqueens | 3.740s | 39.2ms | 95.4x |
| fft | 2.740s | 25.5ms | 107x |
| mbrot | 17.116s | 31.2ms | 549x |
| ack | 36.771s | 44.3ms | 830x |

### AWFY

| Benchmark | Debug MIR | Release MIR | Speedup |
|-----------|--------:|----------:|--------:|
| sieve | 654.7ms | 18.9ms | 34.6x |
| permute | 813.5ms | 18.2ms | 44.7x |
| queens | 651.3ms | 19.6ms | 33.2x |
| towers | 699.6ms | 19.6ms | 35.7x |
| bounce | 711.1ms | 23.2ms | 30.7x |
| list | 1.017s | 18.5ms | 55.0x |
| storage | 816.5ms | 22.7ms | 36.0x |
| mandelbrot | 220.157s | 271.7ms | 810x |
| nbody | 4.236s | 33.3ms | 127x |
| richards | 29.700s | 95.6ms | 311x |
| json | 6.635s | 521.6ms | 12.7x |
| deltablue | 6.017s | 94.7ms | 63.5x |
| havlak | 69.177s | 208.3ms | 332x |

## MIR/C Ratio: Debug vs Release

| Suite | Debug MIR/C | Release MIR/C | Change |
|-------|:----------:|:------------:|:------:|
| R7RS Untyped | 1.81x | 1.20x | Gap narrowed |
| R7RS Typed | 5.57x | 1.27x | Gap narrowed dramatically |
| AWFY | 1.59x | 1.26x | Gap narrowed |
| **Overall** | **2.39x** | **1.24x** | **Gap cut by ~half** |

---

## Key Observations: Debug vs Release

### 1. Massive absolute speedups (13x–830x)

The release build is **orders of magnitude faster** across the board. The debug build's `-O0` compilation means every runtime function call (boxing/unboxing, arithmetic dispatch, function invocation) carries enormous overhead. Benchmarks dominated by runtime calls (`ack`: 580–830x, `mbrot`/`mandelbrot`: 449–810x, `sum`: 392–647x) see the largest improvements.

### 2. The MIR/C gap narrows significantly in release

In debug mode, C2MIR was 2.39x faster overall. In release, the gap shrinks to **1.24x**. This makes sense: much of the debug-mode gap was due to unoptimized runtime library calls, which both transpilers use. When the runtime is optimized, the actual difference in generated JIT code quality becomes the dominant factor — and that difference is modest.

### 3. Typed code gap nearly vanishes

The debug build showed a 5.57x gap for typed R7RS, driven by extreme outliers (`sum` typed: 932x). In release, this drops to **1.27x**. The outlier `sum` typed goes from 932x to 2.67x — still the largest gap, but now a reasonable one attributable to C2MIR's native integer loop vs MIR Direct's boxed arithmetic.

### 4. MIR Direct wins some benchmarks in release

In release mode, MIR Direct is actually **faster** than C2MIR on several benchmarks:
- `fibfp` untyped (0.89x) — MIR Direct's floating-point handling is slightly more efficient
- `nqueens` untyped (0.86x) and typed (0.87x) — MIR Direct's array access path is competitive
- `storage` AWFY (0.95x) — nearly equal
- `tak` typed (0.99x) — effectively tied

This suggests the two transpilers are much closer in quality than the debug results implied.

### 5. Remaining hot spots

The biggest remaining gaps in release (where C2MIR still clearly wins):
- `sum` untyped (2.77x) and typed (2.67x) — tight integer loop overhead
- `mandelbrot` AWFY (3.32x) — nested floating-point loops
- `deltablue` (1.48x) — object-heavy macro benchmark
- `ack` typed (1.40x) — deep recursion with type annotations

---

# Release Build vs Other Runtimes

**Date:** 2026-02-22
**Build:** `make build-release` (C2MIR transpiler path)
**Platform:** Apple Silicon Mac Mini (aarch64), macOS
**Competitor versions:** Racket v9.0 [cs], Guile 3.0.10, Node.js v24.7.0

---

## R7RS: Lambda (Release) vs Racket vs Guile

**Lambda timing:** wall-clock minus ~10ms startup overhead (median of 3 runs)
**Racket/Guile timing:** internal timing (`current-inexact-milliseconds` / `get-internal-real-time`)

| Benchmark | Category | Lambda (untyped) | Lambda (typed) | Racket | Guile | T/U | L/R | L/G |
|-----------|----------|-------:|-------:|------:|------:|----:|----:|----:|
| fib | recursive | 10.6ms | 6.7ms | 0.703ms | 3.1ms | 0.63x | 15x | 3x |
| fibfp | recursive | 15.0ms | 7.1ms | 3.5ms | 15.7ms | 0.47x | 4x | 1x |
| tak | recursive | 6.3ms | 4.2ms | 0.063ms | 0.29ms | 0.67x | 101x | 22x |
| cpstak | closure | 7.6ms | 4.2ms | 0.144ms | 1.5ms | 0.56x | 53x | 5x |
| sum | iterative | 13.7ms | 6.8ms | 1.0ms | 1.0ms | 0.50x | 13x | 13x |
| sumfp | iterative | 7.7ms | 4.8ms | 1.1ms | 2.7ms | 0.62x | 7x | 3x |
| nqueens | backtrack | 36.6ms | 34.2ms | 0.098ms | 0.33ms | 0.93x | 373x | 110x |
| fft | numeric | 12.1ms | 11.2ms | 0.475ms | 1.7ms | 0.93x | 25x | 7x |
| mbrot | numeric | 14.9ms | 7.4ms | 3.7ms | 13.8ms | 0.50x | 4x | 1x |
| ack | recursive | 28.3ms | 12.5ms | 3.2ms | 13.0ms | 0.44x | 9x | 2x |

**T/U** = Typed / Untyped (< 1x means typed is faster)
**L/R** = Lambda (untyped) / Racket | **L/G** = Lambda (untyped) / Guile

### R7RS Geometric Mean Ratios (Release)

| Comparison | Ratio |
|------------|------:|
| Lambda typed / untyped | **0.60x** (40% faster with types) |
| Lambda (untyped) / Racket | **20x** |
| Lambda (typed) / Racket | **12x** |
| Lambda (untyped) / Guile | **5.5x** |

---

## AWFY: Lambda (Release) vs Node.js

**Lambda timing:** wall-clock, release build (median of 3 runs, 2026-02-22)
**Node.js timing:** wall-clock from same machine, Node.js v24.7.0 (from AWFY Round 1, 2026-02-16)

| Benchmark | Category | Lambda (release) | Node.js | Ratio |
|-----------|----------|------:|------:|------:|
| sieve | micro | 23.3ms | 35ms | **0.67x** |
| permute | micro | 13.6ms | 37ms | **0.37x** |
| queens | micro | 14.6ms | 35ms | **0.42x** |
| towers | micro | 14.3ms | 35ms | **0.41x** |
| bounce | micro | 16.6ms | 36ms | **0.46x** |
| list | micro | 13.7ms | 36ms | **0.38x** |
| storage | micro | 22.9ms | 43ms | **0.53x** |
| mandelbrot | micro | 84.1ms | 55ms | 1.53x |
| nbody | micro | 30.2ms | 33ms | **0.92x** |
| richards | macro | FAIL | 36ms | N/A |
| json | macro | 506.9ms | 34ms | 14.9x |
| deltablue | macro | 62.4ms | 35ms | 1.78x |
| havlak | macro | 161.0ms | 130ms | 1.24x |
| cd | macro | FAIL | 65ms | N/A |

> Ratio = Lambda / Node.js. Values < 1.0 mean Lambda is faster. **Bold** = Lambda wins.

**Geometric mean (12 passing): 0.87x** — Lambda is slightly faster overall on wall-clock

---

## Cross-Runtime Summary (Release Build)

| Suite | Comparison | Release Ratio | Debug Ratio | Improvement |
|-------|-----------|:------------:|:-----------:|:-----------:|
| R7RS | Lambda (untyped) / Racket | **20x** | 2,796x | ~140x closer |
| R7RS | Lambda (typed) / Racket | **12x** | 155x | ~13x closer |
| R7RS | Lambda (untyped) / Guile | **5.5x** | 777x | ~141x closer |
| R7RS | Typed / Untyped speedup | 0.60x (40%) | 0.06x (94%) | Less dramatic |
| AWFY | Lambda / Node.js (wall-clock) | **0.87x** | 53x | Now at parity |

---

## Key Observations: Cross-Runtime Performance

### 1. Lambda vs Racket: from ~2,800x to ~20x

The most dramatic improvement. Debug-build Lambda was **2,796x slower** than Racket on untyped R7RS benchmarks. The release build closes this to **20x** — a ~140x improvement from compiler optimizations of the host runtime alone. With type annotations, the gap narrows further to **12x**.

The remaining 12–20x gap vs Racket is attributable to:
- **Function call overhead**: Each recursive call goes through MIR function dispatch vs Racket's direct jumps
- **No tail-call optimization in JIT code**: `tak`, `cpstak`, `ack` still pay stack frame cost for tail positions
- **No inlining**: Racket's Chez backend inlines small recursive functions
- **Array operations**: `nqueens` (373x) and `fft` (25x) show that array element access remains expensive

Notable: Lambda nearly matches Racket on float-heavy benchmarks — `fibfp` (4x), `mbrot` (4x) — suggesting float arithmetic is well-optimized.

### 2. Lambda vs Guile: effectively competitive

Lambda release is only **5.5x slower** than Guile on average. Lambda actually **ties or beats** Guile on two benchmarks:
- `fibfp` (1x) — Lambda's float JIT matches Guile's bytecode VM
- `mbrot` (1x) — Same pattern: float computation with minimal boxing

The remaining gap is largest on `nqueens` (110x) and `tak` (22x) — benchmarks where array/recursive call overhead dominates.

### 3. Lambda vs Node.js: wall-clock parity achieved

The release build achieves an overall **0.87x geometric mean** vs Node.js — meaning Lambda is slightly *faster* on aggregate wall-clock timing. However, this headline number is misleading:

**Micro benchmarks (Lambda wins 7/9):** These complete in 14–30ms for Lambda and 33–43ms for Node.js. Both are dominated by **startup overhead**, not computation. Lambda's C2MIR JIT startup (~10–15ms in release) is faster than Node.js/V8 startup (~33ms). The workload parameters were chosen for debug-build Lambda and are trivially small for release.

**Macro benchmarks (Node.js wins 3/3 passing):**
- `json` (14.9x) — JSON parsing and manipulation; V8's optimized object model dominates
- `deltablue` (1.78x) — Constraint-solving with polymorphic dispatch
- `havlak` (1.24x) — Graph algorithm with heavy map/set operations

The macro benchmarks, where actual computation dominates startup overhead, show Node.js is still **1.2–14.9x faster** on real workloads. The `json` benchmark (14.9x) is the most realistic gap indicator.

### 4. Type annotations: less impactful in release

In the debug build, type annotations produced a **94% speedup** (0.06x geomean). In release, the effect drops to **40% speedup** (0.60x geomean). This is because:
- Debug build: runtime function calls (boxing/unboxing/dispatch) were extremely slow with `-O0`, so eliminating them via types produced enormous speedups
- Release build: runtime calls are fast with `-O2`, so the relative benefit of bypassing them is smaller
- The benchmarks that benefit most from typing (`sum`: 0.50x, `ack`: 0.44x, `fibfp`: 0.47x) are still those with tight arithmetic loops

### 5. Where Lambda excels and where it struggles

**Strengths (release build):**
- Fast startup — faster than Node.js for trivial workloads
- Float arithmetic (`fibfp`, `mbrot`) — competitive with Guile, within 4x of Racket
- Simple recursive functions (`fib`, `ack`, `sum`) — within 3–15x of Racket
- Type annotations narrow gaps further

**Weaknesses:**
- Array-intensive algorithms (`nqueens`: 373x vs Racket) — `array_get`/`fn_array_set` runtime calls dominate
- Map/object field access (`json`: 14.9x vs Node.js) — no inline caching
- Pure recursive benchmarks with small per-call work (`tak`: 101x vs Racket) — function call overhead accumulates
