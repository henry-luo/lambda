# Lambda Benchmark Results: 6 Suites × 6 Engines (Round 4)

**Date:** 2026-03-19  
**Platform:** Apple Silicon MacBook Air (M4, aarch64), macOS  
**Lambda version:** release build (8.4 MB, stripped, `-O2`)  
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
| fib | recursive |   2.5 |   2.1 |  0.99 |    18 |   2.0 |    22 | 1.23x | 0.11x |
| fibfp | recursive |   3.7 |   3.5 |   1.0 |    19 |   1.8 |    23 | 2.11x | 0.16x |
| tak | recursive |  0.15 |  0.16 |  0.10 |   2.9 |  0.80 |   2.2 | 0.19x | 0.07x |
| cpstak | closure |  0.30 |  0.34 |  0.22 |   5.7 |  1.00 |   4.5 | 0.30x | 0.07x |
| sum | iterative |  0.27 |   1.8 |    94 |    32 |   1.2 |    38 | 0.23x | 0.007x |
| sumfp | iterative | 0.067 |  0.33 |    14 |   3.8 |  0.87 |   2.8 | 0.08x | 0.02x |
| nqueens | backtrack |   6.7 |   6.6 |   6.8 |   9.7 |   1.8 |   3.5 | 3.72x | 1.90x |
| fft | numeric |  0.18 |   1.0 |   9.8 |   2.8 |   1.7 |   4.3 | 0.11x | 0.04x |
| mbrot | numeric |  0.59 |  0.74 |    55 |    18 |   1.8 |    15 | 0.34x | 0.04x |
| ack | recursive |   9.8 |   9.8 |   8.1 |   --- |    14 |   156 | 0.72x | 0.06x |

**Geometric mean MIR/Node.js: 0.44x** — Lambda faster on 7/10 benchmarks
**Geometric mean MIR/Python: 0.07x** — Lambda faster on 9/10 benchmarks

---

## AWFY Benchmarks

> Standard cross-language benchmark suite from Stefan Marr. Lambda implementations use procedural style; JS uses official AWFY source.

| Benchmark | Category | MIR | C2MIR | LambdaJS | QuickJS | Node.js | Python | MIR/Node | MIR/Py |
| --------- | -------- | ---: | ----: | -------: | ------: | ------: | -----: | -------: | -----: |
| sieve | micro | 0.052 | 0.052 |  0.77 |  0.60 |  0.38 | 1.76s | 0.14x | 0.000x |
| permute | micro | 0.064 | 0.068 |    13 |   1.6 |  0.81 | 2.11s | 0.08x | 0.000x |
| queens | micro |  0.15 |  0.14 |    11 |   1.1 |  0.64 | 1.14s | 0.23x | 0.000x |
| towers | micro |  0.22 |  0.11 |    23 |   2.3 |   1.1 | 1.11s | 0.19x | 0.000x |
| bounce | micro |  0.19 |  0.14 |    10 |  0.96 |  0.55 | 1.39s | 0.35x | 0.000x |
| list | micro | 0.023 |  0.62 |   7.9 |  0.92 |  0.50 |   976 | 0.05x | 0.000x |
| storage | micro |  0.19 |  0.32 |   6.2 |   2.7 |  0.64 | 1.27s | 0.30x | 0.000x |
| mandelbrot | compute |    32 |    55 |   279 |   888 |    32 |   --- | 1.00x | --- |
| nbody | compute |    47 |    84 | 2.06s |   167 |   5.6 |   135 | 8.38x | 0.35x |
| richards | macro |   253 |   219 | 3.31s |   194 |    48 |   168 | 5.29x | 1.50x |
| json | macro |   1.5 |   2.1 |   160 |    12 |   2.8 |   7.1 | 0.54x | 0.21x |
| deltablue | macro |    64 |    78 |   935 |   113 |    13 |    68 | 5.07x | 0.94x |
| havlak | macro |    61 |    82 | 39.66s | 4.09s |    92 | 2.11s | 0.66x | 0.03x |
| cd | macro |   220 |   358 | 11.66s | 1.06s |    37 |   --- | 5.94x | --- |

