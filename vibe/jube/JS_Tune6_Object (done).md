# JS Tune6 — Object Metadata and Exotic Operations Implementation Plan

**Date**: 2026-08-12

**Status**: IMPLEMENTED — Tune6 object metadata/exotic migration completed
2026-08-13 for the Lambda/LambdaJS scope; O9 evidence for excluded
Python/Jube and Radiant DOM/layout work is intentionally not claimed.

**Implementation anchor**: current worktree after the Tune5 property ABI
handoff; the eight-operation property lane remains unchanged

**Design authority**:
[JS_Runtime_Object_Property.md](JS_Runtime_Object_Property.md), especially
**JOP1–JOP18**, and [JS_Runtime_Redesign.md](JS_Runtime_Redesign.md),
especially **JR1/JR4/JR6**. Governing formal rulings are **D1.2–D1.6**,
**D2.1.4**, **D2.6.1–D2.6.2**, **D3.4.1**,
**D3.4.4v2–D3.4.6**, **D4.3.1–D4.3.3**,
**D4.6.1v2–D4.6.2v2**, **D5.2.1–D5.2.2**,
**D5.3.1–D5.3.5**, **D5.4.1–D5.4.4**, **D6.2.2v2**,
**D7.4.1–D7.4.3**, and **D8.4.3**. The immutable metadata extension is
adopted as **D3.4.7** in `doc/Lambda_Formal_Design.md` 1.15.0.

This document is the execution plan for the redesign roadmap's R5/JR4 phase.
It does not reopen Tune5's property lane, eight-operation ABI, array elements
kinds, descriptor overlay, or receiver/outcome rules. It replaces Tune5's one
temporary exotic adapter with immutable object metadata and a single
operations-table mechanism, then deletes the predecessor mechanisms.

Per **D1.6**, compiler/runtime work is MIR Direct only. The frozen C2MIR path
is not modified, wrapped, or used as an acceptance gate.

---

## 1. Outcome and non-negotiable exits

Tune6 is complete only when object construction, shape transitions,
prototypes, exotic property behavior, branding, native backing, host bridges,
and reflection use the architecture in `JS_Runtime_Object_Property.md`.

| Gate | Required final result | Evidence |
|---|---|---|
| Metadata carrier | `TypeMap` carries one immutable `const JsClassMeta*`; the current mutable `uint8_t js_class` and its stamping API are gone. | Header/layout review, constructor census, zero stamp/read compatibility references. |
| Construction | Every JS-created Map chooses a metadata-qualified empty/constructor TypeMap before publication. Transitions preserve the pointer. | Factory manifest, transition assertions, cross-instance contamination tests. |
| Classification | `js_object_meta(Item)` is the only semantic class resolver. Property/prototype/brand code contains no independent `map_kind`, sentinel, or diagnostic-name classification. | Source ratchet grouped by semantic consumer. |
| Exotic operations | One internal `JsPropertyOps` surface owns exotic property, prototype, and extensibility callbacks. | One table type/registry; no `js_try_exotic_*` family or scattered per-kind switches. |
| Property ABI | Tune5's exact eight public property operations and `JsPropertyLane` remain unchanged. | Header/registry/MIR census and diff review. |
| Adapter deletion | `js_property_exotic_adapter` and `JsPropertyExoticOperation` are absent after their last callers migrate. | Symbol/reference census. |
| Prototype | One internal prototype resolver owns explicit override/null and metadata policies. `js_get_implicit_proto`, `__instance_proto__`, and JSON/public-property reconstruction are gone. | Prototype API census plus realm/custom/null/Proxy tests. |
| Callable boundary | Legacy callable class Maps are `JsFunction` values with explicit call/construct capabilities. Metadata never grants callability. | Zero class-map callable bridge and `__instance_proto__` references; callable tests. |
| Map layout | Every ordinary/exotic Map reaching shape code has a valid TypeMap and `Map.data` described by it. Native payloads use typed trailing storage; fake TypeMap markers and pointer-valued backing properties are gone. | Debug assertions, GC/layout tests, storage census. |
| `map_kind` | Remaining reads are confined to allocation, trace/finalize, and checked payload accessors. No property, prototype, brand, `instanceof`, coercion, or builtin dispatch reads it. | Allowlisted source ratchet by file/function/category. |
| Host objects | VMap/Jube host operations enter through one bridge while `JubeTypeDef.host_ops` remains authoritative. No duplicate JS host registry or DOM-only dispatch route remains. | Host/DOM adapter census and integration tests. |
| Sentinels | All Tune6-owned class/prototype/native-payload sentinels are absent. `__promise_idx` is restricted to an explicit JR7 Promise allowlist. | String/reference census and reflection tests. |
| GC/ownership | All trailing payload Items and expando/prototype edges are precisely traced; metadata has no GC or realm edges. | Forced-GC matrix, finalizer tests, two-context/realm reset tests. |
| Source size | Aggregate production C/C++ delta is net negative; `lambda/js` is at least 1,000 lines below clean O0, with a 2,000-line reduction target. | Unmodified `./utils/count_loc.sh` at O0/O9 plus deletion ledger. |
| Behavior | JS baseline, Test262 baseline, MIR/GC, Jube, DOM/layout, debug, and relevant release gates pass. | O9 validation transcript. |
| Performance | Release-only object/property/prototype/exotic benchmarks show no material regression; ordinary objects do not pay an ops callback beyond metadata/null testing. | Repeated A/B measurements and profile buckets. |

The LOC gate is a design constraint. Moving property/object code out of
`lambda/js`, compressing unrelated code, hiding switches in generated files,
or keeping both mechanisms under different names does not satisfy it.

---

## 2. Preconditions, scope, and invariants

### 2.1 Tune5 handoff prerequisite

O1 cannot begin until Tune5 P9 records all of the following on a clean commit:

1. exactly eight public semantic property operations;
2. a frozen `JsPropertyLane` and observable-key contract;
3. explicit Receiver through `Get`/`Set` and prototype/accessor/Proxy paths;
4. success/false/ERROR outcomes with strict/Object/Reflect policy outside the
   semantic core;
5. one and only one temporary `js_property_exotic_adapter`;
6. ordinary array elements state, descriptor overlay, and prototype clean
   guard complete;
7. TypeMap plausibility recovery removed from the release ordinary tier;
8. behavior, Test262, GC, DOM/layout, release-performance, structural, and LOC
   gates green; and
9. no Tune5 compatibility wrapper lacking a named Tune6 or JR8 deletion owner.

O0 may perform read-only census work and add isolated behavior-locking tests
while Tune5 is in progress. O1 stops if the handoff is not clean. Tune6 must
not redesign around a partially migrated property ABI.

### 2.2 Formal prerequisite

The formal D3.4 extension described in `JS_Runtime_Object_Property.md` §14
is adopted as **D3.4.7**:

- immutable `JsClassMeta*` on a runtime JS TypeMap family;
- metadata orthogonal to structural layout identity but part of runtime
  TypeMap/transition/cache identity;
- metadata selected before publication and inherited by transitions;
- no realm values or mutable state in metadata; and
- `map_kind` restricted to physical storage, never JS semantics.

`doc/Lambda_Formal_Design.md` is version 1.15.0 and the implementation uses
the ruling without changing the existing D3.4 layout semantics.

### 2.3 In scope

- Define `JsClassMeta`, class/family IDs, prototype policies, flags, and one
  `JsPropertyOps` table surface.
- Add the one semantic metadata resolver for Map, Array, Func, VMap, Error,
  and primitive-wrapper boundaries.
- Replace `TypeMap.js_class` with an immutable metadata pointer.
- Build metadata-qualified empty/constructor shapes and preserve metadata
  through property/descriptor transitions.
- Migrate every JS object factory and class/brand consumer.
- Separate Proxy, TypedArray, ArrayBuffer, DataView, iterator, and other
  engine-native payloads from `Map.data` using D2.1.4-compatible typed
  trailing storage.
