# JS Tune12 — Full LambdaJS performance from MVP v1 designs

**Version:** 1.2.0

**Date:** 2026-09-16

**Status:** PHASE 1 IMPLEMENTED; PHASE 2 PLANNED — the 0.80x full-LambdaJS/QuickJS milestone remains unmet.

**Scope:** improve the full LambdaJS runtime and MIR lowering while retaining Lambda runtime types and full JavaScript semantics.

**Investigation baseline:** `f72f7552b027d01829a874ece9a8a591206db722`.

This is the implementation plan for [Full LambdaJS performance: lessons from MVP v1](JS_MVP_Full_Runtime_Comparison.md). It continues [Tune11](JS_Tune11%20%28done%29.md), whose shared numeric, index, field and string improvements are already implemented. The retained [MVP v1](JS_MVP_Runtime.md) is a source of design evidence and a comparison backend; Tune12 changes the full engine.

## 1. Objective and completion policy

Make common full-JS operations perform the same small amount of physical work that makes MVP fast, after establishing the guards and ownership required by full JavaScript. Prioritize unnecessary algorithms and missed specialization before broad runtime restructuring.

The Phase 1 implementation order was:

1. Eliminate Map/Set insertion-order rescans.
2. Extend existing native numeric variants to local accumulators and recursive numeric results.
3. Remove dominated TDZ checks and unnecessary ownership traffic using those facts.
4. Add small Number and own-element heads to existing runtime helpers.
5. Extend MIR array access to parameters, numeric arrays and fixed typed arrays.
6. Broaden guarded ordinary-field access and proved call entries.
7. Admit bulk RegExp operations through the existing semantic entry points.
8. Validate the complete standard workload population and publish the residuals.

Phase 2 (§17–§27) follows the measured residuals: remove irrelevant property
bookkeeping, specialize complete numeric/array loops, then address object/call
costs and separately profiled allocation, compilation and built-in work. Its
unchecked work packages are not covered by the Phase 1 completion record.

Track three outcomes separately:

| Outcome | Required evidence |
|---|---|
| Implementation complete | Every work package in the declared phase has its code, structural evidence, correctness gates and measurement record; deliberately excluded cases retain a tested semantic fallback; Phase 1 completion does not close Phase 2 |
| Tuning successful | Exact-release A/B demonstrates a full-population improvement, with no unresolved material regressions or correctness failures |
| Performance milestone reached | A complete fresh 63-row matrix meets the stated full-JS/QuickJS milestone; a subset or estimated speedup cannot substitute |

Proposed performance milestones are **full LambdaJS / QuickJS geometric mean ≤0.80x**, followed by the earlier discussion's **≤0.30x** stretch objective. These are proposed full-engine targets, not measurements or promised results. The user's earlier 0.80x acceptance applied to MVP; this plan does not retroactively change that acceptance contract. If these targets remain unmet, report the actual result and leave the performance milestone open even when the listed implementation is complete.

From historical Result45's 2.588162x, these milestones would require approximately 3.24x and 8.63x improvement respectively. They cannot be forecast from the measured MVP gaps because MVP makes additional semantic assumptions. Per-row reporting remains mandatory; there is no requirement that every row beat QuickJS.

The Phase 1 final matrix is now 3.481106x against its recorded QuickJS binary;
reaching 0.80x from that observation would require approximately 4.35x further
geometric-mean improvement. This is arithmetic, not a Phase 2 speedup forecast.

## 2. Authority and constraints

The following existing rulings govern all phases. Tune12 proposes no change to them.

| Formal ruling | Implementation consequence |
|---|---|
| **S1.11** | Full LambdaJS keeps ECMAScript semantics, including coercion, mutation, iteration, proxies and observable built-in lookup |
| **D1.3v3**, **D8.2.3** | Share physical operations below language admission; converge existing consumers before extracting shared machinery |
| **D2.4.1–D2.4.3** | Keep semantic contracts, inference, representation and ownership distinct; unboxing is not permission to coerce |
| **D3.3.2v2**, **D3.3.3v3** | Candidate native entry shapes do not retype the generic source contract; container facts must respect their binding and lifetime |
| **D5.3.2–D5.3.5** | GC happens at declared safepoints; precisely root live references and preserve scalar homes/argument ownership |
| **D5.4.2–D5.4.4** | Publish stable metadata at construction/finalization; avoid repeated preparation and context-dependent code-baked pointers |
| **D6.2.2v2** | Dispatch through actual callable capabilities and existing entries; names/catalog IDs cannot select semantics |
| **D8.4.1v2** | Use immutable predictions and guards; no mutable per-site inline caches, feedback vectors or patched targets |
| **D8.4.2v2**, **D8.4.3v2** | Reuse direct/native and dynamic call ABIs and explicit completion lanes; retain stack/error handling |
| **D8.6.1–D8.6.3** | Respect the MIR size ratchet; assert finalized instruction shapes; verify liveness with forced-GC dynamic tests |

Retain `Item`, `String`, `Array`, `ArrayNum`, `Map`, `TypeMap`, `ShapeEntry`, `Function`/`JsFunction`, the shared heap, and precise rooting. Most doubles are already inline in Item. A new value ABI or collector is outside Tune12.

Do not import MVP's semantics: its numeric wrapper coerces `add1("2")` to `3`; a replaced `this.value()` method can be bypassed; its Uint8Array-like storage retains `257` instead of converting to `1`. These are negative tests for full-JS optimization admission, not shortcuts to port.

Do not modify benchmark sources/inputs, replace algorithms with recognized answers, weaken Test262, edit vendor code, or rewrite unrelated runtime modules. Use C++17 and existing `lib/` containers. Promote a required existing `static` helper rather than copying it. Scratch output belongs under `temp/`; accepted evidence belongs in the benchmark archive.

## 3. Evidence carried into Phase 1

### 3.1 Observations, not attribution estimates

| Finding | Evidence | Consequence |
|---|---|---|
| Full Map set/add performs a second linear search | `hashmap_set` → `js_collection_order_upsert`; deletion has another list search | Fix the representation link before tuning dispatch |
| Full numeric admission misses important functions | Fresh finalized MIR has no numeric variant for benchmark `sum`/`fib` | Repair analysis/admission and reuse the existing native entry |
| `sum` retains redundant loop work | Compare/add/subtract helpers, two TDZ call sites, root publication and possible scalar-result handling | Track initialization and native values through the loop |
| Packed MIR reads are narrowly admitted | `jm_is_array_literal_candidate` requires literal provenance | Add parameter/local admission with the same complete runtime storage guards |
| Generic helpers classify after preparing general machinery | `js_add` opens roots before Number classification | Add common heads while retaining the existing slow body |
| MVP's heap is not universally superior | Full JS wins fresh `binarytrees` and `gcbench` | Preserve storage/collector and protect these control rows |

The [fresh 12-row timing artifact](../../test/benchmark/js_mvp/JS_MVP_Full_Comparison_20260916.json) contains three samples per engine/row, all 108 runner statuses `ok`. Full/MVP ratios include `sum` 39.43x, `fib` 13.61x, `fft` 26.55x, `primes` 22.33x, `knucleotide` 15.33x and `regexredux` 49.12x. These are cross-backend observations, not isolated optimization A/B effects.

The [evidence artifact](../../test/benchmark/js_mvp/JS_MVP_Full_Comparison_Evidence_20260916.json) includes probe sources/results, MIR and Map scaling. With four constructions per sample, increasing map size from 1,000 to 8,000 entries raises full-JS time from 12.034 ms to 687.220 ms; MVP rises from 0.660 ms to 7.764 ms. The full-JS curve corroborates the algorithmic list-rescan diagnosis.

Historical full-population figures remain [Result45](../../test/benchmark/Overall_Result45.md) at 2.588162x full-JS/QuickJS and [MVP v1 acceptance](../../test/benchmark/js_mvp/JS_MVP_Release_Acceptance_20260915.md) at 0.691519x. Different sessions/binaries prevent causal subtraction. Neither the 12-row refresh nor the abandoned v2 experiment is the Tune12 control baseline.

### 3.2 Existing machinery to extend

| Area | Current implementation | Tune12 delta |
|---|---|---|
| Native JS entry guard | `jm_emit_exact_native_shape_test` and boxed-to-native selection already exist in `js_mir_function_class_lowering.cpp` | Improve eligibility and variant-local facts; reuse these guards |
| Numeric MIR | `MirNumericOpPlan` in `mir_emitter_shared.hpp` | Admit more proved operations/functions; do not duplicate the opcode table |
| Function facts | `FnAnalysis`, `FnVariantAnalysis`, parameter/binding/value facts in `ast-core.hpp` | Extend function-owned analysis only where an existing fact cannot represent the proof |
| Access lowering | `JsMirReference`, `jm_emit_packed_array_read`, shared element-address emission | Retain evaluation order and widen storage admission |
| Predicted fields | `MirConstructionPlan`, `MirFieldAccessPlan`, `mir_shape_candidate` | Broaden safe field carriers and ordinary-object helper hits |
| JS calls | `fn->invoke`, finalized body entries, `jm_call_direct_native`, direct boxed bodies, caller-rooted argument adapters | Select a smaller correct entry; do not add another dispatcher |
| Collections | `JsCollectionMap`, `JsCollectionData`, `JsCollectionEntry`, `JsCollectionOrderNode`, `HashMap` | Make hash entries identify stable ordered nodes |
| Diagnostics/tests | `JsOpt`, MIR `.mir-check` fixtures, MIR ratchet and GC stress suites | Add reason/shape assertions and adversarial misses |

The abandoned v2 migration changed many layers together. It does not justify avoiding Item in full JS. Tune12 isolates each proposed change so its benefit and cost can be measured independently.

## 4. Phase 1 work breakdown and dependencies

| Phase | Deliverable | Depends on | Initial state |
|---|---|---|---|
| T12-0 | Frozen release control, measurement protocol and structural census | — | Implemented |
| T12-1 | Hash-to-order-node Map/Set storage | T12-0 | Implemented |
| T12-2 | Numeric function admission/dataflow | T12-0; execute after T12-1 for clean attribution | Implemented |
| T12-3 | Definite initialization and minimal native frames | T12-2 | Implemented |
| T12-4 | Number and own-element helper heads | T12-0; measure separately from T12-2/3 | Implemented |
| T12-5 | Parameter/numeric/typed-array MIR access | T12-2, T12-4 | Implemented |
| T12-6 | Ordinary named-field hits | T12-0, shared ownership tests | Implemented |
| T12-7 | Proved callable entry selection | T12-2/3, T12-6 where property access participates | Verified existing entry contract |
| T12-8 | Admitted bulk RegExp operations | T12-7 admission/evaluation contract | Implemented |
| T12-9 | Consolidation, complete A/B, guarded final report | T12-1 through T12-8 | Closed |

Implement in the listed order, keeping each phase independently reviewable. A newly exposed correctness defect is fixed at its cause before accepting the affected optimization; record it separately so its cost is not mistaken for the intended tuning effect.

The checked Phase 1 work items below are closed by the release provenance, fixtures,
and artifacts in the completion record. The semantic fallback in each phase is
required by **S1.11**, **D5.3.2–D5.3.5**, and **D8.4.1v2–D8.4.3v2**; a checked
item is not permission to generalize an admitted physical path.

