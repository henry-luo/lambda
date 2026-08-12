# JS Tune3 — NameId-First Runtime Name Management Implementation Plan

**Date**: 2026-08-11
**Status**: IMPLEMENTED — NameId-first migration complete; validation evidence recorded below
**Tree anchor**: master `2bfb9bdfa`
**Design authority**: [JS_Runtime_Name.md](JS_Runtime_Name.md), especially
RN1–RN16, R0–R7, and RN-O1–RN-O9. The governing formal rulings are
**D1.4v2**, **D1.5**, **D1.6**, **D1.7**, **D3.4.4**, **D4.2.1v2**,
**D4.6.1**, **D4.6.2**, **D5.4.3**, **D5.4.4**, and **DO16**. The planned
formal revisions are D3.4.4v2, D4.6.1v2, and D4.6.2v2.

This is the execution plan for the NameId design. It replaces the former
pointer-identity Tune3 proposal; do not implement any pointer-based
`PropertyKeyRef` or compiler-pool-retention scheme from a historical copy.

Per **D1.6**, all implementation and validation work is MIR Direct only. The
frozen `--c2mir` path is neither changed nor made compatible by a wrapper.

## 0. Implementation status and evidence

The formal-adoption gate is closed. `doc/Lambda_Formal_Design.md` is normative
version 1.8.0 and contains **D3.4.4v2**, **D4.6.1v2**, and **D4.6.2v2**. The
implementation uses the existing NamePool, PropertyKeySpec, LambdaModuleState,
Input parent, and IC-slab mechanisms; it adds no JS-only interner or pointer
identity compatibility route. **RN-D5** (cross-context Input ownership and
rebinding) remains deliberately deferred as specified.

The final mechanical evidence is:

| Check | Result |
|---|---|
| `./utils/count_loc.sh` | `lambda/js`: **225,711** lines; R0: 227,778; net **-2,067** |
| JS diff accounting | 1,637 insertions / 3,917 deletions in `lambda/js`; no displacement |
| `make build` | Pass; 0 errors |
| `make build-test` | Pass; 0 errors |
| `make test-lambda-baseline` | 3,348/3,349 combined tests pass; the sole failure is `test_js_gtest` idle-timeout after 360 seconds with no output |
| NamePool unit tests | 21/21 pass, including dynamic rollover past 65,535 IDs |
| Item/runtime representation tests | 28/28 pass |
| Input baseline | 2,104/2,104 pass |
| post-audit focused NameId suite | 21 NamePool + 28 Item/runtime + 14 JS/property + 3 MIR checks pass |
| release build | Pass; 0 errors, 929 warnings |
| release NameId smoke suite | 10/10 pass byte-for-byte |
| MIR emission ratchet | 16/16 pass after recording the six intentional NameId transport/IC budget changes |
| generated-name catalog | `python3 utils/generate_well_known_names.py --check` pass |
| structural legacy scan and `git diff --check` | pass; legacy pointer APIs: zero |

The full baseline completed 3,348/3,349 combined tests: input was 2,104/2,104
and the Lambda runtime suites were 1,244/1,245. The only incomplete suite was
`test_js_gtest`, which the runner killed after 360 seconds with no output. An
earlier unrestricted direct corpus run reached 303/327; its 24 failures are
retained as known host/library/BigInt/sparse-array fixture failures (including
DOM bootstrap/library fixtures). The latest narrowed 16-test NameId-focused
set and the release smoke suite passed. The standalone validator-input shared
library executable still cannot load on this macOS build because
`liblambda-runtime-full-cpp.dylib` does not export the pre-existing `_ItemNull`
symbol. Schema behavior itself is covered by the passing
`NegativeScriptTest.InputSchemaUsesTheSharedTypedBoundary` test and the
positive/negative direct fixtures.

### N8 deletion ledger closure

The R0-to-final `lambda/js` diff closes the legacy census without moving an
equivalent implementation elsewhere:

| Deleted surface | Sole replacement | Evidence |
|---|---|---|
| pointer-key `ShapeEntry`/transition and duplicate property-key matchers | `NameId` plus the explicit id-less Input byte matcher | N3 shape/property tests; structural scan is zero |
| shaped constructor object/slot optimizer and its inference/composition phases | ordinary object construction and existing typemap allocation where still needed | class/constructor/slot regressions; 323 + 744 + 781-line compiler deletions |
| compiler NamePool retention and pointer-valued JS MIR name operands | sealed `PropertyKeySpec` image + per-context `NameId[]` table | MIR emission corpus and cache/link focused tests |
| duplicate module-name linker and IC state ownership paths | generic module linker + owning module IC slab | module/eval/parallel focused tests |
| pointer Symbol/private/reflection fallbacks and raw string helper variants | NamePool NameId conversion with observable-key materialization at the boundary | Symbol/private/descriptor/Proxy focused tests |

The aggregate diff is 1,637 added versus 3,917 deleted JS lines, matching the
2,280-line `count_loc.sh` reduction. These deletions remove obsolete owners;
the replacements remain in their original runtime ownership modules.

## 1. Outcome and non-negotiable exits

