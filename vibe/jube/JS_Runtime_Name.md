# LambdaJS Runtime Name Management — NameId-First Proposal

**Date:** 2026-08-11

**Status:** ADOPTED — NameId-first implementation complete; RN-D5 remains deferred

**Tree anchor:** master `2bfb9bdfa`

**Scope:** the shared `NamePool` substrate, Lambda/JS module linking, MIR
property operands, JS shapes and inline caches, computed property keys,
ECMAScript Symbols/private names, reflection, and the Mark/Input seam.

Per **D1.6**, implementation and verification cover MIR Direct only; the
retired C2MIR compatibility path received no NameId-first extension, and has
since been removed.

**Current formal authority:** **D1.4v3** (language failures return as explicit
completions through every frame), **D1.5** (precise rooting only), **D1.6** (C2MIR is frozen), **D1.7**
(MIR is a local derived cache, not a distribution format), **D3.4.4v2**
(`ShapeEntry` identity is one `NameId`), **D4.2.1v2** (MemContext factory
owns every allocator), **D4.6.1v2** (`NameId` is the semantic property
identity), **D4.6.2v2** (evolve NamePool, per-context property-key table, no
arbitrary Mark/Input NameIds), **D5.4.3** (no
context-dependent value at a code-baked address), **D5.4.4** (no
synchronization on repeated execution paths), and **DO16** (name growth and
MIR-cache reconciliation). The `v2` rulings are adopted in
`doc/Lambda_Formal_Design.md` 1.8.0. Cross-context Input ownership/rebinding
remains explicitly deferred under RN-D5.

**Relationship to existing design:** this proposal reuses and simplifies
`vibe/Lambda_Design_Name_Identity.md`. It keeps the implemented NameRecord,
generated catalogs, name classifier, NamePool hierarchy, unique-key
allocation, `PropertyKeySpec`, and per-module key table. It supersedes that
document's pointer-identity decisions NI1, NI7, NI10, and the pointer-specific
parts of NI11–NI15. It also replaces the pointer-based plan in
`JS_Tune3_Name.md`.

The implementation is recorded in [`JS_Tune3_Name.md`](JS_Tune3_Name.md). The
canonical LOC gate is measured by `./utils/count_loc.sh`: R0 `lambda/js` was
227,778 lines and the implemented tree is 225,711 lines, a 2,067-line
reduction. The implementation deletes the pointer-key and shaped-constructor
optimizer paths; it does not move equivalent runtime code to another
production directory.

---

## 1. Decision

> **`NameId` is the sole internal identity of a runtime property name.**
> `NamePool` remains the sole allocator, interner, metadata owner, and
> ID-to-name resolver. A `String*` may materialize a name's spelling, but its
> address is never compared as property identity.

The design deliberately introduces no parallel name system:

- no `PropertyKeyId` sibling type;
- no JS-private interner or name registry;
- no content hash promoted to identity;
- no process-global mutable dynamic-name service;
- no second module relocation format; and
- no separate key carrier struct on property hot paths.

The existing mechanisms are extended in place:

| Existing mechanism | NameId-first extension |
|---|---|
| `NameMeta.predefined_id` | becomes `name_id`; every runtime NameRecord has a non-zero ID |
| `NamePool` spelling hashmap | remains ordinary-string canonicalization authority and assigns runtime IDs |
| generated `[pool16][ordinal16]` `NameId` | remains the ID format; the lower pool16 half holds generated/static segments and the upper half holds dynamic segments |
| `PropertyKeySpec` | remains generated-ID-or-spelling module metadata |
| `LambdaModuleState.property_keys[]` | becomes a dense `NameId[]` link table |
| `ShapeEntry.predefined_id + key_ref` | becomes one `NameId name_id` identity field |
| NameRecord `String` image | remains spelling, hash metadata, diagnostics, and reflection materialization |

This changes identity, not JavaScript semantics. ECMAScript string equality,
Symbol uniqueness, private-name scoping, property ordering, Proxy behavior,
array-index rules, descriptors, and reflection remain unchanged.

## 2. Why the existing design should be extended

Most of the required substrate is already present:

1. `NamePool` already provides hierarchical, length-aware content interning
   and pool-scoped lifetime
   ([name_pool.cpp](../../lambda/core/name_pool.cpp)).
2. Every pooled result already has a fixed `NameMeta` prefix and shared
   ordinary-name classification
   ([name_identity.h](../../lambda/core/name_identity.h)).
3. Generated markup, Lambda, and JS/DOM catalogs already use 32-bit
   `[pool16][ordinal16]` NameIds.
4. JS Symbols and private names already allocate unique NameRecords through
   NamePool instead of content-interning their diagnostic spelling.
5. `PropertyKeySpec` already seals either a generated ID or ordinary spelling
   into a compiler-neutral MIR module image
   ([lambda.h](../../lambda/lambda.h)).
6. `LambdaModuleState.property_keys[]` already relinks those specifications
   for each `EvalContext`, and MIR already bakes only a dense table index
   ([runtime-state.cpp](../../lambda/runtime/runtime-state.cpp),
   [transpile-mir.cpp](../../lambda/runtime/transpile-mir.cpp)).

The remaining pointer path is therefore unnecessary duplication. In
particular, JS lowering currently bakes compiler-pool `String*` and mutable IC
addresses into MIR
([js_mir_expression_lowering.cpp](../../lambda/js/js_mir_expression_lowering.cpp)),
while ordinary runtime lookup re-interns strings before comparing
`ShapeEntry.key_ref` ([js_props.cpp](../../lambda/js/js_props.cpp)). That
retains compiler pools, prevents clean cross-context code sharing, and leaves
ID and pointer identity active at the same time.

The existing Lambda module-key pipeline is the model to reuse. The necessary
change is that load-link returns a `NameId`, not a `PropertyKeyRef`, and JS
lowering registers its static property names through the same table.

## 3. Decision ledger

| ID | Decision |
|---|---|
| **RN1** | **One runtime identity:** every property operation, runtime JS shape, and named IC compares `NameId`; pointer equality is forbidden as property identity. Proposed revision: D4.6.1v2. |
| **RN2** | **One authority:** NamePool assigns, owns, and resolves NameIds. Existing generated catalogs, sealed static pools, and dynamic pools are segments of the same abstraction. Proposed revision: D4.6.2v2. |
| **RN3** | **Three allocation origins, one type:** generated catalog IDs are engine-build identities; static script/schema IDs allocate from the lower pool16 half; dynamic IDs allocate from the upper half. Arbitrary static and dynamic IDs are valid only within their owning identity-scope lifetime. |
| **RN4** | **Generated IDs may be MIR immediates; arbitrary IDs are linked values.** Shared MIR bakes the existing dense module-table index for arbitrary static names and loads the context's NameId, whether that ID links to a lower-half static record or an upper-half dynamic record. D5.4.3 forbids baking either context-linked value. |
| **RN5** | **NameRef is materialization, never identity.** NamePool may return a `String*` for spelling, formatting, diagnostics, Proxy arguments, or reflection. No shape, IC, registry, or routing decision compares that pointer. |
| **RN6** | **One ShapeEntry identity field:** every entry born from a JS property operation carries non-zero `name_id`; arbitrary Input-origin entries carry `NAME_ID_NONE` and remain id-less through shape-preserving COW/type transitions. `name_hash` remains hash-only; no bulk document-name promotion occurs. Proposed revision: D3.4.4v2. |
| **RN7** | **Input does not mint runtime IDs.** A schema-backed Input reuses IDs from its retained shared static parent, or from the runtime dynamic parent for a late schema; misses stay Input-local with `NAME_ID_NONE`. A schemaless Input gives every non-generated name an Input-local id-less record. This preserves D4.6.2's million-key document boundary. |
| **RN8** | **Key kind is orthogonal to ID.** Ordinary STRING names content-intern; SYMBOL and PRIVATE names receive fresh NameIds from existing unique NamePool allocation. Equal diagnostic bytes never imply equal IDs. |
| **RN9** | **No mutable process-global interner.** Generated catalogs are immutable process data; static and dynamic NamePools are context/identity-scope owned and single-writer. Shared code is achieved through per-context linking, not synchronized interning. D5.4.4 remains unchanged. |
| **RN10** | **One compile/link pipeline:** initial Lambda/JS documents, scripts, and schemas are scanned into the static pool before it is sealed; module tables then link against it. Eval, `new Function`, REPL, late imports, fresh compilation, and L1 cache re-entry use the same table but allocate a missing spelling only in the dynamic child. No pointer-bearing fast path is allowed. |
| **RN11** | **Hash is routing metadata only.** NamePool owns the cached FNV/identity hash and array-index classification. Hash collision confirmation is NameId equality for runtime shapes and byte equality only at the id-less Input/raw boundary. |
| **RN12** | **Dynamic ID growth is append-only for now.** Upper-half IDs are never reused during the identity-scope lifetime. Allocation count, interner attempts, and new insertions are measured under DO16; dynamic-pool GC/compaction is explicitly deferred. |
| **RN13** | **Parent-first lookup plus sealing establishes priority.** Every ordinary dynamic intern checks the static root first. The root becomes immutable before dynamic allocation begins, so one spelling cannot later acquire both a static and a dynamic ID in the same identity scope. |
| **RN14** | **Pool halves are allocation origin, not identity domains.** Equality does not depend on whether an ID is lower- or upper-half. Existing ancestry records always win; a source-static name first seen after sealing legitimately receives an upper-half ID. |
| **RN15** | **Arbitrary IDs are scope-confined.** The initial implementation supports ID-bearing Inputs only in their creating identity scope. Cross-context ownership, cache reuse, rebinding, and enforcement are deferred as RN-D5; a bare 32-bit ID has no scope tag. |
| **RN16** | **Mutable pools remain single-writer.** Synchronous parsing on the owner thread may inherit the dynamic pool. Before worker dispatch, the owner constructs the Input-local child and retains only the sealed static root; the worker mutates neither parent state nor its refcount, and absent names remain id-less. No completion-time rebinding is required, and D5.4.4 adds no hot-path lock. |

## 4. Identity scopes and encoding

### 4.1 NameId remains 32-bit

The current representation remains:

```text
NameId = [ pool number:16 ][ ordinal:16 ]
NAME_ID_NONE = 0
```

Pool-number ownership is extended without changing the type:

