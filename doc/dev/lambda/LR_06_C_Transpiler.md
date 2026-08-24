# Lambda Runtime — The C Transpiler (legacy C2MIR backend)

> **Part of the [Lambda core-runtime detailed-design set](LR_00_Overview.md).** This document covers the **legacy code-generation backend**: how the typed AST is lowered to **C source text** that is then handed to an embedded C compiler (c2mir) to produce MIR IR, which the JIT generator turns into native code. It owns the `Transpiler` struct and its `StrBuf` emission model, the per-node `transpile_*` family, the generated-C naming conventions, the `TypeBoxInfo` box/unbox table, the `lambda-embed.h` header-prepend trick, the `define_func`/`define_func_boxed` wrappers, the `_store_i64`/`_store_f64` SSA-reorder workaround, the `is_idiv_expr` unbox case, and the call-site parameter inference. The *default* backend — AST lowered directly to MIR with no C text — is a separate document, [LR_07 — The MIR Direct Transpiler & JIT](LR_07_MIR_Transpiler_JIT.md); this doc deliberately contrasts with it rather than repeating it.
>
> **Historical primary sources:** the former `lambda/runtime/transpile.cpp`, `transpile-call.cpp`, `lambda-embed.h`, and `jit_compile_to_mir` entry in `mir.c` (all removed). `lambda/transpile_shared.cpp` remains only for active MIR Direct helpers.
> **Audience:** engine developers. **Convention:** `file:line` references are historical and drift. **Status warning:** this backend has been removed from the Lambda source tree. No supported core or Jube build defines `LAMBDA_C2MIR`, the CLI does not advertise it, and no test target builds it. The mechanics below are retained solely as a historical design record.

---

## 1. Purpose & scope

