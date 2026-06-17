# Transpile_Js59 - Retire JS String-Marker Metadata

Date: 2026-06-17

Status: proposal with P1/P2/P3 landed. Js58 closed the sparse-array work with a clean ES2024 baseline. Js59 finishes the object/property metadata migration described in `doc/dev/js/JS_06_Objects_Properties_Prototypes.md`: retire engine dependence on string-marker metadata (`__nw_` / `__ne_` / `__nc_` / `__get_` / `__set_` / `__class_name__`) and make `ShapeEntry::flags`, `JsAccessorPair`, and `TypeMap::js_class` the only ordinary metadata sources.

This is a correctness and maintainability proposal, not a performance-tuning proposal. The rule for every phase is: no hard-coded workaround, no MIR patch, no parser regeneration, and no broad refactor unless the phase root cause requires it.

## 0. Implementation Status

Landed on 2026-06-17:

- P1 accessor-marker production/removal is implemented.
- Object-literal getter/setter AST keys now keep the visible property name instead of synthesizing `__get_` / `__set_`.
- MIR object/class accessor lowering routes bare keys through `js_install_user_accessor` and no longer strips accessor-marker prefixes.
- `js_property_set` no longer intercepts `__get_` / `__set_` writes; those names are ordinary user properties.
- At the P1 boundary, ordinary delete and array companion-map delete tombstoned only the then-remaining attribute markers (`__nw_`, `__ne_`, `__nc_`).
- `Symbol.prototype.description` is installed through `js_install_native_accessor`, not the old `__get_description` marker.
- `Object.getOwnPropertyNames` preserves the legacy broad hide for other `__*` metadata but explicitly allows user-visible `__get_` / `__set_` names. This keeps existing bundled-library behavior stable while removing accessor-marker dependence.
- Added `test/js/props/metadata_accessor_storage.{js,txt}` and documented it in `test/js/props/README.md`.
- P2 named attribute-marker fallback removal is implemented.
- At the P2 boundary, `js_attr_set_*` mutated `ShapeEntry::flags` for named properties while array companion-map numeric index / `length` marker fallback remained P3-scoped.
- `js_property_set` no longer runs `js_dual_write_marker_flags` for ordinary named objects, so user keys like `__nw_x`, `__ne_x`, and `__nc_x` store ordinary data.
- Named descriptor synthesis, property-set writability checks, delete non-configurable checks, and ordinary delete no longer read named `__nw_` / `__ne_` / `__nc_` marker slots. Ordinary delete clears shape flags directly before writing the delete sentinel.
- Built-in named marker writes for Error message/name/stack, class instance `constructor`, native accessor configurability, and `@@unscopables` were replaced with direct attr helpers after the target property exists.
- `Object.keys` / `Object.getOwnPropertyNames` now allow user-visible `__nw_` / `__ne_` / `__nc_` names on ordinary objects. The remaining array companion-map transition metadata was removed in P3.
- Added `test/js/props/metadata_named_attr_flags.{js,txt}` and `test/js/props/metadata_user_attribute_keys.{js,txt}`.
- P3 array index and length attribute-marker retirement is implemented.
- `js_attr_set_*` now materializes array digit-string indices and `length` into companion-map shape entries before mutating `ShapeEntry::flags`; it no longer writes `__nw_` / `__ne_` / `__nc_` marker slots for those properties.
- Descriptor-special array indices keep one authoritative value source: the companion-map slot owns data/accessor descriptors and the dense slot is tombstoned only after companion-map materialization succeeds. This preserves `Object.freeze` / `preventExtensions` cases where descriptor bookkeeping is internal even though public extension is closed.
- Array `length` writability checks now read the `length` shape entry flags, and growth paths reject non-writable length through that shape-backed state.
- Array descriptor synthesis, `Object.keys`, `Object.getOwnPropertyNames`, `propertyIsEnumerable`, `delete`, `hasOwnProperty`, and dense-hole lookups now consult companion-map digit entries instead of marker fallback slots.
- Mapped arguments reads now consult promoted companion descriptor slots before falling back to mapped parameter storage, preserving Test262 `Object.defineProperty(arguments, "0", ...)` behavior after dense argument slots are tombstoned.
- User keys that look like array metadata, such as `__nw_0` and `__nw_length`, are ordinary own properties.
- Added `test/js/props/metadata_array_index_attrs.{js,txt}` and `test/js/props/metadata_array_length_attrs.{js,txt}`.

