# Lambda Script Cache Proposal

**Status:** Implementation in progress — common source admission, Lambda AST
import/view overlays, Lambda fresh-runtime MIR dependency cones with
logical-to-dense module-slab mapping, source-change retirement, and
single-flight artifact builds, broad JS AST templates with execution
overlays, synchronous static-JS-import module MIR artifacts with
source-generation cones, and the Radiant JS-MIR lease session are landed;
eager-JIT AST-miss reuse and the broad Phase 1–4 exit gates remain open
(2026-09-14)  
**Date:** 2026-09-13 (updated 2026-09-14)  
**Scope:** A single in-process script-cache service for every Lambda, LambdaJS,
and future hosted-language command pipeline. `InputManager` is the persistent
central store across disposable script runtimes. Each cached script has a
manager-owned persistent `ScriptInput` whose AST and optional MIR artifacts
remain linked to that input; this proposal specifies scripts first and reserves
the same identity and ownership model for future input-document artifacts.

**Formal linkage:** D1.7 (source remains the distributed form), D1.8 and
D5.4.3 (no context-dependent value at a code-baked address), D4.2.3 (every
allocator has a named context owner), D7.1.2 (I/O versus derived-artifact
layering), D7.2.2–D7.2.3 (transactional module initialization and in-process
import caching), D8.1.3v10 (retained LambdaJS AST and explicit AST tier),
D8.4.1v2 (immutable generated code), D7.1.2v2 (I/O and opaque-artifact
layering), and D8.5.1v2 (the persistent executable-script cache).
This proposal records the 2026-09-13 policy decisions in §15; formal rulings
are kept synchronized as implementation lands.

**Companions:** `Lambda_Design_MIR_Cache.md`, `Lambda_Design_JS_Cache.md`,
`Lambda_Design_JS_Interpreter.md`, and `doc/Lambda_Formal_Design.md`.

---

## 1. Decision sought

Adopt one general `InputScriptCache` service, reached through `InputManager`,
as the only admission path for executable source in a process. `InputManager`
and its persistent `ScriptInput` records survive when a script runtime,
`Runtime`, `EvalContext`, document, or test item is discarded. Every command
pipeline first acquires a script input by source identity and then asks a
language adapter for an AST artifact and, where that adapter has MIR Direct
output, a MIR artifact.

The cache has two independently useful levels:

1. **AST level.** Retain an immutable, parsed and validated compilation
   template. Interpreter execution rebinds/instantiates that template in the
   current execution context; a MIR miss may lower from the retained AST.
2. **MIR level.** Retain the AST-level artifact plus an optional immutable
   compiled MIR artifact. A hit creates fresh per-execution state and links or
   instantiates the artifact into that state; it never reuses a prior script's
   globals, heap, DOM, module namespace, or exception state.

The design applies to Lambda, JavaScript, and future hosted languages. The
common cache owns persistent script-input identity, source, artifact handles,
lookup, leases, accounting, invalidation, and future memory management. Each
language owns parse, binding, compilation, instantiation, and the operations
for its opaque artifact. This preserves the language boundary while making
source loading and module admission one path.

The proposal is deliberately **in-process only**. It creates no distributed
format and no disk MIR cache: scripts still ship as source under D1.7. Any
cross-process native-code image cache remains the distinct D8.5.2–D8.5.3
project.

---

## 2. Problem in the current tree

Executable source is loaded through several unrelated paths:

| Area | Current cache/reuse | Gap |
|---|---|---|
| Lambda `.ls` | `Runtime::loaded_script_index` in `runner.cpp` | Runtime-local source/import registration, not artifact ownership or a retention cache. Closed interpreter units use common AST templates; reusable code uses logical compilation-unit IDs mapped to this runtime's dense module slabs. |
| JS AST tier | `InputScriptCache` AST template plus `JsScript` execution overlay | Common identity owns immutable parser ASTs; functions, classes, modules, eval, and TypeScript allocate mutable execution facts into a fresh overlay. |
| JS MIR layout | `JsMirLeaseSession` in `js_mir_cache.cpp` | A bounded Radiant lease/accounting session over common artifacts, not a second cache index; covers browser preambles and eligible external classic scripts. |
| JS test batches | Retained harness preamble and hot-heap reset contract | Useful but a separate protocol, not a general source/artifact service. |
| Hosted languages | Jube dispatch chooses a language-specific loader | No common script identity, AST cache, MIR cache, or module-artifact contract. |
| Input documents | `InputManager` tracks only inputs it directly creates | No content-addressed artifact registry; most script and JS compiler inputs use `Input::create()` directly. |

