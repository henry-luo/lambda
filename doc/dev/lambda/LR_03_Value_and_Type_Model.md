# Lambda Runtime — Value & Type Model

> **Part of the [Lambda core-runtime detailed-design set](LR_00_Overview.md).** This document covers how a Lambda value is represented at runtime: the 64-bit tagged `Item` layout, the `TypeId` enum and its three storage classes, the boxing/unboxing rules, the `Container` struct family (range, list/array, numeric array, map, object, element, vmap), the map *shape* machinery (`TypeMap`/`ShapeEntry`), and the static `Type*` family used for compile-time typing.
>
> **Primary sources:** `lambda/lambda.h` (C-mode `Item`, `EnumTypeId`, container structs, boxing macros, C-API), `lambda/lambda.hpp` (the real bit-field `struct Item`, C++ container structs, unbox helpers), `lambda/lambda-data.hpp` (`Type*` family, `ShapeEntry`/`TypeMap`, singletons), `lambda/lambda-data.cpp` (unbox implementations, `item_deep_equal`, type singletons), `lambda/lambda-data-runtime.cpp` (element access + boxing at the boundary), `lambda/vmap.cpp` (`VMap`), and `lambda/js/js_object_meta.{h,cpp}` (the JS-only immutable TypeMap metadata refinement).
> **Audience:** engine developers. **Convention:** `file:line` references drift; confirm against the cited symbol names.

---

## 1. Purpose & scope

Everything in the Lambda runtime — script values, parsed documents, JIT-produced temporaries — is a `Item`: a single 64-bit tagged word. This document is the map of that representation: how the type tag is packed, where each kind of value physically lives, how the JIT and the runtime move values in and out of their boxed forms, and how the container types are laid out so that one `get_type_id` dispatch handles all of them uniformly. The *memory* that backs these values (the GC heap, execution side stacks, and name/shape pools) is owned by [LR_08 — Memory Management & Garbage Collection](LR_08_Memory_and_GC.md); the *code that emits* the boxing operations is owned by [LR_06 — The C Transpiler](LR_06_C_Transpiler.md) and [LR_07 — The MIR Direct Transpiler & JIT](LR_07_MIR_Transpiler_JIT.md); the numeric, string and vector *operations* over these values live in [LR_04 — Numbers, Decimal & DateTime](LR_04_Numbers_Decimal_DateTime.md) and [LR_05 — Strings, Symbols & Vectors](LR_05_Strings_and_Vectors.md).

One structural fact frames the rest: there are **two definitions of `Item` with one ABI**. The MIR JIT consumes a slim, C-clean header where `Item` is an opaque `typedef uint64_t Item` (`lambda.h:526`); the C++ runtime sees the real `struct Item`, a union of bit-fields (`lambda.hpp:88`). The two are bit-compatible by construction, so a value produced by JIT code and a value produced by the C++ runtime are interchangeable.

---

## 2. The `Item` tagged-value representation

<img alt="Item storage-class taxonomy" src="diagram/d03_item_taxonomy.svg" width="720">

`Item` is a 64-bit union. The **high byte `[63:56]` is the `TypeId` tag** (`_type_id:8`, `lambda.hpp:91`); the low 56 bits are either an inline value or a tagged pointer. The C++ struct overlays several bit-field views on the same word: a signed `int_val:56` for inline integers (`lambda.hpp:91`), a `bool_val:8` (`:95`), the tagged-pointer views `int64_ptr`/`double_ptr`/`decimal_ptr`/`string_ptr`/`symbol_ptr`/`datetime_ptr`/`binary_ptr`/`uint64_ptr` (`:100`–`138`), the `NUM_SIZED` layout (`num_value:32` in `[31:0]`, `num_type:8` in `[55:48]`, `:128`–`134`), and the direct container-pointer views (`container`/`range`/`array`/`array_num`/`map`/`vmap`/`element`/`object`/`type`/`function`/`path`, `:143`–`156`).

`Item::type_id()` (`lambda.hpp:159`) is the universal dispatch:

1. if the high byte `_type_id` is non-zero, return it — the value is a packed scalar or a tagged pointer;
2. else if the word is non-zero, it is a **direct container pointer**, so dereference it and read the `TypeId` stored at offset 0 (every `Container` begins with a `TypeId` field, `lambda.h:580`);
3. else the word is fully zero, which reads as `LMD_TYPE_NULL`.

`get_type_id(Item)` is the thin free-function wrapper (`lambda.hpp:293`); the C-API entry `item_type_id` simply forwards to it (`lambda-eval.cpp:2074`). This single rule — *high byte zero ⇒ container* — is why the enum keeps container TypeIds at the high end and scalars at the low end.

