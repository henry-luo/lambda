# Lambda Benchmark Results: Result45

- **Date:** 2026-09-15
- **Platform:** Darwin arm64
- **Lambda commit:** `714d448fb2b64b47d12e38daff1f3c2922477dec`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v45-714d448fb2` (19,552,696 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 63.20s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 63.1s (batched 62.1s: sync 47.1s, async 15.0s; non-batched 1.0s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Range notation:** per-row timing cells are `median [minimum–maximum]`; the complete ordered sample set is retained in the result JSON's status detail.
- **Engines in this report:** MIR (untyped), MIR (typed), LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v45.json`
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

---

## Part 1 — Execution time (self-reported)

Each engine's own `__TIMING__` figure: the timed workload only, with startup and compilation outside the measured region. This is the historical series, comparable back through Result18, and the MIR columns pin `LAMBDA_TIER=jit`.

### Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 0.35x | 0.32x | 5.75x | 6.51x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 1.51x | 0.78x | 21.0x | 5.22x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 0.39x | 0.34x | 8.78x | 1.70x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 1.53x | 0.88x | 36.8x | 12.0x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 1.48x | 0.76x | 19.1x | 13.2x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 4.92x | 2.70x | 37.8x | 12.0x |
| Text | 7 | 7 | 7 | 7 | 7 | 7 | 7.04x | 4.77x | 51.7x | 12.8x |
| **Overall** | 63 | 63 | 63 | 63 | 63 | 63 | 1.34x | 0.85x | 18.7x | 7.24x |

### Population Accounting

The first population is the only one comparable with Result38; the complete-suite figure includes the four subsequently added text rows. Do not compare geomeans across these populations.

| Population | Rows | MIR (typed)/Node geo | MIR (untyped)/Node geo | MIR (typed)/C2MIR geo | C2MIR matched rows |
|---|---:|---:|---:|---:|---:|
| v38-comparable (without the four text extensions) | 59 | 0.76x | 1.18x | --- | 0 |
| complete current suite | 63 | 0.85x | 1.34x | --- | 0 |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

### Notable Results

- Missing timings: **0** cells

#### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| kostya/primes | 1.70s | 4.30 | 394x |
| awfy/havlak | 21.03s | 93.4 | 225x |
| awfy/cd | 5.30s | 35.5 | 149x |
| text/text_search | 115.95s | 775.9 | 149x |
| larceny/triangl | 7.28s | 66.2 | 110x |
| jetstream/hashmap | 1.64s | 15.2 | 108x |
| awfy/nbody | 559.6 | 5.28 | 106x |
| larceny/quicksort | 147.4 | 1.64 | 89.8x |

#### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.506 | 1.92 | 0.26x |
| r7rs/tak | 0.308 | 0.775 | 0.40x |
| r7rs/cpstak | 0.607 | 0.977 | 0.62x |

### R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.27 [1.26–1.27] | 1.29 [1.28–1.33] | 15.4 [15.3–15.5] | 18.7 [18.6–18.7] | 1.89 [1.84–7.90] | 0.67x | 0.68x | 8.17x | 9.90x |
| fibfp | recursive | 2.17 [2.16–2.19] | 1.23 [1.22–1.24] | 15.1 [15.1–15.2] | 18.4 [18.4–18.5] | 1.74 [1.73–1.78] | 1.24x | 0.71x | 8.67x | 10.6x |
| tak | recursive | 0.122 [0.121–0.123] | 0.126 [0.121–0.127] | 0.308 [0.304–0.318] | 2.77 [2.77–2.78] | 0.775 [0.771–0.797] | 0.16x | 0.16x | 0.40x | 3.58x |
| cpstak | closure | 0.243 [0.239–0.243] | 0.242 [0.242–0.250] | 0.607 [0.606–0.607] | 5.59 [5.54–5.62] | 0.977 [0.971–0.990] | 0.25x | 0.25x | 0.62x | 5.72x |
| sum | iterative | 0.266 [0.266–0.266] | 0.266 [0.265–0.266] | 27.7 [27.2–28.0] | 30.8 [30.8–30.8] | 1.18 [1.18–1.88] | 0.23x | 0.23x | 23.5x | 26.1x |
| sumfp | iterative | 0.069 [0.068–0.069] | 0.068 [0.068–0.069] | 2.83 [2.79–2.98] | 3.64 [3.64–3.71] | 0.862 [0.855–1.71] | 0.08x | 0.08x | 3.29x | 4.22x |
| nqueens | backtrack | 1.54 [1.53–1.56] | 1.68 [1.68–1.71] | 52.5 [52.3–53.0] | 7.76 [7.73–7.97] | 1.71 [1.71–2.95] | 0.90x | 0.98x | 30.7x | 4.54x |
| fft | numeric | 0.279 [0.276–0.281] | 0.158 [0.157–0.167] | 23.8 [23.5–23.8] | 2.73 [2.73–2.74] | 1.58 [1.57–1.60] | 0.18x | 0.10x | 15.0x | 1.73x |
| mbrot | numeric | 0.710 [0.710–0.711] | 0.929 [0.922–0.932] | 16.5 [16.4–16.5] | 17.6 [17.5–17.9] | 1.79 [1.78–1.87] | 0.40x | 0.52x | 9.22x | 9.79x |
| ack | recursive | 11.0 [11.0–11.3] | 9.47 [9.46–9.49] | 91.2 [88.0–91.6] | 100.5 [100.3–101.4] | 13.3 [13.3–13.3] | 0.83x | 0.71x | 6.86x | 7.56x |

### AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.030 [0.030–0.030] | 0.029 [0.029–0.030] | 3.16 [3.14–3.19] | 0.613 [0.603–0.614] | 0.384 [0.375–0.388] | 0.08x | 0.08x | 8.22x | 1.60x |
| permute | micro | 0.588 [0.583–0.589] | 0.093 [0.093–0.094] | 8.14 [8.07–8.17] | 1.56 [1.52–1.57] | 0.815 [0.805–0.815] | 0.72x | 0.11x | 10.00x | 1.92x |
| queens | micro | 0.410 [0.407–0.410] | 0.205 [0.205–0.209] | 4.50 [4.48–4.52] | 1.04 [1.04–1.04] | 0.648 [0.630–0.659] | 0.63x | 0.32x | 6.95x | 1.60x |
| towers | micro | 1.21 [1.21–1.21] | 0.328 [0.320–0.328] | 19.6 [19.5–19.9] | 2.22 [2.20–2.25] | 1.10 [1.09–1.10] | 1.10x | 0.30x | 17.8x | 2.02x |
| bounce | micro | 0.067 [0.067–0.068] | 0.097 [0.097–0.097] | 3.95 [3.94–3.96] | 0.869 [0.868–0.869] | 0.537 [0.532–0.550] | 0.12x | 0.18x | 7.35x | 1.62x |
| list | micro | 0.727 [0.677–0.735] | 0.145 [0.144–0.145] | 2.14 [2.13–2.15] | 0.899 [0.897–0.906] | 0.485 [0.484–0.501] | 1.50x | 0.30x | 4.42x | 1.85x |
| storage | micro | 0.716 [0.709–0.725] | 0.336 [0.335–0.338] | 6.08 [6.04–6.16] | 2.09 [2.06–2.10] | 0.626 [0.623–0.641] | 1.14x | 0.54x | 9.72x | 3.34x |
| mandelbrot | compute | 41.7 [41.7–41.7] | 41.8 [41.8–41.8] | 447.1 [445.4–448.1] | 864.1 [862.6–869.5] | 30.8 [30.8–30.9] | 1.35x | 1.35x | 14.5x | 28.0x |
| nbody | compute | 51.9 [51.7–53.0] | 15.8 [15.8–15.9] | 559.6 [556.0–560.4] | 158.6 [157.9–159.2] | 5.28 [5.28–5.75] | 9.83x | 3.00x | 106x | 30.0x |
| richards | macro | 419.5 [418.5–429.3] | 397.3 [396.7–399.3] | 1.15s [1.15s–1.16s] | 190.2 [189.3–190.8] | 46.1 [46.1–46.5] | 9.09x | 8.61x | 24.9x | 4.12x |
| json | macro | 6.10 [6.10–6.22] | 3.11 [3.10–3.19] | 45.1 [45.0–45.4] | 10.7 [10.6–11.1] | 2.61 [2.58–2.66] | 2.34x | 1.19x | 17.3x | 4.10x |
| deltablue | macro | 125.3 [124.1–126.1] | 41.8 [41.5–42.1] | 532.2 [531.0–535.3] | 98.8 [98.6–98.9] | 11.5 [11.5–11.7] | 10.9x | 3.62x | 46.1x | 8.56x |
| havlak | macro | 69.1 [68.9–73.3] | 62.6 [62.2–64.7] | 21.03s [21.00s–21.25s] | 3.22s [3.20s–3.22s] | 93.4 [92.8–102.7] | 0.74x | 0.67x | 225x | 34.5x |
| cd | macro | 570.3 [569.2–585.6] | 482.1 [481.6–493.9] | 5.30s [5.28s–5.33s] | 949.2 [948.3–952.1] | 35.5 [35.5–35.5] | 16.1x | 13.6x | 149x | 26.8x |

### BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 8.28 [8.18–8.30] | 2.31 [2.24–2.32] | 32.2 [32.0–32.7] | 23.5 [23.4–23.5] | 4.02 [4.00–5.11] | 2.06x | 0.57x | 7.99x | 5.83x |
| fannkuch | permutation | 0.332 [0.329–0.332] | 0.360 [0.358–0.360] | 37.6 [37.5–38.1] | 7.13 [7.11–7.15] | 3.99 [3.91–4.00] | 0.08x | 0.09x | 9.43x | 1.79x |
| fasta | generation | 0.749 [0.731–0.774] | 0.872 [0.864–0.877] | 45.7 [45.7–45.8] | 8.68 [8.68–8.82] | 6.08 [6.07–6.60] | 0.12x | 0.14x | 7.52x | 1.43x |
| knucleotide | hashing | 4.55 [4.50–4.75] | 5.31 [5.29–5.37] | 162.7 [162.6–162.9] | 7.57 [7.56–7.67] | 4.81 [4.80–5.83] | 0.95x | 1.10x | 33.8x | 1.57x |
| pidigits | bignum | 0.344 [0.342–0.349] | 0.349 [0.344–0.353] | 0.506 [0.504–0.510] | 0.127 [0.125–0.130] | 1.92 [1.89–1.96] | 0.18x | 0.18x | 0.26x | 0.07x |
| regexredux | regex | 1.27 [1.26–1.27] | 1.27 [1.26–1.28] | 30.9 [30.9–30.9] | 5.56 [5.52–5.56] | 2.39 [2.38–2.54] | 0.53x | 0.53x | 12.9x | 2.32x |
| revcomp | string | 1.24 [1.23–1.26] | 1.18 [1.17–1.18] | 37.0 [36.9–37.2] | 2.55 [2.55–2.58] | 3.31 [3.26–3.32] | 0.38x | 0.35x | 11.2x | 0.77x |
| spectralnorm | numeric | 1.73 [1.72–1.74] | 1.74 [1.74–1.75] | 123.2 [123.0–123.3] | 63.3 [63.0–64.5] | 2.54 [2.51–2.75] | 0.68x | 0.69x | 48.5x | 24.9x |

### KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 219.4 [219.3–220.5] | 190.5 [190.3–192.3] | 2.87s [2.85s–2.95s] | 874.4 [873.6–876.8] | 33.5 [33.2–33.5] | 6.54x | 5.68x | 85.5x | 26.1x |
| matmul | numeric | 5.85 [5.83–5.92] | 5.78 [5.78–5.80] | 774.1 [772.7–774.9] | 536.8 [536.6–537.3] | 15.3 [15.2–15.4] | 0.38x | 0.38x | 50.6x | 35.1x |
| primes | numeric | 15.1 [15.1–15.2] | 2.46 [2.43–2.64] | 1.70s [1.68s–1.70s] | 94.5 [94.1–94.7] | 4.30 [4.29–4.43] | 3.51x | 0.57x | 394x | 22.0x |
| base64 | string | 16.2 [16.2–16.2] | 8.99 [8.93–9.07] | 525.4 [518.9–525.7] | 155.5 [155.4–160.6] | 17.1 [17.1–17.4] | 0.95x | 0.52x | 30.6x | 9.07x |
| levenshtein | string | 9.88 [9.86–10.0] | 6.08 [6.06–6.09] | 216.1 [215.3–217.6] | 53.7 [53.7–53.7] | 3.95 [3.88–3.97] | 2.50x | 1.54x | 54.6x | 13.6x |
| json_gen | data | 25.8 [25.8–26.1] | 11.1 [11.1–11.3] | 40.0 [39.0–40.9] | 19.8 [19.6–19.9] | 6.17 [6.11–6.23] | 4.19x | 1.80x | 6.49x | 3.21x |
| collatz | numeric | 314.0 [311.3–315.1] | 314.2 [312.8–316.1] | 6.99s [6.98s–7.00s] | 6.19s [6.19s–6.21s] | 1.41s [1.41s–1.42s] | 0.22x | 0.22x | 4.94x | 4.38x |

### LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 275.5 [275.2–276.8] | 185.3 [184.9–185.8] | 7.28s [7.28s–7.29s] | 2.16s [2.16s–2.17s] | 66.2 [65.6–66.6] | 4.16x | 2.80x | 110x | 32.7x |
| array1 | array | 0.812 [0.805–0.816] | 0.807 [0.807–0.808] | 50.5 [50.1–50.8] | 35.7 [35.6–35.9] | 1.90 [1.89–1.90] | 0.43x | 0.42x | 26.5x | 18.8x |
| deriv | symbolic | 28.9 [28.6–29.3] | 7.18 [7.16–7.25] | 78.0 [77.5–78.4] | 58.8 [58.5–59.5] | 3.63 [3.60–3.70] | 7.96x | 1.98x | 21.5x | 16.2x |
| diviter | iterative | 265.4 [265.2–265.5] | 265.3 [265.1–265.4] | 15.25s [15.24s–15.27s] | 26.58s [26.55s–26.60s] | 468.7 [468.3–469.1] | 0.57x | 0.57x | 32.5x | 56.7x |
| divrec | recursive | 5.49 [5.49–5.50] | 1.20 [1.20–1.21] | 15.3 [15.2–15.3] | 35.6 [35.2–36.4] | 7.57 [7.51–7.62] | 0.72x | 0.16x | 2.02x | 4.70x |
| gcbench | allocation | 189.5 [188.1–189.6] | 83.4 [81.2–85.1] | 785.5 [778.1–790.1] | 546.8 [540.0–548.5] | 23.3 [23.0–23.3] | 8.14x | 3.59x | 33.8x | 23.5x |
| paraffins | combinat | 0.248 [0.246–0.249] | 0.213 [0.213–0.218] | 3.98 [3.96–4.00] | 2.51 [2.50–2.52] | 0.978 [0.976–1.01] | 0.25x | 0.22x | 4.07x | 2.56x |
| pnpoly | numeric | 12.5 [12.5–12.6] | 12.5 [12.5–12.5] | 136.9 [136.7–140.5] | 200.3 [200.3–200.5] | 5.79 [5.76–5.86] | 2.16x | 2.16x | 23.6x | 34.6x |
| puzzle | search | 18.8 [18.4–19.4] | 13.7 [13.4–13.8] | 61.9 [61.8–62.2] | 29.1 [29.1–29.3] | 3.28 [3.27–3.29] | 5.74x | 4.19x | 18.9x | 8.88x |
| quicksort | sorting | 11.4 [11.4–11.4] | 0.781 [0.775–0.781] | 147.4 [147.2–147.8] | 19.3 [19.1–19.3] | 1.64 [1.63–1.65] | 6.95x | 0.48x | 89.8x | 11.7x |
| ray | numeric | 0.265 [0.263–0.267] | 0.262 [0.260–0.265] | 19.0 [18.9–19.1] | 13.6 [13.6–13.8] | 3.55 [3.47–3.56] | 0.07x | 0.07x | 5.35x | 3.83x |

### JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 10.0 [9.88–10.1] | 9.09 [9.07–9.09] | 431.8 [430.9–434.6] | 215.1 [214.5–216.2] | 17.6 [17.5–17.6] | 0.57x | 0.52x | 24.5x | 12.2x |
| navier_stokes | numeric | 96.2 [96.2–96.3] | 95.6 [95.6–95.6] | 385.9 [383.3–387.8] | 98.7 [98.4–98.7] | 14.0 [13.8–14.1] | 6.89x | 6.84x | 27.6x | 7.06x |
| splay | data | 313.2 [311.0–318.1] | 294.5 [294.5–296.2] | 487.6 [486.7–491.6] | 144.7 [144.2–144.8] | 19.0 [18.7–19.1] | 16.4x | 15.5x | 25.6x | 7.60x |
| hashmap | data | 127.5 [127.1–128.6] | 41.4 [41.2–41.6] | 1.64s [1.63s–1.64s] | 312.4 [311.3–316.5] | 15.2 [15.2–15.2] | 8.38x | 2.72x | 108x | 20.5x |
| crypto_sha1 | crypto | 61.1 [59.1–61.8] | 26.8 [26.6–27.4] | 389.0 [387.5–392.6] | 216.7 [216.6–217.6] | 8.66 [8.58–8.80] | 7.05x | 3.10x | 44.9x | 25.0x |
| raytrace3d | 3d | 67.5 [67.1–67.9] | 15.4 [15.2–15.4] | 630.3 [627.4–632.1] | 161.4 [161.4–164.1] | 18.1 [17.9–18.1] | 3.73x | 0.85x | 34.8x | 8.92x |

### Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 236.0 [235.2–238.0] | 137.7 [137.6–138.1] | 2.83s [2.82s–2.83s] | 607.4 [606.2–610.9] | 38.8 [38.6–38.9] | 6.08x | 3.55x | 72.9x | 15.6x |
| microdiff | data-diff | 51.2 [51.0–51.7] | 56.7 [56.5–57.8] | 1.25s [1.25s–1.26s] | 108.6 [107.6–109.7] | 16.0 [15.8–16.8] | 3.20x | 3.54x | 78.3x | 6.78x |
| hyphen | hyphenation | 71.0 [70.1–72.2] | 71.0 [70.9–71.6] | 527.5 [525.2–535.5] | 79.3 [78.7–79.6] | 6.57 [6.55–7.22] | 10.8x | 10.8x | 80.3x | 12.1x |
| prettier_ast | formatting | 928.3 [920.7–937.5] | 686.7 [679.5–693.3] | 5.00s [4.97s–5.02s] | 1.41s [1.41s–1.41s] | 99.1 [98.7–99.2] | 9.37x | 6.93x | 50.5x | 14.2x |
| text_search | search | 15.02s [15.02s–15.02s] | 1.80s [1.80s–1.81s] | 115.95s [115.15s–116.07s] | 38.64s [38.63s–38.66s] | 775.9 [774.8–777.8] | 19.4x | 2.32x | 149x | 49.8x |
| three_way_merge | merge | 3.41s [3.39s–3.41s] | 3.73s [3.73s–3.75s] | 15.17s [15.03s–15.19s] | 6.09s [6.08s–6.11s] | 961.1 [957.6–963.9] | 3.55x | 3.89x | 15.8x | 6.33x |
| log_pipeline | log-processing | 5.96s [5.90s–6.07s] | 6.18s [6.12s–6.20s] | 16.94s [16.88s–17.03s] | 9.43s [9.38s–9.51s] | 939.2 [933.0–945.7] | 6.34x | 6.58x | 18.0x | 10.0x |

