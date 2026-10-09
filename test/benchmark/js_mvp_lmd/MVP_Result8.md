# JS MVP Benchmark Results: MVP_Result8

**Closures, class specialization, CD allocation and regression fixes** — [MVP §§22–28](../../../vibe/jube/JS_MVP_Lmd.md#22-closures-callbacks-and-array-growth).

[Series index and comparison](README.md) · [Raw JSON](MVP_Result8.json) · [Previous phase](MVP_Result7.md)

## Summary

- **60 workloads pass** their output checks: 43 standard benchmarks and 17 microbenchmarks. Six workloads are added since Result7: Bounce, Storage, NBody, Richards, CD and SHA-1.
- Longer confirmations show **25.9% less deletion time**, **25.8% less Bounce time** against its older control, and **12.8% less CD execution time**. Richards takes **2.2% less time** in the matrix. Map lookup is within measurement uncertainty.
- No execution slowdown is statistically confirmed across the 60 MVP workloads or three supplementary Lambda checks. Small regressions remain possible within the intervals.
- Recorded from existing accepted captures; publication did not rerun benchmarks or implementation tests.

## Measurement and limits

- Matrix captured **2026-10-09 12:59:30–13:08:37 UTC+08**. Separate confirmations span **12:59:09–13:15:27**; shared Lambda checks finish at **13:15:47**.
- Platform: `macOS-26.5.2-arm64-arm-64bit-Mach-O`; release binaries, pinned native MIR. Each matrix cell is the median of **15 fresh processes**, with one discarded process per lane and an identical-control peer. Candidate/control/peer order cycles through all six permutations. Node version: **v22.13.0**.
- Values are **self-reported workload milliseconds**. Initial parsing/JIT compilation and startup are excluded; Node tiering during the workload is included. Process wall times remain separate in JSON.
- The primary control is the archived pre-regression release, which already has class/closure support. **It is not the Result7 binary.** Bounce's historical confirmation uses a separately identified older control.
- Intervals are **two-sided 95% paired-bootstrap intervals for MVP/control medians**, with 10,000 resamples. Control-peer intervals and raw samples are retained. An interval spanning one is inconclusive.
- **Host contention limits precision.** Another Test262 run was active when the final matrix began. The earlier severe build-overlap run was stopped and excluded. CD's matrix interval is particularly wide; these are qualified paired measurements, not quiet-host absolute latency claims.

```text
JS_EXEC_BACKEND=mir
JS_EXECUTION_BACKEND=mir
JS_MIR_INTERP=0
LAMBDA_JS_LARGE_INTERP=0
LAMBDA_EXEC_BACKEND=jit
LAMBDA_TIER=jit
```

## Six newly enabled workloads

Fresh references exist only for these six rows. “Untyped Lambda” denotes the
canonical ports, retaining any existing explicit numeric annotations. Typed
references were measured only for CD and Richards. No historical reference
cells are substituted for missing measurements.

| Workload | MVP ms | Untyped Lambda ms | Node ms | MVP / Lambda | MVP / Node |
| --- | ---: | ---: | ---: | ---: | ---: |
| awfy/cd | 203.627 | 775.543 | 43.430 | 0.26× | 4.69× |
| awfy/richards | 75.843 | 312.867 | 8.756 | 0.24× | 8.66× |
| awfy/nbody | 46.571 | 7.634 | 5.543 | 6.10× | 8.40× |
| awfy/bounce | 0.619 | 0.072 | 0.748 | 8.60× | 0.83× |
| awfy/storage | 0.522 | 0.611 | 0.696 | 0.85× | 0.75× |
| jetstream/crypto_sha1 | 15.774 | 58.714 | 11.177 | 0.27× | 1.41× |

| Workload | MVP ms | Typed Lambda ms | MVP / typed Lambda |
| --- | ---: | ---: | ---: |
| awfy/cd | 203.627 | 222.751 | 0.914× |
| awfy/richards | 75.843 | 81.015 | 0.936× |

MVP and Node run the same adapted JavaScript workload body. Both include class
setup and verification; Lambda clocks its existing kernel interval and uses
its native data structures. These compare language ports and do not isolate
typing or implementation overhead (**S1.11**). The observed CD/Node ratio is
**4.69×**, and Richards/Node is **8.66×**, subject to the host limits above.

## Complete 60-workload matrix

All cells below retain the original 15-round matrix. Longer confirmations do
not replace them. `Control / MVP` above one means faster MVP.

### Standard workloads (43)

| Workload | Control ms | MVP ms | Control / MVP | MVP / control 95% interval |
| --- | ---: | ---: | ---: | ---: |
| awfy/bounce | 0.840 | 0.619 | 1.36× | 0.7278–0.7485 |
| awfy/cd | 233.665 | 203.627 | 1.15× | 0.5728–1.0891 |
| awfy/list | 0.276 | 0.283 | 0.98× | 1.0000–1.0403 |
| awfy/mandelbrot | 32.746 | 32.772 | 1.00× | 0.9920–1.0103 |
| awfy/nbody | 46.603 | 46.571 | 1.00× | 0.9480–1.0629 |
| awfy/permute | 0.629 | 0.629 | 1.00× | 0.9848–1.0398 |
| awfy/queens | 0.346 | 0.345 | 1.00× | 0.9799–1.0117 |
| awfy/richards | 77.559 | 75.843 | 1.02× | 0.9478–0.9962 |
| awfy/sieve | 0.130 | 0.128 | 1.02× | 0.9685–1.0157 |
| awfy/storage | 0.523 | 0.522 | 1.00× | 0.9470–1.0515 |
| awfy/towers | 1.156 | 1.148 | 1.01× | 0.9778–1.0302 |
| beng/binarytrees | 4.192 | 4.144 | 1.01× | 0.9660–1.0139 |
| beng/fannkuch | 0.312 | 0.312 | 1.00× | 0.9560–1.0533 |
| beng/spectralnorm | 1.207 | 1.229 | 0.98× | 0.9110–1.0456 |
| jetstream/crypto_sha1 | 15.704 | 15.774 | 1.00× | 0.9769–1.0099 |
| kostya/base64 | 13.700 | 13.691 | 1.00× | 0.9847–1.0109 |
| kostya/brainfuck | 46.889 | 46.709 | 1.00× | 0.9760–1.0162 |
| kostya/collatz | 203.709 | 203.495 | 1.00× | 0.9980–1.0075 |
| kostya/json_gen | 6.532 | 6.505 | 1.00× | 0.9705–1.0207 |
| kostya/levenshtein | 1.665 | 1.624 | 1.03× | 0.9569–1.0210 |
| kostya/matmul | 6.957 | 6.922 | 1.01× | 0.9658–1.0013 |
| kostya/primes | 2.398 | 2.367 | 1.01× | 0.9789–1.0313 |
| larceny/array1 | 0.388 | 0.386 | 1.01× | 0.9415–1.0156 |
| larceny/deriv | 9.287 | 9.183 | 1.01× | 0.9496–1.0151 |
| larceny/diviter | 272.336 | 272.968 | 1.00× | 0.9903–1.0127 |
| larceny/divrec | 0.629 | 0.628 | 1.00× | 0.9843–1.0127 |
| larceny/gcbench | 100.766 | 99.043 | 1.02× | 0.9520–1.0154 |
| larceny/paraffins | 0.104 | 0.104 | 1.00× | 0.9811–1.0097 |
| larceny/pnpoly | 8.488 | 8.463 | 1.00× | 0.9558–1.0786 |
| larceny/puzzle | 2.818 | 2.810 | 1.00× | 0.9908–1.0092 |
| larceny/quicksort | 1.465 | 1.467 | 1.00× | 0.9646–1.0374 |
| larceny/ray | 0.267 | 0.269 | 0.99× | 1.0000–1.0187 |
| larceny/triangl | 166.150 | 164.662 | 1.01× | 0.9799–1.0075 |
| r7rs/ack | 10.721 | 10.675 | 1.00× | 0.9753–1.0141 |
| r7rs/cpstak | 0.288 | 0.290 | 0.99× | 0.9829–1.0394 |
| r7rs/fft | 0.189 | 0.195 | 0.97× | 0.9894–1.0430 |
| r7rs/fib | 1.680 | 1.690 | 0.99× | 0.9827–1.0326 |
| r7rs/fibfp | 1.748 | 1.733 | 1.01× | 0.9464–1.0459 |
| r7rs/mbrot | 0.543 | 0.540 | 1.01× | 0.9848–1.0018 |
| r7rs/nqueens | 1.023 | 1.028 | 1.00× | 0.9790–1.0176 |
| r7rs/sum | 0.319 | 0.319 | 1.00× | 1.0000–1.0458 |
| r7rs/sumfp | 0.032 | 0.032 | 1.00× | 0.9412–1.0312 |
| r7rs/tak | 0.146 | 0.146 | 1.00× | 0.9861–1.0355 |

### Microbenchmarks (17)

| Workload | Control ms | MVP ms | Control / MVP | MVP / control 95% interval |
| --- | ---: | ---: | ---: | ---: |
| js_micro/args_ctl | 3.564 | 3.568 | 1.00× | 0.9997–1.0965 |
| js_micro/args_fp | 3.566 | 3.613 | 0.99× | 0.9735–1.0502 |
| js_micro/lit | 0.153 | 0.152 | 1.01× | 0.9935–1.0000 |
| js_micro/named | 4.964 | 5.162 | 0.96× | 0.9883–1.0576 |
| js_mvp_lmd/calls | 5.125 | 5.151 | 0.99× | 0.9871–1.0180 |
| js_mvp_lmd/dense_array | 58.661 | 58.646 | 1.00× | 0.9968–1.0049 |
| js_mvp_lmd/integer_dense | 3.053 | 3.054 | 1.00× | 0.9719–1.0295 |
| js_mvp_lmd/integer_indirect | 1.723 | 1.734 | 0.99× | 0.9621–1.0435 |
| js_mvp_lmd/map_iteration | 1.472 | 1.454 | 1.01× | 0.9504–1.0104 |
| js_mvp_lmd/map_lookup | 8.600 | 8.762 | 0.98× | 0.9807–1.0316 |
| js_mvp_lmd/numeric | 80.282 | 76.121 | 1.05× | 0.9339–1.0066 |
| js_mvp_lmd/object_delete | 1.248 | 0.939 | 1.33× | 0.7270–0.7690 |
| js_mvp_lmd/object_fields | 0.498 | 0.497 | 1.00× | 0.9980–1.0020 |
| js_mvp_lmd/object_growth | 3.687 | 3.753 | 0.98× | 0.9731–1.0469 |
| js_mvp_lmd/object_retype | 0.335 | 0.335 | 1.00× | 0.9571–1.0448 |
| js_mvp_lmd/object_retype_escaped | 5.980 | 5.978 | 1.00× | 0.9697–1.0108 |
| js_mvp_lmd/strings | 0.109 | 0.106 | 1.03× | 0.9545–1.0094 |

## Separate longer confirmations

These compare the same final candidate to its archived controls; they retain
their own raw samples. Bounce uses the older historical control. Other rows
use the matrix's primary control. None adds a new workload to the coverage count.

| Workload | Rounds | Control ms | MVP ms | Time change | MVP / control 95% interval |
| --- | ---: | ---: | ---: | ---: | ---: |
| js_mvp_lmd/object_fields | 90 | 0.3780 | 0.3775 | -0.13% | 0.9792–1.0133 |
| js_mvp_lmd/object_delete | 90 | 1.1340 | 0.8405 | -25.88% | 0.7366–0.7489 |
| js_mvp_lmd/map_lookup | 90 | 7.7625 | 7.7875 | +0.32% | 0.9959–1.0116 |
| js_mvp_lmd/map_iteration | 90 | 1.3320 | 1.3165 | -1.16% | 0.9728–1.0060 |
| awfy/cd | 30 | 204.1630 | 178.1025 | -12.76% | 0.8264–0.8925 |
| awfy/bounce | 30 | 0.8295 | 0.6155 | -25.80% | 0.7368–0.7458 |

The same 30-round CD confirmation has **3,345.540 ms control versus 3,313.980 ms
MVP total-process time**, with a ratio interval of **0.9698–1.0411**. Its
execution gain is confirmed; a small startup regression remains unresolved.
There is no Node lane in this confirmation, so its 178.1025 ms execution median
must not be divided by the earlier matrix's Node median as a matched comparison.

## Supplementary shared Lambda checks

These three 30-round checks run Lambda scripts against both releases. They
check the shared runtime and are **not additional MVP workloads**.

| Lambda workload | Control ms | Candidate ms | Candidate / control 95% interval |
| --- | ---: | ---: | ---: |
| r7rs/fib | 1.5810 | 1.5760 | 0.9887–1.0057 |
| larceny/gcbench | 116.3475 | 115.6965 | 0.9876–1.0032 |
| kostya/json_gen | 23.9915 | 23.9075 | 0.9856–1.0064 |

## Validation and provenance

- **4,440 measured and 221 discarded-process outputs match**, across the matrix, longer confirmations and shared Lambda checks.
- **67/67 MVP tests pass normally and with forced GC** (`LAMBDA_GC_FORCE_EVERY=1`, `LAMBDA_GC_POISON_FREED=1`).
- **6,487/6,487 Lambda/input checks pass on a full rerun**: 2,112 input and 4,375 runtime checks. The initial attempt passed 6,486/6,487 and timed out on the LaTeX corpus. The rerun's corpus takes about 58 seconds against a 60-second limit; that narrow margin remains unresolved. Aggregate evidence is `lambda-gate-final.log`; the similarly named JSON covers the 1,266-test Lambda runtime executable only.
- **40,261/40,261 Test262 cases pass**, zero retries or unstable cases; 2,652 excluded cases. This validates the shared runtime/full LambdaJS, not the bounded MVP's conformance coverage.
- The release restored after validation matches the measured candidate, and all frozen implementation source hashes match. Diagnostic counters and code-size/frame audits are retained separately from performance timing (**D5.4.4**).

| Binary | SHA-256 |
| --- | ---: |
| Final MVP | `5f0ce0ed8d41059e76a65ac9f972f95c0daed9d42ccedb1b419c0c779d10ce16` |
| Primary control | `2756f62f430544be114769d8ec133af30d58befc5818e3d7279079a8dafe8f40` |
| Historical Bounce control | `f85933d635f3cdcb60f69b2fc9717dd8195bd50133fd944b3e6ba406ed03c96f` |

Source HEAD: `024f2a12c73218d80b2831e906c3b57de98b55af` plus the implementation patch embedded in
the JSON. The local frozen binary is `temp/mvp_regressions_20261009/accepted/candidate.exe`.
The eighth implementation candidate is published as **MVP_Result8**.

The raw JSON retains all seven accepted captures, their exact samples,
normalized 60-row results, source and binary hashes, source text and patch,
commands, runner sources, MIR hashes, discarded-process stdout, validation
logs and the initial timeout record. Exact binaries, MIR dumps and remaining
per-process logs stay under `temp/mvp_regressions_20261009/accepted/`.

| Capture | Workloads | Rounds | Measured / discarded outputs |
| --- | ---: | ---: | ---: |
| new6 | 6 | 15 | 480 / 32 |
| cohort48 | 48 | 15 | 2160 / 144 |
| class6 | 6 | 15 | 270 / 18 |
| map-confirm-final | 4 | 90 | 1080 / 12 |
| cd-cold-confirm | 1 | 30 | 90 / 3 |
| bounce-historical | 1 | 30 | 90 / 3 |
| shared-lambda | 3 | 30 | 270 / 9 |

The interrupted build-overlap matrix, superseded seventh candidate and earlier
Map confirmation are excluded from these tables. Their locations and status
remain recorded in JSON. This result incorporates the latest runtime/compiler
work; sequential differences from Result7 do not establish causal gains.

Implementation and earlier experiments:
[regression closure record](../../../vibe/impl/JS_MVP_Lmd_Regression_Closure.md).
