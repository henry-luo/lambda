# Lambda Benchmark Results: Result37

- **Date:** 2026-09-07
- **Platform:** Darwin arm64
- **Lambda commit:** `4758dc716580375581d605af4d56b67aaea8b0be`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v37-4758dc7165` (18,803,416 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 140.40s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 140.3s (batched 139.5s: sync 71.6s, async 67.9s; non-batched 0.7s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v37.json`
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Part 1 — Execution time (self-reported)

Each engine's own `__TIMING__` figure: the timed workload only, with startup and compilation outside the measured region. This is the historical series, comparable back through Result18, and the MIR columns pin `LAMBDA_TIER=jit`.

### Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 0.36x | 0.38x | 0.19x | 12.2x | 6.57x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 1.32x | 0.93x | 0.09x | 39.6x | 5.19x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 0.38x | 0.36x | 0.10x | 12.4x | 1.69x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 1.66x | 1.41x | 0.23x | 57.5x | 12.0x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 1.17x | 0.86x | 0.33x | 33.7x | 13.2x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 5.54x | 3.76x | 0.29x | 78.0x | 12.0x |
| Text | 3 | 3 | 3 | 3 | 3 | 3 | 3 | 0.31x | 0.22x | 0.02x | 68.2x | 9.39x |
| **Overall** | 59 | 59 | 59 | 59 | 59 | 59 | 59 | 0.97x | 0.78x | 0.15x | 30.9x | 6.84x |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 5.12x over 59 of 59 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| awfy/deltablue | 121.4 | 1.14 | 106x |
| awfy/havlak | 76.9 | 1.84 | 41.8x |
| awfy/cd | 487.6 | 15.0 | 32.5x |
| jetstream/hashmap | 86.0 | 2.84 | 30.2x |
| jetstream/cube3d | 14.4 | 0.514 | 28.0x |
| text/hyphen | 2.12 | 0.091 | 23.3x |
| awfy/queens | 0.346 | 0.017 | 20.2x |
| kostya/base64 | 11.4 | 0.574 | 19.8x |
| beng/knucleotide | 4.85 | 0.285 | 17.0x |
| jetstream/splay | 314.2 | 18.6 | 16.9x |
| kostya/json_gen | 25.1 | 1.53 | 16.4x |
| awfy/json | 3.74 | 0.260 | 14.4x |

---

### Notable Results

- Missing timings: **0** cells

#### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| kostya/primes | 3.86s | 4.39 | 881x |
| awfy/havlak | 89.20s | 102.4 | 871x |
| awfy/cd | 28.22s | 35.9 | 787x |
| jetstream/hashmap | 5.52s | 15.3 | 360x |
| larceny/quicksort | 404.4 | 1.63 | 248x |
| larceny/gcbench | 4.08s | 23.3 | 175x |
| larceny/triangl | 11.46s | 66.6 | 172x |
| awfy/nbody | 745.3 | 5.54 | 135x |

#### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.425 | 1.95 | 0.22x |

### R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.24 | 1.34 | 1.03 | 43.8 | 25.4 | 1.91 | 0.65x | 0.70x | 0.54x | 22.9x | 13.3x |
| fibfp | recursive | 2.03 | 1.21 | 1.07 | 43.3 | 18.8 | 1.86 | 1.09x | 0.65x | 0.58x | 23.2x | 10.1x |
| tak | recursive | 0.121 | 0.159 | 0.118 | 2.67 | 2.81 | 0.775 | 0.16x | 0.21x | 0.15x | 3.45x | 3.63x |
| cpstak | closure | 0.239 | 0.314 | 0.246 | 5.38 | 5.56 | 0.987 | 0.24x | 0.32x | 0.25x | 5.45x | 5.63x |
| sum | iterative | 0.823 | 0.827 | 0.268 | 26.9 | 31.0 | 1.18 | 0.70x | 0.70x | 0.23x | 22.7x | 26.3x |
| sumfp | iterative | 0.068 | 0.069 | 0.081 | 2.62 | 3.65 | 0.886 | 0.08x | 0.08x | 0.09x | 2.96x | 4.12x |
| nqueens | backtrack | 1.02 | 1.47 | 0.128 | 40.1 | 7.78 | 1.90 | 0.54x | 0.77x | 0.07x | 21.1x | 4.09x |
| fft | numeric | 0.270 | 0.219 | 0.024 | 53.3 | 2.75 | 1.60 | 0.17x | 0.14x | 0.01x | 33.4x | 1.72x |
| mbrot | numeric | 0.701 | 0.657 | 0.446 | 16.3 | 17.6 | 1.85 | 0.38x | 0.35x | 0.24x | 8.81x | 9.50x |
| ack | recursive | 10.7 | 14.4 | 11.8 | 227.7 | 101.7 | 13.4 | 0.80x | 1.08x | 0.88x | 17.1x | 7.62x |

### AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.032 | 0.037 | 0.015 | 10.2 | 0.618 | 0.388 | 0.08x | 0.10x | 0.04x | 26.3x | 1.59x |
| permute | micro | 0.273 | 0.136 | 0.024 | 10.9 | 1.56 | 0.805 | 0.34x | 0.17x | 0.03x | 13.5x | 1.94x |
| queens | micro | 0.413 | 0.346 | 0.017 | 6.52 | 1.04 | 0.647 | 0.64x | 0.53x | 0.03x | 10.1x | 1.61x |
| towers | micro | 0.671 | 0.386 | 0.027 | 30.2 | 2.24 | 1.11 | 0.61x | 0.35x | 0.02x | 27.3x | 2.02x |
| bounce | micro | 0.062 | 0.066 | 0.024 | 7.25 | 0.877 | 0.539 | 0.12x | 0.12x | 0.04x | 13.4x | 1.63x |
| list | micro | 0.745 | 0.207 | 0.021 | 3.23 | 0.908 | 0.491 | 1.52x | 0.42x | 0.04x | 6.58x | 1.85x |
| storage | micro | 0.512 | 0.326 | 0.089 | 7.22 | 2.14 | 0.636 | 0.81x | 0.51x | 0.14x | 11.4x | 3.36x |
| mandelbrot | compute | 39.6 | 39.6 | 30.6 | 449.9 | 869.3 | 31.1 | 1.27x | 1.27x | 0.99x | 14.5x | 28.0x |
| nbody | compute | 44.1 | 15.4 | 1.49 | 745.3 | 159.7 | 5.54 | 7.96x | 2.79x | 0.27x | 135x | 28.8x |
| richards | macro | 426.1 | 424.9 | 29.7 | 1.75s | 191.5 | 47.1 | 9.05x | 9.03x | 0.63x | 37.2x | 4.07x |
| json | macro | 7.66 | 3.74 | 0.260 | 97.8 | 10.8 | 2.63 | 2.91x | 1.42x | 0.10x | 37.1x | 4.09x |
| deltablue | macro | 124.0 | 121.4 | 1.14 | 1.56s | 99.3 | 12.0 | 10.3x | 10.1x | 0.10x | 130x | 8.27x |
| havlak | macro | 72.1 | 76.9 | 1.84 | 89.20s | 3.42s | 102.4 | 0.70x | 0.75x | 0.02x | 871x | 33.4x |
| cd | macro | 619.0 | 487.6 | 15.0 | 28.22s | 962.9 | 35.9 | 17.3x | 13.6x | 0.42x | 787x | 26.8x |

### BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 9.31 | 4.61 | 3.03 | 163.3 | 23.6 | 4.29 | 2.17x | 1.08x | 0.71x | 38.1x | 5.50x |
| fannkuch | permutation | 0.323 | 0.395 | 0.149 | 61.9 | 7.23 | 4.01 | 0.08x | 0.10x | 0.04x | 15.5x | 1.80x |
| fasta | generation | 0.817 | 0.923 | 0.240 | 27.1 | 8.91 | 6.14 | 0.13x | 0.15x | 0.04x | 4.42x | 1.45x |
| knucleotide | hashing | 4.32 | 4.85 | 0.285 | 162.2 | 7.67 | 4.85 | 0.89x | 1.00x | 0.06x | 33.4x | 1.58x |
| pidigits | bignum | 0.311 | 0.303 | 0.045 | 0.425 | 0.129 | 1.95 | 0.16x | 0.16x | 0.02x | 0.22x | 0.07x |
| regexredux | regex | 1.27 | 1.28 | 1.14 | 52.3 | 5.58 | 2.41 | 0.53x | 0.53x | 0.47x | 21.7x | 2.31x |
| revcomp | string | 1.25 | 1.18 | 0.384 | 34.7 | 2.53 | 3.33 | 0.38x | 0.36x | 0.12x | 10.4x | 0.76x |
| spectralnorm | numeric | 1.66 | 1.64 | 0.352 | 326.7 | 63.7 | 2.53 | 0.66x | 0.65x | 0.14x | 129x | 25.2x |

### KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 227.2 | 307.9 | 28.1 | 3.75s | 884.9 | 33.5 | 6.77x | 9.18x | 0.84x | 112x | 26.4x |
| matmul | numeric | 15.3 | 23.0 | 6.09 | 1.29s | 539.2 | 15.5 | 0.99x | 1.49x | 0.39x | 83.1x | 34.9x |
| primes | numeric | 15.0 | 3.41 | 1.59 | 3.86s | 94.9 | 4.39 | 3.41x | 0.78x | 0.36x | 881x | 21.6x |
| base64 | string | 11.1 | 11.4 | 0.574 | 863.0 | 157.5 | 17.3 | 0.65x | 0.66x | 0.03x | 50.0x | 9.13x |
| levenshtein | string | 10.4 | 6.40 | 0.902 | 443.4 | 54.2 | 3.97 | 2.62x | 1.61x | 0.23x | 112x | 13.6x |
| json_gen | data | 23.8 | 25.1 | 1.53 | 69.4 | 20.2 | 6.23 | 3.82x | 4.03x | 0.25x | 11.1x | 3.24x |
| collatz | numeric | 342.2 | 343.0 | 226.0 | 5.84s | 6.26s | 1.42s | 0.24x | 0.24x | 0.16x | 4.10x | 4.39x |

### LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 418.5 | 354.6 | 59.7 | 11.46s | 2.19s | 66.6 | 6.29x | 5.33x | 0.90x | 172x | 32.9x |
| array1 | array | 0.821 | 0.816 | 0.319 | 79.3 | 36.0 | 1.90 | 0.43x | 0.43x | 0.17x | 41.8x | 19.0x |
| deriv | symbolic | 30.4 | 13.0 | 2.82 | 345.8 | 58.8 | 3.72 | 8.16x | 3.50x | 0.76x | 92.9x | 15.8x |
| diviter | iterative | 266.5 | 266.6 | 262.5 | 11.62s | 26.73s | 471.6 | 0.57x | 0.57x | 0.56x | 24.6x | 56.7x |
| divrec | recursive | 5.42 | 2.01 | 4.88 | 46.3 | 36.1 | 7.58 | 0.71x | 0.26x | 0.64x | 6.10x | 4.76x |
| gcbench | allocation | 215.2 | 175.5 | 71.3 | 4.08s | 548.3 | 23.3 | 9.25x | 7.54x | 3.06x | 175x | 23.6x |
| paraffins | combinat | 0.828 | 0.230 | 0.049 | 4.21 | 2.49 | 1.000 | 0.83x | 0.23x | 0.05x | 4.22x | 2.49x |
| pnpoly | numeric | 12.8 | 14.4 | 1.92 | 117.8 | 202.1 | 5.90 | 2.16x | 2.44x | 0.32x | 20.0x | 34.2x |
| puzzle | search | 2.39 | 2.42 | 1.29 | 110.6 | 29.3 | 3.28 | 0.73x | 0.74x | 0.39x | 33.7x | 8.95x |
| quicksort | sorting | 0.992 | 1.06 | 0.202 | 404.4 | 19.1 | 1.63 | 0.61x | 0.65x | 0.12x | 248x | 11.7x |
| ray | numeric | 0.297 | 0.297 | 0.172 | 18.3 | 13.9 | 3.64 | 0.08x | 0.08x | 0.05x | 5.04x | 3.82x |

### JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 13.6 | 14.4 | 0.514 | 766.8 | 216.5 | 17.9 | 0.76x | 0.80x | 0.03x | 42.8x | 12.1x |
| navier_stokes | numeric | 153.5 | 153.2 | 47.0 | 1.08s | 98.7 | 14.2 | 10.8x | 10.8x | 3.31x | 75.7x | 6.96x |
| splay | data | 309.0 | 314.2 | 18.6 | 1.16s | 147.6 | 18.9 | 16.4x | 16.6x | 0.98x | 61.3x | 7.82x |
| hashmap | data | 148.4 | 86.0 | 2.84 | 5.52s | 314.8 | 15.3 | 9.69x | 5.61x | 0.19x | 360x | 20.6x |
| crypto_sha1 | crypto | 49.2 | 28.0 | 2.63 | 627.5 | 218.5 | 8.75 | 5.63x | 3.20x | 0.30x | 71.7x | 25.0x |
| raytrace3d | 3d | 72.0 | 19.9 | 2.18 | 800.4 | 162.9 | 18.3 | 3.93x | 1.09x | 0.12x | 43.7x | 8.89x |

### Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 250.8 | 143.7 | 13.1 | 2.63s | 611.9 | 39.2 | 6.39x | 3.66x | 0.33x | 66.9x | 15.6x |
| microdiff | data-diff | 0.140 | 0.140 | 0.018 | 1.31s | 108.8 | 16.3 | 0.009x | 0.009x | 0.001x | 80.3x | 6.68x |
| hyphen | hyphenation | 3.60 | 2.12 | 0.091 | 381.5 | 51.2 | 6.46 | 0.56x | 0.33x | 0.01x | 59.1x | 7.93x |

---

## Part 2 — End-to-end time (wall clock, auto tier)

Wall clock from process invocation to exit, so **every engine pays its own startup and compilation inside the number**. The MIR columns use the shipped auto tier -- no `LAMBDA_TIER` override -- which is what `lambda.exe run script.ls` actually does.

This set exists because the two questions are different. Part 1 asks how fast the compiled workload runs; part 2 asks how long it takes to run the script. Timing the auto tier under part 1's rules would charge Lambda for JIT compilation performed *inside* the measured region while crediting Node.js with a post-warmup figure -- comparing two different things. Here the accounting is the same for everyone.

Same processes, where possible: the reference engines report their wall and `__TIMING__` figures from the *same* run, so parts 1 and 2 are two readings of one launch. Only the MIR columns are re-run, because part 1 pins the JIT and part 2 must use the auto tier.

⚠ Short workloads are dominated by fixed process startup here, so a row whose part-1 time is a fraction of a millisecond says more about executable launch cost than about the language. Read part 2 by the longer rows.

### Summary

| Suite | Total | Timed MIR (untyped, auto) | Timed MIR (typed, auto) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped, auto)/Node geo | MIR (typed, auto)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 0.47x | 0.51x | 1.00x | 1.58x | 0.43x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 1.56x | 2.38x | 0.90x | 7.45x | 0.82x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 0.61x | 0.78x | 1.00x | 2.58x | 0.34x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 3.38x | 7.28x | 0.72x | 14.0x | 2.84x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 2.20x | 3.22x | 0.99x | 6.36x | 1.90x |
| JetStream | 6 | 6 | 5 | 6 | 6 | 6 | 6 | 9.85x | 14.0x | 0.90x | 23.1x | 3.58x |
| Text | 3 | 3 | 3 | 3 | 3 | 3 | 3 | 0.76x | 0.62x | 0.76x | 27.7x | 2.58x |
| **Overall** | 59 | 59 | 58 | 59 | 59 | 59 | 59 | 1.52x | 2.06x | 0.91x | 6.23x | 1.09x |

