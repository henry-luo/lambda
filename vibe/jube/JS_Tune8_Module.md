# JS Tune8 — JR9 Unified Module Registry and Loader Implementation Plan

**Date:** 2026-08-13

**Status:** PROPOSED — implementation has not started

**Redesign target:** **JR9**, phase R8 of
[`JS_Runtime_Redesign.md`](JS_Runtime_Redesign.md). The `Tune8` label is the
requested implementation-series name; it does **not** rename or absorb JR8
feedback vectors. JR8 remains a separate redesign target and is only a
prerequisite for the optional reusable-JS-module-artifact slice described in
§5.10 and T6.

**Implementation anchor:** the worktree after JS Tune7 completes and its
Promise/job ownership gates are green. Tune8 must re-run the T0 census against
the actual handoff because Tune7 and any intervening JR8 work can move symbols
and ownership seams.

**Design authority:** JR1 and JR9 in
[`JS_Runtime_Redesign.md`](JS_Runtime_Redesign.md). Governing formal rulings
are **D1.2–D1.3**, **D1.6**, **D4.3.1–D4.3.3**, **D5.2.2**,
**D5.3.1–D5.3.5**,
**D5.4.1–D5.4.4**, **D6.3.1**, **D7.2.1–D7.2.3**,
**D7.3.1–D7.3.5**, **D7.4.1v2–D7.4.3**, **D8.1.1**,
**D8.4.3**, and **D8.5.1–D8.5.3** in
[`Lambda_Formal_Design.md`](../../doc/Lambda_Formal_Design.md). Failed module
initialization is a transaction barrier under **S7.11.3** in
[`Lambda_Formal_Semantics.md`](../../doc/Lambda_Formal_Semantics.md) and
**D7.2.2**; a half-established module must never become an importable success.

This plan replaces the separate Lambda path index, cross-language publication
map, JavaScript `JsModule[]` cache, CommonJS metadata cache, and loader-specific
identity rules with one registry-owned definition/instance model and one
resolve → instantiate → evaluate pipeline. It keeps the Jube specifier index as
an immutable **provider catalog**, not as a second module-instance cache.

Per **D1.6** and **D8.1.1**, implementation is MIR Direct only. The frozen
C2MIR path, parser/grammar work, vendor code, JR8 feedback-vector policy, JR10
broad file decomposition, and new JavaScript module semantics are not Tune8
work.

---

## 1. Outcome and non-negotiable exits

Tune8 is complete only when every import form resolves to one canonical module
key, every module instance has one lifecycle record, and all predecessor
caches and fixed-cap module graphs are deleted.

