# Lambda Benchmark Results: Result53

- **Date:** 2026-10-23
- **Platform:** Darwin arm64
- **Lambda commit:** `ad882c1181dffaef5ada507484e1d07595c021fc`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v53-ad882c1181` (25,592,200 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 237.80s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 237.7s (batched 236.8s: sync 83.9s, async 152.9s; non-batched 0.8s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **Julia microbenchmark suite:** every language uses one complete warmup, then one measured workload; formatted-output timing includes synchronous null-sink writes.
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run; suites run in order `r7rs -> awfy -> beng -> kostya -> larceny -> julia -> jetstream -> text` with a 10s idle gap between suites
- **Range notation:** repeated-run timing cells are `median [minimum–maximum]`; single-run cells show the one measured value; the complete ordered sample set is retained in the result JSON's status detail.
- **Engines in this report:** MIR (untyped), MIR (typed), LambdaJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v53.json`
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

---

## Part 1 — Execution time (self-reported)

Each engine's own `__TIMING__` figure: the timed workload only, with process startup and prior build steps excluded. Runtime compilation performed during the workload is included. This is the historical series, comparable back through Result18, the MIR columns pin `LAMBDA_EXEC_BACKEND=jit`, and LambdaJS pins `JS_EXEC_BACKEND=mir`.

### Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed LambdaJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | LambdaJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 0.38x | 0.29x | 0.28x |
| AWFY | 14 | 13 | 14 | 14 | 14 | 1.46x | 0.49x | 2.57x |
| BENG | 8 | 8 | 8 | 8 | 8 | 0.26x | 0.18x | 1.52x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 1.22x | 0.58x | 0.51x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 0.75x | 0.61x | 0.76x |
| Julia microbenchmarks | 4 | 4 | 4 | 4 | 4 | 1.60x | 1.47x | 2.11x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 4.14x | 2.22x | 4.06x |
| Text | 11 | 11 | 10 | 11 | 11 | 2.37x | 1.40x | 11.9x |
| **Overall** | 71 | 70 | 70 | 71 | 71 | 1.04x | 0.60x | 1.63x |

### Population Accounting

The first population is the only one comparable with Result38; the complete-suite figure includes the four subsequently added text rows. Do not compare geomeans across these populations.

| Population | Rows | MIR (typed)/Node geo | MIR (untyped)/Node geo | MIR (typed)/C2MIR geo | C2MIR matched rows |
|---|---:|---:|---:|---:|---:|
| v38-comparable (without the four text extensions) | 67 | 0.56x | 0.94x | --- | 0 |
| complete current suite | 71 | 0.60x | 1.04x | --- | 0 |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

### Notable Results

- Missing timings: **2** cells
- MIR (untyped) missing: awfy/queens (exit_-6)
- MIR (typed) missing: text/jq_bf (exit_1)

#### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/spectralnorm | 233.3 | 2.65 | 87.9x |
| text/jq_bf | 247.54s | 2.91s | 85.1x |
| text/jq_tree | 400.39s | 5.76s | 69.5x |
| text/jq_mix | 260.29s | 3.92s | 66.3x |
| awfy/havlak | 2.53s | 95.0 | 26.7x |
| text/jq_records | 53.38s | 2.00s | 26.6x |
| awfy/nbody | 127.1 | 5.32 | 23.9x |
| jetstream/hashmap | 352.5 | 15.2 | 23.2x |

#### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| r7rs/sumfp | 0.054 | 0.875 | 0.06x |
| larceny/ray | 0.226 | 3.54 | 0.06x |
| r7rs/fft | 0.150 | 1.53 | 0.10x |
| larceny/paraffins | 0.101 | 0.981 | 0.10x |
| r7rs/tak | 0.090 | 0.773 | 0.12x |
| beng/fasta | 0.896 | 6.06 | 0.15x |
| kostya/collatz | 229.2 | 1.42s | 0.16x |
| r7rs/cpstak | 0.175 | 0.963 | 0.18x |

