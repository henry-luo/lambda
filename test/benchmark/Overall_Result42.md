# Lambda Benchmark Results: Result42

- **Date:** 2026-09-10
- **Platform:** Darwin arm64
- **Lambda commit:** `584748bc54e209e220ca4a73cd1f29fcaf67523f`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-v42-584748bc54` (19,448,312 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 56.50s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 56.4s (batched 55.5s: sync 42.4s, async 13.0s; non-batched 0.9s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Range notation:** per-row timing cells are `median [minimum–maximum]`; the complete ordered sample set is retained in the result JSON's status detail.
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v42.json`
- **Separately measured:** LambdaJS measured on 2026-09-10, 3 run(s) from `temp/benchmark_v42_ljs5_patch.json` on Lambda commit `584748bc54`. Targeted five-row LambdaJS refresh after runtime fixes; measured with cached release test/benchmark/exe/lambda-v42-584748bc54-ljs5fix (sha256 336a5820e67412920cf995dec2c38cc23d0d8903c2c563c960fc361e445bd418). All other Result42 cells remain from the original 63-row guarded snapshot.
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Part 1 — Execution time (self-reported)

Each engine's own `__TIMING__` figure: the timed workload only, with startup and compilation outside the measured region. This is the historical series, comparable back through Result18, and the MIR columns pin `LAMBDA_TIER=jit`.

### Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 0.45x | 0.50x | 0.19x | 5.46x | 6.56x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 1.59x | 1.13x | 0.09x | 17.2x | 5.26x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 0.39x | 0.36x | 0.10x | 5.79x | 1.68x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 2.11x | 1.92x | 0.23x | 28.6x | 11.9x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 1.69x | 1.02x | 0.33x | 13.8x | 13.2x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 6.78x | 5.01x | 0.29x | 60.5x | 12.0x |
| Text | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 8.05x | 7.21x | 0.47x | 57.4x | 12.8x |
| **Overall** | 63 | 63 | 63 | 63 | 63 | 63 | 63 | 1.56x | 1.27x | 0.19x | 16.4x | 7.24x |

### Population Accounting

The first population is the only one comparable with Result38; the complete-suite figure includes the four subsequently added text rows. Do not compare geomeans across these populations.

| Population | Rows | MIR (typed)/Node geo | MIR (untyped)/Node geo | MIR (typed)/C2MIR geo | C2MIR matched rows |
|---|---:|---:|---:|---:|---:|
| v38-comparable (without the four text extensions) | 59 | 1.12x | 1.39x | 6.35x | 59 |
| complete current suite | 63 | 1.27x | 1.56x | 6.53x | 63 |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 6.53x over 63 of 63 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| text/prettier_ast | 2.52s | 41.6 | 60.5x |
| jetstream/cube3d | 29.2 | 0.523 | 55.9x |
| text/hyphen | 74.3 | 1.49 | 49.9x |
| awfy/deltablue | 48.3 | 1.15 | 42.0x |
| awfy/cd | 547.5 | 15.1 | 36.2x |
| awfy/havlak | 61.1 | 1.83 | 33.4x |
| awfy/richards | 724.0 | 29.1 | 24.9x |
| jetstream/hashmap | 69.6 | 2.82 | 24.7x |
| text/microdiff | 56.7 | 2.65 | 21.4x |
| text/fast_diff | 276.6 | 13.1 | 21.1x |
| jetstream/splay | 388.1 | 18.7 | 20.7x |
| r7rs/nqueens | 2.61 | 0.133 | 19.6x |

---

### Notable Results

- Missing timings: **0** cells

#### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| kostya/json_gen | 1.36s | 6.30 | 216x |
| jetstream/hashmap | 3.23s | 15.3 | 211x |
| jetstream/cube3d | 3.53s | 17.6 | 201x |
| awfy/havlak | 18.08s | 94.7 | 191x |
| text/text_search | 111.99s | 781.1 | 143x |
| larceny/triangl | 7.68s | 66.5 | 115x |
| awfy/cd | 4.05s | 36.0 | 112x |
| awfy/nbody | 587.4 | 5.28 | 111x |

#### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.463 | 1.92 | 0.24x |
| r7rs/tak | 0.315 | 0.781 | 0.40x |
| r7rs/cpstak | 0.626 | 0.968 | 0.65x |

### R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.60 [1.51–1.61] | 1.58 [1.56–1.62] | 1.11 [1.03–1.16] | 14.6 [14.4–15.2] | 18.8 [18.6–18.9] | 1.83 [1.77–7.89] | 0.87x | 0.87x | 0.61x | 8.02x | 10.3x |
| fibfp | recursive | 2.01 [2.01–2.01] | 1.19 [1.19–1.22] | 1.13 [1.12–1.14] | 14.4 [14.3–14.5] | 18.6 [18.6–18.6] | 1.79 [1.74–1.83] | 1.12x | 0.66x | 0.63x | 8.05x | 10.4x |
| tak | recursive | 0.155 [0.155–0.157] | 0.201 [0.195–0.201] | 0.119 [0.116–0.123] | 0.315 [0.310–0.318] | 2.86 [2.78–2.92] | 0.781 [0.773–0.815] | 0.20x | 0.26x | 0.15x | 0.40x | 3.66x |
| cpstak | closure | 0.298 [0.298–0.304] | 0.395 [0.388–0.406] | 0.234 [0.233–0.256] | 0.626 [0.618–0.627] | 5.62 [5.50–5.70] | 0.968 [0.965–0.969] | 0.31x | 0.41x | 0.24x | 0.65x | 5.81x |
| sum | iterative | 1.21 [1.21–1.22] | 1.21 [1.21–1.22] | 0.271 [0.271–0.276] | 25.7 [25.6–25.7] | 31.0 [31.0–31.1] | 1.18 [1.17–1.88] | 1.02x | 1.02x | 0.23x | 21.7x | 26.2x |
| sumfp | iterative | 0.068 [0.068–0.069] | 0.068 [0.068–0.069] | 0.077 [0.077–0.088] | 2.64 [2.56–2.70] | 3.66 [3.63–3.70] | 0.865 [0.859–1.69] | 0.08x | 0.08x | 0.09x | 3.05x | 4.23x |
| nqueens | backtrack | 1.85 [1.71–1.85] | 2.61 [2.58–2.73] | 0.133 [0.126–0.198] | 54.9 [54.8–55.5] | 7.81 [7.73–8.13] | 1.77 [1.72–2.81] | 1.05x | 1.48x | 0.08x | 31.0x | 4.41x |
| fft | numeric | 0.290 [0.287–0.305] | 0.345 [0.345–0.357] | 0.024 [0.024–0.028] | 24.0 [23.6–24.1] | 2.79 [2.71–2.79] | 1.59 [1.57–1.60] | 0.18x | 0.22x | 0.02x | 15.1x | 1.76x |
| mbrot | numeric | 0.712 [0.712–0.721] | 0.915 [0.912–0.931] | 0.447 [0.445–0.460] | 13.5 [13.4–13.5] | 17.7 [17.7–17.8] | 1.80 [1.76–2.21] | 0.40x | 0.51x | 0.25x | 7.50x | 9.86x |
| ack | recursive | 13.6 [13.2–18.5] | 15.8 [15.4–16.0] | 11.7 [9.53–11.9] | 80.8 [79.8–81.6] | 101.4 [101.0–161.8] | 13.3 [13.3–13.5] | 1.02x | 1.19x | 0.88x | 6.06x | 7.61x |

### AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.039 [0.039–0.044] | 0.142 [0.135–0.231] | 0.015 [0.015–0.017] | 0.416 [0.416–0.426] | 0.603 [0.603–0.644] | 0.385 [0.376–0.388] | 0.10x | 0.37x | 0.04x | 1.08x | 1.57x |
| permute | micro | 0.577 [0.577–0.634] | 0.171 [0.170–0.172] | 0.026 [0.025–0.027] | 7.90 [7.88–8.23] | 1.59 [1.56–1.60] | 0.812 [0.806–0.843] | 0.71x | 0.21x | 0.03x | 9.73x | 1.96x |
| queens | micro | 0.429 [0.429–0.442] | 0.262 [0.254–0.265] | 0.019 [0.018–0.021] | 4.30 [4.24–4.30] | 1.05 [1.04–1.06] | 0.635 [0.635–0.683] | 0.68x | 0.41x | 0.03x | 6.76x | 1.66x |
| towers | micro | 1.36 [1.34–1.52] | 0.454 [0.444–0.477] | 0.029 [0.029–0.031] | 16.2 [16.1–16.4] | 2.40 [2.23–2.40] | 1.11 [1.10–1.11] | 1.23x | 0.41x | 0.03x | 14.7x | 2.17x |
| bounce | micro | 0.073 [0.072–0.074] | 0.106 [0.105–0.114] | 0.024 [0.024–0.025] | 4.01 [4.01–4.17] | 0.883 [0.869–0.891] | 0.543 [0.537–0.548] | 0.13x | 0.20x | 0.04x | 7.38x | 1.63x |
| list | micro | 0.739 [0.731–0.757] | 0.219 [0.217–0.224] | 0.024 [0.021–0.026] | 2.21 [2.20–2.23] | 0.915 [0.905–0.916] | 0.485 [0.481–0.506] | 1.52x | 0.45x | 0.05x | 4.56x | 1.89x |
| storage | micro | 0.702 [0.701–0.711] | 0.372 [0.368–0.388] | 0.094 [0.088–0.095] | 5.86 [5.85–5.88] | 2.06 [2.05–2.07] | 0.629 [0.627–0.638] | 1.12x | 0.59x | 0.15x | 9.31x | 3.27x |
| mandelbrot | compute | 44.1 [44.1–44.2] | 44.1 [44.1–44.3] | 30.7 [30.6–30.7] | 436.0 [434.7–436.3] | 868.5 [867.4–878.1] | 31.1 [31.0–31.2] | 1.42x | 1.42x | 0.98x | 14.0x | 27.9x |
| nbody | compute | 53.9 [53.6–54.6] | 25.3 [25.3–25.3] | 1.49 [1.46–1.51] | 587.4 [585.3–587.6] | 158.9 [158.7–159.6] | 5.28 [5.26–5.68] | 10.2x | 4.80x | 0.28x | 111x | 30.1x |
| richards | macro | 433.6 [432.7–436.5] | 724.0 [722.2–727.4] | 29.1 [28.9–29.6] | 1.10s [1.10s–1.15s] | 191.5 [190.9–191.8] | 46.8 [46.5–47.5] | 9.26x | 15.5x | 0.62x | 23.5x | 4.09x |
| json | macro | 6.11 [6.05–6.14] | 4.69 [4.64–4.71] | 0.254 [0.249–0.260] | 45.7 [45.6–46.7] | 10.9 [10.9–11.9] | 2.62 [2.60–2.73] | 2.33x | 1.79x | 0.10x | 17.4x | 4.15x |
| deltablue | macro | 134.7 [133.7–135.5] | 48.3 [48.0–48.4] | 1.15 [1.14–1.16] | 506.3 [498.9–507.9] | 99.3 [99.2–100.2] | 11.7 [11.4–11.8] | 11.5x | 4.13x | 0.10x | 43.3x | 8.50x |
| havlak | macro | 68.2 [67.2–68.6] | 61.1 [61.1–61.3] | 1.83 [1.79–1.84] | 18.08s [18.07s–18.14s] | 3.30s [3.28s–3.32s] | 94.7 [86.0–105.2] | 0.72x | 0.65x | 0.02x | 191x | 34.8x |
| cd | macro | 652.7 [645.7–708.5] | 547.5 [544.8–552.7] | 15.1 [15.1–15.1] | 4.05s [4.03s–4.07s] | 959.6 [956.4–971.4] | 36.0 [35.9–36.1] | 18.1x | 15.2x | 0.42x | 112x | 26.6x |

### BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 8.94 [8.81–9.13] | 2.30 [2.21–2.30] | 3.02 [3.02–3.05] | 20.7 [20.7–20.8] | 23.5 [23.4–23.6] | 4.13 [4.07–4.55] | 2.17x | 0.56x | 0.73x | 5.02x | 5.69x |
| fannkuch | permutation | 0.434 [0.433–0.461] | 0.628 [0.628–0.629] | 0.152 [0.147–0.155] | 21.2 [21.1–21.3] | 7.15 [7.15–7.26] | 4.07 [3.99–4.08] | 0.11x | 0.15x | 0.04x | 5.21x | 1.76x |
| fasta | generation | 0.810 [0.797–0.824] | 0.917 [0.892–0.931] | 0.244 [0.240–0.244] | 75.9 [75.8–76.2] | 8.88 [8.84–8.90] | 6.23 [6.03–6.42] | 0.13x | 0.15x | 0.04x | 12.2x | 1.43x |
| knucleotide | hashing | 4.62 [4.58–4.77] | 5.33 [5.33–5.49] | 0.284 [0.281–0.286] | 163.9 [163.9–164.3] | 7.69 [7.64–7.74] | 4.99 [4.94–5.92] | 0.93x | 1.07x | 0.06x | 32.9x | 1.54x |
| pidigits | bignum | 0.299 [0.298–0.306] | 0.308 [0.302–0.308] | 0.046 [0.045–0.049] | 0.463 [0.462–0.465] | 0.130 [0.126–0.133] | 1.92 [1.89–1.94] | 0.16x | 0.16x | 0.02x | 0.24x | 0.07x |
| regexredux | regex | 1.28 [1.27–1.29] | 1.27 [1.26–1.30] | 1.14 [1.13–1.15] | 26.0 [25.8–26.0] | 5.60 [5.56–5.62] | 2.44 [2.41–2.52] | 0.52x | 0.52x | 0.46x | 10.6x | 2.29x |
| revcomp | string | 1.26 [1.25–1.32] | 1.19 [1.18–1.19] | 0.373 [0.371–0.400] | 26.7 [26.6–26.8] | 2.59 [2.55–2.59] | 3.34 [3.29–3.35] | 0.38x | 0.35x | 0.11x | 7.99x | 0.77x |
| spectralnorm | numeric | 1.80 [1.76–1.84] | 2.03 [2.03–2.09] | 0.355 [0.354–0.359] | 15.6 [15.4–15.7] | 64.1 [63.8–64.1] | 2.66 [2.61–2.99] | 0.68x | 0.76x | 0.13x | 5.87x | 24.1x |

### KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 224.0 [223.6–225.4] | 334.4 [332.9–334.5] | 28.0 [27.5–28.7] | 2.99s [2.96s–2.99s] | 886.1 [880.9–893.3] | 33.9 [33.8–34.0] | 6.61x | 9.86x | 0.82x | 88.1x | 26.1x |
| matmul | numeric | 33.2 [33.1–33.3] | 43.2 [42.6–43.3] | 6.09 [6.09–6.10] | 358.4 [355.5–417.4] | 539.7 [538.6–540.0] | 15.3 [15.3–15.3] | 2.17x | 2.82x | 0.40x | 23.4x | 35.2x |
| primes | numeric | 16.3 [16.2–16.3] | 23.7 [23.6–23.8] | 1.60 [1.59–2.30] | 60.0 [59.7–60.8] | 94.8 [94.7–94.9] | 4.43 [4.34–4.47] | 3.67x | 5.35x | 0.36x | 13.6x | 21.4x |
| base64 | string | 16.9 [16.9–17.1] | 10.2 [9.62–10.9] | 0.564 [0.556–0.567] | 481.7 [478.2–481.9] | 157.1 [156.5–157.4] | 17.3 [17.2–17.3] | 0.98x | 0.59x | 0.03x | 27.9x | 9.10x |
| levenshtein | string | 11.4 [11.4–11.7] | 8.49 [8.42–8.57] | 0.902 [0.902–0.914] | 89.1 [88.4–89.1] | 54.1 [54.1–54.4] | 3.95 [3.94–3.99] | 2.89x | 2.15x | 0.23x | 22.5x | 13.7x |
| json_gen | data | 27.2 [26.6–27.5] | 11.4 [11.4–11.9] | 1.54 [1.54–1.57] | 1.36s [1.36s–1.38s] | 20.0 [19.9–20.1] | 6.30 [6.20–6.50] | 4.33x | 1.80x | 0.25x | 216x | 3.18x |
| collatz | numeric | 408.2 [406.4–408.8] | 407.2 [405.5–407.6] | 213.4 [212.5–214.1] | 5.80s [5.80s–5.81s] | 6.22s [6.22s–6.32s] | 1.42s [1.41s–1.42s] | 0.29x | 0.29x | 0.15x | 4.10x | 4.39x |

### LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 281.5 [281.4–282.2] | 242.8 [242.5–243.1] | 60.3 [60.2–60.5] | 7.68s [7.60s–7.68s] | 2.18s [2.18s–2.18s] | 66.5 [66.4–66.6] | 4.23x | 3.65x | 0.91x | 115x | 32.8x |
| array1 | array | 1.07 [1.07–1.08] | 1.76 [1.71–1.76] | 0.324 [0.316–0.327] | 18.4 [18.3–18.5] | 36.0 [35.8–36.2] | 1.90 [1.89–1.96] | 0.56x | 0.92x | 0.17x | 9.68x | 18.9x |
| deriv | symbolic | 30.7 [30.4–30.9] | 7.44 [7.42–7.58] | 2.85 [2.84–2.86] | 52.7 [52.4–53.4] | 58.8 [58.7–59.2] | 3.72 [3.66–3.82] | 8.24x | 2.00x | 0.76x | 14.1x | 15.8x |
| diviter | iterative | 532.9 [532.7–587.6] | 531.6 [531.2–531.7] | 249.6 [249.4–252.7] | 11.48s [11.39s–11.51s] | 26.68s [26.64s–26.75s] | 464.6 [464.3–466.3] | 1.15x | 1.14x | 0.54x | 24.7x | 57.4x |
| divrec | recursive | 6.19 [6.18–6.24] | 2.55 [2.54–2.55] | 4.91 [4.90–5.01] | 14.6 [14.6–14.8] | 36.0 [36.0–36.8] | 7.55 [7.48–7.56] | 0.82x | 0.34x | 0.65x | 1.94x | 4.77x |
| gcbench | allocation | 210.2 [208.4–213.3] | 83.0 [80.4–83.1] | 70.6 [70.3–71.3] | 529.5 [526.3–529.7] | 559.0 [549.4–559.6] | 23.6 [23.4–23.6] | 8.91x | 3.52x | 2.99x | 22.4x | 23.7x |
| paraffins | combinat | 0.307 [0.307–0.315] | 0.373 [0.370–0.398] | 0.050 [0.048–0.050] | 2.89 [2.86–2.97] | 2.51 [2.51–2.53] | 1.01 [0.993–1.05] | 0.30x | 0.37x | 0.05x | 2.86x | 2.48x |
| pnpoly | numeric | 14.0 [13.8–14.1] | 18.6 [18.6–18.7] | 1.97 [1.91–2.07] | 97.9 [97.0–98.3] | 201.7 [201.6–248.4] | 5.89 [5.85–5.92] | 2.38x | 3.15x | 0.33x | 16.6x | 34.2x |
| puzzle | search | 14.8 [14.6–15.2] | 2.83 [2.83–3.02] | 1.27 [1.27–1.28] | 33.2 [33.1–33.6] | 29.3 [29.3–29.3] | 3.29 [3.27–3.29] | 4.50x | 0.86x | 0.38x | 10.1x | 8.90x |
| quicksort | sorting | 11.9 [11.9–12.0] | 1.89 [1.86–1.89] | 0.197 [0.196–0.208] | 148.0 [147.0–148.4] | 19.3 [19.2–19.3] | 1.66 [1.63–1.66] | 7.19x | 1.14x | 0.12x | 89.2x | 11.6x |
| ray | numeric | 0.305 [0.304–0.323] | 0.444 [0.439–0.456] | 0.175 [0.172–0.175] | 16.7 [16.5–16.8] | 13.8 [13.8–13.9] | 3.60 [3.54–3.66] | 0.08x | 0.12x | 0.05x | 4.64x | 3.82x |

### JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 25.2 [25.2–25.3] | 29.2 [29.2–29.3] | 0.523 [0.515–0.526] | 3.53s [3.51s–3.53s] | 218.0 [215.9–225.4] | 17.6 [17.6–17.9] | 1.43x | 1.66x | 0.03x | 201x | 12.4x |
| navier_stokes | numeric | 265.2 [264.6–265.9] | 265.1 [264.7–320.1] | 46.9 [46.8–46.9] | 355.5 [354.6–360.3] | 98.9 [98.3–98.9] | 14.1 [14.1–16.0] | 18.8x | 18.8x | 3.33x | 25.2x | 7.02x |
| splay | data | 316.7 [314.6–374.1] | 388.1 [387.2–389.5] | 18.7 [18.7–18.9] | 636.0 [621.0–643.7] | 144.6 [144.0–160.1] | 19.2 [18.1–19.7] | 16.5x | 20.2x | 0.98x | 33.2x | 7.54x |
| hashmap | data | 125.7 [123.5–126.0] | 69.6 [68.5–70.9] | 2.82 [2.81–2.88] | 3.23s [3.23s–3.23s] | 315.8 [315.8–319.1] | 15.3 [15.3–15.3] | 8.21x | 4.55x | 0.18x | 211x | 20.6x |
| crypto_sha1 | crypto | 55.8 [55.7–56.4] | 35.4 [35.4–36.0] | 2.68 [2.66–2.72] | 379.9 [377.4–440.3] | 219.2 [218.8–219.5] | 8.68 [8.63–8.70] | 6.42x | 4.07x | 0.31x | 43.8x | 25.3x |
| raytrace3d | 3d | 76.5 [74.8–77.4] | 24.9 [24.8–25.0] | 2.17 [2.16–2.20] | 584.1 [583.6–587.4] | 162.6 [161.5–163.5] | 18.4 [18.3–18.5] | 4.15x | 1.35x | 0.12x | 31.7x | 8.82x |

### Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 388.1 [384.7–399.3] | 276.6 [276.0–277.7] | 13.1 [13.1–13.1] | 2.99s [2.98s–3.01s] | 619.4 [610.5–624.3] | 39.3 [39.2–39.6] | 9.87x | 7.03x | 0.33x | 76.1x | 15.7x |
| microdiff | data-diff | 54.1 [53.6–54.4] | 56.7 [56.1–56.7] | 2.65 [2.63–2.67] | 1.58s [1.57s–1.59s] | 108.3 [108.1–108.7] | 16.2 [16.2–17.1] | 3.34x | 3.50x | 0.16x | 97.2x | 6.68x |
| hyphen | hyphenation | 73.6 [73.6–74.0] | 74.3 [73.3–75.1] | 1.49 [1.47–1.49] | 586.7 [566.0–647.5] | 81.9 [81.9–147.6] | 7.07 [6.66–7.44] | 10.4x | 10.5x | 0.21x | 82.9x | 11.6x |
| prettier_ast | formatting | 1.15s [1.13s–1.16s] | 2.52s [2.48s–2.55s] | 41.6 [41.4–41.7] | 7.60s [7.59s–7.63s] | 1.42s [1.41s–1.43s] | 99.5 [99.3–100.0] | 11.5x | 25.3x | 0.42x | 76.3x | 14.3x |
| text_search | search | 16.32s [16.20s–16.33s] | 2.79s [2.79s–2.80s] | 535.7 [535.1–537.1] | 111.99s [111.82s–113.75s] | 38.95s [38.89s–39.20s] | 781.1 [781.0–845.3] | 20.9x | 3.57x | 0.69x | 143x | 49.9x |
| three_way_merge | merge | 3.85s [3.85s–3.86s] | 6.25s [6.25s–6.32s] | 2.14s [2.14s–2.15s] | 15.00s [14.93s–15.05s] | 6.17s [6.12s–6.21s] | 966.0 [964.9–969.2] | 3.98x | 6.47x | 2.21x | 15.5x | 6.38x |
| log_pipeline | log-processing | 6.30s [6.26s–6.39s] | 6.38s [6.36s–6.44s] | 625.7 [624.9–690.3] | 18.77s [18.73s–18.78s] | 9.44s [9.43s–9.53s] | 947.1 [942.6–948.4] | 6.65x | 6.73x | 0.66x | 19.8x | 9.97x |

