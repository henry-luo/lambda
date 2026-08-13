# Lambda Benchmark Results: Result29

- **Date:** 2026-08-13
- **Platform:** Darwin arm64
- **Lambda commit:** `211fea19fb378d0b2a7e9648692653c52faccdab`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v29-211fea19fb` (22,334,776 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 141.60s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 141.5s (batched 140.6s: sync 115.1s, async 25.5s; non-batched 0.9s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v29.json`
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 9 | 10 | 0.82x | 0.48x | 0.19x | 8.06x | 6.38x |
| AWFY | 14 | 14 | 14 | 9 | 14 | 14 | 14 | 2.48x | 1.26x | 0.07x | 184x | 5.07x |
| BENG | 8 | 8 | 8 | 7 | 8 | 5 | 8 | 0.60x | 0.46x | 0.12x | 12.9x | 1.89x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 3.10x | 1.65x | 0.23x | 54.4x | 11.8x |
| LARCENY | 11 | 11 | 11 | 10 | 11 | 11 | 11 | 2.88x | 1.20x | 0.29x | 48.9x | 12.8x |
| JetStream | 6 | 6 | 6 | 1 | 6 | 4 | 6 | 7.68x | 3.59x | 0.23x | 95.4x | 11.5x |
| Text | 3 | 3 | 3 | 3 | 3 | 3 | 3 | 0.43x | 0.33x | 0.008x | 80.6x | 4.81x |
| **Overall** | 59 | 59 | 59 | 47 | 59 | 53 | 59 | 1.84x | 0.99x | 0.14x | 45.8x | 6.90x |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 5.84x over 47 of 59 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| kostya/base64 | 48.3 | 0.585 | 82.6x |
| text/microdiff | 1.18 | 0.022 | 53.2x |
| awfy/list | 1.09 | 0.022 | 49.8x |
| text/hyphen | 4.74 | 0.119 | 39.8x |
| text/fast_diff | 480.4 | 13.4 | 36.0x |
| awfy/bounce | 0.798 | 0.024 | 33.0x |
| larceny/quicksort | 5.39 | 0.201 | 26.9x |
| awfy/towers | 0.711 | 0.028 | 25.3x |
| jetstream/hashmap | 84.6 | 3.65 | 23.2x |
| awfy/queens | 0.342 | 0.019 | 18.0x |
| larceny/paraffins | 0.916 | 0.051 | 18.0x |
| beng/knucleotide | 5.07 | 0.287 | 17.7x |

---

## Notable Results

- Missing timings: **18** cells
- QuickJS missing: r7rs/ack (exit_1), beng/knucleotide (exit_1), beng/regexredux (exit_1), beng/revcomp (exit_1), jetstream/cube3d (exit_1), jetstream/raytrace3d (exit_1)
- C2MIR missing: awfy/richards (missing_port), awfy/json (missing_port), awfy/deltablue (missing_port), awfy/havlak (missing_port), awfy/cd (missing_port), beng/pidigits (missing_port), larceny/deriv (missing_port), jetstream/cube3d (missing_port), +4 more

### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/sieve | 2.93s | 0.389 | 7544x |
| larceny/puzzle | 6.46s | 3.34 | 1933x |
| kostya/primes | 4.61s | 4.53 | 1017x |
| awfy/havlak | 107.29s | 107.3 | 1000x |
| awfy/nbody | 5.70s | 6.08 | 938x |
| awfy/cd | 28.51s | 36.5 | 780x |
| awfy/permute | 625.5 | 0.821 | 761x |
| jetstream/hashmap | 6.49s | 15.6 | 416x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.433 | 1.97 | 0.22x |

---

## R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.33 | 1.33 | 1.12 | 23.8 | 18.8 | 1.79 | 0.74x | 0.74x | 0.62x | 13.3x | 10.5x |
| fibfp | recursive | 3.86 | 2.94 | 1.15 | 23.7 | 18.9 | 1.78 | 2.17x | 1.65x | 0.65x | 13.3x | 10.7x |
| tak | recursive | 0.129 | 0.155 | 0.122 | 1.70 | 2.82 | 0.802 | 0.16x | 0.19x | 0.15x | 2.12x | 3.52x |
| cpstak | closure | 0.251 | 0.298 | 0.226 | 3.41 | 5.60 | 1.00 | 0.25x | 0.30x | 0.23x | 3.40x | 5.59x |
| sum | iterative | 0.838 | 0.837 | 0.273 | 14.8 | 31.4 | 1.21 | 0.69x | 0.69x | 0.23x | 12.2x | 25.9x |
| sumfp | iterative | 0.323 | 0.323 | 0.080 | 1.49 | 3.77 | 0.888 | 0.36x | 0.36x | 0.09x | 1.68x | 4.24x |
| nqueens | backtrack | 2.01 | 1.68 | 0.132 | 43.6 | 7.90 | 1.79 | 1.12x | 0.94x | 0.07x | 24.3x | 4.40x |
| fft | numeric | 2.53 | 0.233 | 0.025 | 64.6 | 2.78 | 1.62 | 1.56x | 0.14x | 0.02x | 39.8x | 1.71x |
| mbrot | numeric | 11.2 | 0.646 | 0.449 | 17.0 | 18.0 | 1.87 | 6.03x | 0.35x | 0.24x | 9.13x | 9.65x |
| ack | recursive | 10.8 | 11.3 | 11.9 | 67.6 | --- | 13.5 | 0.80x | 0.84x | 0.88x | 5.00x | --- |

## AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.549 | 0.052 | 0.016 | 2.93s | 0.619 | 0.389 | 1.41x | 0.13x | 0.04x | 7544x | 1.59x |
| permute | micro | 0.948 | 0.151 | 0.027 | 625.5 | 1.59 | 0.821 | 1.15x | 0.18x | 0.03x | 761x | 1.94x |
| queens | micro | 0.558 | 0.342 | 0.019 | 242.0 | 1.06 | 0.651 | 0.86x | 0.53x | 0.03x | 372x | 1.63x |
| towers | micro | 1.40 | 0.711 | 0.028 | 59.9 | 2.31 | 1.12 | 1.25x | 0.63x | 0.02x | 53.3x | 2.06x |
| bounce | micro | 0.266 | 0.798 | 0.024 | 29.3 | 0.888 | 0.552 | 0.48x | 1.45x | 0.04x | 53.2x | 1.61x |
| list | micro | 0.949 | 1.09 | 0.022 | 3.69 | 0.927 | 0.499 | 1.90x | 2.19x | 0.04x | 7.40x | 1.86x |
| storage | micro | 0.882 | 0.665 | 0.090 | 26.1 | 2.17 | 0.643 | 1.37x | 1.03x | 0.14x | 40.6x | 3.38x |
| mandelbrot | compute | 48.6 | 48.7 | 31.1 | 445.4 | 883.9 | 31.6 | 1.54x | 1.54x | 0.98x | 14.1x | 28.0x |
| nbody | compute | 33.4 | 21.1 | 1.52 | 5.70s | 161.3 | 6.08 | 5.50x | 3.48x | 0.25x | 938x | 26.5x |
| richards | macro | 2.87s | 285.5 | --- | 7.67s | 194.0 | 47.6 | 60.2x | 6.00x | --- | 161x | 4.08x |
| json | macro | 8.21 | 2.85 | --- | 265.5 | 11.3 | 3.36 | 2.45x | 0.85x | --- | 79.1x | 3.36x |
| deltablue | macro | 106.3 | 108.9 | --- | 2.67s | 100.8 | 12.2 | 8.73x | 8.94x | --- | 219x | 8.28x |
| havlak | macro | 59.9 | 59.7 | --- | 107.29s | 3.36s | 107.3 | 0.56x | 0.56x | --- | 1000x | 31.3x |
| cd | macro | 918.0 | 269.3 | --- | 28.51s | 991.9 | 36.5 | 25.1x | 7.37x | --- | 780x | 27.1x |

## BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 11.8 | 5.38 | 3.04 | 158.0 | 23.8 | 4.10 | 2.87x | 1.31x | 0.74x | 38.5x | 5.80x |
| fannkuch | permutation | 0.339 | 1.69 | 0.150 | 47.3 | 7.31 | 4.12 | 0.08x | 0.41x | 0.04x | 11.5x | 1.77x |
| fasta | generation | 0.769 | 0.898 | 0.250 | 26.2 | 8.80 | 6.02 | 0.13x | 0.15x | 0.04x | 4.35x | 1.46x |
| knucleotide | hashing | 4.61 | 5.07 | 0.287 | 167.4 | --- | 5.02 | 0.92x | 1.01x | 0.06x | 33.4x | --- |
| pidigits | bignum | 0.319 | 0.309 | --- | 0.433 | 0.131 | 1.97 | 0.16x | 0.16x | --- | 0.22x | 0.07x |
| regexredux | regex | 1.30 | 1.30 | 1.15 | 49.0 | --- | 2.41 | 0.54x | 0.54x | 0.48x | 20.3x | --- |
| revcomp | string | 1.44 | 1.21 | 0.379 | 307.2 | --- | 3.42 | 0.42x | 0.35x | 0.11x | 89.8x | --- |
| spectralnorm | numeric | 45.9 | 2.06 | 0.360 | 80.1 | 64.5 | 2.67 | 17.2x | 0.77x | 0.13x | 30.1x | 24.2x |

## KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 306.4 | 411.2 | 29.2 | 5.34s | 899.4 | 34.0 | 9.01x | 12.1x | 0.86x | 157x | 26.4x |
| matmul | numeric | 13.1 | 11.7 | 6.13 | 1.38s | 548.2 | 15.7 | 0.83x | 0.74x | 0.39x | 87.7x | 34.8x |
| primes | numeric | 66.3 | 3.47 | 1.70 | 4.61s | 96.1 | 4.53 | 14.6x | 0.77x | 0.38x | 1017x | 21.2x |
| base64 | string | 52.2 | 48.3 | 0.585 | 954.1 | 160.4 | 17.7 | 2.94x | 2.73x | 0.03x | 53.8x | 9.04x |
| levenshtein | string | 36.3 | 7.67 | 0.921 | 587.9 | 55.3 | 4.12 | 8.79x | 1.86x | 0.22x | 143x | 13.4x |
| json_gen | data | 21.6 | 22.4 | 1.57 | 56.9 | 20.2 | 6.52 | 3.32x | 3.44x | 0.24x | 8.74x | 3.10x |
| collatz | numeric | 434.7 | 417.4 | 230.1 | 2.24s | 6.48s | 1.49s | 0.29x | 0.28x | 0.15x | 1.50x | 4.35x |

## LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 481.3 | 192.0 | 61.0 | 21.38s | 3.07s | 108.0 | 4.46x | 1.78x | 0.56x | 198x | 28.5x |
| array1 | array | 1.20 | 1.22 | 0.400 | 117.6 | 50.0 | 2.62 | 0.46x | 0.47x | 0.15x | 44.8x | 19.1x |
| deriv | symbolic | 51.2 | 13.4 | --- | 541.0 | 82.1 | 7.60 | 6.74x | 1.76x | --- | 71.2x | 10.8x |
| diviter | iterative | 606.2 | 541.2 | 345.2 | 15.38s | 31.81s | 479.2 | 1.26x | 1.13x | 0.72x | 32.1x | 66.4x |
| divrec | recursive | 15.8 | 2.03 | 4.93 | 30.3 | 36.4 | 7.72 | 2.05x | 0.26x | 0.64x | 3.92x | 4.72x |
| gcbench | allocation | 278.3 | 161.5 | 71.6 | 3.91s | 559.3 | 24.3 | 11.4x | 6.64x | 2.95x | 161x | 23.0x |
| paraffins | combinat | 1.97 | 0.916 | 0.051 | 4.70 | 2.56 | 1.01 | 1.95x | 0.91x | 0.05x | 4.66x | 2.54x |
| pnpoly | numeric | 16.4 | 16.8 | 1.99 | 119.6 | 206.1 | 6.19 | 2.66x | 2.72x | 0.32x | 19.3x | 33.3x |
| puzzle | search | 9.82 | 4.62 | 1.29 | 6.46s | 30.2 | 3.34 | 2.94x | 1.38x | 0.38x | 1933x | 9.03x |
| quicksort | sorting | 10.3 | 5.39 | 0.201 | 489.5 | 19.5 | 1.67 | 6.16x | 3.23x | 0.12x | 293x | 11.7x |
| ray | numeric | 10.8 | 0.890 | 0.174 | 21.4 | 14.4 | 3.67 | 2.95x | 0.24x | 0.05x | 5.82x | 3.93x |

## JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 20.4 | 14.2 | --- | 4.60s | --- | 61.1 | 0.33x | 0.23x | --- | 75.3x | --- |
| navier_stokes | numeric | 1.62s | 323.6 | --- | 1.80s | 135.4 | 29.9 | 54.1x | 10.8x | --- | 60.4x | 4.54x |
| splay | data | 270.2 | 540.6 | --- | 2.02s | 272.6 | 36.2 | 7.47x | 14.9x | --- | 55.8x | 7.53x |
| hashmap | data | 190.3 | 84.6 | 3.65 | 6.49s | 320.6 | 15.6 | 12.2x | 5.43x | 0.23x | 416x | 20.6x |
| crypto_sha1 | crypto | 68.3 | 29.2 | --- | 1.07s | 221.7 | 8.90 | 7.67x | 3.28x | --- | 121x | 24.9x |
| raytrace3d | 3d | 302.9 | 60.3 | --- | 1.11s | --- | 18.7 | 16.2x | 3.23x | --- | 59.3x | --- |

## Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 1.45s | 480.4 | 13.4 | 6.53s | 925.7 | 93.3 | 15.5x | 5.15x | 0.14x | 70.0x | 9.92x |
| microdiff | data-diff | 1.18 | 1.18 | 0.022 | 9.88s | 138.8 | 39.2 | 0.03x | 0.03x | 0.001x | 252x | 3.55x |
| hyphen | hyphenation | 3.30 | 4.74 | 0.119 | 582.6 | 62.3 | 19.7 | 0.17x | 0.24x | 0.006x | 29.6x | 3.16x |
