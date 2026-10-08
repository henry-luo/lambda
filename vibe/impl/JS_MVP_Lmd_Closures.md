# JS MVP closures, callbacks and array growth

**Status:** the first step of [JS_MVP_Lmd §22](../jube/JS_MVP_Lmd.md#22-closures-callbacks-and-array-growth) is implemented, 2026-10-08. Validation and paired measurements are recorded below. Public static fields and `String.substring` remain the separately proposed follow-up.

Authority: **S1.11**, **D1.3v3**, **D5.2–D5.3**, **D6.2.2v2–D6.2.4**. These changes implement the existing guest-language boundary without changing Lambda capture semantics or the shared Function layout.

## Representation and admitted behavior

MVP closures reuse `Function::closure_env`, `closure_field_count`, `heap_calloc_closure_env`, and `owned_item_slot_store`. Each captured binding has a shared one-element Lambda Array cell. Enclosing code, nested closures and sibling closures read and write that same cell. The common environment tracer retains the cells; existing array storage owns their wide scalar homes. Lambda closures continue to store immutable snapshots (**D6.2.3v2–D6.2.4**).

Cells exist before hoisted functions are created, including captures of function-body lexical declarations. Reads and writes retain TDZ and const checks. Captured parameters, named function expressions, escaping captures, nested forwarding and distinct `let`/`const` iteration bindings are supported. Capturing functions use the existing boxed entry ABI; functions without captures retain their direct/native paths.

Arrows in class methods/constructors share a two-element receiver/new-target cell. A successful `super()` updates its receiver slot, so an arrow created before `super()` sees the initialized receiver afterward. Reading that receiver beforehand raises ReferenceError. Ordinary functions observing `this`/`arguments`, global-arrow `this`, and lexical `super` inside arrows remain outside this bounded phase. Arrow `super` is rejected during admission rather than misreported as an unbound identifier at runtime.

Sparse indexed writes and length growth initialize gaps with the existing deleted/hole sentinel. Shrink/regrow cannot resurrect elements. `slice` normalizes JS bounds in MIR and calls Lambda's existing `fn_slice`, preserving holes, shallow identity and independent scalar ownership. `forEach` snapshots length, skips holes, rereads live elements after callbacks, supplies value/index/array and `thisArg`, and propagates failures through the existing call emitter.

No VMap, full LambdaJS runtime import, conservative roots, vendor changes or alternate closure ABI is introduced. The shared collection path never carries full-JS array ownership flags in MVP.

## Helper and dependency inventory

The runtime and compiler helpers were disclosed before implementation.

| Addition/reuse | Dependencies and purpose |
|---|---|
| `mvp_lmd_array_resize` | Only new runtime entry. Uses `RootFrame`, `Rooted`, `array_reserve_append_slots` and the existing hole sentinel. |
| `mvp_lmd_array_store` | Extended existing helper; retains the immediate overwrite/append path and fills gaps on growth. |
| `capture_binding`, `binding_cell`, `initialize_capture`, `scope_capture_initialize`, `clone_loop_bindings` | Compiler binding/environment planning and cell initialization; reuse existing function/array allocation and owned stores. |
| `sequence_bounds` | Extracted from the existing fill lowering, shared by fill and slice. |
| `array_foreach_call` | Reuses argument snapshots and the ordinary boxed call emitter, including class receivers and error propagation. |
| `dense_append_loop` | Reuses binding identity, update deltas, enclosing-loop and initialization analysis. Proves an uninterrupted zero-based unit-step append loop over an unescaped local array. |
| `field_write`, `for_of`, `mvp_lmd_string_concat` | Existing helpers extended/tightened: retain a fresh literal's shape proof across unconditional typed stores, validate the snapshotted iterable's representation once, and reuse the MIR caller's precise string argument roots. |
| `heap_calloc_closure_env`, `owned_item_slot_store`, `fn_slice` | Existing Lambda APIs. Their imports describe allocation/root effects; no new shared-runtime implementation is needed. |

The dense-loop proof rejects aliases, captures, method calls, skipped writes and nonunit steps. The initial regression screen exposed a 10% `integer_dense` slowdown from a newly required generic hole branch; this proof restores the prior read path for dense construction while retaining hole checks elsewhere.

`Object.keys`/`values`/`entries` produce fresh dense arrays, so their direct iteration also retains its previous hole-free read path. Literal initialization drops subsequent shape guards only while every prior store is known to preserve the shape; a possible numeric or optional-value retype restores the guards. The iterator keeps its original owner even if the source binding is reassigned. Only the fixed array/ordered-map discriminator is hoisted: lengths, entries and tombstones remain live. String concatenation has only audited MIR callers, whose argument roots already cover the shared join's allocation. No native-stack scanning is involved (**D5.2–D5.3**).

## Coverage and reproducible harness

The first step targets and executes the five complete AWFY bundles `bounce`, `storage`, `nbody`, `richards`, `cd`, plus JetStream `crypto_sha1`. The adapter retains complete bundle/kernel bodies and native oracles, replacing only host timing and output. AWFY inner counts come from the original bundle harness; SHA1 invokes its original `runIteration()` (25 digest-checked runs).

`test/benchmark/js_mvp_lmd/closures.py` freezes inputs, executable, Node identity, commands, raw stdout/stderr and finalized MIR. All measured samples use fresh processes, rotating lane order, one discarded process per lane and self-reported time. MVP and Node include class construction/setup and the oracle in their timed entry. Lambda retains its canonical port timer. The five AWFY Lambda references are untyped; the SHA1 `.ls` variant contains explicit integer/u32 annotations for wrapping arithmetic and is labeled separately. Node lazy compilation/tiering during execution is included; MVP initial compilation is excluded. These are port comparisons, not isolated compiler comparisons: Bounce uses parallel Lambda arrays, and CD uses index-addressed array nodes in Lambda and object nodes in JavaScript.

The new `mvp_lmd_v1` manifest records the current **71 standard workloads**. Legacy profile populations continue to use their frozen IDs. Inventory membership is not a support claim. Regenerate/verify with:

```sh
python3 test/benchmark/generate_js_mvp_manifest.py --profile mvp_lmd_v1
python3 test/benchmark/verify_js_mvp_manifest.py --manifest test/benchmark/js_mvp_lmd_manifest_v1.json
python3 test/benchmark/js_mvp_lmd/closures.py --candidate lambda.exe --out temp/mvp_closures --runs 15
```

## Validation and measurements

Acceptance evidence is under `temp/mvp_closures_20261008/`. Release timings pin `JS_EXEC_BACKEND=mir`, `JS_MIR_INTERP=0`, `LAMBDA_JS_LARGE_INTERP=0`, and `LAMBDA_EXEC_BACKEND=jit`; legacy equivalents are also set for archived controls. Every timing has an output check. Identical-control peer lanes and longer confirmations accompany the 54-workload regression comparison.

- Frozen Result7 binary SHA-256: `0b6688bc8b5a6742207b23d3b19e6ec42dedbc4ad4fb45227f2ba940efafb037`.
- Final candidate (`final6.exe`) SHA-256: `70957b7ceffcb7ce4a7a9949819a1db06c26a11f3c50393f622f968668f8883e`. Earlier candidates and measurements remain separate; `accepted-48`, `accepted-classes`, `accepted-six` and `final-lambda` use this binary.
- Initial and corrected screens remain separate (`core-48`, `core-classes`, `final-48`, `final-classes`).
- MVP semantic tests: **52/52** normally and **52/52** with `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1` (`tests8.log`, `tests8-gc.log`). Cases include shared/escaping/forwarded captures, parameter mutation, hoisting/TDZ/const, loop bindings, receiver/new-target capture, sparse mutation, scalar ownership, callback exceptions, class reflection/ancestry and explicit collection of a returned closure environment. Fresh-literal retyping and reassignment of an iterated binding have focused assertions.
- Inventory generator/verifier: **71/71** current rows; legacy populations remain 63 unique IDs.
- Lambda baseline: **6,427/6,427** (`lambda-baseline-final.log`).
- Test262 baseline: **40,261/40,261**, zero failures, slow cases, batch instability or retries (`test262-baseline-final.log`). An earlier run had one slow Unicode-identifier case that passed its isolated retry; the final full rerun was clean, without concurrent build/benchmark jobs. No runner or baseline was changed.
- `lambda.exe` after both gates matches the measured `final6.exe` hash above. `gates.json` records commands, environments, exit codes and timestamps.

### Existing workload performance

The final **54/54 output checks pass**, with 30 measured pairs per workload and all six permutations of candidate/control/identical-control-peer order. No candidate's two-sided 95% paired bootstrap interval lies wholly above 1.0. The geometric mean candidate/control ratio is **0.9973** (roughly unchanged). These finite measurements establish no confirmed regression in this cohort, not a guarantee of identical execution time.

| Workload | Result7 control ms | Candidate ms | Candidate/control, 95% interval |
|---|---:|---:|---:|
| deriv | 8.5435 | 8.3075 | 0.9724 [0.9675, 0.9850] |
| binarytrees | 3.8285 | 3.6755 | 0.9600 [0.9424, 0.9720] |
| map_iteration | 1.4570 | 1.4080 | 0.9664 [0.9637, 0.9691] |
| object_growth | 2.9120 | 2.9015 | 0.9964 [0.9847, 1.0017] |
| strings | 0.1065 | 0.1055 | 0.9906 [0.9541, 1.0388] |

Earlier candidates had a confirmed 2–3% `deriv` slowdown and a smaller `strings` slowdown despite unchanged MIR instruction structure. A control rebuilt from the pre-phase MVP sources against today's shared objects reproduced the `deriv` difference; unrelated linked WebGL additions alone did not explain it. The exact native cause was not isolated. The accepted source removes independently identified redundant literal guards, repeated iterable classification, and duplicate string root frames; the final measurements above clear those regressions. The narrow string test remains noisy at about 0.1 ms, so its small median change is not an established speedup.

The three shared untyped Lambda clients (`fib`, `gcbench`, `json_gen`) also pass, with 60 balanced pairs each and no confirmed slowdown (`final-lambda/comparison.json`). Their median candidate/control ratios are 1.0025, 0.9923 and 0.9823. The archived Result7 executable remains unchanged; this phase does not publish a new numbered result or replace historical cells.

Raw outputs, MIR, source hashes and the per-row summary are retained in `accepted-48`, `accepted-classes`, `final-lambda`, and `final-performance-summary.json`. The matched diagnostic control has SHA-256 `05494ba1f2f25187f74e81183fe8897f93482f3214d884a38a2bb9f48a456495`; its exact source snapshots and build commands are in `matched-base/build.json`.


### Six new workload timings

Fifteen measured release processes per lane; medians in milliseconds (`accepted-six/comparison.json`).

| Workload | MVP | Lambda reference | Node | MVP / Lambda |
|---|---:|---:|---:|---:|
| awfy/bounce | 1.066 | 0.070 | 0.720 | 15.23× |
| awfy/storage | 0.549 | 0.594 | 0.678 | 0.92× |
| awfy/nbody | 95.769 | 7.454 | 5.381 | 12.85× |
| awfy/richards | 192.828 | 285.671 | 8.168 | 0.67× |
| awfy/cd | 637.959 | 605.835 | 35.602 | 1.05× |
| jetstream/crypto_sha1 | 14.354 | 50.363 | 8.751 | 0.29× |

The five AWFY references are untyped. SHA1 retains its canonical annotated integer/u32 implementation; its Lambda column is **not an untyped result**. All source oracles pass. These six additions bring measured coverage from **54 to 60 workloads** (43 standard workloads plus 17 existing microbenchmarks).

## Remaining scope

Public static fields and UTF-16 `substring` (DeltaBlue/JSON) are the follow-up, not part of the six-target step. Ordinary-function construction, arbitrary prototype mutation, accessors/descriptors/proxies, `__proto__`, BigInt, RegExp and Node I/O remain deferred. The three Julia kernels (`parse_integers`, `iteration_pi_sum`, `matrix_statistics`) pass both consecutive oracle calls in the correctness-only audit (`julia-audit.json`). They are not counted in the six additions because their canonical harness runs an in-process warmup outside the timed region; the current MVP CLI times the entire entry. Cross-language call adapters remain separate from the shared closure representation.
