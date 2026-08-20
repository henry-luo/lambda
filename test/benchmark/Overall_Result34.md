# Lambda Benchmark Results: Result34

- **Date:** 2026-08-21
- **Platform:** Darwin arm64
- **Lambda commit:** `697b5191b596eed8fd30079714f8c2ff8faf9ddd`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v34-697b5191b5` (20,904,904 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 186.60s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 186.4s (batched 185.5s: sync 114.0s, async 71.6s; non-batched 0.9s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v34.json`
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 9 | 10 | 0.49x | 0.37x | 0.19x | 11.5x | 6.19x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 1.57x | 0.89x | 0.09x | 42.7x | 5.21x |
| BENG | 8 | 8 | 8 | 8 | 8 | 5 | 8 | 0.51x | 0.42x | 0.10x | 12.0x | 1.86x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 2.21x | 1.41x | 0.23x | 54.7x | 12.0x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 2.39x | 0.86x | 0.33x | 33.3x | 13.3x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 4 | 6 | 5.89x | 3.41x | 0.29x | 75.6x | 13.3x |
| Text | 3 | 3 | 3 | 3 | 3 | 3 | 3 | 0.51x | 0.40x | 0.02x | 66.0x | 9.26x |
| **Overall** | 59 | 59 | 59 | 59 | 59 | 53 | 59 | 1.35x | 0.80x | 0.15x | 30.6x | 7.32x |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 5.23x over 59 of 59 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| awfy/deltablue | 75.5 | 1.15 | 66.0x |
| text/microdiff | 0.915 | 0.017 | 53.5x |
| kostya/base64 | 17.1 | 0.571 | 29.9x |
| jetstream/cube3d | 13.8 | 0.516 | 26.8x |
| awfy/havlak | 48.2 | 1.84 | 26.2x |
| awfy/list | 0.569 | 0.022 | 25.6x |
| text/hyphen | 2.11 | 0.088 | 24.0x |
| jetstream/raytrace3d | 45.8 | 2.17 | 21.1x |
| jetstream/hashmap | 53.2 | 2.74 | 19.4x |
| awfy/queens | 0.306 | 0.018 | 17.2x |
| beng/knucleotide | 4.72 | 0.283 | 16.7x |
| awfy/cd | 215.5 | 15.1 | 14.3x |

---

## Notable Results

- Missing timings: **6** cells
- QuickJS missing: r7rs/ack (exit_1), beng/knucleotide (exit_1), beng/regexredux (exit_1), beng/revcomp (exit_1), jetstream/cube3d (exit_1), jetstream/raytrace3d (exit_1)

### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/havlak | 106.83s | 95.8 | 1115x |
| kostya/primes | 3.88s | 4.41 | 879x |
| awfy/cd | 27.96s | 35.8 | 781x |
| jetstream/hashmap | 5.00s | 15.1 | 331x |
| larceny/quicksort | 401.6 | 1.65 | 244x |
| awfy/nbody | 1.00s | 5.38 | 187x |
| awfy/deltablue | 2.00s | 11.6 | 172x |
| larceny/triangl | 11.45s | 66.6 | 172x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.424 | 2.10 | 0.20x |

---

## R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.34 | 1.33 | 1.13 | 39.3 | 18.8 | 2.33 | 0.58x | 0.57x | 0.48x | 16.9x | 8.08x |
| fibfp | recursive | 2.04 | 1.24 | 1.05 | 39.2 | 18.9 | 1.83 | 1.11x | 0.67x | 0.58x | 21.4x | 10.3x |
| tak | recursive | 0.126 | 0.161 | 0.122 | 2.68 | 2.78 | 0.815 | 0.15x | 0.20x | 0.15x | 3.29x | 3.40x |
| cpstak | closure | 0.248 | 0.316 | 0.216 | 5.13 | 5.57 | 1.000 | 0.25x | 0.32x | 0.22x | 5.13x | 5.57x |
| sum | iterative | 0.827 | 0.827 | 0.271 | 25.1 | 31.1 | 1.18 | 0.70x | 0.70x | 0.23x | 21.2x | 26.3x |
| sumfp | iterative | 0.068 | 0.069 | 0.077 | 2.50 | 3.65 | 0.861 | 0.08x | 0.08x | 0.09x | 2.90x | 4.24x |
| nqueens | backtrack | 1.58 | 1.16 | 0.130 | 38.5 | 7.83 | 1.75 | 0.90x | 0.66x | 0.07x | 22.0x | 4.47x |
| fft | numeric | 0.206 | 0.241 | 0.027 | 52.9 | 2.74 | 1.60 | 0.13x | 0.15x | 0.02x | 33.2x | 1.72x |
| mbrot | numeric | 11.1 | 0.540 | 0.446 | 16.0 | 17.6 | 1.80 | 6.16x | 0.30x | 0.25x | 8.89x | 9.81x |
| ack | recursive | 10.9 | 14.4 | 11.8 | 215.5 | --- | 13.4 | 0.82x | 1.07x | 0.88x | 16.1x | --- |

## AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.031 | 0.035 | 0.015 | 10.0 | 0.617 | 0.383 | 0.08x | 0.09x | 0.04x | 26.2x | 1.61x |
| permute | micro | 0.668 | 0.130 | 0.024 | 11.5 | 1.54 | 0.815 | 0.82x | 0.16x | 0.03x | 14.1x | 1.89x |
| queens | micro | 0.384 | 0.306 | 0.018 | 6.81 | 1.05 | 0.638 | 0.60x | 0.48x | 0.03x | 10.7x | 1.64x |
| towers | micro | 1.09 | 0.401 | 0.029 | 30.9 | 2.22 | 1.11 | 0.98x | 0.36x | 0.03x | 27.7x | 1.99x |
| bounce | micro | 0.069 | 0.113 | 0.026 | 7.29 | 0.877 | 0.555 | 0.12x | 0.20x | 0.05x | 13.2x | 1.58x |
| list | micro | 0.679 | 0.569 | 0.022 | 3.35 | 0.910 | 0.491 | 1.38x | 1.16x | 0.05x | 6.83x | 1.85x |
| storage | micro | 0.800 | 0.527 | 0.090 | 7.38 | 2.14 | 0.627 | 1.28x | 0.84x | 0.14x | 11.8x | 3.40x |
| mandelbrot | compute | 38.4 | 38.5 | 30.6 | 445.2 | 870.4 | 31.1 | 1.24x | 1.24x | 0.98x | 14.3x | 28.0x |
| nbody | compute | 32.4 | 20.2 | 1.49 | 1.00s | 159.2 | 5.38 | 6.02x | 3.76x | 0.28x | 187x | 29.6x |
| richards | macro | 2.18s | 217.5 | 29.2 | 1.82s | 190.7 | 47.4 | 46.1x | 4.58x | 0.62x | 38.3x | 4.02x |
| json | macro | 6.88 | 2.55 | 0.256 | 97.8 | 10.9 | 2.61 | 2.64x | 0.98x | 0.10x | 37.5x | 4.19x |
| deltablue | macro | 79.6 | 75.5 | 1.15 | 2.00s | 99.1 | 11.6 | 6.87x | 6.52x | 0.10x | 172x | 8.55x |
| havlak | macro | 47.7 | 48.2 | 1.84 | 106.83s | 3.21s | 95.8 | 0.50x | 0.50x | 0.02x | 1115x | 33.5x |
| cd | macro | 774.0 | 215.5 | 15.1 | 27.96s | 966.8 | 35.8 | 21.6x | 6.02x | 0.42x | 781x | 27.0x |

## BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 9.04 | 19.0 | 3.00 | 160.3 | 23.5 | 4.07 | 2.22x | 4.68x | 0.74x | 39.4x | 5.78x |
| fannkuch | permutation | 0.329 | 0.402 | 0.150 | 60.3 | 7.18 | 3.96 | 0.08x | 0.10x | 0.04x | 15.2x | 1.82x |
| fasta | generation | 0.789 | 0.875 | 0.245 | 26.6 | 8.76 | 6.12 | 0.13x | 0.14x | 0.04x | 4.35x | 1.43x |
| knucleotide | hashing | 4.23 | 4.72 | 0.283 | 161.5 | --- | 4.84 | 0.87x | 0.98x | 0.06x | 33.4x | --- |
| pidigits | bignum | 0.299 | 0.304 | 0.045 | 0.424 | 0.130 | 2.10 | 0.14x | 0.15x | 0.02x | 0.20x | 0.06x |
| regexredux | regex | 1.28 | 1.28 | 1.14 | 51.5 | --- | 2.49 | 0.51x | 0.51x | 0.46x | 20.7x | --- |
| revcomp | string | 1.25 | 1.19 | 0.382 | 32.0 | --- | 3.34 | 0.37x | 0.35x | 0.11x | 9.59x | --- |
| spectralnorm | numeric | 22.2 | 1.58 | 0.354 | 318.0 | 63.8 | 2.66 | 8.33x | 0.59x | 0.13x | 120x | 24.0x |

## KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 314.4 | 379.2 | 28.0 | 3.75s | 890.5 | 33.4 | 9.41x | 11.4x | 0.84x | 112x | 26.7x |
| matmul | numeric | 11.8 | 11.4 | 6.08 | 1.25s | 539.4 | 15.4 | 0.77x | 0.74x | 0.40x | 81.5x | 35.1x |
| primes | numeric | 19.3 | 3.40 | 1.60 | 3.88s | 94.7 | 4.41 | 4.37x | 0.77x | 0.36x | 879x | 21.5x |
| base64 | string | 16.9 | 17.1 | 0.571 | 872.8 | 157.5 | 17.2 | 0.98x | 0.99x | 0.03x | 50.7x | 9.16x |
| levenshtein | string | 34.3 | 7.01 | 0.901 | 430.3 | 54.1 | 3.92 | 8.76x | 1.79x | 0.23x | 110x | 13.8x |
| json_gen | data | 20.7 | 21.6 | 1.52 | 53.2 | 20.0 | 6.31 | 3.27x | 3.42x | 0.24x | 8.43x | 3.17x |
| collatz | numeric | 417.6 | 403.7 | 226.0 | 5.53s | 6.26s | 1.43s | 0.29x | 0.28x | 0.16x | 3.87x | 4.38x |

## LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 501.1 | 210.5 | 60.4 | 11.45s | 2.20s | 66.6 | 7.52x | 3.16x | 0.91x | 172x | 33.1x |
| array1 | array | 0.816 | 0.824 | 0.316 | 77.1 | 36.0 | 1.90 | 0.43x | 0.43x | 0.17x | 40.6x | 19.0x |
| deriv | symbolic | 29.0 | 10.8 | 2.79 | 343.3 | 59.1 | 3.67 | 7.89x | 2.95x | 0.76x | 93.4x | 16.1x |
| diviter | iterative | 266.7 | 266.5 | 266.3 | 11.78s | 26.90s | 472.6 | 0.56x | 0.56x | 0.56x | 24.9x | 56.9x |
| divrec | recursive | 15.7 | 2.02 | 4.87 | 44.5 | 36.4 | 7.48 | 2.10x | 0.27x | 0.65x | 5.95x | 4.87x |
| gcbench | allocation | 212.3 | 148.7 | 70.6 | 3.93s | 548.8 | 23.3 | 9.12x | 6.39x | 3.03x | 169x | 23.6x |
| paraffins | combinat | 0.509 | 0.286 | 0.049 | 4.08 | 2.50 | 0.987 | 0.52x | 0.29x | 0.05x | 4.14x | 2.53x |
| pnpoly | numeric | 14.1 | 15.6 | 2.07 | 116.0 | 201.4 | 5.85 | 2.40x | 2.66x | 0.35x | 19.8x | 34.4x |
| puzzle | search | 9.03 | 4.00 | 1.28 | 110.6 | 29.3 | 3.30 | 2.74x | 1.21x | 0.39x | 33.5x | 8.88x |
| quicksort | sorting | 10.0 | 1.03 | 0.201 | 401.6 | 19.2 | 1.65 | 6.09x | 0.62x | 0.12x | 244x | 11.7x |
| ray | numeric | 9.08 | 0.296 | 0.175 | 17.8 | 13.7 | 3.50 | 2.60x | 0.08x | 0.05x | 5.08x | 3.93x |

## JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 11.3 | 13.8 | 0.516 | 761.8 | --- | 17.6 | 0.64x | 0.79x | 0.03x | 43.3x | --- |
| navier_stokes | numeric | 140.3 | 116.7 | 46.9 | 1.03s | 98.4 | 14.0 | 10.0x | 8.35x | 3.35x | 74.0x | 7.04x |
| splay | data | 133.1 | 151.6 | 18.5 | 1.11s | 148.7 | 18.8 | 7.07x | 8.05x | 0.98x | 58.7x | 7.89x |
| hashmap | data | 141.2 | 53.2 | 2.74 | 5.00s | 333.3 | 15.1 | 9.33x | 3.52x | 0.18x | 331x | 22.0x |
| crypto_sha1 | crypto | 66.0 | 30.0 | 2.74 | 613.1 | 219.3 | 8.62 | 7.65x | 3.48x | 0.32x | 71.1x | 25.4x |
| raytrace3d | 3d | 239.8 | 45.8 | 2.17 | 790.7 | --- | 18.7 | 12.8x | 2.45x | 0.12x | 42.3x | --- |

## Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 256.1 | 155.8 | 13.1 | 2.62s | 627.1 | 39.1 | 6.55x | 3.99x | 0.34x | 67.0x | 16.0x |
| microdiff | data-diff | 0.919 | 0.915 | 0.017 | 1.32s | 110.0 | 16.2 | 0.06x | 0.06x | 0.001x | 81.5x | 6.79x |
| hyphen | hyphenation | 2.52 | 2.11 | 0.088 | 381.2 | 52.8 | 7.24 | 0.35x | 0.29x | 0.01x | 52.6x | 7.29x |