| Gate | Required final result | Evidence |
|---|---|---|
| Canonical identity | A resolved module is identified by one `ModuleKey`: canonical id + format/provider + policy axes. Raw request spelling is never a cache key after resolution. | Alias/symlink/extension fixtures; resolver trace; key census. |
| One registry API | Lambda, JS ESM, dynamic `import()`, CommonJS `require`, JSON, hosted-language imports, and Jube namespaces all enter the same registry transaction API. | Call-site census and loader trace. |
| Correct ownership | Runtime-lived definitions contain no heap `Item`; per-context instances own namespace/evaluation/CJS/TLA Items and exact roots. | Two-context tests, heap replacement, root audit under **D5.4.3**. |
| One instance | One `(context, ModuleKey)` has one `ModuleInstance`, one namespace identity, one initialization outcome, and—when CJS—one `module` object. | Identity/cycle/repeated-load tests. |
| Transactional init | Loading publishes only the cycle-visible placeholder. Success commits atomically; failure cannot be observed as an initialized module. | Failure injection, circular failure, retry-policy tests under **S7.11.3**/**D7.2.2**. |
| No semantic caps | Module count, async-parent edges, TLA ready work, continuation work, and CJS metadata are memory-bounded rather than constant-bounded. | >64 modules, >16 async parents, queue-growth tests; source ratchet. |
| One resolver chain | Static import, dynamic import, `require`, Lambda/guest import, and Jube builtin fallback share request normalization and canonicalization; mode-specific Node behavior is expressed by resolver/format policy, not by a second loader. | Resolution matrix and zero direct-loader bypasses. |
| Jube boundary | The Jube specifier index decides provider availability without executing modules at compile time. Namespace construction occurs once through the registry instance transaction. | Static/dynamic Jube parity, lazy activation, rollback tests under **D7.3**. |
| CJS ownership | `module`, `exports`, `loaded`, `parent`, and `children` are state attached to the same `ModuleInstance`; `JsCjsState` name/object arrays are absent. | CJS cycles, exports replacement, graph metadata, reset tests. |
| Async ownership | TLA state and async-parent edges point to stable module instances, never array indices. Dynamic-import continuation captures are ordinary rooted Items, never a fixed thunk bank. | Forced-GC TLA tests and zero index/thunk-bank census. |
| Compile artifacts | Any retained `Script`, MIR context, source fingerprint, or JS cache artifact is attached to the definition entry; no independent path→artifact module map exists. Realm-bound artifacts remain instance-owned until their reuse audit passes. | Cache hit/invalidation diagnostics and ownership assertions. |
| Reset | Realm reset clears instances before heap destruction; Runtime cleanup destroys definitions and compiled owners exactly once. `module_registry_cleanup()` is not used as a batch-realm reset. | Multi-epoch, batch, teardown, poison tests. |
| Error lane | JS/Jube loader failures return the **D8.4.3** in-band error; no failure is logged and converted to a successful `ItemNull`/empty namespace. | Negative tests and error-return census. |
| Deletion | `JsModule[]`, `js_module_count_v14`, `JsCjsState` cache arrays, the global `registry_map`, raw-specifier cache probes, and duplicate path indices are absent. | Structural ratchet. |
| Behavior | Focused JS modules, Test262 module/async, Node, Lambda↔JS, hosted-language, Jube, GC, and Radiant suites are green. | T8 transcript. |
| Performance | Release builds show no unexplained import/startup regression; repeat loads become one hash lookup after canonical resolution. | Interleaved release A/B and attribution counters. |
| Source size | Production C/C++ is net-negative after old caches/loaders are deleted. | Clean T0/T8 LOC accounting. |

An adapter is not an exit if it preserves a second cache. Keeping
`js_modules[]` synchronized with `module_registry`, storing a registry pointer
inside each old record, or retaining the CJS arrays “for metadata only” fails
JR9. Compatibility function names may remain for generated-code ABI only when
their bodies are thin adapters to the single registry and own no state.

---

## 2. Root cause and current mechanism census

### 2.1 The current four authorities

The current runtime has four path/instance authorities with overlapping but
different lifetimes:

| Mechanism | Current owner | Key | State it owns | Problem |
|---|---|---|---|---|
| `Runtime::script_index` + `Runtime::scripts` | `Runtime` | canonical Lambda source path | retained Lambda `Script`, MIR context, invalidation metadata | Compiled-definition registry exists outside `module_registry`. |
| `lambda/runtime/module_registry.cpp::registry_map` | process-global static | path string | cross-language namespace `Item`, language profile, MIR pointer, loading/initialized flags | Heap Items and roots live in a global map; realm reset destroys the whole map. |
| `JsModuleRuntimeState::modules[JS_MAX_MODULES]` | JS context capsule | JS `String` bytes | namespace, TLA/evaluation state, deferred entry, async-parent indices | Fixed 64-module cap, linear lookup, duplicate cross-language publication. |
| `JsCjsState` arrays | JS context capsule | filename `Item` | CJS `module` objects and load stack | Fixed 256-module/128-stack caps and a third JS-side module identity table. |

Two more structures participate but are not allowed to become replacement
instance caches:

- The Jube specifier index is process-lifetime immutable provider metadata. It
  maps normalized builtin names to descriptors/manifests and is valid under
  **D5.4.4** and **D7.3**. It must remain discovery-only.
- `JsMirCache` and retained Lambda `Script`s own compiled artifacts. Under
  **D8.5.1–D8.5.3**, an artifact cache may be process/runtime-lived, but it may
  not own realm namespace Items or define a second module identity relation.

### 2.2 Observable defects caused by the split

1. **Different spellings can create different cache entries.** `require()`
   probes the raw specifier before resolving, then probes the canonical path;
   static and dynamic imports use other resolution paths. `./x`, `./x.js`, a
   directory fallback, and a symlink can therefore disagree about identity.
2. **A JS module is published twice.** `transpile_js_module_to_mir` first
   updates `js_modules[]`, then calls `module_register`. The two records can
   disagree after a failure, placeholder replacement, reset, or TLA deferral.
3. **Circularity and initialization are split.** The generic registry has
   `loading`/`initialized`; JS has placeholder namespaces plus independent TLA
   flags; CJS separately tracks `module.loaded` and its parent stack.
4. **Fixed caps silently define semantics.** The JS cache stops at 64 modules,
   async-parent fan-out at 16, TLA continuations at 64, the ready queue at 128,
   CJS records at 256, and the CJS stack at 128. Several overflow paths log and
   continue, violating **D8.4.3**.
5. **Indices leak into the graph algorithm.** TLA parent edges and ready work
   store `js_modules[]` indices, tying async correctness to an immovable fixed
   array and making growth/retirement unsafe.
6. **Lifetime is too broad or too narrow.** The generic registry is global but
   contains heap Items; JS records are correctly context-local but duplicate
   definition/cache metadata that should survive an instance reset.
7. **Parallel import discovery creates another identity map.** The
   `JsImportGraphNode` path map owns source/path/artifact state temporarily,
   then copies results into runtime caches. A compiler worklist is legitimate;
   a second path→module authority is not.
8. **Dynamic import uses a fixed capture bank.** Sixty-four generated native
   thunks index `p5_slot_namespace[]`; wraparound can overwrite a namespace
   captured by an unsettled chain.

### 2.3 T0 must measure, not assume

Before production edits, capture exact counts from the Tune7 handoff:

```bash
rg -n "JS_MAX_MODULES|JS_MAX_ASYNC_PARENTS|JS_TLA_MAX_CONTINUATIONS|JS_TLA_READY_QUEUE_MAX|JS_CJS_MODULE_MAX|JS_CJS_STACK_MAX|JS_P5_DYNAMIC_IMPORT_SLOTS" lambda test
rg -n "js_modules|js_module_count_v14|js_module_find|js_module_register|js_module_get" lambda/js lambda/runtime lambda/jube
rg -n "module_register_loading|module_register_with_namespace_ops|module_get|module_registry_cleanup" lambda
rg -n "js_cjs_module_names|js_cjs_module_objects|js_cjs_find_module|js_cjs_store_module" lambda/js
rg -n "read_text_file|jm_resolve_module_path|js_require_read_resolved_path|jube_specifier_resolve" lambda/js lambda/runtime lambda/jube
rg -n "script_index|runtime_script_index" lambda/runtime
wc -l lambda/runtime/module_registry.cpp lambda/js/js_mir_entrypoints_require.cpp lambda/js/js_mir_module_batch_lowering.cpp lambda/js/js_runtime.cpp lambda/js/js_runtime_state.cpp
```

Record source counts, focused-test results, release timings, module-definition
hits, instance hits, resolutions, namespace builds, compilations, evaluations,
and peak live modules. Do not copy historical line numbers or pass counts into
the completion record.

---

## 3. Scope, non-goals, and prerequisites

### 3.1 Preconditions

Production migration starts only when:

1. Tune7 has removed the Promise index/table and fixed Promise-job queues, so
   dynamic import can capture a namespace with ordinary GC-owned bound values;
2. Tune6 metadata/property gates remain green, so namespaces and CJS objects
   use the ordinary object/property mechanism;
3. the current generic registry and Jube module-graph ABI tests are green;
4. T0 establishes failure/cycle/reset behavior before changing it; and
5. the worktree handoff is recorded so unrelated user changes are preserved.

JR8 feedback vectors are **not** required to replace the runtime module
instance caches. They are required before a general JS compiled module can be
declared safely reusable across fresh realms, because mutable IC/guard cells
must be per-instantiation data under **D5.4.3** and **D8.5.3**. T6 therefore
allows realm-bound JS artifacts immediately and enables reusable artifacts only
after JR8/cache eligibility evidence exists.

### 3.2 In scope

- One canonical module request/resolution/result contract.
- A Runtime-owned module registry with definition and per-context instance
  tiers.
- Migration of Lambda retained-module lookup into the definition tier.
- Migration of JS ESM namespace, TLA, and evaluation state into instances.
- Migration of CommonJS metadata and the active module stack into instances.
- JSON, Lambda, hosted-language, and Jube namespace format/provider adapters.
- Static import, dynamic import, `require`, `load_js_module`, and hosted module
  graph calls through one loader.
- Growable dependency, async-parent, ready, continuation, and CJS graph state.
- Transactional begin/commit/fail semantics and explicit cached-failure policy.
- Compile artifact ownership/invalidation keyed by the definition entry.
- Removal of the old maps, arrays, caps, root-registration loops, and reset
  calls.
- Structural checks, focused tests, forced-GC tests, full gates, release
  measurement, and documentation.

### 3.3 Out of scope

- Full ECMAScript Module Record/linking redesign or complete named live-binding
  conformance. Existing namespace and binding behavior is preserved; a later
  semantics project may introduce explicit export cells.
- Replacing the present first-await TLA approximation with true spec-complete
  async module execution.
- Node package `exports`/`imports`, conditional exports, loaders/hooks, or a
  complete Node resolver rewrite unless already supported behavior needs to be
  expressed through the new resolver contract.
- A real disk-backed JS machine-code cache. **D8.5.2–D8.5.3** remain the gate;
  `module.enableCompileCache()` must not claim more than the implementation
  provides.
- JR8 feedback policy or JR10's broad `js_runtime.cpp` decomposition.
- Hot module replacement, unloading Jube DSOs, worker shared heaps, or
  cross-isolate namespace sharing.
- Parser/grammar, vendor, C2MIR, unrelated Node APIs, or DOM redesign.

### 3.4 Formal-spec impact

JR9 is an implementation consolidation of existing rulings: per-context state
under **D5.4.3**, cached definitions under **D7.2.3/D8.5.1**, transactional
initialization under **S7.11.3/D7.2.2**, and Jube discovery/activation under
**D7.3**. No formal semantic change is expected.

If T0 reveals that completing the unification requires choosing new observable
behavior—rather than preserving or correcting an already authoritative
ECMAScript/Node rule—stop before implementation and revise the relevant
formal `S#`/`D#` ruling with a `v2` suffix and semver bump, then update this
plan and the working design in the same change.

---

## 4. Decisions and invariants

The `JM#` labels below cover details not separately ruled by the formal design.

### JM1 — One registry means one authority, not one lifetime

The final `ModuleRegistry` has two ownership tiers behind one API and one
canonical key space:

- **Definition tier, Runtime-owned:** source/provider identity, format,
  dependency identities, fingerprints, immutable compile metadata, and an
  eligible retained compiled artifact. It contains no heap `Item`.
- **Instance tier, EvalContext-owned:** namespace, evaluation status/error,
  module-variable state, TLA fields, CommonJS object/exports, parent/child
  edges, and any realm-bound compiled artifact.

One physical global hashmap holding both would violate **D5.4.3**. Two
unrelated lookup systems would violate JR9. The two-tier registry is the only
valid combination: one identity/transaction mechanism with state stored at
the lifetime that owns it.

### JM2 — Canonical key is established before lookup

No loader probes a cache by raw request spelling. Resolution produces:

```c
typedef struct ModuleKey {
    ModuleScheme scheme;       // file, jube, synthetic, hosted
    ModuleFormat format;       // lambda, js-esm, js-cjs, json, guest, namespace
    const char* canonical_id;  // registry-owned stable bytes
    uint64_t policy_hash;      // format-affecting compile/resolution policy
} ModuleKey;
```

Names are illustrative; grep for an existing key/result type before adding
one. Equality and hashing cover all fields. `canonical_id` is an absolute,
normalized, real path for existing files; a stable URI-like id for synthetic
units; and one normalized builtin id for Jube namespaces. Request spelling,
an `Item` address, a `String*`, and a transient module index are never identity.

`format` remains part of the key because current LambdaJS intentionally treats
some `.js` files differently by goal (`require` CJS versus ESM import). JR9
must not accidentally merge semantically different instantiations merely
because they read the same file.

### JM3 — Resolver and format are separate

Resolution answers **what module is this?** A format/provider answers **how is
it instantiated and evaluated?** The single loader consults both:

```text
request(raw specifier, importer key, import goal, attributes)
  -> resolve without executing module code
  -> get/create ModuleDefinition by ModuleKey
  -> get/create ModuleInstance in current context
  -> instantiate placeholder/link graph
  -> evaluate or join the in-progress transaction
  -> project namespace / require value / dynamic-import promise
```

Node path rules remain a resolver plug-in, as JR9 requires. The Jube specifier
catalog is another resolver/provider input. Neither owns instance state.

### JM4 — One explicit instance state machine

Every instance moves monotonically through:

```text
NEW -> INSTANTIATING -> INSTANTIATED -> EVALUATING
                                |          |
                                |          +-> ASYNC_PENDING -> EVALUATED
                                +-----------------------------> EVALUATED
                                \------------------------------> FAILED
```

- `INSTANTIATING` publishes the cycle-visible placeholder identity.
- `INSTANTIATED` means dependencies/bindings are linked but the body has not
  necessarily completed.
- `ASYNC_PENDING` owns TLA target/order/parent state.
- `EVALUATED` is the only successful terminal state.
- `FAILED` owns a rooted error and is never returned as a namespace success.

State transitions have one owner and are checked in debug builds. There is no
independent `loading`, `initialized`, `body_executed`, and `module.loaded`
authority; format adapters derive their projections from the instance state.

### JM5 — Transaction policy is format-explicit

- **ESM/Lambda/hosted modules:** evaluation failure is cached on the instance;
  subsequent imports in the same context observe the same failure, never the
  partial namespace.
- **CJS:** expose the in-progress `module.exports` during a cycle. On an abrupt
  body failure, remove the failed CJS instance from the successful-instance
  index after unlinking parent/child bookkeeping, matching Node retry shape;
  the definition/compiled artifact may remain cached. T0 locks the exact
  existing/error-object expectations before this correction lands.
- **JSON:** parse then commit; parse failure publishes no successful instance.
- **Jube namespace:** provider activation/namespace build is one transaction;
  failed activation leaves no partial instance, while the provider catalog
  retains its unavailable/diagnostic state under **D7.3.2**.

All failure paths return the **D8.4.3** error lane to JS callers. Generic C
internals may use a checked status plus a rooted error out-slot, but the JS/JIT
boundary never converts failure to `ItemNull` or logs-and-continues.

### JM6 — Namespace and CJS value are projections of one instance

A module instance may have both:

- `namespace_obj`: the ESM/cross-language namespace identity; and
- `require_value`: the value returned by CJS `require` (normally current
  `module.exports`).

These are fields of one record, not entries in separate caches. Replacing
`module.exports` updates `require_value` and the namespace's `default`
projection through one helper. Repeated `require` returns the same current
value; ESM access returns the same namespace object.

### JM7 — Async graph uses stable instance references

Async parents, dependency edges, and ready work store stable
`ModuleInstance*` references or stable registry handles. They never store
hashmap-entry addresses or numeric positions in a movable array. Instance
records are individually allocated from context-owned native storage and live
until realm reset, so an edge cannot dangle during evaluation.

Parent/dependency vectors and ready work grow through an existing `ArrayList`
or shared internal vector helper. Item continuations use the shared growable
rooted deque introduced by Tune7. No third grow/trace implementation is
created (project rule 13).

### JM8 — Active evaluation is a stack, not scattered globals

The context owns a growable stack of active `ModuleInstance*` records. Entering
an initializer establishes from that record:

- active module-variable state id;
- active namespace;
- `context->current_file`/canonical importer base;
- CJS parent when applicable; and
- the recovery/transaction boundary.

Exit restores the prior frame in LIFO order on success, error, and recovery.
Generated code may keep compatibility accessors such as
`js_get_active_module_namespace`; the authoritative value comes from the top
registry frame, not a free `Item` global.

### JM9 — Provider catalogs are not instance registries

The Jube catalog remains process-lifetime immutable metadata under
**D5.4.4/D7.3**. Catalog queries may normalize names, identify a manifest, and
return a provider descriptor without executing module code. The unified loader
performs lazy activation and calls a namespace builder only while creating the
current context's `ModuleInstance`.

`jube_specifier_resolve()` may remain as a compatibility wrapper during
migration, but its returned Item must immediately commit into the instance and
no Jube-global namespace cache may become authoritative over registry identity.

### JM10 — Compiled code and realm state never share an owner by accident

Each definition has an explicit artifact class:

| Class | Owner | Use |
|---|---|---|
| `NONE` | — | Jube namespace, JSON before parse, or uncompiled definition. |
| `RUNTIME_REUSABLE` | `ModuleDefinition` | Retained Lambda `Script`; audited JS MIR artifact with per-instance mutable cells. |
| `REALM_BOUND` | `ModuleInstance` | Current JS module MIR context until JR8/de-pointering eligibility is proven. |

No raw realm `Item`, NameId image, module-variable slab, IC cell, or shape
cache enters a reusable artifact. **D5.4.3** forbids context-dependent values
at code-baked addresses; **D8.5.3** requires per-instantiation mutable JS cells
to become named data before persistent code-image reuse.

The Node compile-cache API stores policy and artifact status on the definition
entry. It does not gain a parallel filename map, and it does not claim disk
machine-code caching before **D8.5.2–D8.5.3** are implemented.

### JM11 — Invalidation versions definitions, never mutates a live instance

File-backed definitions record canonical path, mtime, size, and source hash.
The existing Lambda L1 invalidation cone is promoted into the registry. A
changed definition and its dependent artifacts are retired at a quiescent
boundary; a live instance pins the definition/artifact it began with. New
instances resolve to the replacement definition.

Within one realm, an already evaluated CommonJS/ESM instance remains cached as
today even if the file changes on disk. JR9 is not hot reload.

### JM12 — Locks stay out of execution

Runtime definition lookup/creation and parallel compilation may use the
existing cold-path mutex. The per-context instance registry is owner-thread
only. Namespace/property reads, function calls, and repeated evaluation-state
checks add no lock, atomic RMW, or coherence operation, as required by
**D5.4.4**.

### JM13 — Reset and destroy are different operations

The required order is:

1. stop new module evaluation and drain/cancel module-owned async work;
2. invoke Jube runtime reset/detach while its session and heap roots are valid;
3. destroy the context instance registry, releasing module roots and
   realm-bound MIR contexts;
4. clear module-variable slabs and other JS context state;
5. replace/destroy the heap;
6. keep Runtime definitions/reusable artifacts for a compatible new realm;
7. at Runtime cleanup, destroy definitions and retained artifacts exactly once.

`js_batch_reset_to` may create a new instance generation while preserving its
hot preamble state, but it may not reuse a namespace from the prior test
generation.

### JM14 — Diagnostics never select behavior

Registry counters, trace names, source labels, and compile-cache reports are
context/runtime-local diagnostics under **D5.4.4**. They cannot decide module
format, identity, failure policy, or whether initialization is skipped.

---

## 5. Target data and API shape

Exact names are subject to the existing-helper audit; the ownership and
separation are not.

### 5.1 Runtime definition tier

```c
typedef struct ModuleDefinition {
    ModuleKey key;
    const char* source_lang;              // stable registry-owned/static name
    const ModuleFormatOps* format_ops;    // immutable table
    const ModuleProvider* provider;       // file/Jube/hosted/synthetic

    ModuleFingerprint fingerprint;
    ArrayList* dependencies;              // ModuleDefinition*; no Items
    ArrayList* dependents;                // invalidation edges; no Items

    ModuleArtifactClass artifact_class;
    void* compiled_artifact;              // owner defined by artifact_class
    void (*artifact_destroy)(void*);

    uint64_t version;
    bool loading_definition;
    bool retired;
} ModuleDefinition;

typedef struct ModuleRegistry {
    struct hashmap* definitions;           // ModuleKey -> ModuleDefinition*
    ArrayList* definition_order;           // stable diagnostics/cleanup order
    ModuleRegistryStats stats;
    void* definition_mutex;                // cold path only
} ModuleRegistry;
```

`Runtime` owns `ModuleRegistry*`. `Runtime::scripts` may remain the owner/order
list for Lambda `Script` objects and stable `Script::index`, but
`Runtime::script_index` is deleted: canonical path lookup goes through the
definition index. A Lambda definition points to its `Script` as the compiled
artifact and reuses existing L1 retention/invalidation semantics.

### 5.2 Context instance tier

```c
typedef struct ModuleInstance {
    ModuleDefinition* definition;          // stable, non-GC owner
    ModuleInstanceState state;

    // Contiguous rooted fields; exact list finalized by the T0 Item census.
    Item namespace_obj;
    Item require_value;
    Item evaluation_error;
    Item awaited_target;
    Item cjs_module_obj;

    uint32_t module_state_id;
    void* deferred_entry;
    void* realm_bound_artifact;
    int64_t async_eval_order;
    int64_t pending_async_deps;

    ArrayList* dependencies;               // ModuleInstance*
    ArrayList* async_parents;              // ModuleInstance*
    ArrayList* cjs_children;               // ModuleInstance*
    ModuleInstance* cjs_parent;

    bool post_await_pending;
    bool failure_committed;
} ModuleInstance;

typedef struct ModuleRealmRegistry {
    struct hashmap* instances;             // ModuleKey -> ModuleInstance*
    ArrayList* instance_order;              // teardown and diagnostics
    ArrayList* active_stack;                // ModuleInstance*
    ArrayList* tla_ready;                   // ModuleInstance*
    RuntimeAsyncDeque continuations;        // rooted callback Items
    Item continuation_storage;
    int64_t async_eval_order_counter;
    int64_t module_depth;
    int64_t draining_depth;
    uint64_t realm_generation;
} ModuleRealmRegistry;
```

The Item block in each instance is registered as an exact root range once at
instance construction and unregistered before heap teardown, satisfying
**D5.3/D5.4.2**. Alternatively, if an existing context-owned GC container can
own the complete instance Item block without adding another mechanism, use it;
the implementation plan must record the chosen existing helper. Raw
`ModuleInstance*` pointers never cross a heap or context lifetime.

### 5.3 Resolver contract

```c
typedef struct ModuleRequest {
    const ModuleKey* importer;
    const char* specifier;
    int specifier_len;
    ModuleLoadGoal goal;       // static import, dynamic import, require, host
    ModuleImportAttributes attributes;
} ModuleRequest;

typedef struct ModuleResolution {
    ModuleKey key;
    const ModuleFormatOps* format_ops;
    const ModuleProvider* provider;
    const char* source_path;
} ModuleResolution;
```

Resolution is side-effect-free with respect to module execution. File probing
and manifest discovery are allowed; Jube `init`, namespace builders, JS body
execution, and Lambda package initialization are not.

Resolution order is explicit and tested:

1. normalize goal-specific aliases (`node:`, supported `.js` builtin alias);
2. query the Jube provider catalog without activation;
3. for file/package requests, apply the existing Node/Lambda path rules using
   the importer key as base;
4. canonicalize an existing file once;
5. classify its format (`js-esm`, `js-cjs`, JSON, Lambda, hosted guest);
6. construct/lookup the canonical definition.

Static/dynamic ESM and `require` use the same resolver entry with different
goals. There is no raw-specifier pre-cache probe.

### 5.4 Format operations

The generic registry owns identity and transactions; language adapters own
format semantics:

```c
typedef struct ModuleFormatOps {
    Item (*create_placeholder)(ModuleLoadContext*, ModuleInstance*);
    Item (*instantiate)(ModuleLoadContext*, ModuleInstance*);
    Item (*evaluate)(ModuleLoadContext*, ModuleInstance*);
    Item (*project_result)(ModuleLoadContext*, ModuleInstance*, ModuleLoadGoal);
    void (*rollback)(ModuleLoadContext*, ModuleInstance*);
    void (*destroy_instance)(ModuleInstance*);
} ModuleFormatOps;
```

Fallible callbacks return `ItemError` on the JS/Jube lane. No callback owns
registry insertion/removal; it asks the transaction object to commit/fail.
The format table is immutable and contains no context Item under **D5.4.4**.

Required adapters:

- Lambda package → per-context JS/guest namespace projection;
- JS ESM;
- JS CommonJS wrapper;
- JSON;
- hosted language through `JubeModuleGraphAPI`;
- Jube namespace provider; and
- synthetic/entry module where needed by eval/test harnesses.

### 5.5 One load transaction

The one public internal loader has this semantic shape:

```text
module_load(request):
  resolution = module_resolve(request)
  definition = definition_get_or_create(resolution)
  instance = instance_lookup(current_context, definition.key)

  EVALUATED       -> project cached result
  FAILED          -> return cached error (except retired failed CJS instance)
  INSTANTIATING/
  EVALUATING      -> return cycle projection or join async completion
  NEW             -> begin transaction, create placeholder, load deps,
                     instantiate, evaluate, commit/fail, project result
```

Every early return restores the active module frame and recovery checkpoint.
The transaction owns the invariant comment at the commit/fail point required
by project rule 12.

### 5.6 Compatibility ABI

Generated MIR and external tests currently call `js_module_*` helpers. Avoid a
flag-day rename:

- retain required exported names temporarily;
- implement each as a no-state adapter that resolves a `ModuleInstance` in the
  active registry;
- change lowering to use stable instance handles or canonical ids where that
  removes repeated string lookup;
- delete adapters not referenced by generated code/catalog after the source
  and MIR-import census reaches zero.

An adapter may translate arguments; it may not maintain mirrored fields.

### 5.7 CJS integration

The CJS wrapper remains a format adapter, but registry begin must create the
`module` object before body execution and install it on the active instance.
`__lambda_cjs_enter/complete/leave` become active-frame operations:

- `enter`: validates the active instance, exposes its pre-created `module`,
  connects parent/child once, and sets `loaded = false`;
- assignment to `module.exports`: updates the instance's require projection;
- `complete`: commits `loaded = true` only after body success;
- `leave`: pops the exact active frame on all exits;
- cycle lookup: returns the in-progress current `module.exports`;
- failure: removes the failed CJS instance from successful lookup and detaches
  graph edges without deleting the reusable definition.

The fixed name/object arrays and manual string scan are deleted. The active
stack grows and returns an in-band error on allocation failure.

### 5.8 TLA and dynamic import integration

JR9 preserves the current TLA approximation but moves its ownership:

- `has_tla`, pending dependency count, awaited target, deferred entry,
  body/post-await state, saved module-state id, and evaluation error live on
  `ModuleInstance` or its JS-format extension;
- async-parent edges are growable stable instance references;
- ready selection reads `async_eval_order` from queued instances;
- a failed child propagates the rooted failure through parent instances once;
- continuation callbacks live in the registry-owned Tune7 deque;
- `js_p5_chain_dynamic_import` uses one cached native handler bound with the
  namespace Item through `js_bind_function`, deleting all 64 generated thunks,
  `p5_slot_namespace[]`, and modulo slot reuse.

Dynamic `import()` asks the same loader for an ESM-goal instance, then projects
the namespace through the existing Promise/TLA ordering behavior. It never
calls `read_text_file` or `transpile_js_module_to_mir` directly.

### 5.9 Jube and hosted-language integration

The existing `JubeModuleGraphAPI` operations map to registry transactions:

| Existing host callback | Final meaning |
|---|---|
| `loading_namespace` | Lookup active instance; return cycle placeholder only while state is instantiating/evaluating. |
| `module_state` | Project the unified state enum, not generic-registry booleans. |
| `module_begin_loading` | Begin/get transaction for canonical hosted key. |
| `module_publish` | Commit namespace/provider membrane into that transaction. |
| `load_lambda_module` | Re-enter the one loader with a Lambda-format goal. |

The ABI may be extended additively only if the current callbacks cannot carry
canonical key or failure state without ambiguity, following **D7.3.2–D7.3.4**.
Do not expose `Runtime*`, `EvalContext*`, `ModuleInstance*`, or other core types
to a guest; opaque execution handles remain mandatory under **D7.4.3**.

Jube namespace specifiers resolve to a provider descriptor first. The namespace
builder runs once per context instance, after lazy module activation succeeds.
Static and dynamic Jube packaging must produce identical instance behavior.

### 5.10 Compile-cache integration

JR9 attaches cache ownership but does not invent a new cache format:

1. Promote `Runtime::script_index` lookup/invalidation into definition entries;
   retained Lambda `Script` remains the artifact owner required by L1.
2. Attach existing `JsMirCacheEntry`/compiled-module metadata to a JS
   definition when its source/base/policy key matches the registry key.
3. Until JR8 and the pointer audit prove reusable instantiation, tag normal JS
   module artifacts `REALM_BOUND` and destroy them with the instance.
4. Once eligible, definition owns the MIR context and immutable pools;
   instance owns module vars, namespace, name-link image, feedback cells, and
   other mutable runtime cells.
5. The parallel compiler stores results directly on definitions. Its
   topological worklist contains non-owning `ModuleDefinition*` references and
   dependency edges; it has no second path hashmap or artifact owner.
6. Source changes retire definitions/dependent artifacts at a safe boundary,
   reusing the Lambda L1 invalidation-cone implementation.
7. `module.enableCompileCache()` policy and diagnostics reference these entry
   states. Disk code-image read/write remains disabled until **D8.5.2–D8.5.3**.

### 5.11 Rooting and GC

For each `ModuleInstance`, enumerate every Item field in one contiguous root
block. Registration occurs once with the explicit current heap/context at
construction under **D5.4.2**. Stores into namespace/error/await/CJS slots use
destination-owned scalar storage under **D5.2.2** where relevant.

Rules:

1. A `ModuleInstance*` is native context state, not a script-visible pointer.
2. Any derived GC pointer is protected by a rooted owner across `MAY_GC`, user
   callbacks, provider activation, compilation re-entry, and event-loop drain.
3. Definition entries contain zero Items and require no heap root.
4. TLA continuation queues root their Item storage once through the shared
   deque owner.
5. Realm reset unregisters/clears instance roots before heap destruction.
6. A retained MIR artifact cannot retain an old realm's module-var slab,
   namespace, NameId array, or feedback cell.

### 5.12 Capacity and allocation failure

All former module caps become growable storage. Growth failure is consumed by
the current load transaction and returned as an in-band error. It may not:

- log and drop an async parent;
- return a successful empty namespace;
- partially append one side of a parent/child edge;
- leave an instance stuck as loading; or
- publish a definition whose dependency list is incomplete.

Use append-then-publish or reserve-before-mutate so each graph update is
transactional.

---

## 6. Phased implementation

```text
T0  census + behavior/ownership baseline
 |
T1  registry ownership split + canonical key/transaction core
 |
T2  resolver/format contract + Lambda definition-index migration
 |
T3  JS ESM/static/dynamic/TLA instance migration
 |
T4  CommonJS + JSON migration
 |
T5  hosted-language and Jube namespace migration
 |
T6  parallel compilation + artifact/cache integration
 |
T7  predecessor deletion + reset/teardown closure
 |
T8  full validation, release evidence, docs, JR10 handoff
```

T3 and T4 may be developed on separate branches after T2, but they may not
land with two authoritative caches. Each landed migration either routes a
format completely through the unified registry or remains entirely on the old
path until its atomic switch.

### T0 — Baseline, census, and semantic locks

#### Work

1. Run §2.3 and record exact mechanism/call-site/cap/root counts.
2. Add diagnostics behind a non-semantic flag:
   - raw requests and canonical resolution;
   - definition lookup hit/miss/create/retire;
   - instance hit/begin/commit/fail;
   - namespace build, compile, and evaluation counts;
   - cycle joins and TLA edge/queue peaks;
   - peak definitions/instances/retained artifact bytes.
3. Capture release baselines for a no-import entry, repeated builtin require,
   repeated file require, static ESM diamond, dynamic import, and a mixed
   Lambda↔JS graph.
4. Add focused behavior fixtures before refactoring:
   - alias identity and canonical path;
   - ESM cycle and dependency failure;
   - CJS cycle, `module.exports` replacement, parent/children, and throw retry;
   - JSON identity/failure;
   - TLA ordering/error propagation;
   - Jube lazy activation and failed activation rollback;
   - batch reset/second realm identity.
5. Record which current behavior is standards-correct and which correction is
   explicitly part of JM5. Do not accidentally freeze a known bug merely
   because T0 observed it.

#### Exit gate

- Baseline transcript and LOC/census are committed to this doc's evidence
  appendix during implementation.
- Each observable transition has a focused test.
- No production behavior has changed.

### T1 — Registry ownership split and transaction core

#### Work

1. Replace the process-global `registry_map` with a `ModuleRegistry*` owned by
   `Runtime`. Construction/destruction happens in Runtime lifecycle, not lazy
   heap state.
2. Add the definition key/index and context-owned `ModuleRealmRegistry` capsule.
   The stable `EvalContext` reaches it through its Runtime/JS capsule under
   **D5.4.1–D5.4.3**.
3. Split the old `ModuleDescriptor` into definition and instance data. Move
   `path`, language/format/provider, profile, and artifact metadata to the
   definition; move namespace, namespace ops, loading/evaluation state, and
   error to the instance.
4. Implement checked begin/commit/fail/get operations and the state-transition
   assertions in JM4/JM5.
5. Implement exact instance Item rooting and realm reset/destroy. Definition
   destruction must never unregister heap roots because definitions own none.
6. Adapt current `module_get`, `module_register_loading*`, and
   `module_register*` callers temporarily to the new transaction core. These
   adapters use the active Runtime/context and own no table.
7. Add unit tests for canonical key equality, state transitions, failed commit,
   reset ordering, and two independent contexts.

#### Exit gate

- No global module registry contains Items.
- Existing cross-language tests pass through adapters.
- Definition and instance teardown counters balance under forced GC.
- The old JS/CJS caches are still untouched, but the generic registry is no
  longer a competing lifetime mechanism.

### T2 — Resolver/format contract and Lambda definition migration

#### Work

1. Inventory and promote the existing path helpers instead of copying them:
   `jm_resolve_module_path`, require extension/directory/package-main logic,
   Lambda source classification, hosted-language discovery, and Jube specifier
   normalization.
2. Implement one `module_resolve` chain with goal-specific policy and one
   canonical `ModuleResolution` result. Resolver failure returns a materialized
   error to its caller.
3. Remove all raw-request cache probes from loader entry points; lookup begins
   only after resolution.
4. Move `Runtime::script_index` lookup, put/delete, and invalidation-cone entry
   points behind `ModuleRegistry` definitions. Keep `Runtime::scripts` stable
   slots and `Script::index` for current Lambda MIR symbol naming.
5. Delete `Runtime::script_index` after every lookup uses the definition tier.
6. Register Lambda package definitions/artifacts through the registry and
   instantiate a fresh namespace per context. Preserve boxed export membrane
   behavior under **D7.2.2** and existing `ModuleNamespaceOps`.
7. Make source fingerprints and dependent edges registry-owned; verify L1
   retained-cache hit/invalidation behavior is byte-for-byte unchanged.

#### Exit gate

- One definition index serves Lambda and cross-language lookups.
- `script_index`/`runtime_script_index_*` are absent.
- `make test-lambda-baseline` and Lambda MIR-cache A/B remain green/non-regressing.
- Static provider discovery executes no Jube initializer.

### T3 — JS ESM, dynamic import, and TLA instance migration

#### Work

1. Define the JS ESM format adapter and move all `JsModule` fields into
   `ModuleInstance` or a single JS-format extension owned by it.
2. Change `transpile_js_module_to_mir` to accept/obtain the active load
   transaction. It must not independently register placeholder/final namespace.
3. Change `jm_load_imports` to call `module_load` with the parent instance/key.
   Dependencies are recorded once on definition and instance graphs.
4. Change lowering/runtime `js_module_*` helpers to resolve active instances.
   Preserve current generated-code ABI until the MIR import census permits
   deletion/renaming.
5. Replace numeric async-parent and ready-queue indices with stable instance
   references and growable vectors.
6. Move TLA depth/order/continuations into `ModuleRealmRegistry`; replace fixed
   callback storage with the Tune7 deque.
7. Replace the dynamic-import P5 thunk bank with one bound native handler that
   captures the namespace Item.
8. Route `js_dynamic_import` exclusively through the loader with ESM goal; it
   performs no direct file read or compilation.
9. Atomically switch all ESM lookup/publication to the new registry, then in
   the same migration batch delete `JsModule[]`, its count, find/index helpers,
   root loop, and fixed TLA arrays/caps.

#### Exit gate

- `JS_MAX_MODULES`, `JS_MAX_ASYNC_PARENTS`, `JS_TLA_MAX_CONTINUATIONS`,
  `JS_TLA_READY_QUEUE_MAX`, and `JS_P5_DYNAMIC_IMPORT_SLOTS` are absent.
- More-than-capacity fixtures complete without dropped work.
- Static/dynamic ESM, cycles, TLA, evaluation failure, module vars, and
  forced-GC tests pass.
- A JS module is published once, by the transaction commit.

### T4 — CommonJS and JSON migration

#### Work

1. Define CJS/JSON format adapters over the same resolver and transaction.
2. Pre-create the CJS `module` object and exports projection on the instance;
   make wrapper enter/complete/leave operate on the active frame.
3. Store CJS parent/child relationships as instance references, de-duplicated
   before append. Expose JS `children` through the instance-owned array/value
   without a second native cache.
4. Make in-progress cycle reads return current `module.exports`; keep namespace
   `default` synchronized from one helper.
5. Implement JM5 failure removal/retry and graph rollback.
6. Parse JSON inside the transaction and commit one instance result.
7. Route `js_require` through one loader call. It retains only argument
   coercion/Node result projection; direct builtin probing, raw cache probing,
   file reading, wrapping, compilation, and JSON parsing leave the entry point.
8. Delete `JsCjsState` arrays, manual filename scans, module count/stack caps,
   duplicated reset/root ranges, and direct `js_module_get_builtin` loader use.

#### Exit gate

- CJS/JSON behavior and failure tests pass, including cycles and exports
  replacement.
- `JS_CJS_MODULE_MAX`, `JS_CJS_STACK_MAX`, module name/object arrays, and the
  independent CJS cache helpers are absent.
- `require` and ESM imports of canonical aliases show the intended same or
  format-distinct identity from `ModuleKey`, never accidental spelling-based
  identity.

### T5 — Hosted-language and Jube namespace migration

#### Work

1. Map `JubeModuleGraphAPI` callbacks to the unified begin/state/commit/fail
   transaction operations. Extend the ABI additively only if required.
2. Route `load_js_module` and hosted-language imports through `module_load`;
   remove direct source-read/transpile ownership from cross-language entry
   points.
3. Add Jube provider adapter: catalog lookup is resolution, descriptor
   activation + namespace build is instantiation, and the result commits to
   the current context's instance.
4. Ensure a failed dynamic module init rolls back registry instance state and
   Jube descriptor visibility consistently under **D7.3.2**.
5. Preserve namespace membranes: the registry calls language-owned `get`,
   arity, and function-pointer ops without assuming a JS Map representation.
6. Test static/dynamic packaging parity, absent module, integrity failure,
   dependency init failure, repeated aliases, GC, and context reset.
7. Update `Lambda_Design_Jube_Node_Hosting.md` JN6/JN11 and its cache language:
   the registry instance now owns namespace identity; Jube session hooks own
   only provider/session resources.

#### Exit gate

- All module kinds use one registry transaction.
- The Jube specifier index contains no namespace Items and performs no instance
  caching.
- Hosted-language circular imports and Lambda↔guest imports pass.
- Static and dynamically packaged Node namespace behavior is identical.

### T6 — Parallel compilation and artifact/cache integration

#### Work

1. Change `jm_precompile_js_imports` discovery to resolve canonical definitions
   and record dependency edges on them.
2. Replace `JsPathIndexEntry` and artifact-owning `JsImportGraphNode` records
   with a transient non-owning schedule of `ModuleDefinition*`. A worklist may
   carry depth/status; it may not own identity/source/artifacts separately.
3. Store parallel compile output directly on the definition (reusable) or the
   target instance (realm-bound), according to JM10.
4. Preserve static NamePool prelink ordering and serial dependency evaluation;
   worker threads compile only immutable definition inputs and never touch the
   instance registry or heap Items.
5. Connect existing `JsMirCache` module class to definition keys and counters.
   Do not enable reusable general module artifacts until the pointer/feedback
   audit proves every mutable cell per-instance.
6. Reuse Lambda L1 invalidation for JS definitions and transitive dependents;
   ensure live instances pin old versions until the realm boundary.
7. Move Node compile-cache policy/report state to registry/cache policy naming;
   keep disk artifact behavior honest and gated by **D8.5.2–D8.5.3**.
8. Remove deferred-MIR ownership for artifacts now owned by definitions or
   instances. Retain `jm_defer_mir_cleanup` only for genuinely non-module
   compilation owners; audit its fixed cap separately rather than hiding it.

#### Exit gate

- Parallel and recursive loading produce identical registry/evaluation traces.
- No second path→module map owns compile results.
- Cache disabled/enabled modes are behavior-identical; eligible hits and
  invalidations are visible in diagnostics.
- JR8-dependent reuse is either proven and enabled or explicitly remains
  `REALM_BOUND`; registry completion does not depend on an unsafe optimization.

### T7 — Delete predecessors and close lifecycle

#### Work

1. Delete obsolete compatibility adapters after the MIR import/catalog census
   reaches zero; retain only minimal ABI wrappers still called by generated
   code.
2. Delete the old global `registry_map`, `ModuleDescriptor` mixed-lifetime
   fields, `js_module_cache_reset`, `js_cjs_metadata_reset`, and all fixed
   module root-registration loops.
3. Replace batch calls to `module_registry_cleanup()` with instance-generation
   reset. Runtime cleanup alone destroys definitions.
4. Verify the JM13 order in normal exit, parse/compile failure, JIT fault,
   timeout recovery, heap replacement, batch reset, and Jube detach.
5. Add a structural checker (prefer extending an existing architecture checker)
   that fails on:
   - predecessor arrays/caps/symbols;
   - direct module source reads outside resolver/provider code;
   - direct namespace publication outside transaction commit;
   - heap Items in `ModuleDefinition`;
   - process-global `ModuleInstance`/namespace state;
   - use of a raw request string as a cache key.
6. Record mechanism and LOC deltas. Delete forwarding code rather than moving
   it to satisfy the gate.

#### Exit gate

- Structural ratchet is green with zero exceptions.
- Multi-context and multi-epoch poison tests show no stale namespace/root/code
  pointers.
- Every compiled artifact and module instance is destroyed exactly once.
- Production C/C++ delta is net-negative.

### T8 — Full validation, release evidence, docs, and handoff

#### Work

1. Run §10 from a clean build and record exact pass counts/exclusions.
2. Run interleaved release measurements from §11. Debug builds are never used
   for performance acceptance (project rule 10).
3. Update implementation documentation and diagrams listed in §9.
4. Update `JS_Runtime_Redesign.md`: mark JR9 implemented only after deletion,
   not after the compatibility adapter lands.
5. Record the final state/ownership diagram, resolver matrix, failure policy,
   cache eligibility, retained risks, source census, and LOC.
6. Hand JR10 real module/file boundaries; JR10 must not rediscover or split
   transitional loader mechanisms.

#### Exit gate

- All gates pass with no unexplained baseline loss.
- Release performance meets §11.
- Docs describe implemented ownership and behavior rather than this proposal.
- The old private JS/CJS loader lifecycle cannot be re-enabled by a flag.

---

## 7. Migration and deletion ledger

| Predecessor | Temporary migration owner | Final replacement | Delete by |
|---|---|---|---|
| global `registry_map` | T1 compatibility API | Runtime `ModuleRegistry` | T1 |
| mixed `ModuleDescriptor` | T1 split adapter | `ModuleDefinition` + `ModuleInstance` | T2 |
| `Runtime::script_index` | T2 definition adapter | registry definition index | T2 |
| raw-specifier `require` cache probe | none | resolve then instance lookup | T2/T4 |
| `JsModule modules[64]` | atomic T3 switch | context instance index | T3 |
| module linear byte scans | T3 adapter | canonical key hashmap | T3 |
| async-parent numeric indices | T3 | stable instance-reference vectors | T3 |
| fixed TLA continuation/ready arrays | T3 | rooted deque + growable ready vector | T3 |
| 64 P5 namespace thunks/slots | T3 | one bound handler capture | T3 |
| CJS filename/object arrays | atomic T4 switch | CJS fields on `ModuleInstance` | T4 |
| fixed CJS active stack | T4 | registry active frame stack | T4 |
| direct builtin dispatch in loaders | T4/T5 adapter | Jube provider resolver | T5 |
| hosted module begin/publish mixed map | T5 adapter | unified transaction | T5 |
| `JsPathIndexEntry` artifact graph | T6 schedule adapter | definition dependency graph | T6 |
| module-owned deferred MIR list entries | T6 | definition/instance artifact owner | T6/T7 |
| batch `module_registry_cleanup()` | T7 | realm instance reset | T7 |

Every adapter has one delete phase. There is no “legacy fallback” after its
format switches.

---

## 8. File and ownership map

| File / area | Tune8 ownership |
|---|---|
| `lambda/runtime/module_registry.h/.cpp` | Canonical key, Runtime definition registry, context instance transactions, namespace membrane, lifecycle/reset APIs. |
| `lambda/runtime/transpiler.hpp` | Runtime registry owner; remove `script_index`; preserve stable `scripts` ownership/order. |
| `lambda/runtime/runner.cpp` | Lambda definition/artifact registration, L1 lookup/invalidation, Runtime construction/cleanup. |
| `lambda/runtime/transpile-mir.cpp` | Cross-language namespace/function lookup via the unified instance/definition API. |
| `lambda/js/js_runtime_state.hpp/.cpp` | Own `ModuleRealmRegistry*`/generation seam; remove `JsModuleRuntimeState`, `JsCjsState`, fixed roots/caps, and duplicate resets. |
| `lambda/js/js_runtime.h` | Temporary generated-code adapters and final narrow module runtime ABI. |
| `lambda/js/js_runtime.cpp` | Remove private module/TLA arrays, linear lookup, CJS cache state, P5 thunk bank; retain JS-format behavior only until JR10 moves files. |
| `lambda/js/js_mir_entrypoints_require.cpp` | Request coercion/result projection; resolver/format adapters; remove direct loader orchestration from `require`/dynamic import/load bridge. |
| `lambda/js/js_mir_module_batch_lowering.cpp` | ESM format compile/evaluate callbacks, dependency loads through registry, definition-backed parallel schedule. |
| `lambda/js/js_mir_statement_lowering.cpp` and expression lowering | Route static/dynamic/require sites through the final ABI; no registry semantics in lowering. |
| `lambda/jube/jube_registry.h/.cpp` | Discovery-only specifier provider adapter; map hosted module-graph ABI to unified transactions. |
| `lambda/jube/jube_language.cpp` | Hosted-language loader entry through registry transaction. |
| `lambda/module/npm/` | Existing resolution helper/provider input only; no registry ownership move unless the existing code already owns the relevant Node path rule. |
| `lambda/runtime/sys_func_registry.c` | Remove obsolete module helper imports; preserve effect/error metadata for remaining **D8.4.3** adapters. |
| `utils/` architecture checks | Add/extend module-registry structural ratchet. |
| `test/js`, `test/node`, `test/lambda`, `test/jube` | Identity, cycles, failures, capacity, async, cross-language, reset, and packaging fixtures. |

JR10 remains responsible for the broad `js_runtime.cpp` split. Tune8 may add
one cohesive module-loader/registry implementation file if required by
layering, but it must not perform unrelated decomposition or duplicate a
`static` helper; promote shared helpers to the owning header per project rule
13.

---

## 9. Documentation updates

| Document | Required update after implementation |
|---|---|
| `doc/Lambda_Formal_Design.md` | No change expected; update only if T0 triggers §3.4's formal-ruling process. |
| `doc/dev/js/JS_09_Async_Modules.md` | Replace fixed `JsModule[]`, dual publication, require/dynamic direct loader, CJS arrays, and TLA index graph with the unified definition/instance lifecycle. |
| `doc/dev/js/diagram/d09_module_load.mmd/.svg` | Show resolve → definition → context instance → transaction → projection. |
| `doc/dev/js/JS_14_Node_Compat.md` and module-resolution diagram | Show Node resolver as plug-in and Jube catalog as provider discovery, not a namespace cache. |
| `doc/dev/js/JS_01_Compilation_Pipeline.md` | Document definition-owned versus realm-bound module artifact ownership and parallel schedule. |
| `doc/dev/js/JS_00_Overview.md` | Remove fixed module-cap and private-loader debt that JR9 retires. |
| `vibe/jube/JS_Runtime_Redesign.md` | Mark JR9/R8 status and exact deletion/mechanism evidence. |
| `vibe/Lambda_Design_JS_Cache.md` | Resolve module Phase 6 ownership/key integration; preserve de-pointering gate. |
| `vibe/Lambda_Design_Jube_Node_Hosting.md` | Supersede JN6/JN11 statements that make `js_modules[]` authoritative; clarify provider/session versus instance ownership. |
| `vibe/Lambda_Design_MIR_Cache.md` and L1 implementation record | Record definition-index reuse without changing retained Lambda artifact semantics. |
| `doc/Lambda_Jube_Runtime.md` | Describe one loader/registry across source languages and Jube namespaces. |

Every updated design/implementation document cites the governing formal IDs
listed at the top before using JR9/JM ledger ids, per project rule 17.

---

## 10. Validation matrix

### 10.1 Structural checks

The final checker and explicit census must prove:

```bash
rg -n "JS_MAX_MODULES|JS_MAX_ASYNC_PARENTS|JS_TLA_MAX_CONTINUATIONS|JS_TLA_READY_QUEUE_MAX|JS_CJS_MODULE_MAX|JS_CJS_STACK_MAX|JS_P5_DYNAMIC_IMPORT_SLOTS" lambda test
rg -n "js_modules|js_module_count_v14|js_cjs_module_names|js_cjs_module_objects|g_p5_slot_namespace|g_p5_thunk_fns" lambda test
rg -n "runtime_script_index|script_index" lambda/runtime
rg -n "module_registry_cleanup" lambda
rg -n "read_text_file|transpile_js_module_to_mir" lambda/js/js_mir_entrypoints_require.cpp
```

Expected results are zero for retired symbols; allowed direct compile/source
calls are enumerated by the architecture checker, not ignored broadly.

### 10.2 Focused behavior

- Static ESM: default/named/namespace/side-effect imports; diamond graph;
  self/circular imports; same namespace identity; evaluation error caching.
- Dynamic import: canonical alias identity, Promise ordering, nested imports,
  TLA fulfilled/rejected dependency, repeated import, missing module rejection.
- CJS: repeated require identity, `exports` mutation, `module.exports`
  replacement, circular partial exports, parent/children, JSON, directory
  index, package main, relative importer base, throw retry.
- Resolution: `node:` aliases, supported `.js` builtin aliases, extension
  fallback, lexical normalization, realpath/symlink canonicalization, distinct
  goal/format keys, missing/permission failure.
- Scale: >64 modules, >16 async parents of one dependency, >128 ready modules,
  >256 CJS modules, and nested depth beyond old stack capacity without drops.
- Cross-language: Lambda→JS, JS→Lambda, hosted guest→Lambda, Lambda→hosted
  guest, circular hosted graph where supported, boxed function membrane.
- Jube: static/dynamic provider parity, lazy activation once per session,
  absent provider, corrupt manifest, init failure rollback, alias identity,
  runtime reset/detach.
- Cache: definition hit, instance hit, one-byte change invalidation, dependent
  retirement, live-instance pin, cache disabled parity, realm-bound JS artifact.

Every new `*.ls` fixture has its required `*.txt` expected result (project rule
8); JS/Node fixture outputs likewise use checked expected files.

### 10.3 GC, reset, and recovery

Run focused graphs with:

```bash
LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1 ./lambda.exe js <fixture> --no-log
```

Cover:

- collection during placeholder creation, dependency append, namespace build,
  CJS exports replacement, TLA parent registration, and dynamic-import binding;
- full batch reset and hot-preamble partial reset;
- two sequential heaps/realms using the same Runtime definitions;
- two independent EvalContexts with the same canonical module key;
- JIT fault/timeout during evaluation followed by a clean next realm;
- Jube detach while instance namespaces and provider roots are still valid;
- Runtime shutdown with retained Lambda/JS artifacts.

Assertions include zero stale roots, zero double destruction, zero old-heap
Items in definitions, and zero callbacks into freed MIR contexts.

### 10.4 Required suite sequence

Use the exact targets available on the implementation branch; the expected
minimum sequence is:

```bash
make build
make lint
make test-lambda-baseline
make test262-baseline
make node-baseline
make test-jube-module-loader-negative
make test-jube-language-dispatch
make test-jube-node-core-leaves
make test-jube-node-core-dynamic
make test-jube-node-zlib-dynamic
make test-radiant-baseline
```

Run targeted test executables/fixtures before the broad suites. If a listed
target is renamed, record the actual equivalent rather than silently skipping
it. Test262/module or Node exclusions must be listed explicitly in the final
evidence appendix.

---

## 11. Performance and size acceptance

### 11.1 Measurement protocol

1. Build with `make release`.
2. Use the same release binary for cache-policy A/B where possible.
3. Interleave A/B runs; report median and spread, not one sample.
4. Ensure no concurrent build/test contaminates measurements.
5. Measure cold first load and warm instance hit separately.
6. Use profiling only for attribution; performance acceptance uses the ordinary
   release build.

### 11.2 Required workloads

- no-import JS entry (regression floor);
- repeated Jube builtin `require`;
- repeated file CJS require;
- static ESM diamond with cached shared dependency;
- dynamic import of already evaluated and cold modules;
- 100+ module graph (hash/growth behavior);
- Lambda↔JS mixed graph;
- Node baseline aggregate;
- Test262 module/async slice;
- Lambda L1 cache benchmark to prove definition-index migration preserved its
  existing win.

### 11.3 Counters

At minimum:

```text
resolve_requests
resolve_hits / resolve_failures
definition_hits / definition_misses / definitions_retired
instance_hits / instances_created / instances_committed / instances_failed
cycle_joins
namespace_builds
module_compiles / artifact_hits / realm_bound_compiles
module_evaluations
async_parent_edges_peak / tla_ready_peak / active_depth_peak
live_instances / peak_live_instances
retained_artifact_bytes
resolve_us / compile_us / instantiate_us / evaluate_us / reset_us
```

Counters are diagnostic-only under **D5.4.4**.

### 11.4 Acceptance

- No unexplained regression beyond run-to-run noise on no-import or single
  module execution.
- Repeated evaluated loads perform one canonical lookup and result projection;
  no file read, provider build, compile, or evaluation repeats.
- Large graphs scale approximately linearly in definitions/edges rather than
  through repeated linear `js_modules[]` scans.
- Lambda L1 retained-cache performance remains within noise or improves.
- Production C/C++ is net-negative across the JR9 phase. New registry/resolver
  code must be outweighed by deletion of duplicate maps, arrays, loader
  orchestration, root/reset code, and thunk banks.

Correctness wins from removing caps are mandatory even if wall time is neutral.
An unexplained slowdown blocks completion; do not retain a second fast path.

---

## 12. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Canonicalization accidentally merges ESM and CJS views | Include format/goal-derived policy in `ModuleKey`; explicit matrix tests. |
| One global registry leaks Items across heaps | Enforce JM1 split; definition Item-field compile-time assertion/census; two-context poison tests. |
| Circular import placeholder becomes successful after failure | One state machine and atomic fail; cached-failure/cycle fixtures under **S7.11.3**. |
| CJS retry policy diverges from Node | Lock T0 behavior, implement JM5 explicitly, run Node fixtures/baseline. |
| TLA migration changes ordering | Preserve AEO and current checkpoint algorithm; compare detailed transition traces before/after. |
| Stable instance pointers dangle after hashmap growth | Individually allocate instances; maps contain pointers, edges never point at hashmap storage. |
| Jube catalog becomes an accidental namespace cache | Provider descriptors only; architecture checker forbids Item fields/instance ownership in catalog entries. |
| Retained JS MIR references an old realm | Default to `REALM_BOUND`; reusable classification requires JR8/pointer audit and **D8.5.3** evidence. |
| Parallel compiler races context state | Workers touch immutable definitions/compile pools only; instance registry remains owner-thread. |
| Lambda L1 invalidation regresses | Promote existing implementation, do not rewrite policy from memory; A/B and invalidation tests at T2/T6. |
| Reset destroys definitions or leaves instances alive | Separate reset/destroy APIs and counters; JM13 order tested on every recovery path. |
| Compatibility wrappers become permanent second semantics | Wrappers own no state and have delete phases/source ratchets. |
| Scope expands into full ESM/Node resolver conformance | Preserve §3.3 boundaries; record separate follow-ups for live bindings, package exports, and true TLA. |

---

## 13. Suggested commit sequence

Keep commits reviewable and tree-green; do not mix broad JR10 moves into them.

1. `test(js): lock module identity cycle failure and reset behavior`
2. `refactor(runtime): split module definitions from realm instances`
3. `refactor(runtime): add canonical module resolver and transactions`
4. `refactor(lambda): use module definitions for retained script lookup`
5. `refactor(js): move esm module state into unified instances`
6. `refactor(js): replace fixed tla graph and dynamic-import thunk bank`
7. `refactor(js): route require and json through unified loader`
8. `refactor(js): fold commonjs metadata into module instances`
9. `refactor(jube): publish hosted and node namespaces through registry transactions`
10. `refactor(js): attach parallel module artifacts to definitions`
11. `refactor(runtime): delete legacy module registries and reset paths`
12. `test(js): add forced-gc scale and packaging module gates`
13. `docs(js): record unified module registry evidence and ownership`

Each bug fix includes a brief root-cause/invariant comment at the fix point
(project rule 12). Each commit that creates a third similar growable
vector/deque/property path must extract/reuse the shared helper first (rule 13).

---

## 14. Completion checklist

### Authority and ownership

- [ ] Formal rulings and JR9/JM decisions are cited in changed docs.
- [ ] Runtime definitions contain no heap Items.
- [ ] Per-context instances own every namespace/evaluation/CJS/TLA Item.
- [ ] Jube catalog is immutable provider metadata only.
- [ ] One canonical key and transaction state machine serve all formats.

### Identity and behavior

- [ ] Raw request spelling is never used as a post-resolution cache key.
- [ ] Alias/symlink/extension identity tests pass.
- [ ] Format-distinct ESM/CJS cases remain distinct by design.
- [ ] ESM/CJS cycles expose one stable placeholder/value identity.
- [ ] Failure cannot publish a successful partial namespace.
- [ ] CJS exports replacement and retry policy pass.
- [ ] Dynamic import Promise/TLA ordering passes.

### Capacity and GC

- [ ] All fixed module/CJS/TLA graph caps and drop paths are absent.
- [ ] Async/dependency edges use stable instance references.
- [ ] P5 thunk bank/namespace slots are absent.
- [ ] Instance Item roots are exact and released before heap teardown.
- [ ] >old-capacity and forced-GC poison tests pass.
- [ ] Two contexts never share namespace/module/CJS Items.

### Caches and lifecycle

- [ ] `Runtime::script_index` is replaced by definition lookup.
- [ ] Lambda L1 hits/invalidation remain correct.
- [ ] JS artifacts are explicitly reusable or realm-bound—never ambiguous.
- [ ] Node compile-cache policy attaches to registry entries without a new map.
- [ ] Batch reset clears instances, not Runtime definitions.
- [ ] Runtime cleanup destroys every retained artifact exactly once.

### Deletion and validation

- [ ] Structural architecture checker has zero predecessor exceptions.
- [ ] Old global registry, `JsModule[]`, CJS arrays, linear scans, roots, and
      reset paths are deleted.
- [ ] Production C/C++ delta is net-negative.
- [ ] Focused, Lambda, Test262, Node, Jube, GC, and Radiant gates pass.
- [ ] Release A/B and exact pass-count evidence are recorded.
- [ ] Runtime/module docs and diagrams describe the implemented system.
- [ ] JR9 is marked complete only after all deletion gates pass.

---

## 15. Implementation evidence appendix

Fill this section during T0–T8; placeholders are deliberate and block a
premature “done” status.

### T0 baseline

- Commit/worktree anchor: **TBD**
- Mechanism/symbol census: **TBD**
- Focused test results: **TBD**
- Full-suite pass counts: **TBD**
- Release timings and spread: **TBD**
- Production LOC: **TBD**

### Final T8 evidence

- Commit: **TBD**
- Definition/instance/provider mechanism count: **TBD**
- Removed symbols/caps: **TBD**
- Focused and forced-GC results: **TBD**
- Full-suite pass counts/exclusions: **TBD**
- Release A/B: **TBD**
- Cache hit/invalidation metrics: **TBD**
- Root/destruction balance: **TBD**
- Production LOC delta: **TBD**
- Residual risks/follow-ups: **TBD**