Lambda is JIT-only, with MIR Direct as its sole supported backend. The archived **C2MIR** path documented here was the original: it walked the typed AST and emitted C source text into a growable `StrBuf`, prepended the runtime API header, and fed the whole thing to c2mir (the MIR project's embedded C compiler), which produced MIR IR for native generation. **MIR Direct** ([LR_07](LR_07_MIR_Transpiler_JIT.md)) skips the C-text and IR-from-C steps entirely, lowering the AST straight to MIR instructions.

Historical path selection used `runtime->use_mir_direct` and the `--c2mir` CLI flag. The C-text emitter, its embedded header, and the `jit_compile_to_mir` entry have been deleted, so this document describes a non-selectable historical implementation.

This doc owns the **AST → C-text → c2mir → MIR** lowering. The *value representation* the generated C manipulates is owned by [LR_03 — Value & Type Model](LR_03_Value_and_Type_Model.md); the *memory and GC* it allocates against by [LR_08 — Memory Management & Garbage Collection](LR_08_Memory_and_GC.md); the *runtime functions* it calls (`fn_*`, `array_*`, `push_*`, `it2*`) by [LR_09 — Runtime Builtins](LR_09_Runtime_Builtins.md); the *AST* it consumes by [LR_02 — Parsing & AST Construction](LR_02_Parsing_AST.md).

<img alt="AST to C text to c2mir to MIR flow" src="diagram/d06_c2mir_flow.svg" width="628">

---

## 2. The `Transpiler` struct and the `StrBuf` emission model

`Transpiler` is defined in `ast.hpp` and **extends `Script`**; its central field is `StrBuf* code_buf`, the single output buffer into which all generated C is appended. Other state steers emission: `Runtime* runtime`, `current_closure` (non-null inside a closure body), `tco_func`/`in_tail_position` (TCO tracking), `temp_var_counter` (unique-temp counter), `method_owner` (non-null inside a method body), `while_depth` and the `loop_unsafe_vars`/`loop_unsafe_count` set (the swap-safe workaround, §6), plus error-tracking, namespace, and assignment-context fields.

All code generation is **string append**, never structured IR. The emit helpers are `strbuf_append_str` (literal), `strbuf_append_format` (printf-style), `strbuf_append_char`, `strbuf_append_int`, and `strbuf_append_str_n` (N bytes, used for names that are not NUL-terminated). A `transpile_*` function for a construct writes its C fragments directly: e.g. an `if` emits `"("`, recurses on the condition, emits `" ? "`, recurses on the then-branch, emits `" : "`, recurses on the else-branch, emits `")"` — producing a C ternary.

The model leans heavily on **GCC statement-expressions** `({ ...; v; })` for any construct that needs locals but must remain expression-valued: pipes, match, constrained-type checks, and inline collection builders all open a `({ ... })` block, declare internal temporaries, and yield a final expression as the block value. This is the C-text analogue of MIR Direct's register juggling, and it is the reason internal temp names must be chosen carefully (§4).

This is a fundamentally different shape from [LR_07](LR_07_MIR_Transpiler_JIT.md)'s `MirTranspiler`: there is no register/type tracking, no immutable-register constraint, no per-scope variable hashmap, and no JIT-root-frame emission. C variables can be freely reassigned and re-typed by the generated code, so the entire type-widening machinery of MIR Direct (truncate-in-loops / box-to-ANY) has no counterpart here — a C `Item` local can simply be reassigned.

---

## 3. Dispatch and the per-node `transpile_*` family

The central dispatch is `transpile_expr` (`transpile.cpp:7023`), a switch on `node->node_type` that delegates to one `transpile_*` per construct. The principal node functions are `transpile_primary_expr` (`:1254`), `transpile_binary_expr` (`:1613`), `transpile_unary_expr` (`:1539`), `transpile_if`, `transpile_match`/`transpile_match_stam` (`:3882`/`:3949`), `transpile_pipe_expr` (`:2742`), the collection builders `transpile_array_expr`/`transpile_list_expr`/`transpile_map_expr`/`transpile_object_expr`/`transpile_element` (`:4504`/`:4661`/`:5083`/`:5264`/`:5356`), the access forms `transpile_index_expr`/`transpile_member_expr` (`:5447`/`:6056`), and the statement forms `transpile_assign_stam`/`transpile_index_assign_stam`/`transpile_proc_content` (`:4114`/`:4267`/`:4805`).

Call-expression lowering is large enough that it lives in its own file, `transpile-call.cpp` (~1000 lines), split out of `transpile.cpp`: it handles function, sysfunc, method, and dynamic-call emission, argument boxing/coercion, and `fn_call*` dispatch. A direct call to a resolved function/proc reference emits a plain C call; everything else (index/member/call-expr callee, or an unresolved identifier) is a *dynamic dispatch* through the runtime `fn_call*` family, decided by the `is_dynamic_fn_call` predicate (`transpile.cpp:845`).

Function and module emission entry points are `define_func` (`:6313`), `define_func_boxed` (`:6709`, the `_b` wrapper variant, §5), `transpile_fn_expr` (closures), `define_ast_node`, and the top-level `transpile_ast_root` (`:7703`, §8). All nested and closure function definitions are *hoisted to global scope* in the generated C — C has no nested functions — so `transpile_ast_root` pre-defines closure environment structs and then emits every function at file scope.

---

## 4. Naming conventions in the generated C

User-defined Lambda variables are emitted with a leading `_` via `write_var_name` (`transpile_shared.cpp:95`): `let count = 10` becomes `Item _count = ...`. The hard rule this creates: **internal transpiler temporaries must never begin with `_`**, or they collide with a same-named user variable. Internal temps therefore use bare names like `idx`, `pipe_item`, `match_result`, `tco_count`, `self_item`, `cenv` — every one chosen to be `_`-free. (When MIR Direct was extracted, this naming logic was the part deliberately kept *shared*, in `transpile_shared.cpp`, precisely so the two backends agree on symbol names.)

Function names are emitted by `write_fn_name_ex` (`transpile_shared.cpp:72`): a leading `_`, then the function name (or `f` for anonymous functions), then an optional suffix (e.g. `_b`), then a **`_` + `ts_node_start_byte` byte-offset** so the name is unique across the script even when two functions share a base name. Imported functions get an `m<index>.` prefix (`m0._square_42`), keyed on the import script's `index`. `write_fn_name` is the no-suffix wrapper (`:91`).

Module-level statics use the `_` prefix and are **safe** despite the user-variable rule, because they live at C file scope and cannot collide with the always-function-local user variables: `_lambda_rt`, `_mod_consts`, `_mod_type_list`, the init functions `_init_mod_consts`/`_init_mod_types`, and the module-local redirection wrappers (`_mod_map`, `_mod_elmt`, `_mod_const_type`, …) all emitted by `transpile_ast_root` (`:7716`–`:7753`).

---

## 5. The `TypeBoxInfo` box/unbox table

The box/unbox machinery is **table-driven**. `type_box_table[]` (`transpile.cpp:76`) is the single source mapping each `TypeId` to its C type string, unbox function, box function, literal-box function, and zero value — for example `{LMD_TYPE_INT, "int64_t", "it2i", "i2it", NULL, "0"}`, `{LMD_TYPE_FLOAT, "double", "it2d", "push_d", "const_d2it", "0.0"}`, and the container row `{LMD_TYPE_MAP, "Map*", "it2map", NULL, NULL, "NULL"}`. `get_box_info` (`:103`) looks up a row; it returns NULL for `ANY`/`NULL`/`ERROR`/`NUMBER`, which have no fixed representation.

Three thin emitters consume the table: `emit_unbox_open` (`:116`) writes `it2i(`/`it2map(`/… and returns whether it emitted a wrapper; `emit_box_open` (`:129`) writes the `box_fn(` opener, or — for container rows where `box_fn` is NULL — emits a bare `(Item)(` cast, because a container pointer *is* its Item ([LR_03](LR_03_Value_and_Type_Model.md)); `emit_zero_value` (`:144`) writes the `zero_value` for a missing parameter. The container-unbox helper `get_container_unbox_fn` (`:46`) is the analogous Item→native-pointer map (`it2map`/`it2elmt`/`it2arr`/…).

The main boxing emitter is `transpile_box_item` (`:996`). It first **short-circuits** any expression that already yields an `Item`: a dynamic `fn_call*` dispatch (always returns Item), a binary that uses Item-returning runtime functions (`binary_already_returns_item`, `:884`), or a direct call returning Item — for these it just calls `transpile_expr` and skips boxing, avoiding a double-box. Otherwise `try_box_scalar` (`:963`) routes through the table: a captured/optional/closure-param reference is already an Item (emitted directly), a literal uses the `const_box_fn(const_index)` path, and a non-literal uses `box_fn(expr)`. The boxing decision tree thus mirrors the producer logic of `transpile_binary_expr`, exactly the same producer/gateway coupling that MIR Direct's `transpile_box_item` has — but here implemented over C strings rather than registers.

<img alt="C2MIR emit and box machinery" src="diagram/d06_codegen_pattern.svg" width="720">

---

## 6. Two C2MIR-specific workarounds: swap-safe store and idiv unbox

**`_store_i64`/`_store_f64` (SSA-reorder guard).** MIR's SSA optimizer at optimization level ≥ 2 can reorder assignments inside a while loop, breaking swap patterns such as `temp = a + b; a = b; b = temp;` (the lost-copy bug). The fix routes those assignments through opaque external runtime functions that MIR cannot inline or reorder. `analyze_loop_var_safety` (`transpile.cpp:3000`) scans a while-loop body — via `collect_loop_assigns` into a fixed `LoopAssignInfo assigns[MAX_LOOP_ASSIGN]` array (`:3001`) — and marks a variable *unsafe* if any *other* assignment's RHS reads it (a cross-variable read dependency). `transpile_assign_stam` (`:4147`–`:4185`) then emits `_store_i64(&_var, value)` for `INT`/`INT64`/`BOOL` targets and `_store_f64(&_var, value)` for `FLOAT` targets when `while_depth > 0` *and* the variable is in the unsafe set; self-updating variables (`q = q + 1`) stay direct because their phi chains have no cross-dependency. `_store_i64`/`_store_f64` are defined in `lambda-data.cpp` and registered in `mir.c`'s import table. (These same opaque-store functions also serve MIR Direct's value model — see [LR03-3](../../../vibe/Lambda_Issue_Ledger.md).)

