# Tune 13: Result22 Typed-Lane Regressions and the Path Toward the C2MIR Ceiling

- **Date:** 2026-08-06
- **Input:** `test/benchmark/Overall_Result22.md` (Lambda commit `4babb408a2`)
- **Status:** ANALYSIS — root causes verified empirically against the archived v22 binary via `LAMBDA_MIR_DUMP_PATH` dumps and micro-benchmarks in `temp/r22/`
- **Related:** `vibe/Lambda_Tune_Typed_Vs_C2MIR.md` (Result18 dissection, M1–M8), `vibe/Lambda_Design_Type_Enforcement.md` (TE-15..TE-18), `vibe/Lambda_Design_Compiline_Lane.md` (ValueRep), C16 / v5 int migration (`vibe/Lambda_Semantics_Int_Type.md`)

## 1. Headline numbers

| Metric | Result18 | Result22 |
|---|---:|---:|
| MIR (untyped)/Node geo | — | 2.75x |
| MIR (typed)/Node geo | — | 2.27x |
| MIR (typed)/C2MIR geo | 9.48x | **11.2x (worse)** |

The typed column is only ~1.2x better than untyped in geomean, and on 16 of 56 rows it is **slower** than untyped — in the worst cases by 4–9.5x (quicksort 10.2→40.8ms, matmul 18.0→71.3ms, bounce 0.31→2.97ms, nqueens 1.93→5.90ms, fft 2.67→5.12ms, permute 1.02→3.09ms). The widest static-ceiling gaps (quicksort 204x, fft 182x, permute/bounce 119x, base64 111x, towers 87x) are **exactly the rows where typed regresses below untyped**, and all share one workload shape: in-place element stores into annotated arrays. One front-end defect family explains nearly the entire top of the "Distance to the Static Ceiling" table.

## 2. Verified root causes (typed regressions)

### R22-1: Per-store checked-helper path on annotated arrays is O(n) per store — quadratic overall  **[dominant]**

Micro-benchmark (10k-element `int[]`, 10k-iteration loop, archived v22 binary):

| Variant | Time |
|---|---:|
| typed `var a:int[]`, read-only loop | 0.174 ms |
| typed, loop adds one write `a[i] = a[i] + 1` | **23.3 ms** |
| identical write loop, untyped | 0.05 ms |

One in-loop store makes the typed version **465x slower** than untyped. Mechanism, verified in the MIR dump and `lambda-eval.cpp`:

1. `transpile-mir.cpp` (assign lowering, ~line 16528): the inline "native write witness" requires `value_type == typed_root->elem_type` — an **AST-level** type equality. Element-read types are not resolved in the AST (by design, see comment at `mir_store_may_change_elem_type`), so any RHS containing an element read (`a[i] + 1`, `arr[j]`, `0 - axv`) has `value_type == ANY` and fails the witness — even though the MIR lowering has *already computed the value in the native int lane* with range proofs.
2. The store then calls `lambda_array_set_checked` (locals) / `_inplace` (var params). Per store, the non-inplace helper does:
   - `fn_mutable_value(owner)` — **a full clone of the array** (`lambda-eval.cpp:8040`);
   - `lambda_type_check` on the value (fair) **plus** `lambda_type_matches(candidate, int[])` post-store, which routes occurrence contracts (`TYPE_KIND_UNARY`/`REPEAT`) into **`runtime_validate_value_against_type` — the full schema validator** (`lambda-eval.cpp:1787-1794`).
   - The `_inplace` variant skips the clone but still pays the validator-backed `lambda_type_matches` on **entry and exit** per store.

10k stores × O(n) clone/validate = the observed 23 ms. This is a live violation of **TE-18** (enforcement lives at declaration boundaries only; the guard dominates the scope — interior re-checks are retired) and of **TE-17** (container acceptance is type-sensitive; native lanes gate on provable infallibility — an int-lane value entering `int[]` is provably infallible). It is the concrete mechanism behind the ledger's "V1 fn_array_set despecialization = live violation" note.

Affected Result22 rows (in-place stores into annotated arrays): quicksort, permute, towers, bounce, storage, nqueens, fft, matmul, base64, levenshtein, spectralnorm, pnpoly, navier_stokes, brainfuck (tape writes), splay/deltablue partially. This is essentially the entire widest-gap table.

### R22-2: Annotation kills the inline read path for whole functions via the invalidation scan

In `bounce2.ls`, all 12 in-loop element **reads** compile to boxed `fn_index` calls (+ `lambda_item_to_int_lane` decode), while untyped bounce inlines them. Cause: the declaration-site element witness (`elem_type` + `elem_type_guarded`, transpile-mir.cpp ~9676) is dropped when `has_elem_type_invalidation` finds any store whose RHS resolves to a concrete non-`int` AST type. Flexint arithmetic (`0 - axv` → semantic `integer`/DECIMAL carrier) triggers this, so **one syntactic form poisons every array access in the function**. The scan's conservatism is justified only for the *unguarded* fill-narrowing witness; the annotated witness is **guarded** — the inline load re-checks the runtime representation and falls back to `item_at`, so a despecialized array costs a fallback, never a wrong read. Dropping the guarded witness is doubly conservative and should not happen at all.

Also visible in `quicksort2`: 6 boxed `fn_index` read sites + 15 `lambda_item_to_int_lane_c` decodes where the untyped version reads inline.

