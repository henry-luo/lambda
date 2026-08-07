# Lambda Benchmark Results: Result25

- **Date:** 2026-08-07
- **Platform:** Darwin arm64
- **Lambda commit:** `acb46fb4d52e20985d9b51358ee472b7ec3ed06f`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v25-812ddaef0b` (21,185,816 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 167.90s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 167.9s (batched 166.1s: sync 116.7s, async 49.4s; non-batched 1.7s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v25.json`
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 0 | 10 | 9 | 10 | 1.39x | 0.51x | --- | 5.76x | 6.44x |
| AWFY | 14 | 14 | 14 | 0 | 14 | 14 | 14 | 2.64x | 1.33x | --- | 23.3x | 5.14x |
| BENG | 8 | 8 | 8 | 0 | 8 | 5 | 8 | 0.65x | 0.45x | --- | 7.10x | 1.88x |
| KOSTYA | 7 | 7 | 7 | 0 | 7 | 7 | 7 | 3.63x | 2.50x | --- | 16.0x | 11.9x |
| LARCENY | 11 | 11 | 11 | 0 | 11 | 11 | 11 | 3.28x | 1.98x | --- | 14.0x | 13.1x |
| JetStream | 6 | 6 | 6 | 0 | 6 | 4 | 6 | 11.1x | 7.05x | --- | 68.3x | 12.6x |
| Text | 3 | 3 | 3 | 3 | 3 | 3 | 3 | 0.96x | 0.84x | 0.02x | 62.7x | 9.52x |
| **Overall** | 59 | 59 | 59 | 3 | 59 | 53 | 59 | 2.32x | 1.32x | 0.02x | 16.0x | 7.30x |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 50.3x over 3 of 59 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| text/microdiff | 0.979 | 0.016 | 60.8x |
| text/hyphen | 4.29 | 0.088 | 48.8x |
| text/fast_diff | 557.1 | 13.0 | 42.8x |

---

## Notable Results

- Missing timings: **62** cells
- C2MIR missing: r7rs/fib (not_recorded), r7rs/fibfp (not_recorded), r7rs/tak (not_recorded), r7rs/cpstak (not_recorded), r7rs/sum (not_recorded), r7rs/sumfp (not_recorded), r7rs/nqueens (not_recorded), r7rs/fft (not_recorded), +48 more
- QuickJS missing: r7rs/ack (exit_1), beng/knucleotide (exit_1), beng/regexredux (exit_1), beng/revcomp (exit_1), jetstream/cube3d (exit_1), jetstream/raytrace3d (exit_1)

### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/havlak | 48.28s | 121.7 | 397x |
| awfy/cd | 10.63s | 40.2 | 264x |
| jetstream/hashmap | 3.49s | 16.7 | 208x |
| jetstream/crypto_sha1 | 1.86s | 9.38 | 199x |
| beng/spectralnorm | 297.6 | 2.62 | 114x |
| awfy/nbody | 575.7 | 5.86 | 98.3x |
| text/microdiff | 1.44s | 16.1 | 89.0x |
| awfy/deltablue | 970.2 | 12.3 | 79.2x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.323 | 2.00 | 0.16x |

---

## R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 4.99 | 1.34 | --- | 23.0 | 19.0 | 1.81 | 2.76x | 0.74x | --- | 12.7x | 10.5x |
| fibfp | recursive | 7.68 | 3.04 | --- | 23.1 | 18.9 | 1.79 | 4.30x | 1.70x | --- | 12.9x | 10.6x |
| tak | recursive | 0.427 | 0.149 | --- | 1.96 | 2.82 | 0.799 | 0.53x | 0.19x | --- | 2.45x | 3.53x |
| cpstak | closure | 0.856 | 0.295 | --- | 3.90 | 5.63 | 0.991 | 0.86x | 0.30x | --- | 3.94x | 5.68x |
| sum | iterative | 0.837 | 0.837 | --- | 10.9 | 31.5 | 1.20 | 0.70x | 0.70x | --- | 9.13x | 26.3x |
| sumfp | iterative | 0.323 | 0.323 | --- | 1.09 | 3.73 | 0.894 | 0.36x | 0.36x | --- | 1.22x | 4.17x |
| nqueens | backtrack | 1.78 | 2.41 | --- | 23.1 | 8.49 | 1.88 | 0.94x | 1.28x | --- | 12.3x | 4.51x |
| fft | numeric | 2.74 | 0.250 | --- | 12.3 | 3.07 | 1.71 | 1.60x | 0.15x | --- | 7.20x | 1.79x |
| mbrot | numeric | 12.3 | 0.705 | --- | 9.30 | 19.1 | 1.98 | 6.23x | 0.36x | --- | 4.70x | 9.63x |
| ack | recursive | 30.4 | 15.5 | --- | 80.0 | --- | 14.6 | 2.08x | 1.06x | --- | 5.49x | --- |

## AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.550 | 0.115 | --- | 0.498 | 0.627 | 0.393 | 1.40x | 0.29x | --- | 1.27x | 1.60x |
| permute | micro | 0.910 | 0.131 | --- | 8.75 | 1.54 | 0.816 | 1.12x | 0.16x | --- | 10.7x | 1.88x |
| queens | micro | 0.551 | 0.503 | --- | 5.56 | 1.06 | 0.644 | 0.86x | 0.78x | --- | 8.63x | 1.65x |
| towers | micro | 1.28 | 0.566 | --- | 24.0 | 2.28 | 1.13 | 1.14x | 0.50x | --- | 21.3x | 2.02x |
| bounce | micro | 0.291 | 0.795 | --- | 4.65 | 0.888 | 0.545 | 0.53x | 1.46x | --- | 8.52x | 1.63x |
| list | micro | 0.845 | 0.924 | --- | 3.14 | 0.934 | 0.507 | 1.67x | 1.82x | --- | 6.19x | 1.84x |
| storage | micro | 0.784 | 0.473 | --- | 14.3 | 2.18 | 0.640 | 1.22x | 0.74x | --- | 22.3x | 3.40x |
| mandelbrot | compute | 50.6 | 50.7 | --- | 304.7 | 885.1 | 31.6 | 1.60x | 1.61x | --- | 9.65x | 28.0x |
| nbody | compute | 170.9 | 30.3 | --- | 575.7 | 162.8 | 5.86 | 29.2x | 5.18x | --- | 98.3x | 27.8x |
| richards | macro | 2.40s | 240.5 | --- | 1.67s | 193.0 | 47.5 | 50.6x | 5.06x | --- | 35.2x | 4.06x |
| json | macro | 7.44 | 2.51 | --- | 45.1 | 11.2 | 2.68 | 2.78x | 0.94x | --- | 16.8x | 4.20x |
| deltablue | macro | 91.4 | 88.7 | --- | 970.2 | 101.0 | 12.3 | 7.46x | 7.23x | --- | 79.2x | 8.24x |
| havlak | macro | 48.7 | 48.3 | --- | 48.28s | 3.74s | 121.7 | 0.40x | 0.40x | --- | 397x | 30.7x |
| cd | macro | 974.4 | 553.7 | --- | 10.63s | 1.05s | 40.2 | 24.2x | 13.8x | --- | 264x | 26.0x |

## BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 9.65 | 9.32 | --- | 40.7 | 24.1 | 4.12 | 2.34x | 2.26x | --- | 9.89x | 5.86x |
| fannkuch | permutation | 0.373 | 0.369 | --- | 12.9 | 7.23 | 4.25 | 0.09x | 0.09x | --- | 3.04x | 1.70x |
| fasta | generation | 1.76 | 1.68 | --- | 25.8 | 8.77 | 6.20 | 0.28x | 0.27x | --- | 4.16x | 1.41x |
| knucleotide | hashing | 4.71 | 5.18 | --- | 151.3 | --- | 5.30 | 0.89x | 0.98x | --- | 28.6x | --- |
| pidigits | bignum | 0.303 | 0.305 | --- | 0.323 | 0.135 | 2.00 | 0.15x | 0.15x | --- | 0.16x | 0.07x |
| regexredux | regex | 1.30 | 1.32 | --- | 17.5 | --- | 2.48 | 0.53x | 0.53x | --- | 7.07x | --- |
| revcomp | string | 1.41 | 1.45 | --- | 47.3 | --- | 3.40 | 0.41x | 0.43x | --- | 13.9x | --- |
| spectralnorm | numeric | 45.6 | 2.65 | --- | 297.6 | 64.7 | 2.62 | 17.4x | 1.01x | --- | 114x | 24.7x |

## KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 321.4 | 452.0 | --- | 1.00s | 893.5 | 34.3 | 9.36x | 13.2x | --- | 29.1x | 26.0x |
| matmul | numeric | 13.0 | 14.2 | --- | 934.2 | 548.0 | 15.6 | 0.84x | 0.91x | --- | 60.0x | 35.2x |
| primes | numeric | 60.6 | 10.4 | --- | 98.3 | 95.9 | 4.49 | 13.5x | 2.31x | --- | 21.9x | 21.4x |
| base64 | string | 83.1 | 69.7 | --- | 685.7 | 159.2 | 17.6 | 4.73x | 3.97x | --- | 39.0x | 9.06x |
| levenshtein | string | 26.5 | 21.9 | --- | 85.0 | 55.1 | 4.10 | 6.47x | 5.34x | --- | 20.8x | 13.4x |
| json_gen | data | 19.5 | 21.5 | --- | 35.7 | 20.2 | 6.29 | 3.10x | 3.43x | --- | 5.68x | 3.22x |
| collatz | numeric | 1.20s | 440.2 | --- | 2.17s | 6.37s | 1.44s | 0.83x | 0.31x | --- | 1.51x | 4.43x |

## LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 227.0 | 226.2 | --- | 4.36s | 2.22s | 67.8 | 3.35x | 3.33x | --- | 64.3x | 32.7x |
| array1 | array | 1.18 | 1.18 | --- | 27.1 | 37.0 | 1.94 | 0.61x | 0.61x | --- | 14.0x | 19.1x |
| deriv | symbolic | 30.8 | 15.1 | --- | 92.7 | 59.8 | 3.71 | 8.28x | 4.07x | --- | 24.9x | 16.1x |
| diviter | iterative | 405.1 | 405.1 | --- | 8.80s | 27.16s | 505.0 | 0.80x | 0.80x | --- | 17.4x | 53.8x |
| divrec | recursive | 19.7 | 2.31 | --- | 29.7 | 40.6 | 8.38 | 2.35x | 0.28x | --- | 3.54x | 4.85x |
| gcbench | allocation | 248.2 | 255.8 | --- | 1.54s | 624.9 | 27.7 | 8.97x | 9.24x | --- | 55.5x | 22.6x |
| paraffins | combinat | 2.14 | 1.83 | --- | 2.89 | 2.79 | 1.08 | 1.97x | 1.69x | --- | 2.67x | 2.57x |
| pnpoly | numeric | 123.9 | 69.7 | --- | 137.4 | 220.6 | 6.36 | 19.5x | 11.0x | --- | 21.6x | 34.7x |
| puzzle | search | 9.47 | 8.99 | --- | 27.1 | 31.8 | 3.81 | 2.49x | 2.36x | --- | 7.12x | 8.36x |
| quicksort | sorting | 10.9 | 6.48 | --- | 73.1 | 21.1 | 1.83 | 5.95x | 3.54x | --- | 40.0x | 11.5x |
| ray | numeric | 11.5 | 2.78 | --- | 12.3 | 15.3 | 3.91 | 2.95x | 0.71x | --- | 3.15x | 3.92x |

## JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 14.7 | 13.6 | --- | 666.9 | --- | 18.1 | 0.81x | 0.75x | --- | 36.8x | --- |
| navier_stokes | numeric | 1.07s | 259.1 | --- | 812.2 | 106.6 | 15.5 | 69.2x | 16.7x | --- | 52.4x | 6.87x |
| splay | data | 156.9 | 221.3 | --- | 527.2 | 165.8 | 23.3 | 6.73x | 9.49x | --- | 22.6x | 7.11x |
| hashmap | data | 167.0 | 54.4 | --- | 3.49s | 340.8 | 16.7 | 9.99x | 3.26x | --- | 208x | 20.4x |
| crypto_sha1 | crypto | 255.1 | 228.2 | --- | 1.86s | 233.7 | 9.38 | 27.2x | 24.3x | --- | 199x | 24.9x |
| raytrace3d | 3d | 361.2 | 261.9 | --- | 1.13s | --- | 20.1 | 17.9x | 13.0x | --- | 56.3x | --- |

## Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 1.37s | 557.1 | 13.0 | 2.23s | 610.2 | 39.1 | 35.0x | 14.3x | 0.33x | 57.1x | 15.6x |
| microdiff | data-diff | 0.982 | 0.979 | 0.016 | 1.44s | 109.7 | 16.1 | 0.06x | 0.06x | 0.001x | 89.0x | 6.80x |
| hyphen | hyphenation | 2.64 | 4.29 | 0.088 | 307.5 | 51.5 | 6.33 | 0.42x | 0.68x | 0.01x | 48.6x | 8.13x |
