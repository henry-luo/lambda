# Lambda Benchmark Results: Result36

- **Date:** 2026-09-02
- **Platform:** Darwin arm64
- **Lambda commit:** `33a178ed0f6a26fa95a1c595ad514ec5f52a72bd`
- **Lambda build:** clean release build (`make release`)
- **Instrumentation check:** not_recorded
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v36.json`
- **Separately measured:** C2MIR, C2MIR measured on 2026-09-02, 3 run(s) from `temp/benchmark_results_v36_c2mir.json`. Native C2MIR refresh after rebuilding the on-demand lambda/mir/c2m driver; 59 canonical rows, three runs each.
- **Separately measured:** MIR (untyped), MIR (untyped, auto), MIR (typed), MIR (typed, auto) measured on 2026-09-02, 3 run(s) from `temp/result36_mir_recheck.json`. MIR wrong-output refresh after release rebuild and live self-verification; 11 previously excluded rows, three runs each, JIT and auto tiers.
- **Separately measured:** C2MIR, C2MIR, LambdaJS, LambdaJS, MIR (untyped), MIR (untyped, auto), MIR (typed), MIR (typed, auto), Node.js, Node.js, QuickJS, QuickJS measured on 2026-09-02, 3 run(s) from `temp/result36_hyphen_queens.json`. text/hyphen and awfy/queens re-measured after fixing the benchmark sources: hyphen/hyphen2 predicates now return bool (an int-declared bool return raised E201 on every call and produced a wrong checksum; the checksum is now verified against the C port's 731008) and queens/queens2 use var params with a validated placement (plain-param snapshots had reduced the search to 8 probes). Two rows, three runs each, all engines, HEAD release build.
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Part 1 — Execution time (self-reported)

Each engine's own `__TIMING__` figure: the timed workload only, with startup and compilation outside the measured region. This is the historical series, comparable back through Result18, and the MIR columns pin `LAMBDA_TIER=jit`.

### Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 0.50x | 0.39x | 0.18x | 12.1x | 6.48x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 1.65x | 1.09x | 0.09x | 39.8x | 5.19x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 0.52x | 0.36x | 0.10x | 12.3x | 1.69x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 2.24x | 1.57x | 0.22x | 57.2x | 11.9x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 2.35x | 0.91x | 0.32x | 33.8x | 13.3x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 7.54x | 4.52x | 0.29x | 77.1x | 12.0x |
| Text | 3 | 3 | 3 | 3 | 3 | 3 | 3 | 0.32x | 0.22x | 0.02x | 68.0x | 9.57x |
| **Overall** | 59 | 59 | 59 | 59 | 59 | 59 | 59 | 1.37x | 0.85x | 0.15x | 30.9x | 6.84x |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 5.67x over 59 of 59 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| awfy/deltablue | 117.7 | 1.16 | 101x |
| awfy/havlak | 153.9 | 1.72 | 89.4x |
| jetstream/crypto_sha1 | 101.0 | 2.61 | 38.6x |
| awfy/cd | 493.8 | 14.2 | 34.8x |
| jetstream/hashmap | 87.6 | 2.84 | 30.9x |
| jetstream/cube3d | 14.1 | 0.513 | 27.5x |
| text/hyphen | 1.85 | 0.082 | 22.6x |
| kostya/base64 | 11.5 | 0.555 | 20.7x |
| awfy/queens | 0.345 | 0.018 | 19.4x |
| awfy/towers | 0.427 | 0.025 | 17.1x |
| beng/knucleotide | 4.72 | 0.278 | 17.0x |
| kostya/json_gen | 24.0 | 1.53 | 15.7x |

---

### Notable Results

- Missing timings: **0** cells

#### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/havlak | 93.46s | 93.4 | 1001x |
| kostya/primes | 3.89s | 4.42 | 879x |
| awfy/cd | 28.12s | 36.0 | 781x |
| jetstream/hashmap | 5.55s | 15.6 | 356x |
| larceny/quicksort | 422.6 | 1.65 | 256x |
| larceny/gcbench | 4.02s | 23.5 | 171x |
| larceny/triangl | 11.31s | 66.6 | 170x |
| awfy/nbody | 742.8 | 5.32 | 140x |

#### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.428 | 1.99 | 0.21x |

### R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.31 | 1.27 | 1.04 | 40.9 | 18.6 | 1.80 | 0.73x | 0.70x | 0.58x | 22.8x | 10.4x |
| fibfp | recursive | 2.08 | 1.19 | 1.05 | 40.7 | 18.7 | 1.79 | 1.17x | 0.67x | 0.59x | 22.8x | 10.5x |
| tak | recursive | 0.126 | 0.158 | 0.106 | 2.62 | 2.77 | 0.792 | 0.16x | 0.20x | 0.13x | 3.30x | 3.50x |
| cpstak | closure | 0.246 | 0.332 | 0.219 | 5.26 | 5.58 | 0.989 | 0.25x | 0.34x | 0.22x | 5.32x | 5.64x |
| sum | iterative | 0.824 | 0.824 | 0.250 | 26.5 | 31.1 | 1.23 | 0.67x | 0.67x | 0.20x | 21.5x | 25.3x |
| sumfp | iterative | 0.069 | 0.068 | 0.081 | 2.58 | 3.62 | 0.860 | 0.08x | 0.08x | 0.09x | 3.00x | 4.21x |
| nqueens | backtrack | 1.64 | 1.16 | 0.122 | 39.9 | 7.94 | 1.77 | 0.92x | 0.65x | 0.07x | 22.5x | 4.48x |
| fft | numeric | 0.202 | 0.306 | 0.023 | 53.4 | 2.75 | 1.58 | 0.13x | 0.19x | 0.01x | 33.8x | 1.74x |
| mbrot | numeric | 9.90 | 0.643 | 0.422 | 16.0 | 17.7 | 1.82 | 5.43x | 0.35x | 0.23x | 8.79x | 9.69x |
| ack | recursive | 10.7 | 14.3 | 11.0 | 220.4 | 101.5 | 13.3 | 0.81x | 1.08x | 0.83x | 16.5x | 7.62x |

### AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.042 | 0.047 | 0.015 | 10.2 | 0.605 | 0.390 | 0.11x | 0.12x | 0.04x | 26.3x | 1.55x |
| permute | micro | 0.763 | 0.131 | 0.028 | 10.8 | 1.55 | 0.812 | 0.94x | 0.16x | 0.03x | 13.3x | 1.91x |
| queens | micro | 0.393 | 0.345 | 0.018 | 6.34 | 0.974 | 0.643 | 0.61x | 0.54x | 0.03x | 9.86x | 1.52x |
| towers | micro | 1.18 | 0.427 | 0.025 | 30.7 | 2.24 | 1.13 | 1.05x | 0.38x | 0.02x | 27.2x | 1.99x |
| bounce | micro | 0.067 | 0.108 | 0.022 | 7.30 | 0.869 | 0.552 | 0.12x | 0.20x | 0.04x | 13.2x | 1.57x |
| list | micro | 0.773 | 0.211 | 0.021 | 3.25 | 0.918 | 0.483 | 1.60x | 0.44x | 0.04x | 6.73x | 1.90x |
| storage | micro | 0.745 | 0.551 | 0.090 | 7.15 | 2.07 | 0.638 | 1.17x | 0.86x | 0.14x | 11.2x | 3.25x |
| mandelbrot | compute | 38.2 | 38.3 | 28.6 | 439.5 | 869.5 | 31.2 | 1.22x | 1.23x | 0.92x | 14.1x | 27.8x |
| nbody | compute | 43.5 | 15.5 | 1.49 | 742.8 | 159.5 | 5.32 | 8.16x | 2.91x | 0.28x | 140x | 29.9x |
| richards | macro | 433.9 | 445.4 | 28.8 | 1.76s | 197.0 | 46.8 | 9.27x | 9.51x | 0.61x | 37.7x | 4.21x |
| json | macro | 7.70 | 3.84 | 0.256 | 96.8 | 10.7 | 2.60 | 2.96x | 1.48x | 0.10x | 37.2x | 4.12x |
| deltablue | macro | 123.7 | 117.7 | 1.16 | 1.56s | 108.5 | 12.4 | 9.99x | 9.50x | 0.09x | 126x | 8.76x |
| havlak | macro | 152.6 | 153.9 | 1.72 | 93.46s | 3.29s | 93.4 | 1.63x | 1.65x | 0.02x | 1001x | 35.2x |
| cd | macro | 596.3 | 493.8 | 14.2 | 28.12s | 962.7 | 36.0 | 16.6x | 13.7x | 0.39x | 781x | 26.7x |

### BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 9.14 | 4.80 | 3.04 | 162.7 | 23.6 | 4.06 | 2.25x | 1.18x | 0.75x | 40.1x | 5.81x |
| fannkuch | permutation | 0.329 | 0.396 | 0.151 | 61.1 | 7.26 | 3.98 | 0.08x | 0.10x | 0.04x | 15.3x | 1.82x |
| fasta | generation | 0.774 | 0.915 | 0.231 | 27.3 | 8.85 | 6.27 | 0.12x | 0.15x | 0.04x | 4.36x | 1.41x |
| knucleotide | hashing | 4.36 | 4.72 | 0.278 | 162.1 | 7.72 | 4.89 | 0.89x | 0.96x | 0.06x | 33.1x | 1.58x |
| pidigits | bignum | 0.300 | 0.300 | 0.045 | 0.428 | 0.135 | 1.99 | 0.15x | 0.15x | 0.02x | 0.21x | 0.07x |
| regexredux | regex | 1.28 | 1.28 | 1.06 | 52.3 | 5.58 | 2.42 | 0.53x | 0.53x | 0.44x | 21.6x | 2.30x |
| revcomp | string | 1.25 | 1.18 | 0.377 | 34.6 | 2.54 | 3.38 | 0.37x | 0.35x | 0.11x | 10.2x | 0.75x |
| spectralnorm | numeric | 24.0 | 1.65 | 0.357 | 322.6 | 64.3 | 2.61 | 9.20x | 0.63x | 0.14x | 124x | 24.6x |

### KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 311.5 | 343.6 | 26.5 | 3.77s | 885.4 | 33.8 | 9.21x | 10.2x | 0.78x | 112x | 26.2x |
| matmul | numeric | 22.8 | 28.5 | 6.13 | 1.27s | 539.5 | 15.4 | 1.48x | 1.85x | 0.40x | 82.6x | 35.0x |
| primes | numeric | 16.9 | 5.62 | 1.59 | 3.89s | 95.0 | 4.42 | 3.81x | 1.27x | 0.36x | 879x | 21.5x |
| base64 | string | 11.5 | 11.5 | 0.555 | 873.5 | 159.0 | 17.4 | 0.66x | 0.66x | 0.03x | 50.2x | 9.14x |
| levenshtein | string | 35.5 | 6.49 | 0.903 | 449.2 | 54.2 | 3.98 | 8.92x | 1.63x | 0.23x | 113x | 13.6x |
| json_gen | data | 23.2 | 24.0 | 1.53 | 67.0 | 19.9 | 6.29 | 3.70x | 3.82x | 0.24x | 10.6x | 3.16x |
| collatz | numeric | 358.4 | 343.5 | 210.5 | 5.80s | 6.24s | 1.42s | 0.25x | 0.24x | 0.15x | 4.08x | 4.39x |

### LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 507.5 | 210.3 | 56.6 | 11.31s | 2.19s | 66.6 | 7.62x | 3.16x | 0.85x | 170x | 32.9x |
| array1 | array | 0.808 | 0.812 | 0.316 | 79.4 | 36.0 | 1.89 | 0.43x | 0.43x | 0.17x | 42.0x | 19.0x |
| deriv | symbolic | 30.1 | 12.7 | 2.81 | 341.0 | 59.0 | 3.67 | 8.21x | 3.46x | 0.77x | 93.0x | 16.1x |
| diviter | iterative | 263.3 | 267.2 | 249.3 | 11.82s | 27.00s | 470.1 | 0.56x | 0.57x | 0.53x | 25.1x | 57.4x |
| divrec | recursive | 15.2 | 2.00 | 4.59 | 45.0 | 36.8 | 7.60 | 2.00x | 0.26x | 0.60x | 5.93x | 4.84x |
| gcbench | allocation | 210.3 | 257.8 | 69.7 | 4.02s | 548.1 | 23.5 | 8.96x | 11.0x | 2.97x | 171x | 23.3x |
| paraffins | combinat | 0.323 | 0.281 | 0.051 | 4.15 | 2.55 | 0.995 | 0.32x | 0.28x | 0.05x | 4.17x | 2.57x |
| pnpoly | numeric | 12.9 | 14.8 | 1.95 | 122.1 | 201.9 | 5.94 | 2.17x | 2.49x | 0.33x | 20.6x | 34.0x |
| puzzle | search | 13.1 | 3.98 | 1.28 | 110.8 | 29.3 | 3.29 | 3.98x | 1.21x | 0.39x | 33.7x | 8.90x |
| quicksort | sorting | 10.6 | 1.08 | 0.199 | 422.6 | 19.2 | 1.65 | 6.43x | 0.65x | 0.12x | 256x | 11.6x |
| ray | numeric | 8.85 | 0.297 | 0.171 | 18.2 | 13.7 | 3.51 | 2.52x | 0.08x | 0.05x | 5.17x | 3.91x |

### JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 11.4 | 14.1 | 0.513 | 756.1 | 217.7 | 17.7 | 0.64x | 0.80x | 0.03x | 42.8x | 12.3x |
| navier_stokes | numeric | 392.8 | 156.8 | 44.3 | 1.05s | 98.6 | 14.1 | 27.9x | 11.1x | 3.15x | 74.7x | 7.01x |
| splay | data | 281.3 | 288.5 | 18.5 | 1.15s | 146.7 | 19.3 | 14.5x | 14.9x | 0.96x | 59.5x | 7.59x |
| hashmap | data | 179.2 | 87.6 | 2.84 | 5.55s | 318.4 | 15.6 | 11.5x | 5.63x | 0.18x | 356x | 20.5x |
| crypto_sha1 | crypto | 137.4 | 101.0 | 2.61 | 627.9 | 220.1 | 8.74 | 15.7x | 11.6x | 0.30x | 71.8x | 25.2x |
| raytrace3d | 3d | 72.1 | 18.2 | 2.17 | 802.4 | 162.9 | 18.6 | 3.89x | 0.98x | 0.12x | 43.2x | 8.77x |

### Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 258.8 | 152.0 | 13.4 | 2.61s | 635.5 | 39.5 | 6.55x | 3.85x | 0.34x | 66.1x | 16.1x |
| microdiff | data-diff | 0.157 | 0.158 | 0.017 | 1.30s | 108.6 | 16.3 | 0.010x | 0.010x | 0.001x | 79.7x | 6.66x |
| hyphen | hyphenation | 3.35 | 1.85 | 0.082 | 369.5 | 50.5 | 6.18 | 0.54x | 0.30x | 0.01x | 59.8x | 8.18x |

---

## Part 2 — End-to-end time (wall clock, auto tier)

Wall clock from process invocation to exit, so **every engine pays its own startup and compilation inside the number**. The MIR columns use the shipped auto tier -- no `LAMBDA_TIER` override -- which is what `lambda.exe run script.ls` actually does.

This set exists because the two questions are different. Part 1 asks how fast the compiled workload runs; part 2 asks how long it takes to run the script. Timing the auto tier under part 1's rules would charge Lambda for JIT compilation performed *inside* the measured region while crediting Node.js with a post-warmup figure -- comparing two different things. Here the accounting is the same for everyone.

Same processes, where possible: the reference engines report their wall and `__TIMING__` figures from the *same* run, so parts 1 and 2 are two readings of one launch. Only the MIR columns are re-run, because part 1 pins the JIT and part 2 must use the auto tier.

⚠ Short workloads are dominated by fixed process startup here, so a row whose part-1 time is a fraction of a millisecond says more about executable launch cost than about the language. Read part 2 by the longer rows.

### Summary

| Suite | Total | Timed MIR (untyped, auto) | Timed MIR (typed, auto) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped, auto)/Node geo | MIR (typed, auto)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 2.34x | 0.87x | 0.99x | 1.56x | 0.42x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 2.71x | 3.14x | 0.90x | 7.68x | 0.81x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 0.85x | 0.78x | 1.02x | 2.64x | 0.33x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 16.0x | 12.1x | 0.72x | 14.4x | 2.91x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 7.76x | 3.12x | 1.00x | 6.56x | 1.94x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 16.5x | 12.1x | 0.90x | 23.6x | 3.67x |
| Text | 3 | 3 | 3 | 3 | 3 | 3 | 3 | 3.41x | 2.73x | 0.78x | 28.6x | 2.68x |
| **Overall** | 59 | 59 | 59 | 59 | 59 | 59 | 59 | 4.13x | 2.79x | 0.92x | 6.36x | 1.09x |

> Ratio < 1.0 means the engine finished the whole run faster than Node.js.

### R7RS

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 217.6 | 16.6 | 44.3 | 75.2 | 24.6 | 45.2 | 4.81x | 0.37x | 0.98x | 1.66x | 0.54x |
| fibfp | recursive | 202.4 | 16.5 | 44.4 | 75.3 | 24.7 | 45.0 | 4.50x | 0.37x | 0.99x | 1.67x | 0.55x |
| tak | recursive | 53.3 | 60.9 | 43.9 | 40.2 | 8.43 | 44.4 | 1.20x | 1.37x | 0.99x | 0.90x | 0.19x |
| cpstak | closure | 92.7 | 106.1 | 43.8 | 42.7 | 11.5 | 44.5 | 2.09x | 2.39x | 0.99x | 0.96x | 0.26x |
| sum | iterative | 155.0 | 25.1 | 43.9 | 60.9 | 37.1 | 45.0 | 3.45x | 0.56x | 0.98x | 1.36x | 0.83x |
| sumfp | iterative | 26.1 | 29.8 | 46.5 | 36.8 | 9.62 | 44.2 | 0.59x | 0.67x | 1.05x | 0.83x | 0.22x |
| nqueens | backtrack | 53.3 | 61.1 | 44.6 | 120.6 | 14.0 | 45.2 | 1.18x | 1.35x | 0.99x | 2.67x | 0.31x |
| fft | numeric | 29.8 | 32.6 | 45.0 | 97.6 | 8.60 | 44.9 | 0.66x | 0.73x | 1.00x | 2.18x | 0.19x |
| mbrot | numeric | 110.9 | 131.9 | 45.1 | 54.6 | 23.7 | 45.3 | 2.45x | 2.91x | 1.00x | 1.21x | 0.52x |
| ack | recursive | 1.32s | 29.9 | 55.5 | 255.0 | 107.9 | 56.6 | 23.3x | 0.53x | 0.98x | 4.50x | 1.91x |

### AWFY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 15.5 | 16.7 | 43.6 | 50.5 | 6.61 | 44.6 | 0.35x | 0.37x | 0.98x | 1.13x | 0.15x |
| permute | micro | 31.5 | 35.1 | 44.8 | 53.6 | 7.19 | 44.8 | 0.70x | 0.78x | 1.00x | 1.20x | 0.16x |
| queens | micro | 20.0 | 27.6 | 44.2 | 50.1 | 5.93 | 42.9 | 0.47x | 0.64x | 1.03x | 1.17x | 0.14x |
| towers | micro | 22.2 | 59.5 | 44.1 | 78.8 | 8.11 | 45.4 | 0.49x | 1.31x | 0.97x | 1.74x | 0.18x |
| bounce | micro | 18.2 | 19.0 | 44.6 | 132.1 | 7.40 | 45.0 | 0.40x | 0.42x | 0.99x | 2.94x | 0.16x |
| list | micro | 22.4 | 25.3 | 44.6 | 47.1 | 6.50 | 44.8 | 0.50x | 0.56x | 1.00x | 1.05x | 0.15x |
| storage | micro | 23.2 | 25.4 | 44.2 | 123.2 | 8.56 | 45.3 | 0.51x | 0.56x | 0.98x | 2.72x | 0.19x |
| mandelbrot | compute | 3.87s | 4.72s | 73.1 | 487.5 | 876.4 | 75.5 | 51.3x | 62.5x | 0.97x | 6.46x | 11.6x |
| nbody | compute | 877.7 | 1.01s | 47.1 | 817.7 | 166.2 | 49.3 | 17.8x | 20.5x | 0.95x | 16.6x | 3.37x |
| richards | macro | 4.11s | 4.31s | 77.3 | 1.88s | 203.9 | 91.0 | 45.1x | 47.3x | 0.85x | 20.7x | 2.24x |
| json | macro | 54.3 | 53.0 | 52.7 | 303.9 | 18.7 | 47.7 | 1.14x | 1.11x | 1.10x | 6.37x | 0.39x |
| deltablue | macro | 1.45s | 1.49s | 53.0 | 1.84s | 116.4 | 58.8 | 24.7x | 25.4x | 0.90x | 31.2x | 1.98x |
| havlak | macro | 249.4 | 247.8 | 53.1 | 95.03s | 3.30s | 139.9 | 1.78x | 1.77x | 0.38x | 679x | 23.6x |
| cd | macro | 7.77s | 7.36s | 65.9 | 28.79s | 969.3 | 80.5 | 96.6x | 91.4x | 0.82x | 358x | 12.0x |

### BENG

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 117.2 | 77.2 | 48.0 | 209.0 | 29.5 | 45.5 | 2.58x | 1.70x | 1.05x | 4.60x | 0.65x |
| fannkuch | permutation | 53.6 | 64.2 | 45.0 | 102.0 | 13.1 | 45.5 | 1.18x | 1.41x | 0.99x | 2.24x | 0.29x |
| fasta | generation | 23.3 | 24.8 | 45.1 | 74.9 | 14.6 | 48.2 | 0.48x | 0.51x | 0.94x | 1.55x | 0.30x |
| knucleotide | hashing | 33.5 | 35.6 | 46.0 | 208.8 | 13.6 | 46.9 | 0.71x | 0.76x | 0.98x | 4.45x | 0.29x |
| pidigits | bignum | 14.1 | 15.3 | 46.8 | 44.4 | 5.49 | 43.0 | 0.33x | 0.35x | 1.09x | 1.03x | 0.13x |
| regexredux | regex | 14.7 | 15.0 | 47.9 | 98.6 | 11.3 | 44.3 | 0.33x | 0.34x | 1.08x | 2.23x | 0.25x |
| revcomp | string | 17.1 | 17.5 | 45.8 | 81.9 | 8.06 | 44.7 | 0.38x | 0.39x | 1.02x | 1.83x | 0.18x |
| spectralnorm | numeric | 300.4 | 140.0 | 45.6 | 365.9 | 70.1 | 45.9 | 6.54x | 3.05x | 0.99x | 7.97x | 1.53x |

### KOSTYA

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 6.20s | 6.74s | 71.7 | 3.81s | 892.0 | 77.8 | 79.7x | 86.6x | 0.92x | 49.0x | 11.5x |
| matmul | numeric | 2.23s | 2.53s | 52.7 | 1.32s | 547.1 | 58.5 | 38.2x | 43.3x | 0.90x | 22.5x | 9.34x |
| primes | numeric | 445.0 | 571.5 | 46.6 | 3.95s | 101.3 | 47.5 | 9.36x | 12.0x | 0.98x | 83.1x | 2.13x |
| base64 | string | 459.1 | 487.8 | 45.9 | 916.1 | 165.3 | 60.7 | 7.57x | 8.04x | 0.76x | 15.1x | 2.72x |
| levenshtein | string | 786.0 | 369.2 | 46.0 | 493.9 | 60.2 | 47.1 | 16.7x | 7.83x | 0.98x | 10.5x | 1.28x |
| json_gen | data | 146.2 | 95.6 | 47.0 | 109.0 | 25.8 | 49.5 | 2.95x | 1.93x | 0.95x | 2.20x | 0.52x |
| collatz | numeric | 36.37s | 9.91s | 255.5 | 5.84s | 6.24s | 1.46s | 24.8x | 6.77x | 0.17x | 3.99x | 4.26x |

### LARCENY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 535.7 | 237.3 | 101.9 | 11.38s | 2.20s | 110.0 | 4.87x | 2.16x | 0.93x | 103x | 20.0x |
| array1 | array | 168.4 | 210.1 | 44.8 | 115.0 | 41.8 | 44.7 | 3.76x | 4.69x | 1.00x | 2.57x | 0.93x |
| deriv | symbolic | 226.6 | 534.8 | 48.9 | 390.9 | 65.2 | 47.2 | 4.80x | 11.3x | 1.03x | 8.28x | 1.38x |
| diviter | iterative | 107.25s | 15.0 | 298.7 | 11.86s | 27.01s | 513.6 | 209x | 0.03x | 0.58x | 23.1x | 52.6x |
| divrec | recursive | 451.8 | 19.5 | 49.2 | 82.0 | 42.7 | 50.7 | 8.91x | 0.39x | 0.97x | 1.62x | 0.84x |
| gcbench | allocation | 2.84s | 7.69s | 115.2 | 4.21s | 559.9 | 65.7 | 43.3x | 117x | 1.75x | 64.2x | 8.53x |
| paraffins | combinat | 32.7 | 36.9 | 46.2 | 53.6 | 8.36 | 43.7 | 0.75x | 0.84x | 1.06x | 1.23x | 0.19x |
| pnpoly | numeric | 1.38s | 1.56s | 47.2 | 161.5 | 208.0 | 48.9 | 28.3x | 31.9x | 0.97x | 3.30x | 4.26x |
| puzzle | search | 214.6 | 252.3 | 46.3 | 150.3 | 35.0 | 46.1 | 4.66x | 5.48x | 1.01x | 3.26x | 0.76x |
| quicksort | sorting | 130.2 | 159.0 | 45.3 | 464.5 | 25.4 | 44.7 | 2.92x | 3.56x | 1.01x | 10.4x | 0.57x |
| ray | numeric | 138.6 | 157.3 | 45.4 | 61.7 | 19.4 | 46.3 | 2.99x | 3.40x | 0.98x | 1.33x | 0.42x |

### JetStream

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 341.0 | 365.8 | 49.7 | 868.6 | 224.9 | 63.1 | 5.40x | 5.79x | 0.79x | 13.8x | 3.56x |
| navier_stokes | numeric | 9.36s | 10.12s | 92.0 | 1.35s | 109.7 | 60.9 | 154x | 166x | 1.51x | 22.1x | 1.80x |
| splay | data | 1.84s | 354.5 | 65.8 | 2.95s | 561.2 | 92.3 | 19.9x | 3.84x | 0.71x | 32.0x | 6.08x |
| hashmap | data | 209.6 | 118.8 | 48.3 | 5.68s | 325.9 | 60.5 | 3.47x | 1.96x | 0.80x | 94.0x | 5.39x |
| crypto_sha1 | crypto | 2.05s | 2.36s | 48.9 | 677.4 | 226.8 | 52.8 | 38.9x | 44.7x | 0.93x | 12.8x | 4.30x |
| raytrace3d | 3d | 577.6 | 593.7 | 52.3 | 922.4 | 170.0 | 62.7 | 9.21x | 9.47x | 0.83x | 14.7x | 2.71x |

### Text

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 6.33s | 6.34s | 61.5 | 2.78s | 642.8 | 83.6 | 75.8x | 75.8x | 0.74x | 33.2x | 7.69x |
| microdiff | data-diff | 24.9 | 24.7 | 45.1 | 1.38s | 114.7 | 59.7 | 0.42x | 0.41x | 0.76x | 23.2x | 1.92x |
| hyphen | hyphenation | 63.9 | 33.1 | 43.5 | 1.55s | 66.6 | 51.1 | 1.25x | 0.65x | 0.85x | 30.4x | 1.30x |

