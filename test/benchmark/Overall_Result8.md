# Lambda Benchmark Results: LambdaJS Engine - Round 8 GC-Tuned Rerun

**Date:** 2026-06-25  
**Platform:** Darwin arm64, Apple Silicon macOS  
**Lambda version:** release build (`make release`, `./lambda.exe` 15 MB)  
**Node.js:** v22.13.0  
**Methodology:** 3 runs per benchmark, median of self-reported execution time (`__TIMING__`, excludes process startup)  
**Runner:** `env -u JS_EXEC_PROFILE -u JS_EXEC_PROFILE_OUT python3 test/benchmark/run_benchmarks.py -e lambdajs,nodejs -n 3`  
**Results source:** `test/benchmark/benchmark_results_v3.json`  
**Previous snapshot for LambdaJS delta:** `temp/benchmark_results_v3_before_gc_tuning_rerun.json`  
**Instrumentation check:** release binary was checked with `strings lambda.exe | rg "JS_EXEC_PROFILE|js_profile_property_set|gc_sweep_walked_objects"` and no profiling symbols were found.

This is the first full LambdaJS-vs-Node rerun after the Cube3D GC sweep ownership tuning in Tune12. The tuning moved GC sweep ownership classification from repeated per-object owner searches to allocation-time header flags (`GC_FLAG_BUMP`, `GC_FLAG_LARGE`, otherwise object-zone). The main expected benchmark beneficiary is `jetstream/cube3d`.

Important comparison caveat: Node.js was rerun as v22.13.0 in this environment, while the original Result7 header recorded Node.js v24.7.0. Therefore, the new LambdaJS-vs-Node geometric mean is a current-machine snapshot, not a clean Node-held-constant comparison against Result7.

---

## Summary

LambdaJS produced usable timings for **57 / 62** registered JS benchmarks. Five LambdaJS cases still failed to produce a timing within the runner's 120s-per-run timeout / success criteria:

- `awfy/havlak`
- `awfy/cd`
- `jetstream/navier_stokes`
- `jetstream/richards`
- `jetstream/hashmap`

Across the **57 timed** benchmarks, LambdaJS is **14.11x slower than Node.js** by geometric mean in this run. LambdaJS is faster than Node on two BENG cases:

- `beng/fannkuch`: **0.47x** LambdaJS / Node.js
- `beng/pidigits`: **0.15x** LambdaJS / Node.js

| Suite | Total | LambdaJS timed | Missing LambdaJS | Geo mean LambdaJS / Node.js |
|---|---:|---:|---:|---:|
| R7RS | 10 | 10 | 0 | 5.02x |
| AWFY | 14 | 12 | 2 | 41.20x |
| BENG | 10 | 10 | 0 | 6.32x |
| KOSTYA | 7 | 7 | 0 | 14.33x |
| JETSTREAM | 9 | 6 | 3 | 106.19x |
| LARCENY | 12 | 12 | 0 | 8.08x |
| **Overall timed** | **62** | **57** | **5** | **14.11x** |

---

## GC Tuning Impact

The GC sweep ownership tuning dramatically improved the benchmark it was designed around:

| Benchmark | Previous LambdaJS (ms) | Current LambdaJS (ms) | Speedup | Improvement |
|---|---:|---:|---:|---:|
| `jetstream/cube3d` | 23460.383 | 3873.063 | **6.06x** | **+83.49%** |

This aligns with the Tune12 profile result: before tuning, `cube3d` triggered one very large GC where `gc_sweep()` was dominated by ownership classification over about 4.45M tracked objects. After adding allocation-time ownership flags, the full benchmark median is now **3873 ms** for LambdaJS instead of **23460 ms** in the previous saved benchmark JSON.

The whole-suite LambdaJS-only comparison against the previous JSON is noisier:

| Metric | Result |
|---|---:|
| Comparable LambdaJS benchmarks | 56 |
| Geometric mean current / previous | 1.123x |
| Geometric mean speedup | 0.891x |

Interpretation: the targeted GC fix is real and large for Cube3D, but this full sweep ran under different system/runtime conditions and shows broad run-to-run slowdowns in several unrelated AWFY and numeric cases. Treat the Cube3D delta as the reliable target result; treat aggregate current/previous LambdaJS deltas as a noisy fresh snapshot unless repeated on a quieter system.

### Largest LambdaJS Wins vs Previous JSON

| Benchmark | Previous (ms) | Current (ms) | Speedup | Improvement |
|---|---:|---:|---:|---:|
| jetstream/cube3d | 23460.383 | 3873.063 | 6.06x | +83.49% |
| kostya/base64 | 1548.872 | 779.723 | 1.99x | +49.66% |
| awfy/nbody | 516.357 | 428.558 | 1.20x | +17.00% |
| beng/revcomp | 57.226 | 49.642 | 1.15x | +13.25% |
| kostya/json_gen | 38.766 | 34.060 | 1.14x | +12.14% |

### Largest LambdaJS Slowdowns vs Previous JSON

| Benchmark | Previous (ms) | Current (ms) | Speedup | Change |
|---|---:|---:|---:|---:|
| awfy/bounce | 7.062 | 11.395 | 0.62x | -61.37% |
| awfy/mandelbrot | 920.855 | 1473.007 | 0.63x | -59.96% |
| awfy/deltablue | 2781.521 | 4434.680 | 0.63x | -59.43% |
| awfy/permute | 26.671 | 42.131 | 0.63x | -57.97% |
| awfy/towers | 50.754 | 77.894 | 0.65x | -53.47% |

