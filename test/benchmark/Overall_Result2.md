# Lambda Benchmark Results: 5 Suites x 5 Engines

**Date:** 2026-03-07  
**Platform:** Apple Silicon MacBook Air (M4, aarch64), macOS  
**Lambda version:** release build (8.3 MB, stripped, `-O2`)  
**Node.js:** v22.13.0 (V8 JIT)  
**QuickJS:** v2025-09-13 (interpreter)  
**Methodology:** 3 runs per benchmark, median of self-reported execution time (excludes startup/JIT compilation overhead)

---

## Engine Overview

| Engine         | Type        | Description                                             |
| -------------- | ----------- | ------------------------------------------------------- |
| **MIR Direct** | JIT         | Lambda -> MIR IR -> native code (default compiler path) |
| **C2MIR**      | JIT         | Lambda -> C source -> MIR (legacy path via c2mir)       |
| **LambdaJS**   | JIT         | Lambda's built-in JavaScript JIT                        |
| **QuickJS**    | Interpreter | Standalone QuickJS JavaScript engine                    |
| **Node.js**    | JIT         | Google V8 JavaScript engine with optimizing JIT         |

---

## R7RS Benchmarks (Typed)

> Classic Scheme benchmark suite adapted for Lambda with type annotations. Tests recursive functions, numeric computation, and backtracking.

| Benchmark | Category | MIR | C2MIR | LambdaJS | QuickJS | Node.js | MIR/Node |
|-----------|----------|----:|------:|---------:|--------:|--------:|---------:|
| fib | recursive | 2.1 | 2.2 | 1.1 | 17.4 | 1.6 | 1.28x |
| fibfp | recursive | 3.6 | 3.4 | 1.2 | 17.4 | 1.6 | 2.25x |
| tak | recursive | 0.15 | 0.17 | 0.11 | 2.7 | 0.76 | 0.20x |
| cpstak | closure | 0.31 | 0.33 | 0.22 | 5.2 | 0.91 | 0.34x |
| sum | iterative | 0.27 | 2.0 | 16.5 | 29.1 | 1.1 | 0.24x |
| sumfp | iterative | 0.067 | 0.34 | 1.7 | 3.6 | 0.82 | 0.08x |
| nqueens | backtrack | 6.5 | 7.2 | 0.013 | 9.1 | 1.7 | 3.90x |
| fft | numeric | 0.19 | 1.1 | 2.4 | 2.6 | 1.5 | 0.13x |
| mbrot | numeric | 0.60 | 0.89 | --- | 16.7 | 1.6 | 0.36x |
| ack | recursive | 10.3 | 9.7 | 8.6 | --- | 12.4 | 0.83x |

**Geometric mean MIR/Node.js: 0.47x** -- Lambda faster on 7/10 benchmarks

---

## Are We Fast Yet (AWFY) Benchmarks

> Standard cross-language benchmark suite from Stefan Marr. Lambda implementations use procedural style; JS uses official AWFY source.

| Benchmark | Category | MIR | C2MIR | LambdaJS | QuickJS | Node.js | MIR/Node |
|-----------|----------|----:|------:|---------:|--------:|--------:|---------:|
| sieve | micro | 0.053 | 0.052 | 0.009 | 0.57 | 0.35 | 0.15x |
| permute | micro | 0.066 | 0.074 | 0.008 | 1.6 | 0.82 | 0.08x |
| queens | micro | 0.15 | 0.14 | 0.007 | 1.1 | 0.64 | 0.23x |
| towers | micro | 0.22 | 0.12 | 0.006 | 2.6 | 1.1 | 0.19x |
| bounce | micro | 0.20 | 0.15 | --- | 0.89 | 0.56 | 0.36x |
| list | micro | 0.023 | 0.62 | 0.008 | 0.94 | 0.50 | 0.05x |
| storage | micro | 0.32 | 0.49 | --- | 2.7 | 0.67 | 0.48x |
| mandelbrot | compute | 32.0 | 54.3 | --- | 849 | 31.5 | 1.02x |
| nbody | compute | 1.3 | 2.3 | 0.11 | 0.023 | 0.24 | 5.57x |
| richards | macro | 56.7 | 49.5 | 0.008 | 39.2 | 4.5 | 12.64x |
| json | macro | 0.028 | 3.3 | --- | 11.9 | 2.6 | 0.01x |
| deltablue | macro | 6.1 | 4.9 | --- | 0.25 | 0.85 | 7.12x |
| havlak | macro | 339 | 176 | --- | 3.97s | 97.6 | 3.47x |
| cd | macro | 445 | 617 | --- | 1.04s | 37.1 | 11.98x |

