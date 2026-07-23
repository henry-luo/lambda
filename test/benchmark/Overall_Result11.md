# Lambda Benchmark Results: Round 11 (Tune-COW)

- **Date:** 2026-07-23
- **Platform:** Darwin arm64
- **Lambda commit:** `1704c2f43126e41957bdb3470b8111f5c1201cc9`
- **Lambda build:** clean release build (`make release`)
- **Instrumentation check:** passed
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Engines in this report:** MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v11.json`

JetStream JavaScript-engine wrappers are standardized to an explicit x8 loop over the detected benchmark function. They do not use per-file `Benchmark.runIteration()` counts, because those counts drift across JetStream files.

---

## Summary

| Suite | Total | Timed MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 9 | 10 | 9 | 10 | 1.06x | 7.05x | 6.19x |
| AWFY | 14 | 13 | 13 | 14 | 14 | 2.73x | 35.6x | 5.08x |
| BENG | 10 | 9 | 10 | 7 | 10 | 2.62x | 8.25x | 4.22x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 23.3x | 17.1x | 11.9x |
| LARCENY | 12 | 12 | 12 | 12 | 12 | 10.0x | 19.2x | 13.0x |
| JetStream | 9 | 9 | 9 | 7 | 9 | 16.8x | 87.7x | 10.8x |
| **Overall dedup** | **56** | **53** | **55** | **50** | **56** | **4.73x** | **19.3x** | **7.10x** |
| Overall raw | 62 | 59 | 61 | 56 | 62 | 5.20x | 20.0x | 7.67x |

> **Overall dedup** is the default headline metric: duplicate benchmark names across suites are counted once, using the best timed value per engine. **Overall raw** keeps the row-weighted value for auditability.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Historical Comparison

### Matched Result10 comparison

Result10 and Result11 were both measured on this Darwin arm64 host with clean release builds, Node.js v22.13.0, QuickJS 2025-09-13, the standard four-engine matrix, three-run medians, and a 180-second timeout.

The like-for-like comparison holds the Result10 deduplicated population fixed. Result11 has one additional correct MIR row (`awfy/cd`), so its 53-row headline is reported separately.

**Deduplicated engine/Node geometric means on the 52 Result10 rows timed by both runs**

| Engine | Result10 | Result11 | Change |
|---|---|---|---|
| Lambda/MIR | 8.60x | 4.62x | 1.86x better |
| LambdaJS | 20.1x | 19.3x | 1.05x better |
| QuickJS | 7.74x | 7.10x | 1.09x better |

---

## Notable Results

- Missing timings: **10** cells
- MIR missing: r7rs/fft (exit_1), awfy/list (wrong_output), beng/pidigits (exit_1)
- QuickJS missing: r7rs/ack (exit_1), beng/knucleotide (exit_1), beng/regexredux (exit_1), beng/revcomp (exit_1), jetstream/cube3d (exit_1), jetstream/raytrace3d (exit_1)
- LambdaJS missing: awfy/havlak (timeout)
- Deduplicated benchmark names: mandelbrot (awfy/beng), nbody (awfy/beng/jetstream), richards (awfy/jetstream), deltablue (awfy/jetstream), primes (kostya/larceny)

### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| jetstream/hashmap | 143.77s | 55.1 | 2609x |
| awfy/cd | 87.30s | 37.0 | 2360x |
| jetstream/navier_stokes | 39.67s | 44.8 | 885x |
| larceny/puzzle | 845.1 | 3.37 | 250x |
| awfy/sieve | 50.0 | 0.395 | 127x |
| jetstream/crypto_sha1 | 861.3 | 7.03 | 122x |
| beng/spectralnorm | 276.6 | 2.53 | 109x |
| awfy/permute | 72.0 | 0.829 | 86.9x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.314 | 1.95 | 0.16x |

---

## R7RS

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 6.72 | 40.5 | 19.0 | 1.85 | 3.63x | 21.9x | 10.3x |
| fibfp | recursive | 5.24 | 40.1 | 19.0 | 1.79 | 2.93x | 22.4x | 10.6x |
| tak | recursive | 0.514 | 3.40 | 2.82 | 0.809 | 0.64x | 4.20x | 3.49x |
| cpstak | closure | 1.02 | 6.70 | 5.68 | 1.01 | 1.00x | 6.61x | 5.60x |
| sum | iterative | 4.03 | 11.9 | 31.5 | 1.26 | 3.20x | 9.44x | 25.0x |
| sumfp | iterative | 0.068 | 1.20 | 3.75 | 1.08 | 0.06x | 1.11x | 3.47x |
| nqueens | backtrack | 3.07 | 16.5 | 8.05 | 1.80 | 1.71x | 9.21x | 4.48x |
| fft | numeric | --- | 9.52 | 2.77 | 1.64 | --- | 5.82x | 1.70x |
| mbrot | numeric | 0.804 | 8.50 | 17.9 | 1.88 | 0.43x | 4.52x | 9.52x |
| ack | recursive | 22.4 | 119.2 | --- | 13.6 | 1.64x | 8.75x | --- |

## AWFY

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.550 | 50.0 | 0.621 | 0.395 | 1.39x | 127x | 1.57x |
| permute | micro | 0.784 | 72.0 | 1.57 | 0.829 | 0.95x | 86.9x | 1.90x |
| queens | micro | 0.588 | 32.5 | 1.06 | 0.655 | 0.90x | 49.7x | 1.62x |
| towers | micro | 1.16 | 71.1 | 2.28 | 1.17 | 0.98x | 60.6x | 1.94x |
| bounce | micro | 0.930 | 3.58 | 0.893 | 0.560 | 1.66x | 6.39x | 1.59x |
| list | micro | --- | 2.58 | 0.936 | 0.502 | --- | 5.15x | 1.87x |
| storage | micro | 0.971 | 13.6 | 2.21 | 0.644 | 1.51x | 21.1x | 3.43x |
| mandelbrot | compute | 46.2 | 349.1 | 886.0 | 31.8 | 1.45x | 11.0x | 27.8x |
| nbody | compute | 187.0 | 278.1 | 162.4 | 5.74 | 32.6x | 48.5x | 28.3x |
| richards | macro | 351.0 | 1.21s | 194.7 | 48.5 | 7.23x | 24.9x | 4.01x |
| json | macro | 11.5 | 37.8 | 11.5 | 2.87 | 4.01x | 13.2x | 4.02x |
| deltablue | macro | 107.9 | 215.1 | 101.7 | 13.8 | 7.79x | 15.5x | 7.35x |
| havlak | macro | 104.0 | --- | 3.51s | 107.7 | 0.97x | --- | 32.6x |
| cd | macro | 562.7 | 87.30s | 978.9 | 37.0 | 15.2x | 2360x | 26.5x |

## BENG

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 12.3 | 41.0 | 24.0 | 4.21 | 2.93x | 9.73x | 5.70x |
| fannkuch | permutation | 3.78 | 12.2 | 7.24 | 4.08 | 0.93x | 3.00x | 1.78x |
| fasta | generation | 7.27 | 37.1 | 8.94 | 6.01 | 1.21x | 6.16x | 1.49x |
| knucleotide | hashing | 13.5 | 150.5 | --- | 5.26 | 2.57x | 28.6x | --- |
| mandelbrot | numeric | 133.7 | 61.6 | 689.1 | 15.4 | 8.71x | 4.01x | 44.9x |
| nbody | numeric | 183.9 | 354.4 | 151.3 | 7.65 | 24.0x | 46.3x | 19.8x |
| pidigits | bignum | --- | 0.314 | 0.133 | 1.95 | --- | 0.16x | 0.07x |
| regexredux | regex | 1.30 | 14.9 | --- | 2.51 | 0.52x | 5.95x | --- |
| revcomp | string | 1.16 | 49.5 | --- | 3.38 | 0.34x | 14.6x | --- |
| spectralnorm | numeric | 47.3 | 276.6 | 65.7 | 2.53 | 18.7x | 109x | 25.9x |

## KOSTYA

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 620.0 | 1.08s | 897.5 | 34.8 | 17.8x | 31.0x | 25.8x |
| matmul | numeric | 428.7 | 867.8 | 548.2 | 15.8 | 27.2x | 55.0x | 34.7x |
| primes | numeric | 70.9 | 107.0 | 96.0 | 4.46 | 15.9x | 24.0x | 21.5x |
| base64 | string | 10.81s | 858.6 | 160.4 | 17.5 | 617x | 49.0x | 9.15x |
| levenshtein | string | 45.9 | 88.5 | 55.1 | 4.05 | 11.3x | 21.9x | 13.6x |
| json_gen | data | 109.6 | 38.4 | 20.2 | 6.34 | 17.3x | 6.07x | 3.19x |
| collatz | numeric | 5.77s | 2.32s | 6.37s | 1.44s | 4.01x | 1.61x | 4.44x |

## LARCENY

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 2.30s | 3.74s | 2.23s | 67.7 | 34.0x | 55.2x | 32.9x |
| array1 | array | 19.3 | 29.7 | 36.7 | 1.94 | 9.96x | 15.3x | 18.9x |
| deriv | symbolic | 99.9 | 81.4 | 60.7 | 3.76 | 26.6x | 21.7x | 16.2x |
| diviter | iterative | 19.61s | 10.74s | 27.23s | 482.9 | 40.6x | 22.2x | 56.4x |
| divrec | recursive | 20.5 | 42.8 | 40.5 | 8.30 | 2.47x | 5.16x | 4.88x |
| gcbench | allocation | 452.8 | 1.02s | 624.8 | 27.0 | 16.8x | 37.6x | 23.1x |
| paraffins | combinat | 2.25 | 2.39 | 2.54 | 1.80 | 1.25x | 1.33x | 1.41x |
| pnpoly | numeric | 147.8 | 186.4 | 211.9 | 6.19 | 23.9x | 30.1x | 34.2x |
| primes | iterative | 72.1 | 120.4 | 97.3 | 4.84 | 14.9x | 24.9x | 20.1x |
| puzzle | search | 19.8 | 845.1 | 30.4 | 3.37 | 5.88x | 250x | 9.00x |
| quicksort | sorting | 13.2 | 53.5 | 19.6 | 1.68 | 7.83x | 31.8x | 11.7x |
| ray | numeric | 13.2 | 15.7 | 14.1 | 3.85 | 3.42x | 4.09x | 3.65x |

## JetStream

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| nbody | numeric | 192.7 | 236.6 | 134.7 | 5.63 | 34.2x | 42.0x | 23.9x |
| cube3d | 3d | 78.9 | 939.8 | --- | 18.9 | 4.17x | 49.7x | --- |
| navier_stokes | numeric | 1.94s | 39.67s | 907.9 | 44.8 | 43.3x | 885x | 20.2x |
| richards | macro | 394.0 | 214.3 | 32.0 | 7.25 | 54.3x | 29.6x | 4.42x |
| splay | data | 479.9 | 64.3 | 30.1 | 10.1 | 47.8x | 6.40x | 2.99x |
| deltablue | macro | 23.8 | 414.9 | 52.0 | 8.73 | 2.72x | 47.5x | 5.95x |
| hashmap | data | 206.1 | 143.77s | 2.50s | 55.1 | 3.74x | 2609x | 45.4x |
| crypto_sha1 | crypto | 224.5 | 861.3 | 70.3 | 7.03 | 31.9x | 122x | 9.99x |
| raytrace3d | 3d | 367.8 | 1.07s | --- | 18.4 | 20.0x | 58.0x | --- |
