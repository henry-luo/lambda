# Typed Lambda performance audit — 2026-10-03

**Status:** the first tuning round (§10–§12) and follow-up P0–P5 scope (§13) are implemented and validated. All fifteen matched annotation workloads meet the declared parity gate in both tiers; actual new-typed/previous-erased comparisons also pass all thirty combinations (§14.6). Remaining same-source and existing-port differences stay visible (§14.5–§14.7). Strict interpreter selection and untyped Queens support were fixed before these tuning rounds (§6/§9). Performance comparisons use **pinned MIR (`LAMBDA_EXEC_BACKEND=jit`)**, with separate strict pure-T0 evidence. No AUTO timings enter these comparisons.

**Follow-up completed, 2026-10-04:** the release passes the complete **6,213/6,213** baseline. Two independent acceptance campaigns establish 24 annotation speedups and six practical-equivalence results within the declared 2% uncertainty tolerance. Direct comparisons against the previous erased release establish 29 speedups and one practical-equivalence result. §13 also corrects the earlier interpretation of MIR root counters: the old counter instrumentation introduced extra safepoints (§13.1). The uninstrumented timings remain valid.

**Authority:** D8.1.1v15 (execution selection), D2.4.1–D2.4.3 (contract and representation), D3.2.4v4/D3.3.4 (record admission and full inferred contracts), D5.3.3 (precise roots), S7.7.2–S7.7.4 (boundary failures), S9.1.2–S9.1.3 (snapshots and borrows).

## 1. Which results are current?

The latest committed benchmark snapshot is `test/benchmark/benchmark_results_v50.json`, dated **2026-09-30**, commit `a9489bc329`. Its `mir` and `mir_typed` columns are the relevant forced-MIR measurements. Of 63 numeric pairs, four typed ports were more than 10% slower: bounce (+49%), nqueens (+18%), fasta (+15%), and fannkuch (+14%).

The quoted T0 numbers (ack 2.78→3.24 seconds, etc.) are the older baseline in `Lambda_Impl_Interp_Tune2.md` §2.4. They precede the latest untyped tuning recorded in §12.6. In particular, lazy boundary diagnostics and the exact scalar admission fast paths already exist; proposing them as new work would miss the current bottlenecks.

This audit rebuilt **`make release` at `72cc6de7675429e1654ae0bdbb2cb7bea8e61e08`** on Darwin arm64, on AC power. The measurement binary SHA-256 is `ca5eeb01cb1d2e0896f7d0aaf74dc7cc1edce2ba9e86b9dfec8d549b189fddbe`. An unchanged copy is retained as `temp/typed_t0_audit/lambda-before-pin-fix.exe`.

Method:

- Same release binary for each typed/untyped pair; one warmup per variant; alternating execution order.
- Five process pairs for pinned MIR, three for T0; execution time from the benchmark's `__TIMING__` marker, excluding compile/startup time.
- Canonical benchmark manifest, plus BENG mandelbrot to match the original question. The extra mandelbrot row is excluded from corpus counts.
- Compare stdout after timing-marker removal. Exclude timeouts and unequal-output pairs. For T0 additionally require both variants to report execution with **zero fallback and no MIR satellite** in a separate diagnostic run.
- Profiling is separate from timing: existing MIR execution counters, COW/admission counters, and native macOS `sample` on the release binary. Several private functions are stripped, so the native samples do not justify precise percentages for individual interpreter routines.
- The existing ports are not perfectly annotation-only controls: some also change casts, borrows, or source organization. These are benchmark-pair results, not an isolated estimate of annotation cost.

Raw data and reproduction driver: `temp/typed_t0_audit/{jit.json,interp.json,diagnostics.json,run_audit.py}`. Profiles: `*.jit.exec.tsv`, `*.cow.tsv`, and `*.sample.txt` in that directory. Timing JSON retains all individual samples, paired uncertainty, binary identity, output hashes, and host load.

## 2. Pinned MIR: a small but persistent regression set

Across **61 equal-output canonical pairs**, 14 typed medians are slower at all, seven are more than 5% slower, and **four are more than 10% slower**. Geometric mean typed/untyped is **0.607** (about 1.65× faster for typed overall). Small sub-millisecond differences need more repetitions before being a hard gate.

| Benchmark | Untyped ms | Typed ms | Typed change |
|---|---:|---:|---:|
| bounce | 0.061 | 0.085 | +39.3% |
| fasta | 0.730 | 0.916 | +25.5% |
| nqueens | 0.917 | 1.090 | +18.9% |
| fannkuch | 0.272 | 0.300 | +10.3% |
| ack | 14.428 | 10.177 | −29.5% |
| Richards | 278.756 | 67.929 | −75.6% |
| Mandelbrot, AWFY | 28.495 | 28.595 | +0.4% |
| Mandelbrot, BENG | 13.054 | 13.090 | +0.3% |

The smaller watchlist is pidigits (+10.0% before rounding), puzzle (+5.9%), and microdiff (+5.9%). A confirmation run is recorded separately below; these initial corpus counts are not recomputed by mixing sample sizes.

### Invalid pairs

**Matmul is not a valid speed comparison.** Untyped prints `matmul: sum=0`; typed prints `matmul: sum=-29562`. The untyped procedure's output parameter lacks `var`, so its writes remain local snapshots (S9.1.3); the typed port borrows the output. Its measured timings cannot establish an annotation regression.

**Spectralnorm also differs:** untyped prints a NaN result and typed prints approximately `1.274219991`. Exclude it until the ports compute the same result. This audit did not alter either benchmark or bless either result as equivalent work.

## 3. Pure T0 still has broad typed overhead

After excluding fallback, timeout, and unequal-output rows, there are **52 canonical pairs**: 39 typed medians are slower, 30 by more than 5%, and **27 by more than 10%**. Geometric mean typed/untyped is **1.148**. Thus the broad T0 concern remains, although ack and Mandelbrot no longer reproduce the old large penalties.

| Benchmark | Untyped ms | Typed ms | Typed/untyped |
|---|---:|---:|---:|
| ack | 512.454 | 464.791 | 0.91× |
| Mandelbrot, BENG | 1840.770 | 1877.150 | 1.02× |
| Richards | 1728.300 | 1982.420 | 1.15× |
| gcbench | 1204.250 | 4210.360 | 3.50× |
| deriv | 91.144 | 275.230 | 3.02× |
| binarytrees | 54.898 | 160.256 | 2.92× |
| prettier_ast | 3393.610 | 6750.600 | 1.99× |
| quicksort | 66.357 | 111.288 | 1.68× |
| bounce | 3.209 | 4.876 | 1.52× |
| primes | 303.395 | 389.718 | 1.28× |

The apparent untyped/typed Queens gap was **not T0 versus T0**. Untyped Queens ran MIR after a support-scan rejection; typed Queens ran T0. Towers has the same problem. Both triangl and text_search variants fell back; typed splay fell back while untyped splay interpreted. Both k-nucleotide variants executed a MIR task satellite despite reporting `fallback=0` (file input makes main task-backed). None belongs in a pure-T0 performance ratio.

## 4. What the profiles establish

### MIR

These are executed counter differences, not inferred timing percentages.
**Correction (§13.1):** root-store/reload counts include work introduced by
the profiler itself. Their original implications below are superseded; they
cannot establish production root traffic. Ordinary helper-call counts still
identify the executed lowering paths.

| Workload/function | Untyped | Typed | Implication |
|---|---:|---:|---|
| bounce benchmark root reloads | 36,815 | 192,627 | Instrumented code only; see §13.1 |
| bounce benchmark root stores | 6,091 | 15,606 | Instrumented code only; see §13.1 |
| bounce `is_truthy` calls | 0 | 20,000 | Typed conditions lose native boolean lowering |
| fannkuch hot-body root reloads | 40,349 | 346,690 | Instrumented code only; see §13.1 |
| fannkuch `is_truthy` calls | 0 | 8,659 | Investigate condition descriptor/proof loss |
| nqueens solve root reloads | 812,501 | 998,885 | Instrumented code only; see §13.1 |
| nqueens numeric-array admissions | 0 | 13,075 | Repeated contract crossings in recursion |
| fasta random-body root reloads | 812,848 | 1,198,838 | Instrumented code only; see §13.1 |
| fasta type checks | 0 | 8,007 | Typed numeric path retains runtime admission |
| fasta `fn_float` / `fn_div` calls | 0 / 0 | 8,000 / 8,000 | Native numeric path is missed |

The relevant source is `lambda/runtime/transpile-mir.cpp`: `mir_numeric_comparison_native_lane`, `mir_profile_emit_condition`, the existing dense-loop/finite-value planners, and typed call/return admission. Native comparison requires the appropriate non-null/error proof. A declared element type alone cannot supply a bounds proof. These locations and the counters identify concrete optimization targets; they do **not** yet prove one patch eliminates all measured overhead.

### T0

- **Fresh record construction/admission:** typed gcbench has **12,866,918 map admissions, 3,222,190 reifications, and 157,865,468 admitted bytes copied**. Typed deriv has **880,000 admissions, 220,000 reifications, and 14,160,000 copied bytes**. Their untyped ports have no map admissions. `eval_map` builds the inferred literal shape, then declared boundaries may build another representation.
- **Repeated proof work without copying:** typed Richards has **2,182,750 admissions**, of which **2,182,300 are storage-compatible read-only validations**. Only 450 reifications copy 34,000 bytes. Calling this a COW-copy problem would be wrong.
- **Checked stores without detachment:** typed primes performs **2,122,050 checked direct stores**, without shared-array copies. The cost remains even on unique storage.
- `interp_coerce_declared_binding` already fast-paths exact int/float/string tags and constructs diagnostic names lazily. It still runs generic boundary work after the array coercion path. `lambda_array_set_checked_impl` repeats contract/lane work on each store; its existing certificates already avoid many copies. Nested stores also materialize key paths. These are the next places to measure, not reasons to remove validation.

## 5. Proposed tuning order

1. **MIR: retain the same useful proofs for declared and inferred values.** Trace bounce/fannkuch's condition descriptors from bounds/finite/error facts through `MirValue`; reuse the existing planners so a proven in-bounds scalar comparison stays native. Preserve null, out-of-bounds and error behavior (S7.1.1v3, S7.7.2–S7.7.4, D2.4.1–D2.4.3). Removing `is_truthy` alone is insufficient if the operand still needs boxing. Add annotation-only controls around these loops before attributing every port difference to typing.
2. **MIR: reuse admitted array and numeric-result facts at call boundaries.** Start with nqueens recursion and fasta's PRNG arithmetic. Reuse full-contract certificates across proven non-invalidating calls; select the native coercion path when the result descriptor proves it. Both generic error/null paths and required conversion semantics must remain. Use existing shared facts under D8.2.4v2/D8.2.5v3; do not introduce a second inference engine.
3. **MIR: reduce root traffic after eliminating redundant helpers.** Recompute live roots around the remaining allocation/re-entry safepoints. Unboxed numeric temporaries need no Item root, but pointer carriers keep their owning boxed root. Verify with GC stress and existing precise-root accounting (D5.3.3); never delete roots based only on the benchmark or restore native-stack scanning.
4. **T0: construct fresh typed record literals into their admitted destination layout.** Carry the declared destination through existing frame/binding plans and `eval_map`, avoiding an inferred map followed by reification when all fields can be admitted correctly at construction. Keep named-record identity, recursion, nullable fields, errors, and snapshot semantics (D3.2.4v4, S9.1.2–S9.1.3). Target gcbench, deriv, binarytrees first.
5. **T0: make successful admission reusable.** Classify a boundary's complete contract once in its existing plan; share the existing admission/certificate helpers, avoid redundant array-plus-generic checks, and recognize exact bool scalars alongside the existing scalar cases. For Richards, prove when the existing storage-compatible record remains admitted instead of revalidating every crossing. Invalidate facts when a write can change the contract or representation (D3.3.4); this is not an inline cache (D8.4.1v2).
6. **T0: preplan checked stores and nested paths.** Reuse resolved declared element/field contracts and static path segments, retaining the checked runtime setter for dynamic or incompatible cases. Unique admitted arrays should not rediscover their lane on each store. Target primes, sieve, quicksort, bounce. Keep the interpreter boxed as required by Ast_Interpreter AI3.