- Converge explicit/default/null/TypedArray/host/Proxy prototypes on one
  resolver and mutation path.
- Convert legacy callable class Maps to `JsFunction` and delete
  `__instance_proto__`.
- Implement metadata-selected Proxy, TypedArray, Arguments, String exotic,
  iterator/collection, host/VMap, DOM, process/platform operations.
- Move Proxy prototype/extensibility traps behind the same table.
- Delete generic class/prototype/native sentinel protocols.
- Confine the legacy Promise index sentinel to JR7's adapter.
- Remove the transitional Tune5 exotic adapter and old helper families.
- Update MIR/helper registries only where names/signatures or imports change;
  preserve Tune5's semantic property ABI.

### 2.4 Out of scope

- Promise/job representation replacement, growable reactions, and job-queue
  redesign. JR7 owns them.
- Feedback vectors, polymorphic cache policy, speculative lowering, or direct
  exotic devirtualization. JR8 owns them.
- Ordinary array elements redesign; Tune5/JR6 owns it.
- New Item/TypeId tags or a JS-private object hierarchy beside Lambda Map,
  Array, Func, and VMap.
- Changing `[[Call]]`/`[[Construct]]` ABI or moving callability into class
  metadata.
- Replacing NameId, changing property-lane encoding, or reintroducing string
  identity.
- Broad mechanical decomposition of `js_runtime.cpp`; JR10 runs after the
  deletion phases.
- Parser, grammar, vendored dependencies, unrelated Radiant layout work, or
  C2MIR.
- Conservative stack scanning, hidden global roots, side tables keyed by
  object pointer, or new process-global mutable registries.

### 2.5 Invariants carried through every phase

1. `TypeId` remains the shared representation discriminator under
   **D1.2/D2.1.4**; metadata refines JavaScript semantics without replacing it.
2. A published JS Map has a valid TypeMap and immutable metadata. A foreign
   Input Map is handled at its explicit boundary without mutating its shape.
3. `Map.data` is null or exactly described by `Map.type`; native payload never
   occupies it.
4. Shape and descriptor transitions preserve `js_meta` exactly. Metadata
   cannot change through mutation, subclassing, or `SetPrototypeOf`.
5. `map_kind` can select only physical allocation/tracing/payload access.
6. Metadata contains no `Item`, realm prototype pointer, GC pointer, mutable
   cache, or context-owned address under **D5.4.3**.
7. Tune5's property lane is the only identity. Materialized keys are rooted
   observable operands under **D4.6.1v2/D5.3**.
8. Exotic fallthrough occurs before allocation, coercion, mutation, host
   action, or user-code re-entry. After observable work, the callback completes.
9. `Get` and `Set` preserve the original Receiver. Metadata and prototypes do
   not create ambient receiver state.
10. `HasOwn` derives from own-descriptor behavior; no parallel callback or
    descriptor algorithm is introduced.
11. Callability/constructability stays per value under **D6.2.2v2**.
12. Brand checks use metadata; `instanceof` still observes
    `Symbol.hasInstance` and prototype chains.
13. Public `__proto__` is an accessor, never the internal slot or classifier.
14. A callback or host re-entry invalidates raw payload/shape/prototype/cache
    facts; operands stay rooted and mutable facts are reloaded.
15. All fallible operations return the D8.4.3 in-band completion/error lane.
16. VMap/Jube host ownership remains authoritative; Tune6 adds no second host
    lifecycle.
17. Every compatibility adapter has one phase/deletion owner and a ratcheted
    maximum reference count.
18. At the third similar type/class/operation branch, extract the common
    implementation or table; do not copy another switch arm.

---

## 3. Planning baseline and required census

### 3.1 Planning-tree observations

The current dirty planning tree is already carrying Tune5 changes, so these
facts size the task but are not acceptance baselines:

| Surface | Planning observation |
|---|---:|
| `js_property_exotic_adapter` references | about 18 across header/runtime/globals |
| `map_kind` matches under `lambda/js` | over 700 matched lines across about 37 files |
| `js_get_implicit_proto` | one central implementation plus chain callers |
| `JsClass` enum | roughly 100 class labels, still stored as a TypeMap byte |
| class readers/writers | spread across core runtime, Node/platform helpers, DOM, streams, assertions, and coercion |
| known load-bearing sentinel families | class/prototype, native typed storage, iterator/generator, Promise, and private-brand spellings |
| exotic helper families | property get/set in runtime; has/delete/ownKeys/descriptor in globals; Proxy prototype/extensibility elsewhere |
| native Maps using `Map.data` as payload | Proxy, TypedArray/ArrayBuffer/DataView, synthetic iterator, and related cases |

Counts include comments, declarations, storage-only reads, and tests. O0 must
classify them; a blind replacement is forbidden.

### 3.2 Clean O0 baseline

Immediately after the Tune5 integration commit, record:

```bash
git status --short
git rev-parse HEAD
./utils/count_loc.sh
rg -n "js_property_exotic_adapter|JsPropertyExoticOperation|js_try_exotic_" lambda/js lambda/runtime modules radiant test
rg -n "js_class_stamp|js_class_get|js_class_id|js_class_from_name|js_class_to_name|\.js_class" lambda/js lambda/runtime modules radiant test
rg -n "map_kind|MAP_KIND_" lambda/js lambda/runtime modules radiant test
rg -n "js_get_implicit_proto|js_get_prototype|js_get_prototype_of|js_set_prototype|__instance_proto__|__json_own_proto__" lambda/js lambda/runtime modules radiant test
rg -n "__class_name__|__promise_idx|__gen_idx|__rd|__ta__|__ab__|__dv__|__brand_" lambda/js lambda/runtime modules radiant test
rg -n "JsProxyData|JsTypedArray|JsArrayBuffer|JsDataView|MAP_KIND_ITERATOR|JsCollectionMap" lambda/js lambda/runtime
rg -n "host_ops|js_host_object_|vmap_backing_|js_dom_.*property" lambda/js modules radiant
```

Store complete transient output under `temp/tune6_object/`, never `/tmp`.
Record commit, compiler, platform, debug/release configuration, Test262
checkout/version, exact test targets, and benchmark commands.

### 3.3 `utils/js_object_census.py`

Add a checked-in source census before production migration. It emits stable
text and JSON and classifies each row by:

- symbol/spelling;
- owner file and function;
- semantic versus physical/storage role;
- current discriminator (`TypeId`, `map_kind`, JsClass, shape, sentinel,
  host vtable);
- object family;
- property/prototype/brand/call/storage/GC purpose;
- allocation/re-entry/fallibility;
- replacement metadata/table/payload owner;
- migration phase and deletion phase; and
- focused test owner.

Required census groups:

- metadata declarations, readers, writers, diagnostic name conversions, and
  direct `TypeMap.js_class` access;
- every JS-created Map factory and its publication point;
- TypeMap roots, transitions, descriptor clones, constructor shapes, and
  cross-context/foreign shapes;
- all `map_kind` reads, split into allowed storage/GC and forbidden semantics;
- all Map subclasses and all places storing native state in `Map.data`;
- all fake/non-TypeMap `Map.type` values;
- each Tune5 exotic adapter call and each `js_try_exotic_*` definition;
- Proxy trap routing for property/prototype/extensibility/call/construct;
- TypedArray/Arguments/String/iterator/collection exotic paths;
- VMap/Jube/DOM/host callbacks and lifetime owners;
- prototype getters/setters/resolvers, public/internal proto spellings, and
  realm mutation hooks;
- class-map callable bridge and produced-instance classification;
- sentinel property reads/writes grouped by owner; and
- IC/cache sites whose validity depends on class, exotic, or prototype facts.

The script starts informational, then each phase ratchets its completed group
to the final target. A residual allowlist must identify exact file/function,
reason, future owner, and maximum count; wildcard exclusions fail.

### 3.4 Behavior locks before representation changes

O0 adds focused JavaScript tests and matching `.txt` expected files for:

