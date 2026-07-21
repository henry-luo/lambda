# Lambda Runtime — The MIR Direct Transpiler & JIT

> **Part of the [Lambda core-runtime detailed-design set](LR_00_Overview.md).** This document covers the supported code-generation backend: how the typed AST is lowered **directly to MIR IR** (no intermediate C text), how values are kept native or boxed under MIR's immutable-register constraint, the function calling convention and parameter-type inference, the root/number execution side frames, and the `mir.c` JIT integration that links and generates native code. The retired C-text backend is archived in [LR_06 — The C Transpiler](LR_06_C_Transpiler.md).
>
> **Primary sources:** `lambda/transpile-mir.cpp` (the `MirTranspiler`, all node lowerings, boxing, rooting, inference), `lambda/mir.c` (import resolution, `jit_init`/`jit_gen_func`, BSS root registration, debug table), `lambda/transpile_shared.cpp` (shared naming/wrapper helpers), `lambda/lambda.h` (the runtime C-API the generated code calls).
> **Audience:** engine developers. **Convention:** `file:line` references drift; confirm against the cited symbol names. This is the most workaround-dense area of the runtime; the Known Issues section is correspondingly long and is part of the design record, not an afterthought.

---

## 1. Purpose & scope

Lambda is **JIT-only** — there is no tree-walking interpreter. Supported builds lower the AST straight to [MIR](https://github.com/vnmakarov/mir) intermediate representation and hand it to the MIR generator. The old C2MIR sources remain archived but are excluded from core and Jube build configurations.

This doc owns the AST → MIR lowering and the JIT mechanics. The *value representation* the generated code manipulates is owned by [LR_03 — Value & Type Model](LR_03_Value_and_Type_Model.md); the *memory and GC* the rooting machinery protects is owned by [LR_08 — Memory Management & Garbage Collection](LR_08_Memory_and_GC.md); the *runtime functions* the generated code calls (`fn_*`, `array_*`, `push_*`) are owned by [LR_09 — Runtime Builtins](LR_09_Runtime_Builtins.md); the *AST* it consumes is produced by [LR_02 — Parsing & AST Construction](LR_02_Parsing_AST.md).

## 2. The `MirTranspiler` and its state

`MirTranspiler` (`transpile-mir.cpp:122`) is the per-compilation state. It holds the MIR context, module, and current function; several hashmaps (`import_cache`, `local_funcs`, `global_vars`, `native_func_info`, `infer_cache`); a 64-deep `var_scopes[]` array of per-scope variable hashmaps with a `scope_depth`; a 32-deep `loop_stack[]`; register and label counters; a set of pinned registers established in the function prologue (`rt_reg`, `gc_reg`, `consts_reg`, `type_list_reg`); per-module BSS handles (`consts_bss`, `type_list_bss`); the JIT-root cursor `jit_root_next`; and a bag of context flags that steer lowering (`in_user_func`, `in_proc`, `in_pipe`, `in_view_*`, `block_returned`, `native_return_tid`, the `tco_*` set, `current_closure`/`env_reg`, `method_owner`/`self_reg`).

Per-variable state is a `MirVarEntry` (`:105`): `{ reg, root_slot, mir_type, type_id, elem_type, env_offset, is_state_var, state_name_ptr }`. Variables are bound with `set_var` (`:458`) and resolved with `find_var` (`:496`), against the scope stack pushed/popped at `:445`/`:451`. Module-level (global) variables are BSS-backed instead of register-backed: `GlobalVarEntry` (`:288`), accessed via `load_global_var`/`store_global_var` (`:1217`/`:1238`), and created up front by `prepass_create_global_vars` (`:11448`).

---

## 3. Lowering: dispatch and representative nodes

The central dispatch is `transpile_expr` (`:8450`), a switch on `node->node_type`. The principal node lowerings are `transpile_primary` (`:1643`), `transpile_ident` (`:1758`), `transpile_binary` (`:2332`), `transpile_unary` (`:2889`), `transpile_spread` (`:2961`), `transpile_match` (`:3205`), the loop forms `transpile_for`/`transpile_while`, the collection builders `transpile_array`/`list`/`content`/`map`/`element` (`:4046`/`:4298`/`:4457`/`:4647`/`:5004`), `transpile_member`/`index` (`:5375`/`:5467`), `transpile_call` (`:6441`), `transpile_pipe` (`:7484`), `transpile_raise` (`:7805`), and the statement forms `transpile_assign_stam` (`:7926`) and `transpile_let_stam` (`:3670`).

A recurring subtlety: **statements have no value**, but every `transpile_expr` call must yield a valid MIR register. `let`/`var`/assignment/index-assignment/`break`/`continue` therefore synthesize a boxed-null register via `emit_null_item_reg` (`:348`) — the `MIR_T_I64` null moves at `:8504` — rather than returning the invalid sentinel register 0, which would crash the MIR generator with "undeclared reg 0".

---

## 4. Registers, types, and the boxing strategy

MIR registers are only `I64` or `D` (double): `type_to_mir` (`:310`) collapses pointer and float-of-other-width to `I64`, and `reg_type` (`:322`) does the same. **A register's type is immutable once declared** — this single constraint shapes the entire backend. Consequently every value in flight is either a *raw native* (an `I64` or `D`) or a *boxed `Item`* (always `I64`), and the transpiler must track which is which for every register.

The authoritative type oracle is `get_effective_type` (`:2070`). It deliberately **distrusts the AST's static type** in the cases where the AST is known to be stale: for identifiers it consults the live `MirVarEntry::type_id` rather than the AST node; it forces a `MATCH_EXPR` result to `ANY` (so it is always boxed); it forces a call returning NULL to `ANY` (so the result is rooted); and it resolves typed-array element types through `elem_type`.

Boxing is inline for cheap tags and a runtime call for the rest. `emit_box` (`:1058`) dispatches by `TypeId`:

- `emit_box_int` (`:829`) — inline INT56 range-check and tag, `ITEM_ERROR` on overflow.
- `emit_box_bool` (`:885`) — `UEXT8` to clear garbage upper bits, error-check, tag.
- `emit_box_float` routes through canonical `push_d`; `emit_box_int64` and the
  `uint64` sibling use the shared full-domain number-home boxers.
- String/symbol/decimal/binary boxing tags their owned pointers. Datetime
  boxing calls `push_k`, which creates a GC-owned object rather than a number
  home.
- `emit_box_container` (`:1011`) — an identity move; the `TypeId` is already in the object header.

Unboxing is the mirror: `emit_unbox` (`:1114`) emits `it2i`/`it2d`/`it2b`/`it2s`/`it2l` runtime calls, or `emit_unbox_container` (`:1096`, an AND-mask to strip the tag). Tying it together is `transpile_box_item` (`:8074`), the **smart gateway**: given a node, it decides whether `transpile_expr` already returned a boxed Item (return it unchanged) or a native value (box it), via a per-operator decision tree (`:8190`) that must *exactly mirror* the producer logic in `transpile_binary`/`transpile_unary`. Any divergence between producer and gateway is a latent double-box or type-confusion bug.

---

## 5. Functions, calls, and parameter inference

A user function is built by `transpile_func_def`: it creates the MIR function, loads the per-module `consts`/`type_list`/`gc` handles from BSS into pinned registers, brackets the body with a root/number side frame (§6), sets up the parameter scope, and handles closure environments (`env_reg`), methods (`self_reg`), proc multi-value returns, the native return type, and tail-call optimization.

Calls split by kind. A direct call to a known local or imported function is a `MIR_new_call_insn` against that MIR func item; functions with typed parameters or a native return get a `_w`/`_b` wrapper, decided by `needs_fn_call_wrapper` (`transpile_shared.cpp:39`). Indirect and closure calls go through the runtime `fn_call0`..`fn_call3` family (`:7420`) — and only up to three arguments are supported (§Known Issues #3).

**Parameter-type inference** lets untyped functions still compile to native arithmetic. `infer_param_type` gathers evidence and resolves it through the inference cache. The policy is deliberately conservative: a prior speculative-INT guess truncated float arguments at the call boundary, so weak arithmetic evidence stays `ANY`/boxed. The current rule treats every `OPERATOR_DIV` use as positive FLOAT evidence; that is stale because only int/float-domain true division is float, while `integer`/full-width sized division is decimal. `Lambda_Impl_Numbers.md` requires inference to consume the shared numeric-domain result classifier. The existing fixed parameter-count caps remain a separate known issue.

---

## 6. Precise roots and scoped numbers: execution side stacks

<img alt="JIT root-frame lifetime" src="diagram/d07_root_frame.svg" width="720">

Because generated code allocates freely, every live GC-managed local must be reachable across a collection, while wide scalar temporaries need fast storage that can be reclaimed at return. Every context therefore owns two separate stable virtual regions: a precise Item root stack and a raw 64-bit number stack. Every Lambda MIR-Direct and LambdaJS function saves both watermarks and restores them through one epilogue.

- Root-slot counts are lowering-time facts. The prologue calls `lambda_side_stack_ensure`, saves `side_root_top`/`side_number_top`, and bumps the root top once; rooted assignments are inline frame-relative stores.
- Heap/pointer/ANY locals get root slots. Reassignment refreshes the slot, and helper-call boundaries publish all live values before the call. The collector scans only `[side_root_base, side_root_top)`.
- Full-width `INT64`/`UINT64` and out-of-band `FLOAT` payloads use `[side_number_base, side_number_top)`. Generated Item returns copy into a caller-donated canonical home before restoring the complete callee watermark; containers and closure environments own analogous scalar tails. `DTIME` is owner-backed and never enters this number extent: dynamic values are GC-owned and static Mark values are Input-arena-owned.
- All generated returns branch to one epilogue, including error, generator/async suspension, handler, and TCO-controlled paths. Batch crash-recovery boundaries restore an outer side-stack snapshot because a signal `longjmp` bypasses normal generated epilogues.
- Module-level BSS globals cannot use a per-call frame, so `register_bss_gc_roots` still registers them after linking.

The two transpilers emit through `mir_emitter_shared.hpp`; the old heap `JitGcRootFrame` block/cache machinery has no users and is deleted. Static frame telemetry is available through `LAMBDA_MIR_LOG_FRAME_SLOTS`. Runtime details are in [LR_08](LR_08_Memory_and_GC.md).

---

## 7. `mir.c` — JIT integration

`mir.c` (523 lines) is the thin C layer between the transpiler's MIR module and executable native code:

- **Import resolution (O(1)).** `init_func_map` (`:50`) builds a hashmap from the `sys_func_defs[]` table (each entry's `c_func`/`native_func`) and the `jit_runtime_imports[]` list. `import_resolver` (`:106`) consults a thread-local `dynamic_import_map` first (cross-module functions and variables, registered by `register_dynamic_import`, `:91`) and then the static map; a miss logs `failed to resolve native fn/pn`.
- **Init / teardown.** `jit_init` (`:128`) builds the map, calls `MIR_init`, then `MIR_gen_init` and sets the optimization level. The environment variable `JS_MIR_INTERP=1` flips `g_mir_interp_mode` to run the MIR *interpreter* instead of JIT-generating native code — useful for debugging codegen. `jit_cleanup` (`:345`) tears it down.
- **Codegen.** `jit_gen_func` (`:252`) loads each module, calls `MIR_link(ctx, MIR_set_gen_interface, import_resolver)` to bind imports, and runs `MIR_gen` on the target function to produce native code.
- **Symbol lookup & debug.** `find_func`/`find_func_prefix`/`find_import`/`find_data` (`:294`–`327`) locate generated symbols; `build_debug_info_table` (`:420`) collects function addresses, sorts them, and derives end addresses for native-stack symbolication (used by the error/stack-trace machinery in [LR_10](LR_10_Error_Handling.md)).

The retired C2MIR entry remains guarded source only; no supported build defines `LAMBDA_C2MIR`.

---

## 8. Level 1 module cache

MIR Direct has a process-local Level 1 module cache for long-lived `Runtime` instances, primarily `lambda.exe test-batch`. Main scripts are still compiled per batch entry, but cacheable Lambda imports retain their `Script`, AST pool, type list, MIR context, generated `main_func`, and direct-import list across per-script heap resets. On a later import of the same canonical file path, `load_script` returns the retained script instead of parsing, AST-building, lowering, linking, and generating it again.

The cache is intentionally narrow: it applies only to MIR Direct Lambda imports. JS, C2MIR, and cross-language-tainted module subtrees are not retained. Runtime heap state is not retained; before each module init, module-level BSS globals are zeroed and registered as GC roots so cached code recomputes heap-backed values for the current script run. Execution uses the current main script's direct-import cone, not the whole registry, so unrelated cached modules are neither rooted nor initialized.

Invalidation is mtime/size based. A file-backed cache hit stats the canonical path; if the source changed, the stale script and retained dependents are retired from the index and the current load falls through to a fresh compile. The cache is enabled by default in both debug and release builds (`LAMBDA_MIR_CACHE_DEFAULT=1` unless a build opts out). `LAMBDA_DISABLE_MIR_CACHE=1` disables retained import caching for timing comparisons while keeping normal import deduplication and circular-import detection within a single compilation.

Design and rollout details live in [Level 1 MIR Cache — Implementation Plan](../../../vibe/Lambda_Impl_MIR_Cache_L1.md).

---

## 9. Naming & the shared helpers

Both backends share `transpile_shared.cpp`, which was extracted precisely so the C-text backend can be excluded from the core build. It provides the generated-identifier naming used by both: `write_var_name` (`:95`, the `_`-prefix for user variables), `write_fn_name_ex`/`write_fn_name` (`:72`/`:91`, name + `ts_node_start_byte` offset for uniqueness, with an `m<index>.` prefix for imported functions), plus `has_typed_params` (`:14`) and `needs_fn_call_wrapper` (`:39`). Both backends also share the `sys_func_defs[]` table, the runtime function set, and the `mir.c` import resolver — so a runtime function added once is reachable from either path.

---

## Known Issues & Future Improvements

The MIR Direct backend carries a large, deliberate set of workarounds. They cluster around three structural facts: MIR's immutable register types, the dual native-or-boxed value representation, and GC rooting under a non-moving collector.

1. **Numeric semantic result and physical representation are still coupled.** `get_effective_type`, `transpile_binary`, and `transpile_box_item` contain separate repairs for runtime helpers that return boxed Items even when the AST names `INT64` or another concrete numeric type. The latest promotion model adds `integer`/decimal results for full-width mixed arithmetic and `/`; all three sites must consume one shared result-domain decision or a raw register can be mistaken for an Item (and vice versa).
2. **"undeclared reg 0" guard.** Value-less statements would otherwise return the invalid register 0 and crash MIR; `emit_null_item_reg` (`:344`) synthesizes a boxed-null register instead. The same hazard recurs in `match` (`:3484`) and the let/var/break/continue null-move blocks (`:8504`).
3. **Indirect calls cap at 3 arguments.** `transpile_call`'s dynamic/closure path logs `mir: calls with >3 args not yet fully supported` and returns the wrong value (`:7466`). Direct calls are unaffected; only `fn_call`-dispatched closure calls hit this hard cap. A bare-expression spread is likewise "not yet handled" (`:9083`).
4. **Typed-array construction gap.** MIR Direct always builds a generic `Array*`; it never emits `array_int()`/`array_int64()`/`array_float()`. This produces a runtime-type divergence from C2MIR in reductions like `fn_sum`/`fn_min`/`fn_max`. Element access and mutation have partial fast paths gated on an `elem_type` proven through `fill()` narrowing or mutation analysis, guarded by `safe_native_int` (`:5736`, `:6036`), with frequent `item_at`/`fn_array_set` fallbacks — and the AST's `nested` type is explicitly distrusted after mutation (`:5737`).
5. **Type widening is truncate-or-box.** `transpile_assign_stam` (`:7973`) assigns a FLOAT to an INT variable by truncating via `MIR_D2I` *inside loops* (lossy, but required to keep the register type stable) and by boxing to `ANY` *outside* loops. This is the most architecturally impactful divergence from C2MIR. A related sharp edge: an error Item (e.g. from division by zero) is silently coerced to `0`/`0.0`/`false` when a boxed value is unboxed into a native variable (`:8000`).
6. **`get_effective_type` only narrows IDENTs to ANY** (`:2137`); it does not catch every post-mutation type change, leaving a stale-type boxing hazard for non-identifier expressions.
7. **MATCH and vectorized-comparison results are forced boxed** (`:2077`, `:2200`) to prevent callers re-boxing an already-boxed value and then dereferencing it as a pointer.
8. **Inference caps.** `NativeFuncInfo` stores at most 16 parameter types (`param_types[16]`/`param_mir[16]`, `:278`), `InferCacheEntry` likewise caps at 16 (`:301`), and `transpile_func_def` copies at most 32 parameter names (`param_name_copies[32]`, `:10580`) — all silent truncations past the cap. The MIR Direct inference engine is also entirely separate code from the C2MIR call-site inference, with no shared table.
9. **Precise-root correctness is type-driven.** BUG-001's heap-frame growth hole is closed by static side-stack slots and publish-before-call lowering. The remaining invariant is that any register carrying a heap-capable boxed value must retain a heap/ANY MIR type; the root predicate deliberately roots unknown/manual capture entries pessimistically.
10. **Bitwise ops are special-cased before generic dispatch** (`:6560`) because `SysFuncInfo` has no native-argument-convention field: `band`/`bor`/`bxor` lower to a single MIR instruction, and `shl`/`shr` are guarded against out-of-range shift counts. (Note: the older runtime doc claims these go through `fn_band` calls; the current code lowers them inline.)
11. **`uint8_t Bool` returns need masking.** Runtime functions returning a `uint8_t` bool leave garbage in the upper 56 bits of the MIR return register, so every bool box/unbox must `emit_uext8` (`:874`, `:1121`).
12. **Out-of-bounds index semantics differ by type.** Integer index fast paths return `ITEM_NULL` on OOB, preserved by a runtime branch in `transpile_box_item` (`:8330`); float index fast paths return `0.0` in a `D` register and cannot branch against an int, so a float OOB read returns `0.0` rather than null (`:5670`).
13. **Fixed-size structural caps.** `var_scopes[64]` (overflow at `scope_depth >= 63` errors, `:151`/`:446`), `loop_stack[32]` (`:155`), hashmap key buffers `char name[128]` that silently truncate long identifiers (`:257`), and `proto_name[140]` (`:587`).
14. **Magic struct offset.** The function prologue loads the GC handle using a hard-coded `heap->gc` offset of 8 with `-Winvalid-offsetof` suppressed (`:10622`) — fragile if `EvalContext` layout changes; it is a magic constant rather than an `offsetof`.
15. **TCO iteration ceiling.** Tail-recursive loops emit a guard that raises a stack-overflow error past `LAMBDA_TCO_MAX_ITERATIONS` (`:10934`) — a hard-coded iteration cap.

Several cross-cutting gaps remain open: numeric result-domain inference is duplicated across AST/MIR/runtime; `SysFuncInfo` has no complete data-driven argument convention, so some return conventions still use ad-hoc switches; and there is no debug-mode validation that a boxed value carries the representation the transpiler believes it does. The Stack API is the physical ownership authority; `Lambda_Impl_Numbers.md` owns the semantic-promotion consolidation.

---

## Appendix A — Source map

| File | Responsibility (this doc) |
|---|---|
| `lambda/transpile-mir.cpp` | The `MirTranspiler`, all AST→MIR node lowerings, inline boxing/unboxing, `get_effective_type`, function/closure/method emission, parameter inference, side-frame emission, TCO. |
| `lambda/mir.c` | JIT integration: import-resolver hashmap, `jit_init`/`jit_gen_func`/`jit_cleanup`, symbol lookup, `register_bss_gc_roots`, `build_debug_info_table`, interpreter-mode switch. |
| `lambda/transpile_shared.cpp` | Generated-identifier naming and call-wrapper helpers shared with the C2MIR backend. |
| `lambda/lambda.h` | The runtime C-API surface (`fn_*`, `array_*`, `push_*`, `it2*`) that generated MIR code imports and calls. |

## Appendix B — Related documents

- [LR_06 — The C Transpiler](LR_06_C_Transpiler.md) — the legacy C-text backend this one replaced as default; shares naming helpers and the runtime function set.
- [LR_03 — Value & Type Model](LR_03_Value_and_Type_Model.md) — the tagged `Item` representation and boxing macros the lowering manipulates.
- [LR_08 — Memory Management & Garbage Collection](LR_08_Memory_and_GC.md) — the non-moving collector and execution side-stack runtime the JIT feeds.
- [LR_09 — Runtime Builtins & System Functions](LR_09_Runtime_Builtins.md) — the `sys_func_defs[]` table and runtime functions resolved by `mir.c`.
- [LR_01 — Compilation Pipeline, CLI & REPL](LR_01_Compilation_Pipeline.md) — how `compile_script_as_mir_direct` and `run_script_mir` fit into the end-to-end run.
- [LR_02 — Parsing & AST Construction](LR_02_Parsing_AST.md) — the typed AST and its (sometimes stale) `Type*` annotations that `get_effective_type` second-guesses.
