# Lambda Proposal: One MIR Context per Script for P2 Satellites

- **Date:** 2026-10-06
- **Status:** PROPOSAL — not ruled, not implemented. Findings below come from reading the vendored MIR and the satellite compiler plus one micro-benchmark; no prototype was built.
- **Scope:** Lambda P2 MIR satellites (`compile_ast_function_satellite_image` in `lambda/runtime/transpile-mir.cpp`, the worker queue in `lambda/runtime/interp.cpp`, the MIR wrappers in `lambda/runtime/mir.c`). The JS satellite path (`lambda/js/js_mir_*`) has the same per-context shape but was not examined and is out of scope here.
- **Formal authority:** D8.1.1v9 (cluster images), D8.1.1v12 (worker compiles in a *private* MIR context and may not mutate the Script MIR context), D8.1.1v14 (loop-handoff continuation images), D8.5.1v6 (the artifact is a private immutable image until a safe point adopts it), D8.5.1v7 / LR01-16 (key-suffix linking at publication). Accepting this proposal revises D8.1.1v12 and D8.5.1v6; per `doc/Doc_Convention.md` §3 that is a USER ruling, so nothing here is to be built before the ruling lands.
- **ID series:** extends the AST-interpreter ledger `AI#` (home: `vibe/Lambda_Design_Ast_Interpreter.md`, last used AI24). This proposal mints `AI25`–`AI31`. Implementation stages are `MC0`–`MC3`.

## 0. Question and answer

**Question.** Each MIR satellite today is compiled into, and owned by, its own `MIR_context_t`. Can MIR be patched or extended so that every hot function and hot loop promoted from one script/module is compiled under one MIR context?

**Answer.** Yes, and the vendored MIR needs no patch for the core of it. MIR already supports adding modules to a live context one at a time: `MIR_load_module` queues only the module it is given and `MIR_link` processes only that queue (`lambda/mir/mir.c`, `modules_to_link`). What forces a context per satellite is Lambda's own `jit_gen_func`, which reloads and relinks every module in the context on each call. The hard part is not MIR but the D8.1.1v12 worker model: a MIR context has no internal locking, so a pool worker cannot lower into the Script's context while the evaluator runs code from it. A MIR patch becomes necessary only if individual images must be *reclaimed* (MIR frees machine code solely at `MIR_finish`).

## 1. Current state (what is actually running)

- Every live satellite is compiled by a pool worker via `compile_ast_function_satellite_snapshot` (`interp.cpp:190`), which calls `compile_ast_function_satellite_image(..., snapshot=true, private_context=true, ...)`. The worker calls `jit_init` for a fresh context, lowers one definition (no cluster: D8.1.1v12 keeps the worker snapshot read-only), links, generates with `jit_gen_func`, and returns an `InterpSatelliteImage` that owns the context.
- The evaluator adopts the image at a safe point (`interp_satellite_publish_image` / `interp_loop_continuation_publish`), prepares the module state through `prepare_context_module_state` (walks every `_sat_*_layout` BSS in the context), and appends the image to `script->interp_satellite_images`. Images are destroyed one by one when stale, failed, cancelled, or at script teardown (`runner.cpp:2938`).
- **The synchronous entry `compile_ast_function_satellite` (`transpile-mir.cpp:47589`) has no callers** in `lambda/`, `radiant/`, `test/`, or `lib/`. Its anchor/retain split (`install_anchor`) and the `private_context=false` branch exist only for this dead entry. The "former shared-context path" the comments refer to is this one; it was replaced by private contexts in 97ae70307 (2026-09-23, "Fix pipeline promotion lifecycle regressions").
- Native codegen is already serialized process-wide by `g_mir_native_codegen_mutex` (`transpile-mir.cpp:60`), because MIR's generator initializes mutable process-global target patterns. Workers therefore lower in parallel but generate one at a time already.

### 1.1 Why a shared context breaks today (the real blocker)

`jit_gen_func` (`lambda/runtime/mir.c:380`) locates a function by walking *every* module in the context and calling `MIR_load_module` on each, then `MIR_link`s again. On a module that is already live this:

1. re-zeroes the head item of each BSS section — `load_bss_data_section` runs its setup `memset` for `curr_item == item` even when `item->addr` already exists — wiping the `consts`/`type_list`/`layout` pointers Lambda stored there after the first link (the hazard noted at `transpile-mir.cpp:47602`);
2. redirects every already-generated function's thunk to `undefined_interface` until the relink reaches it again (a running caller would fault in that window);
3. re-runs `simplify_func` on already-simplified functions, and on functions whose IR `jit_release_generated_ir` freed.

None of this is a MIR defect. MIR's contract is "load a module once, link what was loaded"; `jit_gen_func` was written for one-module contexts where the reload is harmless because the BSS is written after it.

