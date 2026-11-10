# Result55 typed and untyped regression tuning

## Current checkpoint

- Updated 2026-11-11, master HEAD `4e046a786`. Task changes remain
  uncommitted; both earlier merges are committed and no conflicts remain.
  No vendor/sibling source, benchmark workload or published Result54/55 is
  changed. User target: every typed/untyped Result54 workload individually
  faster. **Still open.** Evidence lives in `temp/result54_tuning/`.
- Last aggregate-qualified milestone: candidate23, full Lambda **7,370/7,370**
  (debug, including 2,283 scripts, 65 emission methods and 25 ratchets),
  pinned release Test262 **40,261/40,261**. Root `lambda.exe` remains that debug
  host. `candidate23-provenance.json`, `candidate23-qualified.patch`,
  `candidate23-baseline-reports/` and `candidate23-test-fixtures/` identify it.
- Current compiler is candidate23. All 65 emission methods and 25 ratchets
  pass, including forced/poisoned GC, nullable/mixed domains, recursive
  helpers and exact literal receiving contracts. Its fresh three-pair replay
  against the pinned Result54 archive completes all 142 rows: 106 lower
  medians, 35 higher/equal, one correct typed sumfp below timer resolution.
  All outputs and provenance stability flags pass; 99 rows have paired 95%
  upper bounds below one. `candidate23-comparison.md` retains every row.
  The all-row goal remains open. Pinned release Test262 passes all
  **40,261/40,261**, zero regressions. Full debug Lambda baseline passes
  **7,370/7,370**, all 40 selected suites.
- Candidate17's one-pair historical screen had 106 faster rows, 35 slower or
  equal, and correct typed sumfp output below timer resolution. This is a
  prioritization screen, not final proof. The preceding robust candidate12
  three-pair replay had 104 faster, 37 slower, and the same timing-zero row.
- Candidate17's independent local-range pass (excluding provisional private
  parameter and String-length facts) cuts untyped Ray 0.679 to 0.129 ms in
  seven paired wins versus candidate14 (D7.4.1, S7.1.1v3). Candidate18 guards
  generic Array/Error physical hypotheses and finite runtime unit-step bounds
  (D4.3.1, D8.2.6); its public fill/reduction micro falls 9.458 to 0.946 ms
  in eleven paired wins. Failed/empty and INT53-boundary loops retain checks.
- Nested integer selections now use a scoped proof, reject recursive body
  edges and charge complete nested expansion against the shared inline budget
  (D6.3, D7.4.1). Candidate20 versus candidate18 cuts untyped Levenshtein
  7.868 to 3.973 ms (seven paired wins, stable inputs); typed and Ray, Matrix,
  Triangl controls remain near parity. Ordinary call ABIs remain checked.
- Candidate23 adds shared receiving-contract proofs to closed calls. A literal
  parameter contract previously emitted a collecting validator inside a closed
  loop (E501, reproduced on candidates17 and20); the new regression now
  compiles and yields the expected error on invalid arguments (D4.3.1, D6.3).
- Rejected candidate21: eager cap 5,000 to 6,000 improves untyped Levenshtein
  13.4% but adds 6.5% to cold chart startup (10.422 to 11.095 seconds, three
  paired runs). Rejected candidate22: native inline result carriers regress
  Levenshtein 13.5% in seven losses and add a size-budget failure. Both edits
  are removed. The optimizer cap remains 5,000.
- Rejected candidates24/25: selecting one construction-predicted array family,
  including fallible fill predictions, cuts Levenshtein MIR by 105 instructions
  but gives no speedup and regresses untyped Matrix 62.75% (seven losses).
  The precise cause within those combined changes is unconfirmed. They are
  removed; compiler hashes again match candidate23. All focused checks passed
  those experiments, demonstrating why performance controls remain required.
