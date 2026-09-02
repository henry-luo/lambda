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
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Part 1 — Execution time (self-reported)

Each engine's own `__TIMING__` figure: the timed workload only, with startup and compilation outside the measured region. This is the historical series, comparable back through Result18, and the MIR columns pin `LAMBDA_TIER=jit`.

### Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 9 | 10 | 0 | 10 | 10 | 10 | 0.38x | 0.39x | --- | 12.1x | 6.48x |
| AWFY | 14 | 8 | 10 | 0 | 14 | 14 | 14 | 0.78x | 0.70x | --- | 40.0x | 5.22x |
| BENG | 8 | 8 | 8 | 0 | 8 | 8 | 8 | 0.52x | 0.36x | --- | 12.3x | 1.69x |
| KOSTYA | 7 | 6 | 7 | 0 | 7 | 7 | 7 | 2.05x | 1.58x | --- | 57.2x | 11.9x |
| LARCENY | 11 | 10 | 11 | 0 | 11 | 11 | 11 | 2.13x | 0.91x | --- | 33.8x | 13.3x |
| JetStream | 6 | 4 | 4 | 0 | 6 | 6 | 6 | 5.76x | 3.17x | --- | 77.1x | 12.0x |
| Text | 3 | 3 | 3 | 0 | 3 | 3 | 3 | 0.30x | 0.22x | --- | 66.6x | 9.37x |
| **Overall** | 59 | 48 | 53 | 0 | 59 | 59 | 59 | 0.99x | 0.70x | --- | 30.8x | 6.84x |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

### Notable Results

- Missing timings: **76** cells
- C2MIR missing: r7rs/fib (toolchain_missing), r7rs/fibfp (toolchain_missing), r7rs/tak (toolchain_missing), r7rs/cpstak (toolchain_missing), r7rs/sum (toolchain_missing), r7rs/sumfp (toolchain_missing), r7rs/nqueens (toolchain_missing), r7rs/fft (toolchain_missing), r7rs/mbrot (toolchain_missing), r7rs/ack (toolchain_missing), awfy/sieve (toolchain_missing), awfy/permute (toolchain_missing), awfy/queens (toolchain_missing), awfy/towers (toolchain_missing), awfy/bounce (toolchain_missing), awfy/list (toolchain_missing), awfy/storage (toolchain_missing), awfy/mandelbrot (toolchain_missing), awfy/nbody (toolchain_missing), awfy/richards (toolchain_missing), awfy/json (toolchain_missing), awfy/deltablue (toolchain_missing), awfy/havlak (toolchain_missing), awfy/cd (toolchain_missing), beng/binarytrees (toolchain_missing), beng/fannkuch (toolchain_missing), beng/fasta (toolchain_missing), beng/knucleotide (toolchain_missing), beng/pidigits (toolchain_missing), beng/regexredux (toolchain_missing), beng/revcomp (toolchain_missing), beng/spectralnorm (toolchain_missing), kostya/brainfuck (toolchain_missing), kostya/matmul (toolchain_missing), kostya/primes (toolchain_missing), kostya/base64 (toolchain_missing), kostya/levenshtein (toolchain_missing), kostya/json_gen (toolchain_missing), kostya/collatz (toolchain_missing), larceny/triangl (toolchain_missing), larceny/array1 (toolchain_missing), larceny/deriv (toolchain_missing), larceny/diviter (toolchain_missing), larceny/divrec (toolchain_missing), larceny/gcbench (toolchain_missing), larceny/paraffins (toolchain_missing), larceny/pnpoly (toolchain_missing), larceny/puzzle (toolchain_missing), larceny/quicksort (toolchain_missing), larceny/ray (toolchain_missing), jetstream/cube3d (toolchain_missing), jetstream/navier_stokes (toolchain_missing), jetstream/splay (toolchain_missing), jetstream/hashmap (toolchain_missing), jetstream/crypto_sha1 (toolchain_missing), jetstream/raytrace3d (toolchain_missing), text/fast_diff (toolchain_missing), text/microdiff (toolchain_missing), text/hyphen (toolchain_missing)
- MIR (untyped) missing: r7rs/mbrot (wrong_output), awfy/sieve (wrong_output), awfy/nbody (wrong_output), awfy/richards (wrong_output), awfy/json (wrong_output), awfy/havlak (wrong_output), awfy/cd (wrong_output), kostya/primes (wrong_output), larceny/quicksort (wrong_output), jetstream/splay (wrong_output), jetstream/hashmap (wrong_output)
- MIR (typed) missing: awfy/nbody (wrong_output), awfy/json (wrong_output), awfy/havlak (wrong_output), awfy/cd (wrong_output), jetstream/splay (wrong_output), jetstream/hashmap (wrong_output)

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
| fib | recursive | 1.31 | 1.27 | --- | 40.9 | 18.6 | 1.80 | 0.73x | 0.70x | --- | 22.8x | 10.4x |
| fibfp | recursive | 2.08 | 1.19 | --- | 40.7 | 18.7 | 1.79 | 1.17x | 0.67x | --- | 22.8x | 10.5x |
| tak | recursive | 0.126 | 0.158 | --- | 2.62 | 2.77 | 0.792 | 0.16x | 0.20x | --- | 3.30x | 3.50x |
| cpstak | closure | 0.246 | 0.332 | --- | 5.26 | 5.58 | 0.989 | 0.25x | 0.34x | --- | 5.32x | 5.64x |
| sum | iterative | 0.824 | 0.824 | --- | 26.5 | 31.1 | 1.23 | 0.67x | 0.67x | --- | 21.5x | 25.3x |
| sumfp | iterative | 0.069 | 0.068 | --- | 2.58 | 3.62 | 0.860 | 0.08x | 0.08x | --- | 3.00x | 4.21x |
| nqueens | backtrack | 1.64 | 1.16 | --- | 39.9 | 7.94 | 1.77 | 0.92x | 0.65x | --- | 22.5x | 4.48x |
| fft | numeric | 0.202 | 0.306 | --- | 53.4 | 2.75 | 1.58 | 0.13x | 0.19x | --- | 33.8x | 1.74x |
| mbrot | numeric | --- | 0.685 | --- | 16.0 | 17.7 | 1.82 | --- | 0.38x | --- | 8.79x | 9.69x |
| ack | recursive | 10.7 | 14.3 | --- | 220.4 | 101.5 | 13.3 | 0.81x | 1.08x | --- | 16.5x | 7.62x |

### AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | --- | 0.049 | --- | 10.2 | 0.605 | 0.390 | --- | 0.13x | --- | 26.3x | 1.55x |
| permute | micro | 0.763 | 0.131 | --- | 10.8 | 1.55 | 0.812 | 0.94x | 0.16x | --- | 13.3x | 1.91x |
| queens | micro | 0.032 | 0.310 | --- | 6.65 | 1.05 | 0.647 | 0.05x | 0.48x | --- | 10.3x | 1.62x |
| towers | micro | 1.18 | 0.427 | --- | 30.7 | 2.24 | 1.13 | 1.05x | 0.38x | --- | 27.2x | 1.99x |
| bounce | micro | 0.067 | 0.108 | --- | 7.30 | 0.869 | 0.552 | 0.12x | 0.20x | --- | 13.2x | 1.57x |
| list | micro | 0.773 | 0.211 | --- | 3.25 | 0.918 | 0.483 | 1.60x | 0.44x | --- | 6.73x | 1.90x |
| storage | micro | 0.745 | 0.551 | --- | 7.15 | 2.07 | 0.638 | 1.17x | 0.86x | --- | 11.2x | 3.25x |
| mandelbrot | compute | 38.2 | 38.3 | --- | 439.5 | 869.5 | 31.2 | 1.22x | 1.23x | --- | 14.1x | 27.8x |
| nbody | compute | --- | --- | --- | 742.8 | 159.5 | 5.32 | --- | --- | --- | 140x | 29.9x |
| richards | macro | --- | 438.6 | --- | 1.76s | 197.0 | 46.8 | --- | 9.37x | --- | 37.7x | 4.21x |
| json | macro | --- | --- | --- | 96.8 | 10.7 | 2.60 | --- | --- | --- | 37.2x | 4.12x |
| deltablue | macro | 123.7 | 117.7 | --- | 1.56s | 108.5 | 12.4 | 9.99x | 9.50x | --- | 126x | 8.76x |
| havlak | macro | --- | --- | --- | 93.46s | 3.29s | 93.4 | --- | --- | --- | 1001x | 35.2x |
| cd | macro | --- | --- | --- | 28.12s | 962.7 | 36.0 | --- | --- | --- | 781x | 26.7x |

### BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 9.14 | 4.80 | --- | 162.7 | 23.6 | 4.06 | 2.25x | 1.18x | --- | 40.1x | 5.81x |
| fannkuch | permutation | 0.329 | 0.396 | --- | 61.1 | 7.26 | 3.98 | 0.08x | 0.10x | --- | 15.3x | 1.82x |
| fasta | generation | 0.774 | 0.915 | --- | 27.3 | 8.85 | 6.27 | 0.12x | 0.15x | --- | 4.36x | 1.41x |
| knucleotide | hashing | 4.36 | 4.72 | --- | 162.1 | 7.72 | 4.89 | 0.89x | 0.96x | --- | 33.1x | 1.58x |
| pidigits | bignum | 0.300 | 0.300 | --- | 0.428 | 0.135 | 1.99 | 0.15x | 0.15x | --- | 0.21x | 0.07x |
| regexredux | regex | 1.28 | 1.28 | --- | 52.3 | 5.58 | 2.42 | 0.53x | 0.53x | --- | 21.6x | 2.30x |
| revcomp | string | 1.25 | 1.18 | --- | 34.6 | 2.54 | 3.38 | 0.37x | 0.35x | --- | 10.2x | 0.75x |
| spectralnorm | numeric | 24.0 | 1.65 | --- | 322.6 | 64.3 | 2.61 | 9.20x | 0.63x | --- | 124x | 24.6x |

### KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 311.5 | 343.6 | --- | 3.77s | 885.4 | 33.8 | 9.21x | 10.2x | --- | 112x | 26.2x |
| matmul | numeric | 22.8 | 28.5 | --- | 1.27s | 539.5 | 15.4 | 1.48x | 1.85x | --- | 82.6x | 35.0x |
| primes | numeric | --- | 5.58 | --- | 3.89s | 95.0 | 4.42 | --- | 1.26x | --- | 879x | 21.5x |
| base64 | string | 11.5 | 11.5 | --- | 873.5 | 159.0 | 17.4 | 0.66x | 0.66x | --- | 50.2x | 9.14x |
| levenshtein | string | 35.5 | 6.49 | --- | 449.2 | 54.2 | 3.98 | 8.92x | 1.63x | --- | 113x | 13.6x |
| json_gen | data | 23.1 | 24.8 | --- | 67.0 | 19.9 | 6.29 | 3.68x | 3.94x | --- | 10.6x | 3.16x |
| collatz | numeric | 358.4 | 343.5 | --- | 5.80s | 6.24s | 1.42s | 0.25x | 0.24x | --- | 4.08x | 4.39x |

### LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 507.5 | 210.3 | --- | 11.31s | 2.19s | 66.6 | 7.62x | 3.16x | --- | 170x | 32.9x |
| array1 | array | 0.808 | 0.812 | --- | 79.4 | 36.0 | 1.89 | 0.43x | 0.43x | --- | 42.0x | 19.0x |
| deriv | symbolic | 30.1 | 12.7 | --- | 341.0 | 59.0 | 3.67 | 8.21x | 3.46x | --- | 93.0x | 16.1x |
| diviter | iterative | 263.3 | 267.2 | --- | 11.82s | 27.00s | 470.1 | 0.56x | 0.57x | --- | 25.1x | 57.4x |
| divrec | recursive | 15.2 | 2.00 | --- | 45.0 | 36.8 | 7.60 | 2.00x | 0.26x | --- | 5.93x | 4.84x |
| gcbench | allocation | 210.3 | 257.8 | --- | 4.02s | 548.1 | 23.5 | 8.96x | 11.0x | --- | 171x | 23.3x |
| paraffins | combinat | 0.323 | 0.281 | --- | 4.15 | 2.55 | 0.995 | 0.32x | 0.28x | --- | 4.17x | 2.57x |
| pnpoly | numeric | 12.9 | 14.8 | --- | 122.1 | 201.9 | 5.94 | 2.17x | 2.49x | --- | 20.6x | 34.0x |
| puzzle | search | 13.1 | 3.98 | --- | 110.8 | 29.3 | 3.29 | 3.98x | 1.21x | --- | 33.7x | 8.90x |
| quicksort | sorting | --- | 1.08 | --- | 422.6 | 19.2 | 1.65 | --- | 0.65x | --- | 256x | 11.6x |
| ray | numeric | 8.85 | 0.297 | --- | 18.2 | 13.7 | 3.51 | 2.52x | 0.08x | --- | 5.17x | 3.91x |

### JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 11.4 | 14.1 | --- | 756.1 | 217.7 | 17.7 | 0.64x | 0.80x | --- | 42.8x | 12.3x |
| navier_stokes | numeric | 392.8 | 156.8 | --- | 1.05s | 98.6 | 14.1 | 27.9x | 11.1x | --- | 74.7x | 7.01x |
| splay | data | --- | --- | --- | 1.15s | 146.7 | 19.3 | --- | --- | --- | 59.5x | 7.59x |
| hashmap | data | --- | --- | --- | 5.55s | 318.4 | 15.6 | --- | --- | --- | 356x | 20.5x |
| crypto_sha1 | crypto | 137.4 | 101.0 | --- | 627.9 | 220.1 | 8.74 | 15.7x | 11.6x | --- | 71.8x | 25.2x |
| raytrace3d | 3d | 72.1 | 18.2 | --- | 802.4 | 162.9 | 18.6 | 3.89x | 0.98x | --- | 43.2x | 8.77x |

### Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 258.8 | 152.0 | --- | 2.61s | 635.5 | 39.5 | 6.55x | 3.85x | --- | 66.1x | 16.1x |
| microdiff | data-diff | 0.157 | 0.158 | --- | 1.30s | 108.6 | 16.3 | 0.010x | 0.010x | --- | 79.7x | 6.66x |
| hyphen | hyphenation | 2.78 | 2.03 | --- | 378.3 | 51.7 | 6.73 | 0.41x | 0.30x | --- | 56.2x | 7.68x |

---

## Part 2 — End-to-end time (wall clock, auto tier)

Wall clock from process invocation to exit, so **every engine pays its own startup and compilation inside the number**. The MIR columns use the shipped auto tier -- no `LAMBDA_TIER` override -- which is what `lambda.exe run script.ls` actually does.

This set exists because the two questions are different. Part 1 asks how fast the compiled workload runs; part 2 asks how long it takes to run the script. Timing the auto tier under part 1's rules would charge Lambda for JIT compilation performed *inside* the measured region while crediting Node.js with a post-warmup figure -- comparing two different things. Here the accounting is the same for everyone.

Same processes, where possible: the reference engines report their wall and `__TIMING__` figures from the *same* run, so parts 1 and 2 are two readings of one launch. Only the MIR columns are re-run, because part 1 pins the JIT and part 2 must use the auto tier.

⚠ Short workloads are dominated by fixed process startup here, so a row whose part-1 time is a fraction of a millisecond says more about executable launch cost than about the language. Read part 2 by the longer rows.

### Summary

| Suite | Total | Timed MIR (untyped, auto) | Timed MIR (typed, auto) | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped, auto)/Node geo | MIR (typed, auto)/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 9 | 10 | 10 | 10 | 10 | 2.33x | 0.87x | 1.56x | 0.42x |
| AWFY | 14 | 9 | 10 | 14 | 14 | 14 | 1.24x | 2.19x | 7.68x | 0.81x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 0.85x | 0.78x | 2.64x | 0.33x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 15.9x | 12.0x | 14.4x | 2.91x |
| LARCENY | 11 | 10 | 11 | 11 | 11 | 11 | 8.55x | 3.11x | 6.56x | 1.94x |
| JetStream | 6 | 4 | 4 | 6 | 6 | 6 | 23.3x | 25.3x | 23.6x | 3.67x |
| Text | 3 | 3 | 3 | 3 | 3 | 3 | 10.2x | 2.79x | 28.4x | 2.67x |
| **Overall** | 59 | 50 | 53 | 59 | 59 | 59 | 3.95x | 2.59x | 6.36x | 1.09x |

> Ratio < 1.0 means the engine finished the whole run faster than Node.js.

