# LambdaJS Object and Property Runtime — Shape Metadata and Exotic Operations

**Date**: 2026-08-12

**Status**: IMPLEMENTED — JR4/Tune6 object metadata and exotic operations
landed 2026-08-12

**Implementation anchor**: current worktree after the Tune5 property ABI
handoff

**Companions**: [JS_Runtime_Redesign.md](JS_Runtime_Redesign.md) (JR1, JR4,
JR6), [JS_Runtime_Name.md](JS_Runtime_Name.md) (RN1–RN16),
[JS_Runtime_Callable.md](JS_Runtime_Callable.md) (JC1–JC12),
[JS_Tune5_Property.md](JS_Tune5_Property.md) (the eight-operation property
kernel), and [JS_Tune6_Object.md](JS_Tune6_Object.md) (implementation plan)

Decisions introduced here use **JOP#** ledger IDs. Formal rulings are cited
first whenever they cover the point. The design applies **D1.2–D1.3**,
**D1.5–D1.6**, **D2.1.4**, **D2.6.1–D2.6.2**, **D3.4.1**,
**D3.4.4v2–D3.4.6**, **D4.3.1–D4.3.3**,
**D4.6.1v2–D4.6.2v2**, **D5.2.1–D5.2.2**,
**D5.3.1–D5.3.5**, **D5.4.1–D5.4.4**, **D6.2.2v2**,
**D7.4.1–D7.4.3**, and **D8.4.3**. The immutable JavaScript class metadata
extension is adopted as **D3.4.7** in the formal design specification
(version 1.15.0).

Per **D1.6**, this design evolves MIR Direct and the current runtime only.
The frozen C2MIR implementation is not modified, wrapped, or used as an
acceptance gate.

---

## 0. Decision and target outcome

LambdaJS will have one answer to each of these questions:

- What kind of object is this? Resolve its immutable `JsClassMeta`.
- Does it have exotic internal methods? Read `meta->property_ops`.
- What is its ordinary property layout? Read its `TypeMap` and shape.
- What is its physical native backing? Read `map_kind` only inside storage,
  tracing, finalization, and checked payload accessors.
- What is its prototype? Read an explicit internal override, then apply the
  metadata's realm-relative prototype policy.
- How is a property operation executed? Enter Tune5's eight semantic
  operations; they consult at most one exotic table and otherwise use the
  ordinary core.

The target flow is:

```text
source/MIR/native caller
  -> Tune5 semantic operation(target, property lane, observable key, receiver)
      -> resolve immutable object metadata
      -> optional JsPropertyOps callback
           COMPLETE: return its value/status/error
           FALLTHROUGH: no observable work occurred
      -> ordinary own elements or TypeMap shape/slot
      -> one prototype resolver
      -> repeat with original Receiver
```

The target is not a new JavaScript-private object model. JS objects remain
Lambda `Item` values; ordinary objects remain `Map` + `TypeMap`; arrays remain
Lambda arrays governed by Tune5's per-container elements kind; native host
objects remain VMaps under **D7.4.1**. This design removes the parallel
classification and dispatch mechanisms that accumulated around those shared
representations.

The redesign is complete only when:

| Area | Required result |
|---|---|
| Object classification | Runtime behavior consults `TypeId` plus one resolved `JsClassMeta`. A per-object `JsClass` byte, sentinel property, or general-purpose `map_kind` branch is not a semantic discriminator. |
| Shape ownership | Every JS-created Map is born into a metadata-qualified `TypeMap` family. Property/descriptor transitions inherit the same immutable metadata. Class identity never requires cloning a shape after object creation. |
| Exotic dispatch | One `JsPropertyOps` table surface owns exotic property, prototype, and extensibility hooks. Scattered type/kind switches and `js_try_exotic_*` families are absent. |
| Property API | Tune5's eight public semantic operations remain unchanged. The ops table is internal and does not become a ninth public property family. |
| Prototype | One resolver owns explicit `[[Prototype]]`, intrinsic defaults, TypedArray per-kind defaults, host prototypes, and null. `js_get_implicit_proto` and `__instance_proto__` are deleted. |
| Native payloads | `Map.data` always matches `Map.type` under **D3.4.1/D3.4.5**. Proxy, TypedArray, ArrayBuffer, DataView, iterator, and collection state uses typed trailing/native storage, never a fake TypeMap or a marker property. |
| Callability | Metadata never grants `[[Call]]` or `[[Construct]]`. Callable capability remains per value under **D6.2.2v2**; legacy callable class Maps become functions. |
| Host boundary | VMap/Jube host operations remain the authority under **D7.4.1–D7.4.3**; one core bridge exposes them to the property kernel without copying their tables into JS Maps. |
| Sentinels | Class/prototype/native-backing sentinels are deleted. `__promise_idx` is the only named temporary exception and is confined to JR7's legacy Promise adapter. |
| Ownership | Metadata contains no realm `Item`, GC pointer, mutable cache, or context-owned address. All callback operands that survive allocation/re-entry are precisely rooted under **D5.3**. |

---

## 1. Root cause in the current representation

### 1.1 Five facts currently compete

Object behavior is currently inferred from a mixture of:

1. `TypeId` (`MAP`, `ARRAY`, `FUNC`, `VMAP`, `ERROR`, and primitives);
2. `Map.map_kind` (`TYPED_ARRAY`, `PROXY`, `ITERATOR`, `PROCESS_ENV`, and
   others);
