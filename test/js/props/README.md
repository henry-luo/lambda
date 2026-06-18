# Property-Model Invariant Tests

Fast, focused regression tests targeting the property-semantics bug classes
identified in `vibe/jube/Transpile_Js38_Refactor.md` (Stage B).

Each `.js` here is paired with a `.txt` of expected stdout. They are run as
part of `test_js_gtest.exe` in batch mode and complete in <1s collectively.

## Conventions

- One invariant per file. Filename describes the invariant.
- Output is `OK` for each passing assertion (or a specific value).
- A test that crashes or produces unexpected output is a regression.

## Coverage map

| File | Invariant | Spec |
|---|---|---|
| `delete_float_key_no_sentinel_leak.js` | `delete obj[1.5]` then redefine same key never reads `JS_DELETED_SENTINEL_VAL` as `JsAccessorPair*` | §10.1.10 OrdinaryDelete |
| `delete_negative_int_key_canonical.js` | `delete obj[-1]` matches the shape entry created by `obj[-1] = ...` | §7.1.19 ToPropertyKey |
| `delete_then_define_accessor.js` | data → delete → accessor sequence on same key does not crash | §10.1.6 OrdinaryDefineOwnProperty |
| `delete_accessor_then_define_data.js` | accessor → delete → data sequence on same key clears IS_ACCESSOR | same |
| `class_method_dispatch_honors_own_deletion.js` | `delete Boolean.prototype.toString` falls through to `Object.prototype.toString` | §10.1.5 OrdinaryGet |
| `class_method_dispatch_honors_proto_deletion.js` | deletion on inherited proto level falls through correctly | same |
| `numeric_string_key_equivalence.js` | `obj["1"]` and `obj[1]` refer to the same slot | §7.1.19 |
| `super_property_set_finds_inherited_setter.js` | `super.x = v` walks proto chain via accessor pair API | §10.1.9 OrdinarySet |
| `metadata_accessor_storage.js` | accessor names beginning `__get_`/`__set_` remain user-visible keys after marker removal | §10.1.7 OrdinaryOwnPropertyKeys |
| `metadata_named_attr_flags.js` | named descriptor attributes live in `ShapeEntry::flags` without `__nw_`/`__ne_`/`__nc_` own slots | §10.1.6 OrdinaryDefineOwnProperty |
| `metadata_user_attribute_keys.js` | user properties named `__nw_x`/`__ne_x`/`__nc_x` are ordinary enumerable data keys | §10.1.7 OrdinaryOwnPropertyKeys |
| `metadata_array_index_attrs.js` | array index descriptor attributes live on the companion-map digit `ShapeEntry`, not `__nw_`/`__ne_`/`__nc_` marker slots | §10.4.2.1 Array Exotic Objects |
| `metadata_array_length_attrs.js` | array `length` writability is a companion-map `ShapeEntry` flag and marker-looking user keys stay ordinary | §10.4.2.4 ArraySetLength |
| `metadata_function_custom_attrs.js` | function custom-property enumerability is read from `properties_map` `ShapeEntry::flags`, not `__ne_` marker slots | §20.1.3.4 propertyIsEnumerable |
| `metadata_regexp_lastindex_attrs.js` | `RegExp.prototype.compile` enforces `lastIndex` writability from `ShapeEntry::flags`, not `__nw_lastIndex` | §22.2.3.1 RegExpAlloc |
| `metadata_class_identity.js` | public `__class_name__` is an ordinary key; built-in brands use `TypeMap::js_class`, and user classes use constructor/prototype identity | §10.1.1 OrdinaryObjectCreate |
