# JS tuning follow-up: MVP and untyped Lambda

**Date:** 2026-09-29. **Status:** implementation and measured acceptance complete.
**Source audit:** `9059289b54dc538a4a7a83482741411810fe83a1`.
**Measured host:** the exact tuned O3/LTO/NDEBUG release-profile archive,
SHA-256 `e3b6effce178cf2568bdf7b4fff7e036763f487f06e56c8048bd33bfbc597545`.
At proposal-writing time, the JS sources were unchanged since tuning commit
`06517a2c1`; the reference measurements belong to that archive. The later
implementation is measured separately in the closeout below.

This follows the [completed round](JS_MVP_Release_Profile_Comparison_20260928.md).
It proposes implementation under existing rulings: **S1.11** preserves hosted
ECMAScript semantics; **D8.2.3** requires extraction after two working clients;
**D8.2.4v2–D8.2.6** govern shared indexing, inference, and demand-driven MIR;
**D8.3.1v2**, **D8.4.1v2–D8.4.3v2**, and **D5.3** govern guarded variants,
immutable code, direct calls, explicit completions, and precise roots.
No normative ruling changes are proposed.

Sections 1–5 preserve the original analysis, measurements, and proposed
sequence. The implementation closeout below records which candidates were
retained after the required semantic and release-timing gates.

## Implementation closeout (2026-09-29)

The [implementation and exact-archive evidence](../../test/benchmark/js_mvp/untyped_tuning_20260929/README.md) records retained code, rejected screens, MIR examples, provenance, and validation. The final O3/LTO/NDEBUG release-profile archive is `temp/js-untyped-tune-20260929/final-guarded-profile.exe`, SHA-256 `b1f79a45f35d28309d320f1d36d827446b4e5bf63dbfba5ef3b9708f05cfcdc9`. Every attributed full-JS pair pins `JS_EXECUTION_BACKEND=mir`; early AUTO screens are retained only as diagnostics. **S1.11**, **D5.3**, **D8.2.3**, **D8.2.6**, **D8.3.1v2**, and **D8.4.3v2** still govern the retained admissions and generic continuations.

* **Priority 1:** the tagged-array fast reader now admits rooted Float-home computed keys, while length expansion invalidates packed evidence and holes still perform inherited Get. The common Float Item decoder was extracted into `mir_emitter_shared.hpp` and called by both Lambda and JS. Text search's own-element helper count dropped from 195.25M to 36,864 in the focused profile; direct old/final MIR timing is 35,398 → 28,822 ms (0.8142, 5/5 faster, identical output). A companion direct reader failed the paired gain gate. Ordinary JS Arrays have no exact ArrayNum physical proof merely from homogeneous examples, so cross-function promotion to Lambda's ArrayNum witness was not admitted; the guarded tagged representation supplies the useful hit without importing Lambda's COW/null rules (**S1.11**, **D8.2.4v2–D8.2.6**).
* **Priority 2:** passing additional JS scalar lists to the shared hoister found no hot hoists and was not retained. A bounded, uncaptured lexical Number `while` copy with a runtime entry guard and complete generic sibling is retained. Triangl's copy adds native `dadd` sites while preserving the boxed `js_increment` fallback. Its isolated completion-only/final archive replay is 0.8982 with 5/5 faster pairs and identical output; the direct original/final median is 0.9223. Lambda's int53/null accumulator proof is not JS semantics, so only the existing shared physical emitters and hoister are reused; the language-owned proof scope stays separate (**S1.11**, **D8.2.3**, **D8.3.1v2**).
* **Priority 3:** guarded numeric-field reads and the individual-operand exact method ABI failed the NBody/Bounce and broader AWFY gates and were removed. A narrower exact `Math.max` method edge was retained: after original Get and both argument evaluations, it guards the retrieved callable and Number arguments, calls the shared scalar max, and falls back to the same retrieved callable and rooted arguments. Its text-search screen was 0.9190, 5/5 faster against the preceding shared-decoder archive. Coercing binary and branch comparisons now carry Error Items through catch routing instead of swallowing throwing ToPrimitive completions (**S1.11**, **D5.3**, **D8.4.2v2–D8.4.3v2**).
* **Priority 4:** log-pipeline profiling recorded 2.16M ASCII splits, 28.8M ASCII slices, 24.66M substring-cache hits, 4.12M misses, and 42.09M named probes. Extra cache and split-append experiments did not pass exact paired timing and were removed. Triangl's whole-module MIR grows from 5,444 to 7,926 lines from the bounded loop copy, while direct old/final wall medians improve 6,800 → 6,271 ms. The 62-row full/MVP wall geomean is 1.908× versus the earlier snapshot's 1.827×; those one-pair separate-session snapshots are descriptive, not causal compiler-cost evidence (**D8.2.6**, **D8.3.1v2**).