---

## Part 2 — End-to-end time (wall clock, auto tier)

Wall clock from process invocation to exit, so **every engine pays its own startup and compilation inside the number**. The MIR columns use the shipped auto tier -- no `LAMBDA_TIER` override -- which is what `lambda.exe run script.ls` actually does.

This set exists because the two questions are different. Part 1 asks how fast the compiled workload runs; part 2 asks how long it takes to run the script. Timing the auto tier under part 1's rules would charge Lambda for JIT compilation performed *inside* the measured region while crediting Node.js with a post-warmup figure -- comparing two different things. Here the accounting is the same for everyone.

Same processes, where possible: the reference engines report their wall and `__TIMING__` figures from the *same* run, so parts 1 and 2 are two readings of one launch. Only the MIR columns are re-run, because part 1 pins the JIT and part 2 must use the auto tier.

⚠ Short workloads are dominated by fixed process startup here, so a row whose part-1 time is a fraction of a millisecond says more about executable launch cost than about the language. Read part 2 by the longer rows.

### Summary

| Suite | Total | Timed MIR (untyped, auto) | Timed MIR (typed, auto) | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped, auto)/Node geo | MIR (typed, auto)/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 0.42x | 0.42x | 1.05x | 0.41x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 1.01x | 0.98x | 4.76x | 0.80x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 0.50x | 0.53x | 1.81x | 0.33x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 0.75x | 0.64x | 9.55x | 2.91x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 0.84x | 0.67x | 3.85x | 1.92x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 2.55x | 1.97x | 12.1x | 3.68x |
| Text | 7 | 7 | 7 | 7 | 7 | 7 | 4.29x | 2.93x | 34.1x | 6.82x |
| **Overall** | 63 | 63 | 63 | 63 | 63 | 63 | 0.97x | 0.85x | 4.69x | 1.27x |

> Ratio < 1.0 means the engine finished the whole run faster than Node.js.

### R7RS

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 17.1 [16.5–17.4] | 17.2 [17.0–17.8] | 40.7 [40.7–40.9] | 24.8 [24.6–26.7] | 47.2 [46.7–91.7] | 0.36x | 0.36x | 0.86x | 0.52x |
| fibfp | recursive | 18.9 [18.5–18.9] | 17.2 [16.5–17.8] | 39.6 [39.2–39.9] | 24.0 [23.8–24.3] | 44.0 [43.7–44.1] | 0.43x | 0.39x | 0.90x | 0.55x |
| tak | recursive | 16.2 [16.2–16.2] | 15.6 [15.5–15.6] | 25.8 [25.8–26.1] | 8.14 [7.95–8.56] | 42.9 [42.6–43.5] | 0.38x | 0.36x | 0.60x | 0.19x |
| cpstak | closure | 15.9 [15.6–16.3] | 15.4 [15.1–15.6] | 26.2 [25.9–26.2] | 10.8 [10.8–11.2] | 43.2 [42.9–43.5] | 0.37x | 0.36x | 0.61x | 0.25x |
| sum | iterative | 15.0 [14.8–15.2] | 15.2 [14.9–15.4] | 52.4 [51.6–52.4] | 36.3 [36.3–36.6] | 44.1 [43.9–44.2] | 0.34x | 0.35x | 1.19x | 0.82x |
| sumfp | iterative | 14.7 [14.3–14.7] | 18.0 [14.6–18.6] | 27.7 [27.4–28.0] | 8.88 [8.85–9.32] | 43.3 [43.2–43.6] | 0.34x | 0.42x | 0.64x | 0.21x |
| nqueens | backtrack | 25.1 [25.0–25.2] | 27.1 [26.9–27.2] | 121.3 [121.1–121.6] | 13.2 [12.9–14.1] | 44.3 [44.3–44.8] | 0.57x | 0.61x | 2.74x | 0.30x |
| fft | numeric | 23.6 [23.3–23.6] | 25.6 [25.4–25.7] | 56.1 [56.0–56.3] | 8.10 [8.00–8.73] | 44.0 [43.6–44.7] | 0.54x | 0.58x | 1.28x | 0.18x |
| mbrot | numeric | 18.8 [18.6–19.1] | 19.4 [19.0–19.6] | 44.5 [44.5–44.6] | 23.3 [22.9–23.3] | 44.1 [43.8–44.1] | 0.43x | 0.44x | 1.01x | 0.53x |
| ack | recursive | 27.0 [26.9–27.7] | 24.8 [24.4–25.1] | 115.5 [112.7–116.2] | 106.1 [106.0–107.2] | 55.4 [55.2–55.6] | 0.49x | 0.45x | 2.09x | 1.92x |

