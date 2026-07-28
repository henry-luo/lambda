# Lambda Runtime Globals — Audit & Migration to EvalContext

**Date:** 2026-07-27
**Status:** Implemented. Core context ownership, Lambda module state, JS
runtime state, the Direct hidden-context ABI, Jube session ownership, and the
in-scope Radiant bridge state are context-owned. Release verification is
recorded in the implementation checkpoint; known unrelated fixture diagnostics
are explicitly called out rather than being treated as migration regressions.
**Scope:** `lambda/runtime/`, `lambda/core/`, `lambda/js/`, `lambda/jube/`, guest runtimes (`py/`, `bash/`), `lambda/module/`, and the `lib/` infra they lean on.
**Relation to prior docs:** expands the global-state ledger in `vibe/Lambda_Js_Thread.md` §6.5 into a full inventory and migration design. The Js_Thread JT decisions (JT1 context-thread rule, JT4 per-thread recovery, JT6 loop affinity) are taken as given; this doc is the state-ownership side of the same program.

---

## 1. Goal

> **No context-dependent mutable semantic state may remain process-global.**
> The Lambda and JS runtimes execute through a long-lived `EvalContext` identity. Any value
> that may differ between two live contexts, and whose mutation can affect script-visible
> results, errors, timing, visibility, or behavior, belongs to that context (or to an explicitly
> shared object such as an agent cluster), never to one process-wide storage slot.

The practical review test is:

> If context A writes the value, can context B subsequently observe different behavior because
> of that write? If yes, the value is context-dependent semantic state and must not be global.

This deliberately permits process-wide storage that is independent of context semantics:
immutable registries, explicitly process-wide policy frozen before execution, diagnostics that
never feed execution, signal/thread infrastructure, and locked library bookkeeping. Those
exceptions are tagged and constrained below; "global" alone is not the bug, shared mutable
semantic ownership is.

The migration has an equally strong performance invariant:

> **No lock, atomic read-modify-write, publication check, or shared-cache coherence operation is
> added to a repeated execution path.** Synchronization is allowed at module/context
> construction, one-time publication, invalidation, and explicitly shared language operations
> such as Atomics. Mutable hot-path data is context-owned and accessed with ordinary loads and
> stores by its single owner thread.

The desired result is structural cleanup **and** equal-or-better steady-state performance.
Moving a global behind a lock or atomic is not an acceptable migration for ICs, module variables,
call/eval registers, exception state, queues, timers, or other repeated runtime paths.

Why now:

- **Correctness under concurrency.** Everything today is safe only under the single-executor
  rule (one thread executing script at a time), maintained by religious resetting of shared
  globals between runs. RC2 (page isolates), K11–K18 (`start`), and JS workers all need N live
  contexts. Every global in this inventory is a latent cross-isolate bug.
- **Hot-context hygiene.** Batch reuse works by hand-resetting dozens of scattered statics
  (`js_eval_state_reset`, `js_root_range_reset_all`, epoch checks on every cache). When state
  lives on `EvalContext`, "reset" collapses to "make/clear the capsule".
- **GC honesty.** Rooted `Item` globals are scattered across ~30 files, each with its own
  `*_roots_epoch` re-registration dance. Centralizing them under the context that owns the heap
  removes an entire class of stale-root bugs (the forced-GC sweep keeps finding these).

### Implementation checkpoint (2026-07-27)

- `Runtime::eval_context` is the canonical long-lived owner. `context` is now
  only a scoped TLS binding; runner cleanup and JS entrypoints no longer create
  a stack `EvalContext` for a normal activation.
- Lambda globals and member ICs are instantiated as fixed per-context module
  slabs. Their roots are registered once when the module instance is prepared;
  generated loads/stores and IC probes use ordinary context-owned memory.
- JS semantic state, queues/timers, CJS/deferred-MIR state, call state,
  generator/async records, and wrapper identity caches are context-owned.
- Lambda Direct and JS MIR Direct forward `Context*` as a hidden ABI argument.
  JS wrappers, fixed-arity dynamic entries, direct calls, closures, native
  variants, generator resumption, async resumption, and module entrypoints all
  preserve the pointer without `_lambda_rt` import/TLS reloads in generated
  code. JS function objects retain their immutable owner context for the one
  dynamic-dispatch adaptation boundary.
- Template registries are now `EvalContext` state. Guest activation and Jube
  frame-runtime plumbing use canonical/execution-owned context pointers rather
  than publishing `_lambda_rt`.
- Render reconciliation and template-state maps are context-owned capsules.
  Their fixed document-root slots are registered once against the owning heap
  and unregistered during that context's heap reset/teardown.
- Lambda document loading, custom layout, and retained Lambda event handlers
  now re-enter the document Runtime's canonical context instead of creating
  heap-only stack contexts after script execution.
- The active DOM document, main document, UI context, host-loop mode,
  `designMode`, and active element are now `JsDomState` fields rather than
  thread-local state. Document-script reattachment explicitly rebinds the
  document Runtime before publishing DOM wrappers; post-teardown host hooks
  are context-free no-ops rather than creating or mutating a replacement
  realm.
- DOM platform `localStorage`, `sessionStorage`, and `matchMedia` records are
  now context-owned. Their object homes are published once to the owning heap;
  storage reads/writes and media-query evaluation use direct owner-thread
  storage without synchronization.
- Pre-runtime document/UI setup no longer writes a thread-global DOM binding:
  those host notifications are no-ops until a document `EvalContext` exists,
  and the normal script-entry rebind publishes the document into that owner.
- Mutation, resize, and intersection observer records (including callback
  roots and pinned native targets) are lazily allocated per `JsRuntimeState`.
  The fixed callback/object root homes are registered once per owning heap;
  observer delivery and mutation notification stay direct context-local work.
- DOM event listener tables, IDL-handler ordering, and legacy propagation
  mirrors are now a lazy context-owned capsule. Listener dispatch remains
  direct owner-thread array/hash lookup; its precise callback roots and DOM
  pins are released during the cold pre-heap teardown phase.
- XMLHttpRequest pools, base URLs, and asynchronous request tokens are
  context-local. History traversal task queues and their rooted state payloads
  are likewise context-owned; neither path retains a thread-local fallback or
  introduces synchronization in request/event delivery.
- Attached-DOM expando roots and all live `children`/form/select/lookup
  collection registries are now context-owned. Their hot property-read path
  is a direct capsule lookup; weak array homes and native-node pins are
  released only in cold document/heap teardown.
- Foreign-document wrapper identity, iframe-document ownership, browsing
  context markers, and deferred iframe-load pins are now context-local. This
  keeps DOMParser/iframe state isolated without process-wide TLS caches.
- `fetch()` relative-path state, response-body slots, and the synchronous
  Promise-executor handoff are context-owned. The CLI document path is only a
  one-shot bootstrap input: it is copied into the first context and cleared,
  so response delivery has no process-global fallback.
- FS callback-style pending requests are linked from the owning context's FS
  state. Their existing precise Jube roots and cancellation path run during
  that context's teardown, with no shared pending-request list.
- TLS ticket-generation records and tracked native secure contexts are held
  by the context's TLS capsule and released with its cold reset, rather than
  through process-global linked lists.
- Permission grants and `process.permission.drop()` mutations are now kept in
  a per-context policy capsule. Command-line flags are frozen bootstrap input
  copied on demand; permission checks retain only direct local reads after
  context entry.
- Net defaults, BlockList records, and Node host callbacks (shutdown/IPC,
  cluster, console formatting) are context-local. Net allocates its native
  capsule only when the module is first loaded; BlockList backing records are
  separately lazy. Socket paths read direct local fields and take no lock or
  atomic readiness branch.
- `assert`/`node:test` namespaces, hook stores, event queues, assertion
  instances, and fixed mock-wrapper slots are context-owned. The generated
  mock wrappers retain their fixed slot dispatch but select the active
  context's table, keeping the repeated call path lock-free.
- Crypto's native HMAC/hash/sign/cipher lifecycle registries, canvas font
  context/handle pool, Test262 Atomics waiter state, and the dynamic Function
  MIR cache are lazy per-context capsules. Atomics remains an explicitly
  shared language operation at the SharedArrayBuffer boundary; the runtime's
  waiter bookkeeping is no longer shared between unrelated realms.
- Dynamic-eval parser diagnostics and MIR debug-site counters are compiler
  instance state. Tagged-template eval salts are context-local, so concurrent
  compilation cannot overwrite a process-global last-error/counter value.
- `process.exitCode` is now retained only in the executing context; CLI code
  rebinds the Runtime-owned context before reading it instead of synchronizing
  through a process-global scalar.
- Jube's active Node-service session is now selected from the bound
  `EvalContext`; module attachment is recorded per session rather than by a
  single last-attached generation. The registry uses a cold attach/detach lock
  only to publish those session records. Namespace and runtime calls continue
  through the already-bound context without a repeated synchronization check.
