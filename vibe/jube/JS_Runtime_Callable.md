# LambdaJS Callable Runtime — Direct Per-Callee Call and Construct

**Date**: 2026-08-11

**Status**: IMPLEMENTED — formal ruling D6.2.2v2

**Tree anchor**: master `88aa5556c8` plus the Tune4 implementation worktree;
the reproducible C0 snapshot and C8 evidence are recorded in
`JS_Tune4_Callable.md`

**Companions**: `JS_Runtime_Redesign.md` (JR1/JR5/JR6),
`JS_Runtime_Name.md` (RN1–RN16), `JS_Tune2_Exception.md` (JR3.3–JR3.9),
`doc/dev/js/JS_05_Functions_Closures.md`, and
`doc/dev/js/JS_07_Classes.md`

Decisions introduced here use **JC#** ledger ids only where the formal design
does not already rule the point. Existing formal rulings are cited first, per
repository rule 17. The design is implemented; D6.2.2v2 was adopted in
`doc/Lambda_Formal_Design.md` before the callable ABI landed.

---

## 0. Outcome and non-negotiable exits

LambdaJS should have one answer to “what happens when this value is invoked?”:
the value supplies a per-callee entry. A JavaScript builtin must not be a
function-shaped token whose `builtin_id` is interpreted by a second runtime
dispatcher, and a constructor must not be selected by comparing the mutable
observable `.name` string.

The target keeps the ECMAScript distinction between `[[Call]]` and
`[[Construct]]` while giving both the same runtime shape:

```text
Call(callee, this, args)
    -> resolve callable capability
    -> callee call entry
    -> compiled body or native body
    -> one merged Item result/error lane

Construct(callee, args, newTarget)
    -> resolve constructable capability
    -> callee construct entry
    -> user or native construction body
    -> one merged Item result/error lane
```

The migration is not complete until all of these exits hold:

| Area | Required exit |
|---|---|
| Call dispatch | Every `LMD_TYPE_FUNC` has a non-null call entry selected when the function is created or finalized. No call executes semantics by switching on `builtin_id`. |
| Construct dispatch | Constructability is an explicit construct entry/capability. No constructor is selected by `String*` spelling, `strncmp`, `special_ctor_kind`, or a mutable `.name`. |
| Builtin installation | Standard methods/accessors are real realm-local function values installed as real properties. The catalog is consulted at realm construction and by compiler metadata, not as a property-miss oracle during ordinary execution. |
| Call versus construct | `[[Call]]` and `[[Construct]]` remain distinct entries. A value may support either or both; `new.target` is passed explicitly into construct dispatch. |
| Activation state | The active `new.target` binding may remain context state while a body executes, but the `js_pending_new_target` / `js_has_pending_new_target` one-shot handoff is deleted. |
| Native ABI | Native targets are held through typed function-pointer forms. `void* + param_count + cast at call time` is not the final native-call contract. |
| Property semantics | A source method call observes `Get` followed by `Call`. Name/receiver-type lowering alone may not bypass a replacement, deletion, accessor, Proxy trap, or extracted function value. |
| Ownership | Caller-donated scalar homes and precise argument roots remain intact under D5.2.1 and D5.3.3–D5.3.5. Ownership-qualified adapters all enter the same call/construct kernels. |
| Errors | All fallible entries return the D8.4.3 merged `Item` error lane. No pending exception channel or per-call poll is reintroduced. |
| Retirement | `js_dispatch_builtin`, public method-name dispatch helpers, constructor-name dispatch, and the old pending-`new.target` protocol are removed rather than retained as fallbacks. |

This is the next runtime mechanism after exceptions and names because those
two migrations remove its two former excuses: entries can now return one
in-band `Item` outcome (D8.4.3), and properties/catalog bindings can now use
one NameId identity (D4.6 / RN1–RN16).

## 1. Current mechanism and root cause

### 1.1 Current measured shape

The tree at the anchor contains:

- 374 `JS_BUILTIN_ID` rows, 395 `JS_BUILTIN_METHOD` bindings, and 92
  `JS_BUILTIN_GLOBAL` rows in `lambda/js/js_builtin_catalog.def`;
- 337 `case JS_BUILTIN_*` labels in `js_runtime.cpp`;
- a roughly 2,300-line `js_dispatch_builtin` body;
- a roughly 600-line `js_new_from_class_object` dynamic-construction body;
- public receiver/name routes including `js_string_method`,
  `js_number_method`, `js_map_method`, `js_array_method`, and
  `js_array_method_direct`;
- 103 references to the pending-`new.target` pair under `lambda/js`; and
- 340 `builtin_id` references under `lambda/js`.

These are census facts, not individual defects. The defect is the relation
between them: a function object does not fully describe how it executes.

### 1.2 Builtins are currently interpreted tokens

`JsFunction` already contains `JsCallEntry invoke`, and ordinary compiled
functions are finalized onto a per-callee entry. This is the mechanism ruled
by **D6.2.2**.

Builtin creation bypasses that mechanism. `js_get_or_create_builtin` stores:

```text
func_ptr = NULL
builtin_id = JS_BUILTIN_...
```

The generic caller then recognizes `builtin_id > 0` and sends the ID to
`js_dispatch_builtin`. The dispatcher selects a group, reconstructs method
names in several branches, and may enter a receiver-specific method switch.
The function value is therefore a token for an interpreter living beside
`fn->invoke`.

That has four consequences:

1. the catalog ID becomes execution identity even though it should be static
   metadata;
2. method extraction, binding, receiver validation, and direct lowering need
   builtin-specific exceptions;
3. catalog lookup and property lookup stay coupled; and
4. a future call feedback slot cannot identify one executable target without
   rediscovering the ID/group path.

### 1.3 Dynamic construction is selected by observable spelling

`js_new_from_class_object` receives a function value, reads `fn->name`, strips
a `"bound "` prefix, compares the remaining bytes against intrinsic names,
and then selects constructors such as Array, Date, Error, Promise, Map,
TypedArray, and DOM classes. An intrinsic-identity guard prevents arbitrary
spoofing, but the selection mechanism is still backwards: behavior is
reconstructed from a user-visible label after the function was created.

The same pattern produced `special_ctor_kind` / `special_ctor_name` in the
call dispatcher. Caching a spelling classification makes the repeated work
cheaper; it does not make spelling a valid executable identity.