> Ratio < 1.0 means the engine finished the whole run faster than Node.js.

### R7RS

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 17.6 | 18.5 | 48.1 | 102.7 | 38.7 | 48.0 | 0.37x | 0.38x | 1.00x | 2.14x | 0.81x |
| fibfp | recursive | 18.6 | 18.0 | 49.4 | 79.7 | 25.9 | 49.3 | 0.38x | 0.36x | 1.00x | 1.61x | 0.52x |
| tak | recursive | 16.3 | 16.0 | 46.5 | 40.7 | 8.80 | 44.5 | 0.37x | 0.36x | 1.04x | 0.91x | 0.20x |
| cpstak | closure | 16.3 | 16.4 | 47.1 | 43.8 | 11.9 | 46.1 | 0.35x | 0.35x | 1.02x | 0.95x | 0.26x |
| sum | iterative | 18.8 | 17.4 | 46.4 | 62.9 | 37.8 | 45.4 | 0.41x | 0.38x | 1.02x | 1.38x | 0.83x |
| sumfp | iterative | 29.1 | 33.0 | 45.8 | 37.9 | 9.89 | 47.9 | 0.61x | 0.69x | 0.96x | 0.79x | 0.21x |
| nqueens | backtrack | 26.3 | 69.5 | 47.2 | 122.3 | 14.5 | 47.8 | 0.55x | 1.45x | 0.99x | 2.56x | 0.30x |
| fft | numeric | 31.8 | 34.4 | 46.1 | 99.5 | 8.96 | 45.4 | 0.70x | 0.76x | 1.02x | 2.19x | 0.20x |
| mbrot | numeric | 29.6 | 20.0 | 46.4 | 55.2 | 24.2 | 47.1 | 0.63x | 0.42x | 0.98x | 1.17x | 0.51x |
| ack | recursive | 29.7 | 30.9 | 57.7 | 263.4 | 108.8 | 57.7 | 0.51x | 0.53x | 1.00x | 4.56x | 1.89x |

### AWFY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 17.5 | 18.2 | 46.0 | 51.9 | 6.91 | 46.6 | 0.38x | 0.39x | 0.99x | 1.11x | 0.15x |
| permute | micro | 19.3 | 36.5 | 45.7 | 54.4 | 7.93 | 46.8 | 0.41x | 0.78x | 0.98x | 1.16x | 0.17x |
| queens | micro | 22.2 | 30.7 | 46.1 | 54.7 | 7.33 | 47.7 | 0.47x | 0.64x | 0.97x | 1.15x | 0.15x |
| towers | micro | 21.8 | 58.7 | 46.5 | 78.3 | 9.86 | 47.3 | 0.46x | 1.24x | 0.98x | 1.65x | 0.21x |
| bounce | micro | 20.4 | 20.1 | 46.5 | 133.3 | 7.91 | 47.1 | 0.43x | 0.43x | 0.99x | 2.83x | 0.17x |
| list | micro | 24.5 | 27.8 | 46.3 | 48.6 | 7.55 | 44.9 | 0.55x | 0.62x | 1.03x | 1.08x | 0.17x |
| storage | micro | 19.1 | 26.7 | 46.3 | 124.1 | 9.40 | 47.4 | 0.40x | 0.56x | 0.98x | 2.62x | 0.20x |
| mandelbrot | compute | 3.98s | 4.81s | 77.4 | 499.1 | 877.0 | 77.5 | 51.4x | 62.1x | 1.00x | 6.44x | 11.3x |
| nbody | compute | 202.8 | 997.8 | 48.7 | 820.8 | 167.3 | 52.1 | 3.89x | 19.2x | 0.93x | 15.8x | 3.21x |
| richards | macro | 601.2 | 646.2 | 78.9 | 1.87s | 199.4 | 94.1 | 6.39x | 6.86x | 0.84x | 19.8x | 2.12x |
| json | macro | 71.6 | 154.4 | 53.3 | 306.0 | 19.4 | 48.7 | 1.47x | 3.17x | 1.09x | 6.28x | 0.40x |
| deltablue | macro | 332.2 | 653.8 | 54.6 | 1.83s | 107.9 | 59.1 | 5.62x | 11.1x | 0.92x | 30.9x | 1.83x |
| havlak | macro | 242.3 | 241.6 | 55.0 | 90.83s | 3.44s | 154.0 | 1.57x | 1.57x | 0.36x | 590x | 22.3x |
| cd | macro | 810.7 | 951.2 | 68.2 | 28.94s | 971.8 | 81.5 | 9.95x | 11.7x | 0.84x | 355x | 11.9x |