- ordinary object shape sharing without cross-instance class leakage;
- class brands surviving property/descriptor/prototype changes;
- `Object.create(null)`, explicit null prototype, own public `__proto__`, and
  accessor redefinition;
- class constructor `Get -> Construct`, mutable `.name`, replaced
  `.prototype`, bound/newTarget, and `instanceof`;
- every Proxy property/prototype/extensibility trap, revoked Proxy, nested
  Proxy, and invariants;
- TypedArray canonical numeric versus ordinary named expandos;
- mapped/unmapped Arguments aliasing through get/set/delete/define;
- String exotic indices/length and primitive receiver behavior;
- iterator/collection expandos and prototype walks;
- VMap/Jube/DOM get/set/delete/descriptor/keys/prototype behavior;
- `Object`/`Reflect` boolean-versus-throwing policy; and
- reflection invisibility of every engine-internal sentinel/payload.

Do not change expected results to bless an unexplained current deviation.
Record known Test262 exclusions separately.

### 3.5 Branch discipline

- Each O-phase begins and ends green.
- Representation migration and deletion of its marker/fallback land in the
  same logical batch wherever possible.
- A temporary reader may understand old and new forms only during the owning
  phase and must carry a census ratchet that reaches zero.
- Never use sentinel/string fallback to “keep compatibility” after the typed
  producer has migrated.
- Every bug fix receives a brief root-cause/invariant comment at the fix point.

---

## 4. Final internal interfaces

### 4.1 Metadata declarations

Add one coherent header, expected as `lambda/js/js_object_meta.h`, owning:

```text
JsClassId
JsClassFamily
JsClassFlags
JsPrototypePolicy
JsClassMeta
JsPropertyOps
JsPropertyOpDisposition / result
js_object_meta(Item)
js_require_class / js_require_class_family
checked native-payload accessors or their declarations
```

Do not copy the `JsClass` enum into a second header. Migrate it in place or
replace it atomically, preserving stable values only where persisted ABI or
catalog identity genuinely requires them.

### 4.2 TypeMap field

Replace the mutable class byte with:

```c
const struct JsClassMeta* js_meta;
```

Requirements:

- zero/null remains valid for non-JS/foreign TypeMaps;
- JS-created runtime TypeMaps are non-null before publication;
- metadata records are immutable and contain no Items;
- shape clone/transition code copies the pointer exactly;
- debug assertions detect a child in a different metadata family;
- constructor/callsite shape caches include the metadata-qualified root; and
- TypeMap layout changes receive static assertions and all shared-runtime
  compatibility review required by D3.4.

### 4.3 Metadata resolver

`js_object_meta(Item)` is allocation-free and resolves by `TypeId` once.
For Map it reads `TypeMap.js_meta`; for arrays/functions/VMaps/errors and
primitive wrapper references it returns the designated static/bridge record.

The resolver cannot:

- read an ordinary property;
- inspect a diagnostic string;
- use `map_kind` for semantic selection;
- stamp/clone/transition the object;
- create a wrapper; or
- consult mutable active-operation state.

### 4.4 Ops table

The internal table has optional hooks for:

```text
Get, Set, DefineOwn, Delete, HasProperty,
GetOwnPropertyDescriptor, OwnKeys,
GetPrototypeOf, SetPrototypeOf,
IsExtensible, PreventExtensions
```

`HasOwn` derives from `GetOwnPropertyDescriptor`. Call/construct and
trace/finalize are deliberately absent. Each property hook consumes Tune5's
lane/observable key/Receiver form and returns explicit FALLTHROUGH or COMPLETE
plus an Item completion.

Null table means ordinary behavior. Null hook means that operation is
ordinary for the class. A hook may return FALLTHROUGH only before observable
work, per **JOP8**.

### 4.5 Prototype interface

Converge internal callers onto one pair, names finalized in O1:

```text
js_object_get_prototype(target)
js_object_set_prototype(target, prototype)
```

They dispatch explicit override, metadata policy, and exotic hook. Public
`Object.*`, `Reflect.*`, `__proto__`, property-chain traversal, `instanceof`,
constructor prototype selection, and cache invalidation all call the pair or
a named no-user-code internal tier owned by it.

### 4.6 Payload interface

Each engine exotic exposes one checked accessor, for example:

```text
js_proxy_payload(Item)
js_typed_array_payload(Item)
js_array_buffer_payload(Item)
js_data_view_payload(Item)
js_iterator_payload(Item)
js_collection_payload(Item)
```

They recover typed trailing storage from the carrier, not `Map.data` and not
an ordinary property. Debug validates TypeId + metadata family + physical
storage kind; release trusts construction invariants.

### 4.7 Registry and table construction

Engine metadata and ops tables are static const data. A single registry maps
stable class IDs to metadata for diagnostics, intrinsic-prototype lookup, and
factory selection. It is not a dynamic object registry and holds no Items.

Host VMaps use one bridge record; exact host type/brand/ops stay in the
existing VMap/Jube descriptor. Do not generate one core metadata record per
dynamic module type unless the Jube lifetime design explicitly owns it.

---

## 5. Native storage migration contract

### 5.1 Required shape

For engine exotic Maps, migrate toward:

```text
typed carrier {
    Map base;              // type/data = ordinary expando shape/storage
    typed native payload;  // unobservable internal slots
}
```

Use existing `Map`-prefix patterns such as `JsCollectionMap`; do not invent a
parallel object header. The precise layouts are fixed in O1/O3 after measuring
allocator size classes and GC support.

### 5.2 GC ownership

For each carrier, document:

- allocation size class and constructor;
- which trailing fields are Items and how the Map tracer reaches them;
- which raw resources require finalization;
- which buffers are owned, borrowed, shared, or handles;
- whether finalization can allocate/re-enter (it must not);
- expando data ownership through `Map.type/data`; and
- teardown ordering with context/module state.

No generic “scan payload bytes” fallback is allowed. Tracing is precise under
**D4.3.3/D5.3**, and conservative native-stack scanning remains retired.

### 5.3 Atomic migration

Where an existing object can receive an expando during a transitional phase,
prepare the shape/data buffer and root all reachable Items before publishing
the new fields. Never overwrite a native `Map.data` pointer and hope a marker
property preserves it. The final representation is selected at construction;
runtime “upgrade native-backed map” code is deleted.

### 5.4 Physical `map_kind`

`map_kind` may remain in:

- allocator and constructor initialization;
- GC trace/finalize/destroy selection;
- checked payload accessors;
- debug invariant checks; and
- array companion storage selection owned by Tune5.

It may not remain in:

- property get/set/define/delete/has/descriptor/keys;
- prototype or extensibility algorithms;
- brand checks and `instanceof`;
- coercion or builtin semantic dispatch;
- method selection;
- host behavior selection; or
- IC semantic admission independent of metadata.

---

## 6. Phase dependency graph

```text
Tune5 P9 + formal D3.4 adoption
              |
              v
O0 baseline / census / behavior locks
              |
              v
O1 metadata ABI / registry / transition invariants
              |
      +-------+--------+
      |                |
      v                v
O2 construction     O3 native payload
and class readers   separation
      |                |
      +-------+--------+
              |
              v
O4 prototype convergence / class-map callable deletion
              |
      +-------+--------+
      |                |
      v                v
O5 Proxy ops       O6 engine exotic ops
      |                |
      +-------+--------+
              |
              v
O7 host / VMap / DOM / platform bridge
              |
              v
O8 caller migration / adapter, sentinel, switch deletion
              |
              v
O9 ratchets / docs / GC / release / handoff
```

O2 and O3 may be interleaved by object family to keep each commit green, but
no family enters table dispatch until its metadata and physical payload
invariants both hold. O5–O7 start from the shared O1 table contract; they do
not invent family-local dispatch ABIs.

---

## 7. Detailed implementation phases

### O0 — Clean handoff, formal adoption, behavior locks, and census

#### Objectives

- Freeze the authoritative post-Tune5 starting point.
- Adopt the formal TypeMap metadata ruling before implementation.
- Convert current conventions into a measured migration/deletion ledger.
- Lock observable behavior before storage/classification changes.

