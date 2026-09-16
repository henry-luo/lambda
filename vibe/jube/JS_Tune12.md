# JS Tune12 — Full LambdaJS performance from MVP v1 designs

**Version:** 1.0.0

**Date:** 2026-09-16

**Status:** PLANNED — no Tune12 runtime changes have been implemented.

**Scope:** improve the full LambdaJS runtime and MIR lowering while retaining Lambda runtime types and full JavaScript semantics.

**Investigation baseline:** `f72f7552b027d01829a874ece9a8a591206db722`.

This is the implementation plan for [Full LambdaJS performance: lessons from MVP v1](JS_MVP_Full_Runtime_Comparison.md). It continues [Tune11](JS_Tune11%20%28done%29.md), whose shared numeric, index, field and string improvements are already implemented. The retained [MVP v1](JS_MVP_Runtime.md) is a source of design evidence and a comparison backend; Tune12 changes the full engine.

## 1. Objective and completion policy

Make common full-JS operations perform the same small amount of physical work that makes MVP fast, after establishing the guards and ownership required by full JavaScript. Prioritize unnecessary algorithms and missed specialization before broad runtime restructuring.

The implementation order is:

1. Eliminate Map/Set insertion-order rescans.
2. Extend existing native numeric variants to local accumulators and recursive numeric results.
3. Remove dominated TDZ checks and unnecessary ownership traffic using those facts.
4. Add small Number and own-element heads to existing runtime helpers.
5. Extend MIR array access to parameters, numeric arrays and fixed typed arrays.
6. Broaden guarded ordinary-field access and proved call entries.
7. Admit bulk RegExp operations through the existing semantic entry points.
8. Validate the complete standard workload population and publish the residuals.

Track three outcomes separately:

| Outcome | Required evidence |
|---|---|
| Implementation complete | Every T12 phase below has its code, structural evidence, correctness gates and measurement record; deliberately excluded cases retain a tested semantic fallback |
| Tuning successful | Exact-release A/B demonstrates a full-population improvement, with no unresolved material regressions or correctness failures |
| Performance milestone reached | A complete fresh 63-row matrix meets the stated full-JS/QuickJS milestone; a subset or estimated speedup cannot substitute |

Proposed performance milestones are **full LambdaJS / QuickJS geometric mean ≤0.80x**, followed by the earlier discussion's **≤0.30x** stretch objective. These are proposed full-engine targets, not measurements or promised results. The user's earlier 0.80x acceptance applied to MVP; this plan does not retroactively change that acceptance contract. If these targets remain unmet, report the actual result and leave the performance milestone open even when the listed implementation is complete.

From historical Result45's 2.588162x, these milestones would require approximately 3.24x and 8.63x improvement respectively. They cannot be forecast from the measured MVP gaps because MVP makes additional semantic assumptions. Per-row reporting remains mandatory; there is no requirement that every row beat QuickJS.

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

## 3. Evidence carried into the plan

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

## 4. Work breakdown and dependencies

| Phase | Deliverable | Depends on | Initial state |
|---|---|---|---|
| T12-0 | Frozen release control, measurement protocol and structural census | — | Planned |
| T12-1 | Hash-to-order-node Map/Set storage | T12-0 | Planned |
| T12-2 | Numeric function admission/dataflow | T12-0; execute after T12-1 for clean attribution | Planned |
| T12-3 | Definite initialization and minimal native frames | T12-2 | Planned |
| T12-4 | Number and own-element helper heads | T12-0; measure separately from T12-2/3 | Planned |
| T12-5 | Parameter/numeric/typed-array MIR access | T12-2, T12-4 | Planned |
| T12-6 | Ordinary named-field hits | T12-0, shared ownership tests | Planned |
| T12-7 | Proved callable entry selection | T12-2/3, T12-6 where property access participates | Planned |
| T12-8 | Admitted bulk RegExp operations | T12-7 admission/evaluation contract | Planned |
| T12-9 | Consolidation, complete A/B, guarded final report | T12-1 through T12-8 | Planned |

Implement in the listed order, keeping each phase independently reviewable. A newly exposed correctness defect is fixed at its cause before accepting the affected optimization; record it separately so its cost is not mistaken for the intended tuning effect.