The migration is complete only when every JavaScript runtime property identity
is a `NameId`, allocated and resolved by the existing `NamePool` mechanism.
`String*` remains spelling/diagnostic/observable-key material, never property
identity. Static names are linked from a sealed module table; dynamic names
come from the one context-local dynamic child; id-less Input names compare
ordinary bytes only at the Input seam.

The deliverable must meet all of these exit gates.

| Gate | Required result | Evidence |
|---|---|---|
| Identity | No JS shape, property helper, transition, IC, Symbol registry, or private environment uses `String*`/`PropertyKeyRef` pointer equality as name identity. | Structural scan plus focused unit tests. |
| Sharing | Shared MIR contains only generated NameId immediates or a sealed module-table index; it contains no compiler-pool pointer, dynamic NameId immediate, or instantiated IC address. | MIR emission checks and cache tests. |
| Ownership | The static root is sealed before runtime dynamic allocation; exactly one ID-allocating dynamic child exists per identity scope; schema Input follows the stated parent rules. | NamePool topology/lifetime tests. |
| Semantics | Test262, Node/module, DOM, property/Proxy, Symbol, private-field, Input, and GC/teardown regressions pass. | Commands in §10. |
| LOC | `lambda/js` has at least **1,000 fewer C/C++ LOC** than the R0 baseline, measured only by `./utils/count_loc.sh`. | Baseline 227,778; final result must be **<= 226,778**. |
| No displacement | The LOC gate is earned by deleting obsolete JS runtime paths and consolidating equivalent behavior, not by moving existing JS runtime code to another production directory or generated source. | R0/R8 deletion ledger and diff review. |

The LOC gate is intentionally hard. A smaller runtime surface is a design
result: NameId must replace the duplicate pointer-key route, not coexist with
it behind compatibility shims. Tests, documentation, build files, generated
catalog input, and code outside `lambda/js` do not count toward this gate.

## 2. Scope, fixed contracts, and deferred work

### 2.1 In scope

- Extend the existing `NamePool` and generated-name catalogs with NameId
  assignment/resolution, static sealing, and static/dynamic pool-number halves.
- Replace JS property-name identity in shapes, type-map transitions, property
  helpers, named inline caches, Symbols, private names, and reflection routes.
- Reuse `PropertyKeySpec`, `LambdaModuleState`, module linking, and the
  compile-only module batch to carry static JS names into per-context state.
- Make schema-aware Input share an allowed static/dynamic parent while keeping
  unknown fields id-less; preserve fully schemaless Input isolation.
- Remove the legacy pointer-key APIs, compiler pool lifetimes, and duplicate
  lookup/IC logic once there are no callers.

### 2.2 Out of scope

- A new `PropertyKeyId`, JS-only name interner, global dynamic-name registry,
  or a second module relocation representation. **RN1**, **RN2**, and **RN9**
  expressly reject these mechanisms.
- NamePool GC, compaction, reuse, or quotas. Dynamic IDs are append-only in
  this migration under **DO16** / RN-D1.
- A computed-string NameId side cache (RN-D2), mapped name sections (RN-D3),
  or a cross-release NameId ABI (RN-D4).
- Cross-context Input transfer, ownership rebinding, or parsed-Input caching.
  This is explicitly deferred as **RN-D5**. An ID-bearing Input is valid only
  in the identity scope that created it.
- Changes to C2MIR, vendor code, parser-generated files, or a rewrite of the
  `PropertyKeySpec` serialized spelling format.

### 2.3 Invariants carried through every phase

1. `NAME_ID_NONE` is not an ordinary runtime identity. It represents the
   id-less Input/raw seam only.
2. Generated IDs are immutable catalog identities. Arbitrary static and
   dynamic IDs are context-linked and must not be persisted as raw values.
3. Parent-first ordinary interning establishes static-before-dynamic priority.
   The static root becomes immutable before the dynamic child exists.
4. `name_hash` is routing metadata. It never replaces `NameId` equality and
   must be byte-confirmed at the id-less boundary.
5. Symbols and private names get fresh IDs through existing unique NamePool
   allocation. Equal descriptions do not make them equal names.
6. A helper that observes a JavaScript property key (Proxy/reflection) keeps
   or materializes the observable Item at that boundary. Internal identity is
   still `NameId`.
7. A NameId is an integer, not a GC root. Pool backing/lifetime follows the
   existing NamePool parent retain/release chain, satisfying **D1.5** and
   **D4.2.1v2**.
8. Repeated execution contains no global synchronization. Dynamic pool
   mutation remains owner-thread-only as required by **D5.4.4**.

## 3. Baseline, accounting, and branch discipline

### N0 — freeze measurable baseline

Before code changes, record these values in the implementation PR/commit
series and retain the output with the Tune3 validation record:

```bash
git rev-parse --short HEAD
./utils/count_loc.sh
python3 utils/generate_well_known_names.py --check
```

At the stated tree anchor the canonical counter reports:

```text
./lambda/js
  Lines: 227778
```

The final line is therefore mathematically constrained to:

```text
final_lambda_js_loc <= 227778 - 1000 = 226778
```

Use `./utils/count_loc.sh` unchanged at both endpoints. Do not substitute a
hand-filtered `wc`, a git diff statistic, or a source-only counter. The script
is the requested common accounting mechanism.