| Pool-number range | Meaning | Stability |
|---|---|---|
| `0` | generated markup/presentation catalog | engine catalog |
| `1` | generated Lambda catalog | engine catalog |
| `2` | generated JS/DOM catalog | engine catalog |
| `3..0x7fff` | static document/script/schema NamePool segments | sealed identity-scope lifetime |
| `0x8000..0xffff` | dynamic context-local NamePool segments | owning identity-scope lifetime |

The static allocator starts after the generated-catalog count for the current
engine build (currently pool numbers 0–2). A future generated catalog consumes
the next low pool number and advances that build's static floor. This is safe
because arbitrary static IDs are always relinked and never persisted directly.

Ordinal zero is reserved; each segment therefore holds at most 65,535
records. A static or dynamic NamePool opens another logical segment in its own
half when the current one fills. Segment tables are owned by the identity-scope
root and are not process registries.

The split records where an ID was first allocated; it does not create two
ordinary-string identity domains. Parent-first interning must return an
existing lower-half static ID before considering upper-half allocation. Once
the static root is sealed, later source-static names from eval, dynamic
import, or lazy schema loading use an existing ID if found and otherwise
allocate in the upper half.

Static and dynamic segment numbers may repeat in another `EvalContext` or
process. Those arbitrary IDs are never compared across identity scopes, and
share-nothing isolates do not exchange runtime objects. Generated IDs retain
the same value in every context using the same catalog. Arbitrary lower-half
IDs are therefore not a persistence ABI merely because they are called
static.

The existing generator remains the one cross-catalog spelling authority. An
ordinary generated spelling has exactly one owning NameId across markup,
Lambda, and JS/DOM pools; another subsystem aliases that owner rather than
emitting a duplicate record. `generate_well_known_names.py` already rejects a
duplicate global spelling. Without this invariant, direct NameId comparison at
the JS/Input seam would make equal generated bytes unequal.

### 4.2 What “shared MIR” means

Three cases must not be conflated:

| Name source | MIR representation | Cross-context/process behavior |
|---|---|---|
| generated predefined name | direct NameId immediate or module-table entry | same ID when catalog fingerprint matches |
| arbitrary static source name in initial closure | baked dense index into module NameId table | each context links its lower-half static ID from sealed spelling |
| static source name loaded after sealing | baked dense index into module NameId table | reuses an existing ID or links a new upper-half dynamic ID |
| computed runtime name | runtime NamePool result | context-local; never persisted in code |

Thus an arbitrary name such as `customerField` does not need the same numeric
static or dynamic ID in two processes. With respect to name identity, the MIR
module remains shareable because it contains the spelling in `PropertyKeySpec`
and a dense table index in code. Each module instance links that spelling into
its context's NamePool before execution. Only generated catalog IDs are durable
MIR immediates; the broader cache/module-cell contract remains D5.4.3 and DO16.

Making arbitrary numeric IDs durable across processes would require a
persistent global allocator, a collision-management protocol, or a global
catalog update. All are new mechanisms and are rejected here.

### 4.3 SectionNameId remains location metadata

`SectionNameId` remains a serialized `[slot16][offset16]` location for a
future compact name-section encoding. It is never compared as identity. The
currently implemented `PropertyKeySpec` offset/length spelling image is
already sufficient for this proposal and should not be replaced merely to
land NameId-first runtime identity.

At load-link, either representation resolves through NamePool and yields a
runtime `NameId`. No SectionNameId reaches an execution helper or ShapeEntry.

## 5. NamePool extension

### 5.1 Keep the existing abstraction

NamePool continues to own:

- ordinary spelling lookup through its existing hashmap;
- parent lookup and retain/release lifetime;
- generated-catalog lookup before local allocation;
- NameRecord allocation and shared classification;
- unique Symbol/private record allocation; and
- allocation/statistics integration with `MemContext`.

The existing ordinary creation path already searches the parent before
allocating locally. That behavior is the static-before-dynamic priority
mechanism; it is reused rather than wrapped in a second interner.

NamePool gains only the data needed to assign and resolve arbitrary IDs. The
single static NamePool is also the identity-scope root; all initial sources
merge into it instead of creating sibling static pools. It owns:

- a fixed `NamePoolIdMode` (`IDLESS`, `STATIC`, or `DYNAMIC`);
- a dedicated NamePool backing `Pool` independent of the GC heap, destroyed
  only when the root's existing retain count reaches zero;
- a raw `identity_root` pointer on descendants, whose lifetime is protected by
  the existing retained-parent chain;
- one raw canonical dynamic-child slot plus a permanent `dynamic_started` bit,
  preventing ID-bearing dynamic siblings or child recreation;
- lower-half and upper-half `ArrayList` segment directories;
- the current segment and next ordinal for each allocation class;
- a dense ordinal-to-NameRef table per allocated segment;
- a seal bit that permanently disables new lower-half allocations; and
- general ID-to-record resolution that dispatches generated IDs to existing
  generated arrays and arbitrary IDs through the root's segment directories.

There is no second hashmap. The existing spelling hashmap remains the only
ordinary-name interner.

### 5.2 Pool identity policy

The identity policy is fixed at creation so a published id-less record is
never mutated later:

| Pool use | Parent hit | New ordinary record | Unique JS key |
|---|---|---|---|
| static identity-scope root | generated fallback | lower-half static ID until sealed | not a unique-key authority |
| runtime dynamic child | reuse static/dynamic ancestor ID | upper-half dynamic ID | upper-half dynamic ID |
| schema-backed Input-local child | reuse selected shared static/dynamic parent ID | `NAME_ID_NONE` | not a JS-key authority |
| schemaless Input/Mark pool | generated fallback only | `NAME_ID_NONE` | not a JS-key authority |
| compiler AST pool | generated fallback only | `NAME_ID_NONE` | not a runtime-key authority |
| immutable generated pool | generated ID | no allocation | generated Symbol IDs only |

Generated-catalog lookup remains available to every mode. A schema-backed
Input-local pool retains the exact selected static or dynamic parent used by
the JS runtime; it does not merely point at an unrelated pool that happens to
use the same pool-number half. Numeric equality is safe only within that shared
ancestry and identity scope.

The internal `js_input` used as the JS type/shape arena is not a parsed
schema-backed Input. Its own NamePool remains detached/id-less; JS-created
ShapeEntry spelling views come from `name_pool_ref(context->name_pool, id)`, and
identity comes from the stored ID. Thus it neither allocates IDs nor adds a
second retained parent edge to the runtime name scope.

The existing `name_pool_create()` remains the compatibility constructor for
id-less local pools. `name_pool_create_with_mode()` is the one internal/public
initializer used by the static-root and dynamic-child convenience wrappers;
it rejects invalid topologies at construction:

- `STATIC` has no arbitrary-ID-bearing parent and becomes its own root;
- `DYNAMIC` requires the sealed `STATIC` root as its direct parent, inherits
  that root, and is rejected if `dynamic_started` is already set;
- `IDLESS` may have no parent or may retain a static/dynamic parent, but never
  allocates an arbitrary ID itself; and
- a generated catalog remains an internal fallback, not a mutable parent.

Sealing changes the static root from allocating to lookup-only. No mode may be
changed after creation, and no second NamePool implementation is permitted.
The Runtime holds the canonical dynamic child for its whole active lifetime.
Its raw root slot is cleared if the last dynamic reference is released, but the
permanent bit prevents a second dynamic spelling hashmap from being created for
the same identity scope.

### 5.3 Required hierarchy and seal order

The initial runtime builds one static root from all names known before its
first execution: generated-catalog fallbacks, initial Lambda/JS module
`PropertyKeySpec` spellings, document-script modules, and exact field/element
names in the initial modules' type graphs. The resulting topology is:

```text
immutable generated catalogs
  -> static identity-scope root (lower-half allocation, then sealed)
       -> runtime dynamic pool (upper-half allocation)
            -> late-schema Input-local pool (id-less misses)
       -> startup-schema Input-local pool A (id-less misses)
       -> startup-schema Input-local pool B (id-less misses)

schemaless Input-local pool (no arbitrary-ID-bearing parent)
```

All ID-bearing descendants share the root's segment allocator and resolver.
Merely allocating from the same numeric half is insufficient: two unrelated
pools can assign the same number to different spellings.

#### 5.3.1 Initial closure

The closure boundary reuses the existing loader boundary:

- for a Lambda entry, it is `collect_import_cone(main)` plus `main`;
- for a standalone JS entry, it is the statically discovered ESM batch plus
  its entry module and any scripts the host has already enumerated;
- document scripts already collected before document execution join the same
  batch; and
- eval, `new Function`, runtime `require()`, dynamic `import()`, and modules
  discovered after first entry are late by definition.

Compilation and MIR linking finish first. Compiler AST NamePools remain id-less
and no user/runtime code executes during this stage. Every resulting sealed
module exposes `PropertyKeySpec` and IC counts through its
`LambdaModuleLayout`; the loader retains the existing script/module
`type_list` for applicable Lambda/schema roots. Prelink therefore does not need
a second metadata format.

The JS loader makes its existing compile-only capability the phase boundary:

1. parse the entry and recursively discover its static ESM imports, plus
   document scripts already known by the host;
2. freeze the loader's stable discovery/catalog order and have the owner thread
   assign module-state IDs from the existing `Runtime::next_module_state_id`
   allocator in that order;
3. compile and link **every** discovered dependency and the entry with its
   assigned ID, without executing a module, constructing the realm, installing
   DOM bindings, or allocating a JS module-state slab;
4. finalize one `LambdaModuleLayout` per compiled unit and register the layouts
   in that same stable order;
5. run the fresh-runtime transaction in §5.3.2 once over the complete layout
   list; and
6. only then construct the realm and hand the prepared graph to the existing
   ESM evaluation protocol, followed by the entry under its prepared module
   state.

Dynamic `import()`, CommonJS `require()` (including a literal call in a
conditional branch), resolver callbacks that run only during execution, and
host scripts added after this boundary are late modules. Precompiling an
unexecuted CommonJS branch could otherwise make file-resolution failure
observable, so it is deliberately not part of static closure discovery. Late
modules use the already-published dynamic pool through §6.4.

This is a sequencing change to the existing JS batch, not a second loader. The
current `jm_precompile_js_imports()` compiles and executes one depth at a time;
it must retain all compile-only node artifacts until the whole initial graph is
finalized, then invoke its existing evaluation path. Cycles and top-level await
do not define a topological execution list: their current SCC/async-parent
evaluation protocol remains authoritative, but it starts only after the same
seal/instantiate boundary. The single-module path uses that boundary after
`MIR_link`/`find_func` and before
`js_get_global_this`, DOM/event-loop initialization, `js_activate_module_state`,
or `js_main`.

