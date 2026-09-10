# Result42 versus Result37: typed Lambda regression analysis

Analysis against repository HEAD `842577e5e`, using the original Result42
Lambda archive, not its later five-row LambdaJS repair. No runtime or benchmark
source was changed. Diagnostic copies and raw evidence are under
`temp/result42_analysis/`.

## Findings

The regression is real, but the report headline overstates the comparable
change. Three things are mixed together:

1. Four additional text workloads raise the current-suite geometric mean.
2. Two existing text ports now execute substantially more work.
3. The compiler/runtime lost cheap typed-array admission and added repeated
   array and integer checks to otherwise unchanged workloads.

The two principal implementation changes are `794847ec83` (proper typed-array
support) and `58bbb6eeee` (Tune22 integer comparison lowering). Their relevant
mechanisms remain in Result42 after subsequent tuning. This attribution uses
source history, archived-binary MIR, unchanged-source interleaved replay, and
an instrumented phase probe. It is not a complete rebuild-and-bisect of every
intervening commit, nor an allocation of every row's regression to one commit.

## Comparison contract and population

| Property | Result37 | Result42 |
|---|---|---|
| Snapshot date | 2026-09-07 | 2026-09-10 |
| Recorded Lambda commit | `4758dc716580375581d605af4d56b67aaea8b0be` | `584748bc54e209e220ca4a73cd1f29fcaf67523f` |
| Archive | `test/benchmark/exe/lambda-v37-4758dc7165` | `test/benchmark/exe/lambda-v42-584748bc54` |
| Node | v22.13.0 | v22.13.0 |
| QuickJS | 2025-09-13 | 2025-09-13 |
| Platform recorded | Darwin arm64 | Darwin arm64 |
| Samples / timeout | 3 / 180 s | 3 / 180 s |
| Population | 59 | 63 |
| Test262 pre-gate | 40,261 / 40,261 | 40,261 / 40,261 |

Both archives byte-match their JSON SHA-256 and lack the debug-build banner.
The replay ran on one host, AC power, `lowpowermode 0`, with
`LAMBDA_TIER=jit`. Self-reported execution time is the comparison metric;
startup and compilation are separate. The historical metadata records only
Darwin/arm64, not an exact hardware model, so it alone cannot establish host
identity. The archived-binary replay removes that ambiguity for its rows.

Result42's later merge refreshes only five LambdaJS rows; its typed Lambda
cells still come from the original archive above. Result39/40 use Node
v24.7.0 and different Python/QuickJS versions, so their Node ratios should not
be spliced into the Result37/42 control comparison. Their archives also were
not available in this workspace for replay. Tune22's implementation record
explicitly identifies Result40 as its completion measurement despite the
earlier recorded commit label.

The headline typed/Node ratio goes from **0.78x to 1.27x**, but its populations
differ. Recalculation from the JSON gives:

| Population | Rows | Typed R42/R37 | Untyped R42/R37 | C2MIR R42/R37 | Node R42/R37 | QuickJS R42/R37 |
|---|---:|---:|---:|---:|---:|---:|
| Common benchmark names | 59 | 1.427x | 1.431x | 1.150x | 0.996x | 1.004x |
| Exclude rewritten microdiff/hyphen | 57 | **1.222x** | 1.238x | 1.008x | 0.994x | 0.996x |
| Identical typed entry-source bytes | 43 | **1.341x** | — | 1.009x | 0.994x | 0.995x |

These are geometric means of per-row time ratios, not ratios of total runtime.
The 57-row group still includes annotation/implementation rewrites of other
ports. The 43-row group compares the selected typed entry script (or its
untyped fallback) byte-for-byte between the recorded source commits; it is a
stricter source filter, not an independent full dependency audit. The 12
replayed rows below all have unchanged source and no imports.

On the 59 shared names, typed/Node is **0.781x -> 1.119x**, not 1.27x.
Removing the two changed text workloads gives **0.859x -> 1.055x** on both
snapshots' same 57 rows. Source-rewritten graph workloads include substantial
wins, which is why the unchanged-source group regresses more than the 57-row
group. This is not exclusively a typed regression: untyped Lambda also slows.

### Text-port discontinuities

