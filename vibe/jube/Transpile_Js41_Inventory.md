# Transpile_Js41 Inventory

Date: 2026-05-08

This is the phase-0 inventory for the Js41 refactor. It records the current
large ownership boundaries and the state that must be centralized before broad
js262/test262 work can stay predictable.

## Build Ownership

`build_lambda_config.json` includes `lambda/js` as a source directory. Future
JS file splits can add new `.cpp` files under `lambda/js/` and regenerate the
build files through the normal config-driven build flow. Do not manually edit
Premake Lua files.

## Large Files To Split

| Current file | Current size | Primary split target |
|---|---:|---|
| `lambda/js/transpile_js_mir.cpp` | 29,035 lines | `js_mir_context`, `js_mir_emit`, `js_mir_analysis`, `js_mir_expr`, `js_mir_stmt`, `js_mir_pattern`, `js_mir_func`, `js_mir_class`, `js_mir_module`, `js_mir_eval`, `js_mir_batch` |
| `lambda/js/js_runtime.cpp` | 25,575 lines | `js_runtime_state`, `js_value`, `js_ops`, `js_object_ops`, `js_descriptor`, `js_function`, `js_builtin_registry`, `js_array`, `js_string`, `js_number_bigint`, `js_promise`, `js_generator`, `js_collection` |
| `lambda/js/js_globals.cpp` | 12,739 lines | `js_global_object`, `js_builtin_registry`, `js_node_process`, `js_date`, `js_json`, `js_reflect`, `$262` host hooks |
| `lambda/js/build_js_ast.cpp` | 4,040 lines | keep mostly intact until transpiler split is stable |

## Existing Spec-Kernel Islands

These files should become the preferred homes for property/descriptor logic:

- `lambda/js/js_props.{h,cpp}` - ordinary property kernels:
  `js_ordinary_get_own`, `js_ordinary_get_own_descriptor`,
  `js_ordinary_set_via_accessor`, `js_ordinary_has_own`,
  `js_ordinary_own_status`, `js_ordinary_has_property`,
  `js_ordinary_get`, and related helpers.
- `lambda/js/js_property_attrs.{h,cpp}` - shape-entry descriptor bits,
  `JsAccessorPair` storage, shape clone for mutation, and attribute queries.
- `lambda/js/js_class.h` - typed class stamping for built-in maps.
- `lambda/js/js_state_guards.h` - current scoped guard foundation.

## MapKind Exotics

Observed `MAP_KIND_*` values:

| Map kind | Notes |
|---|---|
| `MAP_KIND_PLAIN` | ordinary JS/Lambda map path |
| `MAP_KIND_ARRAY_PROPS` | companion map for array indexed attributes/accessors |
| `MAP_KIND_TYPED_ARRAY` | typed-array internal-slot object |
| `MAP_KIND_ARRAYBUFFER` | ArrayBuffer object |
| `MAP_KIND_DATAVIEW` | DataView object |
| value 4, retired from DOM nodes | Radiant DOM wrappers now use branded native VMaps |
| value 8, reserved | retired active document proxy map carrier |
| value 10, reserved | retired foreign-document proxy map carrier |
| `MAP_KIND_CSSOM` | CSSOM wrappers |
| `MAP_KIND_ITERATOR` | engine-internal iterator objects |
| `MAP_KIND_PROXY` | Proxy object |
| `MAP_KIND_PROCESS_ENV` | Node `process.env` host object |
| `MAP_KIND_CSS_NAMESPACE` | global `CSS` namespace object with ordinary shape-backed methods |

Refactor rule: each exotic should hook named operations (`Get`, `Set`,
`Delete`, `DefineOwnProperty`, `OwnPropertyKeys`, `HasProperty`) explicitly,
then fall back to ordinary kernels where the spec permits.

## Reset Surface

`js_batch_reset()` and `js_batch_reset_to()` currently orchestrate reset across
many modules. Known reset hooks:

- core/module/runtime: `js_reset_module_vars`, `js_module_cache_reset`,
  `js_func_cache_reset`, `js_builtin_cache_reset`, `js_deep_batch_reset`,
  `js_regex_cache_reset`
- globals/constructors: `js_globals_batch_reset`, `js_process_reset_listeners`,
  `js_with_batch_reset`, `js_global_builtin_fn_cache_reset`,
  `js_ctor_cache_reset`, `js_reset_constructor_prototypes`
- DOM/host: `js_dom_batch_reset`, `js_dom_events_reset`,
  `js_dom_selection_reset`, `js_xhr_reset`
- Node modules: `js_child_process_reset`, `js_fs_reset`, `js_path_reset`,
  `js_os_reset`, `js_url_module_reset`, `js_util_reset`,
  `js_crypto_reset`, `js_dns_reset`, `js_zlib_reset`, `js_readline_reset`,
  `js_stream_reset`, `js_net_reset`, `js_tls_reset`, `js_http_reset`,
  `js_https_reset`, `js_string_decoder_reset`, `js_assert_reset`,
  `js_node_test_reset`, `js_fetch_reset`, `js_reset_buffer_module`,
  `js_reset_events_module`, `js_reset_querystring_module`

Refactor rule: introduce `JsRuntimeState` and make these reset hooks reachable
from one documented reset path. Batch mode should be able to assert that no
pending exception, pending `this`, pending `new.target`, proxy receiver, or
pending call args leak after each script.

## Hidden State To Migrate

High-priority globals in `js_runtime.cpp`:

- `js_strict_mode`
- `js_skip_accessor_dispatch`
- `js_current_this`
- `js_proxy_receiver`
- `js_new_target`
- `js_pending_new_target`
- `js_has_pending_new_target`
- `js_pending_call_args`
- `js_pending_call_argc`
- `js_module_vars`
- `js_active_module_vars`
- `js_module_var_count`
- `js_heap_epoch`
- `js_exception_pending`
- `js_exception_value`
- `js_exception_msg_buf`
- `js_cached_object_proto`
- `js_private_field_initializing`

