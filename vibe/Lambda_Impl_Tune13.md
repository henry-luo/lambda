# Tune 13: Result22 Typed-Lane Regressions and the Path Toward the C2MIR Ceiling

- **Date:** 2026-08-06
- **Input:** `test/benchmark/Overall_Result22.md` (Lambda commit `4babb408a2`)
- **Status:** IMPLEMENTED — P0/P1 complete; P2/P3 explicitly deferred by their current
  release-profile gates. The retained implementation and all required correctness gates are
  recorded below.
- **Related:** `vibe/Lambda_Tune_Typed_Vs_C2MIR.md` (historical Result18 dissection),
  `vibe/Lambda_Impl_Tune11 (done).md`, `vibe/Lambda_Impl_Tune12.md`,
  `vibe/Lambda_Design_Type_Enforcement.md`, `vibe/Lambda_Design_Compiling_Lane.md`,
  and `vibe/Lambda_Semantics_Int_Type.md`
- **Formal authority:** `doc/Lambda_Formal_Semantics.md` S1.4–S1.6, S4.1, S4.5.3,
  S11.4; `doc/Lambda_Formal_Design.md` D1.4–D1.6, D2.2–D2.6, D3.2–D3.3,
  D4.4, D5.1–D5.3, D8.3–D8.4, D8.6

## 1. Headline numbers

| Metric | Result18 | Result22 | Tune13 current |
|---|---:|---:|---:|
| MIR (untyped)/Node geo | — | 2.75x | 2.73x |
| MIR (typed)/Node geo | — | 2.27x | **1.93x** |
| MIR (typed)/C2MIR geo | 9.48x | 11.2x | **9.39x** |

The typed column is only about 1.2x better than untyped in geomean, and on 16 of 56 rows it is
slower than untyped — in the worst cases by 4–9.5x (`quicksort` 10.2→40.8 ms, `matmul`
18.0→71.3 ms, `bounce` 0.31→2.97 ms, `nqueens` 1.93→5.90 ms, `fft` 2.67→5.12 ms,
`permute` 1.02→3.09 ms). The widest static-ceiling gaps are concentrated in rows with hot
annotated-array access and mutation, but the remaining recursion, statement-loop, string, and
allocation gaps are distinct tracks rather than variants of one array defect.

Result18 and Result22 are different compiler/semantics snapshots. Their aggregate comparison is
directional evidence only; the P0/P1 target decisions below use interleaved archived/current
release probes over identical scripts, while the aggregate matrix supplies the fixed-population
current release evidence.

The current release matrix uses the same 56 canonical rows, three runs per engine, and the
workload-only `__TIMING__` protocol. MIR typed and Node timed all 56 rows; the current C2MIR
reference ports timed 44. The current result JSON is
`temp/tune13_release_full_results.json` (SHA-256
`4f748b6d174cf4215a344816907f45b2440127ffc19baeece6d29c1555a946b3`) from source base commit
`cc4cdf9efd3221390b18c54c322b2192e99c4122`; the release binary SHA-256 is
`dd50efc2acbdc0e48847b320b458febdcbf8932d00c39a49487d87c829d2a83d`. Node is v22.13.0.

## 2. Verified Result22 root causes

### R22-1: Per-store checked-helper path on annotated arrays is O(n) per store — quadratic overall

Micro-benchmark (10k-element `int[]`, 10k-iteration loop, archived v22 binary):

| Variant | Time |
|---|---:|
| typed `var a:int[]`, read-only loop | 0.174 ms |
| typed, loop adds one write `a[i] = a[i] + 1` | **23.3 ms** |
| identical write loop, untyped | 0.05 ms |

One in-loop store makes the typed version 465x slower than untyped. The archived MIR/runtime
trace shows this sequence:

1. The inline native-write witness requires AST-level type equality. An RHS containing an
   element read may still carry `ANY` in the AST even when lowering has produced a proved native
   lane, so the witness fails.
2. The store calls `lambda_array_set_checked` or `_inplace`.
3. The non-inplace helper clones the array; both helpers can run a post-store whole-container
   `lambda_type_matches`, which routes an occurrence contract through the deep validator.