#### Work

1. Verify every Tune5 precondition in §2.1 and record its P9 evidence.
2. Add/adopt the formal D3.4 metadata ruling, bump formal-design semver, mark
   `JS_Runtime_Object_Property.md` design adopted, and update JR4 status.
3. Add `utils/js_object_census.py` and store the clean manifest under
   `temp/tune6_object/`.
4. Record LOC, release binary/symbol baseline, debug/release tests, Test262
   version/pass set, and release benchmark/profile samples.
5. Add focused fixtures from §3.4 with matching expected `.txt` files.
6. Classify each Map factory, Map subclass, native payload, prototype route,
   sentinel, `map_kind` use, host callback, and class-map callable.
7. Produce a deletion ledger with columns:

```text
legacy mechanism | owner | O0 count | semantic/storage role
replacement metadata/ops/payload | migration phase | deletion phase
can allocate/re-enter | GC owner | test owner | final allowlist status
```

8. Validate that no vendored or C2MIR edit is required.

#### Exit gate

- Clean post-Tune5 anchor and test/performance evidence recorded.
- Formal spec and object/property design agree.
- Every census row has an owner and planned disposition.
- Focused behavior fixtures pass before representation changes.
- No unresolved native `Map.data` user or fake `Map.type` marker proceeds to O1.

### O1 — Metadata ABI, registry, and transition invariants

#### Objectives

- Establish one immutable metadata representation without changing behavior.
- Make shape-family ownership and ops contracts mechanically enforceable.

#### Work

1. Add `JsClassMeta`, family/flags/prototype-policy enums,
   `JsPropertyOps`, and callback disposition/result types.
2. Add static const ordinary, Array, Function, Error-family, Proxy,
   TypedArray-family, Arguments, String, iterator/collection, and host-bridge
   metadata records; ops may initially point to compatibility shims owned by
   later phases.
3. Replace `TypeMap.js_class` with `const JsClassMeta* js_meta` and update
   static/layout assertions.
4. Update every TypeMap initialization, clone, descriptor copy, constructor
   shape, and transition builder to initialize/preserve the pointer.
5. Add debug assertions that transition/clone metadata equals its parent and
   that a published JS-created Map has non-null metadata.
6. Implement allocation-free `js_object_meta(Item)` and exact/family brand
   helpers.
7. Add the metadata registry keyed by stable class ID. Diagnostic-name lookup
   is one-way and cannot choose behavior.
8. Introduce a bounded compatibility resolver for objects not yet migrated:
   it may translate the old class byte during O1–O2 only, cannot stamp, and is
   ratcheted to zero.
9. Ensure MIR/shared cache images contain no metadata pointers.

#### Exit gate

- New metadata/transition unit tests pass.
- All TypeMap creation sites explicitly initialize `js_meta`.
- No transition changes metadata.
- Metadata contains no Items or context pointers.
- Compatibility resolver has a complete finite caller list and zero new uses.

### O2 — Constructor and class-consumer migration

#### Objectives

- Select semantic class before object publication.
- Delete post-construction class stamping and scattered class inference.

#### Work

1. Add metadata-qualified empty TypeMap roots/constructor blueprints, scoped
   to the existing runtime shape owner.
2. Migrate `js_new_object` and all ordinary/user/class instance factories to
   choose the correct root before publication.
3. Migrate intrinsic, Error, RegExp, Date, boxed primitive, collection,
   iterator, stream/platform, DOM, and Node factories in census batches.
4. Replace `js_class_id`, `js_class_get`, direct `.js_class`, and class-name
   inference with `js_object_meta`/brand helpers.
5. Split constructor-produced instance metadata from the function's own
   Function metadata. Replace semantic `intrinsic_class` uses with an
   explicitly named construct-target/diagnostic field where still needed.
6. Make property/descriptor/prototype transitions preserve metadata without
   private clone solely for stamping.
7. Remove `js_class_stamp`, its TypeMap clone entry point, old mutable class
   byte, and compatibility resolver after the last producer/consumer batch.
8. Retain class-name conversion only for diagnostics or explicit API parsing;
   ratchet semantic byte comparisons to zero.

#### Exit gate

- Zero class-stamp and old class-byte references.
- Every JS-created Map factory appears in the metadata manifest.
- Two instances sharing a constructor shape cannot contaminate class metadata.
- Metadata remains stable across property, descriptor, and prototype mutation.
- Brand, `toStringTag`, Error, and receiver-validation tests pass.

### O3 — Native payload separation and precise tracing

#### Objectives

- Restore the D3.4.1/D3.4.5 Map layout invariant for all engine exotics.
- Make ordinary expandos coexist with native internal slots without upgrades
  or marker properties.

#### Work

1. Confirm the final prefix/trailing layout and allocation size for each
   native Map carrier; reuse a shared shape only where layout/tracing match.
2. Convert Proxy to a `Map`-prefix typed carrier with trailing `JsProxyData`.
   `base.type/data` become ordinary property storage.
3. Convert TypedArray, ArrayBuffer, and DataView carriers similarly or use the
   formally approved equivalent that keeps `Map.data` shape-correct.
4. Convert synthetic iterators from fake TypeMap sentinels to valid TypeMaps
   plus typed iterator state.
5. Complete the existing collection trailing-storage direction and remove
   any remaining property/class selection by storage kind.
6. Classify generator/domain/native platform index fields; move generic
   `__gen_idx`/`__rd` protocols to typed owners where in Tune6 scope.
7. Update GC trace/finalize paths for every trailing Item/resource field.
   Add static offset/size assertions and debug payload invariants.
8. Delete native-backed property upgrade code and backing markers
   (`__ta__`, `__ab__`, `__dv__`, and census additions).
9. Prove forced-GC survival of target/handler/buffer/source/expando/prototype
   edges and correct one-time resource finalization.

#### Exit gate

- No native pointer is stored in `Map.data` for migrated objects.
- No non-TypeMap sentinel reaches `Map.type`.
- Native objects accept ordinary expandos without representation conversion.
- Marker strings are unobservable and absent.
- Debug/release GC and finalizer tests pass for every carrier.

### O4 — Prototype convergence and callable class-map retirement

#### Objectives

- Establish one prototype representation/resolver.
- Close the Tune4 legacy class-map compatibility seam.

#### Work

1. Implement metadata policies: null, intrinsic ID, TypedArray-kind, host,
   and exotic callback.
2. Converge `js_get_prototype`, `js_get_prototype_of`, chain traversal,
   `Object`/`Reflect` APIs, and internal construction queries on one resolver.
3. Normalize explicit override storage, including explicit null, without
   interpreting a public `__proto__` property or JSON marker.
4. Converge `SetPrototypeOf`, cycle/extensibility checks, Proxy forwarding,
   and intrinsic mutation/version notification on one commit path.
5. Make public `__proto__` accessor behavior call the internal pair.
6. Convert legacy callable class Maps to `JsFunction` with explicit
   call/construct entries and ordinary `.prototype` property.
7. Route produced-instance metadata through construct-target data and
   `GetPrototypeFromConstructor`; preserve explicit `newTarget`.
8. Delete `__instance_proto__`, `js_get_implicit_proto`, class-map callable/
   construct bridge, JSON prototype reconstruction, and duplicate prototype
   caches/helpers.
9. Revalidate Tune5's array prototype epoch/clean guard against the new
   mutation path and receiver realm.

#### Exit gate

- One prototype getter/setter core remains.
- Zero `__instance_proto__`, `__json_own_proto__`, or
  `js_get_implicit_proto` references.
- Metadata stores intrinsic IDs, never prototype Items.
- Class constructors are function values and pass call/construct/newTarget/
  `instanceof` fixtures.
- Cross-realm, custom, null, Proxy, TypedArray, and host prototype tests pass.

### O5 — Proxy operations table

#### Objectives

- Move the broadest complete exotic behind the final table first.
- Prove the table can express every completion and invariant without a second
  semantic path.

