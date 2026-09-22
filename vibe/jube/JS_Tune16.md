# JS Tune16 — Reduce shared helper and execution-protocol overhead

**Status:** PROPOSED; analysis completed, implementation and performance acceptance open.

**Date:** 2026-09-22.

**Scope:** full LambdaJS, MIR Direct; build on the in-progress [Tune15](JS_Tune15.md), without replacing its unfinished validation gates.

## 1. Decision

Tune16 should make common operations cheaper **after they have already qualified for a fast path**. Increasing admission at a few constructor, array, or numeric sites is insufficient for the requested improvement across the suite.

The leading structural targets are:

1. **Helper/caller boundaries:** repeated Number decoding, generic effect contracts, root publication/reloads, scalar-result ownership work, and completion handling around otherwise cheap operations.
2. **The shared property kernel:** repeated receiver/key classification and fallback traversal, including work on successful ordinary named gets/sets, not just missing shape predictions.
3. **Activation and binding plumbing:** repeated acquisition of `this`, module/name state, argument/return ownership, and context guards in calls that already use a reduced activation.
4. **String/collection materialization and allocation:** measure dispatch, conversion, copying, and allocation separately; optimize shared primitives only where several workloads establish the same cause.

The central change is to separate a proved, non-allocating physical operation from the semantic fallback **at the MIR call boundary**, as well as simplifying the C helper itself. A quick branch inside a `MAY_GC` helper does not retroactively eliminate its caller's safepoint protocol. Conversely, simply changing the helper's effect declaration would be unsound.

This is a proposal to improve the existing runtime, not a private-value/heap rewrite or a return to inline caches. Reuse the shared emitter, `Item`, `JsPropertyLane`, `NameId`, `TypeMap`, function capability metadata, and precise roots. Authority: **D1.2v2, D1.3v3, D1.5v2, D4.6.1v3–D4.6.2v2, D5.3.1–D5.3.5, D8.4.1v2–D8.4.3v2** in [Formal Design](../../doc/Lambda_Formal_Design.md). No normative ruling changes are proposed.

## 2. Evidence and its limits

### 2.1 The complete-suite problem is broader than a few slow scripts

The latest 63-row snapshot inspected here is [Result52](../../test/benchmark/benchmark_results_v52.json), not a fresh matrix of the latest Tune15 executable. QuickJS has 63/63 `ok` rows; LambdaJS has **62 `ok` and one `partial_ok`** (`text/hyphen`: one `exit_-9`, two successful samples, in both execution and end-to-end columns). The cause of that killed sample is not established here. Thus this is a full-population snapshot with a failed sample, **not a clean acceptance baseline**. Ratios below use its recorded values, including that partial row, and are descriptive **LambdaJS / QuickJS** ratios; lower is better.

| Result52 population | Rows | Execution-time geometric ratio |
|---|---:|---:|
| R7RS | 10 | 0.156 |
| AWFY | 14 | 2.380 |
| BENG | 8 | 2.488 |
| KOSTYA | 7 | 1.050 |
| LARCENY | 11 | 0.609 |
| Text | 7 | 2.141 |
| JetStream | 6 | 2.576 |
| All rows | 63 | **1.113** |

LambdaJS wins only **24/63** execution-time rows using the recorded values. Its geometric end-to-end ratio is **2.592**, and its ratio of summed execution times is **1.463**. These are different measures: the sum weights long-running workloads; the geometric mean gives each row equal weight. Do not substitute one for another. The evidence JSON separately records the 62-clean-row sensitivity calculation; excluding a failure is not a way to pass the fixed-population acceptance gate.

Examples of the remaining gaps in that snapshot are `microdiff` 8.01x, `hashmap` 4.87x, `bounce` 4.85x, `fast_diff` 3.40x, and `base64` 3.08x. This distribution points beyond one numeric loop: object protocols, mixed values, calls, strings, and short-script startup all matter. Snapshot ratios identify priorities, not the causal effect of a particular Tune15 patch.

A twofold improvement on one of 63 equally weighted rows improves the aggregate geometric mean by only about **1.1%**. A 20% time reduction across 25 rows improves it by about **8.5%**. Broad coverage must therefore be an explicit acceptance criterion, not an inference from a spectacular individual result.

### 2.2 Fresh cross-suite helper census

A new single-process-per-row diagnostic survey ran 16 canonical workloads across all seven suites using the existing optimized profile executable. All 16 exited zero. Sources came from `run_benchmarks.build_benchmark_list`, using AWFY bundles and generated JetStream wrappers as the benchmark runner does.

The durable [structural census](../../test/benchmark/js_tune16/structural_census_20260922.json) records source/binary hashes, nonzero counters, output evidence, Result52 calculations, and a separately captured release MIR excerpt. This is **not** a new performance result or cross-engine correctness certification.

