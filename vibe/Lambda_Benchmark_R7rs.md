# Lambda Benchmark: R7RS Scheme Benchmark Suite

## Overview

Port a selection of benchmarks from the [R7RS Benchmark Suite](https://ecraven.github.io/r7rs-benchmarks/) to Lambda Script. The R7RS suite is a well-established collection of Scheme benchmarks used to compare dozens of Scheme implementations. By porting these to Lambda, we gain:

1. **Cross-paradigm comparison** — Lambda (imperative/functional hybrid with JIT) vs Racket (JIT-compiled Scheme) vs Guile (bytecode-compiled Scheme)
2. **Coverage of different workloads** — recursion, floating-point, list processing, array mutation, symbolic computation
3. **Complementary to AWFY** — the AWFY suite tests OOP-heavy patterns; R7RS tests classic functional/recursive patterns that are closer to Lambda's sweet spot

## Benchmark Selection & Feasibility Analysis

### Selected Benchmarks (10 benchmarks)

| # | Benchmark | Category | Description | Lambda Feasibility |
|---|-----------|----------|-------------|--------------------|
| 1 | **fib** | Recursion | Naive recursive Fibonacci fib(40) | ✅ Trivial — basic recursion + arithmetic |
| 2 | **fibfp** | Float recursion | Fibonacci with floating-point | ✅ Trivial — same as fib but with floats |
| 3 | **tak** | Recursion | Takeuchi function tak(40,20,11) | ✅ Trivial — triple recursion + comparison |
| 4 | **cpstak** | Closures | Continuation-passing TAK | ✅ Good — tests Lambda closures (`fn`/`pn`) |
| 5 | **sum** | Loop/Tail-call | Sum integers 0..10000 (×200000) | ✅ Trivial — while loop |
| 6 | **sumfp** | Float loop | Sum floats 0..1e6 (×500) | ✅ Trivial — while loop with floats |
| 7 | **nqueens** | Backtracking | Count all solutions to N-Queens (N=13) | ✅ Good — recursion + list manipulation |
| 8 | **fft** | Numeric/Array | Fast Fourier Transform on vector | ✅ Good — array mutation + float math + `sin` |
| 9 | **mbrot** | Float compute | Mandelbrot set generation (75×75) | ✅ Trivial — nested loops + float ops |
| 10 | **ack** | Deep recursion | Ackermann function ack(3,12) | ✅ Trivial — deep recursion stress test |

### Rejected Benchmarks (with reasons)

| Benchmark | Reason for Rejection |
|-----------|---------------------|
| **takl** | Uses cons/car/cdr linked lists as counters — Lambda doesn't have native cons cells; would test array overhead instead of the intended algorithm |
| **primes** | Returns a list structure and uses `remainder` on list filtering — testing Scheme-specific list idioms, not general computation |
| **deriv** | Symbolic differentiation using nested cons/pair trees and `eq?` on symbols — requires deep Scheme-specific list pattern matching |
| **diviter/divrec** | Divides by 2 using linked lists of `()` — tests cons cell allocation, meaningless without native cons cells |
| **triangl** | Board game using `vector-set!` + backtracking — feasible but complex setup with global mutable vectors and `do` loops; deferred to future round |
| **quicksort** | Feasible but complex setup with custom RNG + `vector-map` — deferred to future round |
| **pnpoly** | Point-in-polygon with vectors — feasible but small; deferred |
| **mazefun** | Pure functional maze using nested list operations — heavy Scheme-list idiom |
| **compiler/sboyer/nboyer** | Scheme-specific: self-hosting compiler, Boyer theorem prover with association lists |
| **read0/read1/cat/sum1** | File I/O benchmarks — not meaningful for compute performance comparison |
| **gcbench** | GC stress test — Lambda uses refcounting, not tracing GC; results would be misleading |
| **nucleic** | 3,000+ lines of numeric code — too large to port in this round |

### Language Feature Gap Analysis

| Feature Needed | Lambda Support | Notes |
|----------------|---------------|-------|
| Recursion | ✅ Full | Both `fn` and `pn` support recursion |
| Closures / lambdas | ✅ Full | `fn(x) => ...` anonymous functions |
| Mutable arrays | ✅ Full | `var arr = [...]; arr[i] = v` |
| Floating-point math | ✅ Full | `sqrt`, `sin`, `cos`, `+`, `*`, etc. |
| Integer arithmetic | ✅ Full | `+`, `-`, `*`, `/`, `%` |
| While loops | ✅ Full | `while (cond) { ... }` |
| Nested functions | ✅ Full | Functions can be defined inside `pn` |
| Bitwise ops | ✅ Available | `shl`, `bxor` (used in AWFY mandelbrot) |
| Cons cells / pairs | ❌ Missing | Lambda uses arrays/maps instead of linked lists |
| Tail-call optimization | ⚠️ Partial | Lambda uses while-loops; JIT may optimize tail recursion |
| call/cc | ❌ Missing | No first-class continuations — not needed for selected benchmarks |
| Symbols / `eq?` | ⚠️ Limited | Lambda has symbols (`'x`) but not Scheme-style symbol comparison in list trees |

## Implementation Plan

### Directory Structure
```
test/benchmark/r7rs/
├── fib.ls          # Fibonacci
├── fibfp.ls        # Fibonacci (float)
├── tak.ls          # Takeuchi function
├── cpstak.ls       # CPS Takeuchi
├── sum.ls          # Integer sum
├── sumfp.ls        # Float sum
├── nqueens.ls      # N-Queens counter
├── fft.ls          # Fast Fourier Transform
├── mbrot.ls        # Mandelbrot
├── ack.ls          # Ackermann
├── run_bench.py    # Benchmark runner (Lambda vs Racket vs Guile)
└── Result.md       # Performance comparison
```

### Benchmark Parameters (matching R7RS inputs)

| Benchmark | Parameters | Expected Output |
|-----------|-----------|-----------------|
| fib | fib(40), 5 iterations | 102334155 |
| fibfp | fibfp(35.0), 10 iterations | 9227465.0 |
| tak | tak(40,20,11), 1 iteration | 12 |
| cpstak | cpstak(40,20,11), 1 iteration | 12 |
| sum | sum(10000), 200000 iterations | 50005000 |
| sumfp | sumfp(1e6), 500 iterations | 5.000005e11 |
| nqueens | nqueens(13), 10 iterations | 73712 |
| fft | four1(65536-element vector), 100 iterations | 0.0 |
| mbrot | mbrot(75), 1000 iterations | 5 |
| ack | ack(3,12), 2 iterations | 32765 |

### Timing Methodology

- **External wall-clock timing** using Python `time.perf_counter_ns()`
- Each benchmark run 3 times, report **median**
- Lambda: `./lambda.exe run test/benchmark/r7rs/<name>.ls`
- Racket: `racket test/benchmark/r7rs/<name>.rkt`
- Guile: `guile test/benchmark/r7rs/<name>.scm`
- All iterations are done **inside** the script (matching r7rs harness behavior) so startup cost is amortized

### Comparison Engines

| Engine | Version | Type |
|--------|---------|------|
| Lambda Script | v1.0 | C2MIR JIT |
| Racket | v9.0 [cs] | Chez Scheme backend, JIT |
| Guile | 3.0.10 | Bytecode compiler |

## Actual Outcomes (2026-02-17)

See [test/benchmark/r7rs/Result.md](../test/benchmark/r7rs/Result.md) for full results.

### Key Findings

- **Lambda / Racket**: ~2,800× slower (geometric mean)
- **Lambda / Guile**: ~780× slower (geometric mean)
- **Racket / Guile**: Racket is ~3.6× faster than Guile

### Deviations from Plan

1. **All parameters scaled down** — original R7RS parameters were 100-8000× too large for Lambda. E.g., `fib(27)` instead of `fib(40)`, `tak(18,12,6)` instead of `tak(40,20,11)`.
2. **cpstak not truly CPS** — Lambda JIT does not support `fn(a) => expr` closures inside `pn` functions. Replaced with double invocation of direct `tak`.
3. **sumfp workaround** — transpiler bug #21 (float reassignment) required using `var s = n - n` instead of `var s = 0.0`.
4. **Performance far worse than expected** — initial estimate of "competitive with Guile" was wrong. Lambda is 780× slower than Guile, not competitive. The gap is 15× larger than the AWFY gap vs Node.js (~47×), because R7RS benchmarks exercise tight recursive/arithmetic loops where Lambda's `Item` boxing overhead dominates.