`Script` already extends `Input`, but Lambda `load_script()` allocates it
directly and JavaScript compiler inputs are direct runtime-pool `Input`s.
Consequently neither route is centrally managed by `InputManager`. The current
`InputManager` list is an ownership/cleanup list, not a cache index. The new
persistent `ScriptInput` must be a distinct input role with a cache-owned
pool/context; it must never be an ordinary document `Input` promoted after
execution.

The result is duplicated source reads, parsing, AST construction, MIR lowering,
and cache rules. More importantly, the split hides the essential ownership
boundary: reusable compilation artifacts are immutable templates, while every
execution receives new runtime state.

---

## 3. Goals and non-goals

### 3.1 Goals

- Route every executable source acquisition through one persistent
  `InputManager` cache service, including main scripts, imports, document
  scripts, test harnesses, schemas that execute Lambda, and hosted-language
  requests.
- Admit all successfully compiled scripts by default, including main scripts;
  uniqueness is not a reason to decline caching. Future memory-pressure policy
  may selectively clear entries, but does not change default admission.
- Cache Lambda and JS AST artifacts whenever their parse/bind facts can be
  frozen and rebound safely.
- Cache their MIR artifacts when generated, without requiring MIR for an AST
  cache hit.
- Make module compilation and module source identity use the same service;
  each runtime still instantiates its own module state.
- Keep future input-document cache support possible without making script
  cache entries own a parsed document or an execution `Input`.
- Give every cache entry an explicit source identity, dependency identity,
  cache scope, owner, memory budget, invalidation rule, and metrics.
- Migrate and retire duplicated Lambda L1, JS AST, and Radiant cache-façade
  ownership after equivalent coverage exists in the common service.

### 3.2 Non-goals

- Retaining a previous execution's Lambda heap, JS heap, `window`, DOM,
  timers, listeners, module namespace, exception, or mutable Jube state.
- Serializing AST or MIR artifacts to disk, distributing them, or accepting
  compiled source as an input format (D1.7).
- Treating a filename, URL, or module specifier alone as cache identity.
- Making dynamic code, `eval`, or REPL fragments share a cache entry without a
  complete lexical/environment identity in their key.
- Replacing language-specific parsers, semantic rules, or module loaders with
  one cross-language parser.
- Caching arbitrary parsed documents in the first implementation. The common
  framework is designed for that later work; scripts are the only artifact
  family admitted by this proposal.

---

## 4. Terminology and lifetime model

```text
InputManager (process-persistent central input/cache store)
  └─ InputScriptCache (identity index, scopes, budgets, metrics)
       └─ ScriptInput (one source identity + language/profile identity)
            ├─ SourceBlob                 immutable source bytes
            ├─ AstArtifact                optional immutable parse/bind template
            ├─ MirArtifact                optional immutable compiled template
            ├─ dependency interface image
            └─ compilation_unit_id         stable logical code identity

Execution scope (one command worker/batch lifetime)
  └─ InputScriptLease (pins an entry while it is compiled or instantiated)

Execution instance (one CLI run, test item, document, or module realm)
  ├─ execution-local Input / Runtime / EvalContext
  ├─ per-context module slabs and GC roots
  └─ language-specific AST/MIR instantiation state
```

A persistent `ScriptInput` is an `InputManager`-owned cache input, but never an
execution input. It owns immutable source and the handles to immutable AST/MIR
artifacts. An execution constructs a short-lived `Input` or `ScriptRuntime`
view against that cached source and creates all document-, heap-, and
context-owned data in its normal execution context.

This distinction is mandatory:

- A persistent `ScriptInput` receives its own named cache pool, arena, and
  memory context. It does not reuse a document input's pool, name pool, shape
  pool, type list, URL ownership, or document attribution.
- A cached Lambda artifact may own an AST, constants, type metadata, and a
  `MIR_context_t`; its module values remain in fresh context slabs.
- A cached JS artifact may own parser/AST/compiler metadata and MIR code; JS
  objects, module registries, DOM wrappers, timers, and prototype state remain
  execution-owned.

D1.8, D5.4.3, and D8.4.1v2 require this split. Generated code is immutable;
all context-dependent mutable state must have a named per-instance owner.

---

## 5. Central service and language adapter boundary

`InputManager` becomes the central persistent store through a composition
member such as `InputScriptCache* script_cache`. It owns each persistent
`ScriptInput`, its source, cache identity, artifact handles, and lifetime. It
does not gain includes of `lambda/runtime/` or `lambda/js/`; instead, the
runtime registers opaque language adapters at startup.

Illustrative neutral API:

```c
typedef struct InputCacheScope InputCacheScope;
typedef struct ScriptInput ScriptInput;
typedef struct InputScriptLease InputScriptLease;
typedef struct InputScriptRequest InputScriptRequest;
typedef struct InputScriptArtifactOps InputScriptArtifactOps;

InputCacheScope* input_manager_open_script_scope(
    InputManager* manager, const InputScriptRequest* request);
void input_manager_close_script_scope(InputCacheScope* scope);

InputScriptLease* input_script_cache_acquire(
    InputCacheScope* scope, const InputScriptRequest* request);
void input_script_cache_release(InputScriptLease* lease);

bool input_script_cache_get_ast(InputScriptLease* lease,
    const InputScriptArtifactOps* ops, void** out_ast);
bool input_script_cache_get_mir(InputScriptLease* lease,
    const InputScriptArtifactOps* ops, void** out_mir);
```

The language adapter owns callbacks conceptually equivalent to:

- parse and freeze an AST artifact from `SourceBlob`;
- validate a requested compiler/profile key;
- compile an optional MIR artifact from the frozen AST;
- instantiate AST or MIR into a supplied execution context;
- destroy an artifact, enumerate dependencies, and report retained bytes;
- reject or poison an artifact when a safety invariant fails.

The service owns the persistent `ScriptInput` key/index, single-flight
compilation state, leases, counters, and future memory management. The adapter
provides the artifact representation and its operations, while the
manager-owned `ScriptInput` owns the opaque artifact handle and invokes its
registered destruction operation. This lets a future Python, Bash, Ruby, or
Jube guest participate without exposing its AST or runtime structures to
another language.

### 5.1 Layering reconciliation

D7.1.2 places derived script/MIR artifacts above the I/O layer. The adopted
interpretation is that `InputManager` owns persistent `ScriptInput` records
and opaque artifact handles, while the LambdaJS/Lambda/guest adapter remains
the source of the artifact type and finalizer. `lambda/input/` therefore need
not name `MIR_context_t` or a language AST class. This centralizes lifetime and
lookup without moving language semantics into the I/O layer. The formal design
records this interpretation in **D7.1.2v2**.

---

## 6. Cache identity, compatibility, and invalidation

An entry key has two layers. The AST key names source and parsing; the MIR key
extends it with all lowering/code-generation choices.

```text
SourceIdentity = hash(
  canonical source URI or explicit synthetic identity,
  exact source bytes and length,
  source kind: file | URL | inline | generated | harness,
  language profile and parser grammar/version,
  parser and strictness flags,
  source-origin capabilities and resolution base
)

AstKey = hash(SourceIdentity, AST cache ABI/version)

MirKey = hash(
  AstKey,
  compiler build ID and cache ABI/version,
  backend and optimization policy,
  classic | module | preamble | eval execution mode,
  module/preamble/Jube interface ABI digest,
  transitive public dependency-interface digests
)
```

For a file, canonical path plus `(mtime, size)` is a cheap change detector,
not sufficient identity. A changed detector forces a byte read and a new
content hash. A cache hit verifies source length and, in debug/canary mode,
bytes as well as the hash. URL or generated sources must supply an immutable
snapshot identity; a document-local label such as `<inline-script-2>` alone is
never reusable identity.

The service invalidates an entry when any source, parser/compiler version,
compile policy, preamble/interface ABI, or transitive public interface differs.
It invalidates dependent MIR artifacts as a dependency cone. AST artifacts can
remain when only a MIR policy changes. Cache entries are inserted only after
their relevant phase succeeds; a failed or poisoned artifact cannot be served
as a hit.

---

## 7. AST level

The AST cache is the first common level and must work in both execution tiers.

### 7.1 Frozen artifact contract

An AST artifact contains source-independent parse and validation facts only:

- the source-owned AST graph and immutable source spans;
- parser diagnostics and early-error result;
- lexical/index/binding facts that are valid for the key's language profile;
- static import declarations and public-interface summary;
- optional immutable frame/lowering plans.

It contains no current `Runtime`, `EvalContext`, heap `Item`, document `Input`,
DOM pointer, module namespace, mutable scope cell, temporary name-pool pointer,
or current exception state. A language whose existing AST mutates any of those
facts must either move them to an execution-side binding image, clone them per
instance, or decline AST admission until it is fixed.

### 7.2 Execution from AST

- Lambda's interpreter receives a fresh module slab and execution-local
  `Script` view from a cached T0 artifact. Direct import cones clone fresh
  Script shells, view registration stores generated anonymous references in
  that shell, and a cached MIR hit instantiates the complete JIT dependency
  cone before entering code. Eager JIT AST-miss reuse remains ineligible until
  MIR's AST-mutated facts move to a lowering overlay.
- `JS_EXECUTION_BACKEND=ast` instantiates a fresh `JsScript` execution image
  from the frozen JS AST. It does not return a prior realm's interpreter
  objects.
- A MIR miss starts from the cached AST when compatible, avoiding reparse and
  rebinding.