3. the `TypeMap.js_class` byte and `js_class_stamp()`;
4. ordinary shape entries, including internal-looking string properties; and
5. sentinel conventions such as `__instance_proto__`, `__promise_idx`,
   `__gen_idx`, `__ta__`, `__ab__`, and `__dv__`.

Each was introduced for a valid local reason. Their combination is the defect:
property lookup, prototype selection, branding, `instanceof`, native-payload
access, tracing, and reflection can disagree about what the object is.

### 1.2 Class stamping mutates the shape carrier after construction

`js_class_stamp()` clones a `TypeMap` before writing its `js_class` byte so a
shared constructor shape is not contaminated. That makes class identity an
after-the-fact mutation of the object-layout carrier. Two objects with the
same fields can unexpectedly split shapes solely because one was stamped
later, while a missed stamp silently changes semantic behavior.

The root cause is temporal: construction publishes an object before its full
semantic class is selected. The fix is to choose a metadata-qualified empty
shape/constructor blueprint before publication, not to make stamping cheaper.

### 1.3 `Map.data` has two incompatible meanings

For an ordinary Map, **D3.4.1/D3.4.5** require `Map.type` to describe the
packed fields in `Map.data`. Several native-backed objects instead store a
`JsProxyData*`, `JsTypedArray*`, `JsArrayBuffer*`, `JsDataView*`, or iterator
payload in `Map.data`. Some later “upgrade” the same object to ordinary shape
storage and preserve the native pointer in a hidden string property.

That is why ordinary lookup must first branch on `map_kind`, why synthetic
iterators use a non-TypeMap sentinel in `Map.type`, and why native-backed
objects need marker strings. A dispatch table alone would conceal this
violation rather than fix it.

The target gives each field one meaning:

```text
Map.type       -> valid TypeMap describing ordinary property storage
Map.data       -> packed bytes described by Map.type
trailing state -> typed engine-internal payload selected by storage kind
VMap payload   -> module/host-native state selected by its vtable
```

### 1.4 Prototype synthesis mixes internal slots and public properties

The current chain walk combines an own `__proto__`-like slot, special JSON
markers, TypedArray backing-kind checks, iterator-kind checks,
`__instance_proto__`, `JsClass`, and the realm intrinsic table.
`js_get_implicit_proto()` therefore reconstructs `[[Prototype]]` from several
conventions on every miss.

ECMAScript has one `[[Prototype]]` internal slot. A public `__proto__`
accessor is only an API that reads or changes it. The target represents an
explicit override once and otherwise selects a default through immutable
metadata; property spelling never participates.

### 1.5 Tune5 intentionally leaves one temporary seam

Tune5 converges property behavior on eight receiver-aware operations but
leaves `js_property_exotic_adapter` as a temporary operation-tagged switch.
This is deliberate sequencing, not the final architecture. Tune6 replaces
that adapter with metadata-selected operations and deletes every other exotic
property switch; it does not change Tune5's key, receiver, descriptor, or
outcome contracts.

---

## 2. Decision ledger

| ID | Decision |
|---|---|
| **JOP1** | **One semantic classification path.** `js_object_meta(Item)` resolves immutable `JsClassMeta`; property/prototype/brand code does not independently interpret `map_kind`, sentinel fields, or diagnostic names. |
| **JOP2** | **Metadata is selected before publication.** Every JS-created Map starts with the correct metadata-qualified TypeMap. There is no post-construction class stamp. |
| **JOP3** | **Metadata is immutable and pointer-carried.** A `TypeMap` carries one `const JsClassMeta*`. Shape transitions and descriptor clones preserve it exactly. |
| **JOP4** | **Layout identity stays structural.** Per **D3.4.2**, class metadata does not alter the ordered field/type layout identity. It does qualify the runtime TypeMap/transition family and cache guard; TypeMaps with different metadata are never the same runtime blueprint even when their field layouts match. |
| **JOP5** | **Metadata has no realm values.** It may contain stable IDs, flags, policies, diagnostic literals, and static function-table pointers, but no `Item`, GC pointer, mutable realm cache, or context-owned address under **D5.4.3**. |
| **JOP6** | **One exotic table.** `JsPropertyOps` is the only JS-core table for exotic property, prototype, and extensibility operations. Plain objects have no table lookup beyond a null pointer test. |
| **JOP7** | **Tune5 remains the semantic owner.** The eight public property operations, `JsPropertyLane`, receiver propagation, key materialization, descriptor rules, and caller-owned throwing policy do not move into metadata. |
| **JOP8** | **Fallthrough is non-observable.** An exotic callback may decline only before allocating, coercing, invoking user code, mutating state, or otherwise becoming observable. After any such action it must return a complete success/failure/error outcome. |
| **JOP9** | **HasOwn is derived.** The public Tune5 `HasOwn` operation uses the exotic `GetOwnPropertyDescriptor` result; the table does not grow a second near-identical own-presence algorithm. |
| **JOP10** | **One prototype resolver.** Explicit override, explicit null, intrinsic default, TypedArray-kind default, and host callback are policies behind one `GetPrototypeOf`/`SetPrototypeOf` core. Public `__proto__` is an ordinary accessor. |
| **JOP11** | **Native payload is not shape data.** Engine exotics use typed trailing storage (or another D2.1.4-approved container extension); VMaps use their vtable payload. `Map.data` never holds a native payload. |
| **JOP12** | **`map_kind` is physical only.** Allocation, tracing, finalization, and checked payload accessors may inspect it. Property, prototype, branding, `instanceof`, coercion, and builtin semantics may not. |
| **JOP13** | **Callability remains per value.** A class ID, metadata pointer, or ops table cannot make an object callable or constructable. Callable Proxies keep explicit per-value capabilities; class constructor Maps are retired in favor of `JsFunction`. |
| **JOP14** | **Brand and `instanceof` are distinct.** Internal-slot brand checks use class/family metadata. JavaScript `instanceof` follows `Symbol.hasInstance` and the prototype chain; class ID is not a substitute, except where an existing formal error-carrier ruling explicitly maps error code to prototype. |
| **JOP15** | **Host authority is not copied.** A single VMap/Jube bridge delegates to declared member records and record-owned hooks; module vtables remain the brand and lifecycle authority under **D7.4.1–D7.4.4**. |
| **JOP16** | **No hidden string protocol.** Internal slots use typed payload fields, capability fields, or NamePool private identities. Prefix/spelling tests do not identify engine state. |
| **JOP17** | **Promise exception is narrowly owned.** Until JR7, a legacy Promise wrapper may retain `__promise_idx` only inside the Promise adapter. Metadata, prototype lookup, generic private-property code, and all other object kinds must not inspect it. |
| **JOP18** | **Deletion is part of the feature.** Tune6 is incomplete while `js_property_exotic_adapter`, class stamping, `js_get_implicit_proto`, `__instance_proto__`, fake TypeMap sentinels, or semantic `map_kind` switches remain. |