A practical acceptance gate is equal golden outputs and zero forbidden fallback, then at least 15 alternating release pairs per targeted row. For very small rows, enlarge the timed workload with identical repetition on both ports; do not assert equality from one noisy median. Aim for no reproducible typed penalty beyond a small measured noise band, without weakening typed failure semantics. Include negative admission, nullable/out-of-bounds, snapshot/borrow, and forced-GC tests. Existing untyped optimizations must also satisfy those semantics; faster incorrect work is not the target.

## 6. Strict interpreter fix

The runner previously treated rejection identically for INTERP and AUTO, then entered whole-module MIR. Independently, its support scan admitted task-backed MIR satellites even under an explicit interpreter pin. Cached import AST activation had a third route to MIR.

The fix enforces **D8.1.1v15** at each of these boundaries: explicit interp reports `E501` and increments `excluded`, with no execution/fallback; AUTO retains its fallback behavior. A cached support profile remains mode-independent, and strict execution separately validates indexed definitions before accepting it. Synchronous procedures conservatively marked task-capable keep their existing local proof. The formal design and Ast_Interpreter working ledger (AI24) record the user ruling.

**At the strict-pin checkpoint, Queens was rejected before execution.** Its untyped `c + r` / `c - r + 7` store keys failed the scalar-index admission test. The subsequent interpreter support fix is recorded in §9.

Regression coverage includes Queens, a task satellite, an immutable task alias, an imported task module using AST prebuild, and the existing character-range exclusion. Older parity/COW tests that actually relied on fallback now explicitly assert rejection under interp and retain their JIT/AUTO correctness checks.

## 7. Validation and remaining failures

- `make test-lambda-baseline`: **6,180/6,180 passed** after the fix.
- Focused strict-pin tests: **6/6 passed**.
- Full interpreter/JIT differential sweep: **942 matches, 39 explicit exclusions, one mismatch, no timeouts** across 982 scripts.
- The mismatch is **`test/lambda/pipe_filter.ls` crashing under pinned MIR**. It also crashes with the untouched release binary archived before these edits. A debug backtrace reaches `_map_get`/`item_attr` with an invalid key address (`0x500000000000010`). This audit leaves the failure visible; no fallback, golden, or engine workaround is added.
- Because that mismatch blocks full list regeneration, the committed lists receive only an incremental strict-pin update: 29 previously listed entries move to exclusions; no new passing rows are promoted. The existing pipe-filter entry remains, and the differential failure is not reclassified as a passing row.
- Standalone interpreter gtest on the final release: **798/810 passed**. Eleven failures are existing subset entries whose expected files are missing/empty at their listed paths; the other is `DirectNumericNdimIndicesUseArrayNumHelpers`, whose fixture still contains an unsupported index expression. The pre-change release reports `executed=0 fallback=1` for that fixture, contradicting the test's existing zero-fallback expectation. These failures remain visible; no goldens or assertions were weakened to hide them.
- Final release strict-pin regression run: **6/6 passed**. Python harnesses parse successfully; `git diff --check` passes.

## 8. Fifteen-pair MIR confirmation

After rebuilding the final release (including only the pin-policy code changes), reran the four principal regressions plus the watchlist, with one warmup and 15 alternating pairs per row. No test/build/profile workload ran concurrently. Binary SHA-256: `1e2b4f9329c9a2755ae4359e2092c87c633960c2bf72f620c1453e1d7c24a571`. Raw samples: `temp/typed_t0_audit/jit_confirm.json`.

| Benchmark | Untyped ms | Typed ms | Typed change | Typed wins / 15 |
|---|---:|---:|---:|---:|
| bounce | 0.067 | 0.098 | +46.3% | 0 |
| fasta | 0.775 | 0.938 | +21.0% | 1 |
| nqueens | 0.939 | 1.122 | +19.5% | 0 |
| puzzle | 12.719 | 14.139 | +11.2% | 1 |
| fannkuch | 0.267 | 0.291 | +9.0% | 2 |
| microdiff | 32.149 | 33.281 | +3.5% | 6 |
| pidigits | 0.300 | 0.301 | +0.3% | 8 |

All output pairs agree. Bounce, fasta, nqueens and fannkuch retain the direction seen in v50; puzzle also merits follow-up. The pidigits penalty did not reproduce, which illustrates why the smaller five-pair deltas are a watchlist rather than proof of a compiler regression.

## 9. Untyped Queens interpreter support (2026-10-03)

The support scanner accepted a bare untyped binding as a store key, but its arithmetic-expression walk accepted only integer literals and proved integer range-loop variables. Consequently, `free_maxs[c + r] = v` rejected the whole untyped Queens script even though both operands independently had an executable interpreter path. The typed port passed because its key expression had an explicit integer type.

`interp_checked_scalar_index_expr` now composes the existing binding, integer-type, and range-loop admission cases through `+`, `-`, and `*`. These expressions retain their boxed keys through the existing `member_set_cow` / checked typed-array / COW-path setters: no machine conversion or unchecked store is added. Runtime keys and values retain **S7.1.3v2** validation, snapshots retain **S9.1.2**, and only `var` borrows write through under **S9.1.3**. Mask dispatch, character-range exclusions, and the separate N-D coordinate gate remain. The strict pin still follows **D8.1.1v15**.

Regression coverage runs both Queens ports under explicit interp, checks the validated-board golden, and requires `executed=1 fallback=0 excluded=0`. `proc_derived_untyped_index.ls` covers untyped and typed borrowed stores, nested paths, plain-parameter and binding snapshots, and rejected fractional/null/negative/out-of-bounds/vector keys and incompatible typed values against a shared golden under both pinned tiers.

Validation on tree `6a5170563` plus this support fix:

- `make release` succeeds; both Queens ports and all **8/8 focused regressions pass** with the release executable. The mask, character-range, dynamic N-D rejection, task-satellite, and cached-import pin checks are included.
- `make test-lambda-baseline`: **6,170/6,182 pass**. The remaining 12 rendering failures reproduce with the scanner restored to the pre-change source and rebuilt against the same tree. Evidence: `temp/queens_interp_fix/before_render_tests.log`. The old recursive-array test's rejection expectation is replaced by golden checks on interp, jit and AUTO, all passing.
- Full release interpreter/MIR sweep: **944 matches, 39 exclusions, one mismatch, zero timeouts** across 984 scripts; no explicit interp fallback. The mismatch remains the MIR `pipe_filter.ls` crash; the same-tree pre-change executable also exits on signal 11. Every previously excluded corpus row keeps its exclusion verdict. Full list regeneration refuses the mismatch, so the committed partition is retained.
- Raw results are under `temp/queens_interp_fix/`; `git diff --check` passes. Release SHA-256: `bbea26454ee908d48d86fbe86fd11b8dc47b48139889b117de2ecf065f185ded`.

## 10. Implemented tuning (2026-10-03)

The untouched control is a release from `4753e70d3d3f84d4e52b06e97ceeb3a0d5b4f9f9`,
retained as `temp/typed_tuning/lambda-before.exe`, SHA-256
`eb308e1df1272b522f5cf349fdd758fda7a9e6d83e24d9f80532efce892cc559`.
All six proposals in §5 are implemented; no formal semantics or design ruling
changes. The final measurements below supersede the earlier performance
checkpoints, which remain historical evidence.

### 10.1 MIR native numeric and boolean proofs — proposals 1–3

Ordered nullable numeric comparisons now retain the native 0/1/2 boolean lane.
Their descriptors include the operands' absence, and the comparison emitter
publishes the same representation consumed by condition lowering. Missing
numeric-array reads still yield null, including mixed int/float comparisons;
no declaration is treated as a bounds proof (S6.1.2, S7.1.1v3,
D2.4.1–D2.4.3). Integer `abs` uses the shared type-preserving native builtin
path, retaining the int53 boundary and null/infinity/NaN behavior (S4.1.2).

The existing local float-tree guard also recognizes pure `float()` conversions
of numeric typed-array reads. The guarded present arm uses native arithmetic;
the original boxed conversion/error arm remains. `MirValue` records the complete
successful-admission contract across that join. Unconverted nullable reads
prevent publication of a non-null success proof. Fasta therefore avoids the
conversion/division helpers and repeated successful scalar admission, without
removing the negative-index or conversion failure arms (D8.2.4v2/D8.2.5v3,
S7.7.2–S7.7.4).

The full corpus caught a boxed/native mismatch in Navier-Stokes. A pure
`float(i)` over a present numeric scalar does not introduce an error arm, so
it retains the native join. Fallible conversions keep their boxed join;
declarations now require the actual emitted carrier to match before selecting
a native sentinel boundary, as assignments already do (D2.4.1–D2.4.3).
Coverage includes scalar conversion with a nullable read, mixed converted and
unconverted reads at a declaration, and reassignment. Both Navier-Stokes ports
pass after this correction; the failed checkpoint is retained under
`temp/typed_tuning/before_float_fix/` and its timings are superseded below.

The follow-up Towers check exposed the other half of that descriptor rule:
an integer arithmetic initializer explicitly lowered as an `IntLane` must
publish that carrier even when its default AST prediction describes a boxed
result. The declaration now uses the requested emitted lane, retaining its
cold null rejection (D2.4.1–D2.4.3, S7.7.2). A minimal nullable integer
arithmetic declaration covers both success and out-of-bounds failure; Towers
passes on both pinned tiers. The failed checkpoint is retained under
`temp/typed_tuning/before_int_carrier_fix/`.

Array call boundaries reuse maintained full contracts across compatible,
non-invalidating edges. Mutable locals qualify only while their full proof
survives; widened, environment, state, borrowed-parameter and guarded-element
bindings retain their existing admission. Non-identical counted contracts still
check lengths (D3.3.4).

A fresh primitive `fill` can now construct its admitted destination through
`lambda_fill_for_contract`, installing the exact array certificate before
return. Declaration/return consumers own their failure; call consumers use the
existing parameter-error join. The shared call descriptor predicts that join,
so a boxed error cannot be mistaken for a native scalar return (S7.7.3,
D2.4.1). Counted call-argument contracts keep ordinary admission to retain its
failure order. Zero, floating zero, negative, fractional and null counts retain
`fill`'s exact-count behavior.

The fill helper interns its certificate before allocating the result. Immediate
scalar arguments have no GC owner; non-immediate arguments use a precise rooted
span, and allocating admission roots the filled result. Successful carrier
checking/certificate installation cannot allocate. This removes the native
root-frame overhead found in the first recursive nqueens pilot (D5.3.3).
A bare float literal proves its numeric lane but does not install a durable
certificate, so its declaration still performs admission.

The existing CFG liveness pass runs after helper elimination and recomputes root
stores/reloads from the resulting MIR. There is no second liveness planner,
conservative native-stack scan or benchmark-specific root deletion. Pointer
carriers retain their boxed owner roots (D5.3.3).

### 10.2 T0 destination construction and admission — proposals 4–5

