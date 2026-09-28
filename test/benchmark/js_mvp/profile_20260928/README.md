# JS MVP versus full LambdaJS — release profile, 2026-09-28

Validated **62/63** canonical workloads with five alternating pairs per validated row.
On those 62 comparable rows, full JS / MVP execution geometric mean: **1.692×**; ratio of summed medians: **1.580×**.
Full JS wins **14** rows; MVP wins **48**.
This is not a passing 63-row acceptance score. The wall-time geometric ratio is **1.921×**.

Navier–Stokes fails only in MVP: its frame-15 check writes `this.result` in a
non-strict plain function, but MVP supplies an undefined receiver. Full JS and
Node both pass the checksum and density digest. A minimal reproducer and the
exact error are retained in [semantic_probes.json](semantic_probes.json).
The [tuning proposal](../../../../vibe/impl/JS_MVP_Release_Profile_Comparison_20260928.md)
connects the measurements to current MIR and runtime code.

## Method

- Both backends use the same archived O3/LTO/NDEBUG executable from `make build-release-profile`.
- `JS_EXECUTION_BACKEND=mir`, `JS_OPT_TRACE=0`; five isolated process pairs alternate full/MVP and MVP/full. One prior execution of each backend validates output.
- Node v22.13.0 is an output oracle, not a timed comparison column. All measured outputs must match its SHA-256 after removing only timing lines and trailing whitespace.
- Canonical source/input hashes are retained. JetStream uses one identical wrapper preserving strict mode on all engines; Navier includes the post-timing frame-15 checksum and density digest.
- Execution medians use source-defined timers; wall medians include startup, parsing, compilation, cleanup, and any post-timing validation. Wall results use explicit MIR, not the shipped AUTO tier.
- Counters are disabled during measurement, but the full engine retains dormant profiling hooks. These are profile-build results, not ordinary-release performance or an ablated optimization speedup.
- Five pairs are diagnostic evidence, especially for sub-millisecond rows. No source or input edits were used to improve performance. MVP is a restricted semantic implementation; matching these workload outputs does not certify full ECMAScript parity.
- Measurements were serial, with no concurrent build/test work or explicit suite cooldown. No sampled CPU-time attribution is claimed.

Binary SHA-256: `dffd67fc5439f64b42fcc530b66ce8cc998d4df111c7bd097f78e0474d5702de`.
Base commit: `276879abfdfa100005a2009ac244df8fd01747c0`; the build patch is in the raw metadata.
Binary archive: `temp/js-mvp-profile-20260928/lambda-profile.exe`.
Raw [pairs, manifests, oracles and provenance](comparison.json); [computed aggregates](summary.json); [separate counters and MIR summaries](diagnostics.json).
The exact measured runner sources are archived beside the binary as
`run_js_mvp_comparison.measured.py` and `run_paired_benchmarks.measured.py`.
The current comparison runner additionally withholds ratios and continues the
matrix if a timed sample fails after successful preflight; this failure path was
checked separately and does not alter the accepted measurements above.

## Validation

- `make build-release-profile`: passed. Both MVP CLI selector spellings run;
  the ordinary release host rejects MVP as intended.
- `make test-lambda-baseline`: **5,996/5,996**, including MVP **51/51**.
- `make test262-baseline`: **40,261/40,261**, zero regressions, no retries.
- MVP isolation audit, 63-workload Tune14 manifest audit, paired CLI check,
  failure-continuation check, and source/binary/artifact checks passed.
- All 12 separate counter/MIR runs exited zero and matched Node output.
- `make test-premake-generator` fails before assertions on this macOS host:
  `could not determine Linux multiarch triplet from gcc`. The same error was
  reproduced with the unmodified HEAD generator. Generated main-host makefiles
  contain all seven MVP objects in `release_profile` and none in `release`.

See [baseline exit statuses and logs](validation.json) and
[build and harness checks](build_checks.json). The production optimizer was
not changed; Navier's MVP failure remains explicitly recorded above.

## Suite results

| Suite | Rows | Full / MVP geomean | Full wins | Full / MVP sum |
|---|---:|---:|---:|---:|
| r7rs | 10 | 2.348× | 0 | 2.520× |
| awfy | 14 | 2.446× | 0 | 2.148× |
| beng | 8 | 2.049× | 2 | 1.725× |
| kostya | 7 | 0.895× | 4 | 0.389× |
| larceny | 11 | 0.868× | 7 | 1.471× |
| text | 7 | 1.800× | 1 | 1.773× |
| jetstream | 5 | 2.236× | 0 | 2.060× |

## All workloads

Ratios above 1 mean MVP is faster. Times are milliseconds. Failed rows have no accepted ratio.