High-priority globals in `js_globals.cpp`:

- process caches and listeners: `js_process_object`,
  `js_process_exit_code_value`, `process_exit_listeners`,
  `process_uncaught_listeners`, `js_process_exiting`, `process_listener_map`
- console state: `js_console_count_map`, `js_console_count_used`,
  `js_console_timers`, `js_console_timer_used`, `js_console_group_depth`
- constructor/global caches: `js_global_this_obj`, `global_builtin_fn_cache`,
  `js_constructor_cache`, typed-array prototype caches
- `with` stack: `js_with_stack`, `js_with_stack_depth`
- symbol registry counters and maps

High-priority globals in `transpile_js_mir.cpp`:

- deferred MIR context arrays and counters
- eval preamble snapshot state
- preamble mode globals
- dynamic function counter

## Built-in Dispatch Surface

Current dispatch is still centered on:

- `enum JsBuiltinId` in `js_runtime.cpp`
- `js_lookup_builtin_method(TypeId, name, len)`
- `js_get_or_create_builtin(...)`
- `js_dispatch_builtin(...)`
- constructor/prototype caches in `js_globals.cpp`

Target: a declarative `JsBuiltinSpec` table that installs method functions,
constructor functions, accessors, `name`, `length`, and attributes from the
same source of truth used by descriptor queries.

## Bigger Phase - Built-in Registry Foundation

Goal: turn built-in installation and lookup into a table-driven pattern without
changing dispatch behavior. This phase should be done family-by-family, with
the table becoming the one source for:

- function object creation (`builtin_id`, display name, `.length`)
- namespace/prototype installation
- dynamic property lookup fallback
- later descriptor synthesis (`writable`, `enumerable`, `configurable`)

Execution slices:

1. Add a generic `JsBuiltinMethodSpec` plus lookup/install helpers. Done for
   `Math`.
2. Migrate `JSON` and `Reflect` namespace methods to the same helper.
   Extended to include the same low-risk namespace method pattern for
   `Atomics`, because it already used a local method table with matching
   non-enumerable installation semantics. `CSS` was investigated and then
   completed in the later CSS namespace representation phase, because its old
   marker occupied `Map::type`, which conflicted with ordinary shape-backed
   method storage.
3. Migrate constructor static methods (`Object`, `Array`, `Number`, `Promise`,
   `Date`, `Symbol`, `TypedArray`) without changing their existing fallback
   semantics. Done for the plain method surfaces, and extended to matching
   `String`, `Map`, `ArrayBuffer`, and `Proxy` statics. Accessor statics such
   as `[Symbol.species]` remain in their existing accessor installation paths.
4. Migrate prototype families after focused descriptor/name/length checks.
   Completed for the plain method/accessor surfaces of `Object`, `Function`,
   `Array`, `Number`, `String`, `Promise`, `Map`, `Set`, `WeakMap`,
   `WeakSet`, `ArrayBuffer`, `Date`, `RegExp`, `DataView`, and
   `%TypedArray%`. Symbol and exotic descriptor surfaces remain on their
   dedicated paths where they carry non-method semantics.

## First Mechanical Cleanup Candidates

Safe cleanup candidates before broad file splits:

1. Factor repeated `JsMirTranspiler` cleanup blocks in `transpile_js_mir.cpp`. Done.
2. Remove duplicated `jm_free_scope_env_names(...)` calls in MIR cleanup paths. Done.
3. Move debug-only property traces behind a runtime flag.
4. Add no-leak assertions after `js-test-batch` script execution. In progress:
   transient call state now has one reset helper shared by batch reset paths.
5. Convert one small built-in family to table-driven installation as a pattern.

## Tranche Notes

### Tranche 1 - MIR transpiler cleanup

Added `jm_cleanup_mir_transpiler_state()` and converted the repeated lifecycle
tail blocks to call the shared cleanup path. This removed the duplicate
`jm_free_scope_env_names(...)` calls and made each failure path easier to audit.

### Tranche 2 - MIR transpiler construction

Added `jm_create_mir_transpiler()` and `jm_destroy_mir_transpiler()` in
`transpile_js_mir.cpp`, then routed the module, dynamic `new Function`, direct
`eval`, TypeScript AST, and main script paths through those helpers. The helper
still lives in the original translation unit, but it now defines the future
`js_mir_context` extraction boundary.

### Tranche 3 - Runtime transient reset boundary

Added shared reset helpers in `js_runtime.cpp` for transient call state and
heap-bound cached state. `js_batch_reset()`, `js_batch_reset_to()`, and
`js_deep_batch_reset()` now reset `this`, proxy receiver, pending `new.target`,
pending call args, pending `arguments` metadata, accessor-dispatch bypass, array
method receiver state, Object prototype cache state, private-field init state,
and `js_input` through named choke points.

### Tranche 4 - MIR validation trace hygiene

Removed direct stderr writes from `jm_validate_mir_labels()` and made successful
validation scan logging opt-in through `JS_MIR_VALIDATE_TRACE`. NULL-label
failures still log as errors, but normal test runs no longer emit one
`validate scanned ...` line per compiled module.

### Tranche 5 - Runtime diagnostic trace gate

Added a single `JS_RUNTIME_TRACE` opt-in gate for legacy runtime TRACE probes in
`js_runtime.cpp`. Shape-slot, `.R`, `.col1`, undefined-property ring-buffer, and
raw-string `.y` diagnostics no longer run or log during normal execution, but
remain available for targeted debugging. Removed unused trace globals left over
from earlier setter/call instrumentation.

