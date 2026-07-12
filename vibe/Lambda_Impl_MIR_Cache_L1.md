# Level 1 MIR Cache — Implementation Plan (In-Process Persistent Module Cache)

**Status:** Phase 4 implemented; retained Lambda MIR imports include measurement and summary output
**Date:** 2026-07-12
**Design:** realizes **MC1** of `vibe/Lambda_Design_MIR_Cache.md` §3. Read that first for the why; this doc is the how.
**Scope:** Lambda modules under MIR Direct, in `test-batch` (and any long-lived Runtime). Main scripts are never cached. JS / C2MIR / cross-language modules excluded from v1 (§8).

**Implementation checkpoint (2026-07-12):** Phase 1 registry mechanics are landed: `Script` cache metadata, `Runtime::script_index`, path-index lookup in `load_script`/`precompile_imports`, null-safe batch teardown with stable slots, synthetic module registration through the same registry helper, and cache hit/miss summary logging. Phase 2 adds direct-import cone initialization, zero-then-root BSS handling, cross-language taint gating, and enables retained Lambda MIR imports. Phase 3 adds mtime/size invalidation for changed file-backed modules and retained dependents. Phase 4 adds cache summary output, `LAMBDA_DISABLE_MIR_CACHE=1` measurement gating, and developer documentation.

---

## 1. Goal and non-goals

**Goal:** within one process, an imported module is parsed/AST-built/transpiled/`MIR_gen`'d **at most once**. A batch of N tests sharing M imported modules pays compilation O(M), not O(N·M).

**Non-goals (v1):** caching main scripts; caching across processes; caching JS or cross-language modules; eviction under memory pressure; REPL integration.

---

## 2. Current behavior (verified 2026-07-12)

### 2.1 Compile flow
- `test-batch` loop (`lambda/main.cpp:3944-4057`): reads script paths from stdin, calls `run_script_file` → `run_script_mir` (`transpile-mir.cpp:14204`) → `load_script`.
- `load_script` (`runner.cpp:1120`): optional `precompile_imports` (parallel, POSIX, `runner.cpp:946`), then canonical-path dedup by **linear scan** over `runtime->scripts` (`runner.cpp:1147-1167`), then registers a stub `Script` (append; `index = length-1`, `runner.cpp:1169-1173`) and compiles via `transpile_script`.
- `transpile_script` (`runner.cpp:525`): parse → **fresh `Input` base per script** (`Input::create(mem_pool_create(...))`, `runner.cpp:573`) supplying `tp->pool`, `tp->arena`, **`tp->name_pool`**, `tp->type_list` — i.e. *each script owns its compile-time name pool and arena* — → `build_script` (recursively `load_script`s imports depth-first via `build_module_import`, `build_ast.cpp:9256`) → `compile_script_as_mir_direct` (`transpile-mir.cpp:13961`).
- `compile_script_as_mir_direct`: `clear_dynamic_imports()`; for each direct import `register_module_pub_fns(imp)` (`:13795` — walks the dep's `ast_root`, `find_func`/`find_import` on the dep's **live `jit_context`**, registers `m<index>.`-prefixed fn names and `_<name>` pub-var BSS addresses into the thread-local `dynamic_import_map`); `jit_init` (fresh `MIR_context_t`); `transpile_mir_ast`; `MIR_link`; `jit_gen_func("main")`; wires BSS `_mod_consts_ptr` → `const_list->data` and `_mod_type_list_ptr` → `type_list` (`:14035-14051`).

### 2.2 Execution flow (`run_script_mir`, imports case, `transpile-mir.cpp:14258-14320`)
1. `runner_setup_context` — creates/reuses `rt->heap`/`nursery`/`name_pool` (the *runtime* name pool, distinct from the per-script compile pools) (`runner.cpp:1305-1330`).
2. Registers `_gvar_*` BSS slots of **every script in the registry** as GC roots (`:14273-14278`; `register_bss_gc_roots`, `lambda/mir.c:370-393`; roots go into `context->heap->gc` via `gc_register_root`, `lambda-mem.cpp:343-346`).
3. Runs every non-main registry entry's `main_func` in **reverse index order** (`:14286-14294`), repointing `context->consts`/`type_list` per module, then runs the main script.

