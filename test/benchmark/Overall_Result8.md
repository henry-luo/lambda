# Lambda Benchmark Results: LambdaJS Engine - Round 8 Latest JS Rerun

- **Date:** 2026-06-25
- **Platform:** Darwin arm64, Apple Silicon macOS
- **Lambda version:** release build (`make release`, `./lambda.exe` 15 MB)
- **Node.js:** v22.13.0
- **Methodology:** 3 runs per benchmark, median of self-reported execution time (`__TIMING__`, excludes process startup)
- **Runner:** `env -u JS_EXEC_PROFILE -u JS_EXEC_PROFILE_OUT python3 test/benchmark/run_benchmarks.py -e lambdajs,nodejs -n 3 -t 180`
- **Results source:** `test/benchmark/benchmark_results_v3.json`
- **Previous snapshot for LambdaJS delta:** `temp/benchmark_results_v3_before_gc_tuning_rerun.json`
- **Instrumentation check:** release binary was checked with `strings lambda.exe | rg "JS_EXEC_PROFILE|js_profile_property_set|gc_sweep_walked_objects"`; no profiling symbols matched.

This rerun refreshes Result8 after the dense-array fast-path work and after restoring JetStream Navier-Stokes to the pre-lowered effective load (`NAVIER_STOKES_SIZE = 128`, `NAVIER_STOKES_ITERATIONS = 20`).

Important comparison caveat: Node.js was rerun as v22.13.0 in this environment, while the original Result7 header recorded Node.js v24.7.0. Therefore, the LambdaJS-vs-Node geometric mean is a current-machine snapshot, not a clean Node-held-constant comparison against Result7.

Important workload note: Result8 used the newer JetStream wrapper shape (`new Benchmark().runIteration()`) instead of the older standardized wrapper that calls the detected benchmark function 8 times. Several JetStream files define different internal `runIteration()` counts, so Result8 JetStream rows are not directly comparable to Result7 or older x8 snapshots without workload normalization.

---

## Summary

LambdaJS produced usable timings for **60 / 62** registered JS benchmarks. Two LambdaJS cases still failed to produce a timing within the runner success criteria:

- `awfy/havlak`
- `awfy/cd`

Across the **60 timed** benchmarks, LambdaJS is **17.23x slower than Node.js** by geometric mean in this run. LambdaJS is faster than Node on two BENG cases:

- `beng/fannkuch`: **0.49x** LambdaJS / Node.js
- `beng/pidigits`: **0.15x** LambdaJS / Node.js

| Suite | Total | LambdaJS timed | Missing LambdaJS | Geo mean LambdaJS / Node.js |
|---|---:|---:|---:|---:|
| R7RS | 10 | 10 | 0 | 5.37x |
| AWFY | 14 | 12 | 2 | 38.12x |
| BENG | 10 | 10 | 0 | 6.46x |
| KOSTYA | 7 | 7 | 0 | 14.64x |
| LARCENY | 12 | 12 | 0 | 8.49x |
| JetStream | 9 | 9 | 0 | 189.46x |
| **Overall timed** | **62** | **60** | **2** | **17.23x** |

---

## Notable Changes

The latest run changes JetStream coverage materially:

- `jetstream/navier_stokes` now records **211 ms** at the restored 128/20 load.
- `jetstream/richards` now records **3776 ms**.
- `jetstream/hashmap` now records **9092 ms**.
- `jetstream/cube3d` remains much faster than the pre-GC-tuning snapshot: **3562 ms** now vs **23460 ms** before.

`jetstream/splay` is substantially slower in this run than the previous Result8 snapshot and should be rechecked before treating it as a stable regression.

---

## GC Tuning Impact

The GC sweep ownership tuning still shows the intended Cube3D effect:

| Benchmark | Previous LambdaJS (ms) | Current LambdaJS (ms) | Speedup | Improvement |
|---|---:|---:|---:|---:|
| `jetstream/cube3d` | 23460.383 | 3562.482 | **6.59x** | **+84.81%** |

The whole-suite LambdaJS-only comparison against the previous JSON remains noisy:

| Metric | Result |
|---|---:|
| Comparable LambdaJS benchmarks | 57 |
| Geometric mean current / previous | 1.213x |
| Geometric mean speedup | 0.825x |

### Largest LambdaJS Wins vs Previous JSON

| Benchmark | Previous (ms) | Current (ms) | Speedup | Improvement |
|---|---:|---:|---:|---:|
| jetstream/cube3d | 23460.383 | 3562.482 | 6.59x | +84.81% |
| kostya/base64 | 1548.872 | 781.916 | 1.98x | +49.52% |
| beng/revcomp | 57.226 | 45.271 | 1.26x | +20.89% |
| awfy/nbody | 516.357 | 421.763 | 1.22x | +18.32% |
| kostya/json_gen | 38.766 | 32.005 | 1.21x | +17.44% |

### Largest LambdaJS Slowdowns vs Previous JSON

| Benchmark | Previous (ms) | Current (ms) | Speedup | Change |
|---|---:|---:|---:|---:|
| jetstream/splay | 1496.892 | 11491.477 | 0.13x | -667.69% |
| jetstream/richards | 651.714 | 3775.967 | 0.17x | -479.39% |
| jetstream/crypto_sha1 | 237.299 | 705.258 | 0.34x | -197.20% |
| jetstream/deltablue | 1765.647 | 4748.273 | 0.37x | -168.93% |
| awfy/mandelbrot | 920.855 | 1484.810 | 0.62x | -61.24% |