### The `TypeId` enum

`enum EnumTypeId` defines the scalar and container tags. Notable members: `LMD_TYPE_UNDEFINED` is distinct from `LMD_TYPE_NULL` for JS interop; `LMD_TYPE_NUM_SIZED` packs sub-word numerics; `LMD_TYPE_INT` has a 56-bit physical payload but a semantic range capped to the ±(2⁵³−1) float64 safe-integer band; `LMD_TYPE_DECIMAL` also carries arbitrary-precision `integer` through `DECIMAL_BIGINT`; and `LMD_TYPE_VMAP` reports itself as `"map"`. The `NUM_SIZED` sub-types occupy bits `[55:48]` of the word.

### Three storage classes

- **Packed immediate** — the value lives entirely in the word; the high-byte tag is non-zero. `NULL`/`UNDEFINED`, `BOOL`, safe-band `INT`, and compact `NUM_SIZED` use this form. `i2it` enforces the semantic safe band even though the payload has 56 physical bits. These values never touch the heap; `get_int56()` and the `get_num_sized_*` accessors recover their payloads.
- **Tagged pointer** — the high byte is the tag, the low 56 bits are a heap, owned scalar-tail, or number-side-stack pointer: `INT64`, `UINT64`, `FLOAT`, `DECIMAL`, `DTIME`, `STRING`, `SYMBOL`, `BINARY`, `PATH`, `ERROR`. Boxing macros `l2it`/`u2it`/`d2it`/`c2it`/`k2it`/`s2it`/`y2it`/`x2it` OR the tag onto the pointer; each maps a NULL pointer to `ITEM_NULL`. The pointer is recovered by masking off the tag byte.
- **Direct container pointer** — the high byte is **zero** and the whole word *is* the pointer: `RANGE`, `ARRAY_NUM`, `ARRAY`, `MAP`, `VMAP`, `ELEMENT`, `OBJECT`, `TYPE`, `FUNC`. No masking; `it2map`/`it2arr`/etc. are bare casts (C macros `lambda.h:953`; C++ `lambda.hpp:436`), and the tag is read by dereferencing the object's first byte.

This is a **high-byte tag scheme, not NaN-boxing**. It works because user-space pointers fit in 48 bits, leaving the top byte free — `typemap_ptr_is_plausible` checks `<= 0x0000FFFFFFFFFFFF` (`lambda-data.hpp:320`). Keeping container pointers tag-free means a container `Item` is pointer-identical to its object, so the GC can scan it directly and pass-through is free.

---

## 3. Boxing & unboxing

Boxing is the act of turning a native C value (an `int64_t`, a `double`, a `String*`) into a tagged `Item`; unboxing is the reverse. The rules:

- **Inline scalars are never heap-allocated.** `bool` → `b2it` (`lambda.h:874`); `int` → `i2it` with a range check that degrades overflow to `ITEM_ERROR` (`:879`); sized numerics → the `*_to_item` `NUM_SIZED` packers (`:224`).
- **Numeric temporaries (`int64`, `uint64`, and out-of-band double) use the raw number execution side-stack.** `box_int64_value`/`box_uint64_value` and the rare `push_d` residue reserve activation-owned words. Generated Item returns copy into caller-donated homes; retaining containers/environments copy into storage-owned tails. `DateTime` is deliberately excluded: `push_k` allocates dynamic values in GC storage, while `MarkBuilder` owns static parser values in the `Input` arena. [LR_08](LR_08_Memory_and_GC.md) owns the lifetime rules and the counted ownerless-persistence fallback.
- **Strings, symbols and decimals** are heap- or pool-allocated and GC-managed; boxing is a pure tag-OR (`s2it`/`y2it`/`c2it`), and a NULL pointer always boxes to `ITEM_NULL`.
- **Containers never box or unbox** — the pointer *is* the Item (`p2it`, `it2map`, …); `type_id()` recovers the tag by dereferencing.
- **Float double-box guard.** `push_d_safe` recognizes canonical self-tagged or pointer-backed float Items before falling back to `push_d`. Full-width integer boxing uses the explicit `box_int64_value`/`box_uint64_value` paths; datetime uses `push_k`. Generated lowering must track these representations rather than guessing from raw high bytes.

The JIT-facing scalar unbox entry points are `it2d` (`lambda-data.cpp:309`), `it2b` (JS-style truthiness, `:335`), `it2i` (`:358`), `it2l` (`:383`), and `it2s` (`:399`). Container element reads box at the boundary: `array_num_get` returns a fresh `push_l`/`push_d` per the array's `elem_type` (`lambda-data-runtime.cpp:198`,`373`), and map/object/element field reads box the stored native field on the way out (`:1176`). The coercion helpers `ensure_typed_array`/`ensure_sized_array` unbox each Item into a compact buffer (`:1934`). Error Items propagate *without* unboxing through the `GUARD_ERROR*` macros (`lambda.hpp:304`).