### R22-3: Redundant per-call container boundary checks

`mir_boundary_is_redundant` elides re-checking a **stable annotated map binding** passed to a matching declared param (the JSON-parser fix), but has no equivalent for array/occurrence contracts. Every direct call passing an annotated `int[]` to a matching `int[]` param re-runs `emit_checked_boundary` → boxed `lambda_type_check`. Recursion-heavy typed rows (quicksort: ~10k calls; nqueens; cpstak 1.18→2.01) pay this per call. **DF15** (visibility elision) remains unimplemented; the cheaper targeted fix is extending the map-style stable-binding elision to occurrence contracts.

### R22-4: In-loop typed declarations emit per-iteration `lambda_type_check`

`var axv: int = abs(bxv[j])` inside the bounce inner loop → checked declaration boundary per iteration (12 static sites in bounce2), because `abs` returns ANY. Known-builtin return-type inference (abs over int → int) plus the T-A1 redundancy rule would elide these.

## 3. Why C2MIR is the right mirror

C2MIR shares MIR's code generator, so its 0.17x-of-Node geomean is what the backend delivers when the front end emits clean code. The typed gaps above are 100% front-end emission artifacts: annotations currently **install checks** (and in the array case, per-store clones + validator sweeps) instead of **pinning representations** (T-A1's framing). The C2MIR ports do the identical workloads with plain loads/stores; nothing about Lambda's semantics forces the gap — TE-17/TE-18 already rule that these interior checks should not exist.

## 4. Untyped/general gaps (secondary, unchanged from Result18 dissection)

- Per-call runtime overhead visible in every dump: `lambda_side_stack_ensure_for` + `lambda_stack_overflow_error` + number-frame enter + `lambda_restore_number_frame_top` + `lambda_item_adopt_scalar_home` pairs. For small recursive fns (fib typed 6.12ms vs C2MIR 1.10ms = 5.6x with *zero* typed-lane defects) this call-frame ceremony is the remaining gap. Inlining frame push/pop as raw MIR stores and eliding scalar-home adoption for provably non-escaping callees are the levers (Result18 M-items).
- navier_stokes (111x, LambdaJS beats MIR!), collatz, brainfuck: flexint poisoning of index arithmetic — the C16 int⊕int→int inference remains partially unimplemented in inference paths feeding `mir_store_may_change_elem_type` and index lowering.
- gcbench (10.8x, C2MIR itself 3.0x): allocation/GC pacing, separate track (GC tuning plan).

## 5. Proposal (priority order)

**P0 — TE-18-compliant array stores** (attacks quicksort 204x, fft 182x, permute/bounce 119x, base64 111x, …):

1. **Inline guarded store for annotated arrays.** Mirror the read path: runtime guard (ArrayNum tag + elem check, already emitted for guarded reads) + value-in-lane test → raw store; fall back to the checked helper only on guard failure. The value-side proof must come from the **lane the MIR lowering already computed** (extend ValueRep to expression results per the Compiling-Lane design — the int-lane add already carries range proofs; do NOT probe `MIR_reg_type`).
2. **De-quadratify `lambda_array_set_checked`.** (a) Element admission is `lambda_type_check(value, element_type)` — already O(1); drop the per-store whole-container `lambda_type_matches`/validator sweep: for ArrayNum the elem tag is an O(1) witness (TE-17). (b) Clone only when the owner is actually shared (COW Stage-2 exclusivity / `is_shared` bit), not unconditionally per store.
3. **Never drop a guarded elem witness.** Restrict `has_elem_type_invalidation` to the unguarded fill-narrowing witness; the annotated witness keeps its runtime guard and pays at most a fallback (fixes R22-2's whole-function read poisoning).

**P1 — call-boundary elisions:**

4. Extend the stable-annotated-binding recheck elision (`mir_boundary_is_redundant`) from map contracts to array/occurrence contracts (fixes R22-3). DF15 visibility elision remains the general solution but ships separately.
5. Builtin return types (abs/min/max/len over known lanes) into inference so in-loop `var x: int = abs(...)` declarations satisfy the T-A1 redundancy rule (fixes R22-4).

**P2 — the remaining static-ceiling gap (typed rows with no array stores):**

6. Inline number-frame enter/restore and batch side-stack/overflow checks (constant frame sizes are known at emission); elide scalar-home adoption for non-escaping scalars. Target: fib-class recursion from 5.6x → ~2x of C2MIR.
7. Finish C16 int⊕int→int in AST inference (unblocks navier_stokes/collatz/brainfuck and removes the flexint trigger in R22-2's scan).

**Expected effect:** P0 alone should return every typed row to ≥ its untyped result (removing the 4–9.5x regressions) and, on the store-bound rows, land near the untyped inline-store bound (micro-benchmark: 465x headroom). MIR(typed)/C2MIR geomean should drop from 11.2x to roughly the 4–6x band, with P1/P2 taking the recursion-heavy remainder.

**Measurement guard:** re-run `make test-lambda-baseline` (100%) and the typed suite after each phase; watch `LAMBDA_MIR_SPECIALIZATION_PROFILE=1` and the cow-profile counters (`array_checked_store_calls`, `_full_clone`) — both should go to ~0 on the benchmark corpus after P0.