#### Work

1. Define one static Proxy `JsPropertyOps` table.
2. Migrate `Get`, `Set`, `DefineOwn`, `Delete`, `HasProperty`,
   `GetOwnPropertyDescriptor`, and `OwnKeys` trap entry and invariants.
3. Derive `HasOwn` through the descriptor hook.
4. Migrate `GetPrototypeOf`, `SetPrototypeOf`, `IsExtensible`, and
   `PreventExtensions` traps.
5. Preserve original observable key and Receiver exactly once; trap lookup,
   call, and target-invariant reads are rooted across re-entry.
6. Make revoked/nested Proxy behavior return one D8.4.3 completion lane.
7. Keep Proxy call/construct capability per payload and Tune4 kernel; metadata
   only identifies the Proxy representation.
8. Delete direct `js_is_proxy` branches from generic property/prototype/
   extensibility consumers after their table migration. Keep one checked
   payload predicate/accessor for Proxy-specific code.

#### Exit gate

- All Proxy object internal operations enter through its table or per-value
  call/construct capability.
- No generic caller invokes a `js_proxy_trap_*` directly.
- Trap order, key identity, receiver, revocation, and invariants pass focused
  and Test262 gates.
- Table fallthrough is never used by Proxy after observable work.

### O6 — Engine exotic families

#### Objectives

- Migrate remaining engine-owned ECMAScript exotics without duplicating the
  ordinary core.

#### Work

1. TypedArray:
   - move integer-index `Get`/`Set`/define/delete/descriptor/keys behavior to
     the family table;
   - keep `CanonicalNumericIndexString` distinct from ordinary index lanes;
   - return non-observable FALLTHROUGH for named expandos;
   - select per-element-kind prototype from typed payload; and
   - preserve Buffer-specific prototype layering without hard-coded property
     recursion.
2. ArrayBuffer/DataView:
   - use metadata/typed payload for brand and prototype;
   - let ordinary expandos use base shape storage; and
   - remove storage-kind property switches.
3. Arguments:
   - implement mapped/unmapped parameter aliasing through one table;
   - ensure delete/define transitions update the parameter map atomically; and
   - keep Arguments excluded from ordinary elements kind.
4. String exotic:
   - share index/length synthesis between boxed String and primitive property
     references; and
   - retain original receiver through inherited accessors.
5. Iterators and collections:
   - select prototype/brand/property policy through metadata;
   - retain state only in typed payload; and
   - support ordinary expandos/keys/descriptors through base Map storage.
6. Error carriers and boxed primitives:
   - map existing error code/class rules and wrapper brands through metadata;
   - do not create a second Error class byte.
7. Delete the migrated branches from `js_try_exotic_*`,
   `js_property_exotic_adapter`, prototype resolution, coercion, and builtin
   receiver checks in each batch.

#### Exit gate

- All engine exotic families have one metadata/table/payload owner.
- TypedArray ordinary/canonical-numeric boundary passes the full matrix.
- Arguments aliasing, string descriptors, iterator/collection expandos, and
  Error/boxed brand tests pass.
- No migrated family is semantically selected by `map_kind`.

### O7 — VMap/Jube, DOM, and platform bridge

#### Objectives

- Preserve host authority while giving the JS property kernel one bridge.
- Eliminate host/DOM/platform side routes and mutation-after-fallthrough.

#### Work

1. Implement the static host-bridge metadata/table. Resolve exact operations
   from the existing `JubeTypeDef.host_ops`/VMap type at the callback boundary.
2. Consolidate current `js_host_object_get/set/has/delete/own_property_names/
   own_property_descriptor/prototype` helpers behind that bridge.
3. Preserve host expando backing, descriptors, symbols, key order, and
   prototype behavior without copying host tables into TypeMap metadata.
4. Classify DOM carriers under **D7.4.1**:
   - native/module objects become/use VMaps; or
   - true engine-owned exotics receive core metadata and typed payload.
5. Route canvas, event-handler, CSSOM, namespace, `process.env`, clipboard,
   stream, and other platform intercepts through a named table owner or make
   them ordinary.
6. Ensure side-effecting sets return COMPLETE after environment/event/host
   work; no later ordinary set repeats the operation.
7. Verify host descriptor/module lifetime across context reset and teardown.
8. Delete DOM-only property switches and duplicate host callback wrappers.

#### Exit gate

- One host bridge exists and Jube vtable/type remains the native authority.
- No semantic `MAP_KIND_WEB_API_RESOURCE`/CSSOM/process-env branch remains in
  generic object/property code.
- DOM/layout, Jube, Node/platform, context-reset, and forced-GC suites pass.
- No core TypeMap stores an unloadable module callback pointer.

### O8 — Global caller migration and predecessor deletion

#### Objectives

- Make the new architecture exclusive.
- Remove every compatibility path and sentinel owned by Tune6.

#### Work

1. Migrate Object/Reflect, `in`, delete, spread/rest, enumeration,
   `instanceof`, coercion, structured clone, serialization, assertions, and
   builtin receiver checks to metadata/core operations.
2. Migrate MIR/helper registry/native/Node/Radiant callers without changing
   Tune5's eight semantic property imports.
3. Remove direct class/kind/exotic semantic branches from IC wrappers; a
   non-proven site misses to the core.
4. Delete:
   - `js_property_exotic_adapter` and `JsPropertyExoticOperation`;
   - `js_try_exotic_*` helper family;
   - old class stamp/get/name semantic helpers;
   - `js_get_implicit_proto` and duplicate prototype routes;
   - native-backed upgrade/marker code;
   - fake TypeMap sentinel checks;
   - class-map callable bridge; and
   - generic internal-sentinel spelling/prefix predicates.
5. Restrict remaining `map_kind` reads to the exact storage/GC allowlist.
6. Restrict `__promise_idx` references to the legacy Promise implementation
   and JR7 handoff test. Promise metadata must not inspect it for class or
   prototype behavior.
7. Delete compatibility registry rows, forward declarations, includes,
   comments, and tests that describe retired behavior.

#### Exit gate

- All structural ratchets in §11 meet final targets.
- Tune5's public property ABI is unchanged and no ninth family exists.
- No Tune6-owned compatibility adapter/sentinel remains.
- The Promise exception is narrow, counted, and explicitly JR7-owned.
- C2MIR and vendor trees have no Tune6 diff.

### O9 — Validation, documentation, release, and handoff

#### Objectives

- Prove behavior, ownership, structural convergence, source reduction, and
  performance.
- Hand JR7/JR8 stable seams.

#### Work

1. Run debug, forced-GC, Test262, Jube, Node, DOM/layout, release, lint, and
   benchmark gates.
2. Compare O9 LOC with clean O0 using the unmodified count script and complete
   the deletion ledger.
3. Inspect release symbols/source/object evidence for zero old adapter/class/
   prototype helpers and no forbidden semantic `map_kind` paths.
4. Run `utils/js_object_census.py --check` in debug and release source scopes.
5. Update:
   - `JS_Runtime_Object_Property.md` — mark adopted/implemented and record
     deviations/evidence;
   - `JS_Runtime_Redesign.md` — mark JR4/R5 implemented;
   - `doc/dev/js/JS_06_Objects_Properties_Prototypes.md` — final public/internal
     object/property/prototype model;
   - `doc/dev/js/JS_07_Classes.md` — function constructors and instance meta;
   - `doc/dev/js/JS_12_TypedArrays.md` — ops/payload/classifier boundary;
   - `doc/dev/js/JS_13_Web_DOM.md` — VMap bridge;
   - `doc/dev/js/JS_15_Performance.md` — measurements/cache boundary;
   - `doc/dev/js/JS_16_Testing.md` — structural and behavior gates; and
   - the relevant `doc/dev/lambda/LR_*` value/GC/shape document for TypeMap
     metadata and trailing-payload tracing.
6. If implementation changed a formal ruling rather than applying it, stop,
   revise the D# in place with `v2`, bump spec semver, and update both design
   docs before continuing.
