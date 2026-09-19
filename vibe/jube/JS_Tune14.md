# JS Tune14 — Shared native regions and cheaper ordinary operations

**Version:** 1.2.0

**Date:** 2026-09-19

**Status:** IN PROGRESS — the first T14-2 numeric slice and a T14-6
cache/prebuild lifetime repair are implemented. The numeric slice has focused
semantic and paired-release evidence. T14-0, T14-1 and the remaining T14-2
through T14-8 outcomes remain open; this is not a performance acceptance record.

**Source audit:** `c5052085ce91026edd94c50cf89c4fd0e539c2ec`.

**Scope:** full LambdaJS MIR Direct compilation, its runtime operations, and
shared compiler/runtime primitives where a measured cause requires changes.
Untyped Lambda is the first reuse target and a required co-consumer of new
common physical machinery; §3.2–3.5 specifies the source audit and migration.
This implementation plan lives at the owner's requested `vibe/jube/` path.
`T14-*` identifiers identify work packages, not new normative rulings.

**Predecessors:** [JS Tune13](JS_Tune13.md),
[Tune13 implementation record](../impl/JS_Tune13_Impl.md),
[Result46](../../test/benchmark/Overall_Result46.md), and
[Result47](../../test/benchmark/Overall_Result47.md).

## 1. Objective and implementation decision

Make common full-JavaScript operations small enough that native execution
consistently beats QuickJS across the canonical benchmark population and
approaches the synchronous MVP's performance, while preserving full JS semantics.

The next unit of optimization is a **complete native region**: entry facts,
local values, indices, loads, arithmetic, comparisons, updates, stores and exits
retain their useful representations together. Merely emitting a native function
variant, adding a fast helper, or removing one runtime query does not meet this
objective when surrounding operations still box, resolve names, publish roots
and call generic helpers on every iteration.

The implementation order is:

1. Establish a fresh fixed control and repair the confirmed Navier regression.
2. Complete Number inference and native local updates.
3. Complete ordinary/typed array regions and reuse proofs within valid effects.
4. Broaden ordinary fields, NameId continuity and cheap property inspection.
5. Finish measured compiler-memory/startup work inherited from Tune13.
6. Address remaining built-in, call and allocation costs when profiles support
   them, then close the full correctness and performance gates.

This plan changes no semantic, execution-policy or ownership ruling. It uses
the existing MIR backend, `Item`, `MirValue`, `FnAnalysis`/`FnVariantAnalysis`,
indexed binding facts, helper catalog, property kernels and precise-root model.

### 1.1 Authority

| Formal authority | Binding constraint |
|---|---|
| **S1.11**, **D1.3v3** | LambdaJS retains ECMAScript coercion, identity, property, evaluation-order and exception behavior. Shared physical machinery does not import Lambda semantics. |
| **D2.4.1–D2.4.3**, **D3.3.2v2** | Native representation requires sound facts or a noncoercing guard. Entry specialization is separate from source and result contracts. |
| **D3.3.3v3** | Binding-local element facts do not become an unproved global promise about an aliased container. |
| **D3.4.4v2**, **D4.6.1v3–D4.6.2v2** | Preserve context-owned NameId identity. Do not use String pointer identity or persist arbitrary dynamic IDs in generated code. |
| **D5.3.1–D5.3.5**, **D5.4.1** | Precise roots, scalar owners, safepoints and activation/argument lifetimes remain authoritative. |
| **D6.2.2v2**, **D8.4.3v2** | Keep the callable-entry contract and explicit completion/error propagation. |
| **D8.1.3v11** | Selected JS MIR executes native code. AST is an explicit backend; no hidden MIR interpretation or selected-MIR replay through AST. |
| **D8.2.3–D8.2.6** | Extend existing indexed analysis and common physical lowering. Keep profile semantics distinct and avoid duplicate fact owners. |
| **D8.4.1v2** | Immutable predicted specialization, inline guards and a shared semantic miss; no mutable property/call-site caches or feedback vectors. |
| **D8.6.1–D8.6.3** | Zero-slack MIR ratchets, finalized artifacts and dynamic forced-GC rooting oracles. |

Sources: [formal design](../../doc/Lambda_Formal_Design.md),
[formal semantics](../../doc/Lambda_Formal_Semantics.md), and
[documentation convention](../../doc/Doc_Convention.md).

## 2. What Results 46/47 establish

All workload figures below are medians in milliseconds. Aggregate execution
ratios use equal logarithmic weight for the same 63 canonical rows.

| Observation | Result |
|---|---:|
| Result46 full JS / QuickJS geometric mean | 1.464372x |
| Result47 / Result46 full-JS geometric mean | 0.786940x |
| Result47 / Result46 Node geometric mean | 0.808804x |
| Full-JS change normalized through Node | 0.972968x |
| Full-JS median sum, Result46 → Result47 | 163,162.383 → 135,314.986 ms |
| Result47 JS / Result46 QuickJS, different sessions | 1.152373x |
| Same comparison normalized through Node, proxy only | 1.424787x |
| Result47 JS / historical MVP acceptance session 2 | 1.683102x |

The 21.3% raw geometric improvement is not an attributed compiler speedup:
Node improved 19.1% in the same comparison. Node normalization is a sensitivity
check, not an exact correction. Result47 did not rerun QuickJS. Its `pidigits`
and `crypto_sha1` Lambda cells were separately refreshed with a repaired binary.
Do not publish a current QuickJS gap by relabeling Result46's cells as Result47.

The historical MVP used a narrower semantic surface. Its frozen manifest now
rejects 57 source hashes. The audited acceptance-commit-to-Result46 changes in
the selected JS sources replace `process.hrtime.bigint()` timing with
`performance.now()`; those diffs do not change workload algorithms. This still
requires an explicit new manifest and fresh comparison, not an overwritten v1
acceptance record. Selected JS sources did not change between Results46 and 47.

### 2.1 Priority workloads

QuickJS and MVP columns in this table are historical comparators, not a
same-session Result47 comparison.

| Workload | JS46 | JS47 | QuickJS46 | MVP session 2 | Why it matters |
|---|---:|---:|---:|---:|---|
| `jetstream/navier_stokes` | 213.933 | 565.384 | 99.084 | 57.513 | Confirmed 2.646x archived-binary regression; numeric/indexed code expansion. |
| `larceny/diviter` | 16,262.958 | 12,688.846 | 33,091.976 | 688.598 | 9.38% of JS47 median sum; incomplete Number inference and updates. |
| `text/text_search` | 57,560.615 | 53,625.015 | 38,949.416 | 22,254.900 | 39.63% of median sum; character-code array loops remain partly generic. |
| `awfy/havlak` | 22,277.617 | 14,336.525 | 4,446.140 | 7,719.698 | 10.59% of median sum; object/name/field coverage. |
| `text/microdiff` | 1,346.326 | 825.140 | 163.411 | 173.476 | Property/enumeration/name work across many small operations. |
| `larceny/quicksort` | 179.679 | 133.237 | 23.017 | 32.227 | Int32Array, comparisons, swaps and native loop updates. |
| `r7rs/fft` | 6.205 | 3.805 | 3.071 | 1.163 | Native function still contains boxed stores and arithmetic. |
| `beng/regexredux` | 10.567 | 8.826 | 6.794 | 0.688 | Built-in/protocol residual; profile after core improvements. |
| `text/hyphen` | 843.169 | 100.593 | 101.624 | 87.595 | Preserve the large win; separately measure compiler and allocation residuals. |

`text/log_pipeline` is another 10.56% of the JS47 sum, and
`text/three_way_merge` is 7.32%. The largest five rows account for 77.49%.
Absolute time and broad geometric improvement therefore require different
views of the same population. A 2x win on one row improves the 63-row geomean
only about 1.1%; a 2x win on 30 rows improves it about 28.1%.

### 2.2 Verified structural gaps

Fresh MIR diagnostics used the archived release binaries, not a debug timing
build. Dump-enabled diagnostic elapsed times are not performance evidence.