### R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.55 [1.54–1.58] | 1.58 [1.55–1.62] | 0.935 [0.911–0.948] | 2.01 [1.91–8.16] | 0.77x | 0.79x | 0.46x |
| fibfp | recursive | 2.64 [2.64–2.77] | 1.45 [1.42–1.45] | 0.990 [0.989–0.994] | 1.77 [1.73–1.77] | 1.49x | 0.82x | 0.56x |
| tak | recursive | 0.141 [0.136–0.143] | 0.142 [0.139–0.143] | 0.090 [0.089–0.090] | 0.773 [0.773–0.808] | 0.18x | 0.18x | 0.12x |
| cpstak | closure | 0.273 [0.273–0.275] | 0.272 [0.270–0.285] | 0.175 [0.173–0.176] | 0.963 [0.958–0.966] | 0.28x | 0.28x | 0.18x |
| sum | iterative | 0.266 [0.265–0.266] | 0.265 [0.265–0.266] | 0.537 [0.537–0.540] | 1.17 [1.17–1.94] | 0.23x | 0.23x | 0.46x |
| sumfp | iterative | 0.069 [0.068–0.069] | 0.068 [0.068–0.069] | 0.054 [0.054–0.059] | 0.875 [0.859–1.69] | 0.08x | 0.08x | 0.06x |
| nqueens | backtrack | 1.12 [1.11–1.14] | 1.11 [1.08–1.13] | 0.890 [0.889–0.892] | 1.75 [1.75–3.19] | 0.64x | 0.64x | 0.51x |
| fft | numeric | 0.324 [0.324–0.341] | 0.075 [0.074–0.075] | 0.150 [0.150–0.153] | 1.53 [1.53–1.58] | 0.21x | 0.05x | 0.10x |
| mbrot | numeric | 0.708 [0.708–0.710] | 0.580 [0.566–0.612] | 1.39 [1.35–1.39] | 1.79 [1.76–1.89] | 0.40x | 0.32x | 0.78x |
| ack | recursive | 14.6 [13.4–14.6] | 9.86 [9.84–9.93] | 7.48 [7.47–7.49] | 13.2 [13.2–13.2] | 1.10x | 0.74x | 0.57x |

### AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.031 [0.029–0.041] | 0.024 [0.024–0.025] | 0.083 [0.083–0.084] | 0.397 [0.392–2.08] | 0.08x | 0.06x | 0.21x |
| permute | micro | 0.260 [0.258–0.498] | 0.063 [0.061–0.067] | 1.01 [0.999–1.01] | 0.811 [0.809–0.826] | 0.32x | 0.08x | 1.24x |
| queens | micro | --- | 0.062 [0.061–0.063] | 0.503 [0.479–0.969] | 0.638 [0.637–0.639] | --- | 0.10x | 0.79x |
| towers | micro | 0.533 [0.467–0.719] | 0.209 [0.208–0.217] | 2.12 [2.06–2.13] | 1.10 [1.10–1.10] | 0.48x | 0.19x | 1.92x |
| bounce | micro | 0.066 [0.064–0.068] | 0.054 [0.052–0.055] | 0.671 [0.667–0.689] | 0.560 [0.545–0.842] | 0.12x | 0.10x | 1.20x |
| list | micro | 0.785 [0.783–0.786] | 0.118 [0.117–0.127] | 0.501 [0.484–0.504] | 0.487 [0.480–0.490] | 1.61x | 0.24x | 1.03x |
| storage | micro | 0.594 [0.579–0.814] | 0.365 [0.358–0.366] | 0.690 [0.667–0.713] | 0.656 [0.628–0.677] | 0.91x | 0.56x | 1.05x |
| mandelbrot | compute | 37.8 [37.8–38.1] | 37.7 [37.6–38.0] | 34.0 [33.7–34.3] | 31.0 [30.9–31.1] | 1.22x | 1.22x | 1.10x |
| nbody | compute | 7.10 [7.05–7.12] | 2.59 [2.58–2.65] | 127.1 [126.9–127.3] | 5.32 [5.26–5.80] | 1.33x | 0.49x | 23.9x |
| richards | macro | 314.0 [312.7–335.4] | 77.2 [76.6–77.7] | 220.3 [219.0–224.2] | 46.9 [46.7–47.7] | 6.69x | 1.64x | 4.69x |
| json | macro | 5.85 [5.81–5.86] | 2.02 [2.00–2.02] | 9.41 [9.38–9.58] | 2.60 [2.59–2.69] | 2.25x | 0.78x | 3.62x |
| deltablue | macro | 84.6 [84.6–84.9] | 20.4 [20.3–20.8] | 94.6 [94.5–96.5] | 11.7 [11.4–11.9] | 7.24x | 1.74x | 8.10x |
| havlak | macro | 1.74s [1.73s–1.82s] | 606.6 [594.5–611.4] | 2.53s [2.49s–2.58s] | 95.0 [91.9–98.7] | 18.3x | 6.39x | 26.7x |
| cd | macro | 739.1 [736.4–773.0] | 178.3 [177.2–178.3] | 407.6 [404.0–407.7] | 35.7 [35.5–36.2] | 20.7x | 4.99x | 11.4x |

### BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 5.95 [5.29–7.50] | 2.54 [2.49–2.54] | 3.92 [3.85–3.96] | 3.99 [3.87–4.90] | 1.49x | 0.64x | 0.98x |
| fannkuch | permutation | 0.278 [0.272–0.298] | 0.288 [0.287–0.292] | 43.5 [42.5–44.1] | 3.84 [3.82–4.00] | 0.07x | 0.07x | 11.3x |
| fasta | generation | 0.543 [0.527–0.548] | 0.467 [0.466–0.488] | 0.896 [0.889–0.921] | 6.06 [6.02–6.10] | 0.09x | 0.08x | 0.15x |
| knucleotide | hashing | 1.10 [1.09–1.39] | 0.442 [0.429–0.443] | 3.32 [3.29–3.34] | 4.83 [4.72–5.56] | 0.23x | 0.09x | 0.69x |
| pidigits | bignum | 0.370 [0.364–0.377] | 0.372 [0.370–0.375] | 0.329 [0.329–0.334] | 1.77 [1.77–1.98] | 0.21x | 0.21x | 0.19x |
| regexredux | regex | 0.851 [0.842–0.861] | 0.835 [0.824–0.844] | 2.94 [2.93–2.94] | 2.30 [2.30–2.33] | 0.37x | 0.36x | 1.28x |
| revcomp | string | 0.654 [0.640–0.656] | 0.554 [0.541–0.557] | 3.86 [3.82–3.92] | 3.29 [3.16–3.41] | 0.20x | 0.17x | 1.17x |
| spectralnorm | numeric | 1.65 [1.65–1.68] | 0.790 [0.787–0.801] | 233.3 [233.2–233.7] | 2.65 [2.58–2.75] | 0.62x | 0.30x | 87.9x |

### KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 249.7 [246.2–262.1] | 105.7 [105.6–106.5] | 42.7 [42.7–43.4] | 33.6 [33.4–33.9] | 7.44x | 3.15x | 1.27x |
| matmul | numeric | 4.74 [4.69–4.79] | 4.76 [4.74–4.83] | 7.01 [7.01–7.05] | 15.3 [15.3–15.4] | 0.31x | 0.31x | 0.46x |
| primes | numeric | 2.28 [2.27–2.33] | 2.18 [2.18–2.18] | 2.75 [2.73–2.78] | 4.39 [4.36–4.42] | 0.52x | 0.50x | 0.63x |
| base64 | string | 12.3 [12.2–12.6] | 5.05 [5.04–5.13] | 9.86 [9.74–9.87] | 17.3 [17.1–17.4] | 0.71x | 0.29x | 0.57x |
| levenshtein | string | 22.5 [22.3–22.6] | 2.82 [2.69–2.82] | 1.53 [1.47–1.53] | 3.96 [3.96–3.97] | 5.68x | 0.71x | 0.39x |
| json_gen | data | 24.2 [23.8–24.3] | 7.00 [6.98–7.33] | 4.44 [4.37–4.48] | 6.11 [6.06–6.19] | 3.97x | 1.15x | 0.73x |
| collatz | numeric | 296.5 [294.4–310.4] | 276.9 [275.4–279.3] | 229.2 [228.2–230.4] | 1.42s [1.41s–1.43s] | 0.21x | 0.20x | 0.16x |

### LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 170.2 [168.4–173.4] | 150.9 [150.1–152.1] | 141.8 [140.7–142.3] | 68.6 [68.4–68.7] | 2.48x | 2.20x | 2.07x |
| array1 | array | 0.803 [0.803–0.822] | 0.807 [0.804–0.808] | 0.546 [0.543–0.561] | 1.80 [1.80–1.83] | 0.45x | 0.45x | 0.30x |
| deriv | symbolic | 22.5 [21.8–22.8] | 6.11 [6.06–6.19] | 14.8 [14.6–14.9] | 3.78 [3.71–3.79] | 5.96x | 1.62x | 3.91x |
| diviter | iterative | 265.8 [265.6–266.2] | 266.1 [265.7–290.9] | 539.4 [538.8–539.7] | 471.6 [471.4–481.8] | 0.56x | 0.56x | 1.14x |
| divrec | recursive | 1.21 [1.20–1.21] | 1.22 [1.21–1.25] | 4.75 [4.75–4.77] | 7.66 [7.52–7.66] | 0.16x | 0.16x | 0.62x |
| gcbench | allocation | 124.1 [123.1–124.2] | 77.5 [77.5–77.6] | 94.7 [93.5–95.1] | 23.2 [23.1–23.4] | 5.36x | 3.34x | 4.09x |
| paraffins | combinat | 0.220 [0.196–0.248] | 0.152 [0.151–0.155] | 0.101 [0.100–0.102] | 0.981 [0.966–1.00] | 0.22x | 0.15x | 0.10x |
| pnpoly | numeric | 2.30 [2.27–2.36] | 3.63 [3.60–3.68] | 10.6 [10.6–10.7] | 5.89 [5.87–5.97] | 0.39x | 0.62x | 1.80x |
| puzzle | search | 13.9 [13.7–14.0] | 14.6 [14.5–14.8] | 2.42 [2.37–2.43] | 3.28 [3.27–3.28] | 4.22x | 4.44x | 0.74x |
| quicksort | sorting | 1.03 [0.987–1.03] | 0.620 [0.609–0.625] | 1.23 [1.22–1.23] | 1.66 [1.65–1.67] | 0.62x | 0.37x | 0.74x |
| ray | numeric | 0.218 [0.215–0.224] | 0.217 [0.215–0.219] | 0.226 [0.225–0.227] | 3.54 [3.53–3.66] | 0.06x | 0.06x | 0.06x |