### 2.3 Teardown per batch script (`main.cpp:4024-4052`)
`runtime_reset_heap` (destroys heap + nursery + **runtime** name pool + `runtime->type_list`, `runner.cpp:1528-1555`); `path_reset()`; then **frees every `Script`**: `reference`, `source`, `directory`, `syntax_tree`, `pool` (which kills the script's AST, arena, name pool, and every P1-baked string), `type_list`, `jit_cleanup(jit_context)`; truncates `scripts->length = 0`. The parser survives.

### 2.4 Facts the design leans on (settling the design doc's open questions)

- **OQ1 resolved — no stale-root hazard, but a stale-*value* hazard.** GC roots live inside `context->heap->gc` and die with the heap; nothing needs unregistering. But a *cached* module's `_gvar_` BSS slots still hold Items pointing into the **destroyed** heap of the previous test. Rooting them (step 2 above) before its `main_func` re-runs means a GC triggered during module init would trace dangling pointers. **The slots must be zeroed before rooting** (§4 D4). Zero is exactly the state fresh MIR BSS has today when it gets rooted, so the GC already handles it.
- **Name-pool lifetime is a non-issue.** The P1 baked string pointers come from `tp->name_pool` = the **script's own** `Input` name pool, backed by `script->pool` (`runner.cpp:573-582`; `transpile_mir_ast(..., tp->name_pool)` at `transpile-mir.cpp:14013`). Not freeing a cached `Script` keeps them alive. The **runtime** name pool (`rt->name_pool`) may keep resetting per test unchanged. (The design doc's L1-a "promote name pool to batch lifetime" is thereby narrowed to: *don't free cached scripts' pools* — no lifetime change to the runtime pool.)
- **`m<index>.` coupling is link-time only.** Prefixed names are constructed at the *importer's* compile from `import->script->index` (`transpile_shared.cpp:65,88`) and registered from the same field at the same time (`register_module_pub_fns`); after `MIR_link` the references are resolved addresses. A cached module's own code never contains its own index. So index stability is required only *while an importer is being compiled*. We keep indices stable anyway (never compact) as the conservative invariant — it costs nothing and removes a class of reasoning.
- **`_mod_consts_ptr`/`_mod_type_list_ptr` stay valid on a cache hit.** They point at `const_list->data`/`type_list`, which live on the retained `Script`. No rewiring needed.
- **What `register_module_pub_fns` needs from a cached dep:** `ast_root` (walked for pub fns/vars), `jit_context` (address lookups), `index`. All retained.
- **`module_register` namespaces** (`runner.cpp:1250-1253`) create heap objects but only when `context && context->heap` at load time — not the case in the pure Lambda→Lambda path. Cross-language namespaces are a JS-interop concern, excluded from v1 (§8, gate D6).

---

## 3. Data-structure changes

### 3.1 `Script` (`lambda/ast.hpp`)
```c
// L1 cache fields
bool      cache_retain;      // true = survives per-script teardown (imported, cacheable)
bool      cache_retired;     // invalidated; slot is dead, free at next teardown
time_t    src_mtime;         // stat at load time, for invalidation
off_t     src_size;
ArrayList* direct_imports;   // Script* of direct imports (Lambda-only), set during compile
```
`direct_imports` is populated in `compile_script_as_mir_direct` while iterating `AST_NODE_IMPORT` children (`transpile-mir.cpp:13981-13992`) — append `imp->script` for non-cross-lang imports. It gives O(edges) cone traversal at run time without re-walking ASTs, and reverse-edge computation for invalidation.

### 3.2 `Runtime` (`lambda/transpiler.hpp`)
```c
struct hashmap* script_index;   // canonical path -> Script*, replaces linear dedup scan
```
Keyed by `Script->reference` (canonical path). Entries added at stub registration (`runner.cpp:1169`), removed on retirement. This also fixes the existing O(n²) scan that `precompile_imports` already works around with its transient `path_index` (`runner.cpp:977`).