Verified gates:

```text
make build-test
./test/test_js_gtest.exe --gtest_filter='*metadata_accessor_storage*:*delete_then_define_accessor*:*delete_accessor_then_define_data*:*proto_accessor_redef_safe*:*super_property_set_finds_inherited_setter*' --gtest_brief=1
./test/test_js_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/*metadata*' --gtest_brief=1
./test/test_js_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/*delete*:JavaScriptTests/JsFileTest.Run/*metadata*:JavaScriptTests/JsFileTest.Run/*super_property_set_finds_inherited_setter' --gtest_brief=1
./test/test_js_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/*metadata*:JavaScriptTests/JsFileTest.Run/*sparse*:JavaScriptTests/JsFileTest.Run/*property_descriptors*:JavaScriptTests/JsFileTest.Run/*delete*:JavaScriptTests/JsFileTest.Run/lib_immer:JavaScriptTests/JsFileTest.Run/v20_tagged_templates' --gtest_brief=1
./test/test_js_gtest.exe --gtest_brief=1
./test/test_js_test262_gtest.exe --batch-only --run-async --batch-file=temp/js59_p3_regressions.txt --jobs=1 --batch-chunk-size=1 --async-chunk-size=1 --write-failures=temp/js59_p3_regressions_failures.tsv
make test262-baseline
```

Results:

- Focused JS gtest: 5/5 passed.
- Focused metadata shard: 3/3 passed.
- Focused delete/metadata/super shard: 8/8 passed.
- Focused P3 metadata/sparse/descriptors/delete/freeze/tagged-template shard: 23/23 passed.
- Targeted P3 Test262 regression rerun: 19/19 passed, failed 0.
- Full JS gtest: 214/214 passed.
- test262 baseline: first full P3 run fully passed 40261 / 40261, failed 0, regressions 0.
- Final post-cleanup test262 baseline: fully passed 40260 / 40261; 1 retry-only non-fully-passing slow test (`built_ins_decodeURI_S15_1_3_1_A2_5_T1_js`); failed 0; regressions 0.

Remaining work:

- P4 still owns `__class_name__` retirement in favor of `TypeMap::js_class` and explicit prototype identity.
- P5/P6 still own deleted-state cleanup and final documentation.

## 1. Starting Baseline

Current checked-in state at proposal time:

- `ShapeEntry::flags` already carries `JSPD_NON_WRITABLE`, `JSPD_NON_ENUMERABLE`, `JSPD_NON_CONFIGURABLE`, `JSPD_IS_ACCESSOR`, and `JSPD_DELETED` in `lambda/lambda-data.hpp`.
- `JsAccessorPair` already stores getter/setter pairs directly under the real property key, with `JSPD_IS_ACCESSOR` disambiguating the slot.
- `js_property_set` still runs the transition hooks `js_intercept_accessor_marker` and `js_dual_write_marker_flags` before ordinary storage.
- Named-property attribute reads are mostly shape-first, but digit-string array attributes and array `length` still use `__nw_` / `__ne_` / `__nc_` marker fallbacks.
- Static object-literal accessor parsing in `build_js_ast.cpp` still encodes non-computed getter/setter keys as `__get_X` / `__set_X`, and MIR lowering strips the prefix later.
- `TypeMap::js_class` exists and many readers prefer it, but many writers/readers still use `__class_name__`, especially older built-ins, DOM/CSS wrappers, stream/formdata helpers, user-defined class lowering, and legacy instanceof/duck-typing paths.
- Deletes still dual-write slot sentinel state and `JSPD_DELETED`; array holes and FUNC virtual property shadows still depend on `JS_DELETED_SENTINEL_VAL`.

Latest known Js58 admission evidence in the checked-in proposal is `make test262-baseline` with `Fully passed: 40261 / 40261`, `Failed: 0`, and `Regressions: 0`. Js59 P0 must refresh this in the current checkout before code changes.

## 2. Goal And Non-Goals

Goal:

- No ordinary object/property metadata is stored as public-looking string keys.
- `Object.keys`, `Object.getOwnPropertyNames`, spread, assign, JSON, descriptor APIs, prototype lookup, setters/getters, deletion, freeze/seal, and instanceof agree on the same metadata representation.
- User properties named `__get_x`, `__set_x`, `__nw_x`, `__ne_x`, `__nc_x`, or `__class_name__` behave as normal JS properties unless an explicit public API says otherwise.
- Array index descriptors use companion-map shape entries when they need attributes/accessors beyond the dense array slot.
- Built-in class identity uses `TypeMap::js_class`; dynamic/user class identity uses normal constructor/prototype links, not engine duck-typing through `__class_name__`.

Non-goals:

- Do not remove `JS_DELETED_SENTINEL_VAL` from dense array holes in Js59. Array holes are value-slot state, not property metadata.
- Do not patch MIR or change MIR register allocation.
- Do not change `Map`/`TypeMap` layout more than the remaining flag-byte bits require.
- Do not run performance claims from a debug build.
- Do not touch `parser.c`, `.lua` build files, or `log.conf`.

## 3. Risk Model

This migration touches one of the hottest and widest semantic surfaces in LambdaJS. The safe route is not "grep and delete markers"; it is to remove one marker family only after all producers, readers, enumerators, delete paths, and descriptor APIs have a shape-backed replacement.

Highest-risk areas:

- Array numeric index descriptors: dense storage lives in `Array::items`, while descriptor attributes live in the companion map.
- Array `length`: virtual property with special writable semantics, currently tracked by `__nw_length`.
- FUNC virtual properties: `length`, `name`, and `prototype` can be shadow-deleted by sentinel slots even when no ordinary stored slot exists.
- Built-in class hierarchy checks: current code sometimes uses string suffix checks such as "ends with Error" or class-name equality along a prototype chain.
- User-defined classes: dynamic names cannot all become enum values; they need a separate non-marker model.

## 4. Phase Plan

### P0 - Inventory And Regression Fixtures

Work:

1. Refresh the current correctness baseline:

```text
make build-test
./test/test_js_gtest.exe --gtest_brief=1
make test262-baseline
```

2. Add focused fixtures under `test/js/props/metadata_*.{js,txt}` before changing storage:

| Fixture | What it pins |
|---|---|
| `metadata_accessor_storage` | User properties named `__get_x` / `__set_x` are ordinary properties, and object/class/static accessors still dispatch through `JsAccessorPair` with no marker keys in own names. |
| `metadata_user_attribute_keys` | User properties named `__nw_x`, `__ne_x`, and `__nc_x` are ordinary enumerable data properties when written by user JS. |
| `metadata_named_attr_flags` | `Object.defineProperty` for named data props survives get/set/delete/redefine without marker slots. |
| `metadata_array_index_attrs` | `Object.defineProperty(arr, "5", ...)` preserves value, writable/enumerable/configurable, delete behavior, and sparse/dense reads. |
| `metadata_array_length_attrs` | `Object.defineProperty(arr, "length", { writable:false })` blocks growth/shrink correctly without `__nw_length`. |
| `metadata_class_identity` | Built-in wrappers, errors, promises, typed arrays, streams, DOM/CSS wrappers, and util/assert type checks resolve through `JsClass` or explicit prototype links. |

Acceptance:

- New focused fixtures pass in `./test/test_js_gtest.exe --gtest_filter='*props*:*property_descriptors*' --gtest_brief=1`.
- Full `./test/test_js_gtest.exe --gtest_brief=1` passes.
- `make test262-baseline` reports 0 regressions.

### P1 - Remove Accessor Marker Production

Root cause:

`__get_` / `__set_` were an AST/storage encoding. Runtime accessors now have first-class flags and `JsAccessorPair`, but the parser/AST and `js_property_set` compatibility intercept still let marker-looking user keys change engine metadata.

Work:

1. Change `build_js_ast.cpp` so getter/setter property nodes keep the bare key and only set `is_getter` / `is_setter`. Do not synthesize `__get_` / `__set_` identifiers.
2. Keep computed and non-computed lowering on the same path: box the bare property key, then call `js_install_user_accessor` / `js_define_accessor_partial`.
3. Remove the public `js_property_set` call to `js_intercept_accessor_marker`. After this point, `obj.__get_x = 1` must create a normal property.
4. Delete the remaining `__get_` / `__set_` marker tombstone loops from ordinary delete paths after fixtures confirm they are unreachable.
5. Remove `__get_` / `__set_` from `js_is_engine_internal_enumeration_key` so user keys with those prefixes can enumerate normally.
6. Update stale comments that still describe array accessor marker bypasses.