---

## Part 2 — End-to-end time (wall clock, auto tier)

Wall clock from process invocation to exit, so **every engine pays its own startup and compilation inside the number**. The MIR columns use the shipped auto tier -- no `LAMBDA_TIER` override -- which is what `lambda.exe run script.ls` actually does.

This set exists because the two questions are different. Part 1 asks how fast the compiled workload runs; part 2 asks how long it takes to run the script. Timing the auto tier under part 1's rules would charge Lambda for JIT compilation performed *inside* the measured region while crediting Node.js with a post-warmup figure -- comparing two different things. Here the accounting is the same for everyone.

Same processes, where possible: the reference engines report their wall and `__TIMING__` figures from the *same* run, so parts 1 and 2 are two readings of one launch. Only the MIR columns are re-run, because part 1 pins the JIT and part 2 must use the auto tier.

⚠ Short workloads are dominated by fixed process startup here, so a row whose part-1 time is a fraction of a millisecond says more about executable launch cost than about the language. Read part 2 by the longer rows.

### Summary

| Suite | Total | Timed MIR (untyped, auto) | Timed MIR (typed, auto) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped, auto)/Node geo | MIR (typed, auto)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 0.41x | 0.43x | 1.05x | 1.01x | 0.41x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 1.00x | 1.06x | 0.93x | 5.09x | 0.81x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 0.54x | 0.52x | 1.00x | 1.53x | 0.33x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 0.83x | 0.86x | 0.72x | 7.38x | 2.88x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 0.90x | 0.74x | 0.98x | 3.19x | 1.89x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 3.22x | 4.92x | 0.90x | 18.8x | 3.65x |
| Text | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 4.84x | 4.39x | 0.92x | 36.1x | 6.76x |
| **Overall** | 63 | 63 | 63 | 63 | 63 | 63 | 63 | 1.03x | 1.04x | 0.94x | 4.57x | 1.26x |

> Ratio < 1.0 means the engine finished the whole run faster than Node.js.

### R7RS

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 16.8 [16.8–73.5] | 17.1 [17.1–17.7] | 49.1 [48.3–699.4] | 39.1 [38.0–41.3] | 25.1 [24.6–26.9] | 44.6 [44.6–89.9] | 0.38x | 0.38x | 1.10x | 0.88x | 0.56x |
| fibfp | recursive | 17.2 [17.0–19.8] | 16.1 [15.9–16.6] | 46.4 [46.2–46.9] | 37.9 [37.6–38.8] | 24.3 [24.2–24.5] | 44.4 [44.2–44.5] | 0.39x | 0.36x | 1.05x | 0.85x | 0.55x |
| tak | recursive | 15.7 [15.6–15.8] | 15.6 [15.4–15.6] | 45.3 [45.3–45.9] | 24.8 [24.4–25.9] | 8.25 [8.25–8.52] | 43.6 [43.0–44.1] | 0.36x | 0.36x | 1.04x | 0.57x | 0.19x |
| cpstak | closure | 16.2 [16.1–16.4] | 15.8 [15.6–16.2] | 45.3 [45.1–45.4] | 25.2 [25.2–26.6] | 11.2 [10.9–11.3] | 44.2 [43.1–44.2] | 0.37x | 0.36x | 1.03x | 0.57x | 0.25x |
| sum | iterative | 16.0 [15.8–16.3] | 16.3 [16.2–16.3] | 45.1 [44.9–45.3] | 49.6 [49.4–50.6] | 36.7 [36.6–37.1] | 44.8 [43.9–45.2] | 0.36x | 0.36x | 1.01x | 1.11x | 0.82x |
| sumfp | iterative | 14.8 [14.5–14.8] | 14.6 [14.6–14.8] | 45.3 [44.9–45.3] | 27.0 [26.8–28.0] | 9.09 [9.08–9.74] | 44.4 [44.2–44.7] | 0.33x | 0.33x | 1.02x | 0.61x | 0.20x |
| nqueens | backtrack | 24.4 [24.0–24.5] | 28.9 [28.9–29.1] | 56.0 [45.4–97.1] | 136.5 [136.0–137.1] | 13.5 [13.2–14.3] | 44.9 [44.9–44.9] | 0.54x | 0.64x | 1.25x | 3.04x | 0.30x |
| fft | numeric | 21.1 [20.9–21.6] | 27.5 [27.4–27.9] | 45.5 [45.4–46.8] | 56.9 [56.1–58.2] | 8.25 [8.07–9.01] | 44.6 [44.3–44.9] | 0.47x | 0.62x | 1.02x | 1.28x | 0.19x |
| mbrot | numeric | 18.3 [18.2–19.0] | 18.9 [18.8–19.0] | 45.3 [45.0–45.7] | 43.5 [43.4–44.6] | 23.5 [23.3–23.7] | 44.9 [44.5–48.6] | 0.41x | 0.42x | 1.01x | 0.97x | 0.52x |
| ack | recursive | 31.1 [29.6–42.0] | 31.2 [31.1–31.6] | 57.1 [55.1–58.5] | 104.9 [104.8–106.4] | 108.0 [107.0–167.9] | 56.4 [56.0–56.8] | 0.55x | 0.55x | 1.01x | 1.86x | 1.91x |

