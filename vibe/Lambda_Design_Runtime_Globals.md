# Lambda Runtime Globals — Audit & Migration to EvalContext

**Date:** 2026-07-27
**Status:** Design / audit. No implementation started.
**Scope:** `lambda/runtime/`, `lambda/core/`, `lambda/js/`, `lambda/jube/`, guest runtimes (`py/`, `bash/`), `lambda/module/`, and the `lib/` infra they lean on.
**Relation to prior docs:** expands the global-state ledger in `vibe/Lambda_Js_Thread.md` §6.5 into a full inventory and migration design. The Js_Thread JT decisions (JT1 context-thread rule, JT4 per-thread recovery, JT6 loop affinity) are taken as given; this doc is the state-ownership side of the same program.

---

## 1. Goal

> Only `lambda.exe` (the CLI/batch shell in `lambda/main.cpp`) may hold process globals.
> The Lambda and JS runtimes run purely on `EvalContext`. Executed/interpreted code must not
> read or write any process global. All runtime process globals and TLS variables migrate to
> fields (or capsules) under `EvalContext`.

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

---

## 2. Classification taxonomy

Every file-scope mutable variable (global, `static`, `__thread`, `thread_local`) gets one tag:

| Tag | Meaning | Verdict |
|-----|---------|---------|
| **B** | Bootstrap root — the one pointer that *finds* the context | stays TLS (see RG1) |
| **R** | Runtime state — read/written while script executes | **migrate to EvalContext** |
| **C** | Compile-time state — parser/AST/transpiler/MIR-codegen | migrate to `Runtime` / transpiler object (RG12) |
| **I** | Init-once immutable registry — written during process init, read-only after | stays process-global (RG6) |
| **T** | Thread infrastructure — stack guard, side-stack backing, signal recovery | stays `__thread` (RG10) |
| **S** | Shell state — `lambda/main.cpp`, REPL, batch protocol | stays (user rule: lambda.exe may hold globals) |
| **D** | Env-gated diagnostics/stats — profile counters, atexit reports | may stay process-global; must never feed semantics (RG7) |
| **L** | Library infra — log, mempool, mem_context, memtrack | stays; already process-scoped by design |
| **X** | Vendored third-party (`lib/sqlite`, tree-sitter) | out of scope |

The end-state invariant, checkable by grep + lint:

> In `lambda/runtime/`, `lambda/core/`, `lambda/js/`, `lambda/module/`, guest runtimes:
> **no mutable file-scope variable tagged R remains.** Every remaining global is tagged
> B/C/I/T/D in a comment at its declaration, e.g. `// global-ok: I (init-once registry)`.

---

## 3. Design decisions (RG ledger)

### RG1 — Exactly one bootstrap TLS root

`__thread EvalContext* context` ([runtime-state.cpp:6](../lambda/runtime/runtime-state.cpp)) is
the *only* sanctioned runtime TLS variable. It is not state — it is the address of the state.
Chicken-and-egg makes it irreducible: something outside `EvalContext` must locate the current
`EvalContext`.

`input_context` / `input_allocation_context` ([input.cpp:27](../lambda/input/input.cpp)) are a
second and third root today. They exist because input parsing can run without a full
`EvalContext` (the `convert` path). Decision: fold them as fields —
`EvalContext::input_ctx` for runtime-initiated parsing — and have the standalone convert path
construct a minimal `EvalContext` (it already builds a `Context` with pool/arena; the marginal
cost is nil). Until that lands they are tolerated as transitional roots, tagged B-transitional.

### RG2 — Capsule pointers, not inline megastructs

`EvalContext` grows **opaque capsule pointers**, lazily allocated:

```c
struct EvalContext : Context {
    ...existing...
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
- Capsules are **allocated once per context and never reallocated** — `JsRootRange` slots and
  every GC-registered `Item` array must have addresses that "outlive every heap epoch"
  (js_runtime_state.hpp's own contract). No growable containers for rooted ranges.
- Root registration already targets the per-context collector
  (`heap_register_gc_root_range` → `context->heap->gc`, [lambda-mem.cpp:429](../lambda/runtime/lambda-mem.cpp)),
  so capsule roots register against the owning heap with no collector change.

### RG3 — The macro pivot is the migration mechanism

`js_runtime_state.hpp` already funnels ~40 legacy names through defines:
`#define js_exception_pending (js_runtime_state.exception.pending)` etc. That makes step one
mechanical: change the expansion root once —