### Tranche 6 - Batch reset invariant checks

Added a shared `js_assert_batch_runtime_state_clear(...)` check at the end of
`js_batch_reset()` and `js_batch_reset_to(...)`. The check verifies that pending
exception state, strict/accessor flags, `this`, proxy receiver, `new.target`,
pending call/arguments state, array-method receiver state, Object prototype
cache state, private-field init state, and `js_input` are clear after each reset
boundary. This does not change normal semantics, but gives future batch-state
work one precise diagnostic prefix: `js-batch-state`.

### Tranche 7 - Built-in registry foundation, Math slice

Started the larger built-in registry phase by adding `JsBuiltinMethodSpec`,
`js_lookup_builtin_method_spec(...)`, and `js_install_builtin_method_specs(...)`
in `js_runtime.cpp`. Migrated `Math` method installation and first-class method
fallback lookup to the shared `JS_MATH_METHOD_SPECS` table, eliminating the two
duplicate Math method tables while preserving the existing `js_dispatch_builtin`
behavior.

Verification also exposed a separate dynamic member-call crash for direct
`Math["max"](...)` / `Math["hypot"](...)` syntax. Direct calls
`Math.max(...)`, extracting the function first, and Math key enumerability pass;
the bracket-call crash should be handled as a later call-lowering/runtime
receiver tranche, not folded into the table migration.

### Tranche 8 - Literal-computed Math call guard

Fixed the call-lowering guard that classified every `Math[...]` call as a
direct Math call and then cast the property node as an identifier. Literal
computed calls such as `Math["max"](...)` now resolve through
`jm_get_math_method(...)` and use the existing Math lowering; non-literal
computed calls such as `Math[name](...)` fall through to ordinary member-call
lowering with receiver binding. This keeps the fast path explicit while
removing the unsafe AST cast that caused the direct bracket-call crash.

### Tranche 9 - Namespace registry expansion

Expanded the built-in registry foundation from Math to other low-risk
namespace method surfaces in `js_runtime.cpp`: `JSON`, `Reflect`, and
`Atomics`. Added shared `JsBuiltinMethodSpec` tables for each family and routed
their object initialization through `js_install_builtin_method_specs(...)`.
This removes three ad-hoc local method tables while preserving the existing
method names, built-in ids, `.length` values, and non-enumerable method
installation behavior.

`CSS` was deliberately postponed after investigation, then completed in the
CSS namespace representation phase below. Its namespace object had been tagged
through `MAP_KIND_CSSOM` plus a marker in `Map::type`, and `Map::type` is also
the ordinary property shape pointer used by `js_property_set` /
`js_map_get_fast`. Migrating `CSS.supports` and `CSS.escape` therefore needed a
representation fix before the method-table substitution.

Constructor static methods and prototype methods remain outside this tranche.
Those need descriptor/name/length tests before migration because they carry a
larger compatibility surface and more receiver-sensitive dispatch paths.

### Tranche 10 - Constructor static registry expansion

Moved constructor static method lookup and eager reflection population onto the
same `JsBuiltinMethodSpec` foundation. Added shared tables for `Object`,
`Array`, `String`, `Date`, `Promise`, `Number`, `Map`, `ArrayBuffer`, `Proxy`,
`%TypedArray%`, and `Symbol` static methods. `js_lookup_constructor_static(...)`
now resolves through the selected table, and `js_populate_constructor_statics(...)`
uses the same selected table when installing own non-enumerable constructor
properties.

Added `js_install_builtin_method_specs_on_function(...)` for function-object
properties, preserving the existing skip-if-present behavior used by
`Number.parseInt` / `Number.parseFloat` so they keep identity with the global
builtins. `%TypedArray%.from` and `%TypedArray%.of` now use that helper as
well, while `%TypedArray%[Symbol.species]` and the other species accessors stay
on the existing native-accessor path.

This removes the separate name-only constructor population tables and the large
hand-written static lookup branch, making first-class access, own-property
reflection, `.length`, and enumerability draw from one method spec source for
these constructor families.

### Tranche 11 - Prototype registry foundation

Extended the `JsBuiltinMethodSpec` foundation to the first low-risk prototype
families: `Object.prototype`, `Function.prototype`, `Array.prototype`, and
`Number.prototype`. Added shared prototype method spec tables and routed
prototype population through `js_populate_builtin_prototype_methods(...)`.
Dynamic built-in fallback lookup for Array, Function, and Number receiver
types now uses the same tables instead of local ad-hoc arrays/branches.

`js_append_builtin_method_names(...)` also reuses the shared prototype specs
for these families, while keeping String on its existing path because String
has alias display-name rules (`trimLeft`/`trimRight`) and Annex B wrapper
methods that should be migrated in a dedicated slice.

Verification:

- `make -C build/premake config=debug_native lambda -j1 CC="cc" CXX="c++"`
- temporary smoke script for dynamic Array/Function/Number/Object prototype
  access through `./lambda.exe js ... --no-log`
- `./test/test_js_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/array_methods:JavaScriptTests/JsFileTest.Run/number_methods:JavaScriptTests/JsFileTest.Run/v11_function_bind:JavaScriptTests/JsFileTest.Run/v11_object_methods:JavaScriptTests/JsFileTest.Run/v9_array_methods'`
- `make test262-baseline`: fully passed `30009 / 30009`, failed `0`,
  regressions `0`.

### Tranche 12 - Collection and promise prototype registry expansion

Expanded the prototype registry foundation to the next receiver-sensitive but
plain-method families: `Promise.prototype`, `Map.prototype`, `Set.prototype`,
`WeakMap.prototype`, `WeakSet.prototype`, and `ArrayBuffer.prototype`.
Added shared `JsBuiltinMethodSpec` tables and routed each constructor
prototype population path through `js_populate_builtin_prototype_methods(...)`.