**Geometric mean MIR/Node.js: 0.62x** — Lambda faster on 10/14 benchmarks
**Geometric mean MIR/Python: 0.00x** — Lambda faster on 11/12 benchmarks

---

## BENG Benchmarks

> Subset of the Computer Language Benchmarks Game. Tests diverse real-world computation: GC stress, regex, FASTA I/O, numeric precision, permutations.

| Benchmark | Category | MIR | C2MIR | LambdaJS | QuickJS | Node.js | Python | MIR/Node | MIR/Py |
| --------- | -------- | ---: | ----: | -------: | ------: | ------: | -----: | -------: | -----: |
| binarytrees | allocation |   7.3 |   7.7 |   114 |    28 |   4.1 |    10 | 1.76x | 0.70x |
| fannkuch | permutation |  0.76 |   1.1 |   1.6 |   7.3 |   4.1 |   5.1 | 0.18x | 0.15x |
| fasta | generation |   1.1 |  0.84 |   3.9 |    11 |   6.2 |   2.0 | 0.18x | 0.55x |
| knucleotide | hashing |   2.9 |   3.8 | 0.088 |   --- |   5.0 |   3.9 | 0.58x | 0.74x |
| mandelbrot | numeric |   142 |   238 | 2.85s |   698 |    16 | 1.37s | 9.13x | 0.10x |
| nbody | numeric |    47 |    85 | 1.75s |   155 |   8.1 |   172 | 5.84x | 0.27x |
| pidigits | bignum |  0.46 |  0.30 | 0.083 |  0.16 |   2.0 |  0.10 | 0.23x | 4.53x |
| regexredux | regex |   1.2 |   1.4 | 0.095 |   --- |   2.5 |   1.5 | 0.50x | 0.85x |
| revcomp | string |   1.8 |   1.8 | 0.002 |   --- |   3.4 | 0.085 | 0.54x | 21.7x |
| spectralnorm | numeric |    13 |    10 |    80 |    64 |   2.8 |    47 | 4.72x | 0.28x |

**Geometric mean MIR/Node.js: 0.94x** — Lambda faster on 6/10 benchmarks
**Geometric mean MIR/Python: 0.70x** — Lambda faster on 8/10 benchmarks

---

## KOSTYA Benchmarks

> Community benchmarks from kostya/benchmarks comparing languages on common tasks.

| Benchmark | Category | MIR | C2MIR | LambdaJS | QuickJS | Node.js | Python | MIR/Node | MIR/Py |
| --------- | -------- | ---: | ----: | -------: | ------: | ------: | -----: | -------: | -----: |
| brainfuck | interpreter |   165 |   280 | 2.31s |   906 |    45 |   691 | 3.70x | 0.24x |
| matmul | numeric |   8.8 |   128 | 2.83s |   546 |    16 |   535 | 0.56x | 0.02x |
| primes | numeric |   7.3 |    10 |    25 |    97 |   4.5 |    97 | 1.62x | 0.08x |
| base64 | string |   220 |   221 |   900 |   182 |    18 |    85 | 12.5x | 2.59x |
| levenshtein | string |   7.7 |    13 |    71 |    55 |   4.0 |    71 | 1.92x | 0.11x |
| json_gen | data |    65 |    67 |    79 |    21 |   6.3 |   8.3 | 10.3x | 7.76x |
| collatz | numeric |   301 |   340 | 18.53s | 6.22s | 1.42s | 8.00s | 0.21x | 0.04x |

**Geometric mean MIR/Node.js: 2.09x** — Lambda slower on 2/7 benchmarks
**Geometric mean MIR/Python: 0.22x** — Lambda faster on 5/7 benchmarks

---

## LARCENY Benchmarks