Do not remove `JsAccessorPair`; it is the target representation.

Acceptance:

- `metadata_accessor_storage` and existing `delete_then_define_accessor`, `delete_accessor_then_define_data`, `proto_accessor_redef_safe`, and `super_property_set_finds_inherited_setter` pass.
- `rg -n "__get_|__set_" lambda/js` has no executable object/class accessor producer or `js_property_set` intercept. Array numeric accessor descriptor marker code remains P3-scoped until array index descriptors move fully to companion-map `ShapeEntry` flags.
- Full `./test/test_js_gtest.exe --gtest_brief=1` and `make test262-baseline` pass.

### P2 - Remove Named Attribute Marker Fallbacks

Root cause:

Named properties already have shape entries by the time descriptor attributes matter. Keeping `__nw_` / `__ne_` / `__nc_` as fallback for named keys makes user marker-looking names magical and keeps enumeration filters alive.

Work:

1. Split the attribute helper surface:
   - `js_attr_set_named_*` requires or creates a real named shape entry and mutates `ShapeEntry::flags`.
   - `js_attr_set_indexed_*` is a temporary wrapper for array index/length work until P3.
2. Replace named-property calls to `js_attr_apply_or_marker` with `js_attr_set_named_*`.
3. Remove named-key marker reads from `js_props_desc_from_storage`, `js_prop_attrs_fast_path`, `js_property_set`, `js_delete_property`, freeze/seal, `Object.assign`, spread, and enumeration.
4. Remove the public `js_property_set` call to `js_dual_write_marker_flags` for named properties. Marker-looking keys written by user code must store ordinary values.
5. Replace explicit built-in marker writes such as `__ne_message`, `__ne_constructor`, `__nw___sym_11`, and `__nc_length` with direct `js_attr_set_named_*` calls after their target property exists.

Special case:

Do not fold array `length` or numeric array indices into this phase. They are not ordinary named properties because their values are virtual or stored in `Array::items`.

Acceptance:

- `metadata_user_attribute_keys`, `metadata_named_attr_flags`, property descriptors, and the full props fixture set pass.
- `Object.getOwnPropertyNames` no longer needs to suppress `__nw_` / `__ne_` / `__nc_` for named objects because engine code no longer creates those slots.
- Full `./test/test_js_gtest.exe --gtest_brief=1` and `make test262-baseline` pass.

Landed evidence:

- `metadata_named_attr_flags` confirms named descriptor attributes are shape flags and no `__nw_locked` / `__ne_locked` / `__nc_locked` own names are produced.
- `metadata_user_attribute_keys` confirms user-authored `__nw_` / `__ne_` / `__nc_` names enumerate and do not mutate another property's descriptor bits.
- Full `./test/test_js_gtest.exe --gtest_brief=1` passed 210/210.
- `make test262-baseline` passed 40261 / 40261 with failed 0 and regressions 0.

### P3 - Migrate Array Index And Length Attributes

Root cause:

Dense array data lives in `arr->items[idx]`, so a plain dense element has no `ShapeEntry`. That is why numeric index attributes still fall back to `__nw_<idx>` / `__ne_<idx>` / `__nc_<idx>`, and length writability still falls back to `__nw_length`.

Chosen model:

- Plain dense elements with default attributes stay in `arr->items`.
- Any numeric index that receives non-default attributes or an accessor is promoted to the companion map under the bare digit-string key.
- The companion-map `ShapeEntry` carries the attributes/accessor bit. Its slot carries either the data value or a `JsAccessorPair`.
- The dense slot for that index becomes a hole sentinel when the companion map owns the descriptor-special value. This avoids two authoritative data sources.
- Array `length` gets a companion-map shape entry named `length` whose flags carry writable/configurable/enumerable semantics. The actual length value remains `Array::length`.

Work:

