# Lambda / LambdaJS Performance Tuning Proposal

- **Date:** 2026-07-03; **rev 2: 2026-07-22** (implemented items removed, remainder re-ranked)
- **Co-Author:** Anthropic Fable
- **Scope:** LambdaJS engine (`lambda/js/`) primarily; the shared runtime (`lambda/runtime/`, `lambda/runtime/gc/`) and the Lambda-script MIR transpiler (`lambda/transpile-mir.cpp`) where they share the cost.
- **Baseline:** `test/benchmark/Overall_Result9.md` (2026-06-27) — **now stale**: every major runtime redesign below post-dates it. Producing Result10 is proposal R0.
- **Convention:** `file:line` references drift; confirm against the symbol name. Line refs in this revision were re-verified 2026-07-22.
- **Related docs:** [JS_15 — Performance & Optimization](../doc/dev/js/JS_15_Performance.md), [JS_03 — Value Model](../doc/dev/js/JS_03_Value_Model.md), [JS_04 — MIR Lowering](../doc/dev/js/JS_04_MIR_Lowering.md), `Lambda_Impl_JS_Tune.md` (Tune-6 plan), `Lambda_Issues_Outstanding.md` (OI ledger), `Lambda_Design_Stack_Frame.md` / `Lambda_Design_Stack_Rooting.md` / `Lambda_Design_Scalar_GC_Invariant.md` (the designs that superseded Parts 2–3).

> **Revision note.** The 2026-07-03 original (six parts, ~670 lines) is in git history.
> Since it was written, its two deep-design parts (stack-boxed scalars, root-frame
> elimination) and its representation proposal (inline self-tagged doubles) were
> implemented — mostly via successor designs with their own docs. This revision
> keeps: a ledger of what landed (§1), the updated wall analysis (§2), the
> re-ranked remaining proposals (§3), and the settled design verdicts worth
> keeping on record (§4).

---

# §1 — Ledger: proposal items that landed

| Original item | Outcome | Where it lives now |
|---|---|---|
| **Part 5 — inline doubles via high-byte float self-tagging** | **LANDED 2026-07-16, unconditional** (no compile flag; the S0–S3 phases collapsed into one landing) | `Lambda_Impl_Double_Boxing (done).md`. `push_d` is now just `flt2it` (`lambda/runtime/lambda-mem.cpp:807`); in-band doubles are inline `Item`s; `ITEM_DBL_MASK` test sequences are emitted inline by both transpilers (`transpile-mir.cpp:1340`); ±0 has dedicated encodings (`ITEM_FLOAT_P0`/`ITEM_FLOAT_N0`); the out-of-band residue (subnormals, \|x\| ⪆ 10¹⁵⁴) goes to the **side number stack**, not a GC nursery |
| **Part 2 — stack allocation of boxed 64-bit scalars** | **Superseded and landed via a different design** | `Lambda_Design_Stack_Frame.md` SF1–SF20 + Stack API Phase 7, implemented in `Lambda_Impl_Stack_Frame (done).md`, `…_JS (done).md`, `…_JS2 (done).md`, and `Lambda_Impl_Sized_Int (done).md` (2026-07-21). INT64/UINT64/out-of-band-float **transients live on the side number stack**; escape is handled by **caller-donated canonical scalar homes** (`em_adopt_scalar_item`, `incoming_scalar_home`) and **destination-owned storage** in containers, not by sink-side range checks; FLOAT has a **scalar return lane** (`RETURN_LANE_SCALAR`, `Context.mir_return_lane`). Part 2's escape/aliasing analysis is obsolete — the ownership model replaced it |
| **Part 3 — per-call JIT GC root-frame overhead (L0–L4)** | **Machinery deleted wholesale; every layer realized or exceeded** | `heap_jit_gc_root_frame_enter/_set/_exit` no longer exists. The frame subsystem emits **inline side-root stack watermarks** (`emit_jit_root_frame_enter/exit`, `transpile-mir.cpp:516`; `em_finalize_frame_prologue` post-pass sizes/elides frames = L1+L2). Root *population* is governed by GC-effect import metadata + **safepoint-current canonical slots** (= L3). Native-stack root discovery is **retired from every build** (beyond L4). Authority: `Lambda_Design_Stack_Rooting.md` — **S0–S6 complete**, release Test262 fully green. (Interim history: the first shadow-copy rooting cut measured 7.59× MIR — `Lambda_Stack_JS_MIR.md` R0–R10 — before S0–S6 replaced it) |
| **Part 3 L4.1 — int64/datetime nursery lifecycle** | **Superseded** | Transient wide scalars never reach the GC heap (number stack). The remaining persistent-ownerless fallback is `lambda_item_heap_rehome()` (`lambda/scalar_heap.cpp`), counted per type; **eliminating even that** is `Lambda_Design_Scalar_GC_Invariant.md` SG1–SG8 (proposed 2026-07-22, direction confirmed — owned there, not here) |
| **Part 4 — high-byte vs NaN-boxing assessment** | **Executed** | Recommendation ("keep high-byte, finish road 2, evaluate self-tagging before NaN-boxing") was followed; Part 5 landed. Verdict kept in §4.1 |
| **T0 — bisect the March→June regression** | **Root-caused; bisect moot** | The mechanism was per-call rooting + GC churn; the entire subsystem was since redesigned (honest typing → frame subsystem → safepoint rooting). The Result4/Result9 comparison no longer describes any code that exists. Successor: **R0 re-baseline** |
| **T3 — literal shapes (partial)** | **Pre-shaped creation landed; sharing did not** | `jm_transpile_object` (`js_mir_expression_lowering.cpp:11645`) now calls `js_new_object_with_shape` (`js_runtime.cpp:2771`): pre-sized data buffer, `slot_entries[]`, reserved-mask fast writes. **Still per-instance**: each literal evaluation builds its own TypeMap + ShapeEntry chain + hash — shapes are not shared per callsite, so ICs cannot go monomorphic on literal objects. Residue → **R3** |
| **T2.1 (partial) — native-gate return inference** | **P6 re-inference landed** | `jm_p6_reinfer_return_type` recovers typed returns for ANY-return functions with typed params. The numeric-return **requirement** itself still stands → **R4.1** |
| **Part 6 (slice) — redundant exception polls** | **Consecutive-check dedup landed** | `jm_emit_pending_exception_check` (`js_mir_completion.cpp:139`) elides back-to-back checks with the same target. The check itself is still a C call → **R5** |