During compile-only work, `Runtime::name_pool` and
`EvalContext::name_pool` remain unpublished on a fresh runtime. Parse, AST,
lowering, profile-label, and compiler-generated temporary names use only the
existing id-less `JsTranspiler::name_pool`. Any compile helper that currently
consults the runtime pool is redirected to that existing compiler pool. The JS
runtime `Input`, realm, and other services that can create semantic names are
initialized only after the dynamic child is published. This prevents bootstrap
names from consuming upper-half IDs before the static closure has been sealed.

#### 5.3.2 Fresh-runtime transaction

For the first entry on a fresh `Runtime`:

1. create an unpublished `STATIC` NamePool root;
2. iterate the existing initial-closure list in module-state-id order;
3. validate and intern every ordinary `PropertyKeySpec` spelling into the
   static root; generated specs require no allocation;
4. walk each module's static type graph using §7.7 and intern its exact names;
5. if any scan/allocation fails, destroy the unpublished root and enter no user
   code;
6. seal the root irreversibly and create an unpublished `DYNAMIC` child;
7. use that child explicitly to build the final module-state catalog and
   instantiate every `LambdaModuleState` NameId/IC slab;
8. if any state fails, destroy the staged states through the existing
   module-state lifecycle, release the dynamic child/root, and enter no user
   code;
9. commit the staged catalog, publish the pools as
   `Runtime::static_name_pool` and the existing `Runtime::name_pool`, and attach
   the dynamic child to `EvalContext::name_pool` in one owner-thread step;
10. initialize JS realm/Input/DOM/event-loop services, if this is a JS runtime;
   and
11. execute module initialization through the language loader's existing
    cycle/TLA-aware evaluation order, followed by the entry point.

The root's spelling hashmap naturally deduplicates names across modules and
schemas, so no prelink set or new registry is needed. Module order may affect
arbitrary numeric IDs, which is acceptable because those IDs are relinked and
never persisted.

The staging operation is a batch wrapper over the existing
`lambda_module_state_prepare_layout`/destroy lifecycle, not another state
representation. Its linker takes the candidate `NamePool*` explicitly instead
of reading `context->name_pool`; this allows the exact final
`LambdaModuleState**` catalog and its registered root ranges to be built before
publication. Rollback unregisters those ranges through the existing destroy
path. The ordinary late-module path uses the same builder for one state and
publishes that state only after its NameId table and IC slab both succeed.

JS graph discovery snapshots `Runtime::next_module_state_id`. Assigned IDs and
compiled layouts remain unpublished until every compile-only unit succeeds. A
compile failure releases all graph artifacts and restores that watermark on the
owner thread; no worker allocates IDs and no live module can observe reuse.

If a `Runtime` already owns a sealed root, a later execution does not reopen or
replace it. Every newly compiled module links through the existing dynamic
child. This makes retained sessions, REPL entries, cache hits, and lazy modules
follow one post-seal rule.

### 5.4 NameMeta

The 16-byte prefix keeps its layout; only the final field's meaning broadens:

```c
typedef struct NameMeta {
    uint32_t hash;
    uint32_t array_index;
    uint16_t flags;
    uint8_t key_kind;
    uint8_t reserved;
    NameId name_id;
} NameMeta;
```

Invariants:

- generated records carry their generated ID;
- initial static ordinary records carry a lower-half static ID;
- runtime ordinary, Symbol, and private records carry an upper-half dynamic
  ID unless an ordinary spelling resolves to an existing static ancestor;
- arbitrary Input/compiler records carry `NAME_ID_NONE`;
- a record's ID is assigned before publication and never changes;
- a NameId is never reused during its owning NamePool lifetime; and
- `hash`, `array_index`, and `key_kind` are metadata, not alternate identity.

The current `predefined_id` spelling is renamed because leaving that name
would make dynamic IDs look invalid or exceptional throughout the runtime.

### 5.5 API direction

The API exposes policy and resolution through NamePool rather than JS-owned
tables:

```c
typedef enum NamePoolIdMode {
    NAME_POOL_IDLESS,
    NAME_POOL_STATIC,
    NAME_POOL_DYNAMIC,
} NamePoolIdMode;

NamePool* mem_name_pool_scope_create(const char* label);
NamePool* name_pool_create_with_mode(Pool* backing, NamePool* parent,
                                     NamePoolIdMode mode);
NameId name_pool_intern_id(NamePool* pool, StrView spelling);
NameId name_pool_create_unique_id(NamePool* pool, uint8_t key_kind,
                                  StrView diagnostic_name);
NameRef name_pool_ref(NamePool* pool, NameId id);
const NameMeta* name_pool_meta(NamePool* pool, NameId id);
bool name_pool_seal_static(NamePool* pool);
NamePool* name_pool_static_root(NamePool* pool);
```

`name_pool_intern_id` performs generated lookup, parent lookup, existing local
lookup, and policy-selected allocation in the same order as today's NamePool
path. A dynamic pool never asks a sealed static parent to allocate; it only
reuses an existing ancestor record. The unique operation accepts only SYMBOL
or PRIVATE, bypasses the spelling hashmap, and is valid only in a dynamic pool.
Generated lookup is key-kind aware: ordinary interning accepts only a generated
`NAME_KEY_STRING` record. For example, the ordinary string
`"Symbol.iterator"` must not resolve to the generated well-known Symbol merely
because its diagnostic bytes match.

`mem_name_pool_scope_create()` is the only `STATIC` constructor. It creates a
dedicated root-context backing Pool, creates the static root inside it, and
marks that root as the backing owner. `DYNAMIC` allocation uses the same root
backing; an Input/compiler `IDLESS` pool continues to use its caller-owned
backing. The factory registers both allocator and logical NamePool nodes under
MemContext as required by D4.2.1v2. When the static root's retain count reaches
zero, `name_pool_release` first releases its hash/segment metadata and
MemContext registration, then destroys that dedicated backing Pool. It must not
touch the root afterward.

This is the existing NamePool retain/release relation extended to cover the
memory it already claims to own; it is not a second scope registry. In
particular, a schema-backed Input may keep its selected parent records alive
without retaining the GC heap or relying on Runtime teardown order.

When the dynamic child's count reaches zero, its release path clears the root's
raw child slot before releasing its retained static parent. The root is still
alive at that point because of that parent edge; `dynamic_started` remains set.

`name_pool_create()` remains an `IDLESS` compatibility wrapper.
`mem_name_pool_create()` gains the same mode argument internally so memory
accounting and retain/release continue through the existing factory. Callers
must not initialize NamePool fields directly, and generic callers cannot create
a `STATIC` root over an externally destroyed backing Pool.

Existing pointer-returning creation APIs may remain as compatibility and
materialization wrappers, but they call the same allocation/interner path.
They do not establish a second identity relation.

### 5.6 Segment allocation, resolution, and failure

Within its dedicated backing, the static root owns two `ArrayList` directories
of `NameIdSegment*`. Segment index determines pool number: the static directory
starts at the current generated-catalog count, while the dynamic directory
starts at `0x8000`. Each segment owns a dense `NameRef` array whose index is the
16-bit ordinal; slot zero is permanently empty.

Allocation rules are fixed:

1. generated, parent, and current-pool lookups run before reserving an ID;
2. a new static/dynamic record reserves the next ordinal in the appropriate
   directory, opening the next segment after ordinal `0xffff` is assigned;
3. the NameRecord receives that ID before either the spelling hashmap or the
   resolver slot publishes it;
4. publication makes the hashmap entry and resolver slot visible on the same
   owner thread; and
5. an allocation failure may retire a reserved ordinal, but it never publishes
   a partial record and never reuses that ordinal.

`name_pool_ref/meta` first dispatch generated pool numbers to the existing
catalog arrays. For arbitrary IDs it derives the directory index from pool16,
rejects absent segments/zero or out-of-range ordinals, and returns the dense
slot. It never scans spellings.

Exhausting either pool-number half or allocation memory returns
`NAME_ID_NONE`, logs a distinct `name-id static exhausted` or
`name-id dynamic exhausted` prefix, and reports `ERR_OUT_OF_MEMORY` at the
caller boundary. Module/schema prelink fails transactionally; a runtime
property operation propagates the existing error lane. Wrapping, widening,
falling back to pointer identity, and ID reuse are forbidden.

## 6. Module and MIR pipeline

### 6.1 Sealed module representation stays intact

`PropertyKeySpec` remains compiler-neutral:

```c
typedef struct PropertyKeySpec {
    uint32_t predefined_id;
    uint32_t name_offset;
    uint32_t name_length;
    uint32_t reserved;
} PropertyKeySpec;
```

- `predefined_id != NAME_ID_NONE`: a generated catalog ID;
- otherwise `name_offset/name_length`: an ordinary static spelling in the
  sealed module image.

Dynamic runtime IDs are never written into persistent `PropertyKeySpec`.
Generated IDs require the existing catalog fingerprint check.

One shared validator is used by static prelink and module instantiation. It
requires `reserved == 0` and exactly one encoding:

- a generated form has a pool number below the generated-catalog count,
  resolves through the current fingerprinted catalog, has zero spelling
  offset/length, and is never PRIVATE; or
- an ordinary form has `predefined_id == NAME_ID_NONE`, an offset after the
  complete spec array, checked `length + 1` bounds within
  `property_key_bytes_size`, and a trailing NUL after the exact bytes. Embedded
  NUL bytes are allowed because length, not C-string termination, defines a JS
  property spelling.

An arbitrary lower/upper-half ID in a sealed spec, overlapping/out-of-bounds
bytes, an unknown generated ID, or a mixed form rejects the module before any
runtime state is published. Duplicate valid specs are permitted and resolve to
the same NameId through NamePool.

### 6.2 Module state becomes a NameId table

The existing per-context table changes element type and name:

```c
typedef struct LambdaModuleState {
    Item* vars;
    uint64_t* var_payloads;
    NameId* property_name_ids;
    void* js_ic_slab;
    uint32_t js_load_ic_count;
    uint32_t js_store_ic_count;
    /* existing fields */
} LambdaModuleState;
```

The sealed layout adds context-neutral IC sizing metadata:

```c
typedef struct LambdaModuleLayout {
    /* existing module/property fields */
    uint32_t module_state_abi;
    uint32_t js_load_ic_count;
    uint32_t js_store_ic_count;
    uint32_t js_load_ic_offset;
    uint32_t js_store_ic_offset;
    uint32_t js_ic_bytes;
} LambdaModuleLayout;
```

Offsets are aligned compile-time constants derived from `sizeof(JsLoadIC)` and
`sizeof(JsStoreIC)`. The runtime layer allocates one zeroed opaque slab of
`js_ic_bytes`; it does not maintain a JS IC registry or interpret individual
cells. MIR derives a site address as `state->js_ic_slab + class_offset +
site_index * sizeof(IC)`. Site indices are assigned monotonically and
separately for load and store sites during lowering.

`state` is always `context->module_states[owning_module_id]`, where the sealed
module ID is baked just like the existing Lambda module cell. IC lookup never
uses `active_js_module_state`: an exported function, callback, or async
continuation may execute while another module is active, but its IC sites still
belong to the module that compiled them.

The IC layout is canonical rather than compiler-chosen:

```text
load_offset  = 0
store_offset = align_up(load_count * sizeof(JsLoadIC), alignof(JsStoreIC))
ic_bytes     = store_offset + store_count * sizeof(JsStoreIC)
```

A shared `js_ic_layout_compute()` helper performs checked multiplication,
alignment, and addition in both finalization and load validation. The loader
recomputes the three values from the sealed counts and requires exact equality;
`module_state_abi` also fingerprints both IC sizes/alignments and the relevant
`LambdaModuleState` offsets. When both counts are zero, all offsets/bytes are
zero. Thus a malformed cache entry cannot make MIR index beyond its allocation.

JS stops reserving an anonymous state at execution time. Before compilation,
the owner thread assigns each JS unit a module-state ID through the existing
Runtime allocator. Finalization writes that ID and the
variable/property/IC counts into its `LambdaModuleLayout` and exposes the same
`_mod_layout` artifact consumed by `prepare_context_module_state()` on Lambda
MIR. Compile-only workers receive an assigned ID and return a sealed layout;
they never increment `Runtime::next_module_state_id` or allocate context state.

Before the initial static pool is sealed, the loader scans every
`PropertyKeySpec` in the initial module closure and interns each arbitrary
spelling into that pool. At module instantiation:

1. validate the sealed `PropertyKeySpec` image;
2. validate `module_state_abi`, IC counts, canonical offsets,
   multiplication/addition overflow, alignment, and `js_ic_bytes`;
3. allocate an unpublished NameId table and zeroed IC slab before mutating a
   live dynamic NamePool;
4. for each spec, use a generated ID directly or intern its spelling through
   the explicit candidate/runtime NamePool, whose parent-first lookup returns
   the preallocated static ID when present and whose dynamic policy handles a
   late-module miss;
5. store the returned NameId at the same dense index in the temporary table;
   and
6. publish both allocations to `LambdaModuleState` only after all steps
   succeed, otherwise free the temporary allocations and reject the module.

For an initial fresh-runtime batch, failure destroys the entire unpublished
name scope as specified in §5.3.2. For a late module in a live scope, names
successfully interned before a later failure remain canonical dynamic records;
the module state is still unpublished and those NameIds are never reused. This
is the existing append-only NamePool transaction boundary, is counted as
failed-link growth under RN12/DO16, and does not add a rollback interner.

An L1 in-memory cache hit must execute this linking step for every new
`EvalContext`; retaining a prior context's table is invalid. This preserves
DO16 and D5.4.3.

`lambda_module_state_reset()` clears the IC slab as well as variable storage.
`lambda_module_state_destroy()` frees the NameId table and slab. Direct eval
may grow variable storage, but its sealed property/IC counts never mutate; a
new eval layout receives a new module-state ID.

### 6.3 MIR operands

Generated MIR carries property names as integer values:

- native/generated well-known routing may use a direct NameId immediate;
- arbitrary static source names load
  `module_states[owning_module_id]->property_name_ids[k]`, where both the module
  cell and dense index are sealed code constants;
- computed names obtain a NameId at runtime; and
- private names load the ID stored in the runtime private environment.

`NameId` storage is unsigned 32-bit. MIR table loads and immediates zero-extend
to the runtime's integer argument lane; they never sign-extend bit 31, because
every dynamic ID is in the `0x80000000..0xffffffff` range. Native helper
internals use `NameId`; JIT-facing wrappers accept the existing unsigned
64-bit integer lane, reject non-zero high 32 bits, and cast once. The
private-environment Item encoding similarly uses the non-negative `uint32_t`
value widened to the existing integer Item lane and checked on decode.

No MIR instruction may embed:

- a compiler-pool `String*` as property identity;
- a runtime NameRecord pointer;
- a context-local dynamic NameId immediate;
- a mutable `JsLoadIC*`/`JsStoreIC*` allocated from the compiler pool; or
- a SectionNameId passed directly to a runtime helper.

The current Lambda `module_property_key_index()` registration and
`PropertyKeySpec` finalization are reused by JS lowering. They should be
promoted into a shared runtime/transpiler API rather than copied into the JS
transpiler.

### 6.4 Eval, REPL, and dynamic compilation

Eval, `new Function`, REPL, CommonJS loading, and ESM loading seal the same
in-memory module layout and instantiate its NameId table before entry. Names
in the initial load closure are lower-half static; names first discovered
after runtime startup reuse an existing static/dynamic ID or allocate through
the upper-half dynamic pool. A freshly compiled module does not bypass linking
merely because its compiler and runtime happen to share a process.

This keeps one path and allows compiler AST/name pools to be released once no
other compiler metadata needs them.

## 7. JS property pipeline

### 7.1 Static named access

For `object.field` and a non-computed literal key:

1. lowering registers `field` in the module's existing property-key list;
2. MIR emits its generated ID or dense module-table index;
3. the property helper receives `NameId`;
4. shape probing obtains cached hash/array-index metadata through NamePool;
5. the final runtime-shape comparison is `entry->name_id == key_id`; and
6. IC installation stores the NameId, shape, slot, and descriptor facts in
   the context-owned IC cell.

Static named access performs no spelling hashmap lookup after module
instantiation and no string allocation or boxing on the ordinary path.

Registration covers every site lowering treats as a static ordinary key:
member identifiers, decoded string-literal keys (including `obj["x"]`),
non-computed object/class fields and methods, and static destructuring keys.
Numeric/BigInt literal keys are registered only after the existing
ECMAScript-exact key-string conversion produces sealed bytes; if lowering
cannot prove that conversion, it keeps the ordinary computed-key path instead
of inventing compiler spelling rules. Private declarations register their
ordinary lexical source key for the class environment, but their fresh PRIVATE
identity remains runtime-only under §8.3. Well-known Symbols use their generated
IDs. No registration scans raw source text or changes evaluation order.

### 7.2 Computed access

ECMAScript `ToPropertyKey` remains the semantic conversion boundary. The
generic property helper then derives a NameId without introducing a new key
carrier:

| ToPropertyKey result | NameId derivation |
|---|---|
| pooled ordinary String with non-zero ID | read `NameMeta.name_id` |
| plain or pooled id-less String, including Input | `name_pool_intern_id` by exact bytes |
| well-known Symbol | generated Symbol NameId |
| dynamic Symbol | existing symbol registry's NameId |
| private lexical reference | runtime private environment's NameId; never public ToPropertyKey |

The original `Item` remains available to generic operations that must invoke
a Proxy trap. Static NameId helpers materialize the corresponding string or
Symbol only if an exotic/Proxy path actually requires the observable key
value.

The conversion/error seam is one native helper, not a new persistent carrier:

```c
Item js_resolve_property_key(Item input, NameId* out_name_id);
```

Under D1.5, the computed wrapper opens one existing `RootFrame` for the
receiver, input, and observable result. The helper performs `ToPropertyKey`, roots any internal
temporaries, resolves or interns the NameId, and returns the observable Item;
the wrapper installs it in its reserved root slot before any further allocating
call. Failure returns the existing JS error Item and leaves `out_name_id` as
`NAME_ID_NONE`. Existing computed get/set/has/delete/define wrappers use this
stack-local pair and then call one shared NameId core. Static MIR calls narrow
`*_name_id` entry points with only the linked ID; if the core reaches a Proxy,
it materializes the observable key through §8.4. Thus the two ABI entry shapes
share all lookup/descriptor semantics, while the static hot path does not box a
key or carry a dummy Item.

No side cache is added to plain GC Strings in this proposal. If repeated
computed-string interning remains material after the static-name migration,
measure it first under RN12; extending String or adding a side cache is a
separate decision.

### 7.3 Indexed elements

Dense Array and TypedArray element access keeps its existing integer/indexed
fast path. NameId governs the named-property path, not element storage.

When a string key reaches named lookup, NamePool's cached `array_index`
classification selects the indexed path where ECMAScript requires it.
TypedArray canonical numeric-index-string behavior remains its own semantic
classifier and must not be replaced by ordinary array-index metadata.

### 7.4 Shapes

`ShapeEntry` becomes:

```c
typedef struct ShapeEntry {
    StrView* name;
    Type* type;
    int64_t byte_offset;
    struct ShapeEntry* next;
    Target* ns;
    struct AstNode* default_value;
    uint32_t name_hash;
    NameId name_id;
    uint8_t flags;
} ShapeEntry;
```

Every ShapeEntry created for a JS semantic property publishes a non-zero
NameId. An arbitrary Input-origin entry retains `NAME_ID_NONE`, including when
a shape-preserving COW clone or an incompatible-value type transition copies
that entry into runtime-owned storage. A new JS-added entry still receives a
non-zero ID. This per-entry origin rule prevents one mutation from promoting a
large document's untouched names into runtime identity state. The `name` view
remains for property enumeration, diagnostics, formatters, and the Input seam;
it is not identity.

Lookup confirmation is:

1. runtime entry with non-zero `name_id`: compare NameId;
2. generated or schema-backed Input entry with non-zero `name_id`: compare
   NameId within its retained identity scope;
3. arbitrary Input entry with `NAME_ID_NONE`: compare hash, length, and bytes;
4. SYMBOL/PRIVATE versus an id-less entry: never byte-match.

One shared runtime helper owns this seam:

```c
bool shape_entry_matches_name_id(const ShapeEntry* entry,
                                 NamePool* names, NameId key_id);
ShapeEntry* typemap_hash_lookup_name_id(TypeMap* map,
                                        NamePool* names, NameId key_id);
```

