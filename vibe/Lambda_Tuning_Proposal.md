# Lambda / LambdaJS Performance Tuning Proposal

- **Status:** remaining-work proposal, rev 4
- **Revision date:** 2026-07-23
- **Scope:** LambdaJS (`lambda/js/`), the shared runtime, and Lambda
  MIR-Direct (`lambda/transpile-mir.cpp`)
- **Latest full benchmark:** `test/benchmark/Overall_Result10.md`
- **Related plans:** `Lambda_Impl_Tune_COW.md`, `Lambda_Impl_JS_Tune.md`,
  `Lambda_Issues_Outstanding.md`
- **Completed Lambda tuning records:** `Lambda_Impl_Tune1 (done).md`,
  `Lambda_Impl_Tune2 (done).md`, `Lambda_Impl_Tune3.md`

This revision contains only work that remains open. Implemented and
superseded items have been removed; their details remain in git history and in
their owning implementation records.

Result10 is still the latest complete cross-engine benchmark, but it predates
the landed Lambda M1/M2 numeric fixes and the planned COW Stage 1 work.
`Lambda_Impl_Tune_COW.md` owns the next full release run, Result11. The
Result10 LambdaJS measurements remain the current evidence for the separate
LambdaJS regression described in R0.

---

## 1. Governing constraints

### 1.1 JavaScript arithmetic remains binary64

JavaScript has no separate INT value type in LambdaJS. Ordinary JS numeric
values and arithmetic results use canonical binary64 (`LMD_TYPE_FLOAT`).
`MIR_T_I64` remains valid for implementation-only indices, lengths, bitwise
intermediates, guards, and Boolean conditions, but it must not choose the
observable representation of a JS arithmetic result.

**Future follow-up — transient integer-ALU strength reduction.** A future
optimization may use integer ALU instructions only as an invisible
intermediate when the compiler proves that the final binary64 value is
identical to the current double path. It must:

- never create or box an `LMD_TYPE_INT` JavaScript result;
- preserve signed zero and all required rounding and overflow behavior;
- fall back to the existing double operation whenever the proof does not hold;
- avoid per-operation double-to-int-to-double conversion;
- demonstrate a release-build win after all guards and conversions.

This is not part of Tune-6 and is not an implementation commitment.

### 1.2 Preserve the current runtime architecture

- New lowering uses the common Stack API, normalized call effects, precise
  rooting, scalar homes, and side-stack ownership.
- C2MIR is frozen. Optimization work targets MIR-Direct only.
- Existing JS and host in-place mutation APIs remain unchanged; Lambda COW
  policy stays in the `_cow` wrappers owned by the COW design.
- Fast paths retain the current semantic path as fallback.

### 1.3 Performance claims require a fresh floor

Use release builds, three-run medians, matched configurations, and
output-correctness checking before a timing enters an aggregate. MIR size or
instruction-count reduction is supporting evidence, not proof of a runtime
win.

**Current floor: Result13** (`test/benchmark/benchmark_results_v13.json`,
2026-07-26, commit `22eefe3f1`) — MIR/Node dedup geo **2.94x**, LambdaJS/Node
**15.4x**, QuickJS/Node **7.45x**.

**Three calibration facts, measured by running Result13 twice — once under
normal load and once on a deliberately quiesced machine** (run 1 kept as
`benchmark_results_v13_run1_noisy.json`):

1. **Same-day reproducibility is excellent.** Between the two runs, every
   engine's median wall time moved ≤0.3% (QuickJS 0.998, Node 0.997, MIR 1.000,
   LambdaJS 0.998; p10–p90 within 0.96–1.02), and the headline geo means moved
   ≤0.4%. A single 3-run snapshot is a reliable floor.
2. **Cross-date differences are systematic, not noise, and quiescing does not
   remove them.** Both runs sit ~10% faster than Result12 in absolute terms
   (median v13/v12: QuickJS 0.912, Node 0.885) and ~6% worse on the untouched
   QuickJS/Node control (7.00x → 7.45x). The cleanup was expected to close that
   gap and did not, which is what proves it is a stable property of the two
   snapshot dates rather than variance.