## 5. T12-0 — Freeze the control and make costs observable

**Purpose:** replace historical comparisons with a reproducible starting point and prove which paths the benchmarks take.

**Work:**

- [x] Verify the 63-workload manifest with `verify_js_mvp_manifest.py`; record the source/input/wrapper hashes and current dirty-tree/source-patch identity.
- [x] Complete correctness checks, then build release and archive its exact binary under `test/benchmark/exe/` using a name without `.exe`, so `make clean` does not remove the control. Record SHA-256, build configuration, compiler/platform and required module/package identity.
- [x] Produce a fresh guarded full-population full-JS/QuickJS control, plus typed/untyped Lambda measurements to protect shared emitter changes. Use the standard runner rather than an ad hoc subset as the control.
- [x] Reproduce the five semantic probes in the comparison evidence on full JS. Use their expected ECMAScript outputs for later regression fixtures; MVP is not the semantic oracle.
- [x] Capture finalized MIR for `sum`, `fib`, `fft`, `primes`, an array-parameter search loop and a predicted-field loop. Record native variants, static helper sites and why candidate admission failed.
- [x] Extend the existing opt trace/analysis reporting for relevant refusal reasons, without introducing a second profiler. Separate compile-time refusal counts from runtime hits/misses and elapsed-time samples.
- [x] Retain the Map-size diagnostic and add existing-key update and delete/reinsert workloads to distinguish hashing, order maintenance and iteration costs. These remain diagnostic microbenchmarks outside the 63-row metric.

**Diagnostic questions:** How many numeric functions fail on returned locals or recursive `+`? Which array operations stay generic because of parameter provenance, element carrier, descriptors or index type? Are named misses caused by shape transitions, value representation or host receivers? Which RegExp path actually executes on `regexredux`?

Diagnostic instrumentation is disabled in release timing runs and must not impose an additional unconditional hot-path call. Reuse the existing trace enablement. The control and candidate must have identical instrumentation settings.

**Exit:** control archive and hashes exist; all 63 rows have valid statuses and normalized output evidence; baseline failures and the previously observed `sigaltstack` cleanup diagnostics are investigated/documented rather than silently called a clean gate. The census identifies target and control rows for each subsequent phase.

## 6. T12-1 — Indexed ordered Map/Set entries

**Primary code:** [js_runtime.cpp](../../lambda/js/js_runtime.cpp): collection structs; `js_collection_hash`/compare; `js_collection_method`; order maintenance; iterators; GC tracing and weak cleanup.

### 6.1 Storage contract

Use the existing stable `JsCollectionOrderNode` as the authoritative key/value owner. Change the hash entry to identify that node. Prefer a node-pointer payload with hash/equality reading `node->key`; a stack-local probe node supplies the lookup key. If the current HashMap interface requires a separate key field, it is a non-owning reference to the node's stable key storage, never a second mutable value authority.

No new collection heap type or generic container framework is required. Keep `JsCollectionMap`'s ordinary Map base and internal `JsCollectionData`. Keep the existing SameValueZero policy, including NaN and signed-zero normalization.

Nodes remain address-stable across hash-table growth. Key/value scalars must have a destination-owned lifetime: audit the current Item copies, use the existing owned-slot/scalar-storage facilities, and extend the existing node with home storage only where required. Trace the node's live Item slots, not stale independent hash copies.

### 6.2 Algorithms

- [x] Existing-key set/add: hash lookup, mutate the identified live node, return. Do not rehash/reinsert merely to update its value unless required by the existing API.
- [x] New-key insertion: root incoming values, allocate/initialize a stable node, insert into the hash table, then publish it to the order chain once insertion succeeds. Handle allocation/insertion failure without leaving a ghost ordered entry.
- [x] Get/has: one hash lookup; get reads the node's authoritative value.
- [x] Delete: retrieve the identified node, remove the hash entry and mark the node deleted; no key search through the order chain.
- [x] Clear: clear the hash index and mark live order nodes deleted, releasing their key/value ownership. Linear clear is acceptable; repeated set/delete must not inherit a list walk.
- [x] Weak cleanup: use the same indexed removal operation from the ephemeron callback, respecting the collector's permitted allocation/cleanup behavior.

Retain the traversal chain through deleted nodes until safe reclamation. A simple initial implementation keeps an append-only chain with tombstones and stable `next` links until collection teardown; existing iterators skip deleted nodes. An update preserves position; delete followed by reinsert creates a new tail node. Clear must not sever an active iterator's route to subsequently added entries. Once an iterator reports done, it remains done.

This reuses existing nodes and deleted flags. Do not introduce compaction or iterator generation tracking merely to optimize this first change. Record tombstone/native-memory retention under churn; clear deleted key/value references promptly. If retention becomes a measured regression, address it as a separately specified lifecycle change rather than adding an unmeasured reclamation subsystem.

### 6.3 Tests and exit

- [x] Extend collection behavior/GC coverage around `test/js/collections_advanced.js` and `collection_gc_retention.js`; add focused fixtures with expected `.txt` output for missing cases.
- [x] Cover NaN, ±0, equal-content distinct strings, object identity, update-without-reorder, delete/reinsert, clear/repopulate, and independent collections.
- [x] Exercise active iterators and `forEach` while deleting the current/next/tail entry, appending entries, clearing, and reinserting. Test iteration after hash growth and the permanently exhausted iterator state.
- [x] Force GC with object keys/values, string keys, out-of-band numeric scalar homes, weak keys and values reachable only through ephemeron semantics.
- [x] Add a diagnostic comparison counter or direct structural assertion showing no insertion-order key comparisons on set/add/delete. Keep it out of timed code.

**Performance acceptance:** the 1K/2K/4K/8K construction curve must stop exhibiting quadratic growth; the final two doubling ratios should be materially closer to 2 than 4. Use operation counts to distinguish allocator noise from an algorithmic failure. `knucleotide` must show a reproducible improvement; benchmark other actual collection consumers identified in T12-0. Track peak native/GC memory and churn retention. Do not promise a specific fraction of the 15.33x MVP gap before this A/B exists.

## 7. T12-2 — Admit complete numeric functions safely

**Primary code:** [js_mir_function_collection_class_inference.cpp](../../lambda/js/js_mir_function_collection_class_inference.cpp), [js_mir_module_batch_lowering.cpp](../../lambda/js/js_mir_module_batch_lowering.cpp), [js_mir_function_class_lowering.cpp](../../lambda/js/js_mir_function_class_lowering.cpp), [js_mir_calls_boxing_types.cpp](../../lambda/js/js_mir_calls_boxing_types.cpp), [ast-core.hpp](../../lambda/runtime/ast-core.hpp).

### 7.1 Analysis and bounded specialization

- [x] Replace the returned-local blind spot with binding-identity dataflow. Track Number facts through initializers, assignments, branches and loop joins; an incompatible write widens the fact rather than silently retaining a numeric carrier.
- [x] Keep candidate entry shapes separate from the generic parameter contract using existing function/variant facts. Do not make a generic parameter Number merely because its body uses arithmetic.
- [x] Resolve numeric result facts through direct calls and recursive components. Use a bounded monotone fixed point with a diagnostic refusal when the bound is exhausted; do not add source-name special cases for `fib` or `sum`.
- [x] For recursive candidates, verify the complete body under the assumed guarded entry shape, including every return and reachable edge. A circular assumption alone is not a proof. Fallthrough/undefined and mixed returns remain boxed until explicitly represented.
- [x] Begin with one Number-native variant per eligible simple noncapturing function, alongside the existing complete boxed body. No combinatorial parameter-shape expansion or feedback-driven variants.
- [x] Initially exclude defaults/rest/destructuring, `arguments`, direct eval, with, generators/async, mutable captured state and other bodies whose effects are not modeled. Existing supported native cases must remain correct and must not be silently disabled.

Use the existing indexed AST/binding analysis and `FnVariantAnalysis` binding/value records. If extra dataflow scratch is necessary, make it function-owned and transient; do not establish another persistent per-node semantic-fact authority.

### 7.2 Entry and body lowering

- [x] Reuse `jm_emit_exact_native_shape_test` and the existing boxed wrapper's native guard; audit that it recognizes all admitted JS Number carriers while excluding Symbols and BigInts. Keep safe unsupported carriers on the boxed path.
- [x] A guard must inspect representation without calling `ToNumber`, `valueOf`, getters or user code. Missing/mismatched arguments enter the boxed body with their original values.
- [x] Preserve evaluation of extra actual arguments even when the numeric body ignores them. Do not duplicate default/argument effects between entries.
- [x] Emit F64 locals and results through `MirValue`, existing native ABI helpers and `MirNumericOpPlan`. Box only at an Item consumer/return boundary.
- [x] Direct numeric calls require both admitted actual shapes and a stable selected callee. A recursive reference to a rebound outer function must still see the new binding. If stability cannot be proved or guarded before effects, use the existing call path.
- [x] Preserve stack-limit checks and explicit error companions. Do not replace a recursive overflow with an unchecked native stack failure.

### 7.3 Tests and exit

Add behavior and `test/mir/js/*.js` + `.mir-check` fixtures for a returned accumulator, local aliases, loop joins, self recursion, a recursive numeric component, mixed returns and a body that changes a parameter's type. Reuse `JsOpt` fixtures for emitted-path/refusal checks.

Call the same function with Number, string, boolean, null, undefined, BigInt, Symbol and a coercing object. Cover throwing/side-effecting `Symbol.toPrimitive`, omitted/extra arguments, NaN, infinities, ±0, fractional values and out-of-band doubles. Include aliasing/rebinding of a recursive function. Assert exact outputs/errors, not only aggregate checksums.

**Structural exit:** benchmark-equivalent `sum` and `fib` produce native bodies; the native `sum` loop contains F64 compare/add/subtract, and native recursive edges retain native operands/results. Generic mixed calls still execute the boxed implementation. Preserve full-JS direct boxed calls already present; do not attribute their existence to Tune12.

**Performance targets:** `sum`, `sumfp`, `fib`, `fibfp`, `mbrot`, `diviter` and any additional admitted rows from the census. Measure compile time and MIR growth because every extra body has a cost. A blanket return-inference relaxation without semantic guards does not pass this phase.

## 8. T12-3 — Definite initialization and minimal native ownership work

**Primary code:** existing function/binding analysis, [js_mir_expression_lowering.cpp](../../lambda/js/js_mir_expression_lowering.cpp), [js_mir_statement_lowering.cpp](../../lambda/js/js_mir_statement_lowering.cpp), [mir_emitter_shared.hpp](../../lambda/runtime/mir_emitter_shared.hpp), function frame/call lowering.

- [x] Track definite initialization independently of Number type. A read or assignment may omit its TDZ check only if initialization dominates every incoming control-flow path.
- [x] Model loop backedges, zero-iteration paths, shadowing and branch joins; exclude or invalidate facts across unmodeled eval/with/capture effects. Preserve const-assignment errors separately from TDZ.
- [x] Derive live root slots from the selected variant's actual references and safepoints. Do not publish root homes for F64 values merely because the generic body uses Items.
- [x] Retain ownership for boxed arguments, callee/context state and values live across allocating error paths. A mostly numeric function can still require roots on its slow/exception edge.
- [x] Use known result representation to remove irrelevant pending/scalar-home handling within native call chains; retain the existing companion and adoption protocol at the real boxed boundary.
- [x] Keep canonical safepoint slots and native stack checks. Do not remove a root frame or change a helper effect declaration merely because a benchmark did not collect.