```c
#define js_rt_state (*context->js_state)          // was: extern JsRuntimeState js_runtime_state
#define js_exception_pending (js_rt_state.exception.pending)
```

— and hundreds of use sites migrate without touching them. The same technique generalizes to
file-scope statics: group each file's statics into a named file-capsule struct, alias the old
names with `#define`, hang the capsule off the right `EvalContext` capsule. Mass-rename comes
later (or never); ownership moves now.

### RG4 — `_lambda_rt`: from process global to derived register

The one global with a hard constraint: MIR cannot express TLS, and the import mechanism bakes
`&_lambda_rt` into generated code at `MIR_link` time
([sys_func_registry.c:1708](../lambda/runtime/sys_func_registry.c), [mir.c:21](../lambda/runtime/mir.c)).
A `__thread _lambda_rt` would resolve to the linking thread's slot — silently wrong on any
other thread. Options:

| Option | Mechanism | Cost | Concurrency |
|---|---|---|---|
| A (status quo) | plain global + save/restore discipline (`prev_lambda_rt` dances in [js_event_loop.cpp:465](../lambda/js/js_event_loop.cpp), [js_mir_module_batch_lowering.cpp:8295](../lambda/js/js_mir_module_batch_lowering.cpp)) | zero | single executor only |
| B (entry call) | JIT'd function calls `Context* lambda_rt_current(void)` once at entry (native helper reads `__thread context`), caches in a local reg; all `rt` uses read the local | one call+move per function entry; replaces today's absolute-address load per use | correct for N threads |
| C (hidden arg) | thread `Context*` as an extra parameter through every JIT↔JIT call (generalizes `main(Context*)`) | best steady-state perf (a register); ABI change touching fn->invoke entries, closures, dynamic dispatch | correct for N threads |

**Decision:** phase it. Keep A while single-executor holds (it is not wrong today, and every JT
decision keeps it additive). Implement **B** as the concurrency-enabling step — it is local to
codegen (`transpile-mir.cpp` prologue + the JS lowering's `jm_ensure_import("_lambda_rt")`
sites) and deletes the global and all save/restore dances at once. Record **C** as the KIV
end-state, to be costed together with the next dynamic-call ABI revision (it composes with the
fn->invoke per-callee entry design). The same treatment covers the per-module BSS cells
(`_mod_consts_ptr`, `_mod_type_list_ptr`) — those hold compile-time pointers (tag C, per-module,
immutable after link) and are fine as-is.

### RG5 — Rooted-Item address stability

Everything GC-rooted that moves must keep the `JsRootRange` ownership contract: registration
owned by the range, re-registration on heap-epoch change, reset hooks registered once. Capsules
therefore expose the same `js_root_range_*` API, just with `range->slots` pointing into
context-owned storage. The root-range *registry* itself
(`js_root_range_registry[]`, [js_runtime_state.cpp:11](../lambda/js/js_runtime_state.cpp)) moves
into `JsRuntimeState` so reset-all is per-context.

### RG6 — What legitimately stays process-global (tag I)

Written only during init (or under a lock), content independent of any context:

