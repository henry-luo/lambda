# JS Tune14 C14 session 1

- **Date:** 2026-09-21
- **Platform:** Darwin arm64
- **Lambda commit:** `5ce9894d4728d6d8df728c7c59ca4472fadb9d45`
- **Lambda build:** archived release binary `test/benchmark/exe/lambda-5ce9894d47` (18,680,520 bytes)
- **Instrumentation check:** passed
- **Test262 baseline:** 40,261 / 40,261 passed in 57.60s (harness time; required pre-benchmark gate)
- **Test262 phases:** prep 0.0s; batch 57.5s (batched 56.6s: sync 39.3s, async 17.2s; non-batched 0.9s); retry 0.0s; partial 0.0s; timing 0.0s; memory 0.0s; eval 0.0s
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run; suites run in order `r7rs -> awfy -> beng -> kostya -> larceny -> jetstream -> text` with a 10s idle gap between suites
- **Range notation:** per-row timing cells are `median [minimum–maximum]`; the complete ordered sample set is retained in the result JSON's status detail.
- **Engines in this report:** MIR (untyped), MIR (typed), C2MIR, LambdaJS, mvpjs, QuickJS, Node.js
- **Results source:** `test/benchmark/js_mvp/tune14/c14_session1.json`
- **MIR columns:** untyped and typed; `*` means the typed column reuses the untyped result because no typed source exists

JetStream JavaScript-engine wrappers run each benchmark's own `Benchmark.runIteration()` workload — the loop count is read from the file itself (nbody/cube3d/raytrace3d 8, richards/splay 50, crypto_sha1 25, deltablue 20, navier_stokes/hashmap 1). Each Lambda `.ls` port implements exactly one `runIteration()`, so every engine times the same work. A previous revision hard-coded 8 repeats for every file, which made the JS engines run 8/50 of Lambda's work on richards and splay, and 8x too much on navier_stokes and hashmap.

C2MIR and Go are native statically typed ports of the same workloads, present as a reference bound rather than as Lambda execution paths. The C2MIR column is **not** the retired `lambda --c2mir` transpiler: it is the C port run through MIR's own C frontend (`lambda/mir/c2m`), so its emitted MIR can be read side by side with Lambda's. Both native columns report workload-only `__TIMING__` milliseconds like every other engine — the C ports are compiled alongside `test/benchmark/c2mir/bench_timer_main.c` under `-Dmain=`, keeping c2m's own parse and JIT time outside the measurement, and the Go ports time the body inside `bench.Run`, excluding Go process startup. Each port asserts the same expected result as the `.ls` it mirrors. C2MIR coverage is partial by design (see `C2MIR_COVERAGE.md`); rows marked `not_recorded` are duplicate benchmark names whose canonical row lives in another suite.

---

## Part 1 — Execution time (self-reported)

Each engine's own `__TIMING__` figure: the timed workload only, with startup and compilation outside the measured region. This is the historical series, comparable back through Result18, and the MIR columns pin `LAMBDA_TIER=jit`.

### Summary

| Suite | Total | Timed MIR (untyped) | Timed MIR (typed) | Timed C2MIR | Timed LambdaJS | Timed mvpjs | Timed QuickJS | Timed Node.js | MIR (untyped)/Node geo | MIR (typed)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | mvpjs/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 0 | 10 | 10 | 0.34x | 0.28x | 0.21x | 1.05x | --- | 6.51x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 0 | 14 | 14 | 1.06x | 0.55x | 0.09x | 12.3x | --- | 5.18x |
| BENG | 8 | 8 | 8 | 8 | 8 | 0 | 8 | 8 | 0.39x | 0.24x | 0.10x | 4.71x | --- | 1.72x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 0 | 7 | 7 | 1.47x | 0.82x | 0.23x | 11.8x | --- | 12.0x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 0 | 11 | 11 | 1.04x | 0.64x | 0.32x | 8.62x | --- | 13.3x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 0 | 6 | 6 | 4.01x | 2.36x | 0.29x | 26.7x | --- | 11.9x |
| Text | 7 | 7 | 7 | 7 | 7 | 0 | 7 | 7 | 4.70x | 2.51x | 0.46x | 27.3x | --- | 12.7x |
| **Overall** | 63 | 63 | 63 | 63 | 63 | 0 | 63 | 63 | 1.08x | 0.65x | 0.20x | 8.09x | --- | 7.23x |

### Population Accounting

The first population is the only one comparable with Result38; the complete-suite figure includes the four subsequently added text rows. Do not compare geomeans across these populations.

| Population | Rows | MIR (typed)/Node geo | MIR (untyped)/Node geo | MIR (typed)/C2MIR geo | C2MIR matched rows |
|---|---:|---:|---:|---:|---:|
| v38-comparable (without the four text extensions) | 59 | 0.59x | 0.99x | 3.28x | 59 |
| complete current suite | 63 | 0.65x | 1.08x | 3.30x | 63 |

> The benchmark runner keeps one canonical row for each known duplicate workload, so no reporting deduplication is required.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Distance to the Static Ceiling

How far MIR (typed) is from the same workload written in a statically typed language. These columns are a reference bound, not another Lambda execution path: they say what is still on the table, and C2MIR is the sharper of the two because it shares MIR's code generator, so a gap there is attributable to Lambda's front end rather than to the backend.

- **MIR (typed) / C2MIR geomean:** 3.30x over 63 of 63 rows

**Widest gaps vs C2MIR**

| Benchmark | MIR (typed) | C2MIR | MIR (typed)/C2MIR |
|---|---:|---:|---:|
| awfy/havlak | 60.6 | 1.85 | 32.7x |
| awfy/deltablue | 26.3 | 1.15 | 22.9x |
| text/microdiff | 58.3 | 2.64 | 22.1x |
| jetstream/splay | 330.9 | 19.5 | 17.0x |
| kostya/base64 | 8.79 | 0.555 | 15.8x |
| text/prettier_ast | 641.7 | 41.7 | 15.4x |
| jetstream/cube3d | 6.98 | 0.515 | 13.5x |
| jetstream/hashmap | 36.4 | 2.84 | 12.8x |
| awfy/cd | 179.5 | 15.1 | 11.9x |
| larceny/puzzle | 14.8 | 1.27 | 11.7x |
| awfy/towers | 0.324 | 0.028 | 11.5x |
| awfy/richards | 334.2 | 29.4 | 11.4x |

---

### Notable Results

