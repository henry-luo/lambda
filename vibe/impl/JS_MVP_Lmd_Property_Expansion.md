# JS MVP: prototypes, accessors, proxies and sparse arrays

**Date:** 2026-10-10. **Status:** implementation in progress; acceptance pending.
This document records the complete phase 31 plan. No feature or validation
completion is implied by its publication.

**Design:** [JS_MVP_Lmd §31](../jube/JS_MVP_Lmd.md#31-prototypes-accessors-proxies-and-sparse-arrays).
**Starting evidence:** [MVP_Result9](../../test/benchmark/js_mvp_lmd/MVP_Result9.md)
and its [raw JSON](../../test/benchmark/js_mvp_lmd/MVP_Result9.json).
**Source inspected:** `9c63181d6a3b1fa16560023dfe1425117e2b0fb9`.

## 1. Scope and delivery order

Expand the current MVP through one receiver-aware property layer. Deliver
prototype/descriptor foundations, accessor execution, sparse arrays, then
proxies, including callable and constructable proxies. Preserve normal own-field
and dense-element performance throughout.

**USER decision: do without `is_normal`.** Add no universal normal/special bit
to `Container`, `TypeMap`, or a side table, including a cached equivalent under
another name. Use operation-specific shape facts, the existing element kinds,
carrier classification and separately validated prototype assumptions.

| Area | Phase 31 scope |
|---|---|
| Prototypes | Mutable ordinary prototype links; Object/Reflect prototype APIs; ordinary constructor `.prototype` replacement; inherited lookup, dynamic `instanceof` and `super`; observable builtin replacement |
| Legacy prototype spelling | Lift the earlier phase exclusion for `Object.prototype.__proto__` and object-literal prototype-setting syntax; computed keys remain ordinary properties |
| Descriptors and accessors | Object/class getters and setters; full ordinary data/accessor descriptor validation and reflection; extensibility, seal and freeze; indexed and length descriptors on ordinary arrays |
| Sparse arrays | Large logical lengths and distant indices with bounded physical storage; holes, indexed descriptors, prototype-aware array operations and ordered reflection |
| Proxies | All property/prototype/extensibility traps; forwarding, revocation, invariants; callable/constructable targets and their `apply`/`construct` traps |
| Interop | Stable Lambda nominal ancestry and field contracts; traced shared storage; explicit procedural entry for JS property operations that can invoke effectful callbacks |

The first new canonical workload is
[`jetstream/hashmap`](../../test/benchmark/jetstream/hash-map.js). It replaces
constructor prototypes, executes getters, and assigns `prototype.__proto__`.
Success would expand the published population from 87 to **88 workloads**:
71 standard benchmarks and 17 existing microbenchmarks. This is a target, not
a current support claim. Sparse/proxy semantics need additional fixtures even
when no additional canonical benchmark exercises them.

Async scheduling, general Node compatibility, private fields and broader
multi-realm support are outside this phase. This is not a full ECMAScript
conformance claim. Existing unsupported brands stay explicitly unsupported;
an unsupported interaction must not silently take an ordinary-object shortcut.

## 2. Governing contracts

| Topic | Authority and consequence |
|---|---|
| Shared substrate | **D1.3v3**: reuse Lambda/lib implementations first; extract neutral storage primitives from JS code when its semantic adapter cannot be reused |
| JS semantics | **S1.11**: JS follows ECMAScript; sharing storage does not replace guest property semantics with Lambda map semantics |
| Values and containers | **D2.3.2**, **D2.6.6v3**, **D2.6.9v3**: exact carrier checks, ordinary nominal Maps and Arrays, no VMap conversion of plain objects |
| Nominal contracts | **S2.1.4**, **S11.3.1v2**, **D2.6.10**, **D3.2.4v4**: nominal identity/ancestry and declared field prefixes survive JS representation changes |
| Shape and key identity | **D3.4.3v5–D3.4.5**, **S8.2.2v5**: shared transitions and canonical UTF-8/lossless WTF-8 keys, with no Unicode normalization or loss of Symbol identity |
| Metadata/accessors | **D3.4.7–D3.4.8**: immutable class refinement; object-local accessor cells on private virtual shape entries |
| Ownership and failure | **D1.4v4**, **D1.5v2**, **D5.2–D5.3**, **D8.4.3v2**: explicit completions, destination-owned scalars and precise roots across every callback |
| Optimization | **D8.4.1v2**: immutable predicted specialization with shared fallback; no new per-site caches or feedback vectors |
| Lambda effects | **S12.1.1v2**: arbitrary JS callbacks cannot be hidden inside a pure Lambda field read |

This plan applies those contracts; it does not revise the formal specifications.
A discovered conflict requires a recorded ruling and the paired formal/working
doc update before implementation changes that contract.

## 3. Current implementation and reuse map

| Location / existing symbols | Reuse or required change |
|---|---|
| `lambda/js/mvp-lmd/mvp_lmd_objects.cpp`: `mvp_lmd_property_*`, `mvp_lmd_define_data_property` | Own storage operations, key conversion and data descriptors; generalize semantic dispatch and descriptor handling |
| `mvp_lmd_runtime_classes.cpp`: `mvp_lmd_class_property`, `mvp_lmd_instanceof`, `mvp_lmd_class_super` | Fixed nominal-chain shortcuts need guarded admission or live prototype traversal |
| `mvp_lmd_runtime.cpp`: `mvp_lmd_array_new`, `mvp_lmd_array_resize`, `mvp_lmd_array_store` | Current storage fills gaps densely; introduce shared sparse admission and separate logical/physical bounds |
| `mvp_lmd_arrays.cpp`, `mvp_lmd_library.cpp`, `mvp_lmd_functions.cpp` | Route builtin property observations through the common operations; retain proven dense paths and current receiver/call ABI |
| `mvp_lmd_mir.cpp`, `mvp_lmd_runtime.h` | Receiver operands, completion routing, effect/root classification, array representation guards and prototype-sensitive specialization |
| `lambda/input/input.cpp`: `type_tree_reflag_field`, `type_tree_retype_field`, `map_clone_typemap_for_mutation` | Shared immutable transitions and private-shape mutation |
| `lambda/runtime/lambda-eval.cpp`: `map_shape_set_resolved`, packed-field readers/stores | Keep the physical storage layer independent of JS callbacks |
| `lambda/js/js_props.{h,cpp}`: `JsPropertyDescriptor`, own-slot classification, descriptor application | Reuse common descriptor/storage logic after checking assumptions about the full runtime |
| `lambda/js/js_property_attrs.{h,cpp}`: `js_alloc_accessor_pair`, accessor installers | Reuse `JsAccessorCell` allocation and attachment; parameterize invocation/storage access where needed |
| `lambda/js/js_runtime.cpp`, `js_runtime.h` | Extract/promote shared sparse primitives, prototype/extensibility support and proxy machinery; avoid importing its entire semantic call graph |
| `lambda/js/js_object_meta.{h,cpp}`: `JsClassMeta`, `JsPropertyOps` | Existing carrier classification and operation-table contract; immutable MVP-compatible dispatch selection |
| `lambda/lambda.h`, `lambda/lambda.hpp`, `lambda/lambda-data.hpp` | Existing eight-byte header, `JsElementsKind`, `SparseArrayMap`, `ShapeEntry`, `TypeNominal` and accessor representation |
| `lambda/runtime/gc/`, `lambda/runtime/lambda-mem.cpp` | Trace cells, prototype state, proxy target/handler, sparse entries and their scalar homes precisely |
| `test/test_js_mvp_lmd_gtest.cpp`, `test/benchmark/js_mvp_lmd/expansion.py` | Extend existing semantic/MIR tests and benchmark capture tooling |

MVP and full LambdaJS currently use different paths for some array property
storage and calls. A matching C signature is not sufficient evidence of safe
reuse. Audit ABI, roots, error behavior and metadata ownership transitively.
Promote a useful `static` helper into its module header; never copy it.

## 4. Storage and fast-path admission without `is_normal`

### 4.1 Facts and their owners

| Fact | Owner | Maintenance |
|---|---|---|
| Accessor presence / non-default property attributes | Compact `TypeMap` summary derived from its entries | Build with the shape; update through transition/clone/descriptor paths |
| Indexed descriptor presence | Existing `TypeMap::has_array_index_shape`, generalized only if necessary | Update whenever indexed metadata is added, removed or converted |
| Ordinary, proxy or other exotic behavior | Existing carrier kind and immutable class metadata | Select at construction/boundary admission; preserve through shape transitions |
| Packed, holey or sparse storage | Existing `Container::JsElementsKind` | Publish with the corresponding buffer/table transition |
| Prototype override and extensibility | Precisely traced per-object state | Reuse the existing extension mechanism after the MVP compatibility audit |
| Ancestor/builtin assumptions | Compile-time closed-unit proof in this implementation | Unknown reflection or mutation selects live semantic dispatch; no inherited-value cache is admitted in that unit |

Candidate summary bits are `HAS_ACCESSORS` and
`HAS_NON_DEFAULT_ATTRIBUTES`; reuse `has_array_index_shape` rather than adding
a second equivalent bit. Their final packing is an implementation choice after
checking `TypeMap` size and alignment. They summarize local shape facts only.
They must not cache ancestor purity or declare the whole object safe for every
operation. Zero flags on an unclassified foreign/Input value do not admit it.

Preserve `sizeof(Container) == 8` and all public offsets. Do not repurpose
`array_flags` bits: its upper three bits already encode `JsElementsKind`.
Native arrays also reuse state bits for lane contracts. No additional header
bit is part of this phase.

Shapes with the same storage but different relevant attributes or semantic
families must not be treated as interchangeable proofs. Verify reflag/retype,
delete, spread/copy and constructor-shape paths maintain summaries. Shared
shapes stay immutable; private accessor shapes obey D3.4.8. Copying or converting
an accessor never copies a cell pointer into a packed Item slot.

### 4.2 Operation-specific guards

| Operation | Direct-path proof | On a miss |
|---|---|---|
| Existing own data-field read | Ordinary carrier; current exact layout/field proof; initialized data slot | Common Get with original receiver |
| Existing own data-field write | Above plus writable descriptor, compatible storage/ownership and ordinary assignment receiver | Common Set |
| Present dense-element read | Managed tagged/numeric representation; physical and logical bounds; present slot; no overriding indexed descriptor | Common indexed Get |
| Existing dense-element write | Above plus writable indexed descriptor and valid value/home representation | Common indexed Set |
| Missing field or hole read | Valid prototype absence proof if one exists | Walk live chain; dispatch exotic behavior encountered there |
| Property addition / hole fill / append | Extensibility, inherited-property rules, array-length policy and allocation capacity | Common Set/DefineOwnProperty |
| Known inherited method/builtin call | Valid current member and prototype assumptions | Resolve and invoke the actual method |

A known own field remains eligible when an unrelated field is an accessor.
An exact-shape test that already proves the needed property facts should not
be followed by a redundant summary test. A holey array's present ordinary
elements remain candidates for direct access.

Mutable chains use shared runtime traversal first. Retain predicted inherited
or intrinsic paths only where existing mutation hooks can substantiate their
guards. Centralize any required shared epoch/version extension; keep it out of
per-site cache cells. Prototype property replacement can leave the field shape
unchanged, so shape equality alone is not an inherited-value validity check.
Observe stores/deletes/redefinitions through aliases and prototype-link changes.
Ordinary own-field hits do not pay for chain guards they do not need.

Do not sweep all descendant objects after a prototype mutation. Do not hoist
guards, buffer pointers or borrowed shape slots across a callback or unknown
call that can invalidate them. Any new proof must name its invalidating effects.

## 5. Common semantic property layer

Extend existing entry points around the following internal operations:

```text
GetOwnProperty(target, key)
DefineOwnProperty(target, key, descriptor)
Get(target, key, receiver)
Set(target, key, value, receiver)
HasProperty(target, key)
Delete(target, key)
OwnPropertyKeys(target)
GetPrototypeOf(target) / SetPrototypeOf(target, prototype)
IsExtensible(target) / PreventExtensions(target)
```

Use `JsPropertyDescriptor` and existing explicit completion conventions.
Internal Set reports a boolean or abrupt completion; the source assignment
adapter handles strictness and the expression result. Keep GetOwnProperty
separate from executing a getter. Reuse common `ToPropertyKey`, `GetMethod`,
callability and descriptor-conversion operations, preserving their evaluation
order. [ECMAScript object operations](https://tc39.es/ecma262/multipage/abstract-operations.html#sec-operations-on-objects)

Only the semantic layer may invoke a getter, setter or proxy trap. Packed map
helpers keep their existing non-reentrant storage contract. Generic property
entries are marked as potentially invoking JS, collecting and throwing in MIR
analysis; existing proven physical operations retain their narrower effects.

Root the target, receiver, key, RHS, descriptor fields and selected callable
across reentry. Move borrowed numeric values into suitable scalar homes before
callback/allocation boundaries. Re-resolve fields/storage after callbacks when
the algorithm needs a fresh observation. Compound assignment and updates must
not repeat key coercion or accessor invocation. Route callback errors through
the existing catch/finally and generator completion machinery.

## 6. Feature implementation

### 6.1 Prototypes and Lambda inheritance

Keep `TypeNominal.base` as declared ancestry. Store the live JS `[[Prototype]]`
relationship separately. Initial class construction aligns instance/prototype
and constructor links with that ancestry; later JS link mutation cannot change
nominal identity or Lambda declared offsets (**S2.1.4**, **D2.6.10**).

Replace the unguarded nominal walk in `mvp_lmd_instanceof` for mutable cases.
Extend current `super` handling to use the appropriate live home-object or
constructor relationship while preserving the actual receiver. A constructor's
`.prototype` replacement affects subsequent allocation; do not retroactively
retarget existing instances. Respect distinctions between ordinary-function
and class constructor property attributes. Class behavior follows the hosted
language contract. [ECMAScript classes](https://tc39.es/ecma262/multipage/ecmascript-language-functions-and-classes.html#sec-class-definitions)

Implement Object/Reflect prototype APIs through the common kernel. Ordinary
cycle/extensibility checks and exotic delegation belong there. Per-instance
links are traced values; do not put them in immutable `JsClassMeta`.

Lift `mvp_lmd_property_key`'s blanket `__proto__` rejection when accessor
dispatch is ready. Install the inherited getter/setter and distinguish it from
an ordinary own property with that spelling.
[Object.prototype APIs](https://tc39.es/ecma262/multipage/fundamental-objects.html#sec-object.prototype.__proto__)
Audit literal admission separately for prototype-setting syntax and computed
data keys. [Object initializers](https://tc39.es/ecma262/multipage/ecmascript-language-expressions.html#sec-object-initializer)

Interop checks must demonstrate unchanged `TypeNominal` identity, ancestry and
declared offsets after legal JS mutations. A foreign declared Lambda field
cannot be deleted/retyped/converted to an accessor in violation of its contract.
An explicit adapter must enforce or reject that operation before mutation.
General cross-language subclass adapters remain outside this phase.

### 6.2 Accessors, descriptors and integrity operations

Reuse `JsAccessorCell`, its dedicated GC allocation, and
`ShapeEntry::{flags,byte_offset,accessor}`. Attach a cell only to a private
shape with `JSPD_IS_ACCESSOR` and `byte_offset == -1`; ordinary data objects
continue using shared transition shapes (**D3.4.8**).

Generalize the current data-only descriptor adapter and reuse shared
validation/application. Install object/class accessors with the correct
attributes; support reflection and Object/Reflect integrity operations.
Check descriptor field presence independently of its value, data/accessor
conversion, non-configurable restrictions and SameValue comparisons.
[Ordinary descriptor operations](https://tc39.es/ecma262/multipage/ordinary-and-exotic-objects-behaviours.html#sec-ordinary-object-internal-methods-and-internal-slots)

Keep callable Items in the cell, including callable proxies once delivered;
audit existing FUNC-only validation/comments and collector tracing. Do not
change a callable proxy into its target just to satisfy an old type check.
Expose absent get/set consistently at the public descriptor boundary.

Adopt the procedural interop policy: access that can execute arbitrary JS uses
an effectful bridge. Pure Lambda reads must reject such execution or use an
explicitly materialized data view under an existing contract; do not silently
add effectful reads to `fn` (**S12.1.1v2**). Reuse existing call-colour checking
instead of a second effect system. Public bridge naming is resolved during P0;
the phase can validate the boundary through existing native test adapters.

### 6.3 Sparse array storage and behavior

Reuse `Array`, `SparseArrayMap`, `JsElementsKind`, `lib/hashmap`, the existing
hole sentinel and scalar-home ownership. Do not add a TypeId or another hole
sentinel. Keep a compact dense prefix where useful; distant default data
elements use the numeric sparse table. Indexed descriptor entries have explicit
precedence and retire overlapping dense/table entries on conversion.

Promote/extract `js_array_sparse_get/set/delete` and the applicable growth,
density and index-collection primitives. Preserve existing MVP named attributes
when adopting the companion representation: current MVP direct attribute
storage and the full engine's `ArrayPropsShape` are not interchangeable casts.
Build the new representation completely, then publish it transactionally while
preserving the Array identity and all aliases. OOM must leave a valid old state.

Separate logical length, initialized dense extent, dense capacity and scalar
tail. Audit every consumer of `array->items[i]`, including MIR, GC, copies,
iteration and shared Lambda builtins. Bounds based on logical length alone are
invalid for sparse storage. Shared consumers must use a compatible reader or
an explicit boundary rejection; never expose the sparse array to an unchecked
dense Lambda loop.

Reuse measured density/gap heuristics. A large constructor length or distant
write must not initialize every missing index. Dense promotion is optional;
retain it only with a bounded allocation policy and evidence against repeated
conversion. Sparse numeric storage needs destination-owned scalar payloads and
precise tracing; raw hash entries must not retain temporary number homes.

Cover the array-index boundary, holes versus own undefined, length attributes
and descending truncation failure semantics.
[Array exotic operations](https://tc39.es/ecma262/multipage/ordinary-and-exotic-objects-behaviours.html#sec-array-exotic-objects)
Audit the supported array methods individually for Get versus HasProperty,
callback mutation, hole preservation, iterator reads and inherited indices.
An O(present-elements) traversal is allowed only when absent indices cannot
produce observable behavior. Receiver/prototype changes also require revisiting
observable method replacement and constructor/species behavior of supported
copying methods. [Array methods](https://tc39.es/ecma262/multipage/indexed-collections.html#sec-array-objects)

### 6.4 Proxies

Reuse `JsProxyData` and the existing proxy carrier/`JsPropertyOps` contract,
with audited MVP call/property adapters. Keep the wrapper identity distinct
from its target. A new VMap carrier is not needed for this delivery; reconsider
representation only if the reuse audit exposes a concrete incompatibility.

Deliver the complete property/prototype/extensibility trap family, revocation,
and target-invariant checks. Missing traps forward through the common kernel
with the receiver intact. Trap lookup itself is an observable property operation;
nested proxies and proxies in a prototype chain stay in semantic dispatch.
[Proxy internal methods](https://tc39.es/ecma262/multipage/ordinary-and-exotic-objects-behaviours.html#sec-proxy-object-internal-methods-and-internal-slots)

Add `apply` and `construct` through the existing receiver/new-target ABI.
Audit callability, constructability, `typeof`, `instanceof`, `Array.isArray`,
bound functions, accessor-callable validation and internal-slot brand checks.
Never generically unwrap proxies for branded builtins. Keep capability errors
for unsupported interactions until their implementation slice is complete;
do not publish partial trap coverage as complete Proxy support.

Trace target, handler and any engine-private slots. Revocation clears retained
references through the shared lifecycle path. No trap may leave borrowed
descriptor or storage pointers live across handler execution.

## 7. Work packages and completion criteria

Packages are tracked in the implementation checkpoint below. Validate each package before enabling the
next exposed surface; P1's legacy accessor syntax is enabled with P2.

| Package | Dependencies | Deliverable / primary locations | Completion criterion |
|---|---|---|---|
| P0: contracts and reuse audit | None | Inventory shared primitives, metadata ownership, callable ABI and baseline; extend `expansion.py` capture support | Exact control/source manifest; disclosed helper signatures; operation/invalidator matrix; existing capability boundaries reproduced |
| P1: common property and prototype foundations | P0 | `mvp_lmd_objects.cpp`, `mvp_lmd_runtime_classes.cpp`, shared descriptor/prototype helpers | Receiver-aware kernel, data descriptors/integrity, live links and guarded specializations; nominal/field contract probes pass |
| P2: accessor execution and legacy prototype surface | P1 | Accessor allocation/attachment, MIR admission/effects, library APIs | Object/class/inherited accessors and descriptor conversions work under forced GC; canonical hashmap oracle and timer pass |
| P3: sparse arrays | P1–P2 | Shared sparse primitives, MVP array helpers/MIR, GC and cross-language readers | Bounded distant-index allocation, descriptor/length/method semantics, representation transitions and aliases pass |
| P4a: object proxies | P1–P3 | Existing proxy payload/property operations plus MVP adapters | Every property trap, invariant, forwarding and revocation fixture passes; combinations with arrays/accessors/prototypes pass |
| P4b: callable proxies | P4a | Function/MIR invocation and construction boundaries | Apply/construct/new-target and callable-accessor integration pass; no target identity substitution |
| P5: aggregate acceptance | P1–P4b | Baselines, MIR audit, exact release comparisons, durable evidence | All scoped fixtures and 88 canonical/micro oracles pass; regression/platform limits explicitly resolved or reported |

Before moving shared code, compare the existing and proposed call graph.
Neutral kernels must not acquire full-runtime realm/lifecycle dependencies.
Build-list changes go through `build_lambda_config.json`; no generated Lua or
vendored files are edited.

## 8. Helper/API inventory to disclose before coding

The intended changes below are the initial disclosure. Final signatures and
any additional helper must be reported before implementation, including static
utilities; a rename does not make duplicated logic acceptable.

| Entry or family | Planned action |
|---|---|
| `mvp_lmd_class_property` / `mvp_lmd_property_*` | Extend semantic entry points with explicit receiver and completion behavior; keep proven physical paths separate |
| `mvp_lmd_define_data_property` | Generalize to ordinary descriptor support, preferably rename to `mvp_lmd_define_property`; reuse one shared validation/application path |
| `js_alloc_accessor_pair`, `js_define_accessor_partial`, descriptor converters | Reuse; extract storage/call adapters or add a small explicit policy only where ABI assumptions differ |
| `js_array_sparse_get/set/delete` and index/density helpers | Promote/extract the existing implementations into an appropriate shared module/header; no duplicate MVP hash table |
| Prototype/extensibility operations | Extend existing kernel and builtin dispatch; add MVP adapter entry points only where the current ABI cannot express the operation |
| `JsPropertyOps`, `JsProxyData`, proxy trap machinery | Reuse; add thin MVP callback adapters and static operation-table bindings as required |
| Shape property summaries | Maintain through existing builders/transitions; extract one common recomputation/update utility if several producers need it |
| `mvp_lmd_array_new/resize/store` | Extend representation admission; keep existing dense storage primitives and their narrow effects |

No second call engine, descriptor representation, collector, universal normal
predicate or per-site cache is proposed. P0 identifies the smallest concrete
adapter set and where promotion suffices instead of a new helper.

## 9. Acceptance fixtures

Add focused cases to `test/test_js_mvp_lmd_gtest.cpp`, using the proposed
`JsMvpLmd.PropertyExpansion*` prefix. Reuse its source execution and MIR-inspection
helpers. Use canonical JS snippets for differential Node/full-LambdaJS checks;
retain output order, thrown values and observable callback counts. New Lambda
golden scripts, if needed for interop, require matching `.txt` expected files.

| Fixture group | Required observations |
|---|---|
| Prototype links | Own shadowing/deletion; null prototypes; cycles; extensibility; constructor replacement; mutations through aliases; inherited numeric properties; source-equivalent `instanceof`/`super` behavior |
| `__proto__` | Inherited accessor versus own data property; computed/literal cases; null-prototype objects; canonical hashmap source unchanged |
| Accessors | Receiver identity, missing getter/setter, strict/sloppy failure, getter/setter order, computed-key coercion once, callback throws and reentrant shape/buffer mutation |
| Descriptors | Absent fields versus explicit undefined, data/accessor conversion, non-configurable invariants, NaN and signed zero, Symbols and canonical Unicode keys |
| Reflection/integrity | Key order/enumerability; descriptor reads without getter execution; value projections with callbacks; preventExtensions/seal/freeze and failed operations |
| Sparse arrays | Distant indices, maximum index versus named key, huge hole-only lengths, delete/undefined distinction, dense/sparse aliases, scalar tails, indexed accessors, failed truncation and method-specific hole behavior |
| Proxies | One case per trap and missing-trap forwarding; invariants, bad results, getter-based trap lookup, nested/revoked proxies, proxy prototypes, mutation during validation |
| Callable proxies | Apply/construct/return validation, receiver/new-target, revoked calls, callability observations, bound and accessor proxy callables |
| Interop/GC | Stable nominal record and declared prefix; field-contract rejection; procedural callback boundary; proxy/cell/table tracing under collection at every allocation |
| MIR/performance guards | Proven own fields and present dense elements stay direct; unrelated accessor does not demote a known field; holes/chain mutation take fallback; invalid assumptions do not survive calls |

Explicitly cover the crucial counterexample: a child with no local special
features acquires an inherited setter because its parent was mutated. A child
shape comparison must not authorize direct property creation in that case.

## 10. Validation and measurement procedure

These are commands for the later implementation work, **not commands run when
writing this plan**. Follow `doc/dev/Developer_Guide.md` §7 before builds.

### 10.1 Focused and aggregate correctness

After adding the planned cases:

```bash
make -j4 -C build/premake config=debug_native test_js_mvp_lmd_gtest
./test/test_js_mvp_lmd_gtest.exe --gtest_filter='JsMvpLmd.PropertyExpansion*'
LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1 ./test/test_js_mvp_lmd_gtest.exe --gtest_filter='JsMvpLmd.PropertyExpansion*'
./test/test_js_mvp_lmd_gtest.exe
LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1 ./test/test_js_mvp_lmd_gtest.exe
make test-lambda-baseline TEST_JOBS=4
make test262-baseline TEST_JOBS=4
```

The focused tests may use debug builds; performance never does. Shared Test262
results validate the full engine/shared changes and do not substitute for MVP
feature coverage. Require zero batch retries/unstable cases for a clean gate.
Do not modify the Test262 harness or weaken expected results to hide failures.

### 10.2 Freeze the release pair

Result9's final candidate is the initial performance control, SHA-256
`2c67152b386faf735960f302264fb08602042812be47eebb09a3970a899b29af`.
Verify that archive before copying; never identify it from the current
`lambda.exe` filename alone. Save the source commit/patch, build configuration,
binary hash and benchmark source/input/output contracts for each side.

```bash
mkdir -p temp/mvp_properties/control temp/mvp_properties/final
shasum -a 256 temp/mvp_tune_library_20261010/round6-tested/candidate.exe
cp temp/mvp_tune_library_20261010/round6-tested/candidate.exe temp/mvp_properties/control/lambda.exe
make build-release-compile
cp lambda.exe temp/mvp_properties/final/candidate.exe
shasum -a 256 temp/mvp_properties/control/lambda.exe temp/mvp_properties/final/candidate.exe
```

Use the established release build configuration with MVP enabled. Confirm the
release checker and an MVP backend preflight before timing. Never run `make
release` in a worktree. Baseline targets can replace the working executable;
only frozen release copies participate in measurement.

### 10.3 Extend the existing runner, then capture

P0 extended `test/benchmark/js_mvp_lmd/expansion.py` with options
`--phase properties`, `--population prior87|new1|all`, `--baseline-result`, and
`--exclude`. These options are implemented. Reuse its capture, normalization,
MIR verification and bootstrap implementations; incorporate the archived tuning
runner's peer/order logic by sharing code. Preserve existing phase contracts.

Read the 87 contracts from Result9 and the new hashmap contract from the
canonical manifest. Reconstruct wrappers only through the existing manifest
adapters, rechecking source/input hashes. If a contract changed, report a new
contract/version instead of silently comparing unlike work. Set fresh replay
timestamps, rather than inheriting earlier row timestamps from the catalog.

Planned commands after that runner extension:

```bash
python3 test/benchmark/js_mvp_lmd/expansion.py --phase properties --population prior87 --baseline-result test/benchmark/js_mvp_lmd/MVP_Result9.json --candidate temp/mvp_properties/final/candidate.exe --control temp/mvp_properties/control/lambda.exe --exclude text/jq_records,text/jq_mix,text/jq_bf,text/jq_tree --runs 0 --timeout 1800 --out temp/mvp_properties/preflight
python3 test/benchmark/js_mvp_lmd/expansion.py --phase properties --population prior87 --baseline-result test/benchmark/js_mvp_lmd/MVP_Result9.json --candidate temp/mvp_properties/final/candidate.exe --control temp/mvp_properties/control/lambda.exe --exclude text/jq_records,text/jq_mix,text/jq_bf,text/jq_tree --runs 7 --timeout 1800 --out temp/mvp_properties/nonjq
python3 test/benchmark/js_mvp_lmd/expansion.py --phase properties --population new1 --candidate temp/mvp_properties/final/candidate.exe --runs 15 --timeout 1800 --lambda-reference --out temp/mvp_properties/hashmap
```

For `new1`, compare output-verified candidate, Node and the available canonical
Lambda port; do not run the unsupported Result9 MVP as a timing reference for
hashmap. Keep reference-lane comparability explicit. Capture scripts pin:

```text
JS_EXEC_BACKEND=mir
JS_EXECUTION_BACKEND=mir
JS_MIR_INTERP=0
LAMBDA_JS_LARGE_INTERP=0
LAMBDA_EXEC_BACKEND=jit
LAMBDA_TIER=jit
```

Preserve script setup, warmup, workload, self-timed interval and exact oracle.
Record process/compilation time separately and account for Node tiering in the
workload. Before/after ratios use newly paired measurements of frozen binaries;
do not divide a fresh candidate by Result9's historical median.

Use an identical-control peer and rotate all six lane orders for repeated MVP
comparisons. Recheck binaries/sources at completion. Suspected slowdowns receive
at least 15 pairs and further runs where intervals remain too wide. Inspect
control/control variation and host activity before attributing a change.

The user requested existing evidence only for the four long jq workloads; do
not run fresh jq timing without a new request. Preserve Result9 provenance and
mark this phase's jq performance acceptance as unmeasured. No historical jq
timing is promoted into fresh evidence.

### 10.4 Performance acceptance

- Compare all 87 previous workloads individually, with correct outputs. An
  aggregate gain cannot excuse a confirmed regression in a normal path.
- Require no confirmed new slowdown against both control lanes; intervals
  spanning one are inconclusive, not proof of exact equality. Extend noisy
  comparisons rather than claiming a universal no-regression guarantee.
- Inspect MIR/helper counts for existing own-field, dense-index, integer/scalar,
  call and allocation kernels. Include CD, Richards, triangl, Havlak, regexredux,
  text_search and jq alongside the dedicated map/array microbenchmarks.
- Account for compile/process time, GC work and allocation/RSS separately.
  Sparse cases should scale storage with present elements and metadata; accessor
  cells or prototype side state must not be allocated for every plain object.
- Compare shared Lambda map/array/closure workloads because extraction changes
  host code too. Record those separately from MVP workload coverage.

## 11. Known limits, review points and final record

Result9 records 87 correct workloads, MVP 100/100 normal/forced-GC tests,
ActivationCore 10/10 in both modes, Test262 40,261/40,261, and Lambda
6,607/6,608 with the known `edit_view_only` failure. Its jq timings are single
observations, including `jq_mix` contention. The earlier `log_pipeline` +1.3%
issue is against an older control. These are historical facts from
[Result9](../../test/benchmark/js_mvp_lmd/MVP_Result9.md#validation-and-limits),
not fresh gates for this phase.

P0 must settle code-level attachment/packing choices and exact adapter signatures.
P3 must audit sparse values crossing Lambda readers; P4 must audit callable
proxy representation and brand checks. These are implementation review points,
not permission to weaken the approved scope or add `is_normal`. Report a true
formal-contract conflict before changing the ruling. Validate platform-specific
behavior on available platforms; label unmeasured platforms explicitly.

At completion publish the implemented scope, helper changes, fixture/gate
results, per-workload paired evidence, source/binary hashes and remaining limits.
Keep the existing Result3–9 history unchanged. The next result number is used
when a new result is requested; this plan does not create a performance result.

- [x] P0 reuse/ABI/guard inventory and exact Result9 control captured.
- [x] P1 prototype/descriptor foundations implemented; focused fixtures pass.
- [x] P2 accessors implemented; canonical hashmap oracle and MIR preflight pass.
- [x] P3 sparse storage and guarded Lambda readers implemented; general raw-value
  export to other Lambda bulk consumers remains outside the exposed boundary.
- [x] P4a/P4b all scoped traps, forwarding, invariants and revocation implemented;
  focused normal/forced-GC fixtures pass.
- [ ] P5 aggregate performance acceptance; regressions remain and other
  platforms and fresh jq measurements are unmeasured.

## Implementation checkpoint (2026-10-11)

The receiver-aware property kernel, mutable prototype links, ordinary
accessors/descriptors and integrity operations, sparse storage, Reflect, and
all eleven property/prototype/extensibility proxy traps are implemented.
Callable/constructable proxies include forwarding, invariants and revocation.
Ordinary objects retain Lambda Map shapes; no `is_normal` or equivalent
container/type/side-table bit was added.

### Concrete guard and storage choices

- Existing exact shapes and field flags authorize own data access. No extra
  universal property-summary bit was needed. Indexed descriptors remove their
  corresponding physical array values; a present dense slot therefore proves
  an ordinary own element. MIR checks current element kind, bounds and presence
  after the RHS. Misses, holes and sparse storage retain semantic dispatch.
- Ordinary Set walks reuse passive descriptor/prototype inspection without
  allocating a frame for each ancestor. Accessor invocation, proxy dispatch,
  lazy function preparation and exotic receiver inspection retain their own
  precise roots. Default indexed definitions enter the existing array storage
  helper before general descriptor conversion; length and extensibility checks
  still precede the write (**D5.2–D5.3**, **D8.4.3v2**). Small numeric property
  strings reuse Lambda's existing immortal ASCII table.
- Prototype assumptions use a **compile-time closed-unit proof**, not a new
  runtime epoch. Explicit reflection/mutation, accessors, proxies or unproved
  computed keys select live property dispatch and disable inherited-value
  caches and numeric regions. Safe numeric key inputs are followed through
  source writes, constructor/method calls, static factories and nonescaping
  backing arrays (including numeric push/pop stacks). Returning `this` retains
  a class family only when the method cannot escape or be replaced. Finite
  integer string suffixes/prefixes are checked against native and source member
  names; reflected global scalar keys require an inventory excluding alias
  writes and bulk projections. Escaped callables, parsed/foreign records and
  unknown inputs invalidate the applicable proof. This changes admission, never
  the value ABI. Native shape inventory planning does not activate the runtime
  library or general coercion. Existing own-field caches remain usable for
  shared immutable shapes; inherited values use live lookup in mutable units.
- Passive top-level prototype initialization can retain fixed inherited lookup.
  The proof rejects later mutation, aliases, destructured prototype escapes,
  intervening observable calls and constructor replacement. Implicit writes
  through iteration/destructuring also invalidate scalar parameter refinement.
- Sparse guards use the sign of the existing element-kind byte. A separate
  compiler inventory can exclude sparse storage using inferred allocation and
  write bounds plus the shared runtime density rules. Uncertain constructors,
  aliases, length writes, bulk operations and reflection retain the guard.
  This is a compile-time fact, not object metadata or a runtime epoch.
  The existing integer range solver now bounds intrinsic `Math.floor` of
  proven nonnegative integer division; replacement, negative-zero and NaN cases
  remain generic. `Object.assign` and member targets in patterns/iteration
  contribute to hole/property admission just like explicit writes.
- Dense wide-number updates reuse `array_store_owned` without a density query.
  Object projections reserve their output once with
  `array_reserve_append_slots`; local iteration bindings adopt scalar values
  before the body without an intermediate snapshot. Captured bindings and
  observable paths retain earlier ownership (**D2.6.4**, **D5.2–D5.3**).
- Ordinary function construction reuses its nominal shape when the closed unit
  excludes prototype replacement and new-target is the callee. Live and proxy
  new-target paths retain validation and scoped prototype roots. Array method
  lowering now shares `sequence_method_runtime`: potentially effectful argument
  coercion uses the existing runtime kernel, which snapshots length before
  conversion (**S1.11**; [ECMAScript Array methods](https://tc39.es/ecma262/multipage/indexed-collections.html#sec-array.prototype.join)).
  String-only joins retain `fn_join2`; other joins reuse the shared converter
  and recursion guard. Callback-free dense fill/search/push/pop retain the
  method's storage proof without repeating its kind guard inside the operation.
  Empty searches skip from-index coercion, and proxy shift preserves Get/Has
  order. Eight failures were reproduced on the prior frozen binary; the
  fourteen coercion/order cases pass on the updated release.
- General Set admits an existing writable own data slot on a plain carrier when
  owner and receiver coincide. It reuses `map_shape_set_resolved`, including
  shared retyping/ownership, before descriptor construction. Accessors, foreign
  declared fields, different receivers, proxies and missing slots retain the
  full algorithm (**D3.2.4v4**, **D3.4.5**, **D5.2–D5.3**).
  The existing store specialization can retain that exact shared-shape proof
  in mutable units; misses always enter semantic Set. The existing
  `mvp_lmd_cache_property` helper was promoted to the module header for reuse,
  with no new cache layout or feedback mechanism (**D8.4.1v2**). Negative cases
  cover redefinition as an accessor/non-writable property, deletion followed by
  an inherited setter, and scalar-to-object retyping. Passive descriptor reads
  place roots only around lazy function preparation. Array edits allocate
  output arrays only when needed and reuse the combined Has/Get operation.
- Enumeration reuses shape `NameId` records and the existing own-key projection
  when a fresh inspection proves that ordinary ancestors have no enumerable
  string properties. `for_in_own_keys_suffice` performs no guest callbacks;
  proxies, function preparation and enumerable ancestors retain the full path
  (**D3.4.3v5–D3.4.5**, **S8.2.2v5**). The proxy fixture checks keys and trap
  counts: **S1.11** delegates this to
  [ECMAScript 14.7.5.9](https://tc39.es/ecma262/multipage/ecmascript-language-statements-and-declarations.html#sec-enumerate-object-properties),
  which permits different enumeration mechanics for exotic prototype chains.
- Compiler helpers `dense_array_loop_node` and `dense_array_loop` admit a bounded
  while-loop region, guard each array's existing storage kind before entry,
  and reuse the loop lowering for dense and generic paths. They reject calls,
  property writes, array rebinding, declarations, captured/external state and
  coercions of possible objects. Only native numeric locals may change; their
  iteration snapshots reuse the existing numeric-loop replay pattern. A hole or
  bounds miss (including a numeric non-index) restores the current iteration
  before semantic lookup, so getters and inherited properties retain their
  effects and order. Unknown receivers first pass an Array type guard; proxy and
  array-like receivers use the original loop. Entry guards cannot
  precede a required initializer or TDZ check. Scalar ownership and the sparse
  fallback remain intact.
  These are compiler facts, with no container bit, side table or new feedback
  mechanism (**D8.4.1v2**, **D5.2–D5.3**). Normal/forced-GC fixtures pass;
  aggregate performance acceptance remains pending.
- Named member keys use the existing frontend name pool at compile time.
  `.length`, named methods and named `super` accesses need no repeated runtime
  property-key conversion; computed keys retain their conversion and effects
  (**S8.2.2v5**, **D1.3v3**). A Text Search profile identified that converter as
  its largest sampled native leaf before this change.
- The existing `mvp_lmd_inherited_member_flags` accepts semantic lookup as an
  option. Ordinary Map chains use the shared traversal without allocating a
  root frame at every missing property. An accessor immediately enters its
  rooted call adapter; arrays, functions and proxies retain receiver-aware
  dispatch. Pure Lambda lookup still rejects guest traps (**S12.1.1v2**,
  **D5.2–D5.3**).
- `guarded_array_index_extent` supplements sparse admission with an enclosing
  loop's upper bound. The index must be a local Number; captured/reflected
  bindings, earlier writes, nested-loop revisits and pattern writes reject
  the proof. Later writes are allowed only when the condition is rechecked
  before another observation. Its bound applies only to nonnegative integer
  array keys, never to scalar typing or arithmetic specialization.
- Coercion imports retain their existing implementations. Separate `_fixed`
  aliases narrow reentry effects only when the closed unit excludes coercion
  hooks, dynamic properties, reflection, iterators and Symbols (**D8.4.3v2**).
  The compiler fact does not mark an object as normal. The new
  `observe_coercion_member` shares admission for object properties, class
  methods and class fields; its field case fixes a reproduced capability error
  for a class field containing `valueOf`. Normal/forced-GC and Node fixtures
  pass; the effect narrowing has not yet received paired performance validation.
- Existing `JsElementsKind` selects sparse storage. The shared
  `js_array_try_promote_sparse_dense` accepts a GC-buffer option for MVP; its
  existing density/length limits bound promotion. It reserves scalar-tail space
  before copying, roots the owner across allocation, and retains indexed
  descriptors and holes. No per-object promotion flag was introduced.
  `SparseArrayMap` entries
  own `TypedItem` payloads; GC traces these and accessor/proxy edges precisely
  (**D2.6.4**, **D2.7.1**, **D5.2–D5.3**). No numeric GC cells were introduced.
- Live JS prototype links use private traced properties. `TypeNominal.base`
  remains the Lambda declared ancestry (**D2.6.10**). Guest writes reject
  foreign declared field contracts (**D3.2.4v4**); passive Lambda readers reject
  accessors/proxies and sparse element access instead of invoking JS callbacks
  or indexing beyond physical storage (**S12.1.1v2**).

The general raw-value export adapter to arbitrary Lambda bulk consumers is
outside this phase. Core guarded reads and CLI printing are covered; this is
not a claim that every legacy `items`/`memcpy` consumer accepts guest sparse
arrays. Cross-runtime subclass/function adapters remain outside the phase.

### Reuse and helper changes

Shared extraction reuses `js_map_store_accessor`, reader-parameterized
`js_descriptor_from_object_with_reader`, `JsAccessorCell`, `js_proxy_carrier_new`,
`JsProxyData`, `JsPropertyOps`, and the existing sparse-table allocation,
lookup/store/index/density primitives. Full LambdaJS retains its own semantic
wrappers. MIR generic imports are classified for reentry; physical readers
retain narrower effects (**D8.4.3v2**).

MVP adapters include `mvp_lmd_set_completion`, `mvp_lmd_define_own_property`,
descriptor compatibility/SameValue, argument-list and public-key projection,
sequence length/species observation, proxy trap/invocation adapters, and JSON
`toJSON` resolution. `mvp_lmd_is_ordered_map` shares the actual collection brand
check across Map calls, collection methods and spread. Compiler helpers
`property_spelling`, `property_receiver_class`, `property_key_may_name`,
`native_property_matches`, `primitive_property_key`, `fixed_prototype_setup`,
`unit_excludes_sparse_arrays`, `guarded_array_index_extent`,
`observe_coercion_member` and `computed_read_is_observation` implement the
admission proof. `branch_sparse_array` shares emission of the existing storage
guard; `sequence_method_runtime` shares argument rooting and fallback calls.
These helpers were disclosed before implementation. No new per-site
feedback cells were introduced (**D8.4.1v2**).

### Current evidence and acceptance still pending

Last comprehensive benchmark/gate checkpoint: `temp/mvp_properties/passive-set/`, SHA-256
`484238c5949fee8b51d3b674a94c2becdf91fc193d796f11d3c65b6da1a2d420`.
The archive retains its source patch, untracked files, configuration and build log.

- **130/130 MVP tests normally and under forced GC/poison**:
  `mvp-passive-set-normal-clean.log` (21.636 seconds) and
  `mvp-passive-set-forced-clean.log` (150.880 seconds). The interrupted
  overlapping run is excluded (`passive-set-test-overlap.md`). Exact test
  binary hash, flags and replay timestamps are retained in
  `mvp-passive-set-validation.json`.
- **357 extracted snippets match Node v22.13.0**:
  `node-passive-set.json`, including exact fixture hash,
  source, expected and actual results. This is a subset of the C++ suite.
- Class-field coercion failure reproduced on the frozen traversal release
  (`class-field-coercion-before.stderr`); the unchanged fixture passes after
  shared coercion-member admission includes class fields.
- **Eight coercion/order failures reproduced** on the constructor release;
  all fourteen cases passed on the array-coercion checkpoint (`coercion-before-corrected-fixture.json`,
  `coercion-after.json`). The discarded initial throw fixture referenced an
  unsupported global RangeError constructor; the corrected fixture throws a
  scalar and checks propagation directly.
- Latest **shared LambdaJS Test262: 40,261/40,261, zero retries**, 56.6 seconds:
  `test262-baseline-passive-set.log`. Latest
  **Lambda baseline: 6,675/6,676**,
  retaining the known `edit_view_only` failure
  (`test-lambda-baseline-passive-set.log`). Its MVP suite passed during an
  overlapping focused run; the separate clean serial replay above supplies
  the authoritative MVP result.
- **84/84 non-jq benchmark contracts pass** the exact output oracle and
  pinned-MIR preflight on this release (`passive-set-oracles/comparison.json`,
  `--population all --runs 0`): 83 prior workloads plus canonical Hashmap.
  This validation-only run excludes incidental elapsed times from performance
  evidence. The four jq workloads retain their historical Result9 evidence;
  they were not executed in this phase.

Diagnostic profiles are separate from timings. Linker maps resolve the exact
frozen binaries. Microdiff's initial profile showed repeated visited-key
comparisons and name interning; the subsequent three-pair enumeration screen
measured **135.356 / 100.732 / 100.470 ms** (candidate/control/identical peer),
down from the array-coercion screen's **309.164 ms** candidate median.
The dense-loop Text Search profile identified repeated runtime conversion of
static member names, now removed. The static-key checkpoint's completed
three-pair screen (`static-keys-screen/comparison.json`) measures Text Search at
**6,845.958 / 5,973.424 / 5,969.985 ms**, Microdiff at
**135.205 / 100.381 / 100.535 ms**, and Log Pipeline at
**5,120.195 / 4,709.910 / 4,689.833 ms**. Numerical controls also retain
3–13% candidate slowdowns. Neither profiles nor short screens establish final
acceptance. A subsequent traversal capture was stopped after the unchanged
control timings rose by roughly 5–7x and the two controls diverged;
`traversal-screen/STOPPED.md` records the incomplete evidence. Concurrent
desktop CPU activity was observed, but the exact cause is unproven. No
unrelated processes were changed.

The later coercion-fields four-workload screen retained the slowdowns:
Microdiff **134.308 / 100.189 / 99.944 ms**, Splay
**64.228 / 59.633 / 59.906 ms**, SHA1 **15.056 / 13.967 / 13.964 ms**.
The five-pair Set-traversal screen (`set-traversal-screen/comparison.json`)
measured Microdiff **125.995 / 100.395 / 100.402 ms**, Splay
**64.987 / 59.763 / 60.190 ms**, and SHA1 **14.985 / 14.078 / 13.977 ms**.
These are separate checkpoint screens, not a direct A/B attribution of the
Set changes.

The **passive-set** release has a completed 15-pair capture with identical
control peers, all six lane orders, matching output and pinned MIR
(`passive-set-regressions/comparison.json`). No builds, tests or profiles ran
alongside these measurements. Medians are script self-reported milliseconds:

| Workload | Candidate | Result9 control | Identical peer | Candidate/control change |
|---|---:|---:|---:|---:|
| CD | 163.743 | 160.963 | 161.867 | +1.7% |
| Richards | 75.454 | 73.610 | 73.726 | +2.5% |
| SHA1 | 14.814 | 13.987 | 13.910 | +5.9% |
| Splay | 63.907 | 59.161 | 59.304 | +8.0% |
| Map lookup | 7.457 | 7.443 | 7.478 | +0.2% |
| Log Pipeline | 5162.519 | 4720.087 | 4708.747 | +9.4% |
| Microdiff | 124.076 | 100.195 | 100.278 | +23.8% |
| Text Search | 6710.722 | 5979.319 | 5976.406 | +12.2% |

All seven slower rows other than Map lookup have paired 95% intervals above
one against both controls; the control/control intervals include one. Map
lookup is inconclusive within noise. Thus **the Result9 comparison fails the
no-regression gate**. This comparison alone does not isolate the phase's cost:
Result9's source base is `e81c7b4a8bcb226f3aba3f6691abeeb7de5bee90`, while this
phase began at `9c63181d6a3b1fa16560023dfe1425117e2b0fb9`. The recorded MVP
compiler/RegExp/activation/fiber sources match the phase-entry commit, but other
shared runtime changes landed between the bases. In particular, the Result9
comparison cannot attribute small shared Lambda deltas to this phase.

A clean phase-entry release was built in an isolated checkout with
`make build-release-compile` (Developer Guide §7.2). Its frozen binary is
`prephase-head-control/control.exe`, SHA-256
`12f4a01bd733aa4571feed4c896e81489b4f958c679eec1a2469c5b1cb3c1e77`;
the manifest records the clean source commit, configuration and build command.
The capture runner's explicit `--control-record` option verifies this binary
against its manifest, independently of the reused Result9 workload catalog.
The completed 15-pair comparison against this exact phase-entry build
(`passive-set-head-regressions/comparison.json`) confirms the regressions.
All outputs match, with pinned MIR and six rotating lane orders; no builds,
tests or profiles overlapped timing. Self-reported median milliseconds:

| Workload | Candidate | Phase-entry control | Identical peer | Change |
|---|---:|---:|---:|---:|
| CD | 166.369 | 163.875 | 164.748 | +1.52% |
| Richards | 76.816 | 74.975 | 74.726 | +2.46% |
| SHA1 | 14.790 | 13.965 | 13.973 | +5.91% |
| Splay | 64.491 | 60.091 | 60.012 | +7.32% |
| Map lookup | 7.299 | 7.317 | 7.496 | -0.25% |
| Log Pipeline | 5160.126 | 4730.555 | 4695.782 | +9.08% |
| Microdiff | 124.265 | 100.204 | 100.372 | +24.01% |
| Text Search | 6726.120 | 5989.195 | 5988.424 | +12.30% |

Seven rows have paired 95% intervals above one against both controls;
Map lookup remains inconclusive. The control/control intervals include one.
The phase-entry shared Lambda replay (`passive-set-lambda-head-peer/comparison.json`,
21 triples) also confirms a Deriv regression: **22.491 / 21.690 / 21.785 ms**
(candidate/control/peer). Sieve (**0.028 / 0.027 / 0.027 ms**) and JSON generation
(**23.134 / 22.971 / 22.982 ms**) are inconclusive. These results fail P5.

The next local change introduces `array_indices_use_own_storage`: a fresh,
operation-local proof after argument coercion and species observations. It
rejects indexed shape descriptors, inherited indices, proxies and unsuitable
write policy, then reuses the existing indexed storage helper via an internal
operation option. No container flag or cached proof is added. The proof lasts
only across callback-free storage work; scalar homes remain destination-owned
(**D2.6.4**, **D3.4.3v5–D3.4.8**, **D5.2–D5.3**). Shared Lambda map reads also
avoid a duplicate accessor check while retaining the passive-reader rejection
(**S12.1.1v2**).

The resulting `array-storage` release (SHA-256
`d93aa81eacece4148851d6fda1f8876c38ca73f467d24c5df998321f253c218e`)
passes **131/131** tests normally and with forced GC/poison, and **373/373**
extracted Node comparisons (`array-storage-validation.json`). Its seven-pair
screen against `passive-set`, with an identical control peer, measures Microdiff
at **112.535 / 123.946 / 124.109 ms**: a 9.2% improvement, with paired 95%
intervals below one against both controls. CD, Richards, SHA1 and Splay remain
inconclusive against both controls in this small screen; Splay's control/control
interval excludes one. These five rows do not establish full phase acceptance.

The next change gives the existing `mvp_lmd_class_property_fixed` MIR import a
C-ABI entry point backed by a compile-time specialization of the same property
implementation. The compiler's existing closed-unit proof selects it; mutable
units retain general dispatch and native accessor handling remains shared.
This removes redundant mutable-prototype/proxy checks without duplicating the
kernel or adding per-container state (**D1.3v3**, **D8.4.3v2**).
The frozen `fixed-dispatch` release (SHA-256
`8ae55da50302142c1b581c48cd38859a368b42982fd158ee12e08ebffaa97f74`)
passes **132/132** normal tests and **41/41** focused forced-GC/poison tests;
the four new snippets match Node (`fixed-dispatch-validation.json`). The
seven-pair screen against `array-storage` confirms improvements in Richards
(**75.406 / 76.310 / 76.616 ms**) and Splay
(**63.077 / 65.541 / 65.711 ms**), with paired 95% intervals below one against
both controls and control/control intervals including one. CD and Microdiff
remain inconclusive (`fixed-dispatch-screen/comparison.json`).

The density proof is being extended through an optional local-binding argument
to `unit_excludes_sparse_arrays`. It reuses Lambda's density thresholds and the
existing bounded-index analysis, rejecting aliases, captures, unbounded growth,
method mutation and pattern writes. A compile-time binding fact can then omit
sparse guards on that allocation without changing element-type facts or adding
runtime object state. SHA1's fixed 80-element work array motivated this change;
its separate input array still requires sparse fallback. The MIR assertion
passes for a bounded local array in a unit that also creates sparse storage.

The same investigation exposed fresh `Array(...)` and rest-parameter writes
using assignment instead of own-property initialization: inherited indexed
setters ran and inherited read-only values blocked construction. Four cases
reproduce on the frozen `fixed-dispatch` release (`array-creation-before.json`).
Fresh arrays, map/filter results and internal receiver cells now use the existing
`array_index_write(..., initialize=true)` option; scalar ownership remains shared
(**D1.3v3**, **D2.6.4**). The five creation fixtures match Node and pass in MVP.
An initial positive fixture using an array-pattern member target hit the existing
unsupported-syntax boundary; that syntax was not added by this phase. The
accepted local-density fixture inventory retains supported map-pattern and
iteration-target cases.

The `local-density` release (SHA-256
`c96770f8b560d0cd45835811768e4e142f001c12906adea16c5fd653b501b705`)
passes **134/134** normal and forced-GC/poison tests, plus **393/393** extracted
Node comparisons (`local-density-validation.json`). Its seven-pair screen against
`fixed-dispatch` is inconclusive for all five rows (CD, Richards, SHA1, Splay and
Microdiff): no candidate interval excludes one against both controls. SHA1's
2% lower median is not a confirmed improvement; CD's control/control interval
also excludes one (`local-density-screen/comparison.json`).
The subsequent diagnostic change extracts Lambda's existing opt-in address
logger as `jit_log_code_addresses` and reuses it from MVP. It is disabled during
timing; sampling profiles remain separate diagnostic evidence (**D1.3v3**).

The frozen `profile-addresses` release (SHA-256
`903c60b5aec8da9e15d9e6428e497af861a4ed010e239a40dd6bd411c19321d2`)
confirms Text Search spends most sampled CPU in generated loops: about 37% in
naive search, 34% in KMP and 18% in Boyer–Moore among the reported leaf samples.
The array storage proof accounts for about 1.6%. Exact JIT addresses, linker map,
profile source hash and checksum are retained in `text-search-address-profile.json`
and `text-search-addresses.sample`. The diagnostic uses four times the canonical
round count and is not performance acceptance evidence.

The next compiler change extends the existing dense `while` versioning to
eligible outer `for` loops. Initialization runs once; numeric local snapshots
restore the current iteration before a failed bounds/hole check replays through
ordinary JS semantics. Body-local lexical initializers run again during replay.
Calls, captured/external writes, array rebinding and observable coercion remain
excluded. Updates must be local numeric operations that cannot cause a storage
bailout. This hoists receiver/storage guards across a nested loop without a
runtime container flag or new helper (**D8.2.1–D8.2.6**, **D8.4.1v2**).
The frozen `for-replay` release (SHA-256
`8bcb1956f453f8860ebec33e9c766e6d106bdae28c429a0a16e992a83282723e`)
passes **135/135** normal and forced-GC/poison tests and **407/407** extracted
Node comparisons (`for-replay-validation.json`). Fourteen new cases cover
initializer retention, partial-iteration rollback, nested loops, lexical locals,
labels, holes, accessors, Proxy arguments and length mutation. The MIR fixture
checks outer entry guards and the separate nested generic fallback. Its completed
seven-pair Text Search screen against `profile-addresses` measures **5104.905 /
6235.416 / 6171.378 ms** (candidate/control/identical peer), an 18.1% median
improvement. Paired 95% ratio intervals are **[0.8133, 0.8288]** and
**[0.8206, 0.8302]** against the two controls; the control/control interval
**[0.9820, 1.0055]** includes one (`for-replay-screen/comparison.json`).
All outputs and pinned-MIR checks pass. This isolates the loop change; the
full comparison against phase entry remains outstanding.

The same checkpoint's shared Lambda replay (21 triples) leaves JSON generation
inconclusive (**22.832 / 22.845 / 22.909 ms**) and confirms Deriv remains slower
(**22.164 / 21.417 / 21.351 ms**, both paired intervals above one;
`for-replay-lambda-head-peer/comparison.json`). The next change moves Proxy
rejection into the no-payload path: `js_proxy_carrier_new` keeps `Map::data` null
and stores traced edges in its trailing carrier. The boundary fixture checks
both key routes and verifies that guest property mutation preserves that layout
(**D2.6.6v3**, **S12.1.1v2**).

A separate Microdiff profile on `for-replay` shows repeated prototype/storage
inspection among its leading runtime costs (`microdiff-for-replay-long-leaves.json`).
The diagnostic round count is extended and checksum-checked against Node; its
elapsed time is not benchmark evidence. Read-only array bulk methods now rely
on the existing present-element path, with policy lookup and semantic dispatch
only on a miss. The whole-chain storage proof remains for writes. This removes
redundant scans without persisting a proof across callbacks or adding state.
The frozen `present-read` release (SHA-256
`6071d20c8175fbb2d2471591a4c9e66dce0bdb9e3b8e9aad78e4bb466c0186bc`)
passes **135/135** normal tests, **35/35** focused forced-GC/poison groups and
**407/407** extracted Node comparisons (`present-read-validation.json`). Its
seven-pair Microdiff screen measures **106.228 / 112.408 / 112.921 ms**, a 5.5%
improvement against `for-replay`; both paired 95% intervals are below one and
the control/control interval includes one (`present-read-screen/comparison.json`).
The shared Lambda replay against phase entry (21 triples) leaves Deriv and JSON
generation inconclusive: Deriv **21.437 / 21.554 / 21.224 ms**, with intervals
**[0.9759, 1.0271]** and **[0.9825, 1.0394]**. Sieve's roughly 27-microsecond
samples are quantized; they do not establish a regression against both controls
(`present-read-lambda-head-peer/comparison.json`).

A separate Log Pipeline profile identifies property definition/storage among
the leading costs. Fresh object literals now reuse Lambda's existing physical
map store for default own data properties, bypassing inherited setters and
non-writable properties as required by JS creation semantics. Descriptor and
prototype-setting literal forms retain their own operations (**S1.11**,
**D1.3v3**, **D3.4.3v5–D3.4.5**). The frozen `literal-storage` release (SHA-256
`fd3c3e2e9abe48665a23fe905ae19942381b3253569584f1a782a1765b308476`)
passes the previous 135 groups, but its new literal group exposes the existing
object-method `super` admission gap; **135/136 is not acceptance**. Its seven-pair
screen against `present-read` confirms Log Pipeline **4264.447 / 5145.545 /
5134.428 ms** (17.1% improvement) and Microdiff **102.690 / 106.484 / 106.174 ms**
(3.6% improvement). Both rows' paired intervals exclude one against both controls;
control/control intervals include one (`literal-storage-screen/comparison.json`).
These incremental screens do not replace the phase-entry comparison.

The home-object gap is addressed through an optional Item slot in the existing
Lambda closure environment, counted by `closure_field_count`. Object methods
retain their literal home; nested arrows retain the enclosing Function and the
existing shared receiver cell. Only methods/arrows using `super` acquire the
extra slot. The callable's slot index fits existing padding, enforced by a size
assertion. Existing `mvp_lmd_class_super` reads the live prototype; the common
call/spread path retains `new.target`, constructor TDZ and derived field
initialization. No new helper or independent closure representation is added
(**D6.2.3v2–D6.2.4**, **D5.3**, **S1.11**). The frozen `literal-home` release
(SHA-256 `60c1bba557fee1cd44b0248fb5f96a7832e18b9d8f30e714794d3673fbbc198e`)
passes **137/137** normal and full forced-GC/poison groups, **439/439** extracted
Node comparisons and **20/20** release home-object snippets
(`literal-home-validation.json`). The first full run retained an obsolete
lexical-super rejection fixture; it now checks the supported value, and the
complete rerun passes. Constructor spreads failed before the shared-call fix
and now retain argument order, new-target and derived initialization. The
15-pair phase-entry comparison is complete
(`literal-home-head-regressions/comparison.json`):

| Workload | Candidate ms | Phase-entry control ms | Identical peer ms | Paired conclusion |
|---|---:|---:|---:|---|
| CD | 165.260 | 163.432 | 162.686 | +1.1%, confirmed |
| Richards | 76.851 | 73.954 | 74.173 | +3.9%, confirmed |
| SHA1 | 14.678 | 13.934 | 13.922 | +5.3%, confirmed |
| Splay | 63.004 | 61.292 | 61.376 | +2.8%, confirmed |
| map_lookup | 7.328 | 7.389 | 7.469 | inconclusive |
| Log Pipeline | 4306.564 | 4775.120 | 4738.424 | -9.8%, confirmed |
| Microdiff | 102.774 | 100.657 | 100.627 | +2.1%, confirmed |
| Text Search | 5092.655 | 5984.112 | 6009.522 | -14.9%, confirmed |

All outputs and pinned-MIR checks pass. Confirmed rows exclude one against both
control lanes; every control/control interval includes one. This remains a
failed performance gate because five ordinary workloads are slower.

The following diagnostics retain source hashes, release linker maps and sampled
JIT addresses. Canonical GC runs show equal collection counts; Richards and SHA1
have no collections. Extending CD's repetitions changes its heap/compaction
behavior, so that profile cannot attribute canonical GC cost. Richards' generated
scheduler dominates its profile. SHA1's core and string-packing functions account
for most reported leaf samples (`richards-literal-home-leaves.json`,
`sha1-literal-home-leaves.json`). These profiles are not timing evidence.

The next compiler change preserves the inferred primitive domain on captured
scalar reads. The previous emitter lost that domain when initialization was not
proven and emitted generic coercion branches. The existing `read_binding` helper
now retains possible `undefined` for hoisted `var`, performs the same TDZ check,
and retains scalar-home snapshots; reflected globals and object-valued domains
keep their existing paths (**S1.11**, **D2.6.4**, **D6.2.3v2**, **D8.2.1–D8.2.6**).
No helper or runtime state is added. The `captured-scalars` release (SHA-256
`852765fe5355647b86bfc5eb1076b6a739ffd0d66e88d1527ad4905dacc2f16e`)
passes **138/138** normal groups, **9/9** focused forced-GC/poison groups and
**17/17** new Node comparisons (`captured-scalars-validation.json`). Its MIR
fixture excludes generic string-to-number conversion from the captured numeric
loop. Its completed seven-pair screen confirms SHA1 **13.253 / 14.611 /
14.517 ms** (9.3% faster), with paired intervals **[0.8892, 0.9299]** and
**[0.8974, 0.9313]** (`captured-scalars-screen/comparison.json`). The identical
control lanes also show a small drift, **[0.9893, 0.9997]**; final phase-entry
confirmation remains required. CD, Richards, Splay and Microdiff are inconclusive.

An experiment passing an already classified Map into prototype traversal passed
139 normal groups, 11 focused stress groups and eight Node cases. Its 15-pair
Microdiff comparison was inconclusive (**103.521 / 103.418 / 103.567 ms**,
`borrowed-prototype-screen/comparison.json`). The runtime change was reverted;
the semantic fixtures remain.

Ordinary method/function receivers now avoid the constructor-only TDZ guard.
The existing `check_tdz` emitter still checks derived constructors and lexical
receiver arrows; `new.target` does not depend on receiver initialization.
No runtime state or helper is added (**S1.11**, **D6.2.3v2–D6.2.4**,
**D8.2.1–D8.2.6**). The frozen `receiver-tdz` release (SHA-256
`005372918d303a7f7596027935d96177d0c10d6014a3ad412e37137b8f99470c`)
passes **140/140** normal groups, **13/13** focused forced-GC/poison groups and
**475/475** extracted Node cases. The seven-pair incremental screen confirms
Richards **72.862 / 77.121 / 77.044 ms** (5.5% faster); paired intervals
**[0.9306, 0.9528]** and **[0.9368, 0.9502]**, with the peer interval including
one. CD, Splay and Microdiff remain inconclusive
(`receiver-tdz-screen/comparison.json`). These incremental gains do not establish
net phase acceptance.

The `own-shape-literal` experiment added `TypeMap::has_private_keys` in
existing padding and summarizes only its own bounded shape chain. Shared hash
insertion/build and transition creation maintain the fact; detached clones
recompute it. Prototype lookup can omit a private-link probe when none exists.
It said nothing about inherited properties, accessors, proxy behavior or sparse
storage; it was not a replacement normal/special flag (**D3.4.3v5–D3.4.5**,
**D3.4.7**). Fresh array literal initialization reuses `array_index_write` with
a reserved-slot count. The existing two-slots-per-item reservation covers both
visible entries and potential scalar homes. Constant prefix indices omit
redundant bounds checks; scalar ownership and post-spread generic handling
remain intact (**D2.6.4**, **D2.7.1**, **D5.3**). No helper is added.
The frozen release (SHA-256
`1144c36552290574acbeed435160cd25ae3ab135bc2ff605d6e0503c18266f51`)
passes **142/142** normal and full forced-GC/poison groups and **492/492** Node
cases (`own-shape-literal-validation.json`). The 15-pair incremental screen
confirms Splay **61.870 / 63.356 / 62.820 ms**, with intervals
**[0.9718, 0.9825]** and **[0.9763, 0.9899]**. Microdiff is inconclusive
(**103.340 / 103.324 / 102.895 ms**); both control/peer intervals include one
(`own-shape-literal-screen/comparison.json`). The private-key summary experiment
was removed because it showed no measurable benefit. Its semantic fixtures and
the reserved-literal emitter remain; there is no new TypeMap field.

The current Microdiff profile still shows property dispatch and root-frame setup
among its leading costs (`microdiff-own-shape-leaves.json`; diagnostic source
reuses the earlier checksum-verified extended workload). Inherited array reads
now avoid assignment-state preparation. Passive Map/Array prototype traversal
can tail-dispatch through existing helpers; accessors root their inputs at
invocation, and function/proxy paths retain a three-slot precise frame before
potential collection. Writes retain their existing ownership path (**D5.3**,
**S1.11**). New cases cover allocating getters/traps, prototype mutation, array
and function prototypes, abrupt completion and revocation. No helper is added.
The frozen `inherited-read` release (SHA-256
`cc92d060a80cd438e75981173be8bc44f06aa3c0d1b3999ec068ae4e01f3a884`)
passes **143/143** normal and full forced-GC/poison groups and **501/501**
extracted Node cases (`inherited-read-validation.json`). The first normal run
passed 142/143: the new revocation fixture referenced an unsupported global
`TypeError` constructor. The corrected fixture uses the existing exact TypeError
diagnostic check; the complete rerun passes. Shared Lambda passes **6,688/6,689**
including all 143 MVP groups; full LambdaJS Test262 passes **40,261/40,261** with
zero retries or unstable cases (`test-lambda-baseline-inherited-read.log`,
`test262-baseline-inherited-read.log`). The existing `edit_view_only` Lambda golden mismatch was reproduced
with the exact phase-entry control, whose stdout matches the candidate's
(`phase-entry-view-only.stdout`, `own-shape-literal-view-only.stdout`).

The `inherited-read-nonjq/comparison.json` capture is complete: **83/83** unchanged
output/backend contracts, seven interleaved measured triples, exact phase-entry
control plus its identical peer. Ten rows are slower against both controls at
the paired 95% interval: Havlak (+3.76% median), Queens (+2.99%), Towers (+1.52%),
Fasta (+2.83%), Knucleotide (+1.67%), Raytrace3D (+2.05%), Paraffins (+2.94%),
Puzzle (+6.26%), Triangl (+0.67%) and NQueens (+2.74%). Very short rows need
longer confirmation. Ten other rows improve against both controls, including
Matrix Statistics (-18.33%), Text Search (-15.58%), Log Pipeline (-8.33%),
Fast Diff (-7.80%) and SHA1 (-4.47%); these gains do not discharge the regressions.
Hyphen's medians are 47.200/41.521/41.735 ms (candidate/control/peer), but a
control outlier leaves one interval inconclusive. Full details and noise intervals
are in `inherited-read-nonjq-summary.json`; this is a screen, not P5 acceptance.

The preceding 15-pair incremental comparison against `receiver-tdz` confirms
Splay and Microdiff improvements (`inherited-read-screen/comparison.json`).
Hyphen's extended diagnostic profile verifies checksum 366591986 on both Node
and MVP. A setup-only file marker separated approximately 50 seconds of JIT
compilation from runtime sampling (`hyphen-inherited-read-ready-hot.sample`).
Its hot sample shows regex matching, allocation/root setup, GC and array stores;
extended repetitions alter GC frequency and are not canonical timing evidence.
The existing fixed-prototype proof is being refined for direct borrowing of
native Object/RegExp methods through call/apply/bind; prototype escapes and
mutations must retain semantic dispatch (**D8.2.1–D8.2.6**, **S1.11**). No new
runtime helper or container flag is proposed for this refinement.

The native-borrow checkpoint passes **144/144** groups normally and with full
forced GC/poison, and **517/517** extracted Node cases. Its edge cases exposed
an older inference error: numeric-array element facts were also applied to
named properties (including computed `constructor` and `map`). Element-domain
inference now requires an indexed key and preserves arbitrary inherited values
at a hole. The direct-borrow proof alone does not remove Hyphen's semantic
dispatch, because other dynamic keys remain observable. Inspection also ruled
out the suspected `.fill()`/iterator coupling: that collector uses the Map
method inventory, so its iterator flag remains unchanged.

The following default-species refinement reuses three passive own-descriptor
inspections. It bypasses a getter invocation only when the array has the default
prototype and inherited constructor and the live species getter uses the
existing native identity entry. Any own constructor, prototype change, user
getter, deletion, or proxy takes semantic dispatch. Existing `object_value` was
promoted to `mvp_lmd_object_value` in the module header; no duplicate helper,
identity cache or object flag was added (**D1.3v3**, **D3.4.7**, **D5.3**).
Twelve new Node cases and focused forced-GC checks pass; that checkpoint passes
145/145 normal groups.

The subsequent truth-dispatch refinement avoids decoding container headers:
MVP objects are always truthy, and only scalar tags need the existing numeric,
string and BigInt checks. Known domains also omit impossible alternatives
(**S1.11**, **D8.2.1–D8.2.6**). The `scalar-truth` release (SHA-256
`7ccbcb8aa0ab59eedc08f607be99bf212bf15ef1890e70278151be6ed37e6d50`)
passes **146/146** normal and full forced-GC/poison groups, **537/537** extracted
Node cases, and shared full LambdaJS Test262 **40,261/40,261** without retries
or unstable cases. Shared Lambda passes **6,690/6,692**: the preexisting
`edit_view_only` mismatch and a 60-second LaTeX phase-3 corpus timeout. That
exact LaTeX case passes separately in 43.634 seconds (its preceding passing
aggregate run took 56.193 seconds); retain the timeout as aggregate evidence,
not a clean gate. Its completed 83-workload capture passes every output/backend
contract. Seven interleaved rounds flag six rows slower than both controls:
object_retype_escaped (+6.87%), Brainfuck (+124.03%), JSON Gen (+4.18%),
Diviter (+0.05%), Triangl (+0.42%) and Hyphen (+10.75%). Ten rows are faster
against both controls, including Text Search (-15.09%) and Log Pipeline
(-8.55%). These are a screen, with longer confirmation required; see
`scalar-truth-nonjq-summary.json`. No new
helper is introduced for truth dispatch. These correctness counts do not imply
performance acceptance.

The next loop refinement promotes the existing bulk-operation proof as
`mvp_lmd_array_indices_use_own_storage` in the module header and MIR import
table. A loop version may reuse physical dense stores only after this passive
entry check excludes indexed descriptors, restrictive integrity metadata and
observable prototype links. Admission permits exactly one indexed write and
no other indexed access, calls or nested loops; an out-of-bounds key restores
numeric locals and replays the iteration before its store. Existing scalar-home
storage handles out-of-band Numbers. This is a lexical proof, with no retained
normal flag, cache or epoch (**D1.3v3**, **D2.6.4**, **D3.4.7**,
**D8.2.1–D8.2.6**, **D8.4.3v2**).

That capture also exposes a new Brainfuck regression (94.809 ms versus
42.320/42.281 ms on the two controls). The named-property inference correction
had widened a recursive Number-or-undefined typed-array index to `K_ANY`.
The fix preserves bottom facts during inference and the optional
numeric result domain for typed indices, retaining the existing capability
failure for unsupported typed-array named keys. Ordinary array named/inherited
values remain generic. This reuses the representation solver and introduces
no helper (**D8.2.1–D8.2.6**). The capability fixture is MVP-only; the six
new semantic cases have a separate Node oracle.

The combined `dense-write-optional` release (SHA-256
`af0b5da8b8239bdbf7d3e9850f04806b2b7280dc84b7cd40a061028d87ad2a69`)
passes **148/148** normal and full forced-GC/poison groups and **560/560**
extracted Node cases. An initial focused MIR assertion incorrectly expected a
register name retained by the compiler dump; the corrected structural assertion
and all semantic cases pass. The fresh Brainfuck oracle passes and its MIR
restores the numeric path, without the newly introduced semantic array calls.
That validation-only preflight overlapped correctness work and supplies no
performance evidence. A separate 15-round screen is running. This checkpoint
still requires final aggregate/shared-runtime performance acceptance.

Its 15-round screen restores Brainfuck to **42.478 / 42.281 / 42.548 ms**
(candidate/control/identical peer), with no confirmed slowdown against both
controls. CD, Richards, Matrix Statistics and Microdiff improve against both.
Object retyping remains slower (**5.452 / 5.295 / 5.295 ms**), as does Triangl
(**155.484 / 154.980 / 155.008 ms**). Havlak, Regex Redux and JSON Gen remain
inconclusive against the two controls. The separate three-round Hyphen capture
is triage only, not acceptance; its MIR admits the initialization loop, but the
initial samples do not show a clear gain.

A repeated object-retyping profile initially added logging and a thrown-value
check, which enabled additional library dispatch. That profile is excluded
from attribution. The corrected harness uses CLI result printing, preserves
the canonical function's normalized instruction sequence and matches Node's
checksum. It finds time in repeated map admission and lookup. The next prepared
change imports existing `typemap_hash_lookup_key` and reuses
`map_shape_field_to_item` for present own fields when the compiler proves an
ordinary map in a closed unit without receiver/property callbacks. Missing
fields retain the original fallback and scalar snapshots remain shared
(**D1.3v3**, **D2.6.4**, **D3.4.3v5–D3.4.5**, **D8.4.3v2**).

The same prepared checkpoint removes `Object.prototype.toString`'s redundant
outer root frame: successful brand checks are passive, semantic lookup owns
the receiver across reentry, and no receiver is used afterward. Any returned
tag is copied to native `StrBuf` storage before the next GC allocation. The
observable tag lookup, proxy errors and getter effects remain mandatory
(**S1.11**, **D5.2–D5.3**). Both changes reuse existing helpers; their new
fixtures pass in the frozen `own-map-tag` release (SHA-256
`97ab362f55b31153f67012f7997d7855f9c98f6901efede15d9cc584e93c2bc0`):
**150/150** normal and full forced-GC/poison groups, **577/577** extracted
Node cases. Its separate 15-round microbenchmark/regression screen completes:
object retyping improves against both controls (**5.227 / 5.299 / 5.293 ms**),
and Brainfuck remains inconclusive (**42.091 / 42.206 / 42.074 ms**). Map
iteration, object deletion and Triangl remain slower against both controls;
JSON Gen is inconclusive against the identical peer. Triangl also has a small
control/control displacement, which remains visible in the saved intervals.
The preceding Hyphen triage completes at **47.457 / 42.610 / 41.382 ms**;
the guarded initialization loop is not a demonstrated performance win.
The clean phase-entry source was relinked with a linker map for native profile
attribution; the resulting binary SHA-256 exactly matches the original
`12f4a01bd733aa4571feed4c896e81489b4f958c679eec1a2469c5b1cb3c1e77`.

The next compiler changes extend the existing local dense-storage proof to
unaliased, unoverridden `push`/`pop`/`join`, and omit unreachable array iteration
when the captured iterable is a proven native Map with no observable iterator
protocol. Both reuse existing analysis and runtime helpers (**D8.2.1–D8.2.6**).
The local-method checkpoint passes **151/151** normal and full forced-GC/poison
groups and **586/586** Node cases. A further passive own-query path
reuses `mvp_lmd_property_has` for canonical string keys on non-proxy maps, where
neither conversion nor lookup can reenter or collect. Other receivers and
keys retain rooted semantic dispatch (**D5.2–D5.3**, **S1.11**). The combined
`passive-own` release (SHA-256
`08641c43fc414f4b4b0b48daec0d292eb34d07b5d73ca2a12c5bb1736ba4f094`)
passes **153/153** normal and full forced-GC/poison groups and **600/600** Node
oracle cases. The refreshed shared LambdaJS Test262 gate is **40,261/40,261**,
with zero retry time and no unstable batches (62.5 seconds). The Lambda baseline
is **6,698/6,699**, with only the preexisting `edit_view_only` mismatch reproduced
by the exact phase-entry control. The LaTeX corpus passes this aggregate run
(53 seconds); the earlier timeout remains recorded above. Performance acceptance
is pending. No helper or storage flag is added by these three changes.

Its isolated 21-round screen (`passive-own-screen/comparison.json`) confirms
improvements in dense-array, map-lookup and escaped-retyping rows. Object growth
is still slower (**2.916 / 2.846 / 2.868 ms**), as are JSON Gen
(**5.809 / 5.656 / 5.638 ms**) and Triangl
(**154.781 / 154.206 / 154.250 ms**). Map iteration and object deletion are
inconclusive against the two controls. These results do not satisfy P5.
Native JSON Gen profiles use 80,000 repetitions for diagnosis only; both return
**61626**, and each normalized MIR differs from its canonical source only in
that outer repeat constant. They show string copying/allocation and GC as the
dominant costs, without isolating the small regression's cause.

The three-round Hyphen triage for that release remains slower at
**47.240 / 40.936 / 41.705 ms**. Separate candidate/control runtime profiles
repeat 64,000 rounds and both match Node's checksum **366591986**. Normalized
MIR is unchanged for every benchmark function; only the outer profiling harness
differs. This is diagnostic evidence, with more frequent GC than the canonical
workload. A prepared forwarding change removes the intermediate heap array
used by bound/call/apply paths: arguments go directly into shared `RootSpan`
slots and side-stack scalar homes, with incoming snapshots before prefix
getters and cleanup on abrupt completion (**D2.7.1**, **D5.2–D5.3**).
It adds no helper. A separate optional nonnegative mode on existing
`em_unbox_finite_int_item` removes sign extension for proven unchanged integer
array literals; other callers keep the signed default (**D2.4.3**,
**D8.2.1–D8.2.6**). The frozen `forward-span` release (SHA-256
`db3f69a068426a5729f8b18949d9d802ce70e1cafb90b7f1c1f4e641ed2471eb`)
passes **155/155** normal and full forced-GC/poison groups and **616/616**
extracted Node cases. Its shared LambdaJS Test262 gate is **40,261/40,261**,
with zero retry time and no unstable batches (57.8 seconds). The latest Lambda
baseline is the preceding `passive-own` checkpoint; the shared unbox default
is unchanged. Current Lambda/performance acceptance remains pending.

The `forward-span` 21-round screen resolves Triangl's regression
(**154.008 / 154.727 / 154.705 ms**, candidate/control/identical peer).
Map iteration (**1.298 / 1.285 / 1.285 ms**) and object growth
(**2.935 / 2.878 / 2.898 ms**) remain slower against both controls; JSON Gen
is inconclusive against the identical peer. The next prepared changes avoid
sorting an already ordered own-key snapshot and borrow native Map pair values
only until both uncaptured local bindings adopt their scalars. Captured,
external, reflected and protocol paths keep their ownership boundaries
(**D5.2–D5.3**, **D8.2.1–D8.2.6**). No helper or container flag is added;
forced-GC and performance confirmation are pending.

The frozen `ordered-bind` release (SHA-256
`ac782fa29a13c8ff7fa36e9e109730c0181827bd97ddff77db59a85d2817f7cb`)
passes **157/157** normal and full forced-GC/poison groups and **631/631**
extracted Node cases. Shared LambdaJS Test262 is **40,261/40,261**, with zero
retry time or unstable batches. The Lambda baseline is **6,702/6,703**, with
only the preexisting `edit_view_only` mismatch; LaTeX phase 3 passes in 53 seconds.
Its isolated 21-round screen improves Map iteration
(**1.190 / 1.284 / 1.285 ms**) and makes object growth inconclusive
(**2.872 / 2.874 / 2.873 ms**). Triangl is also inconclusive in this capture.
JSON Gen remains slower against both controls (**5.756 / 5.654 / 5.621 ms**),
with its cause still unproven. The same frozen release subsequently passes
**631/631** extracted snippets directly through its CLI. Final evidence is in
`ordered-bind-validation.json` under `temp/mvp_properties/`.

Its **complete 83-workload non-jq capture** passes every oracle/backend contract
with seven measured triples, six rotating orders and the exact phase-entry
control plus an identical peer (`ordered-bind-nonjq/comparison.json`). Fifteen
rows are faster against both controls; the screen flags Towers and Knucleotide.
Hyphen improves to **39.947 / 42.063 / 42.575 ms**, with both paired intervals
below 1. Its isolated **76.331 ms** candidate sample remains in the record.
Object growth is inconclusive; Map iteration improves. No build, test or profile
work overlapped the accepted timing captures.

The separate **31-round** confirmation retains three regressions:

| Workload | Candidate ms | Control ms | Identical peer ms | Candidate/control 95% paired ratio interval |
|---|---:|---:|---:|---|
| Towers | 1.067 | 1.057 | 1.060 | 1.00282–1.01328 |
| Knucleotide | 3.845 | 3.790 | 3.800 | 1.00947–1.01666 |
| JSON Gen | 5.739 | 5.625 | 5.617 | 1.00914–1.02603 |

All three are also slower against the identical peer; control/control intervals
include 1. Triangl is inconclusive (**153.461 / 154.222 / 154.239 ms**).
These are open regressions, not a regression-free acceptance result. Towers'
MIR still needs sparse-storage guards where the current range proof cannot
exclude sparse writes; removing them without a valid proof would be unsound.
The exact causes of the residual deltas have not been isolated.

The **21-round shared Lambda** capture (`ordered-bind-lambda-peer/comparison.json`)
finds no workload slower against both controls: JSON Gen
**22.888 / 23.008 / 22.822 ms**, Deriv **21.514 / 21.395 / 21.618 ms**.
Sieve is **0.028 / 0.026 / 0.026 ms**: its control interval touches 1 while its
peer interval is above 1, so this quantized short row remains borderline.

Canonical Hashmap passes **15 measurements** on this release
(`ordered-bind-hashmap/comparison.json`): MVP **468.011 ms**, Node **15.102 ms**,
Lambda reference **73.148 ms**. The reference is annotated and preallocates
parallel arrays while JS grows chained entry objects; this is not a matched
untyped or engine-only comparison. The four long jq workloads retain Result9
history only, with no fresh execution or preflight. P5 remains open; Results3–9
are unchanged and no Result10 is published.

The remaining compiler work concerns narrower proofs for property effects,
array storage and scalar coercion; dynamic behavior must remain observable.

#### Primitive batching and indexed-write follow-up (2026-10-11)

Three general changes address avoidable work in the remaining regression paths:

- Reuse `String.is_ascii` to prove canonical UTF-8 Map keys without a byte scan;
  non-ASCII keys still use Lambda's canonicalization (**S8.2.2v5**, **D4.6.1v4**).
  Property-key conversion now calls the existing `canonical_string` once.
- Batch long primitive string chains through existing `fn_strcat_many`, using
  Lambda's existing 64-piece ceiling. Promote `emit_vararg_call_common` to
  shared `em_emit_vararg_call`; MVP imports the existing runtime join. Its
  prototype identity includes the function because MVP creates one emitter
  per function. Short chains retain fixed-arity calls; source evaluation and
  conversion order, object/Symbol fallback and precise roots remain intact
  (**S1.11**, **D5.2–D5.3**, **D8.2.1–D8.2.6**). No runtime helper is added.
- Reorder indexed writes so an existing element needs one bounds decision.
  The append edge alone checks capacity and updates length immediately before
  the shared store, with no intervening safepoint. Sparse, descriptor and
  scalar-home fallbacks remain intact (**D2.6.4**, **D2.7.1**, **D5.3**).

The frozen `primitive-batch` candidate is
`632fe75643ac6050ce94c6b23dee87a53bfacbda40957d96909309dbb8be679a`.
Its current-HEAD control (`4cbe942fb4837861af56fae7193e546c8a2abdd3`)
is `ad5a63bed23f4512fe0c65295f4e33260cc6bb3bcd9703ee8b4938ab85e8d587`.
Both manifests and source provenance are under `temp/mvp_properties/`.
The candidate passes **160/160** normal and forced-GC/poison groups and
**653/653** extracted cases on both Node and the frozen release CLI.

The first 31-round incremental timing capture is **diagnostic only**: concurrent
Lambda-opus tests were observed, and identical controls show large variation.
Keep every sample in `primitive-batch-incremental/comparison.json`; its
disposition record documents the interference. Performance acceptance remains
pending a quiet repeat and the broader prior-workload comparison. The first
shared Test262 run recovered all **40,261** cases after seven SIGKILLed batches;
this is not a clean gate and also requires a fresh run. The exact source of
those external terminations has not been established.

The byte-identical `primitive-batch-final` rebuild retains that SHA. Its shared
Lambda gate is **6,706/6,707**, including **2,112/2,112** input cases. Only the
LaTeX phase-3 corpus timed out at 60 seconds; its isolated replay passes in
54.747 seconds. The earlier `edit_view_only` mismatch is fixed in current HEAD.
A fresh shared LambdaJS Test262 gate passes **40,261/40,261** in 63.5 seconds,
with zero killed batches and zero retries. These are shared-engine gates,
not an MVP Test262 conformance claim.

A second 31-round incremental capture finds Knucleotide and JSON Gen faster
against both current-HEAD controls; Towers and Triangl are inconclusive.
It remains qualified evidence: a new CPU-heavy `./lambda.exe` process was
observed immediately afterward and may have overlapped. Its samples and
disposition are retained in `primitive-batch-incremental-repeat/` and the
adjacent disposition record.

The next local-initializer refinement reuses `expression`'s existing scalar
borrow option only for uncaptured, non-global, non-native local bindings,
outside inlining, with no external/reflected/intrinsic binding. Initialization
immediately invokes the existing `write_binding` adoption into the local home,
without a call between the read and adoption. This removes an intermediate
snapshot while retaining the final owned copy (**D2.7.1**, **D5.3**, **D8.2.1–D8.2.6**).
Captured/global/native bindings and destructuring retain their existing path;
callback reads retain their own lifetime safeguards. No helper is added.
The `local-adopt` candidate, SHA-256
`71cd2a3120e6c2b2bb327ec491868e14fceca0654cf66ec8a58d2c69eccd61bd`,
passes **161/161** normal groups (22.771 seconds), **161/161** full forced-GC/
poison groups (315.229 seconds), and **662/662** extracted cases on both Node
and the frozen release CLI. Four additional focused forced-GC groups pass.
Its runtime source remains the frozen patch; subsequent edits are documentation.

The monitored 31-round incremental comparison against current HEAD establishes
gains against both control copies for Towers, Knucleotide and JSON Gen
(`local-adopt-incremental/comparison.json`). Triangl is inconclusive against
the primary control and slightly slower against the peer, so no incremental
Triangl gain is claimed. The separate **31-round phase-entry confirmation**
clears all three outstanding regressions and finds a small Triangl gain:

| Workload | Candidate ms | Phase-entry ms | Identical peer ms | Candidate/control 95% paired ratio interval |
|---|---:|---:|---:|---|
| Towers | 1.029 | 1.055 | 1.058 | 0.96978–0.98294 |
| Knucleotide | 3.678 | 3.815 | 3.828 | 0.95489–0.97482 |
| JSON Gen | 4.564 | 5.665 | 5.679 | 0.79655–0.80821 |
| Triangl | 154.294 | 155.012 | 154.950 | 0.99411–0.99873 |

All four candidate/peer intervals are also below 1; all control/control
intervals contain 1. Both captures retain six rotating orders, unchanged
source/output/backend contracts and self-reported workload timing. The
five-second process log (`local-adopt-host.jsonl`) observes no unrelated build
or test during these captures. Exact raw samples are in
`local-adopt-phase-confirm/comparison.json`.

The completed `local-adopt-nonjq/comparison.json` screen passes **83/83**
output/backend contracts, with seven measured triples each. It finds 22
workloads faster against both controls and two slower: Fasta and Raytrace3D.
The process monitor observes no unrelated build/test during its 35.8-minute
window. The four jq workloads were neither run nor preflighted.

Both flags persist in a fresh 31-round phase-entry confirmation
(`local-adopt-flags-confirm/comparison.json`): Fasta **0.897 vs 0.885 ms**
(candidate/control interval **1.00224–1.02152**) and Raytrace3D **248.308 vs
243.798 ms** (**1.01371–1.02522**). Both candidate/peer intervals exceed 1,
and control/control intervals contain 1. Raytrace absolute times shifted
from about 140 to 244 ms in all lanes during the capture; retain this host
variation alongside the consistent paired gap. The monitor excludes observed
build/test interference, not every source of operating-system load.

The separate 31-round comparison against clean current HEAD
(`local-adopt-flags-incremental/comparison.json`) finds neither workload
newly slower against both controls. Fasta is inconclusive; Raytrace improves
against the peer and narrowly overlaps 1 against the primary control
(**0.98061–1.00009**). These small phase-entry gaps predate this tuning round;
the phase's whole-population performance acceptance remains open.

The following array-pair refinement reuses the existing `borrow_pair` local
ownership proof for native Array destructuring. Both uncaptured local bindings
adopt their components before body execution; callback-capable reads still
snapshot immediately, so a second getter cannot overwrite the first value
(**D2.7.1**, **D5.3**, **D8.2.1–D8.2.6**). No helper or representation flag is
added. Among the 83 frozen benchmark sources, array-pattern loop heads occur
in Fasta, Knucleotide, Regexredux and Map Iteration.

The frozen `array-pair` release is
`12de56e90f862f2dadc33d2f73f435d251cd93dba5fa63f9cce148b6564f50c7`.
It passes **162/162** MVP groups normally (15.271 seconds) and with forced
GC/poison (321.243 seconds), and **670/670** extracted cases on both Node and
the frozen release. The added cases cover scalar mutation, captured bindings,
object lifetime, reentrant getters, sparse pairs, globals and custom iterators.
Final shared-code gates pass **6,709/6,709** Lambda baseline cases, including
**2,112/2,112** input cases, and **40,261/40,261** LambdaJS Test262 baseline
cases with no retries or unstable batches (2,652 cases remain skipped by that
baseline). The earlier LaTeX timeout passes inside this aggregate run in
34 seconds. These shared-engine checks do not assert MVP Test262 conformance.
Logs are `test-lambda-baseline-array-pair.log` and
`test262-baseline-array-pair.log`; baseline artifacts are in `array-pair-gates/`.

Its fresh **31-round** phase-entry comparison covers all four affected sources
and the regression sentinels (`array-pair-confirm/comparison.json`):

| Workload | Candidate ms | Phase-entry ms | Identical peer ms | Candidate/control 95% paired ratio interval |
|---|---:|---:|---:|---|
| Towers | 1.037 | 1.059 | 1.060 | 0.97183–0.98673 |
| Fasta | 0.882 | 0.883 | 0.882 | 0.98649–1.00681 |
| Knucleotide | 3.639 | 3.795 | 3.783 | 0.95373–0.96588 |
| Regexredux | 3.109 | 3.103 | 3.100 | 0.98626–1.01368 |
| Raytrace3D | 142.382 | 139.804 | 139.317 | 1.01578–1.02138 |
| Map Iteration | 1.185 | 1.284 | 1.284 | 0.92062–0.92723 |
| JSON Gen | 4.498 | 5.619 | 5.665 | 0.79566–0.80713 |
| Triangl | 153.364 | 154.236 | 154.351 | 0.99372–0.99488 |

All eight oracle/backend contracts pass. Five workloads improve against both
controls; Fasta and Regexredux are inconclusive. Raytrace3D remains slower
against both. Every control/control interval contains 1. A separate 31-round
incremental comparison against `local-adopt` finds all four affected workloads
within noise (`array-pair-incremental/comparison.json`); it does not establish
an independent causal speedup for the array-pair change.

The shared Lambda check (`array-pair-lambda-peer/comparison.json`) passes all
three unchanged output contracts with 21 measured triples. Sieve **0.026 vs
0.026 ms**, JSON Gen **23.141 vs 23.097 ms**, and Deriv **21.383 vs 21.623 ms**
all have candidate/control, candidate/peer and control/control intervals
containing 1. Sieve's microsecond timing resolution limits sensitivity.

The fresh Hashmap capture (`array-pair-hashmap/comparison.json`) passes all
contracts with 15 measured runs: MVP **469.519 ms**, Node **15.049 ms**,
canonical Lambda reference **71.571 ms**. The Lambda source is annotated and
uses preallocated parallel arrays, while JS grows chained entry objects; the
runner's historical `lambda_untyped` field is not a matched untyped comparison.
The five-second host monitor observes no unrelated build/test in these four
final captures (`array-pair-host-summary.json`); short windows limit its
sampling coverage. Exact binaries, sources, samples and outputs are retained.

Raytrace's diagnostic MIR still contains sparse-storage checks. Its closed-unit
proof rejects unbounded writes through global loop indices and postincrements;
the retained guards are required until a stronger proof establishes physical
storage (**D2.6.4**, **D8.2.1–D8.2.6**). This identifies a code-generation
limitation, not a measured attribution of the entire remaining 1.8% gap.
The full 83-row screen belongs to `local-adopt`; the final release has this
eight-row follow-up. No full-population, zero-regression claim is made.

The earlier `passive-set` release passes 15 canonical Hashmap measurements
(`passive-set-hashmap/comparison.json`): MVP **476.079 ms**, Node
**15.116 ms**, canonical Lambda **72.611 ms**. The Lambda port is annotated
and preallocates parallel arrays, while JS grows chained entry objects; this
is not a matched untyped or engine-only comparison.

The preceding frozen constructor release is
`temp/mvp_properties/fixed-ctor/candidate.exe`, SHA-256
`34bb840a584a3b277fe3a539e1223cc7d60fda3e24946a8c594c7f1900b87bf1`.
Its **complete 83-workload non-jq capture** passes every output/backend contract
with seven measured pairs and an identical control peer
(`fixed-ctor-nonjq/comparison.json`). It exposes substantial regressions:

| Workload | Candidate ms | Control ms | Identical peer ms |
|---|---:|---:|---:|
| Microdiff | 332.934 | 100.551 | 100.591 |
| Log Pipeline | 7873.599 | 4701.905 | 4694.081 |
| Text Search | 9159.481 | 5977.556 | 5987.928 |
| Hyphen | 50.076 | 41.380 | 41.302 |
| Fast Diff | 214.623 | 197.006 | 198.198 |

These are diagnostic results for that binary, not measurements of the following
array/coercion/own-store changes. Suspects require longer confirmation.
**Performance acceptance remains pending.** The earlier deliberately stopped
77-row capture and short tuning screens remain in `temp/mvp_properties/`;
they are not substitutes for this full capture or final acceptance.

Canonical hashmap passes its oracle/MIR preflight with 15 samples on that same
constructor release (`hashmap-fixed-ctor/comparison.json`): MVP **619.458 ms**,
Node **15.181 ms**, Lambda **72.463 ms**. Its Lambda port uses preallocated
parallel arrays and contains annotations; its storage workload differs from JS,
so the ratio is not an engine-only or matched untyped comparison. A final
candidate requires its own capture.

Custom array species constructors and alternate new-target for some native
constructors report explicit capability errors; default species and observable
property reads are supported. Existing unsupported syntax/brands (including
private fields and broader class forms) remain explicit boundaries. Platforms
beyond macOS ARM64 are unmeasured. Fresh timing of the four long jq workloads
is excluded by the user's instruction. P5 aggregate acceptance remains open
because ordinary-workload regressions persist. Results3–9 are unchanged.
