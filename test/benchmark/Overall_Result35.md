# Result35

- **Date:** 2026-08-26
- **Platform:** Darwin arm64
- **Lambda commit:** `ef0eab5721`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v35-ef0eab5721` (19,817,880 bytes)
- **Instrumentation check:** not_recorded
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run; suites run in order `r7rs -> awfy -> beng -> kostya -> larceny -> jetstream -> text` with a 10s idle gap between suites
- **Engines in this report:** MIR (untyped), MIR (typed), MIR (untyped, auto), MIR (typed, auto), C2MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v35.json`
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Execution tiers in this report

Four MIR columns, two tiers:

- **MIR (untyped)** / **MIR (typed)** pin `LAMBDA_TIER=jit`. This is what every
  report from Result18 onward has measured, so these are the columns to compare
  against the published series.
- **MIR (untyped, auto)** / **MIR (typed, auto)** use no tier override — what
  `lambda.exe run script.ls` actually does today, which since the
  interpreter-first default is a *different execution path*, not a
  configuration of the same one.

**The gap between them is the headline of this run: typed/Node goes 0.73x pinned
to 17.0x on auto, untyped 1.27x to 31.0x** — roughly 20-25x, and up to 470x on
individual rows (larceny/ray 0.30ms → 140ms; jetstream/hashmap 44.6ms → 15.6s).
A handful of rows invert, and one of those is a genuine interpreter win worth
keeping: larceny/diviter 249ms → 1.4ms, correct on both tiers.

⚠ **The runner does not diff benchmark output** — it records any process that
exits 0 and prints a `__TIMING__` line as `ok`. Every typed benchmark was
therefore re-run on the auto tier and its output checked by hand. Two cells
failed that check:

| Row | auto-tier cell | why |
|---|---|---|
| awfy/json | ~~`wrong_result`~~ → **fixed, re-measured 125.8 ms** | It printed `FAIL: not object` and exited 0 on auto and on explicit `LAMBDA_TIER=interp`, so the 0.058 ms the runner first recorded was the early-exit failure path, not the workload. **This was a real interpreter bug, since fixed**: T0 published typed-container writes through plain `pn` parameters to a *detached* copy while MIR wrote in place, so the parser's `p.cur = …` never reached the caller and the parse returned a number instead of the object. The cell now carries a genuine median-of-3. |
| awfy/cd | `timeout` | exceeded the 180s per-run timeout on the auto tier |

All other auto cells produced correct output. The json case is the argument for
this validation step existing: the bad cell did not look like a failure, it
looked like a **40x speedup**.

⚠ Regenerating this file with `gen_overall_result.py` drops this hand-written
section; re-add it, or the tier caveat silently disappears from the report.

## Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed MIR (untyped, auto) | Timed MIR (typed, auto) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | MIR (untyped, auto)/Node geo | MIR (typed, auto)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 9 | 10 | 0.48x | 0.38x | 46.9x | 10.5x | 0.19x | 11.9x | 6.44x |
| AWFY | 14 | 14 | 14 | 14 | 13 | 14 | 14 | 14 | 14 | 1.53x | 0.81x | 22.6x | 31.4x | 0.10x | 43.3x | 5.24x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 5 | 8 | 0.53x | 0.36x | 3.54x | 3.06x | 0.10x | 12.3x | 1.93x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 2.06x | 1.29x | 72.8x | 55.2x | 0.23x | 54.7x | 12.0x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 2.27x | 0.85x | 59.0x | 17.0x | 0.33x | 32.6x | 13.3x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 4 | 6 | 5.52x | 3.18x | 105x | 140x | 0.29x | 75.7x | 12.9x |
| Text | 3 | 3 | 3 | 3 | 3 | 3 | 3 | 3 | 3 | 0.29x | 0.22x | 12.6x | 7.10x | 0.02x | 66.5x | 9.39x |
| **Overall** | 59 | 59 | 59 | 59 | 58 | 59 | 59 | 53 | 59 | 1.27x | 0.73x | 31.0x | 19.4x | 0.16x | 30.9x | 7.40x |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 4.69x over 59 of 59 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| awfy/deltablue | 69.2 | 1.15 | 60.1x |
| text/hyphen | 2.03 | 0.087 | 23.2x |
| jetstream/cube3d | 11.3 | 0.512 | 22.0x |
| jetstream/raytrace3d | 46.8 | 2.16 | 21.6x |
| awfy/havlak | 38.5 | 1.84 | 20.9x |
| kostya/base64 | 11.3 | 0.558 | 20.2x |
| beng/knucleotide | 4.71 | 0.276 | 17.1x |
| jetstream/hashmap | 44.5 | 2.71 | 16.5x |
| awfy/towers | 0.416 | 0.027 | 15.4x |
| awfy/queens | 0.307 | 0.020 | 15.3x |
| kostya/json_gen | 21.4 | 1.50 | 14.2x |
| awfy/nbody | 20.2 | 1.48 | 13.6x |