The former `JsRuntimeAstCache` runtime-local index is deleted. The common
service is the only owner of AST cache identity and counters; a reusable
`JsScript` gets a fresh execution overlay for every run.

---

## 8. MIR level

The MIR artifact is optional and always subordinate to its AST artifact.

### 8.1 Artifact contract

A cache-owned MIR artifact may retain:

- the sealed `MIR_context_t`, generated entry points, and immutable code;
- transpiler pools, literal/declaration metadata, and source diagnostics that
  generated code legitimately references;
- module export/public-interface metadata and resolved static import identity.

It must not retain values tied to a prior execution context. In particular it
must not embed a prior heap pointer, document pointer, DOM wrapper, temporary
constant-pool unit ID, mutable cache cell, module BSS slab, or raw per-realm
Jube/JS object address. Such a finding rejects the artifact until lowering
names or indexes the data through the active execution instance.

### 8.2 Instantiation

On a MIR hit the adapter:

1. creates the execution's `Input`/`Script` view, `Runtime`, `EvalContext`,
   heap, names, type data, and active source state as required by that command;
2. allocates or binds new per-context module slabs and registers their roots;
3. recreates language/host state, including JS globals, DOM bindings, event
   loop, module registry, and mutable template/regexp/shape data where needed;
4. binds imports and public symbols for this execution's module graph;
5. invokes the immutable entry point; and
6. tears down execution state while leaving only the leased cache artifact.

This is the fresh-document-realm rule of `JsMirLeaseSession`, generalized to
every script runner. It is also the requirement that makes a retained Lambda
MIR module compatible with a fresh heap under D5.4.3.

### 8.3 No implicit tier change

Cache availability must not change user-visible tier policy. An AST command
uses an AST artifact; a MIR command may use AST then MIR; a command that
selects an interpreter or no-JIT policy must not silently execute cached native
MIR. The cache key records the policy, and reporting says which level served
the execution.

---

## 9. Modules and imports

All module loaders become clients of the same script-cache service.

### 9.1 Common module rule

Module **source and compilation artifacts** are cacheable. Module
**instantiation and namespace/global values** are per execution. The loader
must ask the cache for a lease before parsing or compiling a module and must
instantiate it in the current runtime's registry.

The loader preserves D7.2.2: initialization is transactional. A module enters
the current runtime's import registry only after its current instantiation
succeeds; a failed initialization never publishes a partially established
module. Circular-import detection is likewise an execution-graph state, not a
process-global “currently compiling” cache flag.

### 9.2 Lambda modules

`load_script()` stops reading and compiling source directly. It becomes a
client that supplies the canonical import identity, importer resolution base,
and Lambda compiler key to `InputScriptCache`.

The current retained `Script` cache relies on `Runtime::scripts` list indices:
Lambda lowering emits imported function and variable names as `m<index>.*`.
It also bakes the runtime-assigned `module_state_id` when it addresses a
module-variable slab. Both values are valid only in the runtime that performed
the compilation.

Each persistent `ScriptInput` therefore receives one never-reused logical
`compilation_unit_id` at cache admission. Compiled symbol names use that stable
identity. At instantiation, the new runtime binds the logical unit to fresh
module state through a per-runtime mapping; it does not reuse the former
runtime's `Script::index` or `module_state_id`. Without this split, a cached
importer can link `m2._f` to the wrong module—or no module at all—after another
runtime loads modules in a different order. This is a prerequisite for broad
MIR reuse, not a cleanup detail, and preserves D8.5.1's never-compact intent.

### 9.3 JavaScript modules

`require()`, ES module loading, dynamic import, and Jube's JS import bridge
all acquire source through the same service. A JS module artifact key includes
canonical resolved URL/path, resolution base, module kind, import attributes,
strictness, preamble ABI if applicable, and dependency-interface digest.

Each runtime still creates its own CommonJS cache entry, ESM namespace,
live-binding cells, promise/module evaluation state, and host objects. The
current heap-backed JS module cache remains an execution cache; it is not
promoted as a process-global cache. Only the parsed/compiled template is.

### 9.4 Cross-language modules and future guests

The Jube layer supplies the selected language adapter and a stable public
interface digest. A cross-language dependency may receive an AST cache hit
before it is approved for MIR retention. It is never rejected merely because
it crosses languages; it is rejected only when its adapter cannot prove the
immutable-artifact/per-instance-state split.

---

## 10. Command-pipeline coverage

Every executable pipeline must acquire source through this service and admit
its successfully compiled scripts by default. A short command may have no
second execution to hit before process exit, but it still creates the same
manager-owned `ScriptInput` record and follows the same correctness path.

