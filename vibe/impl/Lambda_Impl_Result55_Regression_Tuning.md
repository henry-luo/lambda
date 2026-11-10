# Result55 typed and untyped regression tuning

## Current checkpoint

- Merge integration on 2026-11-10: HEAD `bbf9bf261` (tuning) plus
  MERGE_HEAD `ea4cb4de9` (baseline and slow-test repairs). Resolve the two
  MVP conflicts by retaining both behaviors. The merge remains uncommitted.
- Share the single-read analysis through the MVP module header: both append
  fusion and admission discharge need it. Keep the admitted three-statement
  append chain, the adjacent transfer, compact numeric stores and original
  checked keys, and tail-accumulator ownership.
- Fresh merged functional coverage passes 2,281 script cases, 921 math cases,
  143 structured cases and 63 MVP units. The incoming repairs supersede the
  pre-merge baseline failures below.
- Integration repairs preserve consuming array joins before string
  speculation and share one late ownership check across erased primitive
  stores. Alias/binder wrappers retain their value kind during list imaging;
  unchanged annotated union bindings reuse their existing reification, while
  projected/inferred values retain admission (D4.2.5, D4.3.1, S11.1.6v2).
- Focused gates pass 588/588 MIR emission cases, 56/56 optimization cases and
  63/63 MVP units. Nine reviewed zero-slack size records include signed-zero decoding
  and guarded-store growth, with smaller ownership frames tightened too
  (D8.6.1). Semantic goldens are unchanged by merge integration.
- Final `make test-lambda-baseline`: **7,365/7,365 pass**, zero failures,
  partials or skips. `make test262-baseline`: **40,261/40,261 baseline tests
  pass**, zero regressions. All **25/25 MIR size probes** also pass against
  the pinned release host. Logs/reports are `temp/result55_tuning/merged-*`.
- Merge integration is complete and staged without committing. A short
  post-merge timing screen confirms an incoming Matrix optimization-policy
  tradeoff, detailed below; refining that policy remains performance work.
- The timing tables below measure the pre-merge candidate7. They do not
  claim exact merged-compiler timings. Published Result54/55 remain immutable.

## Pre-merge tuning checkpoint

- Updated 2026-11-10; main checkout `/Users/henryluo/Projects/Lambda-opus`,
  `master`, starting commit `dced2136789c8ed2e164875d3f339c37d4bd5eb6`.
- User requests continued compiler/runtime tuning of the five named untyped
  and five named typed Result55 regressions. Preserve the published Result54
  and Result55 snapshots and the unrelated history-chart changes.
- Result54 archive uses the retired Full compiler; Result55 uses MVP. Replay
  the same current source under both archives before attributing regressions.
  Use release binaries, alternating pairs and observable-output equality.
- Scope: untyped Matrix Statistics, Base64, Fannkuch, JSON Gen and Ray;
  typed JSON Gen, Triangl, Brainfuck, Splay and Three-way Merge. Qualify both
  variants of these nine workloads after shared fixes.
- Preserve exact numeric/absence/error semantics, source-order evaluation,
  contract admission and precise rooting (D2.3.4–D2.3.5, D6.3, D7.3–D7.4,
  D8.2.6, D8.3.1). No sibling or vendor source changes; no benchmark changes
  to obtain a performance gain.
- Initial worktree contains only Result55 JSON/report and history artifacts.
  AC power and `lowpowermode=0` confirmed; no other task started by this turn.
- Evidence directory: `temp/result55_tuning/`. All ten historical/current
  source pairs reproduce with equal stdout in three alternating pairs. Only
  Fannkuch accepts identical source in both archived language revisions; the
  other controls use their historical syntax. All provenance checks pass.
- LI-009 is resolved: the owner corrected its headers; after a concurrent
  curl build finished, the normal split release control passed. Lambda-owned
  consumers were migrated from retired API/alloca header paths; a shared
  `lambda/core/api.h` wrapper preserves visibility-only imports (D9.1.1v4).
- The unchanged-compiler release control is `temp/result55_tuning/control`;
  `control-source.json` records its source/binary identities. Candidate7 is
  built as a private release. One earlier gate launch preceded binary creation
  and is superseded.
- Five new fixture/golden pairs cover boxed primitive keys/values, string
  ownership transfers, dead versus observed/captured/dependent binders,
  recursive union constructors and projected/nullable numeric alternatives.
- Successful focused gates: candidate3 six methods, candidate4 five binder
  methods, candidate5 five methods, candidate6 four methods (all include GC
  modes). Initial structural selector failures for public fixture functions
  were corrected by keeping the relevant fixtures private; semantics passed.
- Current paired screens have equal stdout and stable provenance: untyped
  Base64 0.0503x, JSON Gen 0.1487x, Fannkuch 0.5213x; typed JSON Gen 0.0698x,
  Triangl 0.7386x, Brainfuck 0.5411x, Three-way Merge 0.4647x, Splay 0.4864x;
  untyped Matrix Statistics 0.3598x. These are three/five-pair screens, not the
  final acceptance run. Ray remains unchanged through candidate5.