Observable `.name` must remain mutable/configurable according to each
function's descriptor. Changing it must never change `[[Call]]`,
`[[Construct]]`, receiver branding, or intrinsic allocation.

### 1.4 Call and construct state cross through a one-shot side channel

Dynamic construction currently writes `js_pending_new_target` and
`js_has_pending_new_target`, then calls the ordinary function path. The callee
conditionally consumes the pair and installs the active `js_new_target`.
Many early returns clear the pair manually.

The active `new.target` binding is legitimate execution state. The pending
pair is not: it is a second call-result-like protocol whose correctness
depends on every path consuming or clearing it exactly once. The code already
contains root-cause comments for stale pending state leaking into a later
construction. Passing `newTarget` through the construct entry removes that
bug class structurally.

### 1.5 Property calls still select helpers by name and receiver category

Computed member calls mostly follow the correct shape:

```text
evaluate receiver -> ToPropertyKey -> Get -> evaluate arguments -> Call
```

Static member calls still contain receiver/name routes to
`js_string_method`, `js_number_method`, and `js_map_method`. Those routes
need growing exception lists because a property spelling does not prove which
function value will be found. The existing comments for `.bind`, `.test`,
`.exec`, and Date-named methods document exactly this failure mode.

The root cause is not a missing special case. It is treating `obj.method()`
as one fused name-dispatch operation instead of the ECMAScript `Get` plus
`Call` sequence.

## 2. Scope and non-goals

### 2.1 In scope

- Per-callee call and construct capabilities for `JsFunction`.
- Direct native builtin targets sourced by the existing builtin catalog.
- Explicit `newTarget` plumbing for dynamic `new`, `Reflect.construct`,
  `super`, and bound construction.
- Typed native target storage and explicit native/MIR function factories.
- Realm-local builtin identity, aliases, descriptors, and prototype/global
  installation.
- Removal of name/ID semantic dispatch and method-name call lowering.
- The interaction with bound functions, Proxies, accessors, classes, host
  callbacks, scalar homes, rooting, exception effects, NameId, and later
  feedback vectors.

### 2.2 Out of scope

- Redesigning the JavaScript object/property storage path; JR6 follows this
  work and consumes the simpler `Get -> Call` boundary.
- Folding `JsClass`, `map_kind`, sentinel properties, and exotic behavior into
  shape metadata; JR4 remains the owner. This document defines the callable
  ops contract that JR4 will attach to exotic class metadata.
- Moving promises to VMap/GC-native storage; JR7 follows the object/class
  work.
- Adding feedback vectors or speculative direct-call optimization; JR8 owns
  that. This design leaves a single implemented target for JR8 to cache.
- Changing MIR Direct, `Item`, the GC algorithm, or scalar-home ABI.
- Converting legacy C2MIR. It is frozen by repository rule 14.
- Reorganizing all of `js_runtime.cpp` before obsolete dispatch code is
  deleted. JR10 remains deletion-first, move-once.

### 2.3 Transitional exotic values

Today callable Proxies and class constructor maps are not represented as a
plain `LMD_TYPE_FUNC`. This phase must not force the whole JR4 object-metadata
migration into the callable work.

The public resolver may therefore have three representation tiers:

1. `LMD_TYPE_FUNC` -> `JsFunction` call/construct entries;
2. Proxy/exotic value -> its existing exotic call/construct operation; and
3. the legacy class-map representation -> one named transitional class
   construct entry.

This is TypeId/class dispatch over genuinely different representations, not
semantic selection by name or builtin ID. JR4 later moves tiers 2–3 behind the
shape-carried exotic ops table without changing the call/construct ABI.

An ordinary Map with a property named `call` is not callable. The current
compatibility path that invokes `obj.call` when `obj` itself is called is not
ECMAScript behavior and must be removed with a focused library regression.
Libraries that intend a method call already evaluate the `call` property.

## 3. Governing formal rulings

| Formal ruling | Consequence here |
|---|---|
| **D4.6.1–D4.6.2** | Builtin property bindings resolve through the one NamePool/NameId authority. Runtime dispatch does not reclassify names. |
| **D5.2.1–D5.2.2** | A call or construct entry receives/forwards the caller-donated scalar result home. A wrapper may not return a pointer into its own restored number extent. |
| **D5.3.1–D5.3.5** | Arguments are caller-rooted borrows; native boundary adapters establish one exact rooted span where required. Callees do not invent conservative scans or permanent per-call roots. |
| **D5.4.1–D5.4.4** | Callable objects and alias caches are context/realm-owned. Immutable catalog tables may be global; no context-dependent function object or IC address is baked into shared MIR. |
| **D6.2.1** | A function value owns its executable entries and closure environment. A builtin is therefore a function value with entries, not an ID requiring external interpretation. |
| **D6.2.2v2** | Dynamic calls and construction dispatch through distinct per-callee executable entries; `newTarget` is explicit. Ownership adapters funnel into those entries rather than becoming competing mechanisms. |
| **D8.4.2** | `Item* + argc` remains the JavaScript dynamic boundary. Core Lambda direct-call ABI is not replaced by the JS convention. |
| **D8.4.3** | Every fallible callable returns success or an ERROR-tagged `Item`; call/construct entries never set a pending exception flag. |

No S-layer behavior changes. This design corrects the implementation of
ECMAScript `[[Call]]`, `[[Construct]]`, property access, bound functions, and
`new.target`; it does not define new language semantics.

## 4. Decision ledger