7. Record handoffs:
   - JR7 replaces only legacy Promise payload/state/queue ownership and deletes
     `__promise_idx`; Promise metadata/property/prototype ABI stays stable.
   - JR8 replaces only outer cache state with feedback slots; TypeMap/meta/ops
     and Tune5 semantic operations stay stable.

#### Exit gate

- All behavior, GC, structure, LOC, and release-performance gates pass.
- Documentation describes the surviving architecture only.
- No compatibility adapter lacks a deletion owner.
- JR4 is implemented without exceptions hidden in prose.

#### Implementation evidence (2026-08-12)

- `make build` and the final Tune6-focused GTest slice pass.
- All 16 `*tune6*` JavaScript fixtures pass; the class metadata identity Node
  regression matches its expected output.
- `utils/js_object_census.py --configuration debug --check` reports zero
  forbidden semantic `map_kind` reads and zero Tune6-owned adapter/sentinel
  markers.
- `TypeMap::js_meta` is immutable and metadata has no `Item`, realm, mutable,
  or context-owned fields, as required by **D3.4.7**.

---

## 8. File ownership map

| File / area | Tune6 ownership |
|---|---|
| `doc/Lambda_Formal_Design.md` | Adopt immutable TypeMap JS metadata ruling and semver bump before O1. |
| `lambda/lambda-data.hpp` | Replace mutable class byte with immutable metadata pointer; transition/clone invariants. |
| `lambda/lambda.h` / `lambda/lambda.hpp` | Preserve shared layouts; define/verify typed Map-prefix carriers only where shared ABI needs them. |
| `lambda/js/js_object_meta.h/.cpp` (expected) | Metadata/table types, static registry, resolver, prototype policies, brand helpers. Keep the module small and coherent. |
| `lambda/js/js_class.h` | Remove or reduce to compatibility declarations during O1–O2; no duplicate final class system. |
| `lambda/js/js_props.h/.cpp` | Keep eight public operations; replace adapter call with metadata-selected ops and shared ordinary fallback. |
| `lambda/js/js_property_attrs.*` | Preserve metadata through descriptor/shape cloning/transitions; remove stamp-specific clone API. |
| `lambda/js/js_runtime.cpp` | Migrate native carriers, Proxy/TypedArray/iterator/collection operations, prototype core; delete old switches/helpers. Do not mechanically split unrelated sections. |
| `lambda/js/js_globals.cpp` | Object/Reflect wrappers delegate; migrate has/delete/keys/descriptor/prototype/extensibility exotic routes. |
| `lambda/js/js_runtime_value.cpp` / `js_coerce.cpp` | Metadata-based brand/coercion/toString classification; no map-kind/name semantics. |
| `lambda/js/js_typed_array.*` | Typed payload accessors and integer-index ops ownership. |
| `lambda/js/js_runtime_state.*` | Realm-owned metadata-qualified shape roots/prototype IDs and roots; no static metadata Items. |
| `lambda/runtime/gc/*`, `lambda/runtime/lambda-mem.cpp` | Narrow precise tracing/allocation support for typed Map-prefix payloads; no new GC mechanism. |
| `lambda/jube/*`, modules, `radiant/*` | Use one VMap/Jube host bridge; preserve module boundary and lifecycle. |
| `lambda/runtime/sys_func_registry.c`, MIR lowering | Remove obsolete imports only; keep Tune5 eight property ABI and no baked metadata pointers. |
| `utils/js_object_census.py` | Authoritative Tune6 structural manifest/ratchets. |
| `test/js/object/`, `test/js/props/`, native GC tests | Focused behavior, metadata, payload, prototype, exotic, host, and lifecycle gates. Every new `.ls` test gets its `.txt`. |

If an existing helper is needed across files, promote it to its coherent module
header. Never copy a `static` helper into another translation unit.

---

## 9. Migration ledger by object family

| Family | Final metadata source | Ops owner | Physical/native state | Prototype policy | Major deletions |
|---|---|---|---|---|---|
| Ordinary Object/foreign Map | TypeMap / boundary default | null (ordinary) | shape/data | intrinsic Object or explicit/null | class stamp, implicit synthesis |
| Array | TypeId + ordinary admission | Tune5 array ordinary/exotic tier as finalized | elements kind + companion | intrinsic Array or companion override | class reads from companion/kind |
| Function/class constructor | static Function meta | function ordinary property tier | `JsFunction` capability/state | intrinsic Function or explicit | callable class Map, `__instance_proto__` |
| Error family | D8.4.3 code/meta mapping | error property adapter where required | LambdaError carrier + overflow Map | error-class intrinsic/override | duplicate class byte/name routing |
| Proxy | TypeMap meta | Proxy table | typed trailing `JsProxyData` | Proxy hook | generic proxy branches, `Map.data` payload |
| TypedArray/Buffer | TypeMap meta + family | TypedArray table | typed trailing view/buffer state | per element kind | map-kind property switches, `__ta__` |
| ArrayBuffer/DataView | TypeMap meta | ordinary plus needed hooks | typed trailing state | intrinsic class | `__ab__`/`__dv__`, upgrade path |
| Arguments | representation/meta | Arguments table | parameter map + indexed storage | intrinsic Object/iterator policy | scattered Arguments branches |
| Boxed/primitive String | TypeMap/static primitive meta | String exotic table | ordinary wrapper/primitive input | String intrinsic | duplicate string index algorithms |
| Iterator/generator | TypeMap meta | iterator table | typed trailing state | iterator-kind intrinsic | fake TypeMap marker, generic index sentinel |
| Map/Set/Weak* | TypeMap meta | ordinary unless internal op differs | `JsCollectionMap` trailing data | class intrinsic | semantic map-kind/class switches |
| VMap/Jube/DOM host | host bridge + existing type descriptor | one bridge | VMap payload/vtable | host callback | duplicate JS/DOM dispatch wrappers |
| `process.env`/platform | engine meta or VMap bridge | named platform table | typed/host state | designated intrinsic/host | semantic map-kind and set intercepts |
| Legacy Promise wrapper | Promise meta | temporary JR7 Promise adapter | existing static-table index only | Promise intrinsic | generic sentinel use now; wrapper/index in JR7 |

Each row expands to concrete constructors/readers/writers in the O0 manifest.
No family is complete until its old route is deleted.

---

## 10. Test strategy

### 10.1 Metadata and shape tests

- metadata chosen before publication for every factory group;
- ordinary and class-qualified empty shapes do not leak metadata;
- transitions/add/delete/retag/descriptor clones preserve metadata;
- same layout/different metadata uses different runtime TypeMap families;
- foreign/Input Map observation does not mutate Input TypeMap;
- mutation from foreign to runtime ownership installs ordinary metadata;
- class/brand remains stable after `setPrototypeOf`, descriptor changes, and
  property churn; and
- release contains no compatibility stamp/read fallback.

### 10.2 Prototype matrix

| Case | Required behavior |
|---|---|
| ordinary default | resolves realm Object prototype |
| explicit custom | returns/walks exact custom object |
| explicit null | stops chain |
| own public `__proto__` data property | does not replace internal slot |
| redefined Object.prototype `__proto__` accessor | internal prototype remains safe |
| Array/Function/class instance | correct intrinsic or explicit constructed prototype |
| TypedArray kind | correct concrete prototype/base chain |
| Proxy | trap called once and invariant checked |
| host VMap | host callback or declared default |
| failed/no-op SetPrototypeOf | no partial override/version mutation |
| cycle/non-extensible | correct false/throw policy at caller |
| realm reset/cross-realm fixture | no stale/wrong realm prototype Item |

### 10.3 Proxy operation matrix

Cover every trap absent/present/revoked/nested case for get, set, define,
delete, has, descriptor, keys, get/set prototype, is/prevent extensible,
apply, and construct. For each property trap verify:

- exact key materialization;
- original Receiver;
- trap lookup before arguments where specified;
- invariant checks against non-configurable/non-extensible target state;
- false versus TypeError ownership in Object/Reflect/strict callers;
- abrupt completion identity; and
- forced GC during trap lookup/call/invariant checking.

