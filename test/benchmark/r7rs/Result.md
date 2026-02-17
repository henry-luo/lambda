# R7RS Benchmark Results — Lambda Script (Untyped & Typed) vs Racket vs Guile

**Date**: 2025-07-16
**Platform**: macOS, Apple Silicon (Mac Mini M-series)
**Runs per benchmark**: 5 (median reported)

## Runtime Versions

| Runtime | Version | JIT/Compilation |
|---------|---------|-----------------|
| Lambda Script | v1.0 | C2MIR JIT (optimization level 2) |
| Racket | v9.0 [cs] | Chez Scheme backend JIT |
| Guile | 3.0.10 | Bytecode compiler |

## Timing Methodology

- **Lambda**: External wall-clock minus ~10ms startup overhead (Lambda has no internal timer API)
- **Racket**: Internal timing via `current-inexact-milliseconds`
- **Guile**: Internal timing via `get-internal-real-time`
- All three use identical benchmark parameters and algorithms
- Median of 5 runs to reduce variance

## Benchmark Descriptions

| Benchmark | Category | Workload | Expected Result |
|-----------|----------|----------|-----------------|
| fib | Recursive | `fib(27)` — naive recursive Fibonacci | 196418 |
| fibfp | Recursive | `fibfp(27.0)` — Fibonacci with float arithmetic | 196418.0 |
| tak | Recursive | `tak(18, 12, 6)` — Takeuchi function | 7 |
| cpstak | Closure | CPS Takeuchi¹ `tak(18, 12, 6)` | 7 |
| sum | Iterative | Integer sum 0..10000, repeated 100× | 50005000 |
| sumfp | Iterative | Float sum 0.0..100000.0 | 5000050000.0 |
| nqueens | Backtracking | N-Queens solutions for n=8 | 92 |
| fft | Numeric | FFT on 4096-element zero vector | 0.0 |
| mbrot | Numeric | Mandelbrot 75×75 grid, max 64 iterations | 5 |
| ack | Recursive | Ackermann function `ack(3, 8)` | 2045 |

¹ Lambda does not support closures in JIT-compiled procedures, so `cpstak` runs direct `tak` twice instead of true CPS. Racket and Guile use the true CPS version with continuation-passing lambdas.

## Results

### Computation Time (Pure Benchmark Execution)

| Benchmark | Category | Lambda (untyped) | Lambda (typed) | Racket | Guile | T/U | L/R | L/G |
|-----------|----------|-------:|-------:|------:|------:|----:|----:|----:|
| fib | recursive | 2,347 ms | 1,146 ms | 0.72 ms | 3.1 ms | 0.49× | 3,282× | 746× |
| fibfp | recursive | 5,279 ms | 2,324 ms | 3.4 ms | 15.3 ms | 0.44× | 1,531× | 345× |
| tak | recursive | 122.5 ms | 9.3 ms | 0.06 ms | 0.29 ms | 0.08× | 1,915× | 425× |
| cpstak | closure | 229.6 ms | 10.0 ms | 0.15 ms | 1.5 ms | 0.04× | 1,539× | 157× |
| sum | iterative | 5,740 ms | 9.2 ms | 1.1 ms | 1.1 ms | 0.002× | 5,451× | 5,354× |
| sumfp | iterative | 1,110 ms | 9.1 ms | 1.1 ms | 2.6 ms | 0.008× | 967× | 429× |
| nqueens | backtrack | 1,547 ms | 1,130 ms | 0.10 ms | 0.34 ms | 0.73× | 15,767× | 4,578× |
| fft | numeric | 1,254 ms | 1,462 ms | 0.48 ms | 1.7 ms | 1.17× | 2,642× | 741× |
| mbrot | numeric | 7,675 ms | 152.8 ms | 3.7 ms | 13.7 ms | 0.02× | 2,055× | 561× |
| ack | recursive | 13,751 ms | 24.3 ms | 3.1 ms | 12.9 ms | 0.002× | 4,370× | 1,062× |