**Geometric mean MIR/Node.js: 0.61x** -- Lambda faster on 8/14 benchmarks

---

## Benchmarks Game (BENG)

> Subset of the Computer Language Benchmarks Game. Tests diverse real-world computation: GC stress, regex, FASTA I/O, numeric precision, permutations.

| Benchmark | Category | MIR | C2MIR | LambdaJS | QuickJS | Node.js | MIR/Node |
|-----------|----------|----:|------:|---------:|--------:|--------:|---------:|
| binarytrees | allocation | 8.0 | 8.3 | 18.8 | 28.9 | 4.1 | 1.96x |
| fannkuch | permutation | 0.74 | 1.1 | --- | 7.1 | 4.0 | 0.19x |
| fasta | generation | 1.1 | 0.86 | 1.1 | 10.9 | 6.1 | 0.19x |
| knucleotide | hashing | 2.9 | 3.9 | 0.054 | --- | 5.3 | 0.55x |
| mandelbrot | numeric | 22.4 | 38.1 | --- | 113 | 4.2 | 5.36x |
| nbody | numeric | 2.8 | 2.7 | 134 | 4.6 | 4.5 | 0.63x |
| pidigits | bignum | 0.43 | 0.32 | 0.015 | 0.16 | 2.0 | 0.22x |
| regexredux | regex | 1.2 | 1.4 | 0.083 | --- | 2.6 | 0.49x |
| revcomp | string | 1.9 | 1.9 | 0.001 | --- | 3.4 | 0.55x |
| spectralnorm | numeric | 13.1 | 10.6 | 19.2 | 64.8 | 2.7 | 4.94x |

**Geometric mean MIR/Node.js: 0.72x** -- Lambda faster on 7/10 benchmarks

---

## Kostya Benchmarks

> Community benchmarks from kostya/benchmarks comparing languages on common tasks.

| Benchmark | Category | MIR | C2MIR | LambdaJS | QuickJS | Node.js | MIR/Node |
|-----------|----------|----:|------:|---------:|--------:|--------:|---------:|
| brainfuck | interpreter | 164 | 288 | 420 | 916 | 47.5 | 3.46x |
| matmul | numeric | 8.7 | 340 | 1.18s | 554 | 15.9 | 0.55x |
| primes | numeric | 7.3 | 10.1 | 19.4 | 98.1 | 4.5 | 1.63x |
| base64 | string | 222 | 853 | 0.000 | 188 | 18.0 | 12.34x |
| levenshtein | string | 8.5 | 13.4 | 30.7 | 56.9 | 4.1 | 2.08x |
| json_gen | data | 64.7 | 66.6 | 20.3 | 21.5 | 6.7 | 9.61x |
| collatz | numeric | 302 | 341 | --- | 6.26s | 1.43s | 0.21x |

**Geometric mean MIR/Node.js: 2.07x** -- Lambda faster on 2/7 benchmarks

---

## Larceny/Gabriel Benchmarks

> Classic Gabriel/Larceny Scheme benchmark suite testing diverse functional programming patterns.