Special semantics stayed local to the population blocks: `Map.prototype`
still aliases `[Symbol.iterator]` to `entries`, `Set.prototype.keys` remains
the exact same function object as `Set.prototype.values`, collection
`@@toStringTag` attributes are unchanged, and `ArrayBuffer.prototype`
keeps its existing tag/class setup before installing `slice` and `resize`.

Verification:

- `make -C build/premake config=debug_native lambda -j1 CC="cc" CXX="c++"`
- temporary smoke script for Map/Set/Promise/ArrayBuffer prototype
  method length, enumerability, and iterator identity through
  `./lambda.exe js ... --no-log`
- `./test/test_js_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/v11_map_set:JavaScriptTests/JsFileTest.Run/collections_advanced:JavaScriptTests/JsFileTest.Run/promise_basic:JavaScriptTests/JsFileTest.Run/typed_arrays'`
- `make test262-baseline`: fully passed `30009 / 30009`, failed `0`,
  regressions `0`.

### Tranche 13 - Date and RegExp prototype registry expansion

Moved the plain named method surfaces for `Date.prototype` and
`RegExp.prototype` onto shared `JsBuiltinMethodSpec` tables. Prototype
population now uses `js_populate_builtin_prototype_methods(...)`, and the
receiver-class fallback/prototype dispatch paths use
`js_lookup_builtin_prototype_method_for_class(...)` instead of separate local
Date/RegExp method lists.

Special properties stayed on their existing dedicated paths:
`Date.prototype[Symbol.toPrimitive]` remains installed and resolved through
the symbol-specific handling, while RegExp symbol methods and accessor
properties (`source`, `flags`, `global`, `unicode`, etc.) remain in the
RegExp population block because they carry getter and symbol semantics beyond
the plain method registry.

This also makes `Date.prototype.toJSON.length` consistently come from the
eager-install table value of `1`, matching the spec-facing prototype
installation path instead of preserving the older duplicate fallback value.
String prototype migration is still left for a separate tranche because of
alias/display-name behavior (`trimLeft` / `trimRight`) and Annex B methods.

Verification:

- `make -C build/premake config=debug_native lambda -j1 CC="cc" CXX="c++"`
- temporary smoke script for Date/RegExp prototype method length,
  enumerability, symbol visibility, dynamic dispatch, and RegExp source
  getter through `./lambda.exe js ... --no-log`
- `./test/test_js_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/v11_date_methods:JavaScriptTests/JsFileTest.Run/v11_regex_methods:JavaScriptTests/JsFileTest.Run/regex_advanced:JavaScriptTests/JsFileTest.Run/regex_backrefs:JavaScriptTests/JsFileTest.Run/regex_lookahead:JavaScriptTests/JsFileTest.Run/regex_lookbehind:JavaScriptTests/JsFileTest.Run/regex_neg_lookahead:JavaScriptTests/JsFileTest.Run/regex_unicode_props'`
- `make test262-baseline`: fully passed `30009 / 30009`, failed `0`,
  regressions `0`.

### Tranche 14 - String prototype registry expansion

Moved `String.prototype` named methods onto the shared
`JsBuiltinMethodSpec` registry. The table now drives prototype population,
primitive string method lookup, class fallback lookup for String wrapper
objects, and `getOwnPropertyNames` method-name enumeration.

Extended `JsBuiltinMethodSpec` with an optional `display_name` field so
property aliases can preserve their spec-visible function identity and names.
`trimLeft` and `trimRight` remain properties in the table, but create/resolve
the same cached builtins as `trimStart` and `trimEnd`, with canonical function
names `trimStart` and `trimEnd`. This keeps the alias behavior explicit in
data rather than reintroducing one-off lookup code.

The String registry includes the newer well-formed Unicode methods and Annex B
HTML wrapper methods (`anchor`, `bold`, `link`, `substr`, etc.). They are now
installed as non-enumerable own properties on `String.prototype`, matching the
existing name-enumeration surface that already exposed them.

Verification:

- `make -C build/premake config=debug_native lambda -j1 CC="cc" CXX="c++"`
- temporary smoke script for `trimLeft` / `trimRight` alias identity and
  canonical names, non-enumerability, Annex B own-property installation,
  primitive lookup, and String wrapper calls through
  `./lambda.exe js ... --no-log`
- `./test/test_js_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/string_methods:JavaScriptTests/JsFileTest.Run/v9_string_methods:JavaScriptTests/JsFileTest.Run/string_decoder_basic:JavaScriptTests/JsFileTest.Run/numeric_string_key_equivalence'`
- `make test262-baseline`: fully passed `30009 / 30009`, failed `0`,
  regressions `0`.

## Phase Completion - Built-in Registry Foundation

Finished the built-in registry foundation as a phase instead of continuing
family-by-family tranche work. The remaining ad-hoc prototype method/accessor
tables for `DataView.prototype` and `%TypedArray%.prototype` now use shared
`JsBuiltinMethodSpec` data plus reusable installation helpers:

- `js_install_builtin_function_specs(...)` creates method functions from table
  data while preserving specialized function flags for receiver-sensitive
  typed-array and DataView paths.
- `js_install_builtin_accessor_specs(...)` installs native getter descriptors
  from the same spec shape, preserving getter names such as `get buffer`,
  `get byteLength`, and `get length`.
- `js_populate_dataview_prototype_methods(...)` keeps early constructor
  initialization independent from the later registry table definitions.
- `%TypedArray%.prototype[Symbol.iterator]` remains an identity alias of
  `values`, while typed-array-only stub methods (`set`, `subarray`,
  `toLocaleString`) keep their existing dispatch behavior.