| Diagnostic | Observation | Interpretation limit |
|---|---|---|
| `diviterDiv` | Native signature has boxed `x`, F64 `y`; loop calls `js_cmp_raw`/`js_subtract`; native `q++` calls `js_to_numeric`, `js_increment`, `js_to_number`. | Establishes executed-loop helper structure, not a measured speedup from a proposed fix. |
| `diviterMod` | Only a boxed body is emitted. | Specialization/refusal must be explained with binding facts. |
| Search functions | `naiveSearch`/`boyerMooreSearch` remain boxed; `kmpSearch` has a native variant with generic length/index/arithmetic work. | Character-code arrays are the hot input; do not attribute this to Unicode string decoding. |
| FFT `four1_n` | Eight `js_typed_array_set_numeric_key` sites; generic arithmetic remains despite available Number-store machinery. | Static sites include misses; obtain dynamic reachability before attributing time. |
| Navier `lin_solve2_body` | 2,407 → 6,293 finalized instructions; 115 → 189 static call sites. | Expansion and conversions are suspects, not an isolated root cause. |
| Navier `project_body` | 3,077 → 6,884 finalized instructions. | Measure native code size/spills and runtime helper work separately. |

The archived 31-pair Navier replay has a 2.646183x candidate/control median
ratio, 31 valid timing pairs and matching binary/source hashes. Its normalized
stdout is empty: the canonical one-frame wrapper does not reach the source's
frame-15 checksum. This confirms repeatable elapsed-time regression but is weak
semantic-equivalence evidence. T14-0/1 must strengthen the result oracle before
accepting a fix without changing the canonical timed workload.

## 3. Tune13 carryover: retain the unfinished outcomes

**Yes: substantial Tune13 work is worth carrying forward.** Carry the remaining
outcomes and validation debt, not a fresh implementation of mechanisms already
present. Tune13 remains a partial historical implementation record; creating
this plan does not mark it complete.

| Tune13 package | Already present / retain | Outstanding work worth carrying | Tune14 owner and priority |
|---|---|---|---|
| T13-0: controls and census | Historical matrices, opt-event infrastructure, focused profiles. | Actual starting-tree control; fresh full-JS/MVP/QuickJS population; correct source/binary provenance; dynamic refusal/miss and phase/owner census. | T14-0, required first. |
| T13-1: native-only MIR | Main native compilation paths and JS `--mir-interp` rejection. | Full entry/lifetime matrix and actual-backend evidence. Audit the remaining document-module size branch described below. | T14-0 inventory and T14-8 conformance gate. |
| T13-2: compiler scaling | Call-only root liveness storage; compact admitted numeric-array literal construction. | Quantify remaining interference/IR/native-generation peaks; scaling evidence, retained-context lifetimes and measured initializer expansion. | T14-6, required census and disposition; implement diagnosed costs. |
| T13-3: native arrays | Parameter/local dense candidates; typed read snapshot; guarded Number-store leaf; native variants. | End-to-end producer/consumer coverage, native updates, mixed parameter facts, missing typed kinds, real search/FFT hits. | T14-2/3, highest tuning priority. |
| T13-4: reuse metadata | Dense `reduce` read and pre-rooted callback span with revalidation. | General effect-bounded length/data/receiver witnesses; correct alias kills and reuse across several operations. | T14-4, high priority. |
| T13-5: names/descriptors | One-pass classifier, fewer catalog probes, one outer dynamic-key canonicalization, ordinary enumerability inspection, lazy pending-function metadata. | End-to-end NameId continuity, remaining conversion/miss census, enumeration coverage and broad workload evidence. | T14-5, high priority. |
| T13-6: fields/calls | Predicted field infrastructure, light MIR calls, receiver-only bound-argument forwarding. | Constructor/returned-record field coverage and native field consumers. Further call changes need separately attributed cost. | Fields in T14-5 required; further call tuning in T14-7 conditional. |
| T13-7: strings/RegExp | Existing bulk paths and URI-cache admission narrowing. | Reprofile regexredux/revcomp/Hyphen; immutable classifier ownership and UTF-16 indexing only if measured. | T14-7, conditional implementation with required disposition. |
| T13-8: ownership/closeout | Intrusive external-array ownership and a narrow GC-data allocation path. | Allocation/GC attribution, regression resolution, full baselines, paired/full matrix, cold/RSS evidence and cleanup. | T14-6/7/8; final gates mandatory. |

The current `transpile_js_module_to_mir` still computes
`document_ast_too_large` from document context and `mir_large_interp_enabled()`
and joins it with explicit AST selection before invoking the AST module
executor. The source branch is at
`lambda/js/js_mir_module_batch_lowering.cpp:4194` in the audited tree.
Trace effective selection and exercise this path. If it redirects a selected
MIR unit, resolve that implementation conformance gap under **D8.1.3v11**;
do not silently weaken the formal rule or claim T13-1 complete from the normal
CLI path alone. No backend-policy change is proposed here.

### 3.1 Work that should not restart

- The native Map/Set insertion-order second search is already removed.
  `JsCollectionEntry` indexes the stable order node. Keep its semantic/GC tests.
- Numeric `sum`/`fib` specialization and ordinary Number helper heads already
  exist. Preserve these controls while extending coverage elsewhere.
- Dense parameter/local admission already exists despite historical helper
  names containing `array_literal`; inspect behavior rather than naming.
- Do not repeat unpaired sub-percent adapter experiments as accepted gains.
- Do not adopt MVP's coercing numeric admission or restricted typed-array,
  property or RegExp semantics (**S1.11**, **D2.4.3**).
- An ABI/collector replacement, mutable ICs, interpreter-policy changes,
  speculative OSR/deoptimization, vendor patches and benchmark rewrites are
  outside this plan. An observed residual needs a cause before a new mechanism.

### 3.2 Alignment with untyped Lambda: reuse the optimization, retain the semantics

**Further alignment is a required part of Tune14.** Full LJS and untyped Lambda
already share enough infrastructure that a second JS-specific implementation
of each physical optimization is unnecessary. The useful boundary is:

```text
Lambda admission and semantic rules       ECMAScript admission and semantic rules
                 \                         /
          existing indexed facts and function/variant records
                              |
            immutable physical operation plan + MirValue demand
                              |
        common guards, representation, addressing, calls and root ownership
                              |
                 direct operation on the guarded hit
                              |
          profile-owned continuation for a semantic miss
```

An untyped source parameter may acquire a guarded native representation; it
does not need a user annotation. The reuse target is precisely this transition
from dynamic source values to cheap admitted operations, including safe misses.
Untyped Lambda's benchmark ports are useful controls and sources of compiler
techniques, but different ports/semantics do not establish an achievable JS
speedup or permit transplanting their semantic helpers.

**Already shared, verified in current source:** `AstIndex`, `NameEntry`,
`FnAnalysis`/`FnVariantAnalysis` in `ast-core.hpp`, `CompilerPassManager`,
`MirValue`, `em_require_rep`, `em_call_direct`, the emitter's frame/root/scalar
ownership machinery, `MirNumericOpPlan`, shape candidate/access planning, and
scalar loop-invariant hoisting. Extend these owners; do not introduce another
JS optimization framework, parallel fact database or native-value ABI
(**D1.3v3**, **D8.2.3–D8.2.6**, **D5.3.4**).

The outstanding work is incomplete use of these shared mechanisms plus some
physical emission still embedded in profile code. Sharing a header alone is
insufficient: the useful native producer and consumer must both reach it.