### 1.2 What a shared context does and does not buy (measured)

- Per-context overhead is small: `MIR_init` + `MIR_gen_init` + `MIR_gen_set_optimize_level` costs ~0.11 ms and ~330 KB RSS per empty context (200 contexts, `temp/mir_ctx_cost.c` against the vendored `libmir.a`, arm64 macOS), plus one 16 KB code page and a thunk block once code is published. Consolidation is therefore not primarily a time or memory win; a script that promotes twenty satellites saves roughly 6 MB and 2 ms.
- The substantive gain is **direct native edges between separately promoted satellites.** D8.1.1v9 already makes calls *within* a cluster direct, but a callee compiled in an earlier image "keeps its own entry and stays a dynamic target". In one context a later module can `import` an earlier module's exported raw entry: `MIR_link` resolves it through the context's item table before falling back to `import_resolver`, and `target_change_to_direct_calls` turns it into a direct call. MIR's link-time inlining (`process_inlines`) can also cross module boundaries, but only while the callee's IR is retained (`jit_release_generated_ir` would have to skip exported raw entries).
- That gain is conditional on Lambda-side lane agreement: a raw entry's signature is fixed by the call-site inference of the image that compiled it; a later image must either prove the same lanes or call the boxed `_b` wrapper as today. This proposal does not design that; see §5.

## 2. Rulings proposed (AI25–AI31)

- **AI25 — One MIR context per Script execution.** A Script owns at most one satellite MIR context (`script->jit_context`), created on the first adopted image. Every later image is a new MIR *module* in that context, never a new context. Supersedes the per-image context ownership in `InterpSatelliteImage::context`; the image keeps a `MIR_module_t` instead.
- **AI26 — Single-module finish.** Satellite finalization loads, links, and generates *only its own module*: `transpile_mir_ast_finalize` already calls `MIR_load_module(ctx, mt.module)`; the satellite path then calls `MIR_link(ctx, MIR_set_gen_interface, import_resolver)` and resolves entries by `find_func`. `jit_gen_func` is not called on a context that holds live modules. (The eager whole-module compiler keeps `jit_gen_func`; it owns a fresh context per module.)
- **AI27 — Worker lowering stays private; adoption moves the module.** A worker lowers in a private, generator-less context exactly as D8.1.1v12 requires (frozen facts, no Script mutation). It does **not** link or generate there. At the end of lowering the worker, under the Script's satellite-context lock, calls upstream `MIR_change_module_ctx(private, module, script_ctx)`, then loads, links, and generates the module in the Script context, still on the worker thread, and releases the private context. Lowering errors therefore never touch the shared context (§3.2).
- **AI28 — The satellite-context lock.** A per-Script mutex serializes every mutation of `script->jit_context`: module transfer, load, link, generation, `prepare_context_module_state`, and teardown. The evaluator takes it only at adoption safe points and at teardown; executing published code never takes it. The existing process-wide `g_mir_native_codegen_mutex` stays and nests inside it.
- **AI29 — Layout preparation is per module.** `prepare_context_module_state` is given the new module (or its layout BSS item) and prepares only that layout; it no longer walks the context. Re-preparing earlier layouts on every adoption is at best redundant and at worst re-links their key suffixes (LR01-16).
- **AI30 — Stale and failed images are inert, not freed.** Because MIR frees machine code only at `MIR_finish`, a module that lost its generation check, or whose publication failed, stays in the context as dead weight until script teardown. Its functions are unreachable (no `_b` entry was published) and its BSS was never bound. A single-image `interp_satellite_image_destroy` becomes a no-op on the context. If a reclaim API is ever wanted it is a MIR patch under `patches/` (`MIR_remove_module`: unload items from the item table, free BSS sections, leave code pages) and requires the vendor-patch approval in CLAUDE.md rule 16.
- **AI31 — Teardown.** `runner.cpp` tears the Script context down once (`jit_cleanup_mode`) after the satellite queue is quiesced (`runtime_quiesce_satellite_workers`), instead of destroying N images. Cached AST templates (`script->cache_template->jit_context`) keep today's rule that a shell never owns the template's context.

## 3. Design

### 3.1 Synchronous path (the mechanism, Lambda-side only)

This is the MIR-facing core; it has no threading question because only one thread touches the context. It is described first because the worker path (§3.2) reuses it verbatim after the module transfer. Note that nothing calls the synchronous entry today (§1), so on its own it changes no running behaviour.

