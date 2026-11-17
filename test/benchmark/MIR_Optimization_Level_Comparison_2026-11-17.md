# MIR optimization level comparison — 2026-11-17

Release build, macOS arm64, AC power. Three measured pairs and one warmup per row; alternating order. `LAMBDA_EXEC_BACKEND=jit`, `LAMBDA_DISABLE_MIR_CACHE=1`, `LAMBDA_JS_LARGE_INTERP=0`. Times are medians in milliseconds. Wall time includes process startup, parsing, compilation, execution and teardown; self time is the script’s `__TIMING__` and its timing boundaries vary by workload. L2/L0 below 1 means L2 is faster.

Lambda commit `ae76dbf6de6917dd2a7b397bd6c211f8d118959b`; release binary SHA-256: L0 `6ceda78933153ecc9d424e8a9b2fce76f5b14bd507bf3c5fc8ff3940b458db3c`, L2 `91dd82d69a3758949946df5b3aeffd29174a910c0cd209f841406afc94b6fd50`. The build includes local Input API and DOM declaration repairs. The binaries and source inputs remained stable during the measurement. After the run, the Input API was moved to support the IME configuration and its allocation failure path was tightened; the final release build passed, but these measurements were not repeated on that later binary. Raw samples, output hashes and provenance: `temp/mir_opt_level_benchmark/native_results.json` (313 MB).

The runtime can cap requested L2 to L1 for large functions (5,000 MIR instructions for eager linking, 20,000 for native selection). Therefore the L2 column describes the requested L2 policy, not a guarantee that every function was compiled at L2. Some typed and untyped registry entries select the same source; paths are recorded in the raw artifact. No independent golden comparison was made; the runner checks equality of non-timing stdout between levels.

143/144 variant rows had three valid pairs with matching output. `r7rs/sumfp` typed emitted no valid timing in either warmup and is excluded. Three pairs are a limited sample; small changes can reflect noise.

- untyped, self: geometric mean L2/L0 = 0.835 across 72 rows with positive medians.
- untyped, wall: geometric mean L2/L0 = 1.203 across 72 rows with positive medians.
- typed, self: geometric mean L2/L0 = 0.814 across 71 rows with positive medians.
- typed, wall: geometric mean L2/L0 = 1.140 across 71 rows with positive medians.