### AWFY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 16.1 [16.1–16.1] | 16.8 [16.7–16.9] | 32.7 [32.6–32.8] | 6.01 [5.81–6.97] | 43.6 [43.3–45.4] | 0.37x | 0.39x | 0.75x | 0.14x |
| permute | micro | 17.0 [16.7–17.0] | 17.2 [17.0–17.3] | 39.7 [39.5–40.0] | 6.98 [6.85–7.34] | 44.0 [44.0–44.1] | 0.38x | 0.39x | 0.90x | 0.16x |
| queens | micro | 21.9 [21.5–22.9] | 23.3 [23.3–23.3] | 39.2 [39.1–39.6] | 6.61 [6.49–7.04] | 44.0 [43.8–44.0] | 0.50x | 0.53x | 0.89x | 0.15x |
| towers | micro | 21.6 [21.4–21.7] | 22.3 [22.2–22.4] | 54.8 [54.6–55.4] | 7.69 [7.69–8.44] | 44.4 [44.2–44.6] | 0.49x | 0.50x | 1.23x | 0.17x |
| bounce | micro | 23.0 [22.9–23.1] | 25.4 [25.1–25.6] | 111.5 [111.5–112.0] | 6.92 [6.85–7.52] | 43.8 [43.8–44.5] | 0.52x | 0.58x | 2.55x | 0.16x |
| list | micro | 19.4 [19.2–19.4] | 18.9 [18.6–19.6] | 35.6 [35.5–35.9] | 6.42 [6.17–6.76] | 43.6 [43.4–43.7] | 0.44x | 0.43x | 0.82x | 0.15x |
| storage | micro | 17.6 [17.2–17.8] | 17.7 [17.5–17.8] | 106.7 [106.6–106.9] | 8.13 [8.00–8.89] | 44.1 [44.0–44.4] | 0.40x | 0.40x | 2.42x | 0.18x |
| mandelbrot | compute | 60.9 [60.8–61.5] | 60.9 [60.7–61.3] | 483.3 [482.3–485.1] | 871.2 [869.9–875.9] | 73.9 [73.7–75.5] | 0.82x | 0.82x | 6.54x | 11.8x |
| nbody | compute | 88.1 [87.2–88.2] | 78.6 [78.4–79.3] | 621.5 [617.8–622.3] | 164.9 [164.4–165.4] | 48.6 [48.2–49.7] | 1.81x | 1.62x | 12.8x | 3.39x |
| richards | macro | 472.9 [471.2–473.5] | 450.6 [448.6–452.0] | 1.25s [1.25s–1.26s] | 196.9 [195.8–197.5] | 89.7 [89.5–90.5] | 5.28x | 5.03x | 13.9x | 2.20x |
| json | macro | 51.0 [51.0–51.4] | 51.2 [51.1–51.5] | 226.5 [226.4–227.2] | 17.7 [17.6–18.8] | 46.4 [46.3–47.3] | 1.10x | 1.10x | 4.89x | 0.38x |
| deltablue | macro | 239.9 [239.1–240.7] | 189.2 [188.6–189.5] | 751.8 [750.6–755.0] | 106.0 [105.8–106.6] | 56.1 [55.8–56.3] | 4.28x | 3.37x | 13.4x | 1.89x |
| havlak | macro | 168.6 [168.5–169.1] | 155.8 [155.6–156.6] | 21.38s [21.33s–21.63s] | 3.23s [3.22s–3.24s] | 140.4 [139.6–225.4] | 1.20x | 1.11x | 152x | 23.0x |
| cd | macro | 650.1 [650.1–655.3] | 563.8 [563.3–565.4] | 5.59s [5.57s–5.63s] | 957.2 [956.0–960.2] | 79.8 [79.6–80.4] | 8.15x | 7.07x | 70.1x | 12.0x |

