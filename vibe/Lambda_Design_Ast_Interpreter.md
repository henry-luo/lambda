# AST Interpreter — Tier-0 Execution for Lambda and LambdaJS

**Date:** 2026-08-15 (rev 2 — DECIDED by user ruling; spec revision landed same day)
**Status:** **DECIDED 2026-08-15 (user ruling); implementation substantially landed.** Current formal authority is **D8.1.1v5**: ordinary Lambda compilation retains a boxed AST interpreter as T0, `AUTO` may promote eligible hot definitions to boxed MIR satellites, and explicit `jit` retains eager whole-module MIR. General loop OSR and the remaining reference-parser fragment/span migration are still open. This reverses **U26**, which remains struck/superseded in `vibe/Lambda_Design_Unified_AST.md` §12; the restriction in `impl/Lambda_Impl_Tune_Ast (retired).md` is lifted. Ledger **AI1–AI22** remains the design record; **D8.1.1v5** wins on disagreement.
**Design authority:** `doc/Lambda_Formal_Design.md` — **D8.1.1v2** (landed; §15), D1.3/D1.6/D1.7, D5.1–D5.4, D6.1–D6.2, D7.2, D8.2–D8.6, DI14; `doc/Lambda_Formal_Semantics.md` — S1.6/SI3, S3.1, S5.5.1, S7.4/S7.7/S7.11.4, S9.1, S11.2.1, S15.3.
**Working design:** `vibe/Lambda_Design_Unified_AST.md` (§12/U26 — amended by this doc), `vibe/Lambda_Repl.md`, `vibe/Lambda_Design_MIR_Cache.md` + `_L3`, `vibe/Lambda_Design_Stack_Rooting.md`, `vibe/Lambda_Design_Compiling_Return_Value.md`, `vibe/Lambda_Design_Stack_Frame.md`.
**Scope:** Stage 1 = Lambda core (§3–§8, §10–§11). Stage 2 = LambdaJS (§9). C2MIR is untouched per D1.6 and CLAUDE rule 14.

---

## 0. Summary

This design makes a **tree-walking interpreter over the existing typed AST the default execution mode (Tier 0)** for Lambda, and turns MIR Direct native compilation into a **selective, per-function, demand-triggered optimizer (Tier 1)** — a function is compiled when it proves hot (default: 5th ordinary call or 5th direct self-tail edge). The AST becomes the single runtime source of truth; native code becomes a derived cache, exactly in the spirit of D1.7 (*"every compiled artifact is a local derived cache… memoization, not a format"*).

The interpreter is **boxed-only, forever**. It executes by calling the same C-ABI runtime helper library (`lambda-eval.cpp` et al.) that generated MIR code calls, and it lives on the same side-root/number stacks with the same rooting discipline as generated frames. Semantic micro-decisions therefore stay where they already live — in the helpers — and the standing invariant SI3/D3.3.1 (*"erasing every inferred type and running boxed must produce identical results"*) is the correctness contract: the interpreter *is* the boxed lane, executed directly, gated by the existing differential harness.