### Julia microbenchmarks

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| parse_integers | conversion | 37.9 [37.7–38.0] | 33.6 [33.6–34.1] | 45.6 [45.3–46.8] | 13.3 [13.0–13.4] | 2.86x | 2.53x | 3.43x |
| matrix_statistics | numeric | 16.8 [16.5–17.2] | 16.4 [15.6–16.5] | 66.8 [66.8–66.9] | 21.0 [20.9–21.4] | 0.80x | 0.78x | 3.18x |
| iteration_pi_sum | numeric | 6.81 [6.79–6.85] | 6.81 [6.79–6.87] | 3.21 [3.21–3.22] | 4.86 [4.83–4.88] | 1.40x | 1.40x | 0.66x |
| formatted_output | io | 75.4 [75.4–77.2] | 62.1 [62.1–62.4] | 100.3 [99.7–100.4] | 36.6 [36.3–36.7] | 2.06x | 1.70x | 2.74x |

### JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 9.37 [9.29–9.43] | 7.11 [7.07–7.27] | 69.0 [68.4–69.7] | 17.6 [17.4–17.6] | 0.53x | 0.40x | 3.93x |
| navier_stokes | numeric | 76.5 [75.9–76.6] | 68.5 [68.2–68.5] | 28.9 [28.8–29.0] | 13.9 [13.8–14.0] | 5.49x | 4.91x | 2.08x |
| splay | data | 283.2 [281.6–283.5] | 274.9 [274.4–282.2] | 53.0 [52.5–53.8] | 19.0 [18.5–19.5] | 14.9x | 14.5x | 2.80x |
| hashmap | data | 72.4 [72.3–72.7] | 26.5 [26.3–26.6] | 352.5 [349.8–363.2] | 15.2 [15.2–15.2] | 4.78x | 1.75x | 23.2x |
| crypto_sha1 | crypto | 48.2 [48.2–48.4] | 28.1 [27.9–28.5] | 10.8 [10.8–11.0] | 8.70 [8.57–8.87] | 5.55x | 3.24x | 1.24x |
| raytrace3d | 3d | 78.9 [78.4–79.3] | 13.4 [13.3–13.7] | 124.4 [122.7–125.1] | 18.2 [18.2–18.3] | 4.34x | 0.73x | 6.83x |

### Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 716.6 [716.5–732.9] | 42.7 [42.7–43.0] | 173.4 [170.8–174.6] | 38.9 [38.9–38.9] | 18.4x | 1.10x | 4.46x |
| microdiff | data-diff | 36.9 [36.7–37.2] | 38.8 [38.6–39.1] | 116.8 [116.8–117.3] | 16.3 [16.1–17.1] | 2.27x | 2.38x | 7.18x |
| hyphen | hyphenation | 50.1 [49.4–50.4] | 9.45 [9.45–9.46] | 50.6 [50.6–52.5] | 6.63 [6.54–7.34] | 7.56x | 1.43x | 7.63x |
| prettier_ast | formatting | 842.3 [837.3–847.5] | 363.2 [361.8–365.9] | 558.1 [556.5–559.0] | 99.7 [99.7–99.9] | 8.45x | 3.64x | 5.60x |
| text_search | search | 7.73s [7.72s–7.76s] | 1.37s [1.37s–1.41s] | 4.61s [4.59s–4.62s] | 776.2 [775.5–776.2] | 9.96x | 1.77x | 5.94x |
| three_way_merge | merge | 2.36s [2.32s–2.38s] | 1.63s [1.62s–1.63s] | 1.68s [1.61s–1.68s] | 964.6 [964.5–969.3] | 2.44x | 1.69x | 1.74x |
| log_pipeline | log-processing | 4.35s [4.35s–4.73s] | 1.05s [1.04s–1.05s] | 4.47s [4.46s–4.50s] | 942.2 [942.2–943.4] | 4.62x | 1.11x | 4.74x |
| jq_mix | jq-breadth | 44.85s [44.81s–44.92s] | 44.61s [44.54s–44.62s] | 260.29s [257.07s–260.44s] | 3.92s [3.92s–3.94s] | 11.4x | 11.4x | 66.3x |
| jq_records | jq-data | 350.5 [348.5–350.6] | 398.9 [397.7–402.4] | 53.38s [53.12s–53.63s] | 2.00s [1.99s–2.01s] | 0.17x | 0.20x | 26.6x |
| jq_bf | jq-interpreter | 191.6 [191.2–192.1] | --- | 247.54s [247.38s–249.50s] | 2.91s [2.88s–2.95s] | 0.07x | --- | 85.1x |
| jq_tree | jq-paths | 1.96s [1.89s–1.96s] | 1.59s [1.58s–1.59s] | 400.39s [399.87s–400.92s] | 5.76s [5.76s–5.76s] | 0.34x | 0.28x | 69.5x |

