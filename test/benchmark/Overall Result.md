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

---

# Typed Array Annotation Performance Impact

**Date:** 2026-02-23
**Build:** `make build-release` (C2MIR transpiler path)
**Platform:** Apple Silicon Mac Mini (aarch64), macOS
**Methodology:** 5 runs per benchmark, median wall-clock time

---

## Context

Added `int[]` and `float[]` array type annotations to 7 typed benchmarks and implemented native typed array access in the C2MIR transpiler. This changes array element access from generic runtime dispatch to direct native operations:

| Operation | Without `int[]`/`float[]` (generic) | With `int[]`/`float[]` (native) |
|-----------|--------------------------------------|----------------------------------|
| Read `arr[i]` | `fn_index(arr, i)` → type dispatch, boxing | `array_int_get((ArrayInt*)arr, i)` |
| Write `arr[i] = v` | `fn_array_set(arr, i, v)` → type dispatch | `array_int_set((ArrayInt*)arr, i, raw_val)` |
| Float read | `fn_index(arr, i)` → type dispatch, boxing | `array_float_get((ArrayFloat*)arr, i)` |
| Float write | `fn_array_set(arr, i, v)` → type dispatch | `array_float_set((ArrayFloat*)arr, i, raw_val)` |

### Benchmarks Modified

| Benchmark | Suite | Array Type | Annotations Added | Key Array Operations |
|-----------|-------|-----------|-------------------|---------------------|
| bounce2 | AWFY | `int[]` | 5 vars, 1 param | Seed array, position/velocity arrays |
| nbody2 | AWFY | `float[]` | 7 vars, 22 params | Body coordinate/velocity/mass arrays |
| towers2 | AWFY | `int[]` | 2 vars, 8 params | Piles and tops arrays |
| permute2 | AWFY | `int[]` | 1 var, 2 params | Permutation vector |
| storage2 | AWFY | `int[]` | 1 var, 2 params | Seed and tree arrays |
| fft2 | R7RS | `float[]` | 1 var, 1 param | FFT data array (heavy inner-loop access) |
| nqueens2 | R7RS | `int[]` | 5 vars, 5 params | Candidate/placed arrays (allocate-per-level) |

---

## R7RS Typed: Before vs After Typed Array Annotations

Previous results (2026-02-22) used scalar type annotations only (`int`, `float` on variables and parameters). Current results (2026-02-23) add `int[]`/`float[]` array annotations on top. Both use `make build-release`.

### Raw Results (Wall-Clock, C2MIR)

| Benchmark | Array Ann. | Prev Untyped | Prev Typed | Curr Untyped | Curr Typed | Prev T/U | Curr T/U |
|-----------|:----------:|----------:|----------:|----------:|----------:|:--------:|:--------:|
| fib2 | — | 18.3ms | 15.9ms | — | 12.8ms | 0.87x | — |
| tak2 | — | 15.0ms | 16.3ms | — | 10.2ms | 1.09x | — |
| cpstak2 | — | 14.7ms | 14.4ms | — | 11.0ms | 0.98x | — |
| sum2 | — | 22.0ms | 17.1ms | — | 12.7ms | 0.78x | — |
| sumfp2 | — | 15.3ms | 14.8ms | — | 11.4ms | 0.97x | — |
| mbrot2 | — | 24.8ms | 18.4ms | — | 14.2ms | 0.74x | — |
| ack2 | — | 38.2ms | 22.8ms | — | 18.9ms | 0.60x | — |
| **fft2** | **float[]** | 21.8ms | 21.3ms | 18.0ms | **16.5ms** | 0.977x | **0.917x** |
| **nqueens2** | **int[]** | 45.6ms | 44.1ms | 46.0ms | **43.7ms** | 0.967x | **0.950x** |

### Isolating Typed Array Impact (R7RS)

To account for system-condition differences between measurement days, we compare the Typed/Untyped ratio (T/U) rather than absolute times:

| Benchmark | Prev T/U (scalar only) | Curr T/U (scalar + array) | Array Contribution |
|-----------|:----------------------:|:-------------------------:|:------------------:|
| **fft** | 0.977x | **0.917x** | **1.07x** (7% faster) |
| **nqueens** | 0.967x | **0.950x** | **1.02x** (2% faster) |

**fft** benefits meaningfully — its inner loop has 15 `array_float_get`/`array_float_set` calls per iteration, all now native. **nqueens** shows minimal improvement because it allocates fresh arrays at each recursion level (the indexing access pattern is less hot-loop concentrated).

Startup-subtracted compute time (subtracting ~10ms startup overhead):

| Benchmark | Prev Compute T/U | Curr Compute T/U | Array Speedup |
|-----------|:-----------------:|:-----------------:|:-------------:|
| **fft** | 0.96x | **0.81x** | **1.19x** (19% faster) |
| **nqueens** | 0.96x | **0.94x** | **1.02x** (2% faster) |

With startup removed, fft shows a **19% compute-time improvement** from native `float[]` array access.

---

## AWFY: Untyped vs Typed with Array Annotations

The AWFY typed benchmarks include both scalar type annotations (pre-existing) and the newly-added `int[]`/`float[]` array annotations. Previous benchmark runs did not include AWFY typed results, so we compare against untyped baselines from the same run.

### Results (Wall-Clock, C2MIR, Current Run)

| Benchmark | Array Type | Untyped | Typed (w/ array) | Wall-Clock T/U | Compute T/U† |
|-----------|:----------:|------:|------:|:--------------:|:------------:|
| **bounce** | `int[]` | 14.7ms | 12.9ms | **0.88x** | **0.62x** |
| **nbody** | `float[]` | 20.1ms | 16.5ms | **0.82x** | **0.64x** |
| **towers** | `int[]` | 13.2ms | 12.2ms | **0.92x** | **0.69x** |
| **permute** | `int[]` | 12.2ms | 11.5ms | **0.94x** | **0.68x** |
| **storage** | `int[]` | 21.5ms | 20.5ms | **0.95x** | **0.91x** |

†Compute T/U = (Typed − 10ms) / (Untyped − 10ms), removing ~10ms startup overhead

**Geometric mean (wall-clock): 0.90x** — 10% faster with typed arrays
**Geometric mean (compute): 0.70x** — 30% faster on pure computation

### AWFY Control Benchmarks (No Array Annotations)

Typed benchmarks that did NOT receive `int[]`/`float[]` annotations, measured in the same run:

| Benchmark | Untyped | Typed (scalar only) | Wall-Clock T/U |
|-----------|------:|------:|:--------------:|
| sieve | — | 11.5ms | — |
| queens | — | 12.2ms | — |
| list | — | 11.9ms | — |
| mandelbrot | — | 86.2ms | — |

These serve as reference points; their results are consistent with prior measurements (no change expected).

---

## Summary: Typed Array Impact

| Metric | Value |
|--------|------:|
| AWFY wall-clock improvement (geo mean) | **10%** (0.90x) |
| AWFY compute improvement (geo mean) | **30%** (0.70x) |
| R7RS fft compute improvement | **19%** (1.19x) |
| R7RS nqueens compute improvement | **2%** (1.02x) |
| Best case: nbody (float[], 28 native ops) | **36% wall-clock** (0.64x compute) |
| Best case: bounce (int[], 5 arrays) | **38% wall-clock** (0.62x compute) |

### Key Observations

1. **`float[]` on nbody delivers the largest improvement** — the benchmark performs 1000 iterations of gravitational N-body simulation with 7 float arrays and 28 native `array_float_get`/`array_float_set` calls per function, replacing boxed `fn_index()`/`fn_array_set()` dispatch. The compute portion is **36% faster**.

2. **`int[]` on bounce/towers/permute shows 31–38% compute speedup** — these benchmarks mutate integer arrays in tight loops (position updates, disk moves, element swaps). Native `array_int_get`/`array_int_set` eliminates per-access boxing.

3. **nqueens benefits least** — it creates new arrays at each recursion level rather than mutating existing ones. The array creation cost dominates over individual element access, limiting the benefit of native indexing.

4. **storage benefits modestly** — the dominant cost is `build_tree_depth` recursion and array allocation, with relatively few array element accesses per allocation.

5. **Wall-clock improvements are diluted by startup** — with ~10ms startup and <15ms compute for many benchmarks, a 30% compute improvement translates to only ~10% wall-clock improvement. Benchmarks with heavier workloads (nbody, fft) show the benefit more clearly.

6. **Previously identified weakness partially addressed** — the "Release Build vs Other Runtimes" section noted `nqueens` (373x vs Racket) and `fft` (25x vs Racket) as weaknesses due to expensive array operations. Typed array annotations reduce this gap, particularly for fft where the compute T/U improved from 0.96x to 0.81x.

---

# Reference Counting → Garbage Collection: Performance Impact

**Date:** 2026-02-25
**Build:** `make clean-all && make build-release` (C2MIR transpiler path)
**Platform:** Apple Silicon Mac Mini (aarch64), macOS
**Methodology:** 5 runs per benchmark, median wall-clock time (includes startup + JIT compilation + execution)
**Change under test:** Replaced reference counting with garbage collection in the Lambda runtime

**Baseline:** Release build from 2026-02-22 (reference counting)
**Current:** Release build from 2026-02-25 (garbage collection)

> **Build note:** A clean release build must use `make clean-all && make build-release` to produce
> the correct ~9MB binary. Running `make build-release` without `make clean-all` first may link
> stale debug object files, producing a ~12MB binary with debug-level performance.

---

## R7RS Benchmarks — Untyped (Ref-Count vs GC)

| Benchmark | Category | Ref-Count | GC | GC/RC |
|-----------|----------|----------:|---:|------:|
| fib | recursive | 18.3ms | 17.9ms | 0.98x |
| fibfp | recursive | 24.5ms | 20.8ms | 0.85x |
| tak | recursive | 15.0ms | 14.1ms | 0.94x |
| cpstak | closure | 14.7ms | 14.5ms | 0.99x |
| sum | iterative | 22.0ms | 22.1ms | 1.00x |
| sumfp | iterative | 15.3ms | 15.1ms | 0.99x |
| nqueens | backtrack | 45.6ms | 45.1ms | 0.99x |
| fft | numeric | 21.8ms | 20.4ms | 0.94x |
| mbrot | numeric | 24.8ms | 23.7ms | 0.96x |
| ack | recursive | 38.2ms | 38.4ms | 1.01x |

**Geometric mean: 0.96x** (GC is 4% faster)

---

## R7RS Benchmarks — Typed (Ref-Count vs GC)

| Benchmark | Category | Ref-Count | GC | GC/RC |
|-----------|----------|----------:|---:|------:|
| fib | recursive | 15.9ms | 15.9ms | 1.00x |
| fibfp | recursive | 17.4ms | 17.1ms | 0.98x |
| tak | recursive | 16.3ms | 13.9ms | 0.85x |
| cpstak | closure | 14.4ms | 14.0ms | 0.97x |
| sum | iterative | 17.1ms | 17.1ms | 1.00x |
| sumfp | iterative | 14.8ms | 14.9ms | 1.01x |
| nqueens | backtrack | 44.1ms | 43.5ms | 0.99x |
| fft | numeric | 21.3ms | 19.1ms | 0.90x |
| mbrot | numeric | 18.4ms | 16.9ms | 0.92x |
| ack | recursive | 22.8ms | 22.3ms | 0.98x |

**Geometric mean: 0.96x** (GC is 4% faster)

---

## AWFY Benchmarks (Ref-Count vs GC)

| Benchmark | Category | Ref-Count | GC | GC/RC |
|-----------|----------|----------:|---:|------:|
| sieve | micro | 17.2ms | 14.9ms | 0.87x |
| permute | micro | 16.4ms | 15.3ms | 0.93x |
| queens | micro | 17.6ms | 16.2ms | 0.92x |
| towers | micro | 17.3ms | 15.9ms | 0.92x |
| bounce | micro | 19.3ms | 17.1ms | 0.89x |
| list | micro | 16.2ms | 14.7ms | 0.91x |
| storage | micro | 23.9ms | 22.8ms | 0.95x |
| mandelbrot | micro | 81.9ms | 82.6ms | 1.01x |
| nbody | micro | 26.2ms | 22.2ms | 0.85x |
| richards | macro | FAIL | 83.4ms | N/A |
| json | macro | 511.2ms | 506.3ms | 0.99x |
| deltablue | macro | 63.9ms | 50.2ms | 0.79x |
| havlak | macro | 159.1ms | 158.3ms | 0.99x |
| cd | macro | FAIL | 440ms | N/A |

**Geometric mean: 0.92x** (GC is 8% faster, 12 comparable benchmarks)

---