---

## Notable Results

- Missing timings: **7** cells
- QuickJS missing: r7rs/ack (exit_1), beng/knucleotide (exit_1), beng/regexredux (exit_1), beng/revcomp (exit_1), jetstream/cube3d (exit_1), jetstream/raytrace3d (exit_1)
- MIR (typed, auto) missing: awfy/cd (timeout)

### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/havlak | 105.04s | 94.3 | 1114x |
| kostya/primes | 3.74s | 4.29 | 872x |
| awfy/cd | 27.54s | 35.3 | 780x |
| jetstream/hashmap | 4.90s | 15.1 | 326x |
| larceny/quicksort | 396.9 | 1.62 | 244x |
| awfy/nbody | 1.00s | 5.32 | 188x |
| awfy/deltablue | 2.00s | 11.7 | 172x |
| larceny/gcbench | 3.90s | 23.2 | 168x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.424 | 1.88 | 0.23x |

---

## R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.30 | 1.27 | 188.3 | 2.02 | 1.11 | 40.5 | 18.7 | 1.74 | 0.75x | 0.73x | 108x | 1.16x | 0.64x | 23.3x | 10.8x |
| fibfp | recursive | 2.05 | 1.19 | 175.9 | 2.02 | 1.12 | 40.5 | 18.8 | 1.76 | 1.16x | 0.68x | 100.0x | 1.15x | 0.64x | 23.0x | 10.7x |
| tak | recursive | 0.125 | 0.159 | 36.8 | 43.8 | 0.118 | 2.66 | 2.76 | 0.791 | 0.16x | 0.20x | 46.6x | 55.4x | 0.15x | 3.37x | 3.49x |
| cpstak | closure | 0.249 | 0.315 | 74.3 | 87.4 | 0.224 | 5.13 | 5.54 | 0.975 | 0.26x | 0.32x | 76.2x | 89.6x | 0.23x | 5.26x | 5.68x |
| sum | iterative | 0.831 | 0.824 | 139.1 | 10.0 | 0.271 | 24.2 | 31.0 | 1.19 | 0.70x | 0.69x | 117x | 8.41x | 0.23x | 20.3x | 26.1x |
| sumfp | iterative | 0.069 | 0.068 | 11.1 | 14.5 | 0.079 | 2.42 | 3.70 | 0.905 | 0.08x | 0.08x | 12.3x | 16.0x | 0.09x | 2.68x | 4.09x |
| nqueens | backtrack | 1.57 | 1.15 | 36.5 | 44.4 | 0.127 | 39.4 | 7.87 | 1.73 | 0.91x | 0.66x | 21.1x | 25.7x | 0.07x | 22.8x | 4.55x |
| fft | numeric | 0.121 | 0.253 | 14.8 | 17.3 | 0.026 | 54.9 | 2.80 | 1.59 | 0.08x | 0.16x | 9.34x | 10.9x | 0.02x | 34.6x | 1.76x |
| mbrot | numeric | 10.4 | 0.555 | 94.5 | 113.0 | 0.461 | 15.7 | 18.2 | 1.86 | 5.58x | 0.30x | 50.8x | 60.7x | 0.25x | 8.41x | 9.77x |
| ack | recursive | 12.0 | 14.3 | 1.26s | 14.7 | 11.8 | 219.2 | --- | 13.4 | 0.89x | 1.07x | 93.9x | 1.09x | 0.88x | 16.3x | --- |

## AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.040 | 0.034 | 2.97 | 2.73 | 0.018 | 10.5 | 0.592 | 0.375 | 0.11x | 0.09x | 7.91x | 7.27x | 0.05x | 28.1x | 1.58x |
| permute | micro | 0.568 | 0.130 | 14.3 | 19.3 | 0.027 | 11.3 | 1.52 | 0.813 | 0.70x | 0.16x | 17.5x | 23.8x | 0.03x | 13.9x | 1.87x |
| queens | micro | 0.383 | 0.307 | 0.355 | 14.1 | 0.020 | 7.14 | 1.09 | 0.661 | 0.58x | 0.46x | 0.54x | 21.3x | 0.03x | 10.8x | 1.64x |
| towers | micro | 0.990 | 0.416 | 0.918 | 40.2 | 0.027 | 31.4 | 2.27 | 1.09 | 0.90x | 0.38x | 0.84x | 36.7x | 0.02x | 28.7x | 2.07x |
| bounce | micro | 0.068 | 0.109 | 3.89 | 4.67 | 0.026 | 7.35 | 0.872 | 0.537 | 0.13x | 0.20x | 7.25x | 8.71x | 0.05x | 13.7x | 1.63x |
| list | micro | 0.731 | 0.211 | 8.05 | 10.3 | 0.025 | 3.33 | 0.914 | 0.487 | 1.50x | 0.43x | 16.5x | 21.1x | 0.05x | 6.84x | 1.88x |
| storage | micro | 0.732 | 0.537 | 7.78 | 10.5 | 0.091 | 7.52 | 2.17 | 0.628 | 1.17x | 0.86x | 12.4x | 16.7x | 0.15x | 12.0x | 3.46x |
| mandelbrot | compute | 38.2 | 38.2 | 3.86s | 4.68s | 30.7 | 443.5 | 870.4 | 31.1 | 1.23x | 1.23x | 124x | 150x | 0.98x | 14.2x | 28.0x |
| nbody | compute | 32.4 | 20.2 | 842.1 | 965.7 | 1.48 | 1.00s | 159.1 | 5.32 | 6.10x | 3.81x | 158x | 182x | 0.28x | 188x | 29.9x |
| richards | macro | 2.06s | 207.5 | 28.49s | 2.91s | 29.5 | 1.84s | 190.5 | 46.8 | 44.0x | 4.43x | 608x | 62.1x | 0.63x | 39.3x | 4.07x |
| json | macro | 6.71 | 2.49 | 73.4 | 125.8 | 0.257 | 98.9 | 10.9 | 2.60 | 2.58x | 0.96x | 28.3x | 48.4x | 0.10x | 38.1x | 4.18x |
| deltablue | macro | 72.3 | 69.2 | 1.08s | 1.12s | 1.15 | 2.00s | 99.7 | 11.7 | 6.20x | 5.93x | 92.9x | 96.1x | 0.10x | 172x | 8.55x |
| havlak | macro | 37.5 | 38.5 | 835.2 | 845.3 | 1.84 | 105.04s | 3.22s | 94.3 | 0.40x | 0.41x | 8.85x | 8.96x | 0.02x | 1114x | 34.1x |
| cd | macro | 725.2 | 200.8 | 12.47s | --- | 14.9 | 27.54s | 955.3 | 35.3 | 20.5x | 5.69x | 353x | --- | 0.42x | 780x | 27.1x |

## BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 9.65 | 4.45 | 98.0 | 59.8 | 3.04 | 160.0 | 23.7 | 4.00 | 2.41x | 1.11x | 24.5x | 14.9x | 0.76x | 40.0x | 5.91x |
| fannkuch | permutation | 0.328 | 0.399 | 38.7 | 49.3 | 0.149 | 60.3 | 7.15 | 3.93 | 0.08x | 0.10x | 9.85x | 12.5x | 0.04x | 15.3x | 1.82x |
| fasta | generation | 0.776 | 0.883 | 9.14 | 10.5 | 0.240 | 26.5 | 8.87 | 6.13 | 0.13x | 0.14x | 1.49x | 1.71x | 0.04x | 4.32x | 1.45x |
| knucleotide | hashing | 4.20 | 4.71 | 20.1 | 4.93 | 0.276 | 161.5 | --- | 4.84 | 0.87x | 0.97x | 4.16x | 1.02x | 0.06x | 33.3x | --- |
| pidigits | bignum | 0.298 | 0.296 | 0.464 | 1.43 | 0.045 | 0.424 | 0.130 | 1.88 | 0.16x | 0.16x | 0.25x | 0.76x | 0.02x | 0.23x | 0.07x |
| regexredux | regex | 1.26 | 1.27 | 1.26 | 1.26 | 1.13 | 51.7 | --- | 2.39 | 0.52x | 0.53x | 0.53x | 0.53x | 0.47x | 21.6x | --- |
| revcomp | string | 1.23 | 1.17 | 3.76 | 4.06 | 0.366 | 31.9 | --- | 3.28 | 0.38x | 0.36x | 1.15x | 1.24x | 0.11x | 9.72x | --- |
| spectralnorm | numeric | 21.4 | 1.59 | 280.5 | 121.7 | 0.353 | 318.0 | 63.2 | 2.55 | 8.38x | 0.62x | 110x | 47.7x | 0.14x | 125x | 24.8x |

## KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 318.1 | 355.4 | 6.06s | 6.51s | 28.0 | 3.65s | 856.0 | 33.2 | 9.58x | 10.7x | 183x | 196x | 0.84x | 110x | 25.8x |
| matmul | numeric | 11.7 | 11.3 | 2.24s | 2.53s | 6.02 | 1.26s | 537.3 | 15.2 | 0.77x | 0.74x | 147x | 166x | 0.40x | 82.7x | 35.3x |
| primes | numeric | 18.6 | 3.42 | 412.0 | 540.0 | 1.58 | 3.74s | 94.4 | 4.29 | 4.33x | 0.80x | 96.1x | 126x | 0.37x | 872x | 22.0x |
| base64 | string | 11.2 | 11.3 | 565.4 | 599.2 | 0.558 | 855.2 | 156.4 | 17.1 | 0.66x | 0.66x | 33.1x | 35.1x | 0.03x | 50.1x | 9.17x |
| levenshtein | string | 35.4 | 6.78 | 749.0 | 344.2 | 0.899 | 429.4 | 53.7 | 3.98 | 8.88x | 1.70x | 188x | 86.4x | 0.23x | 108x | 13.5x |
| json_gen | data | 20.4 | 21.4 | 159.1 | 109.5 | 1.50 | 53.3 | 19.7 | 6.12 | 3.34x | 3.49x | 26.0x | 17.9x | 0.25x | 8.70x | 3.21x |
| collatz | numeric | 355.9 | 342.2 | 36.49s | 9.85s | 224.4 | 5.56s | 6.20s | 1.41s | 0.25x | 0.24x | 25.8x | 6.97x | 0.16x | 3.93x | 4.38x |

## LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 495.5 | 208.9 | 498.6 | 210.6 | 59.8 | 10.92s | 2.17s | 66.1 | 7.49x | 3.16x | 7.54x | 3.18x | 0.90x | 165x | 32.7x |
| array1 | array | 0.816 | 0.819 | 153.8 | 194.3 | 0.322 | 78.2 | 35.6 | 1.89 | 0.43x | 0.43x | 81.4x | 103x | 0.17x | 41.4x | 18.8x |
| deriv | symbolic | 28.8 | 10.1 | 205.8 | 483.1 | 2.80 | 338.6 | 59.7 | 3.62 | 7.95x | 2.80x | 56.8x | 133x | 0.77x | 93.5x | 16.5x |
| diviter | iterative | 265.1 | 265.1 | 114.39s | 1.56 | 257.7 | 9.55s | 26.56s | 469.6 | 0.56x | 0.56x | 244x | 0.003x | 0.55x | 20.3x | 56.6x |
| divrec | recursive | 15.3 | 2.00 | 438.0 | 5.37 | 4.87 | 44.3 | 35.6 | 7.52 | 2.03x | 0.27x | 58.2x | 0.71x | 0.65x | 5.89x | 4.74x |
| gcbench | allocation | 209.4 | 136.1 | 2.31s | 5.90s | 70.2 | 3.90s | 549.1 | 23.2 | 9.04x | 5.87x | 99.7x | 254x | 3.03x | 168x | 23.7x |
| paraffins | combinat | 0.325 | 0.280 | 18.9 | 21.3 | 0.051 | 4.11 | 2.50 | 0.990 | 0.33x | 0.28x | 19.1x | 21.5x | 0.05x | 4.16x | 2.53x |
| pnpoly | numeric | 12.6 | 14.4 | 1.36s | 1.54s | 1.94 | 111.5 | 201.1 | 5.83 | 2.17x | 2.48x | 233x | 264x | 0.33x | 19.1x | 34.5x |
| puzzle | search | 8.93 | 3.91 | 191.8 | 228.4 | 1.27 | 108.6 | 29.2 | 3.27 | 2.74x | 1.20x | 58.7x | 70.0x | 0.39x | 33.3x | 8.93x |
| quicksort | sorting | 10.1 | 1.07 | 106.9 | 134.0 | 0.198 | 396.9 | 19.0 | 1.62 | 6.21x | 0.66x | 65.8x | 82.5x | 0.12x | 244x | 11.7x |
| ray | numeric | 8.82 | 0.297 | 122.7 | 139.8 | 0.171 | 18.1 | 13.9 | 3.46 | 2.55x | 0.09x | 35.5x | 40.4x | 0.05x | 5.23x | 4.00x |

## JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 11.2 | 11.3 | 322.8 | 346.5 | 0.512 | 749.8 | --- | 17.4 | 0.64x | 0.65x | 18.5x | 19.9x | 0.03x | 43.0x | --- |
| navier_stokes | numeric | 139.6 | 116.7 | 9.41s | 10.16s | 46.7 | 1.02s | 98.5 | 14.1 | 9.91x | 8.28x | 668x | 721x | 3.31x | 72.6x | 6.99x |
| splay | data | 129.5 | 139.3 | 938.3 | 1.16s | 18.7 | 1.11s | 143.5 | 19.0 | 6.80x | 7.31x | 49.3x | 60.8x | 0.98x | 58.1x | 7.54x |
| hashmap | data | 126.8 | 44.5 | 1.50s | 15.57s | 2.71 | 4.90s | 314.7 | 15.1 | 8.42x | 2.96x | 99.6x | 1034x | 0.18x | 326x | 20.9x |
| crypto_sha1 | crypto | 57.3 | 29.4 | 1.99s | 2.29s | 2.65 | 619.0 | 217.2 | 8.60 | 6.66x | 3.42x | 232x | 266x | 0.31x | 72.0x | 25.3x |
| raytrace3d | 3d | 209.5 | 46.8 | 1.75s | 568.7 | 2.16 | 792.5 | --- | 18.0 | 11.7x | 2.61x | 97.3x | 31.7x | 0.12x | 44.1x | --- |

## Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 248.6 | 146.3 | 6.33s | 6.34s | 12.7 | 2.56s | 608.4 | 38.8 | 6.41x | 3.77x | 163x | 164x | 0.33x | 65.9x | 15.7x |
| microdiff | data-diff | 0.156 | 0.156 | 11.3 | 11.2 | 0.016 | 1.28s | 108.5 | 16.4 | 0.010x | 0.010x | 0.69x | 0.68x | 0.001x | 78.2x | 6.61x |
| hyphen | hyphenation | 2.56 | 2.03 | 114.9 | 20.7 | 0.087 | 366.1 | 51.2 | 6.43 | 0.40x | 0.32x | 17.9x | 3.22x | 0.01x | 57.0x | 7.97x |
