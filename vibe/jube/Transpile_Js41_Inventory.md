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
| `MAP_KIND_DOM` | Radiant DOM wrapper |
| `MAP_KIND_DOC_PROXY` | active document proxy |
| `MAP_KIND_FOREIGN_DOC` | foreign document proxy |
| `MAP_KIND_CSSOM` | CSSOM wrappers |
| `MAP_KIND_ITERATOR` | engine-internal iterator objects |
| `MAP_KIND_PROXY` | Proxy object |
| `MAP_KIND_PROCESS_ENV` | Node `process.env` host object |

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
   non-enumerable installation semantics. `CSS` was investigated but left for
   a separate exotic-object cleanup because its namespace marker currently
   occupies `Map::type`, which conflicts with ordinary shape-backed method
   storage.
3. Migrate constructor static methods (`Object`, `Array`, `Number`, `Promise`,
   `Date`, `Symbol`, `TypedArray`) without changing their existing fallback
   semantics. Done for the plain method surfaces, and extended to matching
   `String`, `Map`, `ArrayBuffer`, and `Proxy` statics. Accessor statics such
   as `[Symbol.species]` remain in their existing accessor installation paths.
4. Migrate prototype families (`Array`, `String`, `Function`, `Number`) only
   after adding focused descriptor/name/length tests, since these have a larger
   compatibility surface.

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

`CSS` was deliberately postponed after investigation. Its namespace object is
tagged through `MAP_KIND_CSSOM` plus `js_css_namespace_marker` in `Map::type`,
and `Map::type` is also the ordinary property shape pointer used by
`js_property_set` / `js_map_get_fast`. Migrating `CSS.supports` and
`CSS.escape` to the ordinary registry path therefore needs a separate
representation fix rather than a small method-table substitution.

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