**Tests:** initialized versus conditionally initialized locals, use-before-declaration, captured let/const, shadowed names, assignment before initialization, const writes, try/finally and throwing paths where supported, recursive overflow, and forced-GC runs byte-matching normal runs.

**Structural exit:** the initialized `sum` loop no longer contains `js_check_tdz`; its numeric values do not acquire Item root homes or transient-number storage between arithmetic operations. Fixtures scope assertions to the native body, so legitimate wrapper/slow-path checks remain allowed. **D8.6.2** forbids fragile assertions on register numbers, absolute pointers or raw immediates.

Measure this separately against the T12-2 candidate so the effects of better admission and smaller frames/checks are distinguishable.

## 9. T12-4 — Small common heads on existing helpers

**Primary code:** [js_runtime_value.cpp](../../lambda/js/js_runtime_value.cpp), own-element helpers in [js_runtime.cpp](../../lambda/js/js_runtime.cpp), existing GC-effect/import declarations.

### 9.1 Number operations

- [x] Put a noncoercing Number-pair case before general root setup in `js_add` and the corresponding shared arithmetic/comparison operation entry points where profitable.
- [x] Reuse one representation classifier/extractor and the existing numeric operation implementation. The third similar operator must use a shared shape/table, not another copied tag-switch body.
- [x] Preserve the normal Item result encoder, signed zero, NaN, infinity, division/remainder and numeric precision. Power/bitwise/equality cases enter only when their JS-specific policy is explicitly covered.
- [x] On a miss, run the existing full semantic body exactly once, preserving ToPrimitive/ToNumeric order, string concatenation, BigInt behavior and errors.
- [x] Keep the complete helper's `MAY_GC`/throwing declaration when its fallback can allocate/throw. Only an independently valid physical leaf may have a narrower effect contract; verify changes with the existing GC-effects checker.

### 9.2 Own elements

- [x] Add/streamline the present-own-element case before root preparation/general property dispatch when no GC/user code can occur on that hit.
- [x] Separate read facts from write policy and existing-slot overwrite from add/grow/hole creation.
- [x] Existing writable own elements do not need extensibility or a prototype-absence proof. Retain descriptor/writability, actual ownership and storage checks; out-of-bounds additions to a nonextensible array still miss/fail correctly.
- [x] Share the proof definition with MIR access planning. Do not duplicate a general getter/setter algorithm under a `fast` name.

**Tests:** mixed Number/non-Number operations, coercion-order logs, Symbol/BigInt errors, numeric versus string keys, holes/inherited accessors, frozen/sealed/nonextensible arrays and scalar-home elements. Keep the existing `NonExtensibleArrayFallsBack` test for element creation; add separate coverage for legal existing-element overwrite rather than weakening that test.

**Exit:** instrumentation demonstrates the intended head is reached before slow setup, and guard misses produce unchanged results. Measure a generic/mixed-call workload where MIR cannot eliminate the helper. A helper-only gain does not justify claiming that caller root stores or helper calls disappeared; those are T12-2/3/5 concerns.

## 10. T12-5 — Native array access beyond literal receivers

**Primary code:** `JsMirReference` and `jm_emit_packed_array_read` in [js_mir_expression_lowering.cpp](../../lambda/js/js_mir_expression_lowering.cpp), existing shared emitter address/load/store primitives, [js_typed_array.cpp](../../lambda/js/js_typed_array.cpp), array/storage ownership helpers.

### 10.1 T12-5a: parameter and local arrays

- [x] Allow a repeated access through a parameter/local to select the existing guarded packed-array plan even without literal provenance. Static candidacy selects a guard; it does not prove the receiver type.
- [x] Carry the evaluated receiver and native key through `JsMirReference`. Preserve base/key evaluation and coercion order; a miss uses those same evaluated values.
- [x] First admit ordinary packed tagged arrays with valid index, length/capacity, backing storage, no overriding indexed descriptors/host behavior and ownership-safe payload. Reuse existing element-state flags to exclude holes or explicitly test the slot when admitting a holey representation.
- [x] Add an ArrayNum physical arm using its established representation and override checks. Keep the loaded F64 native when its consumer admits Number.
- [x] Add existing-own-element stores through the shared storage primitive. Growth, sparse storage, incompatible values, scalar-tail transitions, COW/write preparation and descriptors remain on the semantic path until separately proved.

Use at most a small fixed set of static storage arms: tagged array and numeric array initially. A miss has one existing semantic continuation. Do not build a per-site observed-type table.

### 10.2 T12-5b: actual fixed typed arrays

- [x] Start with Uint8Array and Float64Array, reusing real `JsTypedArray`/buffer metadata; do not route them through ordinary arrays as MVP does.
- [x] Guard brand/element kind, attached fixed buffer, view bounds and valid numeric index. Initially reject shared/resizable/growable or otherwise unsupported buffers to the existing typed-array helper.
- [x] For reads, issue the appropriate native load and extend/convert its representation. For stores, preserve Uint8 conversion and Float64 Number policy. Start with already-proved Number values; coercing values take the full helper before any stale data pointer is reused.
- [x] Reject fractional/nonfinite/out-of-range keys without truncating them into a valid element. Preserve numeric `-0` versus canonical string `"-0"` behavior through the reference/key contract.
- [x] Cover side effects in receiver/key/RHS evaluation: a key or value conversion can detach or change the buffer. Check storage facts after any such effect at the point required by the semantic operation.

### 10.3 T12-5c: guard placement and effects

Per-access guards are the first correct version. Hoist them only after proving an entire region cannot call user code, collect in a way that invalidates the borrow, mutate descriptors/prototypes, grow storage, detach or replace the buffer. A store that might grow or change representation ends the region. An alias can invalidate a fact even if the receiver's local binding is unchanged.

Do not cache raw element pointers across a `MAY_GC` call. For broader loops, reacquire storage after effects or retain per-access guards. Region specialization must not replay prior side effects on a guard failure; no mid-loop restart/deoptimization is introduced.

**Reuse boundary:** use `MirValue`, existing container guards, address calculation and owned stores. If a compact dense-access plan descriptor is needed to converge Lambda and JS consumers, place only physical layout/key/carrier facts in the shared emitter; JS descriptor/prototype/typed-buffer admission stays in JS. Do not add a second JS array representation.

### 10.4 Tests and exit

- [x] Add parameter-array and typed-array MIR fixtures that assert direct native accesses on hits and one semantic fallback arm.
- [x] Cover holes, deleted slots, inherited numeric getters, own accessors, prototypes changed between calls, host/virtual arrays, frozen and nonextensible arrays, negative/fractional/NaN/infinite indices, and keys with observable conversion.
- [x] Cover Uint8 wraparound (`257 → 1`, negatives, fractions), Float64 ±0/NaN/subnormal values, wrong typed-array kinds, detach/resize during RHS/key effects, and buffer/view offsets.
- [x] Force GC with a previously loaded scalar/reference live across the next call, and exercise representation changes from numeric to tagged elements.

**Targets:** `fft`, `sieve`, `primes`, `navier_stokes`, `spectralnorm`, `matmul`, `array1`, and `text_search`. The last searches arrays of character codes passed as parameters; string-search leaf tuning is not a substitute. Show fewer dynamic index helpers on the admitted loops, then a paired release benefit. Protect control rows even when the new guards miss repeatedly.

## 11. T12-6 — Ordinary own-field access

**Primary code:** `js_named_fast_receiver_map`, `js_named_fast_lookup`, `js_get_name_id`/`js_set_name_id` in [js_runtime.cpp](../../lambda/js/js_runtime.cpp); predicted-field lowering in [js_mir_expression_lowering.cpp](../../lambda/js/js_mir_expression_lowering.cpp); [mir_shape_candidates.hpp](../../lambda/runtime/mir_shape_candidates.hpp).

- [x] Measure actual named-field hit/miss reasons before extending shape prediction. Reuse NameId/TypeMap lookup already present; do not optimize a nonexistent per-hit key-string construction.
- [x] Factor the ordinary own default-data-slot proof so C helpers and MIR agree on receiver kind, shape, descriptor/deletion state, byte offset and value carrier.
- [x] Make the ordinary non-host hit avoid host-dynamic preparation and global/DOM synchronization hooks. Prove the receiver is outside those categories; globals, DOM wrappers and host objects retain their current hooks.
- [x] Extend `MirFieldAccessPlan` stores beyond the current float-only admission using existing owned-field writers for supported carriers. An incompatible shape/value or ownership transition takes the current NameId kernel.
- [x] Keep source-predicted shapes immutable and tied to their layout owner. Do not retain a runtime receiver or context-specific object at a code-baked address.

**Tests:** own versus inherited fields, descriptor/accessor changes, deletion/re-add, field-type changes, prototype changes, same-named fields on unrelated shapes, global-binding synchronization, DOM event-handler properties, setters/proxies and retained scalar/reference values under GC.

**Targets:** measured object-heavy rows such as `microdiff`, AWFY class workloads and `raytrace3d`, as confirmed by the census. `binarytrees`/`gcbench` are required controls. The phase passes only if hit paths are measurably simpler without breaking host/global semantics; replacing shaped objects with MVP hash objects is outside scope.

## 12. T12-7 — Select the smallest correct callable entry

**Primary code:** function/call analysis and direct lowering, `js_call_kernel`/`js_function_select_body_entry` in [js_runtime.cpp](../../lambda/js/js_runtime.cpp), [js_runtime_function.cpp](../../lambda/js/js_runtime_function.cpp), existing builtin/callable metadata.

- [x] Inventory activation requirements on current function-owned facts: receiver binding, arguments, new.target, module/realm switch, with-chain, private/super state, generator/async and source/error state.
- [x] Reuse existing native and direct boxed entries for admitted callees. Extend finalization/selection only for a demonstrated missing case; do not copy the general call kernel.
- [x] Reuse caller-rooted actual spans and existing result ownership. Eliminate repeated argument-root preparation only when the caller has fulfilled that same contract.
- [x] For a dynamic/method call, perform Get once, evaluate arguments once, preserve the selected callee/receiver, then guard actual callable capability/identity and any remaining receiver facts. An argument that changes the property's value does not retroactively change the callee already selected.
- [x] A miss calls that selected value through the existing dynamic entry. It must not repeat Get, select by method name, or reread a modified property.
- [x] Resolve reusable metadata at function creation/finalization. No per-site mutable callee cache, prototype epoch cache or feedback vector is added.

**Tests:** a getter returning alternating functions, a getter that throws, an argument replacing the method, replaced `this.method`, overridden built-ins, bound functions/proxies, strict/sloppy/arrow this, extra actuals, arguments, eval/with capture, cross-module/realm functions, new.target/derived constructors and error unwinding. Unsupported activation shapes keep the full path.

