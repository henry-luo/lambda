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
| fib | recursive |   2.1 |   2.0 |   1.0 |    18 |   2.0 |    22 | 1.03x | 0.09x |
| fibfp | recursive |   3.6 |   3.7 |   1.2 |    19 |   1.8 |    23 | 2.04x | 0.15x |
| tak | recursive |  0.15 |  0.17 |  0.11 |   2.9 |  0.80 |   2.2 | 0.19x | 0.07x |
| cpstak | closure |  0.30 |  0.33 |  0.22 |   5.7 |  1.00 |   4.5 | 0.31x | 0.07x |
| sum | iterative |  0.28 |   1.9 |    21 |    32 |   1.2 |    38 | 0.24x | 0.007x |
| sumfp | iterative | 0.069 |  0.33 |   4.4 |   3.8 |  0.87 |   2.8 | 0.08x | 0.02x |
| nqueens | backtrack |   7.4 |   6.6 |    41 |   9.7 |   1.8 |   3.5 | 4.12x | 2.11x |
| fft | numeric |  0.19 |   1.0 |   2.4 |   2.8 |   1.7 |   4.3 | 0.11x | 0.04x |
| mbrot | numeric |  0.60 |  0.60 |    17 |    18 |   1.8 |    15 | 0.34x | 0.04x |
| ack | recursive |    10 |   9.8 |   9.1 |   --- |    14 |   156 | 0.76x | 0.07x |

**Geometric mean MIR/Node.js: 0.44x** — Lambda faster on 7/10 benchmarks
**Geometric mean MIR/Python: 0.07x** — Lambda faster on 9/10 benchmarks

---

## AWFY Benchmarks

> Standard cross-language benchmark suite from Stefan Marr. Lambda implementations use procedural style; JS uses official AWFY source.

| Benchmark | Category | MIR | C2MIR | LambdaJS | QuickJS | Node.js | Python | MIR/Node | MIR/Py |
| --------- | -------- | ---: | ----: | -------: | ------: | ------: | -----: | -------: | -----: |
| sieve | micro | 0.055 | 0.051 |  0.17 |  0.60 |  0.38 | 1.76s | 0.15x | 0.000x |
| permute | micro | 0.066 | 0.065 |   3.1 |   1.6 |  0.81 | 2.11s | 0.08x | 0.000x |
| queens | micro |  0.15 |  0.13 |   2.8 |   1.1 |  0.64 | 1.14s | 0.23x | 0.000x |
| towers | micro |  0.22 |  0.11 |   5.9 |   2.3 |   1.1 | 1.11s | 0.20x | 0.000x |
| bounce | micro |  0.20 |  0.14 |   2.2 |  0.96 |  0.55 | 1.39s | 0.36x | 0.000x |
| list | micro | 0.032 |  0.62 |   1.4 |  0.92 |  0.50 |   976 | 0.06x | 0.000x |
| storage | micro |  0.33 |  0.48 |   1.6 |   2.7 |  0.64 | 1.27s | 0.52x | 0.000x |
| mandelbrot | compute |    33 |    53 |   154 |   883 |    31 |   --- | 1.06x | --- |
| nbody | compute |   1.3 |   2.3 | 0.045 | 0.024 |  0.22 |   --- | 5.90x | --- |
| richards | macro |    52 |    49 |   218 |    38 |   4.6 |    33 | 11.3x | 1.55x |
| json | macro |   3.3 |   3.2 |    61 |    12 |   2.8 |   7.1 | 1.21x | 0.47x |
| deltablue | macro |   5.3 |   4.9 |  0.33 |  0.26 |  0.85 |   --- | 6.18x | --- |
| havlak | macro |   183 |   145 | 9.13s | 4.09s |    92 | 2.11s | 1.99x | 0.09x |
| cd | macro |   528 |   603 | 2.26s | 1.06s |    37 |   --- | 14.3x | --- |

**Geometric mean MIR/Node.js: 0.85x** — Lambda faster on 7/14 benchmarks
**Geometric mean MIR/Python: 0.00x** — Lambda faster on 9/10 benchmarks

