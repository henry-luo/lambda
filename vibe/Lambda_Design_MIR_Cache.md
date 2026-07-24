# Lambda Compilation Cache Design — Module Cache, Lazy Codegen, Code-Image Cache

**Status:** Level 1 **decided**; Level 2 **approved experiment**; Level 3 **direction decided — Route B (code-image)**, long-term
**Date:** 2026-07-12
**Context:** Reduces script startup cost across the Lambda runtime (Lambda, JS, and other Jube guests). Companion to `vibe/Lambda_Desing_Native_Module.md` §7.4 ("Local compiled-code cache"), which already established the governing principle **P14: native compiled artifacts are derived local caches, never distribution formats**. This doc turns that principle into a staged plan. Detailed backend design lives in `doc/dev/lambda/LR_07_MIR_Transpiler_JIT.md` (MIR Direct) and `LR_06_C_Transpiler.md` (legacy C2MIR).

---

## 1. Problem

Two distinct costs, two distinct use cases:

1. **Batch execution (dev/test).** `lambda.exe test-batch` runs many scripts in one process, but between scripts it frees every `Script` including `jit_cleanup(script->jit_context)` and truncates the registry (`lambda/main.cpp:4038-4052`). A module imported by 100 test files (math, pdf, chart, …) is parsed, AST-built, transpiled, and machine-generated **100 times**. The parser is deliberately kept alive across scripts; compiled modules are not.

2. **Cold start (production).** A fresh process pays the full pipeline for the main script and every import.

Profiling (both `LAMBDA_PROFILE=1` phase data and the analysis in `Lambda_Desing_Native_Module.md`) shows the dominant cost is **not** lowering to MIR — it is `MIR_gen`, the MIR→machine-code step. This kills the naive "serialize MIR bitcode to disk" idea: `MIR_write`/`MIR_read` exist in the vendored library, are compiled in, and are fully name-based/position-independent (`mac-deps/mir/mir.c:4929+`, `5158`, `5834`) — but a bitcode cache would skip only parse+AST+transpile and still re-run `MIR_gen` on every load. It caches the wrong phase. (The capability remains available as a cheap add-on if the front-end ever dominates for some workload; see §7.)

Hence the three levels below: **Level 1** kills the batch recompilation, **Level 2** shrinks `MIR_gen` itself, **Level 3** caches its output.

---

## 2. Investigation findings (facts the design rests on)

These were verified in code on 2026-07-12; file:line references are to that state of the tree.

### 2.1 The emitted MIR is not relocatable across processes

The MIR **binary format** is position-independent by construction — imports/exports/ref_data serialize as string names, resolved at `MIR_link` time by `import_resolver` (`lambda/mir.c:106-133`). But the MIR that `transpile-mir.cpp` **emits** embeds absolute host pointers as 64-bit immediates:

| # | What | Where | Points to |
|---|------|-------|-----------|
| P1 | name strings (field names, member keys, fn display names) | `emit_load_string_literal`, ~20 call sites (`transpile-mir.cpp:1176`) | name pool |
| P2 | module `type_list` pointer | `transpile-mir.cpp:1571` | compile-time arena |
| P3 | `TypeMap` descriptors | `transpile-mir.cpp:5879` | type pool |
| P4 | `LIT_TYPE_*` singletons, static `" "` | `transpile-mir.cpp:9665-9723`, `7577` | binary globals (ASLR-shifted per launch) |
| P5 | sys-function native addresses | `transpile-mir.cpp:10907` | binary code (ASLR-shifted) |

Everything else is already relocatable: literal **values** go through an integer index into `const_list`, reached via the named BSS `_mod_consts_ptr` patched after link (`transpile-mir.cpp:1559-1565`, `14035`); runtime builtins and `_lambda_rt` resolve **by name** through `import_resolver`; cross-module functions resolve by mangled name via the thread-local `dynamic_import_map` (`lambda/mir.c:85`).