| Workload | Full JS | MVP | Full / MVP | Status |
|---|---:|---:|---:|---|
| r7rs/fib | 1.958 | 1.132 | 1.729× | validated |
| r7rs/fibfp | 2.006 | 1.116 | 1.798× | validated |
| r7rs/tak | 0.355 | 0.131 | 2.713× | validated |
| r7rs/cpstak | 0.706 | 0.262 | 2.695× | validated |
| r7rs/sum | 0.668 | 0.645 | 1.036× | validated |
| r7rs/sumfp | 0.067 | 0.064 | 1.055× | validated |
| r7rs/nqueens | 28.770 | 9.433 | 3.050× | validated |
| r7rs/fft | 4.004 | 0.976 | 4.103× | validated |
| r7rs/mbrot | 12.527 | 0.911 | 13.751× | validated |
| r7rs/ack | 12.632 | 10.604 | 1.191× | validated |
| awfy/sieve | 0.227 | 0.180 | 1.262× | validated |
| awfy/permute | 6.065 | 2.669 | 2.272× | validated |
| awfy/queens | 3.470 | 1.391 | 2.494× | validated |
| awfy/towers | 9.737 | 3.800 | 2.562× | validated |
| awfy/bounce | 4.213 | 2.296 | 1.835× | validated |
| awfy/list | 2.276 | 0.980 | 2.322× | validated |
| awfy/storage | 4.306 | 1.854 | 2.323× | validated |
| awfy/mandelbrot | 555.779 | 31.515 | 17.635× | validated |
| awfy/nbody | 678.989 | 404.450 | 1.679× | validated |
| awfy/richards | 936.141 | 560.149 | 1.671× | validated |
| awfy/json | 44.203 | 17.222 | 2.567× | validated |
| awfy/deltablue | 399.837 | 162.854 | 2.455× | validated |
| awfy/havlak | 13650.036 | 6534.590 | 2.089× | validated |
| awfy/cd | 4130.178 | 1782.946 | 2.316× | validated |
| beng/binarytrees | 21.095 | 50.040 | 0.422× | validated |
| beng/fannkuch | 21.627 | 8.671 | 2.494× | validated |
| beng/fasta | 25.391 | 7.349 | 3.455× | validated |
| beng/knucleotide | 20.885 | 9.750 | 2.142× | validated |
| beng/pidigits | 0.523 | 0.807 | 0.648× | validated |
| beng/regexredux | 8.541 | 0.610 | 14.002× | validated |
| beng/revcomp | 22.914 | 18.648 | 1.229× | validated |
| beng/spectralnorm | 85.850 | 24.019 | 3.574× | validated |
| kostya/brainfuck | 1898.367 | 1349.302 | 1.407× | validated |
| kostya/matmul | 283.379 | 456.486 | 0.621× | validated |
| kostya/primes | 60.927 | 77.770 | 0.783× | validated |
| kostya/base64 | 480.068 | 214.907 | 2.234× | validated |
| kostya/levenshtein | 87.699 | 47.919 | 1.830× | validated |
| kostya/json_gen | 27.867 | 29.418 | 0.947× | validated |
| kostya/collatz | 1598.276 | 9230.676 | 0.173× | validated |
| larceny/triangl | 6223.108 | 2940.461 | 2.116× | validated |
| larceny/array1 | 19.158 | 47.713 | 0.402× | validated |
| larceny/deriv | 87.304 | 101.133 | 0.863× | validated |
| larceny/diviter | 615.228 | 600.484 | 1.025× | validated |
| larceny/divrec | 18.533 | 5.955 | 3.112× | validated |
| larceny/gcbench | 526.563 | 1299.960 | 0.405× | validated |
| larceny/paraffins | 2.279 | 2.346 | 0.971× | validated |
| larceny/pnpoly | 126.735 | 144.838 | 0.875× | validated |
| larceny/puzzle | 33.534 | 33.117 | 1.013× | validated |
| larceny/quicksort | 20.456 | 28.287 | 0.723× | validated |
| larceny/ray | 5.819 | 16.273 | 0.358× | validated |
| text/fast_diff | 2063.153 | 900.191 | 2.292× | validated |
| text/microdiff | 823.613 | 150.994 | 5.455× | validated |
| text/hyphen | 106.813 | 80.665 | 1.324× | validated |
| text/prettier_ast | 2703.830 | 3414.614 | 0.792× | validated |
| text/text_search | 43950.905 | 19328.751 | 2.274× | validated |
| text/three_way_merge | 9905.909 | 5688.243 | 1.741× | validated |
| text/log_pipeline | 14091.013 | 11968.733 | 1.177× | validated |
| jetstream/cube3d | 347.795 | 94.983 | 3.662× | validated |
| jetstream/navier_stokes | — | — | — | preflight_failure |
| jetstream/splay | 346.954 | 126.651 | 2.739× | validated |
| jetstream/hashmap | 1302.337 | 677.374 | 1.923× | validated |
| jetstream/crypto_sha1 | 320.953 | 248.563 | 1.291× | validated |
| jetstream/raytrace3d | 557.821 | 248.511 | 2.245× | validated |

## Reproduce

```sh
make build-release-profile
python3 test/benchmark/run_js_mvp_comparison.py \
  --binary ./lambda-profile.exe --pairs 5 --timeout 120 \
  --output temp/js_mvp_comparison.json
```
