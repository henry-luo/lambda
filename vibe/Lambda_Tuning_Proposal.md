# Lambda / LambdaJS Performance Tuning Proposal

- **Status:** remaining-work proposal, rev 5
- **Revision date:** 2026-07-28
- **Scope:** LambdaJS (`lambda/js/`), the shared runtime, and Lambda
  MIR-Direct (`lambda/transpile-mir.cpp`)
- **Latest full benchmark:** `test/benchmark/Overall_Result15.md`
- **Related plans:** `impl/Lambda_Impl_Tune_COW (done).md`,
  `Lambda_Impl_JS_Tune (done).md`, `impl/Lambda_Impl_Tune6 (done).md`,
  `impl/Lambda_Impl_Tune7_JS_Plain_Call (done).md`,
  `Lambda_Impl_Tune_JS_Dynamic_Call.md`,
  `Lambda_Impl_Tune8_Result15_Bottlenecks.md`, and
  `impl/Lambda_Impl_Tune9_GC (done).md`, `Lambda_Impl_Tune10.md`, and
  `Lambda_Issues_Outstanding.md`
- **Completed Lambda tuning records:** `impl/Lambda_Impl_Tune1 (done).md`,
  `impl/Lambda_Impl_Tune2 (done).md`, `Lambda_Impl_Tune3.md`

This revision records both the remaining work and the terminal disposition of
the old R-items, so a completed, rejected, or intentionally parked track is
not mistaken for unfinished implementation. Owning implementation records
retain the detailed measurements and fixtures.

Result15 is the current complete cross-engine floor. Result11 completed COW
Stage 1, Result13 closed Tune6, and Result14/15 recorded the later dynamic-call
work and its inline-bitcast follow-up.

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

**Current floor: Result15** (`test/benchmark/benchmark_results_v15.json`,
2026-07-27, commit `770eb273a`) — MIR/Node dedup geo **2.92x**,
LambdaJS/Node **13.8x**, QuickJS/Node **7.39x**. It is a 62-row clean-release
re-run with the same Result14 engine stack and protocol. The LambdaJS movement
against Result14 is real (4.3% while the QuickJS control moved 0.4%), but a
matrix remains a floor, not phase attribution.

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
   snapshots. Result13's MIR/Node −2% sat inside a control that moved +6% the
   other way, even though per-row Lambda wins of 1.3–2.5x were real and
   independently confirmed. Result15's same-machine Result14 comparison is
   stronger, but does not remove the need for per-phase interleaved A/B.

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

### R0 — Result11 and the previous LambdaJS regression — **CLOSED**

Lambda COW Stage 1, the output-correct Result11 run, and its provenance were
completed under `impl/Lambda_Impl_Tune_COW (done).md`. Later Result13–15 records
supersede Result10/11 as tuning floors. New LambdaJS regressions must follow
the Result15 protocol: build both release binaries first, interleave each row,
preserve timestamped raw data under `temp/`, and validate output before
claiming a movement.

### R1 — LambdaJS Tune6 — **CLOSED**

Tune6 is fully implemented with terminal decisions for every track. Track B,
listener/handler indices, exact transpiler storage, and production gating
landed. Shaped-float residency stopped after its census found no viable hot
stream; DOM-name dispatch stopped after profiling put it below 0.3% of editor
wall time. Neither is pending implementation without new evidence.

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

### R2b — Realm-owned shared root shape for plain objects — **OPEN**

Extracted from the Tune6 census, this remains the LJS shape-identity item. The
Result15 ownership-bitmap landing removed hashmap's pathological mark cost, but
it did not make constructed-object TypeMaps converge. Re-profile its direct
ceiling after R2c numeric-key and array-store changes; it remains especially
relevant to the shape-heavy cd/havlak class of rows.

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

### R2c — Result15 LambdaJS slow-row bottlenecks — **OPEN**

Result15 profiling replaces generic “object churn” attribution with four
independently measurable LJS mechanisms. They are complementary to R2b: solve
each only with a release-profiled reproduction and retain the generic semantic
path for unsupported objects.

1. **Numeric-key stringification.** Computed compound assignments and updates
   eagerly run numeric keys through `js_to_property_key`, then hot typed-array
   paths parse and canonicalize the resulting strings again. Preserve proven
   numeric keys as Items through the reference get/put path; separately
   evaluate an integral conversion fast path, a real shortest-double converter,
   and arithmetic typed-array canonical-key validation. This is the leading
   Result15 ceiling on spectralnorm and material on navier, hashmap, and
   havlak.
2. **Descriptor-heavy array stores.** A numeric store that misses the dense
   gate can allocate a full descriptor Map merely to ask whether an own
   property exists. Add a lightweight own-property probe, then evaluate a
   hole-aware dense first-write path for plain arrays whose canonical
   prototypes are known accessor-clean. This targets navier’s compound-store
   path and removes store-generated garbage in hashmap.
3. **Expensive prototype hops.** `js_get_prototype` currently performs several
   shape/map searches for every class-instance method lookup. Evaluate a
   dedicated prototype field or TypeMap-cached prototype pointer with correct
   mutation/invalidation behavior. It compounds with R2b on cd and havlak; do
   not use it as a substitute for shared plain-object roots.
