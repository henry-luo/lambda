# Full LambdaJS performance: lessons from MVP v1

**Version:** 1.0.0  
**Date:** 2026-09-16  
**Status:** historical investigation; its proposal was implemented as Tune12 on 2026-09-16.
**Source baseline:** `f72f7552b027d01829a874ece9a8a591206db722`; the runtime sources match HEAD. MVP sources are the restored v1, before the abandoned Item-alignment experiment.

## 1. Recommendation

Use MVP as a model for **how little work a common operation needs**, then establish the proofs that let full LambdaJS do that work safely on its existing representations. The largest opportunities found here are:

1. **Remove an algorithmic defect in Map/Set bookkeeping.** Full LambdaJS hashes the key, then linearly searches its insertion-order list on every `set`/`add`. MVP's hash index identifies the ordered entry directly. This difference is more fundamental than the cost of an Item tag check.
2. **Extend the existing native numeric function lane.** Full LambdaJS's return inference misses `sum`'s local accumulator and `fib`'s recursive addition. Their generated bodies therefore retain boxed arithmetic, ownership traffic and checks. MVP accepts these whole functions as numeric kernels.
3. **Put cheap, guarded own-element and Number cases before general helper machinery.** Extend these same physical operations into MIR when facts are available, including array parameters and real typed arrays.
4. **Specialize calls and bulk built-ins only after proving the selected operation.** Retain observable property lookup, callable identity, evaluation order, GC ownership and exception behavior.

This is an evolution of the current full engine, including Tune10/11. It requires no second value universe, replacement collector, MVP-to-Item bridge, or copied set of JS semantic helpers.

Authority: **S1.11** requires LambdaJS to retain ECMAScript semantics; **D1.3v3** permits shared physical infrastructure with language-specific admission; **D2.4.1–D2.4.3** separate representation from coercion; **D3.3.2v2** says inference selects an implementation, not a source contract. **D8.4.1v2** allows immutable predicted specialization and a semantic fallback, and prohibits mutable inline caches. This proposal changes no formal ruling.

### 1.1 Implementation outcome (Tune12)

Tune12 implemented the recommended hash-to-order-node collection storage,
bounded Number-native return admission, minimal native frames, guarded Number
and own-element heads, parameter/numeric/typed-array access lowering,
ordinary-field carriers, verified existing callable entries, and admitted bulk
RegExp match/replace. It keeps the full semantic continuation for every
unproved case under **S1.11**, **D5.3.2–D5.3.5**, **D6.2.2v2**, and
**D8.4.1v2–D8.4.3v2**.

The exact-release, interleaved 63-row comparison has equal output in all 189
paired executions and a candidate/control geometric mean of **0.680706x**.
The frozen guarded [control matrix](../../test/benchmark/js_mvp/tune12/control.json)
independently validates all 63 rows at **5.082332x** LambdaJS/QuickJS; the
complete fresh [candidate matrix](../../test/benchmark/js_mvp/tune12/final.json)
is valid for all 63 rows at **3.481106x**. The proposed 0.80x full-runtime
milestone is therefore not met. The implementation and its residuals are recorded in
[JS Tune12](JS_Tune12.md); the raw [paired A/B](../../test/benchmark/js_mvp/tune12/final_paired.json),
[RSS capture](../../test/benchmark/js_mvp/tune12/final_memory.json) are kept
separately from this historical study.

## 2. Evidence and measurement limits

### 2.1 The complete historical population

The archived [v1 acceptance results](../../test/benchmark/js_mvp/JS_MVP_Release_Acceptance_20260915.md) and [full LambdaJS Result45](../../test/benchmark/Overall_Result45.md) cover the same 63 standard workloads:

| Metric | Historical result |
|---|---:|
| MVP v1 / QuickJS geometric mean, session 2 | 0.691519x |
| Full LambdaJS / QuickJS geometric mean, Result45 | 2.588162x |
| MVP v1 / full LambdaJS, directly comparing their archived medians | 0.312630x |
| Rows faster in MVP v1 | 56 / 63 |

These are separate sessions and binaries. The third number describes those snapshots; it does not isolate a design change. In particular, dividing the first two ratios is not the same calculation: their QuickJS samples differ.

Full LambdaJS wins the historical `binarytrees`, `pidigits`, `collatz`, `array1`, `deriv`, `gcbench` and `pnpoly` rows. A universal claim that the private MVP heap or value format is faster is already contradicted by the results.

