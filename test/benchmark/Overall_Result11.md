# Lambda Benchmark Results: Round 11 (Tune-COW)

- **Date:** 2026-07-24
- **Platform:** Darwin arm64
- **Lambda commit:** `52c0f3c027893891799ebefeb91a0d05f8490d80`
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
| R7RS | 10 | 9 | 10 | 9 | 10 | 1.01x | 7.30x | 6.19x |
| AWFY | 14 | 13 | 13 | 14 | 14 | 2.65x | 36.2x | 5.08x |
| BENG | 10 | 9 | 10 | 7 | 10 | 2.12x | 8.48x | 4.22x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 17.2x | 17.1x | 11.9x |
| LARCENY | 12 | 12 | 12 | 12 | 12 | 8.53x | 26.5x | 13.0x |
| JetStream | 9 | 9 | 9 | 7 | 9 | 20.4x | 91.9x | 10.8x |
| **Overall dedup** | **56** | **53** | **55** | **50** | **56** | **4.28x** | **21.1x** | **7.10x** |
| Overall raw | 62 | 59 | 61 | 56 | 62 | 4.78x | 21.7x | 7.67x |

> **Overall dedup** is the default headline metric: duplicate benchmark names across suites are counted once, using the best timed value per engine. **Overall raw** keeps the row-weighted value for auditability.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Historical Comparison

### Matched Result10 comparison

Result10 and the retained Result11 rows use this Darwin arm64 host, clean release builds, Node.js v22.13.0, QuickJS 2025-09-13, the standard four-engine matrix, three-run medians, and a 180-second timeout. This refresh reran MIR and LambdaJS on the current release build with the machine on AC power; QuickJS and Node.js rows remain from the previous Result11 snapshot.

The comparison uses each engine's deduplicated Result10/Result11 common rows; the headline summary still reports the current overall population separately.

**Deduplicated engine/Node geometric means on each engine's common Result10/Result11 rows**

| Engine | Result10 | Result11 | Common rows | Change |
|---|---|---|---|---|
| Lambda/MIR | 8.60x | 4.18x | 52 | 2.06x better |
| LambdaJS | 18.5x | 21.1x | 55 | 1.05x worse |
| QuickJS | 7.74x | 7.10x | 50 | 1.09x better |

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
| awfy/cd | 90.76s | 37.0 | 2454x |
| jetstream/hashmap | 132.83s | 55.1 | 2410x |
| jetstream/navier_stokes | 43.24s | 44.8 | 964x |
| larceny/puzzle | 1.23s | 3.37 | 363x |
| jetstream/crypto_sha1 | 954.8 | 7.03 | 136x |
| awfy/sieve | 52.1 | 0.395 | 132x |
| beng/spectralnorm | 292.2 | 2.53 | 115x |
| awfy/permute | 75.2 | 0.829 | 90.8x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.320 | 1.95 | 0.16x |

---

## R7RS

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 6.72 | 41.0 | 19.0 | 1.85 | 3.63x | 22.1x | 10.3x |
| fibfp | recursive | 5.49 | 41.1 | 19.0 | 1.79 | 3.06x | 22.9x | 10.6x |
| tak | recursive | 0.522 | 3.42 | 2.82 | 0.809 | 0.65x | 4.23x | 3.49x |
| cpstak | closure | 1.04 | 6.81 | 5.68 | 1.01 | 1.03x | 6.72x | 5.60x |
| sum | iterative | 4.13 | 12.3 | 31.5 | 1.26 | 3.28x | 9.78x | 25.0x |
| sumfp | iterative | 0.068 | 1.22 | 3.75 | 1.08 | 0.06x | 1.13x | 3.47x |
| nqueens | backtrack | 1.69 | 17.9 | 8.05 | 1.80 | 0.94x | 9.96x | 4.48x |
| fft | numeric | --- | 11.3 | 2.77 | 1.64 | --- | 6.89x | 1.70x |
| mbrot | numeric | 0.821 | 8.42 | 17.9 | 1.88 | 0.44x | 4.48x | 9.52x |
| ack | recursive | 23.5 | 120.3 | --- | 13.6 | 1.72x | 8.83x | --- |

## AWFY

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.545 | 52.1 | 0.621 | 0.395 | 1.38x | 132x | 1.57x |
| permute | micro | 0.780 | 75.2 | 1.57 | 0.829 | 0.94x | 90.8x | 1.90x |
| queens | micro | 0.565 | 33.1 | 1.06 | 0.655 | 0.86x | 50.5x | 1.62x |
| towers | micro | 1.19 | 73.9 | 2.28 | 1.17 | 1.01x | 62.9x | 1.94x |
| bounce | micro | 0.853 | 3.60 | 0.893 | 0.560 | 1.52x | 6.43x | 1.59x |
| list | micro | --- | 2.60 | 0.936 | 0.502 | --- | 5.17x | 1.87x |
| storage | micro | 0.783 | 13.7 | 2.21 | 0.644 | 1.22x | 21.3x | 3.43x |
| mandelbrot | compute | 46.3 | 345.7 | 886.0 | 31.8 | 1.46x | 10.9x | 27.8x |
| nbody | compute | 168.3 | 279.5 | 162.4 | 5.74 | 29.3x | 48.7x | 28.3x |
| richards | macro | 331.9 | 1.25s | 194.7 | 48.5 | 6.84x | 25.7x | 4.01x |
| json | macro | 10.6 | 39.5 | 11.5 | 2.87 | 3.71x | 13.8x | 4.02x |
| deltablue | macro | 103.6 | 204.6 | 101.7 | 13.8 | 7.48x | 14.8x | 7.35x |
| havlak | macro | 144.0 | --- | 3.51s | 107.7 | 1.34x | --- | 32.6x |
| cd | macro | 513.0 | 90.76s | 978.9 | 37.0 | 13.9x | 2454x | 26.5x |

