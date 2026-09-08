# Lambda Benchmark Results: Result38

- **Date:** 2026-09-08
- **Platform:** Darwin arm64
- **Lambda commit:** `b23793e8326eea98ac05a445c6bf209d9b5452a6`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v38-b23793e832` (18,979,160 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 81.00s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 80.9s (batched 80.1s: sync 62.8s, async 17.3s; non-batched 0.7s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v38.json`
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Part 1 — Execution time (self-reported)

Each engine's own `__TIMING__` figure: the timed workload only, with startup and compilation outside the measured region. This is the historical series, comparable back through Result18, and the MIR columns pin `LAMBDA_TIER=jit`.

### Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 0.37x | 0.39x | 0.19x | 12.4x | 6.74x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 1.31x | 0.94x | 0.09x | 37.1x | 5.23x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 0.38x | 0.37x | 0.10x | 11.9x | 1.71x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 1.67x | 1.44x | 0.23x | 56.4x | 12.0x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 0.97x | 0.83x | 0.33x | 33.3x | 13.3x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 5.52x | 3.74x | 0.29x | 73.1x | 12.1x |
| Text | 7 | 7 | 7 | 3 | 7 | 7 | 7 | 2.06x | 1.77x | 0.02x | 64.6x | 12.0x |
| **Overall** | 63 | 63 | 63 | 59 | 63 | 63 | 63 | 1.08x | 0.91x | 0.15x | 31.5x | 7.24x |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 5.05x over 59 of 63 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| awfy/deltablue | 122.3 | 1.15 | 106x |
| awfy/havlak | 75.3 | 1.83 | 41.1x |
| jetstream/hashmap | 90.0 | 2.73 | 32.9x |
| awfy/cd | 477.7 | 15.0 | 31.8x |
| jetstream/cube3d | 13.8 | 0.532 | 25.9x |
| text/hyphen | 2.09 | 0.088 | 23.7x |
| kostya/base64 | 11.4 | 0.554 | 20.6x |
| beng/knucleotide | 5.10 | 0.278 | 18.3x |
| kostya/json_gen | 26.2 | 1.50 | 17.5x |
| awfy/queens | 0.316 | 0.019 | 16.6x |
| jetstream/splay | 292.8 | 18.7 | 15.7x |
| awfy/towers | 0.406 | 0.027 | 15.0x |

---

### Notable Results

- Missing timings: **4** cells
- C2MIR missing: text/prettier_ast (missing_port), text/text_search (missing_port), text/three_way_merge (missing_port), text/log_pipeline (missing_port)

#### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/havlak | 82.50s | 95.8 | 861x |
| kostya/primes | 3.72s | 4.39 | 848x |
| awfy/cd | 25.93s | 35.4 | 733x |
| jetstream/hashmap | 5.02s | 15.3 | 329x |
| text/prettier_ast | 27.87s | 99.1 | 281x |
| larceny/quicksort | 399.6 | 1.64 | 244x |
| larceny/gcbench | 4.06s | 23.3 | 175x |
| larceny/triangl | 11.24s | 66.4 | 169x |

#### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.410 | 1.91 | 0.21x |

### R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.30 | 1.30 | 1.12 | 42.5 | 18.9 | 1.80 | 0.72x | 0.72x | 0.63x | 23.6x | 10.5x |
| fibfp | recursive | 2.04 | 1.22 | 1.14 | 42.8 | 25.5 | 1.82 | 1.12x | 0.67x | 0.63x | 23.5x | 14.0x |
| tak | recursive | 0.125 | 0.161 | 0.121 | 2.90 | 2.80 | 0.797 | 0.16x | 0.20x | 0.15x | 3.63x | 3.52x |
| cpstak | closure | 0.244 | 0.324 | 0.227 | 5.64 | 5.56 | 0.975 | 0.25x | 0.33x | 0.23x | 5.79x | 5.70x |
| sum | iterative | 0.836 | 0.824 | 0.272 | 25.9 | 31.0 | 1.19 | 0.71x | 0.70x | 0.23x | 21.9x | 26.1x |
| sumfp | iterative | 0.068 | 0.069 | 0.077 | 2.62 | 3.71 | 0.875 | 0.08x | 0.08x | 0.09x | 3.00x | 4.24x |
| nqueens | backtrack | 0.975 | 1.46 | 0.132 | 39.9 | 7.96 | 1.78 | 0.55x | 0.82x | 0.07x | 22.5x | 4.48x |
| fft | numeric | 0.269 | 0.217 | 0.024 | 53.1 | 2.79 | 1.59 | 0.17x | 0.14x | 0.02x | 33.4x | 1.76x |
| mbrot | numeric | 0.693 | 0.645 | 0.445 | 15.9 | 17.7 | 1.79 | 0.39x | 0.36x | 0.25x | 8.90x | 9.89x |
| ack | recursive | 11.3 | 14.3 | 11.4 | 230.1 | 101.1 | 13.3 | 0.84x | 1.07x | 0.86x | 17.3x | 7.58x |

### AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.032 | 0.036 | 0.016 | 10.1 | 0.613 | 0.382 | 0.08x | 0.09x | 0.04x | 26.5x | 1.61x |
| permute | micro | 0.272 | 0.134 | 0.028 | 11.1 | 1.53 | 0.811 | 0.34x | 0.17x | 0.03x | 13.7x | 1.89x |
| queens | micro | 0.387 | 0.316 | 0.019 | 6.55 | 1.05 | 0.665 | 0.58x | 0.48x | 0.03x | 9.84x | 1.58x |
| towers | micro | 0.676 | 0.406 | 0.027 | 19.9 | 2.36 | 1.10 | 0.61x | 0.37x | 0.02x | 18.0x | 2.14x |
| bounce | micro | 0.060 | 0.066 | 0.024 | 6.97 | 0.872 | 0.539 | 0.11x | 0.12x | 0.04x | 12.9x | 1.62x |
| list | micro | 0.690 | 0.211 | 0.024 | 3.11 | 0.906 | 0.486 | 1.42x | 0.43x | 0.05x | 6.41x | 1.86x |
| storage | micro | 0.509 | 0.325 | 0.094 | 7.11 | 2.07 | 0.625 | 0.81x | 0.52x | 0.15x | 11.4x | 3.31x |
| mandelbrot | compute | 39.6 | 39.6 | 30.6 | 447.4 | 868.8 | 31.0 | 1.28x | 1.28x | 0.99x | 14.4x | 28.0x |
| nbody | compute | 44.1 | 15.4 | 1.49 | 715.8 | 158.6 | 5.29 | 8.33x | 2.92x | 0.28x | 135x | 30.0x |
| richards | macro | 415.1 | 418.9 | 29.3 | 1.33s | 191.7 | 47.2 | 8.79x | 8.87x | 0.62x | 28.2x | 4.06x |
| json | macro | 7.37 | 3.89 | 0.260 | 92.5 | 10.7 | 2.62 | 2.81x | 1.49x | 0.10x | 35.3x | 4.09x |
| deltablue | macro | 124.2 | 122.3 | 1.15 | 1.44s | 99.8 | 11.5 | 10.8x | 10.6x | 0.10x | 125x | 8.69x |
| havlak | macro | 71.3 | 75.3 | 1.83 | 82.50s | 3.32s | 95.8 | 0.74x | 0.79x | 0.02x | 861x | 34.6x |
| cd | macro | 578.8 | 477.7 | 15.0 | 25.93s | 951.4 | 35.4 | 16.4x | 13.5x | 0.42x | 733x | 26.9x |

### BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 8.14 | 4.76 | 3.01 | 164.1 | 23.7 | 4.03 | 2.02x | 1.18x | 0.75x | 40.8x | 5.89x |
| fannkuch | permutation | 0.322 | 0.391 | 0.149 | 59.9 | 7.10 | 3.94 | 0.08x | 0.10x | 0.04x | 15.2x | 1.80x |
| fasta | generation | 0.769 | 0.888 | 0.241 | 25.7 | 8.70 | 6.06 | 0.13x | 0.15x | 0.04x | 4.24x | 1.44x |
| knucleotide | hashing | 4.55 | 5.10 | 0.278 | 161.4 | 7.63 | 4.81 | 0.95x | 1.06x | 0.06x | 33.6x | 1.59x |
| pidigits | bignum | 0.301 | 0.303 | 0.045 | 0.410 | 0.127 | 1.91 | 0.16x | 0.16x | 0.02x | 0.21x | 0.07x |
| regexredux | regex | 1.27 | 1.27 | 1.13 | 48.8 | 5.59 | 2.38 | 0.53x | 0.53x | 0.47x | 20.5x | 2.35x |
| revcomp | string | 1.23 | 1.17 | 0.374 | 28.4 | 2.55 | 3.25 | 0.38x | 0.36x | 0.12x | 8.73x | 0.78x |
| spectralnorm | numeric | 1.64 | 1.64 | 0.351 | 314.5 | 63.4 | 2.57 | 0.64x | 0.64x | 0.14x | 123x | 24.7x |

### KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 225.5 | 316.3 | 28.3 | 3.63s | 876.3 | 33.3 | 6.78x | 9.51x | 0.85x | 109x | 26.3x |
| matmul | numeric | 15.1 | 23.6 | 6.02 | 1.28s | 536.4 | 15.3 | 0.99x | 1.55x | 0.39x | 83.5x | 35.1x |
| primes | numeric | 14.8 | 3.39 | 1.58 | 3.72s | 94.1 | 4.39 | 3.37x | 0.77x | 0.36x | 848x | 21.4x |
| base64 | string | 11.0 | 11.4 | 0.554 | 856.3 | 155.5 | 17.1 | 0.65x | 0.67x | 0.03x | 50.1x | 9.09x |
| levenshtein | string | 10.2 | 6.29 | 0.902 | 434.2 | 54.0 | 3.94 | 2.59x | 1.60x | 0.23x | 110x | 13.7x |
| json_gen | data | 24.6 | 26.2 | 1.50 | 62.8 | 19.9 | 6.09 | 4.04x | 4.31x | 0.25x | 10.3x | 3.27x |
| collatz | numeric | 338.4 | 343.3 | 224.3 | 5.85s | 6.23s | 1.42s | 0.24x | 0.24x | 0.16x | 4.12x | 4.39x |

### LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 263.0 | 201.0 | 60.4 | 11.24s | 2.18s | 66.4 | 3.96x | 3.03x | 0.91x | 169x | 32.9x |
| array1 | array | 0.823 | 0.813 | 0.316 | 79.3 | 36.0 | 1.90 | 0.43x | 0.43x | 0.17x | 41.8x | 19.0x |
| deriv | symbolic | 28.4 | 13.9 | 2.86 | 351.6 | 58.9 | 3.73 | 7.62x | 3.72x | 0.77x | 94.4x | 15.8x |
| diviter | iterative | 265.3 | 254.8 | 254.7 | 11.61s | 26.71s | 463.8 | 0.57x | 0.55x | 0.55x | 25.0x | 57.6x |
| divrec | recursive | 5.39 | 2.00 | 4.86 | 45.9 | 36.2 | 7.57 | 0.71x | 0.26x | 0.64x | 6.07x | 4.79x |
| gcbench | allocation | 191.0 | 178.5 | 71.1 | 4.06s | 545.5 | 23.3 | 8.21x | 7.67x | 3.06x | 175x | 23.4x |
| paraffins | combinat | 0.212 | 0.234 | 0.049 | 4.12 | 2.54 | 0.990 | 0.21x | 0.24x | 0.05x | 4.16x | 2.57x |
| pnpoly | numeric | 12.6 | 14.5 | 1.91 | 107.9 | 202.4 | 5.89 | 2.14x | 2.46x | 0.33x | 18.3x | 34.4x |
| puzzle | search | 2.40 | 2.42 | 1.27 | 109.6 | 29.3 | 3.27 | 0.73x | 0.74x | 0.39x | 33.5x | 8.95x |
| quicksort | sorting | 0.976 | 1.07 | 0.199 | 399.6 | 19.2 | 1.64 | 0.60x | 0.65x | 0.12x | 244x | 11.7x |
| ray | numeric | 0.295 | 0.308 | 0.170 | 17.9 | 13.8 | 3.49 | 0.08x | 0.09x | 0.05x | 5.13x | 3.95x |

### JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 12.9 | 13.8 | 0.532 | 729.6 | 216.4 | 17.8 | 0.73x | 0.77x | 0.03x | 41.0x | 12.2x |
| navier_stokes | numeric | 153.3 | 152.3 | 46.9 | 1.07s | 98.6 | 14.1 | 10.9x | 10.8x | 3.32x | 76.1x | 6.98x |
| splay | data | 301.7 | 292.8 | 18.7 | 1.04s | 147.1 | 19.3 | 15.6x | 15.2x | 0.97x | 53.6x | 7.61x |
| hashmap | data | 148.2 | 90.0 | 2.73 | 5.02s | 326.3 | 15.3 | 9.72x | 5.90x | 0.18x | 329x | 21.4x |
| crypto_sha1 | crypto | 53.5 | 28.9 | 2.65 | 630.3 | 218.9 | 8.68 | 6.16x | 3.33x | 0.31x | 72.6x | 25.2x |
| raytrace3d | 3d | 70.3 | 20.0 | 2.19 | 693.8 | 161.7 | 18.3 | 3.85x | 1.09x | 0.12x | 38.0x | 8.86x |

### Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 244.4 | 142.8 | 13.0 | 2.52s | 609.5 | 39.4 | 6.20x | 3.62x | 0.33x | 63.9x | 15.5x |
| microdiff | data-diff | 0.144 | 0.141 | 0.016 | 1.15s | 109.1 | 16.3 | 0.009x | 0.009x | 0.001x | 70.9x | 6.70x |
| hyphen | hyphenation | 3.49 | 2.09 | 0.088 | 374.0 | 51.0 | 6.72 | 0.52x | 0.31x | 0.01x | 55.6x | 7.59x |
| prettier_ast | formatting | 1.12s | 1.12s* | --- | 27.87s | 1.42s | 99.1 | 11.3x | 11.3x | --- | 281x | 14.4x |
| text_search | search | 15.22s | 15.22s* | --- | 95.94s | 38.87s | 781.7 | 19.5x | 19.5x | --- | 123x | 49.7x |
| three_way_merge | merge | 3.65s | 3.65s* | --- | 19.11s | 6.20s | 967.0 | 3.77x | 3.77x | --- | 19.8x | 6.42x |
| log_pipeline | log-processing | 6.34s | 6.34s* | --- | 25.95s | 9.47s | 946.7 | 6.70x | 6.70x | --- | 27.4x | 10.00x |

---

## Part 2 — End-to-end time (wall clock, auto tier)

Wall clock from process invocation to exit, so **every engine pays its own startup and compilation inside the number**. The MIR columns use the shipped auto tier -- no `LAMBDA_TIER` override -- which is what `lambda.exe run script.ls` actually does.

This set exists because the two questions are different. Part 1 asks how fast the compiled workload runs; part 2 asks how long it takes to run the script. Timing the auto tier under part 1's rules would charge Lambda for JIT compilation performed *inside* the measured region while crediting Node.js with a post-warmup figure -- comparing two different things. Here the accounting is the same for everyone.

Same processes, where possible: the reference engines report their wall and `__TIMING__` figures from the *same* run, so parts 1 and 2 are two readings of one launch. Only the MIR columns are re-run, because part 1 pins the JIT and part 2 must use the auto tier.

⚠ Short workloads are dominated by fixed process startup here, so a row whose part-1 time is a fraction of a millisecond says more about executable launch cost than about the language. Read part 2 by the longer rows.

### Summary

| Suite | Total | Timed MIR (untyped, auto) | Timed MIR (typed, auto) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped, auto)/Node geo | MIR (typed, auto)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 0.40x | 0.41x | 1.02x | 1.54x | 0.42x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 0.98x | 0.97x | 0.93x | 7.22x | 0.81x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 0.45x | 0.48x | 1.01x | 2.53x | 0.32x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 0.72x | 0.74x | 0.72x | 14.2x | 2.91x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 0.72x | 0.66x | 1.00x | 6.40x | 1.91x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 2.87x | 3.86x | 0.91x | 22.4x | 3.70x |
| Text | 7 | 7 | 7 | 3 | 7 | 7 | 7 | 2.86x | 2.61x | 0.76x | 41.8x | 6.48x |
| **Overall** | 63 | 63 | 63 | 59 | 63 | 63 | 63 | 0.88x | 0.89x | 0.93x | 7.06x | 1.27x |