| Pipeline | Cached sources | Scope / isolation rule |
|---|---|---|
| `lambda.exe script.ls`, `lambda.exe run script.ls` | main Lambda script and all imports | Command scope; imports may hit within the graph. A standalone process naturally ends cold. |
| Lambda REPL | completed source snapshots and imports | Append-only history supplies a snapshot identity; dynamic fragments include their lexical/history identity before they can share an entry. |
| `lambda.exe test-batch` | each main script and shared imports | Batch scope; every item receives fresh heap/module state. This replaces the current forced L1 disable after the fresh-instantiation proof. |
| `lambda.exe js file.js` | main JS file, `require()`/ESM dependencies | Command scope with a fresh JS realm. |
| `lambda.exe js-test-batch` | harness, `source:`, `module-source:`, and dependencies | Worker scope. Its hot-harness reset remains a named execution policy, not a hidden cache side effect. |
| `layout`, `render`, `view`, `render-batch` | browser preamble, lifecycle sources, inline/external document scripts, and layout Lambda scripts | Command/batch scope; every document gets a fresh DOM/JS realm. `make layout` currently selects AST, so it should demonstrate AST hits rather than MIR hits. |
| `validate` and script-backed tools | schema Lambda source, imports, embedded JS when present | Command scope; data-only inputs are out of script-cache admission. |
| `run --lang` and extension-based Jube dispatch | hosted main source and host-language modules | Language adapter scope with the same identity/lease protocol. |

The direct `Input::create()` calls used by existing Lambda and JS compilation
must be replaced at these boundaries. Generic data parsing may continue to use
its ordinary `Input` factories until the later document-cache project.

---

## 11. Document-input extension point

The common service is intentionally named and placed around `InputManager`
because documents and scripts share source identity, source acquisition,
invalidation, scopes, budgets, and leases. It does **not** imply that the
current parsed `Input` object is immediately cacheable.

A later `InputDocumentArtifact` may occupy a sibling artifact slot only after
its format defines:

- what parsed representation is immutable and shareable;
- how every consumer gets a fresh/isolated mutable document view;
- source identity and dependency invalidation (CSS/includes/entities/etc.);
- allocator ownership and per-document reclamation;
- security/capability boundaries for file, network, and generated inputs.

Until then, document parsing receives a central cached `SourceBlob` at most and
builds a normal execution-local `Input`. The cache must never retain a page
`Input` merely to make source bytes reusable.

---

## 12. Concurrency, memory, failure, and observability

### 12.1 Concurrency

The common index is process-safe. Entries use explicit states:

```text
absent -> source-ready -> ast-compiling -> ast-ready -> mir-compiling -> mir-ready
                            |                 |              |
                            +-> rejected      +-> poisoned   +-> poisoned
```

The common service now provides per-AST/MIR-key build claims. One Lambda, JS
AST, Radiant JS-MIR lease, or synchronous static-JS-import module worker
compiles a claimed key; compatible fresh runtimes wait and re-enter ordinary
artifact lookup after publication. A failed non-poisoned owner permits one
subsequent ordinary retry, while a poisoned key is fail-closed until its source
generation is invalidated. TLA/dynamic/re-export/CommonJS module-MIR and guest
adapters have not yet adopted build claims. A lease prevents an entry from
being evicted while an execution instance references it.

### 12.2 Memory and eviction

Each artifact reports retained source, AST, compiler-pool, MIR-code, and
metadata bytes. The initial generalized mode retains every successfully
admitted `ScriptInput` for the `InputManager` lifetime and records its growth.
It does not add eviction as a hidden admission condition. A later cache-memory
phase will add high-water limits and clear only unleased entries and, for
modules, only a dependency cone whose compiled artifacts no longer reference
one another. Engine-owned preambles and ordinary main scripts follow the same
default retention rule until that phase is implemented.

Every allocation used by a cache artifact receives a named cache context under
D4.2.3. A cache context is neither a document context nor an execution heap.

### 12.3 Failure handling

Compiler errors do not publish an artifact. The initial implementation keeps
poisoning minimal: an adapter can complete a claimed key as poisoned after a
crash, timeout, link/cache-integrity failure, or failed fresh-instance cleanup;
the common entry blocks future artifact reuse/build claims until source
invalidation. Ordinary compile failures complete unpoisoned and may retry.
No eviction policy is introduced by this failure handling.

### 12.4 Counters and controls

The unified report records, by language and source class:

```text
source_lookups, source_hits, source_misses
ast_lookups, ast_hits, ast_misses, ast_builds
mir_lookups, mir_hits, mir_misses, mir_builds
module_hits, dependency_invalidations, rejected, poisoned, evictions
retained_source_bytes, retained_ast_bytes, retained_mir_bytes, peak_bytes
parse_us, bind_us, lower_us, link_us, instantiate_us, execute_us, reset_us
```