### N0.1 — establish deletion ledger

Before changing ABI fields, create a checked-in or review-attached census of
the legacy surface. It need not prescribe implementation, but it must list
each owning file, symbol family, caller count, and planned removal phase for:

- `PropertyKeyRef`/`NameRef` property-identity comparisons;
- `ShapeEntry.key_ref`, pointer-key transition fields, and pointer-key
  comparison helpers;
- string-based static property operands emitted by JS MIR lowering;
- compiler NamePool ownership retained solely for emitted name pointers;
- pointer-valued named IC key fields and compiler-pool IC cell addresses;
- duplicate raw-byte versus canonical-key property lookup routes;
- JS Symbol/private registries or environments storing key pointers; and
- special-name pointer-routing chains that a generated NameId can replace.

The ledger is a deletion plan, not a permission to remove code blindly. Each
row must name the new sole owner before its old helper is removed. At the end,
the ledger records deleted LOC by phase and demonstrates the 1,000-line
reduction was not obtained by relocating the same behavior elsewhere.

### N0.2 — sequence rule

No mixed identity representation is allowed at a persistent boundary. A short
adapter may exist within one phase only when it delegates to the new `NameId`
path and has an explicit caller-removal condition. It may not restore pointer
equality as a fallback. A phase is incomplete if its adapter becomes a second
authoritative property identity route.

## 4. Work breakdown and dependency order

```text
N0 formal adoption + baseline
 │
 ├── N1 NamePool ID substrate
 │    └── N2 initial closure / static seal / Input parent wiring
 │          ├── N3 ShapeEntry + transitions
 │          └── N4 module NameId table + ABI
 │                └── N5 JS static lowering + context IC slab
 │                      └── N6 computed keys / Symbols / private names
 │                            └── N7 Input, reflection, cache and teardown hardening
 │                                  └── N8 legacy deletion + release validation + LOC exit
```

N3 and N4 may overlap only after N1/N2 establish the common NameId ABI. N5
does not begin until the module-state ABI and `ShapeEntry` representation are
settled. N8 is not a cosmetic cleanup phase: it owns removal of compatibility
code and the LOC exit.

## 5. Detailed implementation phases

### N1 — Extend NamePool; do not replace it

**Purpose.** Give the existing NamePool a single, scope-owned allocator and
resolver for generated, static, and dynamic `NameId`s.

**Primary code areas.** `lambda/core/name_pool.*`,
`lambda/core/name_identity.h`, generated-name support, and the MemContext
factory path. The exact declarations should be located with `rg` before
editing; this phase must promote reusable helpers into the appropriate module
header rather than copy a file-local implementation.

**Changes.**

1. Introduce the fixed `NamePoolIdMode`: `IDLESS`, `STATIC`, or `DYNAMIC`.
   It is immutable after construction.
2. Rename `NameMeta.predefined_id` to `name_id` without changing the 16-byte
   layout. Generated records retain their generated IDs; every allocated
   runtime NameRecord receives a non-zero ID only when its pool mode permits
   it.
3. Make one static NamePool the identity-scope root. Give it root-owned segment
   directories for `[pool16][ordinal16]` allocation and resolution, with a
   lower-half range for static segments and an upper-half range for dynamic
   segments. Reserve zero and fail before partial insertion on ordinal or
   segment exhaustion.
4. Give the root a dedicated non-GC backing `Pool`, owned by existing
   retain/release semantics. An Input retaining a parent must keep this backing
   valid without retaining a dead Runtime/GC heap.
5. Reuse parent-first ordinary interning. Add irreversible static sealing and
   enforce a single canonical ID-allocating dynamic child; all other runtime
   descendants are `IDLESS`.
6. Route normal spelling lookup, generated resolution, ID resolution, and
   unique Symbol/private allocation through NamePool APIs. Do not add a JS
   side table to recover `NameId` from spelling.
7. Add debug assertions at allocation and resolution boundaries: non-zero
   IDs resolve in their identity root, a static root cannot allocate after
   sealing, and a dynamic child cannot be recreated after scope closure.

**Required tests.**

- generated ID resolution and catalog alias uniqueness;
- static allocation, upper/lower-half boundaries, segment rollover, and
  explicit exhaustion failure with no partially published record;
- static-parent hit before dynamic allocation, including a spelling first
  seen in a dynamic caller but present in the sealed static root;
- rejection of a second ID-allocating dynamic child and of a static post-seal
  insertion;
- unique Symbols/private names with the same diagnostic spelling receiving
  different IDs;
- `IDLESS` child misses remaining `NAME_ID_NONE`;
- root and parent lifetime: Runtime releases its roots, an Input parent
  remains safe, and the last parent release destroys the dedicated backing.

**Exit condition.** A single NamePool root can assign/resolve names in both
halves, preserve parent-first semantics, and report counters for ordinary
attempts/hits/inserts and unique insertions. No JS property code changes in
this phase are permitted to manufacture a separate allocation rule.

### N2 — Construct and seal the fresh-runtime static closure

**Purpose.** Establish the one point at which initial scripts, modules,
schemas, and static type names acquire lower-half IDs.