Frame planning carries a written record destination through fresh declaration,
assignment, return and single-value branch/block result positions. `eval_map`
evaluates and roots every original field first, then either fills that exact
layout from existing field proofs or transactionally admits the staged values.
Failure returns to the ordinary inferred-map boundary with its original
diagnostic. The shared MIR/T0 adoption gates exclude reordered, computed,
spread, binder-dependent and unsupported storage layouts (D3.2.4v4,
S7.7.1–S7.7.2). Named identity and recursive/null fields remain intact.

Containment coverage also exposed an existing MIR issue: field admission could
return before later initializer expressions ran. MIR now stages all fields
before fallible destination admission, preserving S7.7.1–S7.7.2. The reviewed
DeltaBlue construction change adds nine MIR instructions, confined to
`create_variable` (+4) and `create_planner` (+5); its default budget records that
measured cost. The Windows budget awaits a native measurement and is unchanged.

Binding, parameter and return plans classify the complete written contract once.
They reuse the existing trusted shape/array certificate relation, include an
exact-bool fast path, and avoid a second generic check after completed array
admission. Binder-dependent contracts still use the invocation environment.
The existing runtime representation-proof helper and binder walk are shared,
not copied. No observed-value inline cache is introduced (D3.3.4, D8.4.1v2).
Unsupported field layouts still reify; their existing count tests retain that
requirement.

### 10.3 T0 checked store plans — proposal 6

Assignments retain a precollected AST path, declared leaf contract, exact-index
mask and resolved numeric element/lane. Rooted key spans use the same checked
COW spine writer as MIR, preserving RHS/key/owner evaluation order. Dynamic
keys, incompatible carriers and N-D roots keep the original transactional
checked fallback. Flat stores share the existing mask/exact-integer gate and
setter (S7.1.3v2, S9.1.2–S9.1.3).

After COW preparation, a bounded scalar write into the same certified rank-one
numeric carrier preserves its certificate directly: the admitted value cannot
change its rank, count or lane. Views, static storage, list splices, bad indices
and uncertified owners retain the full checked path (D3.3.3v3). Writes through
`var` borrows and plain-binding snapshots retain their distinct ownership rules.

### 10.4 Correctness checkpoints

Five new MIR fixtures have golden outputs and emission ratchets: nullable
comparisons, integer abs, guarded float conversion, staged record construction,
and typed fill counts/call errors. Existing ratchets are tightened around the
eliminated helpers. T0 admission tests assert eliminated crossings while
retaining incompatible-layout reification; the nested-store test covers 100
writes without repeated graph admission or deep cloning.

The final full baseline is **6,182/6,194**. All **290 forced-GC**, **240 MIR
emission**, **53 optimization** and **20 MIR size-ratchet** tests pass. The
remaining 12 rendering failures are the same failures established before this
tuning (§9); no golden or fallback policy is changed to hide them. Log:
`temp/typed_tuning/complete_baseline_final.log`.

The final debug-tier corpus sweep is **944 matches, 39 explicit exclusions,
one known MIR pipe-filter crash, zero timeouts**, with no explicit-interpreter
fallback. Evidence: `temp/typed_tuning/complete_sweep_final.{tsv,log}`. The earlier
release checkpoint has the same partition. Final release-specific checks and
measurements are recorded below.

## 11. Final release evidence

`make release` succeeds. The final candidate is archived as
`temp/typed_tuning/lambda-after.exe`, SHA-256
`588cb2af85181a9cca3e9b0e1dcff701de936c18a06d923e80e01f5dcad86da5`.
Both executables are optimized releases from the same tree, with only this
working diff separating the candidate from the untouched control. The host is
Darwin arm64/macOS 26.3, on AC power. All timings use a pinned tier; no AUTO
measurement enters this report (D8.1.1v15).

### 11.1 Fifteen-pair targeted timing

One warmup per side, 15 alternating process pairs, execution time from the
script marker, equal normalized output on every pair. Counter profiling,
builds and correctness tests run separately. Same-source rows compare each
**typed script against itself** on the old and new release. The final column
is a separate paired measurement of the current untyped/typed ports; it must
not be computed by dividing across the two campaigns.

Pinned MIR:

| Benchmark | Old typed ms | New typed ms | New/old | New typed/untyped ports |
|---|---:|---:|---:|---:|
| r7rs/nqueens | 1.187 | 1.087 | 0.916× | 1.173× |
| awfy/towers | 0.232 | 0.221 | 0.953× | 0.472× |
| awfy/bounce | 0.089 | 0.058 | 0.652× | 1.052× |
| beng/fannkuch | 0.293 | 0.287 | 0.980× | 1.037× |
| beng/fasta | 0.838 | 0.711 | 0.848× | 1.176× |
| beng/pidigits | 0.311 | 0.313 | 1.006× | 0.993× |
| larceny/puzzle | 12.988 | 13.635 | 1.050× | 1.020× |
| text/microdiff | 33.087 | 33.383 | 1.009× | 1.039× |
| jetstream/navier_stokes | 64.169 | 62.427 | 0.973× | 0.796× |

Pure interpreter:

| Benchmark | Old typed ms | New typed ms | New/old | New typed/untyped ports |
|---|---:|---:|---:|---:|
| r7rs/nqueens | 18.352 | 16.121 | 0.878× | 1.165× |
| awfy/sieve | 2.568 | 1.971 | 0.768× | 1.121× |
| awfy/queens | 7.123 | 5.576 | 0.783× | 1.357× |
| awfy/towers | 16.701 | 13.487 | 0.808× | 1.183× |
| awfy/bounce | 4.587 | 3.871 | 0.844× | 1.122× |
| awfy/richards | 1968.690 | 1703.990 | 0.866× | 1.043× |
| beng/binarytrees | 146.344 | 57.354 | 0.392× | 1.037× |
| kostya/primes | 433.851 | 336.074 | 0.775× | 1.052× |
| larceny/deriv | 278.349 | 113.580 | 0.408× | 1.211× |
| larceny/gcbench | 4263.790 | 1404.770 | 0.329× | 1.115× |
| larceny/quicksort | 105.284 | 83.870 | 0.797× | 1.155× |

Every interpreter diagnostic reports `executed=1 fallback=0 excluded=0`, and
its separately instrumented process has no MIR frame entry. This includes
**both Queens ports**; they now execute the same pinned interpreter tier.

Annotation-only controls repeat identical work: 1,000 times under MIR and 20
under T0. The erased source changes only integer/array/return annotations; both
variants satisfy their golden outputs.

| Control | MIR typed/erased | T0 typed/erased |
|---|---:|---:|
| annotation/bounce | 0.811× | 1.089× |
| annotation/fannkuch | 1.032× | 1.047× |

Raw samples, output hashes, source hashes, binary identity and paired-bootstrap
upper bounds: `temp/typed_tuning/complete_{jit,interp}_{ports,change}.json`.
A one-sided bootstrap upper bound is retained by the shared runner; it is not
an equivalence test. Bounce and fasta improve in all 15 same-source pairs;
their upper bounds are 0.724× and 0.895×. Nqueens has an 8.4% lower median,
but only 10/15 wins and an upper bound of 1.036×, so this campaign does not
establish its speedup. The first checkpoint likewise did not establish it:
counters alone are insufficient timing evidence.
Fannkuch, pidigits and Navier-Stokes deltas remain sample-specific. Puzzle has
a 5.0% slower same-source median (4/15 wins, upper bound 1.078×), and microdiff
0.9% (2/15 wins, upper bound 1.098×). These remain visible watchlist results;
no claim of universal non-regression or performance parity follows.

All 11 same-source T0 targets improve, with one-sided bootstrap upper bounds
below 1.0. That measures the tuning's effect on the typed programs; it does
not establish parity with their untyped ports or annotation-erased controls.

### 11.2 Executed counters

These counts come from separate profile runs of the archived binaries.
**Correction (§13.1):** the MIR root counts measure instrumented programs
whose added calls change safepoint placement. Keep this table as historical
evidence, not as a measurement of uninstrumented root traffic or its reduction.
The T0 admission/copy counters below do not have this MIR instrumentation issue.

| MIR hot function | Old root stores → new | Old root reloads → new |
|---|---:|---:|
| bounce benchmark | 15,606 → 1,320 | 192,627 → 8,465 |
| fannkuch main | 108,936 → 91,612 | 346,690 → 312,042 |
| nqueens solve | 228,981 → 198,725 | 998,885 → 789,885 |
| fasta random_fasta | 202,802 → 172,230 | 1,198,838 → 958,838 |

Bounce's `is_truthy` calls fall 20,000→0 and integer `fn_abs` 1,426→0;
fannkuch's `is_truthy` calls fall 8,659→0. Nqueens has no remaining executed
`lambda_type_check` (2,056→0) or separate numeric-array admission
(13,075→0); 15,131 fills publish their certificate at construction. Fasta's
`fn_float` and `fn_div` calls each fall 8,000→0, with `lambda_type_check`
8,007→7. The producer still performs any required live carrier/count gate;
zero helper calls does not mean validation was removed.

| T0 typed workload | Old map admissions | Old reifications | Old field visits | Old admitted bytes |
|---|---:|---:|---:|---:|
| deriv | 880,000 | 220,000 | 880,000 | 14,160,000 |
| gcbench | 12,866,918 | 3,222,190 | 6,444,380 | 157,865,468 |
| binarytrees | 406,200 | 135,854 | 271,708 | 6,655,484 |
| Richards | 2,182,750 | 450 | 2,450 | 34,000 |

**All four columns are zero in the final candidate for these workloads.**
Primes retains its 2,122,050 checked direct stores, and bounce retains 13,652;
they are real writes whose key/value/ownership rules remain enforced.
Profiles: `temp/typed_tuning/{profiles.json,*.exec.tsv,*.cow.tsv}`.

### 11.3 Release correctness and visible failures

The final release passes **290/290 GC stress**, **240/240 MIR emission**,
**19/19 admission**, **7/7 strict-pin regression** tests and **20/20** new
fixture checks (five scripts × two pinned tiers × normal/forced collection
with freed-memory poisoning). Their logs use the
`temp/typed_tuning/complete_release_*` prefix. Baseline and corpus results are
recorded in §10.4.

Release configuration size ratchets pass **17/20**. Three failures reproduce
with the untouched control, with the same counts: hoisted JS module-write main
76→87, nested COW module 166→169, and exact JS collection main 12,573→12,627.
Evidence: `control_release_ratchet.log` and
`complete_release_ratchet.log`. Their budgets are unchanged. The ordinary
baseline configuration passes all 20; the reviewed nine-instruction DeltaBlue
construction delta is the only budget increase in this tuning.

### 11.4 Full paired corpus

The final release scan uses one warmup and five alternating pairs per port,
with a 15-second process limit. All 63 MIR pairs launch; 61 have equal output.
Pure T0 has 54 comparable pairs. Every accepted T0 diagnostic reports
`executed=1 fallback=0 excluded=0` and no executed MIR frame entry. Both binary
hashes remain stable in every campaign (D8.1.1v15).

| Tier | Equal-output pairs | Typed slower | More than 5% slower | More than 10% slower | Geometric mean typed/untyped |
|---|---:|---:|---:|---:|---:|
| Pinned MIR | 61 | 11 | 5 | 1 | 0.610× |
| Pure T0 | 54 | 40 | 30 | 15 | 1.008× |

These are scan medians, not demonstrated regressions or equivalence bounds.
Small rows vary between campaigns; the 15-pair targeted comparisons and
annotation-only controls in §11.1 are the stronger evidence for those rows.
The geometric mean also compares ports, which may differ beyond annotations.