Commit `a4c2f78c8` (matching benchmark test scripts) changes the work:

| Typed workload | R37 ms | R42 ms | Apparent ratio | Source change |
|---|---:|---:|---:|---|
| microdiff | 0.140 | 56.741 | 405.3x | Precomputed path/type-length scoring becomes recursive snapshot diffing and construction of change records |
| hyphen | 2.118 | 74.282 | 35.1x | Vowel-based insertion over six texts becomes shared Liang-pattern/trie hyphenation over 13 cases |

The native C ports change too: microdiff becomes 146.5x slower and hyphen
16.4x slower. These old/new cells are not engine-regression measurements.
The additional rows are prettier_ast, text_search, three_way_merge, and
log_pipeline. See [text README](../../test/benchmark/text/README.md).

## Unchanged-source reproduction

Seven alternating A/B pairs per row, using the exact archived releases and
the same current script. All 84 pairs had matching normalized stdout and
successful timing samples. Runs were serial, with no concurrent builds.

| Typed workload | R37 replay ms | R42 replay ms | R42/R37 |
|---|---:|---:|---:|
| kostya/primes | 3.362 | 23.160 | **6.889x** |
| awfy/sieve | 0.035 | 0.130 | **3.714x** |
| larceny/array1 | 0.759 | 1.654 | **2.179x** |
| jetstream/cube3d | 14.186 | 29.140 | **2.054x** |
| larceny/diviter | 248.934 | 497.624 | **1.999x** |
| text/fast_diff | 142.050 | 274.300 | **1.931x** |
| kostya/matmul | 22.879 | 42.885 | **1.874x** |
| r7rs/nqueens | 1.350 | 2.491 | **1.845x** |
| r7rs/sum | 0.768 | 1.137 | **1.480x** |
| r7rs/fib | 1.203 | 1.450 | 1.205x |
| r7rs/fibfp | 1.111 | 1.110 | 0.999x |
| r7rs/sumfp | 0.064 | 0.064 | 1.000x |

Artifact: `temp/result42_vs37_typed_paired.json`. This is a focused diagnostic
replay, not a replacement Result42 snapshot or a full performance gate.

## Cause 1: typed-array admission scans already packed arrays

The largest unchanged-source outlier contains:

```lambda
var flags: bool[] = fill(limit + 1, true)
```

`fn_fill` still allocates `ELEM_BOOL` and initializes the bytes with `memset`;
that code is unchanged between the two snapshots. It does not attach an
`ArrayRepCert`.

Commit `794847ec83` routes array contracts through
`runtime_type_admit_array` before generic membership can return success.
Without an existing certificate, even the matching rank-one compact-storage
arm walks the entire array, reads every element back as an Item, and calls
`runtime_type_admit_value`. Only after the walk does it install the
certificate. Thus the declaration of the million-element bool array acquires
an element-by-element validation pass. The former cheap admission after
the necessary fill is lost.

An instrumented copy of primes separates fill/declaration from the sieve:

| Phase | R37 median ms | R42 median ms |
|---|---:|---:|
| Fill plus typed declaration admission | **0.023** | **15.971** |
| Remaining sieve/count loops | **3.590** | **6.143** |

This probe adds clock calls and therefore can change register allocation; it
is phase evidence, not the canonical timing. Nevertheless it establishes
that most of the extra time is before the prime-marking loop, not in COW
copies or failed indexed stores. A separate R42 COW profile records **zero
checked-store fallback calls and zero COW copies** for primes.

Relevant source: [admission](../../lambda/runtime/lambda-eval.cpp),
`runtime_type_admit_value` at line 10423 and `runtime_type_admit_array`'s
compact scan at line 10247; [fill](../../lambda/runtime/lambda-vector.cpp),
line 1701. Raw phase data: `temp/result42_analysis/primes_phases.json`.

The intended optimization is to establish the **full** contract at the
boundary from sufficient producer/carrier facts, then reuse its certificate.
It must retain refined, nullable, rank, and representation checks when those
facts do not prove them. **D3.3.3v3** requires a trustworthy certificate, not
an unconditional element walk for every exact primitive carrier.

## Cause 2: repeated array checks and loss of the loop-wide shortcut