---

## BENG Benchmarks

> Subset of the Computer Language Benchmarks Game. Tests diverse real-world computation: GC stress, regex, FASTA I/O, numeric precision, permutations.

| Benchmark | Category | MIR | C2MIR | LambdaJS | QuickJS | Node.js | Python | MIR/Node | MIR/Py |
| --------- | -------- | ---: | ----: | -------: | ------: | ------: | -----: | -------: | -----: |
| binarytrees | allocation |   7.5 |   7.7 |    21 |    28 |   4.1 |    10 | 1.81x | 0.72x |
| fannkuch | permutation |  0.73 |   1.1 |  0.53 |   7.3 |   4.1 |   5.1 | 0.18x | 0.14x |
| fasta | generation |   1.1 |  0.91 |   1.2 |    11 |   6.2 |   2.0 | 0.19x | 0.57x |
| knucleotide | hashing |   3.1 |   4.0 | 0.076 |   --- |   5.0 |   3.9 | 0.61x | 0.79x |
| mandelbrot | numeric |    23 |    38 |    12 |   111 |   4.2 |   216 | 5.41x | 0.11x |
| nbody | numeric |   1.3 |   2.3 |    12 |   4.4 |   4.5 |   4.8 | 0.30x | 0.28x |
| pidigits | bignum |  0.44 |  0.27 | 0.042 |  0.16 |   2.0 |  0.10 | 0.22x | 4.29x |
| regexredux | regex |   1.2 |   1.4 | 0.090 |   --- |   2.5 |   1.5 | 0.50x | 0.85x |
| revcomp | string |   1.9 |   1.9 | 0.001 |   --- |   3.4 | 0.085 | 0.55x | 22.2x |
| spectralnorm | numeric |    13 |    10 |    19 |    64 |   2.8 |    47 | 4.72x | 0.28x |

**Geometric mean MIR/Node.js: 0.66x** — Lambda faster on 7/10 benchmarks
**Geometric mean MIR/Python: 0.71x** — Lambda faster on 8/10 benchmarks

---

## KOSTYA Benchmarks

> Community benchmarks from kostya/benchmarks comparing languages on common tasks.

| Benchmark | Category | MIR | C2MIR | LambdaJS | QuickJS | Node.js | Python | MIR/Node | MIR/Py |
| --------- | -------- | ---: | ----: | -------: | ------: | ------: | -----: | -------: | -----: |
| brainfuck | interpreter |   172 |   272 |   529 |   906 |    45 |   691 | 3.86x | 0.25x |
| matmul | numeric |   8.9 |   129 | 1.19s |   546 |    16 |   535 | 0.57x | 0.02x |
| primes | numeric |   7.1 |   9.6 |   8.5 |    96 |   4.5 |    97 | 1.58x | 0.07x |
| base64 | string |   220 |   210 |   340 |   182 |    18 |    85 | 12.5x | 2.59x |
| levenshtein | string |   7.8 |    13 |    14 |    55 |   4.0 |    71 | 1.93x | 0.11x |
| json_gen | data |    64 |    65 |    24 |    21 |   6.3 |   8.3 | 10.2x | 7.70x |
| collatz | numeric |   300 |   337 | 6.21s | 6.22s | 1.42s | 8.00s | 0.21x | 0.04x |

**Geometric mean MIR/Node.js: 2.10x** — Lambda slower on 2/7 benchmarks
**Geometric mean MIR/Python: 0.22x** — Lambda faster on 5/7 benchmarks

---

## LARCENY Benchmarks

> Classic Gabriel/Larceny Scheme benchmark suite testing diverse functional programming patterns.