4. **Function-object births.** Re-evaluated arrows and function expressions
   allocate and initialize a full `JsFunction` on each birth. Profile and
   compare an arrow-sized/deferred-field representation, precomputed call-lane
   state, or a separately justified allocation policy; identity semantics rule
   out unproven per-site reuse. This is a major cost in havlak and cd.

The Result15 ownership-bitmap fix is already landed, so it is not a fifth
candidate here: after that fix, hashmap marking is no longer a meaningful GC
ceiling. The detailed attribution and acceptance ceilings live in
`Lambda_Impl_Tune8_Result15_Bottlenecks.md`.

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

### R4 — Reduce dynamic-call overhead — **PARTIALLY IMPLEMENTED**

Tune7 reduced the existing dispatcher cost but did not remove it for the broad
plain-call population. Its successor, `Lambda_Impl_Tune_JS_Dynamic_Call.md`,
landed P0–P2: entry selection at function finalization and specialized
ordinary/home-class entries for arities 0–4. The plain 0/2-argument call-cost
probes improved about 2.07–2.09x; Result15 preserved the expected wins on
call-dominated rows.

Remaining slices are deliberately independent:

1. **P3 / DC5 stage 2:** remove the two-slot callee/receiver RootFrame only
   where safepoint publication proves it safe, and add entry-kind hit counters
   plus benchmark/Test262/Node coverage tables.
2. **P4 / DC6:** test epoch-disciplined monomorphic call-target cells at
   richards/deltablue sites after measuring the thin-entry residual.
3. **P5 / C4.1:** allow native-specialized numeric-parameter functions with a
   boxed or void result, keeping boxing at the wrapper boundary.
4. Re-profile exact-arity adaptation and hidden receiver ABI work before taking
   them; neither is justified merely by classifier eligibility.

Every slice keeps the generic `js_call_function` path as semantic authority
and uses the forced-generic differential mode as its oracle.

### R5 — Cheapen emitted exception polling — **CLOSED**

The online-exception tracker landed the inlined/elided per-call poll work. Do
not reopen poll coalescing unless a fresh profile identifies remaining polling
as a material cost; exception ordering and cleanup edges remain hard barriers.

### R6 — Close Lambda MIR-Direct specialization and call-path gaps — **PARTIALLY IMPLEMENTED**

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

Tune10 subsequently retained compact loop lanes, locally proven boolean-array
narrowing, owned String buffers, static member-store key lowering, packed
`u32` arithmetic, and a narrow fresh-map lifetime region. Those are separate
MIR-tail wins; they do not close the R6 semantic-specialization or boxed-element
residue below.

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

### R7 — Reduce object-churn GC cost — **RE-SCOPED / DEFERRED**

The Result15 investigation found and landed an O(1) bump-block ownership
bitmap: hashmap mark time fell from seconds per collection to milliseconds.
That root-cause fix supersedes the old assumption that generational collection
was the immediate hashmap lever.

Tune9 then tested its two measured allocation candidates and retained neither:
the large-string no-zero path regressed `json_gen`, and direct typed-map
construction improved `gcbench` only 4.7%, below its 10% gate. The remaining
ideas—non-moving nursery/tenured collection with a centralized old-to-young
barrier, sticky marking, lazy sweeping, and slot-array shape tracing—are
deferred. Reopen them only when a completing workload shows at least 20%
repeatable full-collection cost across three or more cycles and a prototype
shows at least 10% end-to-end improvement. This work must not restore
conservative native-stack scanning or introduce moving-GC assumptions.

### R8 — Define Lambda/JS error conversion at the interop boundary — **OPEN**

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

### R9 — Packed numeric storage for JS arrays — **PARKED**

Do not introduce a separate JS INT value type. A future dense numeric array
representation must preserve JS Number semantics and expose binary64 values at
observable boundaries. Lambda ArrayNum COW/views and JS↔Lambda buffer
ownership remain owned by COW Stage 2 and OI-9.

Revisit only after shaped-field residency and boxed-element measurements show
the remaining value.

### R10 — Destination-passing lowering — **PARKED**

This remains the largest code-generation project. Keep it parked until the
smaller call, IC, residency, and element-access changes land and a fresh MIR
volume profile shows that redundant moves are still a leading cost.

---

## 4. Sequencing

Recommended order:

1. Establish an interleaved Result15-era release baseline for the exact target
   row before accepting any phase; do not infer a bottleneck from a geo mean.
2. R2c numeric-key preservation and the descriptor-free/hole-aware array-store
   path. These are the largest currently measured LJS ceilings.
3. R2b shared plain-object roots, then R2c prototype-hop reduction, with fresh
   shape and invalidation census after each change. R3 remains a re-measure only
   after R2b; R2 remains KIV.
4. R2c function-birth work only after a focused birth/allocation profile picks
   a semantics-preserving representation or allocation strategy.
5. R4 P3–P5, independently measured against the thin-entry baseline. R5 is
   complete and is not a prerequisite.
6. R6 residue: typed unboxed system functions and boxed-element arithmetic
   first; static materialization and scalar-recursion audit only on evidence.
7. R7 only when its explicit collection-cost trigger is met.
8. R8 when interop work requires the boundary contract.

R9 and R10 remain parked. R2 is KIV; R3 is closed/reverted.

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