3. **Therefore the geo means cannot resolve movement below ~6%** across
   snapshots. Result13's MIR/Node −2% sits inside a control that moved +6% the
   other way, even though per-row Lambda wins of 1.3–2.5x are real and
   independently confirmed.

Practical consequences:

- **Do not use the matrix to validate a phase.** Use per-phase interleaved A/B
  (build both binaries first, then alternate runs row by row) and reserve the
  matrix for recording the floor after a track lands.
- **When comparing two snapshots, adjust per row by the QuickJS control** for
  that same row — that is what recovered the true Lambda speedups from
  Result13.
- **Check the Node denominator before believing a row-level regression.** Two
  apparent Result13 regressions (`awfy/nbody`, `jetstream/splay`) were Node
  getting faster, not Lambda getting slower; `awfy/nbody`'s Result12 Node time
  (25.9 ms) was itself an outlier against 6.0/7.6 ms on sibling rows.

---

## 2. Remaining proposals

### R0 — Complete Result11 and diagnose the LambdaJS small/mid regression

Three pieces form the next trustworthy floor:

1. Complete Lambda COW Stage 1 under `Lambda_Impl_Tune_COW.md`, including
   retirement of the eager mutable-clone anchor for the in-scope Lambda
   containers.
2. Run Result11 with the Result9/Result10 release protocol and an integrated
   output-correctness sweep.
3. Preserve the raw before/after timing data under `temp/` with timestamped
   JSON or CSV filenames instead of overwriting the previous profiling run.
   The generated aggregate report must identify the exact raw inputs.

In the same run, remeasure the Result10 LambdaJS regression cluster:
`awfy/sieve`, `larceny/puzzle`, `larceny/array1`, `primes`,
`navier_stokes`, `fannkuch`, `fasta`, and `nqueens`. If the regression
remains, profile the smallest exact reproductions and find the lost
specialization before re-ranking the general JS tuning queue.

Exit:

- Result11 records correct-output-only Lambda/MIR and LambdaJS aggregates;
- the LambdaJS regression has a measured root cause or is shown to have
  disappeared;
- R1–R8 are re-ranked against that floor.

### R1 — Execute the revised LambdaJS Tune-6 plan

`Lambda_Impl_JS_Tune.md` is the source of truth. Its remaining tracks are:

- **Track A — shaped-float register residency:** begin with read-only
  residency for proven constructor-shaped FLOAT fields; write coalescing is a
  separate measured decision.
- **Track B — realm-owned intrinsic-prototype fast paths:** define the
  realm/reset owner, consolidate the existing cache and tamper state,
  centralize invalidation, and convert only measured hot lookup families.
- **Track C — runtime and compile-time scaling:** add the event-listener target
  index, profile DOM name dispatch, replace fixed transpiler collections with
  pointer-stable source-sized storage, and gate Test262-only helpers out of
  production builds.

Tune-6 contains no native-INT arithmetic track. Its design questions and
per-track correctness/performance gates are maintained in the implementation
plan rather than duplicated here.

### R2 — Inline named-property IC hits, then design the method/prototype PIC — **KIV (2026-07-26)**

**Parked. Both halves were carried into Tune6 as J3 (inline the hit) and J1
(method/prototype PIC), and both were dropped there without implementation
because the Tune6 T0.1 census removed their premise.** Moved back here per
that decision; `Lambda_Impl_Tune6.md` no longer owns them.

Census evidence (release-profile build, raw data `temp/t6_census_*.tsv`):

| bench | load_ic probes | miss | megamorphic |
|---|---|---|---|
| richards | 1,116,046 | **109 (0.01%)** | 0 |
| deltablue | 1,421,395 | 11,715 (0.8%) | 0 |
| hashmap | 47,010,886 | 1,440,158 | 18,547,462 |

- **The PIC half (J1) has almost nothing to win.** It was premised on
  prototype-chain misses dominating; richards misses on 0.01% of probes. Do not
  build it until a fresh census shows proto misses actually dominating.