| ID | Decision |
|---|---|
| **JC1** | `JsFunction` behavior is selected by entry pointers fixed at creation/finalization. `builtin_id`, display name, formal length, and catalog group are metadata, never executable identity. A declared native ABI may select its typed adapter once at function creation. |
| **JC2** | Preserve two semantic capabilities: `call_entry` and `construct_entry`. Do not encode construction as a call plus a magic/sentinel `newTarget`. |
| **JC3** | `newTarget` is an explicit construct operand. The construct entry installs it as active state only for the dynamic extent of the invoked body; no pending one-shot handoff survives. |
| **JC4** | Builtin bodies use a typed native target. Fixed-arity host adapters may have several entry functions, but the selected adapter is stored per callee; no runtime arity or semantic mega-switch selects the body. |
| **JC5** | The builtin catalog remains the single declarative source for bindings and compiler metadata, and gains direct target references. A new parallel registry is forbidden. |
| **JC6** | A builtin binding is distinct from a builtin semantic target. Bindings own property name/attributes/realm identity; targets own call/construct behavior and effect metadata. |
| **JC7** | Builtin function identity is realm-local and binding-driven. Two properties share one function object only when the catalog declares a spec-required alias identity. Sharing the same native body does not imply `===`. |
| **JC8** | Ordinary source method calls always observe `Get` then `Call`. A compiler direct call is legal only after proving the exact callee identity and retaining a correct fallback for mutation/Proxy/accessor cases. |
| **JC9** | `IsCallable` and `IsConstructor` query capabilities, not names or broad flags. A class-call rejection entry may deliberately throw, while its construct entry remains valid. |
| **JC10** | Function.prototype is represented by a real callable function value. Ordinary Maps are never made callable by sentinel properties or an own `.call` property. |
| **JC11** | Phase-local compatibility adapters are allowed only while their corresponding old switch cases are deleted in the same phase. The completed runtime contains no legacy semantic fallback. |
| **JC12** | This phase is net-negative in mechanisms and source size. Moving switch bodies to another file without replacing the dispatch model does not count as implementation. |

## 5. Target callable representation

### 5.1 Two levels: invocation protocol and semantic body

The existing `JsCallEntry` is the invocation-protocol entry. It owns facts
such as argument rooting, stack-depth checks, active `this`, module/global
switching, home-class installation, and scalar-home forwarding. Different
function shapes may select different protocol entries under D6.2.2.

Native semantic bodies should use explicit typed forms. Directional types:

```cpp
typedef Item (*JsCallEntry)(Item callee, Item this_value,
                            Item* args, int argc,
                            uint64_t* result_home,
                            bool args_prerooted);

typedef Item (*JsConstructEntry)(Item callee,
                                 Item* args, int argc,
                                 Item new_target,
                                 uint64_t* result_home,
                                 bool args_prerooted);

typedef Item (*JsNativeCallBody)(Item callee, Item this_value,
                                 Item* args, int argc,
                                 uint64_t* result_home);

typedef Item (*JsNativeConstructBody)(Item callee,
                                      Item* args, int argc,
                                      Item new_target,
                                      uint64_t* result_home);
```

The exact field order is an implementation-phase ABI decision after measuring
`sizeof(JsFunction)` against its GC size class. The semantic direction is:

```cpp
struct JsFunction {
    // existing Lambda/JS value, closure, property, and realm fields
    JsCallEntry invoke;
    JsConstructEntry construct;
    JsNativeCallBody native_call;
    JsNativeConstructBody native_construct;
    uint32_t catalog_id;       // diagnostics/link metadata only
    uint8_t call_lane_kind;    // diagnostics/protocol classification only
};
```

For a compiled ordinary function, `invoke` selects the existing generic or
specialized MIR protocol and `native_call` is null. For a native builtin,
`invoke` selects `js_call_entry_native` and `native_call` points directly to
the builtin body. For a dynamically-created fixed-arity host callback,
`invoke` selects the corresponding typed adapter entry; it does not enter a
semantic ID switch.

`catalog_id` may survive for diagnostics, catalog fingerprints, MIR metadata,
or profile labels. An assertion/census forbids reading it to choose runtime
semantics.

### 5.2 Call and construct capability matrix

| Function kind | Call entry | Construct entry |
|---|---|---|
| Ordinary user function | compiled call entry | ordinary user construct entry |
| Arrow | compiled call entry | null |
| Method | compiled call entry | null |
| Generator / async function | generator/async call entry | null |
| Class constructor | class-call rejection entry | user-class construct entry |
| Ordinary native callback | native adapter entry | null unless explicitly registered |
| Callable + constructable intrinsic (`Object`, `Array`, `Date`, `RegExp`, Error classes, etc.) | intrinsic call entry | intrinsic construct entry |
| Call-only intrinsic (`Symbol`, `BigInt`, `parseInt`, prototype methods, etc.) | intrinsic call entry | null |
| Construct-only intrinsic (`Promise`, `Map`, `Set`, TypedArray classes, etc.) | constructor-call rejection entry | intrinsic construct entry |
| Bound function | bound call entry | bound construct entry iff target is constructable |
| Function.prototype | native return-undefined call entry | null |

The matrix is encoded when the value is built. The repeated call path does not
re-derive it from flags. Flags may remain as source/reflection facts where
needed, but `IsConstructor` is not a list of forbidden flags once the construct
entry exists.

### 5.3 One call kernel, ownership-qualified adapters

The following are not separate semantic mechanisms:

- a native C caller that needs the kernel to root its argument span;
- generated MIR whose fixed argument suffix is already rooted (D5.3.5);
- a caller that donates an explicit scalar result home (D5.2.1); and
- a legacy caller that requires the boundary to allocate a temporary result
  home and re-home before returning.

They are ownership-qualified adapters into one internal kernel:

```text
js_call_function
js_call_function_into
js_call_function_prerooted_args_into
          |
          v
js_call_value(callee, this, args, argc, home, rooting_mode)
          |
          v
callee.fn->invoke(...)
```

The existing `_into` and pre-rooted APIs must not be deleted merely to hit an
API-count target; recent scalar-home and precise-rooting work made their
contracts load-bearing. The structural gate is that they share one resolver
and one per-callee entry, not that every ownership mode has the same C
signature.

`Function.prototype.apply` and `Reflect.apply` remain JavaScript algorithms
that build/borrow an argument list and then call this kernel. They are not
additional invocation engines.

### 5.4 One construct kernel

The target boundary is conceptually:

```cpp
Item js_construct_value(Item callee, Item* args, int argc,
                        Item new_target, uint64_t* result_home,
                        bool args_prerooted);
```

All dynamic construction funnels through it:

- `new expr(args)`;
- `Reflect.construct(target, args, newTarget)`;
- bound-function construction;
- `super(args)` when runtime construction is required;
- builtin subclass construction; and
- host/DOM constructors represented as function values.

The entry validates constructability by the presence of a construct
capability. It passes `new_target` explicitly and restores the caller's active
binding on every return, including ERROR.

For an ordinary user constructor the construct entry owns the standard
sequence: derive the correct prototype, allocate the initial receiver when
required, invoke the body with active `this` and `new.target`, validate the
returned value, and finish instance fields/brands. Builtin entries allocate
their required internal-slot-bearing representation and apply
`GetPrototypeFromConstructor` through one shared helper.