- `sys_func_defs[]` / `jit_runtime_imports[]` / `func_map` hashmap ([mir.c:47](../lambda/runtime/mir.c)) — the JIT import symbol table.
- `sys_func_map` / `sys_func_name_set` ([build_ast.cpp:95](../lambda/runtime/build_ast.cpp)) — name→SysFuncInfo, init-once. (The *jube dynamic* records appended at manifest load are init-phase too, but guard with the specifier-catalog lock already present.)
- Type singletons `TYPE_NULL`…`TYPE_DECIMAL` ([lambda-data.cpp:28](../lambda/core/lambda-data.cpp)) and `type_info[]` — immutable descriptors.
- Identity-marker statics whose *address* is the value: `js_typed_array_marker`, `js_array_iter_marker`, `TypeMap` markers (`js_computed_style_marker`, stylesheet/rule markers). Never written; keep.
- ASCII char table ([lambda-mem.cpp:217](../lambda/runtime/lambda-mem.cpp)) — init-once interned strings (verify the `String*` it holds are static-storage, not heap — they are, `ascii_char_storage`).
- Decimal contexts `g_fixed_ctx`/`g_unlimited_ctx`/`g_bigint_ctx` ([lambda-decimal.cpp:23](../lambda/core/lambda-decimal.cpp)) — init-once mpd configs. *Caveat:* `mpd_context_t` carries status flags mutated by operations; audit that Lambda always uses local copies for status (it does — ops take `ctx` by pointer but status is read-and-cleared per call; verify during migration and either lock or per-context-copy if not).
- Jube static module registry + specifier index ([jube_registry.cpp:69,97](../lambda/jube/jube_registry.cpp)) — module *catalog* (I). But the node runtime *sessions* (`jube_node_runtime_sessions`, `jube_active_node_runtime_session`, async work queues, MIR cursor/state-token slots, `jube_active_guest_execution` TLS) are execution state → R.
- Input/format registries (`format_registry.cpp`, latex tables, css_properties tables) — I.
- Hook registrations set once by the embedder: `g_emit_fn`/`g_selection_fn` (radiant_event_hook), `g_heap_alloc_fn` (lambda-error), `g_gc_heap_node_release` — set-once function pointers; audit-only.

### RG7 — Diagnostics stay out, tagged

Env-gated stats (COW profile counters in lambda-eval, `g_js_call_stats_*`, MIR volume/phase
timing counters, scope/identifier counters, TA-set stats, dynfunc stats) aggregate across a
process run by design and feed atexit reports, not semantics. They stay process-global tagged D
— with the rule that **no semantic path may read them**. Anything that fails that rule (e.g. a
cache doubling as a counter) is R and moves.

### RG8 — Event loop per context

`g_loop` + prepare/check handles ([uv_loop.c:12](../lib/uv_loop.c)), the microtask/nextTick/rAF
rings, timer table, virtual/mock clocks ([js_event_loop.cpp:63–392](../lambda/js/js_event_loop.cpp))
become a `JsEventLoop` capsule = K3's "one loop per context", owned and pumped only by the
context thread (JT6). The event-loop SIGSEGV drain guard
([js_event_loop.cpp:1766](../lambda/js/js_event_loop.cpp)) converts to the shared recovery kit
(Js_Thread P2.2), not to the capsule.

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
resetting scattered statics, the runner either (a) reuses the context and calls one
`eval_context_reset_run_state()` that walks capsules, or (b) frees and reallocates capsules.
The per-cache `*_roots_epoch` re-registration checks collapse to capsule-create-time
registration — delete the epoch fields where the capsule lifetime now guarantees freshness
(keep `heap_epoch` itself; it also guards cross-epoch cached Items inside one context run).

### RG12 — Compile-time state belongs to `Runtime`, not `EvalContext`

The user rule says runtime state lives on `EvalContext`; compile-side state has a different
natural owner — the `Runtime` object ([transpiler.hpp:53](../lambda/runtime/transpiler.hpp))
that already holds scripts, parser, heap, name_pool. Tag C items move there (or to a
per-compilation transpiler object):

- `g_active_mir_ctx`, `g_active_js_transpiler`, `g_active_mir_transpiler`,
  `g_active_js_owned_source`, the active-transpile stack
  ([js_mir_module_batch_lowering.cpp:113–126](../lambda/js/js_mir_module_batch_lowering.cpp)) —
  "current compile" pointers; also consulted by crash/timeout cleanup, so once the batch worker
  exists they may need a `__thread` mirror for the recovery path (JT4) — decide at
  implementation; either is compatible with this doc.