Ten thousand stores times an O(n) clone/validation path explains the result. The fix must preserve
S11.4.1's checked-before-commit contract and S1.4's value/COW semantics, but a successfully
admitted element stored through a representation-compatible lane already preserves the enclosing
array contract. Re-walking the whole container is not required by D3.2.2 once the original
boundary proof and the admitted element together preserve the full contract.

The interleaved archived/current release probe `test/benchmark/larceny/array12.ls` performs
10,000 indexed writes and 100 full summation passes in a typed lane: archived/current medians
were 1.317/1.199 ms (-9.0%). The untyped `array1.ls` control was 1.082/1.079 ms (-0.3%).
The same interleaved probe set measured `awfy/bounce2` 2.975/0.779 ms (-73.8%),
`larceny/quicksort2` 41.213/5.481 ms (-86.7%), and `r7rs/nqueens2` 5.745/2.152 ms
(-62.5%). Their untyped controls changed by -9.3%, +0.6%, and +0.1% respectively, so the
large gains are isolated to the typed proof paths rather than a general timing shift.
`COW_EXEC_PROFILE=1` recorded zero `array_checked_store_calls`, zero full clones, zero copied
bytes, and zero post-store validation calls for the compatible typed array probe: the
representation stayed on the inline lane. Raw interleaved output is
`temp/tune13_interleaved.log` (SHA-256
`9a385f8072b4abf88d90b426ebd9926438942114126b0e2c555eb09e31735e3b`). This is the required
O(n)-rather-than-O(n²) evidence; the control rows are not claimed as typed-array speedups.

### R22-2: An invalidation scan can suppress guarded reads for an entire function

In `bounce2.ls`, annotated element reads compile to boxed `fn_index` calls while the untyped
variant inlines them. A store whose RHS has an unresolved or overly wide AST type makes
`has_elem_type_invalidation` discard the function-wide element witness. This is appropriate for
an unguarded inferred narrowing, but not for an annotated guarded witness: the guarded load checks
the runtime representation and falls back safely after a real representation change.

The remedy is to distinguish a declared contract witness from an inferred storage narrowing,
not to trust a raw pointer indefinitely. D3.3.3 keeps narrowing local to its binding; D2.6 and
S1.6 require the guarded representation change to remain observationally invisible.

### R22-3: Redundant direct-call container boundaries

`mir_boundary_is_redundant` can reuse a stable annotated map binding's established contract, but
the archived compiler lacks the equivalent for matching array/occurrence contracts. Recursive
typed callers therefore re-enter `lambda_type_check` when passing an unchanged `int[]` binding to
an `int[]` parameter.

This is safe to elide only from an explicit proof: the stable binding was checked before
publication, its contract was not widened, and the direct callee expects the same full occurrence
contract. Unknown, converted, nullable, borrowed-`var`, view, and representation-changing values
retain the checked entry. This is the statically-proven path required by D8.3.2–D8.3.3, not a
TypeId shortcut.

### R22-4: Native results can lose their proof before a typed local boundary

In-loop declarations such as `var axv: int = abs(bxv[j])` can emit a runtime declaration check
because the builtin result is recorded as `ANY`. The same general gap is visible when native int
arithmetic reaches a declared local but its AST contract/provenance is stale: the emitter boxes,
checks, and unboxes a value whose full source contract already proves identity admission.

Builtin result contracts and expression `ValueRep`/provenance must reach boundary lowering
explicitly. S4.1.2 already rules that `int` arithmetic stays `int`; D2.4 forbids reconstructing
that semantic or representation fact from the physical MIR register class.

## 3. What the C2MIR comparison does and does not prove

The benchmark's C2MIR column is the matching C port compiled through MIR's
`mac-deps/mir/c2m` frontend, not Lambda's frozen `--c2mir` execution path. Sharing MIR's optimizer
and code generator makes it a useful static ceiling and localizes large emitted-code differences
to the frontend and workload representation.