Spectralnorm and matmul still produce unequal outputs in both tiers, as they
do in the untouched control, so neither enters the comparison. T0 additionally
excludes seven rows through its execution gate: knucleotide, triangl and
text_search are explicitly rejected in both ports; typed splay is explicitly
rejected while its untyped port runs. Untyped collatz and diviter exceed the
15-second limit, as do both log_pipeline ports. No timeout is counted as a
strict-interpreter rejection or a timing result.

Raw samples and diagnostics:
`temp/typed_tuning/complete_{jit,interp}_corpus.json`; aggregate counts and
excluded-row details: `temp/typed_tuning/complete_corpus_summary.json`.

### 11.5 Reproduction

The permanent driver is `test/benchmark/run_typed_audit.py`. It reuses the
existing paired runner, release gate, output normalization and uncertainty
calculation. `--control` selects a same-typed-source comparison;
`--annotation-controls` creates identical repeated sources with golden files.
Interpreter eligibility is checked in an independent process, outside timing.
Each campaign checks that its binary hashes remain unchanged through completion.

```bash
python3 test/benchmark/run_typed_audit.py --tier jit --pairs 15 \
  --only r7rs/nqueens,awfy/bounce,beng/fannkuch,beng/fasta \
  --annotation-controls --output temp/typed_tuning/reproduce_jit.json
python3 test/benchmark/run_typed_audit.py --tier interp --pairs 15 \
  --only larceny/gcbench,larceny/deriv,beng/binarytrees,awfy/richards,kostya/primes \
  --output temp/typed_tuning/reproduce_interp.json
python3 test/benchmark/run_typed_audit.py --tier interp --pairs 15 \
  --control temp/typed_tuning/lambda-before.exe \
  --only larceny/gcbench,larceny/deriv,beng/binarytrees,awfy/richards,kostya/primes \
  --output temp/typed_tuning/reproduce_change.json
```

## 12. What remains slower

The six proposals are implemented, and the broad 3× T0 record-construction
penalties are removed. **They do not establish that every typed program is
faster than its untyped port.** Targeted pure-T0 ports retain overhead,
and annotation-only T0 controls retain approximately 9% for bounce and 5% for
fannkuch. MIR nqueens and fasta also retain a port penalty in the 15-pair run.

The counter evidence narrows the residual work: it is no longer fresh-record
reification in the four record targets, or nqueens's repeated array admission.
T0 still checks live carriers/certificates at boundaries and keys/values/COW
ownership at checked stores; preplanning removes classification and path
construction, not those semantics. The previously reported MIR root-reload
gaps (fannkuch 312,042 versus 40,349; fasta 958,838 versus 812,848) include
profiler-induced safepoints. They cannot justify a production root-traffic
diagnosis. Repair the measurement first; retain precise ownership and live
representation checks (D3.3.4, D5.3.3).

Further tuning should isolate those remaining checked kernels and boxed joins
with annotation-only repeated workloads and native samples. Every new reduction
must keep nullable/OOB behavior, error joins, exact index validation and
snapshot/borrow semantics (S7.1.1v3, S7.1.3v2, S7.7.2–S7.7.4,
S9.1.2–S9.1.3). The current implementation reports its residual penalties
instead of redefining unequal work or permitting fallback as a speed result.

## 13. Further tuning proposal — 2026-10-03

**Status: implemented and validated (§14.6).** The target is per-workload typed/untyped
parity for equal work in each pinned tier. Implementing §5 delivered large
gains, but completion of those changes is not completion of that target.

This follow-up inspected source at `90decfa0e` and sampled the existing
optimized `lambda.exe`. Its SHA-256 remains
`588cb2af85181a9cca3e9b0e1dcff701de936c18a06d923e80e01f5dcad86da5`,
identical to §11's final candidate. No new timing campaign is substituted for
§11: native sampling and MIR dumps here diagnose those results.

The largest established gains are MIR bounce (35%) and fasta (15%), and T0
gcbench (67%), binarytrees (61%) and deriv (59%), measured against the same
typed source on the previous release. Residual medians from the 15-pair port
comparison are MIR nqueens +17.3%, fasta +17.6%, bounce +5.2%, fannkuch +3.7%;
T0 Queens +35.7%, deriv +21.1%, towers +18.3%, nqueens +16.5%, quicksort +15.5%.
The broader five-pair scan also flags T0 prettier_ast (+38.4%) and
three_way_merge (+27.8%); confirm these with longer paired runs before making
them hard regression gates. A good corpus geometric mean does not discharge
an individual regression.

Port differences still matter. Fasta's typed PRNG adds an explicit `int(...)`
cast absent from the untyped source. Establish its contribution with the
typed source and its annotation-erased twin, keeping the cast, then a separate
pair varying only the cast. Preserve `var` borrows and identical outputs.
The existing annotation-only controls already show T0 overhead of 8.9% for
bounce and 4.7% for fannkuch; MIR bounce is faster by 18.9%, while fannkuch's
3.2% median penalty remains too uncertain to declare parity or a firm regression.

### 13.1 P0 — repair MIR profiling before tuning root traffic

**Confirmed root cause:** `em_profile_before_call` in
`lambda/runtime/mir_emitter_shared.hpp` directly inserts a call to
`lambda_exec_profile_note_call` without registering its call-site effects.
The registry already declares that hook `JIT_EFFECT_NO_GC`, non-reentrant,
and number-stack preserving. However, `em_root_call_may_collect` consults
recorded call sites, not import names, and correctly treats a missing record
as potentially collecting. Markers inserted before root finalization therefore
create extra stores/reloads. Typed and untyped programs have different marker
placement, so this also distorts their relative root counts.

The same release's fannkuch main function shows this directly:

| Emitted root operations in main | Untyped, profiling off | Typed, profiling off | Untyped, profiling on | Typed, profiling on |
|---|---:|---:|---:|---:|
| Stores | 81 | 119 | 142 | 157 |
| Loads | 44 | 48 | 250 | 705 |

These are **static MIR instruction counts**, not executed counts or timing
estimates. In particular, 48 versus 44 does not prove equal hot-loop traffic,
but it invalidates using the historical 312,042/40,349 executed-counter ratio
as an uninstrumented cost ratio.

Route inserted diagnostic calls through the shared call-site effect recording
mechanism. Do not special-case profiler names in GC liveness, or relax the
conservative treatment of genuinely unknown calls. Add an emission regression
that compares production safepoints and root operations with profiling on/off,
ignoring diagnostic calls themselves; include dense-loop markers as well as
helper markers. Recollect executed root counts only after that test passes.
This repairs measurement; it should not be credited as a speedup of ordinary
unprofiled execution. Precise rooting remains required by D5.3.3.

### 13.2 P1 — resolve array proof identity once

**Evidence:** a three-second native sample of typed T0 Queens records 220
exclusive observations in `lambda_array_contract_compatible`, 65 in
`lambda_array_contract_canonical`, and 26 in `lambda_array_contract_info`.
The untyped sample has none of those symbols in its top-of-stack table, whose
reporting threshold is five observations. This identifies repeated contract
analysis as real executed work, independently of MIR counters.

`runtime_array_rep_cert_intern` already shares certificates for structurally
equivalent contract spellings. But `lambda_array_rep_proves(value, Type*)`
compares the certificate's first contract pointer with each boundary's target
pointer. Equal types spelled at different declarations can miss identity and
re-enter structural compatibility on every crossing/store. The existing
`lambda_array_contract_canonical` only unwraps the type; it does not intern
structurally equal declarations. `lambda_array_rep_proves_cert` already offers
the certificate-identity fast arm needed here.

Resolve each immutable boundary/store plan's contract to a shared proof
identity in the owning execution context, then reuse the certificate-based
helper. Keep live carrier, rank and counted-axis validation; keep structural
compatibility as the cold path. Avoid embedding heap-local certificates into
an AST reused across contexts: persistent plans carry stable type metadata,
with context-owned resolution/activation. This is shared contract metadata,
not mutable dispatch feedback (D8.4.1v2).

Acceptance: equivalent declarations no longer trigger a structural type walk
on every warm operation. Test named versus structural types, distinct contract
spellings, context reuse, counted axes after resize, and representation-changing
mutations. Certificate identity alone never authorizes a stale carrier
(D3.3.3v3/D3.3.4). Primary targets: Queens, towers, nqueens and typed stores.

### 13.3 P2 — finish fusing typed numeric fill

**Evidence:** MIR nqueens has already eliminated separate array-admission and
type-check calls. Native sampling nevertheless records 228 exclusive samples
in `lambda_fill_for_contract`, in addition to 253 in `fn_fill`. The active
main thread has 2,548 observations: the wrapper alone occupies about 9% of this
short sample. This does not establish that it explains the entire 17.3% port
penalty. The untyped path calls `fn_fill` directly.

The current wrapper still normalizes/count-checks, resolves a certificate,
calls generic `fn_fill`, then verifies the fresh carrier and publishes its
proof. `fn_fill` repeats count conversion and dispatches on the value type.

Use P1's resolved contract and a shared primitive fill kernel extracted from
`lambda/runtime/lambda-vector.cpp`. Both generic fill and typed construction
should reuse that kernel. The typed arm performs required count/value checks
once, allocates the promised carrier, fills it, and attaches the certificate
without rediscovering the representation it just built. Retain the generic
route for dynamic or incompatible inputs; do not create separate copied loops
for each lane. Resolve float and other supported scalar producers as well as
the current immediate int/bool case where static facts allow it.

Acceptance: nqueens no longer pays a generic-fill-plus-admission wrapper on
its exact path, and the same-source untyped fill does not regress. Cover zero
length, count overflow/allocation limits, exact integral and fractional counts,
negative/null/error inputs, list splicing, numeric conversions and counted
contracts. Any allocating path keeps non-immediate arguments and results
precisely rooted; preserve boundary failure order (D5.3.3, S7.7.2–S7.7.4).

### 13.4 P3 — make an already-proved scalar store cheap

The previous tuning already retains a certificate after a successful bounded
store. It does **not** remove the work before that store:
`lambda_array_set_checked_impl` still validates the value, proves the owner's
contract, checks its physical representation, opens three root slots, and
prepares a candidate before reaching its direct-store arm. T0 preplanning
currently supplies lane and element hints, not a complete successful action.

Add a shared nonallocating arm before candidate/root-frame setup for an exact
key and value, a matching live certificate, and ordinary writable rank-one
numeric storage whose ownership permits an in-place write. Reuse
`array_num_store_admitted` and existing ownership predicates. An exact scalar
operation that cannot allocate needs no extra helper-owned root frame; any
conversion, detach, admission or diagnostic continues through the rooted path.
Do not mark the whole checked setter `NO_GC` merely because its fast arm is.

Precompute immutable scalar boundary actions too: use the existing
`lambda_static_boundary_relation` where it proves a producer already satisfies
the full destination contract. Keep dynamic/error-capable producers checked.
This makes the plan describe the remaining work, instead of only classifying
the type before running the same generic checks.

Acceptance: unique admitted arrays perform constant guarded store work without
structural type walks or root-frame setup on the nonallocating arm. Preserve
mask/N-D dispatch, list splicing, bounds/key errors, views/static storage,
conversion, COW snapshots and `var` borrowing; do not turn an unproved store
into an unchecked one (S7.1.3v2, S7.7.4, S9.1.2–S9.1.3). Targets: Queens,
sieve, primes, quicksort and the annotation-only bounce/fannkuch T0 controls.

### 13.5 P4 — use known record layouts throughout T0 execution