| Area and current source evidence | Reuse decision | Tune14 owner |
|---|---|---|
| Untyped parameter inference: core `infer_param_types_batched`, `prepass_collect_call_sites`, `FnParamEvidence`; JS `jm_infer_param_types` and indexed inference. Common function/variant records already exist. | Reuse indexed identities and records immediately. Converge binding dependency/alias work before extracting common traversal/worklist mechanics; keep evidence interpretation and admission profile-owned. Do not copy core `INFER_*` rules or its recursive walkers into JS. | T14-2; compiler cost in T14-6 |
| Numeric operation selection: `em_numeric_op_plan` in `mir_emitter_shared.hpp` is called by both frontends. | Complete native producer/consumer coverage around this existing plan. Do not create a parallel JS opcode table, arithmetic-tree emitter or special per-benchmark path. | T14-1/2/3 |
| Scalar representation: core `emit_box_float` and JS `jm_box_float` each emit in-band bits, signed-zero encoding and a cold `push_d` call. `em_require_rep` delegates to each profile's converter. | Extract the equivalent nonnullable F64-to-Item physical sequence, migrate both callers, and preserve the common call/result-home protocol. Keep Lambda's nullable lane encoding outside the shared leaf. Audit unboxing separately before merging it. | T14-1/2; size/compile evidence in T14-6 |
| Indexed storage: core `MirIndexLoadPolicy` / `emit_checked_index_load`; JS `jm_emit_packed_array_read_impl` and fixed typed-view loads/stores. Both already use common load/address primitives in some paths. | Split physical storage access from semantic access policy. Share admitted base/length/index/width/load/store emission; keep Lambda contract/null/COW and JS property/view/conversion admission outside it. Migrate a live core caller and a JS caller together. | T14-3 |
| Scalar loop invariants: `em_hoist_loop_scalar_calls` in `mir_loop_invariants.hpp` is already used by Lambda and JS loop lowering. | Extend the existing implementation only for proven additional scalar cases. It deliberately excludes mutable memory witnesses; do not use it as proof that array metadata can survive writes/calls. | T14-4 |
| Dense-loop and local arithmetic-tree versioning: core `mir_prepare_dense_for_guard`, `mir_dense_*`, `mir_local_tree_collect` and checked-index paths. | Reuse the strategy and extract physical extent/guard/emission pieces after two valid clients exist. Current core typed-array/nullability proofs are not JS proofs; unannotated Lambda fixtures must also exercise the shared piece. | T14-3/4 |
| Record candidates: both core `mir_expr_candidate_shape` and JS `jm_plan_predicted_literal_field` use `mir_shape_candidate` / `mir_plan_field_access` from `mir_shape_candidates.hpp`. | Extend the current candidate propagation and access plan. Core already follows parameter/return shapes and considers a unique module shape for a field; reuse that mechanism where profitable with JS-specific validity checks, never a second shape planner. | T14-5 |
| Names and field storage: `NameId`, module property-key images, `em_guard_map_shape`, `em_load_at`, and scalar-home/store ownership. | Keep canonical keys end-to-end and share physical loads/stores after profile admission. A matching TypeMap is insufficient proof of ordinary JS property behavior. | T14-5 |
| Calls, roots and compilation: `em_call_direct`, `em_finish_direct_call_normal`, common root liveness/finalization and pass scheduling. | Diagnose and fix the common owner once. Reduce boxing before changing root handling; preserve emitter-owned lifetimes and truthful helper effects. | T14-2/6/7 |
| Strings, allocation and runtime builtins: shared storage/allocator/name primitives coexist with language-specific operators and protocols. | Reuse a concrete leaf only after matching its input, output, effects and lifetime contracts. Do not forward JS arithmetic, truthiness, equality, property access or regex to Lambda's semantic helpers. | T14-7, profile-gated |

Source owners: [core MIR lowering](../../lambda/runtime/transpile-mir.cpp),
[common emitter](../../lambda/runtime/mir_emitter_shared.hpp),
[common function records](../../lambda/runtime/ast-core.hpp),
[shape planning](../../lambda/runtime/mir_shape_candidates.hpp),
[scalar loop invariants](../../lambda/runtime/mir_loop_invariants.hpp),
[JS analysis](../../lambda/js/js_mir_analysis.cpp),
[JS parameter/variant inference](../../lambda/js/js_mir_function_collection_class_inference.cpp),
[JS expressions](../../lambda/js/js_mir_expression_lowering.cpp), and
[JS boxing/calls](../../lambda/js/js_mir_calls_boxing_types.cpp).

### 3.3 Concrete shared implementation slices

These slices belong to the existing T14 packages, not a separate prerequisite
rewrite. Land each at the point its two clients and measured need exist.

**A. Numeric facts and physical representations — T14-2.**

1. Trace an unannotated Lambda parameter-to-alias-to-update chain alongside
   the `diviter` JS chain. Record current candidate discovery, variant admission,
   actual representation and the first consumer that materializes an Item.
2. Use the indexed binding/use/def relationships for the new dependency
   propagation. If both profiles need the same propagation mechanics, extract
   those mechanics and replace the corresponding existing core path in the
   same slice. Do not require wholesale conversion of core inference before
   fixing the JS coverage gap, or add a speculative framework with one client.
3. Keep joins/admission distinct: Lambda's numeric-use-to-integer default,
   optional/null contracts and exact call-edge policy cannot become JS rules.
   JS starts with a Number guard and F64 semantics; native integer counters
   need their own range proof. Capture/eval/with/missing-argument exclusions
   remain explicit. Candidate evidence never becomes an unconditional fact.
4. Retain effective runtime types on their established AST owners and
   variant-specific facts on `FnVariantAnalysis`; do not publish speculation
   into a generic body or a shared `TYPE_ANY` singleton (**D8.2.5v2**).
5. For boxing, make the extracted physical leaf consume an explicitly F64
   `MirValue`/register. Leave profile normalization and Lambda nullable-sentinel
   handling at the caller. Preserve result publication, scalar provenance and
   exception effects of the cold call. This consolidation prevents divergent
   fixes; its performance value must come from measured emission/coverage
   improvements, not the move to a shared file itself.

**B. Guarded storage access — T14-3.**

The existing core `MirIndexLoadPolicy` mixes physical layout with
`array_contract`, `nonnull_boundary`, null-result selection and `item_at`
fallback. Do not expose that entire Lambda policy as the JS API. Extract only
the common admitted access below it, using existing `em_element_address`,
`em_numeric_storage_type`, load/store and `MirValue` primitives first.

The minimal immutable description should identify:

- precisely rooted receiver/owner and the admitted storage base;
- tagged-slot or numeric storage kind, element width and signedness;
- the already-evaluated index and valid extent, with proof provenance;
- the requested physical result representation and destination;
- an operation-level miss label/continuation which preserves evaluated values.

Language wrappers establish receiver validity and semantic key conversion
before that description can be consumed. The shared leaf emits the remaining
physical bounds/address/load/store sequence and returns a `MirValue`. It does
not inspect a JS prototype or decide that a Lambda null is a JS `undefined`.
Keep operation-local proof lifetime separate from the longer-lived region
witness proposed in T14-4. These are proposed additions to existing helpers,
not claims that a common checked-index API already exists.

First pair an admitted tagged/ArrayNum access from unannotated Lambda with a
guarded ordinary JS-array access. Then extend the same physical leaf to JS
typed views after the JS wrapper supplies its validated base/length and element
conversion. Typed views must not be reinterpreted as Lambda array headers.
Stores retain destination ownership and profile-specific write permission,
widening/COW or view-conversion rules. Use the same machinery for both operand
orders and compound updates instead of adding a new lowering family per shape.

**C. Region proofs and hoisting — T14-4.**

Keep the already-shared scalar hoister for immutable scalar observations.
Its current admission requires a registered pure/loop-stable scalar call,
`NO_GC`, no reentry, preserved exception state, a non-GC scalar result and
preserved number-stack state. Removing one requirement to hoist a JS query is
not an acceptable shortcut (**D5.3.1–D5.3.5**).

For mutable storage, converge a small region witness over owner identity,
storage generation/stability proof, extent and relevant effects. A generation
is useful only if the runtime already maintains it truthfully; do not invent
a new global epoch or require all mutations to gain one for this tuning slice.
Begin with a straight-line or loop region with no unknown mutation/reentry.
Keep Lambda alias/COW kills and JS descriptor/prototype/detach/resize kills
profile-owned. Share the physical dominated-guard and witness-consumption
machinery only after both profiles can supply sound inputs. Unknown effects
terminate the witness conservatively.

Core Tune30's loop/tree versioning is an implementation reference, especially
its guard-before-effects and code-size tradeoffs. It is not a blanket proof
for dynamic JS arrays or a requirement to duplicate whole JS loops. Compare
bounded whole-region versioning with smaller shared access regions using
finalized instruction count, dynamic misses, compile time and paired execution.
Never resume a miss by replaying already-observable effects.

**D. Dynamic records and property names — T14-5.**

Reuse the common shape candidate walker and `MirConstructionPlan` /
`MirFieldAccessPlan`. Extend profile hooks for constructor/return/parameter
facts where the source census shows missed coverage. Candidate selection is
compile-time immutable, and every predicted shape still has an appropriate
runtime guard (**D8.4.1v2**).

The JS wrapper additionally establishes own ordinary data-property admission,
descriptor/writability requirements for a store, and the absence of relevant
proxy/host/accessor behavior. A shape match alone cannot establish those facts.
Share slot addressing, native load/store and scalar ownership; keep ECMAScript
lookup and enumeration ordering in the existing JS property kernel. Carry
context-owned canonical NameIds through that kernel instead of repeatedly
round-tripping through spelling strings (**D4.6.1v3–D4.6.2v2**).