**Exit:** an admitted simple call avoids the identified redundant activation work while all observable Get/Call ordering remains intact. Compare against the current direct-call path, not against an assumed always-dynamic baseline. Numeric recursion gains belong to T12-2/3 unless this phase shows an additional independent reduction.

## 13. T12-8 — Built-in bulk RegExp execution

**Primary code:** `js_regexp_symbol_match`, `js_regexp_symbol_replace`, `js_regexp_exec_dispatch`, existing string match/replace loops and regex internal metadata in [js_runtime.cpp](../../lambda/js/js_runtime.cpp); current matcher/router interfaces.

### 13.1 Admission

Start with primitive string input, a genuine ordinary RegExp, the actual selected built-in operation and builtin exec, supported immutable pattern/flag facts, ordinary relevant descriptors, and a writable lastIndex. For replace, initially require a primitive replacement string and no user callback. Existing slow cases, including overridden accessors/methods, remain on the current protocol.

Place admission before the first operation whose observable effects would be skipped or repeated. Prove known builtin accessors through existing shape/callable metadata rather than invoking arbitrary getters as a test. If a later miss is possible after an observable prefix, use the existing continuation with already evaluated values; never restart the builtin and replay its effects. Prefer a narrower pre-effect guard over a new continuation framework.

### 13.2 Execution

- [x] Reuse/factor the current bulk matching/replacement loops and regex router; both backends already use RE2 where admitted. Do not edit RE2 or replace the full ECMAScript compatibility path with it.
- [x] Eliminate repeated JS exec dispatch and temporary exec-result objects only when the admitted operation does not expose those intermediates.
- [x] Preserve final result shape, null/no-match behavior, lastIndex reads/writes/final state, empty-match progress, UTF-16 positions, capture/substitution semantics and legacy match state.
- [x] Keep required output allocation precisely rooted; allocation is still `MAY_GC`. Do not hold invalidated string/array borrows across it.
- [x] Expand captures, Unicode flags and replacement substitutions only after the basic path passes. Unsupported cases stay explicit fallbacks; no benchmark-pattern whitelist.

**Tests:** overridden `Symbol.match`, `Symbol.replace`, exec/global/unicode/flags accessors, nonwritable lastIndex, empty matches at boundaries, non-BMP/lone-surrogate strings, captures, `$` substitutions, throwing replacements, custom exec results, repeated calls and legacy match-state observations. Use existing regex router/GC tests plus focused new outputs.

**Exit:** counters show fewer per-match dynamic calls/temporary arrays for admitted inputs; `regexredux` improves in isolated A/B. Separate matcher routing, protocol and allocation measurements so a single 49x MVP gap is not attributed entirely to one mechanism.

## 14. Data-structure and code ownership budget

| Existing carrier | Allowed extension | Ownership rule |
|---|---|---|
| `JsCollectionEntry` / `JsCollectionOrderNode` | Hash-to-node link; destination-owned scalar storage if required | One authoritative node value, stable iterator address, precise strong/weak tracing |
| `FnAnalysis` / `FnVariantAnalysis` | Candidate numeric/return/effect facts and diagnostic refusal information | Definition/function owned; no mutable runtime feedback |
| `FnBindingAnalysis` / current flow scratch | Initialization and native-value facts | Binding-identity based, conservative joins; no duplicate global fact registry |
| `JsMirReference` | Additional proved native-key/access facts if not already present | Own evaluated base/key; never duplicate their effects |
| `MirNumericOpPlan`, `MirFieldAccessPlan`, shared emitter primitives | Additional physical cases/fields justified by live consumers | No JS coercion, property or truthiness policy in shared physical operations |
| `JsFunction` / existing finalized entries | A capability/entry fact only if current function analysis cannot already select it | Published once, tied to actual callable state; no callsite cache |

No new JS heap object kind is planned. No private fast array/object/Number/closure/collector is planned. Record every added struct/field and its lifetime in the phase closeout. Consolidate only after two live consumers share the same operation shape; avoid moving unrelated code merely to change file size. Report production code and test/diagnostic LOC separately without compressing formatting to manufacture reductions.

## 15. Validation and measurement protocol

### 15.1 Correctness and emission

For each phase, run its focused semantic/MIR tests before broad gates. At a coherent runtime checkpoint run `make test-lambda-baseline` and `make test262-baseline`. Use the current expected populations rather than copying old pass counts. Retain logs for regressions, crashes and timeouts, including isolated retries where the established harness permits them.

Relevant existing targets/executables include:

```sh
make build-test
./test/test_js_opt_gtest.exe
./test/test_js_mir_emission_gtest.exe
./test/test_mir_ratchet_gtest.exe
python3 utils/check_gc_effects.py
make test-mir-gc-stress
make test-gc-rooting-core
make test-lambda-baseline
make test262-baseline
```

Run only relevant focused suites while iterating; complete the required baseline gates at phase integration. Memory/ownership changes additionally require `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1` on the focused regression cases, compared with their normal outputs. MIR claims use `JS_EXECUTION_BACKEND=mir`; shared Lambda JIT claims use `LAMBDA_TIER=jit`. Helper changes also need AST-backend behavior coverage because those helpers are shared by both JS execution backends.

All new integration `.js` fixtures receive expected `.txt` output; all MIR fixtures receive `.mir-check` sidecars. Use existing discovery/registration conventions. New C++ tests follow the project's C+ rules even when older tests contain `std::` usage.

The MIR size ratchet remains mandatory. A justified extra native body/guard requires an explicit reviewed budget delta in the same change under **D8.6.1**; do not globally loosen budgets or assert raw pointer/register values. Count hot-body and total-module MIR separately.

### 15.2 Release A/B

Correctness targets can rebuild the executable. Therefore build/rebuild release after them and verify the exact binary being timed. Archive the control before any clean/rebuild; archive each candidate before measuring. Include source patches and required module/package identities, not just a base commit hash.

Use `run_paired_benchmarks.py --language js` for same-source full-JS A/B. It already supports canonical workload wrappers, source hashes, stable-output comparison and paired bootstrap statistics. Do not pass the MVP manifest directly as its `--manifest` unless its separate schema has been explicitly translated/verified.

Example for a numeric phase, with the two paths set to the actual archived release binaries:

```sh
JS_EXECUTION_BACKEND=mir python3 test/benchmark/run_paired_benchmarks.py \
  --language js \
  --control "$TUNE12_CONTROL" --candidate "$TUNE12_CANDIDATE" \
  --suite r7rs --bench fib,sum,mbrot --pairs 7 --timeout 180 \
  --output test/benchmark/js_mvp/tune12/numeric_paired.json
```

`TUNE12_CONTROL`/`TUNE12_CANDIDATE` are task-specific path variables set during T12-0 and each phase, not existing repository defaults. Use separate filenames per phase/revision; do not overwrite evidence or merge unmatched cells.

For each phase report candidate/control ratios on its targets, misses and neutral controls. Investigate a reproducible regression above 3% on a stable row; this is a review trigger, not permission to hide smaller systematic regressions. Retain confidence intervals and raw samples. For very short rows, use a diagnostic repetition harness without changing canonical sources, and keep that result outside the full-population metric. Increase sample count only to resolve an actual uncertainty.

A claimed win needs the intended structural change plus a paired interval supporting improvement. Code-path counts without elapsed-time benefit are not a tuning success. A changed output invalidates that row's timing comparison.

### 15.3 Whole-population result

The canonical metric is:

```text
G(full JS / QuickJS) = exp(sum(log(full_js_ms[i] / quickjs_ms[i])) / 63)
```

Run all 63 rows with at least three samples per engine, reporting every row/status. A failure or timeout prevents declaring a complete valid geometric mean; do not silently compute an acceptance score over survivors. Use both equal-log-weight geometric mean and total-median milliseconds: `text_search` can dominate elapsed time without dominating the geometric mean.

The final guarded snapshot uses the standard runner's build/profile/power/Test262/archive workflow. A representative invocation is:

```sh
JS_EXECUTION_BACKEND=mir python3 test/benchmark/run_standard_benchmarks.py \
  --engines mir,lambdajs,quickjs \
  --suite r7rs,awfy,beng,kostya,larceny,jetstream,text \
  --typed --runs 3 --timeout 180 \
  --results-output test/benchmark/js_mvp/tune12/final.json \
  --report-output test/benchmark/js_mvp/tune12/final.md \
  --report-title "JS Tune12 full-runtime results" \
  --log-dir test/benchmark/js_mvp/tune12/final_logs
```

Create the artifact directory first. T12-0 uses distinct control paths. Keep default guard checks enabled; a diagnostic run that bypasses a guard is labeled diagnostic and cannot serve as acceptance. A named ResultN publication must use the complete configured ResultN workflow/population rather than silently presenting this selected engine set as all engines. No Node-specific baseline is added by this plan.

Also run full-population interleaved control/candidate comparisons for causal attribution. Confirm the final snapshot's archived binary matches the final candidate byte-for-byte; if the workflow rebuild changes it, archive and measure the actual final binary. Historical MVP values remain contextual; a new full/MVP claim needs a fresh complete comparison with recorded source/backend identity.

## 16. T12-9 — Consolidation and closeout

- [x] Review all new fast paths for one complete semantic miss and no repeated operand/property/argument effects.
- [x] Remove superseded scans, duplicated classifiers and temporary tuning switches. Retain useful diagnostics under the existing opt trace and preserve the generic semantic implementation.
- [x] Complete focused, GC, MIR-ratchet, Lambda and Test262 gates on the final source. Rebuild matching external modules if a shared runtime layout change affects their ABI.
- [x] Archive final release binary/source provenance and complete 63-row paired/full-suite results. Verify counts, statuses, hashes and output equivalence mechanically.
- [x] Report full-JS/QuickJS geometric mean, final/control geometric mean, total medians, each suite, each row, regressions, timeouts, memory, compiler time and MIR-size changes.
- [x] Separate gains attributable to Map storage, native admission, TDZ/root reduction, helper heads, arrays, fields/calls and RegExp using the retained phase archives. Do not sum overlapping percentage gains.
- [x] Confirm typed/untyped Lambda has no unresolved regression from shared analysis/emitter changes. Keep full-JS control wins such as `binarytrees`, `gcbench`, `array1`, `collatz`, `deriv`, `pidigits` and `pnpoly` visible.
- [x] Update this plan's checklist, the MVP comparison's follow-up status, and implementation documentation with actual behavior. Do not rewrite historical snapshots to match the final implementation.
- [x] Record each performance milestone as reached or unmet, with the remaining dominant paths. A successful subset, a green baseline, or a large individual speedup does not close an unmet overall target.

### Phase 1 completion record