1. Add helper `js_array_promote_index_descriptor(arr, idx, value_or_pair, flags)`.
2. Route `Object.defineProperty(arr, "<idx>", desc)` through the promotion helper whenever descriptor fields are non-default or accessor-based.
3. Teach `js_array_element`, `js_array_has_element`, array iteration helpers, `js_has_own_property`, and descriptor synthesis to prefer the companion-map digit entry when present and not deleted.
4. Replace `__nw_length` probes with a `ShapeEntry` flag lookup for the companion-map `length` entry.
5. Remove digit-string marker fallback from `js_props_query_*`.
6. Remove `__nw_` / `__ne_` / `__nc_` from enumeration filters once no phase writes them.

Risk controls:

- Keep dense default arrays on the current hot path. Only descriptor-special indexes are promoted.
- Do not change sparse-array density logic from Js58.
- Run the sparse fixtures because companion-map numeric entries overlap sparse storage.

Acceptance:

- `metadata_array_index_attrs`, `metadata_array_length_attrs`, `test/js/sparse_*`, and `property_descriptors` pass.
- `make test262-baseline` keeps 0 regressions.
- Dense-array timing can be smoke-checked with `./test/test_js_transpile_timing_gtest.exe --gtest_brief=1`; do not claim a formal perf delta unless a release-mode before/after baseline exists in `temp/js59_perf/`.

Landed evidence:

- `metadata_array_index_attrs` confirms descriptor-special indices store attributes in `ShapeEntry::flags`, preserve data values through companion-map reads, hide no engine marker own names, keep marker-looking user keys ordinary, and enforce non-configurable delete/redefine behavior.
- `metadata_array_length_attrs` confirms `length` writability is shape-backed, no `__nw_length` own name is produced, marker-looking user keys are ordinary, and the descriptor remains non-enumerable/non-configurable.
- `Object.freeze` / `preventExtensions` regressions in `lib_immer` and `v20_tagged_templates` were fixed by materializing companion-map descriptor slots with the internal map writer before tombstoning dense values.
- A 19-test Test262 mapped-arguments regression cluster was fixed by letting mapped argument reads consult promoted companion descriptor slots before falling back to parameter storage.
- Full `./test/test_js_gtest.exe --gtest_brief=1` passed 214/214.
- `make test262-baseline` reported failed 0 and regressions 0. The first full P3 run fully passed 40261 / 40261; the final post-cleanup run recovered one slow `decodeURI` test in Phase 4 and reported 40260 / 40261 fully passing plus 1 retry-only non-fully-passing test.

### P4 - Finish Built-In Class Identity Migration

Root cause:

`TypeMap::js_class` exists, but the engine still has three class identity models: enum byte, `__class_name__` string, and ad hoc string duck-typing.

Work:

1. Inventory every remaining `__class_name__` write. Classify as:
   - built-in class already in `JsClass`;
   - built-in class missing from `JsClass`;
   - dynamic/user-defined class;
   - compatibility display/name property, not engine metadata.
2. Add missing engine-known classes to `JsClass` at the end of the enum only. Do not renumber existing entries.
3. Replace built-in `__class_name__` writes with `js_class_stamp`.
4. Replace string hierarchy checks with explicit enum helpers:
   - `js_class_is_error_like(cls)`;
   - `js_class_is_event_like(cls)`;
   - `js_class_is_stream_like(cls)`;
   - `js_class_builtin_prototype(cls)`.
5. Update `instanceof`, `Object.prototype.toString`, util/assert type checks, implicit prototype synthesis, constructor lookup, and builtin method dispatch to use the enum helpers.
6. For user-defined classes, stop using `__class_name__` as engine metadata. Use constructor/prototype identity and ordinary function `.name` for display.
7. Remove `js_class_id` fallback to `__class_name__` only after all built-in readers are enum-backed and user-defined class paths no longer need the marker.

Acceptance:

- `metadata_class_identity`, `class_advanced`, `promise_subclass_prealloc`, `native_backing_props`, `util_promisify`, Node module gtests that use util/assert, and existing class props fixtures pass.
- `rg -n '"__class_name__"' lambda/js` contains no executable engine metadata write/read for built-in dispatch. User-facing display names may remain only under ordinary property names such as `name`.
- Full `./test/test_js_gtest.exe --gtest_brief=1` and `make test262-baseline` pass.

### P5 - Centralize Deleted/Tombstone Semantics For Ordinary Maps

Root cause:

`JSPD_DELETED` exists, but most readers still check `JS_DELETED_SENTINEL_VAL` directly. Dropping sentinels blindly is unsafe because dense array holes and FUNC virtual shadows still use them.

Chosen model:

- Ordinary MAP/FUNC/ARRAY companion-map properties use a single read helper that combines shape status and slot status.
- Dense array holes continue to use `JS_DELETED_SENTINEL_VAL`.
- FUNC virtual shadowing keeps a narrow sentinel path unless it is first modeled as an explicit shape entry that can shadow `length`, `name`, and `prototype`.

Work:

1. Add `js_own_shape_slot_status(obj, name, len, &slot, &se)` returning `ABSENT`, `DELETED`, `DATA`, or `ACCESSOR`.
2. Route `js_ordinary_get_own`, `js_ordinary_has_own`, `js_get_own_property_descriptor`, enumeration, spread/assign, delete, and descriptor application through that helper for ordinary map storage.
3. On ordinary data/accessor writes, clear `JSPD_DELETED` before installing the new value/pair.
4. On ordinary deletes, set `JSPD_DELETED` and clear `JSPD_IS_ACCESSOR`. Keep the sentinel write only for readers not yet migrated in the same phase; remove it after the helper sweep is complete.
5. Leave array holes and FUNC virtual shadowing explicitly documented as sentinel users.

Acceptance:

- Existing tombstone fixtures plus a new `metadata_delete_shape_status` fixture pass.
- No ordinary MAP reader manually interprets sentinel without going through the status helper.
- Full `./test/test_js_gtest.exe --gtest_brief=1` and `make test262-baseline` pass.

### P6 - Cleanup, Docs, And Final Gate

Work:

1. Remove or verify dead helpers:
   - verify `js_intercept_accessor_marker` remains gone;
   - remove `js_dual_write_marker_flags` after P2/P3 stop writing attribute markers;
   - remove marker-specific named-property helpers;
   - remove internal enumeration filters for retired marker prefixes.
2. Update `doc/dev/js/JS_06_Objects_Properties_Prototypes.md`:
   - mark the migration complete for named properties and built-in class identity;
   - document remaining non-metadata sentinel uses: dense array holes and any retained FUNC virtual shadow.
3. Update `test/js/props/README.md` with the new metadata fixtures.
4. Run final gates:

```text
make build-test
./test/test_js_gtest.exe --gtest_brief=1
make test262-baseline
```

Optional timing smoke after final correctness:

```text
make release
./test/test_js_transpile_timing_gtest.exe --gtest_brief=1
```

## 5. Acceptance Bar

Each phase must meet:

- `make build-test` passes.
- Full `./test/test_js_gtest.exe --gtest_brief=1` passes, not only focused fixtures.
- `make test262-baseline` reports 0 regressions, 0 failed baseline tests, and no crash/batch-lost exits.
- Any new `test/js/*.js` file has a paired `.txt` expected-output file.
- Any temporary artifacts live under `temp/js59_*`, never `/tmp`.
- No phase claims performance improvement unless verified in a release build.

Final Js59 success means:

- `__get_` / `__set_` are not engine metadata.
- `__nw_` / `__ne_` / `__nc_` are not engine metadata.
- `__class_name__` is not built-in engine metadata.
- Named and descriptor-special indexed properties use `ShapeEntry::flags`.
- Accessors use `JsAccessorPair`.
- Built-in class dispatch uses `JsClass`.
- Ordinary map delete state is queried through shape/slot status, with array-hole sentinel usage documented as value storage rather than metadata.

## 6. Why This Should Not Regress Test262

The migration is representation-preserving by phase:

- P1 changes accessor producer encoding, but the observable getter/setter behavior stays routed through the already-admitted `JsAccessorPair` path.
- P2 removes named marker fallbacks only after named properties have shape-backed attributes.
- P3 changes only descriptor-special array indices and `length` attributes; default dense arrays keep their existing storage.
- P4 changes built-in class identity lookup, but keeps prototype and constructor behavior intact.
- P5 changes ordinary map tombstone queries through a helper before dropping any ordinary sentinel write.

The full `make test262-baseline` gate is mandatory because property metadata bugs often surface far from the touched code: object spread, module namespace objects, typed arrays, RegExp `lastIndex`, Promise subclassing, Node util/assert helpers, and DOM wrappers all depend on these same descriptor paths.
