# JS MVP benchmark result series

Five representative milestones, ordered by capture time. Each phase has a
Markdown report and its unchanged raw JSON. Latest: **[MVP_Result5](MVP_Result5.md)**.

| Result | Date / time (UTC+08) | Tuning phase | MVP doc | Workloads | Runs |
|---|---|---|---|---:|---:|
| [MVP_Result1](MVP_Result1.md) · [JSON](MVP_Result1.json) | 2026-10-07 08:57 | Scalar and integer tuning | §9 | 9 | 5 |
| [MVP_Result2](MVP_Result2.md) · [JSON](MVP_Result2.json) | 2026-10-07 17:51 | Object/Map and inlining tuning | §§10–12 | 17 | 15 |
| [MVP_Result3](MVP_Result3.md) · [JSON](MVP_Result3.json) | 2026-10-07 21:42 | Element coercion and scalar ownership | §§13–14 | 30 | 15 + 30-pair follow-up on 6 rows |
| [MVP_Result4](MVP_Result4.md) · [JSON](MVP_Result4.json) | 2026-10-08 09:00 | Numeric libraries and array facts | §§15–16 | 42 | 15 |
| [MVP_Result5](MVP_Result5.md) · [JSON](MVP_Result5.json) | 2026-10-08 09:48 | Slow kernels, especially triangl | §17 | 42 | 15 |

## Reading the history