| Phase | Source revision / patch | Structural evidence | Correctness logs | Exact release A/B | Status / residual |
|---|---|---|---|---|---|
| T12-0 | control `fd998759`; manifest `e585c119…` | 63-workload census; `JsOpt` trace | Test262 40,261/40,261 | `control.json`, `final.json`, `final_memory.json` | Closed; frozen control 5.082332x; RSS unavailable for JetStream wrappers |
| T12-1 | final runtime patch `1864e219…` | node-backed hash index; no order rescan | collection fixture + forced GC | `t12_1_collections_paired.json`; diagnostic 0.3333x | Closed; tombstones intentionally retained |
| T12-2 | final runtime patch `1864e219…` | binding-identity fixed point; `_n` MIR bodies | numeric JS/MIR fixtures | full A/B: fib 0.1262x, sum 0.0249x | Closed |
| T12-3 | final runtime patch `1864e219…` | native frame fixture excludes TDZ/root homes | TDZ fixture + 188 forced-GC corpus | structurally measured with T12-2 | Closed |
| T12-4 | final runtime patch `1864e219…` | Number/own-dense trace hit and miss counters | JS optimization contracts | included in final A/B | Closed |
| T12-5 | final runtime patch `1864e219…` | tagged, ArrayNum, Uint8, Float64 arms plus one fallback | array JS/MIR fixtures + forced GC | included in final A/B | Closed |
| T12-6 | final runtime patch `1864e219…` | predicted float/string carriers and host/global miss | field fixture | included in final A/B | Closed |
| T12-7 | existing finalized entry contract | `invoke` direct/native/boxed selection | callable fixture; dynamic-call MIR contract | no separate dispatcher introduced | Verified; closed |
| T12-8 | final runtime patch `1864e219…` | builtin bulk counters with protocol fallback | RegExp fixture + AST contract | full A/B: regexredux 0.3018x | Closed |
| T12-9 | final archive SHA `02593fe…` | all 63 paired outputs equal | broad gates; baseline residues reproduced at control | `final_closeout.md`, 0.680706x A/B geo | Implementation closed; 0.80x milestone open |

### Closeout evidence

The [final closeout](../../test/benchmark/js_mvp/tune12/final_closeout.md)
links the guarded frozen [control matrix](../../test/benchmark/js_mvp/tune12/control.json),
complete per-row [candidate matrix](../../test/benchmark/js_mvp/tune12/final.json),
[interleaved A/B](../../test/benchmark/js_mvp/tune12/final_paired.json), and
[RSS snapshot](../../test/benchmark/js_mvp/tune12/final_memory.json). The
control independently passes Test262 and has all 63 LambdaJS/QuickJS rows
valid at 5.082332x; the final full matrix is 3.481106x. All 63 paired rows are
valid with equal output and their candidate/control geometric mean is
**0.680706x**, so the proposed 0.80x milestone is explicitly **unmet**.

The final source retains known, output-equivalent A/B regressions above 3% and
the independently reproduced broad-baseline failures. They are recorded as
follow-up performance/maintenance work rather than hidden by a changed
benchmark, a weaker ratchet, or a semantic shortcut (**S1.11**, **D8.6.1**).

## 17. Phase 2 — Close the measured hot-path coverage gaps

**Planning date:** 2026-09-16. **Closeout date:** 2026-09-16. **State:**
implemented; the Phase 2 causal target improved, but the 0.80x QuickJS
milestone remains unmet. This is an extension of the existing implementation
plan, not a new semantic or dispatch ruling.
All constraints in §2 and validation rules in §15 remain in force. In
particular, **S1.11**, **D2.4.1–D2.4.3**, **D3.3.2v2**, **D5.3.2–D5.3.5**, and
**D8.4.1v2–D8.4.3v2** govern the work below.

### 17.1 What Phase 1 achieved, and what it did not

The accepted interleaved comparison is 0.680706x candidate/control in geometric
mean, but 194,782.414 / 212,889.908 ms in total medians: 31.9% less geometric-mean
time versus only 8.5% less total time. Numeric admission produced approximately
40x wins on `sum`/`sumfp`, 8–11x on `fib`/`fibfp`, and the complete change improved
`knucleotide` about 7.1x and `regexredux` about 3.3x. These overlapping effects
must not be summed or attributed to an individual phase without its own A/B.

The [Phase 1 final matrix](../../test/benchmark/js_mvp/tune12/final.json)
identifies the elapsed-time priorities:

| Workload | Full-JS execution (ms) | Full JS / QuickJS | Share of full-JS total |
|---|---:|---:|---:|
| `text_search` | 96,100.986 | 6.52x | 49.37% |
| `havlak` | 19,269.777 | 10.35x | 9.90% |
| `log_pipeline` | 15,138.452 | 3.28x | 7.78% |
| `three_way_merge` | 14,606.329 | 4.97x | 7.50% |

These four rows account for about 74.5% of elapsed execution time. Halving
`text_search` alone would reduce the total by about 24.7%; this illustrates
priority, not an expected optimization result. Separately track large ratio
outliers such as `primes`, FFT and `microdiff`: total milliseconds and the
equal-log-weight score answer different questions.

The post-closeout diagnostic used the exact Phase 1 release
`lambda-tune12-final-1864e219659a`, SHA-256
`02593fe504c5030a04e536e2afe738da62cb9c9d9253b258e8a23db55e9201c6`.
Its three-sample, 11-row refresh measured full/MVP ratios of 1.003x for `sum`,
23.237x for FFT, 19.758x for `primes`, 10.261x for `microdiff`, and 16.884x for
`regexredux`. This is a diagnostic subset, not a new acceptance matrix or a
full-population MVP claim. Historical QuickJS ratios cannot be subtracted:
MVP acceptance used QuickJS 2025-09-13, whereas Phase 1 used 2026-06-04.

### 17.2 Diagnosed costs and limits of the evidence

| Observation | Root cause or remaining question | Phase 2 response |
|---|---|---|
| `text_search` searches arrays of character codes; repeated `.length` reads reach `js_get_name_id` → `js_get_reference` → `js_get_key_core` | Host/window checks precede ordinary array handling and can repeat on fallback | Ordinary receiver admission and direct length access, then native numeric-array consumers |
| Its five-second sample has about 23% self-samples in global/window helpers, 16% in the three generic read entries, and 13% in Number extraction/boxing helpers | Several layers of dispatch and representation work remain per loop iteration | Remove demonstrated work at the corresponding layer; do not treat percentages as independent guaranteed speedups |
| Havlak's five-second capture attributes about 17% inclusive active-worker samples to `js_intrinsic_note_property_mutation` | Generic writes scan intrinsic-prototype slots even for unrelated ordinary receivers | Receiver-owned relevance metadata or an existing rooted identity facility, not a per-site cache |
| FFT's `four1` has no native numeric variant; its MIR body contains 155 static call sites | Numeric-local planning remains coupled to a Number-return variant; its typed-array parameter also misses typed-read candidacy | Separate local representation planning from function return ABI and admit guarded parameter receivers |
| `primes` has guarded typed reads but generic typed stores and no native sieve variant | Narrow entry inference, repeated metadata helpers and incomplete load/arithmetic/store specialization | Extend guarded entry evidence and implement an end-to-end native typed-array loop |
| Phase 1 median RSS is 45.9 MiB versus QuickJS's 2.22 MiB | RSS alone cannot distinguish live data, retained compiler/runtime state, native allocation or collection cost | Separate allocation/retention and compiler profiles before choosing a fix |

The CPU samples exclude the sleeping main thread from their denominators.
Havlak's capture includes startup; neither capture is a whole-run profile.
Static MIR call-site counts are not execution counts. Do not infer that GC,
RE2 or MIR machine-code generation is the dominant runtime cost from these
observations. Havlak and JetStream `hash-map` use custom JS collections; their
remaining costs are not evidence that the built-in Map order-node fix failed.

Diagnostic files currently live under `temp/tune12_analysis/`:
`focused_current.json`, `text_search.sample`, `havlak.sample`, and
`text_search.mir`/`fft.mir`/`primes.mir`. They are local scratch, not durable
acceptance evidence. T12-P2-0 must reproduce/archive the relevant observations
with commands, hashes, sample windows and calculation method before relying
on them in the Phase 2 closeout.

The checked Phase 1 items establish their tested slices, not full coverage of
these loops. In particular, T12-5's array work must not be counted again as
complete native typed-array stores or native load-to-consumer propagation:
those are explicit Phase 2 deliverables below. Likewise, a helper fast head
does not by itself eliminate a caller's safepoint/root preparation.

## 18. Phase 2 work breakdown and ordering

Use `T12-P2-*` identifiers to distinguish these packages from Phase 1's
`T12-2` numeric work. The checkboxes below preserve the initial plan; §28 is
the authoritative implementation/closeout record. Archive each integrated
package separately; a table of combined final effects cannot replace phase
attribution.

| Package | Deliverable | Dependencies | Main target / protection |
|---|---|---|---|
| T12-P2-0 | Exact controls, regression triage and actual-hot-loop census | Phase 1 archive | All 63 rows; current-tree versus archived-release distinction |
| T12-P2-1 | Ordinary read admission, host rejection and array length | P2-0 | `text_search`; globals, DOM and proxy correctness |
| T12-P2-2 | Constant-cost rejection of irrelevant intrinsic mutations | P2-0 | `havlak`, class/object writes; prototype invalidation |
| T12-P2-3 | Native local/loop facts independent of Number return | P2-0 | FFT, `primes`, search loops; `sum`, `fib`, mixed calls |
| T12-P2-4 | Complete native array/typed-array access pipeline | P2-1, P2-3 | FFT, `primes`, `text_search`; holes, coercion, detach |
| T12-P2-5 | Effect-bounded guard/length reuse in loops | P2-3, P2-4 | Repeated array loops; aliasing and precise ownership |
| T12-P2-6 | Measured ordinary-field and callable-entry coverage | P2-1, P2-2; reuse P2-3/4 facts | `havlak`, `microdiff`, `cd`, custom hash maps; `gcbench` |
| T12-P2-7 | Profile-gated allocation, compilation and RegExp residuals | P2-0; repeat census after P2-1–6 | `hyphen`, `prettier_ast`, `regexredux`, high-RSS rows |
| T12-P2-8 | Regression resolution and full-population closeout | P2-0–7 | Correctness, both timing metrics, memory and MIR size |

Implement P2-1 and P2-2 first so their runtime-only benefits are measurable
before compiler changes. Separate P2-3, P2-4 and P2-5 revisions: better
inference, physical access and guard reuse must not be one opaque patch.
Within a package, use reviewable substeps rather than an all-or-nothing
rewrite. Preserve the existing shared helper/emitter ownership boundaries
(**D1.3v3**, **D8.2.3**).

## 19. T12-P2-0 — Freeze controls and turn observations into contracts

**Primary tools:** the manifest verifier, standard and paired benchmark
runners from §15, existing `JsOpt` diagnostics, finalized MIR capture and
platform CPU sampling. Do not introduce another benchmark/profiler framework.