**E. Calls, roots, compilation and runtime leaves — T14-6/7.**

Optimize common call/frame/root finalization in `MirEmitter` and consume it from
both profiles. Native scalar continuity should remove unnecessary Item values
and hence their root work naturally; do not weaken ownership to reproduce a
smaller MVP frame. Attribute root publication, result-home transfer, call
marshalling and compiler interference storage separately.

For an additional runtime leaf, document its full contract and two callers
before promotion. Raw byte operations may be shareable; JS UTF-16 indexing,
String coercion, RegExp protocols and observable hooks stay in JS. Allocator
reuse does not imply interchangeable object layout, finalization or mutability.
Do not introduce a JS-specific copy of a common helper merely to change its
effect annotation; audit the shared effect or split out a truly narrower leaf.

### 3.4 Prevent duplicate tuning work

Each implementation slice must record:

1. **Existing owner:** the core and JS functions searched/read, and whether the
   solution is direct reuse, extension, extraction or justified profile code.
2. **Two-client migration:** for a new common primitive, the working Lambda
   and JS callers, the old physical sequences retired in the same change, and
   the precise profile obligations left at each boundary (**D8.2.3**).
3. **Evidence:** finalized hot-region MIR, semantic misses, lifetime tests,
   code/compile cost and paired release results. A moved block or reduced file
   size is not a performance result.
4. **No duplicate ownership:** facts stay with their existing AST/function/type
   owners; shared plans are immutable. No copied third variant, parallel site
   cache or language-name branch inside the common physical operation.

The prior [P7 proposal](../Lambda_Proposal_JS_Unify_P7.md) explicitly withdrew
U-A's blanket structural lowering driver after examining mismatched layouts
and semantic control flow. Do not resurrect it to claim alignment: matching
AST tags do not prove matching child layouts or evaluation behavior. Its landed
emitter callbacks and the [unified compiler boundary](../Lambda_Design_JS_Unified.md)
are the foundation to extend. This plan shares specific physical operations,
not the full semantic walkers or their completion frames.

[Lambda Tune30](../impl/Lambda_Impl_Tune30.md) supplies live implementation
references; [Lambda Tune31](../impl/Lambda_Impl_Tune31.md) is still marked
PROPOSAL / not started in this audit. Coordinate future common work with that
plan, but do not count proposed Tune31 machinery as available code or as
measured improvement. Prefer the earliest useful shared slice over waiting for
an unrelated full Lambda tuning round.

### 3.5 Acceptance must prove reuse in untyped Lambda

- Add small unannotated `.ls` and `.js` fixtures that reach each newly shared
  physical path: alias/update arithmetic, read-compute-store, mixed/changed
  arrays, parameter/returned-record access and invalidation across a call.
  The `.ls` fixture must not depend on added type annotations to obtain coverage.
- Use a separate semantic oracle for each language. Shared hit paths can have
  comparable MIR, while JS coercion/holes/BigInt/negative zero and Lambda
  null/contracts/COW require deliberately different fallback tests.
- Verify both frontends call the common implementation and that the replaced
  copies are gone. Check the hot region, result representation, owner/root
  lifetime and reachable miss; total MIR similarity alone is insufficient.
- Run affected untyped Lambda release controls against the same predecessor
  used for JS; retain typed Lambda controls because they also consume common
  helpers. Include numeric, array, object and compile-heavy cases selected from
  the existing corpus. Investigate repeatable regressions under §5's policy.
- A useful extraction can be performance-neutral by itself. Accept it for
  demonstrated code reuse and parity, then attribute subsequent speedups to
  the measured extension. Do not assign the Lambda port's full performance
  advantage to the shared component.

These gates supplement the complete Lambda/Test262 closeout in §5. They do not
replace it, and they do not require a semantic or formal-design rule change.

## 4. Work packages and dependencies

| Package | Outcome | Dependencies |
|---|---|---|
| T14-0 | Fixed controls, population/oracle repair, census and backend inventory | None |
| T14-1 | Root-caused and resolved Navier regression | T14-0 |
| T14-2 | Complete Number facts, native local updates and profitable admission | T14-0; preserve T14-1 evidence |
| T14-3 | End-to-end ordinary/typed array native regions | T14-2 |
| T14-4 | Effect-bounded proof and metadata reuse | T14-3 |
| T14-5 | NameId continuity and broader guarded field/enumeration coverage | T14-0; reuse T14-2/3 carriers |
| T14-6 | Measured compiler/startup/memory scaling improvements | T14-0; remeasure after MIR-changing packages |
| T14-7 | Profile-gated built-in, allocation and call residuals | Relevant T14-2–6 packages |
| T14-8 | Native contract audit, consolidation and full acceptance | All required packages and conditional dispositions |

T14-1 investigation comes first, but its fix may be a narrowly isolated portion
of T14-2/3. Do not force a temporary rollback merely to satisfy package order.
Keep each causal experiment against its immediate predecessor and the fixed
starting control. Source, semantic tests and measurements travel together.
Apply §3.2–3.5 within each package: reuse/extraction is part of its delivery,
with explicit untyped Lambda consumers and controls where common code changes.

### T14-0 — Establish controls, meaningful outputs and an operation census

**Primary files:** `test/benchmark/run_standard_benchmarks.py`,
`run_paired_benchmarks.py`, `verify_js_mvp_manifest.py`, existing optimization
events in `lambda/js/js_exec_profile.{h,cpp}`, and helper-effect metadata.

- [ ] Build and archive **C14**, the actual current release starting tree. Record
  full commit, any dirty source patch, build configuration/toolchain, native
  module identities, binary SHA-256, power state and all backend/cache settings.
  Verify executed bytes before and after measurements. Result47 is a historical
  reference, not a substitute for this newer-tree control.
- [ ] Preserve Result46, original Result47 and repaired Result47 as distinct
  immutable artifacts. Use original R47 for reproducing its Navier observation;
  use C14 as the primary optimization control. Do not attribute later changes
  to the old commit merely because an archive filename includes that commit.
- [ ] Freeze 63 JS sources, inputs, wrappers, loop counts, timing boundaries and
  expected results in a versioned Tune14 manifest. Preserve MVP v1. Audit the
  timer-only delta before creating a successor; parameterize/reuse the verifier
  rather than bypassing it or rewriting historical hashes.
- [ ] Audit result oracles before trusting equal stdout. For Navier retain the
  canonical one-frame timing, then check a full-state digest/invariants against
  an independently evaluated reference outside the timed region. Run the
  original 15-frame checksum as a separate correctness diagnostic. Any wrapper
  change is versioned and applied to all relevant controls/engines.
- [ ] Run fresh full-JS/MVP/QuickJS/Node controls in the same session. Record
  every status and supported/unsupported row; do not silently drop MVP misses.
  Keep native Lambda/C ports as secondary physical-backend references.
- [ ] Produce separate execution, parse/analysis/lowering, native generation,
  teardown and peak-memory censuses. Sample the executing worker after startup;
  a compiler/teardown sample is not evidence for guest-loop cost.
- [ ] Extend existing opt diagnostics only as needed: refusal reasons, native
  producer/consumer continuity, guard hits/misses, NameId reconstruction,
  descriptor materialization, repeated metadata queries, helper calls,
  allocations, root stores and safepoints. Distinguish static sites, dynamic
  counts, self samples and inclusive samples. Disable counters for timings.
- [ ] Inventory script, eval, dynamic function, CJS/ESM, document, batch/cache,
  lazy-native and nested-language entries for the T14-8 backend/lifetime gate.

**Exit:** C14 and the comparison manifest are reproducible; each priority row
has a non-vacuous correctness oracle and an evidence category: confirmed cause,
structural gap, sampled hypothesis, or not yet measured.

### T14-1 — Resolve Navier's regression without discarding semantic fixes

**Primary files:** `js_mir_expression_lowering.cpp`,
`js_mir_function_collection_class_inference.cpp`,
`js_mir_module_batch_lowering.cpp`, `js_mir_calls_boxing_types.cpp`, and
`lambda/runtime/mir_emitter_shared.hpp`.

#### Structural trace — 2026-09-19