### 5.5 Active state versus pending state

`js_current_this`, active `js_new_target`, active module/realm, and private
home class describe the currently executing activation. They may remain in
the context capsule while this phase preserves the existing compiled-body
ABI.

The following distinction is mandatory:

```text
allowed:  construct entry receives newTarget -> scoped install -> body -> restore
deleted:  caller writes pending newTarget -> unrelated call path maybe consumes it
```

A small C+ RAII scope or explicit begin/end pair may perform the scoped
install. It must keep saved Items exactly rooted across any `MAY_GC` body call
and restore state on success and ERROR. It must not rely on C++ exceptions.

An activation-frame redesign that also replaces `js_pending_call_args`,
`js_pending_args_callee`, and every active binding is deferred. This phase
must not widen into that work unless measurement shows the same root cause
cannot be fixed locally.

## 6. Native targets and factories

### 6.1 Split MIR bodies from native bodies

The current `js_new_function(void* func_ptr, int param_count)` is used for
both compiled entries and heterogeneous native callbacks. `param_count`
therefore participates in deciding how to cast and call a raw pointer. That
is neither typed nor self-describing.

The target has explicit construction families:

```text
js_new_mir_function(...)
js_new_mir_method(...)
js_new_mir_closure(...)

js_new_native_function(... typed target metadata ...)
js_new_native_method(... typed target metadata ...)
js_new_intrinsic_function(JsIntrinsicTargetSpec*, JsIntrinsicBindingSpec*)
```

The existing MIR pointer storage can remain backend-opaque because its ABI is
selected by compiler-produced metadata and its per-callee protocol entry.
Native C pointers must use matching typedefs.

### 6.2 Fixed-arity host callbacks

Host modules contain many functions naturally written as `Item f()`,
`Item f(Item)`, and so on. They do not need to be mechanically rewritten into
one giant `(this, args, argc)` function if a typed adapter family preserves
their ABI.

Use a small generated/shared family of native adapter entries, selected once
when the function is created:

```text
native arity 0 -> js_call_entry_native_0
native arity 1 -> js_call_entry_native_1
...
rest/explicit receiver -> js_call_entry_native_span
```

Each adapter performs argument defaulting/extra-argument handling according
to its declared contract and calls a correctly typed union member. There is
no repeated `switch(param_count)` and no cast from `void*` to an unrelated
function type.

The adapter family is shared. Implementation must not create hundreds of
hand-copied wrappers; use the existing catalog/X-macro machinery or a single
typed helper template where C+ convention permits it.

### 6.3 Intrinsic semantic bodies

Builtin methods and constructors should converge on the span signatures in
§5.1 because their semantics frequently depend on `this`, optional/variadic
arguments, result homes, or `newTarget`.

During migration, extract one semantic body per catalog target from the
existing switch. Shared algorithms remain shared helpers. For example,
Array.prototype and TypedArray.prototype may share an iteration kernel, but
their binding target entries own distinct brand/species policy. The shared
kernel receives that policy as a compile-time wrapper choice or explicit
validated parameter; it does not rediscover policy from a global dispatch
mode such as `js_dispatch_as_array_method`.

The migration must remove the old case when the direct target lands. Copying a
case into a function while retaining the switch violates JC11 and rule 13.

## 7. Catalog redesign

### 7.1 Keep one catalog, separate target from binding

The existing `js_builtin_catalog.def` remains the authority. Its concepts are
normalized into two rows:

```text
INTRINSIC_TARGET
    stable catalog id
    call body or null
    construct body or null
    exception/GC effect reference
    compiler lowering kind
    default intrinsic prototype policy

INTRINSIC_BINDING
    owner object/prototype
    property NameId specification
    target id
    observable name and length
    data/accessor descriptor attributes
    receiver/brand variant
    alias identity key
```

Global rows reference the same target records. Constructor IDs may remain as
stable catalog/link identifiers, but they do not select implementation in a
runtime switch.

This split fixes an existing conflation: two properties can use the same
algorithm without being the same function object, while two spec aliases can
be the same function object even though they have two property keys.

### 7.2 NameId integration

Under D4.6 and `JS_Runtime_Name.md`:

- a generated JS catalog property name carries its generated NameId;
- an arbitrary catalog spelling is prelinked into the realm/static closure
  and resolved once at realm construction;
- installation and later shape lookup use NameId, not a repeated
  `(chars,len)` search; and
- no process-global context-dependent NameId is stored in immutable catalog
  data.

The observable function `.name` is a JavaScript string value, not executable
identity. It may be materialized from the catalog spelling/NameId at function
creation. Writing or redefining `.name` changes only that property.

### 7.3 Effect metadata

The callable catalog must reference, not duplicate, the helper effect
authority used by MIR lowering. A fallible native body has an `Item` result
under D8.4.3. A raw scalar entry is allowed only where the existing catalog
proves `PRESERVES`; builtin function values exposed to arbitrary JavaScript
should normally use the boxed `Item` boundary.

Catalog validation rejects:

- an executable target with neither call nor construct body;
- a method/accessor binding whose target is missing;
- a construct-only target installed where a call-only function is required;
- an alias identity key with inconsistent target/name/descriptor semantics;
- duplicate owner + NameId bindings;
- an unregistered or mismatched exception effect;
- a native target stored through an incompatible pointer type; and
- a target requiring a result home whose adapter drops it.

### 7.4 Realm-local identity and aliases

Realm construction creates real function objects. The default is one function
identity per binding per realm. Sharing requires an explicit alias identity
key, for cases such as an iterator method and its Symbol alias where the spec
requires equality.

The cache key is therefore not merely `builtin_id`. It is the declared realm
identity key. Two bindings that happen to call the same C body do not become
`===` accidentally.

All cached Items are context-owned and precisely rooted under D5.4.2–D5.4.4.
Immutable target descriptors and C entry pointers may remain process-global.
Function objects, property maps, prototypes, and arbitrary NameIds may not.

## 8. Builtin installation and property semantics

### 8.1 Real properties, no miss-time invention

At realm construction, each intrinsic binding is installed on its namespace,
constructor, or prototype with the catalog's exact descriptor. Accessors use
real getter/setter function values. After publication:

- `Object.getOwnPropertyDescriptor` reads the installed descriptor;
- `Object.getOwnPropertyNames` / `Reflect.ownKeys` see the correct property;
- deletion and redefinition operate on that slot;
- extraction returns the installed function object;
- monkey-patching replaces it normally; and
- an ordinary property miss does not ask the builtin catalog to synthesize a
  value.

Some realm objects are built lazily today. Lazy realm construction may remain,
but the first construction publishes the complete intrinsic object
transactionally. Per-property miss-time fabrication is rejected because it
creates a second prototype/property mechanism and complicates deletion.

### 8.2 Method calls lower as Get plus Call

The canonical lowering of `receiver.name(args)` is:

1. evaluate `receiver`;
2. perform the required nullish/coercibility check;
3. load `name` through the NameId property path;
4. preserve the receiver as the Reference base;
5. evaluate arguments in source order; and
6. call the loaded value with the receiver as `this`.

This applies equally to arrays, strings, numbers, maps, typed arrays, DOM
objects, and user objects. Primitive receiver boxing/prototype lookup belongs
inside property access, not method dispatch.

At completion, MIR for a generic static method call contains no direct import
of `js_string_method`, `js_number_method`, `js_map_method`,
`js_array_method`, or `js_array_method_direct`.

### 8.3 Legal direct calls

A direct call is an optimization of `Get -> Call`, never an alternate
semantic route. It is legal when one of these proves the exact target:

- a lexical/local function definition whose value cannot be replaced;
- a compiler-known class method under the existing source-identity proof;
- an immutable internal operation not reachable as a mutable property; or
- later, a JR8 feedback slot guarded by exact callee identity and realm
  mutation state.

The spelling `"map"` plus an inferred Array receiver is not proof: the
property may be replaced, deleted, inherited through a modified prototype,
provided by an accessor, or observed through a Proxy.

Existing Math/Date/string/array direct lowerings must be audited against this
rule. A direct intrinsic call that bypasses an observable property mutation is
deleted or gains the exact guard/fallback before this phase completes.

## 9. Construction semantics

### 9.1 Explicit `newTarget`

`new expr(args)` passes the evaluated constructor as both `callee` and initial
`new_target`. `Reflect.construct(target, args, newTarget)` passes its third
operand explicitly. `super()` forwards the active derived `new.target`.

No caller writes a pending global before invoking a generic call routine.

### 9.2 Builtin constructors

Each intrinsic constructor owns direct call and/or construct bodies. Important
dual cases remain separate:

- `Date()` returns a date string while `new Date()` creates a Date object;
- `String`, `Number`, and `Boolean` calls return primitives while construction
  creates wrappers;
- `RegExp` call and construct share parts of an algorithm but retain their
  specified identity/return rules;
- Error classes may be called or constructed and preserve the unified
  `LambdaError` payload from JR3; and
- `Array()` and `new Array()` share allocation semantics without name dispatch.

Call-only `Symbol`/`BigInt` and construct-only Promise/collection/TypedArray
families express that capability directly.

### 9.3 `GetPrototypeFromConstructor`

Builtin construct bodies that create internal-slot-bearing values use one
shared helper:

```text
newTarget.prototype if it is an object
    else the target intrinsic's realm-default prototype
```

The target descriptor supplies the default intrinsic prototype policy. This
replaces repeated `"prototype"` materialization and hand-written
`js_apply_constructed_builtin_prototype` variants while preserving subclass
behavior.

The helper uses the NameId property path and propagates an accessor/Proxy ERROR
unchanged. It does not stamp an own `constructor` property unless the
ECMAScript algorithm actually requires one.

### 9.4 Bound construction

A bound function stores its target, bound receiver, and bound arguments as
traced fields. Its capabilities are decided at creation:

- call entry always exists if the target is callable;
- construct entry exists iff the target is constructable;
- bound `this` is ignored during construction;
- bound arguments precede call-site arguments; and
- if the incoming `newTarget` is the bound wrapper itself, forwarding replaces
  it with the target before invoking the target construct entry.

The observable `"bound " + target.name` value is built for `.name` only. No
execution path strips or interprets that prefix.

### 9.5 Classes and Proxies

Class constructors retain their call-time TypeError and construct behavior.
The class-call rejection is an entry/capability fact, not a test of source
spelling. The legacy class-map representation may route through one
`js_construct_class_map` transition as described in §2.3; JR4 later moves it
to shape-carried ops.

A Proxy's call and construct capabilities mirror its target at Proxy creation.
The corresponding exotic entry performs the `apply` or `construct` trap and
falls back to the target entry. A non-callable target cannot become callable
merely because its handler owns an `apply` property.

## 10. Interaction with other redesign phases

### 10.1 Exception redesign — prerequisite satisfied

JR3/D8.4.3 means native and compiled entries return one merged `Item`. Direct
builtin bodies therefore need no pending flag, poll, or wrapper-specific
error protocol. Catalog effects only decide whether generated code may elide
the ERROR-tag branch.

### 10.2 Name redesign — prerequisite

The NameId migration supplies static property keys, catalog linking, and
context-owned named IC state. Callable implementation should begin after the
NameId ABI and static-closure rules used by builtin installation are stable.
It must not reintroduce compiler-pool name pointers or a JS-private interner.

### 10.3 Property redesign — immediately follows

Once a method is a normal property value and invocation no longer needs a
builtin/name interpreter, JR6 can collapse property access to one path without
preserving method-dispatch exceptions. This is the main dependency payoff of
doing callable work before the property API consolidation.

### 10.4 Object metadata / exotic ops

JC2's separate call/construct entries define the callable portion of JR4's
future per-class exotic ops table. JR4 owns moving Proxy/class-map/host VMap
resolution behind shape-carried metadata; this phase must not invent a second
class table.

### 10.5 Promises

Promise constructor and prototype methods migrate to direct callable entries
here. Promise representation remains the existing one until JR7. JR7 can then
replace storage without changing how `Promise`, `.then`, or reaction callbacks
are invoked.

### 10.6 Feedback vectors

JR8 call slots cache exact function identity and, after a stable hit, may cache
its call entry/native target. Because the baseline path already performs
per-callee dispatch, devirtualization is an optimization rather than a second
semantic mechanism.

### 10.7 Shared MIR and persistent cache

Shared MIR never embeds a context-owned `JsFunction*`, target object, or
arbitrary NameId. Static method loads use the per-context NameId/module-state
path. A compiler-proven direct native helper uses the existing symbolic MIR
import/relocation machinery, not an anonymous function-pointer immediate.