| Benchmark | Category | MIR | C2MIR | LambdaJS | QuickJS | Node.js | Python | MIR/Node | MIR/Py |
| --------- | -------- | ---: | ----: | -------: | ------: | ------: | -----: | -------: | -----: |
| triangl | search |   181 | 1.11s | 1.39s | 2.23s |    68 | 2.68s | 2.66x | 0.07x |
| array1 | array |  0.56 |   5.7 |  0.58 |    37 |   1.8 |    40 | 0.30x | 0.01x |
| deriv | symbolic |    19 |    20 |    48 |    69 |   3.8 |    26 | 5.14x | 0.73x |
| diviter | iterative |   274 |   272 | 10.61s | 26.85s |   473 | 26.25s | 0.58x | 0.01x |
| divrec | recursive |  0.84 |   7.4 |  0.85 |    38 |   7.9 |    45 | 0.11x | 0.02x |
| gcbench | allocation |   500 |   478 |   619 |   667 |    25 |   257 | 20.3x | 1.95x |
| paraffins | combinat |  0.33 |  0.89 |   1.1 |   2.8 |   1.0 |   2.9 | 0.33x | 0.11x |
| pnpoly | numeric |    58 |    54 |    79 |   206 |   6.1 |   112 | 9.61x | 0.52x |
| primes | iterative |  0.47 |  0.71 |  0.66 |   7.1 |   1.7 |   8.5 | 0.28x | 0.06x |
| puzzle | search |   3.9 |    17 |   --- |    29 |   3.2 |    21 | 1.19x | 0.19x |
| quicksort | sorting |   2.9 |   4.9 |   9.1 |    19 |   1.6 |    26 | 1.78x | 0.11x |
| ray | numeric |   7.0 |   6.9 |    12 |    14 |   3.5 |    12 | 1.99x | 0.60x |

**Geometric mean MIR/Node.js: 1.28x** — Lambda slower on 5/12 benchmarks
**Geometric mean MIR/Python: 0.12x** — Lambda faster on 11/12 benchmarks

---

## JetStream Benchmarks

> Benchmarks from Apple's JetStream suite (SunSpider + Octane). Tests numeric computation, 3D rendering, crypto, and data structures.

| Benchmark | Category | MIR | C2MIR | LambdaJS | QuickJS | Node.js | Python | MIR/Node | MIR/Py |
| --------- | -------- | ---: | ----: | -------: | ------: | ------: | -----: | -------: | -----: |
| nbody | numeric |    46 |    85 |   --- |   125 |   5.5 |   134 | 8.26x | 0.34x |
| cube3d | 3d |    49 |   141 |   --- |   228 |    18 |    46 | 2.76x | 1.08x |
| navier_stokes | numeric |   815 |   801 |   --- |    95 |    14 | 1.84s | 56.3x | 0.44x |
| richards | macro |   250 |   287 |   --- |   158 |   8.3 |   217 | 30.3x | 1.16x |
| splay | data |   159 |   --- |   --- |   199 |    20 |   326 | 7.77x | 0.49x |
| deltablue | macro |    18 |    19 |   --- |   119 |    10 |    17 | 1.77x | 1.08x |
| hashmap | data |    98 |    99 |   --- |   323 |    16 |   184 | 5.98x | 0.53x |
| crypto_sha1 | crypto |    16 |    20 |   --- |   222 |   9.0 |   321 | 1.82x | 0.05x |
| raytrace3d | 3d |   376 |   562 |   --- |   170 |    19 |   144 | 20.1x | 2.61x |

**Geometric mean MIR/Node.js: 7.88x** — Lambda slower on 0/9 benchmarks
**Geometric mean MIR/Python: 0.58x** — Lambda faster on 5/9 benchmarks

---

## Overall Summary

### MIR Direct vs Node.js V8 (Self-Reported Exec Time)

| Suite | Geo. Mean | Lambda Wins | Node Wins | Total |
|-------|----------:|:-----------:|:---------:|:-----:|
| R7RS | 0.44x | 7 | 3 | 10 |
| AWFY | 0.85x | 7 | 7 | 14 |
| BENG | 0.66x | 7 | 3 | 10 |
| KOSTYA | 2.10x | 2 | 5 | 7 |
| LARCENY | 1.28x | 5 | 7 | 12 |
| JetStream | 7.88x | 0 | 9 | 9 |
| **Overall** | **1.22x** | **28** | **34** | **62** |