All work items below are unchecked deliberately. Fill them with source revisions, exact binaries, test logs and result artifacts as implementation proceeds.

## 5. T12-0 — Freeze the control and make costs observable

**Purpose:** replace historical comparisons with a reproducible starting point and prove which paths the benchmarks take.

**Work:**

- [ ] Verify the 63-workload manifest with `verify_js_mvp_manifest.py`; record the source/input/wrapper hashes and current dirty-tree/source-patch identity.
- [ ] Complete correctness checks, then build release and archive its exact binary under `test/benchmark/exe/` using a name without `.exe`, so `make clean` does not remove the control. Record SHA-256, build configuration, compiler/platform and required module/package identity.
- [ ] Produce a fresh guarded full-population full-JS/QuickJS control, plus typed/untyped Lambda measurements to protect shared emitter changes. Use the standard runner rather than an ad hoc subset as the control.
- [ ] Reproduce the five semantic probes in the comparison evidence on full JS. Use their expected ECMAScript outputs for later regression fixtures; MVP is not the semantic oracle.
- [ ] Capture finalized MIR for `sum`, `fib`, `fft`, `primes`, an array-parameter search loop and a predicted-field loop. Record native variants, static helper sites and why candidate admission failed.
- [ ] Extend the existing opt trace/analysis reporting for relevant refusal reasons, without introducing a second profiler. Separate compile-time refusal counts from runtime hits/misses and elapsed-time samples.
- [ ] Retain the Map-size diagnostic and add existing-key update and delete/reinsert workloads to distinguish hashing, order maintenance and iteration costs. These remain diagnostic microbenchmarks outside the 63-row metric.

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

- [ ] Existing-key set/add: hash lookup, mutate the identified live node, return. Do not rehash/reinsert merely to update its value unless required by the existing API.
- [ ] New-key insertion: root incoming values, allocate/initialize a stable node, insert into the hash table, then publish it to the order chain once insertion succeeds. Handle allocation/insertion failure without leaving a ghost ordered entry.
- [ ] Get/has: one hash lookup; get reads the node's authoritative value.
- [ ] Delete: retrieve the identified node, remove the hash entry and mark the node deleted; no key search through the order chain.
- [ ] Clear: clear the hash index and mark live order nodes deleted, releasing their key/value ownership. Linear clear is acceptable; repeated set/delete must not inherit a list walk.
- [ ] Weak cleanup: use the same indexed removal operation from the ephemeron callback, respecting the collector's permitted allocation/cleanup behavior.

Retain the traversal chain through deleted nodes until safe reclamation. A simple initial implementation keeps an append-only chain with tombstones and stable `next` links until collection teardown; existing iterators skip deleted nodes. An update preserves position; delete followed by reinsert creates a new tail node. Clear must not sever an active iterator's route to subsequently added entries. Once an iterator reports done, it remains done.

This reuses existing nodes and deleted flags. Do not introduce compaction or iterator generation tracking merely to optimize this first change. Record tombstone/native-memory retention under churn; clear deleted key/value references promptly. If retention becomes a measured regression, address it as a separately specified lifecycle change rather than adding an unmeasured reclamation subsystem.

### 6.3 Tests and exit

- [ ] Extend collection behavior/GC coverage around `test/js/collections_advanced.js` and `collection_gc_retention.js`; add focused fixtures with expected `.txt` output for missing cases.
- [ ] Cover NaN, ±0, equal-content distinct strings, object identity, update-without-reorder, delete/reinsert, clear/repopulate, and independent collections.
- [ ] Exercise active iterators and `forEach` while deleting the current/next/tail entry, appending entries, clearing, and reinserting. Test iteration after hash growth and the permanently exhausted iterator state.
- [ ] Force GC with object keys/values, string keys, out-of-band numeric scalar homes, weak keys and values reachable only through ephemeron semantics.
- [ ] Add a diagnostic comparison counter or direct structural assertion showing no insertion-order key comparisons on set/add/delete. Keep it out of timed code.

**Performance acceptance:** the 1K/2K/4K/8K construction curve must stop exhibiting quadratic growth; the final two doubling ratios should be materially closer to 2 than 4. Use operation counts to distinguish allocator noise from an algorithmic failure. `knucleotide` must show a reproducible improvement; benchmark other actual collection consumers identified in T12-0. Track peak native/GC memory and churn retention. Do not promise a specific fraction of the 15.33x MVP gap before this A/B exists.