### AWFY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 15.7 [15.4–15.8] | 16.4 [16.2–16.5] | 44.7 [44.6–45.7] | 31.8 [31.6–32.1] | 6.51 [6.04–7.02] | 43.7 [43.6–46.9] | 0.36x | 0.37x | 1.02x | 0.73x | 0.15x |
| permute | micro | 17.5 [17.4–17.5] | 19.0 [18.9–19.5] | 45.3 [45.0–45.3] | 45.1 [45.0–46.5] | 7.80 [7.45–8.16] | 45.0 [44.4–45.7] | 0.39x | 0.42x | 1.01x | 1.00x | 0.17x |
| queens | micro | 21.8 [21.4–24.6] | 25.5 [25.3–25.7] | 45.3 [45.2–45.3] | 45.6 [45.5–47.2] | 7.06 [7.03–7.71] | 45.8 [45.4–45.8] | 0.48x | 0.56x | 0.99x | 0.99x | 0.15x |
| towers | micro | 22.0 [21.8–22.0] | 24.2 [23.5–24.5] | 60.3 [47.2–97.4] | 58.2 [57.9–58.4] | 8.63 [8.61–8.69] | 46.9 [46.0–47.0] | 0.47x | 0.52x | 1.29x | 1.24x | 0.18x |
| bounce | micro | 25.2 [25.0–25.9] | 33.8 [33.3–34.3] | 46.4 [45.7–47.8] | 171.7 [170.7–202.9] | 7.10 [7.05–7.72] | 45.0 [44.3–45.2] | 0.56x | 0.75x | 1.03x | 3.82x | 0.16x |
| list | micro | 19.4 [19.2–19.7] | 19.6 [19.3–19.7] | 45.2 [44.9–45.4] | 38.8 [38.4–39.9] | 6.44 [6.28–7.20] | 44.1 [44.1–103.4] | 0.44x | 0.44x | 1.02x | 0.88x | 0.15x |
| storage | micro | 17.3 [17.2–17.5] | 17.8 [17.7–17.8] | 45.0 [45.0–45.0] | 150.0 [148.2–150.4] | 8.48 [8.20–9.12] | 44.9 [44.5–45.2] | 0.38x | 0.40x | 1.00x | 3.34x | 0.19x |
| mandelbrot | compute | 61.3 [61.1–61.6] | 61.4 [61.3–61.5] | 75.8 [75.6–75.8] | 471.4 [470.3–471.6] | 875.3 [874.6–885.3] | 76.4 [75.9–77.5] | 0.80x | 0.80x | 0.99x | 6.17x | 11.5x |
| nbody | compute | 85.1 [84.8–85.1] | 100.8 [91.6–145.7] | 47.9 [47.7–48.2] | 680.4 [679.5–681.1] | 165.4 [165.3–166.3] | 48.9 [48.8–51.1] | 1.74x | 2.06x | 0.98x | 13.9x | 3.38x |
| richards | macro | 487.6 [487.0–488.1] | 776.7 [775.4–779.4] | 77.3 [76.8–77.5] | 1.22s [1.21s–1.27s] | 198.5 [197.8–198.9] | 93.1 [92.7–93.5] | 5.24x | 8.34x | 0.83x | 13.1x | 2.13x |
| json | macro | 51.9 [51.4–52.1] | 53.6 [52.4–53.6] | 52.9 [52.5–53.1] | 307.2 [304.5–317.9] | 18.9 [18.6–20.0] | 47.4 [47.1–49.2] | 1.10x | 1.13x | 1.12x | 6.49x | 0.40x |
| deltablue | macro | 247.7 [247.3–247.9] | 191.1 [190.5–191.8] | 53.6 [53.5–53.9] | 805.7 [791.9–806.2] | 107.3 [107.0–107.9] | 58.1 [57.5–58.5] | 4.26x | 3.29x | 0.92x | 13.9x | 1.85x |
| havlak | macro | 160.5 [160.5–161.8] | 139.2 [139.0–139.3] | 54.1 [54.1–55.7] | 18.54s [18.52s–18.58s] | 3.31s [3.29s–3.33s] | 142.9 [132.9–152.4] | 1.12x | 0.97x | 0.38x | 130x | 23.2x |
| cd | macro | 717.1 [715.3–718.7] | 612.2 [608.7–613.7] | 67.9 [67.9–68.2] | 4.79s [4.77s–4.81s] | 967.7 [964.6–979.6] | 81.4 [81.1–82.9] | 8.81x | 7.52x | 0.83x | 58.8x | 11.9x |