| Suite / workload | Variant | L0 self | L2 self | Self ratio | L0 wall | L2 wall | Wall ratio |
|---|---|---:|---:|---:|---:|---:|---:|
| r7rs/fib | untyped | 0.832 | 0.722 | 0.868 | 10.102 | 10.802 | 1.069 |
| r7rs/fib | typed | 0.837 | 0.730 | 0.872 | 10.117 | 10.409 | 1.029 |
| r7rs/fibfp | untyped | 0.804 | 0.640 | 0.796 | 9.471 | 10.050 | 1.061 |
| r7rs/fibfp | typed | 0.819 | 0.623 | 0.761 | 9.704 | 10.270 | 1.058 |
| r7rs/tak | untyped | 0.094 | 0.084 | 0.894 | 9.531 | 10.478 | 1.099 |
| r7rs/tak | typed | 0.096 | 0.086 | 0.896 | 10.009 | 10.198 | 1.019 |
| r7rs/cpstak | untyped | 0.190 | 0.168 | 0.884 | 9.830 | 10.637 | 1.082 |
| r7rs/cpstak | typed | 0.186 | 0.167 | 0.898 | 10.076 | 11.207 | 1.112 |
| r7rs/sum | untyped | 0.116 | 0.101 | 0.871 | 8.933 | 9.369 | 1.049 |
| r7rs/sum | typed | 0.112 | 0.100 | 0.893 | 8.692 | 9.492 | 1.092 |
| r7rs/sumfp | untyped | 0.003 | 0.003 | 1.000 | 9.257 | 10.170 | 1.099 |
| r7rs/sumfp | typed | — | — | — | — | — | — |
| r7rs/nqueens | untyped | 1.096 | 1.004 | 0.916 | 28.534 | 57.454 | 2.014 |
| r7rs/nqueens | typed | 0.814 | 0.721 | 0.886 | 18.708 | 25.863 | 1.382 |
| r7rs/fft | untyped | 0.247 | 0.214 | 0.866 | 26.389 | 29.057 | 1.101 |
| r7rs/fft | typed | 0.083 | 0.048 | 0.578 | 15.232 | 21.261 | 1.396 |
| r7rs/mbrot | untyped | 0.651 | 0.488 | 0.750 | 16.628 | 25.049 | 1.506 |
| r7rs/mbrot | typed | 0.672 | 0.528 | 0.786 | 16.573 | 24.139 | 1.456 |
| r7rs/ack | untyped | 6.254 | 5.466 | 0.874 | 14.838 | 14.354 | 0.967 |
| r7rs/ack | typed | 6.265 | 5.576 | 0.890 | 14.815 | 14.461 | 0.976 |
| awfy/sieve | untyped | 0.022 | 0.018 | 0.818 | 9.552 | 10.957 | 1.147 |
| awfy/sieve | typed | 0.021 | 0.017 | 0.810 | 9.331 | 9.755 | 1.045 |
| awfy/permute | untyped | 0.218 | 0.152 | 0.697 | 26.915 | 55.481 | 2.061 |
| awfy/permute | typed | 0.102 | 0.080 | 0.784 | 12.793 | 17.952 | 1.403 |
| awfy/queens | untyped | 0.202 | 0.165 | 0.817 | 21.209 | 41.580 | 1.960 |
| awfy/queens | typed | 0.034 | 0.024 | 0.706 | 17.038 | 25.063 | 1.471 |
| awfy/towers | untyped | 0.435 | 0.332 | 0.763 | 29.999 | 61.298 | 2.043 |
| awfy/towers | typed | 0.232 | 0.181 | 0.780 | 20.954 | 31.908 | 1.523 |
| awfy/bounce | untyped | 0.075 | 0.062 | 0.827 | 28.478 | 32.588 | 1.144 |
| awfy/bounce | typed | 0.048 | 0.038 | 0.792 | 15.384 | 22.630 | 1.471 |
| awfy/list | untyped | 0.267 | 0.211 | 0.790 | 22.410 | 37.723 | 1.683 |
| awfy/list | typed | 0.107 | 0.093 | 0.869 | 14.199 | 19.543 | 1.376 |
| awfy/storage | untyped | 0.409 | 0.387 | 0.946 | 14.321 | 20.340 | 1.420 |
| awfy/storage | typed | 0.402 | 0.358 | 0.891 | 11.452 | 13.577 | 1.186 |
| awfy/mandelbrot | untyped | 45.167 | 31.022 | 0.687 | 55.028 | 41.891 | 0.761 |
| awfy/mandelbrot | typed | 45.034 | 31.017 | 0.689 | 55.130 | 42.479 | 0.771 |
| awfy/nbody | untyped | 4.810 | 2.538 | 0.528 | 86.898 | 97.229 | 1.119 |
| awfy/nbody | typed | 4.013 | 1.716 | 0.428 | 42.791 | 46.452 | 1.086 |
| awfy/richards | untyped | 284.561 | 258.896 | 0.910 | 348.946 | 387.416 | 1.110 |
| awfy/richards | typed | 123.728 | 99.220 | 0.802 | 180.140 | 202.125 | 1.122 |
| awfy/json | untyped | 3.699 | 3.197 | 0.864 | 65.053 | 111.071 | 1.707 |
| awfy/json | typed | 1.543 | 1.294 | 0.839 | 48.493 | 71.614 | 1.477 |
| awfy/deltablue | untyped | 48.303 | 39.581 | 0.819 | 238.557 | 260.783 | 1.093 |
| awfy/deltablue | typed | 23.432 | 18.748 | 0.800 | 165.134 | 277.301 | 1.679 |
| awfy/havlak | untyped | 5150.920 | 4932.150 | 0.958 | 5325.602 | 5134.569 | 0.964 |
| awfy/havlak | typed | 515.362 | 490.447 | 0.952 | 599.941 | 607.599 | 1.013 |
| awfy/cd | untyped | 371.699 | 346.343 | 0.932 | 564.490 | 571.902 | 1.013 |
| awfy/cd | typed | 178.998 | 173.738 | 0.971 | 251.342 | 255.855 | 1.018 |
| beng/binarytrees | untyped | 4.065 | 3.532 | 0.869 | 20.445 | 26.960 | 1.319 |
| beng/binarytrees | typed | 2.320 | 2.200 | 0.948 | 16.336 | 20.225 | 1.238 |
| beng/fannkuch | untyped | 0.425 | 0.366 | 0.861 | 20.761 | 45.681 | 2.200 |
| beng/fannkuch | typed | 0.355 | 0.295 | 0.831 | 13.083 | 18.649 | 1.425 |
| beng/fasta | untyped | 0.562 | 0.496 | 0.883 | 20.324 | 36.359 | 1.789 |
| beng/fasta | typed | 0.423 | 0.403 | 0.953 | 14.354 | 19.964 | 1.391 |
| beng/knucleotide | untyped | 0.483 | 0.429 | 0.888 | 35.652 | 77.448 | 2.172 |
| beng/knucleotide | typed | 0.319 | 0.304 | 0.953 | 18.991 | 28.908 | 1.522 |
| beng/pidigits | untyped | 0.348 | 0.351 | 1.009 | 12.927 | 17.615 | 1.363 |
| beng/pidigits | typed | 0.329 | 0.337 | 1.024 | 9.997 | 11.268 | 1.127 |
| beng/regexredux | untyped | 0.715 | 0.751 | 1.050 | 12.520 | 16.015 | 1.279 |
| beng/regexredux | typed | 0.690 | 0.725 | 1.051 | 11.076 | 11.746 | 1.061 |
| beng/revcomp | untyped | 0.624 | 0.607 | 0.973 | 18.549 | 28.293 | 1.525 |
| beng/revcomp | typed | 0.475 | 0.470 | 0.989 | 12.122 | 15.039 | 1.241 |
| beng/spectralnorm | untyped | 0.516 | 0.272 | 0.527 | 22.795 | 41.120 | 1.804 |
| beng/spectralnorm | typed | 0.488 | 0.236 | 0.484 | 16.450 | 22.662 | 1.378 |
| kostya/brainfuck | untyped | 98.035 | 86.947 | 0.887 | 115.475 | 118.218 | 1.024 |
| kostya/brainfuck | typed | 86.147 | 77.074 | 0.895 | 102.240 | 102.703 | 1.005 |
| kostya/matmul | untyped | 8.363 | 5.203 | 0.622 | 20.747 | 22.013 | 1.061 |
| kostya/matmul | typed | 8.512 | 5.277 | 0.620 | 21.599 | 23.303 | 1.079 |
| kostya/primes | untyped | 2.345 | 1.966 | 0.838 | 12.163 | 13.840 | 1.138 |
| kostya/primes | typed | 1.397 | 1.119 | 0.801 | 10.531 | 10.599 | 1.007 |
| kostya/base64 | untyped | 10.488 | 7.453 | 0.711 | 26.114 | 31.100 | 1.191 |
| kostya/base64 | typed | 12.996 | 9.657 | 0.743 | 29.102 | 32.732 | 1.125 |
| kostya/levenshtein | untyped | 4.942 | 4.275 | 0.865 | 32.197 | 35.167 | 1.092 |
| kostya/levenshtein | typed | 2.174 | 1.368 | 0.629 | 14.854 | 18.001 | 1.212 |
| kostya/json_gen | untyped | 4.379 | 4.311 | 0.984 | 17.023 | 19.649 | 1.154 |
| kostya/json_gen | typed | 1.596 | 1.520 | 0.952 | 12.207 | 13.299 | 1.089 |
| kostya/collatz | untyped | 201.830 | 161.031 | 0.798 | 213.226 | 174.473 | 0.818 |
| kostya/collatz | typed | 198.412 | 159.650 | 0.805 | 209.233 | 172.029 | 0.822 |
| larceny/triangl | untyped | 784.519 | 672.099 | 0.857 | 806.477 | 711.589 | 0.882 |
| larceny/triangl | typed | 508.906 | 402.786 | 0.791 | 528.067 | 435.214 | 0.824 |
| larceny/array1 | untyped | 0.217 | 0.123 | 0.567 | 10.266 | 12.714 | 1.239 |
| larceny/array1 | typed | 0.232 | 0.125 | 0.539 | 9.057 | 9.807 | 1.083 |
| larceny/deriv | untyped | 17.388 | 16.562 | 0.952 | 38.040 | 48.292 | 1.270 |
| larceny/deriv | typed | 5.797 | 5.339 | 0.921 | 20.650 | 25.194 | 1.220 |
| larceny/diviter | untyped | 359.167 | 229.367 | 0.639 | 368.972 | 239.571 | 0.649 |
| larceny/diviter | typed | 360.880 | 228.520 | 0.633 | 370.844 | 239.042 | 0.645 |
| larceny/divrec | untyped | 0.939 | 0.935 | 0.996 | 15.451 | 16.046 | 1.039 |
| larceny/divrec | typed | 0.936 | 0.941 | 1.005 | 15.367 | 15.864 | 1.032 |
| larceny/gcbench | untyped | 125.278 | 110.734 | 0.884 | 151.567 | 150.047 | 0.990 |
| larceny/gcbench | typed | 74.090 | 70.987 | 0.958 | 97.696 | 99.243 | 1.016 |
| larceny/paraffins | untyped | 0.287 | 0.257 | 0.895 | 64.202 | 72.725 | 1.133 |
| larceny/paraffins | typed | 0.123 | 0.096 | 0.780 | 31.412 | 51.815 | 1.650 |
| larceny/pnpoly | untyped | 3.839 | 2.070 | 0.539 | 14.872 | 15.080 | 1.014 |
| larceny/pnpoly | typed | 3.314 | 1.979 | 0.597 | 16.486 | 19.894 | 1.207 |
| larceny/puzzle | untyped | 4.689 | 4.398 | 0.938 | 16.985 | 18.633 | 1.097 |
| larceny/puzzle | typed | 4.807 | 4.383 | 0.912 | 17.138 | 19.070 | 1.113 |
| larceny/quicksort | untyped | 0.596 | 0.286 | 0.480 | 20.045 | 32.600 | 1.626 |
| larceny/quicksort | typed | 0.537 | 0.269 | 0.501 | 14.681 | 18.758 | 1.278 |
| larceny/ray | untyped | 0.275 | 0.134 | 0.487 | 13.637 | 15.732 | 1.154 |
| larceny/ray | typed | 0.275 | 0.134 | 0.487 | 13.417 | 15.665 | 1.168 |
| julia/parse_integers | untyped | 33.289 | 30.217 | 0.908 | 116.234 | 171.911 | 1.479 |
| julia/parse_integers | typed | 27.738 | 26.487 | 0.955 | 83.252 | 100.615 | 1.209 |
| julia/matrix_statistics | untyped | 35.807 | 17.816 | 0.498 | 125.269 | 150.548 | 1.202 |
| julia/matrix_statistics | typed | 27.466 | 12.646 | 0.460 | 86.577 | 76.894 | 0.888 |
| julia/iteration_pi_sum | untyped | 3.464 | 3.343 | 0.965 | 60.425 | 122.629 | 2.029 |
| julia/iteration_pi_sum | typed | 3.471 | 3.354 | 0.966 | 38.075 | 58.197 | 1.528 |
| julia/formatted_output | untyped | 71.429 | 67.951 | 0.951 | 205.396 | 261.401 | 1.273 |
| julia/formatted_output | typed | 45.566 | 44.362 | 0.974 | 128.566 | 145.787 | 1.134 |
| text/fast_diff | untyped | 47.740 | 43.758 | 0.917 | 74.741 | 73.736 | 0.987 |
| text/fast_diff | typed | 59.481 | 46.297 | 0.778 | 78.278 | 76.951 | 0.983 |
| text/microdiff | untyped | 32.334 | 32.420 | 1.003 | 71.498 | 108.669 | 1.520 |
| text/microdiff | typed | 31.847 | 31.500 | 0.989 | 57.250 | 72.348 | 1.264 |
| text/spelling | untyped | 542.897 | 537.599 | 0.990 | 646.244 | 715.215 | 1.107 |
| text/spelling | typed | 542.787 | 537.364 | 0.990 | 645.803 | 715.178 | 1.107 |
| text/hyphen | untyped | 42.774 | 40.616 | 0.950 | 87.275 | 127.687 | 1.463 |
| text/hyphen | typed | 8.998 | 7.510 | 0.835 | 49.957 | 58.132 | 1.164 |
| text/prettier_ast | untyped | 685.081 | 653.849 | 0.954 | 794.782 | 780.091 | 0.982 |
| text/prettier_ast | typed | 454.796 | 434.000 | 0.954 | 555.539 | 549.867 | 0.990 |
| text/text_search | untyped | 6123.290 | 4594.170 | 0.750 | 6154.037 | 4655.593 | 0.757 |
| text/text_search | typed | 2301.250 | 1689.960 | 0.734 | 2322.096 | 1719.634 | 0.741 |
| text/three_way_merge | untyped | 2368.690 | 2289.520 | 0.967 | 2391.270 | 2329.585 | 0.974 |
| text/three_way_merge | typed | 1786.610 | 1702.770 | 0.953 | 1803.672 | 1726.580 | 0.957 |
| text/log_pipeline | untyped | 4549.620 | 4421.070 | 0.972 | 4608.648 | 4519.623 | 0.981 |
| text/log_pipeline | typed | 830.067 | 594.465 | 0.716 | 865.504 | 644.612 | 0.745 |
| text/jq_mix | untyped | 2178.730 | 2132.890 | 0.979 | 2267.478 | 2267.598 | 1.000 |
| text/jq_mix | typed | 2038.440 | 2043.830 | 1.003 | 2120.297 | 2161.275 | 1.019 |
| text/jq_records | untyped | 304.002 | 302.695 | 0.996 | 371.995 | 434.603 | 1.168 |
| text/jq_records | typed | 342.546 | 340.063 | 0.993 | 399.968 | 439.787 | 1.100 |
| text/jq_bf | untyped | 152.707 | 142.078 | 0.930 | 199.993 | 230.112 | 1.151 |
| text/jq_bf | typed | 47.096 | 43.771 | 0.929 | 84.314 | 108.145 | 1.283 |
| text/jq_tree | untyped | 1578.080 | 1598.600 | 1.013 | 1646.071 | 1688.441 | 1.026 |
| text/jq_tree | typed | 1684.790 | 1740.630 | 1.033 | 1748.923 | 1824.716 | 1.043 |
| jetstream/cube3d | untyped | 16.129 | 15.245 | 0.945 | 280.321 | 311.436 | 1.111 |
| jetstream/cube3d | typed | 4.105 | 3.683 | 0.897 | 81.863 | 90.699 | 1.108 |
| jetstream/navier_stokes | untyped | 140.153 | 93.927 | 0.670 | 182.534 | 174.170 | 0.954 |
| jetstream/navier_stokes | typed | 162.073 | 128.816 | 0.795 | 206.745 | 219.089 | 1.060 |
| jetstream/splay | untyped | 259.918 | 248.653 | 0.957 | 327.869 | 355.391 | 1.084 |
| jetstream/splay | typed | 303.358 | 296.756 | 0.978 | 361.208 | 374.066 | 1.036 |
| jetstream/hashmap | untyped | 5653.880 | 5610.390 | 0.992 | 5700.025 | 5719.251 | 1.003 |
| jetstream/hashmap | typed | 5690.700 | 5645.910 | 0.992 | 5718.277 | 5690.831 | 0.995 |
| jetstream/crypto_sha1 | untyped | 29.228 | 26.685 | 0.913 | 67.044 | 69.510 | 1.037 |
| jetstream/crypto_sha1 | typed | 30.670 | 27.355 | 0.892 | 55.184 | 65.914 | 1.194 |
| jetstream/raytrace3d | untyped | 31.607 | 28.023 | 0.887 | 153.020 | 173.704 | 1.135 |
| jetstream/raytrace3d | typed | 8.198 | 6.406 | 0.781 | 77.204 | 145.346 | 1.883 |