### R7RS

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 217.6 | 16.6 | 75.2 | 24.6 | 45.2 | 4.81x | 0.37x | 1.66x | 0.54x |
| fibfp | recursive | 202.4 | 16.5 | 75.3 | 24.7 | 45.0 | 4.50x | 0.37x | 1.67x | 0.55x |
| tak | recursive | 53.3 | 60.9 | 40.2 | 8.43 | 44.4 | 1.20x | 1.37x | 0.90x | 0.19x |
| cpstak | closure | 92.7 | 106.1 | 42.7 | 11.5 | 44.5 | 2.09x | 2.39x | 0.96x | 0.26x |
| sum | iterative | 155.0 | 25.1 | 60.9 | 37.1 | 45.0 | 3.45x | 0.56x | 1.36x | 0.83x |
| sumfp | iterative | 26.1 | 29.8 | 36.8 | 9.62 | 44.2 | 0.59x | 0.67x | 0.83x | 0.22x |
| nqueens | backtrack | 53.3 | 61.1 | 120.6 | 14.0 | 45.2 | 1.18x | 1.35x | 2.67x | 0.31x |
| fft | numeric | 29.8 | 32.6 | 97.6 | 8.60 | 44.9 | 0.66x | 0.73x | 2.18x | 0.19x |
| mbrot | numeric | --- | 132.9 | 54.6 | 23.7 | 45.3 | --- | 2.93x | 1.21x | 0.52x |
| ack | recursive | 1.32s | 29.9 | 255.0 | 107.9 | 56.6 | 23.3x | 0.53x | 4.50x | 1.91x |

### AWFY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 16.2 | 17.1 | 50.5 | 6.61 | 44.6 | 0.36x | 0.38x | 1.13x | 0.15x |
| permute | micro | 31.5 | 35.1 | 53.6 | 7.19 | 44.8 | 0.70x | 0.78x | 1.20x | 0.16x |
| queens | micro | 19.5 | 29.3 | 52.5 | 6.72 | 45.0 | 0.43x | 0.65x | 1.17x | 0.15x |
| towers | micro | 22.2 | 59.5 | 78.8 | 8.11 | 45.4 | 0.49x | 1.31x | 1.74x | 0.18x |
| bounce | micro | 18.2 | 19.0 | 132.1 | 7.40 | 45.0 | 0.40x | 0.42x | 2.94x | 0.16x |
| list | micro | 22.4 | 25.3 | 47.1 | 6.50 | 44.8 | 0.50x | 0.56x | 1.05x | 0.15x |
| storage | micro | 23.2 | 25.4 | 123.2 | 8.56 | 45.3 | 0.51x | 0.56x | 2.72x | 0.19x |
| mandelbrot | compute | 3.87s | 4.72s | 487.5 | 876.4 | 75.5 | 51.3x | 62.5x | 6.46x | 11.6x |
| nbody | compute | --- | --- | 817.7 | 166.2 | 49.3 | --- | --- | 16.6x | 3.37x |
| richards | macro | --- | 4.30s | 1.88s | 203.9 | 91.0 | --- | 47.3x | 20.7x | 2.24x |
| json | macro | --- | --- | 303.9 | 18.7 | 47.7 | --- | --- | 6.37x | 0.39x |
| deltablue | macro | 1.45s | 1.49s | 1.84s | 116.4 | 58.8 | 24.7x | 25.4x | 31.2x | 1.98x |
| havlak | macro | --- | --- | 95.03s | 3.30s | 139.9 | --- | --- | 679x | 23.6x |
| cd | macro | --- | --- | 28.79s | 969.3 | 80.5 | --- | --- | 358x | 12.0x |

### BENG

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 117.2 | 77.2 | 209.0 | 29.5 | 45.5 | 2.58x | 1.70x | 4.60x | 0.65x |
| fannkuch | permutation | 53.6 | 64.2 | 102.0 | 13.1 | 45.5 | 1.18x | 1.41x | 2.24x | 0.29x |
| fasta | generation | 23.3 | 24.8 | 74.9 | 14.6 | 48.2 | 0.48x | 0.51x | 1.55x | 0.30x |
| knucleotide | hashing | 33.5 | 35.6 | 208.8 | 13.6 | 46.9 | 0.71x | 0.76x | 4.45x | 0.29x |
| pidigits | bignum | 14.1 | 15.3 | 44.4 | 5.49 | 43.0 | 0.33x | 0.35x | 1.03x | 0.13x |
| regexredux | regex | 14.7 | 15.0 | 98.6 | 11.3 | 44.3 | 0.33x | 0.34x | 2.23x | 0.25x |
| revcomp | string | 17.1 | 17.5 | 81.9 | 8.06 | 44.7 | 0.38x | 0.39x | 1.83x | 0.18x |
| spectralnorm | numeric | 300.4 | 140.0 | 365.9 | 70.1 | 45.9 | 6.54x | 3.05x | 7.97x | 1.53x |

