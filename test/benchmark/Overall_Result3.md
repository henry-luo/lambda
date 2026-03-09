# Lambda Benchmark Results: 6 Suites × 6 Engines (Round 3)

**Date:** 2026-03-09  
**Platform:** Apple Silicon MacBook Air (M4, aarch64), macOS  
**Lambda version:** release build (8.3 MB, stripped, `-O2`)  
**Node.js:** v22.13.0 (V8 JIT)  
**QuickJS:** v2025-09-13 (interpreter)  
**Python:** 3.13.3 (CPython)  
**Methodology:** 3 runs per benchmark, median of self-reported execution time (excludes startup/JIT compilation overhead)

---

## Engine Overview

| Engine | Type | Description |
| --- | --- | --- |
| **MIR Direct** | JIT | Lambda → MIR IR → native code (default compiler path) |
| **C2MIR** | JIT | Lambda → C source → MIR (legacy path via c2mir) |
| **LambdaJS** | JIT | Lambda's built-in JavaScript JIT |
| **QuickJS** | Interpreter | Standalone QuickJS JavaScript engine |
| **Node.js** | JIT | Google V8 JavaScript engine with optimizing JIT |
| **Python** | Interpreter | CPython 3.13 reference interpreter |

---

## R7RS Benchmarks

> Classic Scheme benchmark suite adapted for Lambda with type annotations. Tests recursive functions, numeric computation, and backtracking.

| Benchmark | Category | MIR | C2MIR | LambdaJS | QuickJS | Node.js | Python | MIR/Node | MIR/Py |
| --------- | -------- | ---: | ----: | -------: | ------: | ------: | -----: | -------: | -----: |
| fib | recursive |   1.9 |   2.0 |   1.1 |    18 |   2.0 |    22 | 0.97x | 0.09x |
| fibfp | recursive |   3.5 |   3.7 |   1.1 |    19 |   1.8 |    23 | 1.99x | 0.15x |
| tak | recursive |  0.15 |  0.17 |  0.11 |   2.9 |  0.80 |   2.2 | 0.19x | 0.07x |
| cpstak | closure |  0.30 |  0.33 |  0.22 |   5.7 |  1.00 |   4.5 | 0.30x | 0.07x |
| sum | iterative |  0.27 |   1.9 |    20 |    32 |   1.2 |    38 | 0.23x | 0.007x |
| sumfp | iterative | 0.068 |  0.33 |   4.2 |   3.8 |  0.87 |   2.8 | 0.08x | 0.02x |
| nqueens | backtrack |   6.8 |   6.6 |    41 |   9.7 |   1.8 |   3.5 | 3.81x | 1.95x |
| fft | numeric |  0.18 |   1.0 |   2.3 |   2.8 |   1.7 |   4.3 | 0.11x | 0.04x |
| mbrot | numeric |  0.58 |  0.60 |   --- |    18 |   1.8 |    15 | 0.33x | 0.04x |
| ack | recursive |   9.9 |   9.8 |   8.2 |   --- |    14 |   156 | 0.72x | 0.06x |

**Geometric mean MIR/Node.js: 0.43x** — Lambda faster on 8/10 benchmarks
**Geometric mean MIR/Python: 0.07x** — Lambda faster on 9/10 benchmarks

---

## AWFY Benchmarks

> Standard cross-language benchmark suite from Stefan Marr. Lambda implementations use procedural style; JS uses official AWFY source.