- Node-core's session-owned namespace caches, rooted Items, Blob URL table,
  EventEmitter keys/prototype, process capture callback and umask, and the
  OS priority override now live in fixed module-state slots on that Jube
  session. Slot allocation is serialized only during module attach; normal
  Node calls are a direct active-context slot load. Jube async-work lists and
  request-id counters are likewise per session, so cancellation and late
  completion no longer scan a process-global queue.
- Node process uptime and macOS monotonic-clock conversion are per session;
  OS information queries use call-local buffers instead of unsynchronized
  process caches. These APIs remain lock-free and avoid stale context data.
- Jube module activation now has a cold condition-variable publication gate:
  concurrent requests wait for one initializer to finish, cyclic re-entry is
  rejected, and callbacks run outside the lock. Published descriptors are
  immutable for execution; no module-call or IC path takes this lock.
- Radiant iframe recursion depth is now owned by `UiContext`, which preserves
  the depth across nested short-lived layout contexts without coupling two
  independent UI/document trees through thread-local state.
- Radiant's editing transaction sequence, state-batch suppression depth,
  state-key interning, and native click-series tracking are now document/UI
  owned. This removes shared lazy state from input dispatch and keeps the
  repeated state-map comparison path as ordinary per-document pointer loads.
- A `DomDocument` now retains a complete JS `Runtime`, rather than copied
  heap/name-pool/type-list fragments. Script tasks, inline-handler compilation,
  event dispatch, the host timer pump, focus simulation, and selection APIs
  all borrow that Runtime's canonical `EvalContext`. Transient document
  teardown drains deferred MIR and event-loop roots before releasing the
  capsule; later host cleanup is context-free and does not recreate state.
- The broad JS-host namespace/prototype cache sweep is complete. The
  capsule now owns DNS, builtin, readline, Buffer, HTTPS, util, crypto,
  child-process, zlib, TLS, stream, HTTP, net, fs, clipboard, the primary DOM
  singleton wrappers, string-concatenation fast tables, global-object/lexical
  bindings, constructor identity caches, core namespace objects, Test262-agent
  queues, Node process state, diagnostics channels, and async hooks. Process
  IPC now allocates a context-owned libuv pipe and re-enters its owning context
  from libuv callbacks. Their one-time root registration happens at
  module/context initialization; repeated calls use direct owner-thread fields
  with no lock or atomic check. The remaining process globals in those files
  are classified bootstrap, immutable registry/configuration, signal support,
  or diagnostics and do not feed realm semantics.
- The context capsule now also owns Promise/domain records (allocated only on
  first Promise use), ES-module/TLA registries and dynamic-import slots,
  tagged-template identity, runtime-owned array-buffer tracking,
  constructor/prototype reset snapshots, AsyncLocalStorage and cluster state,
  performance clocks, and both realm-bound RegExp compilation caches. Promise,
  module, and cache root publication is once per context/heap epoch; Promise,
  module lookup, RegExp lookup, and async execution use direct context-local
  fields with no lock, atomic, or repeated readiness test. Heap-backed native
  regex records are explicitly released before their owning heap is destroyed.
- The remaining `_lambda_rt` definition is legacy C2MIR compatibility only;
  the frozen C2MIR path is outside the new Direct-runtime ABI contract.
- JS MIR timeout-recovery ownership (active MIR context, transpiler, owned
  source, and nested compilation stack) is a lazy `JsRuntimeState` capsule.
  The compile entry binds the canonical context before parsing and restores the
  caller on every early error; this cold path never appears in generated code.
- The Radiant editor source-path side table is an opaque extension of the
  context-owned render-map capsule, with a lifecycle cleanup callback. Range
  ids, text-control undo/redo guard depth, and ambient history `inputType` are
  direct `DocState` fields, so document editing has no TLS or synchronization
  dependency.
- Release validation (`make build-release-compile`) completed with zero build
  errors. Fresh-realm JS microbenches completed for property, binop, and
  equality paths; the property benchmark measured direct owner-local accesses
  (best: set 16.02 ns/op, get 20.46 ns/op). The Jube OS registry batch and the
  Radiant click-bubbling layout fixture complete successfully. That layout
  fixture still emits its pre-existing `document.fonts` null errors, and the
  batch runner still reports its pre-existing retained JS-runtime allocations
  at process shutdown; neither is introduced by this migration.

### Post-migration implementation invariants (2026-07-27)

- **Branch-safe module-state materialization.** A generated function may first
  reference its module state in either arm of a branch. The cached
  `LambdaModuleState*` is therefore inserted at that function's entry label,
  not emitted lazily at its first textual use. Both branch arms then see one
  defined owner-local pointer, without a repeated readiness test or a
  per-operation reload of the variable slab.
- **Member IC bases are deliberately reacquired at each probe.** “Reload” here
  means ordinary loads from the current function's hidden `Context*` to its
  already-instantiated, owner-thread module slab; it is not an atomic load,
  lock, TLS lookup, helper call, or shared-cache validation. This keeps an IC
  probe from retaining a virtual register across a runtime call/control-flow
  edge while ensuring that the entry it mutates is never shared with another
  context. Module setup/root registration remains the only cold-path work.
- **Bootstrap-owned async handles must be attached before they are observed.**
  A forked child's inherited IPC descriptor can arrive while `process` is
  being built, before ordinary JS script entry initializes libuv. The process
  bootstrap establishes the event-loop instance before opening that descriptor
  and stores the canonical `EvalContext` in `uv_handle_t::data`; every libuv
  callback re-enters that owner with `EvalContextScope`. This is a one-time
  bootstrap edge, not a per-message synchronization mechanism.
- **Function ABI and realm ownership are explicit boundary contracts.** Only
  compiled MIR method wrappers carry the hidden `Context*` ABI. Jube trampolines
  and native/DOM callbacks retain the ordinary ABI and receive fresh wrapper
  identities where JS observability requires it. Similarly, a stack-worker or
  document execution realm receives its borrowed UI context before script
  execution; transferring it after callbacks have been installed is invalid.
  These are allocation/bootstrap boundaries, so callback invocation and DOM/IC
  hot paths remain direct owner-local accesses.
- **Verification.** `make test-lambda-baseline` completed with 1,480/1,480
  passing tests after this implementation checkpoint, including JS IPC,
  hosted `BoundSocket`, forced-GC MIR, and emission-ratchet coverage.

---

## 2. Classification taxonomy

Every file-scope mutable variable (global, `static`, `__thread`, `thread_local`) gets one tag:

| Tag | Meaning | Verdict |
|-----|---------|---------|
| **B** | Bootstrap root — the one pointer that *finds* the active context | stays TLS (see RG1) |
| **R** | Context-dependent mutable semantic runtime state | **migrate to EvalContext** (or an explicit shared semantic owner) |
| **C** | Compile-time state — parser/AST/transpiler/MIR-codegen | migrate to `Runtime` / transpiler object (RG12) |
| **I** | Init-once immutable registry/config — safely published, read-only after freeze | stays process-global (RG6) |
| **T** | Thread infrastructure — stack guard, side-stack backing, signal recovery | stays `__thread` (RG10) |
| **S** | Shell state — `lambda/main.cpp`, REPL, batch protocol | stays (user rule: lambda.exe may hold globals) |
| **D** | Env-gated diagnostics/stats — profile counters, atexit reports | may stay process-global; must be race-safe and never feed semantics (RG7) |
| **L** | Library infra — log, mempool, mem_context, memtrack | stays only where process scope is intentional and no context pointer/state is retained |
| **X** | Vendored third-party (`lib/sqlite`, tree-sitter) | out of scope |

The end-state invariant, checkable by grep + lint:

> In `lambda/runtime/`, `lambda/core/`, `lambda/js/`, `lambda/module/`, guest runtimes:
> **no mutable file-scope variable tagged R or C remains.** During migration every remaining
> mutable global is tagged B/C/I/T/D; at exit only B/I/T/D remain, e.g.
> `// global-ok: I (frozen registry)`.
> A tag is a reviewed ownership claim, not a lint escape hatch: I must be frozen and safely
> published; D must not affect semantics; T must not carry context-owned data.

---

## 3. Design decisions (RG ledger)

### RG0 — `EvalContext` is the long-lived isolate identity

Today `EvalContext` is used for several different things: `Runner::context` is embedded in a
short-lived runner, cleanup builds a stack temporary containing only a heap, timers fabricate a
borrowed `runtime_ctx`, and guest/module entrypoints construct activation-local contexts. That
is compatible with process globals because the real state lives elsewhere; it is incompatible
with capsules owned by "the context".

End state:

- A logical isolate has one **canonical, long-lived `EvalContext`**. `Runtime` owns compile
  artifacts and owns or indexes its live contexts; a context owns its heap-facing runtime state,
  scheduler binding, and capsules.
- `Runner`, timer callbacks, module entrypoints, cleanup, and guest activations **borrow a
  canonical context**. They do not create a partial `EvalContext` merely to make TLS-dependent
  helpers work.