---

## 4. The `Container` struct family

<img alt="Container struct family" src="diagram/d03_container_family.svg" width="720">

All container types extend `struct Container` (`lambda.h:580`; C++ derivations in `lambda.hpp`). The shared header is: `TypeId type_id` at byte 0 (the dereference target for `type_id()`), a lifecycle-flags union (`is_content`/`is_spreadable`/`is_heap`/`is_data_migrated`/`is_static`, `:586`), a second `array_flags` union (`is_ndim`/`is_view`/`is_pinned`/`is_mutable_view`, `:595`), and a `uint8_t map_kind` byte that is **reused as `elem_type` for `ArrayNum`** (`ArrayNum::get_elem_type()`, `lambda.hpp:366`).

The concrete containers:

- **Range** (`lambda.h:609` / `lambda.hpp:339`) — `start`, `end`, `length`, all `int64_t`, inclusive bounds.
- **List / Array** (`typedef List Array`, `lambda.h:505`; struct `:619` / `lambda.hpp:345`) — `Item* items` with `length`, `extra`, `capacity`. `extra` counts items appended past the original `length` by the growable-array builtins (`push`/`splice`, [LR_12](LR_12_Procedural_Runtime.md)).
- **ArrayNum** (the unified numeric array; aliases `ArrayInt`/`ArrayInt64`/`ArrayFloat`, `lambda.h:507`; struct `:630` / `lambda.hpp:354`) — a union `{int64_t* items; double* float_items; void* data}` plus `length`/`extra`/`capacity`, with the element type stored in the `elem_type` byte and a per-element size table `ELEM_TYPE_SIZE[16]` (`lambda.h:175`). For n-dimensional and view arrays, `extra` holds an `ArrayNumShape*` side table carrying `ndim`, contiguity flags, element `offset`, semantic `base`, explicit `backing_kind`, optional `ByteBufferHandle*`/`ByteStorage*` backing, cached handle generation, and the `shape[ndim]`/`strides[ndim]` arrays. The tag distinguishes GC-owned/GC-view storage, stable borrowed external storage, retained storage, and replaceable buffer handles; code must never infer ownership from the pointer shape of `base`. The numeric/vector operations over this type are owned by [LR_05](LR_05_Strings_and_Vectors.md).
- **Map** (`lambda.h:659` / `lambda.hpp:370`) — `void* type` (→ `TypeMap*`), `void* data` (a packed struct of field values), and `int data_cap`. For runtime JS Maps, `type` is a valid shape before publication and `data` is described by that shape; engine-native JS payloads use typed trailing carriers and host-native values remain VMaps. This is the **D3.4.1/D3.4.5** layout invariant.
- **SparseArrayMap** (`lambda.h:670` / `lambda.hpp:380`) — a `Map base` (which must be first, because `arr->extra` is cast to `Map*`) plus a `hashmap* sparse_indices` and a `sparse_version`.
- **Object** (`lambda.h:676` / `lambda.hpp:417`) — laid out identically to Map (`type` → `TypeObject*`, `data`, `data_cap`) so that field-access codegen is shared, plus a type name and methods at the `TypeObject` level.
- **VMap** (`lambda.hpp:410`) — a `void* data` backing store behind a `VMapVtable*` (get/set/count/keys/key_at/value_at/destroy, `:400`); it reports the type name `"map"` to scripts (`get_type_name`, `lambda-data.cpp:114`), decoupling map *semantics* (hashmap, treemap) from the language surface (`vmap.cpp`).
- **Element** (`lambda.h:687` / `lambda.hpp:385`) — the **dual list+map node**. In C++ it is `struct Element : List`, so it inherits `items`/`length`/`extra`/`capacity` (its *child content list*) and adds `void* type` (→ `TypeElmt*`), `void* data`, and `data_cap` (its *attribute map*). One struct is therefore simultaneously an indexed list of children and a keyed map of attributes — exactly matching XML/HTML/Mark semantics. The element case in `item_deep_equal` (`lambda-data.cpp:1395`) compares tag, then attribute bytes, then children; the unified for-loop iterator yields attributes followed by children (`lambda-data-runtime.cpp:1759`).

### Map shape: `TypeMap` & `ShapeEntry`

