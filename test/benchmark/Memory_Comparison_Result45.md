# Memory Comparison Report — 2026-09-16

## Result

This report compares peak resident set size (RSS) for the canonical benchmark matrix. Each value is the median of three independent process runs.

| Engine | Rows measured | Arithmetic mean | Median | Minimum | Maximum | Geo. ratio vs Node.js |
|---|---:|---:|---:|---:|---:|---:|
| Lambda-U | 63 | 50.3 MiB | 35.9 MiB | 31.7 MiB | 256.5 MiB | 0.85x (63 matched) |
| Lambda-T | 63 | 53.1 MiB | 36.5 MiB | 31.9 MiB | 283.8 MiB | 0.87x (63 matched) |
| C2MIR | 63 | 23.1 MiB | 21.4 MiB | 19.2 MiB | 85.4 MiB | 0.43x (63 matched) |
| LambdaJS | 53 | 141.2 MiB | 46.8 MiB | 35.2 MiB | 2.65 GiB | 1.26x (53 matched) |
| QuickJS | 57 | 3.6 MiB | 1.9 MiB | 1.7 MiB | 54.8 MiB | 0.05x (57 matched) |
| Node.js | 63 | 54.3 MiB | 48.2 MiB | 41.3 MiB | 153.7 MiB | 1.00x (63 matched) |

The geometric ratio is computed row-by-row against Node.js using only rows measured by both engines; it is less dominated by the largest allocation-heavy outliers than the arithmetic mean.

## Method

- Source data: `test/benchmark/memory_results.json`
- Command: `test/benchmark/run_benchmarks.py -m memory --typed -e mir,c2mir,lambdajs,quickjs,nodejs -n 3 -t 180 --fresh --results-output test/benchmark/memory_results.json`
- Finished: `2026-09-16T22:47:34`
- Platform: `Darwin arm64`
- Lambda binary: `./lambda.exe` (18,508,008 bytes)
- Lambda commit: `c3ad9e0b53af5adcffe8751090781150aae2233b`
- Node.js: `v24.14.1`
- QuickJS: `2026-06-04`
- Metric: process peak RSS from macOS `/usr/bin/time -l`; this is not heap-only memory.

C2MIR values include the native C frontend and MIR generator process footprint. They are useful as a native reference, but are not the same runtime architecture as Lambda-U or Lambda-T.

## Per-benchmark peaks