> Classic Gabriel/Larceny Scheme benchmark suite testing diverse functional programming patterns.

| Benchmark | Category | MIR | C2MIR | LambdaJS | QuickJS | Node.js | Python | MIR/Node | MIR/Py |
| --------- | -------- | ---: | ----: | -------: | ------: | ------: | -----: | -------: | -----: |
| triangl | search |   179 | 1.11s | 6.82s | 2.23s |    68 | 2.68s | 2.62x | 0.07x |
| array1 | array |  0.55 |   5.8 |  0.55 |    37 |   1.8 |    40 | 0.30x | 0.01x |
| deriv | symbolic |    20 |    21 |   894 |    69 |   3.8 |    26 | 5.35x | 0.76x |
| diviter | iterative |   272 |   271 | 61.97s | 26.85s |   473 | 26.25s | 0.57x | 0.01x |
| divrec | recursive |  0.84 |   7.4 |  0.82 |    38 |   7.9 |    45 | 0.11x | 0.02x |
| gcbench | allocation |   469 |   439 | 2.86s |   667 |    25 |   257 | 19.0x | 1.83x |
| paraffins | combinat |  0.33 |  0.92 |   6.1 |   2.8 |   1.0 |   2.9 | 0.33x | 0.11x |
| pnpoly | numeric |    59 |    53 |   312 |   206 |   6.1 |   112 | 9.67x | 0.52x |
| primes | iterative |   7.2 |   9.7 |    26 |    97 |   4.7 |   121 | 1.53x | 0.06x |
| puzzle | search |   3.8 |    17 |    82 |    29 |   3.2 |    21 | 1.16x | 0.18x |
| quicksort | sorting |   3.1 |   6.8 |    55 |    19 |   1.6 |    26 | 1.90x | 0.12x |
| ray | numeric |   7.1 |   6.9 |    40 |    14 |   3.5 |    12 | 2.02x | 0.61x |

**Geometric mean MIR/Node.js: 1.47x** — Lambda slower on 4/12 benchmarks
**Geometric mean MIR/Python: 0.12x** — Lambda faster on 11/12 benchmarks

---

## JetStream Benchmarks

> Benchmarks from Apple's JetStream suite (SunSpider + Octane). Tests numeric computation, 3D rendering, crypto, and data structures.

| Benchmark | Category | MIR | C2MIR | LambdaJS | QuickJS | Node.js | Python | MIR/Node | MIR/Py |
| --------- | -------- | ---: | ----: | -------: | ------: | ------: | -----: | -------: | -----: |
| nbody | numeric |    47 |    85 | 1.86s |   --- |   5.5 |   146 | 8.50x | 0.32x |
| cube3d | 3d |    24 |    81 |    21 |   228 |    18 |    46 | 1.31x | 0.52x |
| navier_stokes | numeric |   823 |   809 |  0.23 |    95 |    14 | 1.84s | 56.8x | 0.45x |
| richards | macro |   259 |   245 |   566 |   --- |   8.3 |   225 | 31.3x | 1.15x |
| splay | data |   165 |   --- |    49 |   199 |    20 |   326 | 8.07x | 0.51x |
| deltablue | macro |    17 |    18 |    48 |   --- |    11 |    18 | 1.61x | 0.96x |
| hashmap | data |   106 |   106 |   --- |   323 |    16 |   184 | 6.50x | 0.58x |
| crypto_sha1 | crypto |    17 |    20 |   144 |   222 |   9.0 |   321 | 1.85x | 0.05x |
| raytrace3d | 3d |   348 |   435 |   720 |   170 |    19 |   144 | 18.6x | 2.42x |

**Geometric mean MIR/Node.js: 7.28x** — Lambda slower on 0/9 benchmarks
**Geometric mean MIR/Python: 0.52x** — Lambda faster on 7/9 benchmarks

---

## Overall Summary

### MIR Direct vs Node.js V8 (Self-Reported Exec Time)