| Workload | Number arithmetic head hits | Number comparison head hits | Named get/set probes | Reduced-activation calls |
|---|---:|---:|---:|---:|
| r7rs/fib | 1 | 0 | 5 | 0 |
| r7rs/fft | 20,494 | 0 | 5 | 0 |
| awfy/bounce | 12,514 | 20,000 | 54,686 | 5,401 |
| awfy/richards | 524,201 | 272,000 | 10,490,455 | 2,265,501 |
| awfy/deltablue | 556,297 | 443,650 | 3,334,900 | 779,768 |
| awfy/havlak | 14,278,326 | 12,570,827 | 52,295,421 | 24,159,104 |
| beng/knucleotide | 105,011 | 35,227 | 35,241 | 76 |
| beng/revcomp | 169 | 10,521 | 10,527 | 0 |
| kostya/base64 | 3,333,701 | 333,600 | 107 | 0 |
| kostya/levenshtein | 9 | 1,233,023 | 18 | 0 |
| larceny/gcbench | 6,422,543 | 0 | 12 | 0 |
| text/fast_diff | 11,400,705 | 8,419,072 | 1,178,637 | 1,536 |
| text/microdiff | 88,065 | 25,088 | 190,474 | 45,056 |
| text/log_pipeline | 20,938,381 | 37,204,740 | 42,092,166 | 0 |
| jetstream/splay | 744,451 | 650,742 | 3,156,749 | 0 |
| jetstream/hashmap | 2,446,926 | 442,181 | 8,479,088 | 1 |

Interpretation and caveats:

- These are **whole-process events**, including setup and each script's own repetitions. They are not normalized per benchmark iteration, and event counts are not CPU percentages.
- The numeric columns count existing runtime Number heads, not all arithmetic. `fib` is a useful control: its hot recursive numeric work largely avoids these counters already. This does not promise another broad speedup in workloads that are already native.
- Named probes include reads **and writes**, and include array/string virtual lengths. Richards has 10,275,652 hits out of 10,490,455 probes, with no length hits. Thus its problem cannot be explained merely by a low named-fast-path hit rate. Havlak has 50,315,057 named hits; 5,581,626 are array-length hits. Log_pipeline has 42,092,162 hits and only 24,000 array-length hits.
- By contrast, 1,178,624 of fast_diff's 1,178,626 named hits are string lengths. Its numeric/string costs deserve a different attribution from Richards' ordinary properties.
- Reduced calls use `mir_light_call + mir_this_call`. The corresponding `*_direct_activation` counters describe the same executions; do not add them again. Zero here does **not** mean no function calls: native, generic, and built-in routes are not fully covered by these counters.
- `mir_number_admitted`, `mir_dense_index_admitted`, and similar counters are **compiler-site counts**, not execution counts. They cannot measure the percentage of hot operations eliminated.
- Havlak additionally records 2,395,949 deferred function finalizations; microdiff records 28,674. These are reasons to investigate function-object construction, not proof that GC dominates either workload.
- Log_pipeline records 16,560,000 ASCII searches, 28,800,000 ASCII slices, and 2,160,000 ASCII splits. Levenshtein records 411,004 ASCII character accesses. Optimizing string dispatch/materialization can therefore have more reach than replacing one search algorithm.

The Tune15 text_search trace also illustrates the residual problem after a successful narrow optimization. `temp/js_tune15_text_search_mir_array_length.trace` records 36,864 remaining runtime array-length hits, but **687,187,703 Number arithmetic hits and 881,713,603 Number comparison hits**. The latter counts remain large even after moving admitted `.length` operations into MIR. This older trace is contextual, separate from the fresh 16-row census; no timing ratio is inferred from it.

### 2.3 Fresh generated MIR confirms caller-side work

A separate release run captured finalized Richards MIR with logging enabled for the dump. The initial census used `--no-log`, which intentionally suppresses optional MIR dumps; it did not produce MIR despite the environment setting. No missing dump is treated as an empty or optimized-away program.

The release artifact has 24,989 textual MIR lines and 2,886 static call instructions. Examples of static targets are 253 `lambda_active_module_name_id`, 188 `lambda_active_module_name_item`, 155 `lambda_item_adopt_scalar_home`, 133 `js_get_this`, 103 `lambda_active_module_var_at`, 75 `js_set_name_id`, and 74 `js_get_name_id`. These include initialization, cold arms, multiple bodies, and adapters: **not dynamic call frequencies or native-code size**.

The preserved excerpt around one `js_get_name_id` contains:

1. A number-stack watermark snapshot and a scalar-home address.
2. The property helper call.
3. Reloads of live rooted values.
4. Returned-representation classification, with a conditional `lambda_item_adopt_scalar_home` call.
5. Watermark restoration and explicit completion routing.

Another excerpt shows a numeric value encoded as an `Item`, roots published, and `js_cmp_raw` called. Some of this work disappears for already-specialized sites, and MIR's native backend may further optimize the artifact. Nevertheless, it directly verifies that a cheap semantic hit can retain surrounding protocol work. T16-0 must measure the remaining machine-level cost before assigning percentage savings.