| Benchmark | Lambda-U | Lambda-T | C2MIR | LambdaJS | QuickJS | Node.js |
|---|---:|---:|---:|---:|---:|---:|
| `r7rs/fib` | 32.8 MiB | 32.9 MiB | 20.2 MiB | 35.2 MiB | 1.7 MiB | 45.5 MiB |
| `r7rs/fibfp` | 32.9 MiB | 33.0 MiB | 19.3 MiB | 35.4 MiB | 1.8 MiB | 45.5 MiB |
| `r7rs/tak` | 33.2 MiB | 33.0 MiB | 19.6 MiB | 35.4 MiB | 1.7 MiB | 45.3 MiB |
| `r7rs/cpstak` | 33.1 MiB | 33.1 MiB | 20.6 MiB | 35.4 MiB | 1.7 MiB | 45.4 MiB |
| `r7rs/sum` | 32.9 MiB | 33.0 MiB | 19.3 MiB | 35.3 MiB | 1.8 MiB | 45.5 MiB |
| `r7rs/sumfp` | 32.8 MiB | 32.9 MiB | 19.2 MiB | 35.5 MiB | 1.7 MiB | 46.2 MiB |
| `r7rs/nqueens` | 37.5 MiB | 39.4 MiB | 20.4 MiB | 70.0 MiB | 1.8 MiB | 48.0 MiB |
| `r7rs/fft` | 36.0 MiB | 37.2 MiB | 21.4 MiB | 55.1 MiB | 1.8 MiB | 49.1 MiB |
| `r7rs/mbrot` | 33.9 MiB | 34.8 MiB | 20.6 MiB | 37.7 MiB | 1.8 MiB | 48.2 MiB |
| `r7rs/ack` | 33.1 MiB | 33.0 MiB | 21.9 MiB | 35.4 MiB | 3.6 MiB | 45.6 MiB |
| `awfy/sieve` | 33.4 MiB | 33.6 MiB | 19.3 MiB | 38.7 MiB | 1.9 MiB | 42.0 MiB |
| `awfy/permute` | 33.5 MiB | 33.7 MiB | 20.5 MiB | 40.0 MiB | 1.8 MiB | 42.1 MiB |
| `awfy/queens` | 34.3 MiB | 35.3 MiB | 21.0 MiB | 40.8 MiB | 1.8 MiB | 42.4 MiB |
| `awfy/towers` | 34.0 MiB | 34.8 MiB | 22.3 MiB | 41.3 MiB | 1.8 MiB | 42.3 MiB |
| `awfy/bounce` | 35.6 MiB | 37.1 MiB | 23.2 MiB | 71.0 MiB | 2.0 MiB | 42.7 MiB |
| `awfy/list` | 33.6 MiB | 33.7 MiB | 22.1 MiB | 39.5 MiB | 1.8 MiB | 42.0 MiB |
| `awfy/storage` | 34.3 MiB | 34.5 MiB | 21.6 MiB | 71.1 MiB | 2.7 MiB | 43.2 MiB |
| `awfy/mandelbrot` | 34.2 MiB | 34.4 MiB | 20.1 MiB | 38.6 MiB | 1.8 MiB | 47.8 MiB |
| `awfy/nbody` | 38.5 MiB | 47.4 MiB | 20.9 MiB | 94.4 MiB | 1.9 MiB | 49.8 MiB |
| `awfy/richards` | 51.3 MiB | 43.9 MiB | 21.8 MiB | 69.4 MiB | 2.3 MiB | 49.8 MiB |
| `awfy/json` | 45.4 MiB | 44.9 MiB | 22.8 MiB | 96.4 MiB | 3.1 MiB | 45.6 MiB |
| `awfy/deltablue` | 62.8 MiB | 65.7 MiB | 27.9 MiB | 178.6 MiB | 3.7 MiB | 58.3 MiB |
| `awfy/havlak` | 92.4 MiB | 69.0 MiB | 23.9 MiB | 2.65 GiB | 54.8 MiB | 137.5 MiB |
| `awfy/cd` | 63.6 MiB | 60.1 MiB | 23.0 MiB | 213.6 MiB | 5.8 MiB | 59.6 MiB |
| `beng/binarytrees` | 34.8 MiB | 42.1 MiB | 21.3 MiB | 46.8 MiB | 2.5 MiB | 48.3 MiB |
| `beng/fannkuch` | 35.2 MiB | 36.5 MiB | 22.8 MiB | 57.5 MiB | 1.8 MiB | 47.0 MiB |
| `beng/fasta` | 35.9 MiB | 36.8 MiB | 20.1 MiB | 60.4 MiB | 1.8 MiB | 49.8 MiB |
| `beng/knucleotide` | 38.2 MiB | 46.4 MiB | 22.0 MiB | — | 2.5 MiB | 49.6 MiB |
| `beng/pidigits` | 35.2 MiB | 35.8 MiB | 20.0 MiB | 41.4 MiB | 1.8 MiB | 41.3 MiB |
| `beng/regexredux` | 31.7 MiB | 31.9 MiB | 21.4 MiB | — | 1.9 MiB | 41.9 MiB |
| `beng/revcomp` | 35.1 MiB | 35.7 MiB | 19.3 MiB | — | 2.2 MiB | 43.7 MiB |
| `beng/spectralnorm` | 34.7 MiB | 36.2 MiB | 20.1 MiB | 54.9 MiB | 1.9 MiB | 48.6 MiB |
| `kostya/brainfuck` | 199.1 MiB | 198.8 MiB | 22.7 MiB | 286.5 MiB | 2.0 MiB | 122.9 MiB |
| `kostya/matmul` | 35.2 MiB | 35.7 MiB | 21.4 MiB | 40.2 MiB | 2.8 MiB | 49.8 MiB |
| `kostya/primes` | 33.7 MiB | 34.2 MiB | 20.0 MiB | 38.2 MiB | 2.8 MiB | 47.5 MiB |
| `kostya/base64` | 38.1 MiB | 38.4 MiB | 21.3 MiB | 42.3 MiB | 4.6 MiB | 53.9 MiB |
| `kostya/levenshtein` | 38.0 MiB | 36.8 MiB | 20.7 MiB | 55.7 MiB | 1.8 MiB | 48.7 MiB |
| `kostya/json_gen` | 57.8 MiB | 42.7 MiB | 22.1 MiB | 58.2 MiB | 2.5 MiB | 51.4 MiB |
| `kostya/collatz` | 33.0 MiB | 33.1 MiB | 20.1 MiB | 35.6 MiB | 1.7 MiB | 46.7 MiB |
| `larceny/triangl` | 36.0 MiB | 37.8 MiB | 21.0 MiB | 69.3 MiB | 1.8 MiB | 46.8 MiB |
| `larceny/array1` | 33.2 MiB | 33.5 MiB | 19.2 MiB | 40.1 MiB | 1.8 MiB | 46.2 MiB |
| `larceny/deriv` | 56.7 MiB | 58.9 MiB | 27.7 MiB | 49.3 MiB | 1.8 MiB | 49.5 MiB |
| `larceny/diviter` | 33.0 MiB | 33.1 MiB | 19.9 MiB | 35.9 MiB | 1.8 MiB | 46.0 MiB |
| `larceny/divrec` | 33.2 MiB | 33.3 MiB | 20.6 MiB | 35.9 MiB | 2.2 MiB | 45.6 MiB |
| `larceny/gcbench` | 45.6 MiB | 283.8 MiB | 24.1 MiB | 250.8 MiB | 23.3 MiB | 67.5 MiB |
| `larceny/paraffins` | 36.3 MiB | 36.5 MiB | 22.6 MiB | 46.8 MiB | 1.8 MiB | 43.0 MiB |
| `larceny/pnpoly` | 33.7 MiB | 34.2 MiB | 19.9 MiB | 40.0 MiB | 1.8 MiB | 48.0 MiB |
| `larceny/puzzle` | 49.1 MiB | 49.8 MiB | 21.5 MiB | 42.0 MiB | 1.8 MiB | 46.4 MiB |
| `larceny/quicksort` | 34.6 MiB | 35.0 MiB | 20.7 MiB | 54.7 MiB | 1.8 MiB | 46.4 MiB |
| `larceny/ray` | 35.3 MiB | 35.9 MiB | 19.2 MiB | 42.1 MiB | 1.8 MiB | 49.0 MiB |
| `text/fast_diff` | 37.4 MiB | 36.1 MiB | 20.9 MiB | 158.7 MiB | 2.4 MiB | 63.9 MiB |
| `text/microdiff` | 89.6 MiB | 89.4 MiB | 21.9 MiB | 153.1 MiB | 1.9 MiB | 50.8 MiB |
| `text/hyphen` | 93.5 MiB | 94.0 MiB | 54.7 MiB | 1.28 GiB | 4.8 MiB | 57.8 MiB |
| `text/prettier_ast` | 113.0 MiB | 75.8 MiB | 30.5 MiB | — | 5.0 MiB | 69.7 MiB |
| `text/text_search` | 36.2 MiB | 36.3 MiB | 22.2 MiB | 44.6 MiB | 2.8 MiB | 51.7 MiB |
| `text/three_way_merge` | 60.9 MiB | 62.3 MiB | 20.8 MiB | 47.9 MiB | 4.4 MiB | 77.2 MiB |
| `text/log_pipeline` | 75.4 MiB | 77.8 MiB | 22.5 MiB | 87.1 MiB | 3.8 MiB | 73.8 MiB |
| `jetstream/cube3d` | 95.9 MiB | 107.4 MiB | 21.9 MiB | — | — | 60.0 MiB |
| `jetstream/navier_stokes` | 54.0 MiB | 53.5 MiB | 22.0 MiB | — | — | 55.9 MiB |
| `jetstream/splay` | 256.5 MiB | 227.9 MiB | 85.4 MiB | — | — | 153.7 MiB |
| `jetstream/hashmap` | 47.2 MiB | 46.5 MiB | 24.9 MiB | — | — | 60.6 MiB |
| `jetstream/crypto_sha1` | 38.4 MiB | 38.2 MiB | 21.4 MiB | — | — | 51.6 MiB |
| `jetstream/raytrace3d` | 60.9 MiB | 58.9 MiB | 21.6 MiB | — | — | 56.9 MiB |