## BENG

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 12.3 | 42.0 | 24.0 | 4.21 | 2.92x | 9.97x | 5.70x |
| fannkuch | permutation | 0.699 | 12.9 | 7.24 | 4.08 | 0.17x | 3.16x | 1.78x |
| fasta | generation | 7.20 | 37.6 | 8.94 | 6.01 | 1.20x | 6.25x | 1.49x |
| knucleotide | hashing | 11.9 | 151.5 | --- | 5.26 | 2.26x | 28.8x | --- |
| mandelbrot | numeric | 135.3 | 61.5 | 689.1 | 15.4 | 8.82x | 4.00x | 44.9x |
| nbody | numeric | 168.9 | 374.6 | 151.3 | 7.65 | 22.1x | 49.0x | 19.8x |
| pidigits | bignum | --- | 0.320 | 0.133 | 1.95 | --- | 0.16x | 0.07x |
| regexredux | regex | 1.33 | 15.3 | --- | 2.51 | 0.53x | 6.10x | --- |
| revcomp | string | 1.16 | 50.8 | --- | 3.38 | 0.34x | 15.0x | --- |
| spectralnorm | numeric | 46.3 | 292.2 | 65.7 | 2.53 | 18.3x | 115x | 25.9x |

## KOSTYA

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 456.2 | 1.09s | 897.5 | 34.8 | 13.1x | 31.3x | 25.8x |
| matmul | numeric | 426.2 | 917.2 | 548.2 | 15.8 | 27.0x | 58.1x | 34.7x |
| primes | numeric | 58.5 | 109.2 | 96.0 | 4.46 | 13.1x | 24.5x | 21.5x |
| base64 | string | 11.63s | 765.3 | 160.4 | 17.5 | 664x | 43.7x | 9.15x |
| levenshtein | string | 28.2 | 93.5 | 55.1 | 4.05 | 6.97x | 23.1x | 13.6x |
| json_gen | data | 109.2 | 36.8 | 20.2 | 6.34 | 17.2x | 5.81x | 3.19x |
| collatz | numeric | 1.76s | 2.30s | 6.37s | 1.44s | 1.22x | 1.60x | 4.44x |

## LARCENY

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 337.3 | 4.24s | 2.23s | 67.7 | 4.98x | 62.7x | 32.9x |
| array1 | array | 4.43 | 30.0 | 36.7 | 1.94 | 2.29x | 15.5x | 18.9x |
| deriv | symbolic | 34.6 | 90.9 | 60.7 | 3.76 | 9.22x | 24.2x | 16.2x |
| diviter | iterative | 21.38s | 13.57s | 27.23s | 482.9 | 44.3x | 28.1x | 56.4x |
| divrec | recursive | 23.9 | 51.8 | 40.5 | 8.30 | 2.87x | 6.24x | 4.88x |
| gcbench | allocation | 577.7 | 1.84s | 624.8 | 27.0 | 21.4x | 68.2x | 23.1x |
| paraffins | combinat | 3.63 | 3.38 | 2.54 | 1.80 | 2.02x | 1.87x | 1.41x |
| pnpoly | numeric | 164.0 | 289.3 | 211.9 | 6.19 | 26.5x | 46.7x | 34.2x |
| primes | iterative | 93.7 | 150.8 | 97.3 | 4.84 | 19.4x | 31.2x | 20.1x |
| puzzle | search | 25.1 | 1.23s | 30.4 | 3.37 | 7.43x | 363x | 9.00x |
| quicksort | sorting | 22.0 | 105.8 | 19.6 | 1.68 | 13.1x | 62.9x | 11.7x |
| ray | numeric | 19.9 | 26.3 | 14.1 | 3.85 | 5.17x | 6.83x | 3.65x |

## JetStream

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| nbody | numeric | 248.7 | 282.6 | 134.7 | 5.63 | 44.2x | 50.2x | 23.9x |
| cube3d | 3d | 88.9 | 1.25s | --- | 18.9 | 4.70x | 66.2x | --- |
| navier_stokes | numeric | 2.64s | 43.24s | 907.9 | 44.8 | 58.9x | 964x | 20.2x |
| richards | macro | 344.8 | 206.7 | 32.0 | 7.25 | 47.6x | 28.5x | 4.42x |
| splay | data | 2.42s | 55.9 | 30.1 | 10.1 | 240x | 5.56x | 2.99x |
| deltablue | macro | 19.8 | 372.8 | 52.0 | 8.73 | 2.27x | 42.7x | 5.95x |
| hashmap | data | 155.4 | 132.83s | 2.50s | 55.1 | 2.82x | 2410x | 45.4x |
| crypto_sha1 | crypto | 224.8 | 954.8 | 70.3 | 7.03 | 32.0x | 136x | 9.99x |
| raytrace3d | 3d | 389.5 | 1.21s | --- | 18.4 | 21.2x | 65.9x | --- |