---

# §2 — State of the walls (updated 2026-07-22)

Result9's headline (LambdaJS/Node dedup geo mean **13.1×**; disaster tail splay 392×, deltablue 304×, matmul 176×, nbody 163×, richards 112×) predates everything in §1. The walls, re-assessed against current code:

- **Wall 1 — boxed array elements** (matmul/navier_stokes/cube3d/mandelbrot). The *allocation* half collapsed: an element write of an in-band double no longer allocates anything (`push_d` = `flt2it`), and ±0 is packed. What remains: the ~20-instruction guard preamble per access, boxed-`Item` loads with unbox round-trips through C helpers, INT×INT via doubles, and no SIMD-friendly layout. The full fix (element kinds) is **deferred by decision** to OI-9 — see R9.
- **Wall 2 — property/method dispatch** (richards/deltablue/splay). Unchanged. An IC *hit* is still a full C call (`js_property_access_named_ic` emitted at `js_mir_expression_lowering.cpp:11438`); there is still no method/prototype IC (the PIC design was withdrawn from Tune-6 to OI-6 for its own design pass); literals are now pre-shaped but not shape-shared (§1 T3 row).
- **Wall 3 — dynamic call overhead**. Unchanged. `js_call_function` keeps the validation cascade + save/restore of ~12 globals (~61 `js_current_this`/`js_new_target` touch points in `js_runtime.cpp`) + the argc switch; the native dual-version gate still requires a numeric return type (`js_mir_module_batch_lowering.cpp:5781`).
- **Wall 4 — allocation & GC** (splay/gcbench/storage). Transformed but not closed. Transient scalars left the GC heap entirely (inline doubles + number stack); the rooting/promotion apparatus is gone. What remains is **object churn**: per-literal TypeMap/ShapeEntry/hash allocation (R3), whole-heap mark-and-sweep (now with an adaptive threshold, but still non-generational — only generation *tag bits* exist, `gc_heap.h:40`), and mark-time field tracing that still walks the ShapeEntry linked list (`lambda/runtime/gc/gc_heap.c:1319`) instead of `slot_entries[]`. See R7.

Still *not* the problem (unchanged from rev 1): closure variable access (env indexing), shaped-constructor field access (`js_get_slot_f` raw slots), compile/link time (out of scope for `__TIMING__` benchmarks), and the pure-numeric parity cluster.