### BENG

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 26.5 [26.5–26.5] | 22.2 [22.2–22.3] | 63.3 [62.9–63.6] | 28.9 [28.6–29.4] | 44.9 [44.7–45.3] | 0.59x | 0.49x | 1.41x | 0.64x |
| fannkuch | permutation | 21.0 [20.5–21.1] | 24.3 [24.1–25.0] | 69.2 [68.8–69.9] | 12.5 [12.5–13.1] | 44.7 [44.5–45.3] | 0.47x | 0.54x | 1.55x | 0.28x |
| fasta | generation | 24.7 [24.6–24.8] | 27.6 [27.2–27.6] | 83.1 [83.0–83.1] | 14.1 [14.1–14.7] | 47.2 [47.1–47.6] | 0.52x | 0.58x | 1.76x | 0.30x |
| knucleotide | hashing | 29.8 [29.8–30.1] | 37.1 [36.9–37.1] | 196.9 [196.6–205.6] | 13.1 [12.9–13.7] | 45.9 [45.8–46.4] | 0.65x | 0.81x | 4.29x | 0.28x |
| pidigits | bignum | 19.7 [19.6–19.8] | 19.6 [19.5–19.9] | 35.0 [34.6–35.1] | 5.45 [5.23–5.87] | 42.4 [42.0–42.5] | 0.47x | 0.46x | 0.83x | 0.13x |
| regexredux | regex | 14.9 [14.7–15.2] | 14.9 [14.7–15.5] | 65.6 [64.8–65.8] | 11.1 [10.8–11.8] | 43.3 [42.9–46.8] | 0.34x | 0.35x | 1.52x | 0.26x |
| revcomp | string | 22.1 [21.8–22.3] | 22.6 [22.4–22.8] | 71.8 [71.7–72.2] | 7.92 [7.79–8.56] | 44.0 [43.5–44.2] | 0.50x | 0.51x | 1.63x | 0.18x |
| spectralnorm | numeric | 24.1 [23.9–24.2] | 26.0 [25.8–26.1] | 157.2 [157.0–157.2] | 69.0 [68.6–70.6] | 45.0 [44.8–45.2] | 0.54x | 0.58x | 3.49x | 1.53x |

### KOSTYA

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 243.7 [241.7–244.9] | 214.6 [214.0–215.2] | 2.90s [2.88s–2.98s] | 881.2 [880.0–883.8] | 76.4 [76.2–76.9] | 3.19x | 2.81x | 37.9x | 11.5x |
| matmul | numeric | 28.4 [27.9–28.7] | 29.8 [29.7–30.6] | 806.8 [805.0–807.5] | 542.7 [542.3–543.4] | 57.9 [57.0–58.0] | 0.49x | 0.51x | 13.9x | 9.36x |
| primes | numeric | 31.8 [31.6–32.1] | 19.3 [19.3–19.7] | 1.73s [1.71s–1.73s] | 100.1 [99.8–100.7] | 46.5 [46.1–46.6] | 0.68x | 0.42x | 37.2x | 2.15x |
| base64 | string | 37.8 [37.5–37.9] | 37.3 [37.0–37.6] | 558.6 [551.8–558.8] | 161.3 [161.2–167.1] | 59.8 [59.3–59.8] | 0.63x | 0.62x | 9.35x | 2.70x |
| levenshtein | string | 40.1 [40.1–40.3] | 31.5 [31.0–31.8] | 250.5 [249.8–251.9] | 59.2 [59.2–59.8] | 46.1 [46.0–46.3] | 0.87x | 0.68x | 5.43x | 1.28x |
| json_gen | data | 49.9 [49.2–51.9] | 35.3 [35.2–35.8] | 73.3 [72.2–74.0] | 25.1 [25.0–25.8] | 48.5 [48.1–48.8] | 1.03x | 0.73x | 1.51x | 0.52x |
| collatz | numeric | 327.5 [326.0–329.4] | 329.3 [327.7–333.1] | 7.01s [7.00s–7.03s] | 6.20s [6.20s–6.21s] | 1.46s [1.46s–1.47s] | 0.22x | 0.23x | 4.81x | 4.25x |

### LARCENY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 301.5 [301.2–301.8] | 210.9 [210.6–211.0] | 7.32s [7.31s–7.33s] | 2.17s [2.17s–2.17s] | 108.9 [108.5–110.2] | 2.77x | 1.94x | 67.2x | 19.9x |
| array1 | array | 16.3 [16.0–16.3] | 16.8 [16.8–16.8] | 76.7 [76.2–77.2] | 41.2 [40.9–41.7] | 44.0 [43.7–44.0] | 0.37x | 0.38x | 1.74x | 0.94x |
| deriv | symbolic | 53.6 [53.1–53.7] | 27.3 [27.1–27.3] | 109.4 [108.9–109.9] | 64.4 [64.0–65.5] | 46.4 [46.1–46.9] | 1.15x | 0.59x | 2.36x | 1.39x |
| diviter | iterative | 282.2 [282.0–282.3] | 282.6 [282.4–282.8] | 15.29s [15.28s–15.30s] | 26.59s [26.56s–26.59s] | 512.3 [512.2–512.3] | 0.55x | 0.55x | 29.8x | 51.9x |
| divrec | recursive | 21.8 [21.6–21.9] | 17.2 [17.1–17.3] | 41.8 [41.6–41.9] | 40.8 [40.6–42.1] | 49.5 [49.4–50.1] | 0.44x | 0.35x | 0.84x | 0.82x |
| gcbench | allocation | 213.5 [212.8–214.1] | 128.7 [128.3–129.1] | 822.6 [815.1–826.9] | 558.1 [551.1–559.6] | 64.9 [64.7–66.0] | 3.29x | 1.98x | 12.7x | 8.60x |
| paraffins | combinat | 28.7 [28.5–29.1] | 28.6 [28.4–28.7] | 42.4 [42.4–42.6] | 7.91 [7.72–8.71] | 43.4 [43.0–43.4] | 0.66x | 0.66x | 0.98x | 0.18x |
| pnpoly | numeric | 30.8 [30.2–37.8] | 31.5 [31.4–31.9] | 165.4 [165.3–169.4] | 206.2 [206.2–206.5] | 48.2 [47.8–48.5] | 0.64x | 0.65x | 3.43x | 4.28x |
| puzzle | search | 39.1 [38.8–39.8] | 34.9 [34.5–35.0] | 91.4 [91.4–91.8] | 34.6 [34.5–35.0] | 45.6 [45.1–45.7] | 0.86x | 0.77x | 2.00x | 0.76x |
| quicksort | sorting | 31.6 [31.4–31.6] | 22.3 [22.1–22.3] | 178.1 [177.8–178.4] | 24.7 [24.7–25.5] | 44.0 [43.4–44.4] | 0.72x | 0.51x | 4.04x | 0.56x |
| ray | numeric | 28.4 [28.4–28.8] | 21.1 [21.0–21.3] | 52.9 [52.6–53.0] | 19.2 [19.0–19.5] | 46.0 [45.7–46.1] | 0.62x | 0.46x | 1.15x | 0.42x |