`typemap_hash_lookup_name_id` resolves `NameMeta` once, probes with its cached
hash, and calls `shape_entry_matches_name_id` for collision confirmation. The
matcher follows exactly this algorithm:

1. reject `NAME_ID_NONE` or an ID that `name_pool_meta` cannot resolve;
2. if `entry->name_id != NAME_ID_NONE`, compare only the two NameIds;
3. otherwise require `meta->key_kind == NAME_KEY_STRING`;
4. reject hash or length mismatch; and
5. resolve the NameRef and compare exact bytes.

A non-zero mismatch never falls back to bytes. In one identity scope, equal
ordinary spellings cannot have different IDs; allowing fallback would collapse
Symbol/private uniqueness and hide allocator defects. Existing raw C-string
lookup remains the byte API for parsers/formatters and does not intern; when it
is applied to a mixed runtime shape, it skips every non-STRING NameId rather
than matching Symbol/private diagnostic bytes.

Map, element, array-property, descriptor, and ordinary host-object paths call
this helper rather than implementing local variants. A legacy host callback
that still requires bytes gets them through one adapter after NameId routing.
Proxy operations retain the original `ToPropertyKey` Item for the observable
trap argument; the internal NameId is never exposed.

This removes `PropertyKeyRef key_ref` and the current predefined-ID versus
pointer-ID split while preserving the D3.4.4 document boundary.

`TypeMapTransition` follows the same rule and gains `NameId name_id` while
retaining spelling/hash fields only for id-less Input construction:

```c
typedef struct TypeMapTransition {
    const char* name;
    uint32_t name_len;
    uint32_t name_hash;
    NameId name_id;
    TypeId value_type;
    uint8_t flags;
    TypeMap* target;
    struct TypeMapTransition* next;
} TypeMapTransition;
```

A JS property/type transition receives the already-resolved operation NameId
and stores it even when the matched source entry is Input-origin and id-less;
the copied target entry itself remains id-less. Parser-built transitions store
the parent-derived ID when present or `NAME_ID_NONE` otherwise. One transition
matcher applies the same contract as `shape_entry_matches_name_id`: a non-zero
transition compares only NameId; an id-less transition requires an ordinary
STRING key and confirms hash/length/bytes. Pointer equality is not a transition
identity shortcut, and a non-zero mismatch never falls back to spelling.

### 7.5 Inline caches

Named IC metadata stores `NameId key_id`, never name bytes, `key_item`, or a
NameRecord pointer. `JsLoadIC` and `JsStoreIC` drop `name`, `name_len`, and
pointer-valued key identity; their receiver/shape/slot entry arrays remain.
Static named and computed-key ICs both cache the post-`ToPropertyKey` NameId.
The original Item stays in the calling helper only when a Proxy/exotic slow
path needs the observable key.

Each lowering site receives a dense load/store site index. MIR selects the
site's owning module state by its sealed module ID and computes the cell from
that state's `js_ic_slab` using the sealed offsets in §6.2. It may embed the
site index, `sizeof(IC)`, and a pointer to immutable profile-label bytes stored
in the sealed MIR image; it may not embed the mutable cell address or
compiler-pool label storage. The first profiling call may copy that immutable
label pointer into the zeroed context cell.

An IC hit guards receiver kind and shape and loads the known slot. A key guard,
when needed, is integer `key_id` equality. IC reset zeroes the whole slab, so
no stale shape/entry pointers survive module-state reset.

IC cells themselves must move to the existing per-context module-state slab
required by D5.4.3. Replacing the key pointer while continuing to bake a
compiler-owned mutable IC address would not make MIR shareable.

The module-state slab and layout contract in §6.2 are the only IC ownership
mechanism; no per-transpiler or process-global side table remains.

### 7.6 Schema-backed and schemaless Input

“Dynamic Input name” means an Input-local record with Input lifetime, not an
upper-half runtime NameId. Assigning Input-local numeric IDs would be unsafe:
an unrelated JS identity scope could assign the same number to another
spelling, while direct NameId equality assumes a shared allocator/resolver.

For a schema whose type graph belongs to the initial closure, §5.3 has already
interned every exact name in the sealed static root. Its Input-local NamePool
retains that root as parent, reuses parent IDs on a hit, and allocates every
miss only as an Input-local NameRecord carrying `NAME_ID_NONE`.

Open-map keys, pattern-derived names, and fields absent from the schema are
misses. Schema validation still decides whether such a field is allowed;
NameId assignment does not change validation semantics. A name already known
from another initial script/schema may also reuse its static ID, which is safe
and does not grow identity state.

A schema supplied after the static root is sealed cannot retroactively mint
lower-half IDs. Synchronous `input()` on the owning context thread first walks
that schema through the dynamic child, then gives the Input-local id-less pool
that dynamic child as parent. Existing static/dynamic spellings are reused and
new exact schema names receive upper-half IDs; local misses remain
`NAME_ID_NONE`.

A parser running on a worker never receives the mutable dynamic pool. Before
dispatch, the owner thread constructs the Input-local pool and retains the
sealed static root; ownership of that prepared Input is then transferred
exclusively to the worker for parsing. The worker performs only immutable
parent lookups and local id-less allocation—it does not retain/release the
parent or mutate its refcount. Startup-schema/static hits still receive IDs;
late-schema names absent there remain id-less. The completed Input is not
rebound. This deterministic loss of an optimization preserves D5.4.4 and keeps
worker parsing independent of context mutation.

A schemaless Input has no arbitrary-ID-bearing parent. Generated-catalog hits
may retain their generated IDs, but every other field is an Input-local
`NAME_ID_NONE` record. It neither probes nor grows the JS runtime NamePool.

Comparison at the JS/Input seam is therefore:

```text
Input entry has non-zero name_id
  -> compare NameId directly (same generated catalog or static root)

Input entry has NAME_ID_NONE and JS key is ordinary STRING
  -> resolve/materialize the JS NameId through NamePool
  -> reject hash/length mismatches, then compare exact bytes

Input entry has NAME_ID_NONE and JS key is SYMBOL or PRIVATE
  -> not equal
```

The byte fallback is a comparison operation, not an interning operation. It
must not allocate an upper-half ID merely to ask whether a runtime JS name
matches an id-less document field. Conversely, when an Input string is used
as an actual computed JS property key, ECMAScript `ToPropertyKey` legitimately
interns it into the runtime dynamic pool as described in §7.2.

A schema-backed Input carrying an arbitrary non-zero ID is supported only in
its creating identity scope in the first implementation. The current
process-global `InputManager` remains a storage-lifetime owner, not a parsed
cross-context cache. Cross-context cache ownership, enforcement, and rebinding
are explicitly deferred as RN-D5; this proposal does not claim such transfer
is supported.

### 7.7 Exact schema/type-name extraction and parser wiring

One shared `lambda_type_prelink_names(Type*, NamePool*)` walker is used by both
initial-closure prelink and late synchronous schemas. It uses an `ArrayList` of
visited Type pointers to terminate aliases and recursive graphs; no schema-name
registry is added. It reuses `type_field_unwrap_simple_decl()` and
`type_is_global_meta_type()` rather than recasting every `LMD_TYPE_TYPE` value
as `TypeType`, then dispatches compound variants by their existing `TypeKind`.
The walker:

- unwraps simple `TypeType` aliases;
- visits both `TypeBinary` operands, a `TypeUnary` operand, and a
  `TypeConstrained` base;
- visits `TypeArray::nested`;
- for `TypeMap`, `TypeElmt`, and `TypeObject`, interns every exact
  `ShapeEntry.name`, recurses into every field type, and interns an exact
  `TypeElmt.name` when present;
- relies on the complete shape for inherited object fields and does not scan
  methods; and
- skips regex/pattern-produced names, open-map future keys, values, source
  aliases with no distinct shape spelling, and every other non-exact name.

Unions contribute the union of exact names. Multiple schemas/modules simply
walk into the same NamePool and are deduplicated by its existing spelling
hashmap. Failure returns false without sealing/publishing a fresh root or
starting Input I/O. A failed walk over a fresh candidate destroys that root; a
failed late walk may leave already-interned dynamic records under the same
append-only failure rule as §6.2.

The walker does not write context-linked IDs back into compiler/sealed
`Type*`, `ShapeEntry`, or `TypeElmt` objects. That would put an arbitrary
NameId at a code/shared-metadata address forbidden by D5.4.3. Its only effect is
preallocating records in the candidate NamePool; a parsed Input obtains the ID
later through parent lookup, while the shared schema graph may continue to use
its spelling at validation time.

Parser plumbing is explicit rather than thread-local:

```c
Input* Input::create(Pool* pool, Url* url, Input* document_parent,
                     NamePool* name_parent = NULL);
Input* input_from_target(Target* target, String* type, String* flavor,
                         NamePool* name_parent);
```

The canonical Input dispatcher propagates `name_parent` through local, URL,
and source parsing; compatibility wrappers pass NULL. `Input::create` passes
it to the existing `mem_name_pool_create` parent argument while keeping
`Input* document_parent` solely for document ownership.

The dispatcher is factored at its existing create/parse boundary so a worker
entry can receive an already-created `Input*` and parse into it. Only the owner
calls `Input::create(..., sealed_static_root)` before enqueue; failure to enqueue
destroys that prepared Input on the owner. Worker completion transfers the same
Input back without another NamePool retain/release. This is exclusive object
handoff, not shared mutable NamePool access.

`fn_input2` already extracts `schema_type` before calling `input_from_target`.
It now prelinks that type as described above, selects dynamic parent for a
synchronous owner-thread parse or sealed-static parent for a worker, and passes
the parent before parsing. Validation still runs afterward against the same
`schema_type`; NameId prelink never changes schema admission.

The current implementation does not yet wire this hierarchy:
[`Input::create`](../../lambda/input/input.cpp) creates its NamePool with no
NamePool parent, schema validation constructs an independent pool in
[`doc_validator.cpp`](../../lambda/validator/doc_validator.cpp), and
[`lambda-eval.cpp`](../../lambda/runtime/lambda-eval.cpp) currently parses the
Input before applying its schema. Those are the concrete call sites for the
contract above; compiler/schema AST pools remain id-less and independent.

## 8. Symbols, private names, and reflection

### 8.1 Well-known Symbols

