# LambdaJS — Compilation Pipeline & Phase Model

> **Last verified against tree:** 2026-09-23

> **Part of the [LambdaJS detailed-design set](JS_00_Overview.md).** This document covers the default MIR lane from JavaScript source to native code or interpreted MIR, plus its boundary with the explicit AST backend: entry points, per-compile state, multi-phase lowering, MIR-interpreter-vs-JIT policy, and symbol resolution.
>
> **Primary sources:** `lambda/js/js_transpiler.hpp` (`build_js_ast_indexed`), `lambda/runtime/ast-core.hpp` / `ast-core.cpp` (`AstIndex`), `lambda/runtime/compiler_pass.cpp`, `lambda/js/js_mir_entrypoints_require.cpp`, `lambda/js/js_mir_module_batch_lowering.cpp` (`transpile_js_mir_ast`), `lambda/js/js_mir_context.hpp` / `js_mir_internal.hpp` (`JsMirTranspiler`), `lambda/js/transpile_js_mir.cpp` (globals anchor), `lambda/mir.c` + `lambda/sys_func_registry.c` (import resolution), and `lambda/main.cpp` (CLI dispatch).
> **Audience:** engine developers. **Convention:** `file:line` references are accurate as of this writing but drift; treat them as starting points, confirm against the symbol name.

---

## 1. Purpose & scope

LambdaJS reuses Lambda's `Item` value model, GC heap, name pool, module registry, event-loop host, shared AST substrate, compiler-fact scaffolding, `MirEmitter`, and MIR JIT under **D1.2–D1.3**, **D8.1.3v19**, **D8.2.4v2**, and **D8.2.5v3**. It retains its own JavaScript front end and semantic lowering. Under **D8.2.2** and **D8.2.4v2**, P1b reserves shared `AST_NODE_ASSIGN` for its single `AstAssignNode {op, left, right}` contract and moves Lambda declarations to `AST_NODE_VARIABLE_DECLARATOR`; P1c extends that physical assignment record to Lambda's procedural assignment tags, with the common visitor owning every assignment's `left/right` children; P1d publishes `AST_NODE_LOOP`/`AstLoopControlNode {form, init, test, update, body}` for while/do-while/JS C-style for, while iterator clauses use `AST_NODE_FOR_CLAUSE`. A `.js` source reaches this lane through the explicit `js` / `js-test-batch` CLI subcommands, through `require`/`import` and `load_js_module`, or through the explicit AST backend. This document maps the MIR compilation lane; the lowering *mechanics* are in [JS_04 — MIR Lowering & Code Generation](JS_04_MIR_Lowering.md), and the front end is in [JS_02 — Parsing, AST & Front-End](JS_02_Parsing_AST.md).

---

## 2. End-to-end pipeline

<img alt="Compilation pipeline overview" src="diagram/pipeline_overview.svg" width="318">

Source bytes flow through parse → AST/build-time binding → early-error validation pass → shared AST index → context setup → import resolution → compile-unit opening → multi-phase lowering → link → execution of `js_main` → event-loop drain → result. The ordinary MIR implementation is `transpile_js_to_mir_core_profile_len`; wrappers delegate through `transpile_js_to_mir_core_len`. Ordinary source, pre-built-AST, and ES-module entries share `js_mir_open_compile_unit` for MIR context/transpiler/module ownership; dynamic source (eval, `Function` constructors) never opens a MIR compile unit (**D8.1.3v20 / JSI35**). Script declaration-snapshot publication and batch/preamble declaration snapshots share `js_preamble_entries_from_module_consts` for the owned map-to-array copy, while finalized MIR volume accounting uses the cross-language `mir_count_module_volume` walk; mode-specific registry/TLA and lexical/global preamble policy remain active consolidation residue under **D8.2.5**.