### 2.2 Fresh comparison of both backends in one release binary

The executable present at the start of this investigation still contained the abandoned v2 build. It was rebuilt with `make release` before taking these measurements. The manifest verifier passed for all 63 sources; the focused run selected 12 rows and made three isolated process runs per engine/row.

Release SHA-256: `b86b42f976f3d7ecce3907e4db2bd732c71a83d9a6377757a96964e69a8d7961`. Platform: Darwin arm64. QuickJS: 2025-09-13. Both LambdaJS backends use this same executable.

| Workload | MVP ms | Full JS ms | QuickJS ms | Full / MVP |
|---|---:|---:|---:|---:|
| r7rs/fib | 2.083 | 28.350 | 31.602 | 13.61x |
| r7rs/fibfp | 2.092 | 26.563 | 30.959 | 12.70x |
| r7rs/sum | 1.266 | 49.914 | 49.702 | 39.43x |
| r7rs/sumfp | 0.123 | 4.810 | 5.914 | 39.11x |
| r7rs/fft | 1.698 | 45.088 | 6.349 | 26.55x |
| awfy/sieve | 0.315 | 5.739 | 1.059 | 18.22x |
| beng/binarytrees | 120.943 | 71.887 | 53.406 | 0.59x |
| beng/knucleotide | 18.858 | 289.095 | 14.176 | 15.33x |
| beng/regexredux | 1.123 | 55.165 | 9.345 | 49.12x |
| kostya/primes | 142.427 | 3179.907 | 162.542 | 22.33x |
| larceny/gcbench | 2511.965 | 1351.241 | 959.169 | 0.54x |
| jetstream/navier_stokes | 89.174 | 668.150 | 168.503 | 7.49x |

Greater than 1 in the last column means MVP is faster. All 108 timing samples have runner status `ok`. This is a diagnostic subset, not a new 63-row acceptance result. The runner groups samples by engine; this is not randomized or interleaved optimization A/B evidence. The sub-millisecond rows also warrant caution.

Artifacts:

- [Raw timings and provenance](../../test/benchmark/js_mvp/JS_MVP_Full_Comparison_20260916.json).
- [Runner log](../../test/benchmark/js_mvp/JS_MVP_Full_Comparison_20260916.log).
- [Semantic probe sources/results, finalized full-JS MIR, and Map scaling samples](../../test/benchmark/js_mvp/JS_MVP_Full_Comparison_Evidence_20260916.json).

The evidence archive distinguishes static MIR call sites from dynamic executions. No optimization was implemented or ablated, so this study cannot assign a percentage of a workload's time to each proposed fix. Test262 and the Lambda baseline were not rerun for this documentation-only change. Some diagnostic processes report `sigaltstack restore failed` during cleanup; their exit statuses and outputs are retained, without treating these runs as a clean conformance gate.

## 3. Structural comparison

| Layer | MVP v1 | Current full LambdaJS | Transferable lesson |
|---|---|---|---|
| Frontend/backend | Shared JS frontend; private numeric/generic lowering to MIR native code | Shared JS frontend; full lowering to MIR native code | The difference is not interpreter versus JIT, nor a new parser |
| Values | 64-bit NaN-boxed `MvpValue`; raw binary64 Number | 64-bit `Item`; most doubles already inline | Keep values native through operations; changing the public tag format is not a prerequisite |
| Numeric functions | Syntax-admitted numeric kernel, F64 locals/parameters and direct recursive calls | Existing native variants, but conservative/ad hoc admission and return inference exclude important loops | Extend guarded entry/body inference and native fact propagation |
| Generic arithmetic | Number pair returns before allocation/root setup | `js_add` creates roots before classifying operands; general numeric dispatch handles coercion/BigInt/errors | Add a cheap Number head to the existing semantic operation |
| Arrays | One dense value-array model; holes and typed-array semantics are restricted | `Array`, `ArrayNum`, typed arrays, descriptors, prototypes, sparse storage, host collections | Prove present own element and storage kind, then execute a direct physical operation |
| Object fields | Hashed ordered properties with compact existing-slot updates | NameId lookup, TypeMap/ShapeEntry, typed stores, host and descriptor handling; predicted MIR fields already exist | Reuse shape facts and isolate ordinary-object hits; do not add another object layout |
| Map/Set | Hash index points into ordered entries | HashMap plus a separately searched order list | Link the hash entry to the existing stable order node |
| Calls | Direct known/numeric entries; smaller dynamic protocol | Direct MIR entries already exist; open calls use a general activation protocol | Remove repeated work only when callee effects and identity are proved |
| GC/errors | Private precise roots/heap; restricted execution/error protocol | Shared precise roots, scalar ownership, explicit completions and full exception semantics | Reduce unnecessary safepoints and liveness, retain the full ownership/error contract |
| RegExp/built-ins | Small direct RE2 loops and admitted built-in patterns | RE2 plus compatibility routes and observable JS protocols | Guard built-in bulk execution, preserving all externally visible behavior |