Immutable catalog IDs may appear in fingerprints or relocation metadata, but
execution links them to the current build/context before use.

## 11. Implementation plan

Each phase is independently green and net-negative. A converted group deletes
its old cases and callers immediately; no long-lived compatibility layer is
accepted.

### C0 — Formal adoption, census, and fixtures

1. Adopt D6.2.2v2 from §14, bump the formal-design version, and revise JR5 in
   `JS_Runtime_Redesign.md` in the same change. This landed in formal-design
   version 1.11.0.
2. Freeze structural census scripts for:
   `js_dispatch_builtin`, builtin case labels, runtime `builtin_id` semantic
   reads, constructor spelling branches, pending `new.target`, public
   name-method helpers, ambiguous native factories, and method-name MIR
   imports.
3. Capture release call/construct profiles and realm-startup allocation/time.
4. Add semantic fixtures before changing code: monkey-patched methods,
   extraction, aliases, bound constructors, `Reflect.construct`, Proxy traps,
   cross-realm identity, builtin subclass prototypes, and `.name` mutation.
5. Record current `sizeof(JsFunction)`, size class, allocation counts, and
   call-lane distribution.

**Exit:** the old mechanisms are measurable and every high-risk observable
behavior has a failing-if-broken fixture.

### C1 — Callable target fields and typed factories

1. Add the construct-entry type and native target fields without changing
   semantics.
2. Replace `func_ptr` ambiguity with explicit MIR and typed-native factories.
3. Make one initialization/finalization function the sole writer of call and
   construct capabilities.
4. Extend GC tracing only for Item edges; function pointers/NameIds are not GC
   roots.
5. Assert every published `LMD_TYPE_FUNC` has a valid call entry and a
   construct entry exactly when its kind permits construction.
6. Add layout/static assertions and confirm `JsFunction` remains within the
   selected GC size class; if it does not, remove obsolete fields before
   selecting a larger class.

**Exit:** the representation can express the target design, but the old
builtin dispatcher still handles only the not-yet-converted catalog rows.

### C2 — Catalog target/binding split and direct builtin calls

1. Extend the existing X-macro catalog with typed call/construct target
   references and explicit alias identity.
2. Generate/validate target and binding tables from that one file.
3. Convert dispatch groups in bounded batches. Extract semantic bodies, assign
   them to target rows, and delete the corresponding `js_dispatch_builtin`
   cases in the same batch.
4. Replace `js_dispatch_as_array_method` with distinct Array and TypedArray
   target entries feeding shared validated algorithms.
5. Convert accessors to direct getter function targets.
6. Convert Function.prototype call/apply/bind/toString and Function.prototype
   itself early; they exercise extraction, active `this`, and recursive call
   entry behavior.
7. Delete `js_dispatch_builtin` when the last target moves. Retain catalog IDs
   only as metadata and add a lint that rejects semantic branching on them.

Suggested group order: Function/Object primitives -> Math/Number/String ->
Array/TypedArray -> collections/iterators -> RegExp/Date/Error -> Promise ->
host/DOM tail. Each group runs its focused Test262 subset before the next.

**Exit:** calling a builtin reaches a typed target through the function's
entry; no runtime builtin ID switch exists.

### C3 — Explicit construction and `new.target`

1. Add `js_construct_value` and route dynamic `new` and `Reflect.construct`
   through explicit `new_target` operands.
2. Install construct entries for ordinary functions, classes, bound
   functions, and intrinsic constructors.
3. Convert every name-selected builtin constructor into catalog call/construct
   targets.
4. Extract one shared `GetPrototypeFromConstructor`/derived-prototype helper.
5. Route runtime `super()` construction through the same kernel.
6. Delete the builtin-name chain, `special_ctor_kind`,
   `special_ctor_name`, `js_is_intrinsic_constructor_named`, and other
   execution classifications derived from `.name`.
7. Delete `js_pending_new_target` and `js_has_pending_new_target`; retain only
   scoped active `js_new_target` state.

**Exit:** changing a function's `.name` cannot affect callability,
constructability, allocation, prototype selection, or error class.

### C4 — Real intrinsic properties and canonical method lowering

1. Install all catalog bindings as real realm properties with exact
   descriptors and explicit aliases.
2. Remove builtin catalog consultation from ordinary prototype/property
   misses.
3. Change generic static member calls to the same NameId `Get -> Call` path as
   computed calls.
4. Audit and remove name-only direct lowerings. Retain only exact target proofs
   from §8.3.
5. Retire public method-name dispatch APIs and their MIR imports. Preserve
   algorithm helpers that direct target bodies still share.
6. Verify deletion/redefinition and Proxy/accessor ordering before enabling
   any later feedback fast path.

**Exit:** builtin methods participate in exactly the same property and call
semantics as user functions.

### C5 — Bound, Proxy, class-map, and host convergence

1. Make bound call/construct entries forward through target capabilities with
   explicit `newTarget` substitution.
2. Remove builtin-specific bound branches.
3. Make callable/constructable Proxy decisions follow target capability and
   route traps through one exotic entry.
4. Keep only the named transitional legacy class-map construct entry; publish
   the contract JR4 will move to shape ops.
5. Convert remaining `js_new_function((void*)...)` host sites to typed native
   factories and delete the ambiguous API.
6. Represent Function.prototype as a real callable function value and delete
   Map/sentinel callability cases and the own-`.call` compatibility path.

**Exit:** every function/native callback is born with its final invocation
protocol, and non-function callability exists only through a specified exotic
operation.

### C6 — Deletion, documentation, and measurement

1. Run all structural scans and delete phase flags, legacy declarations,
   duplicate adapters, stale catalog groups, and dead comments.
2. Update `doc/dev/js/JS_05`, `JS_06`, `JS_07`, `JS_15`, and diagrams to the
   implemented callable flow.
3. Update `JS_Runtime_Redesign.md`: mark JR5 complete, revise JR6 dependencies,
   and record actual mechanism/LOC counts.
4. Run release profiles and compare call cost, method cost, construct cost,
   startup time, allocations, and binary size.
5. Split only the now-stable callable/catalog code into coherent files, with
   no new TU over 8k lines and no copied static helpers.

**Exit:** all §12 gates pass and the old mechanisms are absent from code,
docs, MIR fixtures, and registries.

## 12. Verification gates

### 12.1 Structural gates