**T/U** = Typed / Untyped (lower is better; <1× means typed is faster)
**L/R** = Lambda (untyped) / Racket &nbsp; **L/G** = Lambda (untyped) / Guile

### Geometric Mean Ratios

| Comparison | Ratio | Interpretation |
|------------|------:|----------------|
| Lambda (typed) / Lambda (untyped) | **0.06×** | **Type annotations make Lambda 94% faster** |
| Lambda (typed) / Racket | **155×** | Typed Lambda is ~155× slower than Racket |
| Lambda (untyped) / Racket | **2,796×** | Untyped Lambda is ~2,800× slower than Racket |
| Lambda (untyped) / Guile | **777×** | Untyped Lambda is ~780× slower than Guile |

## Analysis

### Impact of Type Annotations

Adding type annotations (`: int`, `: float`) to function parameters and local variables produces a **94% geometric mean speedup** in Lambda's JIT-compiled code. The impact varies dramatically by benchmark:

| Tier | Benchmarks | T/U Ratio | Speedup | Why |
|------|-----------|:---------:|--------:|-----|
| **Massive** | sum, ack, sumfp, mbrot | 0.002–0.02× | 50–600× | Loop variables and arithmetic fully unboxed; `Item` tag overhead eliminated |
| **Large** | tak, cpstak | 0.04–0.08× | 12–25× | Recursive call parameters unboxed to native `int64_t` |
| **Moderate** | fibfp, fib | 0.44–0.49× | 2.0–2.3× | Recursive but with function call overhead still dominant |
| **Small** | nqueens | 0.73× | 1.4× | Array operations remain boxed; only scalar args unboxed |
| **Regression** | fft | 1.17× | 0.85× | Array element access remains `Item`-typed; float unboxing limited |

**Key insight**: When the JIT can eliminate `Item` boxing/unboxing in tight loops (sum, ack, sumfp), Lambda approaches competitive speed. The remaining 155× gap vs Racket is primarily from function call overhead and missing tail-call optimization.

### Performance Gap Breakdown

Lambda's performance gap vs mature Scheme implementations is dramatically larger than vs Node.js (where the AWFY suite showed ~47× gap). Key factors:

1. **Racket's Chez Scheme Backend**: Racket v9.0 uses Chez Scheme's world-class native code compiler, producing highly optimized machine code for recursive patterns. Functions like `fib`, `tak`, and `ack` benefit from tail-call optimization, register allocation, and inlining.

2. **Guile's Bytecode Compiler**: Guile 3.0's bytecode VM is well-optimized for Scheme idioms. Even without native JIT, its tight dispatch loop handles recursive calls 100–5000× faster than Lambda's untyped MIR-generated code.

3. **Lambda's MIR JIT Limitations**: The C2MIR compilation pipeline (Lambda → C → MIR → machine code) introduces overhead at every function call. Without type annotations, each call goes through `Item`-typed tagged value boxing/unboxing, which is especially costly in tight recursive loops.

4. **Integer loop performance** (`sum`, `nqueens`): These show the worst untyped ratios (5,000–16,000×) because Racket/Guile optimize integer loops to near-native speed. With type annotations, Lambda's `sum` drops from 5,740ms to 9.2ms — a 624× improvement — showing that the MIR backend *can* generate efficient code when types are known.

### Typed Lambda vs Racket (155×)

Even with type annotations, Lambda remains ~155× slower than Racket. The remaining gap comes from:

1. **Function call overhead**: Each recursive call still involves MIR function dispatch rather than a direct jump
2. **No tail-call optimization**: `fib`, `tak`, and `ack` cannot convert tail calls to jumps
3. **No inlining**: Small recursive functions are never inlined at the MIR level
4. **Array access overhead**: `nqueens` and `fft` still box/unbox through `Item` for array element access

### Parameter Scaling