A map's fields are described by its `TypeMap`, not stored inline as key/value pairs. `ShapeEntry` (`lambda-data.hpp:227`) carries `StrView* name`, `Type* type`, `int64_t byte_offset`, a `next` pointer, a namespace `Target* ns`, an optional `default_value`, a `name_id`, and `flags`. Fields form a **singly-linked shape chain** off `TypeMap::shape`/`last`; each field's value lives in the Map's packed `data` buffer at `byte_offset`, and the field's `type` says how to read those bytes. JS property attributes ride in `flags` via an inverse-bit encoding (`JSPD_*`, `lambda-data.hpp:205`), and `JSPD_IS_ACCESSOR` swaps the data slot for a `JsAccessorPair*` (`:220`).

`TypeMap` (`lambda-data.hpp:246`) holds `length`, `byte_size` (the packed-struct size), the shape chain, and — for fast lookup — an inline open-addressing hash table `field_index[TYPEMAP_HASH_CAPACITY]` with an optional pool-owned `field_index_dynamic` for large maps (`typemap_hash_lookup*`, `:457`). Runtime JavaScript shapes additionally carry one immutable `const JsClassMeta*` selected before publication and preserved by transitions, as ruled by **D3.4.7**; the metadata has no `Item`, realm, GC, mutable-cache, or context-owned fields. The **shape chain remains authoritative** (last-writer-wins) whenever the hash table is unpopulated or saturated. `TypeElmt : TypeMap` (`:482`) adds the element's local `name`, `content_length`, and namespace; `TypeObject : TypeMap` (`:501`) adds a `type_name`, a `base` for inheritance, a method list, and constraint hooks.

---

## 5. Static `Type*` vs runtime `TypeId`

The compile-time type of an expression is a `Type*`. The base `struct Type` (`lambda.h:495`) is `{ TypeId type_id; uint8_t kind:4; is_literal:1; is_const:1 }`. Crucially, the `type_id` field **reuses the same `EnumTypeId` values** as runtime Items, so a static type and a runtime value agree on their tag; `kind` is a `TypeKind` (`lambda.hpp:14`: SIMPLE / UNARY / BINARY / PATTERN / CONSTRAINED) that sub-classifies the several structs sharing one `type_id` (notably the `LMD_TYPE_TYPE` variants).

The `Type*` family (all in `lambda-data.hpp`):

- **Literal/const scalars** — `TypeConst` (+`const_index`) specialized as `TypeFloat`, `TypeInt64`, `TypeUint64`, `TypeNumSized` (`num_type` + `raw_bits`), `TypeDateTime`, `TypeDecimal`, `TypeString`/`TypeSymbol` (`:151`–`192`).
- **Containers** — `TypeArray`/`TypeList` (`nested`, `length`, `type_index`, `:194`), and the `TypeMap`/`TypeElmt`/`TypeObject` shapes above.
- **Type expressions** — `TypeBinary` (union / intersect / exclude via `left`/`right`/`op`, `:571`), `TypeUnary` (occurrence `?+*{n}` via `operand`/`op`/`min_count`/`max_count`, `:578`), `TypeConstrained` (`base where(...)` with a compiled `constraint_fn`, `:590`), `TypePattern` (a compiled RE2 regex, `:630`).
- **Functions** — `TypeFunc` (`param`/`returned`/`error_type` plus the `can_raise`/`is_proc`/`is_variadic` flags, `:604`), `TypeParam` (`:597`), `TypeSysFunc`.
- **Meta** — `TypeType` (`:622`) wraps a `Type*`: the type of a type value (`LMD_TYPE_TYPE`).

Every primitive has a singleton `TYPE_*` object and a `LIT_TYPE_*` reference, declared at `lambda-data.hpp:649`–`737` and wired up by `init_typetype()` (`lambda-data.cpp:195`). `base_type(TypeId)`/`const_type(idx)` (`lambda.h:1616`) and `fn_type(Item)` (`lambda-eval.cpp:1987`) bridge a runtime value back to its static `Type*`. The build-time inference that *produces* these `Type*` objects is owned by [LR_02 — Parsing & AST Construction](LR_02_Parsing_AST.md).

A **second, parallel type vocabulary** (a `TypeSchema`/`SchemaTypeId` "unified schema" model in a former `schema_ast.hpp`) once shadowed this one for the schema validator, but it was unreachable dead code and has been removed; the validator now uses the runtime `Type*` family directly ([LR_13 — Schema Validator](LR_13_Schema_Validator.md)). The `Type*` family described here is the runtime's single type vocabulary.

---

## 6. Design decisions & rationale