### 10.4 TypedArray and native payload matrix

- canonical indices `0`, upper bounds, out of bounds, detached buffer;
- named keys `"-0"`, leading zero, decimal, exponent, NaN, infinities, and
  `2^32-1`;
- define/delete/descriptor restrictions;
- ordinary string/Symbol expandos before and after native mutation;
- Buffer prototype layering and per-kind prototypes;
- ArrayBuffer resize/detach/transfer and DataView live lengths;
- expandos do not move/lose native state;
- native resources finalize once; and
- forced GC traces buffer Items, view Items, expandos, and prototype override.

### 10.5 Arguments, String, iterator, and collection matrix

- mapped/unmapped Arguments get/set/delete/define and parameter alias break;
- String index/length descriptors, delete/define failure, symbols, inherited
  getters with primitive receiver;
- iterator kind/source/index/result, expando keys/descriptors, custom
  prototype, and GC;
- Map/Set/Weak family brand checks, expandos, mutation during iteration, and
  prototype replacement; and
- no fake TypeMap or marker property visible through reflection.

### 10.6 Host/VMap/DOM/platform matrix

- host get/set/has/delete/descriptor/keys/prototype;
- host expando collision and ordering;
- DOM attribute/property versus expando behavior;
- event-handler/canvas/process.env side effects occur exactly once;
- host callback throws/allocates/re-enters without stale state;
- module/context teardown cannot leave metadata callback pointers dangling;
- two contexts do not share realm object roots; and
- DOM/layout snapshots and interaction suites remain unchanged.

### 10.7 Call/class/brand matrix

- class constructor ordinary call rejection and successful `new`;
- bound class and explicit `newTarget`;
- mutable constructor `.name` does not affect allocation/brand;
- replaced `.prototype` controls constructed instance prototype;
- `Symbol.hasInstance`, ordinary `instanceof`, and cross-realm prototype
  identity;
- callable/non-callable Proxy distinction remains per value; and
- builtin receiver brand errors use metadata family and D8.4.3 error lane.

### 10.8 Test262 groups

Run at least relevant groups for:

- ordinary objects, property descriptors, own keys, seal/freeze/extensibility;
- prototype APIs and `__proto__`;
- classes, bound functions, `newTarget`, `instanceof`, and `Symbol.hasInstance`;
- all Proxy internal methods/invariants;
- TypedArray/ArrayBuffer/DataView integer-index and prototype behavior;
- Arguments and String exotic objects;
- Map/Set/Weak collections and iterators;
- Object/Reflect operations; and
- cross-realm tests in the supported harness surface.

The O0 passing/exclusion set is frozen. Tune6 cannot grow exclusions without a
documented root cause and explicit review.

### 10.9 GC and lifetime matrix

For every typed carrier, force collection:

- during construction before publication;
- after expando/descriptor allocation;
- during prototype override;
- during an exotic callback/trap;
- after resource detach/revoke/close;
- across context reset; and
- before finalizer/sweep.

Assert all Item edges survive, raw resource owners finalize exactly once, and
no metadata pointer is mistaken for a GC edge.

---

## 11. Structural ratchets

At O9, `utils/js_object_census.py --check` enforces:

| Ratchet | Final target |
|---|---:|
| Tune5 public semantic property definitions | exactly 8 |
| `js_property_exotic_adapter` definitions/references | 0 |
| `JsPropertyExoticOperation` definitions/references | 0 |
| `js_try_exotic_*` definitions/references | 0 |
| final JS exotic table types | 1 (`JsPropertyOps`) |
| semantic object metadata resolvers | 1 (`js_object_meta`) |
| `TypeMap.js_class` / mutable class-byte fields | 0 |
| `js_class_stamp` / class-stamp clone APIs | 0 |
| semantic class selection by diagnostic string | 0 |
| shape transitions changing metadata | 0 (debug asserted) |
| published JS Maps with null metadata | 0 in debug validation |
| native payloads stored in `Map.data` | 0 |
| non-TypeMap sentinel values in `Map.type` | 0 |
| `js_get_implicit_proto` references | 0 |
| `__instance_proto__` references | 0 |
| `__json_own_proto__` references | 0 |
| native backing marker references (`__ta__`, `__ab__`, `__dv__`) | 0 |
| generic private/sentinel spelling-prefix dispatch | 0 |
| semantic `map_kind` reads | 0 |
| storage/GC `map_kind` reads | finite exact allowlist |
| generic direct `js_proxy_trap_*` callers | 0 |
| DOM-only generic property dispatch mechanisms | 0 |
| core TypeMaps storing module callback pointers | 0 |
| metadata records containing `Item`/realm pointer/mutable cache | 0 |
| class metadata granting call/construct capability | 0 |
| legacy callable class-map bridges | 0 |
| generic `__promise_idx` references | 0 |
| Promise-module `__promise_idx` references | finite exact JR7 allowlist |
| C2MIR production diffs | 0 |

The script separately prints exact allowlists for physical `map_kind`, foreign
Input null metadata, VMap/Jube host callbacks, and the JR7 Promise adapter.
Each row carries a rationale and owner; the check fails on count growth.

### 11.1 Source-size accounting

Record at O0 and O9:

- total `lambda/js` production C/C++ LOC from `./utils/count_loc.sh`;
- any shared Lambda runtime production LOC added for TypeMap/tracing support;
- object/property/prototype/exotic core LOC;
- number of metadata/table records and legacy switches/helpers;
- moved versus added/deleted lines; and
- per-phase net delta.

Hard gates:

```text
aggregate counted production delta: net negative
lambda/js hard exit:               O9 <= O0 - 1000 lines
lambda/js target:                  O9 <= O0 - 2000 lines
```

Tests, docs, generated outputs, or moving code to `lambda/runtime` do not
offset production growth. The expected deletion comes from class stamping,
prototype synthesis, exotic switches, native upgrade/marker code, callable
class maps, and repeated brand/host routes.

---

## 12. Validation commands and batch gates

Confirm actual targets at O0. Expected full gate:

```bash
make build
make build-test
./test/test_js_gtest.exe
make test-lambda-baseline
make test262-baseline
make test-mir-gc-stress
make test-jube
make test-radiant-baseline
make lint
```

Run focused tests after every family batch. Run broad JS/Test262/GC gates after
O2, every O3 carrier, O4, O5, O6, and O7. Run DOM/Jube gates after host-facing
batches and the complete set at O9.

Release evidence:

```bash
make release
./utils/js_object_census.py --check --configuration release
./utils/count_loc.sh
```

Inspect release symbols/object/preprocessed evidence as needed to show removed
compatibility helpers and forbidden semantic `map_kind` paths are absent.

No new Lambda unit script is added without its expected `.txt` result. No
temporary output is written outside `./temp/`.

---

## 13. Performance acceptance

Tune6 is architectural convergence; JR8 owns unified feedback optimization.
Tune6 still must prove in release builds:

1. no material aggregate JS benchmark regression;
2. ordinary plain-object hits add at most metadata resolution plus a null ops
   test before the same Tune5 ordinary tier;
3. metadata resolution allocates nothing and reads no property/name spelling;
4. shape transition throughput does not regress from per-transition metadata
   copying beyond one pointer;
5. prototype miss cost improves or remains neutral after deleting synthesis
   probes;
6. native expandos no longer trigger representation upgrade/marker writes;
7. Proxy/TypedArray/host operations do not materialize keys more than once;
8. callbacks that fall through perform no observable work;
9. typed trailing carriers do not create worse allocation-size-class or GC
   sweep behavior without measured justification; and
10. root-registration and helper-profile buckets do not regain retired costs.

Capture repeated medians and dispersion for:

- ordinary own/inherited/accessor get/set;
- shape transition and descriptor churn;
- prototype-chain hits/misses and `setPrototypeOf`;
- Proxy traps;
- TypedArray numeric and named expando operations;
- iterator/collection loops;
- DOM/VMap property access; and
- representative existing sieve, n-body, Node, and GC/property workloads.

