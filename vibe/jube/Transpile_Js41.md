# Transpile_Js41 - LambdaJS Structure, Modularity, and js262 Robustness Proposal

Date: 2026-05-08

This proposal reviews the current LambdaJS transpiler and runtime layout and
defines a staged refactor plan whose goal is not aesthetic cleanup. The goal is
to make the engine easier to change without regressions, and to raise js262 /
test262 pass count by giving the implementation a clearer spec spine.

## 1. Current Snapshot

The JavaScript implementation is now large enough that code organization is a
correctness issue:

| File | Lines | Primary role |
|---|---:|---|
| `lambda/js/transpile_js_mir.cpp` | 29,035 | AST analysis, MIR emission, closure/module/class lowering, eval, batch/preamble support, import loading |
| `lambda/js/js_runtime.cpp` | 25,575 | coercion, operators, function calls, property access, prototypes, generators, promises, collections, typed arrays, batch reset |
| `lambda/js/js_globals.cpp` | 12,739 | global objects, constructors, Object/Reflect/JSON/Date/Number/String pieces, test262 harness helpers, host APIs |
| `lambda/js/build_js_ast.cpp` | 4,040 | Tree-sitter JavaScript CST to `JsAstNode` |
| `lambda/js/js_props.cpp` | 765 | newer ordinary property kernels |
| `lambda/js/js_property_attrs.cpp` | 673 | newer shape-flag descriptor/attribute helpers |
| `test/test_js_test262_gtest.cpp` | 3,193 | test262 metadata, filtering, batching, baseline/partial accounting |

Recent Js38 and Js40 work already moved the engine in the right direction:

- `js_props.{h,cpp}` now contains canonical ordinary property kernels such as
  `js_ordinary_get_own`, `js_ordinary_set_via_accessor`,
  `js_ordinary_has_property`, and `js_ordinary_get`.
- `js_property_attrs.{h,cpp}` has shape-entry flag helpers, map-local shape
  cloning, accessor-pair storage, and named-property marker retirement.
- `js_class.h` introduced typed class stamping for many built-in objects.
- `js_state_guards.h` introduced the first RAII-style guard for hidden runtime
  state.
- Js40 raised the refreshed baseline to roughly `30,009` passing tests with
  zero current regressions after the TypedArray/DataView work.

But the next 4,000 failures are unlikely to fall cleanly while the remaining
core semantics live behind multi-thousand-line switch forests and process-wide
hidden state.

## 2. Main Structural Problems

### 2.1 The transpiler has too many phases in one translation unit

`transpile_js_mir.cpp` mixes these concerns:

- AST pre-scans: captures, TDZ, locals, free variables, class fields,
  constructor shape discovery, typed-array detection, return/type inference.
- MIR utilities: register allocation, labels, import registration, runtime
  call helpers, boxing/unboxing, condition lowering.
- JS lowering: expressions, statements, patterns/destructuring, calls/new,
  classes, functions, generators, async/await, modules, eval.
- Engine lifecycle: MIR context setup, link validation, signal recovery,
  event-loop draining, DOM setup, deferred MIR cleanup.
- js262 batch/preamble mechanics.

The cost is that a local semantic fix can accidentally perturb compilation
lifecycle, module handling, or batch execution. The file has repeated cleanup
blocks and visible copy/paste hazards, including duplicate
`jm_free_scope_env_names(...)` calls in some cleanup paths.

### 2.2 Runtime semantics are not yet organized around ECMA abstract operations

`js_runtime.cpp` has a strong `js_property_get` / `js_property_set` public ABI,
but internally it still mixes:

- ordinary object logic,
- exotic dispatch by `MAP_KIND_*`,
- virtual function properties,
- array/string wrapper behavior,
- prototype lookup,
- built-in method fallback,
- descriptor and attribute checks,
- debugging traces and batch recovery details.

The newer `js_props` kernels are exactly the right direction, but they are still
only one island inside a much larger hand-rolled dispatch surface.

### 2.3 Hidden process-wide state makes batch isolation fragile

The runtime relies on many globals: current `this`, pending `new.target`,
pending call args, active module vars, strict-mode flag, proxy receiver,
exception state, generator/promise/object caches, global object caches, process
listeners, console timers, and deferred MIR contexts.

Some reset paths exist (`js_batch_reset`, `js_globals_batch_reset`,
module-var reset, regex/module/process resets), but the state is distributed.
This makes test262 batching hard: a test can pass alone, then fail or slow down
because a cache or pending state leaks from a previous script.