It is not permission to copy C semantics. Lambda still requires total reads, checked writes,
COW value semantics, rich declared-boundary failures, error return lanes, and precise GC
[S1.4–S1.6, S11.4, D1.4–D1.5]. The legacy C2MIR transpiler remains frozen and vendored MIR remains
unmodified [D1.6].

## 4. Consolidated remaining gaps from the Result18 dissection

The following are the core Result18 items carried into Tune13:

1. **Identity-boundary proof propagation (M1/A1).** The scalar/map implementation exists, but
   native expression facts and stable array contracts still miss redundant boundaries (R22-3/4).
2. **Native success returns and scalar-home classification (M3/A3/D2).** The native return ABI and
   error lane exist, but unannotated procedural bodies remain conservatively boxed and every boxed
   procedure return is classified dynamic. Current fib therefore returns Items, adopts/restores a
   scalar home, and combines recursive results through `fn_add`.
3. **v5 int proof propagation (successor to M2/A2).** The former flex-int mechanism is obsolete,
   but current AST/lowering paths can still lose a proved S4.1 `int` contract and native lane
   before a store, boundary, or index consumes it. The work is proof plumbing only; it does not
   revive the old overflow representation.
4. **Result-demand propagation for `for` (M6/B2).** Native counted ranges landed, but a procedural
   loop whose value is discarded still constructs `array_spreadable`, pushes every body result,
   and finalizes the array. `AST_NODE_FOR_STAM` alone is not a discard proof because a top-level or
   functional consumer may observe the comprehension value.
5. **Frame/root work after specialization (residual M8/D1b).** The unconditional side-stack ensure
   and dead prologue loads were already removed. Remaining work is profile-gated scalar-home,
   root-frame publication/clearing, and native leaf/recursive-frame reduction under the current
   caller-donated-home and safepoint-current design [D5.2–D5.3].
6. **Typed-array entry/capability/frame residual.** Direct typed-array loads/stores and the
   typed-array native gate already exist. Tune13 must improve proof reuse and checked fallback,
   not add a second direct-addressing path.

The old flex-int overflow mechanism is not carried forward. Under v5, `int` has an i64 lane with
private poison sentinels and closed total arithmetic [S4.1, D2.2.2]; `%` stays in its operand
domain [S4.5.3]. Any remaining boxing or widening is a proof/lowering defect, not permission to
restore the former box-float overflow path.

String/byte loops are also not attributed to M5/M7 without a current allocation/profile trace.
The C ports use mutable byte buffers where Lambda exposes immutable Unicode strings, so `base64`
and `levenshtein` remain a separate conditional track.

## 5. Proposal

### P0 — Contract-preserving typed-array stores

1. **Inline guarded stores for annotated arrays.** Mirror the guarded read path: check current
   storage/element representation, require a compatible proved value lane, perform the raw store,
   and fall back to the checked helper on guard failure. Consume explicit expression
   `ValueRep`/contract provenance; never probe `MIR_reg_type` for semantics [D2.4].
2. **De-quadratify `lambda_array_set_checked`.** Admit the element once, preserve transactionality,
   clone only when COW ownership requires it, and remove the post-store whole-container validator
   sweep when the element/store proof preserves the full array contract [S11.4.1, D3.2.2,
   D4.4.1–D4.4.3].
3. **Keep guarded declared witnesses until a real invalidation.** Restrict conservative
   function-wide invalidation to inferred/unguarded narrowing. A guarded declared witness checks
   the live owner/representation and takes the cold fallback after detachment, demotion, view, or
   replacement [D2.6, D3.3.3].

### P1 — Boundary, expression-proof, and result-demand propagation

4. **Stable array/occurrence direct-call proof.** Extend the existing stable-map proof to an exact
   stable array occurrence contract. The proof includes full `Type*`, nullability, element lane,
   non-widened binding identity, ownership/borrow mode, and current guarded capability. Mismatches
   call the checked wrapper [S11.4.1, D8.3.2–D8.3.3].
5. **Builtin and native-expression result facts.** Publish exact result contracts and emitted
   representations for `abs`, `min`, `max`, `len`, native int arithmetic, and other audited
   builtins so identity declaration/assignment/return boundaries omit only the check, not a
   required carrier conversion [D2.4, D3.2.1, D3.3.1].