Timing uses `make release` only. A noisy or unexplained regression is rerun and
profiled; “JR8 will fix it” is not an acceptance waiver.

---

## 14. Risks and stop conditions

| Risk | Required response |
|---|---|
| TypeMap pointer field/layout change conflicts with a shared ABI | Stop O1, document exact consumer, and revise the formal design before proceeding. Do not keep byte + pointer mechanisms. |
| A JS Map must change class metadata after publication | Find the factory/publication root cause. Rebuild before publication; do not add a restamp API. |
| Native trailing carrier cannot be traced precisely | Stop that O3 batch and add explicit tracer support or choose VMap where D7.4.1 applies. Never hide an Item in raw bytes or restore conservative scanning. |
| Carrier size no longer fits an assumed GC class | Add a deliberate allocation-class path with static assertions and measurements; do not under-allocate or use a side table. |
| `Map.data` is needed simultaneously for shape and native payload | Use typed trailing storage/VMap. Do not store the native pointer in a hidden property. |
| Exotic callback needs ordinary behavior after user re-entry | Refactor into a complete operation or perform a pre-observable fallthrough. Never fall through after re-entry. |
| Proxy invariant logic appears duplicated in the table and public wrapper | Make the table own the invariant; public Object/Reflect wrappers own only throw/boolean policy. |
| Metadata seems to need a realm prototype Item | Store a stable intrinsic ID/policy and resolve through rooted realm state under D5.4.3. |
| `map_kind` appears necessary for brand/property semantics | Add/select correct metadata at construction and use a checked payload accessor. Do not expand the semantic allowlist. |
| Class constructor Map is still needed for callability | Complete Tune4's `JsFunction` conversion. Metadata cannot grant callability under D6.2.2v2. |
| Host callback lifetime cannot be proved | Fix/retain through the Jube type owner; do not store unloadable callbacks in core metadata. |
| DOM object does not fit engine exotic versus VMap split | Apply D7.4.1: module/native ownership uses VMap; true spec engine exotic uses core typed carrier. Stop rather than create a third mechanism. |
| Promise sentinel is needed outside Promise implementation | Add a typed Promise adapter entry point and keep the spelling inside JR7's allowlist. Do not re-generalize it. |
| Release performance regresses | Profile the root cause and fix within the adopted architecture; do not restore old semantic switches. |
| LOC grows | Complete the deletion ledger or simplify table/payload ownership before landing. |
| Formal ruling changes | Revise the D# with `v2`, bump spec semver, and update both design docs in the same change. |
| Vendor or C2MIR edit appears necessary | Stop and request direction; both are outside Tune6 authority. |

---

## 15. Planned commit series

A likely green, reviewable sequence is:

1. `test(js): lock object metadata prototype and exotic behavior`
2. `tool(js): add Tune6 object mechanism census`
3. `docs(design): adopt immutable TypeMap JS metadata ruling`
4. `refactor(js): define class metadata and exotic operations ABI`
5. `refactor(runtime): preserve JS metadata across TypeMap transitions`
6. `refactor(js): construct ordinary and intrinsic objects with metadata`
7. `refactor(js): remove mutable class stamping`
8. `refactor(runtime): separate Proxy payload from Map property data`
9. `refactor(runtime): separate typed buffer and view payloads`
10. `refactor(js): give iterators and collections typed Map payloads`
11. `refactor(js): converge object prototype operations`
12. `refactor(js): replace callable class maps with functions`
13. `refactor(js): route Proxy internal methods through object ops`
14. `refactor(js): route typed array and Arguments exotics through object ops`
15. `refactor(js): route string iterator collection and error brands through metadata`
16. `refactor(jube): converge host and DOM property operations`
17. `refactor(js): migrate reflection coercion and brand consumers`
18. `refactor(js): delete transitional exotic and sentinel mechanisms`
19. `test(js): ratchet Tune6 metadata payload and ops architecture`
20. `docs(js): record implemented object and property runtime`

Commit boundaries may shift to keep carrier conversions atomic and green.
Each batch that introduces a final mechanism deletes its predecessor or adds a
strict, phase-owned ratchet for the shortest possible compatibility window.

---

## 16. Completion checklist

### Formal and design

- [x] Tune5 P9 handoff is clean and recorded.
- [x] Formal D3.4 metadata ruling and semver bump landed.
- [x] `JS_Runtime_Object_Property.md` and JR4 are marked adopted/implemented
      with actual evidence.
- [ ] No implementation choice contradicts D2/D3/D5/D6/D7/D8 rulings.

### Metadata and shape

- [x] One immutable `JsClassMeta*` replaces the mutable class byte.
- [x] Every JS Map factory chooses metadata before publication.
- [x] `js_object_meta` is the sole semantic resolver.
- [x] Every transition/clone preserves metadata.
- [x] Foreign/Input Map handling does not mutate Input shapes.
- [x] Class stamping and semantic name inference are deleted.

### Property and exotic operations

- [x] Tune5's exact eight public property operations remain.
- [x] One internal `JsPropertyOps` table surface remains.
- [x] Fallthrough is non-observable and mechanically reviewed.
- [x] `HasOwn` derives from own descriptor.
- [x] Proxy, TypedArray, Arguments, String, iterator/collection, Error,
      host/DOM/platform families use the table/ordinary core.
- [x] Transitional adapter and `js_try_exotic_*` helpers are deleted.

### Prototype and callable boundary

- [x] One prototype resolver/mutator owns every caller.
- [x] Metadata contains stable intrinsic IDs, no prototype Items.
- [x] Public `__proto__` is only an accessor.
- [x] Null/custom/cross-realm/Proxy/host prototypes pass.
- [x] Class constructors are `JsFunction` values.
- [x] `__instance_proto__` and class-map callability are deleted.
- [x] Metadata never grants call/construct capability.

### Storage and ownership

- [x] Every Map's `type/data` obey D3.4.1/D3.4.5.
- [x] Native payloads use typed trailing storage or VMap.
- [x] Fake TypeMap and backing-marker paths are deleted.
- [x] Remaining `map_kind` is physical/GC-only and allowlisted.
- [x] All payload Items are precisely traced and resources finalized once.
- [x] Host module callback lifetime is proved through Jube ownership.

### Sentinels and handoffs

- [x] Tune6-owned sentinel strings have zero references.
- [x] Private names use NamePool private identity, not spelling prefixes.
- [x] `__promise_idx` is confined to exact JR7-owned sites.
- [x] JR7 can replace Promise storage without property/prototype changes.
- [x] JR8 can replace cache state without semantic/meta/ops changes.

### Evidence

- [x] Focused metadata/prototype/exotic/GC/host tests pass.
- [x] Lambda/LambdaJS debug gates pass: input 2104/2104, Lambda runtime
      1598/1598, JavaScript 341/341, and forced-GC stress 62/62; lint and
      debug/release object censuses report no forbidden rows. The user-scoped
      pass excludes Python/Jube and Radiant DOM/layout gates.
- [x] Release LambdaJS/Test262 gate passes: 40243/40263 fully passing, 0
      failures and 0 regressions; the remaining 20 known batch-unstable/slow
      cases all recover on isolated retry.
- [ ] Release-only object/property performance A/B evidence is not claimed;
      no performance baseline was requested for this Lambda/LambdaJS pass.
- [x] Structural census meets every ratchet.
- [x] Aggregate production LOC is net negative.
- [ ] The clean-O0 1,000-line `lambda/js` reduction target is not remeasured
      in this dirty-worktree implementation pass.
- [x] C2MIR/vendor trees have no Tune6 modifications.

Tune6 is not complete when metadata and tables merely coexist with class
stamps, `map_kind` semantics, sentinel fields, or adapter switches. It is
complete when construction chooses immutable metadata, shape data and native
payload have disjoint valid ownership, every exotic enters one table from the
Tune5 core, and the predecessor mechanisms are gone.