**Evidence:** deriv's old reifications and admitted bytes are zero, but native
sampling still finds `lambda_value_rep_proves_contract` (61 exclusive samples),
`lambda_type_nonnull_map_contract` (43) and `lambda_array_contract_info` (23).
`plan_destination` records a destination map; `eval_map` still reclassifies
each field's contract at construction. Ordinary `AST_NODE_MEMBER_EXPR` also
uses `fn_member` and map lookup even when the declared record layout is known.

Extend the existing construction plan with immutable per-field boundary actions.
Reuse boundary classification and static producer proofs rather than adding
a second contract analyzer. Evaluate the original field expressions in their
required order, then admit only fields whose runtime values need it. Preserve
conversion/error behavior and transactional publication (S7.7.1–S7.7.4).

For statically known fields, plan a shape-guarded access using existing field
metadata and the shared field decoder (`map_shape_field_to_item` or its common
kernel). On an exact compatible live shape, avoid repeated name hashing and
field lookup; on a miss, call `fn_member`. Keep nullable receivers, named
identity, symbol keys and packed field representations correct. Use compile-
predicted guards, not observed-shape inline caches (D3.2.4v4, D8.4.1v2).

Acceptance: repeated construction no longer invokes general contract
classification for statically proved fields, and known-field reads avoid name
lookup. Test recursive/nullable records, shape misses, mixed numeric fields,
errors, snapshot aliases and forced GC. Targets: deriv, gcbench, binarytrees,
Richards and prettier_ast. The existing exact trusted-map fast return already
exists in `runtime_map_contract_relation_cached`; adding it again is not work.

### 13.6 P5 — finish MIR proof propagation after P0

The native fannkuch samples spend their time in anonymous JIT PCs; they do not
identify a remaining runtime helper as the cause of its small median gap.
Uninstrumented MIR still contains repeated bounds/null/value guards in the
typed shrinking two-index reversal loop. Audit those guards against the
existing range and dense-loop facts before proposing any deletion.

Where the existing planner can establish `0 <= lo <= hi < len`, carry that
proof through both array reads/stores and loop joins. Where it cannot, retain
the checks. Preserve invalidation across resize, mutation and re-entry; an
element annotation is not a bounds or non-error proof. Maintain the same
native value descriptors for inferred and declared expressions using the
existing MIR planners (D8.2.4v2–D8.2.6, D3.3.4).

For fasta, first isolate the explicit cast. Both ports still execute 23,796
`fn_index` calls and 23,796 `fn_lt` calls in the profiled random-generation
body, so improving that common path may help both without explaining the
typed gap. Trace the complete array/result contracts before selecting a native
read/comparison; do not attribute common work to annotations.

After P0, measure production safepoints and roots again. Reduce boxing and
root traffic only where eliminated helpers or proven native lanes justify it.
Retain precise owners for pointers and null/OOB/error joins (D5.3.3,
S7.1.1v3, S7.7.2–S7.7.4). Acceptance requires annotation-only timing evidence,
not simply fewer instructions in one MIR dump.

### 13.7 Completion gate and evidence

Implement P0 first, then P1/P2 for the principal MIR target, P3/P4 for broad
T0 parity, and P5 against corrected measurements. Recheck both tiers after
each shared-runtime change. No formal ruling changes are proposed.

- Keep separate tables for existing ports, annotation-only pairs and
  same-source old/new comparisons. Extend annotation controls to nqueens,
  Queens, fasta and representative record/traversal cases; preserve casts,
  algorithms, inputs and borrowing. Include golden files with new scripts.
- Require equal normalized outputs and pure-T0 eligibility, including both
  Queens ports. Exclusions/timeouts remain visible. Explicit interp cannot
  fall back to JIT (D8.1.1v15); AUTO is outside the comparison.
- Run at least 15 alternating pairs per target, preferably 21, in two
  independent release campaigns. Repeat small workloads identically until
  timed execution is roughly 100 ms–1 s; profile separately from timing.
  Extend the existing paired runner to report a two-sided uncertainty interval.
- Aim for typed median no greater than untyped **on every annotation-only
  target**, with the paired 95% upper bound at most 1.02 as a measurement
  tolerance. A reproducible positive penalty still fails the goal, even below
  2%; an interval too wide to decide remains inconclusive. Report practical
  equivalence within tolerance separately from demonstrated speedup. Never
  pass an individual regression using the corpus geometric mean.
- Check same-source untyped performance as well: slowing the control is not
  parity. Reconfirm puzzle and microdiff, which have visible old/new watchlist
  deltas. Run focused boundary/store/GC tests and the required Lambda baseline;
  retain the existing documented failures rather than hiding them.

New evidence is in `temp/typed_followup/`: `native_profiles.json`,
`native_summary.json`, `*.sample.txt`, uninstrumented `*.mir`, instrumented
fannkuch dumps and `profiler_perturbation.json`. Native sampling ran with
`LAMBDA_TIER=jit` or `interp`, with execution/COW counters disabled. All ten
profiled executions completed successfully and all five typed/untyped output
pairs agreed. Queens/deriv/nqueens were repeated identically to obtain useful
samples; the annotation-only fannkuch pair repeats 20,000 times. Sampled
execution times are not benchmark results. Some release symbols and JIT PCs
remain unresolved, and idle worker/main threads must not enter CPU-cost
denominators; these samples locate work, not exact savings promised by a patch.

## 14. P0–P5 implementation and validation

The second implementation preserves the §13 acceptance gate. Code completion
alone does not demonstrate performance parity. Measurements below must use a
release host with pinned MIR or strict interp, and compare equal normalized
outputs; no AUTO timings enter the audit (D8.1.1v15).

### 14.1 Implemented work

| Scope | Result |
|---|---|
| P0 | Diagnostic call markers now record their existing nonallocating, nonreentrant effects through the shared emitter. The new profiler parity regression compares output, static root slots, root stores, safepoints and MIR memory operations with profiling enabled and disabled. Unknown call effects remain conservative (D5.3.3). |
| P1 | A heap-owned pointer-key table retains every contract spelling and resolves structurally equivalent spellings to one certificate. Warm boundaries use certificate identity and live carrier/count checks. Immutable AST plans retain Type metadata, with no heap-local certificate embedded in them (D3.3.3v3, D3.3.4, D8.4.1v2). Native sampling identified SipHash overhead on these trusted internal addresses; the table uses the existing pointer mixer through a parameterized shared hashmap helper. |
| P2 | Generic and typed fill share one primitive construction kernel. The typed exact arm normalizes the count once, captures source scalar bits before allocation, constructs the promised lane and publishes its certificate. T0 destination plans reach this same arm. Certificates resolve primitive-value proof once. Zero/count-limit, incompatible, list, counted and literal-element cases retain checked admission (S7.7.2–S7.7.4, D5.3.3). |
| P3 | Unique, writable ordinary numeric arrays take a bounded, nonallocating admitted scalar-store arm before helper-owned roots/candidate setup. T0's immutable primitive destination action avoids another contract lookup there. Detach, conversion, views, masks, N-D and list paths retain the existing rooted setter. Scalar declaration/assignment plans skip a boundary only when shared static proofs cover the producer and exclude defects (S9.1.2–S9.1.3, D3.3.3v3). |
| P4 | Known record construction carries immutable per-field boundary actions. Known member reads guard the predicted live shape and use the shared packed-field decoder, falling back to ordinary lookup on a miss. T0 also plans numeric-index decoding from a declared array contract, with live lane/rank/view and bounds guards (D3.2.4v4, D8.4.1v2, S7.1.1v3). |
| P5 | The existing finite/dense-loop planners now cover a shrinking two-index tail, retaining invalidation across mutation/reentry. Total explicitly contracted array producers can supply guarded result witnesses across erased locals, with rebind checks. OOB/null and defect joins keep their actual carriers; inferred float returns cannot erase absence (D8.2.4v2–D8.2.6, D2.8.2–D2.8.3). The runner includes a fasta cast/annotation factorial control. |

The residual T0 nqueens penalty justified two additional scalar actions within
this scope. Call entry tests whether an argument is an error before classifying
its error contract. Inferred or declared int arithmetic uses the existing
checked integer kernel after live Item-kind guards, with generic fallback for
out-of-band results, poison, absence and errors. No copied arithmetic kernel
or observed-value specialization was added (D3.3.2v2, D8.4.1v2).

The same-source MIR control exposed duplicate admission after an implicit
untyped parameter's error guard. Once that guard excludes error, the remaining
`any \\ error` boundary is identity; direct calls now retain the guard and omit
the second runtime check (S11.4.3, D8.3.2). Typed fill also lacked import value
classes: its metadata/site pointers are non-GC metadata and its result is always
a container or defect, with no scalar home. Its registry rows now state those
facts while retaining allocating, conservative fallbacks (D5.3.3).

For a statically proved nonnullable, nondefecting int count and plain uncounted
primitive numeric destination, MIR preserves the native count lane into the
shared fill action. This removes count boxing and repeated numeric/contract
classification. Negative counts and poison still use the original diagnostic
path; certificate resolution stays in the owning execution context, and the
primitive construction loop remains shared (D2.4.1, D3.3.3v3).

The expanded annotation controls identified another P4 construction miss in
prettier_ast: fresh two-field record arguments did not inherit a known callee's
parameter layout. Release T0 profiling counted 877,312 reifications, 1,754,624
visited fields and 42,988,288 copied bytes per original workload; its peak RSS
was 533 MB, versus 54 MB after annotation erasure. Destination planning now
resolves known positional/named arguments with the shared argument resolver
and reuses the existing record-construction action. It does not turn an
argument fill into an early checked boundary: failed construction still falls
back to ordinary admission at call entry (S7.7.2–S7.7.4). The diagnostic rerun
has zero reifications, field visits or copied admission bytes and 49 MB peak
RSS, with equal output and 405,560,741 interpreted nodes in both variants.
These counters and memory figures locate the repair; paired release timing
remains the acceptance gate (D3.2.4v4, D8.4.1v2).

### 14.2 Correctness findings and regression coverage

Fill's intrinsic invalid-count/allocation-size error was missing from effect
metadata. The registry and shared AST/MIR defect-origination classifier now
record it. This intentionally preserves checked error joins that the old
lowering could erase (D6.1.3, S7.7.2). Emission review of
`tune4_typed_array_guard` found 67 additional module instructions; its instruction
budget is 1301 rather than 1234. Guarded-load/store and root-frame budgets are
unchanged. `gc_effect_forward_allocating` now expects the five live roots of its
checked caller join rather than the old one-root unchecked return.

An arithmetic boundary regression exposed a second MIR proof defect: the
float return-lane predicate accepted an int success type without rejecting its
carried error. It now rejects defect-capable arithmetic joins before choosing
a native return lane (D2.8.3). The regression covers the actual returned error,
not just an emission count.

Adding immutable call metadata enlarged AstCallNode. Postfix propagation can
morph its unary allocation into a call, so that allocation now reserves the
larger of the two actual node sizes. Parameter binder-site plans also retain
the binder stored on TypeParam; a generic type-graph walk must never inspect
that compact wrapper as a complete contract (S11.4.8v2).

Fresh argument planning also exposed two nominal-identity shortcuts. A plain
map must not adopt a nominal object's layout, and storage compatibility cannot
establish nominal identity. Both shared checks now retain the nominal-base
relation. Conditional inference also joins branches with different nominal
records rather than certifying both with the first branch's shape (S11.3.1v2).

Aggregate user pipe calls now use the existing rooted dynamic-call dispatch
with a virtual first argument, including borrowed homes and suspension handling
(S10.1.2v4, D5.1.3). Shared argument proofs and T0 destination plans resolve
that receiver to its actual parameter position. The ownership interval walk
uses the same resolver for receivers and named arguments: ignoring either
could misclassify a mutating borrow as a readonly observer and omit a snapshot's
share mark (S9.1.2). Regression coverage checks fractional and full-width
record fields, four-argument transport, nominal rejection/inheritance, borrowed
snapshots, immutable-borrow diagnostics, self-tail injection and an async call.