**`is_idiv_expr` unbox.** Integer division `fn_idiv` returns a *boxed* `Item` so it can carry a division-by-zero error, but the AST's static type for the expression says `INT`. When the result of such an idiv is assigned to a *native* scalar variable, the boxed Item must be explicitly unboxed. `is_idiv_expr` (`transpile.cpp:863`) returns true only when the boxed `fn_idiv()` path is used — i.e. when the operands are *not* both `INT`/`INT64` (both-int operands use `fn_idiv_i`, which already returns native `int64_t`). `transpile_assign_stam` (`:4205`) consults it and prepends `it2i(`/`it2d(`/`it2l(`/`it2b(` accordingly. This is the C-text counterpart of MIR Direct's `POST_PROCESS_INT64` unbox.

---

## 7. Functions, wrappers, call-site inference, and `lambda-embed.h`

**`define_func` / `define_func_boxed`.** `define_func` (`transpile.cpp:6313`) emits one complete C function: it picks the return type — `Item` for closures and methods (which are dispatched via `fn_call*`), `RetItem` for `can_raise` non-closure/non-method functions (the structured `{value, err}` pair), `Item` for functions with *all-untyped* params (their bodies use Item-level ops), and the native return type otherwise — then writes the name, the parameter list (with a hidden `self_ptr` for methods and `env_ptr` for closures), closure-env extraction, method field loads, optional TCO scaffolding, and the transpiled body. `define_func_boxed` (`:6709`) emits the `_b` **wrapper**: when a function has typed params or a `can_raise`/native-return ABI that `fn_call*` cannot call directly, it generates a `RetItem`-returning wrapper whose params are all `Item`, unboxes them, calls the real function, and re-boxes the result. `needs_fn_call_wrapper` (`transpile_shared.cpp:39`) and `has_typed_params` (`:14`) — both shared with MIR Direct — decide when a wrapper is required.