## 7. T12-2 — Admit complete numeric functions safely

**Primary code:** [js_mir_function_collection_class_inference.cpp](../../lambda/js/js_mir_function_collection_class_inference.cpp), [js_mir_module_batch_lowering.cpp](../../lambda/js/js_mir_module_batch_lowering.cpp), [js_mir_function_class_lowering.cpp](../../lambda/js/js_mir_function_class_lowering.cpp), [js_mir_calls_boxing_types.cpp](../../lambda/js/js_mir_calls_boxing_types.cpp), [ast-core.hpp](../../lambda/runtime/ast-core.hpp).

### 7.1 Analysis and bounded specialization

- [ ] Replace the returned-local blind spot with binding-identity dataflow. Track Number facts through initializers, assignments, branches and loop joins; an incompatible write widens the fact rather than silently retaining a numeric carrier.
- [ ] Keep candidate entry shapes separate from the generic parameter contract using existing function/variant facts. Do not make a generic parameter Number merely because its body uses arithmetic.
- [ ] Resolve numeric result facts through direct calls and recursive components. Use a bounded monotone fixed point with a diagnostic refusal when the bound is exhausted; do not add source-name special cases for `fib` or `sum`.
- [ ] For recursive candidates, verify the complete body under the assumed guarded entry shape, including every return and reachable edge. A circular assumption alone is not a proof. Fallthrough/undefined and mixed returns remain boxed until explicitly represented.
- [ ] Begin with one Number-native variant per eligible simple noncapturing function, alongside the existing complete boxed body. No combinatorial parameter-shape expansion or feedback-driven variants.
- [ ] Initially exclude defaults/rest/destructuring, `arguments`, direct eval, with, generators/async, mutable captured state and other bodies whose effects are not modeled. Existing supported native cases must remain correct and must not be silently disabled.

Use the existing indexed AST/binding analysis and `FnVariantAnalysis` binding/value records. If extra dataflow scratch is necessary, make it function-owned and transient; do not establish another persistent per-node semantic-fact authority.

### 7.2 Entry and body lowering

- [ ] Reuse `jm_emit_exact_native_shape_test` and the existing boxed wrapper's native guard; audit that it recognizes all admitted JS Number carriers while excluding Symbols and BigInts. Keep safe unsupported carriers on the boxed path.
- [ ] A guard must inspect representation without calling `ToNumber`, `valueOf`, getters or user code. Missing/mismatched arguments enter the boxed body with their original values.
- [ ] Preserve evaluation of extra actual arguments even when the numeric body ignores them. Do not duplicate default/argument effects between entries.
- [ ] Emit F64 locals and results through `MirValue`, existing native ABI helpers and `MirNumericOpPlan`. Box only at an Item consumer/return boundary.
- [ ] Direct numeric calls require both admitted actual shapes and a stable selected callee. A recursive reference to a rebound outer function must still see the new binding. If stability cannot be proved or guarded before effects, use the existing call path.
- [ ] Preserve stack-limit checks and explicit error companions. Do not replace a recursive overflow with an unchecked native stack failure.

### 7.3 Tests and exit

Add behavior and `test/mir/js/*.js` + `.mir-check` fixtures for a returned accumulator, local aliases, loop joins, self recursion, a recursive numeric component, mixed returns and a body that changes a parameter's type. Reuse `JsOpt` fixtures for emitted-path/refusal checks.

Call the same function with Number, string, boolean, null, undefined, BigInt, Symbol and a coercing object. Cover throwing/side-effecting `Symbol.toPrimitive`, omitted/extra arguments, NaN, infinities, ±0, fractional values and out-of-band doubles. Include aliasing/rebinding of a recursive function. Assert exact outputs/errors, not only aggregate checksums.

**Structural exit:** benchmark-equivalent `sum` and `fib` produce native bodies; the native `sum` loop contains F64 compare/add/subtract, and native recursive edges retain native operands/results. Generic mixed calls still execute the boxed implementation. Preserve full-JS direct boxed calls already present; do not attribute their existence to Tune12.