Acceptance on the final source: 91/91 JS optimizer tests, six changed-path tests under forced/poisoned GC, 20/20 MIR ratchet probes, 6,011/6,011 Lambda baseline, and 40,261/40,261 fully passing Test262 with zero regressions. The canonical final screen attempted 63 rows and validated 62 against Node 22.13.0; Navier–Stokes remains the explicit MVP post-timing preflight failure. The fixed 62-row full/MVP execution geomean is 1.550×, versus 1.544× in the earlier separate-session snapshot, with 17 full-JS wins in each. Exact interleaved old/final replay, not the snapshot difference, supports retained tuning attribution.

## 1. What this round achieved

The [five-pair exact-archive old/new replay](../../test/benchmark/js_mvp/tune_more_20260929/overall-before-after-summary.json)
is the tuning attribution evidence. Across the 62 common validated workloads:

| Measure | Result |
|---|---:|
| Tuned / original execution-time geometric mean | 0.9239 |
| Geometric-mean speedup | 8.2% |
| Reduction in summed execution medians | 9.2% |
| Rows faster / slower at their medians | 38 / 24 |
| Full JS / MVP geometric mean, before → after | 1.692 → 1.544 |
| Full JS wins against MVP, before → after | 14 → 17 |

The largest relative improvement was Mandelbrot, 552.6 → 47.9 ms. The largest
absolute improvement was text search, 43,024.9 → 34,339.1 ms: **88.36% of the
net time saved**. Excluding text search, the ratio of summed medians is
**0.9821**, a **1.79%** reduction. The five largest remaining workloads—text
search, log pipeline, Havlak, three-way merge, and Triangl—consume **79.02%**
of the tuned 62-row median sum. These sums describe this benchmark population,
not a production workload distribution.

The round therefore established useful wins without broadly closing the
dynamic-language cost gap. The [retained/rejected experiment record](../../test/benchmark/js_mvp/tune_more_20260929/README.md)
also constrains the next proposal. Inline boxed updates reduced Triangl's
ToNumeric/increment counts dramatically but lost all five timing pairs.
Boxed comparisons, mixed-array arithmetic, additional string caches,
no-home/capability call shortcuts, and named-length/proof shortcuts did not
establish benefits. Repeating these local screens is not a new mechanism.

MVP is a same-JS-source reference for dispatch and representation costs. It is
not the semantic implementation to transplant: its Navier–Stokes result still
fails the post-timing oracle and is excluded from the 62-row comparison.

## 2. Fresh reference survey and comparability corrections

The [reference artifacts](../../test/benchmark/js_mvp/untyped_reference_20260929/README.md)
contain 15 workloads, three rotating runs each of full JS, MVP, and the
registry's untyped Lambda source: **135/135 timing runs passed output checks**.
All three engines use the same archived release-profile executable. JS/MVP
use the canonical JS manifest and Node 22.13.0 output; Lambda uses the paired
runner's untyped-file selector and checked-in golden output. Timing excludes
separate MIR/trace runs. All source and executable hashes remained unchanged.