### BENG

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 33.3 | 80.3 | 50.1 | 209.7 | 30.6 | 47.5 | 0.70x | 1.69x | 1.06x | 4.42x | 0.65x |
| fannkuch | permutation | 56.6 | 66.9 | 46.0 | 105.0 | 14.2 | 47.4 | 1.20x | 1.41x | 0.97x | 2.22x | 0.30x |
| fasta | generation | 24.1 | 25.6 | 46.8 | 74.8 | 15.3 | 50.0 | 0.48x | 0.51x | 0.94x | 1.50x | 0.31x |
| knucleotide | hashing | 27.4 | 40.0 | 47.3 | 209.7 | 14.6 | 48.2 | 0.57x | 0.83x | 0.98x | 4.35x | 0.30x |
| pidigits | bignum | 16.5 | 16.2 | 47.6 | 45.6 | 6.15 | 45.3 | 0.36x | 0.36x | 1.05x | 1.01x | 0.14x |
| regexredux | regex | 15.4 | 15.6 | 48.8 | 99.5 | 11.6 | 45.9 | 0.34x | 0.34x | 1.06x | 2.17x | 0.25x |
| revcomp | string | 18.6 | 18.2 | 46.4 | 84.2 | 8.86 | 46.4 | 0.40x | 0.39x | 1.00x | 1.81x | 0.19x |
| spectralnorm | numeric | 78.5 | 139.6 | 46.8 | 371.1 | 70.4 | 47.8 | 1.64x | 2.92x | 0.98x | 7.76x | 1.47x |

### KOSTYA

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 447.7 | 6.70s | 75.3 | 3.79s | 892.5 | 78.8 | 5.68x | 85.0x | 0.96x | 48.2x | 11.3x |
| matmul | numeric | 2.23s | 2.59s | 51.8 | 1.33s | 546.3 | 61.1 | 36.5x | 42.4x | 0.85x | 21.8x | 8.94x |
| primes | numeric | 439.9 | 581.3 | 47.7 | 3.91s | 101.8 | 49.5 | 8.89x | 11.8x | 0.96x | 79.0x | 2.06x |
| base64 | string | 76.3 | 494.6 | 47.1 | 908.2 | 164.7 | 62.5 | 1.22x | 7.92x | 0.75x | 14.5x | 2.64x |
| levenshtein | string | 217.6 | 262.3 | 47.3 | 490.0 | 61.2 | 49.7 | 4.38x | 5.28x | 0.95x | 9.86x | 1.23x |
| json_gen | data | 62.6 | 69.2 | 47.8 | 113.4 | 26.9 | 51.8 | 1.21x | 1.34x | 0.92x | 2.19x | 0.52x |
| collatz | numeric | 616.1 | 670.5 | 272.2 | 5.88s | 6.26s | 1.47s | 0.42x | 0.46x | 0.19x | 4.00x | 4.26x |