- **The inline half (J3) is real but small, and costlier than it reads.**
  richards/deltablue hit ~100%, so the only prize is the per-hit C-call tax.
  Scoping found `inline_kind` cannot be an emission-time constant (the cell
  fills at runtime), and `_map_read_field` has ~12 repr cases, so covering
  raw-Item/BOOL/INT/FLOAT is a 4-way runtime branch plus the shape guard — not
  one compare-and-load. A raw-Item-only cut would rarely fire, because
  `js_set_shaped_slot` retags entries to concrete types.
- Corroborating evidence from the Lambda side: the equivalent helper-level IC
  (Tune6 L2, `fn_member_ic`) bought **1.12–1.19x**, and its stage-2 inline
  emission was judged not worth the emission risk on top of that. Inlining the
  guard is a fraction of a change that was itself under 1.2x.

**Conclusion: further IC-based tuning is not where the remaining factor lives.**
Revisit only if a post-R4 profile shows property access back on top. If J3 is
ever picked up, the cheapest useful cut is guard + INT + raw-Item with
everything else falling through to the existing helper, measured before adding
FLOAT/BOOL; the verification bar is `js_exec_profile` branch counters identical
before and after across the fixture set. OI-6 remains the record for PIC
ownership and invalidation granularity.

### R2b — Realm-owned shared root shape for plain objects — **OPEN, highest-value LJS item**

Extracted from the Tune6 census; this, not R2/R3, is what the worst LJS rows are
waiting on.

**Result13 confirms the LJS column is entirely untouched** and still owns every
bad row: geo **15.4x**, and the five rows over 100x are unchanged — hashmap
**1121x**, havlak ~730x, cd ~260x, navier_stokes ~148x, spectralnorm ~105x.
Tune6 landed nothing on this engine, so these numbers are the standing target.
hashmap alone is a ~1.5x outlier over the next-worst row and ~9x over the third,
and is the single clearest opportunity in the whole benchmark set.

`hashmap`'s 18.5M megamorphic probes are **shape identity, not shape content**:
structurally identical instances (`Object.keys()` equal) carry pointer-distinct
TypeMaps, one per `run()`. The IC compares shape pointers and never re-warms
once `JS_LOAD_IC_MEGAMORPHIC` is set, so 5+ shapes means permanently
megamorphic. Verified: detach fires **zero** times, and the shapes carry
`is_shared_constructor_shape=0`, `is_transition_shared_shape=0`,
`is_private_clone=0` — they never entered any sharing scheme.

Cause: `map_put` (`lambda/input/input.cpp:474`) only consults the shared
transition table when the shape is *already* shared, so an object born unshaped
from `js_new_object()` grows a private TypeMap chain field by field.

Fix: give plain objects a shared empty root so field-by-field growth flows
through the existing (already correct, already shared) transition table and
structurally identical objects converge on one TypeMap. **Hang the root off
`Input` so it dies with the realm's pool — not off the process-global
`EmptyMap`**, per the realm-ownership constraint that the reverted global
prototype cache established.

### R3 — Share object-literal shapes per call site — **CLOSED: tried, measured, reverted (2026-07-26)**

Implemented in full as Tune6 J2 (`js_new_object_with_shape_cached` plus a
per-site cell in `jm_transpile_object`) and reverted. The mechanism works — a
literal receiver site went 199,996/200,000 megamorphic to 199,999/200,000
monomorphic — but it does not pay, because the megamorphism on these rows comes
from constructed objects (see R2b), not literals.

Isolated A/B on release (dedicated `LAMBDA_JS_SHARED_LITERAL_SHAPE` gate, ctor
sharing on in both arms, 3-run medians): havlak2 **−4%**, jetstream/hashmap
**−7%**, splay +6%, navier +1%, richards/deltablue/crypto_sha1 flat. Against a
gate of "havlak ≥2x". Reverted; a comment at the emission site records the
measurement so it is not retried blindly.

Two artefacts were kept: `test/js/object_literal_instance_isolation.js`, a
12-case instance-isolation net pinning what any future sharing scheme must
preserve; and a correctness fix in `js_set_shaped_slot` for null writes losing
their type tag on shared shapes.

**Do not re-attempt before R2b.** If constructed-object shapes are ever shared,
literal sharing becomes worth re-measuring — but on current evidence it is a
regression.