**Primary code areas.** Runtime/module batch and loader ownership in
`lambda/runtime/`, JS batch/lowering entry points such as
`lambda/js/js_mir_module_batch_lowering.cpp` and
`lambda/js/js_mir_entrypoints_require.cpp`, plus `Input` creation and schema
selection in `lambda/input/` and the validator path.

**Changes.**

1. Reuse the existing import-cone/module batch. Split the first fresh-runtime
   transaction into whole-graph compile-only/finalize and serial execution.
   The owner assigns stable module-state IDs before any initializer runs.
2. Build one unpublished static root. Scan each initial module's sealed
   `PropertyKeySpec` entries and the exact static schema/type graph into that
   root. Do not create per-module or per-schema static siblings.
3. Seal the root; create the canonical dynamic child; allocate/stage all
   module state against that child; then publish root, dynamic child, and
   module state atomically after complete success.
4. Keep realm, Input, DOM bootstrap, callbacks, and module initialization
   after sealing/publication. Cycles and top-level-await retain the existing
   evaluation ordering; this phase changes name preparation, not module
   semantics.
5. Extend Input construction with an explicit retained name-parent argument,
   separate from a document `Input*` parent. Do not overload document
   ownership to mean NamePool ancestry.
6. For a known schema, preprocess exact field names before parsing and create
   an Input-local id-less child with the selected runtime parent. A schema
   name resolves to the parent ID; an unknown field stays `NAME_ID_NONE`.
7. A late schema on the owner thread may inherit the dynamic child. Worker
   parsing receives an owner-prepared child retaining only the sealed static
   root; worker misses are id-less and it neither mutates a parent reference
   nor performs a completion-time rebind.
8. Schemaless Input remains detached from arbitrary runtime IDs; only a
   generated catalog name can carry a generated ID.
9. Make Input destruction release its NamePool parent before destroying its
   own backing. Confirm all tracked Inputs do this on `InputManager` teardown.

**Required tests.**

- importing initial modules with cycles and with top-level-await proves that
  no initializer executes before static sealing;
- a static source name used in several modules maps to one NameId in the
  identity scope, while the same source links afresh in another context;
- eval, `new Function`, REPL, late import, and late schema reuse static hits
  and put first misses in the dynamic half;
- known-schema hit, unknown-schema field, late owner-thread schema,
  schemaless Input, worker Input, and retained-parent teardown fixtures;
- a worker cannot observe/mutate the dynamic parent and its id-less result is
  not rebound after it returns;
- failure while staging the first closure leaves no published root, dynamic
  child, module state, or dangling Input parent.

**Exit condition.** The runtime owns exactly one sealed static root and one
dynamic child. All first-closure names have a defined pre-execution link
point, and Input name ancestry is explicit and testable.

### N3 — Convert shapes and transitions to one NameId field

**Purpose.** Make shape identity match the design before named JS ICs begin
using it.

**Primary code areas.** Shape/TypeMap declarations and creation in `lambda/`,
the property helper surface in `lambda/js/js_props.cpp` and
`lambda/js/js_property_attrs.cpp`, and all clone/descriptor/COW paths found by
the N0 census.

**Changes.**

1. Replace `ShapeEntry.predefined_id + key_ref` with one `NameId name_id`.
   Update layout assertions, allocation, copies, descriptor clones, map
   rebuilds, and all direct field readers in one ABI change.
2. Retain `name_hash` only for routing. A non-zero `name_id` confirms a
   runtime JS name by exact integer equality. `NAME_ID_NONE` confirms only an
   ordinary Input/raw spelling by hash, length, and bytes.
3. Add the same `name_id` seam to `TypeMapTransition`. JS-created transitions
   record a non-zero operation NameId; parser/Input transitions retain the
   id-less byte-confirmation path.
4. Preserve `NAME_ID_NONE` when an Input shape is copied or changed through
   COW/type transitions. Only a newly introduced JS property gets a resolved
   NameId. Never bulk-promote a document shape just because it is later
   inspected by JS.
5. Assert at every JS property insertion that `name_id != NAME_ID_NONE`; put a
   root-cause comment at the invariant boundary when needed, per repository
   policy.
6. Remove `key_ref` equality helpers only after all callers have moved to the
   common ID/id-less matcher. Do not retain two lookup implementations with
   subtly different collision rules.

**Required tests.**

- same hash / different ordinary bytes at the id-less seam;
- a generated/static/dynamic JS property hits by NameId without byte recovery;
- Input COW and descriptor/type-map transitions preserve `NAME_ID_NONE`;
- adding a JS property to an Input-derived object gives only that entry an ID;
- Symbols/private names never match an id-less byte record;
- shape clone/rebuild/delete/order operations preserve property ordering;
- debug build rejects a JS-created zero-ID shape entry.

**Exit condition.** `ShapeEntry.key_ref` and equivalent pointer-identity
transition fields are gone. Every property lookup has one explicit NameId or
id-less confirmation rule, satisfying proposed **D3.4.4v2**.

### N4 — Link static names into module state and establish the ABI

**Purpose.** Reuse the existing sealed `PropertyKeySpec` path so shared MIR
loads context-linked `NameId`s rather than a compiler-pool pointer.