All five baked-pointer categories are **stable within one process** (name pool and arenas persist; binary globals don't move). That is why Level 1 needs no codegen change and any cross-process cache does.

The **JS transpiler** is far worse: property names, string/regex/template literals, inline-cache cells, shape-cache cells, and ctor property pointers are baked as raw pointers throughout `lambda/js/js_mir_*`, and the IC/shape cells are *mutable per-compilation runtime state*, not read-only constants. JS is therefore in scope for Level 1 only; cross-process caching of JS is out of scope until its codegen is de-pointered (a separate, much larger project).

### 2.2 Module pipeline seams

- Each script/module gets its **own `MIR_context_t`** (`jit_context` on `Script`, `runner.cpp:661`; created in `compile_script_as_mir_direct`, `transpile-mir.cpp:14005`). Code pages live exactly as long as their context (`MIR_finish` frees them; no partial reclaim).
- `load_script` already dedups by canonical path against `runtime->scripts` (`runner.cpp:1147-1167`) — the module-level cache boundary exists, but only within one script run.
- Cross-module symbols are prefixed `m<index>.` where `index` is the module's position in `runtime->scripts` (`transpile_shared.cpp:65,88`). An importer's generated code depends on its imports' registry indices.
- On execution, per-module BSS `_gvar_*` slots are registered as GC roots (`register_bss_gc_roots`, `lambda/mir.c:363`) and module globals are (re)computed by running each import's `main_func` in reverse registry order (`transpile-mir.cpp:14286-14294`).
- `runtime_reset_heap` (called by test-batch between scripts) destroys heap, nursery, **and name pool** (`main.cpp:4027`).

### 2.3 MIR library capabilities (vendored at `mac-deps/mir/`, API 0.2, 2024-era)

- **No AOT object/assembly emission exists.** Machine code goes only into executable memory pages (`mir.c:4409-4436`). A DLL cannot come out of MIR directly.
- **`mir2c` exists but is not viable**: not compiled into `libmir.a` (`GNUmakefile:215`), upstream-documented as "might be not portable" (`README.md:321`), missing multi-value return.
- **Lazy codegen is fully supported and unused by us**: `MIR_set_lazy_gen_interface` (thunk per function, generate on first call) and `MIR_set_lazy_bb_gen_interface` (per basic block) in `mir-gen.h:25-26`. Lambda links **eagerly**: `MIR_link(ctx, MIR_set_gen_interface, import_resolver)` at `lambda/mir.c:280` generates machine code for *every* function in every module, called or not. Upstream positions binary MIR + lazy gen as the intended fast-startup story (`README.md:186,214,230`).
- **Opt levels**: `MIR_gen_set_optimize_level` — `0: fast gen; 1: RA+combiner; 2: +GVN/CCP (default); >=3: everything` (`mir-gen.c:209`); set per context in `jit_init` (`lambda/mir.c:151`).
- **No parallel codegen** in this MIR version (single-threaded per context).

### 2.4 Loading/linking infrastructure already present

- A general dlopen+dlsym module loader exists (POSIX only, Windows stubbed): `jube_load_dynamic_module` (`lambda/jube/jube_registry.cpp:679`).
- `lambda.exe` does **not** export dynamic symbols (no `-rdynamic`/export list anywhere in the build config).
- A comprehensive name→function-pointer runtime table already exists — `sys_func_defs[]` (~213) + `jit_runtime_imports[]` (~1,446) in `lambda/sys_func_registry.c`, consumed by `import_resolver` — i.e. the "inject the runtime API as a pointer table" pattern is already the established mechanism, also used by the Jube host API vtables.
- The legacy C backend (`lambda/transpile.cpp`, `LAMBDA_C2MIR`, only in the `jube` build config) generates **fully position-independent C** — consts/types by integer index, runtime functions as extern identifiers, imports via a runtime-populated BSS struct. But it is legacy, frozen, and feature-divergent (GROUP BY is a hard stub at `transpile.cpp:2355`; new features land MIR-first per `LR_06`).

---

## 3. Level 1 — Persistent in-process module cache (DECIDED)

**Goal:** in one process (esp. `test-batch`), compile each imported module at most once. The main script is not cached (it changes per invocation and is the unit of execution); imported modules are.

### 3.1 Design

A module registry that **survives per-script teardown**, keyed by canonical path. `load_script`'s existing dedup scan becomes a lookup against this persistent registry. Cached entry = the existing `Script` object: `jit_context` + `main_func` + `const_list` + `type_list` + AST pub-signature info + `index`.

Four invariants to hold:

**L1-a. Name-pool (and arena) lifetime.** Cached code holds raw name-pool string pointers (P1) and type/arena pointers (P2/P3). Therefore:
- Promote `NamePool` to **batch lifetime** — exclude it from `runtime_reset_heap`. The pool is content-addressed and append-only in usage (`name_pool.cpp:18-28`), so keeping it alive is semantically neutral; it only grows.
- A cached module's `const_list`/`type_list`/AST pool live on its `Script` and survive as long as the `Script` isn't freed — which is the whole point.
- Heap/nursery still reset per script as today.

**L1-b. Module-index stability.** Never compact `runtime->scripts`. Cached modules keep their original indices forever; each new main script appends at the next free index and is removed (slot retired, not reused) at teardown. A newly compiled importer then emits `m<index>.` references that match what `register_module_pub_fns` registers for the cached module. (Alternative considered and rejected: content-derived module IDs instead of ordinal indices — cleaner but touches `transpile_shared.cpp` naming and both backends; not needed for L1.)

**L1-c. Per-hit re-initialization.** On a cache hit, before execution:
1. Re-register the module's `_gvar_*` BSS slots as GC roots against the fresh heap (`register_bss_gc_roots`), and **drop the old heap's registrations** (roots must not dangle into a destroyed heap — the root registry is reset with the heap, so this falls out naturally; verify).
2. Re-run the module's `main_func` to recompute module globals (their old values pointed into the destroyed heap). `run_script_mir` already runs every import's `main_func` per execution (`transpile-mir.cpp:14286-14294`), repointing `context->consts`/`type_list` per module — so this is the existing execution path, unchanged. The *only* thing a cache hit skips is compilation.
3. Re-register the module's pub symbols into the thread-local `dynamic_import_map` for any importer being freshly compiled (`register_module_pub_fns`, `transpile-mir.cpp:13795`) — pointers are still valid since the `jit_context` never died.

**L1-d. Memory model.** One live `MIR_context_t` per cached module for the process lifetime; MIR frees code pages only at `MIR_finish`. Eviction = `jit_cleanup(script->jit_context)` + free the `Script` — safe only when no importer compiled against it is still live, so v1 policy is **no eviction** within a batch (bounded by the distinct-module count of the test corpus, which is small). If needed later: evict whole dependency cones only.

### 3.2 Invalidation (in-process)

Within one batch process, module sources are normally immutable. For safety in dev/watch scenarios, key the cache on `(canonical path, mtime+size)` — stat is cheap relative to compilation; a mismatch recompiles into a fresh entry (old entry retired per L1-d).

### 3.3 Implementation points

- `main.cpp:3944-4057` (test-batch loop): stop freeing non-main `Script`s and their `jit_context`s; stop resetting the name pool; retire only the main script.
- `runner.cpp` `runtime_reset_heap`: split name-pool destruction out of heap reset (new `runtime_reset_heap_keep_names` or a flag).
- `load_script` (`runner.cpp:1120-1173`): replace the linear registry scan with a persistent path-keyed `HashMap` (the linear scan is also O(n²) in module count today; `precompile_imports` already builds a transient `path_index` map to work around it, `runner.cpp:764`).
- Per-hit path in `run_script_mir`: verify GC-root re-registration and `dynamic_import_map` re-population are executed for cached modules (they run today for freshly compiled ones).

**Scope:** Lambda modules first. JS modules second (mechanism works — the baked IC/shape cells persist in-process and are just warm caches — but the JS runtime's per-context lifetime handling in `js_mir_module_batch_lowering.cpp:1897-1935` needs its own audit). Other Jube guests (py/rb/bash) currently do `jit_init`→`MIR_finish` per unit and can adopt the same pattern later.

**Expected win:** for a batch of N tests sharing M imported modules, compilation cost drops from O(N·M) to O(M). This is the bulk of the batch-test prize.

---

## 4. Level 2 — Lazy codegen + opt-level (APPROVED EXPERIMENT)

**Goal:** shrink `MIR_gen` itself — the measured bottleneck — instead of caching around it. Orthogonal to and compounding with Level 1: with both, a module's function is machine-generated at most once per process, *and only if actually called*.

### 4.1 Change

1. **Lazy function generation.** At `lambda/mir.c:280`, switch `MIR_link(ctx, MIR_set_gen_interface, import_resolver)` → `MIR_link(ctx, MIR_set_lazy_gen_interface, import_resolver)`. Every function's entry becomes a thunk that generates machine code on first call and redirects (`mir-gen.c:9807-9813`). Keep the explicit `MIR_gen(ctx, mir_func)` for the entry function (`lambda/mir.c:282`) or let the thunk handle it — measure both.
2. **Opt-level tiering.** `jit_init(opt_level)` already takes the level. Add a policy knob: imported modules (and/or dev/test runs) at level 0–1 ("fast gen", `mir-gen.c:209`), main script and release runs at 2. Test-batch sets it globally low.

For the stated workload — tests importing math/pdf/chart and touching a small slice of each — lazy gen converts "generate the whole module library" into "generate the handful of functions this test calls."

### 4.2 Risks / verification

- **Pub-function pointers become thunk addresses.** `register_module_pub_fns` obtains addresses via `find_func` and registers them into `dynamic_import_map` (`transpile-mir.cpp:13795-13876`). Thunks are callable and stable (the thunk redirects internally), so this should be transparent — verify a cross-module call through a registered thunk address works before and after first-call generation.
- **Function-pointer identity.** Anywhere the runtime compares or stores generated-code addresses (e.g. `func_name_map`, debug info, JS callbacks) must tolerate the thunk being the canonical address.
- **`MIR_set_lazy_bb_gen_interface`** (per-basic-block) is the more aggressive variant — hold in reserve; function-level lazy is the right first step.
- **Perf-sensitive suites** (awfy benchmarks) must run at level 2 eager or level 2 lazy — measure that lazy adds no steady-state overhead after warm-up (it shouldn't: post-generation calls go direct).

### 4.3 Decision gate

Run `LAMBDA_PROFILE=1` on a representative batch (dump at `temp/phase_profile.txt`, schema in `runner.cpp:94-234`) in four configurations: {eager, lazy} × {opt 2, opt 0}. If **L1 + lazy** brings batch time to acceptable, Level 3 is re-scoped to production cold-start only and drops in priority.

---

## 5. Level 3 — Native code cache on disk (LONG-TERM; Route B decided)

**Goal:** production cold-start pays neither transpile nor `MIR_gen` for unchanged code. Per P14 (`Lambda_Desing_Native_Module.md` §7.4 and Appendix A): this is a **local, regenerable, build-ID-keyed cache — never a distribution format**. Scripts still ship as source.

### 5.1 Prerequisite: de-pointering refactor (required for any cross-process cache)

Eliminate baked-pointer categories P1–P5 (§2.1) from `transpile-mir.cpp` output, routing each through mechanisms that already exist:

| Category | Fix |
|---|---|
| P1 name strings | store in `const_list`, load by index via `_mod_consts_ptr` (the existing literal scheme — `emit_load_string_literal` becomes `emit_load_const`) |
| P2 `type_list` ptr | already has a named BSS (`_mod_type_list_ptr`); use it at the one remaining raw-pointer site |
| P3 `TypeMap` descriptors | index into `type_list` via `_mod_type_list_ptr` (indices are stable per compilation) |
| P4 `LIT_TYPE_*` singletons, static strings | named imports resolved by `import_resolver` (add entries to the static registry) |
| P5 sys-function addresses | named imports — the functions already have names in `sys_func_registry.c` |

After this, generated code contains only names, indices, and values. This refactor is worthwhile hygiene regardless of Level 3: it also makes the MIR-bitcode option real (§7) and removes a class of "works until the pool moves" hazards. It is the shared prerequisite for **both** routes below — Route A only escapes it by using the legacy C backend, which is its own trap (§5.2).

**Sidecar serialization** is the second shared prerequisite: a cache entry must carry, besides code, the compile-time tables the runtime consumes — `const_list` values (strings, decimals, datetimes), the `type_list`/`TypeMap`/`ShapeEntry` graphs, pub-function signature info (importers need it for inference and `register_module_pub_fns`), optionally `func_name_map`/debug info. This serializer is where much of the Level 3 effort goes, and it is identical for either route.

**Cache key** (either route): `hash(runtime build ID, module source, transitive imports' pub-interface summaries, compile flags/opt level, cache format version)` — as already specified in `Lambda_Desing_Native_Module.md` §7.4. Content-hash keying makes dev-env staleness a non-issue: a changed exe or script *misses*; it can never produce a wrong hit.

### 5.2 Route A — dylib via C source + system compiler (documented; REJECTED)

*Recorded for completeness; the native-module doc called it a "viable stopgap" and we are not taking it.*

**Shape:** generate C source per module → `cc -shared -O2` → cache `.dylib`/`.so`/`.dll` → `dlopen` + `dlsym`, with the runtime API injected as a pointer table (the existing ~1,659-entry `sys_func_registry` table; `lambda.exe` exports no symbols, and per-platform exe-export is not worth fighting — Windows especially).

**What's already in place:** the legacy C backend's output is fully position-independent (§2.4) — consts/types by index, imports via runtime-poked BSS struct (`init_module_import`, `runner.cpp:358-519`, works under `dlsym` with a two-call swap); a POSIX dlopen loader exists in the Jube registry; the generated C already lands at `temp/_transpiled_<n>.c` under `--transpile-dir`. Genuine upside: clang `-O2` output would outperform `MIR_gen` output — a free performance tier for hot production modules.

**Why rejected:**
1. **A C toolchain becomes a production runtime dependency.** Nothing shells out to a compiler today; requiring clang on end-user machines (esp. the Windows clang64/MSYS2 setup) is a new deployment constraint we explicitly do not want. *(Deciding reason.)*
2. **Frontend trap.** The clean-C property belongs to the *legacy* backend — frozen, off by default, feature-divergent (GROUP BY stub; features land MIR-first). Using it means resurrecting and double-maintaining a second backend forever. The alternative frontend, `mir2c` over MIR-direct output, is not compiled into `libmir.a`, is documented non-portable, and would print the P1–P5 baked pointers as integer literals — i.e. it *still* requires §5.1.
3. Platform friction: slow first-compile, macOS hardened-runtime/library-validation if ever notarized, Windows loader stub unimplemented.

### 5.3 Route B — code-image cache (DECIDED)

*The end-state already recommended by `Lambda_Desing_Native_Module.md` §7.4; precedents: V8 code cache, tree-sitter's `~/.tree-sitter` compiled-grammar cache.*

> **Detailed design (2026-07-24):** `vibe/Lambda_Design_MIR_Cache_L3.md` — decides de-pointer vs image-patching (opt 1 wins, L3-1), the regenerate-front-end load model (no const/type sidecar in v1, L3-2), the relocation-journal kinds/hooks, the differential write verifier, and cache key/location (resolves OQ3/OQ5). The sketch below is superseded at the detail level.

**Shape:** after `MIR_gen`, serialize the module's generated machine code plus a **relocation journal**; loading = `mmap` + apply fixups + W^X flip + register with the runtime. No toolchain, no linker formats, microsecond-scale loads.

**The relocation journal.** `MIR_gen` produces no relocation records, so we create them at the points where process-specific addresses enter the code:

1. **Import fixups.** Every address that enters via `import_resolver` (runtime builtins, `_lambda_rt`, `LIT_TYPE_*` and sys-functions post-§5.1, cross-module `m<index>.*` symbols) is journal-able by name: record `(code offset, import name)` at the point MIR patches the call/reference. Load-time fixup re-resolves by name through the same `import_resolver` — identical semantics to today's link.
2. **Data-item fixups.** BSS items (`_gvar_*`, `_mod_consts_ptr`, `_mod_type_list_ptr`) get fresh addresses per load; journal `(code offset, item name)` for every code reference to a data item, and record the BSS layout (name, size, alignment) so the loader can allocate and bind it.
3. **Intra-module references** (function-to-function calls, jump tables): either journal them the same way or make the image position-independent by mapping code at one contiguous block and journaling only a single image base delta — pick whichever MIR's emitted code shape allows after inspection of `_MIR_set_code` / thunk machinery.

Post-§5.1 there are **no other address classes** in the code — that is precisely what the prerequisite buys.

**Implementation surface (in our tree, not upstream-invasive):** the journal hooks live where MIR resolves and writes addresses — `import_resolver` interception is ours already; code-write interception wraps `_MIR_set_code`/code-allocator callbacks (`mir.c:4409-4436`, and MIR exposes a pluggable code allocator). An upstream-patch-minimal approach should be a hard requirement, since `mac-deps/mir` tracks upstream.

**Load path:**
1. Validate cache key (§5.1). 2. `mmap` code image (macOS: `MAP_JIT` + `pthread_jit_write_protect_np` dance; ARM64: `sys_icache_invalidate` after patching). 3. Allocate BSS per journal layout. 4. Apply fixups: imports via `import_resolver`, data items to fresh BSS. 5. Flip W^X. 6. Deserialize sidecar tables; wire `_mod_consts_ptr`/`_mod_type_list_ptr`. 7. Register pub symbols into `dynamic_import_map`; `register_bss_gc_roots`; run `main_func` per execution as usual.

**Interaction with Level 2:** lazy gen and the code-image cache compose awkwardly (a lazily-generated module has thunks, partial code). Policy: cache-writing compiles run **eager at opt 2** (production/first-run), lazy mode is for uncached dev/test runs. A cache hit loads the full eager image.

**Risks (why this is long-term):** W^X/`MAP_JIT` platform work; journal completeness is a correctness cliff (one un-journaled address class = silent corruption — mitigate with a debug loader mode that maps the image at a randomized base and a verifier that diffs a fresh `MIR_gen` against a patched image on canary modules); sidecar serializer for `TypeMap`/`ShapeEntry` graphs is real work; Windows loader path. None of it requires a client toolchain, which is the deciding constraint.

**Scope:** Lambda modules and main scripts. JS excluded until its codegen is de-pointered (§2.1) — its IC/shape-cache cells are per-instantiation mutable state and need a design of their own (likely: journal them as BSS-like allocations reset per load).

---

## 6. Decision ledger

| ID | Decision | Status |
|---|---|---|
| MC1 | Level 1: persistent in-process cache of **imported modules** (not main), keyed by canonical path (+mtime/size); name pool promoted to batch lifetime; module indices never compacted; no eviction in v1 | **Decided** |
| MC2 | Level 2: switch to `MIR_set_lazy_gen_interface` + opt-level tiering, gated by a 4-way profile experiment; lazy-BB held in reserve | **Approved experiment** |
| MC3 | MIR-bitcode disk cache (`MIR_write`/`MIR_read`) as its own level | **Dropped** — caches the wrong phase (`MIR_gen` dominates); capability noted as optional add-on (§7) |
| MC4 | De-pointering refactor of `transpile-mir.cpp` (P1–P5) | **Prerequisite for Level 3**, worthwhile independently |
| MC5 | Level 3 Route A (C source → system compiler → dylib) | **Rejected** — no new C toolchain in production; legacy-backend double-maintenance trap |
| MC6 | Level 3 Route B (code-image + relocation journal) | **Decided direction**, long-term; eager-at-opt-2 for cache-writing compiles |
| MC7 | All native cache artifacts are local derived caches keyed by build ID + content hash; never distributed | Inherited from **P14**, `Lambda_Desing_Native_Module.md` |
| MC8 | JS cross-process caching | **Out of scope** until JS codegen de-pointering; JS participates in Level 1 only |

## 7. Sequencing

1. **Level 1** (now): teardown changes + persistent registry + name-pool lifetime. Exit gate: `make test-lambda-baseline` 100%, batch wall-time measured before/after with `LAMBDA_PROFILE=1`.
2. **Level 2 experiment** (immediately after; independent code path): 4-way profile matrix. Exit gate: no baseline regressions, thunk-address verification (§4.2), measured `mir_gen_ms` reduction.
3. **Re-scope checkpoint:** if L1+L2 satisfies the batch use case, Level 3 targets production cold-start only.
4. **De-pointering refactor (MC4):** land P1–P5 fixes behind baseline green; this also de-risks the in-process cache (removes reliance on pool-address stability).
5. **Level 3 Route B:** sidecar serializer → journal hooks → loader (macOS first, then Linux, then Windows) → verifier/canary mode.

Optional at any point after step 4: a thin MIR-bitcode cache (`MIR_write_module` + sidecar) if profiling ever shows front-end cost dominating for a workload (e.g. very large scripts on lazy gen, where `MIR_gen` is deferred but parse/transpile is not).

## 8. Open questions

- **OQ1:** Does the GC-root registry get fully reset with the heap, or can stale `_gvar_` root registrations survive `runtime_reset_heap`? (L1-c correctness; verify before relying on it.)
- **OQ2:** Thunk-address identity under lazy gen — any runtime site that assumes a generated function's address is its final code address (`func_name_map`, JS interop, profiling)?
- **OQ3:** Can Route B journal intra-module references as a single image-base delta (position-independent emission), or does MIR's emitted code require per-site fixups? Needs inspection of `mir-gen` target code emission.
- **OQ4:** Name-pool growth bound over very large batches once it is never reset — need a high-water metric before declaring "grow-only" acceptable.
- **OQ5:** For Route B, where does the cache live — `~/.lambda/cache/` vs per-project `./temp/`? (tree-sitter precedent suggests per-user; production deployments may want per-app.)