The same typed-array change adds `emit_array_rep_cert_guard` to indexed
loads/stores. Even the successful path tests certificate presence, rank/flags,
leaf contract, storage lane/nullability, then bounds and mutation ownership.
It also explicitly does:

```cpp
if (policy.array_contract) dense_inbounds = false;
```

The existing loop-wide extent/type proof therefore stops eliminating
per-access checks for explicit `T[]`. Subsequent `af4e6fa35` packs adjacent
certificate checks together, but does not restore that proof propagation.

The archived cube3d `_run_cube_4058` MIR grows from 19,111 instructions and
953 call sites to 27,269 instructions and 1,309 call sites, including 181
`item_at` fallback sites where there were none. These are static emission
counts: a cold call site is not evidence that the call executes on every
access. The extra guards execute even when their fallbacks never run.

Relevant source: [MIR emitter](../../lambda/runtime/transpile-mir.cpp),
`emit_array_rep_cert_guard` at line 18333 and `emit_checked_index_load` at
line 18589. The array1, matmul, nqueens and cube3d results are consistent with
this added checking/admission cost; their individual shares have not been
isolated by a one-change binary comparison.

The appropriate follow-up is to carry the complete certificate/storage proof
into loop specialization and preserve it across proven same-contract writes,
while invalidating it across operations that can change the carrier. This
follows **D3.3.3v3**, **D3.3.4**, and total-read **S7.1.1v3**.

## Cause 3: Tune22 checks integer sentinel range inside hot comparisons

Commit `58bbb6eeee` changes the native integer comparison gate from accepting
non-null integer operands to requiring a static finite-band proof for ordinary
`int` pairs. Otherwise the emitter checks both operands against
`[-9007199254740991, 9007199254740991]`, performs the integer comparison on a
pass, and retains a floating/sentinel arm on failure.

This has a correctness purpose: native `int` can carry private NaN/infinity
sentinels (**D2.2.2**, **S4.1.2**). Simply reverting it is not a valid fix.
The missing optimization is reuse of established loop range facts.
`mir_int_lane_interval` recognizes literals, length calls, and arithmetic
over such expressions, but does not recover mutable induction-variable or
parameter range facts. Even the specialized finite loop in diviter retains
the new band checks.

The archived MIR makes the change explicit: diviter's hot condition changes
from one `ge x, y` plus its branch to range checks, a branch, `ge x, y`, and
a cold conversion/float-comparison arm. Arithmetic in the finite loop still
uses native add/sub. Seven-pair replay reproduces **2.00x** for diviter and
**1.48x** for sum; float controls fibfp and sumfp remain flat.

Relevant source: [MIR emitter](../../lambda/runtime/transpile-mir.cpp),
`mir_int_lane_interval` at line 9874, `mir_emit_int_lane_pair_in_band` at
line 9972, and the comparison gate at line 10532. Dumps are
`temp/result42_analysis/{diviter,sum}_v{37,38,41,42}.mir`.

## What the evidence does and does not establish

- The major unchanged-source slowdowns survive same-host interleaving with
  matching output, verified release archives and a fixed JIT tier.
- The largest apparent text regressions are workload corrections, not
  comparable runtime deltas. The current suite's aggregate must retain its
  population label.
- Most of the broad step is already present by Result41. For example typed
  primes is 23.241 ms in R41 and 23.664 ms in R42. It should not all be
  attributed to the latest Tune24/25 changes. Tune25 separately documents a
  small Navier-Stokes regression in its own paired experiment.
- Source history identifies the guard/admission changes; phase timing
  isolates the largest primes cost. Exact contribution of every commit to
  every benchmark, including fast_diff and other residuals, remains unbisected.
- No runtime fix was made and no full correctness baseline was rerun for
  this analysis. The benchmark snapshots themselves retain their original
  pre-gate status. Raw comparison, replay, phase, annotation-probe and COW
  evidence is retained under `temp/`.

The first repair target is exact primitive-array admission; next are complete
loop certificate proofs and finite-range propagation into comparisons. All
three should be validated against these unchanged regressions and the full
Lambda/Test262 correctness baselines without weakening the semantic contracts.
