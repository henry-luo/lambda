# LambdaJS Tune12 Phase 2

- **Date:** 2026-09-16
- **Platform:** Darwin arm64
- **Lambda commit:** `763269c4fbc6df4ccecd5e7a0991dcebc20f0a4a`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-763269c4fb` (20,547,672 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 50.40s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 50.3s (batched 49.6s: sync 37.0s, async 12.6s; non-batched 0.7s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v24.7.0
- **QuickJS:** 2026-06-04
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Range notation:** per-row timing cells are `median [minimum–maximum]`; the complete ordered sample set is retained in the result JSON's status detail.
- **Engines in this report:** LambdaJS, QuickJS
- **Results source:** `test/benchmark/js_mvp/tune12/phase2/final_phase2.json`

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

---

## Summary

| Suite | Total | Timed LambdaJS | Timed QuickJS | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | --- | --- |
| AWFY | 14 | 14 | 14 | --- | --- |
| BENG | 8 | 8 | 8 | --- | --- |
| KOSTYA | 7 | 7 | 7 | --- | --- |
| LARCENY | 11 | 11 | 11 | --- | --- |
| JetStream | 6 | 6 | 6 | --- | --- |
| Text | 7 | 7 | 7 | --- | --- |
| **Overall** | 63 | 63 | 63 | --- | --- |

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

- Missing timings: **0** cells

### R7RS

| Benchmark | Category | LambdaJS (ms) | QuickJS (ms) | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|
| fib | recursive | 1.33 [1.32–1.40] | 9.74 [9.41–11.0] | --- | --- |
| fibfp | recursive | 1.74 [1.31–1.89] | 9.74 [9.25–10.3] | --- | --- |
| tak | recursive | 0.323 [0.318–0.325] | 1.54 [1.52–1.65] | --- | --- |
| cpstak | closure | 0.740 [0.650–0.800] | 3.32 [3.23–3.45] | --- | --- |
| sum | iterative | 0.833 [0.776–0.833] | 11.4 [11.2–11.8] | --- | --- |
| sumfp | iterative | 0.082 [0.077–0.096] | 1.07 [1.05–1.18] | --- | --- |
| nqueens | backtrack | 33.1 [33.1–39.5] | 3.03 [3.01–3.08] | --- | --- |
| fft | numeric | 5.53 [5.21–5.92] | 1.60 [1.39–1.72] | --- | --- |
| mbrot | numeric | 13.4 [12.9–13.6] | 7.12 [7.03–7.21] | --- | --- |
| ack | recursive | 11.1 [10.8–11.1] | 51.4 [51.2–60.9] | --- | --- |

### AWFY

| Benchmark | Category | LambdaJS (ms) | QuickJS (ms) | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|
| sieve | micro | 0.201 [0.201–0.207] | 0.286 [0.280–0.297] | --- | --- |
| permute | micro | 5.53 [5.34–5.75] | 0.748 [0.738–0.754] | --- | --- |
| queens | micro | 3.11 [3.11–3.29] | 0.509 [0.499–0.558] | --- | --- |
| towers | micro | 12.6 [11.3–13.3] | 1.13 [1.11–1.41] | --- | --- |
| bounce | micro | 3.57 [3.55–3.72] | 0.551 [0.505–0.557] | --- | --- |
| list | micro | 2.28 [2.17–2.35] | 0.477 [0.392–0.526] | --- | --- |
| storage | micro | 4.22 [4.15–4.34] | 0.923 [0.879–0.947] | --- | --- |
| mandelbrot | compute | 178.1 [178.1–199.7] | 434.4 [401.1–459.9] | --- | --- |
| nbody | compute | 508.4 [486.5–531.2] | 75.1 [72.5–75.9] | --- | --- |
| richards | macro | 972.2 [894.8–978.4] | 119.3 [119.1–123.7] | --- | --- |
| json | macro | 39.2 [37.4–39.2] | 7.22 [7.09–7.39] | --- | --- |
| deltablue | macro | 442.6 [409.0–459.6] | 52.4 [52.0–52.4] | --- | --- |
| havlak | macro | 15.93s [15.80s–15.94s] | 1.97s [1.94s–1.97s] | --- | --- |
| cd | macro | 3.56s [3.55s–3.58s] | 520.0 [506.9–534.2] | --- | --- |

### BENG

| Benchmark | Category | LambdaJS (ms) | QuickJS (ms) | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|
| binarytrees | allocation | 33.9 [33.3–33.9] | 12.4 [11.9–12.9] | --- | --- |
| fannkuch | permutation | 32.5 [32.4–32.7] | 3.32 [3.31–3.41] | --- | --- |
| fasta | generation | 25.7 [25.6–25.7] | 3.81 [3.50–3.86] | --- | --- |
| knucleotide | hashing | 19.7 [19.5–20.6] | 4.40 [4.35–5.04] | --- | --- |
| pidigits | bignum | 0.487 [0.447–0.518] | 0.059 [0.057–0.065] | --- | --- |
| regexredux | regex | 9.31 [8.46–9.85] | 2.43 [2.30–2.90] | --- | --- |
| revcomp | string | 27.8 [27.0–39.1] | 1.55 [1.44–1.92] | --- | --- |
| spectralnorm | numeric | 101.3 [89.5–101.9] | 28.2 [28.2–28.5] | --- | --- |

### KOSTYA

| Benchmark | Category | LambdaJS (ms) | QuickJS (ms) | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|
| brainfuck | interpreter | 1.86s [1.84s–1.87s] | 441.9 [436.4–476.2] | --- | --- |
| matmul | numeric | 454.1 [442.6–507.5] | 229.2 [225.7–229.3] | --- | --- |
| primes | numeric | 94.1 [85.7–99.0] | 38.5 [36.9–38.9] | --- | --- |
| base64 | string | 475.4 [444.1–485.3] | 64.9 [62.9–72.9] | --- | --- |
| levenshtein | string | 187.5 [184.4–194.3] | 27.4 [26.9–27.5] | --- | --- |
| json_gen | data | 41.7 [40.6–55.1] | 12.1 [11.9–13.1] | --- | --- |
| collatz | numeric | 2.33s [2.33s–2.34s] | 3.05s [2.99s–3.05s] | --- | --- |

### LARCENY

| Benchmark | Category | LambdaJS (ms) | QuickJS (ms) | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|
| triangl | search | 7.26s [7.25s–7.28s] | 1.05s [1.05s–1.07s] | --- | --- |
| array1 | array | 47.5 [46.6–47.8] | 15.1 [14.0–15.5] | --- | --- |
| deriv | symbolic | 72.4 [71.8–74.5] | 23.6 [23.1–23.7] | --- | --- |
| diviter | iterative | 12.75s [12.61s–12.85s] | 9.94s [9.89s–10.07s] | --- | --- |
| divrec | recursive | 14.2 [13.9–14.3] | 23.4 [23.3–23.4] | --- | --- |
| gcbench | allocation | 720.0 [716.0–779.9] | 267.3 [264.6–268.8] | --- | --- |
| paraffins | combinat | 3.66 [3.59–4.10] | 1.19 [1.17–1.45] | --- | --- |
| pnpoly | numeric | 127.7 [127.3–130.7] | 89.7 [87.7–91.4] | --- | --- |
| puzzle | search | 24.5 [24.4–25.8] | 13.2 [12.4–14.3] | --- | --- |
| quicksort | sorting | 139.5 [138.2–139.9] | 10.5 [9.47–10.6] | --- | --- |
| ray | numeric | 7.37 [6.95–8.27] | 5.83 [5.51–6.41] | --- | --- |

### JetStream

| Benchmark | Category | LambdaJS (ms) | QuickJS (ms) | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|
| cube3d | 3d | 298.2 [287.9–303.4] | 112.5 [106.6–114.8] | --- | --- |
| navier_stokes | numeric | 206.7 [183.7–209.2] | 32.9 [32.7–33.0] | --- | --- |
| splay | data | 312.0 [297.0–378.3] | 53.5 [53.2–53.9] | --- | --- |
| hashmap | data | 1.27s [1.26s–1.33s] | 153.2 [152.4–154.5] | --- | --- |
| crypto_sha1 | crypto | 382.8 [352.4–396.8] | 106.4 [105.8–124.6] | --- | --- |
| raytrace3d | 3d | 485.0 [462.7–517.3] | 69.7 [67.5–71.0] | --- | --- |

### Text

| Benchmark | Category | LambdaJS (ms) | QuickJS (ms) | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|
| fast_diff | text-diff | 2.11s [2.11s–2.14s] | 284.3 [274.6–315.8] | --- | --- |
| microdiff | data-diff | 961.4 [929.0–984.4] | 62.2 [58.6–62.6] | --- | --- |
| hyphen | hyphenation | 595.9 [578.5–610.4] | 32.7 [29.4–32.9] | --- | --- |
| prettier_ast | formatting | 4.60s [4.56s–4.62s] | 765.0 [733.3–790.9] | --- | --- |
| text_search | search | 53.83s [53.64s–54.43s] | 14.66s [14.57s–14.68s] | --- | --- |
| three_way_merge | merge | 12.63s [12.62s–12.72s] | 2.92s [2.84s–2.92s] | --- | --- |
| log_pipeline | log-processing | 14.31s [14.26s–14.34s] | 4.56s [4.55s–4.61s] | --- | --- |