The current release MIR shows that `lin_solve`, `lin_solve2`, `advect`,
`project`, `set_bnd` and `addFields` have only boxed `_body` functions, with
no `_n` variant. Each captures mutable solver state (`width`, `height`,
`rowSize`, `iterations` or related fields), while current native-entry
admission deliberately requires zero captures. Their hot loops consequently
retain `js_to_numeric`, `js_increment`, generic arithmetic and generic ordinary
array operations. This is a structural admission gap, not evidence that the
landed direct-local update regressed Navier. A closure-aware ABI and an
effect-valid ordinary-array region belong to T14-2/T14-3; they must preserve
live captured bindings and ordinary-array semantics (**D2.4.1–D2.4.3**,
**D3.3.3v3**, **D8.2.4–D8.2.6**).

- [ ] Reproduce archived R46/R47 with the stronger oracle from T14-0. Reproduce
  C14 separately: later BigInt/native-fact fixes can change generated code and
  must not be conflated with the original regression.
- [ ] Diff finalized MIR by function and semantic operation. Count instructions,
  static calls, guard branches, box/unbox sequences, scalar homes, roots and
  safepoints; measure native bytes/spills where available. Focus first on
  `lin_solve`, `lin_solve2`, `advect`, `project` and `addFields`.
- [ ] Profile actual hot paths and determine whether time is spent in redundant
  guards/conversions, fallback frequency, generic operations, root publication,
  native spills/code size, or a runtime helper regression.
- [ ] Bisect or ablate independently while retaining the repaired generic
  BigInt path. Capture a small semantic reproducer for the responsible lowering
  shape. A larger MIR dump alone is not sufficient causal attribution.
- [ ] Fix the admitted operation/region. Keep unknown sites compact; evaluate
  operands once and preserve error/ownership joins. A size/profitability rule
  must use general code/effect facts, never a workload/function-name whitelist.
- [ ] Pair the fix with R47 and C14, with R46 as a recovery reference. Run search,
  FFT, numeric controls and mixed/exotic misses to detect displaced cost.

**Exit:** the causal change is identified, the reproducer remains correct, the
2.646x loss is removed or any remaining delta has a measured explanation, and
the full-JS semantic repair remains intact. A remaining unexplained confirmed
regression prevents closing this package.

### T14-2 — Complete Number facts and native local updates

**Primary symbols:** `jm_infer_indexed_node`, `jm_infer_param_types`,
`jm_populate_numeric_binding_facts`, `jm_numeric_binding_type`,
`jm_transpile_update_unary`, `jm_emit_assignment_value`, and existing
`MirNumericOpPlan`/`FnVariantAnalysis` consumers.

**Reuse requirement:** §3.3A. Reuse common numeric operation selection and
variant carriers; converge dependency mechanics with core untyped inference.
Extract equivalent nonnullable boxing only with both live callers migrated.

#### Implementation update — 2026-09-19: shared F64 boxing, native updates and direct alias-chain compound evidence

The first numeric slice extracts `em_box_f64_to_item` into the common emitter
and migrates both untyped Lambda's `emit_box_float` and LambdaJS's
`jm_box_float`. The shared leaf owns only the physical non-null F64-to-`Item`
encoding, including the in-band representation and signed-zero case. Its
profile callback performs the cold allocation/publication call, so JS retains
its error/completion tracking and Lambda retains its existing call protocol.
This is a two-live-caller reuse, not a JS wrapper around Lambda semantics
(**D1.3v3**, **D5.3.4**).

`jm_transpile_update_unary` now emits F64 `dmov` plus `dadd`/`dsub` when a
native variant has a direct, non-captured mutable F64 local. It explicitly
refuses environment, module, state, const, TDZ, `with` and non-identifier
cases. Postfix updates copy the old F64 value before writeback. Generic updates
remain the semantic path for coercion, BigInt, member/reference targets and
unproved representations (**S1.11**, **D2.4.1–D2.4.3**).

The indexed collector also records the candidate edge from a resolved LHS alias
to an RHS resolved binding in compound arithmetic. Its direct-body worklist is
bounded by the eligible declarations in that function and advances in source
order, so `first = x; cursor = first; cursor -= y` resolves through the
already-published `first` binding without a fixed alias cap. This admits a
guarded Number entry only; it is not an unconditional type fact. The native
wrapper guards both F64 inputs, while strings, BigInts and every other miss
continue through the generic body (**D2.4.1–D2.4.3**, **D8.2.5v2**).

Focused `JsOpt.NativeNumberUpdatesKeepPostfixAndGenericSemantics` verifies
postfix ordering, `-0`, string coercion, BigInt fallback, trace-off parity and
the finalized native/generic MIR split. `JsOpt.NativeAliasCompoundAssignmentKeepsGenericSemantics`
adds direct and chained alias cases, string relational behavior and BigInt
fallback; the pre-existing Number-plan contract also passes. The direct-local update slice
alone measured `larceny/diviter` tuned/control ratios 0.744359, 0.742719 and
0.749489 (median 0.744359; 9,625.095 versus 12,959.271 ms) in an alternating
release comparison against an isolated `HEAD` control.

After adding direct alias compound evidence, a separate three-pair alternating
release comparison measured cumulative Tune14 ratios 0.049652, 0.048964 and
0.048967 (median 0.048967; 611.610 versus 12,489.937 ms). Finalized
`diviterDiv` MIR has F64 arguments with `dge`, `dsub` and `dadd`; its generic
body still retains coercive comparison and `js_subtract`. This establishes only
the cumulative `diviter` effect of the current working tree, not an isolated
alias-only delta. Navier remains a separate problem: six one-frame pairs after
the complete slice ranged from about 0.9845 to 1.0019 tuned/control, with a
median near 0.9927. No full fixed-population matrix or QuickJS comparison has
been run.

Comparison/join/dependency propagation beyond direct-body aliases, native
integer range proof, array regions and the Navier root cause remain open.

#### A. Infer a guarded entry candidate through binding relationships

The landed direct-body chain covers `r = x; r -= y` and
`first = x; r = first; r -= y` when the resolved bindings lead back to formals.
It does not yet propagate through comparisons, joins, nested scopes or a general
alias graph. The solution remains a candidate native entry guarded for Number
inputs, not treating these operators as proof that every source call receives
Numbers.

- [~] Express numeric-use dependencies using resolved binding identity and the
  existing function-owned index. Direct-body source-ordered alias chains and
  compound assignment are implemented; propagate comparisons, joins and other
  dependencies with a bounded fixed point and explicit refusal on
  unsupported/ambiguous joins.
- [~] Establish all required Number guards before specialized effects. The
  current edge relies on the existing native-entry F64 guards; preserve
  the exact original values on the generic entry, including missing arguments,
  strings, BigInt, Symbol and objects with coercion hooks.
- [ ] Track initialization and every reaching write. A mutable alias is not a
  permanent type certificate; reassignment, capture, `eval`, `with` and unknown
  effects must invalidate or prevent the relevant proof.
- [ ] Keep variant-local facts with the guarded variant. Do not restore the
  pre-repair publication of speculative F64 facts into the generic boxed body.
  Separately derived unconditional facts may apply there only with their own
  sound proof (**D3.3.2v2**, **D2.4.1–D2.4.3**).
- [ ] Broaden admission beyond numeric return types where local regions justify
  it. Explain zero-numeric-parameter and capture-bearing refusals; admit only
  effect/entry shapes supported by the existing ownership contract, rather than
  removing eligibility restrictions globally.

#### B. Emit native updates and consume native facts consistently

- [x] Add a proved-Number local `++`/`--` arm before the generic reference path.
  Use F64 add/subtract with JS Number rounding; preserve the old numeric value
  for postfix and the new value for prefix. Respect const assignment and TDZ.
- [ ] Reuse common native assignment/writeback machinery. Keep member updates,
  accessors, unresolved/with bindings and unproved values on their full semantic
  path; do not duplicate reference evaluation or coercion.
- [ ] Preserve negative zero in postfix results, NaN/infinities and behavior
  around 2^53. A native integer induction lane requires a separate range proof;
  C integer overflow is not a JS Number implementation.
- [~] Ensure arithmetic/comparison/compound-assignment consumers use the same
  variant facts. Direct alias compound assignment now does so; do not box a
  native local solely because one consumer reads a less precise AST type. Keep
  result/error representations explicit.
- [ ] Use the existing definite-initialization proof to remove only redundant
  TDZ checks. Keep declaration identity, shadowing and loop joins correct.