> Ratio < 1.0 means the engine finished the whole run faster than Node.js.

### R7RS

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 16.2 | 16.4 | 47.6 | 77.2 | 24.9 | 46.0 | 0.35x | 0.36x | 1.03x | 1.68x | 0.54x |
| fibfp | recursive | 17.5 | 16.9 | 47.5 | 77.0 | 36.2 | 47.4 | 0.37x | 0.36x | 1.00x | 1.62x | 0.76x |
| tak | recursive | 16.5 | 16.2 | 46.6 | 39.3 | 8.55 | 45.1 | 0.37x | 0.36x | 1.03x | 0.87x | 0.19x |
| cpstak | closure | 16.8 | 16.7 | 45.7 | 41.8 | 11.1 | 44.2 | 0.38x | 0.38x | 1.03x | 0.95x | 0.25x |
| sum | iterative | 16.4 | 16.3 | 45.3 | 59.1 | 36.9 | 44.0 | 0.37x | 0.37x | 1.03x | 1.34x | 0.84x |
| sumfp | iterative | 14.6 | 14.7 | 45.0 | 35.2 | 9.19 | 43.9 | 0.33x | 0.33x | 1.03x | 0.80x | 0.21x |
| nqueens | backtrack | 23.0 | 24.0 | 44.9 | 118.5 | 13.4 | 45.9 | 0.50x | 0.52x | 0.98x | 2.58x | 0.29x |
| fft | numeric | 20.5 | 24.3 | 45.7 | 96.5 | 8.35 | 44.8 | 0.46x | 0.54x | 1.02x | 2.15x | 0.19x |
| mbrot | numeric | 18.5 | 18.6 | 45.3 | 52.2 | 23.6 | 44.7 | 0.41x | 0.42x | 1.02x | 1.17x | 0.53x |
| ack | recursive | 26.9 | 29.8 | 56.2 | 263.9 | 107.2 | 56.3 | 0.48x | 0.53x | 1.00x | 4.69x | 1.90x |

### AWFY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 15.6 | 15.9 | 45.3 | 49.3 | 6.41 | 44.4 | 0.35x | 0.36x | 1.02x | 1.11x | 0.14x |
| permute | micro | 17.7 | 18.1 | 47.0 | 53.9 | 7.83 | 45.8 | 0.39x | 0.40x | 1.03x | 1.18x | 0.17x |
| queens | micro | 22.4 | 23.2 | 50.8 | 51.3 | 6.64 | 45.2 | 0.50x | 0.51x | 1.12x | 1.14x | 0.15x |
| towers | micro | 21.8 | 21.8 | 45.9 | 65.5 | 8.65 | 45.2 | 0.48x | 0.48x | 1.02x | 1.45x | 0.19x |
| bounce | micro | 23.2 | 28.7 | 45.5 | 130.4 | 7.23 | 44.6 | 0.52x | 0.64x | 1.02x | 2.92x | 0.16x |
| list | micro | 19.5 | 19.3 | 45.3 | 45.6 | 6.45 | 44.7 | 0.44x | 0.43x | 1.01x | 1.02x | 0.14x |
| storage | micro | 17.4 | 17.7 | 45.2 | 122.2 | 8.40 | 45.0 | 0.39x | 0.39x | 1.00x | 2.72x | 0.19x |
| mandelbrot | compute | 56.4 | 56.2 | 76.1 | 493.9 | 875.7 | 76.4 | 0.74x | 0.74x | 1.00x | 6.47x | 11.5x |
| nbody | compute | 73.6 | 54.2 | 47.8 | 790.9 | 165.2 | 49.5 | 1.49x | 1.09x | 0.97x | 16.0x | 3.34x |
| richards | macro | 471.5 | 483.4 | 77.5 | 1.45s | 198.7 | 91.5 | 5.15x | 5.28x | 0.85x | 15.8x | 2.17x |
| json | macro | 51.7 | 52.8 | 52.6 | 297.5 | 18.1 | 47.0 | 1.10x | 1.12x | 1.12x | 6.33x | 0.39x |
| deltablue | macro | 235.1 | 233.5 | 53.8 | 1.69s | 107.7 | 57.7 | 4.08x | 4.05x | 0.93x | 29.3x | 1.87x |
| havlak | macro | 171.8 | 171.7 | 54.5 | 84.08s | 3.33s | 142.0 | 1.21x | 1.21x | 0.38x | 592x | 23.5x |
| cd | macro | 641.2 | 539.2 | 67.3 | 26.66s | 959.5 | 79.1 | 8.11x | 6.82x | 0.85x | 337x | 12.1x |