---

## 3. Object taxonomy and resolution

### 3.1 Two discriminators, with separate jobs

The final semantic classification has two levels:

| Level | Question answered | Examples |
|---|---|---|
| `TypeId` | Which shared Lambda value representation is this? | `MAP`, `ARRAY`, `ARRAY_NUM`, `FUNC`, `VMAP`, `ERROR`, primitive |
| `JsClassMeta` | Which JavaScript brand, default prototype policy, and exotic internal operations apply? | ordinary Object, Proxy, TypedArray, Arguments, iterator, Error family, host bridge |

`TypeId` is not replaced. It remains the shared data-model discriminator under
**D1.2/D2.1.4**. `JsClassMeta` removes the three additional semantic
discriminators layered above it.

### 3.2 Metadata resolution by representation

`js_object_meta(Item value)` is the sole resolver:

| Representation | Metadata source |
|---|---|
| JS-created `MAP` | `((TypeMap*)map->type)->js_meta`; non-null after publication |
| imported ordinary Lambda/Input `MAP` | the static ordinary/foreign-map metadata returned at the explicit boundary; the Input shape is not mutated |
| ordinary `ARRAY` / admitted `ARRAY_NUM` | static Array metadata selected by TypeId plus Tune5 ordinary-array admission |
| `FUNC` | static Function metadata; produced-instance metadata remains construct-target data, not the function's own class |
| `VMAP` | static host-bridge metadata; exact brand and callbacks come from the existing VMap/Jube type descriptor |
| ERROR-tagged/resting error | metadata selected from the D8.4.3 error code-to-class mapping |
| primitive wrapper access | static Boolean/Number/String/Symbol/BigInt metadata used only by the wrapper/property-reference boundary |

The resolver does not allocate, mutate, intern, invoke script, or consult an
observable property. Its result is stable for the value's lifetime.

### 3.3 Plain and foreign Maps

A newly created JS object uses a realm-owned empty TypeMap blueprint whose
`js_meta` points at ordinary Object metadata. `EmptyMap` remains available to
Lambda and explicit foreign/Input seams; it is not silently stamped when JS
observes it.

At the foreign-map seam, ordinary JS behavior is projected without writing
into the Input-owned TypeMap. If the value is mutated into runtime ownership,
the existing transition/copy path creates a runtime TypeMap with ordinary JS
metadata. This applies **D4.6.2v2**'s explicit Input boundary rather than
inventing a metadata side table.

### 3.4 Arrays and functions

Array class metadata is not stored in the companion Map. Tune5's companion is
an orthogonal named/index-descriptor overlay and may be absent. The array's
`TypeId` plus ordinary-array admission selects the Array metadata; its
`JsElementsKind` remains per container as ruled by JR6 and
**D2.6.1–D2.6.2**.

Every function's own object class is Function. An intrinsic constructor's
produced-instance metadata is carried by its construct capability or factory
descriptor. It must not overload the function's class metadata or mutable
`.name`.

### 3.5 Error carriers

**D8.4.3** already makes the error code ↔ error-class prototype table
authoritative for error identity at rest. Tune6 exposes that existing mapping
through `js_object_meta()`; it does not add a second class byte to the shared
`LambdaError` carrier. Ordinary error overflow properties continue through
the error's Map-compatible property storage.

---

## 4. `JsClassMeta`

### 4.1 Logical shape

The implementation may tune field widths, but the semantic structure is:

```c
typedef struct JsClassMeta {
    uint16_t class_id;
    uint16_t family_id;
    uint32_t flags;
    uint16_t prototype_policy;
    uint16_t intrinsic_prototype_id;
    const struct JsPropertyOps* property_ops;
    const char* diagnostic_name;
} JsClassMeta;
```

The one-word TypeMap cost is `const JsClassMeta* js_meta`, replacing the
current mutable `uint8_t js_class` mechanism. The pointed-to records and ops
tables are immutable process data unless a host descriptor is explicitly
module-owned and pinned for the context lifetime.

### 4.2 Field meanings

- `class_id` names an exact engine brand used by internal-slot validation.
- `family_id` groups brands only where the ECMAScript algorithm deliberately
  accepts a family, such as all TypedArray instances or all Error subclasses.
- `flags` describe stable facts such as ordinary-shape availability,
  expando support, and whether a native trailing payload exists. They cannot
  grant callability or encode mutable object state.
- `prototype_policy` describes how the default `[[Prototype]]` is selected
  when no explicit override exists.
- `intrinsic_prototype_id` is a stable lookup ID, never a realm `Item`.
- `property_ops` is null for ordinary semantics and points to one immutable
  table for an exotic class/family.
- `diagnostic_name` is for logging/inspection only. No executable behavior
  branches on its bytes.

### 4.3 Metadata-qualified shape families

The runtime shape cache/transition graph is rooted by:

```text
(realm/context shape owner, initial JsClassMeta, initial field layout)
```

Adding, deleting, retagging, or changing a descriptor creates or selects a
child TypeMap in the same metadata family. A transition target must satisfy:

```text
child->js_meta == parent->js_meta
```

Changing class metadata is not a shape transition. There is no public or
internal “restamp” operation. Deserialization/factory code that selected the
wrong metadata must rebuild before publication or fail; it may not mutate a
live TypeMap family.

### 4.4 Structural identity and cache identity

**D3.4.2** remains true: the visible field layout is structurally identified
by the ordered name/type sequence. Metadata does not make two equal layouts
nominally different at the language type level.

Runtime execution nevertheless cannot share a TypeMap pointer between
different metadata families, because a pointer guard also proves class/ops
policy. Thus:

```text
language layout identity = ordered field/type structure
runtime TypeMap identity  = layout instance + immutable metadata family
```

This distinction is the adopted **D3.4.7** contract. ICs may guard a TypeMap
pointer; they must not assume two structurally equal TypeMaps have the same
exotic behavior.

---

## 5. Native payload and ordinary-property storage

### 5.1 The invariant

For every published Map that participates in ordinary property storage:

```text
Map.type is null only where explicitly permitted;
otherwise Map.type is a valid TypeMap;
Map.data is null or a packed buffer described exactly by that TypeMap.
```

This is **D3.4.1/D3.4.5**, not a JS-only optimization. A property miss must
never be the recovery behavior for a native pointer disguised as a TypeMap.

### 5.2 Engine exotic layout

An engine-owned ECMAScript exotic may extend `Map` with typed trailing state:

```c
typedef struct JsProxyMap {
    Map base;
    JsProxyData proxy;
} JsProxyMap;
```

TypedArray, ArrayBuffer, DataView, collection, and iterator carriers follow
the same shared shape when a Map representation remains appropriate:

```text
base Map       -> ordinary expando/descriptor shape and data
trailing state -> unobservable typed internal slots
map_kind       -> storage/tracer/finalizer selection only
js_meta        -> semantic class and exotic operations
```

The exact C structs are chosen after the P0 allocation/GC census. The
invariant is fixed: no trailing `Item` may go untraced, and no native pointer
may be round-tripped through a string-keyed property.

### 5.3 VMap host layout

Host/module-native objects remain VMaps under **D7.4.1**. Their vtable owns
tracing, finalization, and exact native brand. The JS property kernel sees one
host-bridge metadata record whose operations delegate to the resolved declared
member records and record-owned hooks.

Tune6 must not copy module callback pointers into per-object TypeMaps, invent a
second host registry, or make JS responsible for module payload lifetime.

### 5.4 Arrays

Ordinary arrays keep Tune5's dense/sparse storage and companion Map. Native
TypedArrays are not ordinary arrays and never enter the elements-kind state
machine. Their exotic Map or VMap carrier owns a typed native payload and
ordinary expando storage as separate fields.

### 5.5 Checked payload accessors

Every native payload family has one accessor that validates, in debug builds:

- expected `TypeId`;
- expected metadata class/family;
- expected physical `map_kind`/allocation class; and
- required payload initialization state.

Release accessors use the proven layout directly. Semantic code calls these
accessors rather than recasting `Map.data` or duplicating class/kind checks.

---

## 6. `JsPropertyOps`

### 6.1 Table surface

The one internal table covers the exotic parts of object internal methods:

```text
Get
Set
DefineOwn
Delete
HasProperty
GetOwnPropertyDescriptor
OwnKeys
GetPrototypeOf
SetPrototypeOf
IsExtensible
PreventExtensions
```

There is no separate `HasOwn` callback: **JOP9** derives it from
`GetOwnPropertyDescriptor`. Call and construct entries are excluded by
**D6.2.2v2/JOP13**. GC trace/finalize callbacks are storage-lifecycle
operations and stay with the GC allocation class or VMap vtable.