Selected execution medians, in milliseconds:

| Workload | Full JS | MVP | Lambda reference | JS / Lambda |
|---|---:|---:|---:|---:|
| text_search, corrected table construction | 34,331.9 | 19,302.4 | 1,965.8 | 17.46 |
| triangl | 6,253.8 | 2,952.9 | 235.1 | 26.60 |
| havlak, corrected repetition count | 13,696.8 | 5,953.3 | 1,922.3 | 7.13 |
| richards | 940.5 | 566.5 | 377.7 | 2.49 |
| three_way_merge | 9,849.2 | 5,614.4 | 2,950.0 | 3.34 |
| log_pipeline | 13,315.4 | 11,923.6 | 4,664.8 | 2.85 |
| mandelbrot | 48.1 | 31.8 | 39.7 | 1.21 |

These are ratios between ports, not predicted gains from compiler unification.
The source audit found material differences:

* **Havlak:** `havlak.ls` calls `lta_main(1, 1, 10, 10, 5)`; bundled JS passes
  50 for the second argument, `findLoopIterations`. The original Lambda
  median was 72.429 ms, producing an apparent **189.11×** gap. A diagnostic
  copy changing only that argument to 50 measured 1,922.31 ms and retained
  the exact result. The corrected gap is **7.13×**. Allocation/data-structure
  differences still exist; multiplying the original time by 50 would also
  be misleading because graph construction is fixed work.
* **Text search:** Lambda fills the Boyer–Moore last-occurrence table through
  `len(pattern) - 1`, while JS includes the final character. Including it in
  a diagnostic Lambda copy changes 1,500.48 → 1,965.77 ms, with the same
  checksum. The corrected ratio is **17.46×**, versus 22.88× originally.
  Prefix-table allocation still differs (`fill` versus repeated `push`).
  The registry's untyped source also declares three outer locals as `int`;
  the hot search functions are unannotated.
* **NBody and Bounce:** Lambda uses parallel numeric arrays; JS uses objects,
  methods, and, for NBody, a per-step capturing callback. NBody has matching
  36,000 advance steps, but its 669.0 versus 6.958 ms is not an isolated
  compiler comparison. Bounce has the same 100 balls and 50 frames but the
  same layout confound. **CD** also uses different node/tree representations.
* **Triangl:** the search and 29,760-solution oracle align, but Lambda uses
  Boolean/numeric arrays while JS uses Uint8Array/Int32Array and Number move
  tables. This is useful evidence for indexing and loop lowering, with a
  representation qualification.

The six corrected-work timings and their exact edits are retained in
[diagnostics.json](../../test/benchmark/js_mvp/untyped_reference_20260929/diagnostics.json).
Canonical benchmark sources were not edited. Before a future complete
cross-language report, audit repetitions, input sizes, stopping conditions,
and timed setup for its full population; equal output alone does not prove
equal work. This bounded survey does not claim that audit for all 63 ports.

## 3. What the current code and emitted MIR show

Eight additional instrumented runs passed their output checks. Counters below
are events, not CPU percentages; `mir_*_admitted` counters count compiled sites
and must not be treated as dynamic hit counts.

| Workload | Current dynamic evidence | Implication to investigate |
|---|---|---|
| text_search | 633.23M Number-comparison head hits; 191.11M Number-arithmetic head hits; 195.25M own-element helper hits | Numeric arrays/loop values repeatedly cross generic operations despite overwhelmingly primitive data. |
| triangl | 44.19M ToNumeric calls and 44.19M increment calls; 46.75M Number comparisons | Array-derived locals prevent a fully native loop; another per-update Number guard already failed its timing gate. |
| nbody | 12.35M named probes, almost all hits; 11.70M Number-arithmetic hits; 2.70M dynamic stores | Successful lookup and boxed arithmetic remain expensive candidates; improving misses will barely reach this workload. |
| bounce | 54,686 named probes; 5,401 `mir_this` calls | A small object/method control, useful for semantics and startup/code-volume checks. |