`typed_tuning_boundary_guards.ls` and its golden cover exact/fractional/negative,
null/error and huge fill counts, numeric conversion, bool and empty fills,
counted/literal contracts, list splicing, scalar OOB rejection, COW snapshots,
recursive/nullable records, shape misses, arithmetic overflow/poison/defects and
absent arithmetic. Constrained declaration tests preserve the current
**base-only** interim rule of S11.4.6; this performance change does not implement
predicate enforcement at those boundaries.

New MIR fixtures and goldens cover shrinking-loop bounds, resize invalidation,
array-result witnesses, OOB absence, rebinding, native fill count poison and
implicit untyped argument defects. The item representation test
retains 80 equivalent contract spellings, checks warm reverse lookup and verifies
that a separate execution heap has a separate identity. The boundary fixture
also runs in the forced-GC suite. Python controls test annotation erasure and
the paired two-sided uncertainty interval.

### 14.3 Argument-layout candidate evidence (superseded)

This candidate's validation is archived under `temp/typed_tuning2/`. Preflight
and interrupted campaigns are diagnostic evidence, not the final acceptance
results. The prior release control is `lambda-before.exe`; source revisions and
binary hashes are recorded by each completed paired campaign.

The merged argument-layout candidate passed the complete Lambda baseline:
6,209/6,209, with the input prerequisite also passing 2,104/2,104
(`baseline_argument_complete.log`). The previously recorded twelve render
failures are passing in this merged tree. Release-host checks passed emission
243/243, forced GC 294/294, optimizer 53/53 and nine focused contract/diagnostic
tests; the debug baseline also covers the optimizer's debug-only fault test.
The async pipe fixture separately passed release execution with collection at
every allocation and freed-memory poisoning.

The release candidate is `lambda-argument-final.exe`, SHA-256
`bffadb01f2a7f6f817624d3c538f40653eab79356c4090f58cb8569e64e0c408`.
Its production source corpus is
`066a7863a8214fa6732345d659f2f49a6b29a91d7b667b6dcae4eecf03b3541f`,
recorded in `argument_final_manifest.json`. `final_*` campaigns use this frozen
candidate; the earlier `completed_*` campaigns belong to their archived
candidate and are retained separately.

Corrected executed counts in `argument_corrected_roots/result.json` compare
the typed source with its annotation-erased twin, once per original workload.
Every pair first passes output/static-root/safepoint parity with profiling off
and on (P0, D5.3.3).

| Workload | Erased root stores | Typed root stores | Erased root reloads | Typed root reloads | Erased helper calls | Typed helper calls |
|---|---:|---:|---:|---:|---:|---:|
| fannkuch | 13 | 10 | 9 | 9 | 16 | 16 |
| nqueens | 162,257 | 114,870 | 122,274 | 149,154 | 61,206 | 39,890 |

These executed counters locate remaining traffic; they are not timing ratios.
The two final broad campaigns use 21 alternating release pairs and identical
repetitions targeting one second. The union of annotation-only rows with a
median above one or a paired two-sided 95% upper bound above 1.02 is assigned
two independent 41-pair follow-ups targeting three seconds. The broad results
remain visible rather than being replaced by those precision runs.

#### Broad annotation-only campaigns

Each cell is typed/erased median ratio and paired two-sided 95% interval.
Ratios below one favour typed execution. The six selected precision cases
remain inconclusive against the interval gate in these broad campaigns; their
follow-ups are reported separately. All outputs, source hashes and binary
hashes passed, and every T0 diagnostic reports zero fallback/satellites.

| Workload | MIR A | MIR B | T0 A | T0 B |
|---|---|---|---|---|
| bounce | 0.7871 [0.7232, 0.8156] | 0.7934 [0.7721, 0.8460] | 0.8839 [0.8317, 0.9333] | 0.8671 [0.8410, 0.9140] |
| fannkuch | 0.9752 [0.9199, 1.0512] | 0.9708 [0.9101, 1.0294] | 0.9479 [0.8962, 1.0066] | 0.9537 [0.9012, 0.9900] |
| nqueens | 0.9610 [0.9177, 1.0288] | 0.9692 [0.9102, 1.0121] | 0.9523 [0.9257, 1.0128] | 0.9674 [0.9273, 1.0204] |
| Queens | 0.2711 [0.2552, 0.2912] | 0.2861 [0.2614, 0.3023] | 0.9618 [0.9255, 1.0010] | 0.9851 [0.9450, 1.0141] |
| fasta | 0.6014 [0.5751, 0.6587] | 0.6166 [0.5801, 0.6521] | 0.9812 [0.9615, 1.0003] | 0.9672 [0.9478, 1.0098] |
| deriv | 0.2834 [0.2646, 0.3066] | 0.2711 [0.2581, 0.2836] | 0.8616 [0.8382, 0.8929] | 0.8351 [0.8054, 0.9248] |
| towers | 0.5302 [0.4975, 0.5645] | 0.5234 [0.4891, 0.5684] | 0.9670 [0.9411, 0.9898] | 0.9259 [0.8956, 1.0340] |
| sieve | 0.8216 [0.7384, 0.9264] | 0.8288 [0.7704, 0.8655] | 0.9504 [0.9342, 0.9763] | 0.9600 [0.9162, 0.9815] |
| primes | 0.9280 [0.8889, 1.0617] | 0.9721 [0.9102, 1.0141] | 0.9934 [0.9617, 1.0255] | 0.9651 [0.9307, 0.9876] |
| quicksort | 0.4931 [0.4612, 0.5219] | 0.4791 [0.4485, 0.5155] | 0.8917 [0.8533, 0.9192] | 0.8944 [0.8505, 0.9282] |
| binarytrees | 0.6797 [0.6415, 0.7256] | 0.6804 [0.6436, 0.7151] | 0.9210 [0.8917, 0.9523] | 0.9111 [0.8789, 0.9703] |
| gcbench | 0.5016 [0.4668, 0.5217] | 0.4859 [0.4639, 0.5221] | 0.9367 [0.9154, 0.9615] | 0.9467 [0.9225, 0.9666] |
| Richards | 0.2485 [0.2365, 0.2540] | 0.2449 [0.2335, 0.2541] | 0.8520 [0.8430, 0.8593] | 0.8670 [0.8508, 0.8778] |
| prettier_ast | 0.4175 [0.4053, 0.4460] | 0.4352 [0.4011, 0.4490] | 0.9555 [0.9485, 0.9661] | 0.9566 [0.9452, 0.9639] |

#### Precision follow-ups and annotation parity

Each selected row has two independent 41-pair campaigns, with identical
repetitions targeting three seconds. All full outputs and provenance checks
passed; strict T0 diagnostics report zero fallback and zero MIR satellites.
The paired intervals below resolve the wide broad-campaign intervals.

| Tier | Workload | C ratio [95% interval] | D ratio [95% interval] |
|---|---|---|---|
| MIR | fannkuch | 0.9771 [0.9664, 0.9826] | 0.9709 [0.9641, 0.9772] |
| MIR | nqueens | 0.9704 [0.9546, 0.9787] | 0.9669 [0.9600, 0.9757] |
| MIR | primes | 0.9583 [0.9528, 0.9719] | 0.9746 [0.9564, 0.9819] |
| T0 | nqueens | 0.9793 [0.9674, 0.9882] | 0.9765 [0.9680, 0.9902] |
| T0 | towers | 0.9615 [0.9539, 0.9684] | 0.9659 [0.9602, 0.9690] |
| T0 | primes | 0.9584 [0.9487, 0.9767] | 0.9662 [0.9541, 0.9770] |

All fourteen annotation-only targets meet the per-workload gate in both
pinned tiers across two independent campaigns: median no greater than one
and paired two-sided 95% upper bound at most 1.02. Fannkuch, Queens and fasta in T0
are practical equivalence within tolerance; the other 25 tier/workload
combinations demonstrate a speedup in both acceptance campaigns. This claim
is for the matched annotation controls; port and same-source old/new results
remain separate.


**Untyped-control regression found during final validation**

The first same-source untyped MIR campaign exposed a quicksort penalty of
1.2184×, paired 95% interval [1.1602, 1.2544], over 41 equal-output pairs.
That campaign was stopped; `final_same_untyped_jit.json` is a partial report
and is not completion evidence. The annotation-only results above remain
measurements of their frozen binary, but cannot by themselves establish that
the control was not slowed.

Separate release sampling and uninstrumented MIR isolated the defect: the
nullable integer ordered-comparison emitter refused its existing signed
predicate, despite the caller already testing and absorbing null under
S6.1.2. This added two band checks, float conversions and a double predicate
to each comparison. Erased quicksort's partition grew 504→576 instructions
with four conversion helper sites; its sortedness check grew 153→188 with
two. No algorithm, input or borrow changed.

The shared emitter now accepts nullable lanes. Signed order with the existing
NaN exclusion implements the non-null int domain (S4.1.2, D2.2.2), and the
caller still publishes the bool-null marker for either absent operand
(S6.1.2, S7.1.1v3). The new `nullable_int_ordered_pair` MIR fixture checks
all 81 pairs of finite endpoints, signed infinities, NaN and out-of-bounds
null across `<`, `<=`, `>` and `>=`, with full goldens and a prohibition on
float comparison/conversion in the pair emitter. Both pinned modes pass.
Completion required repeating annotation parity and same-source controls
after this fix; §14.6 records those completed comparisons. The earlier frozen
binary is retained as diagnostic evidence.


The final lowering also addresses the two other sources of redundant work:

- The shared lowering hook now supplies demand to the language producer
  before emission (D8.2.6). Ordered int/float comparisons used only as
  conditions produce the canonical truth value directly, with null mapped to
  false (S3.1). Ordinary expression consumers still receive the 0/1/2 nullable
  bool lane (S6.1.2). Nullable bool branch normalization uses `lane == 1`.
- The existing certified integer-array equality helper is shared with ordered
  comparisons and a sealed non-null integer local or literal. Its live
  unsigned bounds guard proves that a loaded element cannot be null; the
  signed predicate retains NaN exclusion. The existing fallback owns the
  absent-read result, and effectful/unproved operands remain on their original
  path (S4.1.2, S7.1.1v3, D2.6.2–D2.6.3).
- Native integer stores now exclude only `INT_LANE_NULL`, using a 64-bit
  `BEQ`. Infinity and NaN are valid integer storage lanes under D2.6.3; a
  finite-range gate needlessly sent them through checked setters. The cold
  setter still owns null rejection or nullable-array demotion (D2.6.2).

The pair fixture checks array-read values and branches as well as nullable
parameters, including float branch conversion, and adds infinity/NaN stores,
COW snapshots and failed null writes. The existing `tune17b_int_lane_guard`
fixture now asserts the full-width null exclusion rather than the obsolete
finite-range gate, with zero-store and absent-read failure goldens. No
formal ruling changes or native-stack GC scanning are introduced.

### 14.4 Nullable-comparison candidate evidence (superseded) — 2026-10-04

The §14.4 engine source corpus is
`883ca97970a6aec54966598b1d0fe37d9e39385253fea2023b177e69cf2ef84a`.
The rebuilt frozen release, `lambda-proof-final.exe`, has SHA-256
`cf3ff1bf6bedc97f985cfb068c6da93bb60d59d8030d706eb03ab86b25fc5111`;
`proof_final_manifest.json` records its compiler, build inputs and source
identities. The `proof_*` campaigns use this release. Earlier binaries and
reports remain separate; no earlier timing is relabeled as a measurement of
this binary.