- Earlier retained fixes include logical carrier joins, guarded affine
  reductions/copy/reversal, private counter publication, primitive certificate
  checks, caret-aware dispatch and staged array-owner snapshots (S3.4,
  D4.3.1, D4.3.3, D6.3, S9.1.2). Candidate14 text search versus candidate12
  improves untyped 6.3% and typed 29.5%; profile evidence eliminates 30,168,954
  fn_index calls. Two intended size increases have reviewed zero-slack caps
  (D8.6.1); no new ratchet budgets are changed by candidates18–25.
- Live compiler probe on candidate17 debug rules out a new scalar-counter
  transport experiment: Triangl `solutions`, `pegs` and `depth` already use
  native INT lanes. `stack`, `mi` and both `last_m` bindings are erased Items
  with no finite interval; the immutable move arrays are native read-only
  owners. Evidence: `triangl-c17-bindings.log` and the LLDB scripts. A closed
  array-element interval fixed point is a lead, not an implemented proof.
- The follow-up guard-plan debugger probe is inconclusive: the pinned debug
  executable references archive DWARF, and the baseline rebuild replaced that
  archive. Preserve a companion dSYM for candidate23 after its build/gate;
  the earlier binding probe completed before that replacement and remains valid.
- Next: preserve isolated debug symbols, then inspect the guarded-condition
  plan and remaining array-element interval gaps before another experiment.
  All-row performance acceptance remains unfinished.
- Fresh candidate23 symbols confirm that no multi-key `guard_array_iteration`
  plan is emitted for either Triangl variant. The first plan handles one stack
  store. `boolean_branch` deliberately dispatches AND/OR operand by operand,
  so combined-condition key preloading is not an existing hot cost. The
  attempted debugger invocation of a combined plan was inconclusive; no
  compiler edit follows from it. Further work must preserve short circuiting
  and measure any proposed guard sharing rather than remove this policy blindly.

## Continuation investigation history

### Earlier continuation checkpoints (superseded)

The previous continuation checkpoint is retained below for investigation details.

### Checkpoint before candidate23 qualification

- Updated 2026-11-11, master HEAD `4e046a786`. Task-owned changes are
  uncommitted; both earlier merges are committed. No vendor/sibling source,
  benchmark workload, or published Result54/55 is changed. User target:
  every typed/untyped Result54 workload individually faster. **Still open.**
- Qualified candidate17: full Lambda baseline **7,370/7,370** (debug),
  including all 2,283 scripts, 65 emission methods and 25 MIR ratchet probes;
  pinned release Test262 **40,261/40,261**, zero regressions. Evidence:
  `temp/result54_tuning/candidate17-{lambda,test262}-baseline.log`,
  `candidate17-provenance.json`, `candidate17-qualified.patch` and
  `candidate17-baseline-reports/`. Root lambda.exe is candidate17 debug.
- The last full historical replay is candidate12: 142 rows, three alternating
  release pairs plus warmup; every provenance stability flag passes. 104 rows
  are faster, 37 slower; typed sumfp has correct output below timer resolution.
  Full list: `temp/result54_tuning/candidate12-comparison.md`. These counts do
  not describe subsequent candidates and are not final qualification.
- Candidate14's three-pair screen vs candidate12: text search untyped 0.9369x,
  typed 0.7051x, every pair wins. Boyer-Moore eliminates 30,168,954 fn_index
  calls; root reloads fall from 150,869,346 to 24,576. Snapshot-key tests pass
  forced/poisoned GC for int/float/bool/boxed arrays (D6.3, S9.1.2).
- Retained: carrier-correct logical phis, noncollecting region checks, guarded
  affine reductions in either direction, erased copying/reversal, scalar
  publication only for private counters, exact primitive certificate checks,
  caret-aware integer dispatch and one predicted array-predicate alternative
  (D8.2.6, D4.3.1, D4.3.3). The two intended emission increases are reviewed
  with exact zero-slack caps; typed reduction shrinks by four (D8.6.1).
- Discarded: mixed per-function optimizer levels (MIR generator crash), private
  nullable scalar transport (Ray +23.6%), and the first nested integer-helper proof
  (no Levenshtein gain; superseded by candidate19). Cold chart cost for the retained optimizer policy is
  9.734 to 10.297 seconds; keep that tradeoff explicit (D8.3.1–D8.3.4).