### JetStream

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 135.6 [133.6–136.6] | 169.9 [169.7–171.1] | 540.5 [539.0–544.3] | 221.6 [220.7–222.4] | 63.3 [62.4–63.4] | 2.14x | 2.68x | 8.54x | 3.50x |
| navier_stokes | numeric | 176.7 [176.6–177.0] | 176.7 [176.4–177.7] | 561.6 [559.2–563.3] | 108.9 [108.9–109.2] | 59.2 [58.3–59.4] | 2.98x | 2.98x | 9.48x | 1.84x |
| splay | data | 374.9 [374.0–376.7] | 361.6 [357.8–366.0] | 1.22s [1.22s–1.22s] | 551.7 [551.4–557.3] | 91.9 [91.3–92.0] | 4.08x | 3.93x | 13.3x | 6.00x |
| hashmap | data | 156.4 [156.3–157.9] | 75.9 [75.7–76.1] | 1.73s [1.73s–1.73s] | 319.7 [318.8–323.8] | 58.7 [58.7–58.8] | 2.66x | 1.29x | 29.5x | 5.44x |
| crypto_sha1 | crypto | 91.8 [90.0–91.9] | 57.3 [56.9–58.0] | 434.8 [433.8–438.9] | 223.0 [222.9–223.7] | 51.6 [51.6–52.0] | 1.78x | 1.11x | 8.42x | 4.32x |
| raytrace3d | 3d | 136.2 [136.1–136.7] | 78.6 [78.4–78.9] | 728.6 [726.0–730.8] | 168.7 [168.4–170.5] | 61.4 [61.2–61.7] | 2.22x | 1.28x | 11.9x | 2.75x |

### Text

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 263.3 [263.0–264.1] | 162.7 [162.4–163.0] | 3.01s [3.00s–3.01s] | 615.1 [613.8–618.4] | 82.4 [82.3–83.2] | 3.20x | 1.98x | 36.6x | 7.47x |
| microdiff | data-diff | 85.7 [85.7–86.2] | 91.3 [90.7–91.3] | 1.31s [1.31s–1.32s] | 115.0 [113.6–115.3] | 59.0 [58.7–59.2] | 1.45x | 1.55x | 22.2x | 1.95x |
| hyphen | hyphenation | 117.4 [117.3–118.9] | 118.1 [118.0–118.5] | 2.79s [2.78s–2.81s] | 99.5 [99.3–100.6] | 56.6 [56.4–56.7] | 2.08x | 2.09x | 49.3x | 1.76x |
| prettier_ast | formatting | 1.05s [1.03s–1.11s] | 780.8 [778.0–784.2] | 5.21s [5.18s–5.24s] | 1.42s [1.42s–1.42s] | 143.5 [142.8–144.2] | 7.35x | 5.44x | 36.3x | 9.88x |
| text_search | search | 15.05s [15.05s–15.07s] | 1.85s [1.81s–1.89s] | 116.03s [115.22s–116.13s] | 38.65s [38.64s–38.67s] | 820.2 [818.7–821.7] | 18.4x | 2.25x | 141x | 47.1x |
| three_way_merge | merge | 3.42s [3.41s–3.43s] | 3.76s [3.76s–3.77s] | 15.21s [15.07s–15.23s] | 6.09s [6.08s–6.11s] | 1.00s [1.00s–1.01s] | 3.40x | 3.74x | 15.1x | 6.07x |
| log_pipeline | log-processing | 6.01s [6.01s–6.01s] | 6.22s [6.20s–6.24s] | 17.03s [16.98s–17.13s] | 9.46s [9.41s–9.54s] | 991.3 [986.0–998.0] | 6.06x | 6.27x | 17.2x | 9.54x |

