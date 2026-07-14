# Lambda JavaScript MIR Cache Design

**Status:** Implementing; Phases 1 and 2 complete for Radiant layout batches
**Date:** 2026-07-14
**Scope:** In-process reuse of compiled JavaScript MIR across script executions, with fresh runtime and DOM state per execution
**Companion:** `vibe/Lambda_Design_MIR_Cache.md`

---

## 1. Decision summary

Extend Lambda's Level-1 in-process MIR caching model to JavaScript through a
JS-specific compiled-artifact cache.

The cache retains immutable compilation products:

- generated MIR and machine code;
- the owning `MIR_context_t`;
- transpiler AST and name pools referenced by generated code;
- retained source text and declaration metadata;
- a description of mutable cells that must be initialized for each execution.

It does **not** retain a document's JavaScript heap, module-variable values,
DOM wrappers, timers, event listeners, exceptions, or host state. A cache hit
instantiates the cached code into a fresh execution realm and then executes the
entry function normally.

This is deliberately an **in-process Level-1 cache only**. JavaScript code
generation embeds process-local pointers and mutable inline/shape-cache cells.
Cross-process or disk-backed JS code caching remains out of scope until the JS
code generator is de-pointered, as required by
`vibe/Lambda_Design_MIR_Cache.md` MC8.

The first consumers, in priority order, are:

1. the immutable Radiant browser preamble;
2. Radiant's fixed document-lifecycle snippets;
3. repeated external scripts;
4. repeated inline scripts, only after measured admission;
5. JavaScript modules and broader `js-test-batch` reuse after their lifetime
   audit is complete.

### 1.1 Implementation checkpoint — 2026-07-14

The first safe slice is implemented:

- `JsMirCache` is a batch-owned, in-process cache keyed by source bytes,
  execution mode, diagnostic filename, and preamble declaration ABI;
- cache entries own the retained source, MIR context, transpiler pools, entry
  function, and immutable declaration metadata through `JsPreambleState`;
- the browser preamble compiles once per layout worker and is instantiated into
  a fresh heap and DOM realm for every scripted document;
- the four fixed lifecycle units compile once per layout worker against the
  immutable browser-preamble ABI, then execute in the current document realm;
- per-document timing JSON records lookups, hits, misses, cold compiles, and
  cached instantiations;
- `LAMBDA_DISABLE_JS_MIR_CACHE=1` selects the identical uncached batch path for
  differential correctness and performance measurements.

Normal single-file layout execution remains uncached. External scripts, inline
scripts, modules, browser-global synchronization, crash poisoning, and
cross-process persistence are not enabled by this checkpoint.

Release measurements on the 381 runnable form tests were:

| Configuration | Cache on | Cache off | Reduction |
|---|---:|---:|---:|
| Normal runner, 7 workers | 3.18 s | 4.91 s | 35.2% |
| One worker, one 381-file batch | 9.67 s | 15.00 s | 35.5% |

The form correctness gate reported all 378 required baselines passing. A
cached-versus-uncached two-document view-tree comparison differed only in the
generated timestamp field. On the first scripted document the worker recorded
five misses and five cold compiles; the next document recorded five hits and
zero cold compiles, while still creating five fresh execution instances.

---

## 2. Motivation and measured opportunity

The Radiant form-suite profile covered 386 documents. Of these, 371 contained
executable scripts. Aggregate document-script time was approximately 12.02 s:

| Component | Aggregate time | Share of script time | Notes |
|---|---:|---:|---|
| User-script phase | 6.72 s | 55.9% | Includes compilation, execution, and per-task global synchronization |
| Fixed lifecycle snippets | 3.70 s | 30.8% | Four repeated snippets per scripted document |
| Browser preamble | 1.23 s | 10.3% | Compilation can be shared; execution and fresh-realm setup remain per document |
| Runtime cleanup | 0.35 s | 2.9% | Required for heap and DOM isolation |
| Script collection | 0.005 s | <0.1% | Not a meaningful optimization target |