**Primary code areas.** `lambda/lambda.h`, runtime state/layout declarations,
`lambda/runtime/runtime-state.cpp`, `lambda/runtime/transpile-mir.cpp`,
`lambda/mir.c`, and shared transpiler declarations.

**Changes.**

1. Leave `PropertyKeySpec` as generated-ID-or-spelling sealed metadata. It is
   already the appropriate persistent/portable representation.
2. Change the per-context module property-key table from `PropertyKeyRef[]` to
   dense `NameId[]`. Each module instantiation resolves each sealed spec via
   the relevant runtime NamePool.
3. Introduce one shared property-key registration/finalization helper for
   Lambda and JS lowering. It returns a generated immediate or a stable dense
   module-table index; it must not expose a name pointer to emitters.
4. Extend `LambdaModuleLayout` with module-state ABI version, name-table
   count/offset/bytes, and opaque IC-slab count/offset/bytes. `LambdaModuleState`
   owns the context-local `NameId[]` and slab.
5. Allocate module table and IC slab transactionally. Cache hits rebuild both
   per context; neither allocation is retained in an L1 code artifact.
6. Change relevant Lambda helper and MIR boundaries to take `NameId` in an
   integer lane. Audit signed/unsigned conversions: an upper-half ID such as
   `0x80000001` must arrive zero-extended and unchanged.
7. Bump/check the module-state ABI together with generated-catalog and
   String/NameMeta fingerprints before cache use. Reject stale artifacts
   rather than interpreting an old layout.

**Required tests.**

- `PropertyKeySpec` malformed bounds and catalog mismatch reject cleanly;
- static arbitrary spellings link to different numeric IDs across fresh
  contexts while generated IDs remain catalog-stable;
- dynamic values are absent from persisted artifacts and MIR constants;
- a lower/upper-half NameId survives table load, helper call, return, and IC
  setup without sign extension;
- fresh instantiate and L1 re-entry rebuild a non-aliased table and zeroed IC
  slab;
- cache rejection covers module-state ABI, catalog fingerprint, and NameMeta
  ABI mismatch;
- a staged allocation failure tears down all partial table/slab state.

**Exit condition.** A module can execute a static named operation without any
compiler NameRecord pointer in generated MIR. The ABI has one declared owner
and one invalidation rule, satisfying **D5.4.3**.

### N5 — Convert JS static lowering and named inline caches

**Purpose.** Remove compiler-pool pointers and context-specific addresses from
JS MIR while preserving named-property performance.

**Primary code areas.** `lambda/js/js_mir_expression_lowering.cpp`,
`lambda/js/js_mir_internal.hpp`, `lambda/js/js_runtime.h`,
`lambda/js/js_runtime_state.cpp`, and any emitted-ABI declarations identified
by the N0 census.

**Changes.**

1. Route each static JS property spelling through N4's shared registration
   helper. A generated catalog name emits a NameId immediate; any other static
   spelling emits a load from the owning module's `NameId[]` table.
2. Change every static get/set/define/delete/has helper signature and any
   named fast path to accept `NameId`, not chars/length, `String*`, or
   `PropertyKeyRef`.
3. Replace pointer-valued key fields in `JsLoadIC`/`JsStoreIC` with `NameId`.
   Store label/source spelling only in immutable sealed module metadata used
   for diagnostics/profiling.
4. Assign dense load/store site indices during sealed module layout. MIR
   derives an IC address from the owning module-state ID, that context's opaque
   slab, and sealed offset/site index. It must never use the currently active
   JS state as an address source.
5. Maintain miss/hit guards using shape/slot/prototype policy plus NameId. Do
   not weaken Proxy, accessor, prototype, dictionary-mode, or invalidation
   checks merely because the key transport got cheaper.
6. Delete compiler NamePool/IC address constants and compiler-pool lifetime
   retention after emission no longer reads them.

**Required tests.**

- MIR fixtures for generated immediate and arbitrary table-load named access;
- structural scan rejecting emitted NameRecord/String/IC pointer constants;
- nested/module-interleaved calls show each IC uses its sealed owner module,
  not the active caller module;
- upper-half dynamically linked name is exercised through named IC load and
  store;
- cache miss, prototype mutation, descriptor change, Proxy, accessor, and
  dictionary-mode transitions preserve existing behavior;
- L1 cache re-entry rebuilds IC slabs and cannot hit a prior context's state.

**Exit condition.** JS static MIR contains no compiler-pool name pointers or
mutable IC addresses. Named IC identity is `NameId`, with context ownership
coming solely from the module-state slab.

### N6 — Centralize computed keys, Symbols, private names, and observability

**Purpose.** Ensure non-static property names use the same identity without
breaking ECMAScript observability.

**Primary code areas.** `lambda/js/js_runtime.cpp`, `lambda/js/js_globals.cpp`,
`lambda/js/js_props.cpp`, private/class/eval lowering, and property helper
declarations.

**Changes.**

1. Centralize conversion immediately after `js_to_property_key`: ordinary
   string names resolve/intern through the runtime NamePool; a Symbol maps to
   its registry NameId; private lexical resolution supplies its PRIVATE ID.
   The result is one `NameId` plus, only when needed, the original observable
   key Item.