Well-known Symbols keep their generated JS catalog IDs. Hot routing compares
those IDs directly. The existing well-known Symbol-item switch maps its small
semantic Symbol ordinal to the generated NameId; `JsWellKnownRefs` drops the
PropertyKeyRef fields. A realm retains observable Symbol Items, but not a
second name-identity table.

### 8.2 `Symbol()` and `Symbol.for()`

The existing unique NamePool allocation is retained:

- every `Symbol()` call receives a fresh NameId, including equal or absent
  descriptions;
- `Symbol.for(key)` stores one NameId in the existing realm symbol registry;
- the registry maps semantic Symbol identity to NameId and back;
- diagnostic spelling remains metadata only; and
- Symbol IDs are context-local and never serialized into MIR.

The current pointer-valued `property_key` fields in `JsSymbolEntry` and
`JsSymbolDesc` become `NameId name_id`. Existing registry keys remain
unchanged: `Symbol.for` is keyed by registry string and the description table
is keyed by the existing semantic `symbol_id`. Creation is transactional:
allocate the unique NameId first, then publish the registry entry; failure
publishes neither.

The initial reverse operation scans those existing small registries for
`name_id` and returns `js_make_symbol_item(symbol_id)`. This removes pointer
comparison without introducing another registry. If profiling later justifies
an index, it is a secondary index owned by the same symbol registry and does
not become a NamePool identity authority.

The resulting internal API is:

```c
NameId js_symbol_name_id(Item symbol);
bool js_symbol_from_name_id(NameId name_id, Item* out_symbol);
```

### 8.3 Private names

Each class evaluation asks NamePool for a fresh PRIVATE NameId per private
binding. The runtime private environment stores the resulting ID. Repeated
evaluation of the same source receives fresh IDs.

This reuses the existing class-owned private environment and eval bridge:

- its ordinary source-spelling key uses the module-linked ordinary NameId;
- its hidden value is the fresh private NameId stored in an integer lane;
- private property/brand helpers accept `NameId`, not a String Item;
- the class brand is another fresh PRIVATE NameId;
- eval's scoped-private arrays become NameId arrays and no longer require GC
  root ranges for private key Strings; and
- `js_input->name_pool` is no longer a private-key allocator—the owning
  context's dynamic NamePool is the sole allocator.

Every helper validates `name_pool_meta(id)->key_kind == NAME_KEY_PRIVATE`
before use. The hidden integer is never passed through ordinary
`ToPropertyKey`, exposed to Proxy traps, or treated as a JavaScript number.

Private IDs:

- are never derived from spelling or source offsets;
- are never placed in `PropertyKeySpec`;
- are never returned by public property enumeration;
- may use NameRecord spelling only for diagnostics; and
- are compared exactly like other NameIds after lexical resolution.

### 8.4 Reflection and Proxy materialization

NamePool resolves an ordinary NameId to its NameRecord for string
materialization. The Symbol registry resolves a Symbol NameId to the
observable Symbol Item. Private IDs are excluded.

`Reflect.ownKeys` and descriptor enumeration preserve shape/storage order and
materialize each entry by key kind:

1. STRING: `name_pool_ref` supplies bytes for a JS String;
2. SYMBOL: `js_symbol_from_name_id` supplies the exact observable Symbol Item;
3. PRIVATE: skip; and
4. unresolved/invalid ID: fail the internal operation rather than inventing a
   spelling.

Proxy get/set/define/delete helpers retain the original property-key Item
alongside the internal NameId until the trap decision. A static NameId path
materializes a STRING/SYMBOL only when a Proxy or reflection boundary makes it
observable. Private IDs never reach a Proxy boundary.

Property insertion order remains shape/storage order; NameId numeric order
has no semantic meaning and must never affect `Object.keys`,
`Reflect.ownKeys`, serialization, or sorting.

## 9. Ownership, lifetime, and concurrency

### 9.1 Runtime ownership

The sealed static identity-scope root and runtime dynamic NamePool have their
primary references in the long-lived `EvalContext`/`Runtime` and survive all
evaluations that share that runtime. Their dedicated backing is independent of
the GC heap. Module NameId tables, runtime shapes, Symbols, and private
environments never outlive the active scope. Schema-backed Input pools retain
their selected static/dynamic parent through the existing NamePool
retain/release edge, which now also keeps the root-owned name backing alive.

NamePool remains outside GC under D4.1.1. A NameId is an integer and is not a
GC root; NameRecords and NamePool segment tables follow pool lifetime.

Active-runtime teardown order is:

1. stop execution and callbacks;
2. destroy context-owned module/IC state and JS registries;
3. destroy runtime shapes and the GC heap state that may contain NameIds;
4. release the Runtime's dynamic-pool reference; and
5. release the Runtime's static-root reference.

An Input-local NamePool may still retain either parent after step 5, so the
dedicated name backing—not the Runtime or GC heap—remains until that Input is
destroyed. Input teardown must call `name_pool_release(input->name_pool)` before
destroying the Input's own backing Pool/MemContext; this releases its parent
edge. The current process-lifetime `InputManager` already tracks every Input,
so its destructor is the required final release site until RN-D5 defines
shorter or transferable ownership. The last retained parent release destroys
the identity root's backing.

Keeping bytes alive does not make the Input cross-context-valid. After its
creating scope stops, the retained records support safe storage cleanup and
diagnostics only; another JS context must not compare their arbitrary IDs under
the initial contract.

### 9.2 Context isolation

Generated catalog data is immutable and process-shared. Arbitrary static and
dynamic NamePool state is context-owned and single-writer. Two simultaneous
contexts may assign different lower- or upper-half IDs to the same spelling;
their objects never cross the isolate boundary, while shared MIR loads each
context's linked ID.

That isolation rule includes schema-backed Inputs with arbitrary IDs. For the
initial implementation they are supported only in the creating identity
scope. The process-global `InputManager` may track storage lifetime, but no
cross-context parsed-Input cache or transfer contract is provided. Resolving
that ownership model is deferred as RN-D5.

The sealed static root is immutable and may be read concurrently. The dynamic
pool remains single-writer on its context thread. Synchronous parsing may use
the dynamic pool as parent because execution is already serialized on that
thread. A worker's Input child and static-parent retain are created on the owner
before dispatch; worker parsing receives only that prepared child, late names
remain id-less, and no worker mutates the parent refcount or performs a
completion-time rebind.

This preserves D5.4.4: ordinary runtime lookup takes no process-global lock,
atomic RMW, publication check, or cache-coherence operation.

### 9.3 Parent pools

Existing hierarchical lookup supplies the required priority. A child may
reuse an ID-bearing record from a retained parent only when parent and child
belong to the same identity scope. The root allocates lower/static and
upper/dynamic segment numbers for all ID-bearing descendants so two live
children cannot mint the same NameId, and ID resolution routes through that
root.

The hierarchy has no ID-bearing sibling that the runtime dynamic pool cannot
search. All startup script/schema names merge into the one static root before
the dynamic child is created. The root is then sealed permanently; this makes
the existing parent-first create path and current-first lookup path equivalent
with respect to spelling uniqueness.

Constructor enforcement permits exactly one ID-allocating dynamic child for a
root. All other descendants are `IDLESS`; therefore two ordinary spelling
hashmaps that can allocate IDs are always in one searchable root→dynamic chain.
After Runtime teardown, a retained Input may keep the root or dynamic storage
alive, but the scope is closed and cannot acquire a replacement dynamic child.

A schema-backed Input may acquire that static root as parent because its
misses remain id-less. A schemaless Input and a compiler pool must not acquire
the runtime dynamic pool merely to obtain IDs. Generated catalogs remain an
internal immutable fallback rather than a mutable parent edge.

## 10. Cache and ABI contract

### 10.1 Persistent artifacts

“Persistent” here means only a local derived cache under D1.7. Such MIR
artifacts contain:

- generated NameIds plus the generated-catalog fingerprint;
- ordinary static spelling bytes through `PropertyKeySpec` or a future
  SectionNameId name section; and
- dense table indices in MIR.

They never contain dynamic NameIds or NameRecord addresses. Catalog mismatch,
NameMeta/String ABI mismatch, malformed spelling bounds, or module-layout ABI
mismatch invalidates the artifact.

### 10.2 In-memory L1 hits

An L1 hit may reuse sealed code and immutable module metadata. It must allocate
fresh context-owned module/IC state and rebuild every property NameId entry.
Skipping relink because a prior execution populated the table is a lifetime
and cross-context bug under DO16 and D5.4.3.

### 10.3 ABI changes

Expected ABI effects:

- `NameMeta` stays 16 bytes; `predefined_id` is renamed to `name_id`;
- `PropertyKeySpec` stays 16 bytes and retains its current meaning;
- the module table element shrinks from pointer-size to 32-bit NameId;
- `Runtime` gains the sealed `static_name_pool` owner while its existing
  `name_pool` field continues to name the dynamic child;
- `LambdaModuleLayout` gains the module-state ABI and IC count/offset/byte
  fields in §6.2;
- `LambdaModuleState` gains the opaque context-owned IC slab and counts;
- `ShapeEntry` loses `PropertyKeyRef` and uses one NameId field;
- property helper ABIs take NameId in an integer lane;
- IC layout replaces name pointer/length/key-item identity with NameId; and
- MIR/module cache fingerprints must include these layout/helper revisions.

All direct field offsets emitted by MIR and all native readers must change in
the same phase. There is no mixed old/new ShapeEntry or module-table ABI.

`module_state_abi` is a compile-time constant bumped whenever
`LambdaModuleState`, `LambdaModuleLayout`, `JsLoadIC`, or `JsStoreIC` layout
changes. Persistent-cache acceptance compares it together with the generated
catalog fingerprint and existing String/NameMeta ABI fingerprint before any
module state is allocated. An in-memory L1 entry never carries an instantiated
NameId table or IC slab.

## 11. Implementation plan

Each phase must leave one authoritative identity path; compatibility wrappers
may bridge callers, but they must delegate to the same NamePool operation.

### R0 — Formal adoption and refreshed census

1. Accept or amend the proposed formal text in §14.
2. Update `Lambda_Design_Name_Identity.md`, `JS_Tune3_Name.md`, and
   `JS_Runtime_Redesign.md` in the same change as the formal ruling.
3. Freeze the tree anchor and capture current counts for pointer-bearing key
   APIs, emitted `String*` constants, `PropertyKeyRef` comparisons,
   `heap_create_name` property-boundary calls, and compiler-pool IC cells.
