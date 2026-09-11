# Lambda Runtime Structs at the MIR Boundary

> **Status: IMPLEMENTATION CENSUS — 2026-09-11** (re-surveyed against
> `fa77dbdcc`; previous census 2026-09-09). This document records the current
> C/C++ layouts and pointer interfaces consumed by Lambda MIR Direct. It
> introduces no new semantic or ABI ruling.
>
> **Formal linkage:** `D2.1.1` (one-word `Item`), `D2.1.3` (tagged leaf
> pointers), `D2.4.1` (semantic representation discipline), `D2.6.1`
> (array carriers), `D2.6.6v2` (container hierarchy), `D3.4.1` and `D3.4.6`
> (packed shapes and lane descriptors), `D5.2.1v3` (companion returns),
> `D5.4.1`–`D5.4.4` (context ownership and capsules), `D6.2.1` (function
> values), and `D7.4.1v2`–`D7.4.3` (native/guest boundaries).
>
> **Scope:** generated Lambda MIR, its C/C++ runtime helpers, the GC layout
> bridge, and the context-owned module state reached by generated code.

## 1. Boundary model

MIR does not pass C++ aggregates by value. The generated code operates on three
physical classes:

| MIR carrier | Runtime meaning |
|---|---|
| `MIR_T_P` | pointer to a runtime struct or opaque runtime payload |
| `MIR_T_I64` | boxed `Item`, Lambda integer lane, machine integer, or raw 64-bit payload; the surrounding `MirValue`/`ValueRep` says which one |
| `MIR_T_D` | native binary64 floating-point lane |

Under `D2.4.1`, MIR register type alone is not a semantic type. In particular,
boxed `Item`, an `int` lane, Lambda `int64`, and machine quantities can all use
`MIR_T_I64`.

The current entry convention is:

```text
main(Context* runtime) -> Item

boxed entry:
f_b(Context* runtime, [closure env or self], Item p0, Item p1, ...) -> Item

raw/native entry:
f(Context* runtime, [closure env or self], native parameters, ...)
    -> native result, or [Item, uint64_t] for a companion result
```

`Item` is represented as an `I64` MIR result/argument, not as a MIR struct
type. Shape-2 generated returns use the second MIR result for the raw scalar
payload. C-reachable wrappers put that payload in
`Context::mir_companion_slot`, as required by `D5.2.1v3`.

