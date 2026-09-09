# Lambda Runtime Structs at the MIR Boundary

> **Status: IMPLEMENTATION CENSUS — 2026-09-09.** This document records the
> current C/C++ layouts and pointer interfaces consumed by Lambda MIR Direct.
> It introduces no new semantic or ABI ruling.
>
> **Formal linkage:** `D2.1.1` (one-word `Item`), `D2.1.3` (tagged leaf
> pointers), `D2.4.1` (semantic representation discipline), `D2.6.1`
> (array carriers), `D2.6.6v2` (container hierarchy), `D3.4.1` and `D3.4.6`
> (packed shapes and lane descriptors), `D5.2.1v3` (companion returns),
> `D5.4.1`–`D5.4.3` (context ownership), `D6.2.1` (function values), and
> `D7.4.1v2`–`D7.4.3` (native/guest boundaries).
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
`lambda/runtime/transpile-mir.cpp`; common ABI and rooting emission lives in
`lambda/runtime/mir_emitter_shared.hpp`.

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
[`lambda.h`](../lambda/lambda.h#L858).

Generated code must use the runtime's boxing/unboxing conventions. A raw
comparison of two `Item` words is not general semantic equality
(`D2.1.2`).

### 2.2 `Context`

Every generated entry receives a hidden leading `Context* runtime`. The
JIT-visible prefix is defined in
[`lambda.h`](../lambda/lambda.h#L2255) and contains:

- pool, arena, constants, type-list, current-directory, and allocation hooks;
- stack-limit and UI-mode state;
- precise side-root stack base/top/limits;
- non-GC number-stack base/top/limits;
- `mir_return_lane`;
- `mir_bitcast_scratch` for double/bit reinterpretation;
- `mir_companion_slot` for the v3 second return lane;
- `mir_var_homes[]` for the direct `var`-parameter transport.

The scratch, companion, and `var`-home cells are transport locations, not GC
roots. The companion cell holds raw scalar bits only, and is live only between
a shape-2 return and its resolution (`D5.2.1v3`, `D5.2.2v3`).

### 2.3 `EvalContext` and `Heap`

`EvalContext` extends `Context` and is the canonical long-lived isolate owner
(`D5.4.1`–`D5.4.2`). It adds the runtime heap, AST/name pools, result and error
state, scheduler, JS state, module-state table, and capsule tables. Its
definition is in [`lambda-data.hpp`](../lambda/lambda-data.hpp#L107).

Generated code normally treats the pointer as `Context*`, but the inline
allocation path uses the established layout relationship:

```text
Context* runtime -> EvalContext::heap -> Heap::gc
```

The `Heap` definition is in
[`transpiler.hpp`](../lambda/lambda/runtime/transpiler.hpp#L26). The offsets are
asserted in [`transpile-mir.cpp`](../lambda/lambda/runtime/transpile-mir.cpp#L62)
so an `EvalContext` prefix change cannot silently invalidate inline allocation.

## 3. Container hierarchy

The physical hierarchy is one chain:

```text
Container
  └── Map
        ├── List / Array
        │     └── Element / Object
        └── ArrayNum
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
| 5–6 | constructor-reservation mask |
| 7 | reserved state |

The shared C/C++ definition and its static assertions are in
[`lambda.h`](../lambda/lambda.h#L955). The derived C++ container definitions
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
[`lambda.h`](../lambda/lambda.h#L1163).

### 3.3 `List` / `Array`

`List` extends `Map` with:

```text
Item*   items
int64_t length
int64_t extra
int64_t capacity
```

`Array` is the compatibility alias for `List`. The `extra` region owns
container-resident wide scalar payloads where required by `D2.6.4`.

### 3.4 `ArrayNum`

`ArrayNum` extends `Map` with a union buffer pointer:

```text
int64_t* items       // integer lanes
double*  float_items // binary64 lane
void*    data        // compact numeric lanes
int64_t  length
int64_t  extra
int64_t  capacity
```

Its `map_kind` byte carries `ArrayNumElemType`; its `array_flags` byte carries
N-D/view/native-lane state. `ArrayNum` shares the content offsets with `List`
and `Element`, which is asserted by the MIR lowerer at
[`transpile-mir.cpp`](../lambda/lambda/runtime/transpile-mir.cpp#L81).

`ArrayNumShape` is the side table referenced by `extra` for N-D arrays and
views. Its layout is declared in
[`lambda.h`](../lambda/lambda.h#L1245).

### 3.5 `Element` / `Object`

`Element` adds the content face to the inherited `Map` attribute face but adds
no new fields beyond `List`. `Object` is an alias of `Element`; there is no
separate object container TypeId. Nominal identity is carried by the type
descriptor and cached in the container header (`D2.6.6v2`).

### 3.6 Other container carriers

| Struct | Interface |
|---|---|
| `Range` | `Container` header plus `start`, `end`, `length`, and `is_char` |
| `SparseArrayMap` | `Map` plus sparse numeric index table and mutation version |
| `VMap` | `Container` plus opaque backing `data`, `VMapVtable`, host metadata, and expando `Item` |
| `VArray` | Virtual indexed sequence with `VArrayVtable` |
| `Velmt` | Virtual element with `VelmtVtable`, attribute callbacks, and child callbacks |
| `VirtualContainer` | Carrier-neutral virtual-container prefix used by GC/Jube layout checks |

The materialized and virtual definitions, vtables, and offset assertions are
in [`lambda.hpp`](../lambda/lambda.hpp#L805).

`VMap` is the representation used when a native structure must cross into the
Lambda value domain as a projection (`D7.4.1v2`). Raw native pointers are not
script-visible. Guest modules additionally cross through opaque handles and
versioned API tables; they do not receive `Runtime*`, `EvalContext*`,
`Input*`, `MIR_context_t`, or `AstNode*` (`D7.4.3`).

## 4. Leaf payload and dynamic-field structs

### 4.1 Scalar payloads

| Struct | MIR/runtime role |
|---|---|
| `String` | length, flags, and trailing UTF-8 bytes |
| `Symbol` | length, namespace pointer, and trailing name bytes |
| `Binary` | byte length, storage handle/offset, and optional inline bytes |
| `Complex` | `type_id`, real component, imaginary component |
| `Decimal` | decimal mode and opaque `mpd_t*` payload |
| `DateTime` | library-owned datetime payload, usually reached through helpers |
| `Path` | opaque segmented path object reached through the path API |
| `LambdaError` | error object referenced by an error `Item`; not a normal generated value carrier |

The public leaf layouts are in [`lambda.h`](../lambda/lambda.h#L866) and
[`lambda-data.hpp`](../lambda/lambda-data.hpp#L156). `LambdaError` is defined in
[`lambda-error.h`](../lambda/lambda/runtime/lambda-error.h#L201).

### 4.2 `TypedItem`

`TypedItem` is the packed dynamic-field carrier: a `TypeId` followed by one
64-bit payload union. Its payload may be an immediate, scalar word, pointer,
container pointer, `Type*`, or `Function*`.

It is used when a map field cannot be represented by one statically selected
native lane, including `any` and certain union/occurrence contracts. The
runtime definition is in [`lambda-data.hpp`](../lambda/lambda-data.hpp#L183).
The collector's C-only byte-level mirror is
`LambdaGcTypedItemLayout` in [`lambda.h`](../lambda/lambda.h#L723).

## 5. Shape and module metadata

### 5.1 Type and shape records

`Type` is the common type-descriptor prefix: `type_id` plus kind/literal/const/
nominal flags. `TypeMap` adds map byte size, field count, shape chain, and
runtime shape metadata. `ShapeEntry` records:

```text
name
type
byte_offset
next
...
LaneStorageDesc storage
```

The packed map bytes and direct MIR access must agree with the shape chain
(`D3.4.1`). The field's full `Type*` contract and its stored
`LaneStorageDesc` determine the physical lane (`D3.4.6`); MIR must not infer
this from a TypeId or MIR register class.

The C++ definitions are in
[`lambda-data.hpp`](../lambda/lambda-data.hpp#L316) and
[`lambda-data.hpp`](../lambda/lambda-data.hpp#L376). The GC bridge exposes only
the stable prefixes in `LambdaGcTypeMapLayout` and
`LambdaGcShapeEntryLayout` from [`lambda.h`](../lambda/lambda.h#L704).

`TypeInfo` is immutable type metadata used by a few generated retagging paths
for type pointers and byte sizes. It is defined in
[`lambda-data.hpp`](../lambda/lambda-data.hpp#L156).

### 5.2 Module state records

Generated MIR does not bake mutable module-variable addresses into code. It
uses context-selected module state (`D5.4.3`):

| Struct | Purpose |
|---|---|
| `LambdaModuleState` | context-local `Item` variable slab, scalar payload slab, property-key IDs, constants, type list, and capacity/registration metadata |
| `LambdaModuleLayout` | immutable sealed MIR image metadata: module ID, variable count, and property-key image |
| `PropertyKeySpec` | immutable property-key specification in the sealed image |
| `LambdaModuleVarRef` | compact `(module_id, slot)` reference for imports |

`LambdaModuleState` is defined in
[`lambda-data.hpp`](../lambda/lambda-data.hpp#L87); the immutable image records
are in [`lambda.h`](../lambda/lambda.h#L2306).

## 6. Function and closure interface

`Function` is a GC-managed first-class value (`D6.2.1`). Its ABI-relevant
fields are:

```text
type_id
arity
closure_field_count
entry_abi
flags
fn_type
ptr
closure_env
name
runtime_context
def / def_module / method
```

The generated code may update the ABI/return-shape flags and the collector
uses `closure_field_count` and `closure_env` to trace captures. A closure
environment stores captured values as `Item`s (`D6.2.3`–`D6.2.4`). Dynamic
calls enter through runtime dispatch functions such as `fn_call*`; the
function pointer is not itself a script-visible raw pointer.

The layout and dynamic-call declarations are in
[`lambda.h`](../lambda/lambda.h#L1392).

## 7. GC layout mirrors

The C collector cannot include the complete C++ runtime headers. Therefore
`lambda.h` defines checked byte-level mirrors:

- `LambdaGcContainerLayout`;
- `LambdaGcMapLayout` and `LambdaGcListLayout`;
- `LambdaGcTypeMapLayout` and `LambdaGcShapeEntryLayout`;
- `LambdaGcTypedItemLayout`;
- `LambdaGcArrayNumShapeLayout`;
- `LambdaGcVMapLayout` and `LambdaGcVirtualContainerLayout`;
- `LambdaGcFunctionLayout` and virtual-vtable layout records.

These are not alternative runtime value types. They are C collector views of
the prefixes and offsets used by the C++ objects. The corresponding C++
definitions carry static assertions against them. This keeps GC tracing,
packed-field access, and generated MIR on one layout contract
(`D3.4.1`, `D4.3`, `D5.3.4`).

## 8. What generated MIR does not see

Generated MIR does not receive or persistently embed:

- C++ class methods or C++ object ownership semantics;
- a struct-valued MIR parameter or return;
- the native C stack as a GC root;
- context-dependent mutable module addresses;
- raw guest/native pointers as Lambda values;
- the retired C-text/C2MIR backend.

The stable interface is the combination of `Context*`, one-word `Item`s,
runtime-owned pointers, checked struct offsets, and named C ABI helper calls.

## 9. JavaScript runtime structs at the MIR boundary

This section is the JS-profile companion to the Lambda census above. “Faced by
MIR” has two meanings here:

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
[`js_mir_module_batch_lowering.cpp`](../lambda/js/js_mir_module_batch_lowering.cpp#L2897).
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
[`js_mir_function_class_lowering.cpp`](../lambda/js/js_mir_function_class_lowering.cpp#L1109).
The runtime wraps those words in generator or async carriers before they can
outlive the creating call.

Generated JS MIR directly bakes offsets for the shared `Context` transport
fields and for `String::chars`/`String::len`; the current JS lowering does not
bake a `JsFunction`, `JsPromise`, typed-array, or exotic-carrier offset. The
direct offset sites are [`js_mir_hashmap_scope_utils.cpp`](../lambda/js/js_mir_hashmap_scope_utils.cpp#L579)
and [`js_mir_expression_lowering.cpp`](../lambda/js/js_mir_expression_lowering.cpp#L6611).
JS object operations, calls, construction, suspension, and typed-array access
therefore remain named helper calls.

### 9.2 Shared Lambda records used by JS

These records are already detailed in Sections 2–7 and are not duplicated
here:

`Item`, `Context`, `EvalContext`, `Heap`, `Container`, `Map`, `List`/`Array`,
`ArrayNum`, `Element`/`Object`, `Range`, `VMap`, `VArray`, `Velmt`,
`VirtualContainer`, `String`, `Symbol`, `Binary`, `Complex`, `Decimal`,
`DateTime`, `Path`, `LambdaError`, `TypedItem`, `Type`, `TypeMap`, `ShapeEntry`,
`ArrayNumShape`, `Function`, `LambdaModuleState`, `LambdaModuleLayout`,
`PropertyKeySpec`, `LambdaModuleVarRef`, and the `LambdaGc*` layout mirrors.

JS uses those records unchanged. The JS additions below either add a payload to
one of those records or hold JS realm/execution state behind a named helper.

### 9.3 JS callable records

#### `JsFunction`

`JsFunction` is the JS callable carrier occupying the `Item::function` pointer
slot with `type_id == LMD_TYPE_FUNC`. Its stable discrimination prefix is:

```text
offset 0: TypeId type_id
offset 4: uint32_t layout_magic == JS_FUNCTION_LAYOUT_MAGIC
```

The remainder is grouped as follows:

| Group | Fields / role |
|---|---|
| executable identity | `func_ptr`, `env`, `param_count`, `env_size`, `invoke`, `construct` |
| observable function state | `prototype`, `name`, `properties_map`, `home_global`, `source_text` |
| owner/capability state | `runtime_context`, `module_state_id`, `flags`, `formal_length`, `intrinsic_class`, `body_kind` |
| optional payload | `JsFunctionPayload* payload` |

`JsFunctionPayload` holds the optional records `JsBoundData`, `JsClassData`,
`JsWithData`, `JsAstBody`, `JsNativeCode`, and `JsEvalOrigin`. Their purposes are
bound target/arguments, class constructor metadata, a captured `with`
environment, AST/interpreter body state, native-call policy, and dynamic
function source origin respectively. The call and construct function-pointer
types are `JsCallEntry` and `JsConstructEntry`; native target selection is
carried by `JsNativeTarget` and `JsNativeCallPolicy`.

The complete JS callable layout and its two contractual discriminator offsets
are in [`js_function.hpp`](../lambda/js/js_function.hpp#L19). The runtime ABI
entry points are [`js_call_function_into`](../lambda/js/js_runtime.h#L380),
[`js_function_get_ptr`](../lambda/js/js_runtime.h#L479), and
[`js_function_get_arity`](../lambda/js/js_runtime.h#L482).

Generated MIR does not dereference a `JsFunction` field. It calls the named JS
dispatch helpers; C code validates the layout magic and dispatches through
`invoke`/`construct`. This is an important difference from the shared Lambda
`Function` path, where a MIR lowering may load a checked common field offset.

#### `JsAccessorPair`

`JsAccessorPair` is a compact GC object containing `getter` and `setter` Items.
It deliberately starts with the same `LMD_TYPE_FUNC` tag and discriminator
offset as `JsFunction`, but is selected only when
`ShapeEntry::flags & JSPD_IS_ACCESSOR` is set. It is not a callable JS function
despite sharing the function Item tag. The definition and guard are in
[`lambda-data.hpp`](../lambda/lambda-data.hpp#L299).

### 9.4 JS environments and captured MIR state

#### Raw compiled closure environment (`GC_TYPE_JS_ENV`)

`js_alloc_env(count)` returns an `Item*`, but generated MIR carries it as an
I64/raw pointer rather than as an `Item`. The allocation has two equal regions:

```text
first half:  count Item slots traced by the GC
second half: count uint64_t scalar homes, not traced as Items
```

This is the environment used by compiled closures, module scope, native
closures, generators, and async bodies. `js_env_rehome_scalars` moves any
captured wide-number payload out of a transient invocation number stack before
the environment is retained. The allocator and collector contract are in
[`lambda-mem.cpp`](../lambda/runtime/lambda-mem.cpp#L665) and
[`gc_heap.c`](../lambda/runtime/gc/gc_heap.c#L1679).

The environment is intentionally not a C++ struct with a visible header: its
length comes from the GC allocation header, and its first half is the exact
Item edge set. `JsWithData` stores one such environment plus a dynamic-scope
depth when a closure captures a `with` binding.

#### `JsInterpEnv`

`JsInterpEnv` is the separate GC-owned lexical environment for AST-interpreted
functions and direct `eval`. It contains `outer`, `scope`, the arguments/private
class/eval/lexical-`this` Item words, `function_node`, `slot_count`, flags, and a
flexible `slots[]` tail. Its durable slots use the same Item-plus-companion
scalar convention as compiled environments. It is allocated as
`offsetof(JsInterpEnv, slots) + 2 * slot_count * sizeof(uint64_t)` and traced by
the dedicated `GC_TYPE_JS_INTERP_ENV` case. See
[`js_interp_env.h`](../lambda/js/js_interp_env.h#L10) and
[`js_interp.cpp`](../lambda/js/js_interp.cpp#L750).

The two environment forms must not be conflated: compiled MIR receives a raw
`Item*` environment word, while AST/eval helpers receive a `JsInterpEnv*`.

### 9.5 JS object metadata and exotic operation records

#### `JsClassMeta`, `JsPropertyOps`, and `JsPropertyOpResult`

`JsClassMeta` is the JS-specific metadata attached to a nominal JS
`TypeMap`. It contains a stable `JsClassId`, a `JsClassFamily`, class flags, a
`JsPrototypePolicy`, and a pointer to immutable `JsPropertyOps`. The operation
table supplies the JS exotic hooks for get/set, own-property definition and
deletion, own-key and descriptor queries, prototype operations, and extensible
state. Each hook returns `JsPropertyOpResult` with an explicit disposition and
completion `Item`.

This is metadata and dispatch state, not a replacement for the Lambda `Map`
layout. The definitions and operation-table declarations are in
[`js_object_meta.h`](../lambda/js/js_object_meta.h#L7).

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
See [`js_runtime.h`](../lambda/js/js_runtime.h#L1113) and
[`js_runtime.cpp`](../lambda/js/js_runtime.cpp#L92).

### 9.6 JS native payload carriers

The following are JS-specific carriers whose first field is a shared Lambda
`Map` or `VMap` prefix. They are the main object layouts that C helpers inspect
after MIR has passed an `Item`:

| Carrier / payload | Layout and purpose |
|---|---|
| `JsArrayBufferMapCarrier` / `JsArrayBuffer` | `Map` plus a `JsArrayBuffer*`; the buffer identity is stable while its `ByteBufferHandle` swaps storage on detach, resize, transfer, or copy-on-write. |
| `JsTypedArrayMapCarrier` / `JsTypedArray` | `Map` plus element type, backing buffer, original buffer Item, length-tracking flags, Buffer marker, and an `ArrayNum*` view. |
| `JsDataViewMapCarrier` / `JsDataView` | `Map` plus backing buffer, byte offset/length, original buffer Item, and length-tracking state. |
| `JsCollectionMap` / `JsCollectionData` | `Map` plus a `JsCollectionData*`; that record owns the native `HashMap*`, Map/Set/WeakMap/WeakSet kind, and insertion-order nodes, while ordinary shape storage remains available for expandos. |
| `JsIteratorMapCarrier` / `JsIterData` | `Map` plus source `Item`, current index, and length snapshot; its map kind is `MAP_KIND_ITERATOR`. |
| `JsRegExpMapCarrier` / `JsRegexData` | `Map` plus compiled-regex payload and virtual-property override bits; its map kind is `MAP_KIND_REGEXP`. |
| `JsGeneratorMapCarrier` / `JsGeneratorStateRecord` | `Map` plus the suspended generator state, including the re-homed environment. |
| `JsAsyncFrameCarrier` / `JsAsyncContextStateRecord` | `Map` plus the suspended async activation, result promise, environment, resume state, and AST continuation fields. |

Typed-array layouts and their three map carriers are declared in
[`js_typed_array.h`](../lambda/js/js_typed_array.h#L39) and allocated in
[`js_typed_array.cpp`](../lambda/js/js_typed_array.cpp#L25). The collection,
iterator, and RegExp carriers are in
[`js_runtime.cpp`](../lambda/js/js_runtime.cpp#L1017),
[`js_runtime.cpp`](../lambda/js/js_runtime.cpp#L28972), and
[`js_runtime.cpp`](../lambda/js/js_runtime.cpp#L14771).

`JsPromise` is the important non-`Map` object carrier: it extends `VMap` and
holds `state`, `result`, a companion scalar result, reaction records, rejection
domain, expando/prototype state, and rejection bookkeeping. Its context-level
companion is `JsPromiseRuntimeState`, which owns the unhandled-rejection queue,
domain Items, and promise counters. The definitions are in
[`js_runtime_state.hpp`](../lambda/js/js_runtime_state.hpp#L638).

### 9.7 Resumable execution records

#### `JsGeneratorStateRecord`

This record is embedded in `JsGeneratorMapCarrier`. Its MIR-facing core is:

```text
runtime_context, state_fn, env, env_size, state,
done/started/executing/is_async,
private_home_class, delegate, delegate_resume, delegate_idx
```

AST generator continuations, arguments, lexical environments, pending resume
input, and loop/list/destructuring continuation pointers occupy the remainder.
The generator object therefore owns the state and its GC trace, rather than a
context-wide fixed index table. The record is declared in
[`js_runtime_state.hpp`](../lambda/js/js_runtime_state.hpp#L708), and its
carrier is initialized by the MIR-facing constructor in
[`js_runtime.cpp`](../lambda/js/js_runtime.cpp#L28111).

#### `JsAsyncContextStateRecord`

This is the analogous async activation record. Its MIR-facing core is
`runtime_context`, `state_fn`, `env`, `env_size`, `state`, `promise`,
`module_state_id`, and `this_val`; AST async bodies add interpreter lexical
environment and await-continuation fields. It is embedded in
`JsAsyncFrameCarrier`, tagged `MAP_KIND_ASYNC_FRAME`, and traced through the
frame Item retained by promise reactions. The record and carrier trace are in
[`js_runtime_state.hpp`](../lambda/js/js_runtime_state.hpp#L749) and
[`js_runtime.cpp`](../lambda/js/js_runtime.cpp#L31438).

The helper ABI exposes these as `js_generator_create_mir` and
`js_async_context_create_mir`; generated code does not retain a native stack
frame across `yield` or `await`. The declarations are in
[`js_runtime.h`](../lambda/js/js_runtime.h#L990) and
[`js_runtime.h`](../lambda/js/js_runtime.h#L1073).

### 9.8 Realm and execution-state capsule

`JsRuntimeState` is the JS profile's context-owned mutable capsule. Generated
MIR receives `Context*`, not `JsRuntimeState*`; JS helpers recover the active
state from the context/thread binding. The capsule contains the realm's
intrinsics, module/namespace state, Promise and iterator state, event-loop and
timer state, eval journals, current `this`/`newTarget` state, function-wrapper
caches, async scratch, and code-store ownership. It also owns the pointers to
the host-profile substates (fs, net, http, crypto, process, DOM, and related
Node/Test262 surfaces).

The JS-specific state records directly relevant to MIR/helper boundaries are:

`JsRootRange`, `JsRootedState`, `JsNamespaceState`, `JsItemStack`,
`JsWithScopeState`, `JsEventLoopQueueState`, `JsEventLoopTimerState`,
`JsPromiseRuntimeState`, `JsIteratorState`, `JsAsyncHooksState`,
`JsAsyncLocalStorageState`, `JsEvalState`, `JsEvalSourceState`,
`JsEvalBridgeState`, `JsEvalLocalState`, `JsVmRuntimeState`, `JsIntrinsicState`,
`JsModuleRuntimeState`, `JsCodeStore`, `JsProcessState`, and
`JsTest262AgentState`. The complete ownership record is in
[`js_runtime_state.hpp`](../lambda/js/js_runtime_state.hpp#L132) and
[`js_runtime_state.hpp`](../lambda/js/js_runtime_state.hpp#L893).

The common invariant is that Item-bearing state uses explicit `RootVector`,
`JsRootRange`, or carrier-specific GC trace hooks. POD counters, native handles,
and cache metadata are kept out of Item ranges. This is why MIR can call into
the JS runtime without making the native stack or an arbitrary host substate a
GC root.

### 9.9 Boundary summary

For JS MIR, the practical interface is:

```text
generated MIR
    -> Context* + Item/I64/D lanes
    -> raw compiled env or resumable state words when required
    -> named JS C helpers
    -> Item result, with explicit error/companion lanes

named JS helpers
    -> JsFunction / JsAccessorPair
    -> JsClassMeta / JsPropertyOps
    -> Map/VMap-based JS carriers
    -> JsInterpEnv or GC_TYPE_JS_ENV
    -> JsGeneratorStateRecord / JsAsyncContextStateRecord
    -> JsRuntimeState and its precise root ranges
```

The critical ABI conclusion is that the JS-specific structs are helper-side
layout contracts, while the emitted MIR layout contract is deliberately small:
`Context*`, the shared `Item`/container layouts, `String`'s hot fields, raw
environment slots, and the documented resumable state-machine lanes.

## 10. One-glance struct index

One named struct per row. “Direct” means generated MIR emits a field/slot
address or passes the physical carrier; “helper” means MIR passes an `Item`,
pointer, or opaque lane and named C code performs the layout-specific access.

| Struct | Family | One-line MIR-facing role | Contact |
|---|---|---|---|
| `Item` | Lambda | Universal one-word value carrier | direct / helper |
| `Context` | Lambda | Hidden execution, root, number, and companion-lane owner | direct |
| `EvalContext` | Lambda | Long-lived isolate extending `Context` | helper |
| `Heap` | Lambda | GC and allocation state reached from the context | helper |
| `Container` | Lambda | Common header for materialized containers | direct |
| `Map` | Lambda | Shape-backed property face | direct |
| `List` | Lambda | Indexed Item sequence | direct |
| `Array` | Lambda | Compatibility alias for `List` | direct |
| `ArrayNum` | Lambda | Numeric indexed sequence and native numeric lanes | direct / helper |
| `Element` | Lambda | Map-plus-content object carrier | direct |
| `Object` | Lambda | Compatibility alias for `Element` | direct |
| `Range` | Lambda | Range container | helper |
| `VMap` | Lambda | Virtual/native property projection | helper |
| `VArray` | Lambda | Virtual indexed sequence | helper |
| `Velmt` | Lambda | Virtual element with property and child callbacks | helper |
| `VirtualContainer` | Lambda | Common virtual-container prefix | helper |
| `String` | Lambda | UTF-8 scalar; `chars`/`len` are hot MIR fields | direct / helper |
| `Symbol` | Lambda | Symbol scalar payload | helper |
| `Binary` | Lambda | Byte sequence payload | helper |
| `Complex` | Lambda | Complex-number payload | helper |
| `Decimal` | Lambda | Decimal/BigInt payload | helper |
| `DateTime` | Lambda | Date/time scalar payload | helper |
| `Path` | Lambda | Segmented path payload | helper |
| `LambdaError` | Lambda | Error object carried by an error `Item` | helper |
| `TypedItem` | Lambda | Dynamic field value plus type tag | direct / helper |
| `Type` | Lambda | Type metadata | helper |
| `TypeMap` | Lambda | Shape/type descriptor for maps and JS classes | direct / helper |
| `ShapeEntry` | Lambda | Field offset, type, and JS descriptor flags | direct |
| `ArrayNumShape` | Lambda | N-D numeric-array/view side table | helper |
| `Function` | Lambda | Lambda callable and closure carrier | helper |
| `LambdaModuleState` | Lambda | Context-local module variable/scalar slabs | helper |
| `LambdaModuleLayout` | Lambda | Immutable sealed module image metadata | helper |
| `PropertyKeySpec` | Lambda | Immutable generated property-key record | helper |
| `LambdaModuleVarRef` | Lambda | Compact imported module-slot reference | helper |
| `LambdaGcContainerLayout` | GC mirror | C collector view of `Container` | helper |
| `LambdaGcMapLayout` | GC mirror | C collector view of `Map` | helper |
| `LambdaGcListLayout` | GC mirror | C collector view of `List`/`Array` | helper |
| `LambdaGcTypeMapLayout` | GC mirror | C collector view of type-map metadata | helper |
| `LambdaGcShapeEntryLayout` | GC mirror | C collector view of shape entries | helper |
| `LambdaGcTypedItemLayout` | GC mirror | C collector view of `TypedItem` | helper |
| `LambdaGcArrayNumShapeLayout` | GC mirror | C collector view of numeric shape metadata | helper |
| `LambdaGcVMapLayout` | GC mirror | C collector view of virtual-map prefix | helper |
| `LambdaGcVirtualContainerLayout` | GC mirror | C collector view of virtual-container prefix | helper |
| `LambdaGcFunctionLayout` | GC mirror | C collector view of Lambda function fields | helper |
| `JsFunction` | JavaScript | JS callable, closure, and call/construct capability record | helper |
| `JsFunctionPayload` | JavaScript | Optional function payload aggregate | helper |
| `JsBoundData` | JavaScript | Bound target, receiver, and bound arguments | helper |
| `JsClassData` | JavaScript | Class constructor/prototype/superclass facts | helper |
| `JsWithData` | JavaScript | Captured raw environment plus `with` depth | helper |
| `JsNativeCode` | JavaScript | Native call/construct target and policy | helper |
| `JsAstBody` | JavaScript | AST-bodied closure and lexical capture facts | helper |
| `JsEvalOrigin` | JavaScript | Dynamic-function source origin | helper |
| `JsAccessorPair` | JavaScript | Getter/setter pair stored in an accessor property slot | helper |
| `JsInterpEnv` | JavaScript | GC-owned AST/eval lexical environment | helper |
| `JsClassMeta` | JavaScript | JS class family, flags, prototype policy, and ops | helper |
| `JsPropertyOps` | JavaScript | Immutable JS exotic-operation callback table | helper |
| `JsPropertyOpResult` | JavaScript | Explicit exotic-operation disposition/completion | helper |
| `JsProxyData` | JavaScript | Proxy target, handler, capabilities, and revocation | helper |
| `JsProxyMapCarrier` | JavaScript | `Map` prefix plus `JsProxyData` | helper |
| `JsArrayBuffer` | JavaScript | Stable buffer identity over mutable byte storage | helper |
| `JsArrayBufferMapCarrier` | JavaScript | `Map` prefix plus ArrayBuffer payload pointer | helper |
| `JsTypedArray` | JavaScript | Typed element policy, buffer, and `ArrayNum` view | helper |
| `JsTypedArrayMapCarrier` | JavaScript | `Map` prefix plus typed-array payload | helper |
| `JsDataView` | JavaScript | Buffer-relative byte view and offset/length state | helper |
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
| `JsGeneratorStateRecord` | JavaScript | Suspended generator program counter and environment | helper |
| `JsGeneratorMapCarrier` | JavaScript | `Map` prefix owning generator state | helper |
| `JsAsyncContextStateRecord` | JavaScript | Suspended async state, promise, environment, and `this` | helper |
| `JsAsyncFrameCarrier` | JavaScript | `Map` prefix owning an async activation | helper |
| `JsRootRange` | JavaScript | Explicit precise root range descriptor | helper |
| `JsRootedState` | JavaScript | Common root owner for JS state capsules | helper |
| `JsNamespaceState` | JavaScript | Rooted namespace-object state | helper |
| `JsItemStack` | JavaScript | RootVector-backed Item LIFO stack | helper |
| `JsWithScopeState` | JavaScript | Dynamic-`with` stack and last-binding roots | helper |
| `JsEventLoopQueueState` | JavaScript | Next-tick, microtask, and RAF queue state | helper |
| `JsEventLoopTimerState` | JavaScript | Timer handles and mock-scheduler state | helper |
| `JsIteratorState` | JavaScript | Iterator/generator prototype and marker roots | helper |
| `JsAsyncHooksState` | JavaScript | Async resource and hook roots | helper |
| `JsAsyncLocalStorageState` | JavaScript | Realm-local async-local-storage instances | helper |
| `JsEvalState` | JavaScript | Aggregate eval source, bridge, and local journals | helper |
| `JsEvalSourceState` | JavaScript | Eval source/code root lanes and offsets | helper |
| `JsEvalBridgeState` | JavaScript | Direct-eval binding journals and frame marks | helper |
| `JsEvalLocalState` | JavaScript | Caller-local eval bindings and lexical keys | helper |
| `JsVmRuntimeState` | JavaScript | VM namespace and source identifier state | helper |
| `JsIntrinsicState` | JavaScript | Intrinsic prototype roots and mutation versions | helper |
| `JsModuleRuntimeState` | JavaScript | Active namespace and module evaluation depth | helper |
| `JsCodeStore` | JavaScript | Context-owned MIR artifacts and source owners | helper |
| `JsProcessState` | JavaScript | Process object, listeners, and IPC roots | helper |
| `JsTest262AgentState` | JavaScript | Test262 agent object, callbacks, and reports | helper |
| `JsRuntimeState` | JavaScript | Complete context-owned JS realm/execution capsule | helper |