The [Tune11 closeout](JS_Tune11%20%28done%29.md) already implemented `MirNumericOpPlan`, literal-derived packed reads, predicted field access, and string improvements. The proposal below addresses their **coverage and entry conditions**, plus costs that remain in the shared fallback. It does not count those existing mechanisms as new work.

## 4. Numeric functions: an admission problem with a large generated-code cost

### 4.1 What the source and MIR establish

Full JS's [`jm_collect_return_type`](../../lambda/js/js_mir_function_collection_class_inference.cpp) recognizes a returned identifier as numeric only when it matches a numeric formal parameter. It does not propagate the type of a returned local accumulator. For addition, it detects known string chains but otherwise leaves the return unknown. The [native eligibility phase](../../lambda/js/js_mir_module_batch_lowering.cpp) then requires a numeric result and at least one numeric parameter.

Consequences in the fresh finalized MIR:

- `sum`'s `run(n)` has only an Item body. Each loop iteration calls `js_cmp_raw`, two `js_check_tdz` sites, `js_add`, and `js_subtract`. It publishes roots and handles possible scalar-home/error results. The `i` and `s` bindings were initialized before the loop, yet their assignment checks remain.
- `fib(n)` also has an Item body. Its recursive edges **already directly call that body**. Each non-base invocation nevertheless calls two subtract helpers and one add helper, and handles boxed returns and ownership. Its deficit is not solely the dynamic call kernel.
- No `_n` numeric variant is emitted for these benchmark functions in the captured artifacts.

MVP's [`mvp_numeric_mir.cpp`](../../lambda/js/mvp/mvp_numeric_mir.cpp) emits F64 arithmetic and direct numeric recursion. Its [generic compiler](../../lambda/js/mvp/mvp_generic_mir.cpp) also attempts a numeric kernel per eligible non-closure function, so timing/printing in the surrounding benchmark does not prevent that specialization.

This explains a concrete structural difference behind the numeric benchmark gaps. It does not establish that every instruction in full JS's finalized MIR survives MIR's later native optimizer, or quantify each instruction's cost.

### 4.2 The necessary correction to MVP's approach

`mvp_host_numeric_call_values` in [mvp_runtime.cpp](../../lambda/js/mvp/mvp_runtime.cpp) applies `mvp_to_number` to arguments before entering a numeric kernel. This is semantic coercion, not a Number representation guard. The fresh probe:

```js
function add1(x) { return x + 1; }
console.log(add1("2"));
```

prints `21` in full JS and `3` in MVP. Full LambdaJS must not adopt this admission rule (**S1.11**, **D2.4.3**, **D3.3.2v2**).

### 4.3 Proposed native lane

Extend the existing `FnAnalysis`/`FnVariantAnalysis`, `JsFuncCollected` native entry, `MirValue`, and shared numeric plans:

1. Infer candidate native parameter shapes separately from the generic source contract. Use binding identity and dataflow to propagate Number facts through local assignments and loop joins. Solve recursive numeric return candidates to a bounded fixed point; recognize `fib` without assuming every `+` is numeric.
2. Emit an immutable entry selector: already-proved Number actuals call the native entry directly; otherwise a side-effect-free Number guard selects native or the complete boxed body. A string, BigInt, Symbol, object, missing argument or other mismatch enters the boxed body with its original values. Do not coerce to make a guard pass.
3. Keep F64 locals, loop state and intermediate returns native. Use the shared emitter for arithmetic/comparisons and known calls. Box at the actual Item boundary, not between operations.
4. Propagate definite initialization independently of value type. Eliminate TDZ checks only where initialization dominates every incoming path; retain checks for captured/possibly uninitialized bindings and paths affected by `eval` or `with`.
5. Preserve native stack limits and the explicit error companion when required. A numeric recursive kernel still needs correct overflow behavior. Elide root slots only when the variant's actual effects/liveness prove it safe, under **D5.3.2–D5.3.5** and **D8.4.3v2**.