1. **Cluster and lowering** are unchanged: collect the target plus its direct-callee cluster, build the temporary `AstScript` root over shallow copies, `transpile_mir_ast_begin/lower/finalize`. `make_satellite_module_names` already gives every image a unique `_sat_<seq>_` prefix, so module names and the `_sat_*_layout`/`_sat_*_consts` BSS names are unique per image. Function item names (`f_<name>_b`, raw entries) repeat across images, which is fine: MIR's item table is keyed by (name, module) and the entries are not exported.
2. **Finish only this module** (AI26): `MIR_link` the single queued module and read the target and member `_b` entries with `find_func(ctx, name)` scoped to the new module (today's `find_func`, `lambda/runtime/mir.c:469`, walks all modules and returns the first match; with repeated names it must be scoped to `image->module`).
3. **Bind BSS** as today: write `consts`/`type_list` into this module's BSS, fill `LambdaModuleLayout`, `finalize_module_property_key_specs`.
4. **Adopt**: `interp_satellite_image_prepare` prepares *this* layout (AI29) and `interp_satellite_image_retain` records the image; the image now holds `module` instead of `context`.
5. **Delete the anchor logic** in `compile_ast_function_satellite`: with one context there is nothing to anchor, and `install_anchor`'s "later satellites force a new context" rationale disappears. Either the synchronous entry is deleted outright (no callers) or it becomes the thin wrapper the tests use.

### 3.2 Worker path (AI27/AI28)

```
worker                                   | evaluator
-----------------------------------------+-------------------------------
jit_init_mode(opt, generator=0)  (private)|
transpile_mir_ast_begin/lower/finalize    |   runs published code; never
   (frozen facts, D8.1.1v12)              |   touches script->jit_context
lock(script->satellite_ctx_mutex)         |
  MIR_change_module_ctx(priv, m, script)  |
  MIR_load_module(script, m)              |
  MIR_link(script, gen_interface, ...)    |   (blocked only if it reaches an
  jit_gen for the module (process lock)   |    adoption safe point meanwhile)
  bind BSS, finalize key specs            |
unlock                                    |
MIR_finish(priv)                          |
return image{module, entries}             | safe point: generation check,
                                          |   prepare layout (AI29), publish
```

Points verified in the vendored source:

- `MIR_change_module_ctx` (`mir.c:2856`) refuses a *loaded* module (`item->addr != NULL`) and otherwise re-interns every string, register name, alias, and item-table entry into the new context — exactly the state a finished-but-unloaded module carries. It moves a module whose functions were created with `MIR_new_func` in the private context; nothing in it depends on the generator.
- `transpile_mir_ast_finalize` currently calls `MIR_load_module` immediately after `MIR_finish_module`. For AI27 the load must move after the transfer (a `MirModuleBuild` flag, or the satellite path doing its own load), otherwise the transfer is refused.
- Lambda installs no MIR error function for satellites (only the JS batch path does, `js_mir_entrypoints_require.cpp:704`), so a MIR-level error runs MIR's `default_error`, which is `exit(1)` in either design. What a private context protects against is the Lambda-side `goto fail` exits of `compile_ast_function_satellite_image` — unsupported shape, cancel probe, const/type growth, missing BSS — which abandon a half-built or finished-but-unwanted module; in the private context it dies with that context. This is the reason AI27 keeps lowering private rather than lowering straight into the shared context under the lock.
- Code pages: `_MIR_set_code` flips the whole code holder to `PROT_WRITE_EXEC` then back to `PROT_READ_EXEC` on POSIX, `PAGE_EXECUTE_READWRITE` on Windows, and uses `MAP_JIT` + per-thread `pthread_jit_write_protect_np` on arm64 macOS. In every case the pages stay executable for the evaluator while the worker writes, so a worker publishing into the Script's context does not stall running code.

Points **not** verified, to be settled in MC1 before any ruling:

- `target_change_to_direct_calls` and `_MIR_redirect_thunk` on the newly linked module only. Both are expected to touch only the new module's items (the link queue holds one module), but on x86-64 the direct-call rewrite patches call sites inside generated code and must be confirmed not to walk earlier modules.
- Context-level data structures the evaluator might read while the worker mutates them: `MIR_get_module_list`, the item table, the string table. Today the evaluator reads them only in `prepare_context_module_state`, the debug-info table for native stack walking (`mir.c`, "Debug Info Table"), and `find_func`; each of those must sit under the AI28 lock or be fed the module directly.
- `jit_release_generated_ir` (`mir.c:417`) assumes "a sealed per-module ctx never runs `MIR_link` again". With a shared context that assumption is false in exactly the inlining case of §1.2; the gate must become "release IR of functions no later module may import".

### 3.3 Interaction with existing rulings

- D8.1.1v12's "private MIR context" sentence and D8.5.1v6's "the artifact is a private immutable image until a receiving execution safe point adopts it" are both narrowed, not abandoned: lowering stays private and read-only; the *executable* image is produced in the Script context by the worker under a lock, and publication of entries is still deferred to a safe point with a generation check. The ruling text would need a `v13`/`v7` revision stating that.
- D8.1.1v14 loop-continuation images are ordinary modules in the same context; `continuation_pool` ownership is unchanged.
- D8.5.1v2 cached-AST shells: a shell "shares immutable MIR" with its template. Under AI25 that is the template's single context; the per-shell `prepare_context_module_state` call at `interp.cpp:8469` becomes "prepare every satellite layout of the template once per shell" and is the one place a whole-context walk remains legitimate.

## 4. Alternatives considered

- **A. Lower privately, generate on the evaluator at the safe point** (move the module with `MIR_change_module_ctx`, then link and generate on the evaluator thread). Rejected: the evaluator pauses for the whole codegen (`compile_ms` in the satellite log is tens of ms for large clusters), which is precisely the stall D8.1.1v12 removed.
- **B. Lower and generate directly in the shared context under the lock.** Simplest, but a Lambda-side lowering failure (`goto fail`) leaves a half-built module in the shared context with no way to remove it (AI30), and every lowering of the same script is serialized, losing the parallel lowering the pool gives today. Rejected in favour of the hybrid (AI27).
- **C. Patch MIR with a module-unload API** so per-image contexts could be kept *and* images could still call each other through `import_resolver`. Rejected: cross-context calls already work through `import_resolver` (they are what "stays a dynamic target" means); the missing piece is the direct edge, which is a property of being in one context, not of unloading.
- **D. Keep per-image contexts** (status quo). Cheapest; still correct. The only cost it fixes nothing for is the dynamic edge between images (§1.2), and that edge needs the lane-agreement work of §5 to pay off at all. If §5 is not pursued, D is the recommendation.

## 5. Open points (need a ruling or more work)

1. **Lane agreement across images** (the payoff). Options: (a) a later image always calls an earlier image's `_b` wrapper — direct edge, no lane risk, still a box/unbox per call; (b) the image records each exported raw entry's inferred signature in its `InterpSatelliteImage` and a later image's call-site inference consults it, using the raw entry only on exact agreement. (b) is the eager tier's behaviour inside one module and is the one that recovers richards/deltablue-class wins across images.
2. **Whether reclaiming stale modules matters** (AI30 vs a `MIR_remove_module` patch). Measure first: count stale/failed modules per script in the baseline and benchmark suites after MC2.
3. **`find_func` scoping and the native stack-walking debug table**, both of which assume one function name per context.
4. **JS satellites** (`js_mir_entrypoints_require.cpp`, `js_mir_module_batch_lowering.cpp`) — same shape, same `jit_cleanup_mode` per artifact; apply the same rulings after the Lambda side proves out, or rule them out of scope.

## 6. Implementation stages

| Stage | Content | Exit |
|---|---|---|
| MC0 | Delete or wrap the dead synchronous entry; add a scoped `find_func`; make `prepare_context_module_state` take a module (AI29). No behaviour change. | `make test-lambda-baseline` 100 % |
| MC1 | Verification spike for the §3.2 unverified points: a test that loads two modules into one context, generates the second while a thread executes the first, and checks thunks/direct calls of module 1 are untouched. | written evidence in `vibe/impl/` |
| MC2 | AI25–AI29, AI31: one context per Script; worker transfer; lock; teardown. Images record `module`. | baseline 100 %, satellite benchmarks (richards, deltablue, diviter, mandelbrot2) no slower than status quo |
| MC3 | §5.1 lane agreement, option (a) first; measure; (b) only if (a) leaves the dynamic edge the dominant cost. | per-benchmark before/after in `vibe/impl/` |

MC0 is safe to do under today's rulings. MC2 is not: it requires the D8.1.1v12 / D8.5.1v6 revision first.

## Appendix A. Evidence

- `lambda/mir/mir.c`: `MIR_load_module` (1932) pushes only `m` onto `modules_to_link`; `MIR_link` (1986) iterates `modules_to_link` and pops it when setting interfaces; `load_bss_data_section` (setup loop re-`memset`s the head item unconditionally); `MIR_change_module_ctx` (2856); code holders and `_MIR_set_code` (4388–4512).
- `lambda/runtime/mir.c:380–389`: `jit_gen_func` reloads every module; `:417` the `jit_release_generated_ir` assumption; `:546` `prepare_context_module_state` walks the context.
- `lambda/runtime/transpile-mir.cpp:47281–47549`: `compile_ast_function_satellite_image`; `:47589` the uncalled synchronous entry; `:60` the process-wide codegen mutex.
- `lambda/runtime/interp.cpp:172–215`: the worker job; `:278–330` retain/publish/continuation publish.
- Micro-benchmark: `temp/mir_ctx_cost.c` — 200 × (`MIR_init`, `MIR_gen_init`, `MIR_gen_set_optimize_level(2)`): 0.114 ms/ctx create, 0.015 ms/ctx finish, 327 KB/ctx RSS.