- Final 18-row qualification is complete: 15 alternating pairs plus one warmup
  per side; every row has valid timing, matching goldens and equal stdout.
  All binary/source/build/native-module stability flags are true. Every named
  target improves, including Ray after signed-zero decoding. A small 1.1%
  slowdown in the untyped Triangl counterpart remains visible.
- The emission gate has three pre-existing structural failures, reproduced
  on the unchanged release control: propagation outcomes, modular array update
  and single-use string constructor. A new signed-zero fixture initially
  expected `float(null)` to return null; the control confirms Error, and the
  corrected Error-classification assertion passes in all three GC modes.
- The whole-corpus screen is complete: 142 rows, all outputs equal and golden
  checked; 141 have valid timing, while typed `sumfp` emits a zero timing on
  both sides. Untyped geometric-mean ratio is 0.8541 (71 rows); typed is
  0.9290 (70 nonzero rows). All provenance stability flags remain true.
- The final emission gate passes 59/62 methods; the remaining three are the
  unchanged-control failures above. Every new method passes, including normal,
  forced/poisoned and seeded GC execution.
- Fifteen-pair rechecks resolve the screen's >5% flags: untyped Sieve parity,
  untyped GCbench 0.9960x, typed FFT 1.0250x (0.040 to 0.041 ms), untyped
  Queens 1.0323x (0.155 to 0.160 ms). No rechecked ratio exceeds 1.05;
  the Queens difference persists in its two-sided interval and remains open.
- Next: finish Lambda/Test262 baselines. Published Result54/55 remain immutable.
- Validation budget: focused semantic/GC/MIR tests per implementation batch;
  paired release timing for named rows and counterpart variants; Lambda and
  Test262 baselines at the final milestone. Full benchmark publication is a
  separate snapshot and is not implied by focused qualification.

## Acceptance

Improve the confirmed named regressions through shared compiler/runtime
mechanisms. Retain semantic output checks and rejected-path coverage. Report
remaining gaps, measured gains and any regressions explicitly; chronological
Result54/55 differences alone do not establish causality. A measured zero
(`r7rs/sumfp` typed in Result55) is below timer resolution, not a missing source.

## Reproduction and profiles

`historical-matched-syntax.json` retains three alternating release pairs per
target with one warmup, all outputs equal and all source/build/binary stability
checks true. This confirms the snapshot observations, but syntax-migrated
source pairs are not a pure single-variable compiler experiment. New tuning
acceptance uses the unchanged current source and a freshly built release
control with the candidate's sibling archive inputs.

| Target | Mode | Result54 ms | Result55 ms | Ratio |
|---|---|---:|---:|---:|
| Julia Matrix Statistics | untyped | 16.400 | 239.546 | 14.61 |
| Kostya Base64 | untyped | 12.591 | 145.876 | 11.59 |
| BENG Fannkuch | untyped | 0.278 | 1.599 | 5.75 |
| Kostya JSON Gen | untyped | 7.318 | 31.310 | 4.28 |
| Larceny Ray | untyped | 0.198 | 0.826 | 4.17 |
| Kostya JSON Gen | typed | 7.150 | 29.064 | 4.06 |
| Larceny Triangl | typed | 146.608 | 557.010 | 3.80 |
| Kostya Brainfuck | typed | 95.023 | 243.792 | 2.57 |
| JetStream Splay | typed | 232.293 | 602.405 | 2.59 |
| Text Three-way Merge | typed | 1410.850 | 3467.510 | 2.46 |

Diagnostic release profiles and MIR are beside the artifact (the TSV timing
overhead is not performance evidence). Matrix Gram/Square perform three million
generic stores; Fannkuch 35,610. Triangl performs 7,759,068 checked primitive
stores. Projected keys retain Item transport and miss the existing finite-int
store path. Brainfuck performs 9,060,000 generic index/ord pairs. Base64 performs
1,333,500 generic joins. Typed JSON Gen freezes its prefix 10,000 times because
an adjacent admission temporary separates the join from assignment. Three-way
Merge makes 30,855,000 boxed dependent accessor calls. Splay's recursive payload
and tree stores retain repeated structural checks. Ray makes 40,000 boxed
sphere calls and 50,000 numeric conversion calls.

## Implemented mechanisms

- Guard Item-carried finite keys and primitive values before direct array
  stores. Preserve the original checked operands on a miss, with exclusive
  ownership, live bounds and receiving certificates checked before raw writes
  (D2.3.4, D4.3.1, D6.3, S9.1.2).
- Fuse string index/ordinal for guarded dynamic keys, preserving Unicode,
  absence and invalid-key fallbacks (S7.1.1v3). Erased self appends prove their
  string prefix dynamically and replay the original join tree on a miss
  (S10.6.1). Single-use admitted temporaries transfer their owned prefix
  without introducing an observable alias.
- Elide only unobserved, single-site selections on read-only plain array
  parameters of private direct-only functions. Retain the written admission;
  observed, captured, dependent, mutable, optional and escaping cases retain
  their binder environment (D4.1.4, D7.3.1, S11.4.8v2).