### 2.4 Provenance

- Inspected HEAD: `10fddc11245fd0322d489aa7013b61ee32501669`, with existing dirty Tune15 changes. HEAD alone does not identify the executable's complete source state.
- Current release SHA-256: `f4c4288ff7326d79990ea573e5573bc5c19d5287d2cdcdc5617a1d4576886114`; also archived as `test/benchmark/exe/lambda-tune15-mir-array-length-f4c4288f`.
- Profile SHA-256: `99a0dc293faacdd8d3d139e2dfbc76c29b3a458d783eea63c7db70cd7c95df9a`.
- Existing binaries were used, not rebuilt in this analysis. The evidence JSON records current source hashes separately; it does not claim a freshly proved source-to-binary build manifest.
- Result52 predates the latest `.length` work and retains the partial hyphen row described above. Do not combine its QuickJS denominators with new profile timings to manufacture a current aggregate.
- No fresh full benchmark, baseline suite, or isolated optimization A/B was run for this documentation task. Instrumented durations are deliberately excluded from the proposal's performance evidence.

## 3. What the MVP comparison actually teaches

### 3.1 Current release comparison is unavailable

The live command `./lambda.exe js --runtime=mvp test/benchmark/r7rs/fib2.js` exits **9** with `MVP runtime is available only in debug builds`. No debug timing was substituted. A future release MVP comparator requires its own explicit build/validation work; it is not a prerequisite for making full-JS improvements or a reason to weaken semantics.

The earlier [MVP/full comparison](JS_MVP_Full_Runtime_Comparison.md) supplies historical experiments, not a current performance baseline. Its numeric-coercion, replaced-method, and typed-array probes also show why benchmark sufficiency is not semantic parity. The source comparison below was refreshed against this worktree.

| Operation | Full LambdaJS now | MVP implementation | Transferable lesson |
|---|---|---|---|
| Number addition/comparison | Number heads already exist; classified values then pass through general extraction/result machinery; open MIR calls retain declared effects | `mvp_op_add` checks Number tags, extracts doubles, calculates, and encodes immediately | Minimize classification/extraction and expose a truthful pure hit path to MIR |
| Named property | `NameId` shape lookup already exists; general named path resolves a `NameRef`, classifies receiver/storage, checks descriptors, then falls back to the reference kernel | `mvp_op_named_member_get_with_hash` directly uses a hashed property lookup; setters directly update an existing data property when admitted | Carry canonical identity and dispatch once; do not replace shaped objects with MVP objects |
| Indexed data | Packed/dense/typed paths already exist alongside JS descriptor/prototype/buffer semantics | `mvp_array_get` is a compact bounds check and value load | Put the complete cheap guard next to the physical load; preserve the full miss path |
| Calls | Native entries and light/`this` reduced activations already exist; contextual guards, roots, state installation, and result ownership remain | Numeric entries use direct scalar ABIs; generic `mvp_op_call_values` still copies/root-pins arguments and calls through a function object | Optimize the remaining activation protocol, not merely entry selection; generic MVP calls are not free |
| Values and GC | Most ordinary doubles are already inline `Item`s; out-of-band scalars need explicit ownership; shared precise roots use liveness planning | Compact private value representation and heap/root interface; generic MIR pins temporary values | Avoid repeated encode/decode and unnecessary ownership transfers; do not claim every full-JS Number allocates |
| Error handling | Explicit returned completions under the helper catalog contract | Generic MVP MIR reads `MvpExecution::error` after operations | This is not a pattern to copy: **D1.4v4 / D8.4.3v2** require explicit completion, not a pending-error side channel |

Sources: [full value helpers](../../lambda/js/js_runtime_value.cpp), [full property/call runtime](../../lambda/js/js_runtime.cpp), [MVP runtime](../../lambda/js/mvp/mvp_runtime.cpp), [MVP heap](../../lambda/js/mvp/mvp_heap.cpp), [MVP generic lowering](../../lambda/js/mvp/mvp_generic_mir.cpp).

### 3.2 Do not re-propose already implemented work

- The old Map/Set quadratic order-list update is not the current implementation. `JsCollectionEntry` now indexes a stable `JsCollectionOrderNode`. Investigate remaining collection dispatch/allocation rather than claiming that old algorithm is still the explanation.
- `js_add` already attempts its Number pair **before** opening its generic root frame. The historical recommendation to add that head has been implemented.
- Ordinary `js_object_meta` already reads `TypeMap::js_meta` before walking exotic cases. The task is to avoid **repeated** classification, not add another identical ordinary-object head.
- Named lookup already hashes and compares `NameId`; resolving its `NameRef` does not itself mean allocating a new string or rehashing all its bytes on every hit.
- Module NameIds already have compiler-side register reuse in `jm_module_name_id_at_index`; generated catalog IDs can already be constants. Extend valid dominance/activation reuse, not a second name cache.
- Root liveness/dirty-write planning, caller-rooted argument paths, deferred function metadata, and reduced activations already exist. Measure their residual cost and extend the shared mechanisms.