- `module_mir_contexts[]` / name pools / ast pools / source buffers ([js_mir_module_batch_lowering.cpp:15–19](../lambda/js/js_mir_module_batch_lowering.cpp)), `js_source_runtime`, `js_dynamic_func_counter`.
- Preamble mode flags + output (`g_jm_preamble_*`, [js_mir_entrypoints_require.cpp:383–385](../lambda/js/js_mir_entrypoints_require.cpp)), eval-preamble entries ([js_mir_eval_lowering.cpp:27–29](../lambda/js/js_mir_eval_lowering.cpp)).
- `dynamic_import_map` (already `__thread`, [mir.c:166](../lambda/runtime/mir.c)) → `Runtime` field.
- `tls_parser` ([runner.cpp:310](../lambda/runtime/runner.cpp)) → `Runtime::parser` already exists; reconcile.
- `g_mir_interp_mode` ([mir.c:26](../lambda/runtime/mir.c)) — env-derived config read at compile time; tag I (set once at startup).

`EvalContext::runtime` (new back-pointer, RG2) gives execution-side code that needs script
lookup (module registry, import resolution at runtime) its path without globals; the module
`registry_map` ([module_registry.cpp:36](../lambda/runtime/module_registry.cpp)) moves onto
`Runtime`.

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
| `__thread SysinfoCache* g_cache` | sysinfo.cpp:88 | R → capsule (cheap) or D |
| `tls_parser` | runner.cpp:310 | C → `Runtime` |

### 4.2 Lambda core runtime (tag R unless noted)

| Variable(s) | Where | Target |
|---|---|---|
| `Context* _lambda_rt` | mir.c:21 | RG4 — derived register; delete global in option B |
| `g_lambda_shape_epoch` | lambda-data-runtime.cpp:2271 | `rt_state` (shape identity epoch — pairs with heap ownership) |
| render_map statics (`s_render_map`, `s_source_doc_root` — a rooted Item!, reverse map, recorder) | render_map.cpp:18–68 | `rt_state` / render capsule |
| template state map | template_state.cpp:16 | `rt_state` |
| edit bridge (`s_editor`, `s_editor_input`) | edit_bridge.cpp:19–20 | `rt_state` |
| concurrency attach points (`attached_scheduler`, promise fn hooks, `task_handle_brand`) | concurrency.cpp:164–168 | scheduler already per-context (`EvalContext::scheduler`); hooks are set-once → I; `attached_scheduler` → R merge into context |
| `g_dry_run` | lambda-proc.cpp:26 | duplicate of `Runtime::dry_run` — merge (C) |
| `g_safety_analyzer` | safety_analyzer.cpp:19 | C → `Runtime` |
| runner profile arrays + mutexes | runner.cpp:136–143 | D (env-gated profiling) |
| `scripts_mutex`, `registry_map` | runner.cpp:321, module_registry.cpp:36 | C → `Runtime` |
| COW profile counters | lambda-eval.cpp:6158–6188 | D |
| `g_page_size` | pack.cpp:22 | I (cached syscall) |
| gc scalar tag counters | gc_heap.c:53 | D |
| ascii char table | lambda-mem.cpp:217–219 | I |

### 4.3 The JS state capsule — already consolidated, wrong owner