### BENG

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 27.9 [27.8–28.0] | 22.9 [22.6–22.9] | 48.7 [48.5–49.0] | 54.2 [53.9–54.8] | 29.6 [29.2–29.7] | 46.8 [46.8–47.5] | 0.60x | 0.49x | 1.04x | 1.16x | 0.63x |
| fannkuch | permutation | 21.7 [21.5–21.8] | 28.2 [28.1–28.2] | 45.4 [45.1–46.3] | 65.0 [64.8–65.7] | 12.6 [12.5–13.3] | 46.4 [46.2–46.6] | 0.47x | 0.61x | 0.98x | 1.40x | 0.27x |
| fasta | generation | 23.3 [23.2–23.3] | 25.6 [25.4–25.9] | 45.6 [45.6–46.0] | 113.0 [112.9–114.9] | 14.7 [14.5–15.2] | 47.7 [47.7–48.2] | 0.49x | 0.54x | 0.96x | 2.37x | 0.31x |
| knucleotide | hashing | 30.2 [30.1–30.6] | 38.1 [37.8–38.5] | 46.5 [46.4–46.6] | 198.5 [198.5–199.8] | 13.8 [13.7–14.0] | 48.1 [47.8–48.4] | 0.63x | 0.79x | 0.97x | 4.13x | 0.29x |
| pidigits | bignum | 49.7 [20.2–53.8] | 20.3 [19.9–20.4] | 47.7 [47.0–47.7] | 35.2 [34.7–36.1] | 5.51 [5.48–6.08] | 44.5 [44.2–44.8] | 1.12x | 0.46x | 1.07x | 0.79x | 0.12x |
| regexredux | regex | 14.9 [14.8–15.5] | 14.9 [14.9–15.3] | 47.9 [47.8–48.3] | 63.1 [62.7–63.3] | 11.3 [10.9–11.8] | 45.0 [44.5–45.1] | 0.33x | 0.33x | 1.06x | 1.40x | 0.25x |
| revcomp | string | 21.3 [21.2–21.5] | 22.3 [22.2–22.5] | 45.7 [45.5–46.6] | 63.2 [62.8–63.5] | 8.10 [8.05–8.63] | 45.7 [45.3–45.8] | 0.47x | 0.49x | 1.00x | 1.38x | 0.18x |
| spectralnorm | numeric | 23.4 [23.0–23.4] | 26.0 [25.5–26.0] | 45.6 [45.4–46.4] | 58.9 [58.7–60.2] | 70.2 [69.5–70.3] | 47.1 [46.1–48.1] | 0.50x | 0.55x | 0.97x | 1.25x | 1.49x |

### KOSTYA

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 249.3 [247.8–249.4] | 362.0 [358.0–419.1] | 73.8 [73.0–74.0] | 3.03s [3.00s–3.04s] | 892.6 [887.6–900.1] | 78.9 [78.9–79.2] | 3.16x | 4.59x | 0.94x | 38.4x | 11.3x |
| matmul | numeric | 53.7 [53.6–53.9] | 64.9 [64.2–65.1] | 51.4 [51.0–51.7] | 397.7 [394.8–456.5] | 546.0 [544.9–546.5] | 58.9 [58.3–58.9] | 0.91x | 1.10x | 0.87x | 6.75x | 9.27x |
| primes | numeric | 32.5 [32.4–32.5] | 40.7 [40.4–40.7] | 46.9 [46.4–56.0] | 89.2 [88.8–91.3] | 101.0 [100.8–101.5] | 47.6 [47.1–48.7] | 0.68x | 0.85x | 0.98x | 1.87x | 2.12x |
| base64 | string | 39.6 [39.6–39.6] | 39.2 [39.1–39.5] | 46.5 [45.9–46.6] | 518.5 [516.1–518.9] | 163.4 [163.0–163.7] | 60.6 [60.2–60.8] | 0.65x | 0.65x | 0.77x | 8.56x | 2.70x |
| levenshtein | string | 35.2 [35.0–35.5] | 30.4 [29.9–30.7] | 46.4 [46.1–46.5] | 128.1 [127.8–129.2] | 60.1 [60.0–60.5] | 48.2 [48.0–48.5] | 0.73x | 0.63x | 0.96x | 2.66x | 1.25x |
| json_gen | data | 52.4 [50.3–60.3] | 35.6 [34.8–35.7] | 46.9 [46.5–47.1] | 1.40s [1.39s–1.41s] | 26.6 [26.4–26.9] | 51.6 [51.5–52.1] | 1.02x | 0.69x | 0.91x | 27.0x | 0.52x |
| collatz | numeric | 427.2 [424.8–436.8] | 425.2 [422.5–483.5] | 258.7 [257.3–259.7] | 5.83s [5.83s–5.83s] | 6.23s [6.22s–6.32s] | 1.46s [1.46s–1.46s] | 0.29x | 0.29x | 0.18x | 3.99x | 4.26x |

### LARCENY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 308.2 [307.8–308.5] | 273.7 [273.6–274.2] | 105.9 [105.7–107.0] | 7.72s [7.66s–7.73s] | 2.19s [2.18s–2.19s] | 111.1 [111.0–111.4] | 2.78x | 2.46x | 0.95x | 69.6x | 19.7x |
| array1 | array | 16.4 [16.3–16.5] | 17.2 [17.2–17.2] | 44.8 [44.5–44.9] | 45.2 [45.2–45.3] | 42.1 [41.8–42.1] | 45.7 [45.7–46.2] | 0.36x | 0.38x | 0.98x | 0.99x | 0.92x |
| deriv | symbolic | 55.2 [55.2–55.8] | 27.6 [27.6–27.8] | 48.9 [48.6–49.7] | 89.7 [89.6–91.2] | 65.0 [64.8–65.4] | 47.3 [46.9–48.0] | 1.17x | 0.59x | 1.03x | 1.90x | 1.38x |
| diviter | iterative | 549.9 [549.9–550.0] | 550.0 [549.0–550.6] | 295.1 [295.0–298.6] | 11.51s [11.42s–11.53s] | 26.69s [26.65s–26.76s] | 510.0 [509.0–512.1] | 1.08x | 1.08x | 0.58x | 22.6x | 52.3x |
| divrec | recursive | 22.6 [22.0–22.8] | 19.9 [19.8–19.9] | 49.9 [49.9–50.6] | 40.6 [40.5–40.7] | 42.3 [41.6–43.0] | 51.8 [51.5–52.2] | 0.44x | 0.38x | 0.96x | 0.78x | 0.82x |
| gcbench | allocation | 236.0 [234.0–285.8] | 127.4 [124.9–129.9] | 116.1 [115.8–117.0] | 571.1 [567.6–571.1] | 571.4 [561.3–571.9] | 67.4 [67.0–67.7] | 3.50x | 1.89x | 1.72x | 8.47x | 8.48x |
| paraffins | combinat | 41.3 [40.6–41.7] | 47.8 [47.7–48.2] | 46.0 [46.0–46.6] | 49.1 [48.7–49.1] | 7.95 [7.92–8.54] | 45.5 [44.6–46.1] | 0.91x | 1.05x | 1.01x | 1.08x | 0.17x |
| pnpoly | numeric | 31.9 [31.7–32.1] | 39.0 [39.0–39.3] | 47.0 [46.9–47.4] | 128.5 [127.3–128.5] | 208.1 [208.0–254.7] | 50.3 [50.3–50.4] | 0.63x | 0.77x | 0.93x | 2.55x | 4.13x |
| puzzle | search | 33.4 [33.2–33.7] | 22.6 [22.5–23.0] | 46.4 [46.2–46.7] | 67.7 [67.3–67.9] | 35.3 [34.9–35.5] | 47.4 [47.3–47.8] | 0.70x | 0.48x | 0.98x | 1.43x | 0.74x |
| quicksort | sorting | 32.0 [31.9–32.2] | 24.0 [23.9–24.3] | 45.8 [45.7–46.2] | 180.4 [179.0–180.9] | 25.3 [25.0–25.6] | 46.9 [46.2–47.3] | 0.68x | 0.51x | 0.98x | 3.85x | 0.54x |
| ray | numeric | 26.8 [26.6–26.9] | 20.3 [19.9–20.5] | 45.3 [45.2–45.4] | 54.2 [53.8–54.7] | 19.9 [19.5–20.1] | 46.4 [46.2–46.6] | 0.58x | 0.44x | 0.98x | 1.17x | 0.43x |

