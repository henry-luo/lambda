# Lambda Script Cache Proposal

**Status:** Proposal — unratified  
**Date:** 2026-09-13  
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
D8.4.1v2 (immutable generated code), and D8.5.1 (L1 imported-module cache).
This proposal records the 2026-09-13 policy decisions in §15; formal rulings
are revised only when implementation is authorized.

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
| Lambda `.ls` | `Runtime::script_index` and retained `Script` imports in `runner.cpp` | Only Lambda imports; the cache is coupled to `Runtime`, `Script::index`, and its current lifetime. `test-batch` disables retention before each isolated item. |
| JS AST tier | `JsRuntimeAstCache` in `js_scope.cpp` | Runtime-local and JS-only; source loading still creates a direct runtime-pool `Input`. |
| JS MIR layout | Batch-owned `JsMirCache` in `js_mir_cache.cpp` | Covers browser preambles and eligible external classic scripts only; owned by the Radiant path. |
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
- Migrate and delete the duplicated Lambda L1, JS AST, and `JsMirCache`
  indexes after equivalent coverage exists in the common service.

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
must record this interpretation before implementation lands.

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

- Lambda's interpreter/MIR preparation receives a fresh module slab and
  execution-local `Script` view from the cached artifact.
- `JS_EXECUTION_BACKEND=ast` instantiates a fresh `JsScript` execution image
  from the frozen JS AST. It does not return a prior realm's interpreter
  objects.
- A MIR miss starts from the cached AST when compatible, avoiding reparse and
  rebinding.

The existing `JsRuntimeAstCache` becomes an adapter implementation detail
during migration, then its runtime-local index is deleted. The common service
is the only owner of AST cache identity and counters.

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

This is the existing fresh-document-realm rule of `JsMirCache`, generalized to
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

One worker compiles a given key; compatible contenders wait for the result or
take an ordinary uncached path according to the scope policy. A lease prevents
an entry from being evicted while an execution instance references it.

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

Compiler errors do not publish an artifact. A crash, timeout, link failure,
cache-integrity failure, or failed fresh-instance cleanup poisons the active
artifact and releases it when the last lease ends. Existing retry policy may
allow one uncached retry; it must never execute the poisoned artifact again.

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
   cache admission for every script.
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

1. Promote Lambda and JS immutable AST artifacts behind adapters.
2. Replace `JsRuntimeAstCache` lookup ownership with the common service.
3. Make the layout test runner's AST backend emit common AST counters.
4. Route dynamic/eval and REPL snapshots through the same service. Their key
   must include every lexical, preamble, and history dependency; an incomplete
   key is rejected as unsafe rather than excluded by source class.

**Exit gate:** AST-enabled Lambda and JS batches show repeat hits with
byte-identical outputs and fresh runtime/realm state.

### Phase 3 — Lambda MIR and modules

1. Make `load_script()` acquire/instantiate cache leases instead of owning the
   `Runtime::script_index` compilation cache.
2. Implement stable compilation-unit identities, dependency-cone invalidation,
   and per-execution BSS/module-slab rebuild.
3. Enable default cache lookup for `test-batch` main scripts and imports after
   the fresh heap/root proof passes.
4. Delete the old retained-`Script` index, `cache_retain` policy, and its
   duplicate counters after migration.

**Exit gate:** repeated Lambda imports and repeated batch main scripts hit AST
and MIR independently; source changes invalidate dependents; module failures
remain transactional.

### Phase 4 — JS MIR migration

1. Make `JsPreambleState`/equivalent an adapter-owned `MirArtifact`.
2. Migrate Radiant preamble, lifecycle, and external classic entries from
   `JsMirCache` to the common key/scope/lease service.
3. Route JS CLI and `js-test-batch` compatible preambles through the same
   adapter while retaining their explicit hot-heap policy.
4. Delete `JsMirCache` and its Radiant-specific ownership/index once counters
   and fresh-realm tests are equivalent.

**Exit gate:** direct MIR layout batches report common MIR hits; `make layout`
reports common AST hits; cached and uncached results preserve fresh realms.

### Phase 5 — modules, guests, and document artifacts

1. Complete JS CommonJS/ESM MIR instantiation after the
   namespace/live-binding/reset audit; their source and safe AST artifacts use
   the common service from the earlier phases.
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

This generalizes D8.5.1 beyond its current imported-module wording and must be
ratified into the formal design before implementation.

### R3 — Stable logical compilation-unit IDs (resolved in principle)

A stable ID is required only for **reusable Lambda MIR**, not as an additional
user-visible script identity. Today `write_fn_name_ex()` and `write_var_name()`
emit imported symbol names with `import->script->index`, for example
`m2._f`. `runtime_register_script()` assigns that index from the current
runtime's script-list position. If the runtime is discarded, the next runtime
can assign module `A` index 1 instead of 2, or assign index 2 to module `B`.
Cached code compiled against `m2._f` would then resolve the wrong function or
fail to link.

`module_state_id` has the same lifetime issue for compiled module-variable
access: it is currently allocated by the runtime and appears in generated MIR.
The cache must distinguish a persistent `ScriptInput::compilation_unit_id`
used for code identity from a fresh runtime's module-state allocation. The
latter is bound through a per-runtime map during instantiation. The remaining
implementation question is the exact map/indirection shape, not whether the
stable logical identity is necessary.

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