The untyped Lambda MIR gives a concrete reuse model. Text search's
`naive_search`, `prefix_table`, `kmp_search`, and `boyer_moore_search` receive
an `_array_witness` argument. `naive_search` loads payload and length at
entry, uses direct indexed loads, and retains native loop variables. The
source path that admits this is the closed homogeneous-array call-site join
in `transpile-mir.cpp` around `36566–36664`, together with
`mir_typed_array_witness_mask`, `mir_guarded_array_num_witness`, and the
native call/wrapper code. This is evidence from the actual untyped program,
not an assumption that explicit typed-Lambda optimizations apply to it.

Full JS's text-search dump has 15,665 lines and 674 static call instructions;
Lambda's has 3,895 and 242. Triangl has 5,444/251 versus 2,126/186. These
counts include wrappers and cold paths and do not predict execution time.
The NBody `mir_deferred_function_finalize` count of 36,001 describes pending
function/closure creation in `js_runtime_function.cpp`; it does **not** mean
36,001 MIR/JIT compilations.

### Already shared; extend rather than re-create

* `mir_emitter_shared.hpp`: `MirValue`, representation conversion, numeric
  opcode plans, address/load/store helpers, rooting, function plans and direct
  call emission. JS already has native and boxed direct identifier calls.
* `mir_shape_candidates.hpp`: construction candidates and field-access plans.
* `mir_loop_invariants.hpp`: the same pure/loop-stable scalar-call hoister is
  used by both languages. It intentionally excludes mutable memory loads.
* The literal string-split scanner already serves both runtimes; the accepted
  JS change chooses tagged result storage without duplicating the scan.

### Missing connections

* JS `jm_transpile_member_as_number` has native computed-index paths, but a
  named field goes through `jm_emit_member_value` and then representation
  conversion. `jm_emit_predicted_literal_field_read` loads a native field
  and immediately boxes it. Native producers therefore do not always reach
  native consumers.
* `jm_emit_fixed_typed_array_data_if_kind` checks lower/upper index limits,
  converts double → integer → double, checks integrality, then calls the
  backing-store guard per access. The packed-array reader has related
  receiver, key, storage, and bounds screens. Existing proof must reach these
  emitters before checks can be removed.
* Lambda supplies `mir_loop_entry_scalars` to the shared scalar hoister;
  JS's three call sites currently omit its optional initialized-scalar list.
  This is a small, concrete reuse experiment, distinct from hoisting mutable
  array lengths or backing pointers.
* General JS member calls still construct an argument vector and use the
  dynamic call boundary after Get, except narrow builtin leaves. Direct
  identifier calls already reuse `em_call_direct`; extending eligible method
  edges would remove a boundary rather than add another runtime test inside it.

## 4. Proposed implementation sequence

### Priority 1: carry array proofs across function and expression boundaries

Start with text search and Triangl. Reuse the untyped Lambda pattern of
call-site/return evidence feeding an immutable native entry and indexed
consumer. Admit only a complete proven or guarded signature, retaining the
boxed fallback (**D8.3.1v2**, **D8.4.1v2**). A JS ordinary Array is not a
Lambda ArrayNum merely because its current examples contain numbers.

1. Trace exactly where numeric-element, index-range, and representation facts
   disappear through array construction, returns, parameters, and local
   initializers. Publish inferred runtime types through the existing AST/type
   authority, using the shared compilation indexes (**D8.2.4v2–D8.2.5v3**).
2. Admit native array storage where an exact physical representation proves
   it, or where nonescaping construction and every possible write prove it.
   Tagged arrays with unknown elements keep element guards. A builtin name
   such as `charCodeAt` does not prove a numeric result if the method was
   replaced; capability admission and its generic continuation must remain.
