# Lambda Benchmark Results: Result31

- **Date:** 2026-08-17
- **Platform:** Darwin arm64
- **Lambda commit:** `f88b5c8cf954e96406d7b50ec21bcd0a5b661c0e`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v31-f88b5c8cf9` (21,728,728 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 213.60s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 213.4s (batched 212.3s: sync 129.8s, async 82.5s; non-batched 1.2s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v31.json`
- **Separately measured:** C2MIR measured on 2026-08-19, 3 run(s) from `test/benchmark/run_c2mir_benchmarks.py`. Back-patched 12 rows whose native C ports did not exist during this session (awfy/cd, awfy/deltablue, awfy/havlak, awfy/json, awfy/richards, beng/pidigits, jetstream/crypto_sha1, jetstream/cube3d, jetstream/navier_stokes, jetstream/raytrace3d, jetstream/splay, larceny/deriv); all other C2MIR cells are this session's own. C2MIR measures native C ports through lambda/mir/c2m and does not depend on the Lambda binary; its cells are stable to within a few percent across the v18-v33 sessions.
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 9 | 10 | 0.82x | 0.48x | 0.19x | 11.8x | 6.43x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 2.51x | 1.25x | 0.09x | 45.8x | 5.36x |
| BENG | 8 | 8 | 8 | 8 | 8 | 5 | 8 | 0.59x | 0.44x | 0.10x | 12.2x | 1.92x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 3.12x | 1.63x | 0.23x | 54.5x | 12.0x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 3.04x | 1.27x | 0.33x | 33.2x | 13.4x |
| JetStream | 6 | 6 | 6 | 6 | 4 | 4 | 6 | 8.83x | 4.01x | 0.30x | 72.4x | 12.9x |
| Text | 3 | 3 | 3 | 3 | 3 | 3 | 3 | 0.87x | 0.66x | 0.02x | 71.5x | 9.41x |
| **Overall** | 59 | 59 | 59 | 59 | 57 | 53 | 59 | 1.95x | 1.04x | 0.16x | 30.4x | 7.44x |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 6.66x over 59 of 59 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| awfy/deltablue | 101.6 | 1.17 | 87.0x |
| kostya/base64 | 46.0 | 0.556 | 82.7x |
| awfy/list | 1.03 | 0.021 | 48.9x |
| text/microdiff | 0.831 | 0.020 | 41.5x |
| text/hyphen | 3.44 | 0.090 | 38.2x |
| text/fast_diff | 412.2 | 13.1 | 31.4x |
| awfy/bounce | 0.730 | 0.024 | 30.5x |
| awfy/havlak | 56.4 | 1.89 | 29.9x |
| jetstream/raytrace3d | 59.8 | 2.21 | 27.1x |
| larceny/quicksort | 4.78 | 0.197 | 24.3x |
| awfy/towers | 0.588 | 0.027 | 21.7x |
| jetstream/cube3d | 11.1 | 0.523 | 21.2x |

---

## Notable Results

- Missing timings: **8** cells
- QuickJS missing: r7rs/ack (exit_1), beng/knucleotide (exit_1), beng/regexredux (exit_1), beng/revcomp (exit_1), jetstream/cube3d (exit_1), jetstream/raytrace3d (exit_1)
- LambdaJS missing: jetstream/navier_stokes (timeout), jetstream/hashmap (timeout)

### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/havlak | 106.96s | 99.4 | 1077x |
| kostya/primes | 3.85s | 4.40 | 874x |
| awfy/cd | 28.49s | 35.8 | 795x |
| larceny/quicksort | 399.7 | 1.62 | 247x |
| awfy/nbody | 1.01s | 5.26 | 192x |
| awfy/deltablue | 2.03s | 11.8 | 172x |
| larceny/gcbench | 3.92s | 23.3 | 169x |
| larceny/triangl | 11.22s | 66.6 | 168x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.423 | 1.89 | 0.22x |

---

## R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.30 | 1.25 | 1.00 | 38.3 | 18.7 | 1.81 | 0.72x | 0.69x | 0.55x | 21.1x | 10.3x |
| fibfp | recursive | 3.81 | 2.91 | 1.12 | 38.4 | 18.6 | 1.74 | 2.19x | 1.67x | 0.64x | 22.0x | 10.6x |
| tak | recursive | 0.123 | 0.148 | 0.118 | 2.61 | 2.78 | 0.775 | 0.16x | 0.19x | 0.15x | 3.37x | 3.59x |
| cpstak | closure | 0.244 | 0.303 | 0.232 | 5.15 | 5.54 | 0.973 | 0.25x | 0.31x | 0.24x | 5.29x | 5.70x |
| sum | iterative | 0.827 | 0.824 | 0.272 | 24.9 | 31.0 | 1.19 | 0.69x | 0.69x | 0.23x | 20.9x | 26.0x |
| sumfp | iterative | 0.318 | 0.318 | 0.077 | 2.51 | 3.65 | 0.867 | 0.37x | 0.37x | 0.09x | 2.90x | 4.22x |
| nqueens | backtrack | 1.89 | 1.60 | 0.126 | 38.8 | 7.83 | 1.75 | 1.07x | 0.91x | 0.07x | 22.1x | 4.46x |
| fft | numeric | 2.54 | 0.231 | 0.024 | 52.7 | 2.73 | 1.56 | 1.63x | 0.15x | 0.02x | 33.8x | 1.75x |
| mbrot | numeric | 11.0 | 0.626 | 0.446 | 15.9 | 17.7 | 1.82 | 6.04x | 0.34x | 0.24x | 8.72x | 9.69x |
| ack | recursive | 10.8 | 11.2 | 11.8 | 213.9 | --- | 13.3 | 0.81x | 0.84x | 0.88x | 16.0x | --- |

## AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.547 | 0.052 | 0.017 | 10.1 | 0.616 | 0.378 | 1.45x | 0.14x | 0.05x | 26.7x | 1.63x |
| permute | micro | 0.949 | 0.149 | 0.026 | 11.1 | 1.58 | 0.808 | 1.17x | 0.18x | 0.03x | 13.8x | 1.95x |
| queens | micro | 0.543 | 0.322 | 0.017 | 6.91 | 1.05 | 0.656 | 0.83x | 0.49x | 0.03x | 10.5x | 1.60x |
| towers | micro | 1.33 | 0.588 | 0.027 | 55.9 | 2.96 | 1.10 | 1.21x | 0.53x | 0.02x | 50.7x | 2.68x |
| bounce | micro | 0.261 | 0.730 | 0.024 | 7.29 | 0.868 | 0.535 | 0.49x | 1.36x | 0.04x | 13.6x | 1.62x |
| list | micro | 0.869 | 1.03 | 0.021 | 3.38 | 0.907 | 0.488 | 1.78x | 2.11x | 0.04x | 6.94x | 1.86x |
| storage | micro | 0.842 | 0.608 | 0.097 | 7.26 | 2.20 | 0.639 | 1.32x | 0.95x | 0.15x | 11.4x | 3.44x |
| mandelbrot | compute | 47.8 | 47.9 | 30.6 | 447.1 | 873.1 | 31.1 | 1.54x | 1.54x | 0.98x | 14.4x | 28.0x |
| nbody | compute | 33.2 | 20.9 | 1.49 | 1.01s | 159.4 | 5.26 | 6.31x | 3.97x | 0.28x | 192x | 30.3x |
| richards | macro | 2.70s | 268.0 | 31.2 | 2.53s | 192.3 | 46.4 | 58.1x | 5.77x | 0.67x | 54.5x | 4.14x |
| json | macro | 7.78 | 2.52 | 0.278 | 99.6 | 11.2 | 2.59 | 3.00x | 0.97x | 0.11x | 38.4x | 4.31x |
| deltablue | macro | 99.1 | 101.6 | 1.17 | 2.03s | 99.4 | 11.8 | 8.39x | 8.60x | 0.10x | 172x | 8.41x |
| havlak | macro | 56.4 | 56.4 | 1.89 | 106.96s | 3.28s | 99.4 | 0.57x | 0.57x | 0.02x | 1077x | 33.0x |
| cd | macro | 884.0 | 255.3 | 15.6 | 28.49s | 965.1 | 35.8 | 24.7x | 7.13x | 0.44x | 795x | 26.9x |

## BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 10.9 | 4.80 | 3.04 | 160.5 | 23.4 | 4.02 | 2.70x | 1.19x | 0.76x | 39.9x | 5.81x |
| fannkuch | permutation | 0.331 | 1.42 | 0.155 | 60.0 | 7.18 | 3.99 | 0.08x | 0.36x | 0.04x | 15.0x | 1.80x |
| fasta | generation | 0.780 | 0.873 | 0.241 | 26.1 | 8.82 | 6.13 | 0.13x | 0.14x | 0.04x | 4.26x | 1.44x |
| knucleotide | hashing | 4.30 | 4.79 | 0.285 | 163.1 | --- | 4.95 | 0.87x | 0.97x | 0.06x | 33.0x | --- |
| pidigits | bignum | 0.300 | 0.299 | 0.047 | 0.423 | 0.133 | 1.89 | 0.16x | 0.16x | 0.02x | 0.22x | 0.07x |
| regexredux | regex | 1.27 | 1.27 | 1.17 | 51.6 | --- | 2.42 | 0.53x | 0.52x | 0.48x | 21.3x | --- |
| revcomp | string | 1.40 | 1.18 | 0.379 | 32.1 | --- | 3.32 | 0.42x | 0.35x | 0.11x | 9.66x | --- |
| spectralnorm | numeric | 44.5 | 2.03 | 0.357 | 319.0 | 64.2 | 2.60 | 17.1x | 0.78x | 0.14x | 123x | 24.7x |

## KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 301.4 | 358.7 | 28.5 | 3.73s | 879.3 | 33.4 | 9.02x | 10.7x | 0.85x | 112x | 26.3x |
| matmul | numeric | 12.7 | 11.5 | 6.02 | 1.26s | 538.6 | 15.3 | 0.83x | 0.75x | 0.39x | 82.2x | 35.2x |
| primes | numeric | 65.4 | 3.33 | 1.60 | 3.85s | 94.5 | 4.40 | 14.9x | 0.76x | 0.36x | 874x | 21.5x |
| base64 | string | 50.6 | 46.0 | 0.556 | 865.8 | 157.5 | 17.3 | 2.93x | 2.66x | 0.03x | 50.2x | 9.13x |
| levenshtein | string | 34.7 | 7.29 | 0.903 | 428.5 | 54.3 | 3.93 | 8.83x | 1.86x | 0.23x | 109x | 13.8x |
| json_gen | data | 20.8 | 21.6 | 1.50 | 52.3 | 20.0 | 6.21 | 3.36x | 3.48x | 0.24x | 8.42x | 3.23x |
| collatz | numeric | 418.6 | 404.3 | 225.7 | 5.45s | 6.26s | 1.42s | 0.29x | 0.28x | 0.16x | 3.84x | 4.41x |

## LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 472.2 | 187.3 | 60.3 | 11.22s | 2.19s | 66.6 | 7.09x | 2.81x | 0.90x | 168x | 32.9x |
| array1 | array | 1.08 | 1.09 | 0.319 | 78.7 | 35.8 | 1.90 | 0.57x | 0.57x | 0.17x | 41.4x | 18.8x |
| deriv | symbolic | 35.3 | 10.7 | 2.92 | 338.9 | 59.9 | 3.66 | 9.65x | 2.92x | 0.80x | 92.6x | 16.4x |
| diviter | iterative | 399.4 | 399.5 | 266.3 | 11.35s | 26.72s | 471.5 | 0.85x | 0.85x | 0.56x | 24.1x | 56.7x |
| divrec | recursive | 15.6 | 2.01 | 4.85 | 44.7 | 36.0 | 7.56 | 2.07x | 0.27x | 0.64x | 5.92x | 4.77x |
| gcbench | allocation | 253.6 | 148.0 | 70.6 | 3.92s | 546.9 | 23.3 | 10.9x | 6.36x | 3.04x | 169x | 23.5x |
| paraffins | combinat | 1.89 | 0.733 | 0.048 | 4.09 | 2.54 | 0.986 | 1.92x | 0.74x | 0.05x | 4.15x | 2.57x |
| pnpoly | numeric | 16.0 | 16.4 | 1.94 | 115.6 | 202.0 | 5.80 | 2.76x | 2.83x | 0.33x | 19.9x | 34.8x |
| puzzle | search | 9.25 | 4.45 | 1.27 | 108.8 | 29.8 | 3.27 | 2.83x | 1.36x | 0.39x | 33.3x | 9.11x |
| quicksort | sorting | 10.2 | 4.78 | 0.197 | 399.7 | 19.2 | 1.62 | 6.28x | 2.95x | 0.12x | 247x | 11.8x |
| ray | numeric | 10.6 | 0.834 | 0.171 | 18.3 | 13.8 | 3.53 | 2.99x | 0.24x | 0.05x | 5.17x | 3.90x |

## JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 12.0 | 11.1 | 0.523 | 2.24s | --- | 17.7 | 0.68x | 0.63x | 0.03x | 127x | --- |
| navier_stokes | numeric | 1.00s | 182.3 | 48.4 | --- | 98.6 | 14.2 | 70.7x | 12.8x | 3.41x | --- | 6.95x |
| splay | data | 151.2 | 271.4 | 20.4 | 1.09s | 148.5 | 19.2 | 7.88x | 14.1x | 1.06x | 57.0x | 7.73x |
| hashmap | data | 161.3 | 52.5 | 2.73 | --- | 316.0 | 15.1 | 10.7x | 3.47x | 0.18x | --- | 20.9x |
| crypto_sha1 | crypto | 66.3 | 28.2 | 2.70 | 600.9 | 218.6 | 8.75 | 7.58x | 3.23x | 0.31x | 68.7x | 25.0x |
| raytrace3d | 3d | 285.1 | 59.8 | 2.21 | 1.02s | --- | 18.4 | 15.5x | 3.25x | 0.12x | 55.2x | --- |

## Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 1.29s | 412.2 | 13.1 | 2.70s | 612.9 | 39.4 | 32.7x | 10.5x | 0.33x | 68.7x | 15.6x |
| microdiff | data-diff | 0.829 | 0.831 | 0.020 | 1.48s | 108.8 | 16.3 | 0.05x | 0.05x | 0.001x | 91.1x | 6.69x |
| hyphen | hyphenation | 2.55 | 3.44 | 0.090 | 381.3 | 52.2 | 6.52 | 0.39x | 0.53x | 0.01x | 58.5x | 8.01x |