### 2.4 Built-ins are spread across lookup code, constructors, and ad-hoc stubs

`js_lookup_builtin_method`, `js_dispatch_builtin`, global constructors, and
prototype installation are not yet driven by a single descriptor table. This
creates three failure modes:

- missing property descriptor metadata (`name`, `length`, writability,
  enumerability, configurability),
- method exists through one path but not another,
- stubs silently mask unsupported behavior, which can make tests pass for the
  wrong reason or fail later in a harder-to-debug place.

### 2.5 The docs are behind the live code

`doc/dev/JS_Runtime_Detailed.md` still describes the older
`__get_` / `__set_` / `__nw_` marker model and `__class_name__` dispatch as
primary mechanisms. The current code has moved toward shape flags,
`JsAccessorPair`, typed class stamping, and ordinary property kernels. This
documentation drift is now dangerous because it encourages new code to follow
retired patterns.

## 3. Target Architecture

### 3.1 Transpiler target layout

Split `transpile_js_mir.cpp` by phase and responsibility. Keep the public ABI
stable at first: `transpile_js_to_mir`, `transpile_js_ast_to_mir`,
`transpile_js_to_mir_preamble`, and `transpile_js_to_mir_with_preamble`.

Proposed files:

| New file | Ownership |
|---|---|
| `js_mir_context.hpp/.cpp` | `JsMirTranspiler` state, init/free, cleanup guards, MIR context ownership |
| `js_mir_emit.hpp/.cpp` | registers, labels, `jm_emit`, runtime import registration, `jm_call_*`, boxing/unboxing |
| `js_mir_analysis.cpp` | free vars, captures, TDZ, locals, type/return inference, constructor shape scans |
| `js_mir_expr.cpp` | literals, identifiers, unary/binary, calls, new, member access, optional chaining |
| `js_mir_stmt.cpp` | blocks, var declarations, if/switch/loops/try/throw/return/break/continue |
| `js_mir_pattern.cpp` | destructuring, rest/spread binding, assignment patterns |
| `js_mir_func.cpp` | function collection, closures, arrows, arguments object, tail-call handling |
| `js_mir_class.cpp` | class collection/lowering, super, private fields, static blocks |
| `js_mir_generator_async.cpp` | generator state machines, async functions, await lowering |
| `js_mir_module.cpp` | ESM import/export, CJS require wrapping, dynamic import, cross-language imports |
| `js_mir_eval.cpp` | eval/new Function compilation path and preamble interaction |
| `js_mir_batch.cpp` | test262 preamble, deferred contexts, batch recovery hooks |

The first pass should be mechanical: move code, keep function names, and keep
all `static` helpers file-local where possible. Do not change semantics during
the split.

### 3.2 Runtime target layout

The runtime should be arranged around the same abstraction layers as ECMA-262:
value conversion, object internal methods, function call/construct, built-ins,
and host integration.

Proposed files:

| New file | Ownership |
|---|---|
| `js_runtime_state.hpp/.cpp` | `JsRuntimeState`, reset, heap epoch caches, current this/new.target/exception/module vars |
| `js_value.cpp` | `ToPrimitive`, `ToBoolean`, `ToNumber`, `ToNumeric`, `ToString`, SameValue/SameValueZero |
| `js_ops.cpp` | arithmetic, comparison, bitwise, unary, `typeof`, `instanceof`, `in` |
| `js_object_ops.cpp` | `[[Get]]`, `[[Set]]`, `[[Delete]]`, `[[HasProperty]]`, prototypes, property keys |
| `js_descriptor.cpp` | `ToPropertyDescriptor`, `ValidateAndApplyPropertyDescriptor`, define/get own descriptor |
| `js_function.cpp` | `JsFunction`, call/apply/bind, construct/new.target, arguments object |
| `js_builtin_registry.cpp` | data tables for built-in constructors, prototypes, methods, accessors, attributes |
| `js_array.cpp` | array object algorithms and Array.prototype methods |
| `js_string.cpp` | string wrapper and String.prototype methods |
| `js_number_bigint.cpp` | Number/BigInt constructors and prototype methods |
| `js_date.cpp` | Date constructor and Date.prototype methods |
| `js_regexp.cpp` | RegExp object algorithms; keep RE2 wrapper separate |
| `js_promise.cpp` | promises, jobs, async function support |
| `js_generator.cpp` | generator objects and iterator protocol |
| `js_collection.cpp` | Map, Set, WeakMap, WeakSet |
| `js_global_object.cpp` | globalThis, intrinsic installation, `$262` host object |
| `js_node_process.cpp` | process/console/performance host APIs currently in `js_globals.cpp` |