3. Let index loads return `MirValue` for F64, a proven integral address, or
   branch demand without mandatory Item materialization. Share the physical
   portion of Lambda's `MirIndexLoadPolicy`, `MirIndexProof`, and
   `emit_checked_index_load` after the JS client works. Keep JS key conversion,
   holes/prototype/accessor behavior, and out-of-bounds results profile-owned.
4. Carry witnesses through exact direct calls; reload raw storage after
   allocation and invalidate mutation-sensitive witnesses on writes, escape,
   detachment/resizing, or reentry. Unknown effects terminate the proof.

**Success evidence:** fewer dynamic indexed-helper calls and conversions in
the hot loops, with a release speedup on both a target and controls. A shared
wrapper that still performs every original screen is not completion.

### Priority 2: retain native loop state and share bounded proof scope

Extend initialized loop-state inference to the array-derived producers exposed
by Priority 1. Triangl's `mi`, `lastM`, and comparisons are a stronger target
than re-adding the rejected boxed-increment fast path. Proven JS Numbers stay
F64; integer address lanes require finite/integral/range proof and preserve
Number overflow, NaN, and signed-zero behavior (**S1.11**, **D8.2.6**).

First test passing JS's proven initialized, uncaptured lexical scalar
registers to the existing `em_hoist_loop_scalar_calls`. Exclude TDZ paths,
conditional initialization, captured/dynamic bindings, and suspension.
Measure what actually hoists before retaining this small change.

For a larger gain, use bounded loop-entry admission and a complete generic
continuation, with a size/profitability limit. Lambda's
`mir_loop_accumulator_plan` and scope restoration provide a design to converge
on, but its int53/null rules are not JS semantics. Its existing refusal to
duplicate call-heavy or nested loop bodies is valuable: indiscriminate
versioning previously made Lambda slower too. Extract shared scope/lifetime
and emission mechanics only; retain each language's admissibility proof.

### Priority 3: preserve native field values, then specialize exact method edges

Use NBody/Bounce as controls and Havlak/Richards/CD as broader targets.
Extend existing `MirFieldAccessPlan`/shape-candidate propagation through local
aliases, parameters, and returns. Let an admitted numeric field remain F64
through arithmetic and stores. Share load/store and representation mechanics
with Lambda (**D8.2.6**) while retaining exact shape, slot publication,
descriptor, and value-kind proofs. A lexical `const` binding does not make
its object immutable; Lambda's `mir_flow_stable_source` relies on stronger
pure-function/value semantics and cannot be copied as a JS rule.

Treat direct method calls as a separate measured slice. After the original
Get and argument evaluation, an exact compile-predicted callable may use the
existing individual-operand direct ABI and `MirFunctionPlan`/`FnVariantAnalysis`
(**D8.4.2v2**). Preserve receiver, closure environment, realm, private home,
stack limits, roots, and explicit error completion. Guard the callable actually
retrieved; do not repeat Get or assume method identity from receiver shape.
Ineligible, spread, async/generator, bound/proxy, or mutable cases retain the
existing dynamic boundary. There is no mutable method cache (**D8.4.1v2**).

Do not repeat the failed no-home/capability-check micro-optimizations. The new
mechanism must remove argument-vector construction or a complete dynamic
boundary at an eligible edge. Fresh callback identity and captured state stay
observable; NBody's per-step closure cannot simply be cached.

### Priority 4: profile shared string/allocation work and compiler cost

Log pipeline is already relatively close to MVP (1.12× in this survey), while
both remain slower than the Lambda port. Profile allocation, substring
construction, split result representation, and builtin dispatch separately
before choosing the next change. Reuse the existing scanners and substring
facilities; extra cache lookups and a different split append helper already
lost paired timing tests. Expose a no-GC primitive leaf only when its complete
contract is truthful, not by changing the effect flags on a generic helper.