- Missing timings: **63** cells
- mvpjs missing: r7rs/fib (exit_9), r7rs/fibfp (exit_9), r7rs/tak (exit_9), r7rs/cpstak (exit_9), r7rs/sum (exit_9), r7rs/sumfp (exit_9), r7rs/nqueens (exit_9), r7rs/fft (exit_9), r7rs/mbrot (exit_9), r7rs/ack (exit_9), awfy/sieve (exit_9), awfy/permute (exit_9), awfy/queens (exit_9), awfy/towers (exit_9), awfy/bounce (exit_9), awfy/list (exit_9), awfy/storage (exit_9), awfy/mandelbrot (exit_9), awfy/nbody (exit_9), awfy/richards (exit_9), awfy/json (exit_9), awfy/deltablue (exit_9), awfy/havlak (exit_9), awfy/cd (exit_9), beng/binarytrees (exit_9), beng/fannkuch (exit_9), beng/fasta (exit_9), beng/knucleotide (exit_9), beng/pidigits (exit_9), beng/regexredux (exit_9), beng/revcomp (exit_9), beng/spectralnorm (exit_9), kostya/brainfuck (exit_9), kostya/matmul (exit_9), kostya/primes (exit_9), kostya/base64 (exit_9), kostya/levenshtein (exit_9), kostya/json_gen (exit_9), kostya/collatz (exit_9), larceny/triangl (exit_9), larceny/array1 (exit_9), larceny/deriv (exit_9), larceny/diviter (exit_9), larceny/divrec (exit_9), larceny/gcbench (exit_9), larceny/paraffins (exit_9), larceny/pnpoly (exit_9), larceny/puzzle (exit_9), larceny/quicksort (exit_9), larceny/ray (exit_9), jetstream/cube3d (exit_9), jetstream/navier_stokes (exit_9), jetstream/splay (exit_9), jetstream/hashmap (exit_9), jetstream/crypto_sha1 (exit_9), jetstream/raytrace3d (exit_9), text/fast_diff (exit_9), text/microdiff (exit_9), text/hyphen (exit_9), text/prettier_ast (exit_9), text/text_search (exit_9), text/three_way_merge (exit_9), text/log_pipeline (exit_9)

#### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| awfy/havlak | 13.87s | 95.2 | 146x |
| awfy/cd | 3.69s | 36.3 | 102x |
| jetstream/hashmap | 1.45s | 15.4 | 93.9x |
| larceny/triangl | 6.06s | 68.9 | 88.0x |
| awfy/nbody | 475.2 | 5.42 | 87.7x |
| text/text_search | 59.34s | 778.5 | 76.2x |
| kostya/brainfuck | 1.76s | 34.0 | 51.8x |
| text/fast_diff | 1.98s | 40.0 | 49.7x |

#### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| r7rs/sumfp | 0.067 | 0.887 | 0.08x |
| beng/pidigits | 0.534 | 1.79 | 0.30x |
| r7rs/tak | 0.309 | 0.776 | 0.40x |
| awfy/sieve | 0.212 | 0.386 | 0.55x |
| r7rs/sum | 0.670 | 1.19 | 0.56x |
| r7rs/cpstak | 0.619 | 0.995 | 0.62x |
| r7rs/fib | 1.57 | 1.79 | 0.88x |
| r7rs/fibfp | 1.62 | 1.75 | 0.93x |