**Unknown until R0:** how much of the 100–400× tail the §1 landings already recovered, and whether the interim regressions (shadow-copy rooting era) are fully paid back. No overall benchmark run has been recorded since Result9.

---

# §3 — Remaining proposals, ranked

Most important first. Execution order differs slightly (small prerequisites early); see §5.

### R0 — Re-baseline: produce `Overall_Result10.md` — **DONE 2026-07-22; outcome: master is not baseline-able yet**

Executed on commit `e3b6f358`: clean `make release`, instrumentation check passed, same host/Node v22.13.0/QuickJS 2025-09-13/3-run-median `__TIMING__`/180 s protocol as Result9. Report: [`Overall_Result10.md`](../test/benchmark/Overall_Result10.md); data `benchmark_results_v10.json`; comparison blocks are stored in that JSON's `_metadata.historical_comparisons` (regenerate with `inject_v10_history.py` + `gen_overall_result.py`).

**R0 did not produce a usable floor — it surfaced three regressions that outrank every tuning item below.** They are now R0a–R0c and must clear before R1 starts; until then any ranking here stays provisional, exactly as this item warned.

| | Finding | Evidence |
|---|---|---|
| **R0a** | **MIR-Direct aborts on 12 benchmarks that ran in Result9** — 10 × `SIGABRT` (exit 134) with `mir-scalar-invariant: unresolved call retains scalar home`, plus `r7rs/fft` (type checker now rejects `float`→`int` var assign) and `beng/pidigits` (parser: `Unexpected syntax near '1'`) | Introduced by commit `e30dc677b` "impl scalar GC invariant", which replaced the `em_heap_rehome_item_arg()` fallback in `em_emit_unknown_call` (`lambda/mir_emitter_shared.hpp:2654`) with a hard `abort()`. Repro: `./lambda.exe run test/benchmark/jetstream/deltablue2.ls` |
| **R0b** | **LambdaJS lost an order of magnitude on previously-fast small/mid benchmarks** while the disaster tail collapsed as designed | `awfy/sieve` 0.49 ms → 50.1 ms (**102× slower absolute**), `larceny/puzzle` 22.7 → 769 ms, `array1` 3.40 → 28.2 ms, `primes` 15.9 → 105 ms. Node's own times on the same rows moved < 10 %, so this is engine movement, not host noise. Shape suggests **lost native specialization** |
| **R0c** | **`make test-lambda-baseline` cannot build** — the project's mandatory 100 % gate is unrunnable | Debug test build fails `-Werror,-Wunused-function` on two dead statics in `lambda/py/transpile_py_mir.cpp:161` and `:210` (`pm_create_hosted_item_function`, `…_proto` — defined once, referenced nowhere) |

**What R0 does establish.** The §1 landings delivered exactly the predicted effect on the boxing/dispatch tail, measured like-for-like: **splay 392× → 14.6×** (26.8× better), **deltablue 266× → 19.0×** (14×), **cube3d 198× → 50.4×**, **matmul 176× → 54.9×**, **richards 119× → 37.5×**, plus mandelbrot/mbrot/bounce/storage/list/collatz. The Wall 1 and Wall 4 thesis is confirmed. But the aggregate moved the *wrong* way — dedup geo mean 13.1× → 19.9× all-timed, **13.1× → 16.7× like-for-like** on the 53 rows common to both rounds (20 improved, 33 regressed) — because R0b's broad regression outweighs the tail win, and because `cd` (2401×) and `hashmap` (2121×) are timed for the first time in Result10 and raise the mean by construction. Lambda/MIR is **not comparable at all**: 45 of 56 rows survive, so its 4.26× → 7.39× is a different benchmark population.

**Do not re-rank R1–R8 on these numbers.** Fix R0a–R0c, re-run (`Result11`), then rank. The value R0 delivered is the three regressions, not a floor.

### R1 — Execute Tune-6, Tracks A → D → B → E *(design-ready; plan already written; not started)*

`Lambda_Impl_JS_Tune.md` (draft v2, 2026-07-16) is the committed next implementation round; its tracks absorb most of rev 1's T6 list:

- **Track A — ADD inference recovery** (~17× on `ack`-class additive recursion; evidence-predicate + fixed-point return inference; string-concat soundness fixtures first). Cheapest, highest ROI — first.
- **Track D — realm-scoped intrinsic-prototype cache + pristine-tamper flags** (retires per-call `arr.push`-override checks; base64 43×, knucleotide 22×; the reverted process-global cache's realm-leak is the recorded failure mode to avoid).
- **Track B — int multiply fast path + float register residency** (INT×INT without the I2D/DMUL/D2I detour when provably int32; keep doubles in `MIR_T_D` across statements; B2 unboxed storage explicitly out of scope → OI-9).
- **Track E — mechanical scaling fixes** (DOM wrapper/listener storage, per-compile transpiler struct, test262 fast-path production gating).

Gates and benchmark set are specified in that plan; record completion tables there, not here.

### R2 — Inline the IC fast path; add a method/prototype PIC *(highest single lever on the dispatch tail)*

Two halves, separately actionable:

1. **Inline the existing load/store IC hit path** (mechanical): at each named-access site emit `load map->type` → compare against `ic->entries[0].shape` → on match load `map->data[offset]` (through `slot_entries` for typed coercion); call `js_property_access_named_ic` only on guard failure. Hit cost drops from ~35 insns + C call to ~4 inline insns. The IC structs, shape pointers, and hit/miss profile counters all exist — only the fast path lives on the wrong side of the call boundary.
2. **Method/prototype PIC** (needs design first): `obj.m()` still re-runs `js_prototype_lookup_ex` per call; richards/deltablue defeat the compile-time devirtualizer by design. The PIC was **withdrawn from Tune-6 (2026-07-16)** — its design record, constraints (data-driven side table, no code patching, no deopt), and the open invalidation-granularity decision (per-realm version vs per-shape counters — the latter recommended) live in `Lambda_Issues_Outstanding.md` **OI-6**. Do that design pass, then implement.

Prerequisite for literal receivers to benefit: R3.

### R3 — Per-callsite *shared* literal shapes *(small; unlocks R2 for literal-heavy code; finishes T3)*

The constructor path already has exactly the needed pattern: `js_constructor_create_object_shaped_cached` (`js_runtime.cpp:3011`) captures the first instance's `TypeMap*` into a per-site cache slot and subsequent instances take `js_new_object_with_typemap`. Thread the same `void** shape_cache` through the literal lowering (`jm_transpile_object` → `js_new_object_with_shape`), keyed per callsite (the baked `shape_names` array pointer already identifies the site). Kills the per-instance TypeMap + ShapeEntry-chain + hash-build allocations (§1 T3 row) and makes literal objects shape-shared so R2's ICs can go monomorphic on them. Direct wins: splay, gcbench, storage, json. Precedent flag: `LAMBDA_JS_SHARED_CTOR_SHAPE`.

### R4 — Slim the call protocol; widen the native gate *(Wall 3; three independent sub-items)*

1. **Drop the numeric-return requirement** on native dual versions (`js_mir_module_batch_lowering.cpp:5781`): require numeric params only; box the return at the boundary. P6 re-inference already rescues ANY-returns that are *actually* numeric; the remaining exclusion is genuinely-void hot loops (nbody `advance`, cube3d, raytrace3d) — still the smallest diff with an outsized effect.
2. **Fast lane in `js_call_function`**: one flag bit on `JsFunction` meaning "plain user function — not generator/async/bound/proxy/ctor-special"; skip the validation cascade when set.
3. **Pass `this`/`new.target` as real ABI arguments** instead of saving/restoring ~12 globals per call. Combined with R2's method IC, a monomorphic method call becomes shape-guard → direct `MIR_CALL`.
4. (Carried T6 bullet) **MIR-inlinable hot helpers**: MIR's `process_inlines` cannot inline native C imports; emit the smallest hot helpers (`js_get_slot_f`-class) as MIR functions so their call overhead disappears.

### R5 — Cheapen the emitted exception check *(mechanical; multiplies with R4 on call-dense code)*

The per-site check is still a C call: `jm_emit_pending_exception_check` emits `CALL js_check_exception` + branch (`js_mir_completion.cpp:139`; ~155 emission sites fire after nearly every runtime call). The consecutive-poll dedup already landed; the remaining plan is unchanged from rev 1 §6.5:

- **E1 — inline the flag load**: `js_exception_pending` sits at a link-time-constant address in the `JsRuntimeState` singleton; emit a one-byte load + `BT` (precedent: ~59 baked realm pointers already in emitted MIR). ~15 cycles → ~2–4 per site; works identically under JIT and MIR-interp.
- **E2 — elide checks after provably non-throwing helpers**: add a `can_throw` bit to the runtime-helper registry metadata; skip emission after helpers that cannot throw. Pure emission-volume reduction (also shrinks code size).
- **E3 — coalesce checks across guarded call sequences**: only if E1+E2 profiling still shows overhead; the ~395 entry-guarded helpers make it *possible*, but exception-ordering test262 clusters make it *dangerous* — evidence-gated.

### R6 — MIR-Direct specialization parity with frozen C2MIR *(Lambda-script side)*

The MIR-emission-test work (`Lambda_Design_MIR_Emission_Test.md`, P1/P2 green 2026-07-22) verified that MIR-Direct lacks two specializations the frozen C2MIR path has: **unboxed sys-func calls** and **native-math lowering** — and some Lambda tests still run faster under `--c2mir` than under MIR-Direct (the MT8 benchmark-tool deferral records this). With C2MIR frozen (CLAUDE.md rule 14), these are MIR-Direct debt, not optional wins. Port both specializations into `transpile-mir.cpp`; the `.mir-check` sidecars + MT7 ratchet (`test/mir/mir_budgets.json`) are the regression harness this work previously lacked.

**R6b — M4: boxed element loads / `any` arithmetic** *(added 2026-07-23 from `Lambda_Impl_Tune.md` §6.4)*. With M1/M2 landed, index arithmetic is inline but each array-element load still returns a boxed Item whose arithmetic takes the boxed `fn_add` fallback — the residue behind matmul/triangl/array1's flat rows. Fix shape: inline type-test + native fast path on the ANY arm, or unboxed typed-element access. Companion to the two ports above; same harness.

### R7 — Object-churn GC: generational collection within the non-moving constraint *(evidence-gated on R0)*

Re-scoped from rev 1's T4 now that transient scalars never reach the heap: the target is purely **object churn** (splay/gcbench/binarytrees-class allocate-fast-die-young workloads, literal objects, strings). Content unchanged in essence:

- **Sticky mark bits** for non-moving minor collections (generation tag bits already exist in the header — `GC_GEN_NURSERY`/`GC_GEN_TENURED`, `gc_heap.h:40` — but only full collections are implemented today; the adaptive threshold that landed is a tuning knob, not a generational scheme).
- **Write barrier** on old→young pointer stores — property/array/env writes are already centralized C entry points, so emission sites are few.
- **Lazy sweeping**, and switch mark-time field tracing from the ShapeEntry linked list (`gc_heap.c:1319`) to `slot_entries[]`.

Do this only after R0 shows how much whole-heap-mark pressure actually remains, and after R3 (which removes a large slice of the churn at the source).

### R8 — Unify the Lambda ↔ JS error models at the interop boundary *(correctness/API value more than raw perf)*

Unchanged from rev 1 §6.6; none of it exists in-tree yet (no `js_result_from_lambda`/`lambda_result_from_js`/`ERR_JS_EXCEPTION` symbols). Condensed:

> Do not unify the propagation mechanisms (each is right for its language — §4.2). Unify the error **value**, and fix a strict two-function conversion protocol at the boundary.

- **U1 — one payload, two views**: add a GC-traced `Item payload` to `LambdaError`; JS→Lambda wraps the thrown value losslessly (`ERR_JS_EXCEPTION`); Lambda→JS exposes an `Error`-like object (`.name` mapped from `LambdaErrorCode`, `.message`, `.code`, `.help`) holding a reference to the original so **round trips preserve identity** both ways.
- **U2 — two choke points**: `js_result_from_lambda(Item)` (one tag compare on success path; on `LMD_TYPE_ERROR`, throw the wrapped value) and `lambda_result_from_js(Item)` (one flag load; if pending, clear and return `err2it(...)` — a JS `throw` surfaces to Lambda as ordinary `T^E`, so `let x^err = call()` becomes the cross-language catch). Registry support: an ABI bit on `sys_func_registry` entries (lambda-error-returning vs js-throwing) so the JS transpiler auto-wraps lambda-ABI helpers at emission.
- **U3 — boundary invariant**: the pending flag is never set while control is in Lambda code; an `ItemError` never enters JS expression evaluation; enforce with debug boundary assertions.
- **U4 — semantics mapping**: `raise`/`T^E`/`?`/`let x^err` ⇔ `throw`/propagation/`try-catch`; error `cause` chain ⇔ `Error.cause`.

Validation: round-trip identity tests both directions; double-reporting is the main risk (U2 functions must consume their source-side state).

## Parked (decided-deferred; do not start piecemeal)

### R9 — Dense-double / dense-int element kinds for JS arrays *(deferred to OI-9 by decision, 2026-07-16)*

Rev 1's T5. The unboxed-storage end-state (shaped slots *and* array element kinds, all scalar kinds, maps *and* arrays) is deliberately **one design, done once** — decision record, predecisions, and open questions live in `Lambda_Issues_Outstanding.md` **OI-9** (candidate doc `Lambda_Design_Shaped_Slots.md`). Self-tagged doubles already removed the allocation half of the array wall; what element kinds would still buy is raw `MIR_T_D` loads (no guards, no unbox round-trip) and SIMD-friendly layout. Tune-6 Track B's measurements are the go/no-go input.

### R10 — Destination-passing lowering *(biggest codegen project; last)*

Emitted MIR remains 66–88 % MOVs. Structural fix, big and risky; per OI-6 it stays parked until the cheaper levers above land and Result10/11 show what's left. Gate on full test262 + Radiant baselines.

---

# §4 — Settled assessments (kept for the record)

### 4.1 High-byte tagging vs NaN-boxing — **decided: keep high-byte; executed**

For untyped float-heavy dynamic code NaN-boxing wins decisively *in the abstract* — but for Lambda as a system (multi-format data language, 20+ types, `NUM_SIZED` inline scalars, int56, four frontends on one substrate, GC pointer discrimination and container APIs all keyed on the tag byte) high-byte tagging is the better design, and a migration would be a ~50 % runtime refactor for one type category. The V8 lesson (representation matters in inverse proportion to type specialization) pointed at road 2 — typed storage + native paths — plus the self-tagging carve-out for doubles. **That is what shipped**: shaped slots, native `MIR_T_D` paths, and Part 5's no-rotation high-byte float self-tagging (bias `0x2000…`, bit-62 discrimination, `0x40–0x7F`/`0xC0–0xFF` inline band, sentinels renumbered out of the band). Revisit the representation question only if float boxing ever reappears in profiles — it should not.

### 4.2 Exception-model architecture — **decided: flag + emitted checks is right for this engine**

LambdaJS's pending-flag + compiler-emitted checks vs V8's zero-cost handler tables vs Go's two-tier model: the zero-cost designs are funded by frame metadata (handler tables, stack maps, deopt info) that MIR does not have and the MIR interpreter could not honor; frames here unwind by returning normally, which is also what keeps the args stack and scope state correct on every path. The alignment worth remembering: **Lambda-script ≈ Go's error tier** (errors as values, `T^E`/`?` — designed in from day one), **LambdaJS ≈ flag-polling forced by MIR's constraints**, **V8 ≈ Go's panic tier taken to the limit**. Sentinel-return retrofit onto the JS helper surface was examined and rejected. What is *not* architectural is the cost of each check — that is R5. Full comparison tables: rev 1 §6.1–6.4 in git history.

---

# §5 — Sequencing

**R0a + R0c (unbreak master: scalar-invariant abort, dead-code build failure) → R0b (root-cause the lost native specialization) → re-run as Result11 → R1 Tune-6 (A → D → B → E) → R3 (small, feeds R2) → R2 (IC inline, then PIC design + impl) → R4 + R5 (call protocol + check inlining) → R6 (MIR-Direct parity, can run parallel to JS-side work) → R7 (only on Result11 evidence) → R8 (when interop demand warrants). R9/R10 parked.**

R0c is a few minutes' work and unblocks the correctness gate that R0a's fix will need. R0a and R0b are both plausibly consequences of the same recent scalar-ownership/stack-frame landings, so investigate them together before assuming two independent causes.

Gates throughout, unchanged: full test262, `make test-lambda-baseline` 100 %, Radiant baseline where object-model or GC behavior changes, ASan on lifetime-touching changes, release-build-only perf numbers (CLAUDE.md rule 10), and per-change before/after tables on the fixed benchmark set.

Rev 1's calibration ("the 100–400× tail should land in the 10–30× range, dedup geo mean plausibly 3–5× vs Node — without a NaN-boxing rewrite") still stands as the target, but part of it may already be banked by the §1 landings — R0 tells us how much.
