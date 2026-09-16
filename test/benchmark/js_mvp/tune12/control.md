# JS Tune12 frozen-control results

- **Date:** 2026-09-16
- **Platform:** Darwin arm64
- **Lambda commit:** `fd99875910e47cd5647d3510fecfba8bbf393520`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-fd99875910` (20,496,264 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 46.20s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 46.1s (batched 45.4s: sync 34.2s, async 11.3s; non-batched 0.7s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v24.7.0
- **QuickJS:** 2026-06-04
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Range notation:** per-row timing cells are `median [minimum–maximum]`; the complete ordered sample set is retained in the result JSON's status detail.
- **Engines in this report:** MIR (untyped), MIR (typed), LambdaJS, QuickJS
- **Results source:** `test/benchmark/js_mvp/tune12/control.json`
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

---

## Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed LambdaJS | Timed QuickJS | MIR (untyped)/Node geo | MIR (typed)/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | --- | --- | --- | --- |
| AWFY | 14 | 14 | 14 | 14 | 14 | --- | --- | --- | --- |
| BENG | 8 | 8 | 8 | 8 | 8 | --- | --- | --- | --- |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | --- | --- | --- | --- |
| LARCENY | 11 | 11 | 11 | 11 | 11 | --- | --- | --- | --- |
| JetStream | 6 | 6 | 6 | 6 | 6 | --- | --- | --- | --- |
| Text | 7 | 7 | 6 | 7 | 7 | --- | --- | --- | --- |
| **Overall** | 63 | 63 | 62 | 63 | 63 | --- | --- | --- | --- |

### Population Accounting

The first population is the only one comparable with Result38; the complete-suite figure includes the four subsequently added text rows. Do not compare geomeans across these populations.

| Population | Rows | MIR (typed)/Node geo | MIR (untyped)/Node geo | MIR (typed)/C2MIR geo | C2MIR matched rows |
|---|---:|---:|---:|---:|---:|
| v38-comparable (without the four text extensions) | 59 | --- | --- | --- | 0 |
| complete current suite | 63 | --- | --- | --- | 0 |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Notable Results

- Missing timings: **1** cells
- MIR (typed) missing: text/log_pipeline (exit_1)

### R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.45 [1.31–1.47] | 1.35 [1.35–1.52] | 16.1 [16.0–16.3] | 9.94 [9.75–10.3] | --- | --- | --- | --- |
| fibfp | recursive | 2.06 [1.96–2.08] | 1.08 [1.08–1.09] | 14.0 [13.7–14.2] | 8.71 [8.60–8.82] | --- | --- | --- | --- |
| tak | recursive | 0.111 [0.109–0.114] | 0.111 [0.109–0.113] | 0.316 [0.313–0.325] | 1.49 [1.47–1.51] | --- | --- | --- | --- |
| cpstak | closure | 0.266 [0.220–0.269] | 0.222 [0.222–0.226] | 0.641 [0.634–0.644] | 3.07 [3.01–3.61] | --- | --- | --- | --- |
| sum | iterative | 0.294 [0.287–0.340] | 0.308 [0.293–0.330] | 29.5 [29.2–30.0] | 11.2 [11.2–11.6] | --- | --- | --- | --- |
| sumfp | iterative | 0.078 [0.077–0.079] | 0.082 [0.079–0.088] | 2.96 [2.93–3.04] | 1.17 [0.978–1.27] | --- | --- | --- | --- |
| nqueens | backtrack | 1.50 [1.44–1.69] | 1.52 [1.50–1.52] | 45.9 [45.8–46.0] | 2.91 [2.89–3.00] | --- | --- | --- | --- |
| fft | numeric | 0.241 [0.240–0.255] | 0.144 [0.144–0.176] | 24.1 [23.2–25.6] | 1.54 [1.27–1.63] | --- | --- | --- | --- |
| mbrot | numeric | 0.845 [0.828–0.848] | 0.931 [0.928–1.04] | 18.4 [18.1–19.8] | 8.01 [7.32–8.18] | --- | --- | --- | --- |
| ack | recursive | 14.1 [13.3–14.7] | 10.8 [10.3–11.4] | 75.2 [74.6–76.2] | 50.3 [50.0–50.8] | --- | --- | --- | --- |

### AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.026 [0.026–0.031] | 0.028 [0.027–0.028] | 3.22 [3.02–3.68] | 0.341 [0.334–0.380] | --- | --- | --- | --- |
| permute | micro | 0.618 [0.582–0.696] | 0.115 [0.114–0.122] | 7.98 [7.73–7.99] | 0.885 [0.754–0.885] | --- | --- | --- | --- |
| queens | micro | 0.443 [0.421–0.445] | 0.227 [0.226–0.236] | 4.65 [4.23–5.00] | 0.655 [0.506–0.775] | --- | --- | --- | --- |
| towers | micro | 1.14 [1.12–1.25] | 0.330 [0.326–0.334] | 16.0 [15.9–16.1] | 1.11 [1.06–1.13] | --- | --- | --- | --- |
| bounce | micro | 0.062 [0.062–0.065] | 0.089 [0.088–0.091] | 3.79 [3.74–3.93] | 0.510 [0.412–0.520] | --- | --- | --- | --- |
| list | micro | 0.613 [0.606–0.635] | 0.149 [0.148–0.172] | 2.28 [2.12–2.40] | 0.460 [0.395–0.517] | --- | --- | --- | --- |
| storage | micro | 0.799 [0.721–0.827] | 0.362 [0.344–0.362] | 5.01 [4.92–5.49] | 0.894 [0.891–0.909] | --- | --- | --- | --- |
| mandelbrot | compute | 26.7 [26.6–26.8] | 27.2 [26.7–28.3] | 454.8 [453.2–457.3] | 410.1 [403.9–450.4] | --- | --- | --- | --- |
| nbody | compute | 52.4 [51.3–53.0] | 16.1 [14.2–16.3] | 547.4 [504.8–551.4] | 79.7 [74.2–83.4] | --- | --- | --- | --- |
| richards | macro | 293.1 [282.8–323.3] | 350.4 [344.7–369.5] | 1.02s [1.02s–1.02s] | 134.3 [134.3–134.6] | --- | --- | --- | --- |
| json | macro | 5.51 [5.50–5.73] | 5.64 [5.60–5.77] | 39.1 [37.2–42.1] | 6.82 [6.17–7.21] | --- | --- | --- | --- |
| deltablue | macro | 117.1 [116.5–120.1] | 38.3 [36.9–43.5] | 498.4 [454.3–500.2] | 59.1 [57.3–60.6] | --- | --- | --- | --- |
| havlak | macro | 71.2 [67.5–73.3] | 63.1 [61.5–71.2] | 19.57s [19.55s–19.61s] | 1.86s [1.85s–1.86s] | --- | --- | --- | --- |
| cd | macro | 546.0 [537.1–553.5] | 492.1 [438.1–495.9] | 5.01s [4.96s–5.01s] | 522.8 [469.0–526.3] | --- | --- | --- | --- |

### BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 7.57 [7.56–7.62] | 2.11 [1.97–2.11] | 32.0 [29.3–34.4] | 12.1 [11.5–14.6] | --- | --- | --- | --- |
| fannkuch | permutation | 0.328 [0.302–0.437] | 0.394 [0.365–0.410] | 38.0 [38.0–38.6] | 3.87 [3.36–4.20] | --- | --- | --- | --- |
| fasta | generation | 0.854 [0.801–0.859] | 0.995 [0.796–0.997] | 40.5 [40.4–40.7] | 3.92 [3.74–3.98] | --- | --- | --- | --- |
| knucleotide | hashing | 3.95 [3.85–4.14] | 0.207 [0.197–0.244] | 153.1 [149.9–153.4] | 4.68 [4.02–4.75] | --- | --- | --- | --- |
| pidigits | bignum | 0.387 [0.377–0.415] | 0.302 [0.298–0.310] | 0.458 [0.445–0.459] | 0.061 [0.057–0.063] | --- | --- | --- | --- |
| regexredux | regex | 1.15 [1.15–1.16] | 1.17 [1.16–1.17] | 27.9 [27.6–29.3] | 2.12 [2.10–2.34] | --- | --- | --- | --- |
| revcomp | string | 1.16 [1.15–1.16] | 1.25 [1.09–1.30] | 37.9 [37.3–39.5] | 1.65 [1.53–1.70] | --- | --- | --- | --- |
| spectralnorm | numeric | 1.91 [1.75–1.96] | 1.83 [1.72–1.84] | 121.4 [110.3–121.6] | 27.7 [27.6–27.8] | --- | --- | --- | --- |

### KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 191.5 [185.5–197.8] | 185.3 [183.5–189.1] | 2.65s [2.64s–2.65s] | 457.9 [420.1–468.3] | --- | --- | --- | --- |
| matmul | numeric | 6.43 [6.35–6.74] | 6.46 [5.88–6.49] | 778.0 [754.4–789.0] | 203.5 [194.0–229.9] | --- | --- | --- | --- |
| primes | numeric | 13.7 [13.5–13.9] | 2.42 [2.29–2.42] | 1.68s [1.68s–1.73s] | 41.9 [41.7–45.0] | --- | --- | --- | --- |
| base64 | string | 18.3 [18.1–18.8] | 9.69 [9.64–10.0] | 479.3 [438.7–492.0] | 69.1 [68.8–72.0] | --- | --- | --- | --- |
| levenshtein | string | 10.4 [10.4–11.3] | 5.39 [5.05–5.55] | 190.2 [186.5–202.7] | 29.5 [29.1–29.7] | --- | --- | --- | --- |
| json_gen | data | 25.4 [24.8–25.5] | 12.0 [11.4–12.5] | 39.8 [39.0–40.9] | 11.8 [11.6–12.7] | --- | --- | --- | --- |
| collatz | numeric | 289.2 [285.1–309.5] | 288.0 [287.6–317.9] | 6.34s [6.24s–6.40s] | 3.08s [3.02s–3.09s] | --- | --- | --- | --- |

### LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 242.6 [239.4–263.5] | 155.6 [150.5–159.0] | 7.33s [7.23s–7.33s] | 1.08s [1.04s–1.09s] | --- | --- | --- | --- |
| array1 | array | 0.840 [0.780–1.04] | 0.778 [0.666–0.938] | 45.7 [45.3–53.7] | 12.2 [12.1–12.3] | --- | --- | --- | --- |
| deriv | symbolic | 25.5 [25.2–25.7] | 6.42 [6.40–6.49] | 76.9 [73.8–78.3] | 26.5 [25.8–27.6] | --- | --- | --- | --- |
| diviter | iterative | 284.8 [284.7–284.8] | 284.0 [282.6–284.9] | 15.44s [15.24s–15.46s] | 10.11s [10.09s–10.14s] | --- | --- | --- | --- |
| divrec | recursive | 6.76 [6.69–6.81] | 1.44 [1.29–1.48] | 20.2 [19.8–21.7] | 26.7 [25.9–27.8] | --- | --- | --- | --- |
| gcbench | allocation | 176.6 [176.4–184.9] | 84.4 [84.2–85.1] | 766.3 [707.5–777.7] | 240.6 [231.3–243.9] | --- | --- | --- | --- |
| paraffins | combinat | 0.193 [0.190–0.196] | 0.233 [0.196–0.236] | 4.13 [4.08–5.00] | 1.39 [1.18–1.43] | --- | --- | --- | --- |
| pnpoly | numeric | 12.7 [12.4–13.2] | 12.0 [11.9–12.1] | 121.5 [119.9–133.9] | 85.7 [85.1–89.8] | --- | --- | --- | --- |
| puzzle | search | 16.9 [16.6–16.9] | 14.4 [14.4–14.7] | 62.4 [62.2–63.2] | 13.9 [13.2–15.0] | --- | --- | --- | --- |
| quicksort | sorting | 12.4 [12.3–13.2] | 0.842 [0.834–0.879] | 130.6 [130.3–130.9] | 8.57 [8.50–8.73] | --- | --- | --- | --- |
| ray | numeric | 0.239 [0.238–0.240] | 0.238 [0.234–0.286] | 19.1 [19.0–20.6] | 6.73 [5.81–6.97] | --- | --- | --- | --- |

### JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 9.60 [9.50–9.61] | 9.57 [9.48–9.62] | 384.9 [374.8–406.8] | 114.9 [114.1–120.3] | --- | --- | --- | --- |
| navier_stokes | numeric | 94.8 [85.3–95.0] | 97.2 [89.3–104.5] | 349.9 [318.7–353.2] | 36.7 [36.7–36.7] | --- | --- | --- | --- |
| splay | data | 293.4 [289.1–294.0] | 288.3 [288.0–292.0] | 487.5 [481.8–493.5] | 50.2 [47.8–57.4] | --- | --- | --- | --- |
| hashmap | data | 89.7 [89.1–90.0] | 49.4 [49.2–51.2] | 1.51s [1.47s–1.51s] | 171.6 [161.5–173.5] | --- | --- | --- | --- |
| crypto_sha1 | crypto | 62.2 [61.1–81.9] | 28.0 [27.3–30.4] | 339.1 [339.0–374.5] | 119.1 [118.9–121.1] | --- | --- | --- | --- |
| raytrace3d | 3d | 63.8 [63.5–64.8] | 23.8 [23.5–23.8] | 589.4 [540.6–597.5] | 70.4 [67.7–71.2] | --- | --- | --- | --- |

### Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 235.0 [234.9–240.7] | 124.1 [123.8–134.1] | 2.71s [2.66s–2.73s] | 304.4 [304.2–305.3] | --- | --- | --- | --- |
| microdiff | data-diff | 52.2 [49.9–53.5] | 51.1 [50.9–51.3] | 1.25s [1.18s–1.25s] | 50.0 [50.0–50.5] | --- | --- | --- | --- |
| hyphen | hyphenation | 62.4 [61.1–62.4] | 67.5 [67.1–68.4] | 507.2 [496.0–544.6] | 31.0 [28.6–31.3] | --- | --- | --- | --- |
| prettier_ast | formatting | 968.2 [908.6–975.1] | 797.3 [741.1–807.4] | 4.65s [4.62s–4.66s] | 776.1 [744.9–789.2] | --- | --- | --- | --- |
| text_search | search | 14.23s [14.21s–14.43s] | 1.59s [1.57s–1.63s] | 106.89s [106.25s–107.12s] | 14.87s [14.77s–14.92s] | --- | --- | --- | --- |
| three_way_merge | merge | 3.56s [3.53s–3.58s] | 3.51s [3.42s–3.54s] | 14.75s [14.72s–14.76s] | 2.95s [2.87s–2.95s] | --- | --- | --- | --- |
| log_pipeline | log-processing | 5.76s [5.70s–5.80s] | --- | 15.52s [15.50s–15.60s] | 4.66s [4.58s–4.72s] | --- | --- | --- | --- |