`js_runtime.h` should become a thin public C ABI used by MIR imports. Most
internal helpers should move to narrower headers such as `js_object_ops.h`,
`js_descriptor.h`, and `js_runtime_state.h`.

### 3.3 One spec-operation spine

For js262, the engine needs fewer "similar but not identical" paths. Use
ECMA-style operations as the mandatory choke points:

- `js_to_property_key`
- `js_ordinary_get_own`
- `js_ordinary_get`
- `js_ordinary_set`
- `js_ordinary_delete`
- `js_ordinary_has_property`
- `js_get_own_property_descriptor`
- `js_define_own_property_from_descriptor`
- `js_create_data_property`
- `js_create_method_property`
- `js_call`
- `js_construct`

Exotic objects should hook these at explicit seams:

- typed arrays and DataView,
- ArrayBuffer and resizable ArrayBuffer,
- Proxy,
- Module namespace objects,
- String wrappers,
- arrays and array companion maps,
- DOM/CSSOM host objects.

The design rule: public ABI functions such as `js_property_get` can remain as
compatibility wrappers, but their first job is to dispatch to a spec operation,
not to reimplement the operation inline.

## 4. Robustness Plan

### R1. Introduce `JsRuntimeState`

Create one state object owned by the active `EvalContext` or `Runtime`:

- exception pending/value/message,
- current `this`,
- pending and active `new.target`,
- pending call args,
- strict mode,
- proxy receiver,
- active module vars,
- builtin/global caches,
- process listeners and console counters,
- heap epoch.

The first implementation can keep existing globals as fields behind accessors,
then migrate call sites incrementally. The important win is that reset and
batch isolation become one operation: `js_runtime_state_reset_for_script(...)`.

### R2. Replace manual save/restore with scoped guards

Extend `js_state_guards.h` beyond `ScopedSkipAccessorDispatch`:

- `ScopedThis`
- `ScopedNewTarget`
- `ScopedProxyReceiver`
- `ScopedPendingArgs`
- `ScopedStrictMode`
- `ScopedActiveModuleVars`
- `ScopedExceptionCheckpoint`

This directly reduces early-return bugs in descriptor, proxy, super, call/apply,
and eval paths.

### R3. Make lifecycle cleanup single-exit

Every MIR compile path should have one cleanup owner:

- `JsMirCompileUnit` owns `MIR_context_t`, transpiler pools, import cache,
  local funcs, var scopes, module consts, and `JsMirTranspiler`.
- It has explicit transfer methods: `keep_mir_for_preamble`,
  `defer_mir_for_batch`, `register_module_context`.
- Error paths return through one cleanup block.

This removes repeated cleanup blocks and prevents double-free, leaked context,
and dangling pool-string cases.

### R4. Remove process exits from runtime semantics

`js_globals.cpp` still has host functions that call `exit()` or `abort()` for
Node compatibility. That is hostile to batch testing. Internally, these should
be modeled as:

- set requested process exit code in `JsRuntimeState`,
- throw a private control exception or return `ITEM_ERROR`,
- let the CLI boundary decide whether to exit the host process.

`process.abort()` can remain a CLI-visible host behavior outside test mode, but
the engine core should not kill the test worker unless the test explicitly
requires that host effect.

### R5. Make debug traces opt-in and prefixed

There are remaining always-compiled trace paths in `js_property_access` for
specific property names like `.col1` and `.R`. Move these behind a single
`JS_TRACE_PROP` runtime flag or remove them after their corresponding issues
are closed. Every trace should start with a searchable prefix such as
`js-prop-trace:`.

## 5. Built-in Registry Plan

Create a declarative registry for intrinsics:

```c
typedef struct JsBuiltinSpec {
    JsClass owner_class;
    const char* name;
    void* fn_ptr;
    int length;
    uint8_t attrs;
    uint8_t kind; // data, method, getter, setter, constructor
} JsBuiltinSpec;
```

Use it to install:

- globals (`Array`, `Object`, `Reflect`, `Promise`, `$262`, etc.),
- constructor static methods,
- prototype methods,
- accessor descriptors,
- `@@toStringTag`,
- non-enumerable/non-writable/non-configurable metadata.

`js_lookup_builtin_method` should become a lookup over generated or static
tables, not a long chain of string comparisons. The same table should drive
descriptor creation so that method visibility and `Object.getOwnPropertyDescriptor`
cannot disagree.

## 6. js262 / test262 Strategy

### 6.1 Keep the zero-regression contract

The current runner has the right principle:

- no lost tests,
- no crash exits,
- no baseline regressions,
- partial tests cannot be counted as fully passing,
- baseline updates require clean gates.

Keep that contract. Refactoring phases should pass:

```bash
make build
./test/test_js_props_gtest.exe
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --baseline-only --js-timeout=20
```

Use the full non-updating batch at phase boundaries:

```bash
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe --batch-only --js-timeout=20
```

### 6.2 Add smaller focused conformance manifests

The full test262 run is too broad for inner-loop development. Add curated
manifests under `test/js262/focus/`:

- `object_descriptor.txt`
- `proxy_internal_methods.txt`
- `array_exotic.txt`
- `typedarray_internals.txt`
- `promise_jobs.txt`
- `async_generator.txt`
- `module_namespace.txt`
- `function_call_construct.txt`
- `eval_direct_indirect.txt`
- `regexp_unicode.txt`

Each manifest should list test262 relative paths and have a one-line owner
comment at the top. The runner already supports `--batch-file`; these manifests
make feature work reproducible.

### 6.3 Treat skip reasons as product data

Split skipped tests into explicit buckets:

- `unsupported_language_feature` - out of target ES version or intentionally not
  implemented yet,
- `missing_host_hook` - `$262.createRealm`, `$262.detachArrayBuffer`, `$262.gc`,
  `IsHTMLDDA`, etc.,
- `known_semantic_gap` - should be implemented,
- `known_runner_limitation` - metadata/harness issue,
- `non_deterministic`.

Today unsupported features live in a large C++ set in the runner. Move the data
to a generated or plain text manifest so diffs are reviewable, and require every
new skip to include one of these categories.

### 6.4 Implement host hooks before broad semantic rewrites

Several remaining failures are likely blocked by host machinery rather than
core JS semantics. Prioritize:

- `$262.createRealm()` with isolated global object and shared intrinsics policy,
- `$262.detachArrayBuffer()` and ArrayBuffer transfer/detach validation,
- `$262.gc()` as a safe explicit GC trigger or no-op with correct feature gate,
- `$DONE` and async test completion accounting,
- native `compareArray`, `assert.sameValue`, and negative-test error matching
  that preserve spec behavior.

This reduces false failures and avoids "fixing" runtime code around missing
harness behavior.

### 6.5 Add crash minimization to the runner

When a batch worker crashes, the runner currently recovers lost tests
individually. Add automatic crash bisection:

1. record the crashed batch manifest in `temp/js262_crash_batches/`,
2. bisect the batch until a minimal crasher set is found,
3. write `temp/js262_last_crasher.txt`,
4. tag the partial list with the exact signal/exit reason.

This turns batch instability into an actionable file instead of a hunting trip.

## 7. Phased Execution Plan

### Phase J41-0 - Documentation and inventory

Deliverables:

- Update `doc/dev/JS_Runtime_Detailed.md` to mark legacy marker behavior as
  historical and describe shape flags / `JsAccessorPair` / `js_props` kernels.
- Add a generated inventory of runtime imports, built-ins, reset globals, and
  `MAP_KIND_*` exotics.
- Add `temp/` scripts only if needed; do not write to `/tmp`.

Gate:

- documentation-only diff plus `make build` if any scripts or headers change.

### Phase J41-1 - Mechanical transpiler split

Status: complete as of 2026-05-08.

Deliverables:

- Move MIR utilities, analysis, expression lowering, statement lowering,
  functions/classes/modules/eval/batch into separate files.
- Add `js_mir_context.hpp` and a single cleanup helper.
- No semantic changes.

Completion notes:

- `lambda/js/transpile_js_mir.cpp` now anchors the shared global state for the
  split transpiler.
- `lambda/js/js_mir_context.hpp` owns the shared transpiler context structs and
  cleanup helper; `lambda/js/js_mir_internal.hpp` owns the cross-translation-unit
  helper declarations and shared extern state.