- Construct contextual recursive records directly in their trusted declared
  descriptor, including tagged union slots, after field admission. Native
  field consumers keep their stricter concrete-carrier requirement (D4.2.5).
- Feed projected scalar hints only to exact guarded entries, extend existing
  guarded iteration locals to float seeds, and reuse the shared signed-zero
  decoder before the generic boxed-number conversion (D2.3.5, D4.3.1).

## Final target qualification

`temp/result55_tuning/qualification-18.json` contains 15 alternating release
pairs and one warmup per side against the freshly built unchanged-compiler
control. Times are execution medians in milliseconds; all named targets win
15/15 pairs. Each target's one-sided 95% paired-bootstrap upper ratio is below
1. These are same-source causal comparisons, not a new published snapshot.

| Target | Mode | Control ms | Candidate ms | Time reduction |
|---|---|---:|---:|---:|
| Julia Matrix Statistics | untyped | 235.584 | 81.361 | 65.5% |
| Kostya Base64 | untyped | 142.486 | 7.263 | 94.9% |
| BENG Fannkuch | untyped | 1.595 | 0.841 | 47.3% |
| Kostya JSON Gen | untyped | 29.198 | 4.456 | 84.7% |
| Larceny Ray | untyped | 0.822 | 0.712 | 13.4% |
| Kostya JSON Gen | typed | 26.545 | 1.906 | 92.8% |
| Larceny Triangl | typed | 565.898 | 414.681 | 26.7% |
| Kostya Brainfuck | typed | 256.616 | 132.014 | 48.6% |
| JetStream Splay | typed | 578.902 | 281.408 | 51.4% |
| Text Three-way Merge | typed | 3335.450 | 1539.810 | 53.8% |

The other eight counterpart rows range from an 8.4% gain (typed Matrix) to
a 1.1% slowdown (untyped Triangl). Typed Ray differs by 0.001 ms (+0.8%) and
its two-sided interval spans parity. Substantial historical gaps remain:
Matrix is still about five times its syntax-matched Result54 measurement;
Triangl about 2.8 times; Ray about 3.6 times. These cross-snapshot ratios are
descriptive because the historical language revision uses different syntax.

## Post-merge performance check

The merged release SHA256 is
`2e2f9e84af377d130fe0c107e87305dd48d548da973f03b32a975d9262110f57`.
`merged-v-candidate7-18.json` compares it with the pre-merge tuned candidate7
over the same 18 target/counterpart sources, three alternating pairs and one
warmup. All goldens, stdout hashes and provenance stability checks pass.
Typed JSON Gen improves a further 19.4%, and typed Three-way Merge 5.4% in
this screen. The only >5% slowdowns are the two Matrix Statistics variants.

`merged-matrix-recheck.json` confirms those in 15 pairs, with equal golden
stdout, stable provenance and zero merged wins:

| Mode | Pre-merge tuned ms | Merged ms | Change |
|---|---:|---:|---:|
| untyped | 81.309 | 102.496 | +26.1% |
| typed | 21.975 | 31.758 | +44.5% |

The incoming `MIR_EAGER_MODULE_O1_THRESHOLD` caps aggregate eager-module
optimization at O1 above 5,000 instructions; its completed slow-test plan
explicitly left long-running eager throughput unqualified (D8.3.1–D8.3.4).
`merged-matrix-opt-isolation.json` isolates O1/O2 within the same pre-merge
binary using rotated triplets, one warmup and five measurements through the
same single-script batch path. Untyped medians are 81.551 ms at O2 and
105.147 ms at O1; typed medians are 21.948 ms and 30.495 ms. The merged
host, with O2 requested but the aggregate cap active, measures 100.603 ms
and 29.839 ms. Every output matches. This identifies the optimization cap
as the dominant cause of the post-merge Matrix slowdown. Preserve the
incoming startup fix; a policy refinement needs both cold-import latency
and steady-state acceptance. No benchmark source or published snapshot
was changed for this check.

## Qualification evidence

| Gate | Evidence | Result |
|---|---|---|
| Target/counterpart timing | `temp/result55_tuning/qualification-18.json` | 18 rows x 15 pairs; all golden outputs and provenance checks pass |
| Complete canonical timing screen | `temp/result55_tuning/broad-142.json` | 142 rows x 3 pairs; all outputs pass; typed sumfp is below resolution |
| Flagged screen rechecks | `temp/result55_tuning/recheck-15.json` | 4 rows x 15 pairs; no ratio above 1.05; small Queens/FFT deltas retained |
| Emission/ownership/GC units | `temp/result55_tuning/candidate7-emission-final.json` | 59/62 pass; three existing structural assertions also fail on control |
| Unchanged-control structural replay | `temp/result55_tuning/control-known-emission.log` | Same three failures; existing assertions retained |
| Release source/binary provenance | `temp/result55_tuning/candidate7-source.json`, `control-source.json` | Private normal release outputs; source and binary hashes retained |