Start with noncapturing functions, simple formals, stable local callee bindings, and numeric-only operations. Keep other functions generic. Do not require a speculative mid-loop restart: the entry guard selects a complete implementation before its effects begin. Mutable callees need a current identity check, or remain ordinary calls.

**Completion evidence:** the `sum` hot loop contains native compare/add/subtract and no arithmetic/TDZ helpers; `fib` uses native recursive operands/results with the required stack/error handling. Number and non-Number calls to the same function must remain behaviorally identical to the boxed implementation.

## 5. Map/Set: remove the second search

### 5.1 A verified algorithmic difference

In [full js_runtime.cpp](../../lambda/js/js_runtime.cpp), `js_collection_method` performs `hashmap_set`, then calls `js_collection_order_upsert`. That helper walks from `order_head`, comparing keys until it finds the existing entry or reaches the tail. New distinct keys scan every previous entry. `js_collection_order_remove` also walks the list on deletion.

Thus constructing N distinct entries performs O(N²) order-list comparisons even though the collection has a hash table. Repeated updates are O(N) in the position of the key. The collection `get` path already uses a hash lookup; this is not a claim that all Map operations are linear.

In [MVP mvp_heap.cpp](../../lambda/js/mvp/mvp_heap.cpp), `mvp_map_set` uses the hash index's entry position to update the value or append a new ordered entry. There is no second full key search.

A diagnostic constructs four maps with distinct numeric keys per process, with three processes per size/backend and alternating backend order. It checks the resulting sizes. Its sources and samples are in the evidence JSON.

| Entries per map | Full JS median ms | MVP median ms |
|---:|---:|---:|
| 1,000 | 12.034 | 0.660 |
| 2,000 | 43.313 | 1.489 |
| 4,000 | 173.787 | 3.444 |
| 8,000 | 687.220 | 7.764 |

The near-4x full-JS time on each of the last two doublings corroborates the source's quadratic work. MVP's growth is much closer to linear, with allocation/hash-growth overhead. This diagnostic is outside the 63-row acceptance population.

`knucleotide` builds Maps of many k-mers and repeatedly updates counts. Its fresh 15.33x gap makes this a strong workload target, though the exact fraction caused by list scanning versus substring, call and arithmetic costs still requires an implementation A/B.

### 5.2 Proposed representation change

Keep the existing `JsCollectionMap : Map`, `JsCollectionData`, `HashMap`, and stable `JsCollectionOrderNode`. Extend `JsCollectionEntry` with an order-node pointer, or make that pointer its value payload so the node becomes the single owner of the key/value. Avoid duplicate independently updated values.

Expected operation flow:

- Lookup once using the existing JS SameValueZero hash/equality policy.
- Existing key: update that node's value in place; insertion order does not change.
- New key: allocate one node, append at the tail, insert its pointer into the hash table.
- Delete: retrieve the node from the hash entry and update its deletion/list state directly.

Reuse the current GC tracing and weak-collection ownership boundaries. Hash-table growth must not invalidate iterator-held node addresses. Preserve iteration during mutation, deletion/reinsertion, additions during `forEach`, clear during iteration, NaN and signed-zero key behavior, weak ephemerons, and scalar-home ownership. This is an internal storage improvement, not permission to import MVP's restricted iterator behavior or change collection semantics (**S1.11**, **D1.3v3**, **D5.3.3**).

**Completion evidence:** no list search in set/add/delete; the scaling curve ceases to be quadratic; focused collection semantics, forced GC and the canonical `knucleotide` row pass. This is the first recommended implementation because its cause and remedy are unusually well isolated.

## 6. Arrays: specialize the actual storage, including parameters

### 6.1 Why existing fast paths leave work on the table

[`jm_emit_packed_array_read`](../../lambda/js/js_mir_expression_lowering.cpp) already performs a guarded direct load. Admission requires a receiver traceable to an array literal and repeated/loop use. The physical hit excludes content/companion state, attributes and scalar tails, checks length/capacity, and rejects holes. It does not cover arbitrary array parameters or typed-array storage.