| Census | Baseline | Target |
|---|---:|---:|
| `js_dispatch_builtin` definitions | 1 (+ forward declaration) | 0 |
| `case JS_BUILTIN_*` in runtime semantic switches | 337 | 0 |
| Runtime semantic branches on `fn->builtin_id` | non-zero | 0 |
| `js_pending_new_target` / `js_has_pending_new_target` references | 103 | 0 |
| `special_ctor_kind` / `special_ctor_name` references | 17 | 0 |
| Constructor selection by function-name bytes | one large chain | 0 |
| Public `*_method(receiver, method_name, ...)` dispatch imports | at least 5 families | 0 |
| Generic method lowering selected only by property spelling + receiver TypeId | several paths | 0 |
| Published `JsFunction` with null call entry | builtin/partial paths exist | 0 |
| Ambiguous `js_new_function(void*, arity)` native sites | many | 0 |
| Semantic callable mechanisms for `LMD_TYPE_FUNC` | per-callee invoke + builtin ID/name interpreters | per-callee entries only |

The final census must distinguish metadata reads from semantic reads. Keeping
`catalog_id` for logging does not fail the gate; switching on it to choose a
body does.

### 12.2 Source-size and ownership gates

- Net `lambda/js` source delta for the completed phase is negative, with a
  target of at least 1,500 lines removed after new entries/tests are counted.
- `js_runtime.cpp` loses the builtin dispatcher and constructor-name chain;
  moving them intact to another file fails JC12.
- No third near-identical native adapter is handwritten; the shared typed
  family is extracted first (rule 13).
- `sizeof(JsFunction)` remains within its verified size class or the
  implementation records measured justification for a change.
- No new permanent root range is allocated per callable or per call.
- No NameId is treated as a GC root, and no context-owned target address is
  embedded in shared MIR.

### 12.3 Behavioral fixtures

At minimum, focused tests cover:

- replacing/deleting Array, String, Number, Date, RegExp, Map, and DOM methods;
- getter/Proxy effects during method lookup occurring before argument
  evaluation in the specified order;
- extracted builtin methods and Function.prototype.call/apply/bind;
- builtin `.name` and `.length` descriptors, including mutation with unchanged
  behavior;
- explicit alias identity and distinct-body/shared-body non-alias identity;
- realm separation (`realm1.Array.prototype.map !== realm2...`);
- Function.prototype callability and ordinary object non-callability;
- arrows/methods/generators not constructable;
- classes throwing on call but succeeding on construct;
- bound call receiver behavior, bound argument ordering, bound construction,
  and `newTarget` substitution;
- callable and constructable Proxy traps, revoked Proxies, and trap fallback;
- `Reflect.construct` with a distinct `newTarget`;
- builtin subclassing for Array, TypedArray, ArrayBuffer, DataView, Date,
  RegExp, Error, Promise, Map/Set, and DOM constructors;
- `Date()` versus `new Date()`, primitive wrapper call versus construct,
  Symbol/BigInt rejecting `[[Construct]]` entries, and construct-only
  collections;
- constructor ERROR returns leaving no active/pending state for the next call;
- recursive/nested construction and `super()` across ERROR/finally paths;
- GC during argument adaptation, bound-argument merge, prototype access,
  native body execution, and result re-homing; and
- native callbacks from Node, DOM, timers, promises, and event listeners.

### 12.4 MIR fixtures

- A generic `obj.name(args)` fixture shows NameId property load followed by one
  call kernel, not a receiver-specific method import.
- A computed call preserves the same kernel and receiver.
- Dynamic `new C(args)` passes explicit `callee` and `newTarget` operands.
- `Reflect.construct(A, args, B)` carries B without `js_set_new_target`.
- A known lexical function may still direct-call its MIR body under the
  existing proof.
- An intrinsic property call is not direct merely because its spelling and
  receiver type are known.
- ERROR-tag propagation after call/construct remains a fused branch on the
  returned Item under D8.4.3.
- Scalar result homes are forwarded through call and construct adapters.

### 12.5 Suite and performance gates

Use release builds for every performance result (repository rule 10):

```bash
make build-test
./test/test_js_gtest.exe
make test262-baseline
make test-mir-gc-stress
make test-radiant-baseline
make release
```

Also run focused Test262 directories for Function, built-ins, classes,
Proxy, Reflect, bound functions, TypedArray, and subclassing after each
corresponding batch.

Performance gates:

- no statistically significant regression in the standard release benchmark
  suite or js-test-batch wall time;
- generic user-function call cost does not regress, since builtins must not
  perturb the already-selected ordinary call lanes;
- builtin method calls remove `js_dispatch_builtin` and catalog-name lookup
  from sampled stacks;
- dynamic intrinsic construction removes constructor-name comparison from
  sampled stacks;
- realm startup time, function allocations, retained bytes, and binary size
  are recorded; any increase above 5% needs a measured design review rather
  than a lazy-property side mechanism; and
- call feedback/devirtualization is not required to hide a baseline
  regression. JR8 is an optimization phase, not a correctness subsidy.

## 13. Risks and stop conditions

| Risk | Control / stop condition |
|---|---|
| Realm startup grows from eager function installation | Measure time/bytes in C0. First construct the intrinsic object transactionally; do not invent miss-time semantic fabrication. Stop and reduce object weight/duplicate identities if >5%. |
| Function identity changes | Catalog aliases are explicit and tested. Never cache solely by body pointer or target ID. |
| Receiver branding drifts while array/typed-array switches are extracted | Distinct target entries own policy and share only lower algorithm helpers. Focused generic/borrowed-method Test262 gates each group. |
| Call/construct entries duplicate prologue logic | Keep rooting, stack depth, active-state install, realm/module switching, and scalar-home finish in shared protocol helpers. At the third variant, extract the common shape before continuing (rule 13). |
| Explicit `newTarget` breaks nested `super` or arrows | Land fixtures first; scoped active binding restores on every success/ERROR path. Stop if a pending compatibility flag is proposed as the fix. |
| Direct native pointer types expand `JsFunction` past its size class | Measure in C0/C1; use a compact tagged union or descriptor pointer only if it remains context-safe and does not add runtime semantic switching. |
| Method lowering loses an existing performance shortcut | Measure the exact shortcut. Retain it only with exact callee proof and fallback; name/type proof is insufficient. |
| Host callback migration becomes unbounded | Use typed shared adapter families and migrate by constructor API, not site-specific wrappers. Stop if a third manually-copied adapter appears. |
| Legacy class maps force object redesign into scope | Keep one explicit transition branch and publish the JR4 ops contract. Do not duplicate class metadata here. |
| Catalog direct entries conflict with shared MIR/cache | Store target objects per context; use symbolic imports/relocation and catalog fingerprints. Stop if any generated instruction asks for a context-owned function-object pointer immediate. |
| LOC target is met only by moving code | Count all `lambda/js` source and inspect semantic switch/body census. A file move without deletion fails. |

