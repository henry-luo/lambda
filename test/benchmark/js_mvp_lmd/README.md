# JS MVP benchmark result series

Five representative milestones, ordered by capture time. Each phase has a
Markdown report and raw JSON. Result5 also retains its initial round inside the JSON. Latest: **[MVP_Result6](MVP_Result6.md)**.

| Result | Date / time (UTC+08) | Tuning phase | MVP doc | Workloads | Runs |
|---|---|---|---|---:|---:|
| [MVP_Result2](MVP_Result2.md) · [JSON](MVP_Result2.json) | 2026-10-07 17:51 | Object/Map and inlining tuning | §§10–12 | 17 | 15 |
| [MVP_Result3](MVP_Result3.md) · [JSON](MVP_Result3.json) | 2026-10-07 21:42 | Element coercion and scalar ownership | §§13–14 | 30 | 15 + 30-pair follow-up on 6 rows |
| [MVP_Result4](MVP_Result4.md) · [JSON](MVP_Result4.json) | 2026-10-08 09:00 | Numeric libraries and array facts | §§15–16 | 42 | 15 |
| [MVP_Result5](MVP_Result5.md) · [JSON](MVP_Result5.json) | 2026-10-08 10:33 | Slow kernels, recursive allocation and optional integer locals | §17 | 42 | 15 |
| [MVP_Result6](MVP_Result6.md) · [JSON](MVP_Result6.json) | 2026-10-08 11:41 | Ordinary arrays and core string methods | §18 | 48 | 15 |

## Reading the history

