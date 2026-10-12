# JS MVP benchmark result series

Representative milestones, ordered by capture time. The five-result limit applied
only to the initial setup; later results extend the history. Each phase has a
Markdown report and raw JSON. Results5 and 6 also retain their initial rounds inside the JSON. Latest: **[MVP_Result9](MVP_Result9.md)**.

| Result | Date / time (UTC+08) | Tuning phase | MVP doc | Workloads | Runs |
|---|---|---|---|---:|---:|
| [MVP_Result3](MVP_Result3.md) · [JSON](MVP_Result3.json) | 2026-10-07 21:42 | Element coercion and scalar ownership | §§13–14 | 30 | 15 + 30-pair follow-up on 6 rows |
| [MVP_Result4](MVP_Result4.md) · [JSON](MVP_Result4.json) | 2026-10-08 09:00 | Numeric libraries and array facts | §§15–16 | 42 | 15 |
| [MVP_Result5](MVP_Result5.md) · [JSON](MVP_Result5.json) | 2026-10-08 10:33 | Slow kernels, recursive allocation and optional integer locals | §17 | 42 | 15 |
| [MVP_Result6](MVP_Result6.md) · [JSON](MVP_Result6.json) | 2026-10-08 14:26 | Ordinary arrays, core strings and subsequent tuning | §§18–20 | 48 | 15 + 60-pair confirmations |
| [MVP_Result7](MVP_Result7.md) · [JSON](MVP_Result7.json) | 2026-10-08 18:05 | Basic classes and class workload tuning | §21 | 54 | 15 + 60-pair confirmations |
| [MVP_Result8](MVP_Result8.md) · [JSON](MVP_Result8.json) | 2026-10-09 13:08 | Closures, class specialization, CD allocation and regression fixes | §§22–28 | 60 | 15 + 30/90-round confirmations |
| [MVP_Result9](MVP_Result9.md) · [JSON](MVP_Result9.json) | 2026-10-10 19:05 | Library/generator support; Havlak, RegExp, text and jq tuning | §§29–30.1 | 87 | 3/7/15 pairs; 4 jq single observations |

## Reading the history