MVP's useful architectural contrast is **less work between an already-known physical value and its operation**. Its historical speed advantage cannot be assigned entirely to value representation, GC, or helper dispatch, especially where the programs execute different semantics.

## 4. Root-cause analysis

### 4.1 Successful Number heads still traverse generic machinery

`js_numeric_number_pair` rejects Symbols/non-Numbers, obtains type information, calls `js_get_number` twice, dispatches the operator, and calls `js_make_number`. `js_get_number` independently classifies the value again and supports representations and coercing inputs that the head has already ruled out. `js_make_number` uses `flt2it`; most values are inline, but exceptional bit patterns still require the established scalar storage path.

`js_relational_number_pair` repeats Number extraction and creates a boolean `Item`; `js_cmp_raw` consumes that result as a raw boolean. C optimization may fold some of this internal work. A separately compiled C helper still represents a call boundary to generated MIR, and current evidence does not establish how much of each source-level redundancy survives native optimization.

The import catalog's `JIT_IMPORT_JS_NUMBER_BINARY` retains `MAY_GC` and re-entry capability because its fallback can coerce, allocate, or invoke JS. `JIT_IMPORT_RAW_SCALAR_PRESERVES` also retains `MAY_GC`; raw return type is not proof of a no-GC operation. `em_before_resolved_call` and `em_call_with_args_policy` use those contracts to manage live arguments and possible scalar results. Exact work depends on the import's ownership flags and live values; there is no universal fixed tax on every call.

**Root cause:** a fast semantic branch is hidden inside an effectful operation boundary, with incomplete propagation of physical Number facts. The general fallback's contract remains correct, but it applies to the hit as well.

**Broad opportunity:** Number pairs from ordinary fields, mixed arrays, string lengths, counters, and dynamic arguments can benefit even when whole-function numeric inference cannot admit a native variant. Existing native numeric workloads form the no-regression controls.

### 4.2 A property hit is still a layered transaction

`js_get_name_id` first handles selected virtual lengths. Its general path then resolves the ID through NamePool, determines receiver handling, and enters `js_named_fast_lookup`. That helper checks the receiver/map, shape, storage bounds, descriptor flags, constructor reservation, and result sentinels. On miss, `js_get_reference` / `js_get_key_core` handle the complete operation, including receiver/prototype and exotic semantics.

All these distinctions are real. The avoidable part is rediscovering canonical key, receiver kind, ordinary metadata, or own-lookup results as control passes through helpers. Reads and writes also have different descriptor rules; they must not be merged into an unsafe universal slot accessor. A read through `_map_read_field` may need scalar materialization, so an ordinary own-property hit is not automatically a `NO_GC`, scalar-stable operation.

**Root cause:** repeated protocol/classification across the named facade, ordinary lookup, and semantic fallback, plus a generic result contract even for cheaply representable values. Richards' high hit count makes reducing hit cost at least as important as increasing hit rate.

**Broad opportunity:** ordinary object graphs, linked structures, counters, method properties, enumeration, and built-in internals, regardless of whether a source-local constructor prediction succeeded.

### 4.3 Reduced activations retain meaningful per-call work

The light and `this` entries check function layout/capability, active runtime, `with` state, VM stack source, context, module state, and home global. The light body roots its callee and conditionally roots/owns arguments. The `this` body additionally roots/saves the old receiver and method home, installs new state, invokes the body, restores state, and resolves/adopts the result.

Meanwhile `jm_emit_current_this` generally emits `js_get_this`, even though the compiled body ABI receives a receiver. This is necessary for existing generic/lexical/derived-constructor cases, but need not remain the only representation for a proved ordinary body.

**Root cause:** entry selection has been optimized, but the body and surrounding ABI still communicate some activation facts through shared state and reacquire them. The fresh census establishes millions of executions of the reduced path, not that its guard miss rate is the principal problem.

**Broad opportunity:** several AWFY workloads and callback-heavy code. This is not automatically a benefit for every suite, and a new call variant must earn its code-size and compile-time cost.

### 4.4 Binding/name scaffolding and cold compilation remain separate costs

The Richards MIR contains many module/name/`this`/TDZ helper sites. `lambda_active_module_name_id` is already a short accessor, but a short C helper still has a boundary; `lambda_active_module_name_item` also resolves the spelling. Avoiding these operations when only canonical identity is needed can reduce both execution scaffolding and generated code.

Not all repeated loads are redundant: global values can change, live module bindings must remain live, direct `eval` can introduce bindings, and a TDZ check may throw. Existing name-register reuse must obey dominance and entry-context ownership. An initialized local binding is different from an unresolved/global lookup.