The proposed control is `LAMBDA_SCRIPT_CACHE=off|ast|mir|all`; the default is
`all`. Memory-limit controls are deferred to the cache-memory phase. During
migration, `LAMBDA_DISABLE_MIR_CACHE` and
`LAMBDA_DISABLE_JS_MIR_CACHE` are compatibility aliases that map to the
corresponding common policy and emit one migration diagnostic. They are removed
only after all callers migrate.

---

## 13. Migration and retirement plan

### Phase 0 — record decisions and measure

1. Record the resolved §15 policies in the formal design when implementation
   is authorized: persistent manager-owned `ScriptInput` artifacts and default
   cache admission for every script. **Done:** D7.1.2v2 and D8.5.1v2 were
   ratified on 2026-09-13.
2. Inventory every direct executable-source read and direct compiler `Input`
   creation; add source-class counters without changing execution.
3. Audit Lambda and JS ASTs for execution-mutated fields and MIR for
   context-dependent addresses.
4. Define a stable compilation-unit identity that preserves current Lambda
   import-link semantics across fresh runtimes.

**Exit gate:** complete command-path inventory; every emitted address and AST
field is classified cache-owned, instance-owned, or ineligible.

### Phase 1 — source identity, scopes, and leases

1. Add `InputScriptCache` coordination to `InputManager` with opaque adapter
   registrations and named cache contexts.
2. Route Lambda main/import source and JS main/module source acquisition
   through it in source-only mode.
3. Make scope teardown release execution leases only; `InputManager` retains
   admitted `ScriptInput` records. Add byte accounting, invalidation,
   diagnostics, and uncached equivalence tests.

**Exit gate:** no script compiler reads source outside this service; no cached
entry retains an execution `Input` or document allocation.

### Phase 2 — AST cache

1. Promote Lambda immutable AST artifacts behind an adapter (landed for T0
   units, including direct import cones and interpreter views); JS AST
   templates and execution overlays are landed. Eager JIT AST-miss reuse
   remains blocked on a lowering-fact overlay.
2. Keep common-service AST lookup ownership; the former `JsRuntimeAstCache`
   index is retired.
3. Make the layout test runner's AST backend emit common AST counters.
4. Route dynamic/eval and REPL snapshots through the same service. Their key
   must include every lexical, preamble, and history dependency; an incomplete
   key is rejected as unsafe rather than excluded by source class.

**Exit gate:** AST-enabled Lambda and JS batches show repeat hits with
byte-identical outputs and fresh runtime/realm state.

### Phase 3 — Lambda MIR and modules

1. Make `load_script()` acquire/instantiate cache leases instead of owning the
   former Runtime compilation cache (landed; the remaining
   `loaded_script_index` is same-runtime registration only).
2. Implement stable compilation-unit identities, dependency-cone invalidation,
   and per-execution BSS/module-slab rebuild. **Landed for Lambda file-backed
   import cones:** a cached root refreshes changed child source generations
   before reuse, then retires the importer cone through the common service.
3. Enable default cache lookup for `test-batch` main scripts and imports after
   the fresh heap/root proof passes.
4. Delete the old retained-`Script` index, `cache_retain` policy, and its
   duplicate counters after migration (landed; current load counters describe
   only Runtime-local registration).

**Exit gate:** repeated Lambda imports and repeated batch main scripts hit AST
and MIR independently; source changes invalidate dependents; module failures
remain transactional.

### Phase 4 — JS MIR migration

1. Make `JsPreambleState`/equivalent an adapter-owned `MirArtifact`.
2. Migrate Radiant preamble, lifecycle, and external classic entries from the
   legacy cache façade to the common key/scope/lease service (landed through
   `JsMirLeaseSession`). The duplicate `RADIANT_JS_SOURCE_CACHE` URL-source
   LRU and its private counters are retired: remote snapshots are admitted by
   `InputManager` after the generic HTTP disk-fetch cache, as required by
   D8.5.1v2.
3. Route JS CLI and `js-test-batch` compatible preambles through the same
   adapter while retaining their explicit hot-heap policy.
4. Retire the legacy `JsMirCache` ownership/index while retaining only the
   bounded `JsMirLeaseSession` lease boundary (landed; counters and the
   fresh-realm regression remain required coverage).

**Exit gate:** direct MIR layout batches report common MIR hits; `make layout`
reports common AST hits; cached and uncached results preserve fresh realms.

### Phase 5 — modules, guests, and document artifacts

1. A synchronous ESM module whose transitive static-JS-import closure is itself
   cache-safe now instantiates a common MIR artifact after rebuilding its
   namespace and module slab. Its direct/transitive local JS static-import
   source records form a freshness cone, and each hit recreates those dependency
   namespaces before entering the parent image. Complete
   CommonJS, dynamic/re-export, TLA, and cross-language ESM MIR instantiation
   after the namespace/live-binding/reset audit; their source and safe AST
   artifacts use the common service from the earlier phases.