### R7RS

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | mvpjs (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | mvpjs/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 1.27 [1.26–1.29] | 1.32 [1.30–1.33] | 1.10 [1.10–1.13] | 1.57 [1.54–1.58] | --- | 18.7 [18.5–18.9] | 1.79 [1.78–8.14] | 0.71x | 0.74x | 0.61x | 0.88x | --- | 10.4x |
| fibfp | recursive | 2.15 [2.15–2.21] | 1.23 [1.23–1.25] | 1.16 [1.14–1.17] | 1.62 [1.62–1.65] | --- | 19.3 [19.1–19.4] | 1.75 [1.74–1.75] | 1.23x | 0.71x | 0.66x | 0.93x | --- | 11.0x |
| tak | recursive | 0.122 [0.121–0.123] | 0.121 [0.121–0.121] | 0.118 [0.117–0.119] | 0.309 [0.308–0.313] | --- | 2.77 [2.75–2.82] | 0.776 [0.776–0.791] | 0.16x | 0.16x | 0.15x | 0.40x | --- | 3.57x |
| cpstak | closure | 0.241 [0.240–0.245] | 0.243 [0.240–0.253] | 0.221 [0.215–0.234] | 0.619 [0.613–0.622] | --- | 5.59 [5.52–5.67] | 0.995 [0.972–0.999] | 0.24x | 0.24x | 0.22x | 0.62x | --- | 5.62x |
| sum | iterative | 0.266 [0.266–0.266] | 0.266 [0.265–0.266] | 0.268 [0.268–0.269] | 0.670 [0.670–0.720] | --- | 31.0 [30.9–31.2] | 1.19 [1.19–1.96] | 0.22x | 0.22x | 0.23x | 0.56x | --- | 26.1x |
| sumfp | iterative | 0.070 [0.068–0.072] | 0.069 [0.069–0.071] | 0.079 [0.078–0.083] | 0.067 [0.067–0.067] | --- | 3.67 [3.61–3.76] | 0.887 [0.883–1.79] | 0.08x | 0.08x | 0.09x | 0.08x | --- | 4.14x |
| nqueens | backtrack | 0.983 [0.969–0.986] | 1.38 [1.37–1.47] | 0.363 [0.360–0.370] | 30.0 [29.9–30.0] | --- | 7.85 [7.84–8.02] | 1.78 [1.73–2.23] | 0.55x | 0.77x | 0.20x | 16.8x | --- | 4.41x |
| fft | numeric | 0.248 [0.248–0.262] | 0.091 [0.091–0.091] | 0.025 [0.024–0.026] | 2.74 [2.72–2.93] | --- | 2.75 [2.74–2.80] | 1.55 [1.55–1.59] | 0.16x | 0.06x | 0.02x | 1.76x | --- | 1.77x |
| mbrot | numeric | 0.726 [0.725–0.727] | 0.545 [0.545–0.549] | 0.445 [0.442–0.447] | 10.9 [10.9–10.9] | --- | 17.7 [17.7–17.7] | 1.92 [1.90–2.04] | 0.38x | 0.28x | 0.23x | 5.70x | --- | 9.25x |
| ack | recursive | 13.7 [13.7–13.7] | 9.89 [9.88–10.0] | 11.7 [10.3–11.8] | 14.3 [14.3–14.4] | --- | 100.9 [100.6–101.9] | 13.3 [13.2–13.4] | 1.03x | 0.74x | 0.88x | 1.08x | --- | 7.59x |

### AWFY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | mvpjs (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | mvpjs/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.034 [0.028–0.041] | 0.029 [0.029–0.030] | 0.017 [0.015–0.017] | 0.212 [0.212–0.216] | --- | 0.618 [0.612–0.636] | 0.386 [0.386–0.389] | 0.09x | 0.08x | 0.04x | 0.55x | --- | 1.60x |
| permute | micro | 0.270 [0.269–0.292] | 0.080 [0.080–0.082] | 0.027 [0.026–0.028] | 6.18 [6.17–6.50] | --- | 1.54 [1.52–1.56] | 0.831 [0.812–0.840] | 0.32x | 0.10x | 0.03x | 7.44x | --- | 1.85x |
| queens | micro | 0.156 [0.156–0.156] | 0.066 [0.065–0.067] | 0.019 [0.018–0.022] | 3.52 [3.48–3.55] | --- | 1.05 [1.04–1.05] | 0.652 [0.640–0.653] | 0.24x | 0.10x | 0.03x | 5.40x | --- | 1.61x |
| towers | micro | 0.884 [0.881–0.949] | 0.324 [0.319–0.324] | 0.028 [0.028–0.029] | 13.8 [13.7–14.1] | --- | 2.23 [2.22–2.25] | 1.10 [1.09–1.17] | 0.81x | 0.30x | 0.03x | 12.6x | --- | 2.03x |
| bounce | micro | 0.064 [0.063–0.065] | 0.098 [0.096–0.105] | 0.025 [0.024–0.025] | 3.57 [3.57–3.59] | --- | 0.874 [0.868–0.883] | 0.550 [0.545–0.811] | 0.12x | 0.18x | 0.05x | 6.49x | --- | 1.59x |
| list | micro | 0.600 [0.589–0.617] | 0.126 [0.125–0.127] | 0.023 [0.022–0.023] | 2.01 [1.96–2.01] | --- | 0.903 [0.896–0.904] | 0.487 [0.482–0.488] | 1.23x | 0.26x | 0.05x | 4.12x | --- | 1.85x |
| storage | micro | 0.595 [0.570–0.621] | 0.367 [0.358–0.376] | 0.094 [0.075–0.095] | 4.42 [4.38–4.42] | --- | 2.12 [2.06–2.14] | 0.633 [0.628–0.653] | 0.94x | 0.58x | 0.15x | 6.98x | --- | 3.36x |
| mandelbrot | compute | 39.6 [39.5–39.6] | 39.5 [39.5–39.5] | 30.6 [30.5–30.6] | 79.6 [79.5–79.6] | --- | 869.4 [868.4–881.4] | 31.1 [31.1–31.2] | 1.27x | 1.27x | 0.98x | 2.56x | --- | 27.9x |
| nbody | compute | 6.81 [6.77–6.84] | 3.41 [3.39–3.44] | 1.49 [1.47–1.51] | 475.2 [474.5–477.1] | --- | 159.1 [158.8–159.1] | 5.42 [5.40–5.76] | 1.26x | 0.63x | 0.28x | 87.7x | --- | 29.4x |
| richards | macro | 357.4 [356.9–357.7] | 334.2 [333.6–334.7] | 29.4 [28.9–29.5] | 1.04s [1.04s–1.05s] | --- | 192.5 [190.2–193.9] | 46.8 [46.0–47.7] | 7.64x | 7.14x | 0.63x | 22.2x | --- | 4.11x |
| json | macro | 6.12 [6.07–6.14] | 2.57 [2.54–2.59] | 0.258 [0.258–0.260] | 39.2 [38.6–39.3] | --- | 11.0 [11.0–19.2] | 2.92 [2.83–3.06] | 2.10x | 0.88x | 0.09x | 13.4x | --- | 3.79x |
| deltablue | macro | 122.2 [121.0–122.3] | 26.3 [26.3–26.6] | 1.15 [1.13–1.15] | 396.3 [394.2–397.8] | --- | 98.8 [98.5–99.1] | 11.7 [11.6–12.1] | 10.5x | 2.25x | 0.10x | 33.9x | --- | 8.45x |
| havlak | macro | 72.1 [70.0–72.3] | 60.6 [60.3–61.0] | 1.85 [1.83–1.87] | 13.87s [13.86s–13.94s] | --- | 3.28s [3.26s–3.29s] | 95.2 [87.2–99.1] | 0.76x | 0.64x | 0.02x | 146x | --- | 34.4x |
| cd | macro | 569.7 [567.2–571.2] | 179.5 [179.4–269.6] | 15.1 [14.9–15.1] | 3.69s [3.68s–3.69s] | --- | 1.03s [969.6–1.05s] | 36.3 [36.2–36.5] | 15.7x | 4.94x | 0.42x | 102x | --- | 28.4x |

### BENG

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | mvpjs (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | mvpjs/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 8.52 [8.20–11.0] | 2.13 [1.99–2.15] | 3.08 [3.01–3.08] | 36.0 [35.8–108.4] | --- | 23.5 [23.2–23.5] | 4.07 [3.88–4.40] | 2.09x | 0.52x | 0.76x | 8.85x | --- | 5.77x |
| fannkuch | permutation | 0.310 [0.302–0.319] | 0.363 [0.361–0.368] | 0.149 [0.148–0.149] | 21.2 [21.1–21.3] | --- | 7.23 [7.23–7.23] | 3.88 [3.83–3.90] | 0.08x | 0.09x | 0.04x | 5.47x | --- | 1.86x |
| fasta | generation | 0.751 [0.740–0.769] | 0.884 [0.878–0.887] | 0.241 [0.241–0.246] | 27.4 [27.4–28.0] | --- | 8.81 [8.71–8.89] | 6.03 [5.98–6.11] | 0.12x | 0.15x | 0.04x | 4.55x | --- | 1.46x |
| knucleotide | hashing | 4.79 [4.78–5.03] | 0.723 [0.698–0.726] | 0.280 [0.278–0.286] | 21.7 [21.5–21.7] | --- | 7.69 [7.62–7.70] | 4.82 [4.78–6.05] | 0.99x | 0.15x | 0.06x | 4.50x | --- | 1.59x |
| pidigits | bignum | 0.359 [0.359–0.364] | 0.361 [0.357–0.364] | 0.045 [0.044–0.047] | 0.534 [0.524–0.547] | --- | 0.128 [0.127–0.129] | 1.79 [1.79–1.81] | 0.20x | 0.20x | 0.03x | 0.30x | --- | 0.07x |
| regexredux | regex | 1.27 [1.26–1.30] | 1.27 [1.26–1.31] | 1.13 [1.12–1.14] | 8.88 [8.87–8.92] | --- | 5.54 [5.54–5.68] | 2.28 [2.28–2.55] | 0.56x | 0.56x | 0.50x | 3.89x | --- | 2.43x |
| revcomp | string | 1.19 [1.17–1.25] | 1.12 [1.11–1.12] | 0.378 [0.374–0.379] | 22.6 [22.3–22.6] | --- | 2.56 [2.50–2.63] | 3.27 [3.24–3.33] | 0.36x | 0.34x | 0.12x | 6.91x | --- | 0.78x |
| spectralnorm | numeric | 1.65 [1.65–1.66] | 0.784 [0.784–0.785] | 0.351 [0.350–0.353] | 84.0 [83.7–84.2] | --- | 63.7 [63.6–64.3] | 2.77 [2.59–2.79] | 0.60x | 0.28x | 0.13x | 30.3x | --- | 23.0x |

### KOSTYA

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | mvpjs (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | mvpjs/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 202.7 [198.9–204.7] | 200.0 [198.8–202.7] | 28.0 [26.7–28.0] | 1.76s [1.75s–1.77s] | --- | 885.7 [882.8–915.7] | 34.0 [33.9–34.5] | 5.97x | 5.89x | 0.82x | 51.8x | --- | 26.1x |
| matmul | numeric | 5.14 [5.13–5.18] | 5.18 [5.17–5.20] | 6.06 [6.06–6.11] | 247.2 [247.1–249.7] | --- | 538.5 [538.3–627.0] | 15.4 [15.4–15.4] | 0.33x | 0.34x | 0.39x | 16.0x | --- | 34.9x |
| primes | numeric | 15.8 [15.8–15.9] | 2.22 [2.20–2.25] | 1.58 [1.58–1.62] | 55.9 [55.8–56.0] | --- | 94.8 [94.4–95.2] | 4.34 [4.31–4.56] | 3.65x | 0.51x | 0.37x | 12.9x | --- | 21.9x |
| base64 | string | 15.9 [15.8–16.1] | 8.79 [8.68–8.98] | 0.555 [0.546–0.555] | 490.3 [489.7–498.9] | --- | 157.8 [156.1–159.9] | 17.4 [17.3–17.4] | 0.92x | 0.51x | 0.03x | 28.2x | --- | 9.08x |
| levenshtein | string | 10.1 [10.0–10.2] | 6.47 [6.47–6.56] | 0.905 [0.900–0.906] | 81.8 [81.5–82.2] | --- | 54.3 [54.1–54.9] | 3.97 [3.90–4.02] | 2.55x | 1.63x | 0.23x | 20.6x | --- | 13.7x |
| json_gen | data | 27.1 [26.9–27.6] | 9.65 [9.60–9.84] | 1.51 [1.50–1.53] | 27.8 [27.3–27.8] | --- | 19.9 [19.8–20.5] | 6.12 [6.03–6.15] | 4.42x | 1.58x | 0.25x | 4.54x | --- | 3.25x |
| collatz | numeric | 273.0 [272.3–273.8] | 274.5 [268.7–353.7] | 219.0 [217.0–222.0] | 1.60s [1.60s–1.60s] | --- | 6.23s [6.22s–6.26s] | 1.42s [1.42s–1.42s] | 0.19x | 0.19x | 0.15x | 1.13x | --- | 4.40x |

### LARCENY

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | mvpjs (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | mvpjs/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 236.5 [235.2–238.5] | 164.7 [164.2–165.9] | 59.5 [59.4–59.6] | 6.06s [6.04s–6.22s] | --- | 2.17s [2.17s–2.18s] | 68.9 [68.9–69.0] | 3.43x | 2.39x | 0.86x | 88.0x | --- | 31.6x |
| array1 | array | 0.806 [0.805–0.806] | 0.810 [0.810–0.866] | 0.317 [0.317–0.320] | 21.7 [21.7–22.1] | --- | 36.2 [36.1–36.3] | 1.81 [1.81–1.82] | 0.44x | 0.45x | 0.17x | 12.0x | --- | 20.0x |
| deriv | symbolic | 29.9 [29.8–30.7] | 7.75 [7.61–8.01] | 2.90 [2.84–2.96] | 79.6 [78.6–79.7] | --- | 59.3 [59.2–59.7] | 3.74 [3.73–3.87] | 7.98x | 2.07x | 0.77x | 21.3x | --- | 15.8x |
| diviter | iterative | 248.7 [248.6–253.8] | 249.3 [248.4–249.9] | 249.9 [249.4–250.2] | 609.8 [606.4–689.8] | --- | 26.56s [26.54s–26.61s] | 462.1 [458.9–463.2] | 0.54x | 0.54x | 0.54x | 1.32x | --- | 57.5x |
| divrec | recursive | 5.50 [5.31–5.50] | 1.12 [1.12–1.22] | 4.83 [4.74–4.85] | 14.6 [14.5–14.7] | --- | 36.5 [36.5–37.0] | 7.56 [7.45–7.63] | 0.73x | 0.15x | 0.64x | 1.93x | --- | 4.83x |
| gcbench | allocation | 198.5 [197.3–201.1] | 81.4 [81.0–82.2] | 71.4 [70.8–71.4] | 937.2 [936.8–938.9] | --- | 552.1 [546.4–570.9] | 23.6 [23.5–23.7] | 8.43x | 3.46x | 3.03x | 39.8x | --- | 23.4x |
| paraffins | combinat | 0.190 [0.189–0.191] | 0.157 [0.156–0.160] | 0.047 [0.047–0.048] | 2.30 [2.30–2.32] | --- | 2.53 [2.51–2.54] | 0.979 [0.973–0.986] | 0.19x | 0.16x | 0.05x | 2.35x | --- | 2.58x |
| pnpoly | numeric | 11.7 [11.7–11.8] | 4.25 [4.16–4.29] | 1.94 [1.93–1.96] | 129.9 [129.0–130.0] | --- | 202.7 [202.0–203.1] | 5.88 [5.87–5.92] | 2.00x | 0.72x | 0.33x | 22.1x | --- | 34.5x |
| puzzle | search | 13.5 [13.5–13.7] | 14.8 [14.6–14.9] | 1.27 [1.26–1.30] | 30.4 [30.4–31.3] | --- | 29.2 [29.1–29.2] | 3.31 [3.30–3.32] | 4.08x | 4.46x | 0.38x | 9.18x | --- | 8.80x |
| quicksort | sorting | 0.727 [0.715–0.776] | 0.661 [0.657–0.703] | 0.197 [0.197–0.201] | 17.7 [17.6–17.7] | --- | 19.4 [19.1–91.1] | 1.66 [1.65–1.69] | 0.44x | 0.40x | 0.12x | 10.6x | --- | 11.7x |
| ray | numeric | 0.212 [0.209–0.215] | 0.211 [0.207–0.211] | 0.170 [0.169–0.183] | 6.24 [6.19–6.37] | --- | 13.8 [13.7–13.8] | 3.70 [3.61–3.73] | 0.06x | 0.06x | 0.05x | 1.69x | --- | 3.73x |

### JetStream

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | mvpjs (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | mvpjs/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 8.28 [8.04–8.74] | 6.98 [6.92–7.21] | 0.515 [0.509–0.549] | 313.6 [312.7–314.0] | --- | 215.8 [215.2–217.3] | 17.7 [17.7–17.9] | 0.47x | 0.39x | 0.03x | 17.7x | --- | 12.2x |
| navier_stokes | numeric | 86.5 [86.4–86.6] | 69.6 [69.5–69.8] | 46.6 [46.3–46.8] | 184.7 [184.1–185.7] | --- | 98.4 [97.9–98.4] | 14.1 [14.1–14.3] | 6.14x | 4.94x | 3.31x | 13.1x | --- | 6.99x |
| splay | data | 350.7 [349.0–352.3] | 330.9 [327.9–334.1] | 19.5 [19.4–19.8] | 373.0 [371.7–375.8] | --- | 147.4 [147.0–147.5] | 19.8 [19.3–19.9] | 17.7x | 16.7x | 0.98x | 18.9x | --- | 7.45x |
| hashmap | data | 71.2 [71.0–71.3] | 36.4 [36.4–37.1] | 2.84 [2.76–2.84] | 1.45s [1.44s–1.46s] | --- | 312.9 [311.6–314.6] | 15.4 [15.3–15.4] | 4.61x | 2.36x | 0.18x | 93.9x | --- | 20.3x |
| crypto_sha1 | crypto | 41.3 [41.2–41.4] | 24.0 [23.7–24.1] | 2.64 [2.61–2.68] | 264.1 [263.8–353.9] | --- | 218.3 [217.4–218.8] | 8.72 [8.68–8.77] | 4.73x | 2.76x | 0.30x | 30.3x | --- | 25.0x |
| raytrace3d | 3d | 68.7 [67.6–68.8] | 15.1 [14.9–15.1] | 2.19 [2.18–2.19] | 531.9 [530.7–535.8] | --- | 163.4 [162.4–163.8] | 18.3 [18.3–18.4] | 3.75x | 0.82x | 0.12x | 29.0x | --- | 8.90x |

### Text

| Benchmark | Category | MIR (untyped) (ms) | MIR (typed) (ms) | C2MIR (ms) | LambdaJS (ms) | mvpjs (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped)/Node | MIR (typed)/Node | C2MIR/Node | LambdaJS/Node | mvpjs/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 238.2 [236.7–238.3] | 53.4 [53.3–53.5] | 12.9 [12.9–13.1] | 1.98s [1.98s–1.99s] | --- | 610.5 [609.9–612.6] | 40.0 [39.2–40.0] | 5.96x | 1.34x | 0.32x | 49.7x | --- | 15.3x |
| microdiff | data-diff | 52.8 [52.4–53.0] | 58.3 [58.2–60.2] | 2.64 [2.63–2.66] | 823.9 [821.6–825.4] | --- | 110.0 [108.5–195.0] | 16.7 [16.3–17.2] | 3.15x | 3.49x | 0.16x | 49.3x | --- | 6.58x |
| hyphen | hyphenation | 69.3 [68.8–70.2] | 13.3 [13.2–13.3] | 1.47 [1.47–1.47] | 101.3 [100.0–102.8] | --- | 80.0 [79.3–80.1] | 6.72 [6.58–7.64] | 10.3x | 1.98x | 0.22x | 15.1x | --- | 11.9x |
| prettier_ast | formatting | 987.1 [968.2–996.3] | 641.7 [604.5–644.6] | 41.7 [41.3–42.0] | 2.58s [2.56s–2.58s] | --- | 1.41s [1.41s–1.42s] | 100.6 [100.5–101.0] | 9.82x | 6.38x | 0.41x | 25.7x | --- | 14.1x |
| text_search | search | 1.52s [1.49s–1.55s] | 1.75s [1.75s–1.79s] | 536.1 [535.9–538.1] | 59.34s [59.34s–59.65s] | --- | 39.09s [38.95s–39.14s] | 778.5 [778.2–779.8] | 1.95x | 2.25x | 0.69x | 76.2x | --- | 50.2x |
| three_way_merge | merge | 2.81s [2.80s–2.82s] | 2.12s [2.12s–2.12s] | 2.13s [2.12s–2.14s] | 9.96s [9.96s–10.13s] | --- | 6.13s [6.12s–6.17s] | 970.7 [963.4–973.2] | 2.90x | 2.18x | 2.20x | 10.3x | --- | 6.31x |
| log_pipeline | log-processing | 4.42s [4.42s–4.44s] | 2.08s [2.03s–2.13s] | 624.3 [619.9–624.7] | 14.25s [14.23s–14.37s] | --- | 9.50s [9.41s–9.53s] | 944.5 [943.6–983.3] | 4.68x | 2.20x | 0.66x | 15.1x | --- | 10.1x |

---

## Part 2 — End-to-end time (wall clock, auto tier)

Wall clock from process invocation to exit, so **every engine pays its own startup and compilation inside the number**. The MIR columns use the shipped auto tier -- no `LAMBDA_TIER` override -- which is what `lambda.exe run script.ls` actually does.

This set exists because the two questions are different. Part 1 asks how fast the compiled workload runs; part 2 asks how long it takes to run the script. Timing the auto tier under part 1's rules would charge Lambda for JIT compilation performed *inside* the measured region while crediting Node.js with a post-warmup figure -- comparing two different things. Here the accounting is the same for everyone.

Same processes, where possible: the reference engines report their wall and `__TIMING__` figures from the *same* run, so parts 1 and 2 are two readings of one launch. Only the MIR columns are re-run, because part 1 pins the JIT and part 2 must use the auto tier.

⚠ Short workloads are dominated by fixed process startup here, so a row whose part-1 time is a fraction of a millisecond says more about executable launch cost than about the language. Read part 2 by the longer rows.

### Summary

| Suite | Total | Timed MIR (untyped, auto) | Timed MIR (typed, auto) | Timed C2MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR (untyped, auto)/Node geo | MIR (typed, auto)/Node geo | C2MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 10 | 10 | 10 | 10 | 10 | 10 | 0.44x | 0.45x | 1.01x | 0.78x | 0.41x |
| AWFY | 14 | 14 | 14 | 14 | 14 | 14 | 14 | 0.99x | 0.98x | 0.91x | 4.34x | 0.81x |
| BENG | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 0.54x | 0.59x | 1.02x | 1.48x | 0.34x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 0.74x | 0.65x | 0.72x | 3.45x | 2.87x |
| LARCENY | 11 | 11 | 11 | 11 | 11 | 11 | 11 | 0.84x | 0.72x | 0.99x | 2.37x | 1.99x |
| JetStream | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 2.40x | 1.90x | 0.90x | 11.1x | 3.64x |
| Text | 7 | 7 | 7 | 7 | 7 | 7 | 7 | 2.95x | 1.93x | 0.93x | 17.7x | 6.76x |
| **Overall** | 63 | 63 | 63 | 63 | 63 | 63 | 63 | 0.93x | 0.84x | 0.93x | 3.23x | 1.28x |

> Ratio < 1.0 means the engine finished the whole run faster than Node.js.

### R7RS

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 17.2 [16.8–17.7] | 17.8 [17.6–18.0] | 47.4 [46.7–63.9] | 26.0 [25.8–26.4] | 25.3 [24.6–25.4] | 47.2 [46.9–94.4] | 0.36x | 0.38x | 1.00x | 0.55x | 0.54x |
| fibfp | recursive | 19.5 [19.3–19.7] | 18.5 [18.5–18.7] | 49.1 [48.0–83.2] | 27.3 [27.0–27.5] | 26.1 [25.9–26.2] | 46.0 [45.8–46.8] | 0.42x | 0.40x | 1.07x | 0.59x | 0.57x |
| tak | recursive | 17.8 [17.7–18.1] | 17.2 [17.2–17.7] | 46.0 [45.9–46.2] | 25.5 [24.2–25.6] | 8.68 [8.62–9.10] | 45.4 [44.6–45.6] | 0.39x | 0.38x | 1.01x | 0.56x | 0.19x |
| cpstak | closure | 17.7 [17.7–17.7] | 17.3 [17.0–18.0] | 46.5 [46.1–46.6] | 26.2 [25.7–26.9] | 11.8 [11.6–12.3] | 44.9 [44.4–45.1] | 0.40x | 0.39x | 1.04x | 0.58x | 0.26x |
| sum | iterative | 17.0 [17.0–17.2] | 17.3 [17.1–17.4] | 46.2 [45.1–46.6] | 25.1 [25.1–25.5] | 37.4 [37.0–37.5] | 45.3 [45.2–46.4] | 0.38x | 0.38x | 1.02x | 0.55x | 0.83x |
| sumfp | iterative | 15.7 [15.5–16.4] | 15.5 [15.4–15.9] | 44.9 [44.9–45.0] | 23.9 [23.8–24.2] | 9.12 [9.05–9.57] | 44.8 [44.3–45.3] | 0.35x | 0.35x | 1.00x | 0.53x | 0.20x |
| nqueens | backtrack | 24.5 [24.4–26.1] | 28.7 [28.5–29.2] | 46.9 [46.9–47.3] | 112.5 [111.9–112.8] | 14.2 [14.0–14.5] | 46.5 [45.5–46.6] | 0.53x | 0.62x | 1.01x | 2.42x | 0.30x |
| fft | numeric | 31.7 [31.6–31.8] | 40.8 [40.4–41.4] | 46.1 [45.4–46.4] | 77.9 [77.8–78.1] | 8.52 [8.31–9.02] | 45.6 [45.3–45.9] | 0.70x | 0.90x | 1.01x | 1.71x | 0.19x |
| mbrot | numeric | 20.1 [19.5–20.5] | 20.1 [20.1–20.2] | 45.8 [45.7–45.8] | 44.6 [44.5–45.3] | 24.3 [23.7–24.6] | 46.6 [45.1–47.2] | 0.43x | 0.43x | 0.98x | 0.96x | 0.52x |
| ack | recursive | 28.8 [28.8–29.5] | 27.1 [26.7–27.5] | 56.9 [56.5–57.3] | 38.0 [37.4–38.3] | 107.3 [107.0–108.0] | 57.0 [56.6–57.0] | 0.51x | 0.48x | 1.00x | 0.67x | 1.88x |

### AWFY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 18.7 [18.4–18.8] | 19.6 [19.2–20.1] | 45.7 [45.0–46.7] | 33.7 [33.6–34.2] | 6.26 [6.11–6.73] | 44.9 [44.8–47.3] | 0.42x | 0.44x | 1.02x | 0.75x | 0.14x |
| permute | micro | 18.0 [17.9–18.2] | 20.4 [20.1–20.5] | 45.8 [45.7–45.8] | 43.5 [42.8–43.8] | 7.59 [7.51–8.26] | 45.9 [45.6–46.0] | 0.39x | 0.45x | 1.00x | 0.95x | 0.17x |
| queens | micro | 22.8 [22.7–23.2] | 30.1 [30.0–30.4] | 45.8 [45.1–45.8] | 45.9 [45.6–46.7] | 6.63 [6.52–7.30] | 45.1 [45.1–45.2] | 0.50x | 0.67x | 1.01x | 1.02x | 0.15x |
| towers | micro | 22.3 [22.2–22.4] | 25.0 [24.9–25.2] | 45.8 [45.7–46.2] | 53.9 [53.7–54.2] | 8.22 [8.03–9.20] | 46.3 [46.1–46.5] | 0.48x | 0.54x | 0.99x | 1.16x | 0.18x |
| bounce | micro | 22.9 [22.8–23.0] | 26.8 [26.3–27.3] | 46.0 [45.8–46.4] | 160.3 [159.5–160.5] | 8.00 [7.60–8.32] | 46.1 [46.0–46.2] | 0.50x | 0.58x | 1.00x | 3.48x | 0.17x |
| list | micro | 21.8 [21.6–22.5] | 39.4 [21.5–101.2] | 46.0 [45.9–46.3] | 40.6 [40.3–40.7] | 7.20 [6.90–7.37] | 45.4 [45.2–46.0] | 0.48x | 0.87x | 1.01x | 0.89x | 0.16x |
| storage | micro | 18.6 [18.4–19.3] | 19.3 [19.3–19.9] | 45.8 [45.7–46.2] | 154.1 [153.3–156.7] | 8.89 [8.43–9.29] | 45.4 [45.3–46.1] | 0.41x | 0.42x | 1.01x | 3.39x | 0.20x |
| mandelbrot | compute | 59.2 [59.2–59.4] | 59.6 [59.2–59.6] | 76.5 [76.3–76.5] | 118.3 [118.1–118.4] | 876.4 [875.3–888.9] | 76.3 [75.9–77.8] | 0.78x | 0.78x | 1.00x | 1.55x | 11.5x |
| nbody | compute | 54.3 [54.2–55.0] | 63.7 [63.2–63.9] | 48.2 [47.8–48.9] | 566.4 [562.5–567.9] | 166.0 [165.7–166.4] | 50.6 [50.6–52.4] | 1.07x | 1.26x | 0.95x | 11.2x | 3.28x |
| richards | macro | 432.6 [431.4–519.9] | 402.1 [400.0–432.4] | 78.0 [77.3–78.4] | 1.17s [1.17s–1.19s] | 200.0 [197.9–201.3] | 92.1 [91.9–93.1] | 4.70x | 4.36x | 0.85x | 12.7x | 2.17x |
| json | macro | 55.1 [55.0–55.2] | 52.2 [51.8–52.3] | 53.0 [52.5–53.4] | 334.0 [331.0–338.2] | 20.2 [19.8–28.0] | 49.8 [48.9–51.3] | 1.10x | 1.05x | 1.06x | 6.70x | 0.40x |
| deltablue | macro | 274.1 [273.1–274.1] | 155.4 [155.2–245.9] | 54.4 [54.2–55.0] | 755.4 [754.1–757.1] | 107.2 [106.7–107.9] | 58.1 [58.0–59.2] | 4.72x | 2.67x | 0.94x | 13.0x | 1.85x |
| havlak | macro | 201.5 [201.0–203.8] | 179.5 [179.3–179.8] | 56.1 [55.7–57.2] | 14.24s [14.23s–14.31s] | 3.29s [3.28s–3.31s] | 144.5 [136.2–150.4] | 1.39x | 1.24x | 0.39x | 98.5x | 22.8x |
| cd | macro | 669.5 [664.6–672.7] | 263.0 [262.4–271.1] | 69.0 [68.7–69.1] | 4.10s [4.09s–4.10s] | 1.04s [978.4–1.06s] | 82.7 [82.0–84.1] | 8.10x | 3.18x | 0.83x | 49.5x | 12.6x |

### BENG

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 31.1 [30.6–31.1] | 25.1 [24.9–25.2] | 48.8 [48.2–49.0] | 67.8 [67.8–143.1] | 30.1 [30.0–30.2] | 46.4 [46.2–46.5] | 0.67x | 0.54x | 1.05x | 1.46x | 0.65x |
| fannkuch | permutation | 23.0 [22.9–23.1] | 27.9 [27.7–28.0] | 46.0 [45.4–46.0] | 70.6 [70.1–70.9] | 13.4 [12.9–14.0] | 46.5 [45.7–46.7] | 0.50x | 0.60x | 0.99x | 1.52x | 0.29x |
| fasta | generation | 27.3 [27.1–27.3] | 30.3 [29.6–30.8] | 46.4 [46.1–46.5] | 74.1 [73.0–74.5] | 15.0 [14.8–15.5] | 49.1 [49.0–49.4] | 0.56x | 0.62x | 0.94x | 1.51x | 0.31x |
| knucleotide | hashing | 33.4 [33.0–33.7] | 55.8 [55.6–56.0] | 46.4 [46.3–47.3] | 65.5 [65.3–72.2] | 13.9 [13.9–14.4] | 47.9 [47.5–48.2] | 0.70x | 1.16x | 0.97x | 1.37x | 0.29x |
| pidigits | bignum | 22.7 [22.7–22.8] | 22.2 [22.1–22.3] | 47.7 [47.2–48.1] | 53.6 [53.4–53.7] | 6.14 [5.85–6.73] | 44.1 [43.5–44.3] | 0.51x | 0.50x | 1.08x | 1.21x | 0.14x |
| regexredux | regex | 17.2 [16.8–17.3] | 17.0 [16.8–17.3] | 48.7 [48.4–49.0] | 50.6 [49.9–50.6] | 11.8 [11.6–12.3] | 44.9 [44.4–45.0] | 0.38x | 0.38x | 1.08x | 1.13x | 0.26x |
| revcomp | string | 24.1 [23.9–24.6] | 24.2 [24.0–24.5] | 46.8 [46.6–46.8] | 59.5 [59.2–59.9] | 8.86 [8.69–9.21] | 45.8 [45.2–46.3] | 0.53x | 0.53x | 1.02x | 1.30x | 0.19x |
| spectralnorm | numeric | 26.1 [26.0–26.3] | 30.8 [29.8–31.0] | 46.4 [46.1–46.6] | 132.7 [131.5–134.9] | 70.1 [70.0–71.1] | 46.6 [45.8–47.0] | 0.56x | 0.66x | 1.00x | 2.85x | 1.50x |

### KOSTYA

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 224.1 [224.1–225.4] | 223.6 [221.9–224.8] | 73.7 [72.5–73.8] | 1.80s [1.79s–1.80s] | 892.8 [889.7–923.5] | 80.0 [80.0–80.6] | 2.80x | 2.80x | 0.92x | 22.5x | 11.2x |
| matmul | numeric | 30.4 [29.4–30.4] | 32.2 [31.7–33.1] | 52.7 [52.0–53.3] | 283.0 [282.0–285.3] | 545.6 [545.3–633.6] | 59.8 [59.7–60.7] | 0.51x | 0.54x | 0.88x | 4.73x | 9.12x |
| primes | numeric | 34.4 [34.4–34.5] | 21.7 [21.6–21.8] | 47.0 [46.9–47.1] | 87.3 [86.9–87.6] | 101.2 [101.0–101.9] | 48.7 [48.5–48.9] | 0.71x | 0.45x | 0.97x | 1.79x | 2.08x |
| base64 | string | 41.5 [41.2–41.7] | 41.1 [39.9–41.3] | 46.8 [46.5–46.8] | 531.7 [531.4–540.7] | 164.4 [162.5–166.8] | 61.7 [61.6–61.8] | 0.67x | 0.67x | 0.76x | 8.61x | 2.66x |
| levenshtein | string | 41.1 [40.6–41.6] | 33.6 [33.4–33.6] | 47.2 [47.0–47.2] | 120.6 [120.6–121.5] | 61.3 [60.6–61.6] | 48.2 [47.9–48.3] | 0.85x | 0.70x | 0.98x | 2.50x | 1.27x |
| json_gen | data | 54.6 [54.4–56.4] | 39.4 [38.9–39.8] | 47.1 [47.1–47.6] | 64.2 [64.0–65.2] | 26.6 [25.9–26.9] | 50.8 [50.7–50.9] | 1.08x | 0.78x | 0.93x | 1.26x | 0.52x |
| collatz | numeric | 293.3 [292.8–295.9] | 291.9 [288.2–292.2] | 264.4 [262.7–268.3] | 1.62s [1.62s–1.62s] | 6.24s [6.22s–6.27s] | 1.46s [1.46s–1.46s] | 0.20x | 0.20x | 0.18x | 1.11x | 4.26x |

### LARCENY

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 261.9 [261.4–262.1] | 193.8 [193.4–199.8] | 105.5 [105.1–106.2] | 6.10s [6.08s–6.26s] | 2.18s [2.18s–2.18s] | 113.2 [113.2–113.9] | 2.31x | 1.71x | 0.93x | 53.9x | 19.3x |
| array1 | array | 18.0 [17.9–18.1] | 18.6 [18.2–18.7] | 46.2 [45.3–46.3] | 46.9 [46.9–47.6] | 42.8 [42.4–42.8] | 46.1 [45.5–46.2] | 0.39x | 0.40x | 1.00x | 1.02x | 0.93x |
| deriv | symbolic | 59.7 [59.2–59.9] | 29.5 [29.4–30.3] | 49.1 [49.1–49.4] | 109.8 [108.5–110.1] | 65.6 [65.4–65.9] | 48.7 [48.2–49.3] | 1.23x | 0.61x | 1.01x | 2.26x | 1.35x |
| diviter | iterative | 267.4 [267.1–267.5] | 267.8 [267.1–268.2] | 295.1 [294.7–295.9] | 637.3 [631.9–716.6] | 26.56s [26.54s–26.62s] | 506.6 [504.5–508.3] | 0.53x | 0.53x | 0.58x | 1.26x | 52.4x |
| divrec | recursive | 22.6 [22.3–22.8] | 17.9 [17.9–18.4] | 50.4 [49.9–50.5] | 39.9 [39.6–39.9] | 42.9 [42.8–43.0] | 51.1 [51.1–51.5] | 0.44x | 0.35x | 0.99x | 0.78x | 0.84x |
| gcbench | allocation | 225.9 [225.8–312.0] | 129.5 [128.2–130.4] | 117.8 [117.3–118.3] | 975.4 [975.0–976.5] | 564.3 [558.3–584.0] | 67.3 [67.1–67.5] | 3.36x | 1.92x | 1.75x | 14.5x | 8.38x |
| paraffins | combinat | 44.2 [44.1–44.3] | 63.1 [62.9–63.5] | 46.8 [46.8–47.1] | 66.8 [66.2–67.4] | 8.65 [8.61–9.32] | 45.4 [44.7–45.4] | 0.97x | 1.39x | 1.03x | 1.47x | 0.19x |
| pnpoly | numeric | 31.6 [31.2–31.8] | 25.1 [24.1–25.2] | 47.8 [47.7–48.3] | 166.4 [166.0–168.4] | 209.5 [208.7–209.6] | 49.7 [49.5–50.0] | 0.64x | 0.51x | 0.96x | 3.35x | 4.22x |
| puzzle | search | 37.6 [37.3–38.9] | 39.2 [38.9–39.4] | 47.0 [46.7–47.7] | 67.6 [67.2–68.5] | 35.6 [35.6–35.8] | 47.0 [46.9–47.6] | 0.80x | 0.84x | 1.00x | 1.44x | 0.76x |
| quicksort | sorting | 22.3 [22.2–22.7] | 25.1 [25.0–25.3] | 46.7 [46.6–46.9] | 49.6 [49.3–50.4] | 39.7 [26.2–98.2] | 46.0 [45.6–46.2] | 0.48x | 0.55x | 1.01x | 1.08x | 0.86x |
| ray | numeric | 32.1 [31.6–32.5] | 25.3 [25.2–25.5] | 46.6 [46.3–46.7] | 47.9 [47.3–48.7] | 20.1 [19.8–20.4] | 48.1 [47.8–48.2] | 0.67x | 0.53x | 0.97x | 1.00x | 0.42x |

### JetStream

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cube3d | 3d | 119.3 [104.5–176.1] | 114.2 [114.0–115.7] | 49.4 [49.1–49.7] | 761.8 [761.5–764.6] | 222.8 [222.1–224.3] | 63.2 [62.7–65.4] | 1.89x | 1.81x | 0.78x | 12.1x | 3.52x |
| navier_stokes | numeric | 222.0 [220.6–222.3] | 201.0 [200.3–202.0] | 95.0 [94.7–95.3] | 416.6 [415.0–418.1] | 109.5 [108.8–109.6] | 60.6 [60.2–61.0] | 3.67x | 3.32x | 1.57x | 6.88x | 1.81x |
| splay | data | 421.6 [420.7–529.4] | 403.2 [402.7–408.1] | 67.6 [67.5–67.8] | 940.4 [934.8–941.6] | 566.5 [565.5–567.0] | 94.2 [93.6–94.2] | 4.48x | 4.28x | 0.72x | 9.99x | 6.02x |
| hashmap | data | 111.4 [111.2–111.8] | 72.9 [72.8–73.3] | 48.7 [48.4–49.0] | 1.56s [1.55s–1.56s] | 320.5 [319.1–321.7] | 60.1 [60.1–60.3] | 1.85x | 1.21x | 0.81x | 25.9x | 5.33x |
| crypto_sha1 | crypto | 74.8 [74.8–75.5] | 59.6 [59.3–59.8] | 49.2 [49.1–49.3] | 359.4 [357.3–449.0] | 225.0 [224.1–225.7] | 53.0 [52.9–53.1] | 1.41x | 1.12x | 0.93x | 6.78x | 4.24x |
| raytrace3d | 3d | 150.5 [149.5–150.8] | 84.9 [84.0–85.7] | 52.7 [52.5–53.2] | 802.0 [798.9–809.3] | 170.3 [169.5–170.9] | 63.4 [63.2–64.2] | 2.37x | 1.34x | 0.83x | 12.7x | 2.69x |

### Text

| Benchmark | Category | MIR (untyped, auto) (ms) | MIR (typed, auto) (ms) | C2MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR (untyped, auto)/Node | MIR (typed, auto)/Node | C2MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| fast_diff | text-diff | 266.8 [266.3–267.0] | 81.3 [81.3–81.7] | 58.9 [58.6–59.3] | 2.62s [2.62s–2.63s] | 618.6 [618.0–620.5] | 84.9 [83.8–84.9] | 3.14x | 0.96x | 0.69x | 30.9x | 7.29x |
| microdiff | data-diff | 90.2 [90.1–90.5] | 96.7 [96.4–97.3] | 51.9 [51.8–52.0] | 899.3 [896.5–899.4] | 116.6 [115.5–201.8] | 60.9 [60.6–61.2] | 1.48x | 1.59x | 0.85x | 14.8x | 1.92x |
| hyphen | hyphenation | 124.6 [123.7–124.7] | 75.9 [75.9–76.3] | 77.4 [76.9–78.4] | 363.9 [360.6–364.1] | 101.5 [100.5–102.0] | 58.2 [58.2–58.7] | 2.14x | 1.30x | 1.33x | 6.25x | 1.74x |
| prettier_ast | formatting | 1.12s [1.11s–1.12s] | 745.1 [738.2–755.1] | 105.0 [104.2–106.1] | 2.75s [2.73s–2.76s] | 1.42s [1.42s–1.43s] | 146.2 [146.0–147.2] | 7.63x | 5.10x | 0.72x | 18.8x | 9.73x |
| text_search | search | 1.64s [1.64s–1.67s] | 1.84s [1.81s–1.84s] | 583.9 [583.4–585.9] | 59.42s [59.42s–59.75s] | 39.10s [38.96s–39.15s] | 823.6 [823.6–825.2] | 2.00x | 2.23x | 0.71x | 72.1x | 47.5x |
| three_way_merge | merge | 2.87s [2.87s–2.89s] | 2.15s [2.15s–2.16s] | 2.18s [2.17s–2.19s] | 10.00s [10.00s–10.18s] | 6.13s [6.13s–6.18s] | 1.02s [1.01s–1.02s] | 2.83x | 2.12x | 2.15x | 9.85x | 6.04x |
| log_pipeline | log-processing | 4.52s [4.48s–4.54s] | 2.12s [2.11s–2.12s] | 673.8 [669.4–674.4] | 14.34s [14.32s–14.46s] | 9.53s [9.44s–9.57s] | 997.8 [996.8–1.04s] | 4.53x | 2.12x | 0.68x | 14.4x | 9.55x |