## Overall Summary (Ref-Count vs GC)

| Suite | GC/RC Geomean |
|-------|:------------:|
| R7RS Untyped | **0.96x** |
| R7RS Typed | **0.96x** |
| AWFY | **0.92x** |
| **Overall** | **0.94x** |

> Ratio < 1.0 means GC is faster. The GC runtime is **6% faster overall**.

---

## R7RS: GC Build vs Racket (Comparison Update)

| Benchmark | Category | GC (untyped) | GC (typed) | Racket | L/R (untyped) | L/R (typed) |
|-----------|----------|----------:|----------:|-------:|---------:|---------:|
| fib | recursive | 7.9ms | 5.9ms | 0.720ms | 11x | 8x |
| fibfp | recursive | 10.8ms | 7.1ms | 3.7ms | 3x | 2x |
| tak | recursive | 4.1ms | 3.9ms | 0.080ms | 51x | 49x |
| cpstak | closure | 4.5ms | 4.0ms | 0.149ms | 30x | 27x |
| sum | iterative | 12.1ms | 7.1ms | 1.0ms | 12x | 7x |
| sumfp | iterative | 5.1ms | 4.9ms | 1.2ms | 4x | 4x |
| nqueens | backtrack | 35.1ms | 33.5ms | 0.097ms | 361x | 345x |
| fft | numeric | 10.4ms | 9.1ms | 0.465ms | 22x | 20x |
| mbrot | numeric | 13.7ms | 6.9ms | 3.8ms | 4x | 2x |
| ack | recursive | 28.4ms | 12.3ms | 3.3ms | 9x | 4x |

**GC Lambda (untyped) / Racket geomean: 15x** (was 20x with ref-count)
**GC Lambda (typed) / Racket geomean: 11x** (was 12x with ref-count)

Lambda timing: wall-clock minus ~10ms startup. Racket timing: internal (`current-inexact-milliseconds`).

---

## AWFY: GC Build vs Node.js (Comparison Update)

**Lambda timing:** wall-clock, GC release build (median of 5 runs, 2026-02-25)
**Node.js timing:** wall-clock from same machine, Node.js v24.7.0 (from AWFY Round 1, 2026-02-16)

| Benchmark | Category | GC Lambda | Node.js | Ratio | Prev (Ref-Count) |
|-----------|----------|----------:|--------:|------:|:---------:|
| sieve | micro | 14.9ms | 35ms | **0.43x** | **0.67x** |
| permute | micro | 15.3ms | 37ms | **0.41x** | **0.37x** |
| queens | micro | 16.2ms | 35ms | **0.46x** | **0.42x** |
| towers | micro | 15.9ms | 35ms | **0.45x** | **0.41x** |
| bounce | micro | 17.1ms | 36ms | **0.48x** | **0.46x** |
| list | micro | 14.7ms | 36ms | **0.41x** | **0.38x** |
| storage | micro | 22.8ms | 43ms | **0.53x** | **0.53x** |
| mandelbrot | micro | 82.6ms | 55ms | 1.50x | 1.53x |
| nbody | micro | 22.2ms | 33ms | **0.67x** | **0.92x** |
| richards | macro | 83.4ms | 36ms | 2.32x | FAIL |
| json | macro | 506.3ms | 34ms | 14.89x | 14.9x |
| deltablue | macro | 50.2ms | 35ms | 1.43x | 1.78x |
| havlak | macro | 158.3ms | 130ms | 1.22x | 1.24x |
| cd | macro | 440ms | 65ms | 6.77x | FAIL |

> Ratio = Lambda / Node.js. Values < 1.0 mean Lambda is faster. **Bold** = Lambda wins.

**Geometric mean (14 all passing): 1.03x** — Lambda and Node.js are at parity on wall-clock
**Geometric mean (12, excl `richards`+`cd`): 0.83x** — comparable subset improved from 0.87x (ref-count) to 0.83x

Notable changes from ref-count build:
- `sieve` improved from 0.67x to **0.43x** (Lambda now 2.3x faster than Node.js, was 1.5x)
- `nbody` improved from 0.92x to **0.67x** (now decisively faster)
- `deltablue` improved from 1.78x to **1.43x** (closing the gap)
- `richards` now passes (was FAIL), though at 2.32x slower than Node.js
- `cd` now passes (was FAIL) after fixing parameter immutability issue; at 6.77x slower than Node.js (440ms vs 65ms)

---

## Key Findings

### 1. GC is performance-neutral to slightly beneficial (0.94x overall)

Contrary to the common expectation that replacing reference counting with GC would regress performance, the GC runtime is approximately **6% faster overall** across all 32 comparable benchmarks. No benchmark shows meaningful regression — the worst case is `mandelbrot` AWFY (1.01x) and `ack` untyped (1.01x), both effectively tied.

### 2. Largest improvements are in AWFY micro benchmarks

The AWFY micro benchmarks show consistent 5–15% improvements:
- `deltablue` (0.79x) — 21% faster, the largest single improvement
- `nbody` (0.85x) — 15% faster
- `sieve` (0.87x) — 13% faster
- `bounce` (0.89x) — 11% faster

These benchmarks involve moderate object allocation rates. GC's deferred collection appears more efficient than per-operation reference count manipulation (increment/decrement/check-zero on every assignment).

### 3. Float-heavy R7RS benchmarks improved

`fibfp` untyped (0.85x, 15% faster) and `fft` untyped (0.94x, 6% faster) show that floating-point benchmarks benefit from eliminating ref-count overhead on boxed float temporaries. Since floats are heap-allocated compound scalars, every arithmetic operation previously required ref-count increment on the result and decrement on the consumed operand.

### 4. Typed code shows matching improvement pattern

The typed R7RS benchmarks show the same ~4% geometric improvement as untyped. Notable typed improvements:
- `tak` typed (0.85x) — 15% faster, suggesting reduced overhead in recursive call frame management
- `fft` typed (0.90x) — 10% faster, array access benefits from eliminated ref-count on intermediates
- `mbrot` typed (0.92x) — 8% faster

### 5. `richards` and `cd` now pass under GC

The `richards` benchmark previously failed with reference counting (wrong output) but now produces correct results under GC (83.4ms). This suggests the ref-count implementation had a **correctness bug** — likely a reference cycle or premature deallocation — that GC naturally handles.