This reverses U26 (`vibe/Lambda_Design_Unified_AST.md` §12) and D8.1.1 — **ruled by the user and landed as D8.1.1v2 on 2026-08-15**, with the v1 rejection kept as a brief historical footnote in the ruling and the full record retained in Unified_AST §12. §1 records the re-argument against U26's counter-arguments, with evidence that accumulated after U26: the REPL's O(n²) whole-history recompile (LR_01 Known Issue #8), the fact that LambdaJS already needed a size-triggered interpreter escape hatch because *"link-time codegen dominates"* large/cold/document scripts (JS_00 §"JIT with an interpreter escape hatch"), and the growing set of features (const-folding, `that` predicates, debugging, hot reload, REPL, comptime) that all independently want an evaluator that does not require a JIT pipeline warm-up.

## 1. Why revisit U26 — the motivations, re-argued

U26 rejected an AST interpreter on five grounds (`Lambda_Design_Unified_AST.md:805–827`). The five motivations for this design, and what has changed:

| # | Motivation | Evidence today | What T0 changes |
|---|---|---|---|
| 1 | **Turnaround time.** Even MIR-interp mode pays full lowering: parse → AST → whole-module MIR construction → `MIR_link`. For unit/regression suites and pages loading many large scripts, compile time dominates run-once execution. | `MIR_set_gen_interface` generates *every* function of a module during `MIR_link` (`transpile-mir.cpp:25341`, `mir/mir-gen.c:9755`). LambdaJS already auto-selects MIR-interp above 15 KB source at O0, 100k MIR insns, or 20k insns in document context (`runtime/mir_policy.hpp`) — a tiering policy that exists because eager codegen was measurably too slow for exactly the workloads named here. **Measured (JS side, JS_15):** `MIR_link` — eager whole-module codegen, not symbol resolution — is **50–82% of total compile time** on large vendor libraries (lodash: 4.8 s of 6.7 s in link); routing large/cold modules to MIR-interp made them **4–6× faster end-to-end** (lodash 6.7 s → 1.3 s), the Radiant web-template suite ≈3× faster, and the full 39k-test Test262 corpus ≈17% faster summed — short scripts never amortize codegen. Radiant today *skips oversized page scripts entirely* under byte budgets and recompiles byte-identical vendor scripts every load (bootstrap ×116, jQuery ×58/45/30 across the web-template suite). `LAMBDA_PROFILE=1` phase data attributes per-script Lambda-side cost; Lambda-side numbers are a P0 deliverable. | Run-once and cold code executes straight off the AST: no MIR module, no link, no codegen. Only proven-hot functions pay lowering — and each pays it once, individually. |
| 2 | **Memory.** MIR IR is large and generated native code is larger; both are resident per module for the whole session. | The interp thresholds above are *volume*-triggered; `count_lambda_mir_volume` (`transpile-mir.cpp:25188`) exists precisely because MIR insn count is the pressure metric. Test262 crash recovery leaks ~55 MB of MIR code pages per recovery (`main.cpp:3700`) — code-page volume is real. There is no per-module byte metric in-tree — adding one is part of P0 (§11). | The AST is already retained for the `Script` lifetime (`transpile-mir.cpp:25512` handoff; freed only in `runtime_free_script`). T0 therefore adds near-zero marginal memory; T1 allocates MIR + native code only for the hot subset. |
| 3 | **Flexible tooling.** Debugging, profiling, watch/step, hot reload are hard at MIR level — generated code is deliberately immutable (DI14: *"generated code is immutable — never patched"*), which is an asset for correctness and exactly what makes breakpoint patching impossible there. | Source positions already ride on every AST node (`AstNode::node`, a `TSNode`; start row/col readable without the tree). `Script::debug_info` maps native address ranges only — no statement-level map. `vibe/LambdaJS_Hot_Reload.md` wants reload semantics MIR cannot give. | Per-node evaluation is a natural hook point: breakpoints by byte offset, statement stepping, per-node profiling, and hot reload by AST swap (§7). |
| 4 | **Static / comptime / analysis evaluation.** The sanctioned const-folder (D8.1.1, U26 §12.3) is an interpreter fragment by another name. `that` constraint predicates today JIT-compile a `constraint_fn` per constrained type (`TypeObject::constraint_fn`, installed `lambda-data-runtime.cpp:2501`) — so `lambda.exe validate` drags in the whole JIT to run a predicate. | Const-folding exists only on the JS side (`jm_try_fold_const`); Lambda has none. U26 itself KIV'd a "reference interpreter as executable spec". | One engine serves all of it: `EvalMode::CONST` is the const-folder (purity-gated per D6.1.2), `EvalMode::PREDICATE` runs `that` clauses without JIT, and T0 in production *is* the executable-spec oracle, differentially tested continuously instead of KIV (§6). |
| 5 | **REPL / shell.** Every REPL line re-parses, re-builds, re-lowers, re-links, and re-JITs the entire accumulated history (`main.cpp:819` → `run_script_mir(repl_history)`); LR_01 documents it as O(n²) (Known Issue #8). | The history is overwhelmingly run-once top-level statements — the exact workload where compilation is pure overhead. | T0 executes the history directly; functions defined in the session that get hot promote to T1. Incremental top-level *state* (`vibe/Lambda_Repl.md`) remains the orthogonal follow-on that removes re-execution (§8). |

### 1.1 Answering U26's counter-arguments directly

**"An interpreter needs a second, parallel vtable of direct-execution hooks… the two-implementations-of-semantics trap."** The trap is real if the interpreter re-implements semantic micro-decisions. It doesn't. The division of labor in this design: *semantics live in the runtime helpers* (~446 Lambda-relevant C-ABI entries — `fn_*`, `array_*`, `pn_*`, `cow_*`, … — the same ones generated code imports), and both MIR lowering and interpretation are *structural* walks that select helpers. What T0 duplicates is only the walk: node kind → child order → helper choice. Crucially, T0 is boxed-only, so the genuinely hard 80% of the MIR backend — representation inference, the dual-function plan (D8.3.1), lanes, nullable machinery, native specialization, return-value v3 lanes — has **no interpreter counterpart at all**. SI3 guarantees the boxed walk is result-identical by standing invariant, and the differential gate (§10) makes any residual divergence a caught bug, not a silent fork. The in-tree profiling data makes the point quantitatively: on the 261-fixture JS batch corpus, JIT-*generated* code is **0.6% of working CPU** while runtime helpers are **43.5%** — *"generated code is almost pure glue between helper calls; essentially all semantic work happens in C"* (`vibe/jube/JS_Profiling_Helpers.md` §2). A walker that reproduces the glue and calls the same helpers starts from ≈99.4% semantic parity — and ≈99.4% of the runtime cost profile — by construction.

**"Lowering is a single-pass tree walk — the same order of work as one interpretive pass."** Per node visited, roughly yes. But lowering visits *every* node of the module and materializes MIR data structures for all of it, then links and (by default) generates native code for all of it — while interpretation visits only the nodes that actually execute. For run-once code the executed-node count is the same as the lowered-node count and T0 wins by skipping MIR construction + link + codegen entirely; for cold code (functions never called — common in libraries and test scaffolding) T0 visits zero nodes where lowering paid full price. The claim was correct about *lowering* and incorrect about the *pipeline*: measured on real vendor libraries, `MIR_link` alone is 50–82% of total compile time, and on the JS batch corpus compile in total is 33.2% of working CPU (JS_15; `JS_Profiling_Helpers.md`).

**"MIR-interp amortizes over every re-execution of a loop body; a tree-walker pays full price every pass. The AST interpreter would be the slowest of the three options for anything but run-once straight-line code."** Correct, and preserved: that is precisely why T1 exists. The tier boundary is placed so that code which re-executes enough to benefit from amortization crosses it (default: 5th call or direct self-tail edge; backedge escalation per AI8). The design does not argue T0 is fast; it argues T0 + selective T1 dominates eager whole-module compilation on total cost for the workloads in the table above, while `--jit-all` preserves today's behavior for benchmark/steady-state workloads (§5.6).

**"eval via the pipeline is a feature — identical semantics."** Unchanged in spirit: tier choice is invisible (S1.6; the only sanctioned divergence is fault timing, S7.11.4). The deopt/register-materialization problem U26 warned about does not arise because T0 never inspects T1 frames: promotion is function-entry replacement only, never on-stack (§5, AI8).

**"The REPL's cost is elsewhere — re-running history."** Partially true. Re-execution cost is real and is addressed by incremental state (Lambda_Repl.md), not by this design. But the *compilation* share of each line is O(history) too, and it is pure waste for statements that will never re-execute as compiled code. T0 removes that share; the two improvements compose (§8).

**Per-language duplication (LangProfile is MIR-emission hooks only).** Stage 1 is Lambda-only; the interpreter walks the ~80 node kinds Lambda actually produces. Stage 2 (§9) adds a JS walker over the JS-shaped kinds, sharing the frame/tiering/dispatch infrastructure and the JS helper layer, mirroring how the two languages already share `MirEmitter` but keep separate lowering. If/when the unified-AST program converges kinds further (D8.2.2 promotion), walkers converge with them.

## 2. Goals and non-goals

**Goals.** (g1) T0 default execution for scripts, REPL, `validate`, `convert`, layout-embedded scripts. (g2) Per-function demand-triggered T1 with function-entry replacement. (g3) Result identity across tiers, enforced by running the baseline suites under both tiers (SI3/D3.3.1 extended). (g4) One evaluation engine reused for const-folding, `that` predicates, and future comptime (S15.3 `compile()` path). (g5) First-class hooks for stepping, breakpoints, per-node profiling, hot reload. (g6) Strictly lower cold-start time and resident memory on compile-dominated workloads, measured (§10).

**Non-goals.** (n1) No bytecode IR — *"no shared bytecode, no shared object model, no interpreter framework"* stays true across languages (Features J2); within Lambda, the typed AST is the only executable form below MIR (AI22). (n2) No general OSR in v1 — arbitrary interpreter program counters and locals never materialize into MIR frames. A direct validated self-tail call is the narrow exception: it is already a function-entry boundary, so AUTO may hand it to the compiled boxed entry (§5.1; D8.1.1v5). AIO11 still tracks hot inline loops in a once-called `main`. (n3) No unboxed or specialized interpreter lane, ever (AI3). (n4) No inline caches in either Lambda or LambdaJS (**D8.4.1v2**); tier-up swaps data cells and ordinary shape metadata never patches dispatch. (n5) No change to C2MIR (D1.6). (n6) MIR-interp mode is not removed in v1 — it is demoted from size-pressure valve to codegen diagnostic once T0 reaches parity (AI19).

## 3. The tier model

```
            ┌────────────────────────────────────────────────────────┐
            │  Script (single source of truth, lives until teardown) │
            │  AST + NameScope chain + const_list + type_list + CST  │
            └────────────────────────────────────────────────────────┘
                 │                                        │
        default  │                              per-function, on demand
                 ▼                                        ▼
   ┌──────────────────────────┐   promote @ Nth call   ┌──────────────────────────────┐
   │ T0: AST interpreter      │ ─────────────────────► │ T1: MIR Direct native        │
   │ boxed Items, helper calls│                        │ satellite module per function│
   │ side-stack frames        │ ◄───────────────────── │ unboxed entry + _b wrapper   │
   └──────────────────────────┘   calls back through   └──────────────────────────────┘
                 │                lambda_dynamic_call                  │
                 └───────────────► same runtime helper library ◄───────┘
                                   same GC, same side stacks
```

- **T0** — tree-walk over the typed AST, boxed Items only, all operations through runtime helpers. Default for every definition.
- **T1** — MIR Direct native code, compiled one function at a time into "satellite" MIR modules (§5.2) when the definition proves hot. Whole-module eager compilation (today's pipeline) remains available as `--jit-all` / `LAMBDA_TIER=jit` — for benchmarks, for release-mode servers, and as the differential baseline.
- **MIR-interp** — no longer a tier. It survives as a diagnostic for debugging codegen (`--mir-interp`), because it executes the same emitted frames (Stack_Rooting §5.5); the `mir_policy.hpp` size thresholds retire once T0 is default (AI19).

The unit of tiering is the **definition site** (`AstFuncNode`), not the closure instance: many closures share one definition and one counter (AI8, AIO3). Promotion never patches anything: it writes the compiled `_b` entry into the definition-site cell and (lazily) into `Function` values, both plain data (D8.4.1/DI14 respected).

## 4. Interpreter core (Stage 1)

### 4.1 Values, literals, constants

T0 manipulates tagged `Item`s exclusively — the same 64-bit tagged representation, the same helpers to box/unbox, the same COW value semantics (S9.1.2) via the same `cow_*`/`edit_*`/`fn_*` helpers. Two representation facts every walker case must respect:

- **Literals hold no value on the node.** A literal's value lives in the node's `Type*` (`is_literal`, `const_index`) resolved against the *owning* `Script::const_list` — and cross-module `NameEntry→node` links mean "owning" is not always "current" (`declare_module_import` hangs imported declaration nodes into the importer's scope). The interpreter state therefore tracks the current module (`InterpState::module`) and resolves const/type indexes against the declaration's owner, mirroring what lowering does with `_mod_consts_ptr` per module.
- **Wide scalars have homes.** `int64`/`uint64`/out-of-band floats are Items pointing at raw payload words on the number side-stack or in destination-owned tails (LR_08 §4). T0 follows the existing discipline unchanged: produce via `box_*`/`push_d`, store into containers/envs via `owned_item_slot_store`, and resolve callee results into the caller's extent before restoring watermarks — through the same `fn_call*_into` / `lambda_item_resolve_pending` machinery generated code uses (RV12-compatible; nothing new invented).

### 4.2 The stack: frames as side-stack windows

D5.1.1 fixes the stack inventory: native C stack (invisible to GC), the two watermarked side stacks (root stack scanned `[base, top)`, number stack never scanned), heap async frames. **The interpreter adds no fourth mechanism.** An interpreter activation is:

- a C++ `InterpFrame` on the native stack (control state only — no Items), and
- a statically sized **window on the side-root stack** holding every GC-visible value of the activation: `[ param/local slots | operand scratch slots ]`, plus the paired number-stack watermark — exactly the two-watermark frame shape generated code uses (D5.1.2), entered and exited through the existing `lambda_root_frame_begin/end` (or the `RootFrame`/`RootSpan` RAII layer), zero-before-publish, strictly LIFO.

```cpp
struct InterpFrame {                    // C stack; holds no Items directly
    InterpState*        st;
    const AstFuncNode*  fn;             // NULL for module top-level
    Script*             module;         // const_list / type_list / slab owner
    uint64_t*           slots;          // side-root window base (Item lanes)
    uint32_t            slot_count;     // = plan->total_slots
    uint32_t            scratch_top;    // next free scratch slot (debug-checked ≤ plan)
    uint64_t*           number_mark;    // saved number-stack watermark
    InterpFrame*        caller;         // backtrace chain (§7)
    const AstNode*      cur;            // currently evaluating node (backtrace/step)
};
```

**Static frame sizing** comes from a new build-time **frame-plan pass** (§4.3): `total_slots = params + locals + max_scratch_depth`, where `max_scratch_depth` is a Sethi–Ullman-style count over each expression tree — the maximum number of Items that must be held live across a child evaluation or allocating helper call. Scratch depth is a property of expression *shape*, not data size (an N-item element literal accumulates through a rooted list builder, holding O(1) scratch), so the static bound is small and exact. The rules the walker follows are the same ones the JIT prologue and native helpers already obey (Stack_Rooting; D5.3.3):

1. Any Item that must survive a child eval or a MAY_GC helper call is stored to a scratch slot first and **re-read from the slot after** — never cached in a C++ local across an allocation (data buffers move under `gc_compact_data` even though objects don't).
2. Slots are zeroed before the watermark is published; frames exit LIFO; the frame-entry limit check against `side_root_commit_limit` is the same check generated prologues emit, with `lambda_root_frame_overflow_error` as the fail-closed path.
3. Non-Item raw words never go in root slots; wide-scalar payloads use the number stack / owned tails (§4.1).
4. Depth limits: the existing C-stack guard (`lambda-stack.cpp` sigaltstack machinery) and side-stack exhaustion are the two hard stops; T0 adds an explicit recursion-depth budget so ordinary deep recursion produces a clean S7.4.3 fault before either guard fires. Fault *timing* differing from T1 is sanctioned (S7.11.4).

The evaluation discipline is a **hybrid**: control flow recurses on the C stack (`eval_expr` returns `Item`), while GC-visible intermediates live in the frame's scratch window. This keeps the walker readable (no explicit operand-stack machine) while keeping rooting precise and O(1) per value — one frame-relative store, the same cost model as the JIT's `store_gc_root_slot`.

### 4.3 Bindings: the frame-plan pass

Today no local slot indexes exist anywhere — the MIR backend resolves locals by name via per-scope hashmaps at lowering time, and `build_ast` assigns none. T0 adds a **frame-plan pass** to the D8.2.5 pass manager (after capture/effect analysis, before any lowering), producing facts in the designed-but-unfilled homes:

- `NameEntry` gains `slot` (int32) + its `BindingStorage` class (`value_rep.h` already defines `REGISTER / SCOPE_ENV / MODULE / PERSISTENT`): parameters and locals → frame slots; captured-from-outside names → closure env index (the `FnCapture` list build_ast already computes; the pass assigns the `*_slot` fields that today only the MIR backend fills); module-level bindings → module slab index (§4.4).
- `FnAnalysis` gains the `FnFramePlan { param_count, local_count, scratch_depth, total_slots }` — the natural home; `FnAnalysis` is already the designated carrier for per-function facts, and the commented-out `ast_node->locals` at `build_ast.cpp:10333` shows this was anticipated.
- Block-scoped `NameScope`s hanging off `AstListNode/AstForNode/AstWhileNode::vars` flatten into the enclosing function's frame (scopes are compile-time; slots are per-activation). Shadowing gets distinct slots; `let` finality (S9.1.1) means no cell indirection is ever needed.

Identifier evaluation is then `slots[entry->slot]` / `env[entry->slot]` / `slab[entry->slot]` — one indexed load, no hashing, no scope-chain walk at runtime. The pass runs for every function unconditionally (it is cheap: one walk, integer bookkeeping) so its facts are also available to T1 lowering, which today re-derives the same information through hashmaps — a later unification, not a v1 requirement.

### 4.4 Module state: slabs for both tiers

Module-level vars currently live in per-module `_gvar_*` BSS inside the compiled MIR module — storage that does not exist when a module is never compiled. Per D7.2.1 (*"Package state lives in per-context slabs, never at code-baked addresses"*), T0 moves module bindings wholly into the per-context module slab (`EvalContext::module_states` / `LambdaModuleLayout` — the machinery `prepare_context_module_state` already establishes): the interpreter reads/writes slab slots; satellite-compiled functions receive the slab base pointer the same way compiled modules today receive `_mod_consts_ptr` (BSS pointer written after link). Item-holding slab ranges are registered GC roots (the `register_bss_gc_roots` pattern, relocated to slab ownership). Whole-module `--jit-all` mode keeps its current BSS layout during the transition; slab unification for that mode is the end state (AI6, AIO12). Module initialization order and the transaction barrier are unchanged: T0 interprets each module's top level in import post-order inside a `TRANSACTION_BARRIER` recovery frame, exactly as `run_script_mir` arms one per module init today (D7.2.2/S7.7.6).

### 4.5 Functions, closures, and the tier boundary

`Function` (`lambda.h:1107`) is the value both tiers share. Two additions:

- **`def` — the AST definition-site pointer** (`const AstFuncNode*`). Today `Function` has no route back to the AST; D6.2.1 already rules that function *identity* is the static AST definition site, and S5.5.1 equality needs `(module, node)` — so this field is doctrine catching up with the struct, not a tiering hack. It carries: the body to interpret, the frame plan, the promotion state.
- **`FunctionEntryAbi::LAMBDA_INTERPRETED`** — a new entry-ABI tag. Cold functions are created with `entry_abi = LAMBDA_INTERPRETED`, `ptr = NULL`. `lambda_dynamic_call` — already the single dynamic dispatch point, already switching on `entry_abi` — gains one arm: interpreted callees enter `interp_call(ctx, fn, argc, argv)`, which opens a frame per §4.2 and evaluates `fn->def->body`. Every call edge across the tier boundary flows through this helper, so **T1→T0 works today unchanged** (generated code already calls `fn_call*` → `lambda_dynamic_call` for first-class callees), and **T0→T1 is the existing native-invoke arm**. Promotion (§5) stores the compiled `_b` address into `ptr` and flips `entry_abi` to `LAMBDA_BOXED_FUNCTION/PROCEDURE` — a data write on a GC object, unobservable per S1.6, single-threaded per eval context per D5.4.1.

Closures need nothing new: captures snapshot by value at creation into the boxed-Item env with owned-scalar tails (D6.2.3/D6.2.4) via the same `to_closure_named` + `owned_item_slot_store` helpers the JIT emits. The interpreter reads captures as `env[slot]`. Because `let` bindings are final and assignment through a capture is a compile error (S9.1.4 per D6.2.3), **stage 1 has no mutable upvalues, no cells, no write-backs** — a major simplification JS will not share (§9).

Statically-known calls inside T0 (callee resolved to an `AstFuncNode` through `AstIdentNode::entry`) may bypass `Function`-value dispatch: consult the definition-site promotion cell directly — compiled ⇒ boxed native invoke; else push an interpreter frame recursively (still routing argument rooting through a `RootSpan`, as `lambda_dynamic_call` does). This is an internal fast path with identical semantics, not a third ABI.

### 4.6 Dispatch

`eval_expr` is a switch over `AstNodeType`. Lambda produces ~80 of the ~150 defined kinds; the enum is banded, so the switch compiles to dense jump tables per band. Statements (`pn` bodies, top level) run through `exec_stam`, which returns an `EvalSignal`:

```cpp
enum class EvalSignal : uint8_t { NORMAL, RETURNED, BROKE, CONTINUED, ERROR_SKIP };
// RETURNED/ERROR_SKIP carry their Item in a designated frame slot, not a C++ local.
```

`return`/`break`/`continue` are ordinary signal returns up the walker (`pn`-only constructs per S12.1.2); `longjmp` remains reserved for faults (§4.8). Two structural hazards from the AST survey are handled by construction: the AST is a **DAG** (declaration nodes are reachable through both statement lists and `NameEntry->node`) — the walker evaluates only through structural child edges and treats `entry->node` links as *reads*; and generic `(AstNamedNode*)` casts are forbidden in dispatch (the `AstLoopNode` layout divergence — a live-bug pattern `push_name` already documents). Dispatch-table static asserts pin the known enum collisions (`AST_NODE_START` vs `AST_NODE_EVENT_HANDLER`).

Later optimizations — per-node function-pointer threading, node-kind-specialized helper selection cached on the node's `Type*` — are explicitly deferred until profiles ask (AIO10); they change dispatch cost, not semantics, and must never become inline caches in the **D8.4.1v2** sense (no patching, no per-site mutable dispatch state).

### 4.7 Implicit contexts

`~`, `~#`, `last`, `^`, pipe injection, and handler bodies compile against transpiler-only context (`last_index_object`, `building_handler_body`, `pipe_inject_args`, …) that is discarded after build. T0 owns the equivalent explicitly: `InterpState` carries small stacks for *current item*, *current index*, *last-index object*, *current error*, and *pipe argument injection*, pushed/popped by the constructs that bind them (pipes, `for` clauses, `that`/match arms, handler expressions), innermost-wins per S10.1.3. These are Items ⇒ they live in designated frame slots of the construct that pushes them, with `InterpState` holding slot references, so rooting stays precise.

### 4.8 Errors and faults

T0 implements the two existing channels, nothing new:

- **Error-as-value** (S7.4.1): expression interiors let error Items flow (S7.7.1 — *"expression interiors never skip"*); the walker inserts tag checks exactly where lowering emits them today (after calls that can produce errors, at declaration boundaries, at `^`/`is error` sites — the `emit_return_if_item_error` / `emit_jump_if_item_error` placement, reused as a placement spec). `raise` constructs/boxes the error and unwinds to the function boundary via `EvalSignal` (S7.4.2 — only in `T^E` contexts, enforced at build time as today). The `f(...)^` propagate flag suppresses the check and hands the error to the consumer, mirroring `AstCallNode::propagate` handling.
- **Declaration-boundary skip** (S7.7.2): a failed deferred check at `let`/`var`/`for`-var/param/return skips to the end of the *declaring* block, the block yields the error, the binding is never established. In the walker this is `ERROR_SKIP` propagating to the frame that owns the declaring block — a direct transcription of the ruling.
- **Faults** (S7.4.3): stack exhaustion, OOM, depth limits arm and land on the existing `LambdaRecoveryFrame` TLS chain. T0's C code uses `sigsetjmp` naturally (easier than generated code, which has to import `sigsetjmp` as a primitive); `interp_run_script` arms `EXECUTION_BOUNDARY`, each module init arms `TRANSACTION_BARRIER`, `pn ^{}` handlers arm `LOCAL_FAULT` — the same capability set `run_script_mir` uses. A recovery landing restores both side-stack watermarks through the existing checkpoint machinery; the interpreter's C++ frames add no destructors that matter across a fault landing (Items are all in side-stack slots, released by watermark restore).

### 4.9 Construct coverage map

The full walker, by node family (the complete per-kind inventory is the P1 checklist; `emit_ast_dump.cpp` is the reference for a complete Lambda AST traversal):

| Family | Strategy |
|---|---|
| Literals, ident, member/index/field access | const-list resolution (§4.1); slot loads (§4.3); `fn_member`/`fn_index` helpers for dynamic access, `null` totality per S7.1.1v2 comes free from the helpers |
| Unary/binary/comparison/set ops | evaluate operands (left → scratch slot → right), call the boxed helper for the operator (`fn_add`, `fn_eq`, …) — the numeric tower, meets, and int totality (S4.4, SI7) live in the helpers |
| `and`/`or`/`if` | short-circuit walk (S10.2.3); truthiness by tag via the existing helper (S3.1/S3.2) |
| `let`/`pub`, destructuring | evaluate initializer, deferred-check per S7.7.2, store slot/slab |
| `for` comprehension (`where/group/order/limit/offset`, joins), pipes | walk clauses with implicit-context pushes (§4.7); accumulators/group tables are runtime containers built through the same helpers lowering calls; `order` via existing sort helpers (S6 total order lives there) |
| `match` | scrutinee once (S11.2.1), arms in order: literal arms `fn_eq`, type arms `fn_is`, constrained arms base-check then predicate eval (§6.2) |
| List/array/map/element construction | same construction helpers (`list()`, `array_*`, map/element builders + shape machinery); content lists via `AstListNode` walk |
| String patterns, path/query exprs, sys funcs | pattern constants from const pool (prepass-compiled patterns become plan-pass outputs, AIO12); `sys_func_defs[]` gives the C function pointer — direct call, same registry both tiers |
| `pn` statements: `var`, assignment (incl. index/member), `while`/`do-while`, `break`/`continue`/`return` | `EvalSignal` channel; mutation through `cow_prepare_write`-family helpers so COW sharing stays unobservable (S9.1.2); out-of-bounds writes raise per SI11 |
| Imports / module top-level | post-order module init under transaction barriers (§4.4); built-in `math`/`io` and Jube imports resolve at call sites as today (they produce no AST nodes) |
| `start`, async, generators | **not interpreted in v1** — definitions whose analysis says `may_await` / `is_generator` / `needs_task_context` / contains `AST_NODE_START` promote at first call (threshold 0), because suspension today is a MIR-level state-machine transform; interpreter-level continuations (heapified frames à la `LambdaAsyncFrame`) are future work (AI11) |
| Views / templates | modules containing `AST_NODE_VIEW` keep whole-module eager compilation in v1 — registration reaches into the compiled context (`_view_<N>` lookup) (AI12) |
| Error-recovery nodes | T0 refuses to execute a script with `error_count > 0`, same gate as lowering |

## 5. Tier-up: per-function MIR compilation

### 5.1 Trigger

Each `AstFuncNode` (definition site) carries a promotion cell in its `FnAnalysis`: `{ state: INTERP | COMPILING | COMPILED | PINNED_INTERP, call_count, backedge_count, tail_edge_count, void* boxed_entry }`. An ordinary interpreted entry increments `call_count`; at `call_count >= LAMBDA_JIT_THRESHOLD` (default **5**) it compiles synchronously and runs that entry natively. TCO self-tail iterations reuse the active frame, so each validated logical call increments both `call_count` and the separate `tail_edge_count`. On the fifth direct self-tail edge, the runtime compiles the existing eligible satellite, preserves the rooted source argument vector, detaches the no-longer-active T0 frame, and enters the satellite through the ordinary boxed wrapper. This is not arbitrary on-stack replacement: the tail expression has no continuation and its next state is exactly a new function entry. A compile/admission failure keeps the same coerced T0 slots and continues TCO. General loop backedges increment `backedge_count`; crossing their separate budget (default 1024) still promotes only on a later entry. D5.1.2's *"no hotness detection — primitives are unconditionally cheap"* is untouched: that ruling scopes stack primitives; these are per-function counters (D8.1.1v5). Counters are per-eval-context state (D5.4.1), so there is no cross-thread counter traffic.

### 5.2 The satellite module

The promotion unit is a **satellite MIR module**: one Lambda function + its `_b` wrapper, emitted into the Script's existing `jit_context` (MIR supports many modules per context), linked with the existing `import_resolver`. The satellite lowering contract, item by item against what `transpile_mir_ast` does today:

| Whole-module step today | Satellite equivalent |
|---|---|
| `MIR_new_module("lambda_script")` | `MIR_new_module("<fnname>_sat<N>")` per promotion |
| `prepass_create_global_vars` → `_gvar_*` BSS | none — module vars are slab accesses (§4.4); lowering of module-var reads/writes emits slab loads through the module-state pointer |
| `_mod_consts_ptr` / `_mod_type_list_ptr` / `_mod_layout` BSS, written post-link | identical pattern per satellite: tiny BSS pointers, written after `MIR_link` (the post-link BSS write dance already exists at `transpile-mir.cpp:25358–25378`) |
| `prepass_compile_patterns`, property-key specs | pattern/property constants become Script-scoped artifacts produced once (first promotion or load), referenced by satellites like consts |
| `prepass_collect_call_sites` + param narrowing + `FnVariantAnalysis` | run **once per Script, lazily at first promotion**, over the AST (these are AST-level analyses needing no MIR); results persist Script-scoped (AI10). The closed-caller-set narrowing stays sound because the analysis still sees the whole module's AST — laziness changes *when*, not *what* |
| `prepass_forward_declare` + intra-module `MIR_new_ref_op` direct calls | not applicable across satellites: calls to other Lambda functions lower to the dynamic path (`fn_call*_into` → `lambda_dynamic_call`), which tier-dispatches per §4.5. Self-calls (and the TCO loop) stay direct within the satellite. Native→native fast paths across satellites via resolved-address imports are future work (AIO9) |
| module `main` init function | none — module init is T0's job |
| `MIR_link` with gen interface (whole-module codegen) | `MIR_link` per satellite generates just the two functions. Neither known failure of MIR-level laziness applies: `LAMBDA_LAZY_MIR`'s thunk-lifetime hazard (we link on demand with the Script alive by construction), and the measured rejection of `MIR_set_lazy_gen_interface` on the JS side (**≈80× costlier per-function on-demand gen, ≈O(n²) at opt≥2** — JS_15) — that interface re-enters the generator inside a monolithic whole-module context, while a satellite bounds generation scope to its own two-function module. Per-satellite compile latency is still a P2 gate measurement |

The transient `MirTranspiler` session state (var scopes, loop stack, TCO context, …) is already save/restored around each `transpile_func_def`; a satellite session is that machinery pointed at one function. The tables freed at end-of-lowering today (`local_funcs`, `import_cache`, `callsite_info`, `global_vars`) are exactly the ones AI10 promotes to Script lifetime. `ast_index_destroy` moves from pre-handoff to Script teardown so ID-keyed side tables remain valid for late lowering (AIO4 tracks the memory cost).

### 5.3 Entry swap and consistency

On successful compile: write `boxed_entry`, then `state = COMPILED` (release-store; per-context single-threaded anyway). Existing `Function` values created before promotion still carry `entry_abi = LAMBDA_INTERPRETED`; `lambda_dynamic_call`'s interpreted arm consults the def-site cell first and, on `COMPILED`, invokes natively and upgrades the value's `ptr`/`entry_abi` in place (write-once upgrade; unobservable, S1.6). Compile failure sets `PINNED_INTERP` and logs — a compile error at promotion time on a script that passed build is a bug to fix, never a silent behavioral fork; execution continues interpreted. Generated code is never modified after link (DI14); a hot-reload (§7) makes old satellites unreachable rather than editing them.

### 5.4 Compilation scheduling and memory

v1 compiles synchronously at the triggering call — bounded by single-function lowering (small) and forced by simplicity; background compilation on the scheduler is AIO6. Satellites are retained for the `Script` lifetime (immutable code; eviction is a non-problem until measured otherwise). This slot of the design is deliberately aligned with the approved-but-idle lazy direction: D8.5.1 (*"L2 lazy codegen is an approved experiment"*), DO11 (*"lazy slow-path generation belongs with the cache work"*), DO12 (*"per-function granularity"* listed as an L3 open) — tier-up is lazy codegen with a demand signal, and satellites are the per-function units the disk-cache work (D8.5.2 Route B) wants anyway. Per U35, none of the cache work is a prerequisite: T0 attacks cold-start directly.

### 5.5 What T1 keeps

Everything. Satellite-compiled functions are ordinary MIR Direct output: dual-entry planning where the persisted analysis allows (D8.3.1), unboxed params for closed caller sets, the RV12 return lanes, the JIT root-frame prologue — unchanged. The only lowering deltas are the call-site policy (dynamic across satellites) and module-var slab access. In `--jit-all` mode the pipeline is byte-for-byte today's, keeping the MT7 emission ratchet meaningful (AIO2 covers whether satellites get their own budget fixtures).

### 5.6 Policy knobs

`LAMBDA_TIER=auto|interp|jit` (auto = T0 + promotion; interp = never promote; jit = today's eager whole-module — alias `--jit-all`), `LAMBDA_JIT_THRESHOLD=<n>` (default 5 for both ordinary entries and direct self-tail edges), `LAMBDA_JIT_BACKEDGE=<n>` (default 1024 for general loops). `make test` runs the baseline suites under `interp` and `jit` (§10). Benchmarks and release servers document `--jit-all`; `run` mode follows `auto` (its hot inner functions and self-tail bodies promote; a once-called `main` body with hot *inline* loops is the known v1 gap — AIO11).

### 5.7 Whole-script AUTO POC — [measured 2026-08-24]

An opt-in POC tests the alternative promotion policy requested for D8.1.1v4:

```text
LAMBDA_TIER=auto LAMBDA_JIT_THRESHOLD=10 LAMBDA_AUTO_WHOLE_SCRIPT=1
```

At the first eligible threshold trigger, the runtime lowers the complete AST
module once, then publishes the generated `_b` entries for every definition
that passes the existing satellite admission gate. The active T0 frame is not
OSR'd: that trigger call finishes in T0 and later calls use the published
entries. The MIR image is lowered against the already-rooted T0 module slab
(D7.2.1), and the AST index is retained while T0 remains live. This avoids
replacing the live slab with an eager BSS layout. Importing modules remain on
the normal satellite path until dependency images can be promoted atomically;
closures, aggregate/mutable parameters, async/task definitions, and other
satellite-boundary cases remain pinned to T0. This is deliberately opt-in and
is not the shipped AUTO policy.

The release `test_lambda_gtest` gate remained green (**758/758**) in both
modes. Sequential host samples ranged from **33.71–40.17s real** for the
whole-script POC and **32.05–54.48s real** for threshold-10 per-function
satellites, so the end-to-end result is host-load sensitive rather than a
stable win. The direct 740-entry batch produced **24 whole-image compile events
across 20 unique module files** and **226 satellite events**, with every batch
entry ending successfully. On the 13 benchmark entries that emitted
`__TIMING__`, the paired snapshot summed to **5,311.4 ms** for the POC versus
**5,155.2 ms** for per-function promotion (about **3.0% slower**);
`levenshtein2` was the only clear material win in that sample. The result
validates the slab-backed whole-image mechanism and shows no correctness
regression, but does not establish a reliable performance advantage or justify
making whole-script promotion the default. A future iteration needs an
import-cone transaction and a work/profitability gate before reconsideration.

The paired per-script body timings were:

| Script | Whole-image JIT (ms) | Per-function JIT (ms) | Delta (ms) |
|---|---:|---:|---:|
| `awfy/deltablue.ls` | 928.724 | 873.006 | +55.718 |
| `awfy/deltablue2.ls` | 946.564 | 907.655 | +38.909 |
| `awfy/list2.ls` | 12.494 | 9.253 | +3.241 |
| `beng/binarytrees2.ls` | 58.456 | 54.426 | +4.030 |
| `beng/pidigits2.ls` | 5.862 | 1.408 | +4.454 |
| `beng/spectralnorm2.ls` | 128.481 | 118.446 | +10.035 |
| `kostya/json_gen2.ls` | 113.794 | 110.733 | +3.061 |
| `kostya/levenshtein2.ls` | 318.896 | 344.640 | -25.744 |
| `kostya/matmul2.ls` | 2,605.090 | 2,565.190 | +39.900 |
| `larceny/divrec2.ls` | 8.037 | 7.394 | +0.643 |
| `larceny/paraffins2.ls` | 32.320 | 21.102 | +11.218 |
| `larceny/quicksort2.ls` | 135.563 | 126.360 | +9.203 |
| `r7rs/ack2.ls` | 17.136 | 15.553 | +1.583 |

The table reports the `__TIMING__` body result for each script; it excludes
parse, module-load, and JIT compilation time. A positive delta means that the
single whole-image promotion was slower, while a negative delta means that it
was faster. `base642.ls` is intentionally absent because its aggregate
parameter/closure shape was rejected by the promotion gate and remained T0 in
both policies. These are single paired release samples and should be treated
as directional evidence, not as a benchmark ranking.

## 6. One engine: const-folding, `that` predicates, comptime

### 6.1 EvalMode

```cpp
enum class EvalMode : uint8_t {
    RUNTIME,     // full language, effects allowed per fn/pn checking
    CONST,       // build-time folding: pure subset only, fuel-budgeted, no pn, no I/O
    PREDICATE,   // `that` clauses & match guards: pure, fuel-budgeted, ~-bound
};
```

`EvalMode::CONST` **is** the const-folder D8.1.1 sanctioned: same walker, restricted to the pure core subset with the `fn`/`pn` purity bit as the soundness gate — D6.1.2 names that bit *"the const-folder's soundness gate"* verbatim, and U26 §12.3's estimate (*"~95% of argument 4 at ~5% of an interpreter's cost"*) inverts once the interpreter exists anyway: the folder becomes a mode flag, not a second engine. Folding runs in the pass manager over literal-typed subtrees, with a fuel budget and a strict no-effect, no-fault discipline (a folding attempt that raises or exhausts fuel simply doesn't fold). The MIR-cache folding hazard (`Lambda_Design_MIR_Cache_L3.md` — folded data-item REFs kill value-based patching) is a constraint on what folded *pointers* may flow into lowering, inherited as-is.

### 6.2 `that` predicates without the JIT

Constrained-type checks today JIT-compile `TypeObject::constraint_fn` per constrained type and call it from `fn_is`/the validator. `EvalMode::PREDICATE` evaluates `TypeObject::constraint` (already an `AstNode*`) directly, binding `~` per §4.7 — removing the JIT dependency from `lambda.exe validate` and from every `is` on a constrained type in T0. Shipped behavior is unchanged: S11.4.6 rules constrained types *"enforce the base only, for now"*, and this design does not alter that — it changes the *mechanism* available where predicates are already evaluated, and it is the enabling prerequisite if S11.4.6's base-only interim is ever revisited (SO9 stays open, untouched here).

### 6.3 The executable spec

U26 §12.3 KIV'd *"a deliberately slow, obviously-correct reference interpreter as an executable spec… a differential-testing oracle for the JIT"*, to be built only after Phase 4 and never wired into eval. This design subsumes it with a stronger property: T0 is that oracle, and it is not a shelf artifact exercised occasionally — it is the default tier, differentially gated against T1 on every baseline run (§10). `compile(ast)` (S15.3) gains a no-warmup execution path for constructed functions as a natural consequence; S1.8 (*"strings are never code"*) is unaffected.

## 7. Debugging, profiling, hot reload

- **Backtraces.** `InterpFrame::caller` chains give exact interpreted stacks with `cur` node positions (`ts_node_start_point` reads row/col without touching the tree). Mixed stacks interleave native frames via the existing `FuncDebugInfo` address-range map; the unified walker prefers interpreter frames where both views cover the same activation.
- **Breakpoints / stepping.** A per-`InterpState` hook (`on_node`, null in release fast path — one predictable branch) fires with `(node, frame)`; breakpoints match on byte offset from the CST. No code patching anywhere, consistent with DI14. End positions (and node source text) require the live `TSTree` — CST retention is already the status quo; materializing `{start,end}` spans to drop the CST later is AIO5.
- **Profiling.** The def-site counters (§5.1) are already per-function profiles; the JS-side `helper_call_counter` emitter hook is the precedent for cheap inline counting. A sampling mode reading `InterpFrame::cur` gives statement-level self-time without instrumentation.
- **Hot reload.** Because the AST is the source of truth, reload = re-parse/re-build the module, swap `Script`'s AST, reset def-site cells to `INTERP`, drop satellite references (immutable code becomes unreachable; the MIR context tears down with the old Script generation). Closures created against the old AST keep the old generation alive through `Function::def` — generation ownership and module-state identity across reloads are the real design questions and are deliberately deferred to the reload design (AIO7; `vibe/LambdaJS_Hot_Reload.md` owns the product semantics).

## 8. REPL and shell

T0 removes the per-line **compile** share (today: full re-lower + re-link + re-codegen of the entire history per line). The **re-execution** share remains until incremental top-level state lands, which is `vibe/Lambda_Repl.md`'s territory and composes cleanly: with T0, "incremental compilation" (parse-tree caches, function-hash caches, snapshots) stops being the needed mechanism — a persistent interpreter environment (Script kept alive across lines, statements appended, module slab persistent, def-site counters surviving the session so REPL-defined hot functions stay promoted) is both simpler and strictly more useful. That end state is P4 (§11); v1 REPL simply routes the existing whole-history model through T0 and is already strictly faster per line. The shell (`vibe/Lambda_Shell.md`) is the same workload — line-at-a-time, run-once — and inherits the same path.

## 9. Stage 2: LambdaJS (historical design, landed status in D8.1.3v10)

*This section retains the rationale that preceded the LambdaJS walker. Current status is **D8.1.3v10** and `vibe/Lambda_Design_JS_Interpreter.md`: Lambda and JavaScript share the runtime substrate but retain separate semantic walkers, activation records, and completion kinds. On disagreement, those current authorities win.*

**The AST is already shared.** There is no separate JS tree: `JsAstNodeType` is a typedef of `AstNodeType` and `JsAstNode` of `AstNode` (`js_ast.hpp:70–223`) — JS builds the JS-shaped kinds of the unified AST plus ~10 JS-only kinds in the 1000-band (template literals, labeled statements, regex, `with`, tagged templates). Stage 2 is a second walker over the *same* node representation, `NameEntry` symbol table, `Type*` graph, and `AstIndex` — not a new tree. The per-language execution split lives where the lowering split already lives; the dormant `LangProfile` hook table (`ast-core.hpp:884`) is the designated seam if the walkers later want a shared driver.

**Why JS is where the model pays most — measured.** The size policy at the single decision point `js_mir_entrypoints_require.cpp:1043` (plus `g_js_force_document_interp`, which already routes *all* Radiant page JS to MIR-interp, cutting JS compile work ~2–2.6×) is "interpret the cold stuff" in embryo, adopted because `MIR_link` is 50–82% of compile on vendor libraries and the interpreter beat the JIT by ≈17% summed over the full Test262 corpus (JS_15). Radiant additionally *skips oversized scripts outright* under byte budgets and recompiles identical vendor files every page load (`JsMirCache` caches only the preamble). Stage 2 replaces the one-shot static decision with stage 1's promotion cells: the decision point exists; the counters and the interp→satellite promotion path are the genuinely new parts.

**What transfers structurally from stage 1:** precise side-stack/root-frame discipline, the shared runtime and module substrate, debug/profiling hooks, and helper reuse. **D8.1.3v10** does not transfer Lambda's `EvalSignal`, `InterpFrame`, or semantic walker into JavaScript: JS retains `JsInterpFrame`, lexical environments/references, and its own completion kinds. JavaScript throws still use explicit returned completion under **D1.4v3**/**D8.4.3v2**, never C++ exceptions or `longjmp` for ordinary language failure.

**JS-specific work stage 2 must own** (deltas Lambda stage 1 does not have):

1. **Environment records.** JS captures *variables*, not values: by-reference mutation must be visible across sibling closures, with fresh per-iteration `let`/`const` bindings. MIR capture metadata is dynamically sized, while closure read-back/TDZ staging still has explicit 512-entry limits; the interpreter's environment-record chain owns lexical identity directly rather than copying those staging arrays.
2. **Completion semantics.** Labeled break/continue must run IteratorClose for every for-of crossed, `finally` re-raises a saved in-flight error unless its own throw wins, and `with` pushes dynamic scope objects. The walker carries these as JavaScript completion records; the MIR lane uses a dynamic `try_ctx_stack` plus explicit returned error routing.
3. **Suspension.** Generators/async are a compile-time two-function state-machine transform (locals hoisted to env slots, `switch(state)` dispatch, 63-state cap) that does not transfer to a walker. v1 mirrors AI11 — suspension-capable functions promote at first call — with interpreter-native suspension (heapified frames, `LambdaAsyncFrame` precedent) as the eventual replacement, which also lifts the 63-state cap and must interoperate with the existing `js_generator_next`/`js_async_drive`/`js_promise_then` drivers.
4. **`this` / `new.target` / `arguments`.** Today dynamic GC-rooted globals plus synthetic captures for arrows; a walker with real frames should own them per-frame, which needs frame-aware variants of the `js_get_this`-family helpers.
5. **Per-AST-node inline caches are forbidden.** **D8.4.1v2** supersedes the earlier JS-IC direction: the walker calls ordinary JavaScript reference/property kernels, and `AstIndex` side tables hold immutable analysis facts rather than mutable cache cells.
6. **Compile-time facts as a shared pre-pass.** TDZ ranges, Annex-B hoisting, strict-mode resolution, `module_consts` indices are computed inside MIR phases 1.0–1.7c today; stage 2 moves them into shared analysis passes — the same motion AI10 makes for Lambda's call-site/param analyses.
7. **Mixed-tier `eval` and scope visibility.** Direct eval must resolve against interpreter environment records *and* compiled module-var indices when the caller is JIT'd — U26's materialization concern, real for JS and scoped here.
8. **Helper seams that bake compiled artifacts.** `js_new_closure_mir(void* func_ptr, …)` and `js_generator_create(func_ptr, …)` take compiled code pointers; they need interpreter-target variants, and `JsFunction` (like Lambda's `Function`, AI7) gains a `def` AST pointer beside `func_ptr`.

**Primary beneficiaries and the A/B harness.** Test262 (every test still pays full lowering + link today; per-test phase timings are already on the worker wire protocol) and Radiant document JS (per-script `script_runner_timing` exists; forced-interp is the current stopgap). Both A/B measurements reuse existing instrumentation unchanged.

## 10. Performance model and validation

**Expectations, stated honestly.** T0 per-node cost ≈ dispatch + slot traffic + helper call, all boxed. Helper-dominated code (strings, containers, elements, I/O — most data-processing scripts) should land within a small factor of T1-boxed; scalar-arithmetic loop kernels are the worst case (each op = boxed helper call vs a native register op) — expect one to two orders of magnitude on those, which is what promotion is for, and `--jit-all` for the rest. Two measured anchors from the JS side bound the picture: MIR-interp — which shares "no native code" with T0 but has cheaper per-op dispatch than a tree-walk — runs a 50M-iteration hot loop only ≈1.65× slower than JIT yet the full Test262 corpus ≈17% *faster* than JIT summed (JS_15), i.e. for short/cold scripts skipping codegen wins even at interpreter speeds; and generated code is 0.6% of working CPU on the JS batch corpus, bounding what a walker can lose on helper-dominated work. The claim this design actually makes is about **total turnaround**: `parse + build + interpret(executed nodes)` beats `parse + build + lower(all nodes) + link + codegen + execute` whenever executed-node count is within a small multiple of total-node count — run-once scripts, test suites, REPL lines, page scripts. The release-corpus wall-clock comparison below is the current correctness/perf-floor checkpoint; broader Lambda-side performance claims still require the instrumentation that already exists (`LAMBDA_PROFILE` phase profile, `LambdaCompilerTiming` incl. `mir_insn_count`) extended with a T0 execution phase and a resident-memory probe (AIO10).

**Validation gates (D1.10: every invariant names its gate).**

1. **Differential identity:** `make test-lambda-baseline` passes 100% under `LAMBDA_TIER=interp` and `LAMBDA_TIER=jit`, with identical outputs — the SI3/D3.3.1 harness grown a third leg. Divergence in anything but fault timing (S7.11.4) is a release blocker.
2. **GC soundness:** baseline + stress under forced-GC modes (`force_collect_interval`, ASan) with the interpreter driving — every helper call is a potential safepoint (object-zone allocation collects under stress).
3. **Emission ratchet:** `--jit-all` output byte-identical to pre-change (MT7 untouched in v1); satellite budgets decided in AIO2.
4. **Turnaround:** measured corpus — `test/lambda` suite wall-clock, REPL per-line latency at history sizes {10, 100, 1000}, a Radiant page with N scripts, `validate` on a constrained schema — each with before/after and a stated target in the P0 report, not in this doc.
5. **Perf floor:** box2d and the benchmark set under `auto` must reach ≥ today's steady-state (hot functions promoted) and under `--jit-all` must be unchanged.

### Release corpus wall-clock comparison — [measured 2026-08-24]

The complete `test_lambda_gtest` corpus was run against the same release
`lambda.exe` and release `test_lambda_gtest.exe` three times sequentially per
mode. Each run used `./test/test_lambda_gtest.exe --gtest_brief=1`; the
external `/usr/bin/time -p` `real` field is the process wall time. Every sample
passed all **758/758** tests.
The unset environment was the then-shipped AUTO policy specified by
**D8.1.1v4**.

| Mode | Selector | Wall samples | Median |
|---|---|---:|---:|
| AUTO | `env -u LAMBDA_TIER -u LAMBDA_JIT_THRESHOLD` | 32.86s, 36.93s, 41.83s | **36.93s** |
| Full interpreter | `LAMBDA_TIER=interp` | 33.06s, 33.14s, 33.93s | **33.14s** |
| Full JIT | `LAMBDA_TIER=jit` | 36.66s, 37.04s, 43.20s | **37.04s** |

On this corpus, full T0 interpretation is about **11.4% faster** than AUTO
by median wall time. AUTO and eager JIT are effectively tied (0.11s median
difference); the wider sample ranges reflect ordinary host-load variance.
This is a corpus measurement, not a general claim that AUTO dominates either
fixed tier. The correctness result is the release gate for **D8.1.1v4/P5**;
the remaining P2 work is promotion breadth and performance tuning.

### Post-fix AUTO remeasurement — [measured 2026-08-25]

After TCO self-tail iterations began contributing to the definition-site
hotness counter, the same release corpus was rerun three times. AUTO passed
**758/758** at **35.36s, 37.96s, 38.62s real** (median **37.96s**); the matched
full-interpreter control passed **758/758** at **36.05s, 36.82s, 34.61s**
(median **36.05s**). Thus AUTO was **5.3% slower** on this host sample. The
result measures the counter fix's current end-to-end cost; it is not directly
comparable to the prior full-JIT row because that row was not rerun on this
date.

### Threshold-5 tail-handoff remeasurement — [measured 2026-08-25]

With the shipped threshold raised to **5** and the fifth direct self-tail edge
able to enter an eligible satellite during its first activation
(**D8.1.1v5**), a freshly rebuilt release `lambda.exe` and release
`test_lambda_gtest.exe` again passed **758/758** on every AUTO sample:
**61.26s, 52.41s, 47.96s real** (median **52.41s**). A direct release probe
of `loop(20, 0)` logged `satellite compiled function='loop'` during that one
activation and returned `20`; the focused boundary regression also passes
with forced GC and freed-memory poisoning.

These samples were taken on a substantially more loaded host than the earlier
2026-08-25 series, so they are a correctness and current-wall-time record,
not an A/B conclusion against its 37.96s AUTO median. The new policy needs a
same-session interpreter/JIT control before any profitability claim.

### Latest AUTO vs. full-interpreter remeasurement — [measured 2026-08-25]

The release `test_lambda_gtest` corpus was rerun three times per mode in one
consecutive host session, with the same release binaries and `/usr/bin/time -p`
wall-clock measurement. This is the complete **758-test** executable: 740
auto-discovered golden Lambda-script fixtures plus 17 negative-contract tests
and one binary-output test. Every sample passed **758/758**. AUTO used the
unset default selector; the interpreter control set only `LAMBDA_TIER=interp`.
All threshold/backedge and whole-script-POC overrides were unset.

| Mode | Wall samples | Median | Relative to full interpreter |
|---|---:|---:|---:|
| AUTO (**D8.1.1v5**) | 32.55s, 32.41s, 32.39s | **32.41s** | **2.1% faster** |
| Full interpreter | 33.06s, 33.10s, 33.38s | **33.10s** | baseline |

The within-mode spreads are narrow (0.16s AUTO; 0.32s interpreter), making
this a usable current end-to-end comparison: AUTO is **0.69s** faster by
median on this mixed corpus. It does not replace a new AUTO-versus-full-JIT
control, so it makes no claim about eager-JIT profitability.

### Full-JIT stale-binary diagnostic — [measured 2026-08-25]

Before the release runtime was rebuilt, the existing release
`test_lambda_gtest` executable was run three times with `LAMBDA_TIER=jit`;
`LAMBDA_JIT_THRESHOLD`, `LAMBDA_JIT_BACKEDGE`, and
`LAMBDA_AUTO_WHOLE_SCRIPT` were unset. The current tree discovered **741
script fixtures**, producing **759 GTests** including the negative-contract
and binary-output tests.

| Mode | Wall samples | Median | Result |
|---|---:|---:|---|
| Full JIT (`LAMBDA_TIER=jit`) | 35.63s, 41.25s, 35.83s | **35.83s** | **758/759 passed** |

All three runs failed the same `for_at_pairs` fixture: the expected paired
values `a=1`, `b=2`, `c=3` were produced as `a=a`, `b=b`, `c=c`, and the
filtered map result was empty instead of `['b']`. This is a deterministic
full-JIT correctness failure in the stale release artifact, so **35.83s is
diagnostic timing only**, not a passing JIT performance baseline. The source
already contained the S8.1.3 `key_only && !index_name` admission fix; the
release `lambda.exe` had not been rebuilt from that source.

### Full-JIT verification after release rebuild — [measured 2026-08-25]

After `make release` and rebuilding `test_lambda_gtest` as `release_native`,
the focused `for_at_pairs` test passed under `LAMBDA_TIER=jit`. Three fresh
full-corpus runs, with the same unset threshold/backedge/whole-script-POC
overrides, passed **759/759** each:

| Mode | Wall samples | Median | Result |
|---|---:|---:|---|
| Full JIT (`LAMBDA_TIER=jit`) | 41.85s, 36.14s, 42.67s | **41.85s** | **759/759 passed** |

This confirms the source-level S8.1.3 paired-`at` fix is present in the
release artifact; the remaining spread is host-load variance, not a fixture
mismatch. The timing is the current passing full-JIT record for the 741-script
/ 759-test tree (**D8.1.1v5**).

### AUTO promotion-threshold sweep — [measured 2026-08-24]

The release `lambda.exe` was run in `LAMBDA_TIER=auto` over the same **740
script** batch corpus with `LAMBDA_JIT_THRESHOLD` set to 3, 5, and 10. A
script counts as a trigger when its log contains at least one
`interp-tier: satellite compiled` event; every threshold completed all 740
scripts with `BATCH_END 0`.

| Call threshold | Scripts with at least one promotion | Promotion events/images | One-event scripts |
|---:|---:|---:|---:|
| 3 (then default) | 166 / 740 (**22.4%**) | 471 | 83 (**50.0%**) |
| 5 | 109 / 740 (**14.7%**) | 347 | 35 (**32.1%**) |
| 10 | 89 / 740 (**12.0%**) | 272 | 28 (**31.5%**) |

The threshold-5 and threshold-10 full release gates each passed **758/758**.
Their observed external wall samples were 32.84–33.48s and 33.03–33.85s,
respectively. The matched threshold-3 control was 32.82s; the earlier
three-sample AUTO series was 32.86s, 36.93s, and 41.83s. The overlap shows
that host-load variance is larger than the difference between these settings,
so the sweep does not establish threshold 10 as a faster default.

Threshold 3 is effective at finding repeatedly called functions, but it is
aggressive for cold/short functions: raising it to 5 removes 34% of the
then-default promoting scripts and 26% of promotion events. Raising it again
to 10 only removes a further 18% of threshold-5 promoting scripts, while
retaining the high-fanout graph/PDF cases. Threshold 5 is now the shipped
default (D8.1.1v5), but a work-based profitability gate (estimated body cost
and backedge work, plus a per-script satellite/MIR budget) is preferable to a
single global threshold. The existing fail-closed satellite eligibility
policy remains unchanged; once-called hot loops need a separate `main`/OSR
policy because backedge promotion is deferred until the next entry.

## 11. Migration plan

*Detailed implementation plan for P0–P1: `vibe/impl/Lambda_Impl_Ast_Interp.md` (its §6 measurement report is the arc's exit gate).*

- **P0 — skeleton + evidence. ✅ LANDED 2026-08-15.** Frame-plan pass; `InterpFrame`/side-stack integration; walker for the pure L1 core (literals, ident, unary/binary, if, let, call, list/array/map); `LAMBDA_TIER=interp` behind a flag; measurement report (turnaround + memory on the corpus). *Gate met: 81 of 279 corpus scripts run entirely under T0 with golden-identical output, 198 counted fallbacks, **0 divergences**; clean under forced GC.* Measured: **1.69×** faster turnaround on the real-workload subset and **9.4×** on a 1 000-line REPL history (both against native codegen), **11.9–28.5×** on 1k–20k-line run-once scripts against forced native codegen, and resident memory **58× lower** at 20k lines (5.38 GB of MIR IR → 92 MB). Note that the shipped default path routes modules over 100 000 MIR instructions to MIR-interp rather than codegen, so the two largest C2 rows carry a mode column and a separate forced-native baseline. The design's compile-dominance premise is confirmed on the Lambda side; see `vibe/impl/Lambda_Impl_Ast_Interp.md` §6.
- **P1 — full coverage.** Remaining constructs per §4.9 (for-clauses, match, elements, patterns, paths, pn statements, imports/module slabs, sys funcs); error/fault channels; recursion budget. *Gate: validation gate 1 (full baseline differential) + gate 2 (GC stress).*
- **P2 — tiering.** Promotion cells, `LAMBDA_INTERPRETED` entry ABI, satellite lowering contract (§5.2), Script-scoped analysis persistence, entry swap. *Gate: promoted-function outputs identical to interp; perf floor gate 5.*
- **P3 — one-engine unification.** `EvalMode::CONST` folder in the pass manager; `EvalMode::PREDICATE` for `that`; validator de-JIT. *Gate: fold-on/fold-off differential; `validate` runs with JIT never initialized.*
- **P4 — REPL/shell persistent environment.** Script-alive-across-lines, appended statements, persistent slab + counters. *Gate: REPL latency flat in history length.*
- **P5 — default flip + spec. ✅ LANDED 2026-08-24.** `auto` is the unset default; MIR-interp remains diagnostic and `jit` remains the explicit eager path. The release `test_lambda_gtest` corpus is green under the default AUTO policy (**758/758**), including the P2 scalar-module, dynamic-argument, object/procedure, and var-call regressions. **D8.1.1v5 + the implementation-doc status update record the current selector, threshold, tail-handoff rule, and gate** (§15, per CLAUDE rule 17). Stage 2 (JS) design revision opens after this gate.

Each phase is landable and revertible behind `LAMBDA_TIER`; P5 now makes AUTO the shipped default while preserving explicit `interp` and `jit` controls.

## 12. Considered and rejected

- **Bytecode VM.** A third executable form (AST → bytecode → MIR) re-introduces a compile step and a resident IR on the cold path — the two costs this design removes — and contradicts the *no shared bytecode* stance (Features J2). Tree-walking over a typed, banded, already-resident AST is the minimal mechanism; revisit only with T0 profiles in hand (AI22).
- **`LAMBDA_LAZY_MIR` as the answer.** Lazy *codegen* still constructs and links the full MIR module eagerly — it defers only native generation, addresses neither turnaround for build+lower nor MIR-IR memory, and serves none of the tooling/comptime/REPL motivations. It was also measured and rejected on the JS side: `MIR_set_lazy_gen_interface` collapses link but makes on-demand generation ≈80× costlier per function, scaling ≈O(n²) at opt≥2 (JS_15), on top of the documented thunk-lifetime hazard. Satellites bound generation scope structurally instead (§5.2).
- **MIR-interp as the default tier.** Same analysis: full lowering cost, no per-node hooks, no comptime reuse, and no TCO (a known deliberate divergence, JO10). It stays as a codegen diagnostic.
- **Unboxed/specialized interpreter.** Re-implements the representation lane outside `MirEmitter` — precisely the duplication trap U26 warned about, and a violation of the D5.3.4 boundary (*"rooting policy and final store insertion live only in MirEmitter"* for generated code; the interpreter stays on the native-helper side of that line, D5.3.3). Boxed-only, forever (AI3).
- **OSR / on-stack replacement.** High machinery cost (frame materialization into natively-typed registers — the exact problem U26 flagged), low expected value for Lambda's workloads; function-entry replacement plus backedge-triggered next-call promotion covers all but once-called long-running bodies (AIO11).
- **Patching call sites / entry thunks on promotion.** Violates D8.4.1/DI14 (immutable generated code) and buys little over data-cell dispatch through `lambda_dynamic_call`.
- **Conservative stack scanning for interpreter temporaries.** Retired and forbidden (CLAUDE rule 15; D5.3.3's precise-rooting contract). The frame-window design (§4.2) exists so this is never needed.

## 13. Open issues

- **AIO1 — TCO parity in T0.** T1 has TCO; MIR-interp deliberately doesn't (JO10 precedent). v1 T0 ships self-tail-call iteration only; general TCO parity, and whether stack-guard timing differences need more than S7.11.4's allowance, decided before the P5 flip.
- **AIO2 — Emission-ratchet interaction.** Whether satellite modules get their own `mir_budgets.json` fixtures, and how per-function import/BSS glue is budgeted (D8.6.1: 0% slack).
- **AIO3 — Counter placement for closure-heavy code.** Definition-site counters undercount per-instance hotness for factory-produced closures; assess whether value-level escalation is ever needed.
- **AIO4 — `ast_index` retention.** Moving `ast_index_destroy` to Script teardown keeps dense IDs/parents for side tables; measure the memory and decide per-module opt-out.
- **AIO5 — CST retention vs materialized spans.** End positions and re-discrimination reads need the live `TSTree`; decide when/whether to materialize `{start,end}` and drop the tree.
- **AIO6 — Background compilation.** Move promotion compiles off the hot call path onto the scheduler; interacts with D5.4.1 (one context identity) — satellites compile against Script-scoped state, so this looks feasible but is unproven.
- **AIO7 — Hot-reload generations.** Old-generation Scripts pinned by live closures (`Function::def`); module-state identity across reloads; interaction with L1 cache invalidation.
- **AIO8 — Cross-context visibility.** Promotion cells are per-definition (Script-scoped) but consulted per-context; define the story for multiple eval contexts sharing a Script (today: one canonical context per isolate, D5.4.1 — keep it that simple if possible).
- **AIO9 — Satellite→satellite fast calls.** Resolved-address imports when the callee compiled first; multi-version guard chains later (D8.4.1's sanctioned shape). v1 pays the dynamic-call cost across satellites.
- **AIO10 — Dispatch optimization budget.** Threaded dispatch / per-node fn pointers / type-keyed helper preselection — only with profiles, only if T0 time matters after promotion does its job.
- **AIO11 — Once-called hot bodies.** `pn main()` with heavy inline loops never re-enters, so backedge marking never pays. Options: honest documentation + `--jit-all`; per-script pragma; eager-compile heuristic on `run`-mode `main`; OSR (last resort). Decide from real `run`-mode corpora before P5.
- **AIO12 — Satellite module-var/pattern lowering.** The slab-access lowering for module vars in satellites, pattern/property constants as Script-scoped artifacts, and the transition story while `--jit-all` keeps BSS layout (AI6).

## 14. Decision ledger

| # | Decision | Status |
|---|---|---|
| **AI1** | An AST-walking interpreter (T0) is the default execution mode; MIR Direct native (T1) is selective, per-function, demand-triggered. Reverses U26/D8.1.1 (→ D8.1.1v2, §15) | **confirmed** |
| **AI2** | The AST (+ scopes, const_list, type_list) is the single runtime source of truth; all compiled artifacts are derived caches (D1.7 alignment) | **confirmed** |
| **AI3** | T0 is boxed-only, forever; it calls the same runtime helper library as generated code; no unboxed interpreter lane will ever exist (SI3/D3.3.1 as correctness contract) | **confirmed** |
| **AI4** | Interpreter frames are windows on the existing side stacks (root window + number watermark per frame); no fourth stack mechanism (D5.1.1 upheld) | **confirmed** |
| **AI5** | A build-time frame-plan pass assigns `NameEntry` slots/storage classes and static scratch depth onto `FnAnalysis`; joins the D8.2.5 pass manager | **confirmed** |
| **AI6** | Module-level bindings live in per-context module slabs for T0 and satellites (D7.2.1); `_gvar_*` BSS remains only in whole-module `--jit-all` mode during transition | **confirmed** |
| **AI7** | `Function` gains `def` (AST definition site — also discharging D6.2.1/S5.5.1 identity) and entry ABI `LAMBDA_INTERPRETED`; `lambda_dynamic_call` is the single tier-dispatch point; `ptr` NULL until promotion | **confirmed** |
| **AI8** | Promotion trigger: per-definition-site ordinary-call and direct-self-tail-edge counters, threshold 5 (`LAMBDA_JIT_THRESHOLD`); the fifth direct tail edge hands off at an entry-equivalent boundary, while general loop backedges still mark for next-call promotion | **confirmed** |
| **AI9** | Promotion unit: satellite MIR module (function + `_b` wrapper) in the Script's `jit_context`, linked on demand via the existing import resolver, BSS pointers written post-link | **confirmed** |
| **AI10** | Whole-module AST analyses (call sites, param narrowing, `FnVariantAnalysis`) run once per Script at first promotion and persist Script-scoped; lowering-session tables promoted to Script lifetime | **confirmed** |
| **AI11** | Suspension-capable definitions (`may_await`/`is_generator`/`needs_task_context`/`START`) bypass T0 — compiled at first call; interpreter continuations are future work | **confirmed** |
| **AI12** | View/template-containing modules keep whole-module eager compilation in v1 | **confirmed** |
| **AI13** | T0 error handling: error-as-value checks at lowering's check points; declaration-boundary skip per S7.7.1/S7.7.2; faults on recovery frames; only fault timing may differ across tiers (S7.11.4) | **confirmed** |
| **AI14** | `return`/`break`/`continue` travel as `EvalSignal` through the walker; `longjmp` is fault-only | **confirmed** |
| **AI15** | Implicit contexts (`~`, `~#`, `last`, `^`, pipe injection, handlers) are explicit slot-backed stacks in `InterpState` | **confirmed** |
| **AI16** | The sanctioned const-folder is this engine under `EvalMode::CONST`, purity-gated (D6.1.2), fuel-budgeted — one engine, two modes | **confirmed** |
| **AI17** | `that` predicates evaluate under `EvalMode::PREDICATE` instead of JIT-compiled `constraint_fn`; S11.4.6 base-only shipped behavior unchanged | **confirmed** |
| **AI18** | The U26-KIV reference interpreter is subsumed: T0 is the executable-spec oracle, differentially gated on every baseline run | **confirmed** |
| **AI19** | MIR-interp demotes to codegen diagnostic at P5; `mir_policy.hpp` size thresholds retire | **confirmed** |
| **AI20** | REPL/shell route through T0 now; persistent top-level environment (P4) supersedes incremental-compilation caching as the REPL end state | **confirmed** |
| **AI21** | Stage 2 extends the tier model to LambdaJS over `JsAstNode`, sharing frames/tiering/hooks; JS semantics stay in the JS helper layer; the size-based interp policy is replaced | **confirmed** |
| **AI22** | No bytecode IR; tree-walk over the typed AST is the only sub-MIR executable form; revisit only with T0 profiles | **confirmed** |

## 15. Spec impact

**Landed 2026-08-15 with the ruling (this rev):**

- **`doc/Lambda_Formal_Design.md` v1.23.0** — **D8.1.1v2*** revised in place: tiered execution, T0 default, per-definition promotion (default 3rd call), const-folder = CONST mode, MIR-interp demoted to codegen diagnostic, no-patching restated (D8.4.1/DI14), the D5.1.2 "no hotness detection" scope clarification folded into the ruling text, and a brief historical footnote on the v1 rejection (per user instruction). Appendix A carries the implementation footnote (shipped pipeline unchanged until P0–P5); Appendix B adopts the interpreter opens as **DO25** (AIO1/AIO2/AIO8/AIO11 named; AIO1–AIO12 referenced); Appendix C indexes this doc under D8.1–D8.2.
- **`vibe/Lambda_Design_Unified_AST.md` rev 10** — **U26 struck and superseded** in the §9 ledger and banner-noted in §12; the historical analysis retained unchanged in place (retired IDs never reused); §12.3's const-folder and KIV oracle recorded as absorbed (AI16, AI18).
- **`vibe/impl/Lambda_Impl_Tune_Ast (retired).md`** — the "do not implement a new AST interpreter" restriction struck (lifted by D8.1.1v2/AI1); interpreter work proceeds under this doc's phases, not that retired plan's contingencies.

**Lands with implementation (per phase / P5):**

- **`doc/dev/lambda/`** — LR_00 §goals ("JIT-only execution, no interpreter" → tiered model), LR_01 (REPL flow, Known Issue #8 resolution path), LR_07 (interp-selection §, satellite lowering), LR_08 (interpreter frames as a side-stack client), and a new LR doc for the interpreter itself. The LR set describes the implemented system and updates as phases land.
- **`doc/dev/js/`** — JS_00 §3 + JS_01 §6 when stage 2 lands.
- **`doc/Lambda_Formal_Design.md`** — D6.2.1 implementation footnote when `Function::def` materializes definition-site identity (AI7); the Appendix A D8.1.1v2 row updates per phase.
- **`doc/Lambda_Formal_Semantics.md`** — no ruling changes required: S1.6/SI3 already state tier invisibility, S7.11.4 already carves out fault timing. (This is the design's quiet strength: the semantics spec never assumed a compiler.)
