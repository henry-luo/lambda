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

### R2 — Inline named-property IC hits, then design the method/prototype PIC

The named load/store IC data structures exist, but each hit still enters
`js_property_access_named_ic` or `js_property_set_named_ic` through a C call.
Emit the monomorphic hit guard and slot access directly in MIR, retaining the
current helper for misses, polymorphic cases that are not yet inlined, and all
semantic fallbacks.

Method/prototype dispatch remains a separate design step. The single-tier JIT
has no code patching or deoptimization, so the PIC must be data-driven and
realm-correct. Settle cache ownership and invalidation granularity in
`Lambda_Issues_Outstanding.md` OI-6 before implementation.

### R3 — Share object-literal shapes per call site

Static data-key object literals are pre-shaped, but each evaluation still
constructs a separate `TypeMap`/`ShapeEntry` graph. Add a per-call-site shape
cache to the literal path, following the existing constructor-shape-cache
lifetime model.

Requirements:

- one stable cache cell per compiled literal site;
- no sharing between realms or incompatible literal layouts;
- no change to computed keys, spread, accessors, or property order;
- make literal receivers monomorphic enough to benefit from R2.

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
- direct native-math lowering for calls such as `math.sin` and `math.sqrt`.

Port the useful shapes to `transpile-mir.cpp` and update
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
4. R3, then R2.
5. R4 and R5 as separately measured call-path slices.
6. R6 in parallel with JS-only work.
7. R7 only on post-Result11 evidence.
8. R8 when interop work requires the boundary contract.

R9 and R10 remain parked.

Acceptance gates throughout:

- focused fixtures for every changed semantic boundary;
- LambdaJS GTests and `make test262-baseline` with zero failures and zero
  retry-only results for JS changes;
- `make node-baseline` on final JS track candidates;
- `make test-lambda-baseline` at 100% for shared-runtime or MIR changes;
- Radiant/editor gates for DOM, container, object-model, or GC changes;
- forced-GC coverage for lifetime-sensitive changes;
- release-only performance measurements with before/after medians.