The lifecycle total is the sum of the measured interactive `readystatechange`,
`DOMContentLoaded`, complete `readystatechange`, and `window.load` phases.
Body `onload`, async-ready scheduling, load blockers, and event-loop draining
were negligible in this run.

These are aggregate worker times, not suite wall time. Parallel layout workers
mean an aggregate reduction does not translate one-for-one into elapsed suite
time. The acceptance gate must therefore report both aggregate compilation
time and end-to-end wall time.

The key observation is not merely that JavaScript runs. The same engine-owned
JavaScript sources are parsed, lowered, linked, and generated repeatedly:

- one browser preamble per scripted document;
- four fixed lifecycle snippets per scripted document;
- one browser-global synchronization snippet after each classic script task;
- common external WPT support scripts across many documents;
- potentially identical inline setup scripts in generated or related tests.

Caching must target compilation of repeated sources. It cannot remove the
required execution of user scripts or lifecycle events.

---

## 3. Relationship to the general MIR cache

`Lambda_Design_MIR_Cache.md` establishes three levels:

1. persistent in-process compiled-module reuse;
2. lazy MIR code generation and optimization-level policy;
3. a long-term cross-process native code-image cache.

JavaScript participates in Level 1 only. The general policy is reusable, but
the Lambda `Script` cache entry is not the correct JavaScript representation.

| Concern | Lambda module cache | JavaScript cache |
|---|---|---|
| Primary identity | Canonical import path and file identity | Source identity, execution mode, preamble ABI, and resolution base |
| Retained code owner | `Script::jit_context` | `JsMirCacheEntry::mir_ctx` |
| Per-run state | Module globals recomputed | Fresh JS realm, module vars, globals, DOM bindings, and mutable cache cells |
| Dependency identity | Stable module registry index | Preamble declaration ABI plus module-resolution identity |
| Heap policy | Fresh Lambda heap, retained compile-time pools | Fresh JS heap and DOM realm, retained compile-time pools only |
| Cross-process cache | Long-term after Lambda de-pointering | Explicitly out of scope until JS de-pointering |

Common MIR-context ownership and cache accounting should be shared where the
shape is genuinely identical. Language-specific metadata, reset rules, and
cache keys must remain in the owning transpiler rather than being forced into
one union structure.

---

## 4. Existing JavaScript reuse models

### 4.1 js262 hot-preamble model

`lambda.exe js-test-batch` accepts a `harness:<length>` record, compiles the
test262 harness once with `transpile_js_to_mir_preamble_len()`, and executes
many test sources with `transpile_js_to_mir_with_preamble_len()`.

The harness heap remains alive. `js_batch_reset_to(checkpoint)` preserves the
harness module variables while clearing test-owned module variables, globals,
module registries, exceptions, timers, prototypes, and other mutable runtime
state. Constructor and typed-array prototype snapshots restore mutations while
preserving identities captured by the harness.

This is valid for js262 because the worker deliberately owns one hot heap and
uses an extensive reset contract between tests.

### 4.2 Radiant fresh-realm model

A layout document has stronger isolation requirements:

- `window` and `document` must belong to the current `DomDocument`;
- DOM wrapper caches must never retain nodes from the previous document;
- timers, microtasks, listeners, exceptions, module state, and globals must be
  empty at document start;
- one layout test must not observe mutations made by another test;
- a destroyed layout pool must not remain reachable from generated code or
  process-global JS state.

Radiant therefore cannot copy js262's hot-heap policy directly. It should copy
the useful part—retained compiled code—and instantiate that code into a new
heap and DOM realm for each document.

The browser-preamble prototype on the current branch already demonstrates this
split:

1. compile the preamble in compile-only mode;
2. retain its MIR context, transpiler pools, source, entry function, and
   declaration metadata;
3. discard the compile-only heap;
4. clone the immutable metadata;
5. create a fresh heap, nursery, name pool, type list, module-variable table,
   event loop, and DOM binding;
6. invoke the cached preamble entry function in that fresh realm.

That mechanism is the starting point for the general JS cache, not a separate
Radiant-only implementation.

---

## 5. Goals and non-goals

### 5.1 Goals