The generated entry implementations are emitted by
[`transpile-mir.cpp`](../lambda/runtime/transpile-mir.cpp#L31779); common ABI,
frame, rooting and companion-lane emission lives in
[`mir_emitter_shared.hpp`](../lambda/runtime/mir_emitter_shared.hpp#L4394).

## 2. Value and execution-state structs

### 2.1 `Item`

`Item` is Lambda's universal value currency (`D2.1.1`). Its representation is
one 64-bit word:

- C consumers see `typedef uint64_t Item`.
- C++ consumers see a union wrapper with tagged scalar views and direct pointer
  views for containers.
- Inline immediates carry a high-byte type tag.
- String, symbol, binary, and other scalar leaves use tagged pointers.
- Containers are raw header pointers; their `Container::type_id` is read from
  the pointed-to object.

The C++ definition and decoder are in
[`lambda.hpp`](../lambda/lambda.hpp#L48). The C-compatible declarations,
boxing helpers, tags, and static assertions are in
[`lambda.h`](../lambda/lambda.h#L867).

Generated code must use the runtime's boxing/unboxing conventions. A raw
comparison of two `Item` words is not general semantic equality
(`D2.1.2`).

### 2.2 `Context`

Every generated entry receives a hidden leading `Context* runtime`. The
JIT-visible prefix is defined in
[`lambda.h`](../lambda/lambda.h#L2302) and contains:

- pool, arena, constants, type-list, current-directory, and allocation hooks;
- stack-limit and UI-mode state;
- precise side-root stack base/top/limits;
- non-GC number-stack base/top/limits;
- `mir_return_lane`;
- `mir_bitcast_scratch` for double/bit reinterpretation;
- `mir_companion_slot` for the v3 second return lane;
- `mir_var_homes[LAMBDA_MAX_FUNCTION_ARGS]` for the CW33 direct `var`-parameter
  transport.

Both the Lambda and the JS emitter bake `Context` field offsets directly; the
complete inventory is in §7.1. Beyond the transport cells, generated code also
loads `Context::pool` (string/number construction) and `Context::run_main`.

The scratch, companion, and `var`-home cells are transport locations, not GC
roots. The companion cell holds raw scalar bits only, and is live only between
a shape-2 return and its resolution (`D5.2.1v3`, `D5.2.2v3`).

### 2.3 `EvalContext`, `Heap`, and the capsule directory

`EvalContext` extends `Context` and is the canonical long-lived isolate owner
(`D5.4.1`–`D5.4.2`). It adds the runtime heap, AST/name pools, decimal context,
validator, result and error state, debug-info/stack-trace state, scheduler,
current vargs, the edit-session editor, the `Runtime` back-pointer, the
template registry, the module-state table (`active_module_state`,
`module_states`, `module_state_capacity`), `execution_depth`, and — at the
tail — `capsule_directory`. Its definition is in
[`lambda-data.hpp`](../lambda/lambda-data.hpp#L107).

`ContextCapsuleDirectory` is the one directory for context-owned subsystem
state (`D5.4.2`–`D5.4.4`; JSCU15/JSCU27). Reading a capsule is one indexed
load into `ContextCapsuleSlot`; each subsystem owns its own frozen
`ContextCapsuleOps` record, and lifecycle (`release_heap` at heap replacement,
`destroy` at context teardown) is a table walk rather than a hand-maintained
fan-out. `ContextCapsuleExtension` lets optional module families register state
without reserving a permanent slot. The DOM/web capsules are *peers* of the JS
capsule, not children of it, because the DOM core is realm-neutral and is
driven by Lambda as well as by JS (JSCU17). The ids, slot record, and ops
contract are in
[`context_capsule.h`](../lambda/runtime/context_capsule.h#L33).

There is therefore **no** direct `EvalContext::js_state` field: the JS realm
capsule is reached as `CONTEXT_CAPSULE_JS_RUNTIME` (see §9.8), and generated
JS MIR bakes that indexed path (§7.1).

Generated code normally treats the pointer as `Context*`, but the inline
allocation path uses the established layout relationship:

```text
Context* runtime -> EvalContext::heap -> Heap::gc
```

The `Heap` definition is in
[`transpiler.hpp`](../lambda/runtime/transpiler.hpp#L39). The offsets are
asserted in [`transpile-mir.cpp`](../lambda/runtime/transpile-mir.cpp#L70)
so an `EvalContext` prefix change cannot silently invalidate inline allocation.
`capsule_directory` and `execution_depth` are kept at the tail precisely so the
established module-state offsets do not move (`D8.1.3v10`).

## 3. Container hierarchy

The physical hierarchy is one chain:

```text
Container
  └── Map
        ├── List / Array
        │     └── Element / Object
        ├── ArrayNum
        └── SparseArrayMap
```

This is the `D2.6.6v2` layout. The attribute face is shared by every member of
the chain, and the content face is shared by `List`, `ArrayNum`, and `Element`.

### 3.1 `Container`

`Container` is the eight-byte common header:

| Offset | Field |
|---:|---|
| 0 | `type_id` |
| 1 | lifecycle `flags` |
| 2 | `array_flags` |
| 3 | `map_kind` |
| 4 | `cow_state` |
| 5–6 | constructor-reservation mask (`ctor_reserved_mask_lo`/`_hi`) |
| 7 | `reserved_state` |

`array_flags` bits 0–4 are `is_ndim`, `is_view`, `is_pinned`,
`is_mutable_view`, `is_native_lane_array`; **bits 5–7 are not free** — they are
`JsElementsKind`, addressed through `JS_ELEMENTS_STATE_MASK` (`0xe0`).
`reserved_state` is the byte with room to spare.

The shared C/C++ definition and its static assertions are in
[`lambda.h`](../lambda/lambda.h#L964). The derived C++ container definitions
are in [`lambda.hpp`](../lambda/lambda.hpp#L382).

### 3.2 `Map`

`Map` extends `Container` with the attribute face:

```text
void* type       // TypeMap*/TypeElmt*/TypeObject* shape descriptor
void* data       // packed attribute bytes
int   data_cap   // attribute-buffer capacity
```

MIR direct field access loads `map->data`, then adds the field's
`ShapeEntry::byte_offset`. The C++ definition is in
[`lambda.hpp`](../lambda/lambda.hpp#L395); the C mirror is in
[`lambda.h`](../lambda/lambda.h#L1210).

### 3.3 `List` / `Array`

`List` extends `Map` with:

```text
Item*          items
int64_t        length
int64_t        extra
int64_t        capacity
ArrayRepCert*  rep_cert   // full homogeneous-array representation proof
```

`Array` is the compatibility alias for `List`. The `extra` region owns
container-resident wide scalar payloads where required by `D2.6.4`.
`rep_cert` is the non-GC representation certificate described in §3.7; MIR
loads it at a baked offset.

### 3.4 `ArrayNum`

`ArrayNum` extends `Map` with a union buffer pointer:

```text
int64_t*       items       // integer lanes
double*        float_items // binary64 lane
void*          data        // compact numeric lanes
int64_t        length
int64_t        extra
int64_t        capacity
ArrayRepCert*  rep_cert
```

Its `map_kind` byte carries `ArrayNumElemType`; its `array_flags` byte carries
N-D/view/native-lane state. `ArrayNum` shares the content offsets *and the
`rep_cert` tail offset* with `List` and `Element`, which is asserted by the MIR
lowerer at
[`transpile-mir.cpp`](../lambda/runtime/transpile-mir.cpp#L87).

`ArrayNumShape` is the side table referenced by `extra` for N-D arrays and
views. Its layout is declared in
[`lambda.h`](../lambda/lambda.h#L1312).

### 3.5 `Element` / `Object`

`Element` adds the content face to the inherited `Map` attribute face but adds
no new fields beyond `List` — `sizeof(Element) == sizeof(List)` is asserted.
`Object` is a typedef of `Element` ([`lambda.h`](../lambda/lambda.h#L848));
there is no separate object container TypeId. Nominal identity is carried by
the type descriptor (`TypeMap::nominal`) and cached in the container header's
`nominal_reserved` bit (`D2.6.6v2`).

### 3.6 Other container carriers

| Struct | Interface |
|---|---|
| `Range` | `Container` header plus `start`, `end`, `length`, and `is_char` |
| `SparseArrayMap` | `Map` plus `sparse_indices` hashmap and `sparse_version` mutation counter |
| `VMap` | `Container` plus opaque backing `data` and `VMapVtable` |
| `VArray` | Virtual indexed sequence with `VArrayVtable` |
| `Velmt` | Virtual element with `VelmtVtable`, attribute callbacks, and child callbacks |
| `VirtualContainer` | Carrier-neutral virtual-container prefix (`data`, `vtable`, `host_type`, `host_data`, `expando`) used by GC/Jube layout checks |

The materialized and virtual definitions, vtables, and offset assertions are
in [`lambda.hpp`](../lambda/lambda.hpp#L773).

`VMap` is the representation used when a native structure must cross into the
Lambda value domain as a projection (`D7.4.1v2`). Raw native pointers are not
script-visible. Guest modules additionally cross through opaque handles and
versioned API tables; they do not receive `Runtime*`, `EvalContext*`,
`Input*`, `MIR_context_t`, or `AstNode*` (`D7.4.3`).

### 3.7 `ArrayRepCert` and `LaneStorageDesc`

These two records are the physical-representation authority; both are read by
generated MIR at baked offsets, so they belong in this census even though
neither is a value carrier.

`LaneStorageDesc` is the resolved physical decision for one slot
(`D3.4.6`): `semantic_contract`, `base_contract`, `kind` (`LaneStorageKind`),
`nullable`, `byte_size`, `value_domain` (the TypeId that decodes the slot), and
`native`. It is stored on `ShapeEntry` and derived once, by one resolver, at
`shape_entry_set_type`. Readers, writers, the collector, and MIR direct access
all consult the record; none re-derives a lane from `type`.

`ArrayRepCert` is the homogeneous-array proof hanging off `List`/`ArrayNum`:
`array_contract`, `immediate_element`, `leaf_element`, an embedded
`leaf_lane` `LaneStorageDesc`, `rank`, `flags`
(`EXACT`/`REIFIED`/`ERROR_FREE`), `array_num_elem`, and `has_array_num_lane`.
Generated code loads `rank`, `leaf_element`, and `leaf_lane.kind` directly to
select an element access strategy.

Both are declared in [`lambda.h`](../lambda/lambda.h#L1098). They are non-GC
records: the collector never traces them, and they must not hold an owning
edge to a GC object.

## 4. Leaf payload and dynamic-field structs

### 4.1 Scalar payloads

| Struct | MIR/runtime role |
|---|---|
| `String` | `len` (u32) plus an `is_ascii`/`is_buffer`/`is_pooled` flags byte and trailing UTF-8 bytes; the 8-byte header and `chars` at byte 5 are both pinned by assertion, and `chars`/`len` are the only leaf fields MIR loads directly |
| `Symbol` | `len`, `Target* ns` namespace pointer, and trailing name bytes |
| `Binary` | immutable byte span: `len`, `is_ascii`/`flags`, a `ByteStorage*` plus `offset`, and optional `inline_bytes` for compiler constants |
| `Complex` | `type_id`, real component, imaginary component |
| `Decimal` | decimal mode and opaque `mpd_t*` payload |
| `DateTime` | library-owned datetime payload, usually reached through helpers |
| `Path` | opaque segmented path object reached through the path API |
| `LambdaError` | error object referenced by an error `Item`; not a normal generated value carrier |

The public leaf layouts are in [`lambda.h`](../lambda/lambda.h#L875) and
[`lambda-data.hpp`](../lambda/lambda-data.hpp#L164). `LambdaError` is defined in
[`lambda-error.h`](../lambda/runtime/lambda-error.h#L201).

### 4.2 `TypedItem`

`TypedItem` is the packed dynamic-field carrier: a `TypeId` followed by one
64-bit payload union. Its payload may be an immediate, scalar word, pointer,
container pointer, `Type*`, or `Function*`.

It is the `LANE_STORAGE_TYPED_ITEM` lane — used when a map field cannot be
represented by one statically selected native lane, including `any` and
certain union/occurrence contracts. The runtime definition is in
[`lambda-data.hpp`](../lambda/lambda-data.hpp#L179). The collector's C-only
byte-level mirror is `LambdaGcTypedItemLayout` in
[`lambda.h`](../lambda/lambda.h#L729).

## 5. Shape and module metadata

### 5.1 Type and shape records

`Type` is the common type-descriptor prefix: `type_id` plus kind/literal/const/
nominal flags. `TypeMap` adds map byte size, field count, shape chain, and
runtime shape metadata. `ShapeEntry` is:

```text
StrView*             name
Type*                type
int64_t              byte_offset
ShapeEntry*          next
Target*              ns              // namespace target, NULL when unqualified
AstNode*             default_value
uint32_t             name_hash       // FNV lookup hash; never an identity
NameId               name_id
uint8_t              key_kind        // NAME_KEY_STRING / _SYMBOL / _PRIVATE
uint8_t              flags           // JSPD_* descriptor attributes
NameEntry*           binding         // compiler-only edge; NULL at runtime
LaneStorageDesc      storage         // trailing, so the GC ABI view is untouched
```

The packed map bytes and direct MIR access must agree with the shape chain
(`D3.4.1`). The field's full `Type*` contract and its stored
`LaneStorageDesc` determine the physical lane (`D3.4.6`); MIR must not infer
this from a TypeId or MIR register class. `flags` is the JS property-descriptor
byte (`JSPD_NON_WRITABLE`, `JSPD_NON_ENUMERABLE`, `JSPD_NON_CONFIGURABLE`,
`JSPD_IS_ACCESSOR`, `JSPD_DELETED`), zero-defaulting to the JS-conformant data
property.

`TypeMap` carries, beyond the GC-visible prefix: the inline
`field_index[TYPEMAP_HASH_CAPACITY]` lookup table plus an optional
`field_index_dynamic`, the `slot_entries`/`slot_count` fixed-slot index, the
sharing flags `is_private_clone` / `is_shared_constructor_shape` /
`is_transition_shared_shape`, the `TypeMapTransition* transitions` chain, the
immutable `const JsClassMeta* js_meta` (§9.5), `has_array_index_shape`, a lazily
allocated `JsProtoEntryCache*`, and `TypeNominal* nominal` — the authoritative
nominal record that the container header bit only caches.

The C++ definitions are in
[`lambda-data.hpp`](../lambda/lambda-data.hpp#L309) and
[`lambda-data.hpp`](../lambda/lambda-data.hpp#L369). The GC bridge exposes only
the stable prefixes in `LambdaGcTypeMapLayout` and
`LambdaGcShapeEntryLayout` from [`lambda.h`](../lambda/lambda.h#L710).

`TypeInfo` is immutable type metadata used by a few generated retagging paths
for type pointers and byte sizes; MIR loads `TypeInfo::type` at a baked offset.
It is defined in [`lambda-data.hpp`](../lambda/lambda-data.hpp#L150).

### 5.2 Module state records

Generated MIR does not bake mutable module-variable *addresses* into code. It
bakes only the selector path — `EvalContext::module_states` plus
`LambdaModuleState::vars` / `::consts` — and the module id/slot constants, then
reaches storage through context-selected module state (`D5.4.3`):

| Struct | Purpose |
|---|---|
| `LambdaModuleState` | context-local `Item* vars` slab, `uint64_t* var_payloads` scalar slab, `NameId* property_keys`, `consts`, `type_list`, and `var_count`/`var_capacity`/`property_key_count`/`module_id`/`vars_registered` metadata |
| `LambdaModuleLayout` | immutable sealed MIR image metadata: module id, variable count, and property-key image |
| `PropertyKeySpec` | immutable property-key specification in the sealed image |
| `LambdaModuleVarRef` | compact `(module_id, slot)` reference for imports |

`var_capacity` may exceed `var_count` in the REPL, and JS direct `eval` may grow
`vars`; its MIR lowering therefore reloads the pointer from the active context
for every module-slot access.

`LambdaModuleState` is defined in
[`lambda-data.hpp`](../lambda/lambda-data.hpp#L87); the immutable image records
are in [`lambda.h`](../lambda/lambda.h#L2356).

## 6. Function and closure interface

`Function` is a GC-managed first-class value (`D6.2.1`). Its ABI-relevant
fields are:

```text
type_id            // byte 0, pinned
arity
closure_field_count // byte 2, pinned (GC ABI)
entry_abi          // FunctionEntryAbi, checked before ptr dispatch
flags              // read by generated MIR at a baked offsetof
fn_type            // byte 8, pinned
ptr
closure_env        // GC ABI
name
runtime_context
def / def_module / method   // trailing only
```

The `flags` word is a **direct** MIR read: `transpile-mir.cpp` loads
`offsetof(Function, flags)` to test the ABI/return-shape bits, so no field
before it may shift. The collector uses `closure_field_count` and
`closure_env` to trace captures. A closure environment stores captured values
as `Item`s (`D6.2.3`–`D6.2.4`). Dynamic calls enter through runtime dispatch
functions such as `fn_call*` / `fn_call_into`; the function pointer is not
itself a script-visible raw pointer.

The layout, its pin assertions, and the dynamic-call declarations are in
[`lambda.h`](../lambda/lambda.h#L1442).

## 7. GC layout mirrors

The C collector cannot include the complete C++ runtime headers. Therefore
`lambda.h` defines checked byte-level mirrors
([`lambda.h`](../lambda/lambda.h#L684)):

- `LambdaGcContainerLayout`;
- `LambdaGcMapLayout` and `LambdaGcListLayout`;
- `LambdaGcTypeMapLayout` and `LambdaGcShapeEntryLayout`;
- `LambdaGcTypedItemLayout` (packed);
- `LambdaGcArrayNumShapeLayout`;
- `LambdaGcVMapLayout` and `LambdaGcVMapVtableLayout`;
- `LambdaGcVirtualContainerLayout` and `LambdaGcVirtualVtableLayout`;
- `LambdaGcFunctionLayout`.

Every offset generated code or the collector may use is named once, in
`enum LambdaGcLayoutOffset`. These are not alternative runtime value types;
they are C collector views of the prefixes used by the C++ objects, and the
corresponding C++ definitions carry static assertions against them. Note that
`LambdaGcListLayout` stops at `capacity`: `rep_cert` is deliberately outside
the GC ABI view because the certificate is not traced.

This keeps GC tracing, packed-field access, and generated MIR on one layout
contract (`D3.4.1`, `D4.3`, `D5.3.4`).

### 7.1 Baked-offset inventory

This is the complete set of struct offsets the two emitters bake into
generated code. Anything not listed here is reached through a named C helper.
Adding a row is an ABI decision; the census exists so a field move has known
readers rather than guessed ones.

**Lambda MIR** ([`transpile-mir.cpp`](../lambda/runtime/transpile-mir.cpp#L84),
[`mir_emitter_shared.hpp`](../lambda/runtime/mir_emitter_shared.hpp#L1353)):

| Struct | Fields baked |
|---|---|
| `Context` | `pool`, `run_main`, `side_root_top`/`_limit`/`_commit_limit`, `side_number_top`/`_limit`/`_commit_limit`, `mir_return_lane`, `mir_bitcast_scratch`, `mir_companion_slot`, `mir_var_homes` |
| `EvalContext` / `Heap` | `heap`, `gc` (inline bump allocation), `module_states` |
| `Map` | `type`, `data`, `data_cap` |
| `List` / `Element` / `ArrayNum` | `items`, `length`, `extra`, `capacity`, `rep_cert` (one constant each — the D2.6.6v2 single-chain guarantee) |
| `ArrayRepCert` | `rank`, `leaf_element` |
| `LaneStorageDesc` | `kind` |
| `ShapeEntry` | `type` |
| `TypeInfo` | `type` |
| `String` | `chars`, `len` |
| `Function` | `flags` |
| `LambdaModuleState` | `vars`, `consts` |

**JS MIR** (`lambda/js/js_mir_*.cpp`):

| Struct | Fields baked |
|---|---|
| `Context` | `pool`, the four side-stack top/limit pairs, `mir_companion_slot` |
| `String` | `chars`, `len` |
| `EvalContext` → `ContextCapsuleDirectory` → `ContextCapsuleSlot` | `capsule_directory`, `slots`, `capsule` — the indexed path to `CONTEXT_CAPSULE_JS_RUNTIME` |
| `JsRuntimeState` → `JsExecutionState` | `execution`, `call_depth`, `call_stack_limit` |

The capsule/execution path is new since the previous census: the call-depth
guard (`jm_enter_source_invocation`,
[`js_mir_calls_boxing_types.cpp`](../lambda/js/js_mir_calls_boxing_types.cpp#L114))
increments and bounds-checks the JS call depth inline rather than through a
helper call, and raises `RangeError` on overflow. It is the only JS realm-state
offset generated code reads.

## 8. What generated MIR does not see

Generated MIR does not receive or persistently embed:

- C++ class methods or C++ object ownership semantics;
- a struct-valued MIR parameter or return;
- the native C stack as a GC root;
- context-dependent mutable module addresses;
- raw guest/native pointers as Lambda values;
- the retired C-text/C2MIR backend — `transpile.cpp` and `transpile-call.cpp`
  no longer exist in the tree.

The stable interface is the combination of `Context*`, one-word `Item`s,
runtime-owned pointers, the §7.1 checked struct offsets, and named C ABI
helper calls.

## 9. JavaScript runtime structs at the MIR boundary

This section is the JS-profile companion to the Lambda census above. "Faced by
MIR" has two meanings here:

1. a layout or raw slot that generated JS MIR emits directly; or
2. a JS record that a named C helper called by generated MIR receives, creates,
   traces, or resumes.

It does not include every compiler-only `JsMir*` planning record or every Node
request object. Those records die before generated code runs. The formal
constraints for this section are `D1.3` (one core runtime), `D2.6.9v3` (a JS
object is a nominal Lambda `Map`), `D3.4.7` (JS class metadata on `TypeMap`),
`D5.1.1v2`/`D5.1.3` (heap-owned suspension state), `D5.3.3` (precise roots),
`D6.2.2v2`/`D6.2.3v2` (call/construct capabilities and captures),
`D7.4.1v2` (native projections), and `D8.4.3v2` (explicit completion lanes).

### 9.1 Physical JS MIR ABI

The JS MIR module entry is emitted as:

```text
js_main(Context* ctx) -> I64          // returned I64 is an Item
```

The declaration is in
[`js_mir_module_batch_lowering.cpp`](../lambda/js/js_mir_module_batch_lowering.cpp#L2962).
Ordinary boxed JS functions use the same hidden `Context*` ownership and
`Item`/I64 value lane as the Lambda entry convention. Native-specialized
functions may use `MIR_T_D` for a binary64 parameter or result, but the wrapper
still enters through the JS runtime ABI.

Suspending MIR bodies have a distinct state-machine signature:

```text
state(Context* ctx, I64 gen_env, I64 gen_input, I64 gen_state) -> I64
```

`gen_env` is the raw JS closure environment pointer encoded in the I64 lane;
`gen_input` is the value sent to `next`/`await`; and `gen_state` is the program
counter or resume state. The compiler emits this in
[`js_mir_function_class_lowering.cpp`](../lambda/js/js_mir_function_class_lowering.cpp#L1111),
and `gen_env` gets a GC root slot and is registered as an owned environment.
The runtime wraps those words in generator or async carriers before they can
outlive the creating call.

Generated JS MIR bakes offsets only for the shared `Context` transport fields,
`String::chars`/`String::len`, and the capsule path to the JS call-depth guard
(§7.1). It does **not** bake a `JsFunction`, `JsPromise`, typed-array, or
exotic-carrier offset. JS object operations, calls, construction, suspension,
and typed-array access therefore remain named helper calls. The direct offset
sites are
[`js_mir_hashmap_scope_utils.cpp`](../lambda/js/js_mir_hashmap_scope_utils.cpp#L577),
[`js_mir_expression_lowering.cpp`](../lambda/js/js_mir_expression_lowering.cpp#L6106),
and
[`js_mir_calls_boxing_types.cpp`](../lambda/js/js_mir_calls_boxing_types.cpp#L114).

### 9.2 Shared Lambda records used by JS

These records are already detailed in Sections 2–7 and are not duplicated
here:

`Item`, `Context`, `EvalContext`, `ContextCapsuleDirectory`, `Heap`,
`Container`, `Map`, `List`/`Array`, `ArrayNum`, `Element`/`Object`, `Range`,
`SparseArrayMap`, `VMap`, `VArray`, `Velmt`, `VirtualContainer`, `String`,
`Symbol`, `Binary`, `Complex`, `Decimal`, `DateTime`, `Path`, `LambdaError`,
`TypedItem`, `Type`, `TypeMap`, `ShapeEntry`, `LaneStorageDesc`,
`ArrayRepCert`, `ArrayNumShape`, `Function`, `LambdaModuleState`,
`LambdaModuleLayout`, `PropertyKeySpec`, `LambdaModuleVarRef`, and the
`LambdaGc*` layout mirrors.

JS uses those records unchanged. The JS additions below either add a payload to
one of those records or hold JS realm/execution state behind a named helper.

### 9.3 JS callable records

#### `JsFunction` and `JsCallableCode`

`JsFunction` is the JS callable carrier occupying the `Item::function` pointer
slot with `type_id == LMD_TYPE_FUNC`. Its stable discrimination prefix is:

```text
offset 0: TypeId type_id
offset 4: uint32_t layout_magic == JS_FUNCTION_LAYOUT_MAGIC (0x4A53464E)
```

Those two offsets, plus "the prefix precedes every other field", are the
**only** contractual pins (JSCUO6/JSCUO9). The two historical pins — `func_ptr`
at 8 and `bound_this_store` at 48 — are gone: fields are now ordered by
alignment, which is what moved the record out of the 256 B GC size class.

The value record itself holds only mutable per-value state:

| Group | Fields |
|---|---|
| discrimination | `type_id`, `layout_magic` |
| executable identity | `env`, `env_size`, `invoke` (`JsCallEntry`), `construct` (`JsConstructEntry`) |
| observable function state | `prototype`, `name`, `properties_map`, `home_global` |
| shared definition facts | `JsCallableCode* code` |
| optional payload | `JsFunctionPayload* payload` |
| bookkeeping | `flags`, `pool_pointer_roots_registered` |

`JsCallableCode` (JSCU33(A)) is the split-out, **interned and shared**
definition-site record: `func_ptr`, `source_text`, `runtime_context`,
`param_count`, `catalog_id`, `module_state_id`, `formal_length`,
`intrinsic_class`, `typed_array_element_type_plus_one`,
`eval_initializer_context`, `body_kind` (`JsFunctionBodyKind`), plus refcount
and interning state. One record exists per definition; every closure and method
wrapper made from that definition shares it. Reads go through the null-safe
`js_fn_*` accessors (`js_fn_func_ptr`, `js_fn_param_count`, …), which fall back
to an all-zero `js_fn_code_absent` so unguarded call sites keep the shape they
had when these were inline fields. The realm-owned weak intern table is
`JsRuntimeState::callable_code_interned`.

`JsFunctionPayload` is one word on the value carrying every optional record:
`home_class` (an `Item`), `JsBoundData`, `JsClassData`, `JsWithData`,
`JsAstBody`, `JsNativeCode`, and `JsEvalOrigin`. Their purposes are the bound
target/receiver/arguments (`this_store` is the bound receiver's owned scalar
home), class constructor/prototype/superclass plus the custom-element name, a
captured `with` environment and depth, AST closure state, native-call target and
policy, and dynamic-function source origin respectively. `JsAstBody` is now
`{JsAstDefinition* definition, JsInterpEnv* env, Item lexical_this,
Item lexical_new_target}`; the immutable definition facts (`function`, `script`,
`code`, `has_direct_eval`, `uses_arguments`, `tail_reuse_safe`) live in the
`JsScript`-owned `JsAstDefinition`. Native target selection is carried by
`JsNativeTarget` and `JsNativeCallPolicy`.

The complete JS callable layout, the accessor set, and the JSCUO6 reader
inventory are in [`js_function.hpp`](../lambda/js/js_function.hpp#L173). The
runtime ABI entry points are
[`js_call_function_into`](../lambda/js/js_runtime.h#L401),
[`js_function_get_ptr`](../lambda/js/js_runtime.h#L499), and
[`js_function_get_arity`](../lambda/js/js_runtime.h#L502).

Generated MIR does not dereference a `JsFunction` field. It calls the named JS
dispatch helpers; C code validates the layout magic and dispatches through
`invoke`/`construct`. This is an important difference from the shared Lambda
`Function` path, where the MIR lowering loads `flags` at a baked offset (§6).

#### `JsAccessorCell` (alias `JsAccessorPair`)

**Changed since the previous census.** A stored accessor is property storage,
never a callable value, and the two are no longer tag-ambiguous:

```text
offset 0: uint8_t type_id = LMD_TYPE_UNDEFINED   // deliberately NOT LMD_TYPE_FUNC
offset 4: uint32_t layout_magic = JS_ACCESSOR_CELL_LAYOUT_MAGIC (0x4A534143)
offset 8/16: Item getter, Item setter          // ItemNull or LMD_TYPE_FUNC
```

The in-band tag is deliberately undefined so an unchecked generic `Item` path
cannot mistake a cell for a function, and the cell has its own GC tag,
`GC_TYPE_JS_ACCESSOR` (`0x102`), so the `Function` tracer never interprets
property storage as an executable value (JSCU33). The slot is selected when
`ShapeEntry::flags & JSPD_IS_ACCESSOR` is set. `JsAccessorPair` is a
compatibility spelling only — one cell allocation, no FUNC-layout carrier. The
definition is in [`lambda-data.hpp`](../lambda/lambda-data.hpp#L297); its trace
case is [`gc_heap.c`](../lambda/runtime/gc/gc_heap.c#L1709).

### 9.4 JS environments and captured MIR state

#### Raw compiled closure environment

**Changed since the previous census.** There is now one GC environment family,
`GC_TYPE_ENVIRONMENT` (`0x100`), with two payload descriptors selected by the
header flag `GC_FLAG_ENV_INTERP` (`0x20`)
([`gc_heap.h`](../lambda/runtime/gc/gc_heap.h#L51)). The tag is deliberately
outside the public TypeId/Item tag space because a closure environment is
addressed as raw `Item` slots.

`js_alloc_env(count)` ([`js_runtime.h`](../lambda/js/js_runtime.h#L351))
returns an `Item*`, but generated MIR carries it as an I64/raw pointer rather
than as an `Item`. The allocation has two equal regions:

```text
first half:  count Item slots traced by the GC
second half: count uint64_t scalar homes, not traced as Items
```

This is the environment used by compiled closures, module scope, native
closures, generators, and async bodies. `js_env_rehome_scalars`
([`js_runtime.h`](../lambda/js/js_runtime.h#L366)) moves any captured wide
number payload out of a transient invocation number stack before the
environment is retained. The allocator is `heap_calloc_closure_env`
([`lambda-mem.cpp`](../lambda/runtime/lambda-mem.cpp#L794)) — it requests
`size * 2` words — and the collector contract is the `GC_TYPE_ENVIRONMENT_`
case in [`gc_heap.c`](../lambda/runtime/gc/gc_heap.c#L1686), which derives the
Item count as `alloc_size / (2 * sizeof(uint64_t))`.

The environment is intentionally not a C++ struct with a visible header: its
length comes from the GC allocation header, and its first half is the exact
Item edge set. `JsWithData` stores one such environment plus a dynamic-scope
depth when a closure captures a `with` binding.

#### `JsInterpEnv`

`JsInterpEnv` is the interpreter payload shape of the same GC family, selected
by `GC_FLAG_ENV_INTERP` and used for AST-interpreted functions and direct
`eval`. It contains `outer`, `scope`, the `arguments_object`,
`private_home_class`, `private_bindings`, `eval_bindings` and `lexical_this`
Item words, `function_node`, `slot_count`, the `arguments_are_mapped` /
`has_lexical_this` flags, and a flexible `slots[]` tail. Its durable slots use
the same Item-plus-companion scalar convention as compiled environments: it is
allocated as `offsetof(JsInterpEnv, slots) + 2 * slot_count * sizeof(uint64_t)`
and tagged with `gc_environment_set_interpreter`. See
[`js_interp_env.h`](../lambda/js/js_interp_env.h#L10) and
[`js_interp.cpp`](../lambda/js/js_interp.cpp#L925).

The two environment forms must not be conflated: compiled MIR receives a raw
`Item*` environment word, while AST/eval helpers receive a `JsInterpEnv*`.

### 9.5 JS object metadata and exotic operation records

#### `JsClassMeta`, `JsPropertyOps`, and `JsPropertyOpResult`

`JsClassMeta` is the JS-specific metadata attached to a nominal JS
`TypeMap` (`TypeMap::js_meta`, null for foreign or `Input` shapes). It contains
a stable `JsClassId`, a `JsClassFamily` (ordinary, array, function, error,
proxy, typed array, arguments, string, iterator, collection, host), class
flags, a `JsPrototypePolicy`, and a pointer to immutable `JsPropertyOps`. The
operation table supplies eleven JS exotic hooks — get, set, define_own,
delete_property, has_property, get_own_property_descriptor, own_keys,
get_prototype_of, set_prototype_of, is_extensible, prevent_extensions — each
returning `JsPropertyOpResult` with an explicit disposition
(`FALLTHROUGH`/`COMPLETE`) and completion `Item`. The static tables are
`js_proxy_`, `js_typed_array_`, `js_iterator_`, `js_process_env_`,
`js_host_`, and `js_promise_property_ops`.

This is metadata and dispatch state, not a replacement for the Lambda `Map`
layout. The definitions and operation-table declarations are in
[`js_object_meta.h`](../lambda/js/js_object_meta.h#L40).

#### `JsProxyData` / `JsProxyMapCarrier`

Proxy objects use the ordinary JS-as-`Map` representation with a native
trailing carrier:

```text
JsProxyMapCarrier = Map base + JsProxyData payload
JsProxyData        = target, handler, private_slots,
                     callable, constructable, revoked
```

The three Item-valued slots are stored as `uint64_t` for C-header compatibility;
the two capability booleans are immutable snapshots of the target's call and
construct capabilities. The map is marked `MAP_KIND_PROXY`. MIR reaches this
payload through proxy/property helpers, never by an emitted payload offset.
See [`js_runtime.h`](../lambda/js/js_runtime.h#L1126) and
[`js_runtime.cpp`](../lambda/js/js_runtime.cpp#L100).

### 9.6 JS native payload carriers

The following are JS-specific carriers whose first field is a shared Lambda
`Map` or `VMap` prefix. They are the main object layouts that C helpers inspect
after MIR has passed an `Item`:

| Carrier / payload | Layout and purpose |
|---|---|
| `JsArrayBufferMapCarrier` / `JsArrayBuffer` | `Map` plus a `JsArrayBuffer*`, which is now exactly one `ByteBufferHandle`; the buffer identity is stable while the handle swaps storage on detach, resize, transfer, or copy-on-write. |
| `JsArrayBufferView` | **New (JSCU34):** the one authority for every ArrayBuffer view — `buffer`, `buffer_item`, `byte_offset`, `byte_length`, `length_tracking`. Detachment, `.buffer` identity, offset, explicit length, and length-tracking are one mechanism. |
| `JsTypedArrayMapCarrier` / `JsTypedArray` | `Map` plus a `JsArrayBufferView base` (with a legacy member-name overlay union), `element_type`, the `is_buffer` Node-Buffer marker, and an `ArrayNum* view` over the same non-moving storage. |
| `JsDataViewMapCarrier` / `JsDataView` | `Map` plus the same `JsArrayBufferView base` and overlay; DataView adds no stored state of its own. |
| `JsTypedArraySpec` | Immutable per-element-type descriptor (`byte_size`, `elem_type`, `name`, integer/atomic/bigint/signed flags, `bits`) shared by the compiler and the runtime. |
| `JsCollectionMap` / `JsCollectionData` / `JsCollectionOrderNode` | `Map` plus a `JsCollectionData*`; that record owns the native `HashMap*`, the Map/Set/WeakMap/WeakSet kind, and the `JsCollectionOrderNode` insertion-order list, while ordinary shape storage remains available for expandos. |
| `JsIteratorMapCarrier` / `JsIterData` | `Map` plus source `Item`, current index, and length snapshot; its map kind is `MAP_KIND_ITERATOR`. |
| `JsRegExpMapCarrier` / `JsRegexData` | `Map` plus compiled-regex payload and virtual-property override bits; its map kind is `MAP_KIND_REGEXP`. |
| `JsGeneratorMapCarrier` / `JsGeneratorStateRecord` | `Map` plus the suspended generator state, including the re-homed environment. |
| `JsAsyncFrameCarrier` / `JsAsyncContextStateRecord` | `Map` plus the suspended async activation, result promise, environment, resume state, and AST continuation fields. |

Typed-array layouts are declared in
[`js_typed_array.h`](../lambda/js/js_typed_array.h#L55); the typed-array map
carrier is shared with the MIR guards in
[`js_typed_array_carrier.hpp`](../lambda/js/js_typed_array_carrier.hpp#L7), and
the ArrayBuffer/DataView carriers are allocated in
[`js_typed_array.cpp`](../lambda/js/js_typed_array.cpp#L26). The proxy,
collection, RegExp, iterator, generator, and async carriers are in
[`js_runtime.cpp`](../lambda/js/js_runtime.cpp#L100),
[`js_runtime.cpp`](../lambda/js/js_runtime.cpp#L991),
[`js_runtime.cpp`](../lambda/js/js_runtime.cpp#L14575),
[`js_runtime.cpp`](../lambda/js/js_runtime.cpp#L28233), and
[`js_runtime.cpp`](../lambda/js/js_runtime.cpp#L30609).

`JsPromise` is the important non-`Map` object carrier: it extends `VMap` and
holds `state`, `result`, `result_scalar` (the companion scalar), `reactions`,
`reject_domain`, `expando`/`prototype_override`/`extensible` state, and
rejection bookkeeping (`rejection_handled`, `unhandled_check_scheduled`,
`unhandled_reported`, `unhandled_epoch`). Its context-level companion is
`JsPromiseRuntimeState`, which owns the unhandled-rejection job queue and its
storage Item, the domain Items and their `RootVector` domain stack, and the promise
counters. The definitions are in
[`js_runtime_state.hpp`](../lambda/js/js_runtime_state.hpp#L684).

### 9.7 Resumable execution records

#### `JsSuspendedActivation`

**New since the previous census.** Generator and async records now share one
base holding exactly the durable activation edges that outlive the creating
native frame (`D5.1.1v2`, `D6.2.2v2`; JSCU32):

```text
type_id = LMD_TYPE_MAP, runtime_context, state_fn, env, env_size, state,
ast_function, ast_arguments, ast_function_env, ast_body_env,
ast_replay_values, ast_replay_skip, ast_loop_continuations, ast_initialized
```

Generator yields and async awaits replay through the same `ast_replay_values`
Item ledger. It is declared in
[`js_runtime_state.hpp`](../lambda/js/js_runtime_state.hpp#L771).

#### `JsGeneratorStateRecord`

This record extends `JsSuspendedActivation` and is embedded in
`JsGeneratorMapCarrier`. Its own tail is `done`/`started`/`executing`/
`is_async`, `private_home_class`, `delegate`/`delegate_resume`/`delegate_idx`,
`ast_this`, and the replay-safety continuations: `ast_list_continuation`
(terminal-yield loops), `ast_array_binding_continuations` (a destructuring
target that suspends after `IteratorStep`), `ast_try_continuations` (a
catch/finally clause that suspends), and `ast_pending_resume_yield` /
`ast_pending_resume_input` (an injected throw/return held while a `finally`
yields). The generator object owns the state and its GC trace, rather than a
context-wide fixed index table. The record is declared in
[`js_runtime_state.hpp`](../lambda/js/js_runtime_state.hpp#L789), and its
carrier is initialized by the MIR-facing constructor in
[`js_runtime.cpp`](../lambda/js/js_runtime.cpp#L27463).

#### `JsAsyncContextStateRecord`

The analogous async activation record adds `promise`, `module_state_id`
(resumed MIR property names must use the module image that compiled the body),
`this_val`, `ast_list_continuation`, and `ast_try_continuations`. It is
embedded in `JsAsyncFrameCarrier`, tagged `MAP_KIND_ASYNC_FRAME`, and traced
through the frame Item retained by promise reactions. The record and carrier
are in [`js_runtime_state.hpp`](../lambda/js/js_runtime_state.hpp#L813) and
[`js_runtime.cpp`](../lambda/js/js_runtime.cpp#L30609).

The helper ABI exposes these as `js_generator_create_mir` and
`js_async_context_create_mir`; generated code does not retain a native stack
frame across `yield` or `await`. The declarations are in
[`js_runtime.h`](../lambda/js/js_runtime.h#L1008) and
[`js_runtime.h`](../lambda/js/js_runtime.h#L1090).

### 9.8 Realm and execution-state capsule

`JsRuntimeState` is the JS profile's context-owned mutable capsule. Generated
MIR receives `Context*`, not `JsRuntimeState*`; JS helpers recover the active
state from the `CONTEXT_CAPSULE_JS_RUNTIME` slot
(`js_runtime_state_for(EvalContext*)`) or from the derived TLS cache
`js_active_runtime_state`. The one exception is the inline call-depth guard,
which bakes that indexed path (§7.1).

The capsule owns the realm slots and global environment, intrinsics, module and
namespace state, promise and event-loop state, eval journals, the current call
activation, the function-wrapper and callable-code caches, the resource table,
and code-store ownership, plus the host-profile substates (readline, stream,
assert, net, TLS, clipboard, process, console, cluster, performance, Test262).
DOM/web state is *not* a child of this capsule: it lives in its own peer
capsules (JSCU17).

The JS-specific state records directly relevant to MIR/helper boundaries are:

`JsRootedState` (the one `RootVector` root-owner base every capsule state
derives from), `JsNamespaceState`, `JsRealmSlots`, `JsGlobalEnvironment`,
`JsRealmIntrinsicSlots`, `JsIntrinsicState`, `JsWithScopeState`,
`JsEventLoopQueueState`, `JsEventLoopTimerState`, `JsPromiseRuntimeState`,
`JsModuleRuntimeState`, `JsAsyncHooksState`, `JsAsyncLocalStorageState`,
`JsAsyncAwaitState`, `JsEvalState` (`JsEvalSourceState`, `JsEvalBridgeState`,
`JsEvalLocalState`), `JsExecutionState`, `JsCallActivation`, `JsCodeStore`,
`JsProcessState`, and `JsTest262AgentState`. The complete ownership record is in
[`js_runtime_state.hpp`](../lambda/js/js_runtime_state.hpp#L993).

Three records named in the previous census are **gone**:

- `JsRootRange` — every capsule now roots through `JsRootedState::roots`, a
  `RootVector` that also supports fixed contiguous spans, so there is one root
  carrier (`D5.3`).
- `JsIteratorState` — iterator/generator prototype roots moved into
  `JsIntrinsicState::prototype_roots`, indexed by `JsClassId`.
- `JsVmRuntimeState` — the VM namespace became a `JsModuleRuntimeSlot` and its
  source-identifier counter a `JsModuleRuntimeState` field.

`JsExecutionState` / `JsCallActivation` also replace the former fixed
`this`/`newTarget` state fields: a synchronous call has one ambient owner whose
Item homes are either the context-owned base `RootVector` or one exact native
`RootFrame`, so a nested call replaces one activation link instead of mutating
parallel globals (`D5.1.1v2`, `D6.2.2v2`; JSCU28). `JsExecutionState` also
carries the `call_depth` / `call_stack_limit` pair the JIT guard reads.

The common invariant is that Item-bearing state uses explicit `RootVector` or
carrier-specific GC trace hooks. POD counters, native handles, and cache
metadata are kept out of Item ranges. This is why MIR can call into the JS
runtime without making the native stack or an arbitrary host substate a GC
root.

### 9.9 Boundary summary

For JS MIR, the practical interface is:

```text
generated MIR
    -> Context* + Item/I64/D lanes
    -> raw compiled env or resumable state words when required
    -> the capsule-indexed call-depth guard
    -> named JS C helpers
    -> Item result, with explicit error/companion lanes

named JS helpers
    -> JsFunction / JsCallableCode / JsAccessorCell
    -> JsClassMeta / JsPropertyOps
    -> Map/VMap-based JS carriers
    -> JsInterpEnv or a raw GC_TYPE_ENVIRONMENT allocation
    -> JsSuspendedActivation (generator / async records)
    -> JsRuntimeState and its RootVector root owners
```

The critical ABI conclusion is that the JS-specific structs are helper-side
layout contracts, while the emitted MIR layout contract is deliberately small:
`Context*`, the shared `Item`/container layouts, `String`'s hot fields, raw
environment slots, the capsule path to the call-depth counter, and the
documented resumable state-machine lanes.

## 10. One-glance struct index

One named struct per row. "Direct" means generated MIR emits a field/slot
address or passes the physical carrier (see the §7.1 inventory); "helper" means
MIR passes an `Item`, pointer, or opaque lane and named C code performs the
layout-specific access.

| Struct | Family | One-line MIR-facing role | Contact |
|---|---|---|---|
| `Item` | Lambda | Universal one-word value carrier | direct / helper |
| `Context` | Lambda | Hidden execution, root, number, and companion-lane owner | direct |
| `EvalContext` | Lambda | Long-lived isolate extending `Context` | direct / helper |
| `ContextCapsuleDirectory` | Lambda | Indexed directory of context-owned subsystem state | direct (JS) / helper |
| `ContextCapsuleSlot` | Lambda | One capsule pointer plus its lifecycle ops | direct (JS) / helper |
| `ContextCapsuleOps` | Lambda | Frozen per-subsystem construct/release/destroy record | helper |
| `ContextCapsuleExtension` | Lambda | Keyed slot for optional module families | helper |
| `Heap` | Lambda | GC and allocation state reached from the context | direct |
| `Container` | Lambda | Common header for materialized containers | direct |
| `Map` | Lambda | Shape-backed property face | direct |
| `List` | Lambda | Indexed Item sequence | direct |
| `Array` | Lambda | Compatibility alias for `List` | direct |
| `ArrayNum` | Lambda | Numeric indexed sequence and native numeric lanes | direct / helper |
| `Element` | Lambda | Map-plus-content object carrier | direct |
| `Object` | Lambda | Compatibility alias for `Element` | direct |
| `Range` | Lambda | Range container | helper |
| `SparseArrayMap` | Lambda | `Map` plus sparse numeric index table and version | helper |
| `VMap` | Lambda | Virtual/native property projection | helper |
| `VArray` | Lambda | Virtual indexed sequence | helper |
| `Velmt` | Lambda | Virtual element with property and child callbacks | helper |
| `VirtualContainer` | Lambda | Common virtual-container prefix | helper |
| `ArrayRepCert` | Lambda | Homogeneous-array representation proof on `List`/`ArrayNum` | direct |
| `LaneStorageDesc` | Lambda | Resolved physical lane for a shaped slot or array leaf | direct |
| `String` | Lambda | UTF-8 scalar; `chars`/`len` are hot MIR fields | direct / helper |
| `Symbol` | Lambda | Symbol scalar payload | helper |
| `Binary` | Lambda | Byte sequence payload | helper |
| `Complex` | Lambda | Complex-number payload | helper |
| `Decimal` | Lambda | Decimal/BigInt payload | helper |
| `DateTime` | Lambda | Date/time scalar payload | helper |
| `Path` | Lambda | Segmented path payload | helper |
| `LambdaError` | Lambda | Error object carried by an error `Item` | helper |
| `TypedItem` | Lambda | Dynamic field value plus type tag (`LANE_STORAGE_TYPED_ITEM`) | direct / helper |
| `Type` | Lambda | Type metadata | helper |
| `TypeInfo` | Lambda | Immutable base-type metadata; `type` is baked | direct / helper |
| `TypeMap` | Lambda | Shape/type descriptor for maps and JS classes | direct / helper |
| `TypeMapTransition` | Lambda | Shape-transition edge for incremental object shapes | helper |
| `TypeNominal` | Lambda | Authoritative nominal record behind the header cache bit | helper |
| `ShapeEntry` | Lambda | Field offset, contract, lane descriptor, and JS descriptor flags | direct |
| `ArrayNumShape` | Lambda | N-D numeric-array/view side table | helper |
| `Function` | Lambda | Lambda callable and closure carrier; `flags` is baked | direct / helper |
| `LambdaModuleState` | Lambda | Context-local module variable/scalar slabs | direct / helper |
| `LambdaModuleLayout` | Lambda | Immutable sealed module image metadata | helper |
| `PropertyKeySpec` | Lambda | Immutable generated property-key record | helper |
| `LambdaModuleVarRef` | Lambda | Compact imported module-slot reference | helper |
| `LambdaGcContainerLayout` | GC mirror | C collector view of `Container` | helper |
| `LambdaGcMapLayout` | GC mirror | C collector view of `Map` | helper |
| `LambdaGcListLayout` | GC mirror | C collector view of `List`/`Array` (stops before `rep_cert`) | helper |
| `LambdaGcTypeMapLayout` | GC mirror | C collector view of type-map metadata | helper |
| `LambdaGcShapeEntryLayout` | GC mirror | C collector view of shape entries | helper |
| `LambdaGcTypedItemLayout` | GC mirror | C collector view of `TypedItem` | helper |
| `LambdaGcArrayNumShapeLayout` | GC mirror | C collector view of numeric shape metadata | helper |
| `LambdaGcVMapLayout` | GC mirror | C collector view of virtual-map prefix | helper |
| `LambdaGcVMapVtableLayout` | GC mirror | C collector view of the VMap vtable (`trace` offset) | helper |
| `LambdaGcVirtualContainerLayout` | GC mirror | C collector view of virtual-container prefix | helper |
| `LambdaGcVirtualVtableLayout` | GC mirror | C collector view of the VArray/Velmt vtable | helper |
| `LambdaGcFunctionLayout` | GC mirror | C collector view of Lambda function fields | helper |
| `JsFunction` | JavaScript | JS callable value: env, capabilities, observable state | helper |
| `JsCallableCode` | JavaScript | Interned immutable definition-site facts shared by closures | helper |
| `JsAstDefinition` | JavaScript | Script-owned immutable AST definition facts | helper |
| `JsFunctionPayload` | JavaScript | Optional function payload aggregate (one word on the value) | helper |
| `JsBoundData` | JavaScript | Bound target, receiver home, and bound arguments | helper |
| `JsClassData` | JavaScript | Class constructor/prototype/superclass + custom-element name | helper |
| `JsWithData` | JavaScript | Captured raw environment plus `with` depth | helper |
| `JsNativeCode` | JavaScript | Native call/construct target, arity, and policy | helper |
| `JsAstBody` | JavaScript | Per-value AST closure state (definition, env, lexical `this`) | helper |
| `JsEvalOrigin` | JavaScript | Dynamic-function source origin | helper |
| `JsAccessorCell` | JavaScript | Getter/setter property storage; `GC_TYPE_JS_ACCESSOR`, not a FUNC value | helper |
| `JsInterpEnv` | JavaScript | GC-owned AST/eval lexical environment (interp payload shape) | helper |
| `JsClassMeta` | JavaScript | JS class family, flags, prototype policy, and ops | helper |
| `JsPropertyOps` | JavaScript | Immutable JS exotic-operation callback table (11 hooks) | helper |
| `JsPropertyOpResult` | JavaScript | Explicit exotic-operation disposition/completion | helper |
| `JsProxyData` | JavaScript | Proxy target, handler, capabilities, and revocation | helper |
| `JsProxyMapCarrier` | JavaScript | `Map` prefix plus `JsProxyData` | helper |
| `JsArrayBuffer` | JavaScript | Stable buffer identity over a `ByteBufferHandle` | helper |
| `JsArrayBufferMapCarrier` | JavaScript | `Map` prefix plus ArrayBuffer payload pointer | helper |
| `JsArrayBufferView` | JavaScript | The one view record: buffer, offset, length, length-tracking | helper |
| `JsTypedArraySpec` | JavaScript | Immutable per-element-type descriptor | helper |
| `JsTypedArray` | JavaScript | View base plus element type and `ArrayNum` alias view | helper |
| `JsTypedArrayMapCarrier` | JavaScript | `Map` prefix plus typed-array payload | helper |
| `JsDataView` | JavaScript | View base only; no additional stored state | helper |
| `JsDataViewMapCarrier` | JavaScript | `Map` prefix plus DataView payload | helper |
| `JsCollectionOrderNode` | JavaScript | Insertion-order link for Map/Set data | helper |
| `JsCollectionData` | JavaScript | Native hash table and collection-order state | helper |
| `JsCollectionMap` | JavaScript | `Map` prefix plus collection-data pointer | helper |
| `JsIterData` | JavaScript | Iterator source, index, and length snapshot | helper |
| `JsIteratorMapCarrier` | JavaScript | `Map` prefix plus fixed iterator payload | helper |
| `JsRegexData` | JavaScript | Compiled RegExp/native matcher payload | helper |
| `JsRegExpMapCarrier` | JavaScript | `Map` prefix plus regex payload and override bits | helper |
| `JsPromise` | JavaScript | `VMap` promise state, result, reactions, and rejection state | helper |
| `JsPromiseRuntimeState` | JavaScript | Promise domain and unhandled-rejection state | helper |
| `JsSuspendedActivation` | JavaScript | Shared durable core of generator and async activations | helper |
| `JsGeneratorStateRecord` | JavaScript | Suspended generator counter, delegate, and replay cursors | helper |
| `JsGeneratorMapCarrier` | JavaScript | `Map` prefix owning generator state | helper |
| `JsAsyncContextStateRecord` | JavaScript | Suspended async state, promise, module id, and `this` | helper |
| `JsAsyncFrameCarrier` | JavaScript | `Map` prefix owning an async activation | helper |
| `JsRootedState` | JavaScript | Common `RootVector` root owner for JS state capsules | helper |
| `JsNamespaceState` | JavaScript | Rooted namespace-object state | helper |
| `JsRealmSlots` | JavaScript | Fixed realm singleton/cache Item slots | helper |
| `JsGlobalEnvironment` | JavaScript | The one dynamic realm binding table | helper |
| `JsRealmIntrinsicSlots` | JavaScript | Realm intrinsic constructor/prototype slots | helper |
| `JsIntrinsicState` | JavaScript | Intrinsic prototype roots, names, and mutation versions | helper |
| `JsWithFrame` | JavaScript | One `with` scope introduction, or a borrowed captured chain (JSCU44) | helper |
| `JsWithScopeState` | JavaScript | Dynamic-`with` chain head, scope slots and last-binding roots | helper |
| `JsEventLoopQueueState` | JavaScript | Next-tick, microtask, and RAF queue state | helper |
| `JsEventLoopTimerState` | JavaScript | Timer handles and mock-scheduler state | helper |
| `JsModuleRuntimeState` | JavaScript | Module/VM namespace slots and evaluation depth | helper |
| `JsAsyncHooksState` | JavaScript | Async resource and hook roots | helper |
| `JsAsyncLocalStorageState` | JavaScript | Realm-local async-local-storage instances | helper |
| `JsAsyncAwaitState` | JavaScript | The realm-owned await result handoff Item | helper |
| `JsEvalState` | JavaScript | Aggregate eval source, bridge, and local journals | helper |
| `JsEvalSourceState` | JavaScript | Eval source/code root lanes and offsets | helper |
| `JsEvalBridgeState` | JavaScript | Direct-eval binding journals and frame marks | helper |
| `JsEvalLocalState` | JavaScript | Caller-local eval bindings and lexical keys | helper |
| `JsCallActivation` | JavaScript | One synchronous call's ambient `this`/newTarget/args homes | helper |
| `JsExecutionState` | JavaScript | Activation chain plus `call_depth`/`call_stack_limit` | direct / helper |
| `JsCodeStore` | JavaScript | Context-owned MIR artifacts and source owners | helper |
| `JsProcessState` | JavaScript | Process object, listeners, and IPC roots | helper |
| `JsTest262AgentState` | JavaScript | Test262 agent object, callbacks, and reports | helper |
| `JsRuntimeState` | JavaScript | Complete context-owned JS realm/execution capsule | helper |
