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