Tune5's first seven names above plus derived `HasOwn` remain its exact eight
public property symbols. Prototype and extensibility APIs are existing object
operations; adding them to the internal table does not add a property ABI.

### 6.2 Callback disposition

Each property callback returns both:

```text
FALLTHROUGH  -> enter the shared ordinary tier
COMPLETE     -> completion contains success, false, value, or ERROR
```

The representation may be a small C struct or a boolean plus out-parameter;
it is internal and never enters MIR. `FALLTHROUGH` is legal only before any
observable action. A callback that has called a getter, Proxy trap, coercion,
host hook, or allocation-capable operation must return `COMPLETE`, including
when the observed result is “not found”.

This rule prevents the core from repeating a trap or continuing with stale
state after re-entry.

### 6.3 Key contract

Callbacks receive Tune5's canonical inputs:

- one valid `JsPropertyLane` (`NameId` or ordinary `uint32_t` index);
- an optional rooted observable key Item;
- the original receiver for `Get` and `Set`; and
- operation-specific value/descriptor operands.

The lane is the only lookup identity under **D4.6.1v2**. A callback may
materialize the key once when ECMAScript requires it for a Proxy trap,
reflection, TypedArray `CanonicalNumericIndexString`, or a host callback.
Materialization does not create a second identity.

### 6.4 Rooting and re-entry

The semantic core owns a `RootFrame` covering target, receiver, observable
key, value, descriptor components, and the current prototype before calling
an operation that may allocate or re-enter. Callback arguments are borrowed
from that rooted frame. A callback that retains an Item in native payload or
queues work must publish it through an existing traced owner before returning.

After a callback/re-entry boundary, code reloads payload pointers, shape,
prototype policy, extensibility, and any Tune5 elements/prototype epoch facts.
No raw pointer or cached `JsClassMeta*` derived from a movable/unpublished
object is used as proof across the boundary. Static metadata pointers
themselves remain stable; the receiver's mutable state does not.

### 6.5 Ordinary fallback ownership

The ops table does not reimplement:

- ordinary shape lookup or slot reads;
- accessor invocation rules shared with ordinary objects;
- receiver propagation through a normal prototype chain;
- ordinary descriptor validation;
- ordinary key ordering;
- array elements-kind transitions; or
- strict/sloppy/Object/Reflect throwing policy.

An exotic callback either handles the spec-specific difference or falls
through to those shared tiers. At the third similar class-specific branch,
the shared operation is extracted rather than copied.

---

## 7. Prototype model

### 7.1 One internal concept

`[[Prototype]]` has two representations:

1. an explicit per-object override, including explicit null; or
2. an immutable metadata policy that resolves a realm-local default.

An explicit override always wins. The public `Object.prototype.__proto__`
getter/setter and `Object.getPrototypeOf`/`Object.setPrototypeOf` delegate to
the same internal operations; an own public property named `__proto__` has no
special storage meaning.

### 7.2 Prototype policies

The fixed policies are:

| Policy | Meaning |
|---|---|
| `NULL` | intrinsic top-of-chain object; default is null |
| `INTRINSIC` | resolve `intrinsic_prototype_id` in the owning/current realm |
| `TYPED_ARRAY_KIND` | resolve the per-element-kind TypedArray prototype from typed payload |
| `HOST` | delegate to the VMap/Jube host prototype callback |
| `EXOTIC` | use the class table's `GetPrototypeOf` callback, e.g. Proxy |

Metadata stores only the policy and stable ID. Realm-owned prototype Items
stay in the existing rooted runtime state under **D5.4.3**.

### 7.3 Explicit override storage

Ordinary Maps and functions use the existing NamePool-private internal
prototype slot, never the public spelling. Arrays use the Tune5 companion only
when an explicit override is needed. Engine exotic subclasses use ordinary
base-Map property storage for the private slot. VMaps delegate storage to
their host/object contract.

The representation must distinguish:

- no override (use policy);
- explicit null (end chain); and
- explicit object/function prototype.

No JSON marker or deleted public `__proto__` slot may stand in for those
states.

### 7.4 Mutation and caches

`SetPrototypeOf` validates object/null, cycle, extensibility, and Proxy/host
rules before committing the override. A successful change enters the existing
intrinsic mutation/version machinery used by Tune5's array-prototype clean
guard. Failed and no-op changes do not publish partial state.

Any prototype cache or IC fact is guarded by the relevant TypeMap/metadata and
realm mutation version. Metadata may select policy but may not contain a
mutable cache.

### 7.5 Class constructors

`__instance_proto__` currently identifies legacy callable class Maps. Tune6
retires that representation:

- a class constructor is a `JsFunction` with explicit call/construct entries
  under **D6.2.2v2**;
- its public `.prototype` is an ordinary function property;
- construction obtains the instance prototype through the existing
  `GetPrototypeFromConstructor` path; and
- produced-instance metadata is explicit construct-target data, never
  inferred from `.name`, `__instance_proto__`, or the constructor's own class.

This closes the named Tune4 compatibility seam without moving callability
into `JsClassMeta`.

---

## 8. Class, brand, and reflection rules

### 8.1 Exact class versus family

Exact class IDs distinguish brands whose internal slots are incompatible.
Family IDs are used only by algorithms that intentionally accept a family.
Examples:

- all concrete TypedArrays share a TypedArray family but retain an element
  kind in native payload;
