# Typed Lambda performance audit — 2026-10-03

**Status:** measured on a fresh release build; tuning below is proposed, not implemented. Strict interpreter selection is fixed in this change. The performance comparison uses **pinned MIR (`LAMBDA_TIER=jit`)**, with a separate pure-T0 diagnostic. No AUTO timings enter these comparisons.

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