2. Keep array-index/typed-array fast paths ahead of ordinary-name interning.
   Index behavior is not a reason to fork ordinary NameId lookup.
3. Convert the existing Symbol registry and its reverse resolution to store
   NameId. `Symbol()`/private allocations use existing unique NamePool
   allocation; `Symbol.for()` remains a content-keyed registry operation but
   stores the allocated Symbol NameId as its identity.
4. Change class private environments, brands, and eval private bridges to
   arrays/fields of PRIVATE NameIds. Each class evaluation allocates fresh
   IDs; IDs never derive from spelling/source offsets and never enter
   `PropertyKeySpec`.
5. At Proxy/reflection boundaries, materialize a STRING NameId through
   NamePool or a SYMBOL NameId through the existing registry. Private IDs are
   not materialized or exposed. Keep property enumeration order from shape
   storage; numeric NameId ordering has no semantic meaning.
6. Delete synthetic diagnostic-spelling and pointer-equality fallbacks after
   reflection tests prove the materialization route is complete.

**Required tests.**

- computed ordinary string hit/miss, repeated dynamic string, and no
  interning on array-index fast paths;
- `Symbol()`, same-description Symbols, `Symbol.for`, well-known Symbols, and
  reverse materialization;
- repeated class evaluation and eval closures prove fresh private IDs/brands;
- private fields remain absent from `Reflect.ownKeys`, Proxy traps, and public
  descriptor paths;
- Proxy trap receives the original observable key, while a non-Proxy internal
  operation remains NameId-only;
- `Object.keys`, `Reflect.ownKeys`, descriptors, serialization, and ordering
  stay independent of numeric NameId values.

**Exit condition.** Computed names, Symbols, and private names no longer
reintroduce pointer identity. There is one conversion gate and no parallel JS
registry/interner.

### N7 — Harden Input seam, cache behavior, ownership, and teardown

**Purpose.** Complete the boundary rules that make NameId identity safe in
long-lived document and runtime scenarios.

**Primary code areas.** Input parsers/builders, schema validator/type walker,
runtime teardown, InputManager destruction, module-cache entry code, and the
host/DOM adapters that route property names.

**Changes.**

1. Implement one recursive static-type/schema name collector, with pointer
   cycle detection, that feeds the N2 static root. Reuse the same walker for
   every schema-bearing input route instead of creating format-specific name
   extraction.
2. Audit all host adapters: they must pass a resolved NameId into property
   operations or use the explicit id-less byte matcher. An adapter must not
   compare a materialized `String*` to establish identity.
3. Verify all Input copy/COW and parser transition paths retain the N3
   `NAME_ID_NONE` contract. Explicitly cover JSON/HTML field names and parser
   construction paths.
4. Ensure runtime teardown destroys module/IC/JS registry state before
   releasing dynamic/static pool roots. Input teardown releases its retained
   pool before its own allocator; record retained backing bytes/count under
   **DO16**.
5. Enforce cache policy: persisted code has only spells/specs/generated IDs;
   L1 cache code has no instantiated NameId table/IC state; every hit relinks
   into the target context.
6. Codify the RN-D5 boundary in runtime assertions/tests: no ID-bearing Input
   is accepted by another identity scope. Do not "solve" this with numeric ID
   comparison, ownership guessing, or an implicit rebind.

**Required tests.**

- schema field extraction across nested map/element/type structures, including
  pointer cycles;
- JSON and HTML known-field direct-ID comparison; unknown field and
  schemaless byte comparison; no accidental equality across contexts;
- owner-thread late schema and worker static-parent/id-less-miss behavior;
- runtime-first teardown with a retained Input, then last Input teardown;
- forced GC/poison runs while names remain integer IDs and NamePool stays
  outside the GC root protocol;
- cache file/L1 inspection plus two-context reuse proves fresh name table and
  IC state; and
- an explicit cross-context Input rejection fixture for RN-D5.

**Exit condition.** Input isolation has one documented supported contract and
all pool state reaches a safe terminal lifetime. RN-D5 remains visibly
deferred, rather than accidentally becoming an unsupported half-feature.

### N8 — Remove the legacy path and meet the LOC exit

**Purpose.** Make deletion an implementation result, not optional cleanup.

**Removal order.**

1. Delete pointer-key shape fields, comparison helpers, and pointer transition
   APIs after N3 has no callers.
2. Delete pointer module-key table types, load/link code, and pointer helper
   overloads after N4/N5 emission and runtime helpers use integer IDs.
3. Delete compiler-pool key ownership and per-emitted-key pointer retention
   after no MIR constant references compiler NameRecords.
4. Delete pointer-valued IC key fields/cells, active-module routing, and
   duplicate cache reset paths after N5's module slab owns all state.
5. Delete Symbol/private pointer fields, reverse pointer searches, diagnostic
   spelling fallbacks, and duplicate key conversion helpers after N6.
6. Consolidate byte/name lookup variants through the N3 matcher. Keep a raw
   byte path only where the id-less Input contract requires it; remove any
   second JS-only canonicalization wrapper that does the same work.
7. Expand the generated catalog using the existing generator only where a
   measured special-name pointer routing chain remains. Then delete that
   chain; never maintain a hand-written duplicate catalog.