### LARCENY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 444.1 | 384.4 | 105.2 | 11.51s | 2.19s | 112.6 | 3.95x | 3.41x | 0.93x | 102x | 19.5x |
| array1 | array | 173.7 | 216.2 | 45.7 | 116.4 | 42.9 | 46.8 | 3.71x | 4.62x | 0.98x | 2.49x | 0.92x |
| deriv | symbolic | 57.3 | 427.2 | 50.0 | 396.7 | 65.9 | 49.0 | 1.17x | 8.72x | 1.02x | 8.10x | 1.34x |
| diviter | iterative | 19.24s | 281.3 | 309.0 | 11.66s | 26.74s | 517.4 | 37.2x | 0.54x | 0.60x | 22.5x | 51.7x |
| divrec | recursive | 41.0 | 20.4 | 51.5 | 84.4 | 42.7 | 52.3 | 0.78x | 0.39x | 0.98x | 1.61x | 0.82x |
| gcbench | allocation | 399.6 | 1.60s | 118.3 | 4.27s | 560.8 | 67.5 | 5.92x | 23.7x | 1.75x | 63.3x | 8.31x |
| paraffins | combinat | 31.7 | 40.0 | 46.8 | 55.2 | 8.73 | 46.1 | 0.69x | 0.87x | 1.02x | 1.20x | 0.19x |
| pnpoly | numeric | 170.1 | 1.58s | 48.3 | 158.7 | 209.4 | 50.9 | 3.34x | 31.1x | 0.95x | 3.12x | 4.12x |
| puzzle | search | 46.2 | 263.2 | 47.4 | 151.5 | 36.3 | 48.4 | 0.95x | 5.44x | 0.98x | 3.13x | 0.75x |
| quicksort | sorting | 35.6 | 158.5 | 46.5 | 446.1 | 25.9 | 46.7 | 0.76x | 3.40x | 1.00x | 9.56x | 0.56x |
| ray | numeric | 57.2 | 54.2 | 46.3 | 63.5 | 20.8 | 48.9 | 1.17x | 1.11x | 0.95x | 1.30x | 0.42x |

### JetStream

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 405.2 | 485.7 | 50.9 | 880.1 | 224.2 | 65.4 | 6.19x | 7.42x | 0.78x | 13.4x | 3.43x |
| navier_stokes | numeric | 9.44s | 10.29s | 95.7 | 1.41s | 110.8 | 62.1 | 152x | 166x | 1.54x | 22.7x | 1.78x |
| splay | data | 449.0 | 376.9 | 66.9 | 2.90s | 567.4 | 96.0 | 4.68x | 3.92x | 0.70x | 30.2x | 5.91x |
| hashmap | data | 336.0 | --- | 51.0 | 5.66s | 323.0 | 60.2 | 5.58x | --- | 0.85x | 94.1x | 5.37x |
| crypto_sha1 | crypto | 222.6 | 638.6 | 50.1 | 679.1 | 226.1 | 54.3 | 4.10x | 11.8x | 0.92x | 12.5x | 4.16x |
| raytrace3d | 3d | 591.8 | 607.2 | 53.0 | 900.8 | 170.0 | 65.2 | 9.08x | 9.32x | 0.81x | 13.8x | 2.61x |

### Text

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 277.3 | 168.5 | 60.1 | 2.79s | 620.3 | 85.1 | 3.26x | 1.98x | 0.71x | 32.8x | 7.29x |
| microdiff | data-diff | 17.6 | 17.0 | 45.8 | 1.39s | 116.2 | 61.6 | 0.29x | 0.28x | 0.74x | 22.5x | 1.89x |
| hyphen | hyphenation | 26.2 | 24.1 | 46.7 | 1.59s | 69.0 | 55.0 | 0.48x | 0.44x | 0.85x | 28.9x | 1.26x |