## 14. Formal-spec adoption

The direct-call portion was already required by D6.2.1–D6.2.2. Separate
construct capability and explicit `newTarget` were adopted as D6.2.2v2 in
`doc/Lambda_Formal_Design.md` version 1.11.0 before C1 implementation began.

### Adopted D6.2.2v2 — per-callee call and construct entries

> **D6.2.2v2** Dynamic calls dispatch through per-callee executable entries.
> LambdaJS function values carry distinct `[[Call]]` and
> `[[Construct]]` capabilities: call dispatch uses `fn->invoke`; construction
> uses an explicit construct entry and passes `newTarget` as an operand, never
> through a pending one-shot side channel. Builtin catalog IDs, names, formal
> lengths, and class labels are metadata and may not select runtime semantics;
> a declared native ABI may select a typed adapter once when the function is
> created. The JavaScript dynamic boundary remains `Item* + argc`;
> caller-donated scalar
> homes and precise rooted argument spans are ownership-qualified adapters to
> the same entries, not separate dispatch mechanisms. The 16-slot source-
> argument limit remains statically checked where possible with a runtime
> backstop; adapter spans are dynamically sized, precisely rooted, and
> LIFO-destroyed. Dynamic calls with named arguments and dynamic calls to
> `var`/inout signatures remain rejected.

This is an implementation-design revision, not a semantics revision. C0 must:

1. replace D6.2.2 with D6.2.2v2;
2. bump the formal design document semver;
3. revise JR5's “one call convention” wording to “one callable kernel with
   distinct Call/Construct capabilities”; and
4. update the formal decision-record index to this document.

D4.6, D5.2, D5.3, D5.4, D6.2.1, D8.4.2, and D8.4.3 remain unchanged.

## 15. Rejected alternatives

### 15.1 Keep `builtin_id`, replace the switch with an array of function pointers

Rejected. It makes ID interpretation faster but leaves function values without
their own executable target, retains separate builtin binding/call semantics,
and keeps constructor/name dispatch unsolved. The catalog may contain target
pointers, but the instantiated function must carry/link its final entry.

### 15.2 One entry with `newTarget = undefined` meaning ordinary call

Rejected. ECMAScript distinguishes `IsCallable` from `IsConstructor`, several
intrinsics implement both with different algorithms, and classes/construct-only
intrinsics require different diagnostics. A sentinel recreates a mode switch
inside the entry and makes capability checks indirect.

### 15.3 Keep pending `new.target`, add more cleanup guards

Rejected. The root cause is the one-shot ambient transfer itself. More guards
cannot prove every early return, Proxy trap, bound forward, nested construct,
or ERROR path consumes exactly the intended value.

### 15.4 Preserve receiver-specific method dispatch as the fast path

Rejected. Receiver TypeId plus property spelling does not identify a callee in
JavaScript. It is observably wrong under replacement, deletion, accessors,
prototype mutation, and Proxy. Exact callee feedback can optimize later.

### 15.5 Cache builtin identity by native body pointer

Rejected. Different properties/realms may share an implementation but require
distinct function identity; declared aliases may require shared identity even
across different property keys. Identity is a binding/realm decision (JC7).

### 15.6 Make each builtin a new TypeId or private object representation

Rejected. Builtins are ordinary function values under D6.2.1. A JS-private
callable representation would repeat the mechanism this design removes and
consume shared data-model complexity.

### 15.7 Convert class maps and all exotic objects in this phase

Rejected. It would entangle JR4 shape/class metadata with the prerequisite it
depends on. One typed transitional class construct branch is explicit and
temporary; name/ID dispatch still disappears now.

### 15.8 Introduce feedback vectors at the same time

Rejected. The baseline callable mechanism must be correct, simple, and
competitive before it is cached. JR8 then has one stable entry/identity to
observe and can be evaluated independently.

### 15.9 Handwrite per-site or per-builtin adapter copies

Rejected by rule 13. The catalog and typed adapter family generate/reuse the
shared shape; semantic bodies contain only the algorithm-specific work.

## 16. Completion criterion and next mechanism

The callable redesign is complete when this source expression:

```js
receiver.method(arg)
```

has one explainable runtime meaning:

```text
resolve method NameId
-> ordinary property Get
-> obtain a realm-local function value
-> invoke its per-callee call entry
-> return one Item success/error result
```

and this expression:

```js
new Constructor(arg)
```

has the corresponding construct meaning:

```text
resolve constructor value
-> invoke its explicit construct entry with newTarget
-> return one Item success/error result
```

No step reconstructs behavior from a builtin ID, display name, method name,
class label, or pending side channel. Builtins, user functions, bound
functions, native callbacks, classes, and exotic callables differ only in the
entry/capability their value supplies.

After this gate, the next redesign is JR6: consolidate property access around
the now-clean `Get -> Call` boundary, followed by JR4 shape-carried class and
exotic metadata.

### 16.1 Implemented outcome

Tune4 closes this criterion under **D6.2.2v2**. `JsFunction` carries distinct
`invoke` and optional `construct` protocol entries; typed target/factory
families bind MIR, fixed-native, span-native, intrinsic, bound, Proxy, class,
and host behavior at publication. The central builtin semantic dispatcher,
name-selected constructor chain, pending `newTarget`, ambiguous `void*`
factory, receiver/name method APIs, and miss-time intrinsic synthesis are
deleted. Catalog bindings are realm-local real properties, and static and
computed member calls both observe `Get -> Call`.

The final structural census is zero for every retired mechanism. One named
`js_construct_entry_legacy_class_map` bridge remains at the JR4 boundary, with
one definition and two total references; it is deleted when JR4 moves legacy
class-map construction onto ordinary function capabilities. Exact C0/C8 LOC,
layout, startup, performance, conformance, GC, and embedding results are in
`JS_Tune4_Callable.md` §14.