### KOSTYA

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 6.20s | 6.74s | 3.81s | 892.0 | 77.8 | 79.7x | 86.6x | 49.0x | 11.5x |
| matmul | numeric | 2.23s | 2.53s | 1.32s | 547.1 | 58.5 | 38.2x | 43.3x | 22.5x | 9.34x |
| primes | numeric | 427.3 | 558.5 | 3.95s | 101.3 | 47.5 | 8.99x | 11.8x | 83.1x | 2.13x |
| base64 | string | 459.1 | 487.8 | 916.1 | 165.3 | 60.7 | 7.57x | 8.04x | 15.1x | 2.72x |
| levenshtein | string | 786.0 | 369.2 | 493.9 | 60.2 | 47.1 | 16.7x | 7.83x | 10.5x | 1.28x |
| json_gen | data | 145.6 | 96.6 | 109.0 | 25.8 | 49.5 | 2.94x | 1.95x | 2.20x | 0.52x |
| collatz | numeric | 36.37s | 9.91s | 5.84s | 6.24s | 1.46s | 24.8x | 6.77x | 3.99x | 4.26x |

### LARCENY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 535.7 | 237.3 | 11.38s | 2.20s | 110.0 | 4.87x | 2.16x | 103x | 20.0x |
| array1 | array | 168.4 | 210.1 | 115.0 | 41.8 | 44.7 | 3.76x | 4.69x | 2.57x | 0.93x |
| deriv | symbolic | 226.6 | 534.8 | 390.9 | 65.2 | 47.2 | 4.80x | 11.3x | 8.28x | 1.38x |
| diviter | iterative | 107.25s | 15.0 | 11.86s | 27.01s | 513.6 | 209x | 0.03x | 23.1x | 52.6x |
| divrec | recursive | 451.8 | 19.5 | 82.0 | 42.7 | 50.7 | 8.91x | 0.39x | 1.62x | 0.84x |
| gcbench | allocation | 2.84s | 7.69s | 4.21s | 559.9 | 65.7 | 43.3x | 117x | 64.2x | 8.53x |
| paraffins | combinat | 32.7 | 36.9 | 53.6 | 8.36 | 43.7 | 0.75x | 0.84x | 1.23x | 0.19x |
| pnpoly | numeric | 1.38s | 1.56s | 161.5 | 208.0 | 48.9 | 28.3x | 31.9x | 3.30x | 4.26x |
| puzzle | search | 214.6 | 252.3 | 150.3 | 35.0 | 46.1 | 4.66x | 5.48x | 3.26x | 0.76x |
| quicksort | sorting | --- | 154.4 | 464.5 | 25.4 | 44.7 | --- | 3.46x | 10.4x | 0.57x |
| ray | numeric | 138.6 | 157.3 | 61.7 | 19.4 | 46.3 | 2.99x | 3.40x | 1.33x | 0.42x |

### JetStream

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 341.0 | 365.8 | 868.6 | 224.9 | 63.1 | 5.40x | 5.79x | 13.8x | 3.56x |
| navier_stokes | numeric | 9.36s | 10.12s | 1.35s | 109.7 | 60.9 | 154x | 166x | 22.1x | 1.80x |
| splay | data | --- | --- | 2.95s | 561.2 | 92.3 | --- | --- | 32.0x | 6.08x |
| hashmap | data | --- | --- | 5.68s | 325.9 | 60.5 | --- | --- | 94.0x | 5.39x |
| crypto_sha1 | crypto | 2.05s | 2.36s | 677.4 | 226.8 | 52.8 | 38.9x | 44.7x | 12.8x | 4.30x |
| raytrace3d | 3d | 577.6 | 593.7 | 922.4 | 170.0 | 62.7 | 9.21x | 9.47x | 14.7x | 2.71x |

### Text

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 6.33s | 6.34s | 2.78s | 642.8 | 83.6 | 75.8x | 75.8x | 33.2x | 7.69x |
| microdiff | data-diff | 24.9 | 24.7 | 1.38s | 114.7 | 59.7 | 0.42x | 0.41x | 23.2x | 1.92x |
| hyphen | hyphenation | 1.78s | 36.9 | 1.59s | 68.6 | 53.3 | 33.4x | 0.69x | 29.9x | 1.29x |