---

## Part 2 — End-to-end time (wall clock, auto tier)

Wall clock from process invocation to exit, including **startup and compilation performed inside that process**. Build steps completed before invocation are excluded. The MIR and LambdaJS columns use the shipped auto tier -- no `LAMBDA_EXEC_BACKEND` or `JS_EXEC_BACKEND` override -- which is what `lambda.exe run script.ls` and `lambda.exe js script.js` actually do.

This set exists because the two questions are different. Part 1 asks how fast the compiled workload runs; part 2 asks how long it takes to run the script. Timing the auto tier under part 1's rules would charge Lambda for JIT compilation performed *inside* the measured region while crediting Node.js with a post-warmup figure -- comparing two different things. Process timing includes each engine's recorded warmup policy, workload and verification; separately added measurements are described above.

Same processes, where possible: the reference engines report their wall and `__TIMING__` figures from the *same* run, so parts 1 and 2 are two readings of one launch. The MIR and LambdaJS columns are re-run, because part 1 pins the compiled lane and part 2 must use the auto tier.

⚠ Short workloads are dominated by fixed process startup here, so a row whose part-1 time is a fraction of a millisecond says more about executable launch cost than about the language. Read part 2 by the longer rows.

### Summary

| Suite | Total | Timed MIR (untyped, auto) | Timed MIR (typed, auto) | Timed LambdaJS (default) | Timed Node.js | MIR (untyped, auto)/Node geo | MIR (typed, auto)/Node geo | LambdaJS (default)/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 0.35x | 0.34x | 1.34x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 1.16x | 0.93x | 6.49x |
| BENG | 8 | 8 | 8 | 8 | 8 | 0.42x | 0.41x | 1.60x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 1.60x | 0.88x | 13.8x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 0.92x | 0.64x | 4.63x |
| Julia microbenchmarks | 4 | 4 | 4 | 4 | 4 | 1.86x | 0.97x | 37.1x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 2.96x | 2.90x | 24.0x |
| Text | 11 | 11 | 10 | 7 | 11 | 2.38x | 1.95x | 31.8x |
| **Overall** | 71 | 71 | 70 | 67 | 71 | 1.08x | 0.84x | 6.54x |

> Ratio < 1.0 means the engine finished the whole run faster than Node.js.

### R7RS

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | LambdaJS (default) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | LambdaJS (default)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 13.1 [12.5–13.1] | 13.5 [13.3–14.2] | 83.8 [82.2–85.7] | 52.3 [48.0–96.5] | 0.25x | 0.26x | 1.60x |
| fibfp | recursive | 16.8 [16.4–17.0] | 15.5 [15.4–15.5] | 79.4 [77.4–82.1] | 45.3 [44.8–45.5] | 0.37x | 0.34x | 1.76x |
| tak | recursive | 11.6 [11.4–11.7] | 11.7 [11.6–11.9] | 28.6 [28.4–28.6] | 43.2 [43.1–43.3] | 0.27x | 0.27x | 0.66x |
| cpstak | closure | 11.9 [11.7–12.0] | 11.7 [11.6–11.9] | 33.8 [33.7–33.9] | 43.5 [43.2–43.8] | 0.27x | 0.27x | 0.78x |
| sum | iterative | 14.5 [14.3–14.7] | 11.6 [11.3–11.7] | 27.5 [27.2–27.6] | 43.9 [43.8–44.5] | 0.33x | 0.26x | 0.63x |
| sumfp | iterative | 14.3 [14.3–14.4] | 13.1 [12.9–13.2] | 24.3 [24.2–24.4] | 43.7 [43.5–44.9] | 0.33x | 0.30x | 0.56x |
| nqueens | backtrack | 20.6 [20.6–20.6] | 22.2 [22.1–22.2] | 126.0 [125.8–126.6] | 44.4 [44.4–46.2] | 0.46x | 0.50x | 2.84x |
| fft | numeric | 26.1 [25.8–26.6] | 25.8 [24.5–28.0] | 163.8 [163.4–164.0] | 44.5 [44.3–44.6] | 0.59x | 0.58x | 3.68x |
| mbrot | numeric | 14.5 [14.3–14.8] | 15.4 [15.1–15.5] | 29.9 [29.9–30.1] | 44.4 [44.3–44.5] | 0.33x | 0.35x | 0.67x |
| ack | recursive | 26.5 [25.2–26.5] | 21.0 [20.9–21.1] | 298.3 [298.1–298.8] | 55.5 [55.4–55.8] | 0.48x | 0.38x | 5.37x |

