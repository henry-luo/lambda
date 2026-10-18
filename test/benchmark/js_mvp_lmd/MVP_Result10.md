# JS MVP Benchmark Results: MVP_Result10

**Activation stack-pool tuning benchmark record** — direct comparison against the pre-tuning release; the candidate is not compared to the Result9 binary.

[Series index and comparison](README.md) · [Raw JSON](MVP_Result10.json) · [MVP phase 31](../../../vibe/jube/JS_MVP_Lmd.md#31-prototypes-accessors-proxies-and-sparse-arrays)

## Summary

- **88/88 output contracts pass**: 71 standard workloads and 17 microbenchmarks, adding `jetstream/hashmap` since Result9.
- The 83 non-jq control comparisons use 12 seven-pair screens and 71 three-pair captures. Four jq rows use the latest existing paired/single evidence; no new jq run was made.
- Three repeated rows have a 95% paired-bootstrap lower bound above 1 against both control copies: `awfy/towers`, `js_micro/args_ctl`, `r7rs/ack`.
- `jetstream/hashmap` passes its oracle; its MVP median is 501.152 ms versus Node at 15.177 ms (33.02× slower). This new row has no pre-tuning MVP timing.

## Measurement and limits

Captures ran on macOS-26.5.2-arm64-arm-64bit-Mach-O. The repeated captures span **2026-10-18 14:34–15:51 UTC+08**; the jq rows carry the latest existing capture from the same date.
All benchmark execution used release binaries and pinned native MIR:

```text
JS_EXEC_BACKEND=mir
JS_EXECUTION_BACKEND=mir
JS_MIR_INTERP=0
LAMBDA_JS_LARGE_INTERP=0
LAMBDA_EXEC_BACKEND=jit
LAMBDA_TIER=jit
```

Times are workload self-reported milliseconds. Initial MIR compilation and process startup are excluded from the table; each JSON sample retains process wall time separately. Node tiering during the workload is included.

Candidate SHA-256: `37ea2c546b1022adae83f20f0286564606cad2f21a2aa53bc26c5a70717dda5a`. Control SHA-256: `c741e731bd15674a7ef07c485325aa85adcb7c6515028699346627a648db8cd0`. The control is the pre-activation-pool-tuning binary, not the Result9 candidate. The recorded source manifests differ only at `lambda/runtime/activation.cpp`.

Repeated rows show the two-sided 95% paired-bootstrap interval for candidate/control median ratio (10,000 resamples). Candidate/control-peer intervals are retained in JSON and both are used for the slowdown screen. An interval entirely above 1 means slower MVP. The jq rows have two pairs for records and one for each other workload, without intervals.

### Standard workloads (71)

| Workload | Control ms | MVP ms | MVP / control | 95% interval | Pairs |
| --- | ---: | ---: | ---: | ---: | ---: |
| awfy/bounce | 0.601 | 0.609 | 1.0133 | 1.0000–1.0352 | 7 |
| awfy/cd | 155.368 | 155.473 | 1.0007 | 0.9945–1.0127 | 7 |
| awfy/deltablue | 54.759 | 54.841 | 1.0015 | 0.9660–1.0250 | 3 |
| awfy/havlak | 1837.868 | 1826.305 | 0.9937 | 0.9880–1.0043 | 7 |
| awfy/json | 5.778 | 5.731 | 0.9919 | 0.9399–1.0306 | 3 |
| awfy/list | 0.232 | 0.234 | 1.0086 | 0.9590–1.0431 | 3 |
| awfy/mandelbrot | 32.168 | 31.939 | 0.9929 | 0.9897–0.9993 | 3 |
| awfy/nbody | 42.761 | 43.285 | 1.0123 | 0.9890–1.0183 | 3 |
| awfy/permute | 0.517 | 0.513 | 0.9923 | 0.9903–1.0039 | 3 |
| awfy/queens | 0.314 | 0.308 | 0.9809 | 0.9565–1.0000 | 3 |
| awfy/richards | 71.503 | 70.993 | 0.9929 | 0.9869–1.0042 | 7 |
| awfy/sieve | 0.087 | 0.087 | 1.0000 | 0.9770–1.0568 | 3 |
| awfy/storage | 0.496 | 0.500 | 1.0081 | 0.8799–1.0524 | 3 |
| awfy/towers | 1.006 | 1.019 | 1.0129 | 1.0020–1.0271 | 3 |
| beng/binarytrees | 3.776 | 3.785 | 1.0024 | 0.9807–1.0160 | 7 |
| beng/fannkuch | 0.243 | 0.248 | 1.0206 | 0.9918–1.0782 | 3 |
| beng/fasta | 0.877 | 0.877 | 1.0007 | 0.9591–1.0519 | 3 |
| beng/knucleotide | 3.619 | 3.742 | 1.0339 | 0.9940–1.0548 | 3 |
| beng/pidigits | 0.337 | 0.338 | 1.0048 | 0.9751–1.0361 | 3 |
| beng/regexredux | 2.899 | 2.938 | 1.0135 | 0.9926–1.0219 | 7 |
| beng/revcomp | 4.137 | 4.144 | 1.0017 | 0.9792–1.0457 | 3 |
| beng/spectralnorm | 0.805 | 1.156 | 1.4360 | 0.8577–1.6657 | 3 |
| jetstream/crypto_sha1 | 12.729 | 12.860 | 1.0103 | 0.9808–1.0124 | 3 |
| jetstream/cube3d | 66.210 | 66.143 | 0.9990 | 0.9816–1.0031 | 3 |
| jetstream/hashmap | — | 501.152 | — | — | 3 vs Node |
| jetstream/navier_stokes | 34.779 | 34.710 | 0.9980 | 0.9925–1.0051 | 7 |
| jetstream/raytrace3d | 148.916 | 147.623 | 0.9913 | 0.8992–1.0075 | 3 |
| jetstream/splay | 62.093 | 62.634 | 1.0087 | 0.9948–1.0678 | 7 |
| julia/formatted_output | 104.825 | 104.241 | 0.9944 | 0.9941–1.0060 | 3 |
| julia/iteration_pi_sum | 23.120 | 23.126 | 1.0003 | 0.9987–1.0145 | 3 |
| julia/matrix_statistics | 95.210 | 95.235 | 1.0003 | 0.9990–1.0018 | 3 |
| julia/parse_integers | 48.786 | 48.942 | 1.0032 | 0.9960–1.0065 | 3 |
| kostya/base64 | 11.416 | 11.465 | 1.0043 | 0.9969–1.0139 | 3 |
| kostya/brainfuck | 42.281 | 42.169 | 0.9974 | 0.9642–1.0224 | 3 |
| kostya/collatz | 194.458 | 195.253 | 1.0041 | 0.9987–1.0063 | 3 |
| kostya/json_gen | 4.505 | 4.463 | 0.9907 | 0.9803–1.0186 | 7 |
| kostya/levenshtein | 1.488 | 1.488 | 1.0000 | 0.9382–1.0674 | 3 |
| kostya/matmul | 5.469 | 5.335 | 0.9755 | 0.9395–0.9924 | 3 |
| kostya/primes | 2.114 | 2.144 | 1.0142 | 0.9868–1.0734 | 3 |
| larceny/array1 | 0.278 | 0.277 | 0.9964 | 0.9964–1.0320 | 3 |
| larceny/deriv | 8.862 | 8.854 | 0.9991 | 0.9820–1.0024 | 3 |
| larceny/diviter | 250.602 | 250.546 | 0.9998 | 0.9989–1.0055 | 3 |
| larceny/divrec | 4.765 | 4.758 | 0.9985 | 0.9985–1.0017 | 3 |
| larceny/gcbench | 91.204 | 90.579 | 0.9931 | 0.9613–1.0226 | 7 |
| larceny/paraffins | 0.103 | 0.100 | 0.9709 | 0.9009–1.0099 | 3 |
| larceny/pnpoly | 5.883 | 5.898 | 1.0025 | 0.9762–1.0043 | 3 |
| larceny/puzzle | 2.349 | 2.298 | 0.9783 | 0.9655–1.0039 | 3 |
| larceny/quicksort | 1.196 | 1.222 | 1.0217 | 0.9984–1.0234 | 3 |
| larceny/ray | 0.217 | 0.216 | 0.9954 | 0.9954–1.7143 | 3 |
| larceny/triangl | 123.684 | 123.905 | 1.0018 | 0.9991–1.0046 | 3 |
| r7rs/ack | 6.485 | 7.480 | 1.1534 | 1.0066–1.1539 | 3 |
| r7rs/cpstak | 0.176 | 0.174 | 0.9886 | 0.9412–1.0116 | 3 |
| r7rs/fft | 0.155 | 0.154 | 0.9935 | 0.9686–1.0000 | 3 |
| r7rs/fib | 0.871 | 0.881 | 1.0115 | 0.9989–1.0830 | 3 |
| r7rs/fibfp | 0.885 | 0.887 | 1.0023 | 0.9509–1.0242 | 3 |
| r7rs/mbrot | 0.477 | 0.478 | 1.0021 | 1.0000–1.0063 | 3 |
| r7rs/nqueens | 0.915 | 0.923 | 1.0087 | 0.9830–1.0506 | 3 |
| r7rs/sum | 0.270 | 0.270 | 1.0000 | 1.0000–1.0000 | 3 |
| r7rs/sumfp | 0.026 | 0.026 | 1.0000 | 0.9630–1.0385 | 3 |
| r7rs/tak | 0.091 | 0.091 | 1.0000 | 0.9891–1.0333 | 3 |
| text/fast_diff | 203.033 | 204.028 | 1.0049 | 0.9930–1.0082 | 3 |
| text/hyphen | 52.331 | 53.503 | 1.0224 | 1.0097–1.0426 | 3 |
| text/jq_bf † | 349192.069 | 266095.602 | 0.7620 | — | 1 |
| text/jq_mix † | 316025.783 | 302689.286 | 0.9578 | — | 1 |
| text/jq_records † | 83688.128 | 56541.593 | 0.6756 | — | 2 |
| text/jq_tree † | 493568.869 | 416324.131 | 0.8435 | — | 1 |
| text/log_pipeline | 4635.899 | 4433.837 | 0.9564 | 0.9487–0.9864 | 3 |
| text/microdiff | 121.479 | 122.663 | 1.0097 | 1.0008–1.0187 | 3 |
| text/prettier_ast | 517.049 | 512.581 | 0.9914 | 0.9831–1.0033 | 3 |
| text/text_search | 4519.766 | 4531.155 | 1.0025 | 0.9977–1.0066 | 7 |
| text/three_way_merge | 1582.876 | 1685.031 | 1.0645 | 1.0457–1.1167 | 3 |

### Microbenchmarks (17)

| Workload | Control ms | MVP ms | MVP / control | 95% interval | Pairs |
| --- | ---: | ---: | ---: | ---: | ---: |
| js_micro/args_ctl | 2.658 | 2.707 | 1.0184 | 1.0026–1.0403 | 3 |
| js_micro/args_fp | 2.670 | 2.665 | 0.9981 | 0.9981–1.0000 | 3 |
| js_micro/lit | 0.133 | 0.132 | 0.9925 | 0.9925–1.0000 | 3 |
| js_micro/named | 3.954 | 3.948 | 0.9985 | 0.9965–1.0023 | 3 |
| js_mvp_lmd/calls | 3.059 | 3.034 | 0.9918 | 0.9823–1.0040 | 3 |
| js_mvp_lmd/dense_array | 57.513 | 58.049 | 1.0093 | 0.9964–1.0151 | 3 |
| js_mvp_lmd/integer_dense | 2.451 | 2.441 | 0.9959 | 0.9878–1.0130 | 3 |
| js_mvp_lmd/integer_indirect | 1.435 | 1.436 | 1.0007 | 0.9931–1.0007 | 3 |
| js_mvp_lmd/map_iteration | 1.205 | 1.230 | 1.0207 | 1.0106–1.0207 | 3 |
| js_mvp_lmd/map_lookup | 6.431 | 6.366 | 0.9899 | 0.9566–1.0380 | 7 |
| js_mvp_lmd/numeric | 71.026 | 71.175 | 1.0021 | 0.9966–1.0021 | 3 |
| js_mvp_lmd/object_delete | 0.805 | 0.817 | 1.0149 | 0.9925–1.0261 | 3 |
| js_mvp_lmd/object_fields | 0.370 | 0.370 | 1.0000 | 1.0000–1.0027 | 3 |
| js_mvp_lmd/object_growth | 2.218 | 2.151 | 0.9698 | 0.9498–1.0061 | 3 |
| js_mvp_lmd/object_retype | 0.299 | 0.296 | 0.9900 | 0.9900–1.0067 | 3 |
| js_mvp_lmd/object_retype_escaped | 5.046 | 5.206 | 1.0317 | 0.9806–1.1312 | 3 |
| js_mvp_lmd/strings | 0.099 | 0.118 | 1.1919 | 0.9720–1.2020 | 3 |

## Validation and limits

Every row is marked `ok` by its capture contract. The hashmap candidate and Node outputs have matching hashes; jq candidate/control output lines match their expected checksums. Repeated non-jq captures also retain output hashes for every measured sample.

The confirmed slowdown screen uses both frozen copies of the same control binary. `awfy/towers` and `js_micro/args_ctl` are small changes; `r7rs/ack` is about 15.3% slower by the median. This result records those signals for follow-up and does not claim broad performance acceptance.

The full raw sample summaries, workload/source hashes, MIR dump hashes, process wall times and capture artifact hashes are in JSON. The exact executables, detailed output logs and MIR dumps remain under their local `temp/` capture paths.