- Values are MVP self-reported execution medians in milliseconds; lower is faster. `—` means not measured in that snapshot.
- Coverage grows from 9 to 42 workloads. Compare individual matched rows; aggregates from different populations are not comparable.
- Result1 used Node v24.7.0; the later results used v22.13.0. Host load and platform revisions also vary. Sequential history does not isolate an optimization; use each report’s paired control, source hashes and timing audit for that.
- Result3 uses its 30-pair follow-up where marked †; its other rows use the original 15-pair run. The original and follow-up samples remain separate in its JSON.
- Display labels normalize the early `fib2`, `fibfp2`, `sum2`, `tak2` names and underscore/microbenchmark prefixes. Original row identifiers and source paths remain in the JSON.
- The [port and timing audit](MVP_Result4.md#port-and-timing-audit) covers the expanded numeric workloads. Node tiering during a fresh process is included; these are not fully warmed V8 measurements.

## MVP execution history

| Workload | [Result1](MVP_Result1.md) | [Result2](MVP_Result2.md) | [Result3](MVP_Result3.md) | [Result4](MVP_Result4.md) | [Result5](MVP_Result5.md) |
|---|---:|---:|---:|---:|---:|
| beng/binarytrees | — | — | — | 4.212 | 3.887 |
| beng/fannkuch | — | — | — | 0.457 | 0.461 |
| beng/spectralnorm | — | — | — | 1.170 | 1.072 |
| js_micro/args_ctl | — | 4.643 | 3.966 | 3.519 | 3.206 |
| js_micro/args_fp | — | 4.724 | 3.955 | 3.519 | 3.253 |
| js_micro/lit | — | 0.137 | 0.147 | 0.138 | 0.135 |
| js_micro/named | — | 3.971 | 4.302 | 3.945 | 3.918 |
| js_mvp_lmd/calls | — | 5.908 | 5.188 † | 5.086 | 5.139 |
| js_mvp_lmd/dense_array | — | 76.830 | 58.613 † | 58.281 | 57.604 |
| js_mvp_lmd/integer_dense | 3.520 | — | 3.905 | 2.787 | 2.739 |
| js_mvp_lmd/integer_indirect | 1.482 | — | 1.955 | 1.577 | 1.580 |
| js_mvp_lmd/map_iteration | — | 1.892 | 1.442 | 1.461 | 1.455 |
| js_mvp_lmd/map_lookup | — | 13.374 | 12.932 | 13.352 | 12.961 |
| js_mvp_lmd/numeric | — | 70.975 | 71.075 | 71.193 | 70.801 |
| js_mvp_lmd/object_delete | — | 2.300 | 2.469 † | 2.564 | 2.455 |
| js_mvp_lmd/object_fields | — | 0.375 | 0.444 | 0.376 | 0.376 |
| js_mvp_lmd/object_growth | — | 3.556 | 3.904 | 3.608 | 3.514 |
| js_mvp_lmd/object_retype | — | 0.403 | 0.296 | 0.296 | 0.296 |
| js_mvp_lmd/object_retype_escaped | — | 7.697 | 6.616 | 6.781 | 6.594 |
| js_mvp_lmd/strings | — | 2.786 | 2.644 | 2.420 | 2.434 |
| kostya/collatz | 481.496 | — | 1083.473 | 523.517 | 520.372 |
| kostya/matmul | — | — | — | 5.938 | 5.910 |
| kostya/primes | — | — | — | 3.400 | 3.440 |
| larceny/array1 | — | — | — | 0.666 | 0.663 |
| larceny/deriv | — | 20.271 | 23.940 | 19.433 | 8.653 |
| larceny/diviter | 253.543 | — | 294.748 | 266.540 | 266.712 |
| larceny/divrec | 0.638 | — | 0.744 | 0.629 | 0.629 |
| larceny/gcbench | — | 107.006 | 109.881 | 100.224 | 94.913 |
| larceny/paraffins | — | — | — | 0.149 | 0.104 |
| larceny/pnpoly | — | — | 17.069 † | 13.808 | 7.436 |
| larceny/quicksort | — | — | — | 1.685 | 1.715 |
| larceny/ray | — | — | — | 0.344 | 0.236 |
| larceny/triangl | — | — | — | 620.267 | 158.933 |
| r7rs/ack | — | — | 11.154 | 8.909 | 8.913 |
| r7rs/cpstak | — | — | 0.336 | 0.246 | 0.245 |
| r7rs/fft | — | — | — | 0.176 | 0.170 |
| r7rs/fib | 1.291 | — | 1.494 † | 1.485 | 1.485 |
| r7rs/fibfp | 1.282 | — | 2.082 | 1.497 | 1.490 |
| r7rs/nqueens | — | — | — | 0.990 | 0.947 |
| r7rs/sum | 0.252 | — | 0.270 † | 0.276 | 0.270 |
| r7rs/sumfp | — | — | 0.037 | 0.027 | 0.027 |
| r7rs/tak | 0.117 | — | 0.177 | 0.125 | 0.124 |

## Original snapshot names

The selected JSON files are byte-for-byte unchanged. Historical paths and
hashes inside them retain their capture-time meaning; the table below maps
their original filenames to the published series.

| Result | Original JSON | SHA-256 |
|---|---|---|
| MVP_Result1 | `integer_mir_20261007.json` | `615765b072faddef844956c7c63db1b0932d4db65df7085e5e6dc258f7c07a34` |
| MVP_Result2 | `tuning_mir_20261007.json` | `a5fb84523743d4550dd2ae37719e8bcd14463526c5973f93b84141afa7bfb110` |
| MVP_Result3 | `element_tuning_mir_20261007.json` | `5afb735a8d2ed0a9cc060663f01ffeab391f89b21f7d6a540fc5081ec3dee29b` |
| MVP_Result4 | `numeric_tuning_mir_20261008.json` | `23126afda4b9d6c3ddd34a18c69d0979d6d19a6eb36ab114ba7f6f2833383fd3` |
| MVP_Result5 | `slow_tuning_mir_20261008.json` | `b3db70ded08c5c857bfd7d133dd4273f71b3f198ad1b3bdeee7bd6b2f1b4d8b5` |

Intermediate scalar screens, initial object support, duplicate cross-engine
refreshes and the earlier numeric-loop snapshot are omitted from the published
series. A local backup of the original 11 snapshots and four reports is in
`temp/mvp_result_series_20261008/originals/`. Existing frozen run directories
and their source/binary hashes are unchanged. Archived scripts may contain
old result paths; update a working copy when replaying, preserving the original
runner and its recorded hash.

Numbering follows phase order; each JSON retains its original schema and
capture-time provenance.