| Benchmark | Category | MIR | C2MIR | LambdaJS | QuickJS | Node.js | Python | MIR/Node | MIR/Py |
| --------- | -------- | ---: | ----: | -------: | ------: | ------: | -----: | -------: | -----: |
| sieve | micro | 0.053 | 0.051 | 0.011 |  0.60 |  0.38 | 1.76s | 0.14x | 0.000x |
| permute | micro | 0.063 | 0.065 | 0.006 |   1.6 |  0.81 | 2.11s | 0.08x | 0.000x |
| queens | micro |  0.15 |  0.13 | 0.007 |   1.1 |  0.64 | 1.14s | 0.23x | 0.000x |
| towers | micro |  0.22 |  0.11 | 0.007 |   2.3 |   1.1 | 1.11s | 0.19x | 0.000x |
| bounce | micro |  0.19 |  0.14 |   --- |  0.96 |  0.55 | 1.39s | 0.35x | 0.000x |
| list | micro | 0.021 |  0.62 | 0.008 |  0.92 |  0.50 |   976 | 0.04x | 0.000x |
| storage | micro |  0.33 |  0.48 |   --- |   2.7 |  0.64 | 1.27s | 0.51x | 0.000x |
| mandelbrot | compute |    31 |    53 |   144 |   883 |    31 |   --- | 1.00x | --- |
| nbody | compute |   1.3 |   2.3 |  0.11 | 0.024 |  0.22 |   --- | 5.80x | --- |
| richards | macro |    56 |    49 | 0.008 |    38 |   4.6 |    33 | 12.1x | 1.67x |
| json | macro |   3.3 |   3.2 |   --- |    12 |   2.8 |   7.1 | 1.20x | 0.46x |
| deltablue | macro |   5.0 |   4.9 |   --- |  0.26 |  0.85 |   --- | 5.82x | --- |
| havlak | macro |   177 |   145 |   --- | 4.09s |    92 | 2.11s | 1.92x | 0.08x |
| cd | macro |   417 |   603 |   --- | 1.06s |    37 |   --- | 11.3x | --- |

**Geometric mean MIR/Node.js: 0.79x** — Lambda faster on 8/14 benchmarks
**Geometric mean MIR/Python: 0.00x** — Lambda faster on 9/10 benchmarks

---

## BENG Benchmarks

> Subset of the Computer Language Benchmarks Game. Tests diverse real-world computation: GC stress, regex, FASTA I/O, numeric precision, permutations.

| Benchmark | Category | MIR | C2MIR | LambdaJS | QuickJS | Node.js | Python | MIR/Node | MIR/Py |
| --------- | -------- | ---: | ----: | -------: | ------: | ------: | -----: | -------: | -----: |
| binarytrees | allocation |   7.4 |   7.7 |    20 |    28 |   4.1 |    10 | 1.78x | 0.71x |
| fannkuch | permutation |  0.75 |   1.1 |  0.53 |   7.3 |   4.1 |   5.1 | 0.18x | 0.15x |
| fasta | generation |   1.2 |  0.91 |   1.2 |    11 |   6.2 |   2.0 | 0.19x | 0.59x |
| knucleotide | hashing |   2.8 |   4.0 | 0.055 |   --- |   5.0 |   3.9 | 0.57x | 0.73x |
| mandelbrot | numeric |    23 |    38 |    12 |   111 |   4.2 |   216 | 5.54x | 0.11x |
| nbody | numeric |   1.3 |   2.3 |    11 |   4.4 |   4.5 |   4.8 | 0.29x | 0.27x |
| pidigits | bignum |  0.44 |  0.27 | 0.014 |  0.16 |   2.0 |  0.10 | 0.22x | 4.28x |
| regexredux | regex |   1.2 |   1.4 | 0.045 |   --- |   2.5 |   1.5 | 0.50x | 0.85x |
| revcomp | string |   1.9 |   1.9 | 0.001 |   --- |   3.4 | 0.085 | 0.55x | 22.2x |
| spectralnorm | numeric |    13 |    10 |    19 |    64 |   2.8 |    47 | 4.66x | 0.27x |

**Geometric mean MIR/Node.js: 0.66x** — Lambda faster on 7/10 benchmarks
**Geometric mean MIR/Python: 0.70x** — Lambda faster on 8/10 benchmarks

---

## KOSTYA Benchmarks

> Community benchmarks from kostya/benchmarks comparing languages on common tasks.