**Performance targets:** `sum`, `sumfp`, `fib`, `fibfp`, `mbrot`, `diviter` and any additional admitted rows from the census. Measure compile time and MIR growth because every extra body has a cost. A blanket return-inference relaxation without semantic guards does not pass this phase.

## 8. T12-3 — Definite initialization and minimal native ownership work

**Primary code:** existing function/binding analysis, [js_mir_expression_lowering.cpp](../../lambda/js/js_mir_expression_lowering.cpp), [js_mir_statement_lowering.cpp](../../lambda/js/js_mir_statement_lowering.cpp), [mir_emitter_shared.hpp](../../lambda/runtime/mir_emitter_shared.hpp), function frame/call lowering.

- [ ] Track definite initialization independently of Number type. A read or assignment may omit its TDZ check only if initialization dominates every incoming control-flow path.
- [ ] Model loop backedges, zero-iteration paths, shadowing and branch joins; exclude or invalidate facts across unmodeled eval/with/capture effects. Preserve const-assignment errors separately from TDZ.
- [ ] Derive live root slots from the selected variant's actual references and safepoints. Do not publish root homes for F64 values merely because the generic body uses Items.
- [ ] Retain ownership for boxed arguments, callee/context state and values live across allocating error paths. A mostly numeric function can still require roots on its slow/exception edge.
- [ ] Use known result representation to remove irrelevant pending/scalar-home handling within native call chains; retain the existing companion and adoption protocol at the real boxed boundary.
- [ ] Keep canonical safepoint slots and native stack checks. Do not remove a root frame or change a helper effect declaration merely because a benchmark did not collect.

**Tests:** initialized versus conditionally initialized locals, use-before-declaration, captured let/const, shadowed names, assignment before initialization, const writes, try/finally and throwing paths where supported, recursive overflow, and forced-GC runs byte-matching normal runs.

**Structural exit:** the initialized `sum` loop no longer contains `js_check_tdz`; its numeric values do not acquire Item root homes or transient-number storage between arithmetic operations. Fixtures scope assertions to the native body, so legitimate wrapper/slow-path checks remain allowed. **D8.6.2** forbids fragile assertions on register numbers, absolute pointers or raw immediates.

Measure this separately against the T12-2 candidate so the effects of better admission and smaller frames/checks are distinguishable.

## 9. T12-4 — Small common heads on existing helpers

**Primary code:** [js_runtime_value.cpp](../../lambda/js/js_runtime_value.cpp), own-element helpers in [js_runtime.cpp](../../lambda/js/js_runtime.cpp), existing GC-effect/import declarations.

### 9.1 Number operations

- [ ] Put a noncoercing Number-pair case before general root setup in `js_add` and the corresponding shared arithmetic/comparison operation entry points where profitable.
- [ ] Reuse one representation classifier/extractor and the existing numeric operation implementation. The third similar operator must use a shared shape/table, not another copied tag-switch body.
- [ ] Preserve the normal Item result encoder, signed zero, NaN, infinity, division/remainder and numeric precision. Power/bitwise/equality cases enter only when their JS-specific policy is explicitly covered.
- [ ] On a miss, run the existing full semantic body exactly once, preserving ToPrimitive/ToNumeric order, string concatenation, BigInt behavior and errors.
- [ ] Keep the complete helper's `MAY_GC`/throwing declaration when its fallback can allocate/throw. Only an independently valid physical leaf may have a narrower effect contract; verify changes with the existing GC-effects checker.

### 9.2 Own elements

- [ ] Add/streamline the present-own-element case before root preparation/general property dispatch when no GC/user code can occur on that hit.
- [ ] Separate read facts from write policy and existing-slot overwrite from add/grow/hole creation.
- [ ] Existing writable own elements do not need extensibility or a prototype-absence proof. Retain descriptor/writability, actual ownership and storage checks; out-of-bounds additions to a nonextensible array still miss/fail correctly.
- [ ] Share the proof definition with MIR access planning. Do not duplicate a general getter/setter algorithm under a `fast` name.

**Tests:** mixed Number/non-Number operations, coercion-order logs, Symbol/BigInt errors, numeric versus string keys, holes/inherited accessors, frozen/sealed/nonextensible arrays and scalar-home elements. Keep the existing `NonExtensibleArrayFallsBack` test for element creation; add separate coverage for legal existing-element overwrite rather than weakening that test.