- Largest emitted bodies for Triangl, Ray and text search are below the eager
  5,000-instruction cap. Their remaining gaps are not explained by that cap.
  Candidate15's finite-index demand experiment passes 65 methods and all 25
  ratchet probes, but seven Triangl pairs show no measurable gain (untyped
  0.9949x, typed 1.0025x). It is removed; nested-index semantic coverage stays.
- Candidate16 is rejected before performance acceptance: the reassignment
  helper's fourth argument disables whole-binding checks. The original guard
  excludes indexed/member writes, not ordinary assignments. Removing it would
  lose their revocation protocol. Its compiler changes are fully reverted;
  current compiler source hashes again match qualified candidate14. Expanded
  nested-index and scalar-transition fixtures remain as semantic coverage.
  The two expanded GC methods pass on candidate14. No candidate15/16
  performance gain is claimed.
- Candidate17 release passes all 65 methods and 25 ratchet probes. An independent local-interval pass now runs
  before private call transport widens on nullable projections (D7.4.1). It
  excludes provisional private parameters and string-length facts, preventing
  circular ABI/range evidence. The ordinary final interval pass is retained.
  New cases compare a bounded float projection with mixed int/float offsets;
  existing absent projections and mixed numeric-family checks remain.
  Seven stable Ray pairs vs candidate14 cut untyped 0.679 to 0.129 ms
  (0.1900x, 7/7 wins); typed remains 0.129 ms. Other selected kernels are near
  parity. The native-ABI assertion and provisional String/range overflow
  counterexample pass the expanded GC methods. Test262 is 40,261/40,261.
- Candidate17's fresh full historical screen has 106 faster rows, 35 slower
  or equal, and typed sumfp below resolution; all stability flags pass. This is
  **one pair without warmup**, used to prioritize follow-up, not final proof.
  Artifact: `candidate17-historical-screen-142.json`. The all-test target is open.
- Full Lambda baseline for candidate17 passes **7,370/7,370**. Snapshot and
  reports are saved before further experiments. Next investigation: generic
  array/Error owners are excluded from physical hypotheses, and nested integer
  selections lack a closed-body proof. Both are leads for untyped Levenshtein;
  isolate them before accepting a new change. Last fully qualified is candidate17.

- Candidate18 admits generic Array/Error owners to guarded physical array
  hypotheses and guards finite runtime bounds for forward unit-step loops
  (D4.3.1, D8.2.6). The checked sibling retains all admission/error behavior.
  All 65 methods and 25 ratchets pass. An opaque public fill/reduction
  microbenchmark falls from 9.458 to 0.946 ms (11/11 paired wins); standard
  Levenshtein, Ray, Matrix and Triangl controls remain near parity.
- Candidate19 proves nested integer selections only within an inline proof
  scope, rejects recursive body edges, and charges the complete nested
  expansion against the shared inline budget (D6.3, D7.4.1). It passes 65
  methods and 25 ratchets. Seven paired Levenshtein runs versus candidate18
  cut untyped 7.722 to 3.956 ms (7/7 wins); typed +4.3% is unresolved noise.
  The untyped body grows to 5,674 MIR instructions, exceeding the eager O2
  cap. This remains a focused experiment, not aggregate qualification.
- Candidate20 release built successfully. It tightens selection operands to
  integer/Boolean operations and omits unpredicted numeric-array speculative
  versions when a String prediction already has its own guarded body.
  Opaque array and null inputs retain the checked sibling. All 65 methods and
  25 ratchets pass. Seven stable pairs versus candidate18 cut untyped
  Levenshtein 7.868 to 3.973 ms (0.5050x, 7/7 wins); typed and Ray, Matrix,
  Triangl controls are near parity. The body shrinks 5,674 to 5,267 MIR
  instructions. Candidate17 remains the last fully qualified milestone.