- Top-level `lambda/js/js_mir_*.cpp` files contain the mechanical phase areas:
  hashmap/scope utilities, analysis, MIR call/boxing/type helpers, function and
  class collection/inference, expression lowering, statement lowering,
  function/class lowering, module/batch lowering, eval lowering, and public
  entrypoint/require/import lowering.
- No manual build-config change was needed; the normal generated build picks up
  the new top-level `lambda/js/*.cpp` translation units.

Gate:

- `make build`
- representative JS tests: `./test/test_js_gtest.exe`
- baseline-only js262 gate.

### Phase J41-2 - Mechanical runtime split

Status: complete as of 2026-05-08.

Deliverables:

- Move value conversion and operators first. Completed in
  `lambda/js/js_runtime_value.cpp`.
- Move property/descriptor code around existing `js_props` and
  `js_property_attrs` files. Completed by preserving those spec-kernel files
  and leaving property descriptors on their existing public ABI.
- Move function call/construct and built-in registry skeleton. Completed
  mechanically by centralizing the shared `JsFunction` layout and built-in ids
  in `lambda/js/js_runtime_internal.hpp`, moving function object wrappers and
  cache reset to `lambda/js/js_runtime_function.cpp`, and moving the built-in
  method tables/installers/descriptor-registry helpers to
  `lambda/js/js_runtime_builtin_registry.cpp`. The large dispatch switch stays
  in `js_runtime.cpp` for the later semantic phases.
- Preserve public symbols in `js_runtime.h`. Public MIR/runtime entrypoints
  remained stable; only internal C++ linkage moved.
- Use real top-level `.cpp` translation units, not include fragments.

Gate:

- `make build`: passed.
- `./test/test_js_coerce_gtest.exe`: passed `15 / 15`.
- `./test/test_js_props_gtest.exe`: passed `24 / 24`.
- Focused `./test/test_js_gtest.exe` runtime smoke: passed `9 / 9`.
- `make test262-baseline`: fully passed `30009 / 30009`, failed `0`,
  regressions `0`.

### Phase J41-3 - Runtime state isolation

Status: complete for the baseline isolation gate. The runtime now has a
single shared `JsRuntimeState` capsule used across the split runtime and host
modules instead of exported free globals for transient execution state.

Deliverables:

- Introduce `JsRuntimeState`.
- Migrated exception, current-this, new-target, pending args, active module
  vars, strict mode, proxy receiver, RegExp legacy match state, heap epoch,
  and trace counters into `JsRuntimeState`.
- Added `lambda/js/js_runtime_state.hpp` as the narrow shared state header.
- Switched runtime helpers, globals, DOM selection, property attributes, and
  Node-style modules away from direct `extern js_input` /
  `extern js_strict_mode` / `extern js_skip_accessor_dispatch` globals.
- Centralized batch reset around the state capsule while preserving the
  existing public C ABI and MIR-imported symbols.
- Add debug assertions that no pending state leaks after each script in
  `js-test-batch`.

Gate:

- `make build`: passed with `Errors: 0`.
- `./test/test_js_coerce_gtest.exe`: passed `15 / 15`.
- `./test/test_js_props_gtest.exe`: passed `24 / 24`.
- Focused `./test/test_js_gtest.exe` runtime smoke: passed `9 / 9`.
- `make test262-baseline` repeated twice: both runs fully passed
  `30009 / 30009`, failed `0`, regressions `0`.
- Full batch without partial promotion:
  `./test/test_js_test262_gtest.exe --batch-only` fully passed
  `30013 / 34157`, failed `4144`, regressions `0`, improvements `4`.
- Full batch with `--run-partial` was executed after the repeated baseline
  gate. It exposed existing partial-list pressure around RegExp/URI slow tests:
  `30010 / 34169` fully passed, `2` retry-only slow URI tests, `1`
  RegExp crash under the merged partial batch. The RegExp case is already in
  `test/js262/t262_partial.txt` as `SLOW_20017` and passed when run isolated
  with GoogleTest filtering.

### Phase J41-4 - Built-in descriptor registry

Status: complete. Built-in method/accessor registry tables now drive method
installation, runtime lookup, prototype own-property name synthesis, and
`Object.getOwnPropertyDescriptor` synthesis for registry-backed prototype
methods/accessors. Remaining non-registry host surfaces are outside this phase
and should be handled as future family-specific registry additions.

Deliverables:

- Introduce `JsBuiltinSpec` tables.
- Convert Object, Function, Array, String, Number, BigInt, Symbol, Promise,
  RegExp, TypedArray, DataView, Map, Set in groups.
- Make descriptor queries read from the same installed metadata.

Gate:

- focused manifests for each converted family.
- full non-updating batch after each major family.

Completion gate:

- `./test/test_js_props_gtest.exe`: passed `24 / 24`.
- `./test/test_js_coerce_gtest.exe`: passed `15 / 15`.
- focused JS file tests for typed arrays, Date, RegExp, String, CSS namespace,
  and native-backed properties: passed `8 / 8`.
- `./test/test_css_dom_integration.exe`: passed `77`, skipped `25`, failed `0`.
- `make test262-baseline`: fully passed `30009 / 30009`, failed `0`,
  regressions `0`.

### Phase J41-5 - Spec-operation convergence

Status: complete as of the Js41 phase recorded in
`Transpile_Js41_Inventory.md`. Property get/set, has, delete, own-key, and
own-descriptor dispatch now have named exotic boundaries before ordinary
storage/prototype fallback. Module namespace objects currently alias ordinary
frozen maps (`js_module_namespace_create` returns the export map), so there is
no separate module-namespace `MapKind` hook to wire yet; the explicit exotic
dispatch points are in place for when that representation is introduced.

Deliverables:

- Route ordinary MAP and FUNC property paths through the same kernels where
  virtual function properties do not require special handling.
- Make Proxy, typed arrays, module namespace, arrays, and strings explicit
  exotic hooks around the ordinary operations.
- Add GTests for each abstract operation, independent of test262.

Gate:

- `test_js_props_gtest` grows into a fast spec-kernel suite.
- no baseline regressions.

Completion gate:

- `./test/test_js_props_gtest.exe`: passed `23 / 23`.
- `./test/test_js_coerce_gtest.exe`: passed `15 / 15`.
- `./test/test_js_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/native_backing_props:JavaScriptTests/JsFileTest.Run/typed_arrays:JavaScriptTests/JsFileTest.Run/opt_p4_typed_reads:JavaScriptTests/JsFileTest.Run/css_namespace:JavaScriptTests/JsFileTest.Run/dom_style:JavaScriptTests/JsFileTest.Run/dom_basic'`: passed `6 / 6`.
- `./test/test_css_dom_integration.exe`: passed `77`, skipped `25`, failed `0`.
- `make test262-baseline`: fully passed `30009 / 30009`, failed `0`,
  regressions `0`.

### Phase J41-6 - js262 growth program

Deliverables:

- Build focused failure dashboards from the full run.
- Work one family at a time: Realm/host hooks, eval, modules, promises/jobs,
  proxies, RegExp Unicode, class/private fields, Annex B only if target scope
  includes it.
- Refresh baseline only after full `--run-partial --update-baseline` has zero
  regressions and zero crash/lost tests.

## 8. Near-Term Highest-Leverage Work

1. Split `transpile_js_mir.cpp` mechanically. This lowers risk for every later
   semantic fix.
2. Introduce `JsRuntimeState` and migrate the small hidden-state globals first.
   This directly attacks batch flakiness.
3. Convert built-in installation to descriptor tables for one family, probably
   `Object` or `Function`, then reuse the pattern.
4. Add focused manifests and a failure dashboard so js262 progress is measured
   by subsystem, not just one giant pass count.
5. Update the docs immediately after each structural step. The docs should be a
   map of the current engine, not an archaeological layer.

## 9. Non-Goals

- Do not rewrite the JavaScript engine from scratch.
- Do not change the `Item` representation.
- Do not remove MIR JIT support.
- Do not broaden unsupported feature claims just to improve pass rate.
- Do not add more magic-property metadata paths. New semantics should flow
  through shape flags, descriptors, and spec operations.

## 10. Success Criteria

The refactor is successful if:

- `transpile_js_mir.cpp` and `js_runtime.cpp` shrink below 8,000 lines each,
  with no public ABI churn visible to the CLI.
- All property and descriptor behavior routes through named abstract operations.
- Batch reset is centralized and has assertions for leaked runtime state.
- Built-ins are installed from descriptor tables, not scattered string
  comparison chains.
- The baseline-only js262 gate remains zero-regression throughout.
- Full js262 progress becomes predictable: focused manifests improve first,
  then the full baseline grows after stable `--run-partial` runs.