### JetStream

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 172.3 [169.8–172.5] | 395.3 [395.1–456.1] | 49.6 [49.6–49.9] | 3.65s [3.65s–3.65s] | 224.9 [222.8–232.3] | 64.4 [63.0–65.8] | 2.68x | 6.14x | 0.77x | 56.7x | 3.49x |
| navier_stokes | numeric | 352.8 [352.3–357.1] | 356.4 [354.5–363.5] | 95.3 [94.8–96.6] | 542.4 [529.8–564.0] | 109.5 [109.2–109.9] | 61.5 [61.2–63.3] | 5.73x | 5.79x | 1.55x | 8.82x | 1.78x |
| splay | data | 375.2 [374.4–378.6] | 456.8 [452.6–458.9] | 66.3 [66.1–66.3] | 1.59s [1.58s–1.66s] | 555.3 [553.9–576.9] | 92.1 [91.2–94.6] | 4.07x | 4.96x | 0.72x | 17.2x | 6.03x |
| hashmap | data | 156.2 [155.7–156.4] | 109.7 [106.9–168.3] | 48.6 [48.2–49.1] | 3.35s [3.35s–3.36s] | 323.1 [323.0–326.4] | 59.7 [59.3–59.7] | 2.62x | 1.84x | 0.81x | 56.2x | 5.41x |
| crypto_sha1 | crypto | 87.7 [86.2–88.0] | 690.7 [689.9–695.5] | 49.1 [48.8–49.7] | 425.5 [423.6–486.4] | 226.0 [225.7–226.1] | 52.8 [52.3–53.0] | 1.66x | 13.1x | 0.93x | 8.07x | 4.28x |
| raytrace3d | 3d | 258.3 [256.2–258.3] | 210.2 [210.2–211.2] | 52.1 [52.1–52.8] | 699.8 [699.4–703.2] | 169.6 [168.6–170.4] | 62.6 [62.4–62.9] | 4.13x | 3.36x | 0.83x | 11.2x | 2.71x |

### Text

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 419.7 [418.6–421.3] | 308.6 [303.9–366.3] | 58.9 [58.7–59.1] | 3.23s [3.22s–3.24s] | 627.1 [618.1–632.2] | 85.0 [84.9–85.3] | 4.93x | 3.63x | 0.69x | 37.9x | 7.37x |
| microdiff | data-diff | 89.5 [89.4–90.6] | 93.6 [92.2–154.1] | 51.9 [51.9–52.0] | 1.64s [1.63s–1.66s] | 114.8 [114.6–115.1] | 61.5 [60.8–61.9] | 1.45x | 1.52x | 0.84x | 26.6x | 1.87x |
| hyphen | hyphenation | 124.5 [124.5–124.6] | 126.1 [124.9–126.3] | 76.3 [75.8–76.4] | 1.85s [1.83s–1.91s] | 103.9 [103.7–169.6] | 59.1 [59.1–59.8] | 2.11x | 2.13x | 1.29x | 31.3x | 1.76x |
| prettier_ast | formatting | 1.23s [1.21s–1.31s] | 2.83s [2.83s–2.84s] | 103.8 [103.5–105.4] | 7.83s [7.82s–7.87s] | 1.43s [1.42s–1.44s] | 145.3 [145.0–148.9] | 8.48x | 19.5x | 0.71x | 53.9x | 9.86x |
| text_search | search | 16.34s [16.34s–16.43s] | 2.82s [2.82s–2.86s] | 581.8 [581.2–583.7] | 112.06s [111.90s–113.83s] | 38.96s [38.90s–39.22s] | 828.2 [827.2–891.2] | 19.7x | 3.41x | 0.70x | 135x | 47.0x |
| three_way_merge | merge | 3.88s [3.88s–3.89s] | 6.28s [6.28s–6.38s] | 2.19s [2.19s–2.20s] | 15.06s [14.98s–15.09s] | 6.17s [6.13s–6.21s] | 1.01s [1.01s–1.01s] | 3.84x | 6.20x | 2.16x | 14.9x | 6.10x |
| log_pipeline | log-processing | 6.37s [6.34s–6.40s] | 6.50s [6.47s–6.50s] | 675.2 [674.2–739.3] | 23.37s [23.37s–23.52s] | 9.47s [9.46s–9.57s] | 1.00s [996.1–1.00s] | 6.36x | 6.49x | 0.67x | 23.4x | 9.46x |