- Compile an identical safe JavaScript unit at most once per worker process.
- Preserve fresh heap, global, DOM, timer, listener, and exception state for
  every layout document.
- Reuse the js262 preamble APIs and reset knowledge instead of creating another
  unrelated compilation path.
- Make cache eligibility explicit and auditable.
- Provide enough counters to prove hit rate, saved compilation work, retained
  memory, and suite-wall improvement.
- Keep normal single-file layout behavior unchanged; caching is an execution
  optimization, not a test semantic.
- Keep the cache local to a worker process and destroy it deterministically at
  batch shutdown.

### 5.2 Non-goals

- Disk or cross-process JavaScript code caching.
- Reusing a prior document's JS heap or DOM globals.
- Skipping required lifecycle events or user-script execution.
- Caching a script solely because its filename matches another script.
- Making unique inline scripts permanent process-lifetime entries.
- Treating cached execution as permission to weaken crash, timeout, or memory
  isolation.
- Replacing JavaScript semantics with layout-test-specific hardcoded outcomes.

---

## 6. Cache model

### 6.1 Two-layer representation

The design separates a process-lifetime compiled template from a per-execution
instance.

```c
struct JsMirCacheEntry {
    uint64_t source_hash;
    uint64_t preamble_abi_hash;
    size_t source_len;
    uint32_t compile_flags;
    uint32_t execution_mode;

    void* mir_ctx;
    void* entry_func;
    void* transpiler_ast_pool;
    void* transpiler_name_pool;
    char* retained_source;

    JsModuleConstEntry* declarations;
    int declaration_count;
    int module_var_count;

    ArrayList* mutable_cell_descriptors;
    size_t retained_bytes;
    bool poisoned;
};

struct JsMirExecutionInstance {
    JsMirCacheEntry* compiled;
    Heap* heap;
    Nursery* nursery;
    NamePool* name_pool;
    ArrayList* type_list;
    Item* module_vars;
};
```

The exact structure should be factored around existing `JsPreambleState` rather
than duplicating it. `JsPreambleState` can either become the common compiled
unit representation or embed a promoted `JsCompiledArtifact` containing the
shared ownership fields.

The cache owns every address embedded in generated code. The execution instance
owns every value that may vary by realm.

### 6.2 Cache key

The key must include every input that can change generated code or resolution:

```text
hash(
    source bytes,
    classic | module | preamble | lifecycle mode,
    strictness and parser flags,
    preamble ABI signature,
    canonical filename or source identity,
    canonical module-resolution base,
    MIR optimization level,
    eager | lazy | interpreter policy,
    runtime/compiler cache epoch
)
```

The source hash is verified with source length and, in debug/canary mode, a byte
comparison. A hash collision must not execute the wrong source.

The canonical filename is part of the v1 key because it affects diagnostics,
relative imports, source identity, and potentially observable stack traces.
Later profiling may prove that selected engine-owned classic snippets are
filename-independent.

### 6.3 Preamble ABI signature

Scripts compiled with a preamble inherit names and module-variable indices.
Their generated code is valid only for a compatible declaration layout.

The preamble ABI hash must cover the ordered declaration metadata needed by the
transpiler:

- binding name;
- binding kind;
- module-variable index;
- declaration/function classification;
- any flags that alter lowering or call shape.

It must not hash heap values or raw addresses. Two fresh realms instantiated
from the same browser-preamble template have the same ABI even though their
`window`, `document`, constructor, and function objects differ.

For v1, hash the complete preamble declaration table. A later dependency scan
may permit a script to key only the declarations it references, but that is an
optimization and not required for correctness.

### 6.4 Entry states

An entry moves through explicit states:

```text
observed -> compiling -> ready -> instantiated -> ready
                        |                    |
                        +-> rejected         +-> poisoned
```

- **Observed:** source identity recorded, but compiled code is not retained.
- **Compiling:** one thread owns compilation; duplicate lookups wait or miss
  according to worker policy.