- Rejected candidate21: raising the eager cap 5,000 to 6,000 improves untyped
  Levenshtein another 13.4%, but three cold chart pairs increase median wall
  time 10.422 to 11.095 seconds (+6.5%). Both artifacts have stable inputs.
  The cap remains 5,000. Rejected candidate22: native inline result carriers
  pass 65 semantic methods, but Levenshtein regresses 13.5% (0/7 wins), with
  one additional size-budget failure. Its carrier edits are fully removed.
- Candidate23 release passes all 65 methods and 25 ratchets. A literal receiving-contract counterexample
  (`lesser_one` in a guarded reduction) reproduces E501 on candidates17 and20:
  the closed proof permits a collecting `lambda_type_check_lazy` admission.
  A shared call-admission proof now covers both nested integer selections and
  ordinary closed calls (D4.3.1, D6.3). The negative regression fixture includes
  wrong integer values and exact numeric conversion; forced-GC checks pass.

- Candidate24 release passes all 65 methods and 25 ratchets. Array construction predictions select one
  speculative primitive family instead of emitting alternatives for the other
  families; their runtime inputs retain the original checked sibling
  (D8.2.6, D4.3.1). This targets code size rather than widening optimization
  budgets. Levenshtein remains at 5,267 instructions because fallible fill
  producers have no construction hint before count intervals are proved.

- Candidate25 release is building. The existing element-inference helper now
  distinguishes a speculative construction prediction from an admission
  proof: a fill can predict its successful element family without proving a
  nonnegative count. Receiving/inferred contracts retain the original count
  requirement and Error path (D4.3.1, S11.4.1v5). Candidate24's version budget
  can then omit unpredicted physical families. Failed/empty fill fixtures are
  retained. No optimizer-cap increase or inline carrier experiment remains.




- Updated 2026-11-10; clean starting `master` HEAD `4e046a786`, with this
  task's uncommitted Lambda-owned changes. Both earlier merges are committed.
  User target: every typed/untyped Result54 workload individually faster.
  **That target remains unfinished.** No published Result54/55 or benchmark
  workload source is changed. Evidence: `temp/result54_tuning/`.
- The full historical manifest has 142 rows (71 workloads x two variants).
  Historical/current syntax pairs retain exact output/golden checks. The
  initial one-pair replay has 46 slower medians, three failures, and typed
  sumfp below timer resolution. Candidate5's full three-pair same-source
  screen has 38 medians above recorded Result54, stable source/binary/build
  identities and all candidate goldens correct. These are screening leads,
  not proof that every historical row is improved.
- Retained fixes: normalize both logical phi arms to their carrier (S3.4);
  reject collecting bool/Error operations from closed regions (D6.3); guard
  and publish accumulators across affine loops; guard erased reversal and
  copying; avoid nested versions in whole-loop checked siblings; dispatch
  repeated integer tests through one witness, including caret-wrapped tests
  and mutable-callback rejection (D8.2.6, D4.3.3).
- Native optimization now caps the largest eager function at 5,000 MIR
  instructions rather than the aggregate module. Candidate3 Matrix medians
  are 17.309 ms untyped and 12.226 ms typed versus current-control 100.593
  and 27.470 ms. Cold chart startup increases 9.734 to 10.297 seconds;
  retain that measured tradeoff (D8.3.1–D8.3.4).
- Candidate8 screen: untyped array1 0.117 ms, Fannkuch 0.374 ms, Matrix
  17.295 ms; typed Matrix 12.504 ms. Remaining Triangl/Ray/text gaps persist.
  Literal-table and call profiles rule out repeated runtime copies as the
  Triangl cause; remaining overhead is generated guards and carriers.
- Discarded experiments: per-function MIR optimization levels (native
  generator crash) and private nullable scalar transport (Ray +23.6%).
  Temporary binding diagnostics are removed. No vendor/sibling sources edited.
- Candidate6 full Lambda milestone: **7,366/7,370 pass**; all 2,283 script
  cases, 65 emission methods and 921 math cases pass. Four failures are size
  ratchets. Candidate9 eliminates two growth records without budget edits.