**LOC ledger rule.** For every deletion group, record:

| Field | Required record |
|---|---|
| Removed owner/API | File and symbols deleted. |
| Replacement | The one NameId owner/call path that now performs the work. |
| Behavioral proof | Focused test or emission assertion. |
| LOC contribution | `lambda/js` insertions, deletions, and net delta for the group. |
| No-displacement proof | Confirmation that equivalent runtime logic was not copied to another production directory. |

The final cumulative ledger must show a net `lambda/js` reduction of at least
1,000 lines. The plan expects that result from eliminating the legacy pointer
identity machinery, compiler-pool management, duplicated raw/canonical lookup
routes, and duplicated IC routing/reset fields—not from deleting safety checks
or moving code out of the directory.

**Exit condition.** All N0 legacy census rows are closed or explicitly proven
unrelated to name identity. `./utils/count_loc.sh` reports `lambda/js` at or
below 226,778 lines.

## 6. API and ABI change checklist

This checklist prevents a partial representation migration. It is reviewed at
the start and end of each ABI-owning phase.

| Surface | Required final state | Owner phase |
|---|---|---|
| `NameMeta` | 16-byte layout preserved; `name_id` replaces `predefined_id`. | N1 |
| NamePool | Mode, root allocator/resolver, sealing, canonical dynamic child, explicit id-less children. | N1–N2 |
| `ShapeEntry` | One `NameId name_id`; `name_hash` routing only; no `key_ref`. | N3 |
| `TypeMapTransition` | NameId or explicit id-less byte confirmation; no pointer identity. | N3 |
| `PropertyKeySpec` | Unchanged generated-ID-or-spelling sealed image. | N4 |
| `LambdaModuleState` | Context-owned `NameId[]` and opaque IC slab. | N4 |
| `LambdaModuleLayout` | Versioned module-state ABI and table/slab offsets/counts. | N4 |
| MIR | Generated immediate or module-table index; no arbitrary ID/pointer immediate. | N4–N5 |
| `JsLoadIC` / `JsStoreIC` | NameId key and owning-module slab address derivation. | N5 |
| JS property helpers | NameId integer ABI, with Item only at observable boundary. | N5–N6 |
| Symbol registry | NameId identity plus existing observable Symbol mapping. | N6 |
| Private environment/brand | Fresh PRIVATE NameId storage. | N6 |
| Input | Explicit retained name parent, IDLESS misses, defined teardown. | N2, N7 |
| Cache fingerprint | Module-state ABI + generated catalog + String/NameMeta ABI revisions. | N4, N7 |

Any direct field-offset emission in MIR and all native readers of that field
must change in the same commit series. There is no supported old/new mixed
`ShapeEntry`, module-state, or IC layout.

## 7. Structural scans and standing regressions

Add focused scans/ratchets where current test coverage cannot prove an absence
property. A scan must be narrow enough to permit valid spelling materialization
at diagnostics/Proxy/reflection boundaries, while rejecting identity uses.

Required standing checks:

- generated catalog check rejects a duplicate ordinary spelling with different
  generated NameIds;
- scan for `ShapeEntry.key_ref` and legacy pointer-identity APIs reports zero
  after N8;
- scan emitted MIR for compiler NameRecord/String pointer constants,
  compiler-pool IC pointers, and arbitrary dynamic NameId immediates;
- scan module/cache serialization for dynamic NameId values;
- scan named IC code for address derivation from active JS module state rather
  than the sealed owner module state;
- assertion/fixture that an upper-half ID is zero-extended across all MIR/C
  boundaries;
- NamePool topology test proving one dynamic child and parent-first priority;
- cache test proving an L1 hit receives fresh `NameId[]` and IC slab; and
- Input test proving worker parsing cannot mutate or retain-count the dynamic
  parent and ID-bearing Input cannot cross contexts under RN-D5.

Structural scans inform cleanup but cannot replace behavior tests. In
particular, a `String*` is still permitted for formatting and observable
property-key materialization; what is forbidden is using its address as a
shape/IC/registry/routing decision.

## 8. Performance and growth measurement

All performance comparisons use a release build under repository rule 10.
Measure the same workload before and after the N5/N8 path, keeping input and
cache state explicit.

Record at minimum:

| Metric | Expected direction / constraint |
|---|---|
| Static named-property interner attempts | Zero on warmed steady-state static access. |
| Initial-closure dynamic inserts | Zero for names present in the sealed initial closure. |
| Late module/eval/schema inserts | Reported separately; upper-half only for misses. |
| Computed-key attempts/inserts | Reported separately from static linking. |
| NamePool record/segment count | Before/after corpus values reported for DO16. |
| Input-retained backing count/bytes | Reported after runtime teardown. |
| Named IC hit rate | No regression against R0 workload. |
| Property lookup CPU/wall time | Compared against R0 release baseline; investigate any material regression. |
| `lambda/js` LOC | <= 226,778 on the canonical script. |

No performance claim may use a debug build, skip slow semantic cases, or
interpret a lower LOC count as a performance result. If static property
performance regresses, first inspect emitted table loads, IC slab addressing,
and whether a legacy pointer lookup is still present; do not introduce a second
identity representation to mask the regression.