The fallback is useful but more expensive: `js_elements_get_number` creates roots, validates the Number index and calls `js_elements_get_int`; that dispatches among numeric arrays, tagged arrays, typed arrays, strings and general objects. Typed arrays then enter `js_typed_array_get`, refresh the view, obtain current length/data and read/box the element. The fresh `primes` MIR contains calls to `js_elements_get_number` in its loops.

MVP's `mvp_array_get` is essentially kind check, length check, backing-storage lookup, value load. It can be this small partly because it implements a narrower model:

- Newly extended slots are `undefined`, rather than a fully general absent-element/prototype protocol.
- Its typed-array constructors use the same MVP array storage. The probe `new Uint8Array(1); a[0] = 257; console.log(a[0])` prints `257` in MVP and `1` in full JS.

Therefore the full engine should transfer the small **guarded storage operation**, not MVP's array semantics.

### 6.2 Proposed array paths

Extend the existing dense-access physical plan to accept a runtime-guarded receiver from a parameter/local with a numeric index. Do not require literal provenance when the same complete storage guard can establish the facts. Bound code expansion to repeated accesses and hot loops using static analysis, not feedback caches (**D8.4.1v2**).

Use separate cases for:

| Case | Required proof | Hit |
|---|---|---|
| Present tagged own element | Ordinary array, valid index, live in-bounds slot, no overriding indexed descriptor/host behavior; ownership-safe stored value | Load Item |
| Present numeric-array element | Admitted `ArrayNum` representation and no semantic override | Load native F64; box only if consumer needs Item |
| Existing writable own element | Above plus writable data slot and applicable ownership/write preparation | Store through existing storage primitive |
| Fixed typed-array element | Real typed-array brand/element kind, attached valid buffer/view, valid bounds and admissible input value | Native typed load/store with ECMAScript conversion |

For a present own data property, an unrelated prototype property is irrelevant. For overwriting an existing writable element, extensibility is irrelevant. Current `js_array_set_existing_dense_no_gc` conservatively requires extensibility and a clean prototype as well as other guards. Separate an existing-slot write from growth/hole creation so that it need not inherit those extra premises. Frozen/descriptor-altered slots still miss.

Holes, absent/out-of-bounds indices, getters, proxies, host collections, detached/resizable buffers, coercing stores and unsupported representations go to the existing JS kernel. Preserve numeric-key semantics, including numeric `-0` versus string `"-0"`, fractions and nonfinite values. Never cache an element pointer across a call that can allocate, resize, detach, or run user code; hoist only across an effect-proved region and reacquire after invalidation.

The important rows differ: `primes` uses `Uint8Array`; `fft`/`navier_stokes` stress numeric indexing; `text_search` searches **arrays of character codes passed as parameters**. Optimizing only string built-ins or array literals misses `text_search`'s main loop.

## 7. Helpers, field access, and calls

### 7.1 Small heads on existing helpers

Yes, MVP-inspired fast helper heads are appropriate. They should be part of the existing JS operation, using existing Item types and one slow semantic implementation:

```text
evaluate operands once
    -> side-effect-free common-case checks
        -> direct physical result
        -> otherwise existing full JS operation
```

For addition, MVP checks Number/Number and immediately adds. Full [`js_add`](../../lambda/js/js_runtime_value.cpp) opens a four-slot root frame before checking types, then handles ToPrimitive/string paths and enters `js_numeric_binary`. That numeric dispatcher performs operand conversion, error/Symbol/BigInt checks, extraction and result creation.

Put a correctly classified Number pair before that work, and share extraction/operation code with the existing numeric implementation. Initially this can use the normal Item result encoder. Only a separately proved raw numeric leaf may claim `NO_GC`/infallible effects; the whole helper remains allocating/throwing when its slow arm can do so. A helper head alone also does not remove the caller's conservative root publication. MIR specialization is needed for that additional gain (**D5.3.2**, **D5.3.4**, **D8.4.3v2**).

Apply the same pattern to comparisons, present own elements, string-length/character leaves, and common collection operations. Classification must distinguish JS Numbers from Symbols/BigInts even where the host uses overlapping scalar categories. Misses preserve coercion order and exceptions. Reuse/promote existing kernels; do not create parallel `js_fast_*` implementations of full semantics.