- Error subclasses share an Error family but map to distinct prototypes;
- Map and Set do not share a family merely because their payload structs are
  similar; and
- Array and Arguments remain distinct despite indexed storage.

### 8.2 Internal brand checks

Builtin receiver checks call one helper such as
`js_require_class_family(value, JS_FAMILY_TYPED_ARRAY)`. The helper resolves
metadata and reports an in-band TypeError on mismatch under **D8.4.3**.
Individual builtins do not combine TypeId, `map_kind`, sentinel properties,
and class-name bytes.

### 8.3 `instanceof`

Ordinary `instanceof` remains:

```text
GetMethod(C, @@hasInstance)
  -> custom method when present
  -> OrdinaryHasInstance
       -> callable/constructable checks
       -> C.prototype
       -> prototype-chain walk
```

Metadata accelerates internal representation checks but does not replace the
chain. Cross-realm objects with the same class ID therefore do not become
instances of each other's constructors by ID alone.

### 8.4 `Object.prototype.toString`

The algorithm first observes `Symbol.toStringTag`. Only the default tag uses
metadata's diagnostic/class mapping. Diagnostic spelling is never used to
select executable behavior.

---

## 9. Exotic families

### 9.1 Proxy

Proxy metadata selects a complete Proxy ops table. Each operation:

- checks revocation;
- resolves the exact trap once;
- calls it with rooted target/handler/key/receiver operands;
- validates the corresponding invariant against the target; and
- returns one completion/error lane.

Proxy `GetPrototypeOf`, `SetPrototypeOf`, `IsExtensible`, and
`PreventExtensions` move behind the same table. Callable/constructable flags
remain in the per-Proxy payload and enter Tune4's callable resolver; the
metadata does not make every Proxy callable.

### 9.2 TypedArray, ArrayBuffer, and DataView

TypedArray ops own only integer-index exotic behavior and per-kind prototype
selection. Canonical numeric classification remains distinct from Tune5's
ordinary array-index classifier. Named expandos fall through to the base
Map's ordinary shape storage without converting or replacing the native
payload.

ArrayBuffer and DataView use ordinary property semantics plus typed internal
slots and prototype policy unless a spec operation requires an override.
Their native payloads move to trailing storage; `__ab__`, `__dv__`, `__ta__`
and pointer-valued shape entries are deleted.

### 9.3 Arguments

Mapped and unmapped Arguments metadata selects the Arguments ops table.
Parameter-map aliasing is an internal payload rule; indexed operations that
are not aliased fall through to Tune5 array/ordinary storage. Deleting or
redefining an aliased index updates the parameter map transactionally before
returning a complete result.

Arguments arrays remain excluded from ordinary `JsElementsKind`, as fixed by
JR6.

### 9.4 String exotic objects and primitive property references

String index/length synthesis is one class operation shared by boxed String
objects and the primitive property-reference adapter. It does not turn
primitive values into persistent wrapper Maps. Non-index properties continue
through the ordinary prototype path with the original receiver.

### 9.5 Iterators and collections

Synthetic iterators no longer store a one-byte sentinel in `Map.type`.
Iterator kind/index/source live in typed trailing state; the base Map has a
valid metadata-qualified TypeMap and can hold expandos normally.

Map/Set/WeakMap/WeakSet collection data remains in typed trailing storage,
following the existing `JsCollectionMap` direction. Property behavior and
branding resolve from metadata; `map_kind` is used only by allocator/tracer
payload access.

### 9.6 Host VMaps and DOM

Jube VMaps route through one host bridge. DOM wrappers that are already VMaps
use that bridge directly. Any remaining DOM `MapKind` special case is
classified during Tune6 P0:

- module/native ownership -> migrate to VMap per **D7.4.1**; or
- ECMAScript engine-owned exotic -> give it an engine `JsClassMeta` and typed
  trailing storage.

There is no third permanent DOM-only property-dispatch route.

### 9.7 Process and platform exotics

`process.env`, CSS namespace objects, canvas/property interceptors, and other
platform-specific cases receive a named ops owner or become ordinary/host
objects. Their environment/event side effects occur inside the selected
callback and therefore return `COMPLETE`; a callback may not perform the side
effect and then fall through to a second write.

---

## 10. Sentinel retirement

### 10.1 Tune6-owned removals

Tune6 removes all generic use of:

| Sentinel/convention | Replacement |
|---|---|
| `__class_name__` | `JsClassMeta.class_id` / diagnostic name |
| `TypeMap.js_class` + `js_class_stamp` | immutable `TypeMap.js_meta` chosen at construction |
| `__instance_proto__` | `JsFunction` construct capability + ordinary `.prototype` |
| `__json_own_proto__` | explicit internal prototype override state |
| `__ta__`, `__ab__`, `__dv__` | typed trailing native payload |
| fake iterator `Map.type` markers | real metadata-qualified TypeMap + trailing iterator state |
| raw `__brand_*` prefix recognition | NamePool private key kind/ID |
| generic `__gen_idx` / `__rd` recognition | typed owner payload or narrowly owned subsystem adapter |

The P0 census may find more strings. Each must receive a typed replacement
and deletion owner before its phase starts.

### 10.2 Promise exception

JR7, not Tune6, owns the Promise representation replacement. Tune6 therefore
allows exactly one temporary exception:

- the legacy Promise adapter may read/write `__promise_idx` to reach the
  current static Promise table;
- Promise class/prototype/property selection uses metadata, not that field;
- no generic private-property predicate recognizes the spelling; and
- source ratchets restrict references to the Promise implementation and its
  focused migration test.

JR7 deletes the wrapper, sentinel, static 8,192-record table, fixed reaction
arrays, and root-registration storm together.

---

## 11. Construction, transition, and publication

### 11.1 Factory contract

Every JS object factory identifies before allocation/publication:

- representation (`TypeId` and physical storage class);
- immutable `JsClassMeta`;
- initial TypeMap/shape blueprint where applicable;
- default prototype policy/intrinsic ID;
- typed native payload size and tracing owner; and
- call/construct capability when the result is callable.

Factories may share an implementation but not omit this classification and
repair it later.

### 11.2 Transactional publication

Construction roots all operands and allocates/initializes:

1. the carrier and metadata-qualified shape;
2. typed payload and traced Item fields;
3. explicit prototype override, if the default policy is insufficient; and
4. initial ordinary properties/descriptors.

Only then is the Item published to user code, a realm cache, another object,
or a job queue. On failure, no partially initialized carrier becomes visible.

### 11.3 Shape transitions

All ordinary property and descriptor changes use the shared transition
machinery. Transition lookup is keyed from the current TypeMap, so metadata
inheritance is automatic and debug-asserted. A descriptor clone copies the
metadata pointer unchanged. No transition may change physical payload kind.

### 11.4 Prototype changes do not restamp class

Changing `[[Prototype]]`, subclassing, or replacing a constructor's public
`.prototype` does not change the instance's internal brand metadata. This is
required for builtins such as TypedArray and Date: their internal-slot brand
persists even with a custom prototype.

---

## 12. Cache and compiler boundary

### 12.1 MIR ABI

MIR continues to call Tune5's eight property operations with a
`JsPropertyLane` and optional observable key. It never bakes a
`JsClassMeta*`, `JsPropertyOps*`, TypeMap pointer, host descriptor pointer, or
realm prototype Item into shared code under **D5.4.3**.

### 12.2 Existing ICs

Until JR8, an existing load/store IC may guard an ordinary data-property hit
using TypeMap identity. A hit is legal only when the guarded TypeMap proves:

- the same metadata family;
- no exotic callback is required for that operation/key;
- the same slot/descriptor facts; and
- any prototype version fact still matches.

All other cases miss without observable work into the Tune5 core. The core
and `JsPropertyOps` signatures contain no IC/feedback parameter.

### 12.3 JR8 handoff

JR8 replaces outer IC structs with per-function feedback slots. It consumes
the same TypeMap/metadata facts and cannot change the semantic or ops-table
ABIs.

---

## 13. Error, GC, threading, and lifetime contracts

### 13.1 Errors

Every fallible operation returns an ordinary result/status or an ERROR-tagged
`LambdaError*` under **D8.4.3**. There is no pending exception flag, throw
side channel, or ops-table-local error slot.

### 13.2 GC

- Static metadata and function tables contain no Items and require no tracing.
- TypeMaps remain pool/context-owned non-GC metadata.
- Typed trailing payload Item fields are traced by the carrier's existing
  allocation/storage tracer.
- VMap payload tracing remains its vtable's responsibility.
- Ordinary shape data is traced from the TypeMap exactly as required by
  **D3.4.1/D4.3.3**.
- No native pointer is stored as an Item or ordinary property.

Forced-GC tests cover every exotic constructor, expando transition, callback
re-entry, prototype override, Proxy trap, and payload finalizer.

### 13.3 Threading and context

Metadata/ops tables are immutable and may be shared. TypeMaps, prototypes,
shape transitions, and host objects keep their existing context/thread owner
under **D5.4.1–D5.4.4**. Tune6 adds no lock, atomic reference protocol,
process-global mutable registry, or cross-context pointer cache.

### 13.4 Module lifetime

The host bridge resolves callbacks through a context-retained Jube type
descriptor whose DSO remains pinned for every live VMap. A core TypeMap never
stores a raw callback from an unloadable module. If the current host lifecycle
cannot prove this, Tune6 stops at the bridge boundary and fixes the Jube owner;
it does not pin modules through leaked JS metadata.

---

## 14. Formal-spec impact

### 14.1 Adopted D3.4.7 extension

The formal design adds the following ruling after **D3.4.6**:

> A runtime JavaScript `TypeMap` may carry one immutable `JsClassMeta*` that
> selects class/brand, realm-relative default-prototype policy, and an optional
> static exotic-operations table. The pointer is chosen before object
> publication and preserved by every shape/descriptor transition. It contains
> no realm Item or mutable context state. Metadata is orthogonal to D3.4.2's
> structural field-layout identity but qualifies runtime TypeMap/transition and
> cache identity; TypeMaps with different metadata cannot be the same runtime
> blueprint. `map_kind` remains physical storage information and cannot select
> JS property/prototype semantics.

The formal spec is version 1.15.0. Tune6 applies this additive ruling and does
not alter the existing structural D3.4 rulings.

### 14.2 Existing rulings applied without change

- **D2.1.4** permits typed extensions of shared container representations;
  Tune6 uses that layer instead of new TypeIds.
