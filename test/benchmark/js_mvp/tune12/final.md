# JS Tune12 full-runtime results

- **Date:** 2026-09-16
- **Platform:** Darwin arm64
- **Lambda commit:** `fd99875910e47cd5647d3510fecfba8bbf393520`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-fd99875910` (20,530,552 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 51.30s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 51.2s (batched 50.5s: sync 37.9s, async 12.5s; non-batched 0.8s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v24.7.0
- **QuickJS:** 2026-06-04
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Range notation:** per-row timing cells are `median [minimum–maximum]`; the complete ordered sample set is retained in the result JSON's status detail.
- **Engines in this report:** MIR (untyped), MIR (typed), LambdaJS, QuickJS
- **Results source:** `test/benchmark/js_mvp/tune12/final.json`
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
| fib | recursive | 1.19 [1.13–1.27] | 1.30 [1.26–1.33] | 1.36 [1.36–1.56] | 9.37 [9.16–10.7] | --- | --- | --- | --- |
| fibfp | recursive | 2.68 [2.38–2.71] | 1.28 [1.25–1.28] | 1.45 [1.38–1.50] | 10.8 [10.0–10.9] | --- | --- | --- | --- |
| tak | recursive | 0.129 [0.113–0.135] | 0.112 [0.111–0.122] | 0.322 [0.319–0.327] | 1.51 [1.46–1.66] | --- | --- | --- | --- |
| cpstak | closure | 0.224 [0.223–0.229] | 0.222 [0.220–0.225] | 0.738 [0.716–0.867] | 3.71 [3.12–3.75] | --- | --- | --- | --- |
| sum | iterative | 0.287 [0.274–0.297] | 0.305 [0.255–0.307] | 0.678 [0.671–0.849] | 9.76 [9.75–9.81] | --- | --- | --- | --- |
| sumfp | iterative | 0.068 [0.066–0.075] | 0.068 [0.067–0.069] | 0.071 [0.071–0.073] | 0.993 [0.992–1.01] | --- | --- | --- | --- |
| nqueens | backtrack | 1.33 [1.24–1.40] | 1.74 [1.63–2.25] | 55.6 [49.3–57.7] | 3.75 [3.04–3.93] | --- | --- | --- | --- |
| fft | numeric | 0.295 [0.293–0.304] | 0.185 [0.177–0.199] | 25.1 [25.0–25.2] | 1.37 [1.27–1.65] | --- | --- | --- | --- |
| mbrot | numeric | 0.632 [0.620–0.732] | 0.811 [0.809–0.815] | 10.9 [10.9–11.0] | 6.65 [6.63–6.82] | --- | --- | --- | --- |
| ack | recursive | 11.9 [11.9–12.0] | 10.00 [9.91–10.3] | 11.0 [10.9–13.4] | 58.6 [57.5–61.3] | --- | --- | --- | --- |

### AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.036 [0.032–0.045] | 0.030 [0.029–0.030] | 0.448 [0.418–0.473] | 0.333 [0.283–0.370] | --- | --- | --- | --- |
| permute | micro | 0.646 [0.631–0.734] | 0.109 [0.096–0.119] | 5.39 [5.26–6.45] | 0.738 [0.733–0.759] | --- | --- | --- | --- |
| queens | micro | 0.383 [0.376–0.392] | 0.200 [0.182–0.204] | 3.02 [3.02–3.08] | 0.527 [0.502–0.556] | --- | --- | --- | --- |
| towers | micro | 1.21 [1.17–1.23] | 0.349 [0.331–0.808] | 17.4 [17.2–18.7] | 1.31 [1.22–1.49] | --- | --- | --- | --- |
| bounce | micro | 0.122 [0.087–0.173] | 0.105 [0.091–0.107] | 3.85 [3.28–3.92] | 0.413 [0.412–0.416] | --- | --- | --- | --- |
| list | micro | 0.575 [0.570–0.587] | 0.132 [0.132–0.146] | 1.73 [1.73–1.74] | 0.390 [0.385–0.393] | --- | --- | --- | --- |
| storage | micro | 0.661 [0.636–0.886] | 0.343 [0.316–0.345] | 4.99 [4.68–5.18] | 1.01 [0.914–1.01] | --- | --- | --- | --- |
| mandelbrot | compute | 32.5 [32.0–35.0] | 31.8 [31.1–32.9] | 238.7 [235.1–250.0] | 447.4 [424.0–448.3] | --- | --- | --- | --- |
| nbody | compute | 45.7 [45.4–61.8] | 12.6 [11.4–13.3] | 538.6 [485.6–540.7] | 82.0 [79.1–85.7] | --- | --- | --- | --- |
| richards | macro | 311.6 [291.9–335.2] | 369.5 [338.1–372.5] | 1.04s [1.01s–1.06s] | 134.0 [126.6–142.7] | --- | --- | --- | --- |
| json | macro | 4.92 [4.89–5.08] | 5.77 [5.67–5.82] | 42.0 [41.3–62.2] | 7.39 [7.04–7.50] | --- | --- | --- | --- |
| deltablue | macro | 109.6 [108.0–114.5] | 43.3 [39.3–43.4] | 502.2 [470.1–532.1] | 61.5 [57.8–86.8] | --- | --- | --- | --- |
| havlak | macro | 78.9 [67.0–81.0] | 71.9 [67.3–76.4] | 19.27s [19.12s–19.80s] | 1.86s [1.85s–1.90s] | --- | --- | --- | --- |
| cd | macro | 564.0 [519.9–568.5] | 476.8 [447.9–526.3] | 4.98s [4.97s–4.98s] | 523.5 [513.6–524.2] | --- | --- | --- | --- |

### BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 9.24 [9.17–9.73] | 2.06 [2.02–2.06] | 28.0 [27.9–28.2] | 9.92 [9.87–10.1] | --- | --- | --- | --- |
| fannkuch | permutation | 0.292 [0.280–0.601] | 0.311 [0.306–0.316] | 38.5 [35.6–38.8] | 4.15 [3.37–4.28] | --- | --- | --- | --- |
| fasta | generation | 0.732 [0.694–0.742] | 1.00 [0.983–1.07] | 47.6 [47.1–48.4] | 3.94 [3.73–4.03] | --- | --- | --- | --- |
| knucleotide | hashing | 5.04 [4.20–5.09] | 0.220 [0.212–0.227] | 19.0 [18.8–19.0] | 3.97 [3.96–4.00] | --- | --- | --- | --- |
| pidigits | bignum | 0.305 [0.286–0.309] | 0.305 [0.299–0.306] | 0.462 [0.444–0.498] | 0.062 [0.058–0.071] | --- | --- | --- | --- |
| regexredux | regex | 1.32 [1.20–1.56] | 1.35 [1.19–1.44] | 9.54 [9.53–10.4] | 2.17 [2.11–2.37] | --- | --- | --- | --- |
| revcomp | string | 1.32 [1.28–1.50] | 1.23 [1.15–1.34] | 35.9 [35.8–36.4] | 1.48 [1.46–1.50] | --- | --- | --- | --- |
| spectralnorm | numeric | 1.56 [1.53–1.60] | 1.57 [1.55–1.61] | 110.6 [110.5–113.5] | 29.2 [28.2–29.4] | --- | --- | --- | --- |

### KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 213.1 [210.3–213.3] | 170.7 [170.6–178.6] | 2.66s [2.65s–2.70s] | 468.5 [466.3–477.7] | --- | --- | --- | --- |
| matmul | numeric | 6.63 [6.37–6.66] | 5.34 [5.29–5.38] | 705.1 [661.5–719.2] | 199.2 [197.5–204.5] | --- | --- | --- | --- |
| primes | numeric | 15.4 [15.1–16.0] | 2.66 [2.57–3.18] | 1.67s [1.66s–1.74s] | 44.8 [41.4–45.5] | --- | --- | --- | --- |
| base64 | string | 17.9 [16.7–18.2] | 7.95 [7.86–8.04] | 504.9 [468.7–510.3] | 71.8 [70.5–74.0] | --- | --- | --- | --- |
| levenshtein | string | 9.59 [9.09–11.5] | 5.50 [5.29–5.72] | 198.1 [182.4–218.1] | 28.3 [28.2–30.8] | --- | --- | --- | --- |
| json_gen | data | 25.4 [25.0–25.4] | 12.5 [12.0–12.5] | 35.1 [34.6–40.7] | 9.85 [9.76–9.85] | --- | --- | --- | --- |
| collatz | numeric | 293.0 [285.1–324.8] | 289.8 [285.5–316.4] | 2.36s [2.34s–2.38s] | 3.03s [2.98s–3.04s] | --- | --- | --- | --- |

### LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 209.6 [206.2–222.8] | 144.9 [143.8–146.6] | 7.22s [7.19s–7.28s] | 1.05s [1.02s–1.10s] | --- | --- | --- | --- |
| array1 | array | 0.774 [0.774–0.810] | 0.780 [0.780–0.886] | 48.5 [45.6–48.7] | 14.2 [13.9–14.9] | --- | --- | --- | --- |
| deriv | symbolic | 28.8 [28.5–35.7] | 8.22 [8.08–8.53] | 70.3 [70.0–80.1] | 23.0 [22.9–23.1] | --- | --- | --- | --- |
| diviter | iterative | 274.7 [247.6–289.0] | 253.4 [246.1–279.0] | 12.79s [12.76s–12.88s] | 10.02s [9.86s–10.04s] | --- | --- | --- | --- |
| divrec | recursive | 5.54 [5.31–5.78] | 1.16 [1.16–1.16] | 16.4 [14.0–16.4] | 26.4 [26.1–27.2] | --- | --- | --- | --- |
| gcbench | allocation | 203.0 [200.9–207.6] | 75.1 [72.5–78.6] | 736.3 [708.8–752.8] | 261.8 [259.9–269.2] | --- | --- | --- | --- |
| paraffins | combinat | 0.206 [0.197–0.206] | 0.167 [0.166–0.180] | 3.54 [3.50–3.56] | 1.14 [1.13–1.18] | --- | --- | --- | --- |
| pnpoly | numeric | 11.1 [10.5–13.6] | 10.9 [10.1–11.2] | 130.0 [129.3–133.1] | 99.6 [95.5–99.9] | --- | --- | --- | --- |
| puzzle | search | 20.2 [19.5–20.3] | 12.5 [11.8–14.2] | 26.7 [26.7–26.8] | 11.8 [11.8–11.9] | --- | --- | --- | --- |
| quicksort | sorting | 10.3 [10.2–10.4] | 0.717 [0.695–0.776] | 135.4 [133.2–144.9] | 10.8 [9.84–11.1] | --- | --- | --- | --- |
| ray | numeric | 0.267 [0.266–0.271] | 0.279 [0.229–0.294] | 15.1 [14.4–16.2] | 5.63 [5.54–5.67] | --- | --- | --- | --- |

### JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 10.6 [10.4–11.0] | 8.69 [8.26–9.35] | 325.5 [303.9–328.1] | 114.7 [105.9–116.9] | --- | --- | --- | --- |
| navier_stokes | numeric | 82.4 [82.0–82.5] | 94.0 [92.7–94.1] | 200.9 [190.2–203.6] | 38.3 [36.3–38.4] | --- | --- | --- | --- |
| splay | data | 266.5 [261.2–290.1] | 264.7 [257.0–277.0] | 473.7 [449.2–477.6] | 48.3 [48.1–52.2] | --- | --- | --- | --- |
| hashmap | data | 82.8 [82.5–84.1] | 49.5 [48.9–50.4] | 1.45s [1.45s–1.45s] | 169.3 [164.0–177.1] | --- | --- | --- | --- |
| crypto_sha1 | crypto | 62.5 [61.3–63.4] | 29.1 [25.6–29.4] | 380.0 [352.5–381.0] | 118.5 [114.0–121.4] | --- | --- | --- | --- |
| raytrace3d | 3d | 59.1 [57.8–59.7] | 26.5 [26.3–26.6] | 536.7 [507.5–544.9] | 67.6 [67.3–69.1] | --- | --- | --- | --- |

### Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | LambdaJS (ms) | QuickJS (ms) | MIR (untyped)/Node | MIR (typed)/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 211.2 [207.9–216.6] | 135.6 [124.6–136.1] | 2.52s [2.50s–2.52s] | 277.1 [272.4–284.5] | --- | --- | --- | --- |
| microdiff | data-diff | 52.7 [51.5–53.0] | 61.1 [60.9–61.8] | 1.22s [1.22s–1.25s] | 58.2 [57.9–59.4] | --- | --- | --- | --- |
| hyphen | hyphenation | 63.8 [61.0–72.0] | 63.9 [63.5–69.2] | 582.3 [581.9–601.4] | 25.9 [25.7–26.3] | --- | --- | --- | --- |
| prettier_ast | formatting | 940.8 [936.0–975.0] | 783.7 [745.5–812.6] | 4.76s [4.69s–4.79s] | 780.0 [740.4–808.7] | --- | --- | --- | --- |
| text_search | search | 13.98s [13.93s–14.00s] | 1.60s [1.57s–1.64s] | 96.10s [95.83s–97.03s] | 14.74s [14.72s–14.80s] | --- | --- | --- | --- |
| three_way_merge | merge | 3.60s [3.58s–3.63s] | 3.51s [3.50s–3.55s] | 14.61s [14.55s–14.62s] | 2.94s [2.93s–2.95s] | --- | --- | --- | --- |
| log_pipeline | log-processing | 5.70s [5.66s–5.74s] | --- | 15.14s [15.10s–15.18s] | 4.62s [4.55s–4.63s] | --- | --- | --- | --- |