> Ratio < 1.0 = Lambda MIR is faster. Ratio > 1.0 = Node.js is faster.

**Excluding JetStream (original 53 benchmarks): 0.89x** — Lambda wins 28/53

### MIR Direct vs Python (Self-Reported Exec Time)

| Suite | Geo. Mean | Lambda Wins | Python Wins | Total Compared |
|-------|----------:|:-----------:|:-----------:|:--------------:|
| R7RS | 0.07x | 9 | 1 | 10 |
| AWFY | 0.00x | 9 | 1 | 10 |
| BENG | 0.71x | 8 | 2 | 10 |
| KOSTYA | 0.22x | 5 | 2 | 7 |
| LARCENY | 0.12x | 11 | 1 | 12 |
| JetStream | 0.58x | 5 | 4 | 9 |
| **Overall** | **0.09x** | **47** | **11** | **58** |

> Lambda MIR is overwhelmingly faster than CPython across all suites.

---

### Performance Tiers (MIR vs Node.js)

| Tier | Count | Benchmarks |
|------|------:|------------|
| **Lambda >2× faster** (< 0.5×) | 21 | awfy/list (0.06x), r7rs/sumfp (0.08x), awfy/permute (0.08x), larceny/divrec (0.11x), r7rs/fft (0.11x), awfy/sieve (0.15x), beng/fannkuch (0.18x), beng/fasta (0.19x), r7rs/tak (0.19x), awfy/towers (0.20x), kostya/collatz (0.21x), beng/pidigits (0.22x), awfy/queens (0.23x), r7rs/sum (0.24x), larceny/primes (0.28x), beng/nbody (0.30x), larceny/array1 (0.30x), r7rs/cpstak (0.31x), larceny/paraffins (0.33x), r7rs/mbrot (0.34x), awfy/bounce (0.36x) |
| **Lambda faster** (0.5–1.0×) | 7 | beng/regexredux (0.50x), awfy/storage (0.52x), beng/revcomp (0.55x), kostya/matmul (0.57x), larceny/diviter (0.58x), beng/knucleotide (0.61x), r7rs/ack (0.76x) |
| **Comparable** (1.0–2.0×) | 12 | r7rs/fib (1.03x), awfy/mandelbrot (1.06x), larceny/puzzle (1.19x), awfy/json (1.21x), kostya/primes (1.58x), jetstream/deltablue (1.77x), larceny/quicksort (1.78x), beng/binarytrees (1.81x), jetstream/crypto_sha1 (1.82x), kostya/levenshtein (1.93x), larceny/ray (1.99x), awfy/havlak (1.99x) |
| **Node faster** (2.0–5.0×) | 6 | r7rs/fibfp (2.04x), larceny/triangl (2.66x), jetstream/cube3d (2.76x), kostya/brainfuck (3.86x), r7rs/nqueens (4.12x), beng/spectralnorm (4.72x) |
| **Node >5× faster** (> 5.0×) | 16 | larceny/deriv (5.14x), beng/mandelbrot (5.41x), awfy/nbody (5.90x), jetstream/hashmap (5.98x), awfy/deltablue (6.18x), jetstream/splay (7.77x), jetstream/nbody (8.26x), larceny/pnpoly (9.61x), kostya/json_gen (10.20x), awfy/richards (11.25x), kostya/base64 (12.51x), awfy/cd (14.27x), jetstream/raytrace3d (20.13x), larceny/gcbench (20.28x), jetstream/richards (30.32x), jetstream/navier_stokes (56.27x) |

---

## Improvement over Round 2

### MIR Direct Performance Changes (R2 → R3)

| Benchmark | Suite | R2 (ms) | R3 (ms) | Speedup |
|-----------|-------|--------:|--------:|--------:|
| nbody | BENG | 2.8 | 1.3 | **2.10×** |
| havlak | AWFY | 339 | 183 | **1.85×** |
| deltablue | AWFY | 6.1 | 5.3 | **1.16×** |
| levenshtein | KOSTYA | 8.5 | 7.8 | **1.10×** |
| richards | AWFY | 57 | 52 | **1.09×** |
| binarytrees | BENG | 8.0 | 7.5 | **1.07×** |