The complete Lambda baseline passes **6,211/6,211**, with input prerequisite
**2,104/2,104** (`proof_baseline_final.log`). Release-host checks pass
MIR emission **244/244**, forced GC **295/295**, optimizer **53/53**, nine
focused contract/negative tests and three benchmark-helper tests. The emission
suite includes the previously failing
`LambdaMirProfiling.DiagnosticCallsPreserveProductionRootsAndSafepoints`.
The optimizer's debug-only fault check is covered by the full baseline.

An inferred `any` array still needs absence checks: an untyped mutation can
copy an out-of-bounds null into a valid slot. The extended comparison fixture
checks that operation, its old snapshot and the subsequent null comparison
in both tiers (S6.1.2, S7.1.1v3, S9.1.2). A physical integer carrier alone
does not establish a full null-free source contract (D3.3.4).

The archived `proof_*` campaigns include annotation-only, same-source and
actual previous-erased/new-typed comparisons. Their direct MIR nqueens row
remained inconclusive, leading to the separate release profile and subsequent
activation-local certificate reuse below. These measurements retain their
original binary identity; §14.6 contains the completion candidate's results.
Multiplying ratios from independent campaigns does not establish paired
cross-release uncertainty.

The earlier fourteen-case annotation corpus includes **Larceny primes**. P3's
2,122,050-store diagnosis concerns the canonical **Kostya primes** benchmark,
so the permanent fifteen-case corpus now includes matched Kostya controls in
both tiers (§14.5). Its typed and untyped ports differ only in annotations and
a comment; the erased twin preserves its algorithm, casts, input and borrowing.
The temporary port driver's invalid `larceny/primes` selector was corrected
to `kostya/primes`; its failed selection log is retained, and the unfinished
sequence resumed without relabeling or rerunning completed measurements.

### 14.5 Activation-local fill certificate reuse — completed

The release sample for the §14.4 candidate exposed repeated metadata work in
MIR nqueens: `lambda_array_rep_cert_resolve` occupied 117 exclusive observations
in the typed sample and did not exceed the five-observation reporting threshold
in either erased sample. These observations locate a cost; sample durations are
not benchmark ratios. The raw samples and full-output checks remain in
`temp/typed_tuning2/nqueens_final_profile/`.

A synchronous MIR activation now lazily resolves its first eligible native fill
certificate and reuses it at equivalent full-contract destinations. The compiler
uses the same invariant contract relation as heap-owned certificate interning,
qualifies each register by its MIR function, and initializes it at entry even
when first use lies inside a conditional or loop. Resolution stays after argument
evaluation, and negative or poisoned counts skip it. Async functions keep their
existing path. The register dies with the activation: no heap pointer enters an
AST plan, generated code, module cache cell or cross-context state (D3.3.3v3,
D8.4.1v2).

The resolved native-fill helper shares the existing primitive kernel and complete
cold admission; it receives the original contract and boundary for diagnostics.
Its effect remains MAY_GC with conservative reentry, while metadata resolution is
NO_GC/nonreentrant and returns a raw non-GC pointer. Precise Item ownership stays
with the existing root paths (D5.3.3). The activation fixture covers conditional
first use, equivalent aliases, repeated loop allocations, distinct primitive
contracts, invalid counts, incompatible values and full output under forced GC.

The permanent annotation corpus now includes canonical Kostya primes as well as
Larceny primes, so P3's original store-heavy workload is measured directly rather
than represented by the other primes port. New release evidence uses the
`cert_*` prefix; §14.4's `proof_*` reports remain tied to their original binary.

The new release is `lambda-cert-final.exe`, SHA-256
`dc21b010c08afe64e51320661f054eea659f8803e5bb9a62337d66310e30602b`,
with source corpus
`2e1da64ab4adcf565418ac8249bacadfd4d46e81ffc471f985c06b693fb62f92`
(`cert_final_manifest.json`). The complete baseline passes **6,213/6,213**,
including input **2,104/2,104**, MIR emission **245/245**, forced GC **296/296**,
and optimizer **54/54** (`cert_baseline_pass.log`). The expanded profiler parity
check includes the activation fixture.

A separate release diagnostic execution counts **15,131** nqueens native fills
and **5,509** certificate resolutions, eliminating 9,622 lookups (63.6%). Typed
root stores/reloads remain **114,870/149,154**; erased remain
**162,257/122,274**. Profiling still leaves output, static roots, memory traffic
and safepoints unchanged (`cert_corrected_roots/result.json`). The helper counter
now sees resolution as an explicit MIR import: its increase is visibility of
previously internal calls, not evidence of more runtime work. In the new native
sample the resolver has 51 exclusive observations, versus 117 in the prior
candidate; sampling is diagnostic evidence only. Full profiled goldens match.

The separate nqueens preflight uses two independent 21-pair pinned-MIR campaigns,
with equal repetitions calibrated to one second. Typed/current-erased ratios are
**0.9328 [0.8965, 0.9478]** and **0.9176 [0.8058, 0.9648]**. The corresponding
actual new-typed/previous-erased campaigns are **1.0196 [0.9097, 1.1130]** and
**0.9875 [0.9544, 1.0261]**: these wider cross-release intervals remain
inconclusive, and do not inherit the annotation-only speedup claim. Every
preflight output matches its complete golden and all provenance checks pass.

The completion candidate's 41-pair, 300 ms same-source MIR quicksort control
reproduces a remaining untyped slowdown: **1.0726 [1.0362, 1.1331]** versus the
previous release (`cert_same_untyped_jit.json`). The earlier nullable/branch/read
fixes reduced the original 1.2184× regression, but did not eliminate it. This
penalty remains visible independently of annotation ratios and corpus averages;
actual old-erased/new-typed pairs are required before claiming parity against
that reference. No inferred array is treated as null-free to remove required
absence semantics (S6.1.2, D3.3.4).

The existing strict-T0 Prettier ports also retain a visible difference:
**1.1164 [1.1017, 1.1567]** typed/untyped over 15 pairs
(`cert_ports_interp.json`). Their diagnostic executions visit **405,560,741**
and **313,221,749** interpreted nodes respectively, with zero fallback and no
MIR execution. The typed port performs 29.5% more interpreted work. Its
annotation-erased twin keeps that algorithm and workload, so this port ratio
does not establish an annotation penalty. Both timed ports match the complete
formatted-output golden (`cert_port_golden_validation.json`).

### 14.6 Completed release comparisons — 2026-10-04

These tables use only the frozen release and source hashes in §14.5.
All completion sequences finished successfully (`cert_all_validation.log`);
the release, frozen copy and current source manifest were verified again
after timing. Superseded §14.3/§14.4 reports remain separate.

#### Completion annotation-only campaigns

A and B are independent 15-pair campaigns, calibrated to 300 ms with
identical repetitions. Each cell is typed/erased median and paired two-sided
95% interval; a smaller ratio is faster. All fifteen cases remain visible.

| Workload | MIR A | MIR B | T0 A | T0 B |
|---|---|---|---|---|
| bounce | 0.7384 [0.7272, 0.7472] | 0.7428 [0.7374, 0.7591] | 0.8674 [0.8431, 0.8848] | 0.8761 [0.8591, 0.8869] |
| fannkuch | 0.9161 [0.9100, 0.9401] | 0.9272 [0.9167, 0.9382] | 0.9463 [0.9317, 0.9698] | 0.9600 [0.9395, 0.9784] |
| nqueens | 0.9416 [0.9289, 0.9661] | 0.9321 [0.9193, 0.9424] | 0.9699 [0.9437, 1.0220] | 0.9769 [0.9600, 0.9925] |
| Queens | 0.2745 [0.2705, 0.2775] | 0.2716 [0.2678, 0.2736] | 0.9804 [0.9333, 0.9981] | 0.9741 [0.9603, 0.9884] |
| fasta | 0.6178 [0.6004, 0.6307] | 0.6211 [0.6139, 0.6361] | 0.9856 [0.9751, 0.9980] | 0.9820 [0.9561, 1.0244] |
| deriv | 0.2719 [0.2664, 0.2752] | 0.2528 [0.2488, 0.2612] | 0.8769 [0.8604, 0.8923] | 0.8767 [0.8480, 0.8848] |
| towers | 0.5162 [0.4658, 0.5302] | 0.5270 [0.5238, 0.5357] | 0.9525 [0.9351, 0.9759] | 0.9550 [0.9242, 0.9727] |
| sieve | 0.8253 [0.8187, 0.8269] | 0.8204 [0.8140, 0.8282] | 0.9589 [0.9441, 0.9815] | 0.9648 [0.9388, 0.9873] |
| primes | 0.9833 [0.9618, 0.9930] | 0.9729 [0.9459, 1.0057] | 0.9757 [0.9674, 1.0152] | 0.9859 [0.9501, 1.0081] |
| quicksort | 0.5420 [0.5357, 0.5532] | 0.5454 [0.5387, 0.5570] | 0.9065 [0.8759, 0.9214] | 0.8945 [0.8473, 0.9122] |
| kostya_primes | 0.9724 [0.9497, 1.0020] | 0.9673 [0.9483, 0.9877] | 0.9734 [0.9140, 1.1100] | 0.9730 [0.9576, 0.9932] |
| binarytrees | 0.7199 [0.7077, 0.7338] | 0.7602 [0.7384, 0.7758] | 0.9212 [0.8925, 0.9495] | 0.9162 [0.9042, 0.9211] |
| gcbench | 0.4825 [0.4593, 0.4885] | 0.4882 [0.4700, 0.4911] | 0.9523 [0.8390, 0.9627] | 0.9314 [0.9073, 0.9455] |
| Richards | 0.2443 [0.2390, 0.2467] | 0.2440 [0.2410, 0.2467] | 0.8670 [0.8443, 0.8940] | 0.8416 [0.6253, 0.8970] |
| prettier_ast | 0.4313 [0.4224, 0.4392] | 0.4314 [0.4270, 0.4367] | 0.9731 [0.8839, 1.0000] | 0.9456 [0.9385, 0.9678] |

The predeclared uncertainty rule selects the union of rows with either
broad median above one or upper bound above 1.02. C and D use 41 pairs each,
targeting 1.5 seconds; the broad reports above are retained.

| Tier | Workload | C | D |
|---|---|---|---|
| T0 | fasta | 0.9883 [0.8482, 1.1097] | 0.9791 [0.9687, 1.0109] |
| T0 | kostya_primes | 0.9618 [0.9526, 0.9806] | 0.9913 [0.9672, 1.0020] |
| T0 | nqueens | 0.9606 [0.9440, 0.9722] | 0.9764 [0.9658, 0.9877] |

Rows still unresolved after C/D receive two independent E/F campaigns,
41 pairs each at three seconds. C/D remain visible above.

| Tier | Workload | E | F |
|---|---|---|---|
| T0 | fasta | 0.9826 [0.9649, 0.9932] | 0.9751 [0.8554, 1.0183] |

**All 30 tier/workload combinations pass the annotation-only gate** in
their two acceptance campaigns: median at most one and 95% upper bound at
most 1.02. 24 demonstrate speedups in both campaigns; 6 meet practical
equivalence within the 2% measurement tolerance. No positive penalty is
passed by a corpus mean. Strict T0 diagnostics show zero fallback and zero
MIR satellite execution (D8.1.1v15).

#### Previous erased release versus completion typed release

These are actual alternating old-erased/new-typed process pairs, with equal
work, equal repetitions and complete goldens. They independently check the
older untyped control; they are not products of annotation and old/new ratios.