- **High-byte tagging over NaN-boxing.** Putting the `TypeId` in the top byte and the value/pointer in the low 56 bits keeps container Items pointer-identical (free pass-through, directly GC-scannable) and lets one `type_id()` rule cover scalars, tagged pointers, and containers. The cost is a hard reliance on 48-bit pointers and on container TypeIds sorting above `LMD_TYPE_RANGE`.
- **Safe-band inline integers.** Plain `int` avoids allocation inside ±(2⁵³−1). Arithmetic overflow promotes to float, not `int64`; boundary results may round. `int64` is an explicit sized type with its own tag and home.
- **Frame-scoped numeric homes.** Out-of-band double and full-width signed/unsigned integer temporaries use a raw bump side-stack. Caller homes and destination-owned tails handle escapes. Datetime is never activation-backed: dynamic values are GC-owned and static Mark values are Input-arena-owned ([LR_08](LR_08_Memory_and_GC.md)).
- **Element = List + Map in one struct.** Markup nodes act as both ordered children and keyed attributes without a wrapper, matching XML/HTML/Mark directly.
- **Shape chain + inline hash table.** The chain is the authoritative ordered field list (cheap to extend, last-writer-wins); the fixed hash table accelerates the overwhelmingly-common small-map lookup, with a pool-owned dynamic table for large maps. Interned names give pointer-equality fast paths.
- **VMap behind a vtable.** Alternate map representations are hidden from the language, which always sees `"map"`.
- **Dual C/C++ headers.** `lambda.h` is deliberately slim and C-clean so MIR can compile it; the richer machinery lives in `lambda.hpp`/`lambda-data.hpp`, with the container ABI kept identical between the two (explicit comments at `lambda.h:657`).

---

## Known Issues & Future Improvements

Moved to the central ledger: **[Lambda Core Runtime — Central Issue Ledger](../../../vibe/Lambda_Issue_Ledger.md)**, entries **LR03-1 – LR03-7** (open/partial) and **LR03-R1 – LR03-R2** (resolved, Appendix A).

The ledger carries the verification status of each entry (OPEN / PARTIAL / RESOLVED) against the current source, re-resolved `file:line` anchors, and the cross-cutting clusters that group issues shared with other `LR_*` areas.

---
## Appendix A — Source map

| File | Responsibility (this doc) |
|---|---|
| `lambda/lambda.h` | C-mode `Item` typedef, `EnumTypeId`/`EnumNumSizedType`, C container structs, boxing/unboxing macros (`i2it`/`l2it`/`d2it`/`it2map`…), C-API surface. |
| `lambda/lambda.hpp` | The real bit-field `struct Item`, `Item::type_id()`, C++ container structs, unbox accessors, `GUARD_ERROR*`. |
| `lambda/lambda-data.hpp` | `Type*` family, `ShapeEntry`/`TypeMap`/`TypeElmt`/`TypeObject`, `JsAccessorPair`/JSPD flags, `TypeInfo`, type singleton externs. |
| `lambda/lambda-data.cpp` | Scalar unbox (`it2d`/`it2b`/`it2i`/`it2l`/`it2s`), type singletons + `init_typetype()`, `item_deep_equal`, push/splice. |
| `lambda/lambda-data-runtime.cpp` | Container element reads with boundary boxing (`array_num_get`, `map_get`, `elmt_get`), for-loop iterators, `ensure_typed_array`. |
| `lambda/lambda-mem.cpp`, `lambda/lambda-data.cpp`, `lambda/mark_builder.cpp` | Number homes for full-width integers/out-of-band doubles, caller-home adoption and persistent rehoming; dynamic-GC and static-Input-arena datetime construction. |
| `lambda/vmap.cpp` | `VMap` vtable-backed virtual map. |

## Appendix B — Related documents

- [LR_02 — Parsing & AST Construction](LR_02_Parsing_AST.md) — build-time inference that produces the `Type*` objects.
- [LR_04 — Numbers, Decimal & DateTime](LR_04_Numbers_Decimal_DateTime.md) — the numeric tower and operations over `INT`/`INT64`/`FLOAT`/`DECIMAL`/`DTIME`.
- [LR_05 — Strings, Symbols & Vectors](LR_05_Strings_and_Vectors.md) — `String`/`Symbol` and the `ArrayNum` vector machinery.
- [LR_06 — The C Transpiler](LR_06_C_Transpiler.md) / [LR_07 — The MIR Direct Transpiler & JIT](LR_07_MIR_Transpiler_JIT.md) — where boxing/unboxing is emitted.
- [LR_08 — Memory Management & Garbage Collection](LR_08_Memory_and_GC.md) — the heap, nurseries, name and shape pools backing these values.
- [LR_11 — Mark Data API](LR_11_Mark_Data_API.md) — constructing and reading these container values programmatically.