6. **Result-demand-aware `for` lowering.** Thread an explicit result-demand flag from content/call
   consumption into loop lowering. When the value is proved unused, execute the body and all
   effects but emit no comprehension array, push, spread, order/limit result work, or finalization.
   Preserve the existing value-producing path for top-level output, functional expressions,
   nested comprehensions, and any consumer that may observe it.
7. **Finish v5 int proof propagation.** Ensure `int` arithmetic and `div`/`%` retain their full
   semantic contract and `INT_LANE` provenance through AST inference and lowering. Do not change
   S4.1/S4.5 semantics and do not infer from `MIR_T_I64`, which is shared by several unrelated
   representations [D2.2.2, D2.4.1].

### P2 — Native returns, scalar homes, and reduced frames

8. **One whole-function exit-carrier analysis for A3+D2.** Walk every explicit return, implicit
   fallthrough, nested control exit, and checked/error-producing edge. Record separately:
   semantic return contract, emitted success representation, nullable lane, error lane,
   scalar-home requirement, suspension, and managed values live across calls. Use the same summary
   for forward declarations, body emission, direct calls, and boxed wrappers.

   A closed unannotated `pn` may receive a native implementation when every success exit proves
   the same carrier; this is an inferred implementation choice, not a new source contract
   [D3.3.1–D3.3.2]. Declared return firewalls and raised diagnostics remain enforced through the
   boxed wrapper/error lane [S11.4.2, D1.4, D8.3.3].
9. **Remove boxed recursive result dispatch only from that proof.** For fib-class functions, raw
   recursive success results should feed native arithmetic directly; a guard failure, dynamic
   call, open result, or error-capable merge keeps the Item path. This is the load-bearing
   Result18 A3 target, not blanket unboxing.
10. **Reduce scalar-home/root-frame work from emitted effects.** Enter this phase only after a
    fresh release profile attributes at least 10% of a target to boxed adapters, scalar-home
    adoption/restoration, root-frame clearing/publication, or equivalent frame work. Zero-root
    functions elide the root frame; other functions clear/publish only what the existing
    `MirEmitter` liveness proof permits [D5.3.1, D5.3.4]. Keep stack-capacity checks and the cold
    grow path; D1a already removed the unconditional ensure call.

### P3 — Conditional separate tracks

11. **String/byte loops.** Enter only when a release allocation/profile run attributes at least
    15% of `base64` or `levenshtein` to loop-carried concat/copy, generic indexing, one-character
    boxing, or UTF-8 rescanning. Reuse the existing owned mutable builder internally and finalize
    one immutable value at the observable boundary; preserve Unicode semantics and precise roots.
12. **Allocation/GC pacing.** `gcbench` and allocation-heavy residuals remain a separate GC plan.
    C2MIR's own allocation cost shows that this is not an array-store or call-carrier patch.

## 6. Implementation status

### P0 retained

- `lambda/runtime/transpile-mir.cpp` now carries declared array occurrence witnesses through
  guarded loads/stores, admits compatible native RHS proofs, and emits a checked fallback for
  OOB, detached, widened, nullable, view, or otherwise representation-changing values. The
  fallback preserves `var` write-back and COW ownership; it does not infer semantics from
  `MIR_reg_type` [D2.4, D2.6, S11.4.1].
- `lambda/runtime/lambda-eval.cpp` now recognizes the legacy non-nullable `ArrayNum` carrier as
  a valid representation proof, clones it through the normal COW path, and skips the whole-array
  validator only after the lane proof is re-established. The checked path remains transactional
  and rejects an incompatible value before publication [S1.4–S1.6, S11.4.1, D3.2.2, D4.4].
- The declared guarded witness is no longer discarded by a conservative function-wide
  invalidation scan. Borrowed or widened roots still use the checked path [D2.6, D3.3.3].

### P1 retained