### R4 — Reduce dynamic-call overhead

Treat these as independently measurable changes:

1. Allow native-specialized functions whose parameters are numeric but whose
   return is boxed or void; keep boxing at the wrapper boundary.
2. Add a verified fast lane for plain user functions that are not
   generator/async/bound/proxy/constructor-special.
3. Replace save/restore of global `this`/`new.target` state with explicit ABI
   arguments where the call shape permits it.
4. Emit the smallest hot helpers as MIR functions only when measurements show
   that native-import call overhead is material.

Each slice must retain the generic `js_call_function` path for unsupported or
dynamic cases.

### R5 — Cheapen emitted exception polling

`jm_emit_pending_exception_check` still emits a call to
`js_check_exception` followed by a branch. Remaining stages:

1. Inline the pending-flag load and branch.
2. Use the existing normalized import/call effects to omit polls only after
   helpers proven unable to throw.
3. Consider wider poll coalescing only if the first two stages leave a
   measured cost; exception ordering and cleanup edges remain hard barriers.

### R6 — Close Lambda MIR-Direct specialization and call-path gaps

The frozen C2MIR path still has two specializations absent from MIR-Direct:

- typed unboxed system-function variants such as `fn_pow_u` and `fn_abs_i`;
- ~~direct native-math lowering for calls such as `math.sin` and `math.sqrt`~~
  — **DONE as Tune6 L3 (2026-07-26)**, and its gate was mis-targeted. `math.*`
  transcendentals with statically-numeric scalar args now call libm through a
  `d→d` prototype. Correctness is exact (the boxed scalar branch is literally
  `push_d(fn(item_to_double(v)))`) and vectors stay element-wise. But the win is
  **1.017x on nbody2 and flat everywhere else**, not the 1.5x expected:
  spectralnorm holds exactly one `math.sqrt`, outside its hot loop, and removing
  ~10ns of boxing from a ~600ns nbody iteration is ~1.5%. **The boxing was not
  the cost — do not expect further float-benchmark movement here.**
  Trap for the remaining unboxed-variant work: `abs`/`min`/`max`/`round`/
  `floor`/`ceil`/`trunc` are **type preserving** (`abs(-5)` is int `5`), so
  lowering them to a double-returning native call silently changes the result
  type. L3 is gated by an explicit allow-list of the 23 always-`push_d`
  transcendentals — allow-list, not deny-list, because a missing entry only
  costs an optimisation while a wrong entry is a semantic bug.

Also landed under Tune6 against this item: **L1** (hoist `strlen` out of the
map-field loop, compare via `typemap_shape_name_equals_id`) worth 1.15–1.54x,
and **L2** (`fn_member_ic` per-call-site member IC) worth 1.12–1.19x. Together
richards.ls went 372.3 → 212.7 ms (**1.75x**). L2's stage-2 inline emission was
deliberately not enabled — see R2 for why inlining an IC guard is not the
remaining factor.

Port any further shapes to `transpile-mir.cpp` and update
`test/mir/lambda/sys_func_specialization.mir-check` deliberately when the
current boxed-emission expectation changes.

The second open Lambda/MIR issue is boxed element loads feeding `any`
arithmetic. Evaluate an inline type-test plus native fast path against
typed/unboxed element access; do not duplicate the ArrayNum or COW Stage 2
design.

Three smaller Lambda-specific follow-ups remain:

1. **Measure `Element` literal construction before extending const-pool
   materialization.** Static generic arrays and maps are already materialized
   once per script. Audit element name/attribute/content construction and
   mutation semantics, then add an `Element` path only if package-heavy
   profiles show material construction or MIR-volume savings.
2. **Pin static-container mutation safety.** Add a focused regression proving
   that mutation through the array and map/object/element helper families
   rejects const-pooled containers and cannot alter values shared by later
   evaluations. This is a correctness gate for any further static
   materialization.
3. **Re-profile scalar recursion on the current side-stack ABI.** Use `fib`,
   `ack`, and a non-recursive scalar call control to separate checked-entry
   stack-bound checks, call ABI boxing, and general MIR call overhead. Do not
   revive the removed heap `JitGcRootFrame` mechanism; zero-root-slot frame
   cleanup is already handled by the current frame finalization pass.