4. Record `./utils/count_loc.sh` output; current anchor reports
   `lambda/js = 227,778` lines.
5. Separate runtime counters for interner attempts, existing-name hits, new
   ordinary insertions, unique Symbol/private insertions, and identity-scope
   backings retained only by Input parents after Runtime teardown.

### R1 — Runtime NamePool IDs

1. Add `NamePoolIdMode`, the one static-root allocator/resolver, the dedicated
   root-owned name backing, and the constructor topology checks from §5.5.
2. Rename `NameMeta.predefined_id` to `name_id` without changing its size.
3. Add the two root-owned `ArrayList` segment directories, lower/static and
   upper/dynamic ordinal allocation, no reuse, and explicit exhaustion failure.
4. Reuse parent-first ordinary interning and add irreversible static sealing.
5. Route generated, static, and dynamic resolution through NamePool.
6. Make existing unique Symbol/private allocation assign dynamic IDs in a
   runtime pool.
7. Extend NamePool tests for parent priority, seal enforcement, static/dynamic
   ID uniqueness, rejection of a second dynamic child, round-trip resolution,
   segment rollover, parent lifetime, backing survival after Runtime-reference
   release, last-child backing destruction, generated IDs, id-less Input mode,
   and failure atomicity.

### R2 — Static closure and Input hierarchy

1. Reuse the existing import-cone/module batch and module-state-id order as the
   fresh-runtime transaction in §5.3.
2. Split the JS batch into whole-graph compile-only and serial-execute phases;
   assign module IDs on the owner thread and keep realm/Input/DOM bootstrap
   after static sealing.
3. Scan every initial `PropertyKeySpec` and static type graph into the one
   unpublished static root before module instantiation.
4. Seal the root, create the runtime dynamic child, stage every initial
   module-state allocation against it, and publish pools/states only after the
   batch succeeds.
5. Prove that later runtime executions/modules/eval/schemas allocate only
   through the published dynamic child.
6. Extend Input construction to accept a retained NamePool parent separately
   from its existing `Input* parent` document relation.
7. Make schema-backed Input reuse its selected identity-scope parent and keep
   misses id-less; keep schemaless Input detached from arbitrary runtime IDs.
8. Document ID-bearing Input as same-scope-only; defer cross-context ownership,
   caching, and rebinding to RN-D5.
9. Release each Input NamePool before its own backing during InputManager
   teardown, allowing the last retained identity parent to destroy its backing.
10. Give synchronous owner-thread parsing the selected dynamic parent; before
   worker dispatch, construct the Input child and retain only the sealed static
   root on the owner, with id-less misses and no rebind.
11. Move schema selection/preprocessing before Input parsing where the schema
   is known, and add schema-known, schema-unknown, schemaless, late-schema,
   worker-concurrency, and parent-lifetime fixtures.

### R3 — ShapeEntry unification

1. Replace `predefined_id + key_ref` with `NameId name_id`.
2. Update shape construction, transitions, copying, descriptor cloning, and
   hash-table confirmation together.
3. Add `TypeMapTransition.name_id`; route runtime transitions by the operation
   ID and parser transitions through the same ID/id-less matcher.
4. Preserve arbitrary Input-origin entries with `NAME_ID_NONE` and byte
   comparison through COW/type transitions; do not bulk-promote a copied shape.
5. Assert that every entry newly added by a JS property operation has a
   non-zero NameId.
6. Delete pointer-equality shape/transition helpers once all callers compile.

### R4 — Module NameId table

1. Change the existing module table from `PropertyKeyRef[]` to `NameId[]`.
2. Keep `PropertyKeySpec` sealing and validation unchanged.
3. Link arbitrary spellings through the runtime NamePool.
4. Change Lambda MIR loads/helpers from boxed NameRef to integer NameId.
5. Add/validate the module-state ABI and allocate the NameId table plus opaque
   IC slab transactionally.
6. Prove fresh instantiation and L1 hits rebuild both allocations.

### R5 — JS static lowering and context-owned ICs

1. Promote the existing Lambda property-key registration/finalization into a
   shared transpiler helper.
2. Register every static JS property spelling through it.
3. Emit generated NameId immediates or module-table loads.
4. Convert static property helpers and IC metadata to NameId.
5. Assign dense load/store site indices and address cells through the opaque
   module-state slab required by D5.4.3.
6. Remove pointer-valued key fields from ICs and move profile-label bytes into
   immutable sealed module data.
7. Delete compiler-pool key/IC pointer operands from emitted MIR.

### R6 — Computed keys, Symbols, and private names

1. Centralize Item-to-NameId conversion after `js_to_property_key`.
2. Keep array/TypedArray indexed fast paths and Proxy observable-key behavior.
3. Convert existing Symbol registry fields and reverse scans from
   PropertyKeyRef to NameId; add no new registry.
4. Convert class/eval private environments, brand storage, and private property
   helpers to dynamic PRIVATE NameIds allocated by the runtime pool.
5. Add reflection round-trip and repeated-class-evaluation tests.
6. Delete synthetic spelling and pointer-identity fallbacks.

### R7 — Routing cleanup and final gates

1. Expand the generated JS name catalog through the existing generator for
   hot ordinary names; do not hand-maintain a second table.
2. Convert special-name routing chains to generated NameId comparisons where
   they remain after the broader JS runtime redesign.
3. Delete obsolete PropertyKeyRef comparison APIs and realm pointer tables.
4. Run the full behavior, cache, GC, concurrency, and performance gates.
5. Require `lambda/js` LOC to be strictly below the R0 baseline using
   `./utils/count_loc.sh`.

## 12. Verification gates

### 12.1 Structural gates

- no property-identity comparison of `NameRef` or `PropertyKeyRef` pointers;
- `python3 utils/generate_well_known_names.py --check` passes and no ordinary
  generated spelling has two owning NameIds across catalogs;
- no `ShapeEntry.key_ref` field;
- lower-half arbitrary IDs originate only before static sealing;
- upper-half dynamic allocation starts only after the static root is sealed;
- each identity root creates at most one ID-allocating dynamic child, and every
  other descendant is id-less;
- the fresh JS initial closure completes compile-only layout finalization before
  realm/Input/DOM bootstrap or any module initializer executes;
- compile-only workers use only id-less compiler NamePools and never allocate a
  module-state ID or context state;
- a retained Input parent keeps only the dedicated identity-name backing alive,
  never the GC heap, and the last parent release destroys that backing;
- parent-first interning cannot assign two IDs to one ordinary spelling in an
  identity scope;
- every ShapeEntry newly created by a JS property operation has non-zero
  `name_id`, while copied Input-origin entries preserve `NAME_ID_NONE`;
- every transition created by a JS operation stores its resolved NameId;
  parser-built id-less transitions use hash/length/bytes and never pointer
  identity;
- schema-backed Input reuses IDs only from its retained identity-scope parent,
  and arbitrary misses retain `NAME_ID_NONE`;
- no worker traverses a mutable dynamic NamePool concurrently with its owning
  context;
- no worker mutates an identity-root/dynamic-child refcount; its prepared Input
  owns the parent retain established before dispatch;
- schemaless non-generated Input ShapeEntries retain `NAME_ID_NONE`;
- no compiler/runtime NameRecord pointer emitted as a MIR constant;
- no context-local dynamic NameId emitted as a MIR immediate;
- upper-half fixtures such as `0x80000001` survive module-table loads, helper
  calls, private-environment storage, and returns without sign extension;
- no compiler-pool mutable IC pointer emitted as a MIR constant;
- every IC address derives from the site's sealed owning-module ID, that
  context's module-state slab, and a sealed site index—never from the currently
  active JS module;
- no dynamic NameId serialized in `PropertyKeySpec` or a persistent artifact;
- Symbol/private same-description fixtures always receive distinct IDs where
  ECMAScript requires distinct identity;
- L1 cache re-entry rebuilds the module NameId table and zeroed IC slab; and
- dynamic Symbol/private registry/environment entries round-trip through
  NameId without pointer identity.

### 12.2 Behavior gates

- NamePool unit suite;
- Lambda baseline after any shared runtime/transpiler change;
- JS GTest and baseline suites;
- Test262 baseline with property, Proxy, Symbol, private-field, class-eval,
  descriptor, and reflection deltas reviewed;
- Node baseline for CommonJS/ESM and dynamic compilation;
- DOM suites for host-object and generated-name routing;
- Input/parser baseline for schema-known direct-ID comparison, unknown-field
  byte comparison, schemaless isolation, synchronous late schemas,
  worker-static/id-less behavior, and retained-parent lifetime;
- collision fixtures proving an Input-local id-less name cannot accidentally
  equal a context-local NameId from another scope; and
- forced-GC and teardown tests, although NamePool itself remains outside GC
  under D4.1.1.

### 12.3 Performance and growth gates

Performance measurements use a release build only.

- zero NamePool interner attempts on static named-property steady-state paths;
- zero dynamic NameRecord insertions after module instantiation for names in
  the initial static closure;
- late-module/eval/schema upper-half insertions reported separately;
- computed-key interner attempts and insertions reported separately;
- NamePool count before/after the benchmark corpus reported for DO16;
- identity-scope backing count/bytes retained by Inputs after Runtime teardown
  reported separately;
- named IC hit rate does not regress;
- property lookup CPU share and wall time compared against the frozen R0
  baseline; and
- final `lambda/js` LOC strictly decreases according to
  `./utils/count_loc.sh`.

## 13. Resolution ledger and deferred work

The IDs remain stable review labels. A “resolved” entry means this proposal now
contains an implementable contract; implementation and tests are still tracked
by §11–§12.

### 13.1 Resolved design contracts