- [ ] Retain the exact Phase 1 final archive and its original evidence unchanged. Build/archive the actual Phase 2 starting tree after correctness checks; record commit plus dirty patch, release configuration, binary hash, QuickJS/MVP identity, modules, power/profile settings and manifest/input hashes. A merged/current build is not interchangeable with the archived Phase 1 binary.
- [ ] Measure any starting-tree delta against Phase 1 separately. Use the exact new starting release as the causal Phase 2 control; retain Phase 1 as the historical anchor. Store accepted new artifacts under `test/benchmark/js_mvp/tune12/phase2/`, with unique package/revision names; use `temp/` for exploration.
- [ ] Reproduce the `text_search` and Havlak profiles in execution windows, retaining startup separately. Record thread denominators and inclusive versus self counts. Capture FFT, sieve and search-loop MIR from the same release used for timing.
- [ ] Record per target: selected function variant, native/boxed locals, property/length calls, typed/ordinary read and store arms, per-iteration metadata calls, root/safepoint work, and the actual admission/refusal reason. Distinguish emission, runtime hit/miss counts and CPU samples.
- [ ] Recheck Phase 1 regressions with at least 11 interleaved pairs: `nqueens`, `cpstak`, `base64`, `levenshtein`, `pidigits`, `json_gen`, `splay`, `quicksort`, and `paraffins`. Retain uncertain cases as uncertain; three samples do not establish a cause. Reproduce correctness/baseline residues on the actual control rather than inheriting old failure counts.
- [ ] Freeze target, miss-heavy and neutral controls for each package before timing it. Include Phase 1 wins (`sum`, `sumfp`, `fib`, `fibfp`, `knucleotide`, `regexredux`, `collatz`) and full-runtime strengths (`binarytrees`, `gcbench`).

**Exit:** exact controls are runnable and identified; the full-population
baseline has valid statuses/output evidence; the hot-loop census and inherited
regression disposition are archived. A failure, missing timing or unresolved
crash remains visible and cannot be converted into a survivor-only score.

## 20. T12-P2-1 — Ordinary reads before host/global machinery

**Primary code:** `js_get_name_id`, `js_get_reference`, `js_get_key_core`,
`js_get_host_dynamic_property` and the array-length branch in
[js_runtime.cpp](../../lambda/js/js_runtime.cpp);
`js_is_window_event_global_property` in
[js_globals.cpp](../../lambda/js/js_globals.cpp); realm/global accessors in
[js_runtime_state.cpp](../../lambda/js/js_runtime_state.cpp); named access
lowering in [js_mir_expression_lowering.cpp](../../lambda/js/js_mir_expression_lowering.cpp).

- [ ] Classify impossible host receivers before resolving window/global slots. Begin with representations whose identity cannot be a global/window/host receiver. An ordinary-looking Map shape or a familiar property spelling alone is not that proof; preserve genuine globals, proxies, DOM wrappers and host-backed placeholders.
- [ ] Factor/reuse a noncoercing ordinary-receiver admission shared by the named fast head and its semantic kernel. On a slow continuation, avoid repeating a completed host probe only when the continuation carries a valid operation-local proof and no intervening effect can invalidate it. Never use a caller-controlled unchecked skip flag.
- [ ] Add a static-`NameId` ordinary-array `length` head and corresponding MIR plan. Use the existing physical length and storage proof. Content containers, companion overrides or other unsupported representations keep their established path; proxy reads must still execute traps. Do not re-create a key string or convert a canonical name on a successful head.
- [ ] Read already-published realm/global state without repeated initialization only where construction/epoch/lifetime invariants permit it. Preserve unbound-runtime behavior, lazy initialization, heap replacement and multiple realm identities; retain precise rooted storage (**D5.4.2–D5.4.4**).
- [ ] Keep an admitted length value native for an admitted numeric consumer through `MirValue`; boxed consumers still receive a valid Item. Guard failure reuses the original receiver/key and executes the semantic operation once (**D2.4**, **D8.4.1v2**).

**Tests:** ordinary/tagged/numeric arrays; empty and sparse arrays; proxy
arrays; content/companion properties; length mutation and descriptor changes;
inherited names; live window metrics/event state; global-binding mirrors;
unbound DOM/native entry; separate runtime/heap lifetimes. Reuse §11's host
fixtures and exercise shared helpers through both MIR and AST backends.

**Structural exit:** admitted array-length reads perform neither host/global
lookup nor string-name parsing. Unsupported receivers have one correct
continuation, with no repeated observable Get. Do not declare the complete
helper `NO_GC` because its head is cheap: only a separately valid physical leaf
may have that contract (**D5.3.2**, **D8.4.3v2**).

**Measurement:** runtime-head and MIR-length changes get separate checkpoints.
Show reduced host/global/name work in `text_search`, then paired release
improvement; retain miss-heavy host/proxy controls. This package does not
claim loop-hoisting benefits reserved for P2-5.

## 21. T12-P2-2 — Make intrinsic mutation relevance cheap

**Primary code:** `js_intrinsic_note_property_mutation`,
`js_intrinsic_note_prototype_mutation`, intrinsic publication/invalidation in
[js_globals.cpp](../../lambda/js/js_globals.cpp), mutation entry points in
[js_runtime.cpp](../../lambda/js/js_runtime.cpp), and existing realm-slot
ownership in [js_runtime_state.cpp](../../lambda/js/js_runtime_state.cpp).

- [ ] Inventory every observer and mutation route before changing the scan: ordinary assignment, define/delete, prototype changes, builtin construction, constructor `prototype` replacement and Object.prototype's effect on inherited array indices. Keep a root-cause fixture for each route affected.
- [ ] Reuse existing receiver metadata for a constant-cost negative test if it can represent intrinsic relevance. If it cannot, add the smallest runtime-owned identity marker/index with explicit publication, invalidation and teardown; do not add a per-callsite cache or an unrooted object-pointer table. Document why existing metadata is insufficient before adding a field.
- [ ] Mark/register actual intrinsic identities when they are published, and dispatch relevant mutations only to the affected existing invalidation logic. Account for an identity shared by multiple intrinsic roles; constructor names, user-visible `__is_proto__` properties and ordinary class tags are not identity proofs.
- [ ] Preserve bootstrap suppression without suppressing later user mutation. Preserve Object.prototype-driven array invalidation, constructor replacement, lazy intrinsic creation, realm separation and heap/reset epochs. Newly published and replaced identities must not leave stale positive or false-negative membership.
- [ ] Converge all mutation consumers on the same classifier; remove the superseded class-wide scan from ordinary unrelated writes. Keep metadata precisely owned and do not add hot-path locks/atomics (**D5.3.3–D5.3.5**, **D5.4.4**, **D8.4.1v2**).

**Tests:** mutation of each affected intrinsic category versus unrelated
instances, Object.prototype numeric accessors, Array.prototype iterator
changes, constructor prototype replacement, delete/redefine, repeated lazy
initialization, multiple realms and forced GC/heap replacement. Inherited
array behavior and builtin protocol guards must observe the mutation.

**Exit:** an unrelated ordinary write performs no intrinsic-class traversal
and no realm-slot scan. Diagnostic operation counts stay independent of the
number of initialized intrinsic classes; real relevant mutations still
invalidate correctly. Confirm a paired Havlak/class-write win and resample
the old scan; do not attribute every realm-slot sample to this function.

## 22. T12-P2-3 — Native locals without a Number-return requirement

**Primary code:** `jm_infer_indexed_node`, numeric return analysis and
`jm_populate_native_number_binding_facts` in
[js_mir_function_collection_class_inference.cpp](../../lambda/js/js_mir_function_collection_class_inference.cpp);
eligibility in [js_mir_module_batch_lowering.cpp](../../lambda/js/js_mir_module_batch_lowering.cpp);
function/statement lowering and existing `FnVariantAnalysis`/`MirValue` carriers.

- [ ] Separate entry candidates, body-local Number facts, result/completion facts and ABI selection. A boxed or `undefined` result must not prevent independently proven local arithmetic from using F64. Keep generic parameter types unchanged (**D3.3.2v2**); do not just relax the native-return check and reuse an incompatible ABI.
- [ ] Extend existing binding-identity dataflow through initializers, aliases, assignments, branches and loop joins. Carry integer-range facts only where separately proved; JS Number induction is not an unchecked machine-integer contract. Model zero iterations, break/continue, rebinding, mixed writes and exceptional exits conservatively.
- [ ] Broaden static candidate evidence for parameters used with numeric locals/expressions, including the sieve's limit comparisons. Arithmetic syntax, a literal callsite or the source name may select a guarded candidate but never authorize coercion or become a generic Number proof. Bound propagation/variant growth and retain a diagnostic refusal reason.
- [ ] First support numeric local regions in existing boxed bodies and simple guarded entries with boxed object/array parameters plus native scalar parameters. Add an explicit supported result route for `undefined`/boxed completion only where existing entry conventions can carry it; preserve strict/sloppy receiver, stack-limit, exception and handler behavior.
- [ ] Reuse `MirNumericOpPlan` for arithmetic/comparisons and transport native values across admitted direct edges. Materialize valid Items at genuine generic consumers or merges. An unproved operation uses its complete semantic helper with already evaluated operands; no mid-function restart or replay is allowed.
- [ ] Apply definite initialization and shared-emitter root liveness to the selected representation, not the generic source variable. Reference parameters and values live on error/slow edges remain rooted even when numeric locals need no homes (**D2.4**, **D5.3.2–D5.3.5**).

**Tests:** a void-return mutating numeric loop, a boxed-return function with a
numeric inner loop, local-dependent parameter comparisons, branch/loop type
changes, fractional/negative/NaN/infinite limits, signed zero, overflow and
large Numbers, string `+`, BigInt/Symbol/coercing arguments, omitted/extra
arguments, recursion, throw/finally and forced-GC slow exits. Preserve §7's
negative numeric-wrapper probes.

**Structural exit:** benchmark-equivalent FFT/sieve/search numeric regions
show native induction/arithmetic where proven; a nonnumeric function result
is not the refusal reason for those locals. At this checkpoint unresolved
element reads may still prevent native downstream arithmetic; record those
edges for P2-4 rather than claiming the whole loop is specialized. Retain
`sum`/`fib`'s established native bodies and bound MIR/compile-time growth.

## 23. T12-P2-4 — Native load, arithmetic and store as one pipeline

**Primary code:** `JsMirReference`, `jm_fixed_typed_array_receiver_kind`,
`jm_emit_fixed_typed_array_read`, `jm_emit_packed_array_read` and store lowering
in [js_mir_expression_lowering.cpp](../../lambda/js/js_mir_expression_lowering.cpp);
typed view/storage helpers in [js_typed_array.cpp](../../lambda/js/js_typed_array.cpp);
existing owned element stores and shared emitter physical operations.

### 23.1 Receiver admission and read carriers

- [ ] Admit repeated local/parameter array accesses without requiring a `new TypedArray` initializer. Static call/body facts may select a bounded guard plan; actual representation, brand and storage determine the hit. Wrong receivers retain one generic continuation. Begin with the existing tagged/ArrayNum/Uint8/Float64 cases, not an unbounded receiver-type dispatch chain.
- [ ] Reuse one typed-view validation contract for element kind, attached fixed buffer, offset, current bounds and data. Remove redundant per-access type/length/data helper layering by factoring the existing validation or sharing physical loads after a valid guard; do not copy private typed-array layout logic into a second semantic implementation.
- [ ] Preserve the loaded carrier through the numeric consumer: native F64 from Float64/ArrayNum and appropriately extended Uint8 values. A tagged-array load needs an actual Number guard before numeric arithmetic. A non-Number hit or property miss must preserve its original value and JS coercion order; never re-read an accessor to recover a native value.
- [ ] Represent mixed hit/miss results explicitly with `MirValue` and existing merge/consumer machinery. Do not box immediately on every successful native read just because the slow arm returns Item. At a genuine boxed boundary, use the established encoder/home ownership (**D2.4.1–D2.4.3**).