The registry phase now covers namespace methods (`Math`, `JSON`, `Reflect`,
`Atomics`), constructor static methods, and the plain built-in prototype method
surfaces listed in the phase plan. `CSS` was completed in the following
namespace representation phase. Remaining Js41 work should move to the next
structural boundary: exotic object operation tables, descriptor synthesis from
registry data, and eventual file splits.

Verification:

- `make -C build/premake config=debug_native lambda -j1 CC="cc" CXX="c++"`
- temporary smoke script for DataView and `%TypedArray%.prototype` function
  lengths, getter names, non-enumerability, iterator identity, and runtime
  dispatch through `./lambda.exe js ... --no-log`
- `./test/test_js_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/typed_arrays:JavaScriptTests/JsFileTest.Run/opt_p4_typed_reads'`
- `make test262-baseline`: fully passed `30009 / 30009`, failed `0`,
  regressions `0`.

## Phase J41-4 Complete - Built-in Descriptor Registry

Finished the full J41-4 phase by closing the gap left after the registry
foundation: descriptor and own-name synthesis now read from the same
`JsBuiltinMethodSpec` registry data used by method installation and runtime
lookup.

Code changes:

- Added registry-backed descriptor helpers in `lambda/js/js_runtime.cpp`:
  `js_builtin_registry_prototype_method_descriptor(...)`,
  `js_builtin_registry_has_prototype_method(...)`, and
  `js_append_builtin_method_names_for_class(...)`.
- Routed prototype virtual descriptor synthesis in
  `js_object_get_own_property_descriptor(...)` through the registry instead of
  rebuilding built-in descriptors from scattered `JsClass` / `TypeId` checks.
- Routed prototype built-in own-name synthesis through class-aware registry
  tables, so Date, RegExp, Map, Set, typed-array, DataView, and other
  registry-backed prototypes use the same source of truth.
- Added registry entries for compatibility surfaces that had previously been
  supplied by generic fallback behavior: `Array.prototype.toLocaleString`,
  `Number.prototype.toLocaleString`, `Date.prototype.toLocaleString`, and
  `Symbol.prototype.valueOf`.
- Added `JS_BOOLEAN_PROTOTYPE_METHOD_SPECS` and
  `JS_SYMBOL_PROTOTYPE_METHOD_SPECS` so Boolean and Symbol prototype descriptor
  behavior is explicit in registry data.
- Preserved specialized receiver-sensitive function flags for typed-array and
  DataView descriptor values/accessor getters when synthesizing descriptors
  from registry data.
- Added `Props.J41_4_BuiltinRegistry_DescriptorSynthesis` to the fast property
  kernel suite. It checks descriptor metadata across Date, RegExp, Map, Set,
  Promise, String alias functions, Array/Number locale methods, Symbol valueOf,
  `%TypedArray%.prototype`, and `DataView.prototype`.

Verification:

- `make -C build/premake config=debug_native lambda test_js_props_gtest -j1 CC="cc" CXX="c++"`
- `./test/test_js_props_gtest.exe`: passed `24 / 24`
- `./test/test_js_coerce_gtest.exe`: passed `15 / 15`
- `./test/test_js_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/typed_arrays:JavaScriptTests/JsFileTest.Run/opt_p4_typed_reads:JavaScriptTests/JsFileTest.Run/v11_date_methods:JavaScriptTests/JsFileTest.Run/v11_regex_methods:JavaScriptTests/JsFileTest.Run/string_methods:JavaScriptTests/JsFileTest.Run/v9_string_methods:JavaScriptTests/JsFileTest.Run/css_namespace:JavaScriptTests/JsFileTest.Run/native_backing_props'`: passed `8 / 8`
- `./test/test_css_dom_integration.exe`: passed `77`, skipped `25`, failed `0`
- `make test262-baseline`: fully passed `30009 / 30009`, failed `0`,
  regressions `0`

## Phase Completion - CSS Namespace Representation

Finished the CSS namespace representation phase that the registry work had
deferred. The global `CSS` object no longer identifies itself by writing a
sentinel into `Map::type`; it now has a dedicated `MAP_KIND_CSS_NAMESPACE`.
That keeps `Map::type` available for ordinary shape storage, so the namespace
can safely hold built-in methods and user-defined own properties at the same
time.

Code changes:

- Added `MAP_KIND_CSS_NAMESPACE` as a distinct exotic kind for the global
  `CSS` namespace object.
- Changed `js_is_css_namespace(...)` to test the map kind instead of a
  `Map::type` sentinel.
- Routed `CSS.supports` and `CSS.escape` through `JS_CSS_METHOD_SPECS` and
  `js_install_builtin_method_specs(...)`, matching the registry pattern used
  for `Math`, `JSON`, `Reflect`, and `Atomics`.
- Let the CSS namespace fall through to ordinary property get/set after the
  map-kind dispatch, while keeping CSSOM wrapper objects on `MAP_KIND_CSSOM`.
- Carried the new map kind through object-to-primitive fallback checks so
  string conversion keeps the previous namespace behavior.
- Added `test/js/css_namespace.js` and `test/js/css_namespace.txt` to lock the
  behavior: method `.length`, non-enumerability, `@@toStringTag`, direct and
  extracted calls, and user property writes after method installation.

Verification:

- `make -C build/premake config=debug_native lambda -j1 CC="cc" CXX="c++"`
- temporary smoke script for CSS namespace descriptors, own property writes,
  and method dispatch through `./lambda.exe js ... --no-log`
- `./test/test_js_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/css_namespace:JavaScriptTests/JsFileTest.Run/dom_style:JavaScriptTests/JsFileTest.Run/dom_basic'`
- `./test/test_css_dom_integration.exe`: passed `77`, skipped `25`, failed `0`
- `./test/test_wpt_css_syntax_gtest.exe`: still has unrelated existing CSS
  syntax failures in charset/escaped-url cases; not a CSS namespace regression