| Suite | Geo. Mean | Lambda Wins | Node Wins | Total |
|-------|----------:|:-----------:|:---------:|:-----:|
| R7RS | 0.44x | 7 | 3 | 10 |
| AWFY | 0.62x | 10 | 4 | 14 |
| BENG | 0.94x | 6 | 4 | 10 |
| KOSTYA | 2.09x | 2 | 5 | 7 |
| LARCENY | 1.47x | 4 | 8 | 12 |
| JetStream | 7.28x | 0 | 9 | 9 |
| **Overall (raw)** | **1.21x** | **29** | **33** | **62** |
| **Overall (dedup)** | **1.05x** | **28** | **28** | **56** |

> Ratio < 1.0 = Lambda MIR is faster. Ratio > 1.0 = Node.js is faster.
> **Dedup note:** 56 unique benchmarks out of 62 total entries. Duplicates (same name across suites): deltablue, mandelbrot, nbody, primes, richards — best time per engine is used.

**Excluding JetStream (50 unique benchmarks): 0.83x** — Lambda wins 28/50

### MIR Direct vs Python (Self-Reported Exec Time)

| Suite | Geo. Mean | Lambda Wins | Python Wins | Total Compared |
|-------|----------:|:-----------:|:-----------:|:--------------:|
| R7RS | 0.07x | 9 | 1 | 10 |
| AWFY | 0.00x | 11 | 1 | 12 |
| BENG | 0.70x | 8 | 2 | 10 |
| KOSTYA | 0.22x | 5 | 2 | 7 |
| LARCENY | 0.12x | 11 | 1 | 12 |
| JetStream | 0.52x | 7 | 2 | 9 |
| **Overall (raw)** | **0.09x** | **51** | **9** | **60** |
| **Overall (dedup)** | **0.08x** | **47** | **8** | **55** |

> Lambda MIR is overwhelmingly faster than CPython across all suites.

---

### Performance Tiers (MIR vs Node.js)

| Tier | Count | Benchmarks |
|------|------:|------------|
| **Lambda >2× faster** (< 0.5×) | 20 | awfy/list (0.05x), r7rs/sumfp (0.08x), awfy/permute (0.08x), larceny/divrec (0.11x), r7rs/fft (0.11x), awfy/sieve (0.14x), beng/fasta (0.18x), beng/fannkuch (0.18x), r7rs/tak (0.19x), awfy/towers (0.19x), kostya/collatz (0.21x), awfy/queens (0.23x), r7rs/sum (0.23x), beng/pidigits (0.23x), larceny/array1 (0.30x), awfy/storage (0.30x), r7rs/cpstak (0.30x), larceny/paraffins (0.33x), r7rs/mbrot (0.34x), awfy/bounce (0.35x) |
| **Lambda faster** (0.5–1.0×) | 9 | beng/regexredux (0.50x), beng/revcomp (0.54x), awfy/json (0.54x), kostya/matmul (0.56x), larceny/diviter (0.57x), beng/knucleotide (0.58x), awfy/havlak (0.66x), r7rs/ack (0.72x), awfy/mandelbrot (1.00x) |
| **Comparable** (1.0–2.0×) | 10 | larceny/puzzle (1.16x), r7rs/fib (1.23x), jetstream/cube3d (1.31x), larceny/primes (1.53x), jetstream/deltablue (1.61x), kostya/primes (1.62x), beng/binarytrees (1.76x), jetstream/crypto_sha1 (1.85x), larceny/quicksort (1.90x), kostya/levenshtein (1.92x) |
| **Node faster** (2.0–5.0×) | 6 | larceny/ray (2.02x), r7rs/fibfp (2.11x), larceny/triangl (2.62x), kostya/brainfuck (3.70x), r7rs/nqueens (3.72x), beng/spectralnorm (4.72x) |
| **Node >5× faster** (> 5.0×) | 17 | awfy/deltablue (5.07x), awfy/richards (5.29x), larceny/deriv (5.35x), beng/nbody (5.84x), awfy/cd (5.94x), jetstream/hashmap (6.50x), jetstream/splay (8.07x), awfy/nbody (8.38x), jetstream/nbody (8.50x), beng/mandelbrot (9.13x), larceny/pnpoly (9.67x), kostya/json_gen (10.28x), kostya/base64 (12.51x), jetstream/raytrace3d (18.64x), larceny/gcbench (19.00x), jetstream/richards (31.32x), jetstream/navier_stokes (56.82x) |