- Current candidate10 debug passes **65/65 emission methods**, including
  forced/poisoned and seeded GC, COW, absence, mixed numeric tags, and selector
  mutation. Size ratchet is **23/25**; remaining growth is the new affine
  reduction version (+156 module instructions) and erased reduction (+13
  module/+17 function, with typed reduction -4). Review the release counts
  before changing those two zero-slack records (D8.6.1).
- Test262 candidate10 release: **40,261/40,261 baseline pass**, zero
  regressions. The gated host is pinned as `temp/result54_tuning/candidate10`
  with source and binary hashes in `candidate10-provenance.json`.
- Candidate10 Brainfuck has nine valid alternating pairs, all matching the
  golden: untyped 167.831 to 80.173 ms (0.4777x), typed 127.684 to 73.786 ms
  (0.5779x), each winning 9/9 pairs. Recorded Result54 was 96.818/93.533 ms;
  a fresh historical replay remains required for a contemporary claim.
- Candidate11 debug passes all 65 methods. Five-pair screen against candidate10
  cuts Fast Diff 92.569 to 43.590 ms untyped and 96.598 to 46.234 ms typed;
  all five pairs win and goldens match. Binaries and workload sources are stable;
  a subsequent compiler-source edit makes that screen's build-manifest stability
  flag false, so retain the pinned source snapshots and treat it as a lead.
- Candidate12 extends that proof to forward unit steps whose finite limit only
  becomes known after a guarded parameter version (D8.3.2). Release passes all
  **65/65 methods**, including backward affine keys, absence, COW and shared
  counter publication. Release ratchet confirms the same two reviewed growth
  records as debug: +156 module instructions and +13 module/+17 erased_sum.
- The complete archived-Result54/candidate12 three-pair replay is saved as
  `candidate12-historical-142.json`; all provenance stability flags pass. Of
  142 rows, **104 are faster, 37 slower**, and typed sumfp is below timing
  resolution with correct output. No performance target is waived. The full
  sorted comparison is `temp/result54_tuning/candidate12-comparison.md`.
- Candidate13 release passes 65/65 methods, but its nested integer-helper
  experiment has no Levenshtein gain and is removed. Its three-pair predicate
  screen improves typed text search 19%, but regresses untyped 16%; it is not
  an accepted performance endpoint. Both sources/binaries remain pinned.
- Candidate14 release passes all 65 focused methods. It keeps one construction-guided predicate
  hypothesis instead of all primitive versions. Profiling candidate12 identifies
  30,168,954 fn_index calls and 150,869,346 root reload events in Boyer-Moore.
  The existing read path was skipping numeric lanes for a nested/effectful key.
  Candidate14 checks the staged owner after evaluating the key, preserving the
  earlier snapshot instead of consuming the binding's potentially changed cache
  (S9.1.2, D6.3). New cases mutate/rebind the owner during key evaluation across
  int, float, bool and boxed carriers, with forced-GC coverage.
- Candidate14 has three valid alternating pairs against candidate12: text
  search untyped 0.9369x, typed 0.7051x, winning every pair. Profiling confirms
  Boyer-Moore's 30 million fn_index calls are eliminated; its root reload count
  falls from 150,869,346 to 24,576. Sources and binaries are stable. Levenshtein
  remains approximately unchanged, so the removed integer-selection experiment
  is not credited with a gain.
- Reviewed only the two intended size-growth records: typed_array_guard module
  1689 to 1845 (+78 guarded reduction in each of two expansions); array_regions
  module 2679 to 2692, erased_sum 541 to 558, typed_sum 313 to 309. Roots/homes
  and other caps stay fixed. The reviewed release ratchet passes **25/25**
  (D8.6.1); opcode review is in `temp/result54_tuning/size-review/`.
- Full `make test-lambda-baseline JOBS=4 TEST_JOBS=4` is running against
  candidate14 source in `candidate14-lambda-baseline.log`. Do not change source
  or root lambda.exe until it completes. Next: Test262, largest-body/optimizer
  diagnosis of the remaining native hot-loop gaps, and further tuning. The
  all-Result54 target remains unfinished.

### Starting investigation