**Root cause hypothesis:** unnecessary reacquisition of immutable activation/name facts and incomplete binding facts can multiply property/call costs. Static site counts alone do not establish hotness. T16-0 must instrument/sample these paths before substantial compiler restructuring.

The 2.592x end-to-end snapshot ratio separately requires phase timing: source admission, parsing/build/binding, JS analysis, MIR construction/finalization, machine-code generation, execution, and shutdown. Do not attribute all process overhead to compilation or present warm-body gains as a startup fix.

## 5. Implementation work packages

### T16-0 — Establish a shared-cost and breadth baseline

**First deliverable; no speculative runtime changes.**

- Freeze the latest accepted Tune15 release executable, patch/source manifest, dependencies, wrappers, and inputs. Capture a fresh complete baseline before comparing Tune16 changes. Preserve Result52 as historical context, not the new control.
- Extend existing diagnostic infrastructure rather than creating a competing profiler. Count named read/write routes separately; ordinary own hits, prototype hits, absent keys, accessors/exotics, scalar materialization, metadata probes, and generic fallback entries. Add missing generic/native/built-in call coverage and binding/module helper counts.
- For numeric heads, distinguish representation combinations, coercing misses, inline versus out-of-band results, and source/helper family. Keep static admission events separate from dynamic executions.
- Collect generated-MIR safepoint/root-store/reload and scalar-home counts using existing emitter analysis. Sample the **main executing thread** with the exact release binary; do not count idle helper threads as runtime work. Record instrumentation overhead in an independent diagnostic comparison.
- Measure allocation counts/bytes by object kind, number-home growth, GC frequency/time, peak memory, and phase times. Current traces do not establish GC as the dominant cost.
- Use the 16-row census as an initial stratified panel, then cover all 63 before accepting a breadth claim. Include existing native numeric winners and miss-heavy semantic cases as controls.

**Exit evidence:** a family-by-workload cost map that separates operation frequency, sampled CPU, generated scaffolding, allocation, and cold startup. Rank work by measured shared cost and coverage; do not divide unrelated whole-process event counts into an invented CPU percentage.

### T16-1 — Simplify Number helpers and expose truthful pure hit paths

**Priority: first broad runtime/MIR experiment.** Extend `js_runtime_value.cpp`, the existing JS numeric lowering, shared physical-value plans, and the import catalog.

1. Reuse or extract one Number-only decoder that returns classification and the native value together. Optimize the common inline Number representation first; support other already-admitted Number storage without repeating general coercion logic. Symbols and BigInts must remain distinct. Share the classification plan with lowering; do not copy a new switch into each operator.
2. Feed a native boolean directly to raw comparison consumers. Keep the existing explicit error/coercion path for semantic comparisons. Inspect generated native code before crediting a source-level simplification.
3. At an open numeric operation, evaluate operands once, then emit an immutable Number-pair guard and native operation on success, using existing `MirValue` / numeric-plan machinery. The guard is applicable to runtime boxed values; it does not require whole-function native admission or profile feedback.
4. Encode the result through an audited inline/owned representation path. If a result cannot be represented without the required allocation/ownership work, use the established completion-capable slow path. Include NaN, signed zero, infinities, tiny/subnormal values, and integer-width boundaries in this decision.
5. Place the generic helper call, precise root publication, and allocating result protocol on the **miss edge**, not before the guard. Propagate native facts only along valid dominated uses; a phi/join must accurately represent both arms.

An alternative small `NO_GC` try-leaf is acceptable where MIR duplication/code size would be excessive, but its miss must be distinct from every valid JS value. Use the project's declared status/companion conventions; do not invent an undefined/NaN/error sentinel collision or a new universal ABI. No getters/coercions may occur before such a miss, so fallback cannot replay observable work.

Audit the entire transitive implementation before declaring `NO_GC`, non-reentrant, borrowed arguments, scalar-stable results, or `PRESERVES`. Never relabel the whole coercing helper as pure. Reuse existing emitter root/ownership planning (**D5.3.2–D5.3.4, D8.4.3v2**).

**Proof and measurement:** helper-only, MIR-only, and cumulative ablations; Number-heavy mixed/object workloads from at least three suites; native numeric controls; generated fast arms without unnecessary helper/safepoint/home work. Report total helper entries removed, not merely new static admissions.

### T16-2 — One canonical property lookup and a cheaper ordinary hit

**Priority: alongside T16-1; expected reach across object-heavy suites.** Extend `js_props.h`, `js_runtime.cpp`, `js_object_meta.cpp`, existing shape helpers, and property lowering.