`JsRuntimeState js_runtime_state` ([js_runtime_state.cpp:6](../lambda/js/js_runtime_state.cpp),
struct at [js_runtime_state.hpp:177](../lambda/js/js_runtime_state.hpp)) — exception state,
`current_this`/`new_target`/super stacks, pending call args, module_vars[], regexp last-match,
intrinsic prototype roots, the whole eval bridge (source/bridge/local, ~600 rooted Items).
**This is the single biggest win**: it is one struct, one extern, and a macro layer already
fronts it. Phase 1 moves it wholesale to `context->js_state` via the RG3 pivot.
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
| js_mir_eval_lowering.cpp | 21 | dynfunc cache + stats, eval template counter, preamble entries → cache/counter split: cache → C (`Runtime`), stats → D |
| js_mir_entrypoints_require.cpp | 20 | preamble flags (C), CJS module stack/objects + roots gc (R → `js_modules`), phase timing (D) |
| js_typed_array.cpp | 15 | markers (I), TA-set stats (D), atomics waiters (§5.3) |
| js_net / js_tls / js_crypto / js_fs / js_http(s) / js_dns / js_fetch / js_readline / js_clipboard | ~70 total | namespace/proto Items, response tables, pending work, `g_fetch_base_dir` → `js_host`; fetch/net pending queues belong to `js_loop` lifetime |
| js_dom_events.cpp | 14 + 3 TLS | propagation flags, listener tables → `js_dom` |
| js_dom_observers.cpp | 4 | observers[] + delivery flag → `js_dom` |
| js_permission.cpp | 8 | permission model flags/grants — per-process *policy* set at startup from CLI → **I** (written once pre-run), unless dynamic permission API lands |
| js_globals.cpp perf TLS (`js_performance_*`) | 7 TLS | → `js_loop` (clock state) |
| js_history.cpp | 2 TLS | → `js_dom` |
| js_scope.cpp / js_exec_profile.cpp | 27 | counters → D |
| js_runtime_state.cpp root-range registry | 2 | → `js_state` (RG5) |
| js_runtime_value / js_property_attrs / js_class etc. | ~15 | small caches → `js_state` |

### 4.5 Jube + guests + modules

| File | Notes |
|---|---|
| jube_registry.cpp (21) | catalog/index/manifest scan → I (locked); node runtime sessions, async work, MIR cursor/state slots, `jube_active_guest_execution` TLS → R (`Runtime` or context capsule — sessions span evaluations, so `Runtime`) |
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
Covered above. Implementation order: land B behind a flag, A/B diff on Result-suite (the extra
entry call is the only cost), then delete the global + every `prev_lambda_rt` save/restore.

### 5.2 Rooted Items moving home
Every `static Item`/`Item[]` that moves must (a) land in never-reallocated capsule storage,
(b) register through `JsRootRange` against `context->heap->gc`, (c) drop its private
`*_roots_epoch` once capsule lifetime subsumes it. Do NOT move a rooted static and keep it
registered against a stale registry — the forced-GC sweep (`test/mir` P3 harness) is the gate
for every phase below.

### 5.3 Deliberately cross-thread state
`js_atomics_waiters` + agent slots and the test262 agent report queue implement *cross-agent*
semantics (Atomics.wait/notify across workers). Per-context is wrong for them. They become a
shared `JsAgentCluster` object referenced by each participating context, with the lock they
already implicitly assume. Same for any future SharedArrayBuffer registry.

### 5.4 Per-document vs per-context
The js_dom caches are keyed by document, cleared on document swap. Under RC2, context↔page is
1:1, so hanging them off `EvalContext` (in `js_dom`) is equivalent and simpler than per-DomDocument
storage — but keep sub-struct boundaries so a later per-document split is mechanical.

### 5.5 The multi-threaded MIR contract (MIR-resident data; survives P6)

Removing `_lambda_rt` cleans the *import* channel — after P6 the resolver serves only immutable
native function addresses. What remains is the data hard-linked into generated code through
non-import channels. The migration's MT goals fix how each is resolved:

- **MT1** — generated MIR can be *interpreted* by multiple threads;
- **MT2** — generated *machine code* can execute on multiple threads;
- **MT3** — MIR generation/lowering itself parallelizes, one module/script per thread (**KIV**;
  enabler notes only).

**The MT2 contract: no context-dependent value may live at a code-baked address.** An address
baked into shared code may hold only data that is immutable or *identical for every context*
(making races value-idempotent and benign). Anything whose value differs per context is reached
through `rt` (the `Context*` each function holds after P6). Applied:

- **`_gvar_*` module variables → per-context slabs.** Today one 2×Item BSS cell per top-level
  binding ([transpile-mir.cpp:14673](../lambda/runtime/transpile-mir.cpp)), GC-rooted by walking
  all modules against the current heap ([mir.c:492](../lambda/runtime/mir.c)), zeroed-then-
  re-rooted on batch reuse ([transpile-mir.cpp:16159](../lambda/runtime/transpile-mir.cpp)).
  New shape: `rt->module_vars[mod_id][slot]`. `slot` is a baked constant — derived from the
  module's own source, so MIR-module-cache replay-stable. `mod_id` is **not** baked (a baked
  `Script->index` would go stale in cached modules): it lives in one init-once per-module
  `_mod_id` cell written at registration — every context writes the same value → benign
  idempotent race, replay-stable. Cross-module `pub` access = symbolic import of the exporter's
  `_mod_id` cell + baked slot. Slabs are context-owned, registered as root ranges against their
  own heap → `walk_bss_gc_roots` and the reset dance are deleted. Note the naive fix — a
  `_mod_vars_ptr` cell holding "the" slab pointer — *violates* the contract (different value
  per context). Cost: two extra loads, amortized by hoisting the slab pointer per function
  (same pattern as consts).
- **`_mod_consts_ptr` / `_mod_type_list_ptr` — legal as-is.** Per-Script compile artifacts:
  identical value for every context of a Runtime; concurrent re-init is value-idempotent.
- **IC cells — shared, restricted to Runtime-owned facts.** Member/store ICs
  ([transpile-mir.cpp:7546](../lambda/runtime/transpile-mir.cpp)) and compiled patterns are
  `script_pool`-owned, addresses baked as immediates. They may cache only context-independent
  facts: compile-time `TypeMap*` identities, shape entries, slot offsets — meanings shared by
  every context of the Runtime. They must **never** cache context-owned pointers (heap Items,
  runtime-grown shapes) — cross-context that is a foreign/dangling deref. Publication
  discipline: pack entries into a single word, or write-payload-then-publish-key with
  re-validate-after-read (seqlock-lite); a lost update is a miss, not a bug. Audit each IC kind
  against this rule; any kind that needs context-owned values loses that caching or moves to a
  per-context IC slab via the same `rt->` indirection. Compiled patterns are immutable after
  compile — fine.
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
  addresses). Eval contexts are transient — never module-cache-serialized — so eval codegen may
  bake `mod_id` immediates; replay-stability doesn't apply to it. Consequences under MT2:
  the dynfunc source→function cache
  ([js_mir_eval_lowering.cpp:102](../lambda/js/js_mir_eval_lowering.cpp)) becomes per-Runtime
  under a lock (compiled code is context-independent; sharing is correct) or per-context if
  contended; the `jm_defer_mir_cleanup` list is per-EvalContext (tag R — eval-context lifetime
  is closure-escape-sensitive). **Dynamic `import()` is the one new case**: it compiles a
  module destined for Runtime-shared tables (`module_mir_contexts[]`), so it follows
  compile-private-then-publish-under-lock — lower and link in a thread-owned context, seal,
  then register; other threads only ever observe sealed modules (the same publish pattern MT3
  would use).

**MT1 — interpretation.** The MIR interpreter *mutates* `MIR_context` state (frame arenas, FFI
shims): one `MIR_context` = one interpreting thread. Multi-thread interpretation is achieved by
**instantiation, not locking**: each interpreting thread loads its own `MIR_context` from the
serialized module (the L1 module-cache `MIR_write`/`MIR_read` path already exercises this).
The MT2 data contract is what makes clones semantically transparent — today, cloning would
fork `_gvar_` BSS state silently; after the migration no mutable state lives in the module to
fork. Each clone's `_mod_id` cell is written with the Runtime-wide id at clone load.

**MT3 — parallel lowering (KIV).** Half the shape exists: JS already gives each module its own
`MIR_context` (`module_mir_contexts[]`). Blockers are exactly the RG12 tag-C globals (active-
transpiler pointers, preamble flags → thread-confined per-transpiler objects) plus shared
compile pools (name_pool, type arenas need per-worker ownership or locks). Fan out lowering one
module per worker; `MIR_link` + cross-module resolution join on the owner thread. Not scheduled;
RG12 is designed so this becomes additive.