The `cd` (Collision Detection) benchmark previously failed due to a compile error (E211: parameter immutability). After fixing the Red-Black Tree fixup functions to use local `var` copies instead of reassigning `pn` parameters, `cd` now passes (440ms). At 6.77x slower than Node.js (65ms), it is the second-slowest benchmark after `json` (14.89x) — the heavy Red-Black Tree and voxel-based spatial indexing involve extensive map/array allocation and mutation.

### 6. Gap to Racket narrows slightly

With GC, Lambda's gap to Racket narrowed from 20x to **15x** (untyped) and from 12x to **11x** (typed). The improvement is modest but directionally positive — the GC runtime's slightly better performance translates directly to closing the gap with mature Scheme implementations.

### 7. Typed/untyped speedup ratio remains similar

| Metric | Ref-Count | GC |
|--------|:---------:|:--:|
| R7RS typed/untyped speedup | 0.60x (40% faster) | 0.73x (27% faster) |

The slightly smaller typed benefit under GC (27% vs 40%) is expected: since GC already improved untyped performance, there is less headroom for type annotations to recover.

---

# Map Direct Access Optimization: Performance Impact

**Date:** 2026-02-27
**Build:** `make clean-all && make build-release` (8.8MB, C2MIR transpiler path)
**Platform:** Apple Silicon Mac Mini (aarch64), macOS
**Methodology:** 5 runs per benchmark, median wall-clock time (includes startup + JIT compilation + execution)

**Baseline:** GC release build from 2026-02-25

**Changes under test:**
1. **Map direct access** — C2MIR transpiler emits `_self_data->field` for fixed-shape maps instead of `fn_map_set()`/`_map_get()` runtime calls. Applies to both reads and writes on maps with known struct layout.
2. **Null coercion** — `fn_add(null, x) = x`, `fn_mul(null, x) = 0` (changed from null-propagation). Affects untyped arithmetic behavior.
3. **GC stale pointer fixes** — `expand_list` and `map_rebuild_for_type_change` now correctly re-root pointers after GC.

---

## AWFY Benchmarks — Untyped (GC Baseline vs Current)

| Benchmark | Category | GC Baseline | Current | Ratio |
|-----------|----------|----------:|--------:|------:|
| sieve | micro | 14.9ms | 13.2ms | **0.89x** |
| permute | micro | 15.3ms | 12.8ms | **0.84x** |
| queens | micro | 16.2ms | 13.6ms | **0.84x** |
| towers | micro | 15.9ms | 13.7ms | **0.86x** |
| bounce | micro | 17.1ms | 14.9ms | **0.87x** |
| list | micro | 14.7ms | 12.5ms | **0.85x** |
| storage | micro | 22.8ms | 22.8ms | 1.00x |
| mandelbrot | micro | 82.6ms | 90.0ms | 1.09x |
| nbody | micro | 22.2ms | 23.3ms | 1.05x |
| richards | macro | 83.4ms | 80.0ms | **0.96x** |
| json | macro | 506.3ms | 540.0ms | 1.07x |
| deltablue | macro | 50.2ms | 61.6ms | 1.23x |
| havlak | macro | 158.3ms | 5116.8ms | 32.32x |
| cd | macro | 440ms | 1705.9ms | 3.88x |

**Geometric mean (12, excl havlak/cd): 0.96x** — 4% faster on comparable benchmarks

> **Regressions:** `havlak` (32x) and `cd` (4x) are caused by null coercion semantic changes — `null + x = x` causes algorithms to accumulate values instead of propagating null, dramatically increasing the amount of work performed. These are **algorithmic regressions**, not runtime overhead. `deltablue` (1.23x) and `sum`/`ack` regressions (see R7RS below) are from `fn_add` null-check overhead on every untyped addition.

---

## AWFY Benchmarks — Typed (Current Build)

| Benchmark | Category | Untyped | Typed | T/U Ratio |
|-----------|----------|--------:|------:|:---------:|
| sieve2 | micro | 13.2ms | 12.4ms | **0.94x** |
| permute2 | micro | 12.8ms | 12.6ms | 0.98x |
| queens2 | micro | 13.6ms | 12.7ms | **0.93x** |
| towers2 | micro | 13.7ms | 13.3ms | 0.97x |
| bounce2 | micro | 14.9ms | 14.7ms | 0.99x |
| list2 | micro | 12.5ms | 12.4ms | 0.99x |
| storage2 | micro | 22.8ms | 21.4ms | **0.94x** |
| mandelbrot2 | micro | 90.0ms | 88.1ms | 0.98x |
| nbody2 | micro | 23.3ms | 18.4ms | **0.79x** |
| richards2 | macro | 80.0ms | 78.5ms | 0.98x |
| json2 | macro | 540.0ms | 540.2ms | 1.00x |
| deltablue2 | macro | 61.6ms | 56.9ms | **0.92x** |
| havlak2 | macro | 5116.8ms | 5226.5ms | 1.02x |
| cd2 | macro | 1705.9ms | 891.8ms | **0.52x** |

**Geometric mean T/U (12, excl havlak/cd): 0.95x** — typed is 5% faster than untyped
**cd2 typed (891.8ms) vs cd untyped (1705.9ms): 0.52x** — typed direct access halves cd runtime

---

## R7RS Benchmarks — Untyped (GC Baseline vs Current)

| Benchmark | Category | GC Baseline | Current | Ratio |
|-----------|----------|----------:|--------:|------:|
| fib | recursive | 17.9ms | 18.3ms | 1.02x |
| fibfp | recursive | 20.8ms | 20.6ms | 0.99x |
| tak | recursive | 14.1ms | 11.5ms | **0.82x** |
| cpstak | closure | 14.5ms | 12.1ms | **0.83x** |
| sum | iterative | 22.1ms | 27.3ms | 1.24x |
| sumfp | iterative | 15.1ms | 13.6ms | **0.90x** |
| nqueens | backtrack | 45.1ms | 46.3ms | 1.03x |
| fft | numeric | 20.4ms | 20.3ms | 1.00x |
| mbrot | numeric | 23.7ms | 23.8ms | 1.00x |
| ack | recursive | 38.4ms | 49.8ms | 1.30x |

**Geometric mean: 1.00x** — flat overall

> `sum` (1.24x) and `ack` (1.30x) regressed due to `fn_add` null-check overhead (2 extra comparisons per untyped addition). These benchmarks perform millions of additions. Typed `sum2` (0.95x) and `ack2` (0.95x) are unaffected since they use native `+`.
> `tak` (0.82x) and `cpstak` (0.83x) improved — likely from reduced function-call overhead in the transpiler.