### AWFY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | LambdaJS (default) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | LambdaJS (default)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 16.6 [15.8–31.4] | 13.8 [13.5–16.3] | 22.0 [21.6–22.5] | 48.7 [44.3–75.9] | 0.34x | 0.28x | 0.45x |
| permute | micro | 14.4 [14.4–14.6] | 15.1 [15.0–15.3] | 47.2 [47.2–48.0] | 45.6 [44.6–45.9] | 0.32x | 0.33x | 1.04x |
| queens | micro | 17.5 [17.1–17.6] | 19.2 [19.1–19.4] | 32.0 [31.8–32.7] | 44.5 [44.3–45.1] | 0.39x | 0.43x | 0.72x |
| towers | micro | 19.0 [19.0–19.3] | 20.3 [20.1–20.4] | 64.6 [64.3–65.4] | 44.9 [44.6–45.8] | 0.42x | 0.45x | 1.44x |
| bounce | micro | 14.7 [14.5–14.9] | 14.6 [14.2–14.7] | 33.6 [33.1–50.5] | 45.2 [44.7–61.6] | 0.32x | 0.32x | 0.74x |
| list | micro | 14.1 [13.9–14.3] | 14.1 [14.1–14.3] | 29.5 [29.2–29.7] | 45.1 [44.8–45.2] | 0.31x | 0.31x | 0.65x |
| storage | micro | 13.6 [13.6–13.8] | 14.2 [14.1–14.5] | 38.4 [38.4–38.6] | 45.9 [45.8–48.1] | 0.30x | 0.31x | 0.84x |
| mandelbrot | compute | 76.4 [76.1–76.6] | 62.0 [61.9–66.5] | 7.33s [7.32s–7.42s] | 74.9 [74.6–76.0] | 1.02x | 0.83x | 97.8x |
| nbody | compute | 56.4 [56.2–56.7] | 56.9 [56.1–92.8] | 3.37s [3.34s–3.39s] | 49.7 [49.2–51.7] | 1.13x | 1.14x | 67.7x |
| richards | macro | 576.0 [571.7–594.7] | 335.2 [325.2–346.7] | 3.99s [3.96s–4.04s] | 90.8 [90.5–93.1] | 6.34x | 3.69x | 43.9x |
| json | macro | 42.1 [42.0–42.4] | 38.3 [36.4–40.4] | 158.5 [157.9–158.6] | 47.2 [46.7–48.4] | 0.89x | 0.81x | 3.36x |
| deltablue | macro | 330.2 [321.4–361.3] | 219.7 [213.1–220.9] | 1.46s [1.44s–1.50s] | 56.9 [56.3–57.8] | 5.80x | 3.86x | 25.6x |
| havlak | macro | 4.54s [4.53s–4.69s] | 1.23s [1.17s–1.37s] | 48.16s [48.02s–48.38s] | 143.3 [140.5–145.9] | 31.7x | 8.58x | 336x |
| cd | macro | 980.8 [964.4–986.6] | 547.7 [547.6–548.5] | 11.39s [11.36s–11.43s] | 80.4 [80.1–82.2] | 12.2x | 6.81x | 142x |

### BENG

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | LambdaJS (default) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | LambdaJS (default)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 23.1 [22.9–23.3] | 19.0 [18.9–19.1] | 65.7 [65.0–65.9] | 45.9 [45.4–46.5] | 0.50x | 0.41x | 1.43x |
| fannkuch | permutation | 26.2 [26.2–26.5] | 25.0 [24.8–25.2] | 165.9 [165.6–166.6] | 45.7 [45.5–45.7] | 0.57x | 0.55x | 3.63x |
| fasta | generation | 20.3 [20.2–20.4] | 20.0 [20.0–21.0] | 51.4 [51.3–51.9] | 48.5 [47.9–77.2] | 0.42x | 0.41x | 1.06x |
| knucleotide | hashing | 23.7 [23.4–23.7] | 22.6 [22.5–22.7] | 82.8 [81.6–83.1] | 47.1 [46.6–47.9] | 0.50x | 0.48x | 1.76x |
| pidigits | bignum | 11.4 [11.3–11.7] | 11.5 [11.3–11.6] | 22.8 [22.7–23.0] | 43.5 [43.0–43.6] | 0.26x | 0.27x | 0.52x |
| regexredux | regex | 11.1 [10.9–11.8] | 11.2 [11.2–11.6] | 24.9 [24.8–25.2] | 43.9 [43.8–44.0] | 0.25x | 0.26x | 0.57x |
| revcomp | string | 14.3 [14.3–14.8] | 14.4 [14.2–14.9] | 70.4 [70.2–70.7] | 44.7 [44.4–44.7] | 0.32x | 0.32x | 1.58x |
| spectralnorm | numeric | 36.8 [36.6–37.5] | 37.5 [37.1–37.7] | 430.0 [428.8–450.9] | 46.0 [45.9–46.6] | 0.80x | 0.81x | 9.35x |