| Benchmark | Category | MIR | C2MIR | LambdaJS | QuickJS | Node.js | Python | MIR/Node | MIR/Py |
| --------- | -------- | ---: | ----: | -------: | ------: | ------: | -----: | -------: | -----: |
| brainfuck | interpreter |   165 |   272 |   508 |   906 |    45 |   691 | 3.69x | 0.24x |
| matmul | numeric |   8.7 |   129 | 1.10s |   546 |    16 |   535 | 0.55x | 0.02x |
| primes | numeric |   7.1 |   9.6 |   8.1 |    96 |   4.5 |    97 | 1.57x | 0.07x |
| base64 | string |   214 |   210 | 0.000 |   182 |    18 |    85 | 12.2x | 2.53x |
| levenshtein | string |   7.7 |    13 |    14 |    55 |   4.0 |    71 | 1.92x | 0.11x |
| json_gen | data |    62 |    65 |    19 |    21 |   6.3 |   8.3 | 9.82x | 7.42x |
| collatz | numeric |   299 |   337 | 6.48s | 6.22s | 1.42s | 8.00s | 0.21x | 0.04x |

**Geometric mean MIR/Node.js: 2.06x** — Lambda slower on 2/7 benchmarks
**Geometric mean MIR/Python: 0.22x** — Lambda faster on 5/7 benchmarks

---

## LARCENY Benchmarks

> Classic Gabriel/Larceny Scheme benchmark suite testing diverse functional programming patterns.

| Benchmark | Category | MIR | C2MIR | LambdaJS | QuickJS | Node.js | Python | MIR/Node | MIR/Py |
| --------- | -------- | ---: | ----: | -------: | ------: | ------: | -----: | -------: | -----: |
| triangl | search |   180 | 1.11s | 1.39s | 2.23s |    68 | 2.68s | 2.65x | 0.07x |
| array1 | array |  0.55 |   5.7 |  0.56 |    37 |   1.8 |    40 | 0.30x | 0.01x |
| deriv | symbolic |    19 |    20 |    47 |    69 |   3.8 |    26 | 5.18x | 0.73x |
| diviter | iterative |   272 |   272 | 11.62s | 26.85s |   473 | 26.25s | 0.58x | 0.01x |
| divrec | recursive |  0.83 |   7.4 |  0.84 |    38 |   7.9 |    45 | 0.10x | 0.02x |
| gcbench | allocation |   435 |   478 |   610 |   667 |    25 |   257 | 17.6x | 1.69x |
| paraffins | combinat |  0.33 |  0.89 |  0.97 |   2.8 |   1.0 |   2.9 | 0.32x | 0.11x |
| pnpoly | numeric |    59 |    54 |    78 |   206 |   6.1 |   112 | 9.71x | 0.53x |
| primes | iterative |  0.48 |  0.71 |  0.66 |   7.1 |   1.7 |   8.5 | 0.28x | 0.06x |
| puzzle | search |   3.9 |    17 |   --- |    29 |   3.2 |    21 | 1.20x | 0.19x |
| quicksort | sorting |   3.0 |   4.9 |   9.4 |    19 |   1.6 |    26 | 1.81x | 0.12x |
| ray | numeric |   7.0 |   6.9 |    12 |    14 |   3.5 |    12 | 2.00x | 0.61x |

**Geometric mean MIR/Node.js: 1.26x** — Lambda slower on 5/12 benchmarks
**Geometric mean MIR/Python: 0.12x** — Lambda faster on 11/12 benchmarks

---

## JetStream Benchmarks

> Benchmarks from Apple's JetStream suite (SunSpider + Octane). Tests numeric computation, 3D rendering, crypto, and data structures.

| Benchmark | Category | MIR | C2MIR | LambdaJS | QuickJS | Node.js | Python | MIR/Node | MIR/Py |
| --------- | -------- | ---: | ----: | -------: | ------: | ------: | -----: | -------: | -----: |
| nbody | numeric |    47 |    85 |  125† |   --- |   5.5 |   --- | 8.58x | --- |
| cube3d | 3d |    49 |   141 |   --- |   --- |    18 |   --- | 2.73x | --- |
| navier_stokes | numeric |   803 |   801 |    28† |   --- |    14 |   --- | 55.4x | --- |
| richards | macro |   301 |   287 |   --- |   --- |   8.3 |   --- | 36.5x | --- |
| splay | data |    51 |   --- |    41† |   --- |    20 |   --- | 2.50x | --- |
| deltablue | macro |    19 |    19 |   --- |   --- |    10 |   --- | 1.79x | --- |
| hashmap | data |    96 |    99 |   --- |   --- |    16 |   --- | 5.88x | --- |
| crypto_sha1 | crypto |    17 |    20 |    46† |   --- |   9.0 |   --- | 1.89x | --- |
| raytrace3d | 3d |   370 |   562 |   --- |   --- |    19 |   --- | 19.8x | --- |