2. Add hosted-language adapters one language at a time. Each script is
   cache-admitted by default; AST-only is acceptable until that guest can prove
   its MIR artifact has a fresh-instance contract.
3. Propose separate document-artifact profiles after their freeze/clone
   contracts are designed.

**Exit gate:** every executable module loader uses the common cache service;
no language-specific source/AST/MIR cache index remains outside it.

---

## 14. Required verification

- Lambda: repeated imports, repeated main scripts in a batch, circular imports,
  source edit/reload, failed initialization rollback, and forced-GC runs.
- JS AST: identical source in separate fresh realms; `window`, prototype,
  exception, timer, and module-registry mutations must not cross runs.
- JS MIR: cached-versus-uncached layout/render frames; cached browser preamble,
  lifecycle, classic external source, CJS, ESM, and dynamic-import coverage.
- Layout: a direct MIR batch shows first-miss/later-hit counters; `make layout`
  remains AST-selected and shows AST rather than MIR cache activity.
- `js-test-batch`: harness preservation remains exactly policy-controlled;
  independent tests do not inherit mutable test state through the new cache.
- Hosted language: one adapter fixture proves a cached parsed/compiled template
  is instantiated with fresh guest state.
- Failure paths: parse failure, compilation failure, timeout, crash recovery,
  poisoned entry, dependency invalidation, concurrent same-key acquisition,
  budget eviction, and command-scope teardown.
- Gates: focused tests first, then `make test-lambda-baseline`,
  `make test262-baseline`, and the relevant Radiant/layout suite. Performance
  claims use release builds and an enabled/disabled common-cache A/B pair.

---

## 15. Resolved decisions and remaining open questions

### R1 — Persistent manager-owned script inputs (resolved 2026-09-13)

`InputManager` is the central persistent input and related-cache store. Each
admitted script has a persistent `ScriptInput`; its source, AST, and optional
MIR artifact remain linked below that input after an execution runtime is
destroyed. Runtime/context/heap/DOM/module state is never retained there.

The persistent input owns opaque artifact handles and their lifetime. The
language adapter supplies artifact operations and retains the actual type
knowledge, preserving the D7.1.2 layer boundary described in §5.1.

### R2 — Default admission for all scripts (resolved 2026-09-13)

All successfully compiled Lambda, JS, and hosted-language scripts are cached
by default, including main scripts and scripts reached by every command
pipeline. This is not restricted to imported modules or batch runs. A
one-shot command will normally gain no later hit before process exit, but it
uses the same `ScriptInput` path. Cache-memory management and selective
clearing are a future phase; retained-byte accounting starts immediately.

This generalizes D8.5.1 beyond its former imported-module wording and is
ratified in **D8.5.1v2**.

### Implementation status — 2026-09-14

The first production slice is landed, but this proposal is not complete. The
common service now provides manager-owned source records, exact-byte identity,
named cache contexts, scopes/leases, opaque AST/MIR artifact slots, policy
controls, byte accounting, and diagnostics. Lambda source admission uses the
service, including AST-dump/validation paths and imported modules; JS CLI,
Node-style test-runner, module, and interpreter file acquisition use it; hosted
CLI/Jube source bridges use language/profile keys; and Radiant local external
JS, URL snapshots, plus its batch preamble/external-classic MIR adapter use it.
The Radiant remote-fetch LRU remains transport-only and feeds exact URL bytes
into the common service. Per-document input cleanup now retains the process
cache so a batch cannot leave an adapter pointing at destroyed cache state, and
the Radiant MIR adapter holds active artifact leases until batch teardown.

The reusable-artifact slice now includes Lambda interpreter import/view
overlays and JIT module cones. A cached T0 parent creates fresh Script shells
for its direct imports; interpreter view registration keeps generated anonymous
references on that execution shell, not in the frozen parser name pool. A
cached Lambda MIR hit creates the same full dependency shell graph before
binding dense module slabs, and P2 satellite imports derive the current
owner/slot rather than writing resolution state into a cached `NameEntry`.
P2 promotion counters and boxed satellite entries likewise live in a
per-`Script` overlay for cached AST instances; an execution can neither read
an entry from a retired EvalContext nor publish one into the parser-owned
definition (D8.1.1v2/D8.5.1v2). A P2-touched AST image is conservatively
excluded from later AST hits until lowering's remaining mutable facts receive
the same overlay; this is a correctness gate, not poisoning or eviction.
Eager JIT AST-miss reuse remains excluded because MIR lowering still mutates
AST facts. The common JS AST adapter replaces `JsRuntimeAstCache` for all
parsed JS forms. Each hit creates a new `JsScript` shell, module slab, and
execution overlay; function/class lazy definitions, module/eval bookkeeping,
and TypeScript facts never mutate the shared parser template. A synchronous
ESM module whose static-JS-import closure is synchronous and cache-safe now
retains an immutable MIR image, refreshes every recorded dependency source
generation before a hit, and recreates its dependency namespaces, module slab,
property-key image, and declaration metadata on every execution. Radiant's
preamble and external-classic JS MIR adapter is stored in the same opaque
common MIR slot, and `JsMirLeaseSession` retains only batch leases/accounting;
the existing fresh-document-realm regression covers its instantiation path.