### KOSTYA

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | LambdaJS (default) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | LambdaJS (default)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 271.2 [270.0–271.7] | 127.8 [127.1–128.9] | 5.03s [5.00s–5.04s] | 77.2 [77.0–77.7] | 3.51x | 1.66x | 65.1x |
| matmul | numeric | 221.3 [220.3–221.7] | 46.7 [46.0–46.8] | 5.75s [5.75s–5.75s] | 58.3 [58.3–59.1] | 3.80x | 0.80x | 98.5x |
| primes | numeric | 170.5 [165.5–199.3] | 72.5 [72.2–74.3] | 2.19s [2.18s–2.22s] | 47.5 [47.4–48.2] | 3.59x | 1.53x | 46.1x |
| base64 | string | 50.3 [49.8–50.8] | 43.1 [42.9–43.4] | 1.12s [1.12s–1.15s] | 60.9 [60.4–61.0] | 0.83x | 0.71x | 18.5x |
| levenshtein | string | 105.3 [105.2–106.3] | 60.6 [60.1–69.6] | 1.15s [1.13s–1.15s] | 47.0 [47.0–47.2] | 2.24x | 1.29x | 24.4x |
| json_gen | data | 63.3 [63.2–63.4] | 49.7 [49.3–50.0] | 112.0 [109.3–140.2] | 49.8 [49.6–50.2] | 1.27x | 1.00x | 2.25x |
| collatz | numeric | 344.4 [344.1–348.8] | 316.1 [316.0–316.3] | 476.2 [474.0–511.3] | 1.46s [1.46s–1.47s] | 0.24x | 0.22x | 0.33x |

### LARCENY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | LambdaJS (default) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | LambdaJS (default)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 191.4 [190.6–191.6] | 184.2 [170.1–194.4] | 20.42s [20.41s–20.46s] | 112.0 [111.5–112.4] | 1.71x | 1.64x | 182x |
| array1 | array | 133.1 [132.6–133.8] | 126.6 [125.9–127.3] | 365.2 [364.1–367.4] | 44.5 [44.5–44.8] | 2.99x | 2.84x | 8.20x |
| deriv | symbolic | 44.7 [43.8–75.8] | 25.0 [24.8–25.1] | 173.6 [173.6–175.9] | 48.0 [47.4–48.1] | 0.93x | 0.52x | 3.62x |
| diviter | iterative | 391.4 [391.0–392.2] | 11.7 [11.3–12.2] | 668.1 [665.6–671.8] | 517.1 [516.2–525.5] | 0.76x | 0.02x | 1.29x |
| divrec | recursive | 13.3 [13.3–13.5] | 13.9 [13.8–13.9] | 137.7 [137.4–138.9] | 50.3 [50.3–50.7] | 0.27x | 0.28x | 2.74x |
| gcbench | allocation | 225.8 [224.0–253.4] | 112.0 [111.9–112.3] | 902.4 [901.9–909.0] | 65.5 [65.3–65.6] | 3.45x | 1.71x | 13.8x |
| paraffins | combinat | 30.1 [29.4–49.1] | 48.3 [47.9–48.7] | 82.2 [82.0–111.8] | 44.0 [43.8–44.1] | 0.68x | 1.10x | 1.87x |
| pnpoly | numeric | 31.2 [31.2–31.3] | 40.1 [40.0–40.3] | 101.5 [101.1–102.3] | 49.2 [48.9–49.3] | 0.63x | 0.81x | 2.06x |
| puzzle | search | 32.9 [32.9–33.4] | 34.2 [34.0–34.3] | 53.0 [52.8–53.4] | 46.1 [46.0–46.4] | 0.71x | 0.74x | 1.15x |
| quicksort | sorting | 21.7 [21.5–21.9] | 23.6 [23.5–23.6] | 404.0 [397.2–409.6] | 44.7 [44.4–44.9] | 0.49x | 0.53x | 9.04x |
| ray | numeric | 40.1 [40.1–40.2] | 41.5 [40.8–41.9] | 93.4 [92.3–128.2] | 47.2 [47.0–47.7] | 0.85x | 0.88x | 1.98x |