- Values are MVP self-reported execution medians in milliseconds; lower is faster. `—` means not measured in that snapshot.
- Coverage grows from 17 to 48 workloads. Compare individual matched rows; aggregates from different populations are not comparable.
- Result1 used Node v24.7.0; the later results used v22.13.0. Host load and platform revisions also vary. Sequential history does not isolate an optimization; use each report’s paired control, source hashes and timing audit for that.
- Result3 uses its 30-pair follow-up where marked †; its other rows use the original 15-pair run. The original and follow-up samples remain separate in its JSON.
- Result5 shows the latest 15-pair follow-up against the exact initial Result5 candidate. Its complete initial snapshot is retained as `previous_round`; a separate 30-pair escaped-retyping confirmation does not replace any cells.
- Display labels normalize the early `fib2`, `fibfp2`, `sum2`, `tak2` names and underscore/microbenchmark prefixes. Original row identifiers and source paths remain in the JSON.
- The [port and timing audit](MVP_Result4.md#port-and-timing-audit) covers the expanded numeric workloads. Node tiering during a fresh process is included; these are not fully warmed V8 measurements.

## MVP execution history

| Workload | [Result2](MVP_Result2.md) | [Result3](MVP_Result3.md) | [Result4](MVP_Result4.md) | [Result5](MVP_Result5.md) | [Result6](MVP_Result6.md) |
|---|---:|---:|---:|---:|---:|
| `beng/binarytrees` | — | — | 4.212 | 3.753 | 3.748 |
| `beng/fannkuch` | — | — | 0.457 | 0.380 | 0.385 |
| `beng/spectralnorm` | — | — | 1.170 | 1.080 | 1.083 |
| `js_micro/args_ctl` | 4.643 | 3.966 | 3.519 | 2.699 | 2.698 |
| `js_micro/args_fp` | 4.724 | 3.955 | 3.519 | 2.692 | 2.705 |
| `js_micro/lit` | 0.137 | 0.147 | 0.138 | 0.134 | 0.135 |
| `js_micro/named` | 3.971 | 4.302 | 3.945 | 3.898 | 3.944 |
| `js_mvp_lmd/calls` | 5.908 | 5.188 † | 5.086 | 5.046 | 5.065 |
| `js_mvp_lmd/dense_array` | 76.830 | 58.613 † | 58.281 | 57.225 | 57.728 |
| `js_mvp_lmd/integer_dense` | — | 3.905 | 2.787 | 2.741 | 3.022 |
| `js_mvp_lmd/integer_indirect` | — | 1.955 | 1.577 | 1.570 | 1.572 |
| `js_mvp_lmd/map_iteration` | 1.892 | 1.442 | 1.461 | 1.454 | 1.455 |
| `js_mvp_lmd/map_lookup` | 13.374 | 12.932 | 13.352 | 13.286 | 13.124 |
| `js_mvp_lmd/numeric` | 70.975 | 71.075 | 71.193 | 70.723 | 71.009 |
| `js_mvp_lmd/object_delete` | 2.300 | 2.469 † | 2.564 | 2.373 | 2.277 |
| `js_mvp_lmd/object_fields` | 0.375 | 0.444 | 0.376 | 0.376 | 0.376 |
| `js_mvp_lmd/object_growth` | 3.556 | 3.904 | 3.608 | 3.540 | 3.031 |
| `js_mvp_lmd/object_retype` | 0.403 | 0.296 | 0.296 | 0.296 | 0.296 |
| `js_mvp_lmd/object_retype_escaped` | 7.697 | 6.616 | 6.781 | 6.699 | 6.725 |
| `js_mvp_lmd/strings` | 2.786 | 2.644 | 2.420 | 2.416 | 2.427 |
| `kostya/base64` | — | — | — | — | 81.083 |
| `kostya/brainfuck` | — | — | — | — | 125.715 |
| `kostya/collatz` | — | 1083.473 | 523.517 | 519.727 | 524.493 |
| `kostya/json_gen` | — | — | — | — | 14.350 |
| `kostya/levenshtein` | — | — | — | — | 121.696 |
| `kostya/matmul` | — | — | 5.938 | 5.853 | 5.872 |
| `kostya/primes` | — | — | 3.400 | 3.382 | 3.453 |
| `larceny/array1` | — | — | 0.666 | 0.663 | 0.662 |
| `larceny/deriv` | 20.271 | 23.940 | 19.433 | 8.465 | 8.541 |
| `larceny/diviter` | — | 294.748 | 266.540 | 266.126 | 250.297 |
| `larceny/divrec` | — | 0.744 | 0.629 | 0.628 | 0.629 |
| `larceny/gcbench` | 107.006 | 109.881 | 100.224 | 91.792 | 90.752 |
| `larceny/paraffins` | — | — | 0.149 | 0.103 | 0.104 |
| `larceny/pnpoly` | — | 17.069 † | 13.808 | 7.332 | 7.351 |
| `larceny/puzzle` | — | — | — | — | 2.489 |
| `larceny/quicksort` | — | — | 1.685 | 1.298 | 1.301 |
| `larceny/ray` | — | — | 0.344 | 0.237 | 0.237 |
| `larceny/triangl` | — | — | 620.267 | 155.499 | 155.702 |
| `r7rs/ack` | — | 11.154 | 8.909 | 8.884 | 8.876 |
| `r7rs/cpstak` | — | 0.336 | 0.246 | 0.245 | 0.245 |
| `r7rs/fft` | — | — | 0.176 | 0.168 | 0.169 |
| `r7rs/fib` | — | 1.494 † | 1.485 | 1.501 | 1.478 |
| `r7rs/fibfp` | — | 2.082 | 1.497 | 1.500 | 1.486 |
| `r7rs/mbrot` | — | — | — | — | 0.852 |
| `r7rs/nqueens` | — | — | 0.990 | 0.925 | 0.923 |
| `r7rs/sum` | — | 0.270 † | 0.276 | 0.270 | 0.270 |
| `r7rs/sumfp` | — | 0.037 | 0.027 | 0.027 | 0.027 |
| `r7rs/tak` | — | 0.177 | 0.125 | 0.123 | 0.124 |
## Original snapshot names

Results2–5 retain their original JSON captures; Result5 also embeds its initial round. Result1, the first nine-row screen, was rolled out to keep the published series at five reports. Its report and JSON are retained in the local backup `temp/mvp_result_series_20261008/originals/`.

| Result | Original capture | SHA-256 |
|---|---|---|
| MVP_Result2 | `tuning_mir_20261007.json` | `a5fb84523743d4550dd2ae37719e8bcd14463526c5973f93b84141afa7bfb110` |
| MVP_Result3 | `element_tuning_mir_20261007.json` | `5afb735a8d2ed0a9cc060663f01ffeab391f89b21f7d6a540fc5081ec3dee29b` |
| MVP_Result4 | `numeric_tuning_mir_20261008.json` | `23126afda4b9d6c3ddd34a18c69d0979d6d19a6eb36ab114ba7f6f2833383fd3` |
| MVP_Result5 initial round (embedded) | `slow_tuning_mir_20261008.json` | `b3db70ded08c5c857bfd7d133dd4273f71b3f198ad1b3bdeee7bd6b2f1b4d8b5` |
| MVP_Result6 | `MVP_Result6.json` | `4018b294b6f6db1806791a9667bc6bf085dc2348488a0a4223e727a837ac842b` |

Numbering follows phase order. The rolling series retains Result2–Result6. Result5 retains its latest run in `metadata`/`rows` and its initial run under `previous_round`.
The Result5 JSON SHA-256 remains `1e60a3d807d27d3bafa783b633404226483d40c38e1d0751bc40138d154117f0`. Result6's JSON SHA-256 is `4018b294b6f6db1806791a9667bc6bf085dc2348488a0a4223e727a837ac842b`. Fresh Result6 evidence is under `temp/mvp_lmd_arrays_20261008/`.
