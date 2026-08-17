# Lambda Benchmark Results: Result32

- **Date:** 2026-08-17
- **Platform:** Darwin arm64
- **Lambda commit:** `55b5f650c278257ca9997264b87a94f241c3ca5b`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v32-55b5f650c2` (21,068,312 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 196.60s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 196.5s (batched 195.7s: sync 118.3s, async 77.3s; non-batched 0.9s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v32.json`
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 9 | 10 | 0.65x | 0.37x | 0.19x | 11.6x | 6.37x |
| AWFY | 14 | 14 | 13 | 9 | 14 | 14 | 14 | 3.09x | 1.29x | 0.08x | 45.5x | 5.23x |
| BENG | 8 | 8 | 8 | 7 | 8 | 5 | 8 | 0.58x | 0.39x | 0.12x | 12.1x | 1.91x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 3.08x | 1.40x | 0.23x | 54.0x | 11.9x |
| LARCENY | 11 | 11 | 11 | 10 | 11 | 11 | 11 | 2.94x | 0.89x | 0.30x | 33.6x | 13.3x |
| JetStream | 6 | 6 | 6 | 1 | 4 | 4 | 6 | 8.50x | 3.96x | 0.18x | 72.2x | 12.9x |
| Text | 3 | 3 | 3 | 3 | 3 | 3 | 3 | 0.74x | 0.35x | 0.02x | 70.5x | 9.43x |
| **Overall** | 59 | 59 | 58 | 47 | 57 | 53 | 59 | 1.93x | 0.87x | 0.15x | 30.3x | 7.36x |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 4.34x over 47 of 59 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| text/microdiff | 0.541 | 0.016 | 34.1x |
| awfy/bounce | 0.768 | 0.024 | 31.8x |
| kostya/base64 | 17.8 | 0.564 | 31.6x |
| awfy/list | 0.633 | 0.025 | 25.4x |
| text/hyphen | 2.10 | 0.087 | 24.2x |
| jetstream/hashmap | 53.2 | 2.72 | 19.6x |
| beng/knucleotide | 4.72 | 0.286 | 16.5x |
| awfy/queens | 0.311 | 0.019 | 16.3x |
| awfy/towers | 0.438 | 0.029 | 15.1x |
| kostya/json_gen | 21.5 | 1.53 | 14.1x |
| awfy/nbody | 20.4 | 1.51 | 13.5x |
| kostya/brainfuck | 368.7 | 28.4 | 13.0x |

---

## Notable Results

- Missing timings: **21** cells
- QuickJS missing: r7rs/ack (exit_1), beng/knucleotide (exit_1), beng/regexredux (exit_1), beng/revcomp (exit_1), jetstream/cube3d (exit_1), jetstream/raytrace3d (exit_1)
- C2MIR missing: awfy/richards (missing_port), awfy/json (missing_port), awfy/deltablue (missing_port), awfy/havlak (missing_port), awfy/cd (missing_port), beng/pidigits (missing_port), larceny/deriv (missing_port), jetstream/cube3d (missing_port), +4 more
- MIR (typed) missing: awfy/cd (exit_1)
- LambdaJS missing: jetstream/navier_stokes (timeout), jetstream/hashmap (timeout)

### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/havlak | 109.33s | 100.2 | 1091x |
| kostya/primes | 3.89s | 4.50 | 866x |
| awfy/cd | 28.60s | 36.3 | 787x |
| larceny/quicksort | 402.3 | 1.63 | 246x |
| awfy/nbody | 1.02s | 5.41 | 188x |
| larceny/triangl | 11.44s | 67.1 | 171x |
| larceny/gcbench | 3.86s | 23.0 | 168x |
| awfy/deltablue | 2.07s | 12.4 | 167x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.424 | 1.94 | 0.22x |

---

## R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.33 | 1.28 | 1.11 | 39.2 | 19.4 | 1.77 | 0.75x | 0.73x | 0.63x | 22.1x | 10.9x |
| fibfp | recursive | 1.99 | 1.21 | 1.15 | 39.9 | 19.1 | 1.83 | 1.09x | 0.66x | 0.63x | 21.9x | 10.5x |
| tak | recursive | 0.132 | 0.163 | 0.111 | 2.55 | 2.84 | 0.812 | 0.16x | 0.20x | 0.14x | 3.14x | 3.50x |
| cpstak | closure | 0.251 | 0.334 | 0.240 | 5.04 | 5.60 | 0.990 | 0.25x | 0.34x | 0.24x | 5.09x | 5.66x |
| sum | iterative | 0.841 | 0.837 | 0.273 | 25.5 | 31.5 | 1.24 | 0.68x | 0.68x | 0.22x | 20.6x | 25.5x |
| sumfp | iterative | 0.069 | 0.070 | 0.079 | 2.51 | 3.72 | 0.915 | 0.08x | 0.08x | 0.09x | 2.75x | 4.07x |
| nqueens | backtrack | 1.90 | 1.10 | 0.129 | 39.5 | 7.95 | 1.81 | 1.05x | 0.61x | 0.07x | 21.9x | 4.40x |
| fft | numeric | 2.58 | 0.244 | 0.025 | 54.0 | 2.77 | 1.61 | 1.60x | 0.15x | 0.02x | 33.5x | 1.72x |
| mbrot | numeric | 11.4 | 0.548 | 0.453 | 16.0 | 17.9 | 1.86 | 6.13x | 0.30x | 0.24x | 8.64x | 9.62x |
| ack | recursive | 10.9 | 13.4 | 11.5 | 222.3 | --- | 13.5 | 0.81x | 0.99x | 0.85x | 16.5x | --- |

## AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.550 | 0.033 | 0.016 | 10.3 | 0.617 | 0.391 | 1.41x | 0.08x | 0.04x | 26.2x | 1.58x |
| permute | micro | 0.861 | 0.132 | 0.027 | 11.3 | 1.56 | 0.818 | 1.05x | 0.16x | 0.03x | 13.8x | 1.91x |
| queens | micro | 0.550 | 0.311 | 0.019 | 6.96 | 1.07 | 0.643 | 0.86x | 0.48x | 0.03x | 10.8x | 1.67x |
| towers | micro | 1.21 | 0.438 | 0.029 | 56.5 | 2.29 | 1.15 | 1.05x | 0.38x | 0.03x | 49.1x | 1.99x |
| bounce | micro | 0.266 | 0.768 | 0.024 | 7.38 | 0.883 | 0.541 | 0.49x | 1.42x | 0.04x | 13.6x | 1.63x |
| list | micro | 0.516 | 0.633 | 0.025 | 3.42 | 0.926 | 0.494 | 1.04x | 1.28x | 0.05x | 6.93x | 1.87x |
| storage | micro | 0.806 | 0.536 | 0.101 | 7.29 | 2.19 | 0.644 | 1.25x | 0.83x | 0.16x | 11.3x | 3.40x |
| mandelbrot | compute | 39.0 | 39.0 | 31.1 | 446.8 | 881.7 | 31.6 | 1.23x | 1.24x | 0.99x | 14.2x | 27.9x |
| nbody | compute | 32.7 | 20.4 | 1.51 | 1.02s | 161.8 | 5.41 | 6.05x | 3.78x | 0.28x | 188x | 29.9x |
| richards | macro | 9.47s | 981.2 | --- | 2.55s | 198.9 | 47.4 | 200x | 20.7x | --- | 53.9x | 4.19x |
| json | macro | 8.93 | 2.47 | --- | 100.7 | 11.2 | 2.64 | 3.38x | 0.94x | --- | 38.2x | 4.23x |
| deltablue | macro | 89.8 | 91.1 | --- | 2.07s | 104.1 | 12.4 | 7.24x | 7.34x | --- | 167x | 8.39x |
| havlak | macro | 1.06s | 1.06s | --- | 109.33s | 3.32s | 100.2 | 10.6x | 10.6x | --- | 1091x | 33.1x |
| cd | macro | 803.5 | --- | --- | 28.60s | 968.8 | 36.3 | 22.1x | --- | --- | 787x | 26.7x |

## BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 9.46 | 4.62 | 3.03 | 161.7 | 23.8 | 4.12 | 2.30x | 1.12x | 0.74x | 39.3x | 5.79x |
| fannkuch | permutation | 0.340 | 0.736 | 0.152 | 61.3 | 7.24 | 4.08 | 0.08x | 0.18x | 0.04x | 15.0x | 1.77x |
| fasta | generation | 0.812 | 0.903 | 0.246 | 26.3 | 8.80 | 5.98 | 0.14x | 0.15x | 0.04x | 4.39x | 1.47x |
| knucleotide | hashing | 4.16 | 4.72 | 0.286 | 163.8 | --- | 5.01 | 0.83x | 0.94x | 0.06x | 32.7x | --- |
| pidigits | bignum | 0.311 | 0.310 | --- | 0.424 | 0.129 | 1.94 | 0.16x | 0.16x | --- | 0.22x | 0.07x |
| regexredux | regex | 1.28 | 1.29 | 1.14 | 51.9 | --- | 2.42 | 0.53x | 0.53x | 0.47x | 21.5x | --- |
| revcomp | string | 1.42 | 1.19 | 0.380 | 32.3 | --- | 3.35 | 0.42x | 0.35x | 0.11x | 9.63x | --- |
| spectralnorm | numeric | 44.8 | 1.61 | 0.357 | 320.9 | 64.9 | 2.58 | 17.3x | 0.62x | 0.14x | 124x | 25.1x |

## KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 314.4 | 368.7 | 28.4 | 3.64s | 891.6 | 34.0 | 9.25x | 10.9x | 0.84x | 107x | 26.2x |
| matmul | numeric | 12.6 | 11.4 | 6.12 | 1.28s | 545.7 | 15.5 | 0.82x | 0.74x | 0.40x | 82.8x | 35.2x |
| primes | numeric | 66.4 | 3.46 | 1.61 | 3.89s | 96.0 | 4.50 | 14.8x | 0.77x | 0.36x | 866x | 21.4x |
| base64 | string | 48.0 | 17.8 | 0.564 | 865.1 | 158.0 | 17.4 | 2.77x | 1.03x | 0.03x | 49.8x | 9.10x |
| levenshtein | string | 35.0 | 7.06 | 0.916 | 434.6 | 54.5 | 4.00 | 8.75x | 1.77x | 0.23x | 109x | 13.6x |
| json_gen | data | 20.9 | 21.5 | 1.53 | 52.9 | 20.1 | 6.31 | 3.31x | 3.40x | 0.24x | 8.39x | 3.19x |
| collatz | numeric | 425.7 | 408.3 | 227.9 | 5.51s | 6.29s | 1.44s | 0.30x | 0.28x | 0.16x | 3.84x | 4.38x |

## LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 515.0 | 211.7 | 60.6 | 11.44s | 2.20s | 67.1 | 7.68x | 3.16x | 0.90x | 171x | 32.9x |
| array1 | array | 0.828 | 0.832 | 0.322 | 78.2 | 36.1 | 1.93 | 0.43x | 0.43x | 0.17x | 40.6x | 18.7x |
| deriv | symbolic | 35.9 | 10.3 | --- | 341.4 | 59.8 | 3.73 | 9.63x | 2.77x | --- | 91.6x | 16.0x |
| diviter | iterative | 403.9 | 403.7 | 269.0 | 14.29s | 26.60s | 468.5 | 0.86x | 0.86x | 0.57x | 30.5x | 56.8x |
| divrec | recursive | 15.5 | 2.00 | 4.85 | 43.6 | 35.7 | 7.56 | 2.05x | 0.26x | 0.64x | 5.76x | 4.72x |
| gcbench | allocation | 213.6 | 134.4 | 70.5 | 3.86s | 541.9 | 23.0 | 9.29x | 5.85x | 3.07x | 168x | 23.6x |
| paraffins | combinat | 1.91 | 0.278 | 0.046 | 4.02 | 2.51 | 0.978 | 1.95x | 0.28x | 0.05x | 4.11x | 2.57x |
| pnpoly | numeric | 15.7 | 15.7 | 1.91 | 114.0 | 200.8 | 5.79 | 2.71x | 2.70x | 0.33x | 19.7x | 34.7x |
| puzzle | search | 9.39 | 3.61 | 1.27 | 107.7 | 29.1 | 3.27 | 2.87x | 1.10x | 0.39x | 32.9x | 8.88x |
| quicksort | sorting | 10.3 | 1.08 | 0.198 | 402.3 | 19.1 | 1.63 | 6.28x | 0.66x | 0.12x | 246x | 11.7x |
| ray | numeric | 10.0 | 0.325 | 0.172 | 17.7 | 13.6 | 3.49 | 2.87x | 0.09x | 0.05x | 5.08x | 3.90x |

## JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 11.7 | 10.8 | --- | 2.20s | --- | 17.6 | 0.67x | 0.62x | --- | 125x | --- |
| navier_stokes | numeric | 990.2 | 179.0 | --- | --- | 98.3 | 13.8 | 71.7x | 13.0x | --- | --- | 7.12x |
| splay | data | 136.6 | 253.7 | --- | 1.08s | 142.8 | 18.8 | 7.26x | 13.5x | --- | 57.3x | 7.59x |
| hashmap | data | 147.1 | 53.2 | 2.72 | --- | 315.1 | 15.2 | 9.69x | 3.51x | 0.18x | --- | 20.8x |
| crypto_sha1 | crypto | 64.7 | 29.4 | --- | 602.3 | 219.0 | 8.78 | 7.37x | 3.35x | --- | 68.6x | 24.9x |
| raytrace3d | 3d | 279.1 | 56.2 | --- | 1.01s | --- | 18.3 | 15.3x | 3.07x | --- | 55.4x | --- |

## Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 1.32s | 154.8 | 13.0 | 2.63s | 606.6 | 38.7 | 34.0x | 4.00x | 0.34x | 67.9x | 15.7x |
| microdiff | data-diff | 0.485 | 0.541 | 0.016 | 1.46s | 109.0 | 16.2 | 0.03x | 0.03x | 0.001x | 89.9x | 6.73x |
| hyphen | hyphenation | 2.55 | 2.10 | 0.087 | 370.2 | 51.4 | 6.46 | 0.39x | 0.32x | 0.01x | 57.3x | 7.94x |