- `EvalContextScope` (name illustrative) saves the previous `context`, binds the canonical
  context for a native/JIT entry, and restores it on every normal exit. Recovery checkpoints
  also snapshot/restore the binding explicitly; `siglongjmp` cannot rely on a C++ destructor.
  Async records retain a stable context handle/control block plus a generation token, not a raw
  pointer whose token check would dereference freed storage. A callback for a destroyed or
  replaced context is suppressed, never rebound to a look-alike heap.
- Truly standalone paths (`convert`, standalone guest execution) create a complete minimal
  context through `eval_context_init` / `eval_context_destroy`. Raw `memset`, copying, or
  partial stack fabrication of an initialized `EvalContext` is forbidden.
- `Runtime` and `EvalContext` are not synonyms: `Runtime` owns context-independent compile and
  module artifacts; `EvalContext` owns one isolate's instantiated semantic state. A Runtime may
  serve more than one context only where the shared artifacts satisfy the MT2 contract (§5.5).
- A Runtime outlives every context it indexes. Context destruction first closes its stable
  handle, cancels/drains records that retain it, and releases context module instances; only
  after all contexts are gone may Runtime-owned code and metadata be destroyed.

This makes `persistent_last_error` mergeable into the canonical context: its current separate
TLS lifetime exists only because `Runner::context` is stack-owned. Establish this ownership
model before moving the first global.

The current `Runtime` mixes these owners too. Its `heap`, `name_pool`, scheduler, DOM/result
arena, JS bootstrap/runtime-used flags, and other instantiated-run fields become context-owned
(or per-context entries); compile options, source/module indexes, and sealed artifacts remain
Runtime-owned. Transitional Runtime pointers may alias a designated context but are non-owning
and cannot be used once one Runtime serves multiple contexts.

| State kind | Owner |
|---|---|
| Sealed code, AST/type metadata, immutable module descriptors | `Runtime` |
| Heap, globals, module instances, event loop, DOM/guest state | canonical `EvalContext` |
| Current call/eval/exception registers | activation portion of the context capsule, owner-thread only |
| Atomics/SharedArrayBuffer agent coordination | explicit shared `JsAgentCluster` |
| Stack guard, signal checkpoint, side-stack backing | thread infrastructure |
| CLI/REPL/batch protocol | shell |

### RG1 — Exactly one bootstrap TLS root

`__thread EvalContext* context` ([runtime-state.cpp:6](../lambda/runtime/runtime-state.cpp)) is
the *only* sanctioned runtime TLS root. It is execution authority, not semantic storage: it
identifies which canonical context the current native/JIT activation may access. Chicken-and-egg
makes one such root irreducible.

Every entry from shell, worker, event loop, retained callback, guest runtime, or host API must
install an `EvalContextScope` before it can execute code or register roots. Nested entries restore
the previous binding. Functions may assume `context != NULL` only when their API contract says
they execute inside such a scope; init/compile helpers must accept an explicit owner instead.

`input_context` / `input_allocation_context` ([input.cpp:27](../lambda/input/input.cpp)) are a
second and third root today. They exist because input parsing can run without a full
`EvalContext` (the `convert` path). Decision: fold them as fields —
`EvalContext::input_ctx` for runtime-initiated parsing — and have the standalone convert path
construct a minimal `EvalContext` (it already builds a `Context` with pool/arena; the marginal
cost is nil). Until that lands they are tolerated as transitional roots, tagged B-transitional.

### RG2 — Capsule pointers on the canonical context, not inline megastructs

`EvalContext` grows **opaque capsule pointers**, lazily allocated:

```c
struct EvalContext : Context {
    ...existing...
    EvalContextHandle* handle;    // stable async lifetime/generation control block
    JsRuntimeState* js_state;     // JS "registers" + eval bridge + intrinsics (NULL until JS runs)
    JsEventLoop*    js_loop;      // uv loop + microtask/nextTick/rAF rings + timers (K3)
    JsDomState*     js_dom;       // document bindings + live-collection caches
    JsModuleState*  js_modules;   // ESM/CJS module tables, preamble, dynamic-func cache
    JsAsyncState*   js_async;     // promises, generators, async contexts, domains
    JsHostState*    js_host;      // assert/node:test, buffer/stream/http protos, fetch, permission
    LambdaRtState*  rt_state;     // Lambda-side R items (current_vargs, shape epoch, edit bridge…)
    Runtime*        runtime;      // back-pointer for module/script lookup (new)
};
```

Rules:
- The JIT-visible `Context` **prefix stays frozen** — capsules are appended on the
  `EvalContext` C++ side only, so no MIR ABI change and no `lambda.h` layout churn.
