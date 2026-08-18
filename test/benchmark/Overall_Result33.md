# Lambda Benchmark Results: Result33

- **Date:** 2026-08-18
- **Platform:** Darwin arm64
- **Lambda commit:** `8705d85c5a7c1851bde07be4d3dde97d831dc097`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v33-8705d85c5a` (21,119,576 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 185.60s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 185.5s (batched 184.6s: sync 113.8s, async 70.9s; non-batched 0.9s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v33.json`
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 9 | 10 | 0.50x | 0.37x | 0.19x | 11.9x | 6.41x |
| AWFY | 14 | 14 | 14 | 9 | 14 | 14 | 14 | 1.75x | 0.97x | 0.08x | 42.7x | 5.25x |
| BENG | 8 | 8 | 8 | 7 | 8 | 5 | 8 | 0.51x | 0.39x | 0.12x | 11.9x | 1.88x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 2.22x | 1.42x | 0.23x | 54.2x | 11.9x |
| LARCENY | 11 | 11 | 11 | 10 | 11 | 11 | 11 | 2.50x | 0.88x | 0.30x | 33.2x | 13.3x |
| JetStream | 6 | 6 | 6 | 1 | 6 | 4 | 6 | 6.18x | 3.97x | 0.19x | 76.1x | 12.9x |
| Text | 3 | 3 | 3 | 3 | 3 | 3 | 3 | 0.53x | 0.42x | 0.02x | 67.7x | 9.50x |
| **Overall** | 59 | 59 | 59 | 47 | 59 | 53 | 59 | 1.41x | 0.83x | 0.14x | 30.8x | 7.38x |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 4.30x over 47 of 59 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| text/microdiff | 0.918 | 0.016 | 57.0x |
| kostya/base64 | 17.0 | 0.558 | 30.5x |
| awfy/list | 0.608 | 0.023 | 26.5x |
| text/hyphen | 2.15 | 0.087 | 24.7x |
| jetstream/hashmap | 52.9 | 2.84 | 18.6x |
| beng/knucleotide | 4.80 | 0.283 | 17.0x |
| awfy/towers | 0.416 | 0.027 | 15.4x |
| awfy/queens | 0.312 | 0.021 | 14.9x |
| kostya/json_gen | 21.5 | 1.51 | 14.3x |
| kostya/brainfuck | 396.2 | 28.3 | 14.0x |
| awfy/nbody | 20.2 | 1.51 | 13.4x |
| text/fast_diff | 156.1 | 13.0 | 12.0x |

---

## Notable Results

- Missing timings: **18** cells
- QuickJS missing: r7rs/ack (exit_1), beng/knucleotide (exit_1), beng/regexredux (exit_1), beng/revcomp (exit_1), jetstream/cube3d (exit_1), jetstream/raytrace3d (exit_1)
- C2MIR missing: awfy/richards (missing_port), awfy/json (missing_port), awfy/deltablue (missing_port), awfy/havlak (missing_port), awfy/cd (missing_port), beng/pidigits (missing_port), larceny/deriv (missing_port), jetstream/cube3d (missing_port), +4 more

### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/havlak | 107.10s | 95.4 | 1123x |
| kostya/primes | 3.84s | 4.37 | 880x |
| awfy/cd | 27.95s | 35.8 | 782x |
| jetstream/hashmap | 4.98s | 15.3 | 325x |
| larceny/quicksort | 399.2 | 1.64 | 244x |
| awfy/nbody | 1.00s | 5.33 | 188x |
| awfy/deltablue | 1.99s | 11.6 | 171x |
| larceny/gcbench | 3.93s | 23.3 | 169x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.417 | 1.93 | 0.22x |

---

## R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.31 | 1.30 | 1.02 | 39.8 | 18.9 | 1.79 | 0.73x | 0.73x | 0.57x | 22.3x | 10.6x |
| fibfp | recursive | 2.00 | 1.21 | 1.10 | 39.9 | 19.0 | 1.92 | 1.04x | 0.63x | 0.57x | 20.7x | 9.85x |
| tak | recursive | 0.128 | 0.163 | 0.121 | 2.58 | 2.82 | 0.803 | 0.16x | 0.20x | 0.15x | 3.21x | 3.51x |
| cpstak | closure | 0.251 | 0.320 | 0.240 | 5.27 | 5.57 | 0.972 | 0.26x | 0.33x | 0.25x | 5.42x | 5.73x |
| sum | iterative | 0.825 | 0.824 | 0.271 | 25.3 | 31.1 | 1.18 | 0.70x | 0.70x | 0.23x | 21.5x | 26.5x |
| sumfp | iterative | 0.069 | 0.069 | 0.077 | 2.61 | 3.66 | 0.861 | 0.08x | 0.08x | 0.09x | 3.03x | 4.25x |
| nqueens | backtrack | 1.59 | 1.12 | 0.127 | 38.4 | 7.89 | 1.73 | 0.92x | 0.65x | 0.07x | 22.2x | 4.55x |
| fft | numeric | 0.211 | 0.252 | 0.024 | 52.9 | 2.78 | 1.60 | 0.13x | 0.16x | 0.01x | 33.1x | 1.74x |
| mbrot | numeric | 10.7 | 0.535 | 0.445 | 16.0 | 17.6 | 1.79 | 5.95x | 0.30x | 0.25x | 8.92x | 9.82x |
| ack | recursive | 10.7 | 13.2 | 11.8 | 220.6 | --- | 13.4 | 0.80x | 0.98x | 0.88x | 16.5x | --- |

## AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.032 | 0.036 | 0.018 | 10.1 | 0.621 | 0.385 | 0.08x | 0.09x | 0.05x | 26.3x | 1.61x |
| permute | micro | 0.665 | 0.132 | 0.025 | 11.1 | 1.53 | 0.810 | 0.82x | 0.16x | 0.03x | 13.7x | 1.88x |
| queens | micro | 0.533 | 0.312 | 0.021 | 6.80 | 1.06 | 0.641 | 0.83x | 0.49x | 0.03x | 10.6x | 1.65x |
| towers | micro | 1.10 | 0.416 | 0.027 | 30.9 | 2.31 | 1.11 | 0.99x | 0.37x | 0.02x | 27.8x | 2.08x |
| bounce | micro | 0.115 | 0.183 | 0.024 | 7.33 | 0.872 | 0.541 | 0.21x | 0.34x | 0.04x | 13.6x | 1.61x |
| list | micro | 0.666 | 0.608 | 0.023 | 3.39 | 0.905 | 0.486 | 1.37x | 1.25x | 0.05x | 6.98x | 1.86x |
| storage | micro | 0.799 | 0.523 | 0.094 | 7.30 | 2.18 | 0.634 | 1.26x | 0.83x | 0.15x | 11.5x | 3.44x |
| mandelbrot | compute | 38.5 | 38.5 | 30.6 | 438.7 | 867.4 | 31.1 | 1.24x | 1.24x | 0.98x | 14.1x | 27.9x |
| nbody | compute | 32.2 | 20.2 | 1.51 | 1.00s | 159.7 | 5.33 | 6.04x | 3.79x | 0.28x | 188x | 30.0x |
| richards | macro | 2.59s | 256.8 | --- | 1.80s | 190.2 | 46.8 | 55.3x | 5.49x | --- | 38.5x | 4.06x |
| json | macro | 8.67 | 2.49 | --- | 98.5 | 10.9 | 2.61 | 3.32x | 0.95x | --- | 37.7x | 4.19x |
| deltablue | macro | 86.9 | 88.4 | --- | 1.99s | 99.8 | 11.6 | 7.46x | 7.59x | --- | 171x | 8.57x |
| havlak | macro | 50.4 | 50.8 | --- | 107.10s | 3.29s | 95.4 | 0.53x | 0.53x | --- | 1123x | 34.4x |
| cd | macro | 822.6 | 229.4 | --- | 27.95s | 964.5 | 35.8 | 23.0x | 6.41x | --- | 782x | 27.0x |

## BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 9.18 | 4.64 | 3.01 | 159.9 | 23.5 | 4.08 | 2.25x | 1.14x | 0.74x | 39.2x | 5.77x |
| fannkuch | permutation | 0.337 | 0.778 | 0.149 | 60.0 | 7.18 | 4.00 | 0.08x | 0.19x | 0.04x | 15.0x | 1.80x |
| fasta | generation | 0.767 | 0.886 | 0.241 | 26.2 | 8.85 | 6.12 | 0.13x | 0.14x | 0.04x | 4.28x | 1.45x |
| knucleotide | hashing | 4.31 | 4.80 | 0.283 | 163.0 | --- | 5.01 | 0.86x | 0.96x | 0.06x | 32.6x | --- |
| pidigits | bignum | 0.299 | 0.293 | --- | 0.417 | 0.130 | 1.93 | 0.15x | 0.15x | --- | 0.22x | 0.07x |
| regexredux | regex | 1.27 | 1.27 | 1.14 | 51.7 | --- | 2.45 | 0.52x | 0.52x | 0.47x | 21.1x | --- |
| revcomp | string | 1.23 | 1.23 | 0.376 | 32.0 | --- | 3.37 | 0.37x | 0.36x | 0.11x | 9.51x | --- |
| spectralnorm | numeric | 21.9 | 1.62 | 0.351 | 317.0 | 63.7 | 2.73 | 8.03x | 0.59x | 0.13x | 116x | 23.3x |

## KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 309.8 | 396.2 | 28.3 | 3.70s | 872.8 | 33.6 | 9.23x | 11.8x | 0.84x | 110x | 26.0x |
| matmul | numeric | 11.8 | 11.4 | 6.10 | 1.25s | 540.1 | 15.3 | 0.77x | 0.75x | 0.40x | 81.3x | 35.2x |
| primes | numeric | 18.9 | 3.44 | 1.59 | 3.84s | 95.3 | 4.37 | 4.33x | 0.79x | 0.36x | 880x | 21.8x |
| base64 | string | 16.8 | 17.0 | 0.558 | 870.2 | 157.2 | 17.3 | 0.97x | 0.99x | 0.03x | 50.4x | 9.11x |
| levenshtein | string | 34.9 | 7.15 | 0.903 | 431.6 | 54.2 | 4.07 | 8.58x | 1.76x | 0.22x | 106x | 13.3x |
| json_gen | data | 20.7 | 21.5 | 1.51 | 52.3 | 19.9 | 6.19 | 3.34x | 3.47x | 0.24x | 8.45x | 3.21x |
| collatz | numeric | 434.9 | 403.7 | 225.5 | 5.52s | 6.24s | 1.42s | 0.31x | 0.28x | 0.16x | 3.88x | 4.39x |

## LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 489.0 | 210.0 | 60.4 | 11.20s | 2.18s | 66.4 | 7.36x | 3.16x | 0.91x | 169x | 32.8x |
| array1 | array | 0.828 | 0.821 | 0.319 | 77.0 | 35.9 | 1.90 | 0.43x | 0.43x | 0.17x | 40.4x | 18.8x |
| deriv | symbolic | 35.3 | 10.6 | --- | 337.8 | 58.8 | 3.69 | 9.55x | 2.88x | --- | 91.5x | 15.9x |
| diviter | iterative | 399.6 | 399.6 | 263.1 | 12.91s | 26.67s | 464.3 | 0.86x | 0.86x | 0.57x | 27.8x | 57.4x |
| divrec | recursive | 15.7 | 2.00 | 4.84 | 44.8 | 36.9 | 7.50 | 2.09x | 0.27x | 0.65x | 5.97x | 4.92x |
| gcbench | allocation | 210.7 | 148.1 | 71.2 | 3.93s | 550.9 | 23.3 | 9.03x | 6.35x | 3.05x | 169x | 23.6x |
| paraffins | combinat | 0.511 | 0.284 | 0.047 | 4.02 | 2.55 | 1.01 | 0.51x | 0.28x | 0.05x | 3.98x | 2.53x |
| pnpoly | numeric | 13.9 | 15.7 | 1.92 | 114.6 | 202.2 | 5.87 | 2.37x | 2.67x | 0.33x | 19.5x | 34.4x |
| puzzle | search | 8.66 | 3.58 | 1.27 | 108.8 | 29.2 | 3.30 | 2.62x | 1.09x | 0.39x | 33.0x | 8.84x |
| quicksort | sorting | 9.94 | 1.09 | 0.198 | 399.2 | 19.1 | 1.64 | 6.07x | 0.67x | 0.12x | 244x | 11.7x |
| ray | numeric | 8.94 | 0.297 | 0.167 | 17.7 | 13.8 | 3.55 | 2.52x | 0.08x | 0.05x | 4.99x | 3.88x |

## JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 11.2 | 10.9 | --- | 791.3 | --- | 17.5 | 0.64x | 0.62x | --- | 45.1x | --- |
| navier_stokes | numeric | 142.6 | 179.3 | --- | 1.04s | 98.6 | 14.3 | 9.99x | 12.6x | --- | 73.1x | 6.91x |
| splay | data | 137.5 | 256.6 | --- | 1.14s | 147.4 | 18.9 | 7.26x | 13.5x | --- | 60.2x | 7.78x |
| hashmap | data | 163.1 | 52.9 | 2.84 | 4.98s | 316.2 | 15.3 | 10.7x | 3.46x | 0.19x | 325x | 20.7x |
| crypto_sha1 | crypto | 64.4 | 30.6 | --- | 608.9 | 218.2 | 8.72 | 7.39x | 3.51x | --- | 69.8x | 25.0x |
| raytrace3d | 3d | 277.6 | 54.7 | --- | 782.6 | --- | 18.1 | 15.3x | 3.02x | --- | 43.2x | --- |

## Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 257.0 | 156.1 | 13.0 | 2.61s | 613.8 | 39.2 | 6.56x | 3.99x | 0.33x | 66.6x | 15.7x |
| microdiff | data-diff | 0.930 | 0.918 | 0.016 | 1.30s | 108.5 | 16.3 | 0.06x | 0.06x | 0.001x | 79.7x | 6.67x |
| hyphen | hyphenation | 2.52 | 2.15 | 0.087 | 377.1 | 52.9 | 6.45 | 0.39x | 0.33x | 0.01x | 58.4x | 8.20x |