- `make test262-baseline`: fully passed `30009 / 30009`, failed `0`,
  regressions `0`.

## Phase Completion - Exotic Primitive Fallback Boundary

Finished a small but structural exotic-operation cleanup phase. The list of
map kinds that intentionally use the engine's default object-to-primitive
fallback is now centralized in one predicate:
`js_map_kind_uses_default_object_to_primitive(...)`.

Before this phase, the same list appeared separately in `js_to_primitive(...)`,
the object branch of `js_to_string(...)`, and Date's
`@@toPrimitive` fallback. The CSS namespace representation phase had to edit
all of those sites when it introduced `MAP_KIND_CSS_NAMESPACE`, which made the
duplication visible. The new helper is the single policy point for:

- DOM wrappers
- CSS namespace objects
- CSSOM wrappers
- document proxies
- foreign document wrappers

Code changes:

- Added `js_map_kind_uses_default_object_to_primitive(...)` in
  `lambda/js/js_runtime.h`.
- Replaced the repeated map-kind checks in `lambda/js/js_coerce.cpp` and
  `lambda/js/js_runtime.cpp`.
- Extended `test/js/css_namespace.js` / `.txt` to cover `String(CSS)` and
  `CSS + ""`, so both string-hint and default-hint primitive conversion paths
  stay guarded after the centralization.

Verification:

- `make -C build/premake config=debug_native lambda -j1 CC="cc" CXX="c++"`
- `./test/test_js_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/css_namespace:JavaScriptTests/JsFileTest.Run/dom_style:JavaScriptTests/JsFileTest.Run/dom_basic'`
- `./test/test_js_coerce_gtest.exe`
- `make test262-baseline`: fully passed `30009 / 30009`, failed `0`,
  regressions `0`.

## Phase Completion - Native-Backed Property Upgrade Boundary

Finished a focused exotic-object cleanup phase for native-backed map objects.
`ArrayBuffer`, `DataView`, and typed-array instances all start with
`Map::data` holding a native runtime pointer, then upgrade to ordinary
shape-backed storage the first time user code writes a non-index property.
Before this phase, each map-kind branch repeated the same conversion sequence:
capture the native pointer, reset the map to `EmptyMap`, and store the pointer
under an internal `__ab__`, `__dv__`, or `__ta__` slot.

Code changes:

- Added `js_upgrade_native_backed_map_for_properties(...)` in
  `lambda/js/js_runtime.cpp`.
- Routed `MAP_KIND_ARRAYBUFFER`, `MAP_KIND_DATAVIEW`, and
  `MAP_KIND_TYPED_ARRAY` property-write upgrades through the helper while
  preserving their existing map kinds and internal pointer slot names.
- Added `test/js/native_backing_props.js` / `.txt` to lock the boundary:
  user properties survive on ArrayBuffer/DataView/TypedArray objects after
  upgrade, and native getters/indexed storage still resolve correctly through
  the preserved internal pointer slots.

This is intentionally a small structural phase. It does not change the
ArrayBuffer/DataView/TypedArray exotic get/set semantics; it makes the
native-pointer-to-shape-storage transition one named operation so later exotic
operation table work has a single upgrade hook to call.

Verification:

- `make -C build/premake config=debug_native lambda -j1 CC="cc" CXX="c++"`
- `./test/test_js_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/native_backing_props:JavaScriptTests/JsFileTest.Run/typed_arrays:JavaScriptTests/JsFileTest.Run/opt_p4_typed_reads'`
- `make test262-baseline`: fully passed `30009 / 30009`, failed `0`,
  regressions `0`.

## Phase Completion - Exotic Property Dispatch Boundary

Finished a complete structural phase for `MapKind` property operations in
`lambda/js/js_runtime.cpp`. The large exotic `Get` and `Set` switch blocks no
longer sit inline inside `js_property_get(...)` and `js_property_set(...)`.
They now route through named helpers:

- `js_try_exotic_property_get(...)`
- `js_try_exotic_property_set(...)`

This phase keeps behavior unchanged while creating the next extraction
boundary for Js41's exotic operation table work. The helpers preserve the
existing handling for:

- typed-array indexed and intrinsic property reads
- ArrayBuffer/DataView native-backed reads and prototype fallback
- document proxies and foreign document active-document swapping
- DOM and CSSOM host-object property dispatch
- CSS namespace ordinary-property fallthrough
- iterator opacity and `[Symbol.iterator]` identity
- Proxy get/set traps
- process.env value coercion and host environment propagation
- native-backed property upgrade hooks for ArrayBuffer, DataView, and
  typed-array instances

Code changes:

- Added file-scope forward declarations for the typed-array base prototype and
  Buffer prototype helpers so the extracted property dispatcher can call them
  without introducing conflicting block-scope linkage.
- Moved the `js_property_get(...)` map-kind switch into
  `js_try_exotic_property_get(...)`.
- Moved the `js_property_set(...)` map-kind switch into
  `js_try_exotic_property_set(...)`, passing the value by pointer for
  `process.env` string coercion before ordinary storage.
- Left ordinary map property conversion, frozen/non-writable checks, accessor
  dispatch, and final `map_put(...)` storage in the main `js_property_set(...)`
  path.

Verification:

- `make -C build/premake config=debug_native lambda -j1 CC="cc" CXX="c++"`
- `./test/test_js_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/native_backing_props:JavaScriptTests/JsFileTest.Run/typed_arrays:JavaScriptTests/JsFileTest.Run/opt_p4_typed_reads:JavaScriptTests/JsFileTest.Run/css_namespace:JavaScriptTests/JsFileTest.Run/dom_style:JavaScriptTests/JsFileTest.Run/dom_basic'`
- `./test/test_js_coerce_gtest.exe`: passed `15 / 15`
- `./test/test_css_dom_integration.exe`: passed `77`, skipped `25`, failed `0`
- `make test262-baseline`: fully passed `30009 / 30009`, failed `0`,
  regressions `0`.