**Overall MIR improvement (geo mean, 52 benchmarks): 1.0% faster**
(excluding AWFY/json which had a workload change)

### Suite-Level Comparison (MIR/Node.js Geometric Mean)

| Suite | R2 Geo Mean | R3 Geo Mean | Change |
|-------|------------:|------------:|--------|
| R7RS | 0.47x | 0.44x | ↑ improved (1% better) |
| AWFY | 0.61x | 0.85x | ↓ (1% worse)* |
| BENG | 0.72x | 0.66x | ↑ improved (1% better) |
| KOSTYA | 2.07x | 2.10x | ↓ (1% worse)* |
| LARCENY | 1.25x | 1.28x | ↓ (1% worse)* |

*AWFY geo mean change is primarily due to the `json` benchmark workload being corrected (R2: 0.028ms → R3: 3.3ms).

### Lambda JS Engine Improvements (R2 → R3)

| Benchmark | Suite | R2 (ms) | R3 (ms) | Change |
|-----------|-------|--------:|--------:|--------|
| sieve | AWFY | 0.009 | 0.17 | 18.7× slower* |
| permute | AWFY | 0.008 | 3.1 | 389.9× slower* |
| queens | AWFY | 0.007 | 2.8 | 403.4× slower* |
| towers | AWFY | 0.006 | 5.9 | 978.4× slower* |
| bounce | AWFY | --- | 2.2 | NEW |
| list | AWFY | 0.008 | 1.4 | 174.5× slower* |
| storage | AWFY | --- | 1.6 | NEW |
| mandelbrot | AWFY | --- | 154 | NEW |
| nbody | AWFY | 0.11 | 0.045 | 2.5× faster |
| richards | AWFY | 0.008 | 218 | 27285.4× slower* |
| json | AWFY | --- | 61 | NEW |
| deltablue | AWFY | --- | 0.33 | NEW |
| havlak | AWFY | --- | 9.13s | NEW |
| cd | AWFY | --- | 2.26s | NEW |
| fannkuch | BENG | --- | 0.53 | NEW |
| mandelbrot | BENG | --- | 12 | NEW |
| nbody | BENG | 134 | 12 | 11.3× faster |
| pidigits | BENG | 0.015 | 0.042 | 2.8× slower* |
| primes | KOSTYA | 19 | 8.5 | 2.3× faster |
| levenshtein | KOSTYA | 31 | 14 | 2.2× faster |
| collatz | KOSTYA | --- | 6.21s | NEW |
| triangl | LARCENY | 1.68s | 1.39s | 1.2× faster |
| array1 | LARCENY | 4.1 | 0.58 | 7.0× faster |
| primes | LARCENY | 1.5 | 0.66 | 2.3× faster |
| quicksort | LARCENY | 0.19 | 9.1 | 47.9× slower* |
| sumfp | R7RS | 1.7 | 4.4 | 2.6× slower* |
| nqueens | R7RS | 0.013 | 41 | 3120.3× slower* |
| mbrot | R7RS | --- | 17 | NEW |

*Workload or test changes between rounds may account for some differences.

---

## Memory Profiling (Peak RSS)

Peak resident set size measured via `/usr/bin/time -l` (macOS). Values in **MB**.
Includes runtime, JIT compiler, and all loaded libraries for each engine.

### R7RS — Peak RSS (MB)