### BENG

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 25.8 | 24.6 | 48.3 | 207.6 | 28.7 | 46.0 | 0.56x | 0.54x | 1.05x | 4.52x | 0.63x |
| fannkuch | permutation | 19.2 | 21.2 | 44.2 | 99.2 | 12.2 | 45.6 | 0.42x | 0.46x | 0.97x | 2.18x | 0.27x |
| fasta | generation | 20.9 | 22.1 | 45.2 | 70.3 | 14.0 | 46.7 | 0.45x | 0.47x | 0.97x | 1.50x | 0.30x |
| knucleotide | hashing | 27.6 | 36.1 | 45.3 | 204.0 | 13.0 | 46.6 | 0.59x | 0.77x | 0.97x | 4.37x | 0.28x |
| pidigits | bignum | 18.6 | 18.9 | 46.5 | 42.0 | 5.25 | 43.0 | 0.43x | 0.44x | 1.08x | 0.98x | 0.12x |
| regexredux | regex | 13.9 | 14.0 | 46.9 | 93.3 | 10.9 | 44.3 | 0.31x | 0.32x | 1.06x | 2.11x | 0.25x |
| revcomp | string | 19.7 | 20.2 | 45.0 | 73.1 | 7.89 | 44.8 | 0.44x | 0.45x | 1.01x | 1.63x | 0.18x |
| spectralnorm | numeric | 21.6 | 23.2 | 44.6 | 356.5 | 69.5 | 45.9 | 0.47x | 0.50x | 0.97x | 7.76x | 1.51x |

### KOSTYA

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 247.4 | 339.3 | 72.6 | 3.67s | 882.8 | 76.8 | 3.22x | 4.42x | 0.94x | 47.8x | 11.5x |
| matmul | numeric | 33.4 | 41.8 | 50.1 | 1.32s | 542.2 | 57.1 | 0.58x | 0.73x | 0.88x | 23.1x | 9.49x |
| primes | numeric | 29.8 | 18.4 | 45.4 | 3.77s | 99.6 | 46.0 | 0.65x | 0.40x | 0.99x | 81.9x | 2.17x |
| base64 | string | 31.3 | 39.6 | 44.9 | 897.1 | 161.5 | 59.1 | 0.53x | 0.67x | 0.76x | 15.2x | 2.73x |
| levenshtein | string | 32.1 | 27.3 | 45.5 | 476.2 | 59.4 | 47.1 | 0.68x | 0.58x | 0.97x | 10.1x | 1.26x |
| json_gen | data | 47.0 | 47.0 | 45.9 | 102.4 | 25.1 | 49.3 | 0.95x | 0.95x | 0.93x | 2.08x | 0.51x |
| collatz | numeric | 363.5 | 365.0 | 269.2 | 5.88s | 6.24s | 1.46s | 0.25x | 0.25x | 0.18x | 4.02x | 4.26x |