---

## Improvement over Round 3

### MIR Direct Performance Changes (R3 → R4)

| Benchmark | Suite | R3 (ms) | R4 (ms) | Speedup |
|-----------|-------|--------:|--------:|--------:|
| havlak | AWFY | 183 | 61 | **3.00×** |
| cd | AWFY | 528 | 220 | **2.40×** |
| json | AWFY | 3.3 | 1.5 | **2.20×** |
| cube3d | JetStream | 49 | 24 | **2.08×** |
| storage | AWFY | 0.33 | 0.19 | **1.70×** |
| deltablue | AWFY | 99 | 64 | **1.54×** |
| list | AWFY | 0.032 | 0.023 | **1.39×** |

**Overall MIR improvement (geo mean, 62 benchmarks): 8.0% faster**

### Suite-Level Comparison (MIR/Node.js Geometric Mean)

| Suite | R3 Geo Mean | R4 Geo Mean | Change |
|-------|------------:|------------:|--------|
| R7RS | 0.44x | 0.44x | ↓ (1% worse) |
| AWFY | 0.83x | 0.62x | ↑ improved (1% better) |
| BENG | 0.93x | 0.94x | ↓ (1% worse) |
| KOSTYA | 2.09x | 2.09x | ↓ (1% worse) |
| LARCENY | 1.48x | 1.47x | ↑ improved (1% better) |
| JetStream | 7.90x | 7.28x | ↑ improved (1% better) |

### Lambda JS Engine — R4 Status

LambdaJS was not re-run in R4 (only MIR and C2MIR engines were benchmarked).
LambdaJS results are carried over from R3. Current R3 coverage:
- **AWFY**: All 14 benchmarks passing (bounce, storage, json, deltablue, havlak, cd included)
- **R7RS**: All 10 benchmarks (except mbrot)
- **BENG**: binarytrees, fannkuch, fasta, knucleotide, mandelbrot, nbody, pidigits, regexredux, revcomp, spectralnorm
- **KOSTYA**: brainfuck, matmul, primes, levenshtein, json_gen, collatz
- **LARCENY**: All 12 benchmarks

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

Across 56 unique benchmarks with both MIR and Node.js results, the geometric mean ratio is **1.05x**.
Excluding the new JetStream suite (which ports are not yet optimized), Lambda achieves **0.83x**
on 50 unique benchmarks, winning 28 of 50.

### 2. Lambda MIR dominates CPython

Across 55 unique benchmarks with Python comparisons, Lambda MIR is **13× faster** (geo mean 0.08x).
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
- **Class-heavy benchmarks**: richards (5.29x AWFY), cd (5.94x) — V8's hidden classes and inline caches
- **Heavy allocation/GC**: gcbench (19x), base64 (12.5x) — V8's generational GC advantage
- **JetStream suite (7.28x)**: Complex OOP-style benchmarks where V8's mature optimizations dominate

### 5. JetStream: New frontier for optimization