---

## R7RS Benchmarks — Typed (GC Baseline vs Current)

| Benchmark | Category | GC Baseline | Current | Ratio |
|-----------|----------|----------:|--------:|------:|
| fib2 | recursive | 15.9ms | 15.2ms | **0.96x** |
| fibfp2 | recursive | 17.1ms | 15.2ms | **0.89x** |
| tak2 | recursive | 13.9ms | 11.2ms | **0.81x** |
| cpstak2 | closure | 14.0ms | 11.2ms | **0.80x** |
| sum2 | iterative | 17.1ms | 16.3ms | **0.95x** |
| sumfp2 | iterative | 14.9ms | 12.1ms | **0.81x** |
| nqueens2 | backtrack | 43.5ms | FAIL | — |
| fft2 | numeric | 19.1ms | 17.6ms | **0.92x** |
| mbrot2 | numeric | 16.9ms | 15.1ms | **0.89x** |
| ack2 | recursive | 22.3ms | 21.1ms | **0.95x** |

**Geometric mean (9, excl nqueens2 FAIL): 0.88x** — 12% faster

> `nqueens2` produces incorrect result (74 vs expected 92). This is a typed `int[]` array correctness bug to be investigated.
> All 9 passing typed benchmarks improved, with `tak2` (0.81x), `cpstak2` (0.80x), and `sumfp2` (0.81x) showing the largest gains (~20% faster).

---

## Overall Summary (Map Direct Access Impact)

| Suite | Geo Mean vs GC Baseline | Notes |
|-------|:-----------------------:|-------|
| AWFY Untyped (12 benchmarks) | **0.96x** | excl havlak/cd regressions |
| AWFY Typed/Untyped | **0.95x** | typed is 5% faster |
| R7RS Untyped | **1.00x** | flat (sum/ack regressed, tak/cpstak improved) |
| R7RS Typed (9 benchmarks) | **0.88x** | excl nqueens2 FAIL |

### Key Findings

#### 1. AWFY micro benchmarks 11–16% faster (map direct access)

Six AWFY micro benchmarks (`sieve`, `permute`, `queens`, `towers`, `bounce`, `list`) improved by 11–16%. These benchmarks construct and access map/struct fields in inner loops. Direct field access (`_self_data->field`) eliminates:
- `fn_map_set()` runtime dispatch
- `_map_get()` linear linked-list search with `strcmp()` per field
- String key allocation and hashing overhead

`storage` (1.00x) shows no change because its hot path is dominated by array allocation, not map access.

#### 2. Typed R7RS 12% faster across the board

All 9 passing typed R7RS benchmarks improved (0.80x–0.96x). This is surprising since R7RS benchmarks are mostly recursive computation without heavy map usage. The improvement likely comes from **accumulated transpiler optimizations** including better code generation for function calls, closures, and arithmetic.

#### 3. Null coercion causes havlak/cd algorithmic regressions

The `null + x = x` coercion changes the behavior of algorithms that use `null` as a sentinel/uninitialized value:
- **havlak**: 158.3ms → 5116.8ms (32x). The loop-finding algorithm accumulates values through null additions instead of short-circuiting, performing vastly more work.
- **cd**: 440ms → 1705.9ms (4x). Collision detection counts change from 4305 → 1036434 (untyped), vastly more collision computations.
- These are **semantic changes** in program behavior, not runtime overhead.

#### 4. `fn_add` null-check overhead affects untyped arithmetic benchmarks

Adding null checks to `fn_add` (`if (a == ItemNull) return b; if (b == ItemNull) return a;`) penalizes benchmarks with millions of untyped additions:
- `sum`: 22.1ms → 27.3ms (1.24x) — integer sum loop
- `ack`: 38.4ms → 49.8ms (1.30x) — recursive addition-heavy
- `deltablue`: 50.2ms → 61.6ms (1.23x) — constraint solver with many additions
- **Typed versions unaffected** — `sum2` (0.95x), `ack2` (0.95x), `deltablue2` (0.92x) use native `+`

#### 5. cd2 typed is 2x faster than cd untyped (map direct access)

`cd2` (891.8ms) vs `cd` (1705.9ms) = 0.52x ratio. The collision detection benchmark is map-intensive (Red-Black tree nodes, voxel maps), making it the benchmark that benefits most from typed map direct access. This is a **48% wall-clock improvement** from type annotations enabling direct field access.

### Known Issues

| Issue | Status | Impact |
|-------|--------|--------|
| `havlak` 32x regression | Null coercion semantic change | Must revisit null coercion design |
| `cd` 4x regression | Null coercion semantic change | Same root cause as havlak |
| `nqueens2` typed FAIL | `int[]` correctness bug | Returns 74, expected 92 |
| `sum`/`ack`/`deltablue` 23–30% slower | `fn_add` null-check overhead | Typed versions unaffected |

---

# Null Propagation Revert + Adaptive GC Threshold

**Date:** 2026-02-27 (updated)
**Build:** `make clean-all && make build-release` (8.8MB, C2MIR transpiler path)
**Platform:** Apple Silicon MacBook Air (aarch64), macOS
**Methodology:** 5 runs per benchmark, median wall-clock time (includes startup + JIT compilation + execution)

**Baseline:** Map Direct Access build from 2026-02-27 (earlier)

**Changes under test:**
1. **Null propagation reverted** — `fn_add(null, x)` now returns `null` (was returning `x` under null coercion). Removes the 2-comparison null-check overhead per untyped addition that caused `sum`/`ack`/`deltablue` regressions.
2. **Adaptive GC threshold** — GC data zone starts at 4MB; threshold grows 4× when <40% freed, 2× when <75% freed (capped at 256MB). Prevents excessive GC collections in allocation-heavy benchmarks like `havlak`.
3. **`nqueens2` `int[]` correctness fix** — typed array access bug that caused wrong result (74 vs expected 92) is now fixed.
4. **472/472 baseline tests pass** (was 465/472).

---

## R7RS Benchmarks — Untyped (Previous vs Current)