### 5.6 Crash/timeout cleanup paths
Recovery code (batch handler, watchdog) today reads `g_active_mir_ctx` etc. to clean up. After
RG12 it must reach the same via `context->runtime` — which the handler can do, since handlers
run on the executing thread and `context` is TLS. Where a handler can run with `context == NULL`
(early init), it must already do nothing — audit each.

---

## 6. Performance notes

- A process-global read is one absolute-address load. A capsule read is TLS-load + 2 chained
  loads. On the hot paths that matter (exception poll, `current_this`, with-depth, module_vars
  base) the mitigation is the standard one: **hoist the capsule pointer into a local** at
  function/loop entry — same shape as the emission-time exception-state tracker already planned
  (online-exception design). The JIT side is unaffected: generated code reaches state via `rt`
  (Context fields) which it already holds in a reg.
- `module_vars` is the JS hot path most exposed: today `js_active_module_vars` is a global
  pointer swapped per module. It becomes a `js_state` field — one extra indirection at swap
  sites only, since executing code already goes through the pointer.
- Gate: Result-suite geo means (MIR + LJS) within noise (±2%) per phase; any regression >2%
  gets a hoist fix before the phase merges.

## 7. Migration phases

Each phase: green `make test-lambda-baseline` + `make test-radiant-baseline`, js-test-batch,
`make node-baseline` no-regress, forced-GC sweep, Result-suite perf gate (§6).

- **P0 — Contract + lint.** Add `EvalContext` capsule pointers (all NULL), `Runtime` back-pointer,
  the `// global-ok: <tag>` annotation convention, and a lint rule (extend `make lint`) that
  fails on any *new* untagged file-scope mutable in runtime dirs. Stops the bleeding.
- **P1 — JsRuntimeState pivot.** Move the capsule to `context->js_state` via RG3 macro pivot;
  root-range registry moves with it; hot-context reset switches to capsule reset. (Biggest
  value/effort ratio in the plan.)
- **P2 — Event loop.** `JsEventLoop` capsule: uv loop + rings + timers + clocks (RG8, K3). Do
  the drain-guard conversion with it (Js_Thread P2.2 alignment).
- **P3 — JS file-local sweeps.** File-capsule structs per §4.4, in order: js_runtime.cpp,
  js_globals.cpp, js_dom.cpp (+TLS caches), js_modules cluster, js_host cluster (assert/stream/
  buffer/net/fs/…), observers/events. Mostly mechanical; §5.2 discipline throughout. Agent/
  atomics state → `JsAgentCluster` (§5.3).
- **P4 — Lambda core sweeps.** §4.2 rows; merge `persistent_last_error` into
  `EvalContext::last_error`; `current_vargs`; render/template/edit bridges; `input_context`
  fold (RG1).
- **P5 — Compile-side to Runtime.** RG12 set: active-transpiler pointers, module MIR contexts,
  preamble state, dynamic_import_map, registry_map, parser. Independent of P1–P4; can run in
  parallel with P3/P4.
- **P6 — `_lambda_rt` option B.** Entry-call codegen for Lambda MIR + JS MIR, delete the
  global and every save/restore. This is the phase that makes N concurrent executors *possible*;
  RC2 sequencing consumes it.
- **P7 — Guests.** py/bash capsules (RG9). Low risk, do opportunistically.

Exit criteria: the §2 grep invariant holds; `lambda.exe` (main.cpp + cmdedit + log) is the only
untagged-global holder; two `EvalContext`s can be created and run alternately (not yet
concurrently) with zero cross-talk — add a gtest that interleaves two contexts and diffs
results against solo runs.

## 8. Out of scope

- Actually running two executors concurrently (RC2) — this doc removes the state blocker only;
  scheduling, signals-per-thread (JT4/JT5), and loop affinity (JT6) are Js_Thread's program.
- Radiant/UI globals (`radiant/`) — separate audit; the UiContext/document side has its own
  ownership doc trail.
- Rewriting the C transpiler path (frozen per CLAUDE rule 14) — `transpile.cpp`'s emitted
  `extern Context* _lambda_rt` stays as-is; only MIR Direct evolves.