- Continued 2026-11-10 from clean `master` at `4e046a786`; both merges
  below are now committed. The expanded user target is every typed and
  untyped Result54 workload, with an individual improvement requirement.
- Evidence for this continuation is `temp/result54_tuning/`. A new normal
  split release control is pinned by `control-provenance.json`; the full
  142-row manifest is derived from Result54 and uses the original archived
  sources where syntax changed. Require equal outputs, goldens and stable
  provenance. Preserve the published Result54/55 files and benchmark sources.
- The initial 142-row replay completed with stable provenance: 46 slower
  medians in the one-pair screen, three execution/compilation failures and
  typed sumfp below timer resolution. Failures: untyped DeltaBlue/Quicksort
  admit collecting comparisons into closed regions; typed jq_bf selects a
  tagged false as a native truth value across a nested logical phi (S3.4).
- Candidate1 fixes both causes, guards boxed integer/float accumulators at
  whole-loop entry and publishes them once. Six focused methods pass,
  including every-allocation/poisoned and seeded GC. All three failed
  workloads now match their existing goldens. Untyped array1 is 0.118 ms
  versus the 2.4 ms current control and approximately 0.8 ms Result54.
- Candidate2's mixed per-function MIR optimization levels crash during
  Navier–Stokes code generation. That experiment is discarded; no vendor
  code changes. Candidate3 chooses one level per module from the largest
  function and extends affine endpoint proofs to invariant products.
- Candidate3 passes all six focused methods. Three-pair screens: Matrix
  untyped 100.593 to 17.309 ms; typed 27.470 to 12.226 ms; Navier untyped
  110.550 to 96.977 ms; typed 135.348 to 130.183 ms. All outputs match.
  Cold chart JIT startup increases from 9.734 to 10.297 seconds (three
  alternating pairs plus warmup). Retain this explicit latency tradeoff;
  it remains substantially below the original pre-cap 15.4-second evidence.
- Candidate4 recognizes canonical primitive certificates on erased stores,
  retaining checked paths for other certificates, and handles indirect
  integer keys in Boolean branch demand. All six focused methods pass.
- Candidate5 branches on Boolean/Error outcomes and explicit carets without
  materializing logical phis (D8.2.6, S3.4). The full 142-row three-pair
  screen completes with stable binary/source/build identities. Every candidate
  workload matches its golden; typed sumfp still has zero timing. Three
  control failures remain explicitly invalid A/B rows. 38 candidate medians
  still exceed recorded Result54, so the expanded user target is unfinished.
- The full 65-method emission run passes 64 methods. The sole failing method
  encountered a reserved temporary parameter name added while the run was
  active; that fixture is corrected and needs a focused rerun. The changed
  nullable comparison assertion now accepts a direct branch, retaining its
  checked helper/absence assertions and semantic golden.
- Candidate6 adds exact-range guarded reversal for erased arrays/counters.
  All **65/65 emission methods pass**, including new GC/COW/absence checks.
  Untyped Fannkuch wins 7/7 pairs at 0.5246x candidate5; typed is approximately
  unchanged. Triangl's temporary diagnostic confirms its literal tables are
  already inferred int arrays. Its execution-call profile has no collecting
  calls in the search loop: remaining overhead is generated guards/carriers,
  not repeated runtime copies. The diagnostic is removed from current source.
- Candidate7's private nullable scalar experiment passes all 65 methods but
  regresses untyped Ray from 0.695 to 0.859 ms in seven pairs, with no material
  Levenshtein/Triangl improvement. It is discarded. Projection/absence/mixed
  numeric-family cases remain as semantic coverage (D2.3.5, D4.3.1).
- Current production source is candidate6 without its diagnostic. Full
  `make test-lambda-baseline JOBS=4 TEST_JOBS=4` is running; evidence is
  `temp/result54_tuning/lambda-baseline.log`. Next: resolve actual baseline
  or emission-budget findings, run Test262, rebuild the pinned release, and
  continue the historical gaps. The all-Result54 target remains unfinished
  (D6.3, D8.3.1–D8.3.4, D8.6.1–D8.6.3).

## Previous merge checkpoint

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