### R7 — Reduce object-churn GC cost

Evidence-gated candidates for allocation-heavy object workloads:

- non-moving nursery/tenured collection with sticky mark state;
- an old-to-young write barrier at centralized property/array/environment
  stores;
- lazy sweeping;
- mark-time slot tracing through `slot_entries[]` rather than linked
  `ShapeEntry` walks.

Start only after Result11 and R3 quantify the object churn that remains. This
work must not restore conservative native-stack scanning or introduce moving
GC assumptions.

### R8 — Define Lambda/JS error conversion at the interop boundary

Keep the languages' propagation mechanisms separate, but define one
lossless boundary protocol:

- JS throws become Lambda error values without losing the original payload;
- Lambda errors become JS `Error`-like objects without losing identity;
- exactly one conversion choke point exists in each direction;
- a pending JS exception never leaks into Lambda execution, and an
  `ItemError` never enters ordinary JS expression evaluation.

This is correctness/API work with possible call-boundary cost benefits. It
should start when Stage 2 interop work or concrete embedding demand requires
it.

---

## 3. Parked work

### R9 — Packed numeric storage for JS arrays

Do not introduce a separate JS INT value type. A future dense numeric array
representation must preserve JS Number semantics and expose binary64 values at
observable boundaries. Lambda ArrayNum COW/views and JS↔Lambda buffer
ownership remain owned by COW Stage 2 and OI-9.

Revisit only after shaped-field residency and boxed-element measurements show
the remaining value.

### R10 — Destination-passing lowering

This remains the largest code-generation project. Keep it parked until the
smaller call, IC, residency, and element-access changes land and a fresh MIR
volume profile shows that redundant moves are still a leading cost.

---

## 4. Sequencing

Recommended order:

1. COW Stage 1 and Result11.
2. R0 LambdaJS regression diagnosis and queue re-ranking.
3. R1 Tune-6 in its own order: C3, C1, B, profiled C2, A, C4.
4. ~~R3, then R2~~ — **superseded 2026-07-26.** R3 was implemented and reverted
   (net negative); R2 is KIV. **R2b replaces both** as the LJS shape item.
5. **R2b (shared root shape for plain objects)** — the highest-value LJS item on
   current evidence, and a prerequisite for ever re-measuring R3.
6. R4 and R5 as separately measured call-path slices. **Now the leading
   candidate for the next substantial factor**: Tune6 exhausted the
   property-lookup tail (R6's L1+L2+L3 all landed; the largest was 1.54x and the
   combined richards figure 1.75x), so what remains on those rows is call
   overhead and boxing rather than lookup. `Lambda_Impl_Tune7.md` owns the
   dynamic-call flow.
7. ~~R6 in parallel with JS-only work~~ — **largely done** (L1, L2, L3 landed
   under Tune6). Residue: typed unboxed sys-func variants, and boxed element
   loads feeding `any` arithmetic.
8. R7 only on post-Result11 evidence.
9. R8 when interop work requires the boundary contract.

R9 and R10 remain parked. R2 is KIV.

**Standing lesson from Tune6, worth applying to R4/R5.** Three of its five
phases missed their gates, and in every case the gate was set from an assumed
bottleneck rather than a measured one — the census killed J1 outright, J2 went
net-negative, and L3's target benchmark contained one call to the function being
optimised, outside its hot loop. Measure the specific mechanism on the specific
row *before* committing a phase, and build both binaries first then interleave
runs: sequential build-A/measure-A/build-B/measure-B drifted ~10% on the Tune6
host and manufactured a false 1.05x. Guard rows moving is the tell.

Acceptance gates throughout:

- focused fixtures for every changed semantic boundary;
- LambdaJS GTests and `make test262-baseline` with zero failures and zero
  retry-only results for JS changes;
- `make node-baseline` on final JS track candidates;
- `make test-lambda-baseline` at 100% for shared-runtime or MIR changes;
- Radiant/editor gates for DOM, container, object-model, or GC changes;
- forced-GC coverage for lifetime-sensitive changes;
- release-only performance measurements with before/after medians.