1. Carry existing `JsPropertyLane` / canonical `NameId` and the lookup hash through ordinary operations. Resolve observable spelling only when an id-less Input seam, exotic receiver, reflection, diagnostics, or another semantic consumer needs it. Preserve `PropertyKeySpec` / per-context NameId linking; do not embed arbitrary dynamic IDs or treat equal spelling as Symbol identity (**D4.6.1v3–D4.6.2v2**).
2. Preserve the existing shape hash-table contract. It currently uses a key hash as well as NameId equality; an ID-only path must obtain the matching hash from linked key metadata or another audited existing source. Substituting a different hash without changing table construction would be incorrect. Do not create a second property store or per-site lookup cache.
3. Make ordinary receiver classification and own lookup produce a shared internal result: present data, accessor, absent/deleted/reserved, or exotic. Pass this result to the next semantic step where it remains valid instead of repeating the same lookup. Reuse existing descriptor/operation structures; retain public descriptor materialization at observable APIs only.
4. Admit a physical present-own-data leaf with a complete guard and an audited stable result. Start with storage that can actually be read without allocating or borrowing a mutable scalar payload. Wide numeric materialization and all unsupported layouts stay on the established path. Set operations retain extensibility, writability, receiver, descriptor, reservation-publication, and destination-ownership rules.
5. Consolidate repeated checks in prototype traversal and computed-key consumers. Perform `ToPropertyKey` exactly once. Preserve the original receiver for accessors and stop at proxies/exotics. If user code runs, invalidate/reacquire any potentially changed shape/slot before reuse; never carry a raw shape/slot through re-entry without its lifetime proof.

This work must not bypass global/Window/DOM hooks, array virtual properties, function poison-pill/lazy metadata, or constructor-reserved absence. It improves the shared kernel even when literal/constructor prediction fails. Existing predicted shape paths remain users of that kernel, not parallel semantic implementations (**D8.4.1v2**).

**Proof and measurement:** named get and set separately; computed-name and prototype routes; Richards, DeltaBlue, Havlak, microdiff, hashmap, splay, and log_pipeline; own-hit cost, fallbacks, metadata/NamePool calls, scalar materialization, and native-code size. Do not count virtual string-length wins as ordinary-object coverage.

### T16-3 — Carry immutable binding and activation facts farther

**Priority: after T16-0 establishes hotness; before adding more specialized built-ins.** Extend existing binding analysis, `JsMirNameCache`, and shared module/name accessors.

- Reuse linked property identity within a valid function/region rather than emitting repeated calls or constructing a name `Item` for an operation that only needs its lane. Existing compiler register reuse is the starting point.
- Carry an explicit, correctly owned module/name-table base where the activation contract permits it. Prove stability across nested calls, module entry, eval, suspension, and table growth; otherwise reload. Context-specific pointers/IDs must not enter persisted MIR as arbitrary constants.
- Remove dominated TDZ/unresolved-binding checks only from bindings whose identity and initialization are statically proved. A dynamic global or `with`/eval-sensitive reference must retain the semantic resolver. Never hoist a mutable global value just because its slot or name is stable.
- Where the semantic operation already performs a coercibility check, avoid a redundant pre-check only if evaluation order, error identity, and getter/key side effects are unchanged. A combined check is not permission to delay a required early error past argument evaluation.

**Proof and measurement:** hot helper counts and finalized MIR before/after on object, string, and module/eval controls; compare compile time and code size too. If savings are only cold setup in one bundle, report that narrow result and do not count it as broad execution progress.

### T16-4 — Reduce protocol inside already-admitted calls

**Priority: driven by call samples; distinct from a new callee-prediction experiment.** Extend existing function capability analysis and current light/`this` bodies.

1. Share common light/`this` admission predicates rather than adding more near-identical guards. Preserve context/module/realm, `with`, stack/source, and stack-limit semantics.
2. For an explicitly proved ordinary compiled body, consume its receiver operand directly instead of repeatedly calling `js_get_this`. Model whether the body or any reachable operation needs ambient `this`, method home, `super`, private fields, arguments, new.target, eval, or stack metadata. Unsupported/derived/lexical/suspended cases stay on the existing path.
3. Elide installation/restoration of a particular ambient field only when the body and any callees that can observe it are covered by the capability proof. Passing a receiver directly does not by itself justify leaving shared state stale for built-ins or re-entrant calls.
4. Reuse caller-owned argument spans and explicit return ownership through the existing ABI. Audit duplicate scalar-home adoption and no-op RootSpan work, but retain callee/receiver roots across collection and borrowed-value capture. Dynamic `Item* + argc` boundaries remain supported; do not expand them into all direct calls (**D8.4.2v2**).
5. Separate immutable code/source/capability metadata from per-execution closure state using existing structures. If allocation profiling shows redundant initialization, remove it. Fresh observable function objects, captured environments, prototypes, and identity must not be shared merely because their code is identical.

Method optimization must still evaluate base/key and observable Get once, then arguments in order; a miss calls the **already selected callee** with the original receiver/actuals. A guard must not repeat Get or assume `this.method` cannot be replaced because no subclass is syntactically visible. No method/callee cache is permitted (**D8.4.1v2**).