### 23.2 Stores and observable ordering

- [ ] Add real existing-element Uint8Array and Float64Array store arms, rather than feeding them through `js_elements_set_int_completion`'s ordinary-array miss. Start with already-proven Number RHS values and attached fixed, nonshared storage. Reuse existing conversion policy: Uint8 modulo conversion is not clamping or an unchecked C integer cast; Float64 preserves ±0, NaN and infinities.
- [ ] Preserve receiver/key/RHS evaluation, key conversion and typed-array numeric conversion in their ECMAScript order. Coercing RHS/key cases use the full helper; reacquire storage after any potentially detaching/resizing effect. Do not return early on an invalid index if doing so skips required conversion effects.
- [ ] Cover numeric `-0` versus string `"-0"`, fractional/nonfinite/out-of-range keys, view offsets, wrong element brands and incompatible values. Shared/resizable/growable or other unproved buffers remain explicit fallbacks; growth and representation-changing ordinary stores retain their owned storage primitive.
- [ ] Converge scalar read/write validation with live helper consumers where physical work is identical. Do not introduce a private array format, public ABI, raw pointer surviving a `MAY_GC` call, or separate JS GC policy (**D5.3**, **D8.2.3**).

**Tests:** extend §10.4's semantic cases with parameter-supplied typed arrays,
aliases, local rebinding, mixed tagged elements, detached/wrong-brand misses,
Uint8 wraparound/NaN/infinity/fraction conversion, Float64 special values,
descriptor/prototype changes and side-effect ordering. Add `.js`/expected
`.txt` and MIR fixtures for the complete read–compute–write shape.

**Structural exit:** the admitted FFT loop has native Float64 loads,
arithmetic and stores through a parameter; the admitted prime-sieve inner
store no longer calls generic `js_set`/property-key conversion. Search-loop
Number consumers avoid repeated extraction/boxing on their admitted path.
Assertions scope the hot arm, leaving legitimate fallback helpers intact.
Capture actual canonical benchmark MIR as well as small fixtures; runtime
hit/refusal evidence must explain any remaining generic hot edge.

**Measurement:** paired FFT, `primes`, `text_search`, `matmul`,
`navier_stokes` and ordinary-array controls. Keep parameter admission,
carrier propagation and typed stores as separate measured substeps where
possible; do not claim a small load-only fixture closes this package.

## 24. T12-P2-5 — Reuse guards only within proved effect boundaries

**Primary code:** existing function/loop effect analysis, reference/access
plans, statement lowering and shared emitter root/liveness facilities.

- [ ] Identify repeated pure receiver/length/storage predicates within an existing control-flow region. Start with simple synchronous loops whose receiver identity is stable and whose admitted body has no user callback, relevant alias mutation, storage growth, detach/resize or `MAY_GC` borrow boundary.
- [ ] Separate reusable representation/brand facts from mutable length, backing storage and element-value facts. A stable binding alone proves none of the latter. Preserve per-access bounds and tagged-element checks unless an explicit range/value proof subsumes them; arbitrary `a[i]` still needs its normal semantics.
- [ ] Select a guarded loop sibling before its first effect, using already evaluated operands; failure enters the unchanged generic loop from the same initial state. Bound duplication and reuse existing planning. No mid-loop restart/deoptimization or replay of earlier stores/calls is introduced.
- [ ] Invalidate/reacquire facts at every relevant mutation, call or exceptional edge; unsupported loops retain per-access guards. Raw backing pointers never survive an invalidating boundary. If safe reacquisition cannot be expressed without replay, refuse that region rather than inventing a continuation engine.
- [ ] Let the shared emitter remove now-unneeded root stores only from verified effect/liveness facts. General helpers retain their real GC/completion contracts even when a particular hit is nonallocating (**D5.3.2–D5.3.5**, **D8.4.3v2**).

**Tests:** alias mutation of length/storage, callbacks/getters between accesses,
representation transitions, prototype changes, buffer detachment, collection
and throwing slow paths, nested loops and early exits. Prove both the reused
guard case and refusal/invalidation cases with structural and behavior tests.

**Exit:** admitted loops perform demonstrably fewer repeated guards/metadata
calls, and paired timings beat the P2-4 checkpoint without new miss-heavy or
MIR-size regressions. If a region's proof cannot be established, name that
exact residual; do not mark it optimized because its per-access path works.

## 25. T12-P2-6 — Object fields and calls selected by actual evidence

**Primary code:** `js_named_fast_lookup`, property kernels, predicted-field
planning/`MirFieldAccessPlan`, callable finalization, `js_call_entry_generic`
and the direct body/native entry selection already described in §11–§12.

- [ ] Resample Havlak, `microdiff`, `cd`, custom hash maps and relevant text pipelines after P2-1/2. Separate own-field lookup, prototype/method lookup, mutation, actual call setup and allocation. Inclusive samples below a call wrapper do not establish that the wrapper itself consumes that time.
- [ ] Extend immutable literal/constructor shape predictions only for measured missing cases. Reuse shared ordinary-slot admission and owned-field storage; preserve type transitions, deletion, descriptors, proxy/host state and inherited reads. Mutable per-site receiver/key caches remain prohibited (**D8.4.1v2**).
- [ ] Carry usable field representations into existing numeric/access plans rather than loading a field directly only to box/unbox it immediately. Calls or writes that can change the shape/value end the relevant proof.
- [ ] For measured call-entry overhead, use existing finalized callable capabilities to choose the smallest valid entry. Preserve receiver binding, module/realm, arguments, super/private/new.target, recursion limits and explicit completions; unproved activation requirements keep the existing dynamic entry (**D6.2.2v2**, **D8.4.2v2–D8.4.3v2**).
- [ ] Preserve Get-before-arguments ordering and guard the selected callable, not a later property reread. Reuse caller-owned arguments only under the existing rooting contract. Do not duplicate the call kernel or count an already-existing direct call as a Phase 2 improvement.

**Tests/exit:** reuse §11–§12's adversarial field/method cases, including a
getter returning a different function, an argument replacing a method,
rebound constructors, cross-realm callees and forced-GC retained values.
Each accepted change needs a measured hot site, simpler finalized MIR or
runtime operation counts, and an isolated paired win. Protect `binarytrees`
and `gcbench`; stop extending a prediction when guard cost outweighs its hits.

## 26. T12-P2-7 — Profile allocation, compilation and built-in residuals

This package requires a census, not speculative changes to every subsystem.
Implementation is limited to diagnosed causes in the existing runtime and
compiler; a missing diagnosis is reported as open, not replaced by GC tuning,
vendor patches or a new backend.

- [ ] Split execution, parse/build/lower/JIT, startup/module setup and teardown where the current measurement facilities permit. End-to-end minus execution is only aggregate nonexecution time, not automatically compilation. Record source size, MIR instruction/variant counts and compiler allocation beside timings.
- [ ] For `hyphen`, `prettier_ast`, `microdiff`, Havlak and other high-RSS rows, distinguish live guest data, side roots/scalar homes, native payloads, retained AST/MIR/cache state and transient high-water marks. Measure GC CPU/collection counts before treating GC as the bottleneck; preserve `gcbench` as a control.
- [ ] Where profiles confirm repeated whole-function analysis scans or unnecessary duplicate variants, reuse the existing AstIndex/function-owned transient analysis and bounded variant policy. Do not create a second persistent semantic fact authority or remove required fallback bodies just to reduce MIR size (**D2.4.1**, **D3.3.2v2**, **D8.6.1**).
- [ ] Where allocation/retention is demonstrated, shorten the actual owner lifetime or remove an unnecessary intermediate using existing owned storage. Verify normal/error/teardown paths and long-lived roots. Do not tune collection thresholds merely to improve a short benchmark, retain unbounded tombstones/cache data silently, or restore native-stack scanning (**D5.3**).
- [ ] Profile `regexredux` separately: builtin-protocol admission/misses, matcher routing, UTF-16 conversion, exec-result materialization, replacement output and allocation. Extend §13's existing bulk path only for an identified cost, retaining lastIndex, custom exec/accessor, capture and replacement semantics. Do not edit RE2 or whitelist benchmark patterns.
- [ ] Record a disposition for each investigated category: diagnosed and fixed with A/B, disproved by measurements, or a precisely bounded unresolved follow-up. Scope a new implementation substep before changing code; absence of evidence does not authorize a runtime redesign.

**Exit:** archived profiles distinguish execution from compilation and live
memory from retained capacity. Every landed optimization has its own
correctness/ownership test and paired evidence; unresolved categories remain
visible in the final performance assessment. A lower RSS number alone is not
proof of faster execution or correct lifetime management.

## 27. T12-P2-8 — Acceptance, regression resolution and closeout

### 27.1 Measurement contract

Apply §15 with new Phase 2 artifact names; never overwrite Phase 1 records.
Use the existing paired runner's raw samples and 95% bootstrap statistics,
with at least 11 alternating pairs for package targets, inherited regressions
and new suspicious rows. Increase samples only to resolve declared
uncertainty; do not rerun until a favorable median appears. For expensive
rows, budget the longer run rather than silently changing input size or
substituting a synthetic loop for canonical acceptance.

Archive each package candidate, compare it both to its immediate predecessor
for attribution and to the fixed Phase 2 control for cumulative drift, and
retain normalized output equality. Diagnostic counters and sampling are off
in timing runs. Short-row repetition diagnostics remain outside the 63-row
metric; a partial MVP refresh remains explicitly a subset.

The final run includes all 63 canonical rows, at least three samples per
engine through the guarded standard workflow, and full-population interleaved
control/candidate evidence. Recheck material regressions with the stronger
sampling above. Report both geometric mean and total medians against the
same recorded binaries, plus execution/end-to-end timing, available RSS,
compiler costs and MIR growth. The 57-row memory runner's unsupported
JetStream cells remain unavailable, not fabricated.

### 27.2 Correctness and structural gates

- [ ] Add or extend focused `test/js` cases with expected `.txt` output and finalized MIR `.js`/`.mir-check` fixtures for each new admission and miss. Assert hot-arm helper absence/presence by semantic symbols, not register numbers, pointer values or a whole-module ban on legitimate fallback calls (**D8.6.2**).
- [ ] Run the JS optimization and MIR-emission suites, effect checker, relevant runtime/DOM tests, forced-GC poisoning corpus, MIR ratchet, Lambda baseline and Test262 using current populations. Changes to shared helpers additionally receive AST-backend coverage; shared emitter changes protect typed and untyped Lambda. If Radiant code changes, run its required baseline too.
- [ ] Fix every new failure/crash/timeout at its cause. Reproduce inherited failures on the exact control and report them distinctly; do not weaken harnesses, skip failing Test262 entries, raise ratchet budgets to absorb accidental expansion, or claim a green baseline with known failures.
- [ ] Audit new metadata fields and access plans for realm/heap lifetime, publication, completion handling and precise ownership. Preserve original evaluation order and one semantic continuation, with no mutable per-site ICs or code-baked context pointers.
- [ ] Resolve confirmed Phase 2 regressions and re-evaluate the inherited Phase 1 list. A reproducible regression above 3% triggers root-cause investigation; smaller systematic regressions also count. Use available isolated revisions to attribute causes; do not invent Phase 1 attribution where no archive exists.