## Coverage notes

- `beng/knucleotide`: missing LambdaJS.
- `beng/regexredux`: missing LambdaJS.
- `beng/revcomp`: missing LambdaJS.
- `text/prettier_ast`: missing LambdaJS.
- `jetstream/cube3d`: missing LambdaJS, QuickJS.
- `jetstream/navier_stokes`: missing LambdaJS, QuickJS.
- `jetstream/splay`: missing LambdaJS, QuickJS.
- `jetstream/hashmap`: missing LambdaJS, QuickJS.
- `jetstream/crypto_sha1`: missing LambdaJS, QuickJS.
- `jetstream/raytrace3d`: missing LambdaJS, QuickJS.
- LambdaJS is unavailable for the three BENG file-I/O workloads because its current runtime does not provide the required filesystem API; `text/prettier_ast` also failed during LambdaJS execution.
- The memory runner currently does not measure LambdaJS or QuickJS JetStream rows; their six cells are intentionally unavailable rather than substituted.
- AWFY Node.js memory runs use the checked-in standalone bundles when the optional `ref/are-we-fast-yet` checkout is absent.

## Observations

- The normal startup footprint is approximately 32–40 MiB for Lambda-U, 33–40 MiB for Lambda-T, 19–28 MiB for C2MIR, 35–70 MiB for LambdaJS, 1.7–5 MiB for QuickJS, and 42–60 MiB for Node.js.
- Allocation-heavy rows dominate the maxima: LambdaJS reaches 2.65 GiB on `awfy/havlak` and 1.28 GiB on `text/hyphen`; typed Lambda reaches 284 MiB on `larceny/gcbench`.