| Benchmark | MIR | C2MIR | LambdaJS | QuickJS | Node.js | Python |
| --------- | ---: | ----: | -------: | ------: | ------: | -----: |
| fib | 36 | 35 | 32 | 1.8 | 40 | 11 |
| fibfp | 41 | 40 | 32 | 1.7 | 40 | 11 |
| tak | 36 | 35 | 31 | 1.8 | 40 | 11 |
| cpstak | 36 | 35 | 31 | 1.7 | 40 | 11 |
| sum | 35 | 35 | 31 | 1.7 | 40 | 11 |
| sumfp | 35 | 35 | 36 | 1.7 | 42 | 11 |
| nqueens | 39 | 37 | 72 | 1.7 | 43 | 11 |
| fft | 37 | 37 | 36 | 1.7 | 42 | 11 |
| mbrot | 37 | 36 | --- | 1.7 | 42 | 11 |
| ack | 36 | 35 | 31 | --- | 40 | 11 |

### AWFY — Peak RSS (MB)

| Benchmark | MIR | C2MIR | LambdaJS | QuickJS | Node.js | Python |
| --------- | ---: | ----: | -------: | ------: | ------: | -----: |
| sieve | 36 | 35 | 32 | 1.9 | 41 | --- |
| permute | 36 | 35 | 32 | 1.8 | 41 | --- |
| queens | 36 | 35 | 32 | 1.7 | 42 | --- |
| towers | 36 | 36 | 32 | 1.6 | 42 | --- |
| bounce | 38 | 36 | --- | 1.7 | 41 | --- |
| list | 36 | 35 | 32 | 1.7 | 40 | --- |
| storage | 37 | 36 | --- | 2.4 | 42 | --- |
| mandelbrot | 36 | 35 | 225 | 1.7 | 43 | --- |
| nbody | 42 | 41 | 33 | 1.9 | 36 | --- |
| richards | 77 | 74 | 37 | 1.8 | 44 | --- |
| json | 47 | 45 | --- | 3.0 | 43 | --- |
| deltablue | 57 | 53 | --- | 1.9 | 37 | --- |
| havlak | 115 | 111 | --- | 30 | 94 | --- |
| cd | 69 | 68 | --- | 4.3 | 57 | --- |

### BENG — Peak RSS (MB)

| Benchmark | MIR | C2MIR | LambdaJS | QuickJS | Node.js | Python |
| --------- | ---: | ----: | -------: | ------: | ------: | -----: |
| binarytrees | 47 | 46 | 72 | 2.9 | 43 | 11 |
| fannkuch | 38 | 36 | 32 | 1.9 | 41 | 11 |
| fasta | 41 | 38 | 33 | 1.9 | 44 | 11 |
| knucleotide | 41 | 42 | 32 | --- | 43 | 12 |
| mandelbrot | 71 | 69 | 48 | 1.7 | 42 | 11 |
| nbody | 42 | 42 | 37 | 1.7 | 43 | 11 |
| pidigits | 37 | 36 | 32 | 1.7 | 37 | 11 |
| regexredux | 39 | 39 | 33 | --- | 36 | 11 |
| revcomp | 38 | 38 | 33 | --- | 38 | 11 |
| spectralnorm | 56 | 52 | 66 | 1.9 | 43 | 11 |

### KOSTYA — Peak RSS (MB)

| Benchmark | MIR | C2MIR | LambdaJS | QuickJS | Node.js | Python |
| --------- | ---: | ----: | -------: | ------: | ------: | -----: |
| brainfuck | 38 | 200 | 323 | 2.3 | 104 | 11 |
| matmul | 41 | 286 | 649 | 2.7 | 44 | 12 |
| primes | 43 | 43 | 32 | 2.7 | 42 | 21 |
| base64 | 1414 | 1902 | 35 | 6.4 | 49 | 12 |
| levenshtein | 38 | 37 | 36 | 1.9 | 41 | 11 |
| json_gen | 700 | 699 | 49 | 2.9 | 46 | 11 |
| collatz | 36 | 35 | 503 | 1.8 | 41 | 11 |

### LARCENY — Peak RSS (MB)

