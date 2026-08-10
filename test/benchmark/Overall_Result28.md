# Lambda Benchmark Results: Result28

- **Date:** 2026-08-10
- **Platform:** Darwin arm64
- **Lambda commit:** `e91432d4aa1701171cc339dd713232385cf1e6e0`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v28-e91432d4aa` (21,872,568 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 101.10s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 101.0s (batched 99.2s: sync 80.5s, async 18.7s; non-batched 1.8s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v28.json`
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 9 | 10 | 0.87x | 0.49x | 0.19x | 5.81x | 6.49x |
| AWFY | 14 | 14 | 14 | 9 | 14 | 14 | 14 | 2.47x | 1.32x | 0.08x | 25.9x | 5.25x |
| BENG | 8 | 8 | 8 | 7 | 8 | 5 | 8 | 0.59x | 0.44x | 0.12x | 7.49x | 1.90x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 3.13x | 1.65x | 0.23x | 15.9x | 11.9x |
| LARCENY | 11 | 11 | 11 | 10 | 11 | 11 | 11 | 3.05x | 1.31x | 0.30x | 14.7x | 13.3x |
| JetStream | 6 | 6 | 6 | 1 | 6 | 4 | 6 | 8.79x | 4.11x | 0.18x | 71.2x | 12.7x |
| Text | 3 | 3 | 3 | 3 | 3 | 3 | 3 | 0.92x | 0.69x | 0.02x | 62.8x | 9.38x |
| **Overall** | 59 | 59 | 59 | 47 | 59 | 53 | 59 | 1.97x | 1.07x | 0.15x | 16.7x | 7.38x |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 5.71x over 47 of 59 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| kostya/base64 | 48.3 | 0.556 | 86.9x |
| text/microdiff | 0.951 | 0.019 | 49.9x |
| awfy/list | 0.906 | 0.022 | 41.2x |
| text/hyphen | 3.25 | 0.089 | 36.4x |
| text/fast_diff | 434.2 | 13.0 | 33.5x |
| awfy/bounce | 0.746 | 0.024 | 31.2x |
| larceny/quicksort | 4.78 | 0.197 | 24.3x |
| awfy/towers | 0.653 | 0.028 | 23.3x |
| jetstream/hashmap | 56.3 | 2.75 | 20.4x |
| larceny/paraffins | 0.815 | 0.048 | 16.9x |
| beng/knucleotide | 4.86 | 0.288 | 16.9x |
| awfy/queens | 0.336 | 0.020 | 16.8x |

---

## Notable Results

- Missing timings: **18** cells
- QuickJS missing: r7rs/ack (exit_1), beng/knucleotide (exit_1), beng/regexredux (exit_1), beng/revcomp (exit_1), jetstream/cube3d (exit_1), jetstream/raytrace3d (exit_1)
- C2MIR missing: awfy/richards (missing_port), awfy/json (missing_port), awfy/deltablue (missing_port), awfy/havlak (missing_port), awfy/cd (missing_port), beng/pidigits (missing_port), larceny/deriv (missing_port), jetstream/cube3d (missing_port), +4 more

### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/havlak | 54.21s | 95.2 | 569x |
| awfy/cd | 10.32s | 35.7 | 289x |
| jetstream/hashmap | 3.35s | 15.4 | 218x |
| jetstream/crypto_sha1 | 1.75s | 8.80 | 199x |
| beng/spectralnorm | 300.7 | 2.64 | 114x |
| awfy/nbody | 582.9 | 5.39 | 108x |
| text/microdiff | 1.49s | 16.2 | 92.0x |
| awfy/deltablue | 1.03s | 11.8 | 87.2x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.314 | 1.95 | 0.16x |

---

## R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.26 | 1.23 | 1.01 | 22.3 | 18.6 | 1.79 | 0.71x | 0.69x | 0.56x | 12.5x | 10.4x |
| fibfp | recursive | 4.07 | 2.87 | 1.05 | 22.1 | 18.5 | 1.75 | 2.33x | 1.64x | 0.60x | 12.7x | 10.6x |
| tak | recursive | 0.143 | 0.145 | 0.119 | 1.81 | 2.79 | 0.777 | 0.18x | 0.19x | 0.15x | 2.33x | 3.59x |
| cpstak | closure | 0.287 | 0.292 | 0.240 | 3.60 | 5.53 | 0.970 | 0.30x | 0.30x | 0.25x | 3.71x | 5.70x |
| sum | iterative | 0.825 | 0.837 | 0.270 | 10.5 | 31.1 | 1.19 | 0.69x | 0.70x | 0.23x | 8.79x | 26.1x |
| sumfp | iterative | 0.318 | 0.318 | 0.080 | 1.05 | 3.72 | 0.859 | 0.37x | 0.37x | 0.09x | 1.22x | 4.33x |
| nqueens | backtrack | 1.96 | 1.60 | 0.127 | 23.6 | 7.86 | 1.76 | 1.12x | 0.91x | 0.07x | 13.4x | 4.48x |
| fft | numeric | 2.49 | 0.240 | 0.025 | 11.6 | 2.78 | 1.56 | 1.60x | 0.15x | 0.02x | 7.42x | 1.79x |
| mbrot | numeric | 11.2 | 0.636 | 0.454 | 9.94 | 17.7 | 1.76 | 6.34x | 0.36x | 0.26x | 5.64x | 10.1x |
| ack | recursive | 13.2 | 12.1 | 11.7 | 70.8 | --- | 13.3 | 0.99x | 0.91x | 0.88x | 5.31x | --- |

## AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.542 | 0.049 | 0.016 | 0.477 | 0.623 | 0.384 | 1.41x | 0.13x | 0.04x | 1.24x | 1.62x |
| permute | micro | 0.896 | 0.154 | 0.025 | 9.88 | 1.57 | 0.822 | 1.09x | 0.19x | 0.03x | 12.0x | 1.91x |
| queens | micro | 0.558 | 0.336 | 0.020 | 6.45 | 1.05 | 0.642 | 0.87x | 0.52x | 0.03x | 10.0x | 1.63x |
| towers | micro | 1.32 | 0.653 | 0.028 | 27.2 | 2.29 | 1.12 | 1.18x | 0.58x | 0.03x | 24.2x | 2.04x |
| bounce | micro | 0.266 | 0.746 | 0.024 | 5.00 | 0.867 | 0.543 | 0.49x | 1.37x | 0.04x | 9.20x | 1.60x |
| list | micro | 0.843 | 0.906 | 0.022 | 3.35 | 0.924 | 0.489 | 1.72x | 1.85x | 0.04x | 6.84x | 1.89x |
| storage | micro | 0.831 | 0.622 | 0.097 | 15.2 | 2.19 | 0.630 | 1.32x | 0.99x | 0.15x | 24.1x | 3.48x |
| mandelbrot | compute | 48.3 | 48.1 | 30.7 | 309.9 | 871.0 | 31.2 | 1.55x | 1.54x | 0.99x | 9.94x | 27.9x |
| nbody | compute | 33.6 | 21.7 | 1.50 | 582.9 | 159.3 | 5.39 | 6.22x | 4.03x | 0.28x | 108x | 29.6x |
| richards | macro | 2.56s | 257.1 | --- | 1.85s | 191.6 | 47.4 | 53.9x | 5.42x | --- | 39.1x | 4.04x |
| json | macro | 7.53 | 2.78 | --- | 48.0 | 10.9 | 2.62 | 2.88x | 1.06x | --- | 18.3x | 4.16x |
| deltablue | macro | 98.1 | 100.5 | --- | 1.03s | 99.3 | 11.8 | 8.33x | 8.53x | --- | 87.2x | 8.43x |
| havlak | macro | 55.4 | 55.6 | --- | 54.21s | 3.35s | 95.2 | 0.58x | 0.58x | --- | 569x | 35.2x |
| cd | macro | 903.2 | 547.1 | --- | 10.32s | 967.7 | 35.7 | 25.3x | 15.3x | --- | 289x | 27.1x |

## BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 10.4 | 5.31 | 3.03 | 47.6 | 23.5 | 4.08 | 2.55x | 1.30x | 0.74x | 11.7x | 5.76x |
| fannkuch | permutation | 0.350 | 1.48 | 0.150 | 13.2 | 7.17 | 3.99 | 0.09x | 0.37x | 0.04x | 3.30x | 1.80x |
| fasta | generation | 0.752 | 0.869 | 0.244 | 26.3 | 8.80 | 6.09 | 0.12x | 0.14x | 0.04x | 4.32x | 1.45x |
| knucleotide | hashing | 4.43 | 4.86 | 0.288 | 152.0 | --- | 5.01 | 0.88x | 0.97x | 0.06x | 30.3x | --- |
| pidigits | bignum | 0.295 | 0.298 | --- | 0.314 | 0.131 | 1.95 | 0.15x | 0.15x | --- | 0.16x | 0.07x |
| regexredux | regex | 1.28 | 1.28 | 1.15 | 17.7 | --- | 2.43 | 0.53x | 0.53x | 0.47x | 7.30x | --- |
| revcomp | string | 1.37 | 1.21 | 0.382 | 48.8 | --- | 3.32 | 0.41x | 0.37x | 0.11x | 14.7x | --- |
| spectralnorm | numeric | 45.0 | 2.02 | 0.355 | 300.7 | 64.2 | 2.64 | 17.1x | 0.76x | 0.13x | 114x | 24.3x |

## KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 307.9 | 357.7 | 27.9 | 954.6 | 885.7 | 33.9 | 9.07x | 10.5x | 0.82x | 28.1x | 26.1x |
| matmul | numeric | 12.8 | 11.7 | 6.14 | 897.8 | 540.0 | 15.5 | 0.82x | 0.75x | 0.39x | 57.8x | 34.7x |
| primes | numeric | 66.3 | 3.40 | 1.59 | 101.8 | 94.8 | 4.48 | 14.8x | 0.76x | 0.36x | 22.7x | 21.2x |
| base64 | string | 51.6 | 48.3 | 0.556 | 716.8 | 158.6 | 17.6 | 2.94x | 2.75x | 0.03x | 40.8x | 9.02x |
| levenshtein | string | 35.8 | 7.82 | 0.913 | 80.9 | 55.2 | 4.01 | 8.91x | 1.95x | 0.23x | 20.1x | 13.8x |
| json_gen | data | 21.1 | 21.8 | 1.53 | 37.6 | 20.1 | 6.19 | 3.41x | 3.52x | 0.25x | 6.06x | 3.24x |
| collatz | numeric | 426.7 | 413.1 | 225.4 | 1.96s | 6.24s | 1.42s | 0.30x | 0.29x | 0.16x | 1.38x | 4.38x |

## LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 475.8 | 189.5 | 60.3 | 4.26s | 2.19s | 66.8 | 7.13x | 2.84x | 0.90x | 63.9x | 32.8x |
| array1 | array | 1.09 | 1.09 | 0.317 | 28.5 | 35.9 | 1.90 | 0.57x | 0.57x | 0.17x | 15.0x | 18.9x |
| deriv | symbolic | 35.5 | 11.9 | --- | 106.6 | 59.0 | 3.70 | 9.59x | 3.21x | --- | 28.8x | 15.9x |
| diviter | iterative | 400.2 | 399.4 | 263.9 | 8.21s | 26.56s | 468.7 | 0.85x | 0.85x | 0.56x | 17.5x | 56.7x |
| divrec | recursive | 15.6 | 2.00 | 4.85 | 26.3 | 35.8 | 7.48 | 2.09x | 0.27x | 0.65x | 3.52x | 4.79x |
| gcbench | allocation | 239.4 | 153.9 | 70.6 | 1.56s | 550.5 | 23.0 | 10.4x | 6.68x | 3.07x | 67.8x | 23.9x |
| paraffins | combinat | 1.95 | 0.815 | 0.048 | 2.77 | 2.50 | 0.981 | 1.98x | 0.83x | 0.05x | 2.83x | 2.54x |
| pnpoly | numeric | 16.0 | 16.6 | 1.89 | 111.2 | 200.4 | 5.74 | 2.79x | 2.89x | 0.33x | 19.4x | 34.9x |
| puzzle | search | 9.39 | 4.56 | 1.27 | 25.7 | 29.1 | 3.28 | 2.87x | 1.39x | 0.39x | 7.83x | 8.88x |
| quicksort | sorting | 10.1 | 4.78 | 0.197 | 64.8 | 19.0 | 1.64 | 6.17x | 2.92x | 0.12x | 39.6x | 11.6x |
| ray | numeric | 10.5 | 0.882 | 0.176 | 12.4 | 13.7 | 3.51 | 3.00x | 0.25x | 0.05x | 3.53x | 3.89x |

## JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 12.2 | 11.2 | --- | 659.3 | --- | 17.5 | 0.70x | 0.64x | --- | 37.7x | --- |
| navier_stokes | numeric | 1.02s | 190.9 | --- | 713.9 | 98.6 | 14.3 | 71.3x | 13.4x | --- | 50.0x | 6.91x |
| splay | data | 153.8 | 258.5 | --- | 513.5 | 146.2 | 19.6 | 7.85x | 13.2x | --- | 26.2x | 7.46x |
| hashmap | data | 165.9 | 56.3 | 2.75 | 3.35s | 317.5 | 15.4 | 10.8x | 3.66x | 0.18x | 218x | 20.6x |
| crypto_sha1 | crypto | 61.8 | 30.4 | --- | 1.75s | 218.5 | 8.80 | 7.02x | 3.46x | --- | 199x | 24.8x |
| raytrace3d | 3d | 289.4 | 62.1 | --- | 1.13s | --- | 18.5 | 15.6x | 3.36x | --- | 61.0x | --- |

## Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 1.25s | 434.2 | 13.0 | 2.22s | 607.9 | 39.3 | 31.9x | 11.1x | 0.33x | 56.4x | 15.5x |
| microdiff | data-diff | 0.929 | 0.951 | 0.019 | 1.49s | 108.6 | 16.2 | 0.06x | 0.06x | 0.001x | 92.0x | 6.70x |
| hyphen | hyphenation | 2.73 | 3.25 | 0.089 | 306.2 | 51.2 | 6.43 | 0.43x | 0.50x | 0.01x | 47.6x | 7.95x |
