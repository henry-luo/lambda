# Typed Lambda performance audit — 2026-10-03

**Status:** all six tuning proposals are implemented (§10), and final release measurements and validation are complete (§11). Typed/untyped performance parity remains unmet (§12). Strict interpreter selection and untyped Queens support were fixed before this tuning change (§6/§9). The performance comparison uses **pinned MIR (`LAMBDA_TIER=jit`)**, with a separate pure-T0 diagnostic. No AUTO timings enter these comparisons.

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

These are executed counter differences, not inferred timing percentages:

| Workload/function | Untyped | Typed | Implication |
|---|---:|---:|---|
| bounce benchmark root reloads | 36,815 | 192,627 | Much more boxed state crosses helper calls |
| bounce benchmark root stores | 6,091 | 15,606 | Extra precise-root traffic |
| bounce `is_truthy` calls | 0 | 20,000 | Typed conditions lose native boolean lowering |
| fannkuch hot-body root reloads | 40,349 | 346,690 | Same pattern, larger ratio |
| fannkuch `is_truthy` calls | 0 | 8,659 | Investigate condition descriptor/proof loss |
| nqueens solve root reloads | 812,501 | 998,885 | Extra boundary/root work |
| nqueens numeric-array admissions | 0 | 13,075 | Repeated contract crossings in recursion |
| fasta random-body root reloads | 812,848 | 1,198,838 | More live boxed values around calls |
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
construction, not those semantics. MIR fannkuch still has 312,042 hot-body root
reloads versus the untyped port's 40,349, and fasta 958,838 versus 812,848.
Those remaining roots surround the helpers that still exist. These are useful
profiling targets, not sufficient evidence to assign timing percentages or to
remove checks/roots (D3.3.4, D5.3.3).

Further tuning should isolate those remaining checked kernels and boxed joins
with annotation-only repeated workloads and native samples. Every new reduction
must keep nullable/OOB behavior, error joins, exact index validation and
snapshot/borrow semantics (S7.1.1v3, S7.1.3v2, S7.7.2–S7.7.4,
S9.1.2–S9.1.3). The current implementation reports its residual penalties
instead of redefining unequal work or permitting fallback as a speed result.