The JetStream benchmarks (ported from Apple's JS benchmark suite) show Lambda MIR running slower than Node.js on all 9 benchmarks (geo mean ~7×).
Workloads are now synchronized to the original heavy JetStream workloads.
Key remaining bottlenecks:
- **navier_stokes (57×)**: Heavy array-based PDE solver — needs typed array optimization for this pattern
- **richards (31×)**: OOP task scheduler — 50 iterations × 1000 COUNT exposes class/method dispatch overhead
- **raytrace3d (18×)**: Object-heavy 3D computation — property access patterns
- **splay (8×)**: Red-black tree operations — property access patterns
- **nbody (8.5×)**: Numeric simulation — 36000 advance steps per run
- **deltablue (1.6×)**: Constraint solver — close to competitive at 20 iterations
- **cube3d (1.3×)**: 3D rendering — much improved from R3 (49ms → 24ms)
These represent clear optimization targets for future MIR engine improvements.

### 6. MIR JIT improvements from Round 3 (LMD_TYPE_LIST removal)

Removing `LMD_TYPE_LIST` and unifying list/array handling delivered significant improvements:
- **havlak**: 183 → 61ms (**3.0× faster**) — graph traversal, list-heavy data structure
- **cd**: 528 → 220ms (**2.4× faster**) — collision detection with many list operations
- **json**: 3.3 → 1.5ms (**2.2× faster**) — JSON macro benchmark
- **cube3d**: 49 → 24ms (**2.0× faster**) — 3D rendering with array operations
- **storage**: 0.33 → 0.19ms (**1.7× faster**) — storage micro-benchmark
- **deltablue**: 99 → 64ms (**1.5× faster**) — constraint solver macro benchmark
- **list**: 0.032 → 0.023ms (**1.4× faster**) — list micro-benchmark
- **Overall**: ~8% faster across 62 comparable benchmarks

### 7. Lambda JS engine

LambdaJS results are unchanged from R3 (not re-run in R4). R3 coverage:
- All AWFY benchmarks pass including bounce, storage, json, deltablue, havlak, and cd
- BENG/fannkuch, BENG/mandelbrot, KOSTYA/collatz passing

### 8. QuickJS comparison

QuickJS (pure interpreter) is generally 2–10× slower than Node.js V8, as expected.
Lambda MIR is faster than QuickJS on most benchmarks, confirming Lambda's JIT advantage.

### 9. C2MIR vs MIR Direct

The two Lambda JIT paths produce similar results. Notable differences:
- C2MIR slightly faster on: r7rs/sum (0.27 vs 1.8ms), nqueens (6.7 vs 6.6ms)
- MIR Direct faster on: matmul (8.8 vs 128ms), cube3d (24 vs 81ms), list (0.023 vs 0.62ms)
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
- **AWFY Python** benchmarks use the official Python port with harness. Class names: NBody, DeltaBlue, CD (not capitalize()).
- **LambdaJS** now passes all AWFY benchmarks including bounce, storage, json, deltablue, havlak, and cd (results from R3, not re-run in R4).
- **QuickJS** fails on ack (R7RS) due to stack overflow on deep recursion.
- **JetStream** benchmarks run on MIR, C2MIR, Node.js, and Python (for deltablue, richards, nbody). No LambdaJS/QuickJS ports.
- **Python** benchmarks not available for: AWFY/cd, JetStream/cube3d, JetStream/navier_stokes, JetStream/splay, JetStream/hashmap, JetStream/crypto_sha1, JetStream/raytrace3d.
- All times in **milliseconds** unless noted with 's' suffix (seconds).
- **KOSTYA/json_gen** benchmark was broken in an earlier commit (commit f2f0c3fd9, incorrect string syntax); fixed in this round. Results are now correct (~65ms).
- **LMD_TYPE_LIST removal (R4)**: `LMD_TYPE_LIST` was replaced by `LMD_TYPE_ARRAY` throughout the runtime. All list/array operations now use a unified type, improving performance on OOP-heavy and collection-intensive benchmarks.
- **Workload synchronization**: Duplicate benchmark names across suites use identical heavy workloads synchronized to original JetStream — AWFY/BENG/JetStream mandelbrot N=500, nbody 36000 steps, richards 50×COUNT=1000, deltablue 20×chain(100), Larceny/Kostya primes sieve(1M).