- **Ready:** immutable artifact may be instantiated.
- **Rejected:** the unit contains an unsupported mutable-pointer category.
- **Poisoned:** a crash, timeout, integrity failure, or failed reset makes the
  artifact ineligible for reuse.

A poisoned entry is destroyed when no execution instance references it.

---

## 7. Fresh-realm instantiation contract

A cache hit is correct only if it is observationally equivalent to compiling
and executing the same source in a new document realm.

### 7.1 Required initialization

Each instance must create or reset:

1. `Heap`, nursery, allocation pool, and GC roots;
2. `NamePool` and type-list state owned by the execution realm;
3. module-variable storage sized from the compiled declaration metadata;
4. active `EvalContext`, `_lambda_rt`, and `js_source_runtime`;
5. `Input` and current-file state;
6. JS event loop, timers, microtask queue, and process listeners;
7. the current `DomDocument` binding and DOM wrapper caches;
8. global objects, constructors, prototypes, namespaces, and module registries;
9. mutable inline, shape, regex, and template cache cells;
10. exception, strict-mode, async, generator, promise, and transient-call state.

The compiled entry function is then invoked to materialize per-realm function
objects, literals, globals, and browser shims.

### 7.2 Required teardown

Document teardown must:

- shut down or abandon timers before freeing their callbacks;
- clear DOM and global wrapper caches before destroying the layout pool;
- remove module variables and GC roots that point into the document heap;
- destroy heap-owned JS objects and finalizers;
- free the execution nursery, type list, name pool, and pool;
- leave only the immutable cache-owned MIR context and compile-time pools.

The existing `script_runner_cleanup_js_state()`, `js_batch_reset()`, event-loop
shutdown, and document cleanup paths should remain the authoritative reset
helpers. Cache code should call or extend those helpers, not copy their reset
lists.

### 7.3 Mutable compiled cells

Generated JavaScript currently embeds or references mutable cells for property
inline caches, object shapes, constructors, regexes, templates, and other
runtime shortcuts. Every cell must be classified as one of:

| Class | Lifetime | Required action |
|---|---|---|
| Immutable compile metadata | Cache entry | Retain with the owning MIR context and pools |
| Process-stable runtime address | Process | Verify it never points into a document heap |
| Per-realm mutable cell | Execution instance | Allocate or reset on every instantiation |
| Unsupported heap pointer | None | Reject caching until de-pointered or relocated |

No general user script becomes cache-eligible until this audit is complete for
all pointer categories it emits. Engine-owned lifecycle snippets may be enabled
earlier because their emitted instruction and literal surface is small and can
be exhaustively inspected.

---

## 8. Cache ownership, admission, and eviction

### 8.1 Ownership

One cache belongs to one JS worker process. Radiant layout workers and
`js-test-batch` processes do not share memory or artifacts with each other.

The cache owns:

- its `HashMap` index;
- retained keys and canonical identities;
- compiled entries and their MIR contexts;
- compile-time pools, source buffers, and declaration metadata;
- counters and retained-byte accounting.

An active document owns only its execution instance and a non-owning reference
to the compiled entry.

### 8.2 Admission policy

Admission is source-class aware:

1. **Engine-owned immutable sources:** admit on first compilation. This includes
   the browser preamble and fixed lifecycle snippets.
2. **External files:** admit on first successful compilation when canonical
   path and file identity are available. Repetition across documents is likely,
   and invalidation is well-defined.
3. **Inline sources:** record source identity first. Retain compiled code only
   after measured repetition or when the batch can prove the source appears
   more than once.
4. **Dynamic/eval sources:** disabled initially. Their dependency on current
   lexical and eval-preamble state requires a separate eligibility proof.

This avoids retaining thousands of one-off inline scripts merely to discover
that they never hit.

### 8.3 Eviction

V1 may use no eviction for engine-owned and known-repeated entries within a
bounded layout batch. General inline and module caching requires retained-byte
accounting and an eviction policy.

An entry is evictable only when:

- no document instance references it;
- no retained compiled unit imports or otherwise depends on its code address;
- no DOM/event callback still points to one of its functions;
- it is not the active browser-preamble ABI owner.