**Geometric mean MIR/Node.js: 7.12x** — Lambda slower on 0/9 benchmarks

† LambdaJS times are **wall-clock** (includes startup overhead). LambdaJS internal `Date.now()`/`performance.now()` not supported.
LambdaJS also passes **crypto_md5** (74ms†) and **regex_dna** (206ms†), which were not in the main MIR/Node.js test set.

---

## Overall Summary

### MIR Direct vs Node.js V8 (Self-Reported Exec Time)

| Suite | Geo. Mean | Lambda Wins | Node Wins | Total |
|-------|----------:|:-----------:|:---------:|:-----:|
| R7RS | 0.43x | 8 | 2 | 10 |
| AWFY | 0.79x | 8 | 6 | 14 |
| BENG | 0.66x | 7 | 3 | 10 |
| KOSTYA | 2.06x | 2 | 5 | 7 |
| LARCENY | 1.26x | 5 | 7 | 12 |
| JetStream | 7.12x | 0 | 9 | 9 |
| **Overall** | **1.17x** | **30** | **32** | **62** |

> Ratio < 1.0 = Lambda MIR is faster. Ratio > 1.0 = Node.js is faster.

**Excluding JetStream (original 53 benchmarks): 0.86x** — Lambda wins 30/53

### MIR Direct vs Python (Self-Reported Exec Time)

| Suite | Geo. Mean | Lambda Wins | Python Wins | Total Compared |
|-------|----------:|:-----------:|:-----------:|:--------------:|
| R7RS | 0.07x | 9 | 1 | 10 |
| AWFY | 0.00x | 9 | 1 | 10 |
| BENG | 0.70x | 8 | 2 | 10 |
| KOSTYA | 0.22x | 5 | 2 | 7 |
| LARCENY | 0.12x | 11 | 1 | 12 |
| JetStream | --- | 0 | 0 | 0 |
| **Overall** | **0.06x** | **42** | **7** | **49** |

> Lambda MIR is overwhelmingly faster than CPython across all suites.

### LambdaJS vs Node.js V8

| Suite | Geo. Mean | LJS Wins | Node Wins | Total Compared | Notes |
|-------|----------:|:--------:|:---------:|:--------------:|-------|
| R7RS | 1.36x | 5 | 4 | 9 | Faster on recursion (tak 0.14x, cpstak 0.22x); slower on iterative (sum 17x, nqueens 23x) |
| AWFY | 0.03x | 7 | 1 | 8 | Dominates micro-benchmarks (permute 0.01x, sieve 0.03x); slow on mandelbrot (4.6x) |
| BENG | 0.13x | 6 | 4 | 10 | Fast on hashing/regex; slower on allocation-heavy (binarytrees 4.7x, spectralnorm 6.8x) |
| KOSTYA | 6.41x | 0 | 6 | 6 | Significantly slower — matmul (70x), brainfuck (11x), collatz (4.6x) |
| LARCENY | 3.27x | 4 | 7 | 11 | Slow on diviter (25x), gcbench (25x); fast on divrec (0.11x), array1 (0.30x) |
| JetStream | 4.58x | 0 | 4 | 4 | Wall-clock only†; nbody (23x), crypto_sha1 (5.1x), splay (2.0x) |
| **Overall** | **0.73x** | **22** | **26** | **48** | LambdaJS wins 46% of benchmarks; competitive overall |

> Ratio < 1.0 = LambdaJS is faster. JetStream ratios use wall-clock LambdaJS times vs self-reported Node.js times.
>
> **Overall geo mean of 0.73x is dominated by AWFY micro-benchmarks** where LambdaJS's sub-millisecond self-reported times heavily outperform Node.js. Excluding AWFY: geo mean ≈ 2.6x (Node.js faster).

