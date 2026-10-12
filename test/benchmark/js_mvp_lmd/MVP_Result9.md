# JS MVP Benchmark Results: MVP_Result9

**Library and generator support, with Havlak, RegExp, text and jq tuning** —
[MVP §§29–30.1](../../../vibe/jube/JS_MVP_Lmd.md#30-library-completion-and-generator-based-benchmarks).

[Series index and comparison](README.md) · [Raw JSON](MVP_Result9.json) · [Previous phase](MVP_Result8.md)

## Summary

- **87 workloads pass their output checks**: 70 standard benchmarks and 17 microbenchmarks, adding 27 since Result8.
- Havlak takes **11.2% less time**, regexredux is **12.1× faster**, and text_search takes **22.2% less time** against the phase30 round4m control.
- Four jq workloads show **17.6–24.8% less time** in single observations. The `jq_mix` candidate overlapped another test; its ratio is descriptive.
- No new slowdown is confirmed against both control copies in the 83 repeated non-jq comparisons. Short screens and host noise limit this conclusion.
- Publication records existing evidence; no benchmark or implementation test was rerun.

## Measurement and limits

Captures span **2026-10-10 17:38:28–19:05:49 UTC+08**, on macOS arm64.
The selected rows comprise **53 three-pair screens, three seven-pair target
measurements, 27 fifteen-pair confirmations, and four single jq observations**.
Repeated captures include one discarded fresh process per lane and an identical
control peer; measured lane order cycles through all six permutations. Each jq
workload runs once per binary, alternating which binary runs first by workload.

Each table cell uses its latest accepted capture. All earlier observations from
the four accepted captures remain in JSON, including the original 83-row screen.
The control is **round4m, not Result8**. No Node or Lambda reference lane was
refreshed, and no historical reference timings are substituted. Differences from
Result8 are history, not causal measurements of these optimizations.

Repeated-row intervals are **two-sided 95% paired-bootstrap intervals for the
ratio of medians**, with 10,000 resamples. Single jq observations have no interval.
Capture-level timestamps date this replay; raw row `started`/`finished` fields,
where present, were inherited from earlier catalog records.

```text
JS_EXEC_BACKEND=mir
JS_EXECUTION_BACKEND=mir
JS_MIR_INTERP=0
LAMBDA_JS_LARGE_INTERP=0
LAMBDA_EXEC_BACKEND=jit
LAMBDA_TIER=jit
```

Release binaries, pinned MIR, unchanged canonical workloads and output checks. Times are self-reported workload execution; compilation and process time are separate.

Control: `5499bb795ecc8ea24aa49208d2b3852d5f9f44fc92c131e93cec87aa92a7e75d`.
Candidate: `2c67152b386faf735960f302264fb08602042812be47eebb09a3970a899b29af`.

## Changes

- Preserve numeric array indices until a named-property fallback actually needs a string.
- Reuse completed class shapes for proven conditional and nested initializers with simple parameters, through class metadata and existing property stores (D3.4.3v5, D3.4.5). Expanded admission adds no new per-site cache cells (D8.4.1v2).
- Prepare RegExp input once per operation; reuse shared matcher scratch and direct ASCII offsets. `MvpLmdRegExpInput` is the only new local helper type.
- Reduce shared activation discard/reclaim system calls and retain one idle stack for immediate reuse (D1.3v3, D5.1.1v3). Precise roots, warm16 and the 32-stack pool limit remain; retained pages can add at most 3 MiB resident memory.

## Requested targets

| Benchmark | Control ms | Candidate ms | Less time | Speedup | Evidence |
|---|---:|---:|---:|---:|---|
| awfy/havlak | 1993.157 | 1770.569 | 11.2% | 1.13x | 7 measured pairs plus identical-control peer |
| beng/regexredux | 37.979 | 3.139 | 91.7% | 12.10x | 7 measured pairs plus identical-control peer |
| text/text_search | 7764.503 | 6041.896 | 22.2% | 1.29x | 7 measured pairs plus identical-control peer |
| text/jq_records | 88767.855 | 73137.311 | 17.6% | 1.21x | one matched observation |
| text/jq_mix | 379301.152 | 306339.417 | 19.2% | 1.24x | one matched observation; overlapping test activity |
| text/jq_bf | 390781.679 | 307974.399 | 21.2% | 1.27x | one matched observation |
| text/jq_tree | 605849.963 | 455800.465 | 24.8% | 1.33x | one matched observation |

## Complete 87-workload matrix

All times are milliseconds. `MVP / control` below one means faster MVP.
An interval spanning one is inconclusive. † marks a single observation;
‡ also marks overlapping test activity. Raw samples and both control comparisons are in JSON.

### Standard workloads (70)

| Workload | Control ms | MVP ms | MVP / control | 95% interval | Pairs |
| --- | ---: | ---: | ---: | ---: | ---: |
| awfy/bounce | 0.601 | 0.600 | 0.9983 | 0.9274–1.0101 | 3 |
| awfy/cd | 173.439 | 165.604 | 0.9548 | 0.9283–0.9691 | 3 |
| awfy/deltablue | 53.910 | 51.177 | 0.9493 | 0.9312–0.9554 | 3 |
| awfy/havlak | 1993.157 | 1770.569 | 0.8883 | 0.8699–0.9176 | 7 |
| awfy/json | 5.804 | 5.737 | 0.9885 | 0.9570–0.9958 | 3 |
| awfy/list | 0.258 | 0.262 | 1.0155 | 0.9737–1.0275 | 15 |
| awfy/mandelbrot | 32.248 | 32.372 | 1.0038 | 0.9938–1.0080 | 15 |
| awfy/nbody | 44.149 | 44.523 | 1.0085 | 0.9962–1.0589 | 3 |
| awfy/permute | 0.536 | 0.537 | 1.0019 | 0.9799–1.0168 | 15 |
| awfy/queens | 0.363 | 0.365 | 1.0055 | 0.9697–1.0399 | 3 |
| awfy/richards | 75.068 | 74.216 | 0.9887 | 0.9713–0.9979 | 15 |
| awfy/sieve | 0.095 | 0.094 | 0.9895 | 0.9495–1.0104 | 15 |
| awfy/storage | 0.508 | 0.513 | 1.0098 | 1.0059–1.0176 | 3 |
| awfy/towers | 1.073 | 1.084 | 1.0103 | 1.0037–1.0167 | 3 |
| beng/binarytrees | 3.726 | 3.685 | 0.9890 | 0.9725–1.0052 | 15 |
| beng/fannkuch | 0.267 | 0.269 | 1.0075 | 0.9813–1.0227 | 15 |
| beng/fasta | 0.889 | 0.884 | 0.9944 | 0.9847–1.0023 | 3 |
| beng/knucleotide | 3.873 | 3.859 | 0.9964 | 0.9849–1.0036 | 3 |
| beng/pidigits | 0.356 | 0.355 | 0.9972 | 0.9557–1.0341 | 3 |
| beng/regexredux | 37.979 | 3.139 | 0.0827 | 0.0801–0.0846 | 7 |
| beng/revcomp | 3.942 | 3.960 | 1.0046 | 0.9437–1.0302 | 3 |
| beng/spectralnorm | 1.326 | 1.206 | 0.9095 | 0.8755–1.0008 | 3 |
| jetstream/crypto_sha1 | 13.852 | 13.961 | 1.0079 | 0.9973–1.0173 | 15 |
| jetstream/cube3d | 67.343 | 68.351 | 1.0150 | 0.9999–1.0288 | 3 |
| jetstream/navier_stokes | 33.143 | 32.979 | 0.9951 | 0.9546–1.0084 | 3 |
| jetstream/raytrace3d | 138.596 | 137.727 | 0.9937 | 0.9886–0.9944 | 3 |
| jetstream/splay | 60.937 | 61.610 | 1.0110 | 1.0043–1.0170 | 15 |
| julia/formatted_output | 105.924 | 107.319 | 1.0132 | 0.9874–1.0207 | 15 |
| julia/iteration_pi_sum | 23.367 | 23.337 | 0.9987 | 0.9899–1.0042 | 15 |
| julia/matrix_statistics | 112.162 | 112.126 | 0.9997 | 0.9953–1.0007 | 3 |
| julia/parse_integers | 52.391 | 52.147 | 0.9953 | 0.9895–1.4354 | 3 |
| kostya/base64 | 11.990 | 12.108 | 1.0098 | 0.9997–1.0210 | 15 |
| kostya/brainfuck | 45.773 | 45.740 | 0.9993 | 0.9712–1.0376 | 15 |
| kostya/collatz | 204.002 | 204.036 | 1.0002 | 0.9205–1.0803 | 15 |
| kostya/json_gen | 6.913 | 6.463 | 0.9349 | 0.9349–1.0060 | 3 |
| kostya/levenshtein | 1.581 | 1.603 | 1.0139 | 0.9810–1.0215 | 3 |
| kostya/matmul | 6.901 | 6.685 | 0.9687 | 0.9620–1.0059 | 3 |
| kostya/primes | 2.095 | 2.075 | 0.9905 | 0.9712–1.0136 | 15 |
| larceny/array1 | 0.348 | 0.350 | 1.0057 | 0.9642–1.1230 | 3 |
| larceny/deriv | 9.629 | 9.368 | 0.9729 | 0.8832–1.0240 | 3 |
| larceny/diviter | 272.782 | 270.806 | 0.9928 | 0.9846–0.9995 | 3 |
| larceny/divrec | 0.628 | 0.627 | 0.9984 | 0.9797–1.0000 | 3 |
| larceny/gcbench | 98.576 | 96.138 | 0.9753 | 0.8262–1.0208 | 3 |
| larceny/paraffins | 0.115 | 0.114 | 0.9913 | 0.9744–1.0174 | 3 |
| larceny/pnpoly | 5.969 | 5.978 | 1.0015 | 0.9904–1.0185 | 15 |
| larceny/puzzle | 2.491 | 2.492 | 1.0004 | 0.9952–1.0213 | 15 |
| larceny/quicksort | 1.416 | 1.345 | 0.9499 | 0.9181–0.9979 | 3 |
| larceny/ray | 0.251 | 0.252 | 1.0040 | 0.9721–1.1004 | 3 |
| larceny/triangl | 175.666 | 175.025 | 0.9964 | 0.9927–1.1751 | 3 |
| r7rs/ack | 9.488 | 9.469 | 0.9980 | 0.9808–1.0722 | 3 |
| r7rs/cpstak | 0.255 | 0.253 | 0.9922 | 0.9728–1.0161 | 3 |
| r7rs/fft | 0.186 | 0.188 | 1.0108 | 0.9624–1.0217 | 3 |
| r7rs/fib | 1.500 | 1.521 | 1.0140 | 1.0000–1.0267 | 3 |
| r7rs/fibfp | 1.504 | 1.492 | 0.9920 | 0.9792–0.9920 | 3 |
| r7rs/mbrot | 0.540 | 0.543 | 1.0056 | 0.9924–1.0093 | 3 |
| r7rs/nqueens | 1.018 | 1.023 | 1.0049 | 0.9882–1.0312 | 15 |
| r7rs/sum | 0.276 | 0.276 | 1.0000 | 1.0000–1.0652 | 3 |
| r7rs/sumfp | 0.028 | 0.028 | 1.0000 | 0.9655–1.0370 | 3 |
| r7rs/tak | 0.124 | 0.123 | 0.9919 | 0.9840–1.0484 | 3 |
| text/fast_diff | 297.687 | 202.953 | 0.6818 | 0.6813–0.6847 | 3 |
| text/hyphen | 50.959 | 41.964 | 0.8235 | 0.7765–0.8629 | 3 |
| text/jq_bf † | 390781.679 | 307974.399 | 0.7881 | — | 1 |
| text/jq_mix †‡ | 379301.152 | 306339.417 | 0.8076 | — | 1 |
| text/jq_records † | 88767.855 | 73137.311 | 0.8239 | — | 1 |
| text/jq_tree † | 605849.963 | 455800.465 | 0.7523 | — | 1 |
| text/log_pipeline | 4972.954 | 4844.894 | 0.9742 | 0.9742–0.9881 | 3 |
| text/microdiff | 101.301 | 101.355 | 1.0005 | 0.9961–1.0041 | 15 |
| text/prettier_ast | 532.221 | 532.991 | 1.0014 | 0.9612–1.0275 | 15 |
| text/text_search | 7764.503 | 6041.896 | 0.7781 | 0.7481–0.7976 | 7 |
| text/three_way_merge | 1525.033 | 1518.598 | 0.9958 | 0.9718–1.0036 | 3 |

### Microbenchmarks (17)

| Workload | Control ms | MVP ms | MVP / control | 95% interval | Pairs |
| --- | ---: | ---: | ---: | ---: | ---: |
| js_micro/args_ctl | 2.710 | 2.710 | 1.0000 | 0.9908–1.0103 | 15 |
| js_micro/args_fp | 2.948 | 2.983 | 1.0119 | 0.9997–1.0350 | 3 |
| js_micro/lit | 0.141 | 0.141 | 1.0000 | 0.7486–1.0355 | 3 |
| js_micro/named | 3.967 | 3.961 | 0.9985 | 0.9880–1.0048 | 15 |
| js_mvp_lmd/calls | 5.516 | 5.586 | 1.0127 | 0.9569–1.0384 | 15 |
| js_mvp_lmd/dense_array | 65.077 | 63.017 | 0.9683 | 0.9608–1.0445 | 3 |
| js_mvp_lmd/integer_dense | 2.820 | 2.769 | 0.9819 | 0.9514–1.0442 | 3 |
| js_mvp_lmd/integer_indirect | 1.653 | 1.648 | 0.9970 | 0.9598–1.1927 | 3 |
| js_mvp_lmd/map_iteration | 1.286 | 1.289 | 1.0023 | 1.0008–1.0078 | 15 |
| js_mvp_lmd/map_lookup | 8.496 | 8.394 | 0.9880 | 0.8942–1.0064 | 3 |
| js_mvp_lmd/numeric | 79.602 | 79.407 | 0.9976 | 0.9976–1.0030 | 3 |
| js_mvp_lmd/object_delete | 0.932 | 0.933 | 1.0011 | 0.9806–1.1097 | 15 |
| js_mvp_lmd/object_fields | 0.408 | 0.409 | 1.0025 | 1.0025–1.0316 | 3 |
| js_mvp_lmd/object_growth | 3.246 | 3.194 | 0.9840 | 0.9571–1.0862 | 3 |
| js_mvp_lmd/object_retype | 0.296 | 0.296 | 1.0000 | 1.0000–1.0101 | 15 |
| js_mvp_lmd/object_retype_escaped | 6.192 | 6.261 | 1.0111 | 0.7655–1.0111 | 3 |
| js_mvp_lmd/strings | 0.103 | 0.105 | 1.0194 | 0.9528–1.1212 | 15 |

## Validation and limits

Repeated non-jq rows slower against both control lanes at the bootstrap interval: none.

Single jq observations establish output parity and describe this capture; they do not establish a confidence interval. Three-pair rows are a screen; small apparent changes need longer confirmation.

Background activity: the first round6 jq capture (`jq6-final`) was interrupted and rejected for attribution after concurrent Lambda-opus baseline tests caused an unexpected slowdown. The monitored retry (`jq6-quiet`) retains five-second process samples. Another test overlapped the jq_mix candidate, so its ratio is descriptive, not isolated causal evidence. The activity summary estimates timed overlap; it does not eliminate ordinary OS activity. Some late non-jq screen rows were also noisy; longer paired confirmation found no new slowdown against both controls.

Correctness gates recorded before publication: MVP **100/100** and ActivationCore **10/10**, normally and under forced GC/poisoning. Test262 **40,261/40,261**, zero unstable results or retries, with 2,652 excluded cases; this covers the shared runtime/full LambdaJS, not bounded MVP conformance. Lambda **6,607/6,608**, retaining the known `edit_view_only` failure. These are separate correctness binaries/gates; benchmark timings use the frozen release above.

The first candidate regressed triangl by 3.7x because it boxed an optional numeric index and lost its native presence/range proofs. That candidate is rejected (`final/REJECTED.md`, `final83/`). The correction preserves those proofs and has a focused MIR assertion; fresh measurements restore control performance.

Round5 also exposed a constructor admission bug: default parameters can inspect this before the body. Round6 excludes non-simple parameters and includes a focused regression. `default_receiver.json` records control, rejected candidate, corrected candidate and Node output. All final timing rows use the round6 binary; superseded captures remain marked rejected or interrupted.

The previous phase's **log_pipeline +1.3% regression** against its older phase29 control remains open; this round compares against round4m and does not establish that the older issue is fixed. Existing published Results3–8 are unchanged. Validation is on macOS arm64; Linux and Windows performance were not measured.

## Provenance

Measured source HEAD: `e81c7b4a8bcb226f3aba3f6691abeeb7de5bee90` plus the
frozen implementation patch. Publication HEAD is recorded separately in JSON.
Exact candidate: `temp/mvp_tune_library_20261010/round6-tested/candidate.exe`.
Exact control: `temp/mvp_library/round4m/candidate.exe`.

The JSON embeds all four accepted captures, selected rows, source/output hashes
and text, frozen implementation sources and patch, runner sources, validation
logs, and the five-second jq activity samples. Executables and MIR dumps remain
in the local capture directories. The completion manifest verifies the original
report, merged summary, captures and completed patch. Source, binary and output
hashes were rechecked when recording this result.

| Capture | Workloads | Measured outputs | Discarded outputs |
| --- | ---: | ---: | ---: |
| round6-83 | 83 | 1071 | 249 |
| round6-confirm | 1 | 45 | 3 |
| round6-confirm-more | 18 | 810 | 54 |
| jq6-quiet | 4 | 8 | 0 |

Together these retain **1,934 measured outputs and 306 discarded-process outputs**.
Confirmation captures overlap the main screen and do not add to the 87-workload count.
Rejected, superseded and interrupted captures are listed separately in JSON.

Implementation and detailed limits:
[library tuning record](../../../vibe/impl/JS_MVP_Lmd_Library_Generators.md#61-corrected-release-evidence).