- Capsules are **allocated once per canonical context and never reallocated** — `JsRootRange` slots and
  every GC-registered `Item` array must have addresses that "outlive every heap epoch"
  (js_runtime_state.hpp's own contract). No growable containers for rooted ranges.
- Capsule creation is explicit at execution boundaries (`eval_context_ensure_js_state`, etc.),
  never hidden in a macro, signal/recovery handler, GC callback, or allocator fast path.
- `eval_context_reset_run_state`, `eval_context_replace_heap`, and `eval_context_destroy` define
  separate lifecycle operations. Destruction clears/unregisters precise roots and drains/releases
  async owners before the heap or capsule storage is released.
- Existing `memset(..., sizeof(EvalContext))` and partial temporary contexts must be removed or
  confined to pre-init storage as part of RG0. Once initialized, a context is non-copyable and
  can only be reset through lifecycle APIs.

### RG3 — The macro pivot is the migration mechanism

`js_runtime_state.hpp` already funnels ~40 legacy names through defines:
`#define js_exception_pending (js_runtime_state.exception.pending)` etc. That makes step one
mostly mechanical after RG0/RG5: change the expansion root once —

```c
#define js_rt_state (*context->js_state)          // boundary-established, no per-use check
#define js_exception_pending (js_rt_state.exception.pending)
```

— and hundreds of use sites migrate without touching them. The same technique generalizes to
file-scope statics: group each file's statics into a named file-capsule struct, alias the old
names with `#define`, hang the capsule off the right `EvalContext` capsule. Mass-rename comes
later (or never); ownership moves now.

Before the pivot, classify every macro use as execution-side R or compile-side C. Compile/init
code must not acquire an implicit current context. A checked boundary operation establishes
`context != NULL && context->js_state != NULL` before JS execution; the compatibility macros do
not repeat that check. Debug boundary assertions make ownership bugs visible during migration.
Native hotspots progressively take/hoist `JsRuntimeState*` explicitly so repeated macro
expansions do not reload TLS and the capsule pointer.

### RG4 — `_lambda_rt`: from process global to hidden context argument

The one global with a hard constraint: MIR cannot express TLS, and the import mechanism bakes
`&_lambda_rt` into generated code at `MIR_link` time
([sys_func_registry.c:1708](../lambda/runtime/sys_func_registry.c), [mir.c:21](../lambda/runtime/mir.c)).
A `__thread _lambda_rt` would resolve to the linking thread's slot — silently wrong on any
other thread. Options:

| Option | Mechanism | Cost | Concurrency |
|---|---|---|---|
| A (status quo) | plain global + save/restore discipline (`prev_lambda_rt` dances in [js_event_loop.cpp:465](../lambda/js/js_event_loop.cpp), [js_mir_module_batch_lowering.cpp:8295](../lambda/js/js_mir_module_batch_lowering.cpp)) | zero | single executor only |
| B (entry call) | JIT'd function calls `Context* lambda_rt_current(void)` once at entry, caches in a local reg | repeated native call/TLS lookup at every function entry | correct but not an acceptable performance end-state |
| C (hidden arg) | thread `Context*` as a hidden parameter through every JIT↔JIT call (generalizes `main(Context*)`) | ABI/codegen change; steady-state context stays in a call argument/register | correct for N threads; no lookup/call added per function |

**Decision:** implement **C** as the migration end-state. Option B may exist only as a temporary
diagnostic flag while validating codegen and must not be the merged steady-state path. Host,
event-loop, and async entry boundaries already resolve the canonical context once through
`EvalContextScope`; their invoke adapter passes that pointer into generated code. JIT↔JIT calls,
closures, methods, dynamic dispatch, and per-callee invoke entries forward the same hidden
argument. Hot native helpers that need context take it explicitly; context-free helpers keep
their existing ABI.

Every direct native read/write of `_lambda_rt`, every `&_lambda_rt` ABI exposure (including Jube
frame slots), and every `prev_lambda_rt` save/restore migrates to the explicit argument or
boundary scope. The global is deleted when that inventory is empty. This ABI work composes with
the fn->invoke per-callee entry design and is benchmarked for register pressure/code size before
landing. The same MT2 audit covers the per-module BSS cells
(`_mod_consts_ptr`, `_mod_type_list_ptr`) — those hold compile-time pointers (tag C, per-module,
immutable after publication) and may remain only under the publication contract in §5.5.

### RG5 — Rooted-Item address stability

Everything GC-rooted that moves must keep the `JsRootRange` ownership contract: registration
is owned by the range, against an **explicit collector owner**, with re-registration whenever
that collector is replaced. Address stability and collector registration are separate
invariants: a capsule slot may keep the same address while its old collector and root table
have already been destroyed.

Change the root API shape so registration/unregistration receives `EvalContext*`, `Heap*`, or
`gc_heap*`; it must not silently choose `context->heap->gc` from TLS. A `JsRootRange` records the
collector identity/generation it is registered with. `roots_epoch` (or an equivalent owner token)
remains until the collector lifetime itself guarantees registration; capsule lifetime alone is
not sufficient. Registration happens at capsule/module-instance construction and heap
replacement, before execution can publish an Item into the range. Repeated operations such as
stack push, cache fill, or module-variable store do not call `ensure_registered` or compare a
root epoch; a debug-only invariant check may remain outside release hot paths.

Capsules expose the same semantic `js_root_range_*` operations, with `range->slots` pointing into
context-owned storage. The root-range *registry* itself
(`js_root_range_registry[]`, [js_runtime_state.cpp:11](../lambda/js/js_runtime_state.cpp)) moves
into `JsRuntimeState` so reset-all is per-context. Registered Item storage never moves after
publication. Registry metadata (pointers/descriptors, not Item slots) may grow on the context
owner thread; cross-thread requests enqueue work to that owner instead of mutating the registry
directly. Dynamic modules allocate new fixed slabs instead of resizing a rooted slab. Either
prove the current fixed registry bound or replace it—overflow must be detected before the new
range publishes any Item, not opportunistically afterward.

### RG6 — What legitimately stays process-global (tag I)

Constructed during process init, safely published, then read-only and independent of any
context. A catalog that must grow later uses locked/atomic append-only publication of immutable
records and is tagged I only if existing records and execution semantics never mutate. Catalog
lookup occurs at init/module-resolution boundaries and the resolved immutable record is cached;
repeated execution does not lock or poll the catalog:

- `sys_func_defs[]` / `jit_runtime_imports[]` / `func_map` hashmap ([mir.c:47](../lambda/runtime/mir.c)) — the JIT import symbol table.
- `sys_func_map` / `sys_func_name_set` ([build_ast.cpp:95](../lambda/runtime/build_ast.cpp)) — name→SysFuncInfo, init-once. (The *jube dynamic* records appended at manifest load are init-phase too, but guard with the specifier-catalog lock already present.)
- Type singletons `TYPE_NULL`…`TYPE_DECIMAL` ([lambda-data.cpp:28](../lambda/core/lambda-data.cpp)) and `type_info[]` — immutable descriptors.
- Identity-marker statics whose *address* is the value: `js_typed_array_marker`, `js_array_iter_marker`, `TypeMap` markers (`js_computed_style_marker`, stylesheet/rule markers). Never written; keep.
- ASCII char table ([lambda-mem.cpp:217](../lambda/runtime/lambda-mem.cpp)) — init-once interned strings (verify the `String*` it holds are static-storage, not heap — they are, `ascii_char_storage`).
- Decimal **configuration templates** may be I, but live `mpd_context_t` objects are not:
  status/trap fields are mutable and current lazy-init flags are unsynchronized.
  `g_fixed_ctx`/`g_unlimited_ctx`/`g_bigint_ctx`
  ([lambda-decimal.cpp:23](../lambda/core/lambda-decimal.cpp)) become immutable safely-published
  templates copied into per-context decimal state (or local operation copies). No executed
  decimal operation receives a pointer to a shared mutable template.
- Jube static module registry + specifier index ([jube_registry.cpp:69,97](../lambda/jube/jube_registry.cpp)) — module *catalog* (I). But the node runtime *sessions* (`jube_node_runtime_sessions`, `jube_active_node_runtime_session`, async work queues, MIR cursor/state-token slots, `jube_active_guest_execution` TLS) are execution state → R.
- Input/format registries (`format_registry.cpp`, latex tables, css_properties tables) — I.
- Hook registrations set once by the embedder: `g_emit_fn`/`g_selection_fn` (radiant_event_hook), `g_heap_alloc_fn` (lambda-error), `g_gc_heap_node_release` — set-once function pointers; audit-only.

### RG7 — Diagnostics stay out, tagged

Env-gated stats (COW profile counters in lambda-eval, `g_js_call_stats_*`, MIR volume/phase
timing counters, scope/identifier counters, TA-set stats, dynfunc stats) aggregate across a
process run by design and feed atexit reports, not semantics. They stay process-global tagged D
— with the rule that **no semantic path may read them**. Anything that fails that rule (e.g. a
cache doubling as a counter) is R and moves. Repeated-path counters are per-context/per-thread
ordinary fields and merge into the process report only at a quiescent/reporting boundary.
Process-global atomics or mutexes are not added to hot paths; "diagnostic only" does not excuse
either a data race or unnecessary cache-line contention.

### RG8 — Event loop per context

`g_loop` + prepare/check handles ([uv_loop.c:12](../lib/uv_loop.c)), the microtask/nextTick/rAF
rings, timer table, virtual/mock clocks ([js_event_loop.cpp:63–392](../lambda/js/js_event_loop.cpp))
become a `JsEventLoop` capsule = K3's "one loop per context", owned and pumped only by the
context thread (JT6). The event-loop SIGSEGV drain guard
([js_event_loop.cpp:1766](../lambda/js/js_event_loop.cpp)) converts to the shared recovery kit
(Js_Thread P2.2), not to the capsule.

This requires an API change in `lib/uv_loop.c`, not only moving `g_loop`: loop, prepare/check
handles, drain callbacks, and active flags become fields of an explicit loop instance passed to
`lambda_uv_*` operations (or recovered from `uv_handle_t::data` inside callbacks). No libuv
callback may consult a process-global "current loop". Timer/request records retain a stable
handle to their owning canonical context plus generation and enter it through
`EvalContextScope` only after successful validation.

### RG9 — Guest runtimes get the same treatment

`py_runtime.cpp` TLS block (session, exception slots, module vars, args stack), `py_async` coro
slots, `bash_runtime.cpp`'s ~67 globals, rb — each becomes a guest capsule pointer off
`EvalContext` (`py_state`, `bash_state`), allocated when the guest first runs. Same GC-root
rules. Their TLS today is *accidentally* correct (single thread); capsules make it actually
correct.

### RG10 — Thread infra stays `__thread` (tag T)

- lambda-stack guard family (`_lambda_stack_limit/base`, `_lambda_recovery_point/_armed`,
  overflow flag) — must be reachable from a signal handler with no assumptions; per-thread by
  construction (JT4). Stays.
- `side_stack.c` regions — per-thread commit arenas whose base/top pointers are *donated into*
  `Context` fields on entry. The backing stays TLS; the context view is already fields. Correct
  as-is; document the donation contract.
- Shell recovery jmp_bufs in `main.cpp` (`batch_crash_jmp`, `batch_timeout_jmp`,
  `mir_error_jmp`) — tag S, and per Js_Thread P1.3 they become `static __thread` when the batch
  worker lands. Signal *dispositions* stay process-global (OS semantics).

### RG11 — Reset becomes construction

After migration, hot-context reuse (batch) keeps its perf but changes shape: instead of
resetting scattered statics, the runner reuses the canonical context and calls lifecycle APIs
with distinct contracts:

- `eval_context_reset_run_state()` clears activation/transient state while keeping the same heap
  and valid root registrations;
- `eval_context_replace_heap()` first closes callback admission and advances the handle
  generation, then drains/cancels async owners, clears heap-bound Items, unregisters roots from
  the old collector, replaces the heap, and registers fixed ranges with the new collector;
- `eval_context_destroy()` performs full capsule teardown before destroying the collector and
  context storage.

Capsules are allocated once and retain stable addresses. Collector-registration tokens remain
lifecycle metadata checked during heap replacement, not repeated slot access. Other heap-bound
epochs remain only where semantics truly require an owner-thread version guard; module-variable
and IC slabs are bulk-cleared/rebuilt so they do not pay a cross-run epoch check per access.

### RG12 — Compile-time state belongs to `Runtime`, not `EvalContext`

Context-dependent runtime state lives on `EvalContext`; compile-side state has a different
natural owner — the `Runtime` object ([transpiler.hpp:53](../lambda/runtime/transpiler.hpp))
or a per-compilation transpiler object. A Runtime may be shared by contexts only for immutable
context-independent artifacts that are complete before one-time publication:

- `g_active_mir_ctx`, `g_active_js_transpiler`, `g_active_mir_transpiler`,
  `g_active_js_owned_source`, the active-transpile stack
  ([js_mir_module_batch_lowering.cpp:113–126](../lambda/js/js_mir_module_batch_lowering.cpp)) —
  "current compile" pointers. They belong to the per-compilation object. Crash/timeout cleanup
  reaches that object through `context->runtime` and its active compilation record; it does not
  introduce a second TLS root.
- `module_mir_contexts[]` / name pools / ast pools / source buffers ([js_mir_module_batch_lowering.cpp:15–19](../lambda/js/js_mir_module_batch_lowering.cpp)), `js_source_runtime`, `js_dynamic_func_counter`.
- Preamble mode flags + output (`g_jm_preamble_*`, [js_mir_entrypoints_require.cpp:383–385](../lambda/js/js_mir_entrypoints_require.cpp)), eval-preamble entries ([js_mir_eval_lowering.cpp:27–29](../lambda/js/js_mir_eval_lowering.cpp)).
- `dynamic_import_map` (already `__thread`, [mir.c:166](../lambda/runtime/mir.c)) → per-compilation
  state while linking; the frozen resolved import table may publish onto `Runtime`.
- `tls_parser` ([runner.cpp:310](../lambda/runtime/runner.cpp)) → `Runtime::parser` already exists; reconcile.
- `g_mir_interp_mode` ([mir.c:26](../lambda/runtime/mir.c)) — env-derived config read at compile time; tag I (set once at startup).

`EvalContext::runtime` (new back-pointer, RG2) gives execution-side code that needs script
lookup its path without globals. Split the current module `registry_map`
([module_registry.cpp:36](../lambda/runtime/module_registry.cpp)): compiled module metadata,
sealed MIR contexts, and immutable export descriptors belong to `Runtime`; instantiated
`namespace_obj` Items, loading/initialized state, and their precise roots belong to the
context's module capsule. A Runtime registry must never retain an Item rooted in one context's
heap and expose it to another.

### RG13 — Mutation is context-local; sharing is immutable

The migration uses three mutation classes:

1. **Shared and immutable after publication.** Sealed MIR/code, module ids, const/type pointers,
   compiled metadata, and process registries are built privately and published once at a
   module/context boundary. A Runtime module lock (or equivalent one-time release/acquire
   publication) is allowed there. After publication, execution uses ordinary reads—no READY
   checks, atomics, locks, or refcount traffic per call.
2. **Mutable and context-owned.** IC entries, module-variable slabs, globals, call/eval registers,
   exceptions, queues, timers, DOM caches, guest state, and diagnostic counters live in the
   canonical context. The context has one owner thread at a time, so these use ordinary
   loads/stores with no synchronization. A context may move threads only through a quiescent
   handoff after the old owner stops executing it.
3. **Intentionally cross-context.** `JsAgentCluster`, SharedArrayBuffer/Atomics waiters, and
   similar language-visible coordination use synchronization inside those explicit APIs. Their
   locks/atomics do not leak into unrelated property access, calls, IC probes, or event-loop
   checkpoints.

If a proposed migrated field is written repeatedly and would require synchronization, its owner
is wrong: move it into the context (or a context-owned per-module slab). Runtime sharing is for
immutable artifacts, not mutable execution caches.

The common module shape is:

```c
struct RuntimeModule {          // Runtime-owned; immutable after publication
    uint32_t module_id;
    uint32_t var_slot_count;
    uint32_t ic_site_count;
    ...sealed code/type/const metadata...
};

struct ContextModuleState {     // EvalContext-owned; owner-thread mutation
    RuntimeModule* module;
    Item* vars;                 // fixed rooted slab
    IcEntry* ic_entries;        // fixed non-shared slab
    ...namespace/instance state...
};
```

`EvalContext` indexes `ContextModuleState*` by frozen module id. Module instantiation allocates
both slabs once and registers Item ranges before execution. Generated code loads this state only
for functions that use it; all repeated mutation stays within the context-owned structure.

---

## 4. Inventory

Method: sweep of `__thread`/`thread_local` plus file-scope mutable declarations across
`lambda/` and `lib/` (excluding vendored code). Counts below are declarations found by the
sweep; the named rows are the load-bearing ones. Anything not listed follows its file's row.

### 4.1 The bootstrap + thread-infra set (B / T)

| Variable | Where | Tag |
|---|---|---|
| `__thread EvalContext* context` | runtime-state.cpp:6 | **B** — the root; stays |
| `__thread Context* input_context`, `input_allocation_context` | input.cpp:27–28 | B-transitional → `EvalContext` field (RG1) |
| `__thread LambdaError* persistent_last_error` | runner.cpp:433 | **R** — duplicates `EvalContext::last_error`; merge |
| `__thread List* current_vargs` | lambda-eval.cpp:5445 | **R** → `rt_state` (it is live across JIT calls — verify no JIT-baked address; it's accessed via helpers, safe) |
| lambda-stack guard family | lambda-stack.cpp:42–59 | **T** stays |
| side-stack regions | side_stack.c:29–30 | **T** stays (donation contract, RG10) |
| `__thread SysinfoCache* g_cache` | sysinfo.cpp:88 | R → context capsule; cached returned Items/allocations must follow the owning heap |
| `tls_parser` | runner.cpp:310 | C → `Runtime` |

### 4.2 Lambda core runtime (tag R unless noted)

| Variable(s) | Where | Target |
|---|---|---|
| `Context* _lambda_rt` | mir.c:21 | RG4 — hidden context argument; delete after generated and native users migrate |
| `g_lambda_shape_epoch` | lambda-data-runtime.cpp:2271 | context/module-instance generation; remove per-probe shared epoch after IC slabs bulk-clear on heap replacement |
| MIR member/store IC BSS cells | transpile-mir.cpp:7546 | dense per-context `ContextModuleState::ic_entries` slab (RG13/§5.5) |
| decimal contexts + lazy-init flags | lambda-decimal.cpp:23,1253 | immutable templates (I) + per-context live copies (R); no shared mutable `mpd_context_t` |
| render_map statics (`s_render_map`, `s_source_doc_root` — a rooted Item!, reverse map, recorder) | render_map.cpp:18–68 | `rt_state` / render capsule |
| template state map | template_state.cpp:16 | `rt_state` |
| edit bridge (`s_editor`, `s_editor_input`) | edit_bridge.cpp:19–20 | `rt_state` |
| concurrency attach points (`attached_scheduler`, promise fn hooks, `task_handle_brand`) | concurrency.cpp:164–168 | scheduler already per-context (`EvalContext::scheduler`); hooks are set-once → I; `attached_scheduler` → R merge into context |
| `g_dry_run` | lambda-proc.cpp:26 | duplicate of `Runtime::dry_run` — merge (C) |
| `g_safety_analyzer` | safety_analyzer.cpp:19 | C → `Runtime` |
| runner profile arrays + mutexes | runner.cpp:136–143 | per-context/thread D counters; merge under a reporting-boundary lock |
| `scripts_mutex`, `registry_map` | runner.cpp:321, module_registry.cpp:36 | lock/index → `Runtime`; instantiated namespace Items/loading state → context module capsule (RG12) |
| COW profile counters | lambda-eval.cpp:6158–6188 | per-context/thread D counters; no atomic increments in the evaluated path |
| `g_page_size` | pack.cpp:22 | I (cached syscall) |
| gc scalar tag counters | gc_heap.c:53 | D |
| ascii char table | lambda-mem.cpp:217–219 | I |

### 4.3 The JS state capsule — already consolidated, wrong owner

`JsRuntimeState js_runtime_state` ([js_runtime_state.cpp:6](../lambda/js/js_runtime_state.cpp),
struct at [js_runtime_state.hpp:177](../lambda/js/js_runtime_state.hpp)) — exception state,
`current_this`/`new_target`/super stacks, pending call args, module_vars[], regexp last-match,
intrinsic prototype roots, the whole eval bridge (source/bridge/local, ~600 rooted Items).
**This is the single biggest win**: it is one struct, one extern, and a macro layer already
fronts it. The JS-state phase moves it wholesale to the canonical `context->js_state` via the
checked RG3 pivot.
`js_with_stack_state` (extern in the same header, backing slots in js_globals.cpp:15906) moves
with it — note the header comment says dispatch reads it as a *load not a call*; keep that
property: `context->js_state->with_stack.depth` is still one load chain, measure per §6.

### 4.4 JS runtime file-locals (tag R, → capsules per RG2)

Declaration counts from the sweep (mutable file-scope only, functions excluded):

| File | Count | What / target capsule |
|---|---|---|
| js_runtime.cpp | 117 | proto caches (`js_math_object`, iterator/generator protos), template registry, generators[], promises[] + unhandled queue, async contexts, domains, modules[] + CJS tables, private-home class, global-var module bindings, call depth/limit, test262 agent tables → split across `js_state` (protos/registers), `js_async`, `js_modules`. **Special:** `js_262_agent_*` + `js_atomics_waiters` are *cross-agent by design* → per-agent-cluster struct with lock, not per-context (§5.3) |
| js_globals.cpp | 82 | ctor/builtin caches, typed-array protos + snapshots, global lexical bindings, `js_global_this_obj`, window.event, with-stack slots, console count/timer tables, uri/charcode one-entry caches, symbol registries → `js_state` (intrinsics/global-env) and `js_host` (console) |
| js_dom.cpp | 38 + 30 TLS | document proxy/view/title/fonts Items, designMode, active element; TLS live-collection caches, foreign-doc/iframe caches, prompt queue, pending iframe loads → `js_dom` capsule (per-document sub-structs keyed off DomDocument) |
| js_event_loop.cpp | 38 | rings, timer table, clocks, mock scheduler, shutdown flags → `js_loop` (RG8) |
| js_stream.cpp | 47 | interned key Items + protos + hwm defaults → `js_host` (keys are per-heap Items — must be per-context; hwm defaults are I) |
| js_assert.cpp | 23 | assert namespaces/instances, node:test hooks/queues/counters → `js_host` |
| js_mir_eval_lowering.cpp | 21 | dynfunc cache + stats, eval template counter, preamble entries → cache → per-context eval/module capsule; stats → per-context D merged at report; preamble compile state → C |
| js_mir_entrypoints_require.cpp | 20 | preamble flags (C), CJS module stack/objects + roots gc (R → `js_modules`), phase timing (D) |
| js_typed_array.cpp | 15 | markers (I), TA-set stats (D), atomics waiters (§5.3) |
| js_net / js_tls / js_crypto / js_fs / js_http(s) / js_dns / js_fetch / js_readline / js_clipboard | ~70 total | namespace/proto Items, response tables, pending work, `g_fetch_base_dir` → `js_host`; fetch/net pending queues belong to `js_loop` lifetime |
| js_dom_events.cpp | 14 + 3 TLS | propagation flags, listener tables → `js_dom` |
| js_dom_observers.cpp | 4 | observers[] + delivery flag → `js_dom` |
| js_permission.cpp | 8 | process policy may be I only if frozen/safely published and intentionally identical for every context; per-isolate grants or dynamic permission state → `js_host` |
| js_globals.cpp perf TLS (`js_performance_*`) | 7 TLS | → `js_loop` (clock state) |
| js_history.cpp | 2 TLS | → `js_dom` |
| js_scope.cpp / js_exec_profile.cpp | 27 | counters → per-context/thread D, merged only at report |
| js_runtime_state.cpp root-range registry | 2 | → `js_state` (RG5) |
| js_runtime_value / js_property_attrs / js_class etc. | ~15 | small caches → `js_state` |

### 4.5 Jube + guests + modules

| File | Notes |
|---|---|
| jube_registry.cpp (21) | frozen catalog/index → I; compile cursor/lowering state → per-compilation/Runtime; node sessions, async work, state tokens, active guest execution → context guest capsule even when they span evaluations |
| py_runtime.cpp (9 TLS + 9), py_async | → `py_state` capsule (RG9) |
| bash_runtime.cpp (67), bash_builtins, bash_redir | → `bash_state` capsule |
| module/node_core/* (~30 total) | namespace/proto caches → `js_host`-style capsule per module set |
| module/radiant/radiant_dom_bridge.cpp (7 TLS + owner-thread assert) | wrapper cache already thread-owned; → `js_dom` capsule; keep owner assert |
| network_downloader.cpp (5), serve/ (TLS error buf) | session-scoped → R low priority |

### 4.6 Shell & lib (stays)

| Where | Tag |
|---|---|
| main.cpp (batch jmp/flags, cleanup-once flags, REPL) | S (jmp state → `__thread` per Js_Thread P1.3) |
| lib/log.c (13), lib/cmdedit.c (13) | L / S |
| lib/mem_context.c, lib/mempool.c, lib/memtrack.c | L (locked registries + teardown TLS) |
| lib/uv_loop.c (`g_loop`, prepare/check) | **R** — the one lib/ exception; → `js_loop` (RG8) |
| lib/sqlite, tree-sitter | X |

---

## 5. Hard cases

### 5.1 `_lambda_rt` (RG4)
Covered above. Implementation order:

1. establish canonical context entry/exit (`EvalContextScope`) and audit every native/JIT entry;
2. add the hidden-context ABI to direct calls, closures, methods, dynamic dispatch, and host
   invoke adapters behind a flag; A/C diff correctness, code size, and release performance;
3. migrate hot native `_lambda_rt` users to explicit context parameters and cold boundary users
   to `EvalContextScope`;
4. delete the MIR Direct import/global and every `prev_lambda_rt` save/restore.

A retained function pointer or async callback installs its owner context once at the boundary
and passes that context through the hidden argument. Generated callees never rediscover it from
TLS.

### 5.2 Rooted Items moving home
Every `static Item`/`Item[]` that moves must (a) land in never-reallocated capsule storage,
(b) register through `JsRootRange` against an explicitly supplied owning collector, (c) retain
a collector identity/generation token across heap replacement, and (d) unregister or invalidate
that registration before collector destruction. Do NOT move a rooted static and keep it
registered against a stale registry — the forced-GC sweep (`test/mir` P3 harness) is the gate
for every phase below.

Reset ordering is part of the contract: close callback admission and advance generation →
drain/cancel async owners → clear heap-bound Items → unregister native ranges → tear down the
collector → attach/register the replacement → reopen admission. A root helper that silently
consults TLS cannot prove this ordering and is not an acceptable end-state API.
All range registration/re-registration occurs in this lifecycle path or module-instance
construction, not in the repeated operation that writes the rooted slot.

### 5.3 Deliberately cross-thread state
`js_atomics_waiters` + agent slots and the test262 agent report queue implement *cross-agent*
semantics (Atomics.wait/notify across workers). Per-context is wrong for them. They become a
shared `JsAgentCluster` object referenced by each participating context, with explicit lifetime,
membership, locking, and wakeup/shutdown rules. Same for any future SharedArrayBuffer registry.
Shared semantic state is allowed because its sharing is language-visible and intentional, not
because it is convenient process storage.

### 5.4 Per-document vs per-context
The js_dom caches are keyed by document, cleared on document swap. Under RC2, context↔page is
1:1, so hanging them off `EvalContext` (in `js_dom`) is equivalent and simpler than per-DomDocument
storage — but keep sub-struct boundaries so a later per-document split is mechanical.

### 5.5 The multi-threaded MIR contract after `_lambda_rt` removal

Removing `_lambda_rt` cleans the *import* channel — afterward the resolver serves only immutable
native function addresses. What remains is the data hard-linked into generated code through
non-import channels. The migration's MT goals fix how each is resolved:

- **MT1** — generated MIR can be *interpreted* by multiple threads;
- **MT2** — generated *machine code* can execute on multiple threads;
- **MT3** — MIR generation/lowering itself parallelizes, one module/script per thread (**KIV**;
  enabler notes only).

**The MT2 contract: no context-dependent value may live at a code-baked address.** An address
baked into shared code may hold only immutable data that has been safely published. Shared
mutable execution caches are prohibited even when their payload is context-independent: making
them atomic/locked would add coherence traffic to repeated paths. Anything mutable or
context-dependent is reached through `rt` (the `Context*` each function holds after
`_lambda_rt` removal) and is owned by that context.

- **Module publication is synchronized once, never on execution.** The Runtime module registry
  reserves/builds a module privately, assigns its id, initializes immutable BSS, links/generates,
  seals it, then publishes READY under the module lock. A context resolves/pins that sealed
  module when its module instance is created and stores a direct pointer/id in its context-local
  module table. Function calls, variable access, and IC probes never consult the Runtime registry,
  test READY, increment a shared refcount, or take the module lock. Invalidation publishes a new
  sealed version; old code remains alive until contexts using it are torn down or explicitly
  rebound at a quiescent boundary.

- **`_gvar_*` module variables → per-context slabs.** Today one 2×Item BSS cell per top-level
  binding ([transpile-mir.cpp:14673](../lambda/runtime/transpile-mir.cpp)), GC-rooted by walking
  all modules against the current heap ([mir.c:492](../lambda/runtime/mir.c)), zeroed-then-
  re-rooted on batch reuse ([transpile-mir.cpp:16159](../lambda/runtime/transpile-mir.cpp)).
  New shape: `rt->module_states[mod_id]->vars[slot]`. `slot` is a baked constant — derived from
  the module's own source, so MIR-module-cache replay-stable. `mod_id` is **not** baked (a baked
  `Script->index` would go stale in cached modules): the Runtime module registry assigns it once
  while the module is private, initializes the module's `_mod_id` cell, seals the module, and
  publishes it. Executing contexts only read the frozen cell. A generated function that uses
  module variables materializes its cached `ContextModuleState*` at the entry label; functions
  that use neither module variables nor ICs pay no module-state load. Variable operations then
  use baked offsets and ordinary loads/stores. Cross-module `pub` access uses the exporter's frozen `_mod_id` and
  baked slot. Slabs are context-owned, registered as root ranges against their own heap →
  `walk_bss_gc_roots` and the reset dance are deleted.

  **Decision — JS module variables migrate to this common model.** JS currently has a separate
  `JsRuntimeState::module_vars` fallback array and dynamically switches an
  `active_module_vars` pointer; compiled JS embeds only a local slot number, while a
  `JsFunction` captures the selected array pointer. This is a legacy parallel module-state
  system, not an allowed direct-pointer exception. Each sealed JS compilation unit will receive
  a stable runtime module id and its bindings will live in that context's
  `module_states[module_id]->vars` slab. JS MIR will resolve/hoist that slab from its hidden
  `Context*` and inline compiler-proven slot accesses. `JsFunction` and saved-module records
  will retain the sealed module identity/activation information needed to select the same
  context-owned slab, rather than an `Item*` into a JS-private array. Dynamic eval, nested
  `require`, VM contexts, batch preambles, and async continuation re-entry must use the common
  activation lifecycle. This gives Lambda and JS one module-variable owner, one GC-root range,
  and one reset/destroy path per module instance. Note the naive fix — a
  `_mod_vars_ptr` cell holding "the" slab pointer — *violates* the contract (different value
  per context).
- **`_mod_consts_ptr` / `_mod_type_list_ptr` — legal only after ownership proof.** The pointed-to
  artifacts must be Runtime-owned, immutable for the executable lifetime, and initialized
  before module publication. Contexts never reinitialize these cells. Any type/const entry
  allocated from a replaceable context pool moves to a per-context slab or to Runtime-owned
  immutable storage first.
- **All mutable IC cells are per-context.** During compilation, each member/store/call IC site
  receives a dense module-local `site_id`; the sealed module records `ic_site_count` and the
  entry layout. Context module instantiation allocates one fixed `IcEntry[ic_site_count]` slab
  beside the module-variable slab. Each IC probe/update reacquires that context's slab base from
  its hidden `Context*`, then addresses `module_state->ic_entries[site_id]` using a baked offset
  and ordinary loads/stores. There is no lock, atomic, TLS lookup, helper call, shared epoch, or
  publication check in the IC path.

  Because the slab belongs to one context/heap generation, it may safely cache that context's
  `TypeMap*`, shape entries, offsets, and other non-Item pointers whose lifetime is bounded by
  the context. Heap replacement or module-instance reset bulk-clears/rebuilds the slab outside
  execution, eliminating cross-run shared epoch checks. Shape/version guards required by IC
  semantics remain ordinary owner-thread comparisons; they are not synchronization. Facts that
  are truly immutable compile metadata are baked/read directly and do not use a mutable IC.
  Measure the extra per-context memory and module-variable entry load; do not recover it by
  caching an IC base across calls or control-flow edges.
- **Codegen never mutates a *shared* `MIR_context`.** Shared module contexts are sealed after
  the eager pipeline: `MIR_link(…, MIR_set_gen_interface, …)` + `MIR_gen()` then
  `MIR_gen_finish` ([mir.c:403–475](../lambda/runtime/mir.c)) — all machine code exists before
  first execution. Never move shared modules to `MIR_set_lazy_gen_interface`; lazy first-call
  generation would mutate the shared context from an arbitrary executing thread.
  Runtime codegen on executing threads is *allowed* when confined to contexts the executing
  `EvalContext` exclusively owns — and JS `eval`/`new Function` already conform: each eval
  compiles into a fresh private `MIR_context`
  ([js_mir_eval_lowering.cpp:1729](../lambda/js/js_mir_eval_lowering.cpp)), reaching outward
  only through immutable channels (resolver-served helper addresses, sealed JIT function
  addresses). Private eval MIR contexts are transient — never module-cache-serialized — so
  eval codegen may bake context-local module identity only while the generated code cannot
  escape that context; escaped closures retain the owning canonical context/module identity.
  Consequences under MT2:
  the dynfunc source→function cache
  ([js_mir_eval_lowering.cpp:102](../lambda/js/js_mir_eval_lowering.cpp)) is per-context so
  eval/`new Function` cache lookup and mutation need no shared lock. The
  `jm_defer_mir_cleanup` list is per-EvalContext (tag R — eval-context lifetime is
  closure-escape-sensitive). **Dynamic `import()` is the one new case**: it compiles a
  module destined for Runtime-shared tables (`module_mir_contexts[]`), so it follows
  compile-private-then-publish-under-lock — lower and link in a thread-owned context, seal,
  then register under the module lock; each context caches the resulting immutable module
  pointer in its own module table. A repeated dynamic import first checks that owner-thread
  context table with ordinary access; only a true module miss enters Runtime publication.
  Execution of the imported module never revisits the shared lock or publication state.

**MT1 — interpretation.** The MIR interpreter *mutates* `MIR_context` state (frame arenas, FFI
shims): one `MIR_context` = one interpreting thread. Multi-thread interpretation is achieved by
**instantiation, not locking**: each interpreting thread loads its own `MIR_context` from the
serialized module (the L1 module-cache `MIR_write`/`MIR_read` path already exercises this).
The MT2 data contract is what makes clones semantically transparent — today, cloning would
fork `_gvar_` BSS state silently; after the migration no mutable state lives in the module to
fork. Each private clone's `_mod_id` cell is initialized before that clone is published to its
interpreting thread.

**MT3 — parallel lowering (KIV).** Half the shape exists: JS already gives each module its own
`MIR_context` (`module_mir_contexts[]`). Blockers are exactly the RG12 tag-C globals (active-
transpiler pointers, preamble flags → thread-confined per-transpiler objects) plus shared
compile pools, which get per-worker ownership rather than locks in lowering hot paths. Fan out
lowering one module per worker; `MIR_link` + cross-module resolution join and publish once on the
owner thread. Not scheduled; RG12 is designed so this becomes additive.

### 5.6 Crash/timeout cleanup paths
Recovery code (batch handler, watchdog) today reads `g_active_mir_ctx` etc. to clean up. After
RG12 it must reach the same via `context->runtime` — which the handler can do, since handlers
run on the executing thread and `context` is TLS. Where a handler can run with `context == NULL`
(early init), it must already do nothing — audit each. The signal handler itself performs only
the async-signal-safe recovery transfer; object cleanup and capsule mutation occur after control
returns to the normal recovery boundary.

### 5.7 Decimal contexts

`mpd_context_t` is operational state, not merely a precision configuration: status and trap
fields may be updated by decimal calls, and the current lazy-init booleans are process races.
Process init may build immutable configuration templates once. Each `EvalContext` copies the
fixed/unlimited/bigint template into context-owned decimal state (or each operation takes a
local copy) and passes only that private object to mpdecimal. Tests run fixed and bigint
operations concurrently and verify both results and status isolation.

---

## 6. Performance notes

- **No synchronization in steady-state execution.** Generated calls pass the hidden context
  argument; ICs/module variables use context-module slabs; queues/registers/caches are
  owner-thread fields. Inspect generated MIR/machine code to ensure no lock, atomic RMW,
  publication-state load, refcount update, or TLS helper call appears in these paths.
- **Hoist once, reuse often.** A generated function resolves
  `ContextModuleState* module_state = rt->module_states[mod_id]` in its prologue only when it
  contains module-variable or IC operations. Those operations share that pointer and baked
  offsets; functions needing neither pay nothing. Native loops similarly hoist the capsule
  pointer. Checked accessors, root registration, and lazy allocation run only at host/module
  boundaries.
- **Preserve direct context-local pointers.** JS function objects, closures, saved module
  records, promise/timer records, and DOM wrappers are context-owned; they may retain direct
  pointers into their context where lifetime permits. Do not replace those with global ids,
  locked maps, or atomic handles on invocation.
- **Exploit cleanup opportunities.** Per-context slabs remove shared `_gvar_*` root walks,
  process-global IC cache-line bouncing, save/restore dances, and global cache reset protocols.
  Heap replacement bulk-clears module/IC state outside execution instead of adding an epoch or
  synchronization check to every probe.
- **Measure cold and hot costs separately.** One-time module publication, context-module slab
  allocation, and invalidation may take a lock and are reported as startup/import costs. They
  are excluded from steady-state loops only after dedicated cold-start numbers confirm they
  remain reasonable. Track per-context slab memory as well as time.
- **Non-regression gate.** Use release builds only. Repeated Result-suite geo means plus focused
  function-entry, call-dispatch, exception-poll, module-variable, monomorphic/polymorphic IC, and
  event-loop benchmarks must show no statistically reproducible slowdown. A neutral aggregate
  cannot hide a hot-path regression; any confirmed regression blocks the phase until the
  ownership-preserving layout/codegen is improved.

### 2026-07-28 — Test262 realm lifecycle tuning

The matched release Test262 baseline run was reduced from **269.5s** to
**115.5s** by making Jube realm setup and teardown genuinely demand-driven.
Ordinary JS realms no longer attach a Jube/Node session during global or base
`process` construction. A session is attached only when a Jube global, a Jube
module specifier, or a Node-owned `process` method is actually used. Reset and
detach return directly when the realm never attached a live Jube session.

The post-change run completed all 42,889 discovered tests in 115.5s
(114.8s batched: 81.8s sync + 33.0s async), with zero actual baseline
regressions or failed tests. Two existing batch-killed TypedArray cases
recovered on individual retry and remain classified as non-fully-passing by
the harness.

This measurement rules out the two initially suspected steady-state causes for
this workload: the hidden `Context*` argument on generated JS calls, and the
ordinary owner-local accesses through `runtime->js_state` or
`runtime->module_states[module_id]`. Those accesses remain on the hot path,
yet the regression disappeared when only cold realm setup/teardown and lazy
Jube attachment were changed. It does not prove those accesses are free in
every workload; they remain subject to the focused call and module/IC gates
above.

### 2026-07-28 — JS inline-MIR state access audit

The current JS MIR emitter does **not** directly access `JsRuntimeState`,
`EvalContext::js_state`, or a `module_states` entry. The generated code uses
the hidden `Context*` only for base `Context` fields: side-root/side-number
frame management, the template-literal allocation pool, and the shared
double-bitcast scratch slot. Fresh release MIR dumps confirm those direct
offset loads/stores and contain no `JsRuntimeState` field offset.

JS module-variable operations are therefore not inline state accesses today:
their generated MIR calls native `js_get_module_var` and
`js_set_module_var`, whose TLS-backed active-module pointer is the next
separate profiling target. This is an implementation boundary, not a claim
that the native helpers are free.

### 2026-07-28 — Test262 module-variable helper profile

A count-only `release_profile` run of the same 40,261-script Test262 baseline
recorded **54,946,636** `js_get_module_var` calls and **20,989,868**
`js_set_module_var` calls: **75,936,504** calls total, or about **1,886 per
script**. The emitter also recorded 469,616 generated get call sites and
415,675 generated set call sites across the batch processes. This confirms
that these helpers are a meaningful next optimization target, unlike a
direct inline `JsRuntimeState` field load which current JS MIR does not emit.

The counters sit in the native helper bodies, so their call totals also
include the small number of native global-binding synchronization and
module-variable initialization writes. The run deliberately used counts
rather than per-call clocks, avoiding profiler timing overhead in this
75.9-million-call path; it establishes frequency, not the helpers' inclusive
time or a causal share of suite wall time.

### Proposed tuning — inline JS module-variable operations

The Test262 profile makes module-variable helpers an important integration
point: the 40,261-script baseline executed 75,936,504 helper calls, or about
**1,886 `js_get_module_var` / `js_set_module_var` calls per script**. The
decision is to inline compiler-proven JS module-slot loads/stores, but the
primary reason is ownership unification rather than shaving a helper call.

`EvalContext` already owns the generic module-variable slabs as
`module_states[module_id]->vars`. JS must use those slabs as its canonical
binding storage. `JsRuntimeState::module_vars`, `active_module_vars`, and its
JS-private module-variable count are transitional duplicates and should be
retired; JS-specific runtime state may retain module metadata, but not a
second mutable binding array.

Each sealed JS compilation unit needs the same stable `module_id` and
`LambdaModuleState` lifecycle used by Lambda MIR. At JS module instantiation,
prepare the context-owned slab with its sealed variable count and register
its GC roots through the common runtime API. In a generated JS function,
resolve the current module state from the hidden `EvalContext*` once in its
prologue, hoist `LambdaModuleState::vars`, and use baked slot offsets for
ordinary lexical/global loads and stores. Cross-module JS bindings resolve the
target module state by its stable id. Native and dynamic-index operations
remain checked helper calls, but those helpers select the same
`EvalContext`-owned slab rather than JS-private storage.

No `JsRuntimeState` field needs to be migrated merely to make the generated
MIR faster: the required variable storage already belongs under
`EvalContext`. The only possible new context field is a small active-JS-module
selector for native ABI paths that do not receive an explicit module state;
it is an execution-selection aid, not another array or owner. Its update,
save/restore, reset, nested `require`, eval, and Test262 batch-preamble
semantics must be centralized with module activation. The migration is
complete only when JS reset/destroy uses the common module-state lifecycle and
there is one GC-root registration and one authoritative value for every
module slot.

## 7. Migration phases

Each phase: green `make test-lambda-baseline` + `make test-radiant-baseline`, js-test-batch,
`make node-baseline` no-regress, forced-GC sweep, Result-suite perf gate (§6).

- **P0 — Contract, ownership ledger, lint.** Land this goal/taxonomy, enumerate canonical versus
  borrowed/temporary context construction sites, add the `// global-ok: <tag>` convention, and
  extend `make lint` to reject any new untagged file-scope mutable in runtime directories. The
  allow comment records a reviewed owner and freeze/race contract; it is not a generic waiver.
  Add a hot-path synchronization audit/allowlist: new locks, atomic RMWs, TLS context lookups,
  and publication-state checks in IC/call/module-var/event-loop paths fail review.
- **P1 — Canonical context + root lifecycle.** Add `Runtime` back-pointer, context
  init/reset/replace-heap/destroy APIs, `EvalContextScope`, owner generation, and explicit-owner
  root registration. Split Runtime's retained run/heap fields into context ownership; convert
  `Runner`, cleanup temporaries, timers, and guest/module activation scaffolding so initialized
  contexts are never copied or raw-`memset`. Add alternating-context, heap-replacement, and
  stale-callback tests before moving global state.
- **P2 — JsRuntimeState pivot.** Add checked JS entry-boundary setup, allocate the capsule there,
  move the root-range registry with it, and switch hot-context reset to the lifecycle API.
  Compile/init use sites are separated before the macro pivot; compatibility macros perform a
  direct dereference, and native hot loops/functions hoist or receive the state pointer
  explicitly. (Biggest value/effort ratio after P1.)
- **P3 — Event loop.** `JsEventLoop` capsule: explicit-instance `lib/uv_loop` API + rings +
  timers + clocks (RG8, K3). Timer/request records retain canonical owner+generation. Do the
  drain-guard conversion with it (Js_Thread P2.2 alignment).
- **P4 — JS file-local sweeps.** File-capsule structs per §4.4, in order: js_runtime.cpp,
  js_globals.cpp, js_dom.cpp (+TLS caches), js_modules cluster, js_host cluster (assert/stream/
  buffer/net/fs/…), observers/events. Move JS mutable ICs/dynfunc caches and diagnostics to
  context-owned slabs/fields with ordinary accesses. Mostly mechanical; §5.2 discipline
  throughout. Agent/atomics state → `JsAgentCluster` (§5.3).
- **P5 — Lambda core sweeps.** §4.2 rows; merge `persistent_last_error` into
  `EvalContext::last_error`; `current_vargs`; render/template/edit bridges; `input_context`
  fold (RG1); per-context decimal copies (RG6/§5.7). Replace MIR IC BSS with dense per-context
  module IC slabs and bulk invalidation; remove shared shape-epoch checks from probes.
- **P6 — Compile/module split.** RG12 set: active-transpiler pointers, sealed module MIR
  contexts, preamble state, dynamic-import build state, parser. Split Runtime-owned compiled
  module metadata from context-owned namespace/module instances, variable/IC slabs, and roots.
  Publish each sealed module once under the Runtime module lock; context instantiation caches a
  direct module pointer so execution never revisits publication state. This can proceed in
  parallel with late P4/P5 work after P1 contracts exist.
- **P7 — `_lambda_rt` hidden-context ABI.** Thread the context argument through Lambda MIR, JS
  MIR, direct calls, closures, methods, dynamic dispatch, and host adapters; migrate hot native
  users to explicit context parameters. Delete the MIR Direct global/import and every
  save/restore. Run a controlled two-owner-thread concurrency test against the same sealed
  generated code and enforce the function-entry/call-dispatch performance gate.
- **P8 — Guests.** py/bash/rb capsules and Jube execution sessions (RG9), using the same
  canonical-context and explicit-root contracts.

Exit criteria:

- the §2 lint invariant holds and no declaration tagged R or C remains file-scope;
- every I declaration has a freeze/publication argument, every D declaration is race-safe and
  absent from semantic branches, and every T declaration contains infrastructure only;
- initialized `EvalContext` objects are created/reset/destroyed only through lifecycle APIs;
- root registration always names an owner collector and survives repeated heap replacement;
- module publication is the only synchronization on ordinary module execution: generated and
  native hot paths contain no migration-added locks, atomic RMWs, READY checks, shared refcounts,
  or TLS context helper calls;
- every mutable IC and module-variable slot is context-owned and uses ordinary owner-thread
  loads/stores; module/heap reset invalidates them in bulk outside execution;
- two contexts run alternately with forced GC, async callbacks, closures, dynamic import, DOM
  swaps, and guest execution with zero cross-talk;
- after P7, two owner threads run separate contexts concurrently against shared sealed code and
  match solo results; run ThreadSanitizer or the platform race checker where supported;
- release Result-suite and focused hot-path benchmarks show no reproducible regression versus
  the pre-migration baseline; report cold module-publication time and per-context slab memory
  separately.

## 8. Out of scope

- Product scheduling of concurrent executors (RC2) — this doc includes a controlled concurrent
  correctness test, but scheduler integration, signals-per-thread (JT4/JT5), and production
  loop affinity (JT6) remain Js_Thread's program.
- Radiant/UI globals (`radiant/`) — separate audit; the UiContext/document side has its own
  ownership doc trail. The JS/Radiant bridge state named in this inventory remains in scope.
- Rewriting the C2MIR transpiler path (frozen per repository rule 14). Its compatibility
  `_lambda_rt` channel remains explicitly single-executor and unsupported for this concurrency
  contract; MIR Direct and native runtime paths must not read that compatibility slot.