**Fixtures:** prefix/postfix values, discarded updates, shadowed aliases,
Number/non-Number calls to the same function, changing types, const/TDZ,
captured updates, `eval`/`with`, coercion throwing once, BigInt, signed zero,
NaN/infinities and large Number rounding. Add a compact `diviter`-shaped fixture
whose hot loop's MIR excludes generic arithmetic/update helpers.

**Targets/controls:** `diviter`, `mbrot`, search induction and quicksort;
preserve `sum`, `sumfp`, `fib`, `fibfp`, `mandelbrot`, `collatz` and `pidigits`.

**Exit:** both diviter kernels have appropriate guarded native coverage, the
proved loop updates/arithmetic stay native, generic calls preserve full JS
behavior, and paired release evidence supports the improvement.

### T14-3 — Complete ordinary and typed array regions

**Primary symbols/files:** `jm_is_array_literal_candidate`,
`jm_emit_packed_array_read_impl`, `jm_emit_fixed_typed_array_load_number`,
`jm_emit_fixed_typed_array_number_store`,
`jm_try_emit_right_typed_array_number_binary`, `js_typed_array.cpp`, and existing
dense/typed store helpers in `js_runtime.cpp`.

**Reuse requirement:** §3.3B. Split the physical portion of the core checked
index path and pair its Lambda consumer with the JS access consumer. Keep
semantic access policies outside the shared leaf; extend existing address and
storage primitives before adding another helper.

- [ ] Produce an admission/refusal map for all three search algorithms, FFT
  `four1`, Navier's inner functions and quicksort `partition`. Account separately
  for receiver kind, key, element value, operator, local join and destination.
- [ ] Extend producer/consumer continuity to both operand positions and nested
  expressions. Refactor the existing one-sided typed-binary shape into shared
  lowering before introducing another nearly identical arm. Keep left-to-right
  evaluation and the already-evaluated reference on a miss.
- [ ] Keep numeric lengths/indices, loaded Numbers, arithmetic/comparison
  results and writable destinations native. Materialize Items at actual generic
  consumers, representation merges, boxed ABI edges or semantic misses.
- [ ] Wire the existing guarded Number-store leaf into admitted FFT stores.
  Explain each surviving boxed setter; presence of a leaf in the registry is
  not evidence that the workload uses it.
- [ ] Reuse one physical typed-access plan for supported element kinds. Add
  `Int32Array` first for quicksort, with signed loads and exact ToInt32 store
  behavior. Add other kinds only with explicit conversion tests and measured
  use. Keep clamping, Float32 rounding and BigInt kinds distinct.
- [ ] Preserve ordinary-array holes and indexed descriptors/prototypes. Prove a
  present own element before skipping prototype behavior. A Number element
  guard is not a certificate that every element of an aliased tagged array is
  numeric. Numeric-array representation facts must remain valid under mutation.
- [ ] Separate overwriting an existing writable slot from creating/growing a
  property so the former does not inherit irrelevant growth checks. Retain the
  established scalar-home/store ownership primitive.
- [ ] Keep index semantics exact: numeric `-0`, string `"-0"`, fractions,
  NaN/infinities, bounds, holes, detached/resizable views and coercing stores.
  Receiver/source hints select a guarded arm, never prove a runtime brand.

**Fixtures:** read-compute-store with both operand orders; alias mutation;
ordinary mixed arrays; holes with inherited getters; descriptor changes;
proxy/host/wrong-brand receivers; detached/resized views; coercing RHS; thrown
keys; signed and wrapping Int32 stores; fast/miss ownership joins under GC.

**Exit:** the diagnosed search and FFT regions consume native values through
their hot operations, Int32Array quicksort has guarded coverage, and dynamic
hit/miss evidence plus paired release measurements confirm benefit. List any
remaining boxed operations explicitly. Helpers on cold miss arms are allowed.

### T14-4 — Reuse guards and metadata within valid effect regions

**Primary files:** existing loop/effect analysis,
`lambda/runtime/mir_loop_invariants.hpp`, array lowering and helper catalog.

**Reuse requirement:** §3.3C. Extend the common scalar hoister for scalar
invariants; converge memory-witness handling separately. Do not copy Lambda's
typed dense-loop scanner and treat its conclusions as JS admission facts.

- [ ] Represent an operation/region-local witness through existing immutable
  plans: receiver identity, storage/element kind, valid index range, length/data
  validity and its invalidating effects. Do not add a mutable site cache or
  duplicate global analysis table.
- [ ] Reuse or hoist only facts whose full dependencies are invariant. A kind
  check does not alone authorize reusing length, data or element-value facts.
- [ ] Kill/reacquire witnesses for relevant alias writes, length changes,
  backing replacement, descriptor/prototype mutation, detach/resize, reentry or
  unknown calls. `NO_GC` is not proof of nonreentry, nonmutation or nonthrowing.
- [ ] Consume raw data borrows immediately unless a region-wide stability proof
  exists. Keep the owner precisely rooted if a retained borrow can cross an
  admitted safepoint; otherwise end the region at the call.
- [ ] Place misses before observable effects, or continue from a valid
  operation-level continuation. Never restart a partially executed loop or
  repeat a getter/coercion to recover from a failed guard.
- [ ] Measure reduction in dynamic metadata queries/guards against T14-3's
  predecessor, not only against C14. Bound guard/code expansion for miss-heavy
  loops and preserve compact generic lowering where specialization is unhelpful.

**Exit:** at least the diagnosed ordinary-array search and typed FFT regions
reuse justified metadata beyond a single leaf call, with invalidation fixtures,
fewer executed queries and no rooting/evaluation-order regression.

### T14-5 — Preserve names and broaden guarded ordinary fields

**Primary files:** `lambda/core/name_pool.cpp`, `js_props.cpp`,
`js_property_attrs.cpp`, `js_runtime.cpp`, `js_globals.cpp`, and
`jm_plan_predicted_literal_field` with the existing `MirFieldAccessPlan` and
shape-candidate planner.

- [ ] Trace NameIds from parser/static linking and enumeration through reference
  creation, own/prototype lookup and stores. Count each spelling/hash/catalog
  reconstruction and remove only redundant boundaries.
- [ ] Preserve the owning context's ID domain. Materialize observable strings
  at reflection/proxy boundaries; do not make dynamic IDs portable across realms
  or use spelling to identify a JS Symbol (**D4.6.1v3–D4.6.2v2**).
- [ ] Extend existing shape candidates through analyzable constructor assignments,
  returned records and local aliases. Account for partial initialization,
  constructor escape, conditional fields, constructor-returned replacement
  objects, subclasses and prototype changes.
- [ ] Guard the live shape, applicable own descriptor and slot representation.
  Use shared direct load/store primitives on a hit; retain the property kernel
  for misses. Keep mutable field values independent of immutable layout facts.
- [ ] Preserve native numeric field values into native consumers/destinations.
  Avoid wrapping a direct field load in the generic numeric protocol immediately.
- [ ] Extend the existing nonallocating internal descriptor inspection where
  profiles find remaining materialization. Preserve for-in liveness, deletion,
  re-addition, shadowing, ordering and array index/length rules. Public reflection
  must still construct its specified result object.
- [ ] Separate property/method resolution from callable invocation in profiles.
  JetStream `hashmap` is a JS implementation with fields and array buckets;
  native `Map.set` changes are not a substitute for its access coverage.

**Fixtures:** shape hit/miss, field deletion/redefinition, accessors/proxies,
constructor escape/return/subclassing, own versus inherited fields, mixed numeric
stores, symbol keys, enumeration mutation, cross-context cache reuse and GC.

**Targets:** Havlak, Richards, DeltaBlue, CD, microdiff, JetStream hashmap,
Prettier AST, log pipeline and merge workloads.

**Exit:** constructor/returned-record fields have demonstrated guarded coverage,
targeted names avoid repeat resolution, and descriptor inspection avoids measured
temporary allocations. Publish admitted/refused cases and per-family A/B results.

### T14-6 — Finish compiler, cold-start and memory scaling work

**Primary files:** `em_finalize_semantic_root_write_back` and scalar-home
planning in `mir_emitter_shared.hpp`, indexed analysis, JS initializer lowering,
MIR artifact/cache ownership and native-code lifecycle.

#### Implementation update — 2026-09-19: safe cache rejection and parallel AST prebuild