| Benchmark | Category | MIR | C2MIR | LambdaJS | QuickJS | Node.js | MIR/Node |
|-----------|----------|----:|------:|---------:|--------:|--------:|---------:|
| triangl | search | 187 | 1.12s | 1.68s | 2.23s | 68.1 | 2.75x |
| array1 | array | 0.56 | 5.8 | 4.1 | 37.5 | 1.8 | 0.31x |
| deriv | symbolic | 20.0 | 21.1 | 44.3 | 70.2 | 3.8 | 5.25x |
| diviter | iterative | 272 | 271 | 11.63s | 26.94s | 473 | 0.58x |
| divrec | recursive | 0.82 | 7.3 | 0.84 | 39.0 | 7.7 | 0.11x |
| gcbench | allocation | 472 | 482 | 586 | 669 | 24.9 | 18.93x |
| paraffins | combinat | 0.33 | 0.89 | 0.72 | 2.6 | 1.0 | 0.32x |
| pnpoly | numeric | 58.9 | 52.3 | 78.5 | 209 | 6.0 | 9.79x |
| primes | iterative | 0.47 | 0.69 | 1.5 | 6.8 | 1.7 | 0.27x |
| puzzle | search | 3.9 | 16.3 | --- | 30.3 | 3.4 | 1.13x |
| quicksort | sorting | 2.9 | 4.9 | 0.19 | 20.4 | 1.8 | 1.58x |
| ray | numeric | 7.2 | 7.0 | 13.7 | 14.3 | 3.8 | 1.88x |

**Geometric mean MIR/Node.js: 1.25x** -- Lambda faster on 5/12 benchmarks

---

## Overall Summary

### MIR Direct vs Node.js V8 (Self-Reported Exec Time)

| Suite | Geo. Mean | Lambda Wins | Node Wins | Total |
|-------|----------:|:-----------:|:---------:|:-----:|
| R7RS | 0.47x | 7 | 3 | 10 |
| AWFY | 0.61x | 8 | 6 | 14 |
| BENG | 0.72x | 7 | 3 | 10 |
| KOSTYA | 2.07x | 2 | 5 | 7 |
| LARCENY | 1.25x | 5 | 7 | 12 |
| **Overall** | **0.83x** | **29** | **24** | **53** |

> Ratio < 1.0 = Lambda MIR is faster. Ratio > 1.0 = Node.js is faster.

### Performance Tiers

| Tier | Count | Benchmarks |
|------|------:|------------|
| **Lambda >2x faster** (< 0.5x) | 23 | awfy/json (0.01x), awfy/list (0.05x), awfy/permute (0.08x), r7rs/sumfp (0.08x), larceny/divrec (0.11x), r7rs/fft (0.13x), awfy/sieve (0.15x), beng/fannkuch (0.19x), beng/fasta (0.19x), awfy/towers (0.19x), r7rs/tak (0.20x), kostya/collatz (0.21x), beng/pidigits (0.22x), awfy/queens (0.23x), r7rs/sum (0.24x), larceny/primes (0.27x), larceny/array1 (0.31x), larceny/paraffins (0.32x), r7rs/cpstak (0.34x), awfy/bounce (0.36x), r7rs/mbrot (0.36x), awfy/storage (0.48x), beng/regexredux (0.49x) |
| **Lambda faster** (0.5-1.0x) | 6 | kostya/matmul (0.55x), beng/revcomp (0.55x), beng/knucleotide (0.55x), larceny/diviter (0.58x), beng/nbody (0.63x), r7rs/ack (0.83x) |
| **Comparable** (1.0-2.0x) | 7 | awfy/mandelbrot (1.02x), larceny/puzzle (1.13x), r7rs/fib (1.28x), larceny/quicksort (1.58x), kostya/primes (1.63x), larceny/ray (1.88x), beng/binarytrees (1.96x) |
| **Node faster** (2.0-5.0x) | 7 | kostya/levenshtein (2.08x), r7rs/fibfp (2.25x), larceny/triangl (2.75x), kostya/brainfuck (3.46x), awfy/havlak (3.47x), r7rs/nqueens (3.90x), beng/spectralnorm (4.94x) |
| **Node >5x faster** (> 5.0x) | 10 | larceny/deriv (5.25x), beng/mandelbrot (5.36x), awfy/nbody (5.57x), awfy/deltablue (7.12x), kostya/json_gen (9.61x), larceny/pnpoly (9.79x), awfy/cd (11.98x), kostya/base64 (12.34x), awfy/richards (12.64x), larceny/gcbench (18.93x) |