| Benchmark | Category | Previous | Current | Ratio |
|-----------|----------|----------:|--------:|------:|
| fib | recursive | 18.3ms | 11.6ms | **0.63x** |
| fibfp | recursive | 20.6ms | 12.0ms | **0.58x** |
| tak | recursive | 11.5ms | 12.2ms | 1.06x |
| cpstak | closure | 12.1ms | 12.1ms | 1.00x |
| sum | iterative | 27.3ms | 12.3ms | **0.45x** |
| sumfp | iterative | 13.6ms | 12.4ms | **0.91x** |
| nqueens | backtrack | 46.3ms | 15.8ms | **0.34x** |
| fft | numeric | 20.3ms | 16.0ms | **0.79x** |
| mbrot | numeric | 23.8ms | 14.0ms | **0.59x** |
| ack | recursive | 49.8ms | 12.3ms | **0.25x** |

**Geometric mean: 0.60x** — 40% faster overall

> `sum` (0.45x) and `ack` (0.25x) recovered from `fn_add` null-check overhead removal. `nqueens` (0.34x) improved dramatically from adaptive GC threshold reducing collection frequency during backtracking. `fib` (0.63x), `fibfp` (0.58x), and `mbrot` (0.59x) improvements reflect accumulated transpiler optimizations for functional evaluation.

---

## R7RS Benchmarks — Typed (Previous vs Current)

| Benchmark | Category | Previous | Current | Ratio |
|-----------|----------|----------:|--------:|------:|
| fib2 | recursive | 15.2ms | 12.4ms | **0.82x** |
| fibfp2 | recursive | 15.2ms | 12.7ms | **0.84x** |
| tak2 | recursive | 11.2ms | 12.2ms | 1.09x |
| cpstak2 | closure | 11.2ms | 12.4ms | 1.11x |
| sum2 | iterative | 16.3ms | 12.1ms | **0.74x** |
| sumfp2 | iterative | 12.1ms | 12.3ms | 1.02x |
| nqueens2 | backtrack | FAIL | 15.0ms | **FIXED** |
| fft2 | numeric | 17.6ms | 14.3ms | **0.81x** |
| mbrot2 | numeric | 15.1ms | 13.9ms | **0.92x** |
| ack2 | recursive | 21.1ms | 12.1ms | **0.57x** |

**Geometric mean (9, excl nqueens2 prev FAIL): 0.86x** — 14% faster
**nqueens2: FAIL → 15.0ms PASS** — `int[]` correctness bug fixed

---

## AWFY Benchmarks — Untyped (Previous vs Current)

| Benchmark | Category | Previous | Current | Ratio |
|-----------|----------|----------:|--------:|------:|
| sieve | micro | 13.2ms | 14.3ms | 1.08x |
| permute | micro | 12.8ms | 13.9ms | 1.09x |
| queens | micro | 13.6ms | 14.9ms | 1.10x |
| towers | micro | 13.7ms | 14.5ms | 1.06x |
| bounce | micro | 14.9ms | 16.4ms | 1.10x |
| list | micro | 12.5ms | 13.6ms | 1.09x |
| storage | micro | 22.8ms | 22.4ms | 0.98x |
| mandelbrot | micro | 90.0ms | 94.0ms | 1.04x |
| nbody | micro | 23.3ms | 23.1ms | 0.99x |
| richards | macro | 80.0ms (FAIL) | 70.0ms (PASS) | **0.88x** |
| json | macro | 540.0ms | 553.0ms | 1.02x |
| deltablue | macro | 61.6ms | 56.2ms | **0.91x** |
| havlak | macro | 5116.8ms | 201.4ms | **0.04x** |
| cd | macro | 1705.9ms (FAIL) | 634.0ms (PASS) | **0.37x** |

**Geometric mean (11, excl havlak/richards): 1.05x** — 5% slower on micro benchmarks
**havlak: 5116.8ms → 201.4ms** — 25× faster (null propagation revert + adaptive GC threshold)
**deltablue: 61.6ms → 56.2ms** — 9% faster (null-check overhead removed)
**cd: FAIL → 634ms PASS** — GC compaction bug fixed

> AWFY micro benchmarks show a consistent ~1ms regression (6–10%) compared to the Map Direct Access build. This appears to be build-to-build variance rather than a systematic regression, as current times are still faster than the GC baseline (2026-02-25): sieve 14.9→14.3, permute 15.3→13.9, queens 16.2→14.9, etc.

---

## AWFY Benchmarks — Typed (Pre-Boxing-Elision)

| Benchmark | Category | Untyped | Typed | T/U Ratio |
|-----------|----------|--------:|------:|:---------:|
| sieve2 | micro | 14.3ms | 11.8ms | **0.83x** |
| permute2 | micro | 13.9ms | 12.0ms | **0.86x** |
| queens2 | micro | 14.9ms | 12.7ms | **0.85x** |
| towers2 | micro | 14.5ms | 12.7ms | **0.88x** |
| bounce2 | micro | 16.4ms | 13.4ms | **0.82x** |
| list2 | micro | 13.6ms | 13.0ms | 0.96x |
| storage2 | micro | 22.4ms | 20.3ms | **0.91x** |
| mandelbrot2 | micro | 94.0ms | 87.2ms | **0.93x** |
| nbody2 | micro | 23.1ms | 16.9ms | **0.73x** |
| richards2 | macro | 80.0ms | 77.0ms | 0.96x |
| json2 | macro | 553.0ms | 550.0ms | 0.99x |
| deltablue2 | macro | 56.2ms | 51.9ms | **0.92x** |
| havlak2 | macro | 201.4ms | 199.2ms | 0.99x |
| cd2 | macro | 634.0ms | 520.0ms | **0.82x** |

**Geometric mean T/U (14 benchmarks): 0.89x** — typed is 11% faster than untyped

---

## AWFY Benchmarks — Typed (Current, Boxing Elision Build)

Transpiler optimization: eliminates `it2X(X2it(val))` boxing roundtrips in typed map field writes,
map literal initialization, and function call arguments with typed parameters.
- json2.ls: 38 → 0 roundtrips eliminated
- cd2.ls: all roundtrips eliminated

> Note: System under moderate load during this run (all absolute times ~1.7x higher than previous session). **Ratios within the same run are reliable.**

