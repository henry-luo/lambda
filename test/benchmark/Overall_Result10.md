# Lambda Benchmark Results: Round 10

- **Date:** 2026-07-22
- **Platform:** Darwin arm64
- **Lambda commit:** `e3b6f358cf0e8fd75d2cefc00c219bfa24779638`
- **Lambda build:** clean release build (`make release`)
- **Instrumentation check:** passed
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Engines in this report:** MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v10.json`

JetStream JavaScript-engine wrappers are standardized to an explicit x8 loop over the detected benchmark function. They do not use per-file `Benchmark.runIteration()` counts, because those counts drift across JetStream files.

---

## Summary

| Suite | Total | Timed MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 9 | 10 | 9 | 10 | 1.98x | 7.35x | 6.60x |
| AWFY | 14 | 11 | 13 | 14 | 14 | 3.03x | 37.1x | 5.49x |
| BENG | 10 | 8 | 10 | 7 | 10 | 4.23x | 8.40x | 4.51x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 28.3x | 16.3x | 11.7x |
| LARCENY | 12 | 10 | 12 | 12 | 12 | 22.4x | 20.0x | 14.2x |
| JetStream | 9 | 5 | 9 | 7 | 9 | 55.3x | 99.0x | 13.7x |
| **Overall dedup** | **56** | **45** | **55** | **50** | **56** | **7.68x** | **20.1x** | **7.74x** |
| Overall raw | 62 | 50 | 61 | 56 | 62 | 8.08x | 20.8x | 8.32x |

> **Overall dedup** is the default headline metric: duplicate benchmark names across suites are counted once, using the best timed value per engine. **Overall raw** keeps the row-weighted value for auditability.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Historical Comparison

### R0 baseline status — read this before using the numbers above

Result10 re-measures current master on the same host, Node.js v22.13.0 and QuickJS 2025-09-13, with the same clean release build, 3-run median `__TIMING__` protocol and 180 s timeout as Result9. It is the R0 deliverable of `vibe/Lambda_Tuning_Proposal.md`.

**The Lambda/MIR column is not a usable baseline.** 12 benchmarks that ran in Result9 no longer run: 10 abort at transpile time with `mir-scalar-invariant: unresolved call retains scalar home` (`lambda/mir_emitter_shared.hpp`, `em_emit_unknown_call`), and 2 are rejected by the front end. The abort was introduced by commit `e30dc677b` ("impl scalar GC invariant"), which replaced the previous `em_heap_rehome_item_arg()` fallback for unresolved calls with a hard `abort()`. Because the failing rows drop out of the geometric mean, the MIR/Node figures in this report compare different benchmark populations and must not be quoted as a like-for-like movement.

**The LambdaJS column is usable but bimodal**, and its headline moved for two separable reasons: a real broad regression on the small/mid cluster, and a coverage change (`cd` and `hashmap` are timed for the first time in Result10, both above 2000x, which raises the mean by construction). The like-for-like figures below hold the benchmark population fixed.

**Headline, all-timed vs like-for-like (deduplicated, engine/Node geometric mean)**

| Engine | Result9 all-timed | Result10 all-timed | Result9 like-for-like | Result10 like-for-like | Common rows | Like-for-like change |
|---|---|---|---|---|---|---|
| LambdaJS | 13.1x (n=53) | 19.9x (n=55) | 13.1x | 16.7x | 53 | 1.27x worse |
| Lambda/MIR | 4.26x (n=56) | 7.39x (n=45) | 3.83x | 7.39x | 45 | 1.93x worse |

### Lambda/MIR — benchmarks lost since Result9

All 10 SIGABRT rows share one message and one origin. Reproduce with `./lambda.exe run test/benchmark/jetstream/deltablue2.ls` (exit 134). `r7rs/fft` is rejected by the type checker (`cannot assign float value to var 'm' of type int`) and `beng/pidigits` by the parser (`Unexpected syntax near '1'`) — three distinct regression classes, not one.

**Regressed from timed to failing**

| Benchmark | Result9 MIR | Result10 status | Cause |
|---|---|---|---|
| r7rs/fft | 0.447 ms | exit_1 | compile rejected |
| awfy/list | 0.2 ms | exit_-6 | SIGABRT — mir-scalar-invariant |
| awfy/havlak | 221 ms | exit_-6 | SIGABRT — mir-scalar-invariant |
| awfy/cd | 1.53e+03 ms | exit_-6 | SIGABRT — mir-scalar-invariant |
| beng/binarytrees | 44.4 ms | exit_-6 | SIGABRT — mir-scalar-invariant |
| beng/pidigits | 0.325 ms | exit_1 | compile rejected |
| larceny/deriv | 72.9 ms | exit_-6 | SIGABRT — mir-scalar-invariant |
| larceny/gcbench | 1.37e+03 ms | exit_-6 | SIGABRT — mir-scalar-invariant |
| jetstream/splay | 3.17e+03 ms | exit_-6 | SIGABRT — mir-scalar-invariant |
| jetstream/deltablue | 40 ms | exit_-6 | SIGABRT — mir-scalar-invariant |
| jetstream/hashmap | 246 ms | exit_-6 | SIGABRT — mir-scalar-invariant |
| jetstream/raytrace3d | 471 ms | exit_-6 | SIGABRT — mir-scalar-invariant |

### LambdaJS vs Result9 — where it moved

The disaster tail that defined Result9's geometric mean has largely collapsed, which is the expected signature of the landed inline self-tagged doubles, side-number-stack scalars and safepoint rooting. The offsetting regression is concentrated in the previously-fast small and mid benchmarks. These are not measurement artifacts: Node.js's own times for the same rows moved by under 10% between the two rounds, so the host is consistent and the movement is in the engine. In absolute terms `awfy/sieve` went 0.49 ms to 50.1 ms (102x slower), `larceny/puzzle` 22.7 ms to 769 ms (34x), `larceny/array1` 3.40 ms to 28.2 ms (8.3x) and `primes` 15.9 ms to 105 ms (6.6x). The shape — previously-near-parity numeric/array loops losing an order of magnitude while the boxing-bound tail improves — is consistent with lost native specialization on these functions, and is the first thing to investigate before any further tuning work is ranked.

Across the 53 benchmarks timed in both rounds, 20 improved and 33 regressed.

**Largest improvements (LambdaJS/Node ratio)**

| Benchmark | Result9 | Result10 | Change |
|---|---|---|---|
| splay | 392x | 14.6x | 26.81x better |
| deltablue | 266x | 19.0x | 13.99x better |
| list | 22.1x | 5.33x | 4.15x better |
| cube3d | 198x | 50.4x | 3.93x better |
| mbrot | 16.1x | 4.95x | 3.24x better |
| matmul | 176x | 54.9x | 3.21x better |
| richards | 119x | 37.5x | 3.17x better |
| collatz | 4.67x | 1.58x | 2.96x better |
| mandelbrot | 11.6x | 4.04x | 2.87x better |
| bounce | 18.2x | 6.36x | 2.86x better |

**Largest regressions (LambdaJS/Node ratio)**

| Benchmark | Result9 | Result10 | Change |
|---|---|---|---|
| sieve | 1.21x | 133x | 110.03x worse |
| puzzle | 6.62x | 237x | 35.87x worse |
| array1 | 1.71x | 15.3x | 8.96x worse |
| primes | 3.31x | 23.9x | 7.21x worse |
| navier_stokes | 155x | 1005x | 6.49x worse |
| fannkuch | 0.49x | 3.03x | 6.15x worse |
| fasta | 1.25x | 5.85x | 4.66x worse |
| nqueens | 2.17x | 9.57x | 4.40x worse |
| diviter | 5.39x | 21.3x | 3.94x worse |
| crypto_sha1 | 31.5x | 123x | 3.90x worse |

**Per-suite LambdaJS/Node geometric mean**

| Suite | Result9 | Result10 | Change |
|---|---|---|---|
| R7RS | 5.21x | 7.35x | 1.41x worse |
| AWFY | 36.6x | 37.1x | 1.02x worse |
| BENG | 6.18x | 8.40x | 1.36x worse |
| KOSTYA | 14.6x | 16.3x | 1.11x worse |
| LARCENY | 8.24x | 20.0x | 2.42x worse |
| JetStream | 148x | 99.0x | 1.49x better |

---

## Notable Results

- Missing timings: **19** cells
- MIR missing: r7rs/fft (exit_1), awfy/list (exit_-6), awfy/havlak (exit_-6), awfy/cd (exit_-6), beng/binarytrees (exit_-6), beng/pidigits (exit_1), larceny/deriv (exit_-6), larceny/gcbench (exit_-6), +4 more
- QuickJS missing: r7rs/ack (exit_1), beng/knucleotide (exit_1), beng/regexredux (exit_1), beng/revcomp (exit_1), jetstream/cube3d (exit_1), jetstream/raytrace3d (exit_1)
- LambdaJS missing: awfy/havlak (timeout)
- Deduplicated benchmark names: mandelbrot (awfy/beng), nbody (awfy/beng/jetstream), richards (awfy/jetstream), deltablue (awfy/jetstream), primes (kostya/larceny)

### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/cd | 87.38s | 36.4 | 2401x |
| jetstream/hashmap | 114.97s | 54.2 | 2121x |
| jetstream/navier_stokes | 37.46s | 37.3 | 1005x |
| larceny/puzzle | 768.9 | 3.24 | 237x |
| awfy/sieve | 50.1 | 0.376 | 133x |
| jetstream/crypto_sha1 | 856.5 | 6.96 | 123x |
| beng/spectralnorm | 276.0 | 2.52 | 110x |
| awfy/permute | 73.0 | 0.812 | 89.8x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.350 | 1.95 | 0.18x |

---

## R7RS

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 12.5 | 41.1 | 19.2 | 1.85 | 6.73x | 22.2x | 10.4x |
| fibfp | recursive | 5.36 | 39.9 | 18.8 | 1.81 | 2.95x | 22.0x | 10.4x |
| tak | recursive | 0.900 | 3.38 | 2.77 | 0.857 | 1.05x | 3.95x | 3.23x |
| cpstak | closure | 1.75 | 6.87 | 5.57 | 0.964 | 1.82x | 7.13x | 5.78x |
| sum | iterative | 19.6 | 12.5 | 32.1 | 1.19 | 16.5x | 10.5x | 27.1x |
| sumfp | iterative | 0.067 | 1.21 | 3.79 | 0.920 | 0.07x | 1.32x | 4.12x |
| nqueens | backtrack | 4.08 | 16.9 | 9.98 | 1.77 | 2.31x | 9.57x | 5.65x |
| fft | numeric | --- | 9.44 | 2.95 | 1.63 | --- | 5.78x | 1.81x |
| mbrot | numeric | 2.00 | 8.84 | 18.6 | 1.79 | 1.12x | 4.95x | 10.4x |
| ack | recursive | 55.8 | 121.7 | --- | 13.8 | 4.04x | 8.81x | --- |

## AWFY

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.552 | 50.1 | 0.627 | 0.376 | 1.47x | 133x | 1.67x |
| permute | micro | 0.875 | 73.0 | 1.64 | 0.812 | 1.08x | 89.8x | 2.02x |
| queens | micro | 0.702 | 32.5 | 1.11 | 0.639 | 1.10x | 50.8x | 1.73x |
| towers | micro | 1.45 | 72.3 | 2.31 | 1.11 | 1.30x | 65.1x | 2.08x |
| bounce | micro | 0.956 | 3.57 | 0.894 | 0.561 | 1.70x | 6.36x | 1.59x |
| list | micro | --- | 2.61 | 0.934 | 0.489 | --- | 5.33x | 1.91x |
| storage | micro | 1.07 | 14.0 | 2.64 | 0.629 | 1.70x | 22.2x | 4.20x |
| mandelbrot | compute | 94.3 | 348.0 | 912.6 | 32.4 | 2.91x | 10.7x | 28.2x |
| nbody | compute | 198.9 | 286.0 | 166.4 | 6.14 | 32.4x | 46.6x | 27.1x |
| richards | macro | 361.8 | 1.25s | 193.5 | 47.3 | 7.65x | 26.5x | 4.09x |
| json | macro | 11.9 | 38.1 | 11.6 | 2.67 | 4.44x | 14.3x | 4.32x |
| deltablue | macro | 113.9 | 226.1 | 111.5 | 11.9 | 9.57x | 19.0x | 9.36x |
| havlak | macro | --- | --- | 3.98s | 98.8 | --- | --- | 40.3x |
| cd | macro | --- | 87.38s | 1.04s | 36.4 | --- | 2401x | 28.6x |

## BENG

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | --- | 41.3 | 27.5 | 4.00 | --- | 10.3x | 6.88x |
| fannkuch | permutation | 4.69 | 12.1 | 7.09 | 4.00 | 1.17x | 3.03x | 1.77x |
| fasta | generation | 138.3 | 36.3 | 10.6 | 6.21 | 22.3x | 5.85x | 1.71x |
| knucleotide | hashing | 16.1 | 144.6 | --- | 4.97 | 3.24x | 29.1x | --- |
| mandelbrot | numeric | 191.5 | 60.7 | 675.0 | 15.0 | 12.7x | 4.04x | 44.9x |
| nbody | numeric | 187.7 | 350.3 | 150.0 | 7.40 | 25.4x | 47.3x | 20.3x |
| pidigits | bignum | --- | 0.350 | 0.156 | 1.95 | --- | 0.18x | 0.08x |
| regexredux | regex | 1.33 | 14.8 | --- | 2.45 | 0.54x | 6.02x | --- |
| revcomp | string | 1.17 | 48.5 | --- | 3.33 | 0.35x | 14.6x | --- |
| spectralnorm | numeric | 49.7 | 276.0 | 62.6 | 2.52 | 19.7x | 110x | 24.8x |

## KOSTYA

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 702.3 | 1.05s | 882.7 | 43.6 | 16.1x | 24.1x | 20.3x |
| matmul | numeric | 610.5 | 846.7 | 541.0 | 15.4 | 39.6x | 54.9x | 35.1x |
| primes | numeric | 78.1 | 105.6 | 94.5 | 4.45 | 17.6x | 23.7x | 21.2x |
| base64 | string | 14.42s | 843.4 | 179.9 | 17.5 | 823x | 48.1x | 10.3x |
| levenshtein | string | 61.0 | 86.4 | 54.5 | 4.23 | 14.4x | 20.4x | 12.9x |
| json_gen | data | 129.1 | 38.4 | 20.9 | 6.24 | 20.7x | 6.15x | 3.34x |
| collatz | numeric | 7.47s | 2.24s | 6.23s | 1.42s | 5.27x | 1.58x | 4.40x |

## LARCENY

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 2.31s | 3.64s | 2.18s | 66.7 | 34.7x | 54.6x | 32.6x |
| array1 | array | 25.8 | 28.2 | 35.9 | 1.84 | 14.0x | 15.3x | 19.5x |
| deriv | symbolic | --- | 79.8 | 67.5 | 3.80 | --- | 21.0x | 17.8x |
| diviter | iterative | 23.34s | 9.99s | 26.65s | 469.5 | 49.7x | 21.3x | 56.8x |
| divrec | recursive | 24.1 | 41.9 | 35.6 | 7.61 | 3.16x | 5.50x | 4.68x |
| gcbench | allocation | --- | 972.9 | 645.8 | 23.8 | --- | 41.0x | 27.2x |
| paraffins | combinat | 2.62 | 2.32 | 2.56 | 0.994 | 2.64x | 2.33x | 2.58x |
| pnpoly | numeric | 11.00s | 172.1 | 201.8 | 5.83 | 1886x | 29.5x | 34.6x |
| primes | iterative | 77.9 | 105.2 | 94.9 | 4.41 | 17.7x | 23.9x | 21.5x |
| puzzle | search | 26.9 | 768.9 | 29.2 | 3.24 | 8.31x | 237x | 9.00x |
| quicksort | sorting | 16.1 | 47.1 | 19.2 | 1.64 | 9.81x | 28.7x | 11.7x |
| ray | numeric | 209.5 | 14.9 | 13.8 | 3.54 | 59.2x | 4.22x | 3.89x |

## JetStream

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| nbody | numeric | 188.6 | 219.6 | 132.7 | 5.43 | 34.7x | 40.4x | 24.4x |
| cube3d | 3d | 256.8 | 898.8 | --- | 17.8 | 14.4x | 50.4x | --- |
| navier_stokes | numeric | 14.93s | 37.46s | 781.0 | 37.3 | 401x | 1005x | 21.0x |
| richards | macro | 339.0 | 175.8 | 25.4 | 4.69 | 72.2x | 37.5x | 5.42x |
| splay | data | --- | 49.3 | 32.1 | 3.37 | --- | 14.6x | 9.53x |
| deltablue | macro | --- | 339.9 | 45.7 | 6.35 | --- | 53.5x | 7.20x |
| hashmap | data | --- | 114.97s | 2.57s | 54.2 | --- | 2121x | 47.4x |
| crypto_sha1 | crypto | 248.0 | 856.5 | 68.9 | 6.96 | 35.6x | 123x | 9.90x |
| raytrace3d | 3d | --- | 1.07s | --- | 18.3 | --- | 58.4x | --- |