**Proof and measurement:** at least Richards, DeltaBlue, Havlak, and callback/mutation controls; distinguish entry-guard savings, ambient-state savings, argument ownership, and allocation. A high reduced-call count alone is not justification for another entry variant.

### T16-5 — Shared string, collection, and allocation costs

**Conditional package, not permission for another collection of benchmark-specific recognizers.**

- Attribute built-in time to property/call dispatch, coercion, UTF-16/index conversion, search/copy kernels, result construction, and GC. Reuse the current ASCII/Unicode and collection kernels; keep identical Unicode/coercion/override behavior.
- Where current callable identity and receiver/storage guards prove the built-in operation, reach an existing physical kernel with already-converted arguments. Preserve lookup and argument-effect ordering; do not select an intrinsic solely by spelling.
- Investigate repeated length/character extraction and short substring/string-result construction across base64, levenshtein, fast_diff, knucleotide, and log_pipeline. A faster algorithm is useful, but cannot substitute for reducing shared per-operation overhead when that is the measured cause.
- For collections, inspect hashing, entry ownership, method dispatch, iterator maintenance, and allocation separately. The old insertion-order rescan is already fixed. Do not replace correct Map semantics with a plain object table.
- Treat closure/object/array allocation as a separate measured axis. Existing container layouts, precise roots, and shared allocation/accounting remain the default. No GC threshold adjustment, private allocator, or alternate value encoding is justified by counters alone.

**Admission gate:** the same change must have a demonstrated causal benefit in multiple workloads, preferably more than one suite, with allocation/copy/dispatch evidence matching the claimed mechanism. Otherwise retain it as an explicitly narrow follow-up, outside Tune16's breadth completion claim.

### T16-6 — Cold-start and final breadth closeout

Use T16-0 phase timings to choose only measured startup work: redundant analysis passes, generated adapter/body duplication, excessive helper scaffolding, or an incorrectly keyed/reused existing cache. Reuse the shared MIR lifecycle and cache ownership rules; do not introduce a second compiler backend or persist context-specific pointers (**D1.6, D1.7, D8.5**).

Each added guard/variant must report incremental analysis time, emitted code size, and first-run time. Compile-time growth can erase body savings on short scripts. If startup remains a separate unresolved gap, report it plainly rather than declaring the overall task solved from in-process timings.

## 6. Safety and regression gates

The proposal changes physical execution, not observable JS behavior. Existing runtime behavior and supported ECMAScript cases remain the contract; do not import MVP's benchmark-only assumptions or weaken the Test262 harness.

| Area | Required positive and miss-path tests |
|---|---|
| Number leaf | NaN, +/-0, infinities, subnormal/out-of-band values, mixed numeric representations, overflow boundaries, coercing strings/objects, Symbols/BigInts, throwing coercions, conversion order |
| Properties | Same-spelling distinct Symbols; id-less Input seam; inherited getters/setters; receiver preservation; proxies; deleted/reserved slots; non-default descriptors; non-extensible objects; dynamic globals/DOM; mutation during key/value/argument evaluation |
| Arrays/typed storage | Holes, inherited indices, numeric-looking keys, companion properties, length changes, detached/resized buffers, typed conversion, descriptor transitions |
| Binding/state | TDZ, live module bindings, direct/indirect eval, `with`, recursion, cross-context/module calls, lazy metadata, thrown completions, nested/re-entrant calls |
| Calls/ownership | Bound/spread/callback calls; receiver mutation; method replacement; lexical/derived `this`; super/private state; stack overflow; scalar returns; escaping arguments/closures; suspension where supported |

Run focused normal tests and, for rooting/ownership-sensitive changes, `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1` variants. Run the existing GC-effect/catalog audit and finalized-MIR contracts for newly introduced leaves. Roots must be based on precise lifetime ownership; conservative native-stack scanning remains retired (**D1.5v2, D5.3.1–D5.3.5**).

After each meaningful runtime slice and for cumulative closeout:

```sh
make test-lambda-baseline
make test262-baseline
```

Use a fresh correct build for those gates; retain complete logs and the baseline denominator. Focused optimizer tests do not replace full baseline/Test262 validation. Node baseline is not part of this request. Do not loosen MIR budgets merely to accommodate an optimization; inspect finalized MIR and justify any narrow legitimate update.

## 7. Performance acceptance: improvement must be broad

### 7.1 Measurement procedure