Classic standalone script entries can normally be destroyed after their
document ends. Module dependency graphs may require dependency-cone eviction,
matching the constraint in the general MIR-cache design.

Memory limits must be derived from cache byte accounting and worker memory
policy, not a layout-suite-specific filename list or fixed number of tests.

---

## 9. Source-specific integration plan

### 9.1 Browser preamble

The current prototype uses `compile_js_mir_preamble_len()` once and
`instantiate_js_preamble()` for each scripted document. It should be migrated
behind the general cache API and remain the reference implementation for fresh
realm instantiation.

The cache removes repeated parse, AST, lowering, link, and MIR generation. It
does not remove per-document preamble execution because `window`, `document`,
constructors, browser globals, and shims must be created in the new heap.

### 9.2 Lifecycle snippets

Radiant currently compiles four fixed snippets per scripted document:

- interactive `readystatechange`;
- `DOMContentLoaded`;
- complete `readystatechange`;
- `window.load` plus `window.onload`.

Compile these once against the immutable browser-preamble ABI. Their execution
must use the current document's module variables and DOM binding.

They must not be keyed against the complete evolving declaration snapshot after
user scripts. The lifecycle sources reference only browser-preamble bindings.
Use the immutable base-preamble ABI explicitly; otherwise user declarations
would create a different cache key for every document and eliminate reuse.

This is the first general-cache acceptance slice because it has a fixed source,
a narrow dependency surface, and a measured 3.70 s aggregate upper bound.

### 9.3 Browser-global synchronization

After every classic script task, Radiant currently compiles and executes a
snippet that synchronizes `jQuery` and `$` between global bindings and
`window` properties.

Repeated compilation should stop, but caching is not the ideal final fix. The
root cause is that global bindings and corresponding `window` properties are
not represented by one coherent global-environment record. The preferred
sequence is:

1. profile the synchronization snippet separately;
2. fix global/window binding coherence in the JS runtime;
3. remove the synchronization snippet when semantics permit;
4. until then, use a cached compiled unit keyed by the declaration ABI it
   actually references.

This preserves browser semantics without hardcoding jQuery-specific test
behavior as the permanent architecture.

### 9.4 External scripts

External classic scripts use a canonical resolved path plus file identity:

```text
(canonical path, mtime, size, source hash, compile policy, preamble ABI)
```

The existing source-byte cache and the MIR cache are separate layers:

- source cache avoids repeated reads;
- MIR cache avoids repeated parsing, lowering, linking, and generation.

Source bytes must remain available for collision verification, diagnostics,
and any generated code that retains source ranges. The source cache must not be
destroyed after every document when batch-level reuse is enabled.

### 9.5 Inline scripts

Inline script filenames such as `<inline-script-2>` are document-local labels,
not sufficient cache identities. Key on source bytes, execution mode, base URL,
and preamble ABI.

Before enabling compiled retention, add source-frequency instrumentation:

- total inline units;
- unique content hashes;
- hashes seen at least twice;
- executions covered by repeated hashes;
- aggregate compile time attributable to repeated hashes;
- retained bytes required for those entries.

Only then choose an admission threshold. Unique inline scripts continue through
the ordinary compilation path.

### 9.6 JavaScript modules

Module caching adds dependency and resolution constraints:

- canonical module URL and import attributes;
- transitive dependency identities;
- module namespace and live-binding layout;
- per-realm namespace objects;
- module registry reset behavior;
- dynamic import and CommonJS interop state.

The JS runtime currently clears module registry and module cache state in
`js_batch_reset()` and `js_batch_reset_to()` because their objects are
heap-backed. The compiled module artifact can be retained only after those
heap-backed values are separated from immutable code ownership.

V1 should therefore land classic engine-owned sources first. Module support is
a later phase sharing the same cache index and instrumentation.

### 9.7 js-test-batch

The js262 harness already gets most of the high-value preamble reuse. The new
cache should not replace its hot-heap checkpoint model immediately.

It can first provide:

- shared cache accounting and entry ownership;
- retained compiled special preambles across compatible manifest boundaries;
- repeated external/helper module compilation;
- a fresh-realm mode for tests that cannot safely share the hot heap.

The existing zero-crash and zero-lost-test policy remains the acceptance bar.

---

## 10. Proposed API surface

The names are illustrative; implementation should promote existing preamble
helpers rather than add parallel copies.

```c
struct JsMirCache;
struct JsMirCacheKey;
struct JsMirCacheEntry;
struct JsMirExecutionInstance;

JsMirCache* js_mir_cache_create(void);
void js_mir_cache_destroy(JsMirCache* cache);

JsMirCacheEntry* js_mir_cache_lookup(
    JsMirCache* cache, const JsMirCacheKey* key,
    const char* source, size_t source_len);

JsMirCacheEntry* js_mir_cache_compile(
    JsMirCache* cache, Runtime* runtime,
    const JsMirCacheKey* key,
    const char* source, size_t source_len,
    const JsPreambleState* preamble);

bool js_mir_cache_instantiate(
    JsMirCacheEntry* entry, Runtime* runtime,
    DomDocument* document,
    JsMirExecutionInstance* instance);

Item js_mir_cache_execute(
    JsMirExecutionInstance* instance,
    const JsPreambleState* active_preamble);

void js_mir_cache_release_instance(JsMirExecutionInstance* instance);
void js_mir_cache_poison(JsMirCacheEntry* entry, const char* reason);
```

The API must make ownership explicit:

- lookup returns a non-owning entry reference;
- cache destruction owns `MIR_finish()` for retained contexts;
- instance release cannot destroy cache-owned pools or code;
- normal uncached execution retains its existing ownership behavior.

Avoid a global singleton in the final design. A cache pointer should be owned by
the layout batch or JS batch runtime so concurrent worker processes and future
embedded runtimes have independent lifetimes.

---

## 11. Crash and timeout handling

Signal recovery can bypass normal destructors and leave JIT/runtime state
partially updated. Cache reuse must fail closed.

On a crash, timeout, MIR error, or failed integrity check:

1. mark the active cache entry poisoned;
2. abandon timers and callbacks that may reference the failed instance;
3. clean the active and deferred MIR execution contexts as required, without
   destroying unrelated cache-owned contexts;
4. destroy the document execution heap and DOM bindings;
5. evict the poisoned entry when no references remain;
6. allow one ordinary uncached recompilation only when existing retry policy
   permits it;
7. report the poison and retry in cache counters.

If a crash occurs while executing immutable engine-owned cached code, retain
the source identity in the diagnostic. Do not repeatedly execute the same
poisoned artifact in later documents.

---

## 12. Instrumentation

Add process-level counters:

```text
lookups
hits
misses
compiles
instantiations
rejected_unsafe
poisoned
evictions
collision_checks
retained_entries
retained_mir_bytes
retained_pool_bytes
peak_retained_bytes
compile_us
instantiate_us
execute_us
reset_us
```

Add source-class breakdowns for:

- browser preamble;
- lifecycle;
- global synchronization;
- external classic;
- inline classic;
- module;
- eval/dynamic function.

The layout timing JSON should distinguish:

- cache lookup;
- cold compile;
- hit instantiation;
- actual JS execution;
- mutable-cell reset;
- realm teardown.

This is necessary because a lower `preamble` or `user_scripts` wall clock alone
does not prove whether compilation, execution, or cleanup changed.

---

## 13. Implementation sequence

### Phase 0 — Complete attribution and pointer audit

1. Preserve the detailed document-script timing already added to the layout
   timing JSON.
2. Add separate timing for browser-global synchronization.
3. Record source hashes and repeated-source coverage without changing execution.
4. Inventory all raw-pointer and mutable-cell categories emitted by
   `lambda/js/js_mir_*`.
5. Classify the browser preamble and lifecycle snippets against that inventory.

**Exit gate:** every retained address category has an owner and lifetime; the
form-suite timing reconciles cache lookup, compilation, execution, and cleanup.

### Phase 1 — Promote the browser-preamble prototype (implemented)