## Memory and generated code size

Collected separately on the same pinned L0 and L2 binaries, with JIT forced, cache disabled and the large-source interpreter fallback disabled. Each level has one measurement run and a separate code-size run per variant. All 288 runs in each pass exited successfully and matched the original non-timing stdout. These single samples should be treated as indicative. Raw records: `temp/mir_opt_level_benchmark/additional_metrics.json`.

- **Compilation peak RSS** is the process resident-set high-water mark sampled at the compiler boundary, including process startup and earlier work. It is not the memory allocated only by the compiler. Any later JIT compilation is outside that boundary.
- **Whole-process peak RSS** is the maximum resident-set size from `/usr/bin/time -l` for the complete process. It includes compilation, execution and teardown. It is an upper bound on execution-phase peak RSS; the tools used here cannot distinguish the execution-phase peak from an earlier compilation peak.
- **Native code size** sums the MIR generator log’s published function lengths in bytes, including all recorded functions in that process. This is generated machine code, not the serialized MIR intermediate representation. MIR instruction counts are also shown for the pre-generation input; optimization occurs afterward. Generator debug logging is enabled only for the separate code-size run.

| Suite / workload | Variant | L0 compile MiB | L2 compile MiB | L0 process MiB | L2 process MiB | L0 native KiB | L2 native KiB | L0 MIR instructions | L2 MIR instructions |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| r7rs/fib | untyped | 15.39 | 15.61 | 16.88 | 17.05 | 4.53 | 2.92 | 647 | 647 |
| r7rs/fib | typed | 15.39 | 15.66 | 16.88 | 17.14 | 4.53 | 2.92 | 647 | 647 |
| r7rs/fibfp | untyped | 15.38 | 15.69 | 16.88 | 17.11 | 4.03 | 2.56 | 568 | 568 |
| r7rs/fibfp | typed | 15.27 | 15.48 | 16.81 | 16.92 | 3.72 | 2.34 | 532 | 532 |
| r7rs/tak | untyped | 15.44 | 15.78 | 16.89 | 17.22 | 4.44 | 3.28 | 631 | 631 |
| r7rs/tak | typed | 15.44 | 15.84 | 16.92 | 17.28 | 4.44 | 3.28 | 631 | 631 |
| r7rs/cpstak | untyped | 15.73 | 15.95 | 17.17 | 17.41 | 5.30 | 3.94 | 783 | 783 |
| r7rs/cpstak | typed | 15.64 | 15.94 | 17.09 | 17.33 | 5.28 | 3.92 | 781 | 781 |
| r7rs/sum | untyped | 15.14 | 15.33 | 16.67 | 16.77 | 3.20 | 1.61 | 439 | 439 |
| r7rs/sum | typed | 15.16 | 15.42 | 16.64 | 16.91 | 3.20 | 1.61 | 439 | 439 |
| r7rs/sumfp | untyped | 15.47 | 16.03 | 16.98 | 17.45 | 4.66 | 3.14 | 591 | 591 |
| r7rs/sumfp | typed | 15.11 | 15.34 | 16.59 | 16.83 | 2.67 | 1.86 | 352 | 352 |
| r7rs/nqueens | untyped | 24.81 | 33.25 | 28.39 | 36.83 | 54.59 | 36.34 | 6607 | 6607 |
| r7rs/nqueens | typed | 19.39 | 22.48 | 22.80 | 25.84 | 26.31 | 16.55 | 3151 | 3151 |
| r7rs/fft | untyped | 26.91 | 28.22 | 28.48 | 29.75 | 46.92 | 37.14 | 5793 | 5793 |
| r7rs/fft | typed | 19.50 | 22.62 | 20.97 | 24.09 | 19.11 | 11.61 | 2675 | 2675 |
| r7rs/mbrot | untyped | 18.56 | 20.56 | 20.16 | 22.02 | 29.19 | 18.94 | 3726 | 3726 |
| r7rs/mbrot | typed | 18.70 | 20.70 | 20.56 | 22.44 | 28.73 | 18.66 | 3687 | 3687 |
| r7rs/ack | untyped | 15.14 | 15.52 | 16.64 | 16.95 | 3.03 | 2.03 | 441 | 441 |
| r7rs/ack | typed | 15.16 | 15.50 | 16.64 | 16.92 | 3.03 | 2.03 | 441 | 441 |
| awfy/sieve | untyped | 15.77 | 16.61 | 17.28 | 18.02 | 6.80 | 4.50 | 850 | 850 |
| awfy/sieve | typed | 15.55 | 16.11 | 17.05 | 17.59 | 5.53 | 3.42 | 733 | 733 |
| awfy/permute | untyped | 24.23 | 31.88 | 25.59 | 33.20 | 64.28 | 43.09 | 7491 | 7491 |
| awfy/permute | typed | 17.91 | 20.81 | 19.41 | 22.17 | 18.05 | 11.95 | 2031 | 2031 |
| awfy/queens | untyped | 20.91 | 25.66 | 22.33 | 27.00 | 45.61 | 30.64 | 5548 | 5548 |
| awfy/queens | typed | 19.41 | 21.95 | 20.83 | 23.30 | 31.94 | 19.23 | 4298 | 4298 |
| awfy/towers | untyped | 23.58 | 29.80 | 24.97 | 31.17 | 78.42 | 52.83 | 8958 | 8958 |
| awfy/towers | typed | 20.17 | 23.52 | 21.59 | 24.88 | 46.16 | 25.08 | 5624 | 5624 |
| awfy/bounce | untyped | 29.97 | 31.16 | 31.34 | 32.53 | 55.61 | 44.44 | 6659 | 6659 |
| awfy/bounce | typed | 19.97 | 23.36 | 21.38 | 24.73 | 22.48 | 13.27 | 2988 | 2988 |
| awfy/list | untyped | 22.36 | 26.45 | 23.66 | 27.77 | 54.48 | 36.41 | 6094 | 6094 |
| awfy/list | typed | 18.19 | 20.28 | 19.61 | 21.62 | 23.44 | 14.41 | 2862 | 2862 |
| awfy/storage | untyped | 17.52 | 19.38 | 19.92 | 21.72 | 25.77 | 15.55 | 2897 | 2897 |
| awfy/storage | typed | 16.58 | 17.67 | 19.00 | 20.02 | 10.78 | 7.16 | 1266 | 1266 |
| awfy/mandelbrot | untyped | 15.88 | 16.56 | 17.34 | 17.95 | 5.12 | 3.41 | 694 | 694 |
| awfy/mandelbrot | typed | 16.12 | 16.98 | 17.61 | 18.36 | 6.41 | 4.53 | 825 | 825 |
| awfy/nbody | untyped | 52.28 | 54.59 | 53.61 | 55.94 | 167.27 | 132.69 | 19709 | 19709 |
| awfy/nbody | typed | 30.36 | 31.64 | 31.70 | 33.05 | 84.56 | 66.06 | 10906 | 10906 |
| awfy/richards | untyped | 31.81 | 41.33 | 33.70 | 43.20 | 213.02 | 143.69 | 198 | 198 |
| awfy/richards | typed | 31.41 | 40.12 | 33.27 | 41.97 | 171.66 | 113.92 | 169 | 169 |
| awfy/json | untyped | 31.05 | 36.78 | 33.50 | 39.23 | 196.97 | 131.23 | 21717 | 21717 |
| awfy/json | typed | 25.89 | 29.66 | 28.81 | 32.50 | 127.70 | 80.41 | 15779 | 15779 |
| awfy/deltablue | untyped | 61.77 | 63.17 | 67.78 | 69.20 | 603.33 | 477.41 | 66237 | 66237 |
| awfy/deltablue | typed | 43.72 | 58.30 | 51.36 | 65.91 | 409.14 | 264.69 | 48459 | 48459 |
| awfy/havlak | untyped | 59.66 | 61.45 | 211.98 | 149.11 | 498.67 | 395.62 | 56905 | 56905 |
| awfy/havlak | typed | 27.53 | 35.22 | 131.95 | 139.02 | 112.83 | 72.16 | 13298 | 13298 |
| awfy/cd | untyped | 95.39 | 99.00 | 105.70 | 109.30 | 501.45 | 397.83 | 58443 | 58443 |
| awfy/cd | typed | 39.83 | 41.70 | 50.08 | 51.95 | 171.09 | 135.27 | 20917 | 20917 |
| beng/binarytrees | untyped | 19.52 | 22.31 | 30.44 | 32.97 | 28.66 | 19.73 | 3161 | 3161 |
| beng/binarytrees | typed | 18.30 | 20.09 | 28.16 | 29.80 | 17.80 | 11.62 | 2115 | 2115 |
| beng/fannkuch | untyped | 23.81 | 33.30 | 25.22 | 34.67 | 30.88 | 22.78 | 3673 | 3673 |
| beng/fannkuch | typed | 18.41 | 21.52 | 19.84 | 22.86 | 13.42 | 9.14 | 1585 | 1585 |
| beng/fasta | untyped | 20.61 | 24.75 | 22.14 | 26.30 | 41.19 | 27.31 | 4627 | 4627 |
| beng/fasta | typed | 17.95 | 20.09 | 19.61 | 21.61 | 19.23 | 13.03 | 2239 | 2239 |
| beng/knucleotide | untyped | 27.73 | 37.78 | 29.45 | 39.53 | 87.14 | 58.14 | 10373 | 10373 |
| beng/knucleotide | typed | 20.45 | 24.78 | 22.19 | 26.45 | 30.48 | 20.75 | 3700 | 3700 |
| beng/pidigits | untyped | 18.41 | 20.78 | 20.16 | 22.50 | 16.03 | 10.50 | 1820 | 1820 |
| beng/pidigits | typed | 16.44 | 17.28 | 18.28 | 19.02 | 6.95 | 3.98 | 744 | 744 |
| beng/regexredux | untyped | 18.56 | 20.78 | 20.67 | 22.66 | 14.78 | 9.12 | 1522 | 1522 |
| beng/regexredux | typed | 16.78 | 17.78 | 18.97 | 19.97 | 8.16 | 4.61 | 817 | 817 |
| beng/revcomp | untyped | 20.30 | 23.08 | 22.30 | 24.94 | 37.86 | 24.14 | 4234 | 4234 |
| beng/revcomp | typed | 17.23 | 18.61 | 19.53 | 20.81 | 13.28 | 8.19 | 1559 | 1559 |
| beng/spectralnorm | untyped | 22.03 | 26.56 | 23.64 | 28.14 | 48.30 | 32.11 | 6050 | 6050 |
| beng/spectralnorm | typed | 18.72 | 20.73 | 20.27 | 22.19 | 24.06 | 14.42 | 3172 | 3172 |
| kostya/brainfuck | untyped | 20.83 | 25.36 | 25.48 | 29.97 | 29.09 | 19.16 | 3399 | 3399 |
| kostya/brainfuck | typed | 19.91 | 23.66 | 24.55 | 28.28 | 24.41 | 16.30 | 2929 | 2929 |
| kostya/matmul | untyped | 17.73 | 19.36 | 20.17 | 21.64 | 14.09 | 9.27 | 1882 | 1882 |
| kostya/matmul | typed | 17.81 | 19.50 | 20.20 | 21.78 | 15.62 | 10.38 | 2075 | 2075 |
| kostya/primes | untyped | 16.06 | 16.92 | 18.62 | 19.36 | 6.52 | 4.03 | 831 | 831 |
| kostya/primes | typed | 15.64 | 16.14 | 18.11 | 18.58 | 4.33 | 2.62 | 579 | 579 |
| kostya/base64 | untyped | 19.06 | 22.23 | 24.81 | 27.89 | 29.48 | 18.94 | 3340 | 3340 |
| kostya/base64 | typed | 20.06 | 23.97 | 25.80 | 29.61 | 27.58 | 17.52 | 3140 | 3140 |
| kostya/levenshtein | untyped | 27.84 | 28.98 | 32.52 | 33.66 | 55.92 | 44.84 | 6699 | 6699 |
| kostya/levenshtein | typed | 17.70 | 19.39 | 22.42 | 23.97 | 13.80 | 9.05 | 1767 | 1767 |
| kostya/json_gen | untyped | 17.19 | 18.36 | 25.61 | 26.64 | 14.12 | 8.80 | 1580 | 1580 |
| kostya/json_gen | typed | 16.23 | 17.16 | 20.03 | 20.84 | 10.05 | 6.16 | 1124 | 1124 |
| kostya/collatz | untyped | 16.27 | 17.36 | 17.75 | 18.78 | 10.06 | 6.36 | 1419 | 1419 |
| kostya/collatz | typed | 16.03 | 16.70 | 17.48 | 18.11 | 8.34 | 5.19 | 1197 | 1197 |
| larceny/triangl | untyped | 24.30 | 31.36 | 25.75 | 32.80 | 35.75 | 20.88 | 4202 | 4202 |
| larceny/triangl | typed | 22.16 | 28.11 | 23.59 | 29.44 | 29.84 | 17.97 | 3426 | 3426 |
| larceny/array1 | untyped | 16.20 | 17.44 | 17.80 | 18.97 | 9.86 | 5.91 | 1282 | 1282 |
| larceny/array1 | typed | 15.66 | 16.02 | 17.25 | 17.55 | 4.05 | 2.55 | 514 | 514 |
| larceny/deriv | untyped | 20.16 | 22.98 | 37.31 | 39.91 | 47.53 | 33.33 | 5059 | 5059 |
| larceny/deriv | typed | 18.16 | 19.72 | 35.55 | 36.97 | 25.81 | 16.36 | 2982 | 2982 |
| larceny/diviter | untyped | 15.34 | 15.69 | 16.83 | 17.14 | 3.91 | 2.39 | 649 | 649 |
| larceny/diviter | typed | 15.44 | 15.72 | 16.94 | 17.17 | 3.91 | 2.39 | 649 | 649 |
| larceny/divrec | untyped | 15.38 | 15.70 | 16.86 | 17.16 | 4.08 | 2.80 | 590 | 590 |
| larceny/divrec | typed | 15.44 | 15.61 | 16.89 | 17.06 | 4.08 | 2.80 | 590 | 590 |
| larceny/gcbench | untyped | 20.11 | 23.39 | 94.38 | 116.67 | 37.05 | 25.78 | 4034 | 4034 |
| larceny/gcbench | typed | 19.08 | 21.62 | 101.56 | 103.86 | 23.11 | 15.12 | 2788 | 2788 |
| larceny/paraffins | untyped | 46.91 | 48.88 | 48.69 | 50.67 | 154.38 | 126.00 | 18604 | 18604 |
| larceny/paraffins | typed | 26.81 | 34.58 | 28.28 | 35.97 | 61.92 | 46.09 | 8103 | 8103 |
| larceny/pnpoly | untyped | 16.62 | 17.78 | 18.11 | 19.20 | 10.28 | 5.33 | 1500 | 1500 |
| larceny/pnpoly | typed | 18.25 | 20.84 | 19.70 | 22.20 | 14.95 | 8.61 | 2177 | 2177 |
| larceny/puzzle | untyped | 16.47 | 17.64 | 29.48 | 30.58 | 8.72 | 6.03 | 1058 | 1058 |
| larceny/puzzle | typed | 16.77 | 18.14 | 29.83 | 31.11 | 10.38 | 6.73 | 1230 | 1230 |
| larceny/quicksort | untyped | 20.75 | 24.58 | 22.19 | 25.97 | 40.56 | 26.94 | 5136 | 5136 |
| larceny/quicksort | typed | 17.83 | 19.72 | 19.38 | 21.11 | 20.58 | 13.20 | 2726 | 2726 |
| larceny/ray | untyped | 18.16 | 19.64 | 19.53 | 20.97 | 12.64 | 6.92 | 2193 | 2193 |
| larceny/ray | typed | 18.17 | 19.64 | 19.59 | 20.95 | 12.75 | 6.98 | 2193 | 2193 |
| julia/parse_integers | untyped | 31.88 | 43.25 | 55.39 | 66.64 | 146.28 | 98.38 | 91 | 91 |
| julia/parse_integers | typed | 22.67 | 26.66 | 46.30 | 49.88 | 74.39 | 48.06 | 91 | 91 |
| julia/matrix_statistics | untyped | 31.77 | 43.20 | 36.69 | 48.11 | 146.28 | 98.38 | 91 | 91 |
| julia/matrix_statistics | typed | 22.69 | 26.44 | 27.48 | 31.17 | 74.39 | 48.06 | 91 | 91 |
| julia/iteration_pi_sum | untyped | 31.89 | 42.64 | 33.52 | 44.25 | 146.28 | 98.38 | 91 | 91 |
| julia/iteration_pi_sum | typed | 22.75 | 26.64 | 24.23 | 28.09 | 74.39 | 48.06 | 91 | 91 |
| julia/formatted_output | untyped | 31.83 | 43.22 | 80.70 | 91.75 | 146.28 | 98.38 | 91 | 91 |
| julia/formatted_output | typed | 22.70 | 26.52 | 61.05 | 64.34 | 74.39 | 48.06 | 91 | 91 |
| text/fast_diff | untyped | 28.97 | 30.03 | 33.47 | 34.52 | 50.00 | 42.33 | 6103 | 6103 |
| text/fast_diff | typed | 21.42 | 26.53 | 25.95 | 31.03 | 32.39 | 23.45 | 3930 | 3930 |
| text/microdiff | untyped | 25.64 | 32.25 | 51.28 | 57.44 | 107.62 | 73.78 | 11781 | 11781 |
| text/microdiff | typed | 21.41 | 24.52 | 39.09 | 42.06 | 62.08 | 42.06 | 6813 | 6813 |
| text/spelling | untyped | 28.47 | 34.36 | 60.41 | 59.05 | 301.16 | 204.08 | 105 | 105 |
| text/spelling | typed | 28.45 | 34.16 | 53.45 | 58.84 | 301.16 | 204.08 | 105 | 105 |
| text/hyphen | untyped | 26.59 | 35.34 | 35.28 | 42.34 | 96.53 | 65.92 | 105 | 105 |
| text/hyphen | typed | 23.22 | 26.92 | 31.05 | 34.23 | 64.25 | 43.47 | 105 | 105 |
| text/prettier_ast | untyped | 48.58 | 50.16 | 93.23 | 94.81 | 318.23 | 260.08 | 34499 | 34499 |
| text/prettier_ast | typed | 46.41 | 48.17 | 74.95 | 76.80 | 260.89 | 210.12 | 27590 | 27590 |
| text/text_search | untyped | 23.08 | 27.91 | 28.62 | 33.41 | 70.95 | 48.94 | 8269 | 8269 |
| text/text_search | typed | 19.33 | 21.78 | 24.80 | 27.09 | 32.89 | 22.28 | 3849 | 3849 |
| text/three_way_merge | untyped | 20.81 | 24.98 | 37.47 | 40.98 | 45.66 | 31.34 | 5168 | 5168 |
| text/three_way_merge | typed | 18.53 | 20.42 | 35.17 | 36.97 | 27.14 | 17.45 | 3260 | 3260 |
| text/log_pipeline | untyped | 28.52 | 40.03 | 89.80 | 101.17 | 90.97 | 60.72 | 9617 | 9617 |
| text/log_pipeline | typed | 22.19 | 27.38 | 38.53 | 43.52 | 35.08 | 22.42 | 4460 | 4460 |
| text/jq_mix | untyped | 28.72 | 35.62 | 384.58 | 377.95 | 170.75 | 115.62 | 10876 | 10876 |
| text/jq_mix | typed | 26.62 | 32.33 | 380.27 | 384.94 | 139.66 | 95.44 | 8426 | 8426 |
| text/jq_records | untyped | 28.81 | 37.17 | 55.97 | 62.75 | 188.22 | 129.81 | 12956 | 12956 |
| text/jq_records | typed | 26.27 | 31.83 | 52.77 | 57.61 | 149.61 | 103.94 | 9565 | 9565 |
| text/jq_bf | untyped | 25.55 | 31.61 | 32.86 | 38.94 | 147.50 | 101.62 | 8331 | 8331 |
| text/jq_bf | typed | 24.06 | 28.47 | 30.86 | 35.27 | 106.72 | 74.06 | 5119 | 5119 |
| text/jq_tree | untyped | 22.09 | 26.33 | 702.42 | 704.58 | 79.39 | 54.45 | 891 | 891 |
| text/jq_tree | typed | 22.27 | 26.23 | 686.36 | 692.80 | 67.95 | 47.45 | 703 | 703 |
| jetstream/cube3d | untyped | 257.98 | 266.91 | 257.98 | 266.91 | 388.03 | 312.81 | 46578 | 46578 |
| jetstream/cube3d | typed | 57.16 | 60.55 | 74.39 | 77.80 | 162.41 | 130.69 | 20601 | 20601 |
| jetstream/navier_stokes | untyped | 27.06 | 34.50 | 29.23 | 36.66 | 99.48 | 60.83 | 13332 | 13332 |
| jetstream/navier_stokes | typed | 27.28 | 35.03 | 29.45 | 37.17 | 108.53 | 67.23 | 14483 | 14483 |
| jetstream/splay | untyped | 28.02 | 35.25 | 206.64 | 212.36 | 144.61 | 96.34 | 16141 | 16141 |
| jetstream/splay | typed | 24.58 | 28.92 | 197.53 | 201.56 | 89.59 | 57.03 | 10894 | 10894 |
| jetstream/hashmap | untyped | 29.16 | 40.97 | 97.84 | 109.69 | 123.36 | 83.77 | 13821 | 13821 |
| jetstream/hashmap | typed | 22.67 | 26.66 | 91.25 | 95.23 | 66.00 | 43.50 | 7684 | 7684 |
| jetstream/crypto_sha1 | untyped | 33.75 | 35.55 | 36.66 | 38.44 | 90.28 | 73.89 | 9833 | 9833 |
| jetstream/crypto_sha1 | typed | 22.95 | 29.03 | 25.81 | 31.84 | 46.92 | 32.89 | 5600 | 5600 |
| jetstream/raytrace3d | untyped | 50.98 | 52.62 | 65.52 | 67.19 | 360.69 | 289.48 | 42187 | 42187 |
| jetstream/raytrace3d | typed | 34.69 | 48.44 | 46.81 | 60.56 | 204.62 | 129.58 | 24680 | 24680 |

For untyped workloads, geometric mean L2/L0 ratios are 1.149 for compiler-boundary RSS, 1.099 for process peak RSS, and 0.681 for generated native bytes (72 rows).

For typed workloads, geometric mean L2/L0 ratios are 1.129 for compiler-boundary RSS, 1.091 for process peak RSS, and 0.652 for generated native bytes (72 rows).