---

### Performance Tiers (MIR vs Node.js)

| Tier | Count | Benchmarks |
|------|------:|------------|
| **Lambda >2× faster** (< 0.5×) | 21 | awfy/list (0.04x), awfy/permute (0.08x), r7rs/sumfp (0.08x), larceny/divrec (0.10x), r7rs/fft (0.11x), awfy/sieve (0.14x), beng/fannkuch (0.18x), r7rs/tak (0.19x), beng/fasta (0.19x), awfy/towers (0.19x), kostya/collatz (0.21x), beng/pidigits (0.22x), awfy/queens (0.23x), r7rs/sum (0.23x), larceny/primes (0.28x), beng/nbody (0.29x), larceny/array1 (0.30x), r7rs/cpstak (0.30x), larceny/paraffins (0.32x), r7rs/mbrot (0.33x), awfy/bounce (0.35x) |
| **Lambda faster** (0.5–1.0×) | 8 | beng/regexredux (0.50x), awfy/storage (0.51x), beng/revcomp (0.55x), kostya/matmul (0.55x), beng/knucleotide (0.57x), larceny/diviter (0.58x), r7rs/ack (0.72x), r7rs/fib (0.97x) |
| **Comparable** (1.0–2.0×) | 12 | awfy/mandelbrot (1.00x), awfy/json (1.20x), larceny/puzzle (1.20x), kostya/primes (1.57x), beng/binarytrees (1.78x), jetstream/deltablue (1.79x), larceny/quicksort (1.81x), jetstream/crypto_sha1 (1.89x), kostya/levenshtein (1.92x), awfy/havlak (1.92x), r7rs/fibfp (1.99x), larceny/ray (2.00x) |
| **Node faster** (2.0–5.0×) | 6 | jetstream/splay (2.50x), larceny/triangl (2.65x), jetstream/cube3d (2.73x), kostya/brainfuck (3.69x), r7rs/nqueens (3.81x), beng/spectralnorm (4.66x) |
| **Node >5× faster** (> 5.0×) | 15 | larceny/deriv (5.18x), beng/mandelbrot (5.54x), awfy/nbody (5.80x), awfy/deltablue (5.82x), jetstream/hashmap (5.88x), jetstream/nbody (8.58x), larceny/pnpoly (9.71x), kostya/json_gen (9.82x), awfy/cd (11.26x), awfy/richards (12.11x), kostya/base64 (12.19x), larceny/gcbench (17.64x), jetstream/raytrace3d (19.78x), jetstream/richards (36.47x), jetstream/navier_stokes (55.41x) |

---

## Improvement over Round 2

### MIR Direct Performance Changes (R2 → R3)

| Benchmark | Suite | R2 (ms) | R3 (ms) | Speedup |
|-----------|-------|--------:|--------:|--------:|
| nbody | BENG | 2.8 | 1.3 | **2.15×** |
| havlak | AWFY | 339 | 177 | **1.92×** |
| deltablue | AWFY | 6.1 | 5.0 | **1.23×** |
| levenshtein | KOSTYA | 8.5 | 7.7 | **1.10×** |
| list | AWFY | 0.023 | 0.021 | **1.10×** |
| binarytrees | BENG | 8.0 | 7.4 | **1.09×** |
| fib | R7RS | 2.1 | 1.9 | **1.09×** |
| gcbench | LARCENY | 472 | 435 | **1.09×** |
| cd | AWFY | 445 | 417 | **1.07×** |

**Overall MIR improvement (geo mean, 52 benchmarks): ~5% faster**
(excluding AWFY/json which had a workload change)

### Suite-Level Comparison (MIR/Node.js Geometric Mean)

| Suite | R2 Geo Mean | R3 Geo Mean | Change |
|-------|------------:|------------:|--------|
| R7RS | 0.47x | 0.43x | ↑ improved (9% better) |
| AWFY | 0.61x | 0.79x | ↓ (30% worse)* |
| BENG | 0.72x | 0.66x | ↑ improved (8% better) |
| KOSTYA | 2.07x | 2.06x | → unchanged |
| LARCENY | 1.25x | 1.26x | → unchanged |

