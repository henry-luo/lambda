# Lambda Benchmark Results: Result30

- **Date:** 2026-08-16
- **Platform:** Darwin arm64
- **Lambda commit:** `f3589ea1566b79e46ee3b7fe57ce8e29d1399861`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v30-f3589ea156` (21,727,512 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 214.40s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 214.3s (batched 213.4s: sync 117.5s, async 95.9s; non-batched 0.9s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v30.json`
- **Separately measured:** C2MIR measured on 2026-08-19, 3 run(s) from `test/benchmark/run_c2mir_benchmarks.py`. Back-patched 12 rows whose native C ports did not exist during this session (awfy/cd, awfy/deltablue, awfy/havlak, awfy/json, awfy/richards, beng/pidigits, jetstream/crypto_sha1, jetstream/cube3d, jetstream/navier_stokes, jetstream/raytrace3d, jetstream/splay, larceny/deriv); all other C2MIR cells are this session's own. C2MIR measures native C ports through lambda/mir/c2m and does not depend on the Lambda binary; its cells are stable to within a few percent across the v18-v33 sessions.
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 9 | 10 | 0.80x | 0.47x | 0.19x | 11.5x | 6.37x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 2.58x | 1.29x | 0.10x | 46.6x | 5.29x |
| BENG | 8 | 8 | 8 | 8 | 8 | 5 | 8 | 0.59x | 0.45x | 0.10x | 12.0x | 1.90x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 3.10x | 1.63x | 0.23x | 53.9x | 11.9x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 3.05x | 1.29x | 0.33x | 32.7x | 13.3x |
| JetStream | 6 | 6 | 6 | 6 | 4 | 4 | 6 | 8.83x | 4.11x | 0.30x | 72.2x | 12.9x |
| Text | 3 | 3 | 3 | 3 | 3 | 3 | 3 | 0.92x | 0.72x | 0.02x | 70.4x | 9.33x |
| **Overall** | 59 | 59 | 59 | 59 | 57 | 53 | 59 | 1.96x | 1.06x | 0.16x | 30.2x | 7.37x |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 6.79x over 59 of 59 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| awfy/deltablue | 104.8 | 1.17 | 89.7x |
| kostya/base64 | 46.0 | 0.558 | 82.5x |
| text/microdiff | 0.952 | 0.019 | 50.0x |
| awfy/list | 1.10 | 0.025 | 44.1x |
| text/hyphen | 3.62 | 0.088 | 41.1x |
| text/fast_diff | 471.0 | 13.1 | 35.9x |
| awfy/bounce | 0.842 | 0.027 | 31.1x |
| awfy/havlak | 58.4 | 1.89 | 31.0x |
| larceny/quicksort | 5.44 | 0.197 | 27.6x |
| jetstream/raytrace3d | 59.5 | 2.21 | 27.0x |
| awfy/towers | 0.691 | 0.031 | 22.3x |
| jetstream/cube3d | 11.1 | 0.523 | 21.2x |

---

## Notable Results

- Missing timings: **8** cells
- QuickJS missing: r7rs/ack (exit_1), beng/knucleotide (exit_1), beng/regexredux (exit_1), beng/revcomp (exit_1), jetstream/cube3d (exit_1), jetstream/raytrace3d (exit_1)
- LambdaJS missing: jetstream/navier_stokes (timeout), jetstream/hashmap (timeout)

### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/havlak | 106.51s | 96.0 | 1110x |
| kostya/primes | 3.83s | 4.46 | 859x |
| awfy/cd | 28.47s | 35.4 | 804x |
| larceny/quicksort | 400.3 | 1.63 | 245x |
| awfy/nbody | 1.10s | 5.39 | 204x |
| larceny/gcbench | 3.91s | 23.3 | 168x |
| awfy/deltablue | 2.06s | 12.3 | 168x |
| larceny/triangl | 11.07s | 66.8 | 166x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.420 | 1.93 | 0.22x |

---

## R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.37 | 1.33 | 1.16 | 39.7 | 20.1 | 2.06 | 0.67x | 0.64x | 0.56x | 19.3x | 9.76x |
| fibfp | recursive | 4.46 | 3.21 | 1.29 | 39.2 | 20.6 | 1.88 | 2.36x | 1.70x | 0.69x | 20.8x | 10.9x |
| tak | recursive | 0.128 | 0.154 | 0.114 | 2.52 | 2.89 | 0.805 | 0.16x | 0.19x | 0.14x | 3.13x | 3.59x |
| cpstak | closure | 0.250 | 0.301 | 0.240 | 5.18 | 5.76 | 1.04 | 0.24x | 0.29x | 0.23x | 4.97x | 5.53x |
| sum | iterative | 0.844 | 0.857 | 0.279 | 27.0 | 33.7 | 1.22 | 0.69x | 0.70x | 0.23x | 22.1x | 27.6x |
| sumfp | iterative | 0.330 | 0.330 | 0.084 | 2.63 | 4.06 | 0.913 | 0.36x | 0.36x | 0.09x | 2.88x | 4.45x |
| nqueens | backtrack | 1.96 | 1.73 | 0.130 | 40.1 | 7.90 | 1.94 | 1.01x | 0.90x | 0.07x | 20.7x | 4.08x |
| fft | numeric | 2.57 | 0.238 | 0.027 | 58.6 | 2.94 | 1.72 | 1.50x | 0.14x | 0.02x | 34.1x | 1.71x |
| mbrot | numeric | 11.4 | 0.658 | 0.454 | 16.4 | 18.5 | 1.94 | 5.84x | 0.34x | 0.23x | 8.46x | 9.53x |
| ack | recursive | 11.3 | 11.4 | 11.9 | 227.6 | --- | 13.9 | 0.81x | 0.82x | 0.86x | 16.4x | --- |

## AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.579 | 0.054 | 0.018 | 11.0 | 0.633 | 0.410 | 1.41x | 0.13x | 0.04x | 27.0x | 1.54x |
| permute | micro | 0.963 | 0.156 | 0.026 | 12.5 | 1.55 | 0.835 | 1.15x | 0.19x | 0.03x | 15.0x | 1.86x |
| queens | micro | 0.567 | 0.345 | 0.020 | 7.21 | 1.16 | 0.665 | 0.85x | 0.52x | 0.03x | 10.8x | 1.74x |
| towers | micro | 1.38 | 0.691 | 0.031 | 62.8 | 2.32 | 1.14 | 1.21x | 0.61x | 0.03x | 55.3x | 2.04x |
| bounce | micro | 0.305 | 0.842 | 0.027 | 7.71 | 0.996 | 0.562 | 0.54x | 1.50x | 0.05x | 13.7x | 1.77x |
| list | micro | 1.02 | 1.10 | 0.025 | 3.48 | 0.921 | 0.508 | 2.01x | 2.16x | 0.05x | 6.85x | 1.81x |
| storage | micro | 0.858 | 0.619 | 0.105 | 7.68 | 2.50 | 0.652 | 1.32x | 0.95x | 0.16x | 11.8x | 3.83x |
| mandelbrot | compute | 52.9 | 52.5 | 31.8 | 486.6 | 937.3 | 35.4 | 1.49x | 1.48x | 0.90x | 13.7x | 26.5x |
| nbody | compute | 37.8 | 23.6 | 1.54 | 1.10s | 165.3 | 5.39 | 7.02x | 4.38x | 0.29x | 204x | 30.7x |
| richards | macro | 2.85s | 277.7 | 31.2 | 2.55s | 197.8 | 47.8 | 59.6x | 5.81x | 0.65x | 53.3x | 4.14x |
| json | macro | 8.03 | 2.74 | 0.278 | 99.7 | 11.1 | 2.65 | 3.03x | 1.03x | 0.10x | 37.6x | 4.18x |
| deltablue | macro | 102.1 | 104.8 | 1.17 | 2.06s | 100.6 | 12.3 | 8.31x | 8.53x | 0.10x | 168x | 8.19x |
| havlak | macro | 58.0 | 58.4 | 1.89 | 106.51s | 3.31s | 96.0 | 0.60x | 0.61x | 0.02x | 1110x | 34.5x |
| cd | macro | 881.2 | 259.2 | 15.6 | 28.47s | 959.7 | 35.4 | 24.9x | 7.32x | 0.44x | 804x | 27.1x |

## BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 11.0 | 4.78 | 3.03 | 159.7 | 23.5 | 4.22 | 2.60x | 1.13x | 0.72x | 37.9x | 5.56x |
| fannkuch | permutation | 0.333 | 1.77 | 0.149 | 60.0 | 7.12 | 4.03 | 0.08x | 0.44x | 0.04x | 14.9x | 1.77x |
| fasta | generation | 0.757 | 0.883 | 0.246 | 26.1 | 8.88 | 6.04 | 0.13x | 0.15x | 0.04x | 4.32x | 1.47x |
| knucleotide | hashing | 4.32 | 4.70 | 0.279 | 162.7 | --- | 4.87 | 0.89x | 0.96x | 0.06x | 33.4x | --- |
| pidigits | bignum | 0.304 | 0.305 | 0.047 | 0.420 | 0.131 | 1.93 | 0.16x | 0.16x | 0.02x | 0.22x | 0.07x |
| regexredux | regex | 1.27 | 1.28 | 1.13 | 51.5 | --- | 2.43 | 0.52x | 0.52x | 0.46x | 21.2x | --- |
| revcomp | string | 1.38 | 1.20 | 0.376 | 31.9 | --- | 3.39 | 0.41x | 0.35x | 0.11x | 9.41x | --- |
| spectralnorm | numeric | 44.4 | 2.01 | 0.354 | 317.8 | 65.2 | 2.58 | 17.2x | 0.78x | 0.14x | 123x | 25.2x |

## KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 302.7 | 383.1 | 28.7 | 3.69s | 860.4 | 33.8 | 8.95x | 11.3x | 0.85x | 109x | 25.5x |
| matmul | numeric | 12.8 | 11.5 | 6.09 | 1.25s | 539.3 | 15.3 | 0.84x | 0.75x | 0.40x | 81.7x | 35.1x |
| primes | numeric | 65.2 | 3.31 | 1.59 | 3.83s | 94.6 | 4.46 | 14.6x | 0.74x | 0.36x | 859x | 21.2x |
| base64 | string | 50.6 | 46.0 | 0.558 | 851.7 | 157.2 | 17.2 | 2.94x | 2.67x | 0.03x | 49.5x | 9.14x |
| levenshtein | string | 34.8 | 7.22 | 0.901 | 431.3 | 54.2 | 4.01 | 8.68x | 1.80x | 0.22x | 107x | 13.5x |
| json_gen | data | 20.8 | 21.7 | 1.51 | 52.5 | 19.9 | 6.21 | 3.35x | 3.49x | 0.24x | 8.44x | 3.19x |
| collatz | numeric | 422.1 | 408.1 | 225.6 | 5.43s | 6.24s | 1.42s | 0.30x | 0.29x | 0.16x | 3.82x | 4.40x |

## LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 473.1 | 187.7 | 60.2 | 11.07s | 2.18s | 66.8 | 7.08x | 2.81x | 0.90x | 166x | 32.7x |
| array1 | array | 1.10 | 0.820 | 0.323 | 77.4 | 35.8 | 1.91 | 0.57x | 0.43x | 0.17x | 40.5x | 18.7x |
| deriv | symbolic | 36.7 | 11.3 | 2.92 | 338.6 | 59.6 | 3.71 | 9.87x | 3.04x | 0.79x | 91.2x | 16.1x |
| diviter | iterative | 400.3 | 399.9 | 265.6 | 11.34s | 26.69s | 471.6 | 0.85x | 0.85x | 0.56x | 24.1x | 56.6x |
| divrec | recursive | 15.7 | 2.00 | 4.90 | 42.1 | 35.8 | 7.58 | 2.07x | 0.26x | 0.65x | 5.55x | 4.72x |
| gcbench | allocation | 255.3 | 159.6 | 70.5 | 3.91s | 547.3 | 23.3 | 11.0x | 6.86x | 3.03x | 168x | 23.5x |
| paraffins | combinat | 1.91 | 0.903 | 0.049 | 4.02 | 2.50 | 0.995 | 1.92x | 0.91x | 0.05x | 4.04x | 2.51x |
| pnpoly | numeric | 16.0 | 16.6 | 1.96 | 115.9 | 201.6 | 5.76 | 2.78x | 2.88x | 0.34x | 20.1x | 35.0x |
| puzzle | search | 9.24 | 4.67 | 1.30 | 108.8 | 29.3 | 3.30 | 2.80x | 1.42x | 0.39x | 33.0x | 8.87x |
| quicksort | sorting | 10.2 | 5.44 | 0.197 | 400.3 | 19.1 | 1.63 | 6.23x | 3.33x | 0.12x | 245x | 11.7x |
| ray | numeric | 10.5 | 0.846 | 0.171 | 17.7 | 13.8 | 3.50 | 2.99x | 0.24x | 0.05x | 5.07x | 3.94x |

## JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 12.0 | 11.1 | 0.523 | 2.23s | --- | 17.6 | 0.68x | 0.63x | 0.03x | 127x | --- |
| navier_stokes | numeric | 1.01s | 179.1 | 48.4 | --- | 98.8 | 14.1 | 71.5x | 12.7x | 3.43x | --- | 7.01x |
| splay | data | 151.3 | 280.8 | 20.4 | 1.09s | 146.9 | 19.1 | 7.93x | 14.7x | 1.07x | 57.2x | 7.69x |
| hashmap | data | 166.2 | 55.2 | 2.76 | --- | 315.1 | 15.2 | 10.9x | 3.64x | 0.18x | --- | 20.7x |
| crypto_sha1 | crypto | 63.8 | 30.7 | 2.70 | 598.7 | 218.5 | 8.82 | 7.24x | 3.48x | 0.31x | 67.9x | 24.8x |
| raytrace3d | 3d | 285.7 | 59.5 | 2.21 | 1.02s | --- | 18.5 | 15.5x | 3.22x | 0.12x | 55.0x | --- |

## Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 1.38s | 471.0 | 13.1 | 2.68s | 611.6 | 39.1 | 35.3x | 12.0x | 0.34x | 68.5x | 15.6x |
| microdiff | data-diff | 0.949 | 0.952 | 0.019 | 1.48s | 110.9 | 16.3 | 0.06x | 0.06x | 0.001x | 90.8x | 6.81x |
| hyphen | hyphenation | 2.60 | 3.62 | 0.088 | 379.2 | 51.6 | 6.75 | 0.38x | 0.54x | 0.01x | 56.2x | 7.64x |
