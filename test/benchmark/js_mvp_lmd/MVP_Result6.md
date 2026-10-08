# JS MVP Benchmark Results: MVP_Result6

**Ordinary arrays and core string methods** — [MVP §18](../../../vibe/jube/JS_MVP_Lmd.md#18-ordinary-arrays-and-core-string-methods).

[Series index and comparison](README.md) · [Raw JSON](MVP_Result6.json) · [Previous phase](MVP_Result5.md)

Captured **2026-10-08T11:41:03.801585+08:00** on `macOS-26.5.2-arm64-arm-64bit-Mach-O`. This release adds six admitted JS kernels, bringing coverage to **31 standard workloads and 17 microbenchmarks**.

## Newly enabled targets

Medians are self-reported execution milliseconds. Reference/MVP ratios above 1 mean MVP is faster. The six rows passed output checks in all engines. The ordinary Lambda sources retain native Lambda semantics (**S1.11**). The JS kernels, setup, inputs and iteration counts are preserved; adapters return scalar or boolean oracles in place of host output. The small result checks in `base64`, `brainfuck` and `levenshtein` are included in the measured body.

| Workload | MVP | Untyped Lambda | Node | Full LambdaJS | Untyped / MVP | Node / MVP |
|---|---:|---:|---:|---:|---:|---:|
| `r7rs/mbrot` | 0.852 | 0.730 | 1.883 | 2.205 | 0.857× | 2.210× |
| `larceny/puzzle` | 2.489 | 13.857 | 3.351 | 28.951 | 5.567× | 1.346× |
| `kostya/base64` | 81.083 | 12.018 | 17.385 | 478.189 | 0.148× | 0.214× |
| `kostya/json_gen` | 14.350 | 23.282 | 6.170 | 27.703 | 1.622× | 0.430× |
| `kostya/brainfuck` | 125.715 | 124.740 | 33.609 | 1778.156 | 0.992× | 0.267× |
| `kostya/levenshtein` | 121.696 | 5.144 | 4.043 | 76.939 | 0.042× | 0.033× |

The new-feature geometric comparison is **0.603× against untyped Lambda** and **0.367× against Node**. MVP is faster on `puzzle` and `json_gen`, close to untyped Lambda on `brainfuck`, and slower on `mbrot`, `base64`, and `levenshtein`. `base64` and `levenshtein` are the largest gaps to untyped Lambda; these array-heavy loops warrant the next tuning pass.

## Full supported population

Across **31 standard kernels**, MVP is **1.084× faster than untyped Lambda** and **1.691× faster than Node** geometrically. Across all **48 workloads**, MVP is **1.395× faster than Node**; an all-48 untyped Lambda ratio is omitted because the microbenchmarks have no matching `.ls` ports.

The prior 42 workloads were rerun against the exact Result5 candidate with 15 alternating pairs and an identical-control peer. Their paired control/candidate geometric ratio is **0.999×**, effectively flat overall. `integer_dense` is a clear regression: control/candidate **0.908×** (candidate about 10% slower; the paired candidate/control 95% interval is 1.091–1.111). `object_growth` improves **1.172×**. `spectralnorm` is nominally 0.925×, but its candidate/control interval includes 1, so the run does not establish a regression there.

## All 48 workload medians

For the original 42 rows, `Result5 control` is the same-run previous candidate; paired gain is control/current MVP, above 1 meaning the current candidate is faster. New rows have no pre-feature MVP control.

### Standard kernels

| Workload | MVP ms | Result5 control ms | Paired gain | Untyped Lambda ms | Node ms |
|---|---:|---:|---:|---:|---:|
| `beng/binarytrees` | 3.748 | 3.741 | 1.005× | 4.888 | 2.047 |
| `beng/fannkuch` | 0.385 | 0.384 | 0.997× | 0.282 | 2.190 |
| `beng/spectralnorm` | 1.083 | 1.071 | 0.925× | 1.676 | 2.533 |
| `kostya/base64` | 81.083 | — | — | 12.018 | 17.385 |
| `kostya/brainfuck` | 125.715 | — | — | 124.740 | 33.609 |
| `kostya/collatz` | 524.493 | 522.136 | 1.002× | 297.695 | 1414.376 |
| `kostya/json_gen` | 14.350 | — | — | 23.282 | 6.170 |
| `kostya/levenshtein` | 121.696 | — | — | 5.144 | 4.043 |
| `kostya/matmul` | 5.872 | 5.893 | 0.996× | 5.709 | 15.544 |
| `kostya/primes` | 3.453 | 3.387 | 0.979× | 2.312 | 4.369 |
| `larceny/array1` | 0.662 | 0.664 | 1.000× | 0.818 | 1.823 |
| `larceny/deriv` | 8.541 | 8.585 | 1.005× | 21.456 | 3.848 |
| `larceny/diviter` | 250.297 | 250.129 | 0.998× | 250.096 | 455.004 |
| `larceny/divrec` | 0.629 | 0.630 | 1.003× | 1.223 | 7.776 |
| `larceny/gcbench` | 90.752 | 90.554 | 1.000× | 116.288 | 21.987 |
| `larceny/paraffins` | 0.104 | 0.103 | 0.990× | 0.203 | 1.034 |
| `larceny/pnpoly` | 7.351 | 7.379 | 1.009× | 12.432 | 5.905 |
| `larceny/puzzle` | 2.489 | — | — | 13.857 | 3.351 |
| `larceny/quicksort` | 1.301 | 1.296 | 0.998× | 1.105 | 1.822 |
| `larceny/ray` | 0.237 | 0.240 | 1.004× | 0.219 | 3.622 |
| `larceny/triangl` | 155.702 | 156.446 | 1.004× | 182.911 | 68.041 |
| `r7rs/ack` | 8.876 | 8.917 | 1.009× | 13.898 | 13.473 |
| `r7rs/cpstak` | 0.245 | 0.244 | 1.008× | 0.273 | 0.939 |
| `r7rs/fft` | 0.169 | 0.168 | 0.988× | 0.327 | 1.602 |
| `r7rs/fib` | 1.478 | 1.469 | 0.981× | 1.549 | 1.809 |
| `r7rs/fibfp` | 1.486 | 1.479 | 0.994× | 2.371 | 1.820 |
| `r7rs/mbrot` | 0.852 | — | — | 0.730 | 1.883 |
| `r7rs/nqueens` | 0.923 | 0.920 | 0.998× | 1.110 | 1.838 |
| `r7rs/sum` | 0.270 | 0.270 | 1.000× | 0.270 | 1.231 |
| `r7rs/sumfp` | 0.027 | 0.027 | 1.000× | 0.070 | 0.902 |
| `r7rs/tak` | 0.124 | 0.124 | 1.000× | 0.137 | 0.728 |

### Microbenchmarks

| Workload | MVP ms | Result5 control ms | Paired gain | Node ms |
|---|---:|---:|---:|---:|
| `js_micro/args_ctl` | 2.698 | 2.697 | 0.999× | 2.241 |
| `js_micro/args_fp` | 2.705 | 2.698 | 0.998× | 2.254 |
| `js_micro/lit` | 0.135 | 0.137 | 1.007× | 3.700 |
| `js_micro/named` | 3.944 | 3.946 | 0.998× | 6.195 |
| `js_mvp_lmd/calls` | 5.065 | 5.077 | 1.008× | 4.663 |
| `js_mvp_lmd/dense_array` | 57.728 | 57.792 | 1.005× | 11.523 |
| `js_mvp_lmd/integer_dense` | 3.022 | 2.744 | 0.908× | 2.981 |
| `js_mvp_lmd/integer_indirect` | 1.572 | 1.574 | 0.998× | 1.146 |
| `js_mvp_lmd/map_iteration` | 1.455 | 1.452 | 0.998× | 3.589 |
| `js_mvp_lmd/map_lookup` | 13.124 | 13.116 | 0.987× | 4.075 |
| `js_mvp_lmd/numeric` | 71.009 | 70.990 | 1.000× | 70.942 |
| `js_mvp_lmd/object_delete` | 2.277 | 2.264 | 0.996× | 1.117 |
| `js_mvp_lmd/object_fields` | 0.376 | 0.376 | 1.000× | 1.591 |
| `js_mvp_lmd/object_growth` | 3.031 | 3.553 | 1.172× | 3.809 |
| `js_mvp_lmd/object_retype` | 0.296 | 0.296 | 1.000× | 1.310 |
| `js_mvp_lmd/object_retype_escaped` | 6.725 | 6.815 | 1.015× | 1.317 |
| `js_mvp_lmd/strings` | 2.427 | 2.435 | 0.997× | 0.262 |

## Capture and acceptance

- Release compilation: `make build-release-compile`, exit 0, 0 errors and 25 warnings. Binary SHA-256: `d8d64af7f82907a45b17f717a59e7bab257b19270eb06aea059689e673d0f046`.
- Exact Result5 MVP control SHA-256: `190d5f602876b1023ba83e9d25eee603af5fd938ebf64c3aaaefe1ef8ff38cf7`. Node `v22.13.0`; all Lambda engines were pinned to native MIR and the untyped Lambda lane to JIT.
- 15 measured rounds plus one discarded warmup per lane. The old 42-row group passed 3,330 measured and 222 warmup output checks. The new six passed 450 measured and 30 warmup checks; a separate 24-lane preflight also passed. All run inputs and frozen binaries remained unchanged.
- Result6 JSON retains row samples, outputs, commands, hashes, fresh MIR artifacts and backend flags. Reproducible capture logs and adapters are under `temp/mvp_lmd_arrays_20261008/`.
- Semantic unit and forced-GC suites were not run. Benchmark outputs pass, but full semantic acceptance for §18 remains pending.