Also track compile/link time, generated instruction volume and process wall
time. The final full-JS/MVP wall geometric mean is 1.827×, versus 1.544× for
execution. Large guard forests can improve a loop and still worsen startup.
Whole-bundle dump sizes include different source scaffolding, so use exact
same-source candidate/control measurements for a compiler-cost claim.

## 5. Shared boundary and acceptance gates

The intended route is:

```
language proof and semantic admission
    -> immutable access / numeric / function plan
    -> shared physical MIR emitter
    -> native producer-to-consumer path
    -> one existing language continuation on a miss
```

The common code owns addressing, representation transport, roots, and emitted
control/call mechanics. JS owns coercion, reference evaluation order, mutable
identity, prototype/exotic behavior, and exceptions; Lambda owns its value,
nullability, and COW contracts (**S1.11**, **D5.3**, **D8.2.3**, **D8.4.3v2**).
Explicit-contract-only Lambda array guards and scalar-record paths are not
automatically applicable to untyped inputs.

Each slice should have:

1. A before/after emitted-MIR example showing the removed work and a source
   admission/fallback explanation. Keep existing initialized-cycle inference,
   scalar hoisting, native calls, and field plans; do not create parallel ones.
2. Semantic regressions for widening, holes, inherited getters, aliased
   writes, method replacement during argument evaluation, throwing coercion,
   Number edge values, and relevant suspension boundaries. Run changed
   ownership paths with `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`.
3. Exact archived release A/B with output validation, at least five alternating
   pairs for a screen, and more pairs when the gain is near noise. Include
   full JS controls, untyped Lambda controls for shared edits, compilation
   and wall cost. Counters support mechanism; elapsed time decides retention.
4. Extraction after both clients work, with the duplicate physical logic
   removed and an explicit source-diff/LOC accounting (**D8.2.3**).
5. The full JS optimizer suite, Lambda baseline and Test262 baseline, then the
   canonical 63-row output screen and the 62-row validated MVP comparison.
   Preserve failed/unsupported rows and benchmark-work corrections visibly.

The prior implementation finished with 87/87 optimizer tests, 6,006/6,006
Lambda baseline cases, and 40,261/40,261 Test262 cases, as recorded in its
validation artifact. At proposal-writing time, this analysis changed no runtime
code and made no new baseline-pass claim. The implementation closeout above
reports the later source changes and their completed acceptance gates.

**Original recommended first slice:** homogeneous-array proof propagation plus native
indexed/branch consumers on text search and Triangl. It has the strongest
current dynamic evidence, an actual untyped Lambda implementation to reuse,
and a mechanism distinct from the local guards rejected in the completed round.

## Source map

| Area | Live implementation |
|---|---|
| Untyped Lambda array inference, native witnesses, checked loads and loop scopes | [transpile-mir.cpp](../../lambda/runtime/transpile-mir.cpp) |
| Shared value, numeric, address, root and direct-call emission | [mir_emitter_shared.hpp](../../lambda/runtime/mir_emitter_shared.hpp) |
| Shared field/construction plans | [mir_shape_candidates.hpp](../../lambda/runtime/mir_shape_candidates.hpp) |
| Shared scalar hoisting and JS loop callers | [mir_loop_invariants.hpp](../../lambda/runtime/mir_loop_invariants.hpp), [js_mir_statement_lowering.cpp](../../lambda/js/js_mir_statement_lowering.cpp) |
| JS indexed/field demands and member-call boundary | [js_mir_expression_lowering.cpp](../../lambda/js/js_mir_expression_lowering.cpp) |
| Existing JS direct-call variants | [js_mir_calls_boxing_types.cpp](../../lambda/js/js_mir_calls_boxing_types.cpp) |
| Function allocation and capability counters | [js_runtime_function.cpp](../../lambda/js/js_runtime_function.cpp) |
| Helper effects and scalar/no-GC contracts | [sys_func_registry.c](../../lambda/runtime/sys_func_registry.c) |