### LARCENY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 287.2 | 226.9 | 105.8 | 11.29s | 2.19s | 111.0 | 2.59x | 2.04x | 0.95x | 102x | 19.7x |
| array1 | array | 15.6 | 15.5 | 44.9 | 113.9 | 41.9 | 44.5 | 0.35x | 0.35x | 1.01x | 2.56x | 0.94x |
| deriv | symbolic | 51.8 | 33.7 | 48.6 | 400.9 | 64.8 | 46.9 | 1.10x | 0.72x | 1.04x | 8.55x | 1.38x |
| diviter | iterative | 272.4 | 268.7 | 299.7 | 11.66s | 26.72s | 508.2 | 0.54x | 0.53x | 0.59x | 22.9x | 52.6x |
| divrec | recursive | 21.6 | 18.3 | 49.9 | 81.6 | 42.2 | 50.1 | 0.43x | 0.37x | 1.00x | 1.63x | 0.84x |
| gcbench | allocation | 212.1 | 217.3 | 117.8 | 4.24s | 557.0 | 66.5 | 3.19x | 3.27x | 1.77x | 63.8x | 8.37x |
| paraffins | combinat | 25.8 | 26.9 | 45.8 | 51.9 | 7.84 | 45.1 | 0.57x | 0.60x | 1.02x | 1.15x | 0.17x |
| pnpoly | numeric | 30.3 | 32.7 | 47.1 | 145.2 | 208.9 | 48.8 | 0.62x | 0.67x | 0.97x | 2.97x | 4.28x |
| puzzle | search | 19.4 | 19.9 | 46.4 | 148.2 | 34.7 | 45.5 | 0.43x | 0.44x | 1.02x | 3.26x | 0.76x |
| quicksort | sorting | 19.3 | 20.2 | 45.5 | 439.2 | 25.0 | 45.6 | 0.42x | 0.44x | 1.00x | 9.63x | 0.55x |
| ray | numeric | 26.0 | 18.3 | 45.1 | 59.7 | 19.4 | 46.4 | 0.56x | 0.39x | 0.97x | 1.29x | 0.42x |

### JetStream

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 136.6 | 202.8 | 49.5 | 840.6 | 223.3 | 64.1 | 2.13x | 3.17x | 0.77x | 13.1x | 3.48x |
| navier_stokes | numeric | 220.6 | 221.2 | 95.0 | 1.41s | 109.9 | 61.1 | 3.61x | 3.62x | 1.55x | 23.0x | 1.80x |
| splay | data | 367.6 | 355.2 | 65.9 | 2.63s | 564.5 | 91.7 | 4.01x | 3.87x | 0.72x | 28.7x | 6.16x |
| hashmap | data | 180.7 | 122.1 | 47.7 | 5.17s | 333.7 | 59.1 | 3.06x | 2.07x | 0.81x | 87.4x | 5.65x |
| crypto_sha1 | crypto | 82.6 | 631.4 | 49.1 | 678.2 | 225.5 | 52.1 | 1.59x | 12.1x | 0.94x | 13.0x | 4.33x |
| raytrace3d | 3d | 234.1 | 185.5 | 52.4 | 793.2 | 168.6 | 62.3 | 3.76x | 2.98x | 0.84x | 12.7x | 2.71x |

### Text

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 269.2 | 164.7 | 58.8 | 2.70s | 617.2 | 83.2 | 3.24x | 1.98x | 0.71x | 32.4x | 7.42x |
| microdiff | data-diff | 17.5 | 17.4 | 44.5 | 1.23s | 115.4 | 59.5 | 0.30x | 0.29x | 0.75x | 20.7x | 1.94x |
| hyphen | hyphenation | 25.6 | 22.4 | 45.6 | 1.57s | 67.7 | 54.6 | 0.47x | 0.41x | 0.83x | 28.8x | 1.24x |
| prettier_ast | formatting | 1.18s | 1.18s | --- | 28.67s | 1.43s | 145.6 | 8.12x | 8.12x | --- | 197x | 9.84x |
| text_search | search | 15.89s | 15.89s | --- | 96.05s | 38.88s | 827.4 | 19.2x | 19.2x | --- | 116x | 47.0x |
| three_way_merge | merge | 3.63s | 3.63s | --- | 19.16s | 6.21s | 1.01s | 3.59x | 3.59x | --- | 18.9x | 6.13x |
| log_pipeline | log-processing | 6.25s | 6.25s | --- | 26.40s | 9.50s | 999.3 | 6.26x | 6.26x | --- | 26.4x | 9.50x |