**Exit:** instrumentation demonstrates the intended head is reached before slow setup, and guard misses produce unchanged results. Measure a generic/mixed-call workload where MIR cannot eliminate the helper. A helper-only gain does not justify claiming that caller root stores or helper calls disappeared; those are T12-2/3/5 concerns.

## 10. T12-5 — Native array access beyond literal receivers

**Primary code:** `JsMirReference` and `jm_emit_packed_array_read` in [js_mir_expression_lowering.cpp](../../lambda/js/js_mir_expression_lowering.cpp), existing shared emitter address/load/store primitives, [js_typed_array.cpp](../../lambda/js/js_typed_array.cpp), array/storage ownership helpers.

### 10.1 T12-5a: parameter and local arrays

- [ ] Allow a repeated access through a parameter/local to select the existing guarded packed-array plan even without literal provenance. Static candidacy selects a guard; it does not prove the receiver type.
- [ ] Carry the evaluated receiver and native key through `JsMirReference`. Preserve base/key evaluation and coercion order; a miss uses those same evaluated values.
- [ ] First admit ordinary packed tagged arrays with valid index, length/capacity, backing storage, no overriding indexed descriptors/host behavior and ownership-safe payload. Reuse existing element-state flags to exclude holes or explicitly test the slot when admitting a holey representation.
- [ ] Add an ArrayNum physical arm using its established representation and override checks. Keep the loaded F64 native when its consumer admits Number.
- [ ] Add existing-own-element stores through the shared storage primitive. Growth, sparse storage, incompatible values, scalar-tail transitions, COW/write preparation and descriptors remain on the semantic path until separately proved.

Use at most a small fixed set of static storage arms: tagged array and numeric array initially. A miss has one existing semantic continuation. Do not build a per-site observed-type table.

### 10.2 T12-5b: actual fixed typed arrays

- [ ] Start with Uint8Array and Float64Array, reusing real `JsTypedArray`/buffer metadata; do not route them through ordinary arrays as MVP does.
- [ ] Guard brand/element kind, attached fixed buffer, view bounds and valid numeric index. Initially reject shared/resizable/growable or otherwise unsupported buffers to the existing typed-array helper.
- [ ] For reads, issue the appropriate native load and extend/convert its representation. For stores, preserve Uint8 conversion and Float64 Number policy. Start with already-proved Number values; coercing values take the full helper before any stale data pointer is reused.
- [ ] Reject fractional/nonfinite/out-of-range keys without truncating them into a valid element. Preserve numeric `-0` versus canonical string `"-0"` behavior through the reference/key contract.
- [ ] Cover side effects in receiver/key/RHS evaluation: a key or value conversion can detach or change the buffer. Check storage facts after any such effect at the point required by the semantic operation.

### 10.3 T12-5c: guard placement and effects

Per-access guards are the first correct version. Hoist them only after proving an entire region cannot call user code, collect in a way that invalidates the borrow, mutate descriptors/prototypes, grow storage, detach or replace the buffer. A store that might grow or change representation ends the region. An alias can invalidate a fact even if the receiver's local binding is unchanged.

Do not cache raw element pointers across a `MAY_GC` call. For broader loops, reacquire storage after effects or retain per-access guards. Region specialization must not replay prior side effects on a guard failure; no mid-loop restart/deoptimization is introduced.

**Reuse boundary:** use `MirValue`, existing container guards, address calculation and owned stores. If a compact dense-access plan descriptor is needed to converge Lambda and JS consumers, place only physical layout/key/carrier facts in the shared emitter; JS descriptor/prototype/typed-buffer admission stays in JS. Do not add a second JS array representation.

### 10.4 Tests and exit

- [ ] Add parameter-array and typed-array MIR fixtures that assert direct native accesses on hits and one semantic fallback arm.
- [ ] Cover holes, deleted slots, inherited numeric getters, own accessors, prototypes changed between calls, host/virtual arrays, frozen and nonextensible arrays, negative/fractional/NaN/infinite indices, and keys with observable conversion.
- [ ] Cover Uint8 wraparound (`257 → 1`, negatives, fractions), Float64 ±0/NaN/subnormal values, wrong typed-array kinds, detach/resize during RHS/key effects, and buffer/view offsets.
- [ ] Force GC with a previously loaded scalar/reference live across the next call, and exercise representation changes from numeric to tagged elements.