1. Extract the common compiled-artifact ownership from `JsPreambleState`.
2. Move cache lifetime from file-static Radiant state into a batch-owned
   `JsMirCache`.
3. Preserve fresh heap and DOM instantiation.
4. Add cache hit/miss and retained-byte counters.

**Exit gate:** cached and uncached preamble modes produce identical layout JSON;
cross-document mutation tests pass; no retained document heap after cleanup.

### Phase 2 — Cache fixed lifecycle units (implemented)

1. Compile the four lifecycle snippets against the immutable base-preamble ABI.
2. Instantiate them in the active document realm without refreshing or
   replacing user declarations.
3. Verify event order, `readyState`, listener identity, exception behavior, and
   `window.onload` ordering.

**Exit gate:** lifecycle cold compiles fall from four per scripted document to
four per worker process, with unchanged form and Radiant baselines.

### Phase 3 — Fix or cache browser-global synchronization

1. Measure its cold-compile and execution cost separately.
2. Implement coherent global/window binding semantics.
3. Remove the snippet when behavior is equivalent; otherwise route it through
   the cache temporarily.

**Exit gate:** `$`/`jQuery` compatibility tests pass without recompiling the
sync source after every script.

### Phase 4 — Repeated external classic scripts

1. Promote the source-byte cache to batch lifetime.
2. Add canonical path/file-identity keys.
3. Instantiate mutable cells per document.
4. Add safe entry poisoning and teardown.

**Exit gate:** a repeated external source compiles once per worker, file changes
produce misses, and fresh-realm tests remain isolated.

### Phase 5 — Measured inline-script admission

1. Review source-frequency and retained-memory data.
2. Enable caching only for repeated eligible hashes.
3. Add memory-accounted eviction for independent classic entries.

**Exit gate:** measurable wall-time improvement with bounded peak memory; no
performance regression when most sources are unique.

### Phase 6 — Modules and broader JS batch reuse

1. Separate compiled module ownership from heap-backed namespace/live-binding
   state.
2. Include dependency and resolution identity in keys.
3. Audit CommonJS, dynamic import, and module registry resets.
4. Evaluate compatible special-preamble reuse across js262 manifests.

**Exit gate:** JS module tests, js262 baseline, Node preliminary tests, and crash
recovery remain green with cache hits proven in diagnostics.

---

## 14. Correctness and regression tests

### 14.1 Cache identity

- identical source, mode, base, and preamble ABI hits;
- one-byte source change misses;
- classic versus module misses;
- different strictness or MIR policy misses;
- different module base misses;
- different preamble declaration layout misses;
- a forced hash collision fails byte verification and misses.

### 14.2 Realm isolation

Document A mutates each of the following; document B must observe defaults:

- `window` properties and top-level globals;
- `Object.prototype`, `Array.prototype`, and typed-array prototypes;
- constructors and namespace objects;
- `document`, DOM wrappers, and event targets;
- timers, microtasks, promises, generators, and listeners;
- exceptions, strict mode, RegExp legacy state, and module registry state;
- inline/shape/regex/template caches.

Also assert that document B's `window`, `document`, constructors, and wrappers
are allocated in B's heap and never equal pointers retained from A's heap.

### 14.3 Lifecycle behavior

- ready-state transition order remains `loading -> interactive -> complete`;
- `readystatechange`, `DOMContentLoaded`, body `onload`, and window `load`
  ordering is unchanged;
- listeners installed by user scripts run in the correct document realm;
- an exception in one lifecycle listener does not poison the next document;
- user-defined globals remain visible to later scripts in the same document.

### 14.4 Cache teardown

- batch shutdown calls `MIR_finish()` exactly once per owning entry;
- clones never free shared MIR contexts or transpiler pools;
- poisoned entries are not reused;
- no callback points to code after its entry is evicted;
- memory high-water stabilizes across repeated batches;
- crash and timeout recovery leave zero lost tests.

---

## 15. Performance verification

All performance measurements use a release build.

### 15.1 Required runs