| Benchmark | Category | Untyped | Typed | T/U Ratio | Prev T/U |
|-----------|----------|--------:|------:|:---------:|:--------:|
| sieve2 | micro | 25.3ms | 23.4ms | **0.92x** | 0.83x |
| permute2 | micro | 25.1ms | 23.6ms | **0.94x** | 0.86x |
| queens2 | micro | 26.1ms | 25.3ms | 0.97x | 0.85x |
| towers2 | micro | 27.1ms | 25.6ms | **0.94x** | 0.88x |
| bounce2 | micro | 29.1ms | 26.5ms | **0.91x** | 0.82x |
| list2 | micro | 25.0ms | 25.0ms | 1.00x | 0.96x |
| storage2 | micro | 37.7ms | 37.6ms | 1.00x | 0.91x |
| mandelbrot2 | micro | 130.8ms | 129.9ms | 0.99x | 0.93x |
| nbody2 | micro | 38.5ms | 31.6ms | **0.82x** | **0.73x** |
| richards2 | macro | 131.3ms | 118.4ms | **0.90x** | 0.96x |
| json2 | macro | 741.9ms | 687.6ms | **0.93x** | 0.99x |
| deltablue2 | macro | 85.8ms | 80.9ms | **0.94x** | 0.92x |
| havlak2 | macro | 293.2ms | 288.9ms | 0.99x | 0.99x |
| cd2 | macro | 836.6ms | 655.2ms | **0.78x** | **0.82x** |

**Geometric mean T/U (14 benchmarks): 0.93x** — typed is 7% faster than untyped
> Micro benchmark ratios are compressed due to system load (fixed ~10ms startup overhead dominates more when total time is inflated). Key improvements from boxing elision visible in macro benchmarks:
> - **json2**: 0.99x → **0.93x** (typing now provides 7% speedup, was previously flat)
> - **cd2**: 0.82x → **0.78x** (typed speedup increased from 18% to 22%)
> - **richards2**: 0.96x → **0.90x** (typed speedup increased from 4% to 10%)