1. Freeze exact release control/candidate binaries, hashes, build options, dirty patch manifest, linked libraries, and all 63 source/input/wrapper hashes. Isolate builds and runs from concurrent edits or benchmark processes. Use `make release`; debug or instrumented binaries never supply performance acceptance numbers.
2. Start with affected rows and negative controls, using interleaved release A/B through [run_paired_benchmarks.py](../../test/benchmark/run_paired_benchmarks.py), `--language js`. Use at least 11 valid pairs for triage and 31 or more for noisy results/regression decisions; retain raw samples and paired confidence intervals. Do not selectively stop when a favorable estimate appears.
3. Run the cumulative candidate across the entire fixed 63-row population, including QuickJS in fresh complete matrices. Record medians, row ratios, suite geometric means, overall geometric mean, sum of times, win counts, and end-to-end/compile/memory/code-size changes. Exits, timeouts, wrong outputs, and missing rows remain visible and fail acceptance.
4. Validate stdout/checksums and use independent/post-timing correctness oracles where the benchmark lacks a meaningful output. A timing-only JetStream line is not proof of semantic parity. Keep oracle work outside the timed body and use identical wrappers for both engines.
5. Retain separate ablations for C-helper simplification, MIR hit/miss splitting, property consolidation, binding/state work, and allocation. Cumulative performance alone cannot identify which package caused a regression.
6. Repeat the complete release-vs-QuickJS comparison in two independent sessions. Compare same-source same-parameter runs; no historical MVP/QuickJS denominator mixing. Sequential ResultN reports are observations; causal claims require exact-binary paired replay.

Example full-population LambdaJS A/B command, after the control/candidate archives exist:

```sh
python3 test/benchmark/run_paired_benchmarks.py \
  --language js \
  --control test/benchmark/exe/lambda-tune16-control \
  --candidate test/benchmark/exe/lambda-tune16-candidate \
  --pairs 31 --timeout 180 \
  --output temp/tune16/full_paired.json
```

The paired runner compares Lambda binaries; it is not itself a QuickJS matrix command. Use the canonical standard runner with QuickJS explicitly selected for that separate gate. Do not restore the removed C2MIR backend; any legacy archived comparator column is not an implementation dependency.

### 7.2 Proposed success thresholds

These are engineering goals to test, **not predicted speedups**. Set them before candidate measurement and do not lower them after seeing favorable subsets.

- **Overall execution:** full-population candidate/control geometric ratio at most **0.80**, with paired confidence evidence excluding no improvement. The control is the frozen latest Tune15 release, not Result47/52.
- **Breadth:** at least **32/63** rows faster; at least **24 rows improve by 10% or more**; at least **five of seven suite geometric means improve by 10% or more**. Report confidence and absolute milliseconds for small/noisy rows, not just a threshold count.
- **Anti-concentration:** after removing the three largest relative gains, the remaining 60-row geometric ratio must still be at most **0.85**. Report gains grouped by semantic family as well as benchmark suite.
- **QuickJS goal:** complete-population execution ratio below **1.0** in both fresh sessions, with a reproducible margin. Also report per-suite gaps and win counts: a geometric win alone does not mean every script is faster. A 0.90 aggregate is a useful stretch margin, not established evidence.
- **Regression guard:** investigate every row whose candidate/control median exceeds **1.05** using exact-archive replay. Do not accept a confirmed material regression hidden by aggregate wins. For sub-millisecond rows, report absolute deltas and measurement resolution rather than silently dropping them.
- **Other costs:** summed execution time must not regress; end-to-end geometric time must not regress beyond measured noise; track worst-row latency, peak memory, GC time, and native-code size. Any tradeoff needs an explicit decision, not a changed denominator.
- **Correctness:** every required baseline, focused miss-path/forced-GC gate, output check, and complete-matrix status must pass.

If the runtime is broadly faster but remains slower than QuickJS, record that as progress with the QuickJS goal open. If only a few rows improve, record those wins but do not mark Tune16's broad-performance objective complete. If a package's measured opportunity disappears, document the evidence and defer that package instead of adding increasingly narrow recognizers.

## 8. Recommended execution order and deliverables

| Order | Deliverable | Stop/decision condition |
|---|---|---|
| 1 | T16-0 frozen baseline, cost map, instrumentation semantics | No causal optimization claims until the cost and population are defined |
| 2 | T16-1 Number decoder plus effect-separated operation, independent ablations | Keep only if release evidence supports savings without losing existing native winners |
| 3 | T16-2 canonical property kernel and truthful ordinary leaf | Prove hit-cost reduction on several object workloads and all relevant miss semantics |
| 4 | T16-3 binding/name facts; T16-4 residual activation protocol | Require hot-path evidence; no new speculative method shortcut merely to increase admissions |
| 5 | T16-5 shared string/allocation work where profiling justifies it | Narrow one-script improvements do not count as breadth completion |
| 6 | T16-6 cold-start accounting, complete paired matrix, two QuickJS sessions | All correctness, regression, and breadth gates; otherwise report the remaining gap |

Deliver a package ledger with source changes, structural counts, focused/full correctness results, release A/B artifacts, negative results/reverts, and remaining misses. Preserve the successful Tune15 work and its unfinished gates; this proposal does not silently declare Tune15 complete.

The expected route to a substantial improvement is **many fewer expensive boundaries per ordinary operation**, across the shared runtime. MVP motivates that direction, while the fresh full-JS census and generated MIR establish where to test it without adopting MVP's semantic shortcuts.