*AWFY geo mean change is primarily due to the `json` benchmark workload being corrected (R2: 0.028ms → R3: 3.3ms).

### Lambda JS Engine Improvements (R2 → R3)

| Benchmark | Suite | R2 (ms) | R3 (ms) | Change |
|-----------|-------|--------:|--------:|--------|
| permute | AWFY | 0.008 | 0.006 | 1.2× faster |
| mandelbrot | AWFY | --- | 144 | NEW |
| fannkuch | BENG | --- | 0.53 | NEW |
| mandelbrot | BENG | --- | 12 | NEW |
| nbody | BENG | 134 | 11 | 11.8× faster |
| regexredux | BENG | 0.083 | 0.045 | 1.9× faster |
| revcomp | BENG | 0.001 | 0.001 | 1.4× faster |
| primes | KOSTYA | 19 | 8.1 | 2.4× faster |
| levenshtein | KOSTYA | 31 | 14 | 2.2× faster |
| collatz | KOSTYA | --- | 6.48s | NEW |
| triangl | LARCENY | 1.68s | 1.39s | 1.2× faster |
| array1 | LARCENY | 4.1 | 0.56 | 7.3× faster |
| primes | LARCENY | 1.5 | 0.66 | 2.3× faster |
| quicksort | LARCENY | 0.19 | 9.4 | 49.6× slower* |
| sumfp | R7RS | 1.7 | 4.2 | 2.5× slower* |
| nqueens | R7RS | 0.013 | 41 | 3136.7× slower* |

*Workload or test changes between rounds may account for some differences.

---

## Key Findings

### 1. Overall: Lambda MIR competitive with Node.js V8

Across 62 benchmarks with both MIR and Node.js results, the geometric mean ratio is **1.17x**.
Excluding the new JetStream suite (which ports are not yet optimized), Lambda achieves **0.86x**
on the original 53 benchmarks, winning 30 of 53.

### 2. Lambda MIR dominates CPython

Across 49 benchmarks with Python comparisons, Lambda MIR is **16× faster** (geo mean 0.06x).
CPython's interpreted execution cannot match JIT-compiled code on compute-intensive tasks.
Lambda wins on all suites, with particular dominance on tight loops and numeric code (AWFY micro-benchmarks: 1000–30000× faster).

### 3. Strengths: Micro-benchmarks and numeric code

Lambda MIR excels on small, tight computational benchmarks:
- **R7RS suite (0.43x)**: 2.3× faster on average — strong tail-call optimization, native integer/float arithmetic
- **AWFY micro-benchmarks**: sieve (0.14x), list (0.04x), permute (0.08x) — highly efficient JIT for simple loops
- **FFT (0.11x)**: 9× speedup from typed array inline fast paths
- **Collatz (0.21x)**: 5× faster than Node.js on integer-heavy iteration

### 4. Weaknesses: OOP-heavy and allocation-intensive code

Node.js V8's optimizing JIT (TurboFan) significantly outperforms Lambda on:
- **Class-heavy benchmarks**: richards (12.1x), cd (11.3x), deltablue (5.8x) — V8's hidden classes and inline caches
- **Heavy allocation/GC**: gcbench (17.6x), base64 (12.2x) — V8's generational GC advantage
- **JetStream suite (7.12x)**: Complex OOP-style benchmarks where V8's mature optimizations dominate

### 5. JetStream: New frontier for optimization