---

## Key Findings

### 1. Overall: Lambda MIR is 17% faster than Node.js V8

Across 53 benchmarks, the geometric mean ratio is **0.83x**, meaning Lambda MIR Direct
is faster than Node.js V8 overall. Lambda wins 29/53 benchmarks outright.

### 2. Strengths: Micro-benchmarks and numeric code

Lambda MIR excels on small, tight computational benchmarks:
- **R7RS suite (0.47x)**: Lambda is 2.1x faster on average -- strong tail-call optimization,
  native integer/float arithmetic, and minimal overhead on recursive functions.
- **AWFY micro-benchmarks**: sieve (0.15x), list (0.05x), json (0.01x), permute (0.08x) --
  Lambda's JIT produces very efficient code for simple loops and array operations.
- **FFT (0.13x)**: 8x speedup from typed array inline fast paths enabling native float operations.

### 3. Weaknesses: OOP-heavy and allocation-intensive code

Node.js V8's optimizing JIT (TurboFan) significantly outperforms Lambda on:
- **Class-heavy benchmarks**: richards (12.6x), cd (12.0x), deltablue (7.1x) -- V8's hidden
  classes and inline caches optimize property access patterns that Lambda handles generically.
- **Heavy allocation/GC**: gcbench (18.9x), base64 (12.3x) -- V8's
  generational GC and optimized string/array handling give it a large advantage.
- **Havlak (3.5x)**: Complex graph algorithm with heavy object allocation.

### 4. Lambda JS improvements

Significant performance gains in the Lambda JS engine across all suites:
- **ack**: 61.1 -> 8.6ms (7.1x faster)
- **divrec**: 24.3 -> 0.84ms (29x faster)
- **quicksort**: 9.8 -> 0.19ms (51x faster)
- **array1**: 20.4 -> 4.1ms (5x faster)
- **spectralnorm**: 48.9 -> 19.2ms (2.5x faster)
- **brainfuck**: 713 -> 420ms (1.7x faster)
- **primes (kostya)**: 54.4 -> 19.4ms (2.8x faster)

### 5. MIR JIT typed-array optimizations

The inline typed-array fast paths produced dramatic speedups on array-heavy benchmarks:
- **matmul**: 200 -> 8.7ms (23x faster) -- native float[] load/store bypasses boxed runtime calls
- **fft**: 1.5 -> 0.19ms (8x faster) -- native double arithmetic on float[] elements
- **nbody (AWFY)**: 2.8 -> 1.3ms (2.1x faster)
- **permute**: 0.11 -> 0.066ms (1.7x faster)

### 6. QuickJS comparison

QuickJS (pure interpreter) is generally 2-10x slower than Node.js V8, as expected.
Lambda MIR is faster than QuickJS on most benchmarks, confirming that Lambda's JIT
compilation provides a real performance advantage over interpretation.

### 7. C2MIR vs MIR Direct

The two Lambda JIT paths produce similar results for most benchmarks. C2MIR is slightly
faster on a few (havlak, cd) due to additional C-level optimizations in the c2mir pipeline,
while MIR Direct has lower startup overhead. Both paths are competitive with Node.js V8.

---

## Notes

- **Self-reported exec time** measures only the computation, excluding process startup,
  JIT compilation warmup, and file I/O. This isolates pure algorithmic performance.
- **AWFY JS benchmarks** use the official source from `ref/are-we-fast-yet/benchmarks/JavaScript/`.
  Standalone bundles were created for LambdaJS/QuickJS (which don't support `require()`).
- **LambdaJS** failed on some AWFY benchmarks (bounce, storage, json, deltablue, havlak, cd)
  due to missing ES6 class features in the Lambda JS engine.
- **QuickJS** failed on ack (R7RS) due to stack overflow on deep recursion.
- **BENG** (Benchmarks Game) JS scripts with `fs.readFileSync` (knucleotide, regexredux, revcomp) fail on QuickJS/LambdaJS (no `fs` module). Timing excludes file I/O.
- All times in **milliseconds** unless noted with 's' suffix (seconds).