**Targets:** `fft`, `sieve`, `primes`, `navier_stokes`, `spectralnorm`, `matmul`, `array1`, and `text_search`. The last searches arrays of character codes passed as parameters; string-search leaf tuning is not a substitute. Show fewer dynamic index helpers on the admitted loops, then a paired release benefit. Protect control rows even when the new guards miss repeatedly.

## 11. T12-6 — Ordinary own-field access

**Primary code:** `js_named_fast_receiver_map`, `js_named_fast_lookup`, `js_get_name_id`/`js_set_name_id` in [js_runtime.cpp](../../lambda/js/js_runtime.cpp); predicted-field lowering in [js_mir_expression_lowering.cpp](../../lambda/js/js_mir_expression_lowering.cpp); [mir_shape_candidates.hpp](../../lambda/runtime/mir_shape_candidates.hpp).

- [ ] Measure actual named-field hit/miss reasons before extending shape prediction. Reuse NameId/TypeMap lookup already present; do not optimize a nonexistent per-hit key-string construction.
- [ ] Factor the ordinary own default-data-slot proof so C helpers and MIR agree on receiver kind, shape, descriptor/deletion state, byte offset and value carrier.
- [ ] Make the ordinary non-host hit avoid host-dynamic preparation and global/DOM synchronization hooks. Prove the receiver is outside those categories; globals, DOM wrappers and host objects retain their current hooks.
- [ ] Extend `MirFieldAccessPlan` stores beyond the current float-only admission using existing owned-field writers for supported carriers. An incompatible shape/value or ownership transition takes the current NameId kernel.
- [ ] Keep source-predicted shapes immutable and tied to their layout owner. Do not retain a runtime receiver or context-specific object at a code-baked address.

**Tests:** own versus inherited fields, descriptor/accessor changes, deletion/re-add, field-type changes, prototype changes, same-named fields on unrelated shapes, global-binding synchronization, DOM event-handler properties, setters/proxies and retained scalar/reference values under GC.

**Targets:** measured object-heavy rows such as `microdiff`, AWFY class workloads and `raytrace3d`, as confirmed by the census. `binarytrees`/`gcbench` are required controls. The phase passes only if hit paths are measurably simpler without breaking host/global semantics; replacing shaped objects with MVP hash objects is outside scope.

## 12. T12-7 — Select the smallest correct callable entry

**Primary code:** function/call analysis and direct lowering, `js_call_kernel`/`js_function_select_body_entry` in [js_runtime.cpp](../../lambda/js/js_runtime.cpp), [js_runtime_function.cpp](../../lambda/js/js_runtime_function.cpp), existing builtin/callable metadata.

- [ ] Inventory activation requirements on current function-owned facts: receiver binding, arguments, new.target, module/realm switch, with-chain, private/super state, generator/async and source/error state.
- [ ] Reuse existing native and direct boxed entries for admitted callees. Extend finalization/selection only for a demonstrated missing case; do not copy the general call kernel.
- [ ] Reuse caller-rooted actual spans and existing result ownership. Eliminate repeated argument-root preparation only when the caller has fulfilled that same contract.
- [ ] For a dynamic/method call, perform Get once, evaluate arguments once, preserve the selected callee/receiver, then guard actual callable capability/identity and any remaining receiver facts. An argument that changes the property's value does not retroactively change the callee already selected.
- [ ] A miss calls that selected value through the existing dynamic entry. It must not repeat Get, select by method name, or reread a modified property.
- [ ] Resolve reusable metadata at function creation/finalization. No per-site mutable callee cache, prototype epoch cache or feedback vector is added.

**Tests:** a getter returning alternating functions, a getter that throws, an argument replacing the method, replaced `this.method`, overridden built-ins, bound functions/proxies, strict/sloppy/arrow this, extra actuals, arguments, eval/with capture, cross-module/realm functions, new.target/derived constructors and error unwinding. Unsupported activation shapes keep the full path.

**Exit:** an admitted simple call avoids the identified redundant activation work while all observable Get/Call ordering remains intact. Compare against the current direct-call path, not against an assumed always-dynamic baseline. Numeric recursion gains belong to T12-2/3 unless this phase shows an additional independent reduction.