### 27.3 Completion policy and record

**Implementation complete** means P2-0–6's explicit structural/correctness
deliverables are demonstrated in actual target paths, P2-7's census and
diagnosed substeps have honest dispositions, and P2-8's evidence is complete.
A checked helper fixture does not close an unoptimized canonical hot loop.
Any deliberately excluded case must identify its safe fallback and remaining
cost; an unresolved promised deliverable stays unchecked.

**Tuning successful** additionally requires credible full-population
improvement in both geometric-mean execution time and total-median time,
no unresolved material performance regressions, and no new correctness
failures. An inherited regression left unresolved remains a stated obstacle
to the overall Tune12 tuning-success claim, even if Phase 2 improves its own
control. Memory/compiler regressions are reported and investigated rather
than hidden behind runtime gains.

**Milestone reached** still means a complete fresh full-JS/QuickJS geometric
mean at or below 0.80x (§1). Do not promise this from the sample percentages,
MVP ratios or a twofold `text_search` scenario. If unmet, publish the actual
ratio and next dominant paths without changing the denominator or relabeling
implementation completion as performance acceptance.

Populate the record with links to durable evidence, not estimated gains:

| Package | Source / exact binary | Hot-path change and residual | Correctness / GC / MIR | Paired target, miss and control results | Status |
|---|---|---|---|---|---|
| T12-P2-0 | P2 control, SHA 4f1582…ef972 | Exact control and census | Source hashes/output equality archived | [63 x 11 final pairs](../../test/benchmark/js_mvp/tune12/phase2/p2-final-full-paired-release.json) | Complete |
| T12-P2-1 | js_runtime.cpp | Array length/host rejection | JsOpt head test; semantic fallback retained | Cumulative final matrix | Complete |
| T12-P2-2 | js_globals.cpp | O(1) mutation relevance | Intrinsic mutation expected-output case | Cumulative Havlak 0.7933x | Complete |
| T12-P2-3 | JS MIR analysis/lowering | Numeric locals with Item ABI | JsOpt boxed/void-return case | [r1](../../test/benchmark/js_mvp/tune12/phase2/p2-3-numeric-r1.json), final | Complete |
| T12-P2-4 | JS MIR typed access/runtime | Guarded typed load/compute/store | JsOpt + typed pipeline expected-output case | [r2](../../test/benchmark/js_mvp/tune12/phase2/p2-4-fft-r2.json), FFT 0.2337x | Complete |
| T12-P2-5 | MIR invariant metadata | Immutable element kind only | Existing bounds/data fallbacks remain | [r4](../../test/benchmark/js_mvp/tune12/phase2/p2-5-loop-kind-r4.json), uncertain isolated result | Complete, bounded |
| T12-P2-6 | Existing field/call plans | Safe refusal for mutable field/call sites | No mutable IC introduced | Final Havlak/control evidence | Complete by refusal |
| T12-P2-7 | Final release census | RSS/GC/cache diagnosis only | No unsupported GC change | [final RSS](../../test/benchmark/js_mvp/tune12/phase2/p2-7-text-memory-final.json) | Complete, residual open |
| T12-P2-8 | Makefile/tests/evidence | Release integrity and full closeout | Baseline 5,593/5,593; full Test262 40,263/40,263 | [standard snapshot](../../test/benchmark/js_mvp/tune12/phase2/Overall_Phase2.md) | Complete |

## 28. Phase 2 implementation closeout

Phase 2 is implemented under **S1.11**, **D2.4**, **D3.3.2v2**, **D5.3** and
**D8.4.1v2–D8.4.3v2**. No formal semantic or design ruling changed: this
section records implementation and evidence, while §§17–27 preserve the
planning contract.

### 28.1 Exact controls, release integrity and final causal result

The Phase 2 starting release is
test/benchmark/exe/lambda-tune12-p2-control, SHA-256
4f158287b8f342ef703b946a47507d18dcfedf89748eaaf50cc9c9bc30aef972,
from commit 763269c4fbc6df4ccecd5e7a0991dcebc20f0a4a. The full causal
candidate is lambda-tune12-p2-final-release, SHA-256
f6d61644742644d792ff7fd83cb5bdaac7d57ada3ca903740638b297ec739e5c;
the clean standard-workflow release is lambda-tune12-p2-final-standard,
SHA-256 467920b3a9180623c2512dfab99d8327098c2864287dcfed934af9fea25fb957.

The first final-pair attempt exposed a build-system root cause: debug and
release configurations both emit lambda.exe; unlike the debug target, the
release recipe did not force a relink. A newer debug host could therefore
satisfy the release target timestamp. build-release-compile now removes that
shared target before invoking the release configuration. The repaired path was
exercised after a debug baseline and rejected the debug marker before the
standard timing run. This is necessary for **S1.11**, not a benchmark
optimization.

[The paired artifact](../../test/benchmark/js_mvp/tune12/phase2/p2-final-full-paired-release.json)
has 63 canonical JS rows, 11 alternating pairs per row, AC-power metadata,
source-tree hashes, 10,000-resample paired-bootstrap intervals and equal
normalized output in all 693 pairs. Its candidate/control geometric mean is
**0.562698x**. Sums of the per-row medians are 211,935.173 ms control and
140,040.846 ms candidate, or **0.660772x**. Phase 2 implementation is
complete and both aggregate metrics improved. The stricter tuning-success
claim remains unearned: hyphen (1.0562x) and crypto_sha1 (1.0704x) are
investigated, visible residual regressions rather than hidden exclusions.

### 28.2 Implemented packages and deliberate residuals

| Package | Implemented target path | Evidence and remaining limit | Status |
|---|---|---|---|
| T12-P2-0 | Archived starting/final release controls; full workload/source identity, power and output evidence | 63 x 11 causal matrix; inherited regression controls all retained | Complete |
| T12-P2-1 | Early non-MAP host rejection and no-GC own array length head in js_get_key_core/js_get_name_id | JsOpt ArrayLengthNameUsesOwnNoGcHead; unsupported/proxy/companion cases keep one semantic continuation | Complete |
| T12-P2-2 | O(1) intrinsic-prototype relevance classifier with class tag then rooted identity fallback | Object/Array/Object.prototype and constructor-prototype mutation tests; unrelated writes no longer scan intrinsic slots | Complete |
| T12-P2-3 | Local numeric facts now survive boxed/void returns; native Item return ABI carries proven F64/I64 locals safely | NumericLocalFactsSurviveBoxedAndVoidReturns; r1 checkpoint and cumulative sum, fib, FFT and primes results | Complete |
| T12-P2-4 | Guarded Uint8/Float64 stores plus parameter receiver facts; raw typed loads propagate to numeric consumers | P2 fixtures exercise correct and wrong-receiver continuations; FFT 0.2337x, primes 0.0565x cumulative | Complete |
| T12-P2-5 | Loop invariant pass hoists only immutable typed-element-kind observation | Length/data remain per access because detach/resize can change them; r4 short-row result is explicitly uncertain, final matrix is cumulative | Complete, bounded |
| T12-P2-6 | Resampled field/call sites and kept existing guarded generic paths where shapes/capabilities are mutable | Havlak uses constructor-owned mutable fields and dynamic method capability; no unsafe mutable IC or duplicated call kernel was added | Complete by evidence-based refusal |
| T12-P2-7 | Release RSS and GC/cache census, with no speculative collector change | hyphen one 0.767 ms collection and 118,507 B retained source do not explain its 1.45 GiB RSS | Complete, residual open |
| T12-P2-8 | Full release gates, paired and standard snapshots, MIR check update and baseline-build repair | Details in §§28.3–28.5 and phase2 evidence directory | Complete |

The intermediate checkpoints remain durable:
[P2-3 numeric r1](../../test/benchmark/js_mvp/tune12/phase2/p2-3-numeric-r1.json),
[P2-4 FFT r2](../../test/benchmark/js_mvp/tune12/phase2/p2-4-fft-r2.json),
[P2-4 consumer r3](../../test/benchmark/js_mvp/tune12/phase2/p2-4-load-consumer-r3.json),
and [P2-5 kind r4](../../test/benchmark/js_mvp/tune12/phase2/p2-5-loop-kind-r4.json).
They are package attribution aids, not substitutes for the final matrix.

### 28.3 Final release/QuickJS position

The guarded standard workflow rebuilt release, verified no profiling symbols,
ran Test262 baseline 40,261/40,261, then collected three samples on all 63
LambdaJS/QuickJS rows. Its complete
[JSON](../../test/benchmark/js_mvp/tune12/phase2/final_phase2.json) and
[report](../../test/benchmark/js_mvp/tune12/phase2/Overall_Phase2.md) show
LambdaJS/QuickJS **2.936473x** geometric mean, 140,572.512 / 42,451.320 ms
total medians, and 11 LambdaJS wins. This improves Phase 1's 3.481106x but
does not meet the 0.80x milestone.

The largest remaining gaps are string/Unicode and dynamic-object costs:
hyphen, microdiff, revcomp, nqueens, towers, richards and deltablue. The four
original elapsed-time priorities all improved against the Phase 2 control:
text_search 0.5118x (one-sided 95% upper 0.5127), Havlak 0.7933x (0.7951),
three_way_merge 0.8634x (0.8681) and log_pipeline 0.9331x (0.9439). hyphen
remains a visible 1.0562x causal residual; it is not relabeled as a win.

### 28.4 Memory and compiler/GC disposition

The final release RSS census is
[p2-7-text-memory-final.json](../../test/benchmark/js_mvp/tune12/phase2/p2-7-text-memory-final.json):
microdiff 151 MiB, hyphen 1.45 GiB and prettier_ast 1.36 GiB, unchanged from
the prior checkpoint. The 57-row memory runner still lacks its unsupported
JetStream cells; they are not fabricated. A diagnostic LAMBDA_GC_STATS=1
Hyphen run recorded one 0.767 ms collection and 118,507 B retained script
source. The data do not establish GC CPU, compiler-cache retention or a
specific owner as the high-RSS cause, so Phase 2 intentionally makes no
threshold, cache or GC-scanning change (**D5.3**).

### 28.5 Correctness, structural and release gates

The final source passes make test-lambda-baseline, 5,593/5,593; this includes
31/31 JS MIR-emission and 4/4 JS Regex-router tests. The initial baseline
failure updated shared_loop_effects.mir-check from a stale boxed js_add
expectation to the actual proven native dadd while retaining the required
boxed bitwise coercion. The missing Regex-router executable was a focused
baseline build-list omission; its project is now built by
LAMBDA_BASELINE_TEST_PROJECTS, rather than omitted from the runner.

Additional gates passed: JS optimizer 31/31; forced-GC/MIR corpus 189/189;
MIR ratchet 19/19; exception-catalog and callable-catalog censuses clean; and
full Test262 40,263/40,263 fully passing with zero baseline regressions (two
pre-existing baseline improvements). The new JS expected-output cases cover
intrinsic mutation and typed read/compute/write fallback. These guards preserve
evaluation order, the one generic continuation, exact roots and error
propagation required by **D2.4**, **D5.3** and **D8.4.1v2–D8.4.3v2**.