### 7.2 Ordinary object fields

Full JS already has `js_get_name_id`/`js_set_name_id`, NameId-indexed shape lookup and immutable predicted MIR fields. It does not build a property-name string for every named hit.

Residual work visible in [js_runtime.cpp](../../lambda/js/js_runtime.cpp) includes host-dynamic admission before ordinary lookup, repeated kind/shape/descriptor checks, typed field reads/stores, and global/DOM synchronization hooks on an admitted named store. The predicted MIR store in `jm_emit_predicted_literal_field_store` currently accepts float fields only.

Broaden the physical plan where a complete guard proves an ordinary own default data slot. Reuse `Map::map_kind`, `TypeMap`, `ShapeEntry`, NameId and existing owned-field stores. Give ordinary non-host objects a path that does not enter global/DOM hooks; retain those hooks for their actual receivers. Share required predicates between C helpers and MIR plans so descriptor, deletion and scalar-ownership rules cannot diverge.

Do not replace shaped objects with MVP hash objects on the strength of the aggregate score. Full JS is already faster on two allocation-heavy rows in the fresh comparison, and a direct predicted shape offset can be cheaper than hashing.

### 7.3 Calls: preserve Get, then select the entry

MVP's known calls can bypass most dynamic activation work. Full `js_call_kernel` must support rooted actuals, `this`, new.target, module/realm state, captured `with`, generators/derived constructors, source tracking and returned completion ownership. It already has per-callee body entries, caller-rooted argument variants, and direct MIR calls. Do not replace it with another universal dispatcher.

Extend existing function/call analysis to select the smallest correct entry for a known capability. A noncapturing numeric function that cannot observe activation state should use its native variant. A simple known JS function may use the existing direct-body ABI with the required receiver/context/stack and error handling. A dynamic callee remains on its current executable entry unless its capability and activation requirements are fully proved (**D6.2.2v2**, **D8.4.2v2**).

For methods and built-ins, preserve this sequence:

1. Evaluate the base and key; perform the observable property Get once.
2. Evaluate arguments in order, preserving the already selected callee and receiver.
3. Guard the selected callable's identity/capability and any receiver storage facts after argument effects.
4. Call the admitted entry, or call the already selected value through the full kernel. Do not repeat Get or argument evaluation on a miss.

MVP's `mvp_generic_find_direct_self_method` recognizes `this.method()` using the class and absence of a syntactic subclass. That does not prove the method was not replaced. The probe `c.value = function () { return 99; }; c.run()` produces `1` in MVP and `99` in full JS when `run` calls `this.value()`.

Similarly, MVP recognizes several built-ins by spelling. A probe replacing `Math.abs` is rejected by MVP while full JS calls the replacement. Neither shortcut is an admissible general fast path. Static callee prediction needs the identity/semantic guards above; no mutable method cache is proposed.

## 8. Bulk built-ins and memory representation

### 8.1 RegExp: avoid full per-match JS protocol when it is provably unnecessary

MVP uses RE2 directly in `mvp_regex_next` and small C loops in `mvp_builtin_string_match`/replace. Full JS also uses RE2, with additional compatibility routes. The difference is not simply the regex library.

Full `js_regexp_symbol_match` repeatedly executes `js_regexp_exec_dispatch`, looks up `exec`, calls it, constructs an exec result, then reads result element zero. The string built-in checks `Symbol.match`/`Symbol.replace` first. There are already lower-level bulk match/replace loops in the same file, but ordinary intrinsic dispatch must respect the observable protocol before reaching them.

Propose an admitted built-in bulk path reusing those loops and the existing regex router: genuine RegExp internal slots, current built-in operation/exec identity, unmodified relevant descriptors, ordinary lastIndex state, and appropriate string/flag/replacement facts. Begin with a narrow no-callback case. Preserve result shape, lastIndex final state and errors, empty-match advancement, UTF-16 indices, captures, replacement substitutions, and legacy match state. User overrides/accessors/unsupported cases stay on the full path. This is an optimization of the wrapper protocol, not license to replace ECMAScript regex with RE2 for every pattern.

The 49.12x fresh `regexredux` gap makes this worth an isolated follow-up. It does not prove that all of the gap is protocol overhead: routing, allocation and matching work must be measured separately.

### 8.2 Item alignment and the abandoned v2 experiment

