# JS Tune12 — Final closeout

**Date:** 2026-09-16  
**Status:** implementation complete; full-runtime performance milestone unmet.

Tune12 keeps the full JavaScript semantic path as the only fallback for every
unproved fast-path candidate (**S1.11**, **D2.4.1–D2.4.3**, **D5.3.2–D5.3.5**,
**D6.2.2v2**, **D8.4.1v2–D8.4.3v2**). This record distinguishes completed
implementation from the separate performance target.

## Provenance

| Item | Identity |
|---|---|
| Control source | `fd99875910e47cd5647d3510fecfba8bbf393520` |
| Control binary | `lambda-tune12-control-fd998759`, SHA-256 `4f158287b8f342ef703b946a47507d18dcfedf89748eaaf50cc9c9bc30aef972` |
| Final runtime patch at archival | SHA-256 `1864e219659ad69a82ab20cd33b5e9b98e14ed348710ffef45de23b1b0e29562` |
| Final binary | `lambda-tune12-final-1864e219659a`, SHA-256 `02593fe504c5030a04e536e2afe738da62cb9c9d9253b258e8a23db55e9201c6` |
| Release identity check | archived binary, `lambda.exe`, and the standard-runner cache have the same SHA-256 |
| Platform/toolchain | Darwin arm64; Apple clang 17.0.0; release build |
| Manifest | 63 workloads verified by `verify_js_mvp_manifest.py` |

## Correctness and structure

- Focused Tune12 JS fixtures: 7/7 normal and 7/7 forced-GC with poisoning.
- Focused MIR fixtures: 3/3; full JS MIR emission: 31/31; JS optimization
  contracts: 27/27.
- Exact-root gate: 188/188 forced-GC corpus cases; `check_gc_effects.py`
  reports 68 `NO_GC` imports and 135 project call-graph nodes.
- Test262: 40,261/40,261 baseline entries, zero regressions.
- The broad Lambda baseline has 18 failures, all reproduced at the detached
  control: 16 existing MIR-ratchet overages plus `slice_two_arg` and
  `beng_knucleotide2`. No unrelated tests or ratchet budgets were changed.
  The clean control also reproduces the same 16/19 ratchet result, including
  `js_numeric_inference_call`; therefore **D8.6.1** permits no Tune12 budget
  adjustment.

`JsCollectionEntry` now identifies one stable ordered node; no set/add/delete
path scans the order chain. The direct Map diagnostic at 8K keys records
control update/reinsert 132/259 ms and candidate 4/7 ms. Its normalized-output
7-pair A/B is 0.3333x, 7/7 candidate wins. See
`collection_order_diagnostic.js` and
`collection_order_diagnostic_paired.json`.

## Complete time evidence

The frozen [control snapshot](control.json) and its [report](control.md) both
passed the guarded Test262 workflow and contain three samples for every one of
the 63 LambdaJS/QuickJS rows. Its standalone LambdaJS/QuickJS geometric mean
is **5.082332x** (213,965.447 / 42,927.825 ms total medians). The candidate
[standard snapshot](final.json) likewise contains three samples for every
LambdaJS/QuickJS row, and [its report](final.md) prints every row. The
[interleaved A/B](final_paired.json) contains three alternating control and
candidate samples per row; all 63 rows are `ok` and all 189 normalized outputs
match. The separate snapshots establish full-population coverage; the
interleaved artifact is the causal comparison.

| Suite | Rows | Candidate/control geo | Control median sum (ms) | Candidate median sum (ms) |
|---|---:|---:|---:|---:|
| R7RS | 10 | 0.247932x | 233.659 | 103.869 |
| AWFY | 14 | 0.768558x | 27,195.476 | 26,681.448 |
| BENG | 8 | 0.679903x | 435.570 | 295.168 |
| Kostya | 7 | 0.887465x | 12,076.177 | 8,042.668 |
| Larceny | 11 | 0.869977x | 23,832.273 | 21,088.948 |
| JetStream | 6 | 0.852658x | 3,722.369 | 3,364.632 |
| Text | 7 | 0.973316x | 145,394.383 | 135,205.681 |
| **All rows** | **63** | **0.680706x** | **212,889.908** | **194,782.414** |

The exact intended targets improved with equal output: `fib` 0.1262x, `sum`
0.0249x, `sumfp` 0.0244x, `knucleotide` 0.1400x, and `regexredux` 0.3018x.
The candidate won 145 of 189 pairs. These phase effects are not summed because
the final binary contains overlapping changes.

The fresh 63-row LambdaJS/QuickJS geometric mean is **3.481106x**; total
medians are 194,662.839 ms and 42,663.925 ms, respectively. The proposed
0.80x milestone is therefore **unmet**. The standard time runner reports one
missing typed-Lambda cell (`text/log_pipeline`, `exit_1`); this does not affect
the complete LambdaJS/QuickJS population.

Reproducible final/control regressions above the 3% review threshold remain
visible in the A/B artifact: `nqueens` 1.1664x, `cpstak` 1.1389x, `base64`
1.1264x, `levenshtein` 1.1096x, `pidigits` 1.1090x, `json_gen` 1.0615x,
`splay` 1.0529x, `quicksort` 1.0372x, and `paraffins` 1.0316x. They are output
equivalent and do not block implementation correctness, but they leave the
separate tuning-success/performance acceptance open.

## Memory and compilation

The [RSS snapshot](final_memory.json) records all 57 non-JetStream rows the
memory runner supports: LambdaJS average/median/min/max are 158 MiB / 45.9 MiB
/ 33.9 MiB / 2.52 GiB; QuickJS is 3.19 MiB / 2.22 MiB / 2.08 MiB / 24.3 MiB.
JetStream wrappers intentionally have no memory-mode command and are recorded
as unavailable rather than fabricated. The restricted shell prevents macOS
`/usr/bin/time -l` from querying `kern.clockrate`; the accepted artifact was
captured with the same command outside that shell and contains the real RSS.

The final timing workflow reused the Test262-verified archived release so it
did not produce a comparable clean compiler-wall-time sample. The Test262
release build itself completed successfully with no compiler errors; the
current release binary and archive remain byte-identical. MIR-size evidence is
the clean-control comparison above rather than a speculative ratchet budget
change.

## Outcome

All T12-1 through T12-8 implementation and fallback contracts are complete.
T12-9 closeout is complete with full time, A/B, correctness, GC, Test262, and
available-memory evidence. The code change is complete; the proposed
full-runtime performance milestone remains a follow-up because 3.481106x is
above 0.80x and the listed A/B regressions require separate work.