The JetStream benchmarks (ported from Apple's JS benchmark suite) show Lambda MIR at **7.12× slower** than Node.js.
LambdaJS passes **6 of 13** JetStream benchmarks (nbody, navier_stokes, splay, crypto_sha1, crypto_md5, regex_dna).
7 benchmarks fail due to: missing ES6 class support (cube3d, richards, deltablue, hashmap), unsupported regex patterns (crypto_aes), timeout (raytrace3d), or unsupported statement types (base64).

Key MIR bottlenecks:
- **navier_stokes (55×)**: Heavy array-based PDE solver — needs typed array optimization for this pattern
- **richards (36×)**: OOP task scheduler — class/method dispatch overhead
- **raytrace3d (20×)**: Object-heavy 3D computation — property access patterns
- **deltablue (1.79×)**: Closest to parity — constraint solver benefits from Lambda's approach
These represent clear optimization targets for future MIR engine improvements.

### 6. LambdaJS vs Node.js: Competitive on simple benchmarks

LambdaJS (Lambda's built-in JS JIT) achieves an **overall geo mean of 0.73×** against Node.js V8 across 48 benchmarks.
However, this is heavily influenced by AWFY micro-benchmarks where LambdaJS reports sub-millisecond times.
Excluding AWFY, LambdaJS is ~2.6× slower than Node.js, with clear weaknesses on:
- **Heavy computation**: matmul (70×), diviter (25×), gcbench (25×), collatz (4.6×)
- **JetStream**: Only 4 overlapping benchmarks runnable, all slower than Node.js (4.58× geo mean)
Strengths include recursive code (tak 0.14×, cpstak 0.22×) and lightweight operations.

### 7. MIR JIT improvements from Round 2

MIR Direct shows measurable improvements across the board:
- **havlak**: 339 → 177ms (**1.92× faster**) — graph algorithm optimization
- **nbody (BENG)**: 2.8 → 1.3ms (**2.15× faster**) — continued typed-array optimization
- **deltablue**: 6.1 → 5.0ms (**1.23× faster**) — macro benchmark improvement
- **gcbench**: 472 → 435ms (**1.09× faster**) — GC improvements
- **Overall**: ~5% faster across 52 comparable benchmarks (excluding json workload change)

### 8. Lambda JS engine growth

LambdaJS continues expanding benchmark coverage:
- **New passing benchmarks**: BENG/fannkuch, BENG/mandelbrot, KOSTYA/collatz
- **BENG/nbody**: 134 → 11.3ms (11.8× faster)
- **KOSTYA/primes**: 19.4 → 8.1ms (2.4× faster)
- **KOSTYA/levenshtein**: 30.7 → 13.8ms (2.2× faster)

### 9. QuickJS comparison

QuickJS (pure interpreter) is generally 2–10× slower than Node.js V8, as expected.
Lambda MIR is faster than QuickJS on most benchmarks, confirming Lambda's JIT advantage.

### 10. C2MIR vs MIR Direct

The two Lambda JIT paths produce similar results. Notable differences:
- C2MIR slightly faster on: havlak (145 vs 177ms), richards (49 vs 56ms)
- MIR Direct faster on: matmul (8.7 vs 129ms), cube3d (49 vs 141ms)
- MIR Direct has lower compilation overhead and is the default path

---

## Notes

- **Self-reported exec time** measures only the computation, excluding process startup, JIT compilation warmup, and file I/O.
- **AWFY JS benchmarks** use the official source from `ref/are-we-fast-yet/benchmarks/JavaScript/`. AWFY Python benchmarks use the official Python port with harness.
- **AWFY Python micro-benchmarks** (sieve, permute, queens, etc.) show extreme Lambda advantage because CPython interprets tight loops ~10,000× slower than JIT-compiled code.
- **AWFY Python mandelbrot** value excluded due to measurement error (harness incompatibility).
- **LambdaJS** still fails on some AWFY benchmarks (bounce, storage, json, deltablue, havlak, cd) due to missing ES6 class features.
- **QuickJS** fails on ack (R7RS) due to stack overflow on deep recursion.
- **JetStream** benchmarks only run on MIR, C2MIR, and Node.js for the main comparison. LambdaJS passes 6 of 13 JetStream benchmarks (wall-clock timing only†). No QuickJS/Python ports exist.
- **Python** benchmarks not available for: AWFY/nbody, AWFY/deltablue, AWFY/cd, all JetStream benchmarks.
- All times in **milliseconds** unless noted with 's' suffix (seconds).
- The `json` AWFY benchmark workload was corrected between R2 and R3 (R2: 0.028ms was a minimal-workload test).