TLA/dynamic/re-export/cross-language JS module artifacts, CommonJS module MIR
artifacts, hosted-language artifact paths, guest single-flight adoption,
JS/guest dependency-cone invalidation outside static local ESM, richer failure
recovery, eviction, eager-JIT AST-miss overlays, and the full command-pipeline
inventory remain open Phase 2–5 work. Lambda file-backed import cones and the
admitted static-JS-import module cones are recorded by persistent unit ID; a
cache hit refreshes a changed child generation and retires all affected
importers before reuse.
If a dependency-freshness proof or an otherwise-ready artifact cannot be
instantiated, Lambda retires that logical unit and its importer cone before one
retry, so it never re-enters the same untrusted image. Active leases defer
destruction. JS AST template clones also retain their
logical unit ID and obtain a fresh dense module slab in the receiving runtime.
The common service single-flights AST/MIR keys. Lambda AST/MIR admission, JS
AST admission, Radiant's preamble/external-classic JS-MIR lease admission, and
closed synchronous JS-module MIR admission use those claims; the initial
per-key poison state is fail-closed and clears only with source-generation
invalidation.
`Runtime::loaded_script_index` remains a runtime-local source/import registry,
not an artifact owner or retention cache. The legacy Radiant cache façade is
retired: its replacement
owns only batch accounting and active leases while `InputScriptCache` owns
artifact identity and lifetime.

### R3 — Stable logical compilation-unit IDs (implemented 2026-09-14; D8.5.1v2)

A stable ID is required only for **reusable Lambda MIR**, not as an additional
user-visible script identity. Today `write_fn_name_ex()` and `write_var_name()`
emit imported symbol names with `import->script->index`, for example
`m2._f`. `runtime_register_script()` assigns that index from the current
runtime's script-list position. If the runtime is discarded, the next runtime
can assign module `A` index 1 instead of 2, or assign index 2 to module `B`.
Cached code compiled against `m2._f` would then resolve the wrong function or
fail to link.

`module_state_id` has the same lifetime issue for compiled module-variable
access: it is allocated by a runtime and therefore must not be embedded as a
cache-wide identity. The cache distinguishes a persistent
`ScriptInput::compilation_unit_id` used for code identity from a fresh
runtime's dense module-state allocation. `Runtime::module_unit_index` binds
the former to the latter as each `Script` is registered. Generated layouts and
ordinary imported-variable access carry the logical unit and resolve it through
that map; T0 satellite code retains its explicitly physical local slab access.
The BSS identity marks logical units with a reserved high bit, so an ordinary
physical slab `n` cannot be redirected when a cached unit also has raw ID `n`.
Thus a fresh-runtime MIR hit can coexist with independently loaded modules
without sparse slab growth or recompilation caused by an ID collision.

### Q1 — What source-snapshot policy applies during a long-running manager?

**Recommended answer:** each acquisition compiles exact bytes. File stat is a
fast change detector; a detected change yields a new content identity and
invalidates the old dependent cone for future acquisitions. Active leases keep
their source snapshot until execution ends.

### Q2 — Is a parsed input-document cache expected to preserve object identity
or provide clone-on-consume semantics?

This determines whether a future document artifact can retain a frozen Mark
tree, a replay representation, or only source bytes. It is intentionally out
of the script implementation path; the recommended default is immutable
artifact plus fresh consumer view, never shared mutable `Input` state.

### Deferred — Cache memory policy

There is no default eviction or high-water clearing policy in this proposal's
first implementation. It retains all successfully admitted script inputs under
the persistent manager and records exact retained bytes. A later cache-memory
proposal chooses limits, priorities, and safe dependency-cone clearing.

---

## 16. Consequences

This proposal makes `InputManager` the single answer to “where did executable
source come from, what immutable artifacts may be reused, and for how long?”
It does not make `InputManager` a global runtime or a document store.

The resulting execution rule is simple: **cache source and immutable
compilation products; recreate every execution context and mutable language
state.** That rule satisfies the existing fresh-realm behavior, makes AST and
MIR caching composable, and gives future hosted languages and document inputs
one extensible entry point without sharing their semantics or lifetimes.