## 13. T12-8 — Built-in bulk RegExp execution

**Primary code:** `js_regexp_symbol_match`, `js_regexp_symbol_replace`, `js_regexp_exec_dispatch`, existing string match/replace loops and regex internal metadata in [js_runtime.cpp](../../lambda/js/js_runtime.cpp); current matcher/router interfaces.

### 13.1 Admission

Start with primitive string input, a genuine ordinary RegExp, the actual selected built-in operation and builtin exec, supported immutable pattern/flag facts, ordinary relevant descriptors, and a writable lastIndex. For replace, initially require a primitive replacement string and no user callback. Existing slow cases, including overridden accessors/methods, remain on the current protocol.

Place admission before the first operation whose observable effects would be skipped or repeated. Prove known builtin accessors through existing shape/callable metadata rather than invoking arbitrary getters as a test. If a later miss is possible after an observable prefix, use the existing continuation with already evaluated values; never restart the builtin and replay its effects. Prefer a narrower pre-effect guard over a new continuation framework.

### 13.2 Execution

- [ ] Reuse/factor the current bulk matching/replacement loops and regex router; both backends already use RE2 where admitted. Do not edit RE2 or replace the full ECMAScript compatibility path with it.
- [ ] Eliminate repeated JS exec dispatch and temporary exec-result objects only when the admitted operation does not expose those intermediates.
- [ ] Preserve final result shape, null/no-match behavior, lastIndex reads/writes/final state, empty-match progress, UTF-16 positions, capture/substitution semantics and legacy match state.
- [ ] Keep required output allocation precisely rooted; allocation is still `MAY_GC`. Do not hold invalidated string/array borrows across it.
- [ ] Expand captures, Unicode flags and replacement substitutions only after the basic path passes. Unsupported cases stay explicit fallbacks; no benchmark-pattern whitelist.

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

- [ ] Review all new fast paths for one complete semantic miss and no repeated operand/property/argument effects.
- [ ] Remove superseded scans, duplicated classifiers and temporary tuning switches. Retain useful diagnostics under the existing opt trace and preserve the generic semantic implementation.
- [ ] Complete focused, GC, MIR-ratchet, Lambda and Test262 gates on the final source. Rebuild matching external modules if a shared runtime layout change affects their ABI.
- [ ] Archive final release binary/source provenance and complete 63-row paired/full-suite results. Verify counts, statuses, hashes and output equivalence mechanically.
- [ ] Report full-JS/QuickJS geometric mean, final/control geometric mean, total medians, each suite, each row, regressions, timeouts, memory, compiler time and MIR-size changes.
- [ ] Separate gains attributable to Map storage, native admission, TDZ/root reduction, helper heads, arrays, fields/calls and RegExp using the retained phase archives. Do not sum overlapping percentage gains.
- [ ] Confirm typed/untyped Lambda has no unresolved regression from shared analysis/emitter changes. Keep full-JS control wins such as `binarytrees`, `gcbench`, `array1`, `collatz`, `deriv`, `pidigits` and `pnpoly` visible.
- [ ] Update this plan's checklist, the MVP comparison's follow-up status, and implementation documentation with actual behavior. Do not rewrite historical snapshots to match the final implementation.
- [ ] Record each performance milestone as reached or unmet, with the remaining dominant paths. A successful subset, a green baseline, or a large individual speedup does not close an unmet overall target.

### Completion record to fill during implementation

| Phase | Source revision / patch | Structural evidence | Correctness logs | Exact release A/B | Status / residual |
|---|---|---|---|---|---|
| T12-0 | — | — | — | — | Planned |
| T12-1 | — | — | — | — | Planned |
| T12-2 | — | — | — | — | Planned |
| T12-3 | — | — | — | — | Planned |
| T12-4 | — | — | — | — | Planned |
| T12-5 | — | — | — | — | Planned |
| T12-6 | — | — | — | — | Planned |
| T12-7 | — | — | — | — | Planned |
| T12-8 | — | — | — | — | Planned |
| T12-9 | — | — | — | — | Planned |

Implementation starts with T12-0 and the indexed ordered collection in T12-1. The plan remains open until the corresponding evidence is filled in; this document alone records no completed optimization.