A JavaScript module-cache candidate records copied source and declaration
metadata, but initially points at the MIR context whose compiled function bodies
have already been published into the active realm. Cache admission is optional.
When it is disabled or rejects that candidate, cleanup now releases only the
candidate-owned copy; ordinary module completion retains the live context in the
realm's deferred code store. A cross-language exported function therefore cannot
call an address from a context that candidate cleanup has already destroyed
(**D5.4.3**, **D8.5.1v4**).

The Lambda script registry mutex also no longer covers a cache single-flight
claim. A parallel AST-prebuild worker may recursively register an import while
another worker waits on that claim; holding the receiving runtime's registry
mutex across the wait deadlocked the closure. The cold global system-function
maps now publish through `uv_once`, so independent first-import workers cannot
race initialization (**D8.1.1v10**, **D8.5.1v4**). Focused cache tests cover a
two-worker shared-import closure and a one-entry cross-language `test-batch`
manifest with `LAMBDA_SCRIPT_CACHE=off`; the latter previously terminated from
a freed MIR code context.

- [ ] Attribute bytes and lifetime overlap to AST/index, MIR, CFG, root
  candidates, collecting-call liveness, interference, scalar homes, native code
  and guest objects. Measure peak RSS and retained bytes, not just allocation
  counts. Distinguish one-shot compilation from repeated contexts/cache reuse.
- [ ] Retain Tune13's collecting-call-only liveness change. Audit remaining
  candidate-by-candidate interference and repeated full-index scans. Use
  sparse/adaptive storage or indexed facts only when the census shows a scaling
  problem; do not create a second permanent planner or liveness oracle.
- [ ] Preserve exceptional edges, aliases, scalar-home constraints, exact slot
  interference and root reload behavior. Temporary fact comparisons may aid
  migration; dynamic GC remains the independent oracle (**D8.6.3**).
- [ ] Measure compact numeric-array initializer coverage before extending it.
  Reuse construction primitives for further admitted forms only when code
  volume is material. Preserve property order, duplicate keys, `__proto__`,
  holes, computed keys, spreads, getters and partially constructed ownership.
- [ ] Account for recent generated-IR release on retained artifacts. Do not
  retain compiler IR merely to make diagnostics easy, or free code/context state
  while closures, callbacks or native lazy entries can still call it.
- [ ] Run a size-scaling family plus Hyphen, Prettier and affected document
  scripts. Record finalized instructions, native bytes, root stores/safepoints,
  lowering/generation time and memory peaks. Keep diagnostic and timing builds
  separate; no vendored MIR modification is assumed.

**Exit:** remaining major costs have owner/phase attribution, diagnosed scaling
defects are reduced with dynamic-root and MIR evidence, and cold/RSS comparisons
are published. A measured low-cost component can receive a no-change disposition;
an unmeasured or unresolved large peak cannot be declared complete.

### T14-7 — Address measured built-in, allocation and call residuals

This package requires profiling and an explicit disposition for each family;
it does not require speculative implementation in every family.

| Family | First measurements | Permitted next step | Required semantic guards |
|---|---|---|---|
| RegExp | Matching vs property/protocol vs result allocation on regexredux, revcomp and Hyphen. | Reuse bulk kernels; retain immutable pattern classification with its compiled owner when valid. | Custom `exec`, symbol protocols, source/flag accessors, subclassing, global/sticky lastIndex, empty/Unicode matches, throws and reentry. |
| Strings | ASCII/non-ASCII paths, repeated UTF-16 index/length scans, substring/concat allocation. | Existing string leaves; immutable index metadata only for a demonstrated scan cost. | UTF-16 code units, lone surrogates, observable coercion/order and correct lifetime. |
| Allocation/GC | Allocations/bytes, collection time, root/safepoint work and live/retained objects on Hyphen, binarytrees, gcbench and Havlak. | Remove duplicate construction, ownership bookkeeping or temporary objects; audited leaf effects where true. | Precise roots, scalar-home transport, weak/strong edges and cleanup on failure. |
| Calls | Dispatcher self time vs argument preparation, property lookup, metadata and callee work. | Reuse established direct/light invoke paths and rooted argument spans where their contract permits. | Get-before-arguments, `this`, `new.target`, bound args, proxies, constructors, exceptions and activation observability. |

Do not add overlapping inclusive samples into a promised speedup. Do not label
a generic helper `NO_GC` because its frequent branch is nonallocating. A shared
leaf and full semantic miss must retain truthful effect contracts, completion
handling and precise liveness (**D5.3**, **D6.2.2v2**, **D8.4.3v2**).

**Exit:** each family has either an attributed implementation with paired
evidence, or a measured no-change/deferred disposition that names the remaining
cost. Full JS already beats historical MVP on some allocation-heavy workloads;
the MVP's private heap is not evidence for replacing the full engine's collector.

### T14-8 — Native contract, consolidation and full acceptance

- [ ] Complete the T13-1 entry matrix: default/explicit MIR, explicit AST,
  old size boundaries, document scripts/modules, CJS/ESM, eval/dynamic functions,
  batch/cache, nested languages and inherited interpreter-global state.
- [ ] Verify actual native entry publication/execution and generator cleanup,
  including opt=0, admitted lazy-native mode, timeout/failure, retained callbacks
  and repeated contexts. Preserve explicit AST admission; do not confuse AST
  unsupported cases with MIR correctness or silently replay effects.
- [ ] Resolve the document-module routing residual under **D8.1.3v11** or
  document tested unreachability from selected MIR. A source-string scan alone
  cannot close the entry-matrix requirement.
- [ ] Recheck inherited `list`, `crypto_sha1`, binarytrees/deriv and other
  repeatable losses using genuine controls. More than 3% triggers mandatory
  investigation; smaller systematic regressions still require disposition.
- [ ] Remove abandoned experiments, duplicated primitives, unused tuning flags,
  redundant conversions and temporary fact verifiers. Keep useful diagnostics
  disabled by default. Do not reduce LOC by stripping comments/blank lines.
- [ ] Close §3.4's reuse ledger for each common change: two live consumers,
  retired duplicate sequences, profile-owned semantics and untyped Lambda
  reachability. Explain any newly profile-specific implementation by its
  different contract, not by the location of the existing helper.
- [ ] Run the complete gates below on the final source and identified binaries.
  Preserve all statuses and explain any remaining performance miss.
- [ ] Update this plan's progress and link durable evidence. Add a carryover
  disposition to the historical Tune13 record without claiming that new results
  were measured on its old binaries. Sync implementation-status documentation
  only for behavior actually verified; formal semantics/design are unchanged.

**Exit:** required structural/correctness packages pass, conditional work has
evidence-backed dispositions, final performance/memory results are reproducible,
and confirmed regressions are resolved or remain explicitly open. Do not equate
implementation completion with attainment of the QuickJS/MVP milestones.

## 5. Validation and performance acceptance

### 5.1 Correctness and structural evidence

Use the current test population rather than historical passing counts.

| Gate | Required evidence |
|---|---|
| Focused JS semantics | Boundary/miss fixtures for each package; correct output, evaluation count/order and error identity. Extend existing test owners. |
| Optimization contracts | `make test-js-opt`; trace admissions and misses independently of timing. This target builds debug: rebuild release before any performance run. |
| Finalized MIR | `test_js_mir_emission_gtest`, relevant shared emission tests and `test_mir_ratchet_gtest`; inspect the hot region and complete fallback, not just an `_n` name. |
| Precise GC | `make test-mir-gc-stress` and focused JS runs with `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`; fast/miss joins, arrays, closures, throws, captured scalars and retained artifacts. |
| Runtime baselines | `make test-lambda-baseline` and `make test262-baseline` after the final changes. Retain Tune13's full-Test262 closeout with `make test262-full`; report baseline regressions separately from broader unsupported cases. |
| Shared consumers | Typed/untyped Lambda controls; affected document/Radiant baseline and module/eval/batch paths when their compiler/runtime behavior changes. |
| Reuse contract | §3.5 unannotated Lambda/JS fixtures reach the shared physical primitive; old copies retired; profile-specific misses and ownership preserved. |
| Ownership/effects | Existing GC effect/hazard checks; truthful call effects, no conservative native-stack scan and no roots removed solely for speed. |

New Lambda `.ls` fixtures require matching `.txt` expected outputs. Test262
failures/crashes/timeouts must be investigated in the runtime; never modify its
harness to mask them. Inspect deterministic finalized MIR before any necessary
profile-specific zero-slack budget edit (**D8.6.1–D8.6.3**).