Registry slots are **never compacted**: retiring a script NULLs its `runtime->scripts->data[i]` slot; indices are stable for the process lifetime. Every registry loop becomes null-safe (§5 P1-4).

---

## 4. Design deltas

- **D1 — Persistent registry.** Non-main Lambda scripts survive teardown (`cache_retain = true`). Mains are freed as today.
- **D2 — Teardown split.** The `test-batch` free-loop frees only `!cache_retain || cache_retired` entries; heap/path reset unchanged.
- **D3 — Cone-based module init.** Replace both whole-registry loops in `run_script_mir` (root-all at `:14273`, run-all-reverse at `:14286`) with a **post-order DFS over `runner.script`'s `direct_imports` cone**. With a persistent registry the old loops would root and run modules the current test never imported — and rooting an unrelated cached module's stale BSS is a correctness bug, not just waste (§2.4). Post-order = deps before dependents, the ordering the reverse-index loop was approximating.
- **D4 — Zero-then-root BSS.** For every module in the cone (cached or fresh — idempotent), zero its `_gvar_*` slots, then register roots. New `reset_and_register_bss_gc_roots(ctx)` in `lambda/mir.c` beside `register_bss_gc_roots` (`:370`): same item walk, `memset(item->addr, 0, item->u.bss->len)` before `heap_register_gc_root`. Values are recomputed by each module's `main_func`, which the init loop runs anyway.
- **D5 — Invalidation.** On dedup hit for a file-backed script: `stat()`; if mtime/size changed → retire the entry **and its transitive dependents** (reverse edges from `direct_imports`), then treat as a miss. Retired entries are freed at the next teardown boundary (no importer can still be running — mains are per-test and the current test hasn't compiled against them yet).
- **D6 — Gating.** `cache_retain` is set only when: `is_import && runtime->use_mir_direct && !is_cross_lang-tainted`. A module whose import subtree contains any cross-language import (JS/py/…, `build_ast.cpp:9382-9402`) is compiled as today and freed as today (taint propagates upward: if any direct import is untainted-false, the parent is tainted). C2MIR batch (`--c2mir`) keeps the old teardown wholesale.

---

## 5. Implementation phases

### Phase 0 — Instrumentation + baseline (no behavior change)
1. Add `log_info` cache hit/miss lines in `load_script` dedup, and a per-batch summary (modules compiled / hits) printed at `test-batch` end.
2. Record baseline: `LAMBDA_PROFILE=1 make test-lambda-baseline` timings; keep `temp/phase_profile.txt` for comparison. Note the per-module `parse/ast/transpile/mir_gen` split for the common modules (math, pdf, chart).
3. **Exit gate:** baseline green (2942/2942), numbers captured in this doc's §9.

### Phase 1 — Persistent registry mechanics

**Checkpoint 2026-07-12:** fields, path index, null-safe teardown, and summary instrumentation are implemented with `cache_retain = false`; the Phase 2 execution rewrite is the next enabling step before retained modules can go live.

1. **`Script` + `Runtime` fields** (§3). Init `script_index` in `runtime_init` (`runner.cpp:1511`), free in `runtime_cleanup`.
2. **`load_script`**: replace the linear dedup scan (`runner.cpp:1147-1167`) with `script_index` lookup (keep the mutex; keep `is_loading` circular check). Capture `src_mtime`/`src_size` after `read_text_file`. Set `cache_retain` per D6 at the end of a successful import compile.
3. **Null-safe registry loops.** Audit every `runtime->scripts` iteration for NULL slots: `load_script` (now hashmap, moot), `precompile_imports` cached-check (`runner.cpp:1026-1032` — also switch to `script_index`), `run_script_mir` (rewritten in Phase 2), `runtime_cleanup` script-free loop (`runner.cpp:1622+`), the two `main.cpp` free loops, and any others found by `grep -n "scripts->data" lambda/`.
4. **`precompile_imports` reversal** (`runner.cpp:1087-1098`): unchanged in logic — it only touches `[pre_start, pre_end)`, which are the newly appended scripts — but assert it never renumbers a `cache_retain` entry (defensive check, D3 note in §2.4 means it wouldn't matter at link time, but the invariant is cheap to keep).
5. **Teardown split** in `main.cpp:4038-4052`:
   ```c
   for (int i = 0; i < runtime.scripts->length; i++) {
       Script *script = (Script*)runtime.scripts->data[i];
       if (!script) continue;
       if (script->cache_retain && !script->cache_retired) continue;  // cached module lives on
       hashmap_delete(runtime.script_index, script->reference);
       /* existing free block: reference, source, directory, syntax_tree,
          pool, type_list, jit_cleanup(jit_context), mem_free(script) */
       runtime.scripts->data[i] = NULL;   // retire slot; never compact
   }
   /* do NOT reset scripts->length */
   ```
   Also free `direct_imports` in the free block, and mirror the same guard in `runtime_cleanup`'s final loop (process exit frees everything, including retained entries).
6. **Exit gate:** `make test-lambda-baseline` 100% — at this point results must be *identical* because Phase 2 hasn't landed, so run Phase 1 with a temporary hard `cache_retain = false` default, flipping it on only in Phase 2. (Keeps the mechanical refactor separately bisectable.)

### Phase 2 — Correct execution with cached modules (the heart)

**Checkpoint 2026-07-12:** direct-import cone execution, `reset_and_register_bss_gc_roots`, D6 taint gating, cross-language taint gating, and retained Lambda MIR imports are implemented. Phase 3 now handles changed imported files in a long-lived batch by retiring the stale module and retained dependents before recompilation.

1. **`direct_imports` population** in `compile_script_as_mir_direct` (`transpile-mir.cpp:13981-13992`), plus the cross-lang taint bit for D6.
2. **Rewrite module init in `run_script_mir`** (`:14258-14297`). Replace the AST-walk `has_imports` check + two whole-registry loops with:
   ```c
   // post-order DFS over the current script's import cone (visited set on Script
   // or a generation counter to avoid O(n) clearing)
   collect_import_cone(runner.script, &cone);            // deps-before-dependents order
   for (Script* s : cone) reset_and_register_bss_gc_roots(s->jit_context);   // D4
   reset_and_register_bss_gc_roots(runner.script->jit_context);
   for (Script* s : cone) {                               // excludes main
       runner.context.consts = s->const_list ? s->const_list->data : nullptr;
       runner.context.type_list = s->type_list;
       s->main_func(&runner.context);
   }
   ```
   A module imported via multiple paths (diamond) appears once (visited set), matching today's behavior where each registry entry runs once.
3. **`reset_and_register_bss_gc_roots`** in `lambda/mir.c` (D4). Keep the old `register_bss_gc_roots` for the no-imports path in `execute_script_and_create_output` (`runner.cpp:1386` — main script is always fresh there, but switch it to the reset variant anyway for uniformity; zeroing fresh BSS is a no-op).
4. **Cache-hit link path**: nothing to change by design — `register_module_pub_fns` on a cached dep reads its retained `ast_root` and live `jit_context` (§2.4). Verify with a debug assert that `imp->script->jit_context != NULL` whenever an importer compiles.
5. Flip `cache_retain` on (per D6).
6. **Exit gate:**
   - `make test-lambda-baseline` 100%.
   - New targeted test (see §6.2) green.
   - A manual 3-run batch of the same import-heavy test shows: run 1 compiles module cone, runs 2-3 log cache hits and produce byte-identical output.

### Phase 3 — Invalidation (D5)

**Checkpoint 2026-07-12:** file-backed cache hits now compare cached mtime/size against `stat(lookup_path)`. A changed module retires its retained dependent cone, removes matching `Script*` entries from `script_index`, and falls through to a fresh compile. Freeing an old retired slot only removes the index entry when it still points at that exact `Script*`, so a freshly recompiled replacement keeps its cache entry.

1. On `script_index` hit for file-backed scripts: `stat(lookup_path)`; on mtime/size mismatch, call `retire_script_cone(runtime, script)` — marks `cache_retired` on the script and every transitive **dependent** (computed by scanning retained scripts' `direct_imports` for edges into the retired set, iterating to fixpoint — registry is small, O(n·e) is fine), removes them from `script_index` — then fall through to the miss path (fresh stub gets a fresh slot + index).
2. Retired entries are freed by the Phase 1 teardown guard at the next boundary.
3. **Exit gate:** test that edits a module file between two batch entries (touch + content change) and asserts the second run sees the new behavior; ASAN-clean.

### Phase 4 — Measurement + summary output

**Checkpoint 2026-07-12:** batch-end summary now reports `modules_cached`, `compiles_saved`, path-index `hit_rate`, raw counters, invalidations, and whether the retained cache was disabled. `LAMBDA_DISABLE_MIR_CACHE=1` disables retained import caching for apples-to-apples timing while preserving same-run import dedup and circular-import detection. `doc/dev/lambda/LR_07_MIR_Transpiler_JIT.md` now documents the module cache and links this plan.

1. Batch-end summary line (`modules cached: X, compiles saved: Y, hit rate`).
2. Re-run the Phase 0 measurement; record before/after wall-time for `make test-lambda-baseline` and the profile split in §9.
3. Update `doc/dev/lambda/LR_07_MIR_Transpiler_JIT.md` with a short "module cache" section; cross-link the design doc.
4. **Exit gate:** measured speedup documented; no regression in `make test-lambda-baseline`.

---

## 6. Testing

### 6.1 Existing suites
- `make test-lambda-baseline` — must stay 2942/2942 at every phase gate. The gtest runner already drives `lambda.exe test-batch` via popen with sub-batches (`test/test_lambda_helpers.hpp:499-566`), so the cache is exercised by the whole corpus automatically — every test file importing `math`/`pdf`/`chart` after the first becomes a hit.
- `make test` full sweep at Phase 4.
- ASAN debug build over a hand-picked import-heavy sub-batch (catches use-after-free from any missed teardown path — the classic failure mode of this change).

### 6.2 New targeted tests (`test/lambda/` + a gtest driver)
1. **Repeat-run determinism:** same import-using script listed twice in one batch → identical outputs; second run must log a cache hit for each import.
2. **Module-global re-init:** a module with `pub var` computed at module init, mutated by the importing script (if expressible) or dependent on heap state; assert the second batch run sees *fresh* init values (proves `main_func` re-run + BSS zeroing, not stale carryover).
3. **Diamond imports:** A imports B and C, both import D → D compiled once, initialized once per run, correct values in both paths.
4. **Interleaved non-overlapping cones:** test1 imports M1, test2 imports M2, test3 imports M1 again → M1 hit on test3; test2 must not root/run M1 (assert via debug log absence — this is the D3 regression guard).
5. **Invalidation:** batch entry runs script importing M; harness rewrites M (different constant) with bumped mtime; second entry asserts new value.
6. **GC stress during module init:** module whose `main_func` allocates enough to trigger nursery/heap GC while other cached modules exist → exercises D4 (zeroed stale slots traced safely).
7. **pub-var cross-module:** importer reads a dep's `pub var` on a cache-hit run (exercises the `_gvar_`/BSS-address re-registration path in `register_module_pub_fns`).

### 6.3 Assumption checks to do during Phase 2 (cheap asserts, remove or demote to debug-only after)
- On cache hit: `script->jit_context && script->main_func && script->ast_root && script->const_list` all non-NULL.
- After heap reset, before cone init: every cone module's `_gvar_` slot reads 0 post-`reset_and_register_bss_gc_roots`.
- `import->script->index` unchanged between `register_module_pub_fns` and the importer's `MIR_link` (assert by snapshot).

---

## 7. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Stale heap Items in cached BSS traced by GC | D4 zero-then-root; test 6.2-6 |
| Whole-registry init loops touching unrelated cached modules | D3 cone rewrite; test 6.2-4 |
| A missed free-path still freeing a retained script's `pool`/`jit_context` (use-after-free on next hit) | Phase 1-3 audit of every `scripts->data` loop; ASAN sub-batch run in CI-adjacent script |
| Hidden per-script state beyond the audited set (e.g. `script->decimal_ctx`, `func_name_map`, `debug_info`) becoming stale | These live on `Script` and are immutable post-compile; `decimal_ctx` is NULL'd only in the free path today (`main.cpp:4048`) — cached entries skip the free path entirely. Verify `context->debug_info` (set per-run in `runner_setup_context:1292`) points at the retained script's data — it does, per-run assignment |
| `module_register` heap-backed namespaces dangling after heap reset (cross-lang) | Excluded by D6 taint gate in v1; revisit with JS support |
| Memory growth over a long batch (every distinct module's context + pools retained) | Bounded by corpus module count (small); Phase 4 logs retained-module count + a rough RSS check; eviction deferred by design (MC1: no eviction v1) |
| Thread-safety: `precompile_imports` workers touching `script_index` | All registry mutations already run under `scripts_mutex` (`runner.cpp:1145`); keep hashmap ops inside the same lock |
| Windows (no `precompile_imports`, no mutex compiled) | Sequential path uses the same dedup/teardown code; keep hashmap ops unconditional, mutex `#ifndef _WIN32` as today |

## 8. Out of scope / follow-ups
- **JS modules** (`js-test-batch`, `main.cpp:4060+`): separate lifecycle (`js_mir_module_batch_lowering.cpp:1897-1935` context deferral); needs its own audit — after Lambda v1 proves out.
- **C2MIR batch**: keeps old teardown (D6).
- **Eviction / memory pressure**: per MC1, none in v1; revisit if RSS numbers from Phase 4 demand it.
- **REPL/long-lived embedding reuse**: the mechanism is Runtime-generic (nothing here is test-batch-specific except the teardown edit), but only test-batch flips it on in v1.
- **Level 2 (lazy gen)** stacks on top: with `MIR_set_lazy_gen_interface`, a cached module's functions are generated at most once per process *and only if called*. Independent code path (`lambda/mir.c:280`); see design doc §4.

## 9. Measurements (fill during implementation)
- Baseline batch wall-time (Phase 0): _TBD_
- Phase 1 regression gate (retention disabled): `make test-lambda-baseline` passed 3299/3299 on 2026-07-12 (Input 2105/2105, Lambda Runtime 1194/1194).
- Phase 2 regression gate (retained imports enabled): `make test-lambda-baseline` passed 3299/3299 on 2026-07-12 (Input 2105/2105, Lambda Runtime 1194/1194). Manual 3-entry `test-batch` of `test/lambda/import_vars.ls` retained `mod_vars.ls` once and logged two cache hits with byte-identical script output.
- Phase 3 invalidation check: manual single-process `test-batch` over `temp/mir_cache_phase3_main.ls` saw `"phase3-before"`, then after editing the imported module saw `stale`/`retired` logs, recompiled to `"phase3-after"`, and a third run logged a hit on the replacement retained module (`invalidations=1`, `retained=1`).
- Phase 4 release `test_lambda_gtest` measurement (same release `lambda.exe`, same release gtest harness):
  - Cache enabled: `./test/test_lambda_gtest.exe` passed 458/458; `/usr/bin/time -p` real `12.19s`, user `20.64s`, sys `1.60s`; gtest total `11300 ms`.
  - Cache disabled: `LAMBDA_DISABLE_MIR_CACHE=1 ./test/test_lambda_gtest.exe` passed 458/458; `/usr/bin/time -p` real `22.10s`, user `80.14s`, sys `2.78s`; gtest total `22092 ms`.
  - Speedup from retained MIR imports on this harness: `22.10 / 12.19 = 1.81x` real time, about `44.8%` wall-time reduction.
- Phase 4 regression gate: `make test-lambda-baseline` passed 3299/3299 on 2026-07-12 (Input 2105/2105, Lambda Runtime 1194/1194).
- Post-Phase-4 wall-time: see release `test_lambda_gtest` measurement above; full `make test-lambda-baseline` timing still TBD.
- Hit rate over `test-lambda-baseline` corpus: _TBD_ (`test_lambda_gtest` uses `--no-log`, so cache summaries are not emitted by the harness run).
- Retained modules / RSS delta at batch end: _TBD_