**Call-site parameter inference.** `infer_proc_param_types_from_callsites` (`transpile.cpp:3447`, invoked from `transpile_ast_root` at `:7778`) lets an all-untyped proc compile to native arithmetic when every call site passes consistently-typed arguments. It collects candidate procs (all-untyped params, not closures, not variadic, not `can_raise`) into a fixed `candidates[MAX_INFER_PROCS]` array, walks the AST to find every call site, intersects argument types per position across all sites, updates each `TypeParam` if they agree on a concrete scalar (`is_inferable_type`, `:3440`: `INT`/`INT64`/`FLOAT`/`BOOL`), and then runs `reinfer_body_types` (`:3099`) to re-propagate types bottom-up so boxed `fn_add`/`fn_ge` calls become native `+`/`>=`. The caps are `MAX_INFER_PROCS 32` and `MAX_INFER_CALL_SITES 64` (`:3053`–`:3054`); past them, candidates are silently dropped. This is **entirely separate code** from MIR Direct's `infer_param_type` — the two inference engines share no table.

**`lambda-embed.h` and the header prepend.** c2mir compiles isolated C text and so needs to *see* the runtime's type and function declarations. `lambda-embed.h` is a bin2c dump — `unsigned char lambda_lambda_h[]` (the raw bytes of `lambda.h`) plus `lambda_lambda_h_len` — `#include`d at `transpile.cpp:7697`. `transpile_ast_root` (`:7706`–`:7708`) writes `#define LAMBDA_C2MIR_RUNTIME 1` and an `extern memcpy` declaration (c2mir cannot parse `<string.h>` or `__builtin_memcpy`), then appends the entire header via `strbuf_append_str_n(code_buf, lambda_lambda_h, lambda_lambda_h_len)`, then `extern Context* _lambda_rt;` and `#define rt _lambda_rt`. The embedded `lambda.h` also carries MIR-friendly typedefs and `extern` libm math declarations guarded by `!defined(__cplusplus)`, since c2mir cannot parse `<math.h>`/`<cmath>`. After compilation, `runner.cpp:668` skips `lambda_lambda_h_len` bytes to find the start of the *real* generated code for debug dumping. **MIR Direct never touches `lambda-embed.h`** — it imports runtime functions via MIR proto/import and the `mir.c` resolver instead. (`temp/_transpiled*.c` is the C2MIR debug dump, the analogue of MIR Direct's `temp/mir_dump.txt`.)

**TCO.** Tail-recursive functions get a flat `goto` loop: `define_func` (`transpile.cpp:6508`) emits an `int tco_count = 0;`, a `tco_start:` label, and a guard `if (++tco_count > LAMBDA_TCO_MAX_ITERATIONS) { lambda_stack_overflow_error(...); return <typed-error>; }`, then rewrites each tail call into temp assignments followed by `goto tco_start`. The iteration ceiling `LAMBDA_TCO_MAX_ITERATIONS` is a hard cap shared with MIR Direct.

---

## 8. The two backends contrasted

| Aspect | C2MIR (`transpile.cpp`, this doc) | MIR Direct ([LR_07](LR_07_MIR_Transpiler_JIT.md)) |
|---|---|---|
| Output | C source text in a `StrBuf` | MIR IR instructions |
| Steps to native | C → MIR (c2mir) → native (2 stages) | AST → MIR → native (1 stage) |
| Boxing | table-driven C macros (`i2it`, `it2d`, `(Item)(ptr)`) | inline MIR (`emit_box_int`) + runtime `push_*` |
| Type widening | free — C locals reassign | truncate-in-loop / box-to-ANY (register types immutable) |
| Typed arrays | **full native** (`array_int_set`/`array_float_get`) | generic `Array*` only (no `array_int()` construction) |
| Embed header | yes — `lambda-embed.h` prepended | no |
| Param inference | `infer_proc_param_types_from_callsites` (call-site) | `infer_param_type` (evidence) — separate engine |
| GC rooting | via runtime/embed model | precise root side-stack ([LR_07 §6](LR_07_MIR_Transpiler_JIT.md)) |
| Debug dump | `temp/_transpiled*.c` | `temp/mir_dump.txt` |
| Selection | archived; excluded from builds/tests | sole supported path |