- **D3.4.1/D3.4.5** require `Map.type` and `Map.data` to agree; native payload
  separation enforces the existing invariant.
- **D4.6.1v2–D4.6.2v2** remain the sole property-name identity authority.
- **D5.3** governs roots across every operation/callback boundary.
- **D5.4.3** forbids realm prototype Items and metadata pointers in baked MIR.
- **D6.2.2v2** keeps call/construct capability per callable value.
- **D7.4.1** keeps native host objects on VMap and ECMAScript exotics in the
  engine-owned object layer.
- **D8.4.3** remains the one completion/error channel.

---

## 15. Interaction with adjacent redesign phases

### 15.1 Tune5 / JR6 input contract

Tune6 begins only after Tune5 has:

- exactly eight public semantic property operations;
- one canonical `JsPropertyLane`;
- explicit receiver and outcome handling;
- one temporary `js_property_exotic_adapter`;
- ordinary array elements kinds and descriptor overlay; and
- no semantic branches inside retained IC wrappers.

Tune6 replaces the adapter implementation and then its symbol. It does not
reopen any of those decisions.

### 15.2 Tune4 / JR5 callable handoff

Tune6 deletes the remaining legacy class-map callable bridge and
`__instance_proto__`, converting class constructors to the existing
`JsFunction` call/construct capability. It does not add callable entries to
metadata.

### 15.3 JR7 Promise handoff

Tune6 gives the legacy Promise wrapper stable Promise metadata and confines
its state-index sentinel. JR7 then replaces the wrapper and state table with
one GC-heap native Promise VMap without changing property/prototype dispatch.

### 15.4 JR8 feedback handoff

Tune6 exposes stable TypeMap + metadata facts at the outer cache boundary.
JR8 stores those facts in unified feedback slots; it does not add callbacks to
the semantic core.

### 15.5 JR10 decomposition

Tune6 may extract a small coherent metadata/ops owner when required, but it
does not mechanically split the remaining `js_runtime.cpp`. JR10 performs the
broad file decomposition after JR4/JR6/JR7/JR8 deletions so code moves once.

---

## 16. Rejected alternatives

| Alternative | Why rejected |
|---|---|
| Keep `js_class` byte and add an ops table beside it | Preserves post-publication stamping and two metadata mechanisms. |
| Put `JsClassMeta` directly on every Map | Adds per-object size/cost and duplicates immutable data that belongs to shared shapes. |
| Put array elements kind in metadata | Elements representation is mutable per array; JR6 correctly stores it per container. |
| Put call/construct entries in class metadata | Violates **D6.2.2v2** and makes all values of a class share callability. |
| Let `map_kind` index the ops table | Keeps physical storage and semantic behavior coupled; non-Map/VMap/array cases immediately require parallel dispatch again. |
| Keep native payload in `Map.data` and redirect all property access to a side Map | Violates **D3.4.1/D3.4.5**, retains dual property storage, and forces every ordinary access through exotic code. |
| Store native pointers in hidden string properties after “upgrade” | Makes internal slots forgeable/reflectable, burdens shapes and GC, and retains sentinel protocols. |
| Store realm prototype Items in static metadata | Violates **D5.4.3**, leaks/reset-crosses realms, and creates hidden roots. |
| Give every exotic operation a mandatory full implementation | Duplicates ordinary descriptor/prototype algorithms across types. Explicit non-observable fallthrough is the smaller correct seam. |
| Allow arbitrary callback fallthrough after work | Can double-run traps/side effects and continue with stale receiver/shape state. |
| Convert all engine exotics to new TypeIds | Spends shared tag budget and violates the extend-in-place direction of **D1.2/D2.1.4**. |
| Patch C2MIR for the new layout | Forbidden by **D1.6** and repository rule 14. |

---

## 17. Design acceptance checklist

- [x] Formal D3.4 metadata ruling adopted and spec semver bumped.
- [x] `JsClassMeta` has no mutable/realm/GC fields.
- [x] Every JS-created Map chooses metadata before publication.
- [x] Shape and descriptor transitions preserve metadata exactly.
- [x] `map_kind` is documented and enforced as storage-only.
- [x] `Map.data` always agrees with `Map.type`.
- [x] One `JsPropertyOps` table surface covers exotic property/prototype/
      extensibility behavior.
- [ ] Tune5's eight public semantic operations are unchanged.
- [ ] Callback fallthrough is explicitly non-observable.
- [ ] `HasOwn` derives from the own-descriptor operation.
- [ ] One prototype resolver owns defaults and explicit overrides.
- [ ] Class constructors are `JsFunction` values, not callable Maps.
- [ ] Brand checks and `instanceof` are not conflated.
- [ ] Host VMaps retain Jube/vtable authority.
- [ ] Promise sentinel ownership is confined to JR7.
- [ ] All other class/prototype/native sentinel conventions have typed
      replacements and deletion owners.
- [ ] Metadata, payload, prototype, and callback GC tests cover forced
      collection and re-entry.
- [ ] JR7 and JR8 can consume the final seams without modifying object/property
      semantics.

The central invariant is simple: **an object's semantic class is immutable
metadata, its fields are the shape, its native state is typed storage, and its
behavior enters one property kernel. None of those roles is inferred from a
string or borrowed from another role.**