## Phase J41-5 Complete - Spec-Operation Convergence

Finished the full J41-5 phase. The runtime now has explicit exotic boundaries
around the ordinary property operations that test262 exercises most heavily:
`[[Get]]`, `[[Set]]`, `[[HasProperty]]`, `[[Delete]]`,
`[[OwnPropertyKeys]]`, and `[[GetOwnProperty]]`.

Code changes:

- `lambda/js/js_runtime.cpp` routes `js_property_get(...)` and
  `js_property_set(...)` through `js_try_exotic_property_get(...)` and
  `js_try_exotic_property_set(...)` before ordinary storage/prototype handling.
- `lambda/js/js_globals.cpp` routes `in`, `delete`, `Object.getOwnPropertyNames`,
  and `Object.getOwnPropertyDescriptor` through named exotic helpers:
  `js_try_exotic_has_property(...)`,
  `js_try_exotic_delete_property(...)`,
  `js_try_exotic_own_property_names(...)`, and
  `js_try_exotic_own_property_descriptor(...)`.
- Proxy traps for `has`, `deleteProperty`, `ownKeys`, and
  `getOwnPropertyDescriptor` now sit at the same abstract-operation boundary
  instead of being scattered inline through the public built-ins.
- Typed-array numeric index behavior is explicit for has/delete/ownKeys/GOPD,
  while named properties continue through ordinary map storage.
- String wrapper indexed properties are now explicit in `[[HasProperty]]`, so
  `"1" in Object("abc")` agrees with the existing own-name and descriptor paths.
- Arrays retain their indexed exotic behavior through the existing array paths,
  with the new GTest covering deletion, own keys, and named ordinary properties.
- Module namespace objects are currently represented as ordinary frozen maps
  (`js_module_namespace_create(...)` returns the export map), so there is no
  separate module-namespace `MapKind` hook to wire in this phase. The new
  abstract-operation dispatch points are ready for that future representation.

Spec-kernel tests added:

- `Props.J41_5_TypedArrayExotic_AbstractOps`
- `Props.J41_5_ProxyExotic_AbstractOps`
- `Props.J41_5_ArrayAndStringExotic_AbstractOps`

Verification:

- `make build-test -j1`
- `./test/test_js_props_gtest.exe`: passed `23 / 23`
- `./test/test_js_coerce_gtest.exe`: passed `15 / 15`
- `./test/test_js_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/native_backing_props:JavaScriptTests/JsFileTest.Run/typed_arrays:JavaScriptTests/JsFileTest.Run/opt_p4_typed_reads:JavaScriptTests/JsFileTest.Run/css_namespace:JavaScriptTests/JsFileTest.Run/dom_style:JavaScriptTests/JsFileTest.Run/dom_basic'`: passed `6 / 6`
- `./test/test_css_dom_integration.exe`: passed `77`, skipped `25`, failed `0`
- `make test262-baseline`: fully passed `30009 / 30009`, failed `0`,
  regressions `0`

## Phase J41-1 Complete - Mechanical Transpiler Split

Finished the full J41-1 mechanical split for the JavaScript MIR transpiler.
The transpiler is now split into real top-level C++ translation units so later
semantic phases can work in named ownership areas without touching a 28K-line
monolith.

Code changes:

- Replaced `lambda/js/transpile_js_mir.cpp` with a state anchor for the shared
  extern declarations, MIR optimization globals, and module/eval global state.
- Added `lambda/js/js_mir_context.hpp` for the shared JS MIR transpiler context
  structs and cleanup helper.
- Added `lambda/js/js_mir_internal.hpp` for shared helper declarations and
  cross-file extern state.
- Added `lambda/js/js_mir_hashmap_scope_utils.cpp`.
- Added `lambda/js/js_mir_analysis.cpp`.
- Added `lambda/js/js_mir_calls_boxing_types.cpp`.
- Added `lambda/js/js_mir_function_collection_class_inference.cpp`.
- Added `lambda/js/js_mir_expression_lowering.cpp`.
- Added `lambda/js/js_mir_statement_lowering.cpp`.
- Added `lambda/js/js_mir_function_class_lowering.cpp`.
- Added `lambda/js/js_mir_module_batch_lowering.cpp`.
- Added `lambda/js/js_mir_eval_lowering.cpp`.
- Added `lambda/js/js_mir_entrypoints_require.cpp`.

Boundary notes:

- This phase intentionally made no semantic extraction and no manual
  build-system change.
- Shared local structs, constants, helper prototypes, and module/eval globals
  are centralized in `js_mir_context.hpp` and `js_mir_internal.hpp` rather than
  in include fragments.
- The generated Premake configuration picks up the new top-level `.cpp` files
  automatically.
- `make build` regenerated `lambda/lambda-embed.h` from the current
  `lambda/lambda.h`; that generated-file change is incidental to the build
  gate rather than part of the transpiler split.

Verification:

- `make build`: passed.
- `./test/test_js_props_gtest.exe`: passed `24 / 24`.
- `./test/test_js_coerce_gtest.exe`: passed `15 / 15`.
- Focused `./test/test_js_gtest.exe` coverage for native-backed properties,
  typed arrays, CSS namespace, DOM style/basic, eval, and module paths:
  passed `9 / 9`.
- Full `./test/test_js_gtest.exe`: still has 6 unrelated JS semantic failures
  (`buffer_advanced`, `lib_handlebars`, `lib_pretext`, `lib_yup`, `lib_zod`,
  `zlib_basic`); the same 6 fail when filtered directly.