---

## Full Results

### R7RS

| Benchmark | LambdaJS (ms) | Node.js (ms) | Ratio |
|---|---:|---:|---:|
| fib | 18.0 | 1.86 | 9.69x |
| fibfp | 22.9 | 1.82 | 12.58x |
| tak | 1.33 | 0.789 | 1.69x |
| cpstak | 2.68 | 0.978 | 2.74x |
| sum | 22.3 | 1.21 | 18.33x |
| sumfp | 1.69 | 0.879 | 1.92x |
| nqueens | 3.88 | 1.80 | 2.15x |
| fft | 6.80 | 1.61 | 4.22x |
| mbrot | 28.9 | 1.79 | 16.19x |
| ack | 92.8 | 13.7 | 6.78x |

### AWFY

| Benchmark | LambdaJS (ms) | Node.js (ms) | Ratio |
|---|---:|---:|---:|
| sieve | 0.474 | 0.381 | 1.24x |
| permute | 38.2 | 0.835 | 45.74x |
| queens | 24.9 | 0.646 | 38.53x |
| towers | 70.6 | 1.13 | 62.71x |
| bounce | 10.4 | 0.570 | 18.27x |
| list | 11.8 | 0.493 | 23.96x |
| storage | 29.8 | 0.654 | 45.54x |
| mandelbrot | 1485 | 32.3 | 45.99x |
| nbody | 422 | 5.55 | 75.94x |
| richards | 5479 | 48.9 | 112.01x |
| json | 75.5 | 2.73 | 27.66x |
| deltablue | 4118 | 13.0 | 317.92x |
| havlak | --- | 103 | --- |
| cd | --- | 37.3 | --- |

### BENG

| Benchmark | LambdaJS (ms) | Node.js (ms) | Ratio |
|---|---:|---:|---:|
| binarytrees | 51.2 | 3.94 | 13.02x |
| fannkuch | 1.99 | 4.03 | **0.49x** |
| fasta | 7.28 | 6.32 | 1.15x |
| knucleotide | 121 | 5.07 | 23.82x |
| mandelbrot | 171 | 15.0 | 11.41x |
| nbody | 1143 | 7.52 | 152.11x |
| pidigits | 0.300 | 2.04 | **0.15x** |
| regexredux | 14.4 | 2.41 | 5.98x |
| revcomp | 45.3 | 3.33 | 13.61x |
| spectralnorm | 86.9 | 2.53 | 34.34x |

### KOSTYA

| Benchmark | LambdaJS (ms) | Node.js (ms) | Ratio |
|---|---:|---:|---:|
| brainfuck | 710 | 45.7 | 15.54x |
| matmul | 2719 | 15.5 | 175.71x |
| primes | 15.3 | 4.42 | 3.45x |
| base64 | 782 | 17.4 | 44.83x |
| levenshtein | 57.6 | 4.01 | 14.37x |
| json_gen | 32.0 | 6.30 | 5.08x |
| collatz | 6642 | 1420 | 4.68x |

### LARCENY

| Benchmark | LambdaJS (ms) | Node.js (ms) | Ratio |
|---|---:|---:|---:|
| triangl | 2256 | 67.0 | 33.67x |
| array1 | 3.23 | 1.91 | 1.69x |
| deriv | 103 | 3.69 | 27.83x |
| diviter | 2534 | 472 | 5.37x |
| divrec | 28.4 | 7.55 | 3.76x |
| gcbench | 1192 | 24.0 | 49.70x |
| paraffins | 2.14 | 1.01 | 2.12x |
| pnpoly | 129 | 5.88 | 21.94x |
| primes | 15.4 | 4.40 | 3.50x |
| puzzle | 22.2 | 3.25 | 6.82x |
| quicksort | 28.9 | 1.64 | 17.65x |
| ray | 16.0 | 3.55 | 4.51x |

### JetStream

| Benchmark | LambdaJS (ms) | Node.js (ms) | Ratio |
|---|---:|---:|---:|
| nbody | 868 | 5.70 | 152.17x |
| cube3d | 3562 | 17.7 | 201.36x |
| navier_stokes | 211 | 14.3 | 14.78x |
| richards | 3776 | 8.34 | 452.92x |
| splay | 11491 | 19.6 | 585.43x |
| deltablue | 4748 | 9.84 | 482.65x |
| hashmap | 9092 | 15.4 | 590.65x |
| crypto_sha1 | 705 | 8.87 | 79.48x |
| raytrace3d | 2148 | 18.6 | 115.60x |

---

## Interpretation

The latest full JS rerun improves coverage from the earlier Result8 snapshot: LambdaJS now has timings for **60 / 62** benchmarks, with only `awfy/havlak` and `awfy/cd` still missing.

The latest whole-suite LambdaJS-vs-Node ratio is **17.23x slower** over the timed benchmarks. The strongest confirmed targeted result remains Cube3D: its previous GC sweep ownership bottleneck is gone, and the current saved median is **3562 ms** versus **23460 ms** before the GC tuning.

Navier-Stokes is now back at the historical 128/20 load, so its current **211 ms** row should not be compared to the earlier reduced 32-size result. HashMap and JetStream Richards now complete, which makes JetStream coverage substantially more representative than the first Result8 pass.