| Workload | MIR acceptance campaigns | MIR ratios | T0 acceptance campaigns | T0 ratios |
|---|---|---|---|---|
| bounce | A, B | 0.7567 [0.7127, 0.7989]; 0.7450 [0.6314, 0.8559] | A, B | 0.8651 [0.8145, 0.9121]; 0.9265 [0.8710, 0.9809] |
| fannkuch | C, D | 0.9519 [0.9357, 0.9805]; 0.9504 [0.9238, 0.9832] | C, D | 0.9209 [0.9036, 0.9382]; 0.9290 [0.9140, 0.9536] |
| nqueens | E, F | 0.9670 [0.9593, 0.9723]; 0.9781 [0.9575, 0.9934] | C, D | 0.9524 [0.9399, 0.9676]; 0.9652 [0.9528, 0.9800] |
| Queens | A, B | 0.2663 [0.2583, 0.2891]; 0.2593 [0.2393, 0.3205] | C, D | 0.9659 [0.9480, 0.9972]; 0.9676 [0.9533, 0.9806] |
| fasta | A, B | 0.5803 [0.5714, 0.6106]; 0.6279 [0.5989, 0.7082] | C, D | 0.9475 [0.9277, 0.9608]; 0.9445 [0.9329, 0.9580] |
| deriv | A, B | 0.2461 [0.2378, 0.2608]; 0.2532 [0.2381, 0.2758] | A, B | 0.8766 [0.8200, 0.9350]; 0.9163 [0.8188, 1.0160] |
| towers | A, B | 0.3552 [0.3125, 0.5687]; 0.4826 [0.4544, 0.5044] | C, D | 0.9552 [0.9333, 0.9658]; 0.9485 [0.9369, 0.9661] |
| sieve | C, D | 0.8286 [0.8184, 0.8691]; 0.8371 [0.8180, 0.8861] | C, D | 0.8993 [0.8866, 0.9084]; 0.8890 [0.8764, 0.9042] |
| primes | C, D | 0.9686 [0.9609, 0.9803]; 0.9689 [0.9372, 0.9906] | C, D | 0.9339 [0.9206, 0.9476]; 0.9299 [0.9164, 0.9404] |
| quicksort | A, B | 0.5708 [0.5531, 0.6032]; 0.6105 [0.5695, 0.6578] | A, B | 0.9065 [0.8539, 0.9392]; 0.9024 [0.8510, 0.9292] |
| kostya_primes | C, D | 0.9585 [0.9502, 0.9738]; 0.9643 [0.9380, 0.9848] | A, B | 0.9383 [0.9105, 0.9679]; 0.9227 [0.8854, 0.9449] |
| binarytrees | A, B | 0.7343 [0.6387, 0.8199]; 0.6452 [0.6230, 0.6909] | A, B | 0.9228 [0.8767, 0.9627]; 0.8880 [0.8465, 0.9426] |
| gcbench | A, B | 0.4512 [0.4308, 0.4831]; 0.4382 [0.4134, 0.4743] | A, B | 0.9221 [0.9103, 0.9389]; 0.9433 [0.9303, 0.9499] |
| Richards | A, B | 0.2614 [0.2500, 0.2818]; 0.2525 [0.2405, 0.2693] | A, B | 0.8558 [0.8363, 0.8914]; 0.8490 [0.8384, 0.8781] |
| prettier_ast | A, B | 0.4344 [0.4171, 0.4614]; 0.3817 [0.3759, 0.3982] | A, B | 0.9613 [0.8996, 0.9781]; 0.9609 [0.9526, 0.9791] |

Both acceptance campaigns pass for all 30 combinations against the
previous erased release. Cross-release A/B use 15 pairs at 300 ms; selected
C/D use 41 at 1.5 seconds; any unresolved C/D rows receive E/F with 41 pairs
at three seconds. The complete broad and precision reports remain
in `cert_cross_{a,b,c,d,e,f}_{jit,interp}.json`.

#### Same-source old/new controls

These ratios are completion/previous release for the **same** source.
Untyped rows use 41 pairs, typed rows 15; identical repetition targets
300 ms. This table is separate from annotation cost and port differences.

| Workload | Untyped MIR | Typed MIR | Untyped T0 | Typed T0 |
|---|---|---|---|---|
| bounce | 1.0279 [0.9975, 1.0465] | 0.9628 [0.8549, 1.0461] | 0.9942 [0.9397, 1.0147] | 0.7829 [0.7597, 0.7984] |
| fannkuch | 1.0056 [0.9740, 1.0386] | 0.9732 [0.9094, 1.0343] | 1.2866 [0.9470, 1.4443] | 0.8314 [0.8019, 0.8769] |
| nqueens | 1.0337 [0.9782, 1.0911] | 0.8113 [0.7499, 0.8610] | 0.9869 [0.9546, 1.0251] | 0.8244 [0.7709, 0.8589] |
| Queens | 0.9629 [0.9357, 0.9829] | 0.9507 [0.8791, 0.9936] | 1.0271 [0.9999, 1.1430] | 0.7321 [0.7101, 0.7646] |
| fasta | 0.9967 [0.9436, 1.0346] | 0.9916 [0.9204, 1.0151] | 0.9794 [0.9489, 0.9977] | 0.9052 [0.8334, 0.9695] |
| deriv | 0.9108 [0.8868, 0.9583] | 0.9931 [0.9646, 1.0738] | 0.9885 [0.9544, 1.0125] | 0.8620 [0.8030, 0.8847] |
| towers | 0.8725 [0.8409, 0.9197] | 0.9913 [0.8885, 1.1024] | 0.9648 [0.9290, 0.9942] | 0.7215 [0.6915, 0.7444] |
| sieve | 1.0008 [0.9361, 1.0780] | 0.9964 [0.9615, 1.0857] | 1.2620 [0.8030, 1.2804] | 0.7986 [0.7644, 0.8478] |
| primes | 0.9937 [0.9554, 1.0572] | 0.9955 [0.9192, 1.0862] | 0.9537 [0.9173, 0.9873] | 0.8602 [0.8340, 0.9052] |
| quicksort | 1.0726 [1.0362, 1.1331] | 1.0337 [0.9593, 1.1197] | 0.7600 [0.6823, 1.0530] | 0.7797 [0.7379, 0.8314] |
| kostya_primes | 0.9970 [0.9407, 1.0580] | 1.0247 [0.9454, 1.1069] | 0.9446 [0.9273, 0.9630] | 0.8570 [0.8231, 0.9059] |
| binarytrees | 0.8627 [0.8349, 0.9049] | 0.9838 [0.9019, 1.0995] | 1.0157 [0.9689, 1.0628] | 0.9714 [0.8247, 1.2163] |
| gcbench | 0.9010 [0.8611, 0.9373] | 0.9398 [0.8887, 1.0638] | 0.9879 [0.9587, 1.0054] | 0.6440 [0.5438, 0.8319] |
| Richards | 1.0483 [0.9888, 1.1047] | 0.9866 [0.9493, 1.0784] | 0.7588 [0.6730, 1.1399] | 0.8338 [0.8129, 0.8674] |
| prettier_ast | 0.9120 [0.8954, 0.9280] | 0.9555 [0.8696, 1.0263] | 0.9907 [0.9731, 1.0030] | 0.7908 [0.7833, 0.7999] |

#### Existing ports and watchlist

Port ratios are typed/untyped in the completion release, 15 pairs each.
Port algorithms, casts and borrowing can differ; they do not isolate
annotation overhead. Every timed output matches the complete shared or
per-source golden (`cert_port_golden_validation.json`).

| Port | MIR | T0 |
|---|---|---|
| r7rs/nqueens | 0.9696 [0.9489, 0.9821] | 0.9548 [0.9308, 0.9898] |
| awfy/sieve | 1.0000 [0.8800, 1.0833] | 0.9343 [0.8743, 1.0172] |
| awfy/queens | 0.3230 [0.3072, 0.3608] | 0.9735 [0.9378, 1.0061] |
| awfy/towers | 0.5291 [0.5231, 0.5381] | 0.9603 [0.8589, 1.0277] |
| awfy/bounce | 0.8387 [0.7647, 0.9016] | 0.9200 [0.8563, 0.9792] |
| awfy/richards | 0.2406 [0.2316, 0.2647] | 0.8631 [0.8430, 0.8704] |
| beng/binarytrees | 0.5273 [0.5109, 0.5350] | 0.9268 [0.9022, 0.9579] |
| beng/fannkuch | 0.9139 [0.8305, 1.0149] | 0.9408 [0.8907, 0.9663] |
| beng/fasta | 0.9056 [0.8641, 0.9533] | 1.0015 [0.9773, 1.0334] |
| kostya/primes | 0.9694 [0.9608, 0.9791] | 0.9857 [0.9477, 1.0102] |
| larceny/deriv | 0.3778 [0.3694, 0.3803] | 0.9593 [0.9063, 1.0176] |
| larceny/gcbench | 0.8535 [0.7513, 0.8793] | 0.9662 [0.9428, 0.9850] |
| larceny/quicksort | 0.5695 [0.5380, 0.6202] | 0.8941 [0.8725, 0.9318] |
| text/prettier_ast | 0.4226 [0.4122, 0.4539] | 1.1164 [1.1017, 1.1567] |

Watchlist ratios are same-source completion/previous release, with
21 pairs at 500 ms, complete goldens and separate strict-T0 diagnostics.

| Workload | MIR | T0 |
|---|---|---|
| larceny/puzzle | 1.0414 [0.9717, 1.0732] | 0.9737 [0.9412, 1.0004] |
| text/microdiff | 0.9620 [0.9397, 1.0090] | 1.0060 [0.9643, 1.0373] |

#### Fasta cast factorial

Each ratio is cast/no-cast at a fixed annotation setting in pinned MIR.
The explicit cast is varied independently; full sequence goldens agree.

| Source | A | B |
|---|---|---|
| erased | 1.4803 [1.4310, 1.5194] | 1.4526 [1.4397, 1.4978] |
| typed | 0.9791 [0.9597, 1.0126] | 0.9915 [0.9643, 1.0104] |

### 14.7 Limits and reproduction

The acceptance claim covers the fifteen matched annotation controls in the
two pinned modes, and their actual previous-erased/new-typed comparisons.
It does not guarantee that every arbitrary annotated program has zero cost.
Practical equivalence uses the predeclared 2% uncertainty tolerance; a
reproducible positive penalty cannot pass through an aggregate average.
The same-source untyped MIR quicksort regression and the existing strict-T0
Prettier port difference remain visible above. Puzzle and microdiff have no
established positive penalty in these watchlist measurements, but their
intervals remain inconclusive.

The permanent runner regenerates annotation twins and complete goldens,
calibrates equal repetitions, alternates process order and records binary,
source and build provenance. A fresh full-corpus annotation check is:

```bash
make test-lambda-baseline
make release
for tier in jit interp; do
  for campaign in a b; do
    python3 test/benchmark/run_typed_audit.py --candidate ./lambda.exe \
      --tier "$tier" --annotation-controls --pairs 15 \
      --annotation-target-ms 300 \
      --output "temp/typed_tuning2/reproduce_${campaign}_${tier}.json"
  done
done
```

Run profiling and other CPU-heavy validation separately from these timings.
For a same-source comparison, supply `--control <previous-release>` and
`--source typed` or `--source untyped`. Preserve the complete broad reports
when extending selected uncertain rows. The exact completion sequencing,
actual cross-release driver and report renderer are retained alongside the
`cert_*` artifacts in `temp/typed_tuning2/`; `cert_final_manifest.json` binds
those results to the release and source hashes in §14.5.