Original R7RS benchmark parameters (e.g., `fib(40)`, `tak(40,20,11)`, `sum(10000) × 200000`) were designed for mature Scheme implementations running in 1-10 seconds. Lambda requires drastically scaled-down parameters:

| Benchmark | R7RS Original | Lambda Parameter | Scale Factor |
|-----------|---------------|------------------|:------------:|
| fib | fib(40) | fib(27) | ~8000× less work |
| tak | tak(40,20,11) | tak(18,12,6) | ~100× less work |
| sum | 200,000 iterations | 100 iterations | 2000× fewer |
| ack | ack(3,12) | ack(3,8) | ~250× less work |
| nqueens | nqueens(13) | nqueens(8) | ~1000× fewer solutions |

### Lambda-Specific Workarounds

Several benchmarks required workarounds for Lambda JIT limitations:

- **cpstak**: True CPS with closures (`fn(a) => expr`) is not supported in Lambda's JIT transpiler. Replaced with double invocation of direct `tak`.

### Comparison with AWFY Results

| Suite | Untyped vs Competitor | Typed vs Competitor | Type Speedup |
|-------|----------------------:|--------------------:|-------------:|
| AWFY (vs Node.js) | ~47× slower | ~4× slower | ~12× |
| R7RS (vs Racket) | ~2,800× slower | ~155× slower | ~18× |
| R7RS (vs Guile) | ~780× slower | — | — |

The R7RS benchmarks stress exactly where Lambda is weakest: tight recursive loops with arithmetic. The AWFY benchmarks include more diverse workloads (hash maps, object allocation, list operations) where Lambda's overhead is relatively smaller. In both suites, type annotations dramatically narrow the gap.

## Recommendations for Lambda Performance Improvement

Based on typed vs untyped results, the highest-impact optimization targets are:

1. **Function call optimization**: Reduce per-call overhead for known-target recursive calls (biggest remaining gap for `fib`, `tak`, `ack`)
2. **Tail-call optimization**: Convert tail-recursive calls to jumps (critical for all recursive benchmarks)
3. **Type inference for untyped code**: Automatically infer types where possible, bringing untyped performance closer to typed
4. **Array element type specialization**: Allow typed arrays (`Array<int>`, `Array<float>`) to eliminate boxing on element access (would help `nqueens`, `fft`)
5. **Add `clock()` system function**: Enable internal timing for accurate Lambda benchmarking

## Files

```
test/benchmark/r7rs/
├── run_bench.py       # Benchmark runner script (4-phase: untyped, typed, Racket, Guile)
├── bench_results.csv  # Raw timing data
├── Result.md          # This report
├── fib.ls / fib2.ls / fib.rkt / fib.scm
├── fibfp.ls / fibfp2.ls / fibfp.rkt / fibfp.scm
├── tak.ls / tak2.ls / tak.rkt / tak.scm
├── cpstak.ls / cpstak2.ls / cpstak.rkt / cpstak.scm
├── sum.ls / sum2.ls / sum.rkt / sum.scm
├── sumfp.ls / sumfp2.ls / sumfp.rkt / sumfp.scm
├── nqueens.ls / nqueens2.ls / nqueens.rkt / nqueens.scm
├── fft.ls / fft2.ls / fft.rkt / fft.scm
├── mbrot.ls / mbrot2.ls / mbrot.rkt / mbrot.scm
└── ack.ls / ack2.ls / ack.rkt / ack.scm
```

`*2.ls` files are typed variants with `: int` / `: float` annotations on parameters and variables.

## Reproducing

```bash
# Run all benchmarks (5 runs each, ~20 minutes total)
python3 test/benchmark/r7rs/run_bench.py 5

# Run with fewer iterations for quick check
python3 test/benchmark/r7rs/run_bench.py 3

# Run individual Lambda benchmark (untyped vs typed)
./lambda.exe run test/benchmark/r7rs/fib.ls     # untyped
./lambda.exe run test/benchmark/r7rs/fib2.ls    # typed
```