These broad slowdowns do not match the GC sweep change shape, so they should be rechecked before treating them as regressions from the GC tuning.

---

## Full Results

### R7RS

| Benchmark | LambdaJS (ms) | Node.js (ms) | Ratio |
|---|---:|---:|---:|
| fib | 16.9 | 1.84 | 9.18x |
| fibfp | 21.7 | 1.77 | 12.25x |
| tak | 1.21 | 0.795 | 1.52x |
| cpstak | 2.49 | 1.00 | 2.48x |
| sum | 22.4 | 1.20 | 18.64x |
| sumfp | 1.71 | 1.01 | 1.69x |
| nqueens | 3.39 | 1.81 | 1.87x |
| fft | 6.69 | 1.64 | 4.09x |
| mbrot | 29.1 | 1.87 | 15.51x |
| ack | 86.0 | 13.5 | 6.36x |

### AWFY

| Benchmark  | LambdaJS (ms) | Node.js (ms) |   Ratio |
| ---------- | ------------: | -----------: | ------: |
| sieve      |         0.516 |        0.381 |   1.36x |
| permute    |          42.1 |        0.815 |  51.68x |
| queens     |          27.5 |        0.643 |  42.75x |
| towers     |          77.9 |         1.14 |  68.31x |
| bounce     |          11.4 |        0.591 |  19.27x |
| list       |          13.0 |        0.498 |  26.02x |
| storage    |          31.9 |        0.649 |  49.24x |
| mandelbrot |          1473 |         32.3 |  45.58x |
| nbody      |           429 |         5.93 |  72.29x |
| richards   |          6142 |         48.2 | 127.45x |
| json       |          99.7 |         3.00 |  33.26x |
| deltablue  |          4435 |         13.1 | 339.04x |
| havlak     |           --- |          112 |     --- |
| cd         |           --- |         41.6 |     --- |

### BENG

| Benchmark | LambdaJS (ms) | Node.js (ms) | Ratio |
|---|---:|---:|---:|
| binarytrees | 54.2 | 4.70 | 11.52x |
| fannkuch | 1.96 | 4.21 | **0.47x** |
| fasta | 7.96 | 6.27 | 1.27x |
| knucleotide | 125 | 5.31 | 23.49x |
| mandelbrot | 177 | 15.5 | 11.44x |
| nbody | 1203 | 7.84 | 153.59x |
| pidigits | 0.305 | 2.02 | **0.15x** |
| regexredux | 15.1 | 2.74 | 5.52x |
| revcomp | 49.6 | 3.57 | 13.90x |
| spectralnorm | 84.6 | 2.71 | 31.29x |

### KOSTYA

| Benchmark | LambdaJS (ms) | Node.js (ms) | Ratio |
|---|---:|---:|---:|
| brainfuck | 720 | 48.7 | 14.76x |
| matmul | 2753 | 16.3 | 168.77x |
| primes | 15.6 | 4.67 | 3.35x |
| base64 | 780 | 18.4 | 42.34x |
| levenshtein | 58.3 | 4.03 | 14.46x |
| json_gen | 34.1 | 6.50 | 5.24x |
| collatz | 6770 | 1460 | 4.64x |

### LARCENY

| Benchmark | LambdaJS (ms) | Node.js (ms) | Ratio |
|---|---:|---:|---:|
| triangl | 1802 | 68.2 | 26.41x |
| array1 | 3.27 | 1.94 | 1.69x |
| deriv | 106 | 3.80 | 27.98x |
| diviter | 2616 | 486 | 5.39x |
| divrec | 27.5 | 7.80 | 3.52x |
| gcbench | 1232 | 26.8 | 45.97x |
| paraffins | 1.72 | 1.00 | 1.72x |
| pnpoly | 149 | 6.07 | 24.60x |
| primes | 15.5 | 4.63 | 3.35x |
| puzzle | 23.6 | 3.41 | 6.93x |
| quicksort | 28.5 | 1.68 | 16.98x |
| ray | 15.8 | 3.68 | 4.28x |

### JetStream

| Benchmark     | LambdaJS (ms) | Node.js (ms) |   Ratio |
| ------------- | ------------: | -----------: | ------: |
| nbody         |           904 |         5.91 | 152.90x |
| cube3d        |          3873 |         19.0 | 203.51x |
| navier_stokes |           --- |         14.3 |     --- |
| richards      |           --- |         8.75 |     --- |
| splay         |          1540 |         20.8 |  74.01x |
| deltablue     |          2090 |         10.5 | 199.35x |
| hashmap       |           --- |         15.7 |     --- |
| crypto_sha1   |           258 |         9.55 |  27.06x |
| raytrace3d    |          2272 |         19.7 | 115.43x |

---

## Interpretation

The GC sweep ownership tuning accomplished its targeted goal: `jetstream/cube3d` improved from **23460 ms** to **3873 ms**, a **6.06x** speedup in the saved benchmark results.

The latest whole-suite LambdaJS-vs-Node ratio is **14.11x slower** over **57 / 62** timed benchmarks. That looks better than Result7's latest **17.81x** ratio, but Node.js was rerun as v22.13.0, so this should not be read as a clean aggregate LambdaJS improvement. The more defensible conclusion is narrower: Cube3D's former GC sweep bottleneck is gone, while the next Cube3D hot area is now regular property/array store execution and GC tracing rather than sweep ownership classification.