Both `MvpValue` and `Item` are eight bytes. [`flt2it`/`push_d`](../../lambda/runtime/lambda-mem.cpp) already encode most doubles inline in Item, with special zero encoding and a scalar home for out-of-band bit patterns. `js_make_number` uses that path. Full JS does **not** allocate a heap float for every arithmetic result.

Item nevertheless has a broader representation/ownership contract. The captured generic MIR contains checks and transfers for possible scalar-home/companion results even when a benchmark happens to produce ordinary doubles. Better variant facts and native propagation can eliminate that uncertainty locally without changing the public representation (**D2.4**, **D5.3.4**).

The [abandoned v2 history](JS_MVP_Runtime.md#7-abandoned-v2-item-alignment-experiment) changed values, containers, ownership, helper adapters and JSON integration together. Its regression is evidence against that bundled implementation, not a controlled demonstration that sharing Item intrinsically imposes its entire slowdown. No alternative collector or NaN-boxing rewrite is justified by this study.

Keep Lambda's current `String`, `Array`/`ArrayNum`, `Map`/`TypeMap`, `Function`/`JsFunction`, allocator and precise roots. Changes should primarily be analysis facts, existing-struct extensions and physical operations. Native JSON reuse is a separate integration question; no evidence here attributes these numeric, Map or array gaps to JSON parsing/formatting.

## 9. Implementation sequence and acceptance

| Step | Work | Reuse / extend | Evidence required before claiming improvement |
|---|---|---|---|
| 1 | Remove Map/Set order-list rescans | `JsCollectionEntry`, stable order node, existing HashMap | Scaling curve, `knucleotide`, mutation/iterator/weak-GC correctness |
| 2 | Cover numeric accumulators and recursive results; eliminate dominated TDZ checks | `FnAnalysis` variants, binding/dataflow facts, native entries, `MirNumericOpPlan` | Native `sum`/`fib` MIR, numeric and coercing-call parity, stack/error checks |
| 3 | Add cheap Number/own-element helper heads; admit parameter arrays and fixed typed arrays in MIR | Existing JS kernels, shared dense/storage plans and ownership primitives | `fft`, `sieve`, `primes`, `navier_stokes`, `text_search`; descriptor/buffer/GC regressions |
| 4 | Broaden ordinary own-field and proved call entries | TypeMap/ShapeEntry/NameId, function capabilities and current ABIs | AWFY/object workloads; getter/override/realm/eval/with parity |
| 5 | Route admitted built-ins to existing bulk kernels | RegExp internal slots/router, string kernels, current callable identity | `regexredux`, override/lastIndex/Unicode tests and isolated allocation/dispatch evidence |

After each meaningful runtime change, run focused tests plus `make test-lambda-baseline` and `make test262-baseline`; use forced precise-GC/poison checks for ownership-sensitive changes. Test success includes guard misses: altered prototypes/descriptors, getters that mutate receivers, argument side effects, coercions that throw, mixed calls to the same function, and mutation during iteration. Do not weaken the Test262 harness to accommodate a fast path.

For performance, keep exact release before/after binaries and hashes, the frozen source/input/wrapper manifest, identical outputs, and interleaved A/B runs. Start with the target rows and counterexamples (`binarytrees`, `gcbench`, `array1`, `collatz`), then run all 63 with every row reported. Archive results under `test/benchmark/js_mvp/` or the regular ResultN workflow. Instrument miss reasons and helper/allocation counts only in separate diagnostic runs, not in timed builds. Record emitted native variants and why a candidate was refused.

Use separate changes/ablations to distinguish a helper-head gain from native MIR admission, and a Map storage gain from method dispatch. Measure compile time, end-to-end time, memory and generated-code growth alongside execution time. Immutable native variants should have explicit static bounds; they must not become an unbounded specialization framework.

The previous 0.3x QuickJS aspiration would require about **8.63x** improvement over the historical full-JS 2.588162x geometric mean, and about **2.31x** over MVP v1's 0.691519x. Neither factor is established by this proposal. Improving only 10 of 63 rows by 10x improves the total geometric mean by about 1.44x. Numeric kernels alone cannot establish that target; broad collection/index/call coverage and the final complete matrix are necessary.

The first implementation should be the Map index-to-order-node link, followed by the existing numeric lane's admission/dataflow repair. These two changes address directly observed unnecessary work while preserving Lambda's common runtime types.