- Values are MVP self-reported execution medians in milliseconds (Result9 jq cells are single observations); lower is faster. `—` means not measured in that snapshot.
- Published coverage grows from 30 to 87 workloads. Compare individual matched rows; aggregates from different populations are not comparable.
- Result1 used Node v24.7.0; Results2–6 used v22.13.0. Result7 did not measure Node; Result8 uses v22.13.0 for its six new rows. Host load and platform revisions also vary. Sequential history does not isolate an optimization; use each report’s paired control, source hashes and timing audit for that.
- Result3 uses its 30-pair follow-up where marked †; its other rows use the original 15-pair run. The original and follow-up samples remain separate in its JSON.
- Result5 shows the latest 15-pair follow-up against the exact initial Result5 candidate. Its complete initial snapshot is retained as `previous_round`; a separate 30-pair escaped-retyping confirmation does not replace any cells.
- Result6 uses the final 14:25–14:26 MVP run for all 48 cells. Node/LambdaJS references remain historical; untyped Lambda uses the newest available accepted cells, labeled by capture in the report. The original Result6 is embedded as `previous_round`; confirmation runs do not replace table cells.
- Result7 preserves the final 18:03–18:05 capture for all 54 cells; its 60-pair confirmations remain separate. Only six AWFY rows have fresh untyped Lambda references. Its main control includes class support and differs from Result6; a separate confirmation uses the exact Result6 release.
- Result8 retains all 60 cells from the final 12:59–13:08 matrix; its longer confirmations remain separate. Six new rows have fresh Node and canonical Lambda references, and two have typed Lambda references. The primary control differs from Result7. External host activity makes the matrix qualified paired evidence; CD has particularly wide uncertainty.
- Result9 selects the latest accepted measurement per workload across four captures; all raw captures remain embedded. Its control is phase30 round4m, not Result8. There are no fresh Node/Lambda references. Three-pair rows are screens; the four jq cells marked ‡ are single observations, and `jq_mix` had overlapping test activity.
- Display labels normalize the early `fib2`, `fibfp2`, `sum2`, `tak2` names and underscore/microbenchmark prefixes. Original row identifiers and source paths remain in the JSON.
- The [port and timing audit](MVP_Result4.md#port-and-timing-audit) covers the expanded numeric workloads. Node tiering during a fresh process is included; these are not fully warmed V8 measurements.

## MVP execution history

| Workload | [Result3](MVP_Result3.md) | [Result4](MVP_Result4.md) | [Result5](MVP_Result5.md) | [Result6](MVP_Result6.md) | [Result7](MVP_Result7.md) | [Result8](MVP_Result8.md) | [Result9](MVP_Result9.md) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `awfy/bounce` | — | — | — | — | — | 0.619 | 0.600 |
| `awfy/cd` | — | — | — | — | — | 203.627 | 165.604 |
| `awfy/deltablue` | — | — | — | — | — | — | 51.177 |
| `awfy/havlak` | — | — | — | — | — | — | 1770.569 |
| `awfy/json` | — | — | — | — | — | — | 5.737 |
| `awfy/list` | — | — | — | — | 0.402 | 0.283 | 0.262 |
| `awfy/mandelbrot` | — | — | — | — | 32.237 | 32.772 | 32.372 |
| `awfy/nbody` | — | — | — | — | — | 46.571 | 44.523 |
| `awfy/permute` | — | — | — | — | 0.812 | 0.629 | 0.537 |
| `awfy/queens` | — | — | — | — | 0.440 | 0.345 | 0.365 |
| `awfy/richards` | — | — | — | — | — | 75.843 | 74.216 |
| `awfy/sieve` | — | — | — | — | 0.093 | 0.128 | 0.094 |
| `awfy/storage` | — | — | — | — | — | 0.522 | 0.513 |
| `awfy/towers` | — | — | — | — | 1.697 | 1.148 | 1.084 |
| `beng/binarytrees` | — | 4.212 | 3.753 | 3.793 | 3.826 | 4.144 | 3.685 |
| `beng/fannkuch` | — | 0.457 | 0.380 | 0.264 | 0.262 | 0.312 | 0.269 |
| `beng/fasta` | — | — | — | — | — | — | 0.884 |
| `beng/knucleotide` | — | — | — | — | — | — | 3.859 |
| `beng/pidigits` | — | — | — | — | — | — | 0.355 |
| `beng/regexredux` | — | — | — | — | — | — | 3.139 |
| `beng/revcomp` | — | — | — | — | — | — | 3.960 |
| `beng/spectralnorm` | — | 1.170 | 1.080 | 1.066 | 1.174 | 1.229 | 1.206 |
| `jetstream/crypto_sha1` | — | — | — | — | — | 15.774 | 13.961 |
| `jetstream/cube3d` | — | — | — | — | — | — | 68.351 |
| `jetstream/navier_stokes` | — | — | — | — | — | — | 32.979 |
| `jetstream/raytrace3d` | — | — | — | — | — | — | 137.727 |
| `jetstream/splay` | — | — | — | — | — | — | 61.610 |
| `js_micro/args_ctl` | 3.966 | 3.519 | 2.699 | 2.708 | 2.704 | 3.568 | 2.710 |
| `js_micro/args_fp` | 3.955 | 3.519 | 2.692 | 2.718 | 2.700 | 3.613 | 2.983 |
| `js_micro/lit` | 0.147 | 0.138 | 0.134 | 0.133 | 0.133 | 0.152 | 0.141 |
| `js_micro/named` | 4.302 | 3.945 | 3.898 | 3.953 | 3.944 | 5.162 | 3.961 |
| `js_mvp_lmd/calls` | 5.188 † | 5.086 | 5.046 | 5.090 | 5.012 | 5.151 | 5.586 |
| `js_mvp_lmd/dense_array` | 58.613 † | 58.281 | 57.225 | 57.855 | 57.570 | 58.646 | 63.017 |
| `js_mvp_lmd/integer_dense` | 3.905 | 2.787 | 2.741 | 2.715 | 2.708 | 3.054 | 2.769 |
| `js_mvp_lmd/integer_indirect` | 1.955 | 1.577 | 1.570 | 1.572 | 1.575 | 1.734 | 1.648 |
| `js_mvp_lmd/map_iteration` | 1.442 | 1.461 | 1.454 | 1.453 | 1.454 | 1.454 | 1.289 |
| `js_mvp_lmd/map_lookup` | 12.932 | 13.352 | 13.286 | 13.048 | 8.386 | 8.762 | 8.394 |
| `js_mvp_lmd/numeric` | 71.075 | 71.193 | 70.723 | 70.888 | 70.688 | 76.121 | 79.407 |
| `js_mvp_lmd/object_delete` | 2.469 † | 2.564 | 2.373 | 2.424 | 1.188 | 0.939 | 0.933 |
| `js_mvp_lmd/object_fields` | 0.444 | 0.376 | 0.376 | 0.376 | 0.376 | 0.497 | 0.409 |
| `js_mvp_lmd/object_growth` | 3.904 | 3.608 | 3.540 | 2.985 | 2.891 | 3.753 | 3.194 |
| `js_mvp_lmd/object_retype` | 0.296 | 0.296 | 0.296 | 0.296 | 0.296 | 0.335 | 0.296 |
| `js_mvp_lmd/object_retype_escaped` | 6.616 | 6.781 | 6.699 | 6.818 | 5.869 | 5.978 | 6.261 |
| `js_mvp_lmd/strings` | 2.644 | 2.420 | 2.416 | 0.101 | 0.103 | 0.106 | 0.105 |
| `julia/formatted_output` | — | — | — | — | — | — | 107.319 |
| `julia/iteration_pi_sum` | — | — | — | — | — | — | 23.337 |
| `julia/matrix_statistics` | — | — | — | — | — | — | 112.126 |
| `julia/parse_integers` | — | — | — | — | — | — | 52.147 |
| `kostya/base64` | — | — | — | 11.869 | 11.879 | 13.691 | 12.108 |
| `kostya/brainfuck` | — | — | — | 42.139 | 41.607 | 46.709 | 45.740 |
| `kostya/collatz` | 1083.473 | 523.517 | 519.727 | 200.168 | 199.548 | 203.495 | 204.036 |
| `kostya/json_gen` | — | — | — | 5.614 | 5.697 | 6.505 | 6.463 |
| `kostya/levenshtein` | — | — | — | 1.563 | 1.561 | 1.624 | 1.603 |
| `kostya/matmul` | — | 5.938 | 5.853 | 5.903 | 5.891 | 6.922 | 6.685 |
| `kostya/primes` | — | 3.400 | 3.382 | 2.074 | 2.044 | 2.367 | 2.075 |
| `larceny/array1` | — | 0.666 | 0.663 | 0.309 | 0.310 | 0.386 | 0.350 |
| `larceny/deriv` | 23.940 | 19.433 | 8.465 | 8.487 | 8.518 | 9.183 | 9.368 |
| `larceny/diviter` | 294.748 | 266.540 | 266.126 | 266.577 | 265.662 | 272.968 | 270.806 |
| `larceny/divrec` | 0.744 | 0.629 | 0.628 | 0.618 | 0.618 | 0.628 | 0.627 |
| `larceny/gcbench` | 109.881 | 100.224 | 91.792 | 89.510 | 89.915 | 99.043 | 96.138 |
| `larceny/paraffins` | — | 0.149 | 0.103 | 0.102 | 0.102 | 0.104 | 0.114 |
| `larceny/pnpoly` | 17.069 † | 13.808 | 7.332 | 7.391 | 7.394 | 8.463 | 5.978 |
| `larceny/puzzle` | — | — | — | 2.492 | 2.482 | 2.810 | 2.492 |
| `larceny/quicksort` | — | 1.685 | 1.298 | 1.304 | 1.299 | 1.467 | 1.345 |
| `larceny/ray` | — | 0.344 | 0.237 | 0.238 | 0.239 | 0.269 | 0.252 |
| `larceny/triangl` | — | 620.267 | 155.499 | 155.729 | 154.755 | 164.662 | 175.025 |
| `r7rs/ack` | 11.154 | 8.909 | 8.884 | 8.947 | 8.906 | 10.675 | 9.469 |
| `r7rs/cpstak` | 0.336 | 0.246 | 0.245 | 0.243 | 0.247 | 0.290 | 0.253 |
| `r7rs/fft` | — | 0.176 | 0.168 | 0.165 | 0.164 | 0.195 | 0.188 |
| `r7rs/fib` | 1.494 † | 1.485 | 1.501 | 1.480 | 1.482 | 1.690 | 1.521 |
| `r7rs/fibfp` | 2.082 | 1.497 | 1.500 | 1.501 | 1.505 | 1.733 | 1.492 |
| `r7rs/mbrot` | — | — | — | 0.855 | 0.482 | 0.540 | 0.543 |
| `r7rs/nqueens` | — | 0.990 | 0.925 | 0.925 | 0.930 | 1.028 | 1.023 |
| `r7rs/sum` | 0.270 † | 0.276 | 0.270 | 0.270 | 0.270 | 0.319 | 0.276 |
| `r7rs/sumfp` | 0.037 | 0.027 | 0.027 | 0.027 | 0.027 | 0.032 | 0.028 |
| `r7rs/tak` | 0.177 | 0.125 | 0.123 | 0.123 | 0.123 | 0.146 | 0.123 |
| `text/fast_diff` | — | — | — | — | — | — | 202.953 |
| `text/hyphen` | — | — | — | — | — | — | 41.964 |
| `text/jq_bf` | — | — | — | — | — | — | 307974.399 ‡ |
| `text/jq_mix` | — | — | — | — | — | — | 306339.417 ‡ |
| `text/jq_records` | — | — | — | — | — | — | 73137.311 ‡ |
| `text/jq_tree` | — | — | — | — | — | — | 455800.465 ‡ |
| `text/log_pipeline` | — | — | — | — | — | — | 4844.894 |
| `text/microdiff` | — | — | — | — | — | — | 101.355 |
| `text/prettier_ast` | — | — | — | — | — | — | 532.991 |
| `text/text_search` | — | — | — | — | — | — | 6041.896 |
| `text/three_way_merge` | — | — | — | — | — | — | 1518.598 |

## Original snapshot names

Results3–4 retain their original JSON captures; Results5 and 6 embed their initial rounds. Result1, the first nine-row screen, was rolled out to keep the published series at five reports. Its report and JSON are retained in the local backup `temp/mvp_result_series_20261008/originals/`.

Result2 was also rolled out when Result7 was added. Its exact report and JSON are retained in `temp/mvp_result_series_20261008/retired_result2/`; `manifest.json` records their hashes. Archived Result2 JSON SHA-256: `a5fb84523743d4550dd2ae37719e8bcd14463526c5973f93b84141afa7bfb110`.

| Result | Original capture | SHA-256 |
|---|---|---|
| MVP_Result2 | `tuning_mir_20261007.json` | `a5fb84523743d4550dd2ae37719e8bcd14463526c5973f93b84141afa7bfb110` |
| MVP_Result3 | `element_tuning_mir_20261007.json` | `5afb735a8d2ed0a9cc060663f01ffeab391f89b21f7d6a540fc5081ec3dee29b` |
| MVP_Result4 | `numeric_tuning_mir_20261008.json` | `23126afda4b9d6c3ddd34a18c69d0979d6d19a6eb36ab114ba7f6f2833383fd3` |
| MVP_Result5 initial round (embedded) | `slow_tuning_mir_20261008.json` | `b3db70ded08c5c857bfd7d133dd4273f71b3f198ad1b3bdeee7bd6b2f1b4d8b5` |
| MVP_Result6 initial round (embedded) | `previous_round` | `4018b294b6f6db1806791a9667bc6bf085dc2348488a0a4223e727a837ac842b` |

Numbering follows phase order. The published series includes Result3–Result8; new results extend it without retiring existing milestones. Results5 and 6 retain their latest run in `metadata`/`rows` and initial run under `previous_round`.
The Result5 JSON SHA-256 remains `1e60a3d807d27d3bafa783b633404226483d40c38e1d0751bc40138d154117f0`. Updated Result6's JSON SHA-256 is `39494d4484af17796cca1e7542130ebcead86f8d2366ef8d556e87bcd8d5617b`. Latest Result6 evidence is under `temp/mvp_followup_20261008/`; original capture evidence remains under `temp/mvp_lmd_arrays_20261008/`.

Result7 JSON SHA-256: `936321276f1a0ff3b083a6f35c720f5b2cf52ba61c1613571cb75058810b9364`. Its two headline captures and three confirmation captures are embedded without changing their samples. Latest evidence is under `temp/mvp_class_tuning_20261008/`.

Result8 JSON SHA-256: `b71024e081ef564fa710df663066ab2822b5a37dbb1c09af37f6b3d57c16cb6c`. Its three matrix captures, three longer confirmations and three-workload shared Lambda capture are embedded without changing their samples. Latest evidence is under `temp/mvp_regressions_20261009/accepted/`.

Result9 JSON SHA-256: `a25c61a3de38c2ef1f436936e97caefbdbb5175ff00c20a89d56677c902fdfe8`. Its four accepted captures retain every original sample; 87 selected rows use the latest accepted capture. Source/output text, validation logs and jq activity evidence are embedded. Exact binaries and MIR dumps remain under `temp/mvp_tune_library_20261010/` and the recorded control path.
