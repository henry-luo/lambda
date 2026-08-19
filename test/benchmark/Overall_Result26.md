# Lambda Benchmark Results: Result26

- **Date:** 2026-08-08
- **Platform:** Darwin arm64
- **Lambda commit:** `7a876454d056fa4ac603b926fb9e2efcc85ac179`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v26-7a876454d0` (21,385,928 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 119.10s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 119.0s (batched 117.0s: sync 97.1s, async 19.9s; non-batched 2.0s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v26.json`
- **Separately measured:** C2MIR measured on 2026-08-19, 3 run(s) from `test/benchmark/run_c2mir_benchmarks.py`. Back-patched 12 rows whose native C ports did not exist during this session (awfy/cd, awfy/deltablue, awfy/havlak, awfy/json, awfy/richards, beng/pidigits, jetstream/crypto_sha1, jetstream/cube3d, jetstream/navier_stokes, jetstream/raytrace3d, jetstream/splay, larceny/deriv); all other C2MIR cells are this session's own. C2MIR measures native C ports through lambda/mir/c2m and does not depend on the Lambda binary; its cells are stable to within a few percent across the v18-v33 sessions.
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 9 | 10 | 1.70x | 0.48x | 0.19x | 5.53x | 6.40x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 2.43x | 1.50x | 0.09x | 24.6x | 5.11x |
| BENG | 8 | 8 | 8 | 8 | 5 | 5 | 8 | 0.59x | 0.50x | 0.10x | 4.72x | 1.87x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 3.64x | 2.22x | 0.23x | 15.6x | 11.9x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 2.80x | 1.63x | 0.33x | 13.7x | 13.1x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 4 | 6 | 10.9x | 5.96x | 0.28x | 69.0x | 12.4x |
| Text | 3 | 3 | 3 | 3 | 3 | 3 | 3 | 0.91x | 0.69x | 0.02x | 60.5x | 9.05x |
| **Overall** | 59 | 59 | 59 | 59 | 56 | 53 | 59 | 2.25x | 1.26x | 0.15x | 16.0x | 7.25x |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 8.29x over 59 of 59 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| awfy/deltablue | 106.8 | 1.17 | 91.4x |
| jetstream/crypto_sha1 | 235.4 | 2.70 | 87.2x |
| kostya/base64 | 47.0 | 0.575 | 81.7x |
| text/microdiff | 0.893 | 0.018 | 50.1x |
| text/hyphen | 3.72 | 0.091 | 40.9x |
| awfy/list | 0.925 | 0.023 | 40.3x |
| awfy/cd | 610.7 | 15.6 | 39.1x |
| text/fast_diff | 488.4 | 13.4 | 36.5x |
| awfy/havlak | 63.0 | 1.89 | 33.4x |
| awfy/bounce | 0.811 | 0.025 | 32.3x |
| jetstream/raytrace3d | 70.9 | 2.21 | 32.2x |
| awfy/queens | 0.586 | 0.019 | 30.8x |

---

## Notable Results

- Missing timings: **9** cells
- QuickJS missing: r7rs/ack (exit_1), beng/knucleotide (exit_1), beng/regexredux (exit_1), beng/revcomp (exit_1), jetstream/cube3d (exit_1), jetstream/raytrace3d (exit_1)
- LambdaJS missing: beng/knucleotide (exit_1), beng/regexredux (exit_1), beng/revcomp (exit_1)

### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/havlak | 49.47s | 146.6 | 338x |
| awfy/cd | 11.11s | 41.2 | 270x |
| jetstream/hashmap | 3.72s | 16.9 | 220x |
| jetstream/crypto_sha1 | 1.94s | 9.46 | 205x |
| beng/spectralnorm | 304.6 | 2.67 | 114x |
| awfy/nbody | 592.4 | 5.58 | 106x |
| text/microdiff | 1.49s | 17.8 | 83.9x |
| awfy/deltablue | 1.11s | 13.5 | 82.6x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.314 | 1.99 | 0.16x |

---

## R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 5.12 | 1.34 | 1.14 | 21.7 | 18.8 | 1.82 | 2.81x | 0.73x | 0.63x | 11.9x | 10.3x |
| fibfp | recursive | 7.58 | 3.03 | 1.14 | 21.7 | 19.2 | 1.83 | 4.14x | 1.66x | 0.62x | 11.8x | 10.5x |
| tak | recursive | 0.935 | 0.151 | 0.120 | 1.83 | 3.05 | 0.845 | 1.11x | 0.18x | 0.14x | 2.17x | 3.61x |
| cpstak | closure | 1.98 | 0.311 | 0.238 | 3.75 | 6.16 | 1.08 | 1.84x | 0.29x | 0.22x | 3.48x | 5.73x |
| sum | iterative | 0.911 | 0.911 | 0.299 | 11.5 | 33.5 | 1.29 | 0.71x | 0.71x | 0.23x | 8.92x | 25.9x |
| sumfp | iterative | 0.351 | 0.351 | 0.089 | 1.16 | 4.08 | 0.942 | 0.37x | 0.37x | 0.09x | 1.23x | 4.33x |
| nqueens | backtrack | 2.09 | 1.88 | 0.141 | 24.0 | 8.65 | 1.90 | 1.10x | 0.99x | 0.07x | 12.6x | 4.55x |
| fft | numeric | 2.73 | 0.248 | 0.027 | 12.2 | 2.98 | 1.76 | 1.55x | 0.14x | 0.02x | 6.94x | 1.69x |
| mbrot | numeric | 12.4 | 0.684 | 0.492 | 10.1 | 19.1 | 2.04 | 6.10x | 0.34x | 0.24x | 4.98x | 9.38x |
| ack | recursive | 44.4 | 13.0 | 12.4 | 76.4 | --- | 14.5 | 3.06x | 0.89x | 0.86x | 5.27x | --- |

## AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.541 | 0.162 | 0.016 | 0.487 | 0.621 | 0.390 | 1.39x | 0.41x | 0.04x | 1.25x | 1.59x |
| permute | micro | 0.915 | 0.152 | 0.027 | 10.1 | 1.58 | 0.824 | 1.11x | 0.18x | 0.03x | 12.2x | 1.92x |
| queens | micro | 0.556 | 0.586 | 0.019 | 6.42 | 1.06 | 0.649 | 0.86x | 0.90x | 0.03x | 9.89x | 1.63x |
| towers | micro | 1.37 | 0.700 | 0.028 | 26.3 | 2.27 | 1.13 | 1.21x | 0.62x | 0.02x | 23.4x | 2.01x |
| bounce | micro | 0.272 | 0.811 | 0.025 | 5.07 | 0.897 | 0.551 | 0.49x | 1.47x | 0.05x | 9.21x | 1.63x |
| list | micro | 0.867 | 0.925 | 0.023 | 3.39 | 0.918 | 0.496 | 1.75x | 1.86x | 0.05x | 6.84x | 1.85x |
| storage | micro | 0.802 | 0.473 | 0.096 | 15.4 | 2.18 | 0.633 | 1.27x | 0.75x | 0.15x | 24.3x | 3.45x |
| mandelbrot | compute | 48.6 | 49.2 | 31.1 | 303.9 | 883.3 | 31.6 | 1.54x | 1.56x | 0.98x | 9.62x | 28.0x |
| nbody | compute | 37.1 | 39.5 | 1.53 | 592.4 | 161.7 | 5.58 | 6.65x | 7.09x | 0.27x | 106x | 29.0x |
| richards | macro | 2.63s | 280.4 | 31.2 | 1.98s | 206.6 | 51.1 | 51.5x | 5.48x | 0.61x | 38.8x | 4.04x |
| json | macro | 8.26 | 2.94 | 0.278 | 51.2 | 12.0 | 2.82 | 2.93x | 1.04x | 0.10x | 18.1x | 4.25x |
| deltablue | macro | 113.3 | 106.8 | 1.17 | 1.11s | 108.6 | 13.5 | 8.42x | 7.93x | 0.09x | 82.6x | 8.07x |
| havlak | macro | 62.9 | 63.0 | 1.89 | 49.47s | 4.05s | 146.6 | 0.43x | 0.43x | 0.01x | 338x | 27.7x |
| cd | macro | 1.08s | 610.7 | 15.6 | 11.11s | 1.06s | 41.2 | 26.2x | 14.8x | 0.38x | 270x | 25.6x |

## BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 10.6 | 9.13 | 3.12 | 40.2 | 24.0 | 4.12 | 2.58x | 2.21x | 0.76x | 9.74x | 5.81x |
| fannkuch | permutation | 0.363 | 1.87 | 0.153 | 13.3 | 7.25 | 4.22 | 0.09x | 0.44x | 0.04x | 3.16x | 1.72x |
| fasta | generation | 0.796 | 1.27 | 0.247 | 26.1 | 8.75 | 6.17 | 0.13x | 0.21x | 0.04x | 4.23x | 1.42x |
| knucleotide | hashing | 4.44 | 4.75 | 0.288 | --- | --- | 5.08 | 0.87x | 0.94x | 0.06x | --- | --- |
| pidigits | bignum | 0.309 | 0.301 | 0.047 | 0.314 | 0.133 | 1.99 | 0.16x | 0.15x | 0.02x | 0.16x | 0.07x |
| regexredux | regex | 1.32 | 1.33 | 1.16 | --- | --- | 2.58 | 0.51x | 0.52x | 0.45x | --- | --- |
| revcomp | string | 1.41 | 1.23 | 0.390 | --- | --- | 3.39 | 0.42x | 0.36x | 0.12x | --- | --- |
| spectralnorm | numeric | 45.4 | 2.09 | 0.359 | 304.6 | 65.1 | 2.67 | 17.0x | 0.78x | 0.13x | 114x | 24.3x |

## KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 330.7 | 413.2 | 28.7 | 977.0 | 897.7 | 34.4 | 9.62x | 12.0x | 0.83x | 28.4x | 26.1x |
| matmul | numeric | 15.9 | 12.3 | 6.20 | 911.4 | 548.8 | 15.7 | 1.01x | 0.78x | 0.39x | 57.9x | 34.9x |
| primes | numeric | 60.6 | 26.5 | 1.61 | 101.3 | 96.7 | 4.54 | 13.3x | 5.84x | 0.36x | 22.3x | 21.3x |
| base64 | string | 51.2 | 47.0 | 0.575 | 693.9 | 160.2 | 17.8 | 2.88x | 2.65x | 0.03x | 39.1x | 9.02x |
| levenshtein | string | 35.8 | 8.13 | 0.916 | 82.7 | 56.1 | 4.17 | 8.60x | 1.95x | 0.22x | 19.9x | 13.5x |
| json_gen | data | 20.0 | 20.5 | 1.58 | 36.2 | 20.3 | 6.38 | 3.13x | 3.20x | 0.25x | 5.67x | 3.18x |
| collatz | numeric | 1.21s | 426.4 | 229.0 | 2.00s | 6.34s | 1.44s | 0.84x | 0.30x | 0.16x | 1.38x | 4.40x |

## LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 228.8 | 214.3 | 61.8 | 4.33s | 2.23s | 67.8 | 3.37x | 3.16x | 0.91x | 63.9x | 32.8x |
| array1 | array | 0.829 | 1.10 | 0.326 | 27.9 | 36.4 | 1.95 | 0.43x | 0.57x | 0.17x | 14.3x | 18.7x |
| deriv | symbolic | 34.5 | 18.0 | 2.92 | 91.5 | 60.1 | 3.73 | 9.23x | 4.83x | 0.78x | 24.5x | 16.1x |
| diviter | iterative | 405.9 | 406.2 | 270.8 | 8.10s | 27.30s | 480.1 | 0.85x | 0.85x | 0.56x | 16.9x | 56.9x |
| divrec | recursive | 18.2 | 2.03 | 4.96 | 26.8 | 37.4 | 7.97 | 2.28x | 0.25x | 0.62x | 3.36x | 4.69x |
| gcbench | allocation | 248.1 | 260.0 | 72.1 | 1.32s | 559.3 | 24.1 | 10.3x | 10.8x | 2.99x | 54.6x | 23.2x |
| paraffins | combinat | 2.02 | 1.07 | 0.049 | 2.72 | 2.54 | 1.01 | 1.99x | 1.06x | 0.05x | 2.69x | 2.52x |
| pnpoly | numeric | 20.3 | 53.8 | 2.01 | 111.4 | 204.9 | 6.01 | 3.38x | 8.95x | 0.33x | 18.5x | 34.1x |
| puzzle | search | 8.66 | 4.65 | 1.30 | 25.3 | 29.7 | 3.41 | 2.54x | 1.37x | 0.38x | 7.44x | 8.71x |
| quicksort | sorting | 10.5 | 5.64 | 0.202 | 66.6 | 19.5 | 1.68 | 6.23x | 3.35x | 0.12x | 39.6x | 11.6x |
| ray | numeric | 10.9 | 0.904 | 0.176 | 11.5 | 14.0 | 3.71 | 2.93x | 0.24x | 0.05x | 3.09x | 3.78x |

## JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 14.1 | 14.0 | 0.523 | 681.8 | --- | 18.1 | 0.78x | 0.77x | 0.03x | 37.6x | --- |
| navier_stokes | numeric | 1.05s | 232.3 | 48.4 | 717.8 | 100.3 | 14.4 | 72.6x | 16.1x | 3.36x | 49.9x | 6.97x |
| splay | data | 144.1 | 290.8 | 20.4 | 536.5 | 168.6 | 24.9 | 5.80x | 11.7x | 0.82x | 21.6x | 6.78x |
| hashmap | data | 207.5 | 59.0 | 3.37 | 3.72s | 345.0 | 16.9 | 12.3x | 3.49x | 0.20x | 220x | 20.4x |
| crypto_sha1 | crypto | 250.4 | 235.4 | 2.70 | 1.94s | 234.7 | 9.46 | 26.5x | 24.9x | 0.29x | 205x | 24.8x |
| raytrace3d | 3d | 316.5 | 70.9 | 2.21 | 1.19s | --- | 20.0 | 15.8x | 3.54x | 0.11x | 59.3x | --- |

## Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 1.37s | 488.4 | 13.4 | 2.31s | 623.6 | 41.2 | 33.1x | 11.8x | 0.32x | 56.1x | 15.1x |
| microdiff | data-diff | 1.02 | 0.893 | 0.018 | 1.49s | 111.3 | 17.8 | 0.06x | 0.05x | 0.001x | 83.9x | 6.25x |
| hyphen | hyphenation | 2.66 | 3.72 | 0.091 | 315.8 | 52.8 | 6.73 | 0.40x | 0.55x | 0.01x | 46.9x | 7.84x |
