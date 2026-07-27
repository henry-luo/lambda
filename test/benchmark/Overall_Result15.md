# Result15

- **Date:** 2026-07-27
- **Platform:** Darwin arm64
- **Lambda commit:** `770eb273abecbe57bafc291e62f8af37d582eff8`
- **Lambda build:** clean release build (`make release`)
- **Instrumentation check:** not_recorded
- **Node.js:** v22.13.0
- **QuickJS:** 2025-09-13
- **Methodology:** 3 run(s) per benchmark, median of self-reported `__TIMING__` milliseconds, timeout 180s per run
- **Engines in this report:** MIR, LambdaJS, QuickJS, Node.js
- **Results source:** `test/benchmark/benchmark_results_v15.json`

JetStream JavaScript-engine wrappers are standardized to an explicit x8 loop over the detected benchmark function. They do not use per-file `Benchmark.runIteration()` counts, because those counts drift across JetStream files.

> **This snapshot supersedes an earlier Result15 capture**, archived as `Overall_Result15_pre_inline_bitcast.md` / `benchmark_results_v15_pre_inline_bitcast.json`. That capture measured commit `7703c784c` (per-callee dynamic-call entries) and recorded five regressed numeric rows, diagnosed as code/data **placement** around the `lambda_mir_double_bits` / `lambda_mir_bits_double` helper calls — executed code provably identical on both sides. This re-run measures the durable fix for that diagnosis: the bit reinterpretation is now emitted **inline in MIR** (typed store + differently-typed load through `Context::mir_bitcast_scratch`), removing the JIT→helper edge entirely. Same machine, same engine stack, same protocol, so all three of Result14 / archived-Result15 / this capture are apples-to-apples. Diagnosis and landing record: `vibe/Lambda_Impl_Tune_JS_Dynamic_Call.md` §0.1–§0.1.2.
>
> **Headline (dedup geo vs Node, all from this table's metric):**
>
> | | Result14 | Result15 (archived) | **Result15 (this)** |
> |---|---:|---:|---:|
> | LambdaJS/Node | 14.9x | 14.4x | **13.8x** |
> | MIR/Node | 2.90x | 2.91x | 2.92x |
> | QuickJS/Node (untouched control) | 7.28x | 7.36x | 7.39x |
>
> LambdaJS improves 4.3% while the untouched QuickJS control moves +0.4%, so the gain is real rather than capture drift. This was a quiet capture — the previous one had the unchanged Node control drifting +6–8.5% on some rows, which is why its sub-8% movers needed interleaved A/B to rate. 13.8x is the best LambdaJS/Node geo recorded to date (Result12 15.1x → Result14 14.9x → here).
>
> **All five regressed rows are recovered and now beat Result14** (LambdaJS ms, median of 3):
>
> | row | Result14 | Result15 archived | **this** | vs archived | vs Result14 |
> |---|---:|---:|---:|---:|---:|
> | beng/mandelbrot | 71.18 | 85.44 | **49.46** | 0.579 | 0.695 |
> | r7rs/sum | 11.91 | 14.19 | **10.09** | 0.711 | 0.847 |
> | r7rs/sumfp | 1.20 | 1.41 | **0.96** | 0.685 | 0.803 |
> | larceny/diviter | 10176.7 | 11277.4 | **7286.7** | 0.646 | 0.716 |
> | awfy/mandelbrot | 371.0 | 398.3 | **297.0** | 0.746 | 0.801 |
>
> The helper edge was therefore a standing cost, not merely a placement liability: removing it puts every one of these rows 15–30% below the pre-regression Result14 baseline. **The call-dominated rows kept their dynamic-call win** and improved further (r7rs/fib 34.10 → 20.64 → 19.01, fibfp 34.15 → 20.83 → 18.24, cpstak 5.59 → 3.57 → 2.97, tak 2.81 → 1.81 → 1.43).
>
> **MIR is flat in aggregate (2.91x → 2.92x) but moved where the change applies** — the same inline emission is shared by the Lambda transpiler, and its float-heavy rows gained: beng/mandelbrot 134.5 → 118.3 ms (0.88), r7rs/fibfp 5.30 → 4.80 (0.91), r7rs/sum 4.04 → 3.73 (0.92). Non-float MIR rows drift symmetrically within ±6%, which is this capture's per-row noise floor for short benchmarks; do not read individual sub-6% MIR movements as signal.
>
> **Binary provenance:** commit `770eb273a` ("tune7 round3 impl") is the inline double↔bits landing — its only source changes are the four files of that fix (`lambda.h`, `mir_emitter_shared.hpp`, `transpile-mir.cpp`, `js_mir_calls_boxing_types.cpp`) plus the MT7 budget re-baseline. The measured binary is a clean release build of exactly that commit, `cmp`-identical to the one used for the interleaved A/B in §0.1.2. The archived pre-fix Result15 files are also carried in that same commit, which is why they are recoverable from git as well as from the `_pre_inline_bitcast` copies.

---

## Summary

| Suite | Total | Timed MIR | Timed LambdaJS | Timed QuickJS | Timed Node.js | MIR/Node geo | LambdaJS/Node geo | QuickJS/Node geo |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| R7RS | 10 | 9 | 10 | 9 | 10 | 1.02x | 5.11x | 6.35x |
| AWFY | 14 | 13 | 14 | 14 | 14 | 1.86x | 20.5x | 5.22x |
| BENG | 10 | 9 | 10 | 7 | 10 | 1.87x | 7.97x | 4.18x |
| KOSTYA | 7 | 7 | 7 | 7 | 7 | 7.35x | 15.5x | 11.7x |
| LARCENY | 12 | 12 | 12 | 12 | 12 | 6.13x | 13.5x | 13.9x |
| JetStream | 9 | 9 | 9 | 7 | 9 | 8.59x | 68.0x | 12.5x |
| **Overall dedup** | **56** | **53** | **56** | **50** | **56** | **2.92x** | **13.8x** | **7.39x** |
| Overall raw | 62 | 59 | 62 | 56 | 62 | 3.22x | 15.0x | 7.98x |

> **Overall dedup** is the default headline metric: duplicate benchmark names across suites are counted once, using the best timed value per engine. **Overall raw** keeps the row-weighted value for auditability.
> Ratio < 1.0 means the engine is faster than Node.js on matched timed rows; ratio > 1.0 means Node.js is faster.

---

## Notable Results

- Missing timings: **9** cells
- MIR missing: r7rs/fft (exit_1), awfy/list (wrong_output), beng/pidigits (exit_1)
- QuickJS missing: r7rs/ack (exit_1), beng/knucleotide (exit_1), beng/regexredux (exit_1), beng/revcomp (exit_1), jetstream/cube3d (exit_1), jetstream/raytrace3d (exit_1)
- Deduplicated benchmark names: mandelbrot (awfy/beng), nbody (awfy/beng/jetstream), richards (awfy/jetstream), deltablue (awfy/jetstream), primes (kostya/larceny)

### Largest LambdaJS / Node.js Ratios

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| jetstream/hashmap | 63.23s | 56.2 | 1124x |
| awfy/havlak | 77.16s | 97.3 | 793x |
| awfy/cd | 9.50s | 36.7 | 259x |
| jetstream/navier_stokes | 5.37s | 39.2 | 137x |
| beng/spectralnorm | 289.9 | 2.60 | 112x |
| jetstream/crypto_sha1 | 507.3 | 6.93 | 73.2x |
| awfy/deltablue | 803.1 | 12.0 | 67.1x |
| larceny/triangl | 3.93s | 67.3 | 58.5x |

### LambdaJS Faster Than Node.js

| Benchmark | LambdaJS | Node.js | Ratio |
|---|---:|---:|---:|
| beng/pidigits | 0.316 | 1.99 | 0.16x |

---

## R7RS

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| fib | recursive | 6.99 | 19.0 | 17.7 | 1.72 | 4.08x | 11.1x | 10.3x |
| fibfp | recursive | 4.80 | 18.2 | 17.6 | 1.67 | 2.88x | 10.9x | 10.5x |
| tak | recursive | 0.492 | 1.43 | 2.62 | 0.728 | 0.68x | 1.96x | 3.60x |
| cpstak | closure | 0.984 | 2.97 | 5.29 | 0.920 | 1.07x | 3.22x | 5.75x |
| sum | iterative | 3.73 | 10.1 | 29.0 | 1.12 | 3.33x | 9.01x | 25.9x |
| sumfp | iterative | 0.062 | 0.964 | 3.49 | 0.846 | 0.07x | 1.14x | 4.13x |
| nqueens | backtrack | 1.30 | 16.9 | 7.38 | 1.73 | 0.75x | 9.73x | 4.26x |
| fft | numeric | --- | 10.7 | 2.53 | 1.50 | --- | 7.12x | 1.68x |
| mbrot | numeric | 0.763 | 8.09 | 16.9 | 1.74 | 0.44x | 4.65x | 9.74x |
| ack | recursive | 21.5 | 60.9 | --- | 12.7 | 1.70x | 4.81x | --- |

## AWFY

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| sieve | micro | 0.474 | 0.434 | 0.584 | 0.363 | 1.31x | 1.20x | 1.61x |
| permute | micro | 0.531 | 6.78 | 1.55 | 0.804 | 0.66x | 8.43x | 1.92x |
| queens | micro | 0.486 | 4.10 | 0.988 | 0.623 | 0.78x | 6.58x | 1.59x |
| towers | micro | 0.763 | 15.8 | 2.13 | 1.12 | 0.68x | 14.2x | 1.91x |
| bounce | micro | 0.806 | 3.37 | 0.812 | 0.503 | 1.60x | 6.71x | 1.62x |
| list | micro | --- | 3.26 | 0.857 | 0.464 | --- | 7.03x | 1.85x |
| storage | micro | 0.630 | 13.0 | 2.15 | 0.606 | 1.04x | 21.4x | 3.55x |
| mandelbrot | compute | 43.9 | 297.0 | 842.0 | 29.7 | 1.48x | 10.0x | 28.4x |
| nbody | compute | 79.2 | 269.3 | 158.6 | 5.37 | 14.8x | 50.2x | 29.5x |
| richards | macro | 178.4 | 1.17s | 190.9 | 47.1 | 3.79x | 24.9x | 4.05x |
| json | macro | 5.32 | 37.3 | 11.1 | 2.60 | 2.04x | 14.3x | 4.27x |
| deltablue | macro | 59.5 | 803.1 | 100.7 | 12.0 | 4.97x | 67.1x | 8.42x |
| havlak | macro | 48.3 | 77.16s | 3.34s | 97.3 | 0.50x | 793x | 34.3x |
| cd | macro | 372.4 | 9.50s | 971.8 | 36.7 | 10.1x | 259x | 26.5x |

## BENG

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | allocation | 10.3 | 32.7 | 23.7 | 4.15 | 2.48x | 7.87x | 5.72x |
| fannkuch | permutation | 0.634 | 12.6 | 7.27 | 4.05 | 0.16x | 3.10x | 1.80x |
| fasta | generation | 6.97 | 37.8 | 8.76 | 6.08 | 1.15x | 6.22x | 1.44x |
| knucleotide | hashing | 11.5 | 150.6 | --- | 4.94 | 2.32x | 30.5x | --- |
| mandelbrot | numeric | 118.3 | 49.5 | 679.0 | 15.2 | 7.77x | 3.25x | 44.6x |
| nbody | numeric | 80.1 | 370.1 | 149.3 | 7.31 | 11.0x | 50.6x | 20.4x |
| pidigits | bignum | --- | 0.316 | 0.132 | 1.99 | --- | 0.16x | 0.07x |
| regexredux | regex | 1.29 | 15.7 | --- | 2.49 | 0.52x | 6.31x | --- |
| revcomp | string | 1.12 | 41.2 | --- | 3.36 | 0.33x | 12.2x | --- |
| spectralnorm | numeric | 47.7 | 289.9 | 65.2 | 2.60 | 18.4x | 112x | 25.1x |

## KOSTYA

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| brainfuck | interpreter | 452.1 | 975.7 | 892.6 | 34.0 | 13.3x | 28.7x | 26.2x |
| matmul | numeric | 41.7 | 837.7 | 545.2 | 15.8 | 2.65x | 53.2x | 34.6x |
| primes | numeric | 60.1 | 97.1 | 95.6 | 4.73 | 12.7x | 20.5x | 20.2x |
| base64 | string | 297.9 | 754.6 | 158.8 | 17.5 | 17.0x | 43.1x | 9.07x |
| levenshtein | string | 45.1 | 81.0 | 54.8 | 3.95 | 11.4x | 20.5x | 13.9x |
| json_gen | data | 73.2 | 36.2 | 20.1 | 6.61 | 11.1x | 5.48x | 3.04x |
| collatz | numeric | 1.71s | 1.98s | 6.28s | 1.42s | 1.20x | 1.39x | 4.41x |

## LARCENY

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| triangl | search | 653.1 | 3.93s | 2.20s | 67.3 | 9.71x | 58.5x | 32.7x |
| array1 | array | 4.29 | 26.6 | 36.3 | 1.90 | 2.25x | 14.0x | 19.1x |
| deriv | symbolic | 25.1 | 72.3 | 59.2 | 4.01 | 6.27x | 18.0x | 14.8x |
| diviter | iterative | 3.09s | 7.29s | 29.55s | 484.7 | 6.37x | 15.0x | 61.0x |
| divrec | recursive | 20.5 | 24.8 | 38.1 | 7.69 | 2.66x | 3.23x | 4.95x |
| gcbench | allocation | 397.6 | 825.6 | 578.0 | 25.4 | 15.6x | 32.4x | 22.7x |
| paraffins | combinat | 2.15 | 2.35 | 2.55 | 1.04 | 2.07x | 2.27x | 2.46x |
| pnpoly | numeric | 109.0 | 143.5 | 215.9 | 5.99 | 18.2x | 24.0x | 36.1x |
| primes | iterative | 63.5 | 99.7 | 98.1 | 4.47 | 14.2x | 22.3x | 21.9x |
| puzzle | search | 19.1 | 27.2 | 30.4 | 3.36 | 5.70x | 8.10x | 9.06x |
| quicksort | sorting | 13.1 | 65.9 | 20.2 | 1.70 | 7.70x | 38.8x | 11.9x |
| ray | numeric | 11.8 | 14.3 | 14.4 | 3.59 | 3.30x | 3.98x | 4.00x |

## JetStream

| Benchmark | Category | MIR (ms) | LambdaJS (ms) | QuickJS (ms) | Node.js (ms) | MIR/Node | LambdaJS/Node | QuickJS/Node |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| nbody | numeric | 82.8 | 226.3 | 137.5 | 5.57 | 14.9x | 40.7x | 24.7x |
| cube3d | 3d | 14.4 | 814.2 | --- | 18.5 | 0.78x | 43.9x | --- |
| navier_stokes | numeric | 971.8 | 5.37s | 819.6 | 39.2 | 24.8x | 137x | 20.9x |
| richards | macro | 208.4 | 222.7 | 27.0 | 5.04 | 41.3x | 44.2x | 5.35x |
| splay | data | 164.3 | 52.0 | 25.3 | 4.83 | 34.0x | 10.8x | 5.24x |
| deltablue | macro | 12.8 | 369.5 | 46.9 | 6.53 | 1.96x | 56.6x | 7.18x |
| hashmap | data | 76.0 | 63.23s | 2.51s | 56.2 | 1.35x | 1124x | 44.6x |
| crypto_sha1 | crypto | 205.5 | 507.3 | 70.0 | 6.93 | 29.6x | 73.2x | 10.1x |
| raytrace3d | 3d | 147.8 | 1.05s | --- | 18.3 | 8.06x | 57.3x | --- |