- `make test262-baseline`: fully passed `30009 / 30009`, failed `0`,
  regressions `0`.

## Phase J41-2 Complete - Mechanical Runtime Split

Finished the full J41-2 mechanical split for the JavaScript runtime. The split
uses real top-level C++ translation units and a shared internal header instead
of include fragments.

Code changes:

- Added `lambda/js/js_runtime_internal.hpp` as the shared internal runtime
  header for cross-file state, helper declarations, `JsFunction`, built-in ids,
  and built-in method specs.
- Added `lambda/js/js_runtime_state.cpp` for runtime globals, exception state,
  module vars, current `this`/`new.target`, reset helpers, and batch reset
  orchestration.
- Added `lambda/js/js_runtime_value.cpp` for `ToNumber`/`ToNumeric`/string
  conversion, numeric helpers, operators, BigInt checks, comparisons, bitwise
  operations, unary operations, and `typeof`.
- Added `lambda/js/js_runtime_function.cpp` for function wrapper allocation,
  closure env allocation, function metadata setters, function flags, and the
  function wrapper cache reset.
- Added `lambda/js/js_runtime_builtin_registry.cpp` for built-in method tables,
  method/spec lookup, method installation helpers, prototype/static installer
  helpers, and built-in descriptor synthesis hooks.
- Kept `lambda/js/js_runtime.cpp` as the remaining object/exotic/array/string/
  RegExp/promise/module host surface and the large built-in dispatch switch.
- Preserved existing property/descriptor split files:
  `lambda/js/js_props.cpp` and `lambda/js/js_property_attrs.cpp`.
- No manual build-system source list change was needed; the generated Premake
  files pick up the new `lambda/js/*.cpp` files.

Boundary notes:

- This phase is mechanical: public runtime ABI and MIR-imported symbols remain
  stable.
- The dispatch switch intentionally remains in `js_runtime.cpp`; moving it
  safely is a later semantic split after more built-ins are table-backed.
- The IDE's old `transpile_js_mir_parts/*.inc` tabs are stale. The source tree
  now uses proper `.cpp` files for both J41-1 and J41-2.

Verification:

- `make build`: passed with `Errors: 0`.
- `./test/test_js_coerce_gtest.exe`: passed `15 / 15`.
- `./test/test_js_props_gtest.exe`: passed `24 / 24`.
- Focused `./test/test_js_gtest.exe` coverage for native-backed properties,
  typed arrays, CSS namespace, DOM style/basic, eval, and module paths:
  passed `9 / 9`.
- `make test262-baseline`: fully passed `30009 / 30009`, failed `0`,
  regressions `0`.

## Phase J41-3 Complete - Runtime State Isolation

Finished the full J41-3 baseline state-isolation phase. The JS runtime no
longer exposes the migrated transient execution state as scattered free
globals; split runtime files and host modules now share one runtime-state
capsule through a narrow header.

Code changes:

- Added `lambda/js/js_runtime_state.hpp` with `JsRuntimeState`, the single
  `js_runtime_state` extern, active module-var accessor, and compatibility
  aliases for existing internal call sites.
- Moved the state struct and legacy-name aliases out of
  `lambda/js/js_runtime_internal.hpp`, leaving that header focused on internal
  runtime declarations.
- Kept the single `JsRuntimeState js_runtime_state` definition in
  `lambda/js/js_runtime_state.cpp`.
- Removed direct `extern` declarations for migrated state from
  `lambda/js/js_globals.cpp`, `lambda/js/js_property_attrs.cpp`,
  `lambda/js/js_state_guards.h`, `lambda/js/js_dom.cpp`,
  `lambda/js/js_dom_selection.cpp`, and the Node-style module files:
  `js_buffer.cpp`, `js_child_process.cpp`, `js_dns.cpp`, `js_events.cpp`,
  `js_fetch.cpp`, `js_fs.cpp`, `js_net.cpp`, and `js_querystring.cpp`.
- Migrated shared references for `js_input`, strict mode, accessor-dispatch
  suppression, Array iterator override tracking, module vars, heap epoch,
  RegExp legacy match state, current `this`, proxy receiver, `new.target`,
  pending call args, exception state, and runtime trace counters.

Boundary notes:

- Public MIR/runtime symbols remain stable; this is a state-ownership change,
  not a JS semantics change.
- The header still provides legacy-name aliases so the next semantic phases can
  migrate call sites incrementally without reintroducing exported globals.
- A full `--run-partial` test262 run still exposes existing partial-list
  pressure in RegExp/URI stress tests. The baseline isolation gate and normal
  full batch are clean.

Verification:

- `make build`: passed with `Errors: 0`.
- `./test/test_js_coerce_gtest.exe`: passed `15 / 15`.
- `./test/test_js_props_gtest.exe`: passed `24 / 24`.
- Focused `./test/test_js_gtest.exe` coverage for native-backed properties,
  typed arrays, CSS namespace, DOM style/basic, eval, and module paths:
  passed `9 / 9`.
- `make test262-baseline`: passed twice; both runs fully passed
  `30009 / 30009`, failed `0`, regressions `0`.
- `./test/test_js_test262_gtest.exe --batch-only`: fully passed
  `30013 / 34157`, failed `4144`, regressions `0`, improvements `4`.
- `./test/test_js_test262_gtest.exe --batch-only --run-partial`: ran
  `34169` tests; fully passed `30010`, failed `4157`, had `2`
  non-fully-passing URI slow/retry-only tests and one RegExp crash under merged
  partial pressure. The RegExp case
  `built_ins_RegExp_character_class_escape_non_whitespace_js` is already listed
  in `test/js262/t262_partial.txt` as `SLOW_20017` and passed isolated.