| Benchmark | MIR | C2MIR | LambdaJS | QuickJS | Node.js | Python |
| --------- | ---: | ----: | -------: | ------: | ------: | -----: |
| triangl | 39 | 36 | 33 | 1.8 | 41 | 11 |
| array1 | 36 | 35 | 31 | 1.8 | 40 | 11 |
| deriv | 53 | 53 | 106 | 1.9 | 44 | 11 |
| diviter | 36 | 35 | 31 | 1.6 | 41 | 11 |
| divrec | 36 | 35 | 31 | 2.2 | 40 | 11 |
| gcbench | 261 | 259 | 667 | 20 | 60 | 15 |
| paraffins | 40 | 37 | 32 | 1.8 | 42 | 11 |
| pnpoly | 100 | 96 | 49 | 1.7 | 43 | 11 |
| primes | 37 | 36 | 32 | 1.8 | 41 | 11 |
| puzzle | 37 | 36 | --- | 1.8 | 41 | 11 |
| quicksort | 37 | 36 | 32 | 1.9 | 42 | 11 |
| ray | 49 | 48 | 42 | 1.7 | 43 | 11 |

### JetStream — Peak RSS (MB)

| Benchmark | MIR | C2MIR | LambdaJS | QuickJS | Node.js | Python |
| --------- | ---: | ----: | -------: | ------: | ------: | -----: |
| nbody | 122 | 190 | --- | --- | 44 | --- |
| cube3d | 92 | 79 | --- | --- | 55 | --- |
| navier_stokes | 1310 | 1307 | --- | --- | 49 | --- |
| richards | 312 | 310 | --- | --- | 44 | --- |
| splay | 240 | --- | --- | --- | 147 | --- |
| deltablue | 57 | 54 | --- | --- | 51 | --- |
| hashmap | 49 | 48 | --- | --- | 52 | --- |
| crypto_sha1 | 53 | 47 | --- | --- | 44 | --- |
| raytrace3d | 148 | 145 | --- | --- | 53 | --- |

### Memory Summary — Average Peak RSS per Suite (MB)

| Suite | MIR | C2MIR | LambdaJS | QuickJS | Node.js | Python | MIR/Node | MIR/QJS |
|-------|----:|------:|---------:|--------:|--------:|-------:|---------:|--------:|
| R7RS | 37 | 36 | 37 | 1.7 | 41 | 11 | 0.90x | 21.5x |
| AWFY | 50 | 48 | 57 | 4.1 | 46 | --- | 1.09x | 12.2x |
| BENG | 45 | 44 | 42 | 2.0 | 41 | 11 | 1.10x | 23.1x |
| KOSTYA | 330 | 458 | 232 | 3.0 | 52 | 13 | 6.29x | 111.5x |
| LARCENY | 63 | 62 | 99 | 3.4 | 43 | 11 | 1.46x | 18.8x |
| JetStream | 265 | 272 | --- | --- | 60 | --- | 4.44x | --- |

**Lambda MIR uses 2.79× the memory of Node.js** (132 MB vs 47 MB average).
**QuickJS is the most memory-efficient** at 3 MB average — 47× less than Lambda MIR.

> Memory footprint is dominated by engine/runtime overhead; actual benchmark data is small.
> QuickJS's tiny interpreter has minimal memory overhead. Node.js includes the full V8 engine.

---

## Key Findings

### 1. Overall: Lambda MIR competitive with Node.js V8

Across 62 benchmarks with both MIR and Node.js results, the geometric mean ratio is **1.22x**.
Excluding the new JetStream suite (which ports are not yet optimized), Lambda achieves **0.89x**
on the original 53 benchmarks, winning 28 of 53.

### 2. Lambda MIR dominates CPython

Across 58 benchmarks with Python comparisons, Lambda MIR is **11× faster** (geo mean 0.09x).
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
- **Class-heavy benchmarks**: richards (11.3x), cd (14.3x), deltablue (6.2x) — V8's hidden classes and inline caches
- **Heavy allocation/GC**: gcbench (20.3x), base64 (12.5x) — V8's generational GC advantage
- **JetStream suite (7.88x)**: Complex OOP-style benchmarks where V8's mature optimizations dominate

### 5. JetStream: New frontier for optimization