### 5.2 Measurement protocol

1. Use release builds only. Archive C14 and each candidate under unique exact
   identities; capture external-module/configuration differences. Diagnostic
   builds and dump/profile runs do not supply accepted elapsed-time samples.
2. For each implementation package, run at least 11 alternating
   control/candidate pairs on targets, representative misses and controls.
   Compare each predecessor as well as C14. Publish raw samples, valid-output
   accounting and paired uncertainty. Increase pairs where the estimate is
   uncertain; do not keep only favorable reruns.
3. Before closeout, pair all 63 full-JS rows against C14, and collect a fresh
   same-session complete full-JS/MVP/QuickJS matrix with at least three process
   samples per engine/row. Use a second independent complete session to confirm
   any claimed cross-engine milestone.
4. Preserve the standard runner's build, profile, power, output and Test262
   gates. Explicitly request QuickJS and MVP: the current default engine list
   omits them. Run timed processes without concurrent build/profiling load.
5. Report startup/lowering/native generation and outer process time separately
   from workload time. Include peak RSS, retained bytes, cold/reused contexts
   and native code volume. A warm-loop win cannot erase a cold-start regression.
6. Report every row/status. Failure or timeout prevents claiming complete
   63-row acceptance. An explicit matched subset can be diagnostic but cannot
   replace the acceptance population or silently shrink its denominator.

Define, for valid positive row medians:

```text
G(A/B) = exp(mean_i(log(time_A_i / time_B_i)))
T(A/B) = sum_i(time_A_i) / sum_i(time_B_i)
```

Also report row win counts, suite geomeans, worst losses, and the timing range
or ordered samples. `T` is the ratio of sums, not the arithmetic mean of row
ratios. Node-normalized historical comparisons remain labelled proxies.

### 5.3 Explicit milestones

These are performance acceptance targets, not changes to language semantics or
guaranteed consequences of any individual package.

| Milestone | Target | Required qualification |
|---|---|---|
| Structural completion | All required package exits and current correctness gates pass. | List conditional dispositions and remaining unsupported specializations. |
| Beat QuickJS | Fresh `G(full JS / QuickJS) < 1.00`. | Complete 63 rows in both confirmation sessions; publish `T`, suites and losses. This is an aggregate claim, not faster on every row. |
| Tune13 stretch carried forward | Fresh `G(full JS / QuickJS) <= 0.80`. | Same population/protocol; do not retain an obsolete historical gap as the baseline. |
| Close to MVP, Tune14 planning target | Fresh `G(full JS / MVP) <= 1.20`. | Same audited source/input/work; all 63 supported and valid, with full JS semantics retained. Unsupported MVP rows must be resolved or the milestone remains unverified. |
| Absolute workload cost | Reduce `T(candidate/C14)` and publish `T(full JS/MVP)` and `T(full JS/QuickJS)`. | No unsupported promise that one geomean target implies the same sum ratio. |
| Regression/operational gate | No unresolved confirmed correctness regression; investigate timing, startup and RSS regressions. | More than 3% repeatable elapsed regression requires root-cause analysis; smaller systematic losses remain visible. |

If the mandatory implementation work is finished but a performance target is
missed, record **implementation complete; performance milestone unmet** and
retain the measured residual. If a required structural or correctness exit is
still open, the plan remains partial. Avoid claiming completion from effort,
attempted experiments, a favorable subset or historical baseline passes.

### 5.4 Command templates

The following are future execution templates, not commands run to create this
plan. Replace archive names with the exact C14/candidate identities established
by T14-0; create output directories beneath `./temp/` before exploratory work.

```sh
make test-js-opt
make test-mir-gc-stress
make test-lambda-baseline
make test262-baseline
make test262-full
make release

python3 test/benchmark/run_paired_benchmarks.py \
  --control test/benchmark/exe/lambda-tune14-control \
  --candidate test/benchmark/exe/lambda-tune14-candidate \
  --language js --suite jetstream --bench navier_stokes \
  --pairs 31 --timeout 180 \
  --output temp/tune14/navier_paired.json

python3 test/benchmark/run_paired_benchmarks.py \
  --control test/benchmark/exe/lambda-tune14-control \
  --candidate test/benchmark/exe/lambda-tune14-candidate \
  --language js --pairs 11 --timeout 180 \
  --output temp/tune14/final_paired.json

python3 test/benchmark/run_standard_benchmarks.py \
  --engines mir,c2mir,lambdajs,mvpjs,quickjs,nodejs \
  --suite r7rs,awfy,beng,kostya,larceny,jetstream,text \
  --runs 3 --timeout 180 --cooldown 10 --typed \
  --results-output test/benchmark/js_mvp/tune14/final_session1.json \
  --report-output test/benchmark/js_mvp/tune14/Overall_Final_Session1.md \
  --report-title 'JS Tune14 final session 1' \
  --log-dir temp/tune14/final_session1
```

Archive names above are placeholders and must not be reused for different bytes.
The standard runner may rebuild/archive independently; verify its recorded hash
against the paired candidate, rather than assuming a shared filename or commit
means identical executable content. Confirm current runner/report support for
the complete engine list before the long run, without disabling its guards.

## 6. Evidence storage and progress record

Exploratory dumps, probes and profiles belong under `./temp/tune14/`.
Accepted raw JSON, manifest, hashes, commands, focused reproducer references and
generated reports belong under `test/benchmark/js_mvp/tune14/`. Do not hand-edit
published timing cells or overwrite Results46/47, Tune12, Tune13 or MVP v1 data.

Starting evidence, with its limits:

- [Result46 JSON](../../test/benchmark/benchmark_results_v46.json) and
  [Result47 JSON](../../test/benchmark/benchmark_results_v47.json): durable
  snapshots with metadata and raw samples, subject to §2's mixed/session limits.
- [MVP release acceptance](../../test/benchmark/js_mvp/JS_MVP_Release_Acceptance_20260915.md):
  historical narrow-runtime performance; not a full-JS conformance oracle.
- [Tune13 implementation record](../impl/JS_Tune13_Impl.md): landed mechanisms,
  focused verification and open package exits; unpaired Hyphen observations
  do not become package A/B evidence.
- §3.2's source-owner map: verified common primitives, profile-local copies
  and proposed extraction boundaries. Record two-client migrations and
  untyped Lambda controls with the implementing package's evidence.
- `temp/result47_ljs_navier_v46_vs_v47.json`: the prior 31-pair regression
  diagnostic. Its weak stdout oracle is explicitly addressed in T14-0.
- `temp/result46_47_analysis/{comparison.json,mir_provenance.json,*.mir}`:
  arithmetic and six release diagnostic executions used in this analysis.
  Key findings are reproduced in §2 so the plan survives scratch-file removal.
  Reproduce and archive whatever is used for implementation acceptance.

| Package | Current status | Evidence needed to close |
|---|---|---|
| T14-0 | [ ] Not started | Exact C14, audited manifest/oracles, complete controls and census. |
| T14-1 | [~] Structural gap traced | Captured boxed kernels and generic-array helpers identified; still need a semantic reproducer, fixed-control recovery and causal repair. |
| T14-2 | [~] In progress | Extend the landed shared F64 boxer, guarded local `++`/`--` and direct-body alias-chain compound admission to complete Number/update regions; retain generic parity and add comparison/join/range proofs. |
| T14-3 | [ ] Not started | Search/FFT/Int32 coverage, shared physical access with untyped Lambda, complete misses and dynamic hits. |
| T14-4 | [ ] Not started | Shared scalar/region mechanics, profile-valid invalidation and measured query reduction. |
| T14-5 | [ ] Not started | Field/name/enumeration coverage and broad paired results. |
| T14-6 | [~] Stability repair landed | Cache-rejection native-code lifetime and parallel prebuild publication are covered; compiler/memory owner census, scaling fixes/dispositions and cold results remain. |
| T14-7 | [ ] Not started | Per-family profiles and implemented/no-change/deferred dispositions. |
| T14-8 | [ ] Not started | Native entry/lifetime audit, two-client reuse ledger, full gates, durable final matrices and milestone status. |

For every landed package record the exact revision/binary, predecessor, changed
proof or primitive, admitted/refused cases, semantic/GC/MIR checks, paired result
and uncertainty, code/compile/RSS impact and remaining work. No implementation
or runtime validation is claimed by this planning document itself.
