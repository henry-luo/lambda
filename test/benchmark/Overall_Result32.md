# Lambda Benchmark Results: Result32

- **Date:** 2026-08-17
- **Platform:** Darwin arm64
- **Lambda commit:** `a6192c108655448b06625b19d74cdb2ceced77eb`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v32-a6192c1086` (21,068,312 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 208.70s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 208.6s (batched 207.7s: sync 122.4s, async 85.3s; non-batched 0.9s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v32.json`
- **Separately measured:** C2MIR measured on 2026-08-19, 3 run(s) from `test/benchmark/run_c2mir_benchmarks.py`. Back-patched 12 rows whose native C ports did not exist during this session (awfy/cd, awfy/deltablue, awfy/havlak, awfy/json, awfy/richards, beng/pidigits, jetstream/crypto_sha1, jetstream/cube3d, jetstream/navier_stokes, jetstream/raytrace3d, jetstream/splay, larceny/deriv); all other C2MIR cells are this session's own. C2MIR measures native C ports through lambda/mir/c2m and does not depend on the Lambda binary; its cells are stable to within a few percent across the v18-v33 sessions.
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 9 | 10 | 0.66x | 0.38x | 0.20x | 12.0x | 6.59x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 2.33x | 1.07x | 0.10x | 45.8x | 5.26x |
| BENG | 8 | 8 | 8 | 8 | 8 | 5 | 8 | 0.57x | 0.39x | 0.10x | 12.0x | 1.88x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 3.07x | 1.45x | 0.23x | 54.6x | 11.9x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 2.90x | 0.89x | 0.33x | 33.2x | 13.2x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 4 | 6 | 8.37x | 3.95x | 0.29x | 77.2x | 12.5x |
| Text | 3 | 3 | 3 | 3 | 3 | 3 | 3 | 0.81x | 0.37x | 0.02x | 69.6x | 9.30x |
| **Overall** | 59 | 59 | 59 | 59 | 59 | 53 | 59 | 1.81x | 0.85x | 0.16x | 31.5x | 7.37x |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 5.46x over 59 of 59 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| awfy/deltablue | 91.0 | 1.17 | 77.9x |
| text/microdiff | 0.662 | 0.018 | 36.6x |
| awfy/bounce | 0.805 | 0.025 | 32.3x |
| kostya/base64 | 17.9 | 0.566 | 31.7x |
| awfy/list | 0.617 | 0.022 | 28.1x |
| awfy/havlak | 51.8 | 1.89 | 27.5x |
| jetstream/raytrace3d | 56.9 | 2.21 | 25.8x |
| text/hyphen | 2.14 | 0.089 | 24.0x |
| jetstream/cube3d | 11.1 | 0.523 | 21.2x |
| jetstream/hashmap | 57.6 | 2.90 | 19.8x |
| beng/knucleotide | 4.81 | 0.287 | 16.8x |
| awfy/queens | 0.319 | 0.019 | 16.8x |

---

## Notable Results

- Missing timings: **6** cells
- QuickJS missing: r7rs/ack (exit_1), beng/knucleotide (exit_1), beng/regexredux (exit_1), beng/revcomp (exit_1), jetstream/cube3d (exit_1), jetstream/raytrace3d (exit_1)

### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/havlak | 109.02s | 101.2 | 1077x |
| kostya/primes | 3.88s | 4.42 | 877x |
| awfy/cd | 28.60s | 36.3 | 788x |
| jetstream/hashmap | 5.46s | 15.7 | 347x |
| larceny/quicksort | 406.7 | 1.67 | 244x |
| awfy/nbody | 1.02s | 5.63 | 182x |
| awfy/deltablue | 2.08s | 12.1 | 171x |
| larceny/triangl | 11.59s | 68.2 | 170x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.428 | 1.96 | 0.22x |

---

## R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.36 | 1.39 | 1.11 | 40.4 | 19.4 | 1.78 | 0.76x | 0.78x | 0.62x | 22.6x | 10.9x |
| fibfp | recursive | 2.03 | 1.23 | 1.24 | 41.5 | 20.3 | 1.86 | 1.09x | 0.66x | 0.67x | 22.3x | 10.9x |
| tak | recursive | 0.132 | 0.170 | 0.128 | 2.65 | 3.02 | 0.841 | 0.16x | 0.20x | 0.15x | 3.15x | 3.58x |
| cpstak | closure | 0.264 | 0.341 | 0.228 | 5.28 | 5.92 | 1.04 | 0.25x | 0.33x | 0.22x | 5.07x | 5.68x |
| sum | iterative | 0.880 | 0.879 | 0.286 | 27.0 | 32.9 | 1.23 | 0.72x | 0.72x | 0.23x | 22.0x | 26.8x |
| sumfp | iterative | 0.071 | 0.072 | 0.083 | 2.64 | 3.83 | 0.869 | 0.08x | 0.08x | 0.10x | 3.04x | 4.41x |
| nqueens | backtrack | 1.88 | 1.20 | 0.135 | 41.4 | 8.41 | 1.80 | 1.05x | 0.66x | 0.08x | 23.0x | 4.67x |
| fft | numeric | 2.60 | 0.257 | 0.026 | 56.8 | 2.92 | 1.66 | 1.57x | 0.15x | 0.02x | 34.2x | 1.76x |
| mbrot | numeric | 11.8 | 0.569 | 0.488 | 16.8 | 18.8 | 1.87 | 6.31x | 0.30x | 0.26x | 9.01x | 10.0x |
| ack | recursive | 11.1 | 13.6 | 12.2 | 229.9 | --- | 13.8 | 0.81x | 0.98x | 0.88x | 16.7x | --- |

## AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.547 | 0.032 | 0.017 | 10.5 | 0.633 | 0.394 | 1.39x | 0.08x | 0.04x | 26.6x | 1.60x |
| permute | micro | 0.851 | 0.131 | 0.027 | 11.3 | 1.58 | 0.822 | 1.04x | 0.16x | 0.03x | 13.8x | 1.92x |
| queens | micro | 0.560 | 0.319 | 0.019 | 7.01 | 1.08 | 0.643 | 0.87x | 0.50x | 0.03x | 10.9x | 1.68x |
| towers | micro | 1.23 | 0.464 | 0.030 | 58.0 | 2.34 | 1.13 | 1.09x | 0.41x | 0.03x | 51.4x | 2.08x |
| bounce | micro | 0.276 | 0.805 | 0.025 | 7.61 | 0.905 | 0.544 | 0.51x | 1.48x | 0.05x | 14.0x | 1.66x |
| list | micro | 0.696 | 0.617 | 0.022 | 3.50 | 0.924 | 0.495 | 1.41x | 1.25x | 0.04x | 7.07x | 1.87x |
| storage | micro | 0.814 | 0.544 | 0.101 | 7.44 | 2.33 | 0.641 | 1.27x | 0.85x | 0.16x | 11.6x | 3.64x |
| mandelbrot | compute | 39.0 | 39.1 | 31.1 | 462.3 | 884.0 | 31.5 | 1.24x | 1.24x | 0.99x | 14.7x | 28.1x |
| nbody | compute | 32.6 | 20.4 | 1.52 | 1.02s | 161.1 | 5.63 | 5.80x | 3.63x | 0.27x | 182x | 28.6x |
| richards | macro | 2.67s | 265.3 | 31.2 | 2.55s | 196.3 | 47.6 | 56.2x | 5.57x | 0.65x | 53.5x | 4.12x |
| json | macro | 9.01 | 2.55 | 0.278 | 100.4 | 11.6 | 2.74 | 3.29x | 0.93x | 0.10x | 36.6x | 4.23x |
| deltablue | macro | 89.8 | 91.0 | 1.17 | 2.08s | 100.3 | 12.1 | 7.42x | 7.52x | 0.10x | 171x | 8.29x |
| havlak | macro | 51.5 | 51.8 | 1.89 | 109.02s | 3.32s | 101.2 | 0.51x | 0.51x | 0.02x | 1077x | 32.8x |
| cd | macro | 831.5 | 244.7 | 15.6 | 28.60s | 968.9 | 36.3 | 22.9x | 6.74x | 0.43x | 788x | 26.7x |

## BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 9.20 | 4.80 | 3.02 | 161.9 | 23.8 | 4.12 | 2.23x | 1.17x | 0.73x | 39.3x | 5.77x |
| fannkuch | permutation | 0.339 | 0.774 | 0.152 | 60.4 | 7.25 | 4.11 | 0.08x | 0.19x | 0.04x | 14.7x | 1.76x |
| fasta | generation | 0.794 | 0.889 | 0.247 | 26.2 | 8.78 | 6.13 | 0.13x | 0.15x | 0.04x | 4.28x | 1.43x |
| knucleotide | hashing | 4.32 | 4.81 | 0.287 | 163.9 | --- | 5.00 | 0.86x | 0.96x | 0.06x | 32.8x | --- |
| pidigits | bignum | 0.296 | 0.300 | 0.047 | 0.428 | 0.133 | 1.96 | 0.15x | 0.15x | 0.02x | 0.22x | 0.07x |
| regexredux | regex | 1.29 | 1.30 | 1.16 | 52.2 | --- | 2.45 | 0.53x | 0.53x | 0.47x | 21.3x | --- |
| revcomp | string | 1.42 | 1.22 | 0.382 | 32.2 | --- | 3.33 | 0.43x | 0.37x | 0.11x | 9.66x | --- |
| spectralnorm | numeric | 45.9 | 1.61 | 0.357 | 320.5 | 64.8 | 2.73 | 16.8x | 0.59x | 0.13x | 117x | 23.8x |

## KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 315.1 | 412.2 | 28.9 | 3.73s | 860.0 | 33.8 | 9.32x | 12.2x | 0.85x | 110x | 25.4x |
| matmul | numeric | 12.6 | 11.4 | 6.14 | 1.31s | 545.1 | 15.6 | 0.81x | 0.74x | 0.40x | 83.9x | 35.0x |
| primes | numeric | 65.9 | 3.44 | 1.61 | 3.88s | 96.3 | 4.42 | 14.9x | 0.78x | 0.36x | 877x | 21.8x |
| base64 | string | 47.3 | 17.9 | 0.566 | 871.8 | 159.1 | 17.4 | 2.72x | 1.03x | 0.03x | 50.1x | 9.14x |
| levenshtein | string | 35.9 | 7.66 | 0.946 | 447.3 | 55.5 | 4.20 | 8.54x | 1.82x | 0.23x | 107x | 13.2x |
| json_gen | data | 21.1 | 21.7 | 1.54 | 54.9 | 20.3 | 6.32 | 3.33x | 3.43x | 0.24x | 8.69x | 3.22x |
| collatz | numeric | 427.6 | 425.5 | 228.8 | 5.55s | 6.33s | 1.45s | 0.29x | 0.29x | 0.16x | 3.83x | 4.37x |

## LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 497.7 | 212.7 | 61.3 | 11.59s | 2.22s | 68.2 | 7.30x | 3.12x | 0.90x | 170x | 32.5x |
| array1 | array | 0.828 | 0.832 | 0.330 | 79.1 | 36.4 | 1.94 | 0.43x | 0.43x | 0.17x | 40.8x | 18.8x |
| deriv | symbolic | 36.0 | 11.1 | 2.92 | 344.0 | 59.7 | 3.69 | 9.76x | 3.00x | 0.79x | 93.3x | 16.2x |
| diviter | iterative | 408.4 | 405.2 | 270.3 | 13.27s | 27.13s | 478.1 | 0.85x | 0.85x | 0.57x | 27.8x | 56.7x |
| divrec | recursive | 15.8 | 2.03 | 4.98 | 44.6 | 37.0 | 7.67 | 2.05x | 0.26x | 0.65x | 5.81x | 4.82x |
| gcbench | allocation | 214.9 | 155.2 | 71.3 | 3.99s | 557.5 | 24.0 | 8.97x | 6.48x | 2.98x | 167x | 23.3x |
| paraffins | combinat | 1.93 | 0.291 | 0.049 | 4.23 | 2.54 | 1.01 | 1.91x | 0.29x | 0.05x | 4.19x | 2.52x |
| pnpoly | numeric | 16.5 | 16.5 | 1.99 | 116.1 | 206.5 | 6.24 | 2.65x | 2.64x | 0.32x | 18.6x | 33.1x |
| puzzle | search | 9.71 | 3.68 | 1.28 | 110.0 | 29.5 | 3.35 | 2.90x | 1.10x | 0.38x | 32.9x | 8.82x |
| quicksort | sorting | 10.3 | 1.10 | 0.201 | 406.7 | 19.5 | 1.67 | 6.19x | 0.66x | 0.12x | 244x | 11.7x |
| ray | numeric | 10.2 | 0.331 | 0.173 | 17.9 | 13.9 | 3.63 | 2.82x | 0.09x | 0.05x | 4.93x | 3.83x |

## JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 12.3 | 11.1 | 0.523 | 765.0 | --- | 17.9 | 0.68x | 0.62x | 0.03x | 42.6x | --- |
| navier_stokes | numeric | 1.02s | 187.7 | 48.4 | 1.07s | 100.1 | 14.3 | 71.5x | 13.1x | 3.38x | 74.7x | 6.99x |
| splay | data | 140.2 | 264.8 | 20.4 | 1.12s | 151.1 | 21.4 | 6.55x | 12.4x | 0.95x | 52.2x | 7.06x |
| hashmap | data | 156.8 | 57.6 | 2.90 | 5.46s | 322.0 | 15.7 | 9.96x | 3.66x | 0.18x | 347x | 20.5x |
| crypto_sha1 | crypto | 65.0 | 31.0 | 2.70 | 612.4 | 221.6 | 9.07 | 7.17x | 3.42x | 0.30x | 67.5x | 24.4x |
| raytrace3d | 3d | 281.9 | 56.9 | 2.21 | 1.02s | --- | 18.7 | 15.0x | 3.04x | 0.12x | 54.6x | --- |

## Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 1.43s | 159.8 | 13.3 | 2.64s | 620.7 | 40.3 | 35.6x | 3.96x | 0.33x | 65.5x | 15.4x |
| microdiff | data-diff | 0.635 | 0.662 | 0.018 | 1.50s | 110.4 | 16.7 | 0.04x | 0.04x | 0.001x | 89.7x | 6.60x |
| hyphen | hyphenation | 2.60 | 2.14 | 0.089 | 383.7 | 53.0 | 6.70 | 0.39x | 0.32x | 0.01x | 57.3x | 7.91x |