The archived source shares `transpile_shared.cpp`, the `sys_func_defs[]`/`SysFuncInfo` table, and parts of the runtime declarations with MIR Direct. That source sharing is not a compatibility promise and does not make C2MIR an acceptance target.

---

## 9. Design decisions & rationale

- **C text as IR.** Generating C and reusing c2mir gave Lambda a complete, debuggable backend cheaply: every construct is a few `strbuf_append_*` calls, the output is human-readable C (dumpable to `temp/_transpiled*.c`), and correctness is verifiable by eye. The cost is two compile stages and the embed-header dance; MIR Direct exists to remove both.
- **Embed the header rather than ship it.** Prepending the bin2c'd `lambda.h` makes the generated C self-contained — c2mir needs no include paths and no filesystem — at the cost of a fixed `lambda_lambda_h_len` offset that downstream code must skip past.
- **Table-driven boxing.** `type_box_table[]` keeps the C type string, box, unbox, literal-box, and zero value for each `TypeId` in one place, so the many emit sites stay consistent; container rows fall to a `(Item)(ptr)` cast because the pointer already *is* the Item.
- **`_`-prefix discipline.** A single rule — user vars get `_`, internal temps never do — prevents an entire class of name-collision bugs in generated C, and the rule is enforced by the shared `write_var_name`/`write_fn_name_ex` helpers.
- **Native-when-provable, box at boundaries.** Both backends keep numerics native when types are statically known; the C2MIR difference is that C locals widen for free, so there is no truncate/box machinery — only the targeted `_store_i64` SSA guard and the `is_idiv_expr` unbox.

---

## Known Issues & Future Improvements

Moved to the central ledger: **[Lambda Core Runtime — Central Issue Ledger](../../../vibe/Lambda_Issue_Ledger.md)**, entries **LR06-R1 – LR06-R9** (Appendix A). All of this backend’s recorded issues are obsolete — the C2MIR path has been deleted from the tree.

The ledger carries the verification status of each entry (OPEN / PARTIAL / RESOLVED) against the current source, re-resolved `file:line` anchors, and the cross-cutting clusters that group issues shared with other `LR_*` areas.

---
## Appendix A — Source map

| File | Responsibility (this doc) |
|---|---|
| `lambda/transpile.cpp` | The C-text emitter: `Transpiler`/`StrBuf` model, `transpile_expr` dispatch and all `transpile_*` node functions, `type_box_table[]`/`transpile_box_item`/`try_box_scalar`, `define_func`/`define_func_boxed`, TCO, `analyze_loop_var_safety` + `_store_i64`/`_store_f64`, `is_idiv_expr`, call-site inference, `transpile_ast_root` + the `lambda-embed.h` prepend, GROUP BY stub. |
| `lambda/transpile-call.cpp` | Call-expression lowering for the C2MIR path (function/sysfunc/method/dynamic emission, arg boxing/coercion, `fn_call*` dispatch). |
| `lambda/transpile_shared.cpp` | Naming + wrapper helpers shared with MIR Direct: `write_var_name`, `write_fn_name_ex`/`write_fn_name`, `has_typed_params`, `needs_fn_call_wrapper`. |
| `lambda/lambda-embed.h` | bin2c dump (`lambda_lambda_h[]` + `lambda_lambda_h_len`) of `lambda.h`, prepended to the generated C so c2mir sees runtime declarations. C2MIR-only. |
| `lambda/mir.c` | The c2mir entry `jit_compile_to_mir` (`#ifdef LAMBDA_C2MIR`) and the `c2mir_init` in `jit_init`; shared JIT integration (import resolver, `jit_gen_func`) is owned by [LR_07](LR_07_MIR_Transpiler_JIT.md). |

## Appendix B — Related documents

- [LR_00 — Overview](LR_00_Overview.md) — the core-runtime detailed-design set this document belongs to.
- [LR_02 — Parsing & AST Construction](LR_02_Parsing_AST.md) — the typed AST and `Type*` annotations this backend lowers.
- [LR_03 — Value & Type Model](LR_03_Value_and_Type_Model.md) — the tagged `Item` representation and the boxing macros this backend emits.
- [LR_07 — The MIR Direct Transpiler & JIT](LR_07_MIR_Transpiler_JIT.md) — the default backend this one preceded; shares naming helpers, the runtime function set, and the `mir.c` resolver.
- [LR_08 — Memory Management & Garbage Collection](LR_08_Memory_and_GC.md) — the heap and GC the generated code allocates against.
- [LR_09 — Runtime Builtins & System Functions](LR_09_Runtime_Builtins.md) — the `sys_func_defs[]` table and runtime functions the generated C calls.