- Stable direct-call elision now covers exact non-widened array occurrence contracts, including
  element lane, full occurrence identity, and the current guarded binding witness; nullable,
  borrowed, converted, and mismatched values retain the checked boundary [D8.3.2–D8.3.3,
  S11.4.1].
- Exact native facts now flow through the audited scalar `abs`, `round`, `floor`, `ceil`, `trunc`,
  `min`, and `max` helpers. Indexed values and integer arithmetic reopen their native lane only
  when the same `mir_is_native_int_arith` predicate used by emission proves it. This predicate
  restriction was necessary: an early broader proof emitted an Item tag as a raw integer store,
  producing `inf` in the sequential probe and failing `nqueens`; the rejected form is not retained
  [S4.1, S4.5.3, D2.2.2, D2.4.1].
- `for` lowering now receives an explicit result-demand bit. Discarded procedural loops still
  execute effects and control flow but do not allocate, spread, push, order, or finalize a
  comprehension result; observed loops retain the old materializing path [S1.4, D3.2, D8.3].

The structural fixtures are `test/mir/lambda/tune13_array_lane.ls` plus its expected output and
MIR check, `test/mir/lambda/tune13_for_demand.ls` plus its expected output and MIR check, and the
updated `typed_array_guard.mir-check`. The MIR budget was re-baselined for the justified guard
growth: module instructions 1089, guarded-load instructions 102, guarded-store instructions 170;
guarded-load roots/safepoints 3/4 and guarded-store roots/safepoints 5/7 [D8.6.1–D8.6.2].

### P2 explicitly deferred

No P2 code was retained. The current release profiler has no runtime attribution for scalar-home
adoption/restoration, boxed recursive adapters, or root-frame publication on Lambda MIR. The
available `LAMBDA_PROFILE=1` trace is compilation-phase-only, and the current fib/fibfp release
rows do not establish the required 10% runtime attribution or the required 15% target win. P2
therefore does not enter under the stated gate; no scalar-home or root-clearing behavior was
changed [D5.2–D5.3, D8.6.3].

### P3 explicitly deferred

No P3 string-loop code was retained. Fresh release probes with `COW_EXEC_PROFILE=1` on
`kostya/base64.ls` and `kostya/levenshtein.ls` recorded zero array checked-store calls, zero
shared copies, zero copied bytes, and zero map-admission copies. That maintained profile does not
attribute loop concat, character boxing, generic indexing, or UTF-8 rescanning to a percentage;
the entry threshold is therefore not met. The current typed rows were 65.8 ms for `base64` and
46.5 ms for `levenshtein`, so neither meets the 20% retained-change threshold against its
current untyped control or the archived row [S1.4–S1.6, D4.4].

## 7. Verification and release evidence

| Gate | Result |
|---|---|
| `make build-test` | pass; all test executables built |
| `make test-lambda-baseline` | **3596/3596**: input 2104/2104, Lambda runtime 1492/1492 |
| `make test262-baseline` | **40261/40261**, zero failures, zero regressions; ref/test262 `673e9bacbe28590f501e2dcd817aadcc31899191` |
| Focused MIR checks | Tune13 array lane, result-demand, and typed guard checks pass 3/3 |
| Forced-GC/poison | Tune13 array lane stressed output matches; focused forced-GC pass 1/1; full MIR GC stress 30/30 |
| Typed rejection controls | typed write rejection plus nullable-array rejection pass 3/3 |
| Regression probes | `nqueens`, `nqueens2`, and the sequential native-store probe pass after proof narrowing |
| Release matrix | 56/56 MIR typed and Node rows; 44/56 C2MIR rows; three runs per row, exit status 0 |

Current matched geomeans are MIR typed/Node **1.93x** over 56 rows, MIR typed/C2MIR **9.39x**
over 44 rows, and MIR untyped/Node 2.73x. Representative typed rows versus archived Result22
are: `nqueens` 5.90→2.17 ms (-63%), `quicksort` 40.8→5.02 ms (-88%), and `bounce`
2.97→0.811 ms (-73%). The string controls remain separate: `base64` 62.2→65.8 ms and
`levenshtein` 46.8→46.5 ms. The generated current markdown report is
`temp/Overall_Result_Tune13_full.md` (SHA-256
`983ed25d98cfa55d54c1128e84695cd735a1c523396d285d34edfd8e59ef03e1`).