`JS_EXECUTION_BACKEND=ast` is the explicit fail-closed AST lane under **D8.1.3v19**. The unset selector and `JS_EXECUTION_BACKEND=auto` select retained AST execution for interpreter-supported units and their static import closure, whose independent leaves are prebuilt as AST-only `InputScriptCache` templates under **D8.5.1v7**. Unsupported AUTO units fall back to whole-module MIR; `JS_EXECUTION_BACKEND=mir` remains the explicit MIR selector. AUTO additionally promotes an admitted closed, synchronous classic top-level function declaration or expression to a boxed MIR satellite at `JS_JIT_THRESHOLD` calls (default five). The local-data slice admits non-spread arrays, data objects with static or local computed keys, static named member chains, and computed members whose receiver/key satisfy the same local scan; MIR object/Reference lowering preserves source order and `ToPropertyKey` before the existing property kernel. It never transfers an active AST frame and pins unsupported definitions to AST.

The control/data flow, step by step (CLI `lambda js script.js`):

1. `main.cpp:1699` → `transpile_js_to_mir_len` → `transpile_js_to_mir_core_len`.
2. Copy source into an owned buffer; for a real file lacking an explicit `var __filename`, compute the realpath and **inject `var __filename` / `var __dirname`** after the directive prologue (CommonJS ergonomics) (`js_mir_entrypoints_require.cpp:374`).
3. Resolve interpreter env flags once and cache them (`:421`).
4. `js_transpiler_create` → `js_transpiler_parse` (first-party C parser) → direct `JsAstNode` publication, scope reconstruction, validation, and index passes. The manager and planning slices record parse-build, bind, validate, index, collect, captures, environment layout, infer, forward declaration, lowering, finalize, and prelink timings under **D8.2.5v3**.
5. Set up or **reuse** the `EvalContext` + GC heap + name pool + `Input` (reuse is the batch hot-reload fast path); set the `_lambda_rt` runtime pointer (`:483`–`:519`).
6. In AUTO, discover the static import closure with the first-party parser and prebuild only its independent AST cache templates; ordinary linking and module initialization remain serial on the receiving runtime. MIR units use the established `jm_load_imports` path.
7. `js_mir_open_compile_unit` performs `jit_init`, installs the optional batch error handler, calls `jm_create_mir_transpiler`, tracks the active owner, publishes the selected name base, and opens a script or ES-module MIR module (see [§4](#4-the-transpiler-context-jsmirtranspiler)).
8. The unit's mode-specific preamble/import policy is applied before lowering; module registry/TLA policy remains in its semantic callers.
9. **`transpile_js_mir_ast`** count-walks functions/classes, allocates exact metadata, fill-walks them, runs the remaining numbered analysis/lowering phases, and finishes/loads the MIR module (see [§5](#5-compilation-phases)).
10. Count `total_insns`; apply shared `mir_select_link_interface` policy (native/lazy-native/interpreter plus large-function O1); validate MIR labels; then call **`MIR_link(ctx, interface, import_resolver)`** for codegen or MIR-interpreter installation.
11. `find_func(ctx, "js_main")` → typed `Item (*)(Context*)` (`:747`).
12. Initialize the event loop, attach the document if any, allocate module-var storage, arm the stack-overflow `sigsetjmp` guard, and **call `js_main`** (`:813`).
13. `js_event_loop_drain` and (document mode) animation-frame drain run **before** `MIR_finish` so JIT'd callbacks remain valid (`:820`).
14. Normalize a float result to int where exact, restore the previous context, tear down (transpiler, then `MIR_finish` / deferred cleanup / preamble-retain depending on mode), and return (`:862`).

---

## 3. Entry points

All are defined in `js_mir_entrypoints_require.cpp` (except the module entry, in `js_mir_module_batch_lowering.cpp`) and return an `Item`. Public declarations live in `js_transpiler.hpp` and `js_mir_internal.hpp` — **not** in `js_runtime.h`.

| Function | Used by | Notes |
|---|---|---|
| `transpile_js_to_mir_core_profile_len` `js_mir_entrypoints_require.cpp:671` | (internal) | Ordinary source pipeline with JS/TypeScript profile policy. |
| `transpile_js_to_mir_core_len` `js_mir_entrypoints_require.cpp:1276` | wrappers | Pure-JS profile wrapper around the core profile pipeline. |
| `transpile_js_to_mir[_len]` `js_mir_entrypoints_require.cpp:1293/1298` | CLI `js`; batch normal tests | Clears preamble mode and delegates. |
| `transpile_js_to_mir_preamble[_len]` `js_mir_entrypoints_require.cpp:1316/1322` | `js-test-batch` harness compile | Snapshots `module_consts` into a `JsPreambleState`; forces `-O3` for the harness. |
| `transpile_js_to_mir_with_preamble[_len]` `js_mir_entrypoints_require.cpp:1445/1451` | batch per-test execution | Pre-seeds `mt->preamble_entries` so a test inherits harness module vars. |
| `transpile_js_module_to_mir` `js_mir_module_batch_lowering.cpp:5459` | `require` / `import()` / `load_js_module` / batch module tests | Own `MIR_context`; runs with `is_module=true`; `js_main` returns the namespace object; publishes through the shared module registry and defers MIR cleanup. |
| `transpile_js_ast_to_mir` `js_mir_entrypoints_require.cpp:403` | TS transpiler | Transpiles a pre-built AST and skips source parsing/import discovery. |
| `load_js_module` `js_mir_entrypoints_require.cpp:1657` | Lambda→JS import | Reads a file, ensures a persistent heap context, and delegates to the module entry. |
| `js_require` / `js_dynamic_import` `js_mir_entrypoints_require.cpp:2098/2223` | generated/runtime calls | Both route module loading through the shared registry and module compiler. See [JS_09](JS_09_Async_Modules.md). |

The preamble mechanism (compile a shared harness once, then compile each test pre-seeded against that snapshot) is the backbone of the test262 batch runner; it is detailed in [JS_16 — Testing & Conformance](JS_16_Testing.md).

---

## 4. The transpiler context (`JsMirTranspiler`)

`JsMirTranspiler` (`js_mir_context.hpp`, exact symbol) is the central per-compile state, allocated by `jm_create_mir_transpiler` in `js_mir_hashmap_scope_utils.cpp`. Key field groups:

- **MIR targets** — `ctx`, `module`, `current_func_item`, `current_func`.
- **Scopes & control flow** — `var_scopes`, `loop_stack`, `for_of_iterators`, and `try_ctx_stack` are dynamic `ArrayList` stacks. Their initial capacities are policy inputs to `jm_create_mir_transpiler`, not depth limits.
- **Collected program** — `func_entries` and `class_entries` are pool-owned exact-sized arrays. `transpile_js_mir_ast` runs `jm_collect_functions` once in count-only mode, allocates from those counts, runs it again to fill, and fails closed on a count/fill mismatch. Source and synthesized class-field functions publish through the shared `AstIndex` `AstFunctionId` table; no callable pointer index or fallback remains.
- **Type inference** — `widen_to_float`, `force_boxed` hash sets; per-function `current_fc`, `in_native_func`.
- **Module state** — `module_consts` (name → `JsModuleConstEntry`), `module_var_count`, `is_module`, `namespace_reg`, preamble seed fields.
- **Closure read-back** — parallel `last_closure_capture_*[512]` arrays remain fixed-capacity (see [JS_05](JS_05_Functions_Closures.md)).
- **Generators** — `gen_state_labels[64]`, `gen_*` registers/offsets (64-state cap; see [JS_08](JS_08_Iterators_Generators.md)).

Supporting records in the same header include `JsFuncCollected`, whose captures and `FnAnalysis::param_types` are dynamically sized but whose constructor-shape evidence retains 16 slots, and `JsClassEntry`, whose methods/fields/static blocks are exact-sized per class. `JsModuleConstEntry`, `JsMirVarEntry`, `JsCaptureEntry`, and `JsTryContext` carry the remaining pass-local metadata.

The companion `JsTranspiler` (`js_transpiler.hpp:40`) holds the parse/AST context (direct-parser state, name pool, scope, and reference-tree fields only in the isolated lane); see [JS_02](JS_02_Parsing_AST.md).

---

## 5. Compilation phases

`transpile_js_mir_ast` is the manager-owned analysis/lower/finalize entry;
workers live across the split `js_mir_*` files. `collect`, `captures`,
`env-layout`, `infer`, and `forward-declare` are individual pass-manager
entries with required/produced facts; this makes a failed or timed stage
unambiguous under **D8.2.5v3**.

<img alt="Compilation phases" src="diagram/compile_phases.svg" width="346">

| Phase | Worker | Responsibility |
|---|---|---|
| collect | `js_mir_collect_compiler_pass` | Allocate exact indexed function/class metadata, collect source and synthetic field-initializer entries, build module constants, and reject count/fill mismatch. |
| 1.0b | (inline `:2070`) | Resolve strict mode per function (own directive / global / class body / parent). |
| 1.1 | (inline `:2111`) | Build `module_consts`; pre-seed from preamble; assign `js_module_vars[]` indices to top-level decls. |
| captures | `js_mir_captures_compiler_pass` / `jm_analyze_captures` | Free-variable detection plus transitive capture propagation over the indexed parent table. |
| env-layout | `js_mir_env_layout_compiler_pass` | Compute shared scope-env layouts, module scope env, and parent-env reuse/link through indexed child adjacency. |
| infer | `js_mir_infer_compiler_pass` / `jm_infer_param_types` | Evidence-based parameter/return inference and native-version eligibility. |
| forward-declare | `js_mir_forward_declare_compiler_pass` | Widen params contradicted by call-site literals, publish variants and forwards, and plan literal shapes. |
| 1.77 | (inline `:4041`) | P6: narrow still-`ANY` params to INT/FLOAT when all call sites agree. |
| 1.78 | (inline `:4119`) | P4b: constructor field-type propagation from `new C(...)` call sites. |
| 1.9 | (inline `:4175`) | `MIR_new_forward` for every function (+ `<name>_n` native forwards) — enables mutual recursion. |
| 2 | `jm_define_function` (`function_class_lowering.cpp:292`) | Emit MIR for every collected function (innermost-first). |
| 3 | (inline `:4199`) | Create `js_main(Context*)`; emit module/script body; emit `MIR_RET` (namespace if module, else completion value); build exception landing pad; finish + load module. |

Detail of capture analysis (1.5–1.7) belongs to [JS_05 — Functions, Closures & Scope](JS_05_Functions_Closures.md); type inference (1.75–1.78) and the native/boxed dual-version scheme are shared with [JS_04](JS_04_MIR_Lowering.md) and [JS_05](JS_05_Functions_Closures.md).

---

## 6. MIR interpreter vs JIT selection

LambdaJS can link a module either to native code (`MIR_set_gen_interface`), lazy native code, or the MIR interpreter (`MIR_set_interp_interface`). The shared `mir_select_link_interface` policy makes this decision for both Lambda and LambdaJS under **D8.1.3v19**.

<img alt="Interpreter vs JIT selection" src="diagram/interp_jit_selection.svg" width="685">

- **Base mode** — `--mir-interp` CLI or `JS_MIR_INTERP=1` sets `g_mir_interp_mode` (pure interpreter).
- **Large-source-at-O0 pre-check** — if O0 and `source_len ≥ LAMBDA_JS_LARGE_INTERP_BYTES` (default **15000**), temporarily flips interpreter on around `jit_init`.
- **Document AST safety gate** — before a DOM realm's preamble runs, the script scheduler preflights any source large enough to exceed `MIR_RADIANT_AST_NODE_THRESHOLD` (**25000**) indexed AST nodes. A qualifying source selects the existing AST executor for the entire realm, before any script can create a closure. The AST/MIR closure ABIs are not generally shared, so the realm-wide gate prevents unsafe mixed-tier callbacks; the restricted local-data P2 satellite is the only admitted exception (**D8.1.3v19**, **D8.2.4v2**). The flowchart starts after this gate. Explicit `--mir-interp` and `LAMBDA_JS_LARGE_INTERP=0` retain their MIR behavior.
- **Post-MIR instruction policy** — interpret if `total_insns > MIR_LARGE_MODULE_INSN_THRESHOLD` (**100000**), or if a document is attached and `total_insns > MIR_RADIANT_INTERP_INSN_THRESHOLD` (**20000**). `document_context = (runtime->dom_doc != NULL)`.
- **Opt-downgrade fallback** — a function above `MIR_LARGE_FUNCTION_O1_THRESHOLD` (**20000**) lowers only generator optimization from O2 to O1.
- **Lazy** — `JS_LAZY_MIR≠0` selects `MIR_set_lazy_gen_interface`; its optimization-level caveats and measurements are recorded in [JS_15](JS_15_Performance.md).

**"Link-interface interp" vs "pure interp":** size/document-driven interpretation leaves `g_mir_interp_mode = 0`, so `jit_init` still calls `MIR_gen_init` and only the `MIR_link` *interface* differs. Pure interpreter (`g_mir_interp_mode≠0`) skips `MIR_gen_init` entirely. The rationale (link cost dominates for large/cold modules; the interpreter sidesteps codegen) is covered with measurements in [JS_15 — Performance](JS_15_Performance.md). No backend performs tail-call optimization (JC24), so interpreter and JIT differ only in how deep recursion can go before the JC23 stack guard raises `RangeError`.

---

## 7. MIR import resolution

JIT'd JS code calls C runtime functions (`js_add`, `js_property_get`, …) by name; these are resolved at link time.

- **Emit side** — `jm_ensure_import` (`js_mir_calls_boxing_types.cpp:97`) lazily creates a `MIR_new_proto_arr` + `MIR_new_import` per (name, return type, arg count, arg types) signature, caching both in `mt->import_cache`. The dedup key format is `name#r<ret>#n<nres>#a<nargs>#<argtype>…`; the proto name is `name_p_r<ret>_n<nres>_a<nargs>`. Helper wrappers (`jm_call_N`, `jm_call_void_N`) build the call insn.
- **Resolve side** — `import_resolver(name)` (`mir.c:106`), passed to `MIR_link`, checks a thread-local cross-module map first, then the static `func_map` (both O(1) hashmaps). `func_map` is built once by `init_func_map` (`mir.c:50`) from two registry arrays in `sys_func_registry.c`: `sys_func_defs[]` (Lambda system functions) and **`jit_runtime_imports[]`** — the latter holds ~650 `js_`-prefixed runtime entries (e.g. `{"js_property_get", FPTR(js_property_get)}`).

Adding a new runtime function therefore means: implement it, register it in `jit_runtime_imports[]`, and emit a call via `jm_ensure_import`/`jm_call_N`. Tune8 reduced the JS section of the registry from 547 to 452 entries (see [JS_15](JS_15_Performance.md)).

---

## 8. CLI dispatch & batch mode

`main.cpp` routes by `argv[1]`:

- **`js`** (`main.cpp:2280`) — initializes the runtime and stack guard; parses document/interpreter/diagnostic/optimization options; reads the file; optionally attaches a `DomDocument`; sets `process.argv`; and calls `transpile_js_to_mir_len` (ordinary execution call at `main.cpp:1943`).
- **`js-test-batch`** (`main.cpp:4186`) — persistent-process batch driver. With hot reload it keeps one `EvalContext`/heap across tests. It reads the harness/source/module line protocol and dispatches through the preamble, module, or plain entry under per-test recovery and timeout containment. Full protocol and recovery layering: [JS_16](JS_16_Testing.md).

A **bare `.js` path as `argv[1]` does not** enter the JS pipeline — the default extension dispatch handles `.ls`/document formats. JS is reachable only via the `js`/`js-test-batch` subcommands or from within running code (`require`/`import`/`load_js_module`).

---

## 9. Current consolidation boundary

The fixed function/class arrays and fixed scope/loop/try stacks described by the 2026-07-15 version of this document are retired. The current implementation boundary is:

- `AstIndex` supplies dense node/function/scope/binding/class identity, parent/owner links, preorder subtree and per-function ranges, uses-by-binding, calls-by-callee-binding, member-uses-by-object, function-child adjacency, and structural-descendant queries. Synthetic field initializers retain their source-value descendants in a per-function overlay slice, so range density is never weakened. Its source-span recovery runs only for an orphaned structural edge; sanctioned extension rebuilds the derived tables before use. **D8.2.4v2** requires one stable ID authority.
- `CompilerPassManager` and fact bits cover JavaScript parse/build→bind→validate→index→collect→captures→env-layout→infer→forward-declare→lower→finalize/load→static-prelink. Each planning stage is a separately timed, fact-producing manager pass; runtime module-state linking remains after execution-context activation, so it is not falsely published as a compiler fact. **D8.2.5v3** requires truthful produced facts and one schedule.
- `MirValue`, demands, provenance, representation conversion, and emitter-owned rooting exist. Lambda literal primaries now publish their exact `MirValue` directly; `jm_transpile_expression` and Lambda's identifier, call, control, and extension producers still return `MIR_reg_t` through the remaining legacy boundary. **D2.4.1–D2.4.3** and **D8.2.6** require the full contract at every core expression boundary.
- Dynamic `ArrayList` control stacks and exact function/class/member allocation remove the old silent limits. Remaining explicit semantic/optimization capacities include 64 generator resume labels, 512 closure read-back/TDZ entries, and 16 constructor-shape evidence slots; callers fail closed, fall back, or clamp according to the owning feature.
- Ordinary source, pre-built AST, module, and batch/preamble paths now share compile-unit opening and declaration-snapshot materialization, but still duplicate parts of build/validate/link/cleanup orchestration. Their JavaScript policy differs, but **D8.2.5** requires one lifecycle driver with mode policy as data.

Per **D8.4.1v2**, LambdaJS has no property inline cache. `property_name_cache` only reuses MIR registers for immutable module-name-table loads within one generated function; it does not cache receiver/property lookup. `TypeMap` constructor/transition shapes remain ordinary lookup metadata.

The active consolidation sequence and deletion gates are in [`vibe/Lambda_Design_JS_Unified.md`](../../../vibe/Lambda_Design_JS_Unified.md). The historical partial attempt is retained as [`vibe/impl/Lambda_Impl_Tune_Ast (retired).md`](<../../../vibe/impl/Lambda_Impl_Tune_Ast (retired).md>).

---

## Appendix A — Source map

| File | Responsibility (this doc) |
|---|---|
| `lambda/js/js_mir_entrypoints_require.cpp` | All public entry points; the core pipeline; interp/JIT selection; require/import. |
| `lambda/js/js_mir_module_batch_lowering.cpp` | `transpile_js_mir_ast` (phase driver); module entry; preamble; batch. |
| `lambda/js/js_mir_context.hpp`, `js_mir_internal.hpp` | `JsMirTranspiler` + context structs; thresholds; extern decls. |
| `lambda/js/transpile_js_mir.cpp` | Globals/extern anchor after the J41 mechanical split. |
| `lambda/js/js_mir_hashmap_scope_utils.cpp` | `jm_create_mir_transpiler` (allocation). |
| `lambda/js/js_mir_calls_boxing_types.cpp` | `jm_ensure_import` + call-emit helpers. |
| `lambda/mir.c` | `jit_init`, `import_resolver`, `init_func_map`, `MIR_link`. |
| `lambda/sys_func_registry.c` | `sys_func_defs[]`, `jit_runtime_imports[]`. |
| `lambda/main.cpp` | `js` / `js-test-batch` CLI dispatch. |

## Appendix B — Related documents

- [JS_02 — Parsing, AST & Front-End Validation](JS_02_Parsing_AST.md) — the parse/AST/early-error stages.
- [JS_04 — MIR Lowering, Code Generation & Exceptions](JS_04_MIR_Lowering.md) — phase-2/3 emission internals.
- [JS_05 — Functions, Closures & Scope](JS_05_Functions_Closures.md) — capture analysis (phases 1.5–1.7).
- [JS_09 — Async, Promises, Event Loop & Modules](JS_09_Async_Modules.md) — `require`/`import`, module entry.
- [JS_15 — Performance & Optimization](JS_15_Performance.md) — interp/JIT trade-offs, link cost, caching blockers.
- [JS_16 — Testing & Conformance Infrastructure](JS_16_Testing.md) — preamble + batch runner.