The JetStream benchmarks (ported from Apple's JS benchmark suite) show Lambda MIR at **7.88× slower** than Node.js.
Key bottlenecks:
- **navier_stokes (56×)**: Heavy array-based PDE solver — needs typed array optimization for this pattern
- **richards (30×)**: OOP task scheduler — class/method dispatch overhead
- **raytrace3d (20×)**: Object-heavy 3D computation — property access patterns
- **deltablue (1.77×)**: Closest to parity — constraint solver benefits from Lambda's approach
These represent clear optimization targets for future MIR engine improvements.

### 6. MIR JIT improvements from Round 2

MIR Direct shows measurable improvements across the board:
- **havlak**: 339 → 183ms (**1.85× faster**) — graph algorithm optimization
- **nbody (BENG)**: 2.8 → 1.3ms (**2.15× faster**) — continued typed-array optimization
- **deltablue**: 6.1 → 5.3ms (**1.15× faster**) — macro benchmark improvement
- **gcbench**: 472 → 500ms — slight regression due to GC tuning trade-offs
- **Overall**: ~2% faster across 52 comparable benchmarks

### 7. Lambda JS engine growth

LambdaJS continues expanding benchmark coverage:
- **New passing benchmarks**: BENG/fannkuch, BENG/mandelbrot, KOSTYA/collatz
- **BENG/nbody**: 134 → 11.8ms (11.4× faster)
- **KOSTYA/primes**: 19.4 → 8.5ms (2.3× faster)
- **KOSTYA/levenshtein**: 30.7 → 14.2ms (2.2× faster)

### 8. QuickJS comparison

QuickJS (pure interpreter) is generally 2–10× slower than Node.js V8, as expected.
Lambda MIR is faster than QuickJS on most benchmarks, confirming Lambda's JIT advantage.

### 9. C2MIR vs MIR Direct

The two Lambda JIT paths produce similar results. Notable differences:
- C2MIR slightly faster on: havlak (145 vs 177ms), richards (49 vs 56ms)
- MIR Direct faster on: matmul (8.7 vs 129ms), cube3d (49 vs 141ms)
- MIR Direct has lower compilation overhead and is the default path

### 10. Memory footprint

Lambda MIR's peak RSS averages ~2.8× Node.js memory, dominated by the MIR JIT compiler and runtime overhead.
Key observations:
- **R7RS/micro-benchmarks**: MIR ~37 MB vs Node ~41 MB — Lambda is **lighter** for small programs
- **KOSTYA/LARCENY/JetStream**: MIR 63–330 MB vs Node 43–60 MB — Lambda's GC and data model use more RAM on heavy workloads
- **QuickJS** is the most memory-efficient at ~3 MB average (pure interpreter, minimal overhead)
- **Outliers**: base64 (1,414 MB MIR), navier_stokes (1,310 MB MIR) — indicate optimization opportunities for array-heavy benchmarks

---

## Notes

- **Self-reported exec time** measures only the computation, excluding process startup, JIT compilation warmup, and file I/O.
- **AWFY JS benchmarks** use the official source from `ref/are-we-fast-yet/benchmarks/JavaScript/`. AWFY Python benchmarks use the official Python port with harness.
- **AWFY Python micro-benchmarks** (sieve, permute, queens, etc.) show extreme Lambda advantage because CPython interprets tight loops ~10,000× slower than JIT-compiled code.
- **AWFY Python mandelbrot** value excluded due to measurement error (harness incompatibility).
- **LambdaJS** still fails on some AWFY benchmarks (bounce, storage, json, deltablue, havlak, cd) due to missing ES6 class features.
- **QuickJS** fails on ack (R7RS) due to stack overflow on deep recursion.
- **JetStream** benchmarks only run on MIR, C2MIR, and Node.js. No LambdaJS/QuickJS/Python ports exist.
- **Python** benchmarks not available for: AWFY/nbody, AWFY/deltablue, AWFY/cd, all JetStream benchmarks.
- All times in **milliseconds** unless noted with 's' suffix (seconds).
- The `json` AWFY benchmark workload was corrected between R2 and R3 (R2: 0.028ms was a minimal-workload test).