| ID | Resolution |
|---|---|
| **RN-O1** | **Resolved by §5.3 and §6.2:** the first fresh-Runtime import cone/module batch compiles the complete initial closure without execution; the owner assigns stable module IDs; name and module-state publication follows complete prelink/sealing/staging; JS realm/bootstrap and serial execution occur afterward. |
| **RN-O2** | **Resolved by §7.7:** one recursive Type walker extracts exact map/element/object names with pointer-cycle detection, and explicit `name_parent` arguments carry the selected pool into `Input::create` before parsing. |
| **RN-O4** | **Resolved by §7.6 and §9.2 (D5.4.4):** owner-thread parsing may use the dynamic parent; a worker receives an owner-prepared Input child retaining only the sealed static root, keeps misses id-less, mutates no parent/refcount, and performs no rebind. |
| **RN-O5** | **Resolved by §5.5–§5.6 and §9.1–§9.3:** one mode-aware NamePool initializer, one static root and one dynamic allocator with dedicated refcount-owned backing, two root-owned `ArrayList` segment directories, dense resolution, no reuse, explicit Input parent release, and `ERR_OUT_OF_MEMORY` exhaustion behavior. |
| **RN-O6** | **Resolved by §6.2, §7.5, and §10.3 (D5.4.3):** `NameId[]` plus one opaque context IC slab, sealed site indices/offsets, transactional publication, reset/destroy behavior, and one module-state ABI fingerprint. |
| **RN-O7** | **Resolved by §7.4 and §7.6 (proposed D3.4.4v2):** one NameId TypeMap probe/matcher owns direct-ID and id-less byte confirmation; JS/parser transitions use that same split, Input-origin entries stay id-less through COW/type changes, and host adapters/Proxy materialization cannot define another identity relation. |
| **RN-O8** | **Resolved by §8:** existing Symbol registries store NameId and provide initial reverse scans; existing class/eval private environments store fresh PRIVATE NameIds from the runtime dynamic pool. |

RN-O3 is closed as a blocking item and reclassified as deferred RN-D5.

### 13.2 Closed adoption gate

**RN-O9 — Formal adoption and dependent docs (closed).**
`doc/Lambda_Formal_Design.md` 1.8.0 adopted D3.4.4v2, D4.6.1v2, and
D4.6.2v2, and this document and the Tune3 execution plan now cite those
normative rulings. No remaining runtime decision depends on adoption.

### 13.3 Post-audit implementation closure

The final audit found four concrete correctness gaps and closed them without
adding a new name mechanism:

- NamePool now rolls its existing upper-half segment directory after ordinal
  `0xffff`; publication occurs only after the spelling hashmap accepts the
  record, and an uncommitted slot cannot become resolvable.
- Generated JS string values, function names, and source text use GC strings
  via `js_make_string_len`; only `ToPropertyKey` enters NamePool identity.
- A `NameId` shape or named-IC probe can byte-confirm only a
  `NAME_ID_NONE` Input entry. It cannot select another generated/static/dynamic
  entry with matching diagnostic bytes; IC hits additionally guard their
  cached NameId.
- Schema name-parent ownership is passed explicitly through Input creation,
  including target file/HTTP/sys/directory/RDB loaders; the former TLS
  handoff was removed. The schema walker unwraps declarations and includes
  `TypeElmt.name` as well as fields.

These preserve the id-less Input seam in D3.4.4v2/D4.6.2v2 while making the
runtime side fully NameId-first.

### 13.4 Explicitly deferred work

1. **RN-D1 — Dynamic NamePool GC/compaction (DO16).** The first
   implementation is append-only for the identity-scope lifetime, and
   upper-half IDs are never reused. Counters must expose growth; reclaimable
   generations, compaction, or quotas are a future design.
2. **RN-D2 — Computed-string caching.** A plain GC String has no NameId field.
   Do not add a side cache without evidence after the static migration.
3. **RN-D3 — Mapped name sections.** Future SectionNameId/mmap support may
   reduce artifact size, but it does not alter runtime NameId identity.
4. **RN-D4 — Cross-release generated IDs.** Generated-catalog fingerprint
   rejection remains sufficient during beta. Stable arbitrary or generated
   IDs across engine releases are a separate compatibility decision.
5. **RN-D5 — Cross-context Input ownership and rebinding.** The initial
   implementation supports arbitrary Input NameIds only in the creating
   identity scope and provides no parsed-Input transfer/cache contract.
   Process-tracked Inputs may retain that scope's dedicated name backing until
   `InputManager` teardown; counters expose this retention, but reclaiming it
   earlier depends on the future Input ownership contract.
   Future work may add context-local cache ownership, explicit scope tagging,
   rebind-on-import, or an always-id-less transferable representation. None may
   make a bare lower/upper-half NameId portable by numeric value alone.

## 14. Proposed formal-spec revisions

Acceptance of this proposal requires a semver bump in
`doc/Lambda_Formal_Design.md` and the following in-place `v2` revisions.

### D3.4.4v2 — Shape name identity

> **D3.4.4v2** `ShapeEntry.name_hash` is lookup metadata, never identity;
> `name_id` is the definitive generated or runtime NameId when non-zero.
> Every entry born from a JS property operation carries a non-zero NameId;
> arbitrary Input-origin entries preserve `NAME_ID_NONE` through
> shape-preserving COW/type transitions, and only newly added JS entries gain an
> ID. A schema-backed Input may reuse a generated/static/dynamic ID from the
> exact retained NamePool ancestry
> shared with its runtime only while the Input is confined to that identity
> scope; its misses remain Input-local with `NAME_ID_NONE`. Transfer of such an
> Input to another identity scope is unsupported pending RN-D5; only an
> already-id-less representation has no arbitrary NameId scope dependency.
> A schemaless Input carries generated IDs only and keeps every other document
> name id-less. The id-less Input seam confirms ordinary names by hash, length,
> and bytes and never byte-matches Symbol/private identities.
> `TypeMapTransition` carries the resolved NameId for JS transitions and uses
> the same ID/id-less confirmation contract for parser-built transitions;
> transition pointer equality is never identity.

### D4.6.1v2 — NameId identity

> **D4.6.1v2** Every semantic runtime property name is identified by a
> 32-bit `NameId = [pool16][ordinal16]`; equality of valid NameIds within their
> catalog/runtime scope is definitive. Generated catalog IDs are immutable
> engine-build identities and may be compared or emitted directly. Pool
> numbers `3..0x7fff` are allocated to context-linked static segments before
> execution; `0x8000..0xffff` are allocated to dynamic context-local segments.
> The halves describe allocation origin, not distinct identity relations.
> Arbitrary IDs are valid only for their owning identity-scope lifetime and are
> linked rather than baked into shared MIR. `NameRef` materializes spelling
> and is never property identity. `SectionNameId = [slot16][offset16]` is
> serialized location only.

### D4.6.2v2 — NamePool and module linking

> **D4.6.2v2** Evolve NamePool rather than replacing it: its existing spelling
> hashmap content-interns ordinary names; its unique allocation creates Symbol
> and private names; identity-scope roots assign and resolve static and dynamic
> NameIds. The complete initial loader closure finalizes sealed module metadata
> without executing code. One static root merges that closure's
> script/schema/type names, allocates lower-half IDs, and is sealed before an
> upper-half dynamic child is created or realm/module initialization runs.
> Ordinary interning searches retained parents first, so an existing spelling
> always reuses one ID; the sealed static root never gains late entries, and a
> root permits only one ID-allocating dynamic child. The root owns a dedicated
> non-GC backing whose lifetime follows existing NamePool parent retain/release
> edges, so an Input parent does not retain or dangle from a runtime heap.
> Sealed modules retain
> generated-ID-or-spelling `PropertyKeySpec` entries and each `EvalContext`
> builds a dense NameId table addressed by MIR-baked indices. Schema-backed
> Input pools may reuse IDs from their retained shared ancestry, but local
> misses and schemaless arbitrary names remain `NAME_ID_NONE`; an ID-bearing
> Input is confined to that identity scope and cannot be process-global cached
> by numeric ID alone; cross-context ownership/rebinding is initially
> unsupported and deferred. Dynamic pools remain single-writer; concurrent
> Input workers use an owner-prepared child retaining the sealed static root and
> keep other names id-less, without parent refcount mutation, a repeated-path
> lock, or rebind. Eval, REPL, dynamic compilation, late imports/schemas, fresh
> modules, and cache hits use the same link path, with post-seal misses allocated
> only in the dynamic pool.

### DO16 — retained and narrowed

DO16 should continue to track dynamic-intern growth, pooled-record auditing,
L1 table rebuild, mapped-backing granularity, and MIR-cache/String/NameMeta ABI
versioning. The NameId-versus-SectionNameId issue narrows to a clear rule:
only generated NameIds persist directly; arbitrary static names persist as
spelling/SectionNameId and relink to context-local NameIds. Dynamic NamePool
GC/compaction remains deferred; the first implementation is append-only and
never reuses an upper-half ID.

## 15. Rejected alternatives

- **New `PropertyKeyId` type:** duplicates NameId and creates conversion seams.
- **Pointer identity with longer-lived pools:** solves lifetime only, not code
  sharing, context ownership, or unified generated/dynamic comparison.
- **Process-global mutable dynamic NamePool:** introduces synchronization and
  cross-context growth into repeated paths, violating D5.4.4.
- **FNV/content hash as NameId:** collisions make it routing metadata, not
  semantic identity.
- **Bake arbitrary dynamic IDs into persistent MIR:** context/process-specific
  values violate D5.4.3 and cannot survive restart.
- **Bake arbitrary lower-half static IDs into persistent MIR:** the static
  number is still identity-scope linked and may differ across processes;
  persistence uses the existing spelling/table relocation.
- **Mutate a static parent after dynamic allocation:** can assign two IDs to
  one spelling because existing child lookup and parent-first creation no
  longer observe a consistent interner state.
- **Assign IDs to every Input key:** allows document cardinality to bloat
  runtime identity state, violating D4.6.2.
- **Assign independent Input-local NameIds:** an unrelated runtime scope may
  reuse the same numeric value for another spelling, making direct equality
  unsound; local misses remain `NAME_ID_NONE`.
- **Share an ID-bearing parsed Input across contexts unchanged:** storage
  lifetime does not establish NameId scope; the initial contract rejects this
  transfer, and any future rebinding/transferable representation belongs to
  RN-D5.
- **Separate JS name tables:** repeat the mechanism already present in
  NamePool, `PropertyKeySpec`, and `LambdaModuleState`.

## 16. Completion criterion

The migration is complete when one static or computed JS property name follows
exactly one path:

```text
source/value
  -> generated ID or sealed spelling
  -> sealed static root first, then dynamic NamePool on a miss
  -> NameId
  -> module table / computed operation
  -> ShapeEntry and IC integer comparison
  -> NamePool/Symbol-registry materialization only when observable
```

At the Input seam, a schema-parent hit joins that path by NameId; an id-less
miss compares ordinary spelling bytes without interning. Static pools never
change after dynamic allocation begins, and dynamic IDs are not reused.

At that point, NamePool is still the name-management design Lambda already
has; NameId is simply carried through to the places that currently recover and
compare `String*` addresses.