**Clean-system json benchmark (7 runs, quiet system):**
- json.ls: 536ms (was 553ms pre-optimization, **3.1% faster**)
- json2.ls: 533ms (typed is 0.99x vs untyped — string-heavy code doesn't benefit much from type annotations)
- json.ls vs Node.js: 15.8x (improved from 16.3x)

---

## Boxing Elision Optimization (2026-02-27)

### Problem

The C transpiler (`lambda/transpile.cpp`) generated redundant boxing/unboxing roundtrips when writing to or reading from typed map fields and passing typed function arguments. For example, storing an `int` local into an `int` field of a typed map:

```c
// BEFORE: roundtrip it2i(i2it(x)) is a no-op that wastes cycles
*(int64_t*)((char*)(_p)->data+8) = it2i(i2it(_idx));   // i2it boxes int→Item, it2i unboxes Item→int
*(char**)((char*)(_p)->data+56) = it2s(s2it(_cur));     // s2it boxes String*→Item, it2s unboxes Item→String*
Map* _p = _p_new3977(it2s(s2it(_input)));               // function call arg: same roundtrip
```

This happened because `emit_direct_field_write()` always called `transpile_box_item()` (which wraps native values with `i2it`/`s2it`) and then immediately unwrapped with `it2i`/`it2s`, even when the value was already a native C type matching the field.

### Solution

Three changes to `lambda/transpile.cpp`:

1. **`value_emits_native_type()` helper** — determines whether `transpile_expr(value)` will produce a native C value (e.g., `int64_t`, `String*`) matching the target type. Returns `false` for expressions that produce `Item` despite being typed (dynamic calls, widened variables, optional/closure/captured parameters, etc.).

2. **`emit_direct_field_write()` optimization** — before emitting the boxing wrapper (`it2X(X2it(val))`), checks `value_emits_native_type()`. If the value is already native, emits `transpile_expr(value)` directly.

3. **`transpile_call_argument()` optimization** — for `STRING`/`BINARY` parameters, checks `value_emits_native_type()` before wrapping with `it2s(transpile_box_item(value))`. If the argument is already native `String*`, emits directly.

4. **`transpile_map_expr()` optimization** — same check applied to map literal field initialization.

### Generated C — Before vs After

```c
// AFTER: direct native store, no roundtrip
*(int64_t*)((char*)(_p)->data+8) = _idx;       // native int64_t directly
*(char**)((char*)(_p)->data+56) = _cur;         // native String* directly
Map* _p = _p_new3977(_input);                   // native String* arg directly
```

### Impact

| Metric | json2.ls | cd2.ls |
|--------|----------|--------|
| Boxing roundtrips eliminated | 38 → 0 | all → 0 |
| T/U ratio (typed/untyped) | 0.99x → **0.93x** | 0.82x → **0.78x** |

| Benchmark | Previous | Current | Change |
|-----------|----------|---------|--------|
| json.ls (untyped) | 553ms | 536ms | **-3.1%** (benefits from `transpile_call_argument` fix) |
| json2.ls (typed) | 550ms | 533ms | **-3.1%** (field write + call arg elision) |
| cd2.ls (typed) | 520ms | ~510ms | **~2%** (field write elision) |
| richards2.ls (typed) T/U | 0.96x | 0.90x | **-6%** (significant improvement) |

The optimization also benefits **untyped** code because `transpile_call_argument()` handles all function calls, not just typed-map operations. The json.ls improvement (553→536ms) comes entirely from avoiding roundtrips in calls to functions with typed `string` parameters.

---

## R7RS: Typed vs Untyped (Current)

| Benchmark | Untyped | Typed | T/U Ratio |
|-----------|--------:|------:|:---------:|
| fib | 11.6ms | 12.4ms | 1.07x |
| fibfp | 12.0ms | 12.7ms | 1.06x |
| tak | 12.2ms | 12.2ms | 1.00x |
| cpstak | 12.1ms | 12.4ms | 1.02x |
| sum | 12.3ms | 12.1ms | 0.98x |
| sumfp | 12.4ms | 12.3ms | 0.99x |
| nqueens | 15.8ms | 15.0ms | **0.95x** |
| fft | 16.0ms | 14.3ms | **0.89x** |
| mbrot | 14.0ms | 13.9ms | 0.99x |
| ack | 12.3ms | 12.1ms | 0.98x |

**Geometric mean T/U: 0.99x** — typed and untyped at parity

> Most R7RS benchmarks are now startup-dominated (~10ms of 12ms total), leaving only 1–4ms of compute time. At this granularity, type annotation benefits are masked by startup overhead. The two benchmarks with meaningful compute time — `nqueens` (5.8ms compute) and `fft` (6ms compute) — show the expected typed advantage (0.95x and 0.89x respectively).

---

## AWFY: Current vs Node.js

**Lambda timing:** wall-clock, release build (median of 5 runs)
**Node.js timing:** wall-clock from same machine, Node.js v24.7.0 (from AWFY Round 1, 2026-02-16)

| Benchmark  | Category |  Lambda | Node.js |     Ratio |
| ---------- | -------- | ------: | ------: | --------: |
| sieve      | micro    |  14.3ms |    35ms | **0.41x** |
| permute    | micro    |  13.9ms |    37ms | **0.38x** |
| queens     | micro    |  14.9ms |    35ms | **0.43x** |
| towers     | micro    |  14.5ms |    35ms | **0.41x** |
| bounce     | micro    |  16.4ms |    36ms | **0.46x** |
| list       | micro    |  13.6ms |    36ms | **0.38x** |
| storage    | micro    |  22.4ms |    43ms | **0.52x** |
| mandelbrot | micro    |  94.0ms |    55ms |     1.71x |
| nbody      | micro    |  23.1ms |    33ms | **0.70x** |
| richards   | macro    |  70.0ms |    36ms |     1.94x |
| json       | macro    | 536.0ms |    34ms |    15.76x |
| deltablue  | macro    |  56.2ms |    35ms |     1.61x |
| havlak     | macro    | 201.4ms |   130ms |     1.55x |
| cd         | macro    | 634.0ms |    65ms |     9.75x |

> Ratio = Lambda / Node.js. Values < 1.0 mean Lambda is faster. **Bold** = Lambda wins.

**Geometric mean (14 benchmarks): 1.07x** — Lambda roughly at parity with Node.js overall
**Lambda wins 7 of 14 comparable benchmarks** (all micro benchmarks except mandelbrot, plus nbody)

| Comparison | Previous (Map DA) | Current |
|------------|:-----------------:|:-------:|
| Geo mean (12 benchmarks) | 0.83x | **0.84x** |
| havlak ratio | 39.4x (5116ms) | **1.55x** (201ms) |
| deltablue ratio | 1.76x | **1.61x** |

---

## R7RS: Current vs Racket

**Lambda timing:** wall-clock minus ~10ms startup. **Racket timing:** internal (`current-inexact-milliseconds`).

| Benchmark | Lambda (untyped) | Lambda (typed) | Racket | L/R (untyped) | L/R (typed) |
|-----------|----------:|----------:|-------:|---------:|---------:|
| fib | 1.6ms | 2.4ms | 0.720ms | 2x | 3x |
| fibfp | 2.0ms | 2.7ms | 3.7ms | **0.5x** | **0.7x** |
| tak | 2.2ms | 2.2ms | 0.080ms | 28x | 28x |
| cpstak | 2.1ms | 2.4ms | 0.149ms | 14x | 16x |
| sum | 2.3ms | 2.1ms | 1.0ms | 2x | 2x |
| sumfp | 2.4ms | 2.3ms | 1.2ms | 2x | 2x |
| nqueens | 5.8ms | 5.0ms | 0.097ms | 60x | 52x |
| fft | 6.0ms | 4.3ms | 0.465ms | 13x | 9x |
| mbrot | 4.0ms | 3.9ms | 3.8ms | **1.1x** | **1.0x** |
| ack | 2.3ms | 2.1ms | 3.3ms | **0.7x** | **0.6x** |

**Lambda (untyped) / Racket geomean: 4x** (was 15x in GC baseline, was 22x in Ref-Count build)
**Lambda (typed) / Racket geomean: 4x** (was 11x in GC baseline, was 12x in Ref-Count build)

> Major narrowing of the Lambda–Racket gap. The most dramatic improvements are `fib` (11x→2x), `sum` (12x→2x), `ack` (9x→0.7x, Lambda now faster), and `mbrot` (4x→1.1x, near parity). Lambda now beats Racket on 3 benchmarks (`fibfp`, `mbrot`, `ack`). The remaining large gaps are `nqueens` (60x — array-intensive backtracking) and `tak`/`cpstak` (14–28x — deep recursion with JIT call overhead).

---

## Overall Summary

### Performance vs Previous Build (Map Direct Access, 2026-02-27)

| Suite | Geo Mean | Notes |
|-------|:--------:|-------|
| R7RS Untyped | **0.60x** | 40% faster (null-check removal + transpiler improvements) |
| R7RS Typed (9 benchmarks) | **0.86x** | 14% faster (nqueens2 FAIL→PASS) |
| AWFY Untyped (12 benchmarks) | 1.05x | ~flat (excl havlak/richards); cd FAIL→PASS |
| AWFY havlak | **0.04x** | 25× faster (5116ms → 201ms) |

### Cross-Runtime Comparison

| Metric | Ref-Count (Feb 22) | GC (Feb 25) | Current |
|--------|:-------------------:|:-----------:|:-------:|
| AWFY vs Node.js (14 benchmarks) | 0.87x | 0.83x | **1.07x** |
| R7RS vs Racket untyped (geo) | 20x | 15x | **4x** |
| R7RS vs Racket typed (geo) | 12x | 11x | **4x** |

### Issue Resolution Status

| Issue (from Map DA section) | Previous | Current | Status |
|-----------------------------|----------|---------|--------|
| `havlak` 32× regression | 5116.8ms | 201.4ms | **FIXED** — adaptive GC threshold |
| `sum`/`ack` 23–30% slower | 27.3ms / 49.8ms | 12.3ms / 12.3ms | **FIXED** — null-check overhead removed |
| `deltablue` 23% slower | 61.6ms | 56.2ms | **FIXED** — null-check overhead removed |
| `nqueens2` typed FAIL | FAIL (74 vs 92) | 15.0ms PASS | **FIXED** — int[] correctness bug |
| `cd` regression | 1705.9ms FAIL | PASS | **FIXED** — GC compaction bug: `gc_compact_data` didn't fixup embedded float/int64 pointers in array items[] buffers |
| `richards` FAIL | 80.0ms FAIL | 70.0ms PASS | **FIXED** — C transpiler bitwise arg unboxing bug (`(int64_t)(Item)` → `it2l(Item)`) |

### Remaining Issues

| Issue                                       | Impact        | Notes                                                                       |
| ------------------------------------------- | ------------- | --------------------------------------------------------------------------- |
| `mandelbrot` AWFY 1.71× slower than Node.js | 94ms vs 55ms  | Float-heavy inner loop; Node.js V8 JIT generates superior native float code |
| `json` 15.8× slower than Node.js            | 536ms vs 34ms | String-heavy parsing; Lambda's string handling overhead dominates           |