The retained changes obey the formal split: P0/P1 carry semantic and representation proofs
through existing checked boundaries, while P2/P3 remain profile-gated non-entries. No legacy
`--c2mir`, `transpile.cpp`, vendored MIR, or conservative native-stack scanning was changed
[D1.6, D5.1–D5.3].

## 8. Non-goals and superseded Result18 items

- Do not restore flex-int or box-float overflow lowering; v5 S4.1/D2.2.2 governs `int`.
- Do not add another typed-array direct-addressing path; C1/C2 already landed.
- Do not remove comprehension output based only on `AST_NODE_FOR_STAM`.
- Do not remove root clearing from a store-before-first-call guess; use the canonical
  safepoint-current liveness machinery and D8.6.3 forced-GC oracle.
- Do not add Lambda inline caches; specialization-over-caching remains D8.4.1.
- Do not modify `--c2mir`, `transpile.cpp`, `lambda/mir/`, or another vendored dependency [D1.6].
- Do not carry forward the old 2–3x aggregate forecast. Each phase retains only measured wins on
  a fixed current population.
- The historical brainfuck modulo workaround and binarytrees named-map admission diagnosis are
  resolved/superseded; neither is Tune13 work.

## 9. Gates and acceptance

### 9.1 Correctness and structural gates

- `make test-lambda-baseline`: 100% after each retained phase.
- MIR emission ratchet: update `test/mir/mir_budgets.json` in the same commit for justified
  growth; decreases tighten automatically [D8.6.1].
- Add `mir-check` coverage for guarded array stores/fallback, stable occurrence calls,
  result-unused and result-observed loops, native recursive returns, boxed/error returns, and
  zero-root versus managed frames [D8.6.2].
- Run forced-GC plus poison sweeps for every ownership, capability, scalar-home, or root-liveness
  change; stressed output must byte-match the unstressed run [D8.6.3].
- Preserve OOB-null reads, checked-write rejection, COW isolation, `var` caller write-back,
  views/N-D fallback, nullable lanes, named contracts, error propagation, stack overflow, and
  async/closure behavior.

### 9.2 Per-phase performance acceptance

- **P0:** the typed write micro no longer scales quadratically; target store-bound rows improve by
  at least 20%; no target/control regression above 5%; whole-container validation and full-clone
  counters approach zero on proved unique compatible stores.
- **P1:** affected MIR loses the intended boundary/helper/comprehension calls; at least one
  affected target improves by 10%; value-producing loop controls remain unchanged.
- **P2:** do not enter without the 10% profile attribution. Retain only if at least one fib-class
  target improves by 15%, its MIR loses `fn_add`/dynamic scalar-home work on the proved native
  path, and managed/error controls do not regress.
- **P3:** follow its own entry profile; retain a string change only with at least 20% target
  improvement and lower measured allocation/bytes copied.

### 9.3 Integrated release evidence

- Build and benchmark release only; debug MIR dumps are structural evidence, never timing data.
- Run the fixed Result22 population plus current restored rows, reporting both headline and fixed
  matched geomeans so missing cells cannot improve the result silently.
- Report MIR typed/current baseline, MIR typed/C2MIR, and MIR typed/Node, with exact output and exit
  status for every row.
- Keep the C2MIR column as a static reference through MIR's `mac-deps/mir/c2m`; never make legacy
  `--c2mir` a validation gate [D1.6].

## 10. Completion definition

Tune13 is complete: P0 and P1 are retained with interleaved archived/current target evidence; P2
and P3 are explicitly deferred because their entry profiles did not meet the stated thresholds;
all structural, correctness, forced-GC, Test262, and release gates pass; and this document records
the retained code, fixed population, hashes, counters, and rejected unsafe proof experiment.

The governing rule is:

> Enforce the full contract once, carry its semantic and representation proof explicitly, and
> keep native work native only while that proof and its owner remain valid.
