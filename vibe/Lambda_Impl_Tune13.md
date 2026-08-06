# Tune 13: Result22 Typed-Lane Regressions and the Path Toward the C2MIR Ceiling

- **Date:** 2026-08-06
- **Input:** `test/benchmark/Overall_Result22.md` (Lambda commit `4babb408a2`)
- **Status:** PROPOSAL — Result22 array root causes verified against the archived v22 binary;
  the still-live Result18 M1–M8 work is consolidated here and must be re-profiled on the
  current release candidate before implementation
- **Related:** `vibe/Lambda_Tune_Typed_Vs_C2MIR.md` (historical Result18 dissection),
  `vibe/Lambda_Impl_Tune11 (done).md`, `vibe/Lambda_Impl_Tune12.md`,
  `vibe/Lambda_Design_Type_Enforcement.md`, `vibe/Lambda_Design_Compiling_Lane.md`,
  and `vibe/Lambda_Semantics_Int_Type.md`
- **Formal authority:** `doc/Lambda_Formal_Semantics.md` S1.4–S1.6, S4.1, S4.5.3,
  S11.4; `doc/Lambda_Formal_Design.md` D1.4–D1.6, D2.2–D2.6, D3.2–D3.3,
  D4.4, D5.1–D5.3, D8.3–D8.4, D8.6

## 1. Headline numbers

| Metric | Result18 | Result22 |
|---|---:|---:|
| MIR (untyped)/Node geo | — | 2.75x |
| MIR (typed)/Node geo | — | 2.27x |
| MIR (typed)/C2MIR geo | 9.48x | **11.2x** |

The typed column is only about 1.2x better than untyped in geomean, and on 16 of 56 rows it is
slower than untyped — in the worst cases by 4–9.5x (`quicksort` 10.2→40.8 ms, `matmul`
18.0→71.3 ms, `bounce` 0.31→2.97 ms, `nqueens` 1.93→5.90 ms, `fft` 2.67→5.12 ms,
`permute` 1.02→3.09 ms). The widest static-ceiling gaps are concentrated in rows with hot
annotated-array access and mutation, but the remaining recursion, statement-loop, string, and
allocation gaps are distinct tracks rather than variants of one array defect.

Result18 and Result22 are different compiler/semantics snapshots. Their aggregate comparison is
directional evidence only; every Tune13 acceptance decision uses an interleaved current-baseline
and candidate release run over an identical fixed population.

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

## 6. Non-goals and superseded Result18 items

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

## 7. Gates and acceptance

### 7.1 Correctness and structural gates

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

### 7.2 Per-phase performance acceptance

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

### 7.3 Integrated release evidence

- Build and benchmark release only; debug MIR dumps are structural evidence, never timing data.
- Run the fixed Result22 population plus current restored rows, reporting both headline and fixed
  matched geomeans so missing cells cannot improve the result silently.
- Report MIR typed/current baseline, MIR typed/C2MIR, and MIR typed/Node, with exact output and exit
  status for every row.
- Keep the C2MIR column as a static reference through MIR's `mac-deps/mir/c2m`; never make legacy
  `--c2mir` a validation gate [D1.6].

## 8. Completion definition

Tune13 completes when P0 and P1 are implemented or rejected with interleaved release evidence;
P2/P3 are either entered through their profile gates and meet their acceptance thresholds or are
explicitly deferred; all structural/correctness gates pass; and the final document records the
retained code, benchmark population, hashes, counters, and rejected experiments.

The governing rule is:

> Enforce the full contract once, carry its semantic and representation proof explicitly, and
> keep native work native only while that proof and its owner remain valid.