## 9. Risks, ordering constraints, and stop conditions

| Risk | Prevention / response |
|---|---|
| Static/dynamic duplicate ID for one spelling | Enforce parent-first interning, one root-to-dynamic chain, and irreversible static seal before dynamic allocation. Stop if an alternate allocating child is needed; that indicates a topology bug. |
| NameId leak into persistent cache | Keep arbitrary names as `PropertyKeySpec` spelling and scan artifacts. Reject cache on fingerprint mismatch rather than accepting stale data. |
| Context state baked into MIR | Route all arbitrary names through per-context table loads and all IC addresses through owning module state. Stop if an emitter asks for a compiler/runtime pointer constant. |
| Input field explosion | Keep Input misses id-less. Do not assign dynamic IDs to every JSON/HTML field to avoid a comparison branch. |
| Worker race | Prepare the Input child/parent retain on owner; worker sees only sealed static parent and cannot rebind. Do not add a hot lock. |
| Symbol/private semantic drift | Preserve unique allocator and existing registry/environment ownership; test same-description distinctness and reflection exclusion. |
| ABI mismatch / stale cache | Bump and validate one module-state ABI fingerprint in the same phase as layout changes. |
| LOC target missed | Begin N8 deletion only after each replacement has tests; track net `lambda/js` delta per deletion group. If the ledger cannot reach 1,000 lines by deleting obsolete work, stop and reassess duplicate mechanisms rather than padding or relocating code. |
| RN-D5 scope creep | Reject cross-context ID-bearing Input explicitly. Any rebinding/transfer mechanism requires a separate approved design. |

## 10. Validation sequence

Run focused checks at each phase; run the full sequence for N8 completion.
Commands below are existing project gates or direct mechanical checks. Add
new focused NameId targets to the build only when their test files land.

```bash
# Mechanical / design invariants
python3 utils/generate_well_known_names.py --check
./utils/count_loc.sh
git diff --check

# Build and shared-runtime regression
make build
make test-lambda-baseline

# JS runtime and error/MIR regressions
make test-jube-node-error-lane
make test262-baseline

# Release-only performance / growth capture
make release
```

The final validation report additionally records:

1. focused NamePool topology, exhaustion, and lifetime test results;
2. focused module-table/IC-slab/MIR-emission checks;
3. property/Proxy/descriptor/Symbol/private/reflection test results;
4. JSON/HTML schema, schemaless, worker, and cross-context-rejection results;
5. forced-GC/teardown results using the repository's existing stress setup;
6. cache-hit/relink results in at least two separate contexts;
7. release benchmark command, corpus, environment, and the §8 counters; and
8. R0/final `./utils/count_loc.sh` excerpts plus the N8 deletion ledger.

If an existing broad suite is temporarily unavailable for an unrelated host
issue, report the blocked command and root cause accurately. Do not declare
Tune3 complete until all scope-relevant behavior gates have been run on a
compatible host.

## 11. Formal-adoption prerequisite and documentation updates

N1 code work begins only once RN-O9 is resolved: formally adopt D3.4.4v2,
D4.6.1v2, and D4.6.2v2 in `doc/Lambda_Formal_Design.md`, bump that document's
semver, and revise older pointer-identity wording in place. The formal
documents remain authoritative until then.

In the same adoption change, reconcile:

- `vibe/Lambda_Design_Name_Identity.md` pointer-specific NI rulings;
- `vibe/jube/JS_Runtime_Name.md` as the normative working design;
- this execution plan; and
- any JS runtime redesign text that still states pointer key identity.

Implementation commits must cite the applicable formal ID in code/design
comments and review notes. When no formal rule covers a narrower choice, cite
the RN ledger entry (for example RN16 for worker Input behavior), as required
by repository policy.

## 12. Completion checklist

- [x] RN-O9 formal adoption is complete before implementation claims normative status.
- [x] N0 anchor/census/LOC baseline are recorded.
- [x] N1 NamePool has one static root, a canonical dynamic child, IDLESS input children, and safe backing lifetime.
- [x] N2 seals the whole initial closure before runtime execution and wires Input parents explicitly.
- [x] N3 replaces pointer shape/transition identity with `NameId` plus the id-less byte seam.
- [x] N4 links `PropertyKeySpec` to a context-owned `NameId[]` table and versions the module-state ABI.
- [x] N5 emits NameId/table operands and derives IC state from the owning module slab only.
- [x] N6 centralizes computed names and converts Symbol/private/reflection routes.
- [x] N7 validates Input/cache/teardown boundaries and leaves RN-D5 explicitly deferred.
- [x] N8 deletes legacy pointer paths and closes the census ledger.
- [x] All scope-relevant structural, NameId behavior, Input baseline, and release gates pass; unrelated broad-suite failures are recorded in §0.
- [x] MIR emission ratchet passes 16/16 after intentional NameId transport/IC budget re-baselining.
- [x] `./utils/count_loc.sh` reports `lambda/js` at **226,778 lines or fewer**: final **225,711**.

When this checklist is complete, the JavaScript runtime has one name-management
mechanism—the existing NamePool—and one internal property identity—`NameId`.