### Julia microbenchmarks

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | LambdaJS (default) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | LambdaJS (default)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| parse_integers | conversion | 122.0 [121.8–122.2] | 100.1 [100.0–101.3] | 11.61s [11.55s–11.65s] | 72.8 [72.7–72.8] | 1.68x | 1.37x | 160x |
| matrix_statistics | numeric | 74.9 [74.4–109.5] | 73.3 [72.7–73.6] | 3.47s [3.45s–3.50s] | 92.2 [92.2–93.0] | 0.81x | 0.80x | 37.6x |
| iteration_pi_sum | numeric | 227.7 [217.7–244.0] | 32.4 [32.4–32.5] | 144.1 [143.9–145.2] | 53.4 [53.2–53.7] | 4.26x | 0.61x | 2.70x |
| formatted_output | io | 243.9 [243.5–270.0] | 159.2 [158.7–163.5] | 13.98s [13.91s–14.28s] | 119.4 [119.3–119.7] | 2.04x | 1.33x | 117x |

### JetStream

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | LambdaJS (default) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | LambdaJS (default)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 90.3 [90.0–91.3] | 106.3 [105.1–106.5] | 1.76s [1.73s–1.78s] | 63.5 [62.0–63.7] | 1.42x | 1.67x | 27.7x |
| navier_stokes | numeric | 243.4 [243.3–244.5] | 220.5 [219.8–220.6] | 1.11s [1.10s–1.13s] | 59.0 [59.0–59.1] | 4.12x | 3.74x | 18.8x |
| splay | data | 422.3 [406.5–424.2] | 947.9 [945.9–949.7] | 1.30s [1.30s–1.30s] | 90.6 [90.2–92.0] | 4.66x | 10.5x | 14.4x |
| hashmap | data | 173.2 [172.8–174.9] | 127.3 [126.9–129.4] | 4.55s [4.54s–4.55s] | 58.6 [58.4–58.6] | 2.95x | 2.17x | 77.6x |
| crypto_sha1 | crypto | 171.9 [169.8–173.0] | 154.8 [154.6–154.8] | 623.7 [621.9–623.8] | 51.6 [51.5–51.9] | 3.33x | 3.00x | 12.1x |
| raytrace3d | 3d | 154.4 [154.4–154.9] | 86.0 [81.6–97.2] | 1.69s [1.68s–1.70s] | 61.9 [61.4–62.0] | 2.50x | 1.39x | 27.3x |

### Text

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | LambdaJS (default) (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | LambdaJS (default)/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 750.2 [747.3–754.3] | 69.7 [69.6–69.9] | 3.02s [3.01s–3.02s] | 82.7 [82.6–82.9] | 9.07x | 0.84x | 36.5x |
| microdiff | data-diff | 69.2 [68.6–72.4] | 73.9 [73.7–75.5] | 1.36s [1.36s–1.37s] | 59.6 [59.4–60.4] | 1.16x | 1.24x | 22.9x |
| hyphen | hyphenation | 94.0 [93.6–94.8] | 74.2 [74.2–75.2] | 413.8 [412.5–414.2] | 57.2 [56.8–58.3] | 1.64x | 1.30x | 7.23x |
| prettier_ast | formatting | 1.34s [1.33s–1.34s] | 3.76s [3.76s–3.79s] | 11.70s [11.52s–11.77s] | 144.3 [144.2–146.2] | 9.27x | 26.0x | 81.1x |
| text_search | search | 7.79s [7.76s–7.79s] | 1.40s [1.40s–1.41s] | 107.45s [106.97s–108.45s] | 821.1 [820.2–821.2] | 9.49x | 1.71x | 131x |
| three_way_merge | merge | 3.40s [3.37s–3.41s] | 2.44s [2.42s–2.44s] | 16.76s [16.74s–16.80s] | 1.01s [1.01s–1.01s] | 3.36x | 2.41x | 16.6x |
| log_pipeline | log-processing | 5.75s [5.74s–5.78s] | 1.32s [1.30s–1.35s] | 31.06s [31.04s–31.10s] | 994.9 [994.6–996.1] | 5.78x | 1.32x | 31.2x |
| jq_mix | jq-breadth | 44.98s [44.96s–45.02s] | 44.66s [44.62s–44.71s] | --- [600.00s–600.00s] | 4.00s [3.98s–4.06s] | 11.3x | 11.2x | --- |
| jq_records | jq-data | 2.74s [2.73s–2.75s] | 2.53s [2.51s–2.74s] | --- [600.00s–600.00s] | 2.06s [2.05s–2.06s] | 1.33x | 1.23x | --- |
| jq_bf | jq-interpreter | 252.6 [252.4–253.7] | --- [10.4–11.5] | --- [600.00s–600.00s] | 2.96s [2.93s–3.00s] | 0.09x | --- | --- |
| jq_tree | jq-paths | 2.17s [2.15s–2.20s] | 1.72s [1.71s–1.73s] | --- [600.00s–600.00s] | 5.81s [5.81s–5.82s] | 0.37x | 0.30x | --- |