1. Direct 386-file form batch, cache disabled.
2. Direct 386-file form batch, preamble cache only.
3. Direct 386-file form batch, preamble plus lifecycle cache.
4. Direct 386-file form batch with repeated external cache enabled.
5. `make layout suite=form` correctness gate.
6. `make test-radiant-baseline` end-to-end wall-time and correctness gate.
7. js262 focused preamble/reset tests.
8. `make test-lambda-baseline` to ensure shared MIR ownership changes do not
   regress the Lambda cache.

### 15.2 Reported metrics

For every configuration report:

- end-to-end wall time;
- aggregate document load and script time;
- compile, instantiate, execute, reset, and cleanup time;
- cache hits, misses, and hit rate by source class;
- cold compiles avoided;
- peak RSS and cache-retained bytes;
- success, failure, retry, crash, timeout, and lost-test counts.

The optimization is accepted only when output is identical, failures and
retries do not increase, peak memory remains bounded, and release wall time
improves outside run-to-run noise.

---

## 16. Expected impact

The measured upper bounds are:

- 1.23 s aggregate browser-preamble phase;
- 3.70 s aggregate fixed lifecycle phase;
- 6.72 s aggregate user-script phase.

These are not all removable:

- preamble and lifecycle code still executes per document;
- unique user scripts still compile and execute;
- repeated scripts still require fresh mutable state and actual execution;
- isolation cleanup remains required.

The first target is to remove nearly all repeated **compilation** from the
preamble and fixed lifecycle phases. The second target is the compile portion
of repeated external scripts. Inline-script savings depend entirely on measured
source repetition.

No fixed suite-wall target is decided before cold-compile versus execution time
is fully attributed. The implementation should report the achieved reduction
rather than treating the aggregate upper bounds as promised savings.

---

## 17. Decision ledger

| ID | Decision | Status |
|---|---|---|
| JC1 | Extend Level-1 MIR caching with a JS-specific in-process cache | Implemented for preamble and lifecycle units |
| JC2 | Retain compiled code and compile-time pools, never a prior layout document's heap or DOM realm | Decided invariant |
| JC3 | Use js262 as the preamble-reuse reference, but preserve Radiant's fresh-realm model | Decided invariant |
| JC4 | Promote existing preamble ownership rather than duplicate it in a Radiant-only cache | Implemented |
| JC5 | Cache fixed lifecycle snippets before general user scripts | Implemented |
| JC6 | Fix global/window binding coherence; use cached global-sync only as an interim mechanism | Proposed |
| JC7 | Admit external scripts by canonical identity; admit inline scripts only after measured repetition | Proposed |
| JC8 | Reject unsafe pointer categories until per-realm initialization is implemented | Decided invariant |
| JC9 | Poison cache entries after crashes, timeouts, or integrity failures | Decided invariant |
| JC10 | Keep JS disk/cross-process caching out of scope until JS de-pointering | Inherited from MIR-cache MC8 |

---

## 18. Open questions

- **OQ1:** Which JS MIR literal and cache-cell categories currently point into
  the execution heap rather than a retained compiler-owned pool?
- **OQ2:** Can mutable IC/shape cells be described and reset generically, or
  must lowering place them in an explicit per-realm sidecar first?
- **OQ3 (resolved for fixed lifecycle units):** Form-suite and focused batch
  verification confirm that units compiled against the immutable base-preamble
  ABI execute after user scripts extend the active declaration snapshot.
- **OQ4:** How much of the 6.72 s user-script phase is cold compilation versus
  execution and browser-global synchronization?
- **OQ5:** What proportion of inline script executions are covered by repeated
  source hashes in each Radiant baseline suite?
- **OQ6:** Should compatible test262 special preambles survive manifest
  boundaries, or is process recycling already cheaper and safer?
- **OQ7:** What retained-byte metric can be obtained reliably from MIR contexts
  and transpiler pools for admission and eviction decisions?
- **OQ8:** Can global/window coherence remove `execute_browser_global_sync()`
  entirely without breaking classic-script global declaration semantics?

Until OQ1 and OQ2 are answered, only exhaustively audited engine-owned sources
should be cache-eligible.
