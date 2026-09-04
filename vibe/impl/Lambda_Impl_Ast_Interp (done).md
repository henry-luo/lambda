# AST Interpreter — Implementation Plan, Phases P0–P5

**Date:** 2026-09-04 (rev 68 — **partition resynced after the AUTO satellite narrowing**)
**Status:** **P1's committed corpus is fully interpreted, and AUTO is the unset default.** The AST walker covers the P1 expression, comprehension, type/pattern, document/path/query, procedural/error, module, numeric-carrier, COW-mutation, joined-group, pipe-file, procedural-window, view/edit-template, hosted-JavaScript import, typed-object-update, async/task-procedure, object-method, and typed-parameter slices through the same runtime helpers and representation boundaries as MIR. The committed differential corpus is now **751 discovered scripts = 737 interpreted matches, 14 counted fallbacks, and 0 inconclusive rows; 0 unclassified and 0 divergent rows**. The 14 excluded rows are the task-backed boundary, not a walker gap: their `pn main` starts or waits on a task *and* keeps mutable locals, and the boxed satellite membrane fails closed on local `var`/assignment statements and on indirect Lambda calls, so the module takes a counted whole-module fallback (R4). The 687/687 figure predates that narrowing; the lists were regenerated with `make interp-sweep`, which refreshes the subset and exclusion halves of the partition together. The former renderer rows were repaired by restoring the production Tree-sitter parser artifact expected by `ts-enum.h`; the three invalid object/map fixtures were converted to legal `run`-mode `pn main` coverage, with JIT goldens regenerated and matched by T0. Task-backed procedure bodies still delegate only their resumable continuation to a P2 MIR satellite while the module and synchronous code remain under T0. The release `test_lambda_gtest` gate is green under the unset default AUTO, forced `interp`, and forced `jit`: **758/758 tests pass**. P2 promotion now fails closed for aggregate/structured signatures, mutable/index writes, object-field identifiers, indirect/`var` calls, and batch-retained module state; scalar module reads and dynamic multi-argument calls use the shared boxed ABI.
**Design authority:** `doc/Lambda_Formal_Design.md` **D8.1.1v5** (AUTO selector, threshold-5 promotion, direct self-tail handoff), D5.1.1/D5.1.2 (side-stack frames), D5.3.2/D5.3.3 (MAY_GC + native rooting contract), D6.2.1/D6.2.3/D6.2.4 (function values, snapshot captures, traced env), D7.2.1/D7.2.2 (module slabs, init transaction), DI14; `doc/Lambda_Formal_Semantics.md` S3.1, S7.7.1/S7.7.2/S7.7.3, S7.11.4, S9.1, S11.2.1/S11.3.1, **S14.1**, SI3v2.
**Working design:** `vibe/Lambda_Design_Ast_Interpreter.md` (AI1–AI22 confirmed; AIO1–AIO12 = DO25). This revision adds the nullable/pointer native-array declaration boundary, numeric-mask vector-store bridge, fixed-coordinate N-D ArrayNum read/write bridge, direct-binding VMap-set COW bridge, correct Element content traversal plus a bounded direct-literal COW bridge, recursive declared structural-map admission plus nested checked COW update, direct object type/literal construction plus traced local-method receiver closures, the `ELEM_UINT64` literal/carrier and checked direct-store path, the P1 source-level no-task proof for immutable procedure aliases, the dynamic-numeric parameter boundary that preserves optional `null` after the call adapter, direct-binding- and proven-range-index COW slices, declared-numeric binding/error-value assignment, numeric type-call coercion, compact-NumSized array construction, grouped/ordered plus joined-comprehension slices, pure-system named-argument source-order dispatch, and the threshold-5 direct self-tail T1 handoff. It continues §11's P2, P3's PREDICATE plus immediate-scalar CONST vertical slices, and P4's persistent-REPL vertical slice; validator integration remains a subsequent gate, while P5 (default flip) is landed.
**Scope rule:** `LAMBDA_TIER` unset selects `auto`; `jit` explicitly selects the existing eager whole-module pipeline (MT7/D8.6.1), and `interp` pins T0. AUTO must pin an unsupported function to T0, never recompile the full module as a substitute. The release AUTO corpus gate is green.

---

## 0. Summary

P0 builds the skeleton: the frame-plan pass, side-stack interpreter frames, a walker for the L1 core subset, `LAMBDA_TIER=interp` wiring, and the first measurement report. P1 completes the admitted construct coverage for the full Lambda baseline, adds the error/fault channels, module support, and self-tail-call iteration, and records every remaining policy fallback or inconclusive row over the full corpus. P2 adds opt-in tier-up without changing `jit`: the definition-site cell counts dynamic entries, emits one function plus its boxed wrapper into a satellite MIR module, then upgrades pre-existing `Function` values in place at the shared dispatcher (D8.1.1v2 §5.1–§5.3). P3 now has restricted `that` predicate evaluation and immediate-scalar CONST folding. P4 retains one interpreter Script for a REPL session, parses/builds/indexes only appended fragments, executes only those nodes, and makes a failed completed input atomic; validator de-JIT remains gated by its separate `Type*`/validator ownership work, followed by P5.

Execution model recap (from the design doc, not re-argued here): T0 is boxed-only, calls the same C-ABI runtime helpers as generated code (AI3), lives on the existing side stacks with per-function statically-sized windows (AI4/AI5), and uses `EvalSignal` for statement control flow (AI14). No promotion machinery exists until P2 — in P0/P1 the tier is a per-run, whole-script choice.

## 1. New and modified files

| File | Change |
|---|---|
| `lambda/runtime/interp.hpp` **(new)** | `EvalMode`, `EvalSignal`, `FnFramePlan`, `InterpFrame`, `InterpState`, entry points (`interp_run_script`, `interp_call`), fallback/stat counters |
| `lambda/runtime/interp.cpp` **(new)** | the walker (`eval_expr`, `exec_stam`), frame open/close, closure creation, runner glue |
| `lambda/runtime/interp_plan.cpp` **(new)** | the frame-plan pass: slot assignment, scratch-depth computation, module-slab layout, tail-call marking (P1) |
| `lambda/runtime/ast-core.hpp` | `NameEntry` + `slot`/`binding_storage`; `FnAnalysis` + `FnFramePlan` (fills part of the designed-but-empty per-function facts area) |
| `lambda/runtime/ast.hpp` | `Script` + top-level `FnFramePlan`, interp module-slab pointer, tier/fallback bookkeeping |
| `lambda/lambda.h` | `Function` + trailing `const void* def`; `FunctionEntryAbi` + `LAMBDA_INTERPRETED`; `extern "C"` decl of `interp_call` |
| `lambda/runtime/lambda-eval.cpp` | `lambda_dynamic_call`: interpreted-ABI arm; `to_closure_interp` beside `to_closure_named` (shared internal body — rule 13) |
| `lambda/runtime/runner.cpp` | tier switch in the load path: a compile-mode variant of `load_script` that stops after AST build + passes; shared `script_adopt_transpiler()` factored out of the `transpile-mir.cpp:25512` memcpy handoff |
| `lambda/main.cpp` | `LAMBDA_TIER=auto\|interp\|jit` parse (P0: `interp`/`jit` only; unset ⇒ `jit`); REPL routes through the same switch; timing print + RSS extension |
| `lambda/runtime/runner.cpp` (`PhaseProfile`) / `runtime/compiler_timing.hpp` | new columns: `plan`, `interp_exec`, `peak_rss`; TSV format bump |
| `build_lambda_config.json` | add the three new sources (never edit the Lua — rule 7) |
| `test/test_interp_gtest.cpp` **(new)** | frame-plan unit tests + walker unit tests |
| `test/lambda/interp_p0_subset.txt`, `test/lambda/interp_excluded.txt` **(new)** | committed subset/exclusion lists (§5) |
| `test/interp/gen_bench.py`, `test/interp/repl_bench.py` **(new)** | measurement drivers; all outputs under `./temp/` (rule 2) |
| `Makefile` targets | `make test-lambda-interp` (subset in P0, full matrix in P1), `make interp-bench` |

P2 additions in rev 4: `FnAnalysis::promotion` carries the def-site state/counters/boxed entry; `lambda_dynamic_invoke_by_count` is the only upgrade point; `transpile-mir.cpp` has a uniquely named satellite-module lowering path; and `test/lambda/interp_auto_tier.ls` differentially exercises self recursion, multiple satellite images, a module-slab scalar, a cross-satellite dynamic call, and backedge-marked next-entry promotion. The satellite map reads `NameEntry::slot` from the frame-plan slab instead of eager `_gvar_*` BSS numbering, which excludes function values and would address different storage.

Layout-compat constraints on struct edits: `Function::type_id` at offset 0 and `closure_field_count` at offset 2 are poked by generated code (`transpile-mir.cpp:20095–20135`) — new fields go at the **end** only; `NameEntry`/`FnAnalysis` are pool-calloc'd, so zero-init is the correct "no plan" state.

## 2. P0 — skeleton + first evidence

### P0.1 Core types and entry points

- [ ] `interp.hpp` with the structures below; every log line prefixed `interp:` / `frame-plan:` (rule 9); `lib/` types only (rule 3).

```cpp
enum class EvalMode : uint8_t { RUNTIME, CONST, PREDICATE };
enum class EvalSignal : uint8_t { NORMAL, RETURNED, BROKE, CONTINUED, ERROR_SKIP };
// RETURNED / ERROR_SKIP payloads live in the frame's reserved signal slot, never in a C++ local.

struct FnFramePlan {              // on FnAnalysis (functions) / Script (module top level)
    uint16_t param_count;
    uint16_t local_count;         // flattened block scopes; shadowing gets distinct slots
    uint16_t scratch_depth;       // max Items live across a child eval / MAY_GC helper call
    uint16_t total_slots;         // params + locals + scratch + 1 (signal slot)
};

struct InterpFrame {              // C stack: control only, no Items
    struct InterpState* st;
    const AstFuncNode*  fn;       // NULL at module top level
    Script*             module;   // owner of const_list / type_list / slab
    uint64_t*           slots;    // side-root window base (Item lanes)
    uint32_t            slot_count;
    uint32_t            scratch_top;   // debug-checked ≤ plan->total_slots
    uint64_t*           number_mark;   // saved number-stack watermark
    InterpFrame*        caller;
    const AstNode*      cur;      // for backtraces / future stepping
};

struct InterpState {
    EvalContext* ctx;  Runtime* runtime;
    InterpFrame* top;
    uint32_t     depth;           // recursion budget (default 10_000, env-tunable)
    uint64_t*    signal_slot;     // current frame's reserved slot for RETURNED/ERROR_SKIP payloads
    uint64_t     fallback_count;  // scripts/nodes bounced to JIT (must be 0 on gated corpora)
};

extern "C" Item interp_call(Context* ctx, Function* fn, int argc, Item* argv);
Item interp_run_script(Runner* runner, bool run_main);
```

- [ ] Frame open/close as an RAII guard (`InterpFrameGuard`) so no `EvalSignal` path can skip the close: reserve `plan->total_slots` on the side-root stack (the `RootSpan` reservation path — zero-before-publish, limit check, `lambda_root_frame_overflow_error` fail-closed), save the number watermark; close restores both, strictly LIFO. Faults bypass the guard by design — a recovery landing restores both watermarks via `LambdaRecoveryCheckpoint`, abandoning interpreter frames above the landing wholesale (correct because frames own no other resources).
- [ ] Depth budget: decrement per `interp_call`; exhaustion raises a clean S7.4.3-channel fault *before* the C-stack or side-stack guards fire. Fault-timing divergence from T1 is sanctioned (S7.11.4).

### P0.2 The frame-plan pass (`interp_plan.cpp`)

- [ ] Runs in the existing compiler-pass schedule in `runner.cpp` (beside `ast_index_build_profile`), **gated on tier == interp** in P0/P1 so the default path is untouched; unconditional execution (for T1 reuse, AI5) is deferred to P2.
- [ ] Slot assignment: walk each `AstFuncNode` (via its `vars` `NameScope` chain plus the block scopes hanging off `AstListNode`/`AstForNode`/`AstWhileNode::vars`) and the script's `global_vars`; write `NameEntry::slot` + `binding_storage` (`REGISTER` = frame slot; `MODULE` = slab index; captures keep their `FnCapture` env index — the pass fills the `*_slot` fields build_ast leaves at −1). Note: this makes the pass the first consumer of the `BindingStorage` classification (the D5.2.2v2 footnote records it currently has none).
- [ ] Scratch-depth computation, Sethi–Ullman style, per expression tree. Per-kind cost table (P0 kinds; P1 extends it):

| Node shape | scratch need |
|---|---|
| leaf (literal, ident, sys-func ref) | 0 |
| unary(e) | need(e) |
| binary(a,b), member/index | max(need(a), 1 + need(b)) |
| call(f, a₁..aₙ) | max(need(f), maxᵢ (i + need(aᵢ))) — callee + earlier args held while later args evaluate |
| if(c,t,e) | max over branches (branches don't stack) |
| list/array/map of n items | 1 (rooted accumulator) + maxᵢ need(itemᵢ) |
| let initializer | need(init) (destination is a named slot, not scratch) |

- [ ] Module top level gets its own `FnFramePlan` on `Script`; module-level bindings get slab indices (P0: single-module scripts only, so the "slab" is one persistent-rooted Item array owned by the interp runner — the full `module_states` unification is P1.5).
- [ ] Debug assert in the walker: `scratch_top ≤ plan->total_slots` at every push; a violation is a plan bug, never a growth path.
- [ ] Unit tests (`test/test_interp_gtest.cpp`): parse small sources, run the pass, assert slot/depth values; property test with generated deeply-nested expressions (depth 100+) confirming the assert never fires while evaluating.

### P0.3 GC/boundary discipline audit

- [ ] Enumerate every helper the P0 walker calls; record each call site's rooting obligation (which operands must be in slots across it) as a comment block in `interp.cpp` — the placement mirrors the JIT's publish-before-call points (D5.3.3: *"native code writes through"*).
- [ ] Wide-scalar rules: helper results that carry number-stack payloads are resolved into the current frame's extent using the **shipping** conventions (`fn_call*_into`, `LAMBDA_SCALAR_HOME`, `owned_item_slot_store`, `lambda_item_resolve_pending`) — P0/P1 follow the current v1/RV-transition ABI as native helpers do today; the Return_Value P4 migration is orthogonal (RVO3 already established pending Items cannot cross into host code).
- [ ] `interp_call` return: resolve the callee's result into the **caller's** frame (slot or adopted home) before the callee's watermarks restore — same invariant as `lambda_dynamic_call`'s re-read pattern (`lambda-eval.cpp:1146–1191` is the model).
- [ ] Data-buffer motion rule stated once, enforced everywhere: never cache `items[]`/`data` pointers across an allocating call — container *object* pointers are stable, buffers move under `gc_compact_data`.

### P0.4 The L1 walker

Coverage (the design's P0 subset, §4.9 families 1–3 plus construction): `AST_SCRIPT`, `CONTENT` (statement/body lists), `PRIMARY` (const-list resolution through `node->type` → `const_index` → owning `Script::const_list`), `IDENT` (slot/env/slab load via `entry->slot`), `UNARY`, `BINARY` (arith/compare/set ops through the boxed helpers; `and`/`or` short-circuit; truthiness by tag per S3.1), `IF_EXPR`, `LET_STAM`+`ASSIGN` declare lists (statically-checked lets only; deferred-check skip semantics land in P1.4), `CALL_EXPR` (uniform path: evaluate callee to a `Function` Item, root args in a span, enter `interp_call` / native invoke via `lambda_dynamic_call` — the static-callee fast path is a P2 option, AIO10), `FUNC`/`FUNC_EXPR` (closure creation: snapshot captures per D6.2.3 via `to_closure_interp` + `owned_item_slot_store`; `entry_abi = LAMBDA_INTERPRETED`, `ptr = NULL`, `def = node`), `MEMBER_EXPR`/`INDEX_EXPR` (read-only, `fn_member`/`fn_index` — null totality per S7.1.1v2 comes from the helpers), `ARRAY`/`LIST`/`MAP`/`KEY_EXPR` construction, `SYS_FUNC` (direct C call through `SysFuncInfo`), `RETURN_STAM` only as fn-implicit-last-expression (explicit `return` is `pn`, P1.4).

- [ ] `eval_expr` switch with static asserts pinning the known enum collision (`AST_NODE_START` = 541 vs `AST_NODE_EVENT_HANDLER` = 542); no generic `(AstNamedNode*)` casts anywhere in dispatch (the `AstLoopNode` layout divergence is a documented live-bug pattern).
- [ ] Operator→helper mapping: extract the selection currently embedded in binary-op lowering into a shared table/header where a `static` already exists (rule 13 — promote, never copy); where lowering logic is inline MIR emission, the walker keys the same helper by `Operator` + operand `TypeId` with a comment cross-referencing the lowering site.
- [ ] Error Items flow through expression interiors unchecked (S7.7.1); P0 adds only the function-boundary check (an error result returns as-is). Full check-placement parity is P1.4.
- [ ] DAG rule: evaluation descends only structural child edges; `NameEntry->node` links are reads. Fn bodies reached via declaration links are entered only through `interp_call`.

### P0.5 Runner, CLI, fallback

- [ ] `LAMBDA_TIER` parsed once at startup (`main.cpp`, beside `apply_common_mir_option`); unset/`jit` ⇒ today's path bit-for-bit; `interp` ⇒ the new load variant: parse → build_ast → passes (ast_index retained — no `ast_index_destroy`, its memory cost is a report line, AIO4) → `script_adopt_transpiler()` → `interp_run_script`. No `MIR_init`, no `jit_context`, `main_func` stays NULL.
- [ ] `interp_run_script`: arm `EXECUTION_BOUNDARY` recovery frame (mirroring `run_script_mir:25664`), open the top-level frame, execute, surface the result through the same output path as the JIT runner (factored, not duplicated).
- [ ] **Fallback**: a script containing any unsupported node kind (P0: anything outside the P0.4 list; P1: only the committed exclusion classes) is detected by a cheap pre-scan pass over the AST and rerouted to the whole-module JIT path with `log_notice("interp: fallback file=%s reason=node:%s")` and a counter — *no silent caps*: the count is printed in the run summary and must be zero on gated corpora.
- [ ] REPL: `run_repl` history execution goes through the same tier switch — in interp mode each line is parse + build + interpret of the accumulated history (still whole-history in P0; the persistent environment is P4).
- [ ] Imports in P0: scripts with `AST_NODE_IMPORT` fall back (module support is P1.5).

### P0.6 Instrumentation for the report

- [ ] `PhaseProfile` grows `plan` and `interp_exec` columns; `LambdaCompilerTiming` mirrors; the `main.cpp:2426` timing print adds `plan_ms`, `interp_exec_ms`, `peak_rss_mb` (`getrusage(RUSAGE_SELF).ru_maxrss`, normalized for the platform's units).
- [ ] `make interp-bench`: runs the §6 corpus in both tiers, writes `temp/interp_bench.tsv`, prints the summary table for pasting into §6 with `[measured YYYY-MM-DD]` tags.

### P0 gate (G0) — **[met 2026-08-15]**

| Check | Criterion | Result |
|---|---|---|
| G0.1 correctness | Every script in `test/lambda/interp_p0_subset.txt` green under `LAMBDA_TIER=interp` against the existing goldens (`make test-lambda-interp`); subset generated by running the full suite in interp mode, taking the zero-fallback + golden-match set, then reviewed and committed | **PASS** — `test_interp_gtest.exe` 93/93; 81 subset scripts golden-identical with fallback=0. The full 279-script sweep (`make interp-sweep`) reports **0 divergences**: 81 interpreted, 198 counted fallbacks |
| G0.2 non-regression | Default-mode `make test` green; `git diff` shows zero changes to lowering/emission paths; jit-mode run of the suite wall-clock within noise of pre-change | **PASS** — `make test-lambda-baseline` 3715/3721. The 6 failures (2 `test_js_gtest`, 3 `test_mir_gc_stress_gtest`, 1 `test_js_mir_emission_gtest`) were verified to reproduce with this change stashed, so they are pre-existing in the working tree. The `transpile-mir.cpp` diff is exclusively the promotion of four shared helpers to headers (`parse_int_literal`, `parse_bool_literal`, `sysfunc_c_ret_type_id`, `is_declaration_node`/`is_side_effect_stam`) plus one memcpy replaced by the shared `script_adopt_transpiler` — no lowering or emission logic changed |
| G0.3 GC sanity | Subset run clean under the gc_heap stress knobs (`force_collect_interval` / `force_random_one_in`) in a debug build | **PASS** — the whole subset under `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1` (ASan debug build), both tiers, 0 divergences. This gate found four real rooting defects before it went green; see §2.8 |
| G0.4 **measurement report** | §6 tables published for the P0 corpus (subset suite, synthetic scale, REPL), both tiers, with `[measured]` tags — reviewed before P1 proceeds | **PASS** — §6.2 published `[measured 2026-08-15]`, §6.3 reviewed |

### 2.7 What P0 actually shipped

New: `lambda/runtime/interp.hpp`, `interp.cpp` (walker, ~760 lines), `interp_plan.cpp` (frame plan + pre-scan), `test/test_interp_gtest.cpp`, `test/interp/{tier_sweep,gen_bench,repl_bench,run_bench}.py`, `test/lambda/interp_p0_subset.txt`, `test/lambda/interp_excluded.txt`, `make test-lambda-interp` / `interp-sweep` / `interp-bench`.

Modified: `NameEntry` + `slot`/`binding_storage`/`storage_assigned`; `FnAnalysis` + `FnFramePlan frame_plan` + `decl_entry`; `Script` + `interp_plan`/`interp_slab_count`/`interp_planned`/`interp_supported`/`interp_reject_kind`; `Function` + trailing `def`/`def_module` and `FN_ENTRY_ABI_LAMBDA_INTERPRETED`; `lambda_dynamic_call`'s interpreted arm; `transpile_script`'s tier switch; `run_script_mir`'s T0 entry; `LAMBDA_TIER` env + `--tier=` CLI; `PhaseProfile` TSV format 2.

Deviations from rev 1, and why:

- **`FnAnalysis::decl_entry` was added.** `push_name` deliberately writes no `AstNamedNode`-only field, because that alias is `vars` on a function node and the join pointer on a loop node. A named `fn`/`pn` therefore has no route to the binding it installs, so the plan pass records it on the per-function facts carrier instead.
- **The plan pass owns its own AST traversal.** `ast_visit_core_children` is the core cross-language contract feeding `AstIndex`, and it falls through to `default:` for most Lambda-only kinds (`CONTENT`, `LET_STAM`, `ELEMENT`, `FOR_EXPR`, …). A pre-scan built on it would have missed unsupported nodes and produced silent wrong answers. Extending it would change `AstIndex` on the default path, which P0 must not do; `LangProfile::visit_ext_children` is the seam for unifying the two later (design §9).
- **Interpreted calls route through `lambda_dynamic_call`, not around it.** Rev 1 sketched a direct `interp_call` fast path for statically-known callees. Going through the single dispatch point is what gives interpreted callees the shared arity check and the optional/rest adapter for free; a second argument protocol diverged on `type_enforcement_dynamic_call` until this changed. AIO10's static fast path stays deferred.
- **`map_fill_items` / `set_fields_items` were added.** `map_fill` is variadic, which a walker holding a runtime-sized rooted span cannot call. Rather than duplicate the shaped-field store, `set_fields`'s per-field body was extracted into `set_field_value`, shared by the varargs filler and the new array filler.
- **`runner_setup_context` binds the side stacks after `eval_context_thread_initialize`, not before.** The bind resolves its owner through the thread's context identity, so on a fresh thread the original ordering silently bound nothing — a latent defect the JIT path never hit because MIR initialization re-bound later. This is a root-cause fix, not an interpreter workaround.
- **`LAMBDA_RSS_REPORT=1`** gates the per-run peak-RSS line so the measurement driver can read it on either tier without adding stderr noise to the default path.

### 2.8 Defects the gates caught (recorded, all fixed)

1. **Scratch slots were taken before the child evaluated.** `eval_binary`/`eval_call`/member/index acquired their slot and then recursed, making scratch use proportional to nesting depth instead of the plan's `max(need(a), 1 + need(b))`. Caught by `InterpFramePlan.DeepNestingStaysInsideThePlannedWindow` at depth 8. Fixed by publishing after the child returns — nothing between the return and the store is a safepoint.
2. **Helper operands were passed from C++ locals.** `fn_neg`/`fn_add`/`fn_member`/`fn_index` allocate, so an operand reachable only from a C++ local dies under forced GC. Caught only by G0.3. Every operand is now published to a frame slot before the call (D5.3.3), and the plan's cost table carries the extra live slot at the call itself.
3. **The typed-array accumulator was unrooted across element evaluation.** `array_int_new` then `eval_expr` per element, with the fresh `ArrayNum*` in a C++ local. Caught by G0.3.
4. **Closure creation held the env and the `Function` unrooted across three safepoints.** `heap_calloc(env)` → `to_closure_named` → per-capture reads that can `push_d`. Caught by G0.3 on `closure.ls` / `closure_advanced.ls`; now a `RootFrame` with two `Rooted` handles, re-read after every store.

Defects 2–4 were invisible to the ordinary differential and appeared only under `LAMBDA_GC_FORCE_EVERY=1`, which is the argument for keeping G0.3/G1.2 as a standing gate rather than a one-off.

## 3. P1 — full coverage

Each slice lands independently behind `LAMBDA_TIER=interp`, extends the frame-plan cost table for its kinds, adds/extends `.ls` tests **with goldens** (rule 8), and moves its scripts from fallback to supported.

### 3.0.2 Type identity is shared, not re-derived

A type node's runtime identity is not its `TypeId`: `date`/`time` both carry `LMD_TYPE_DTIME`, `list`/`number`/`integer` have no runtime `TypeId` at all, and every sized numeric shares `LMD_TYPE_NUM_SIZED`. Lowering encoded that as a long singleton-selection block inside `transpile_base_type`. Reproducing it in the walker would have been the second-implementation-of-semantics trap AI3 exists to avoid, so the selection moved to `lambda_type_node_singleton()` in `ast.hpp` and *both* tiers now call it — `is`/`query` cannot drift apart. The refactor was verified behaviour-preserving on the type/`is` suites before the walker used it.

The other half is that `type T = …` binds nothing at run time, so a type name read through a slab slot yields null. Lowering distinguishes this in `mir_is_type_value_node`'s IDENT arm; the walker makes the same distinction and publishes the declaration's `Type*`.

### 3.0.1 Why a mixed-tier Lambda import cone is refused

Both tiers keep module globals in the per-context slab (`EvalContext::module_states`), but they *number* it independently: the JIT assigns slots in `prepass_create_global_vars` during lowering, the frame-plan pass assigns its own. A module compiled by one tier and read by the other would therefore resolve the right slab and the wrong slot — a silent wrong-value read, not a crash. The pre-scan refuses an import whose target is not itself `interp_supported`, so a cone is interpretable whole or not at all. If a parent later falls back to MIR after a child was tentatively planned for T0, the loader now demotes that loaded child cone in post-order before linking the parent; MIR therefore never imports a T0 module with no generated symbol context.

This restriction still applies to ordinary Lambda-to-Lambda module execution: both sides must use one planned slab. A task-backed procedure is the explicit mixed-tier exception added in rev63; its resumable body is a satellite that embeds the declaring module id/slot and shares the T0 slab. A hosted JavaScript module is already evaluated and rooted by the JS runtime, so its synthetic exports use the separate namespace membrane and boxed `js_call_export_N_into` bridge. `test/lambda/import.ls` still falls back when a Lambda dependency needs an unsupported AST kind; that is a planned-slab boundary, not a cross-language namespace failure.

### 3.0 P1 progress — **complete 2026-08-22 (rev 60)**

The five P1 slices and the explicit P1.6 policy boundary are implemented. The committed corpus partition is complete and auditable: **687 discovered scripts with checked-in goldens, 648 interpreted matches, 37 counted fallbacks, 2 inconclusive rows, 0 unclassified rows, and 0 differential divergences**. Inconclusive remains a first-class verdict under R4/D8.1.1v2: the two renderer-transform timeout rows are named in `interp_inconclusive.txt`; they are not silently promoted or counted as fallback. The focused edit-template differential and fallback-accounting probes pass; the full committed gate is being refreshed for the new subset count.

The fallback rows are deliberate policy, not hidden P1 omissions: async/task/`START`, Jube/JS import cones, renderer-only object/update forms, and native-system/PDF families without a safe boxed ABI remain in `interp_excluded.txt` with their first unsupported AST kind. View and edit templates now publish interpreter-owned registry entries, initialize state through the shared state store, and expose handler bodies through the same event-dispatch bridge. This is the P1.6 contract from **D8.1.1v2** and **D5.3.3**; P2 may widen it through satellites without changing the default tier.

| Slice | State | Closure evidence |
|---|---|---|
| P1.1 comprehensions | **complete** | `FOR_EXPR`/`FOR_STAM` clause traversal, ordered and unordered windows, `LAST_INDEX`, one-source grouping, chained inner/left equi-joins, joined grouping, and procedural windows share the MIR materialization/sort helpers; `for_join_group_test.ls` and `proc_for_window.ls` are exact under both tiers. |
| P1.2 types/patterns | **complete** | Type-value nodes, declarations, constrained predicates, `MATCH_EXPR`/`MATCH_ARM`, string/symbol patterns, nominal object shapes, imported method ownership, and direct receiver calls use the canonical `Type*`/pattern helpers; `handler_two_arm.ls`, `constrained_type_hoisted.ls`, and `type_syntax_edges.ls` are zero-fallback matches. |
| P1.3 documents/paths/queries | **complete** | Element/content construction, paths, parent/query traversal, positional/named decomposition, and pure named-call labels use the runtime helpers; dynamic/object-only forms stay in the explicit P1.6 policy list. |
| P1.4 procedural/errors | **complete** | `EvalSignal`, declaration-boundary skip, explicit return/raise/propagate, handlers and local-fault recovery, COW writes, typed/native/nullable lanes, generic `u64` carriers, `PIPE_FILE_STAM`, self-tail iteration, `run`-mode `pn main`, procedural windows, and task-backed satellite continuations are covered by the 682-row differential partition. |
| P1.5 modules | **complete** | Import-cone recording, post-order transactional initialization, per-module slabs, declaring-Script lookup, module-state cleanup, Lambda satellite imports, and hosted-JavaScript namespace/export dispatch are exact; only the three compile-invalid rows remain excluded. |

### 3.0.3 Current P1 increment — 2026-08-19 (rev 6)

- **Subscript and stream-window `last` are now T0-native.** `InterpState::last_index_item` points at the current rooted subscript owner while the index expression evaluates, and `AST_NODE_LAST_INDEX` computes `len(owner) - 1`. The guard restores nested ownership and the local-fault landing restores it too, so a recovered handler cannot retain a dead frame slot. This is the innermost-context rule of **S10.1.3**; `test/lambda/interp_last_index.ls` covers scalar arithmetic, empty, nested, and string cases. Unordered `for` expressions complete their stream before applying `fn_drop`, `fn_take`, or `fn_take_last`, matching MIR without dropping prior body effects; `phase2_last_index.ls` and `interp_for_window.ls` are admitted.
- **Statically resolved named calls are now T0-native.** `ast_direct_call_function()` and `ast_resolve_call_args()` are shared by MIR and T0 (rule 13), so reordered and defaulted direct Lambda calls use the same parameter layout. T0 sends the private missing-argument marker for omitted parameter slots and resolves the default in `interp_call`, matching the generated wrapper boundary. Dynamic/system calls and aggregate-pipe injection stay excluded because their ABI is positional; this preserves the tier boundary required by **D8.1.1v2**. `test/lambda/interp_named_args.ls` covers all-named, reordered, mixed positional/named, and omitted-default calls.
- **The current error/fault boundary matches the shipped P1 behavior.** Rejected system-call operands return from the activation before a local handler can turn them into content; discarded procedural side effects publish their error through the procedure channel; a `LOCAL_FAULT` recovery frame lets a procedural handler catch an actual native-stack fault. These retain error-as-value and declaration-boundary distinctions in **S7.7.1/S7.7.2**, while procedure control stays scoped by **S12.1.2**.
- **Evidence.** `make build -j8` passed (0 errors). The named-call, subscript-`last`, unordered-window, and `phase2_last_index` differential rows pass with `fallback=0`; `phase2_last_index.ls` has entered the committed subset. Before the stream-window slice, a full `make test-lambda-interp` ran 367 tests: 364 passed, and `radiant_poc_uaf`, `decorations_basic`, and `drawing_block_integration` each lacked a parsed interpreter summary in that long run. Each passed immediately when rerun alone through the same GTest command, so that prior full gate is recorded as **flaky/unverified**, not passed.

### 3.0.4 Variadic Lambda increment — 2026-08-19 (rev 7)

- **The existing shared dynamic adapter remains the one argument marshaller.** It already transforms semantic extra operands into a trailing physical `List*`. T0 now gives each variadic `FnFramePlan` a dedicated rooted rest slot, installs it through `set_vargs()`, and restores the enclosing activation after a nested call. The adapter's null no-rest transport becomes a rooted empty list at T0 entry, because source `varg()` observes `[]`, never its ABI sentinel. This is the boxed shared-boundary discipline of **D8.1.1v2**; no second rest-argument protocol was introduced.
- **Tail/error parity is explicit.** MIR self-tail lowering rebinds fixed formal slots but retains its activation's hidden `_vargs` list, so T0 preserves that same list across tail iterations. The direct tail path also runs the shared rejected-parameter check before rebinding, preventing a rejected `ItemError` from entering the body; that restores the boundary behavior required by **S7.7.1**. Local, module, and top-level fault landings restore `current_vargs`, because a native longjmp bypasses the callee's C++ cleanup.
- **Scope and evidence.** `func_param.ls`, `func_param2.ls`, `transpile_error_ret_types.ls`, `type_enforcement_dynamic_call.ls`, and `type_param_error_short_circuit.ls` moved from explicit exclusions into the zero-fallback differential subset. New `interp_variadic.ls` covers empty/many rest lists, nested variadic restoration, direct named/mixed fixed-plus-rest calls, and the JIT-defined tail-rest invariant. `make build -j8` passed (0 errors), and the focused subset, walker, frame-plan, and fallback GTests pass.

### 3.0.5 Compact scalar witness and numeric type-call coercion — 2026-08-19 (rev 20)

- **Direct numeric type calls use the shared coercion contract.** For a one-argument `AST_NODE_TYPE` callee whose resolved call type is `NUM_SIZED` or `UINT64`, T0 roots the source and invokes `coerce_num_sized` or `coerce_uint64`, exactly as MIR does. A type AST evaluates to a `Type` value rather than a callable `Function`; intercepting the call before dynamic dispatch prevents that representation detail from becoming an `ItemError` while retaining the runtime's conversion, overflow, and ownership behavior (D8.1.1v2, D5.3.3). Scalar `NUM_SIZED`/`UINT64` annotations can therefore use sized literals, existing typed values, or these explicit conversion calls. T0 also constructs an inferred homogeneous `NUM_SIZED` literal directly in the matching `ArrayNum` carrier (§3.0.12). Dynamic typed-array conversion and the remaining declaration-boundary forms stay gated; a declaration annotation is not treated as an implicit wrapping cast, so dynamic admission/skip behavior remains a dedicated **S7.7.2** slice.
- **Wide scalar returns retain type identity.** Before an interpreted call closes its number-stack extent, T0 copies `int64`/`uint64` payload bits and reboxes them in the caller extent. This prevents `scalar_storage_read()`'s intentional small-`u64` property-read narrowing from changing `type(identity(100u64))` to `int`; the call boundary now meets the side-stack ownership requirement in **D5.1.1/D5.3.3**.
- **Evidence.** `in_container.ls`, `len_err_propagation.ls`, and `sized_numeric_type_annot.ls` previously moved from explicit exclusions into the zero-fallback differential subset. The direct `sized_numeric_annotation_edges.ls` probe now also matches its JIT golden with `executed=1 fallback=0` under normal T0, forced-GC (`LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`), and `auto` with `LAMBDA_JIT_THRESHOLD=2`; it exercises scalar wrapping, parameter/return lanes, `u64`, and its terminal `u8[]` declaration. `make build -j8` passes with 0 errors. Both committed partition files await one clean final-binary full sweep.

### 3.0.6 Assignment-row audit — 2026-08-19 (rev 9)

- **Seven legacy `AST_NODE_ASSIGN` exclusions were stale.** `input_raise_error.ls`, `json_empty_key.ls`, `parse.ls`, `parse_error_convention.ls`, `raise_arm_native_return.ls`, `proc_hex_literals.ls`, and `proc_local_equality_fault.ls` all now pre-scan and execute entirely in T0; no broad declaration-contract gate was removed for this audit. Their error values remain ordinary data until their established handler/raise boundary, in accordance with **S7.7.1**, while `input`/`parse` keep their existing `C_RET_RETITEM` adapter behavior.
- **Evidence.** Each direct `LAMBDA_TIER=interp` run reported `executed=1 fallback=0`; all seven rows have moved to the committed differential subset and match JIT under `interp` and `auto`. The remaining assignment exclusions still represent separately gated work, including dynamic typed-array conversion and deferred declaration-boundary handling.

### 3.0.7 Decomposition increment — 2026-08-19 (rev 11)

- **Positional and named decomposition are T0-native.** `AstDecomposeNode` now retains the resolved `NameEntry` created by the existing name pass. T0 evaluates the source once into a scratch root, then uses the same `item_at` (positional) or `item_attr` (named) runtime helper as MIR for each target, publishing into the planned binding slot. Retaining the source root across each helper call enforces the MAY_GC ownership rule in **D5.1.1/D5.3.3** and avoids shadow-sensitive runtime name lookup.
- **Lexical-list invariant and evidence.** Module execution now preserves `AST_NODE_LIST` as a lexical list instead of routing it through content evaluation, which ignores its `declare` chain. This keeps `(let a, b = pair, body)` in the same planned slots that its body reads, as required by the tier-correct execution boundary in **D8.1.1v2**. `interp_decompose.ls` covers positional and named lexical blocks; `for_decompose.ls` covers positional/named targets with comprehensions; and `math_random.ls` repeatedly binds wide scalar pairs. All three run with `executed=1 fallback=0` in the committed differential subset. The grammar's `for` let-clause accepts one binding only, so no multi-target form exists there. Object literals, patterns, grouping, ordering, and joins remain separate P1 exclusions.

### 3.0.8 Import-cone fallback repair — 2026-08-19 (rev 12)

- **Post-order demotion preserves the one-tier invariant.** A parent that is rejected by T0 can have already loaded a supported dependency during AST building. Before the parent lowers to MIR, T0 now recursively lowers each such direct dependency and its imports, deepest first, and marks it non-interpretable. This prevents MIR from linking against a planned-only child with no JIT context, and preserves the no-mixed-cone rule of **D8.1.1v2** and module initialization boundary of **D7.2.2**.
- **Evidence.** `test/lambda/graph_layout.ls`, an existing explicit exclusion, now completes under `LAMBDA_TIER=interp` with `executed=0` and a counted fallback instead of aborting during MIR import resolution. `InterpFallback.ExcludedScriptsAreCountedNotInterpreted` covers that row in its sampled exclusion gate.

### 3.0.9 Nested COW mutation increment — 2026-08-19 (rev 13)

- **Nested writes retain the complete COW spine.** T0 collects the resolved root and every intermediate key, roots the RHS and keys before the MAY_GC `cow_path_set` call, then publishes the returned root to the planned binding. This gives `a.b.c = v` the same detach-and-relink behavior as MIR, keeping sharing unobservable under **S9.1.2** while respecting side-stack rooting in **D5.1.1/D5.3.3**.
- **Owner-mutating system calls use their COW entries.** Direct `push` and `splice` calls now evaluate non-owner operands before the owner is detached, invoke `pn_push_cow`/`pn_splice_cow`, and publish their replacement binding. Calling the native entry directly mutated both aliases, so this aligns statement and call mutation paths with **S9.1.2**.
- **Evidence.** Existing `test/lambda/proc/cow_alias.ls` and `proc_param_type_infer.ls` now run as zero-fallback `run`-mode differential rows. Together they cover direct and nested array/map aliases, `push`, `splice`, and repeated nested map updates through untyped procedural parameters; T0 output is byte-identical to JIT under both `interp` and `auto`.

### 3.0.10 Direct `var`-parameter COW increment — 2026-08-19 (rev 14)

- **A direct mutable argument retains its caller write-back target.** T0 admits a fixed-arity direct call only when every `var` operand resolves to a distinct, non-imported mutable identifier. A COW-owned container root is detached and published before entering the callee, then the callee's final parameter value is re-homed after its frame closes and written back to that caller binding. This matches the mutable-borrow boundary of **S9.1.2** and the frame/number-stack ownership rules in **D5.1.1/D5.3.3**; dynamic, optional, variadic, aliased, and expression operands remain fail-closed because the boxed dispatcher has no write-back ABI.
- **Satellites stay pinned to T0.** A satellite can be entered only through the boxed dynamic ABI, so a definition with `var` parameters cannot tier up until that ABI gains an explicit mutable-borrow channel. This preserves the tier boundary of **D8.1.1v2**.
- **N-D row writes remain excluded.** `cow_ordering.ls` exposed that the generic COW array setter operates on scalar leaves, while an outer `ArrayNum` index replaces a row view. The pre-scan now recognizes direct aliases of an N-D literal and falls that script back rather than narrowing a row to a scalar. `test/lambda/proc/var_param.ls` is the committed zero-fallback `run`-mode coverage for the supported one-dimensional `any[]` path; it matches JIT under both `interp` and `auto`.

### 3.0.11 Typed-map COW and checked-write increment — 2026-08-19 (rev 15)

- **Direct typed-map writes now use the same checked candidate boundary as MIR.** A root `INDEX_ASSIGN_STAM` or `MEMBER_ASSIGN_STAM` whose resolved binding has a map contract calls `lambda_map_set_checked`; a detached `var` root uses its in-place variant. The candidate is validated before publication, preserving the sharing and mutable-borrow rule of **S9.1.2**; its owner/key/value remain rooted across the MAY_GC helper call under **D5.1.1/D5.3.3**.
- **A rejected direct store is a completion even for an unannotated `pn`.** MIR emits `emit_return_if_item_error` at this checked-store boundary before generic procedural side-effect lowering. T0 now preserves that error rather than discarding it with the statement result, while an enclosing handler still gets first access to its error value. This maintains error-as-value and handler placement in **S7.7.1** and the shared tier boundary in **D8.1.1v2**.
- **Evidence.** `type_enforcement_map_cow.ls`, `type_enforcement_union_map_cow.ls`, `type_enforcement_var_inout.ls`, and nullable native/i64/sized map rows moved to the committed zero-fallback `run`-mode subset: all match JIT under `interp` and `auto`. The existing negative `type_enforcement_map_write.ls` now exits `1` in both tiers and emits the same E201 checked-member-write diagnostic (apart from T0's run summary). Nested typed-map paths remain explicit exclusions.

### 3.0.12 Compact NumSized literal construction — 2026-08-19 (rev 19)

- **T0 now selects the identical compact carrier for inferred homogeneous `NumSized` literals.** `eval_array` reads the already-resolved element subtype on `AstArrayNode::type`, selects `array_num_new(ELEM_*, count)`, and publishes each evaluated Item through `array_num_set_item`. That reuses the allocation, storage, and scalar-conversion helpers MIR uses rather than reconstructing compact-number semantics in the walker (D8.1.1v2, D5.3.3).
- **The boundary is intentionally narrower than typed-array support.** A declared/dynamic typed-array conversion still needs the full checked `ensure_sized_array` declaration boundary. Full-width `u64` also remains pre-scan rejected: a generic container read deliberately normalizes a small `u64` to `int`, whereas MIR uses a typed raw-index lane. Retagging the read after the fact would hide that representation-contract gap and violate the differential rule (SI3).
- **Evidence.** Existing `test/lambda/compact_typed_arrays.ls` now executes entirely under `LAMBDA_TIER=interp` with byte-identical golden output (`executed=1 fallback=0`), including the `u8[]` annotation whose literal already has the required compact witness. The same run passes with `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`. `sized_numeric_collections.ls` is verified as a counted `AST_NODE_ARRAY` fallback because it contains `u64[]`; it is not added to the supported partition. The scalar-conversion and annotated-`u8[]` edge coverage is also now exact in `sized_numeric_annotation_edges.ls` (§3.0.5). `make build -j8` passes with 0 errors. Both partition files remain untouched until one clean final-binary full sweep regenerates them together.

### 3.0.13 Declared numeric bindings and error-valued assignment — 2026-08-19 (rev 21)

- **All admitted compact numeric binding boundaries now canonicalize through the shared runtime helper.** T0 uses one rooted `interp_coerce_declared_numeric` path for declaration lists, lexical/array/`for` declaration forms, direct reassignment, interpreted parameter entry, and self-tail parameter rebinding. It unwraps the recorded source contract and calls `coerce_num_sized` or `coerce_uint64`; it never reimplements wrapping or overflow locally. This restores the carrier that MIR establishes before later typed arithmetic, and keeps a helper-allocated wide result reachable under **D5.1.1/D5.3.3** and the common-tier rule in **D8.1.1v2**.
- **Error-valued assignment distinguishes open from checked bindings.** A fresh RHS error is retained in an untyped mutable binding, so an implicit error-excluding `var` parameter rejects it before COW preparation. A declared contract that excludes error still returns before publication. This matches error-as-value/check placement in **S7.7.1** without letting a failed assignment silently turn into an unrelated `null` mutation.
- **Evidence.** `proc_map_type_change.ls` now matches JIT under T0, including its `u32`-annotated declaration/reassignment that formerly became a floating value; `proc_type_param_error_short_circuit.ls` likewise matches, retaining the rejected `source_fail()` value so its `var` call produces `50`/`60` rather than mutating the binding. Both are exact under forced-GC. A fresh stable-binary `python3 test/interp/tier_sweep.py --dir test/lambda/proc --jobs 2 --timeout 45` reports **142 scripts: 108 match, 34 fallback, 0 mismatch, 0 timeout**. This is diagnostic evidence only: the committed partition files remain unchanged pending their required full-corpus sweep. `make build -j8` passes with 0 errors.

### 3.0.14 Checked typed-array boundary — 2026-08-19 (rev 22)

- **Narrow `T[]` contracts reuse the runtime conversion and store boundaries.** T0's shared declared-binding path roots the source and calls `ensure_typed_array` for `int[]`, `float[]`, `bool[]`, and `int64[]`, or `ensure_sized_array` for compact-sized lanes. A direct supported indexed write then calls `lambda_array_set_checked` (or its in-place `var` form), before publishing the returned COW root. This shares both representation selection and type validation with MIR rather than treating a generic `Array` as an equivalent substitute (D8.1.1v2, D5.3.3, S9.1.2).
- **The remaining boundary is explicit.** `u64[]` remains rejected because a generic read intentionally narrows small `u64` values while MIR's typed index lane retains its raw payload. Nullable/pointer element storage and N-D row writes also remain rejected: their lane descriptors or row replacement semantics require separate checked-store work. No post-read retagging or generic-store workaround is used (SI3).
- **Evidence.** `proc_typed_bool_array.ls`, `proc_typed_bool_array_param.ls`, `proc_typed_bool_array_recursive.ls`, and `proc_fill.ls` now run with `executed=1 fallback=0` and JIT-identical output. `proc_fill_bool_lane.ls` additionally passes forced-GC while covering packed-bool conversion, writes, widening, and typed `int[]`/`float[]` controls. The fresh bounded procedural sweep reports **142 scripts: 115 match, 27 fallback, 0 mismatch, 0 timeout**. Both global partition files remain untouched until their required full-corpus sweep; `make build -j8` passes with 0 errors.

### 3.0.15 Direct parameter-index COW — 2026-08-19 (rev 23)

- **The initial parameter slice admitted an exact untyped parameter index to the existing COW setter.** `array_set_cow` performs the same runtime integer conversion as MIR. Other dynamic expressions remained fail-closed, so this did not convert the scanner into a broad unchecked-index claim. The later direct-binding extension is recorded in §3.0.17; all forms remain subject to the import, element, typed-contract, and N-D row guards (S9.1.2, D5.3.3).
- **Evidence.** `proc_splice.ls` runs with `executed=1 fallback=0`, matching JIT both normally and with forced GC. It covers `splice`/`push` replacement publication, numeric and object arrays, and `set_at(v, idx, item)` where both `v` and `idx` are untyped procedure parameters. The subsequent bounded sweep is recorded in §3.0.16; the committed partition files remain unchanged pending their full-corpus sweep.

### 3.0.16 Proven range-loop index COW — 2026-08-19 (rev 24)

- **An integer-bounded `to` loop binding is an integral subscript despite its conservative AST type.** `fn_to` also supports character ranges, so the scanner requires at least one explicit integer literal bound: that rules out the character form, while a non-integer opposite bound faults before the body. Every successful admitted iteration therefore yields an `int`; the scanner recognizes that structural source rather than treating every `any`-typed loop binding as interchangeable. It also accepts only `+`, `-`, and `*` compositions of those bindings and integer literals. Childless literal `PRIMARY` nodes carry their integer proof on the primary itself, so the check does not mistake their absent inner expression for an unknown value. This is a narrow bridge to MIR's shared machine-index conversion, retaining the COW replacement invariant of **S9.1.2** and the rooted helper-call discipline of **D5.3.3**; arbitrary dynamic keys stay pinned.
- **Evidence.** `proc_for_range.ls` executes under T0 with `executed=1 fallback=0` and JIT-identical output, including typed, generic, variable-bound, and nested `i * 3 + j` stores. It also passes with `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`. The same forced-GC gate passes for `proc_typed_bool_array_loop_recursive.ls`; the bounded procedural sweep reports **142 scripts: 118 match, 24 fallback, 0 mismatch, 0 timeout**, with `proc_splice.ls`, `proc_for_range.ls`, and `proc_typed_bool_array_loop_recursive.ls` newly interpreted. The focused GTest boundary pair passes; the post-range `make test-lambda-interp` run passes 399/400 tests, with its sole accounting failure due to the deliberately stale `arraynum_transform_preserve.ls` and `compact_typed_arrays.ls` exclusion rows, not a differential regression. `make build -j8` passes with 0 errors and 13 warnings. Both global partition files remain untouched until their required full-corpus sweep.

### 3.0.17 Direct untyped binding index COW — 2026-08-19 (rev 25)

- **The direct-identifier bridge now covers every unannotated binding, not only parameters.** MIR and T0 both hand a direct binding's runtime `Item` to the same integer conversion before the generic COW setter. The scanner therefore admits `arr[index] = value` only when `index` is an `AST_NODE_IDENT` with no declared contract. It still rejects computed key expressions, masks/slices, type-constrained roots, and N-D row writes; in particular, a local alias to an N-D array cannot be silently treated as a scalar leaf store. This preserves the COW replacement boundary in **S9.1.2** and the shared-helper discipline of **D5.3.3**.
- **Evidence.** `proc_fill_gc_nested.ls` now executes with `executed=1 fallback=0` and JIT-identical `gc nested: 123 churn=500000` output, including the nested `l0[i0]`, `c1[i1]`, and `c2[i2]` stores through untyped locals. It is exact under forced GC plus freed-memory poisoning. The fresh bounded procedural sweep reports **142 scripts: 119 match, 23 fallback, 0 mismatch, 0 timeout**; this is the sole newly admitted script. `make build -j8` passes with 0 errors and 13 warnings. Both global partition files remain untouched until their required full-corpus sweep.

### 3.0.18 Dynamic numeric admission and plan scratch — 2026-08-20 (rev 27)

- **Declared numeric bindings now use the existing runtime boundary, with conversion semantics preserved.** For `int`, `float`, decimal, and other non-compact numeric contracts, T0 calls `lambda_type_check`, which owns exact numeric admission, materialization, and rejection. `NumSized` and `u64` deliberately continue through `coerce_num_sized`/`coerce_uint64`: those are conversion operations, so `i8(128)` wraps to `-128`, whereas a checked contract would reject it. The frame planner reserves a scratch slot for every declared binding/parameter boundary; a typed identity function otherwise had no body scratch despite needing to root its argument across the MAY_GC admission call. This preserves **D5.3.3** and the concrete destination representation required by **D8.1.1v2**.
- **Optional parameters remain a valid nullable boundary.** The dynamic-call adapter resolves an omitted optional argument to its default or `null` before frame entry; the boxed MIR wrapper explicitly accepts that `null` for an optional parameter. T0 now does the same before numeric admission, so an `int` optional cannot turn a valid absent value into `error`. Supplied non-null values still traverse the shared declared-type boundary. This preserves the optional call contract in **S7.7.2** and the shared dispatch rule in **D8.1.1v2**.
- **Evidence.** `proc_type_numeric_boundary_admission.ls` now executes with `executed=1 fallback=0` and JIT-identical output through dynamic `int`, `float`, decimal, compact, and `u64` declarations, parameters, returns, typed-map fields, and typed-array elements; it is exact under forced GC plus freed-memory poisoning. `proc_sized_numeric_annotations.ls` remains exact, including signed/unsigned wraparound. `type_enforcement_dynamic_call.ls` is exact under the focused T0 GTest, including an omitted `b?: int` dynamic argument. At this slice's landing gate, stable `make test-lambda-interp` passed **399/400** tests: all 385 zero-fallback scripts and all focused walker/frame-plan cases were exact; its one failure was the known stale exclusion accounting for `arraynum_transform_preserve.ls` and `compact_typed_arrays.ls`, not a T0 differential. The then-current parallel procedural sweeps reported 119 match, 22 fallback, 0 mismatch, and one load-sensitive `proc_callsite_infer.ls` timeout; that script completed exactly in the serialized full gate. `make build -j8` passed with 0 errors and 13 warnings. Both global partition files remain untouched until their required full-corpus sweep.

### 3.0.19 Immutable synchronous procedure aliases — 2026-08-20 (rev 28)

- **T0 can distinguish a known synchronous procedure alias from the native async transform's conservative unknown.** The native concurrency analysis must classify every indirect `pn` call as potentially awaiting because generated code cannot generally identify the runtime callable. The interpreter now separately resolves only immutable, non-import lexical aliases through their declaration chain. It recursively scans each resolved procedure body; any `start`, async system call, mutable/dynamic alias, unresolved procedure target, generator, or depth overflow pins the whole script. Recursion is admitted only as an active-cycle proof, never as a bypass for a task edge. This preserves the task/continuation separation in **D8.1.1v2** and bounded interpreter activation semantics in **D5.1.1/D5.1.2** without weakening the shared JIT concurrency analysis.
- **Evidence.** `proc_closure_mutation.ls` now executes with `executed=1 fallback=0`, preserves its closure snapshot result `T1:42 T2:42 T3:5"done"`, and is exact under forced GC plus freed-memory poisoning. The new GTests prove both outcomes: `ImmutableProcAliasStaysSynchronous` matches JIT, while `ImmutableProcAliasWithTaskStaysPinned` rejects a `let` alias whose target calls `sleep`. `proc_stack_frame.ls`, which starts and waits for a task, remains `executed=0 fallback=1`. The bounded procedural sweep reports **142 scripts: 120 match, 21 fallback, 0 mismatch, 1 parallel-load timeout** (`proc_callsite_infer.ls`); that row completed exactly in the serialized full gate. Stable `make test-lambda-interp` passes **401/402** tests; the only failure is the known stale exclusion accounting above. `make build-test -j8` passes with 0 errors (2 warnings). Both global partition files remain untouched until their required full-corpus sweep.

### 3.0.20 Full-width `u64[]` ArrayNum lane — 2026-08-20 (rev 29)

- **T0 now uses MIR's full-width array carrier instead of falling back before the first `u64[]` literal.** `eval_array` allocates `ELEM_UINT64` for a homogeneous `u64[]`; declared-array admission now uses `ensure_typed_array` for that same contract, and a direct checked indexed assignment reaches the shared `lambda_array_set_checked` boundary. The runtime therefore owns full-width conversion, COW detachment, and the destination lane, while `fn_index` returns a boxed `u64` suitable for a later generic heterogeneous array. This follows the shared-helper and root-lifetime requirements of **D5.3.3**, the tier-equivalence requirement of **D8.1.1v2**, and the value-preserving COW rule in **S9.1.2**.
- **Evidence.** `proc_uint64_array_set.ls` now executes with `executed=1 fallback=0`, matches JIT for `MAXu64` writes, a typed `u64[]` parameter read, and subsequent generic widening that still reports `u64`; it is exact under `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`. The new `Uint64TypedArrayKeepsItsFullWidthLane` GTest passes with the positive synchronous-alias and negative task-alias proofs. The bounded procedural sweep reports **142 scripts: 121 match, 20 fallback, 0 mismatch, 1 parallel-load timeout** (`proc_callsite_infer.ls`); the serialized full gate completes that row exactly. `make build -j8` passes with 0 errors (13 warnings), `make build-test -j8` with 0 errors (42 test warnings), and `make test-lambda-interp` completes **402/403** tests. Its sole failure is the intentionally stale exclusion accounting for `arraynum_transform_preserve.ls`, `compact_typed_arrays.ls`, and `eq_total.ls`; the global partition files remain untouched pending the required clean full-corpus sweep.

### 3.0.21 Direct Element-literal mutation — 2026-08-20 (rev 30)

- **T0 now admits only the Element shape it can prove owns both layouts.** `AstElementNode::content` is an `AstListNode` wrapper, not its first child; evaluating the wrapper collapsed a multi-child literal to its final child before a mutation occurred. The walker now iterates the wrapper's `item` chain, matching MIR construction. For a direct untyped Element-literal root, an attribute assignment dispatches to `map_set_cow` and a child-index assignment to `array_set_cow`; using the child path for both would reinterpret an attribute key as a positional index. Derived Elements, markup/input owners, and nested Element paths remain pinned until their source/ownership contracts have their own T0 bridge. This preserves Element's dual layout and COW replacement semantics under **S9.1.2**, as well as the shared-helper/rooting contract in **D5.3.3** and tier equivalence in **D8.1.1v2**.
- **Evidence.** `proc_element_mutation.ls` now executes with `executed=1 fallback=0` and JIT-identical results across string/type-changing attributes, child replacement, retained children/attributes, and a looped attribute update; it remains exact with forced collection and freed-memory poisoning. The new `ElementLiteralMutationUsesTheMatchingCowLayout` GTest passes with the full-width array and procedure-alias focused regressions. The bounded procedural sweep reports **142 scripts: 122 match, 19 fallback, 0 mismatch, 1 parallel-load timeout** (`proc_callsite_infer.ls`); that script completes exactly in the serialized full gate. `make build -j8` passes with 0 errors (13 warnings), `make build-test -j8` with 0 errors (42 test warnings), and `make test-lambda-interp` completes **403/404** tests. Its sole failure is the intentionally stale exclusion accounting for `arraynum_transform_preserve.ls`, `compact_typed_arrays.ls`, and `eq_total.ls`; the global partition files remain untouched pending the required clean full-corpus sweep.

### 3.0.22 Nullable native typed-array boundaries — 2026-08-20 (rev 31)

- **T0 now admits the existing nullable-native array carriers instead of treating their contracts as bare type IDs.** An element such as `int?` is a `TypeUnary` contract, and pointer-backed elements such as `string` have no `ensure_typed_array` carrier at all. Sending either through that bare-TypeId helper rejected a valid typed source before T0 could reach the checked store. The declared-array boundary now asks `lambda_type_lane_storage_desc` for the full contract and delegates nullable or pointer lanes to `lambda_type_check`; that existing runtime boundary owns the COW detachment/rebuild and exact lane selection. Direct scalar-index writes then remain at `lambda_array_set_checked`, so no T0-specific nullable representation or post-read retagging exists. This follows **D5.3.3**, **D8.1.1v2**, and **S9.1.2**.
- **Evidence.** `proc_nullable_native_array.ls`, `proc_nullable_native_float_array.ls`, `proc_nullable_native_int64_array.ls`, `proc_nullable_native_sized_array.ls`, `proc_nullable_native_pointer.ls`, and `proc_nullable_native_extended_pointer.ls` all report `executed=1 fallback=0` and match JIT with `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`. The new `NullableNativeTypedArraysKeepTheirDestinationLane` GTest differentially covers all six lanes and passes. The bounded procedural sweep reports **142 scripts: 129 match, 13 fallback, 0 mismatch, 0 timeout**; every six newly admitted nullable-native row is exact. `make build -j8` passes with 0 errors (13 warnings), `make build-test -j8` with 0 errors (42 test warnings), and the serialized `make test-lambda-interp` run passes **404/405** tests: all 385 committed differential rows are exact, while the only failure remains the deliberately stale exclusion-accounting rows `arraynum_transform_preserve.ls`, `compact_typed_arrays.ls`, and `eq_total.ls`. The global partition files remain intentionally untouched until their required clean full-corpus sweep.

### 3.0.23 Numeric boolean-mask assignment — 2026-08-20 (rev 32)

- **T0 now sends a direct numeric mask store to MIR's existing vector helper instead of misclassifying its ArrayNum key as a scalar index.** A source numeric literal initially retains the broad `ARRAY` AST type, while its comparison key is statically `ARRAY_NUM`; at runtime the literal has become the compact numeric owner. The shared admission test accepts only that owner/key pair, and execution calls `fn_index_assign`, which owns the boolean-lane, shape, scalar-versus-block RHS, and N-D traversal checks. The owner is intentionally not re-published through scalar COW: alias and `var` boundaries have already detached it, and this is the same in-place helper path MIR emits. This preserves COW ownership under **S9.1.2**, the shared MAY_GC/rooting helper boundary in **D5.3.3**, and tier equivalence under **D8.1.1v2**.
- **Evidence.** `proc_mask_assign.ls` now reports `executed=1 fallback=0` and matches JIT across integer and float scalar masks, ordered block RHS consumption, and a 2-D mask. It is exact with `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`. The new `NumericMaskAssignmentUsesTheVectorStore` GTest passes alongside the nullable-native, `u64[]`, and Element mutation focused gates. The bounded procedural sweep reports **142 scripts: 130 match, 12 fallback, 0 mismatch, 0 timeout**; `proc_mask_assign.ls` is the sole newly interpreted row. `make build-test -j8` passes with 0 errors and 692 test warnings. The serialized `make test-lambda-interp` gate completes **405/406** tests: all 385 committed differential rows are exact, while its sole failure remains the deliberately stale exclusion-accounting rows `arraynum_transform_preserve.ls`, `compact_typed_arrays.ls`, and `eq_total.ls`. The global partition files remain intentionally untouched pending the required clean full-corpus sweep.

### 3.0.24 Direct N-D numeric indices — 2026-08-20 (rev 33)

- **T0 now admits fixed-coordinate access to a direct N-D numeric literal through ArrayNum's existing axis helpers.** Source `m[i, j]` carries one chained index list; treating only its first coordinate as an ordinary scalar subscript silently changed the operation. The planner proves a direct N-D literal binding (or plain alias) and fixed integral coordinates, then the walker evaluates all coordinates and delegates to `array_num_at_nd`/`array_num_set_nd`. The helpers own row-major traversal, typed lanes, and c15's axis-bound rule: negative or out-of-range coordinates are absent/no-op. Row replacement and generic/effectful coordinates remain pinned because they need a distinct owner/replacement contract. This preserves the collection-update rule in **S9.1.2**, shared runtime helper/rooting boundary in **D5.3.3**, and tier equivalence in **D8.1.1v2**.
- **Evidence.** `proc_ndim_index_write.ls` now reports `executed=1 fallback=0` and exactly matches JIT across 2-D/3-D integer writes and reads, float writes, negative indices, out-of-range coordinates, and one-axis row reads. It is exact with `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`. The `DirectNumericNdimIndicesUseArrayNumHelpers` GTest passes beside the four preceding native-array/mutation boundaries. The bounded procedural sweep reports **142 scripts: 131 match, 11 fallback, 0 mismatch, 0 timeout**; `proc_ndim_index_write.ls` is the sole newly interpreted row. `make build-test -j8` passes with 0 errors and 42 test warnings. The serialized `make test-lambda-interp` gate completes **406/407** tests: all 385 committed differential rows are exact, while its sole failure remains the deliberately stale exclusion-accounting rows `arraynum_transform_preserve.ls`, `compact_typed_arrays.ls`, and `eq_total.ls`. The global partition files remain intentionally untouched pending the required clean full-corpus sweep.

### 3.0.25 Direct VMap mutation — 2026-08-20 (rev 34)

- **T0 now uses the VMap-specific COW replacement bridge for a direct `m.set(key, value)` receiver.** `map()` construction was already implemented, but `set` has no generic boxed C entry because MIR lowers it directly. The planner admits exactly a local, non-import identifier receiver with two operands; execution evaluates key/value before reading the owner, then calls `vmap_set_cow` and republishes its replacement. This preserves hash-map backing ownership and the observable alias snapshot; dynamic/import receivers remain pinned because T0 has no replacement channel for them. This follows **S9.1.2**, **D5.3.3**, and **D8.1.1v2**.
- **Evidence.** `vmap.ls` now reports `executed=1 fallback=0` and matches JIT across string/int/bool/float keys, overwrites, looped writes, nested values, and a shared-alias mutation. It is exact with `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`. The new `DirectVmapSetUsesTheCowReplacement` GTest passes alongside the five preceding array/mutation boundaries. `radiant_dom_set.ls`, which uses the same direct VMap set path, also now executes with `executed=1 fallback=0` and JIT-identical output. The bounded procedural sweep reports **142 scripts: 133 match, 9 fallback, 0 mismatch, 0 timeout**; those are the two newly interpreted rows. `make build-test -j8` passes with 0 errors and 42 test warnings. The serialized `make test-lambda-interp` gate completes **407/408** tests: all 385 committed differential rows are exact, and only the deliberately stale exclusion-accounting test for `arraynum_transform_preserve.ls`, `compact_typed_arrays.ls`, and `eq_total.ls` fails. The global partition files remain intentionally untouched pending the required clean full-corpus sweep.

### 3.0.26 Open-item and input-markup mutation — 2026-08-20 (rev 35)

- **T0 now recognizes an open `any` / `any | error` root as a boxed COW boundary, not as a typed structural-write contract.** The planner preserves typed map/array validation, but a direct local open-Item binding has no narrower occurrence invariant to check. Its direct member/index write uses the existing runtime-layout dispatch (`map_set_cow` or `array_set_cow`), which in turn makes the actual Map/Element and input-pool migration decisions. This admits input-derived markup Elements through the same replacement-and-republish channel as a literal without fabricating ownership facts. This follows **S9.1.2**, **D5.3.3**, and **D8.1.1v2**.
- **Evidence.** `proc_markup_mutation.ls` now reports `executed=1 fallback=0` and is byte-identical to JIT across JSON and XML input, same-type and type-changing field writes, input-pool data migration, and repeated Element mutation. It remains exact with `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`. `OpenItemMarkupMutationUsesRuntimeCow` passes with the six preceding mutation/native-array GTests. `make build-test -j8` completes with 0 errors and 2 test warnings. The bounded procedural sweep reports **142 scripts: 134 match, 8 fallback, 0 mismatch, 0 timeout**; this is the one newly interpreted row. The serialized `make test-lambda-interp` gate completes **408/409** tests: all 385 committed differential rows are exact, and only the deliberately stale exclusion-accounting test for `arraynum_transform_preserve.ls`, `compact_typed_arrays.ls`, and `eq_total.ls` fails. The partition manifests remain intentionally untouched pending the required clean full-corpus sweep.

### 3.0.27 Direct nominal-object methods — 2026-08-20 (rev 36)

- **T0 now admits nominal object types/literals and a bounded direct `local.method()` call.** The AST builder retains the declared method AST on its `TypeMethod`; dispatch first checks a real field, then creates an interpreted `Function` with a one-field GC-traced closure environment containing the receiver. The method descriptor remains trailing metadata so generated-code offsets stay unchanged. The callee frame roots the extracted receiver separately, so no raw C++ pointer spans a MAY_GC helper. This is the closure/rooting contract in **D6.2.3** and **D5.3.3**, under the tier-equivalence boundary in **D8.1.1v2**.
- **A mutating object `pn` keeps the required observable-COW order.** Before binding its receiver closure, T0 prepares the local receiver for write and republishes the replacement, matching the generated path; field reads and writes then go through the ordinary object lookup/set helpers. The planner accepts only a statically resolved direct local receiver with positional non-`var` parameters; fields take precedence over methods, while captures, imports, dynamic receivers, named arguments, and unsupported mutable signatures remain whole-script fallbacks. This protects unobservable sharing and the exclusive mutable-method receiver rule in **S9.1.2/S9.1.3**.
- **Evidence.** `proc_object_counter.ls` and `object_mutation.ls` now run under T0 with `executed=1 fallback=0` and exact JIT output; `object_mutation.ls` is also exact with `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`. `InterpWalker.ObjectMethodsUseTracedCowReceiver` passes. The corrected assignment-result rule preserves a direct function-body assignment as that function's result while letting handler-local `x = ^` consume its error, and both `proc_local_system_fault.ls` and `proc_oob_write_error.ls` remain exact. `make build -j8` and `make build-test -j8` pass with 0 errors. The bounded procedural sweep reports **142 scripts: 136 match, 6 fallback, 0 mismatch, 0 timeout**. The partition manifests remain intentionally untouched pending a clean full-corpus sweep; the serialized full interpreter gate is pending.

### 3.0.28 Nested declared-map contracts — 2026-08-20 (rev 37)

- **T0 now applies the ordinary recursive contract checker at a declared structural-map binding, and routes a nested map write through the full-root checked COW helper.** A dynamic `{age: 3.0, child: {score: 4.0}}` bound as `Person` must convert its nested numeric fields before the name becomes visible. A later `person.child.score = dynamic(3.5)` must construct/validate a detached `Person` candidate before replacing the root, so a rejected write leaves both the current value and its alias unchanged. This is the contract and no-partial-publication rule in **S11.4.1**, the observable COW rule in **S9.1.2**, and the shared rooting/tier boundary in **D5.3.3/D8.1.1v2**.
- **Evidence.** `proc_type_numeric_structural_admission.ls` now reports `executed=1 fallback=0` and is JIT-identical under normal T0 and `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`; it covers recursive float-to-int admission, nested COW isolation, and a rejected fractional child write. `InterpWalker.NestedDeclaredMapWritesUseFullContract` passes. `make build -j8` and `make build-test -j8` pass with 0 errors. The bounded procedural sweep reports **142 scripts: 137 match, 5 fallback, 0 mismatch, 0 timeout**. The partition manifests remain intentionally untouched pending a clean full-corpus sweep; the serialized full interpreter gate is pending.

### 3.0.29 Interpreter-gate stderr isolation — 2026-08-20 (rev 38)

- **The false fallback reports were a harness capture race, not a T0 fallback.** `test_interp_gtest` sent every child process's stderr to one fixed `temp/interp_gtest_stderr.txt`. A capture collision could make `pclose()` read an empty or unrelated run summary, so `summary_field(..., "fallback=")` returned `-1` although the child had completed with `executed=1 fallback=0`. The harness now names the capture with its process id plus a monotonic local run sequence, reads it only after `pclose()`, then removes that exact temporary file. This restores the gate's ability to enforce the no-fallback requirement of **D8.1.1v2** instead of weakening or bypassing the assertion.
- **Evidence.** `make build-test -j8` passed with 0 errors. The 11 rows that failed only because the summary was missing (`err_value_family`, `error_union_param`, `expr_stam`, `find_replace_options`, `first_class_fn`, `transpile_bitwise`, `type_enforcement_dynamic_call`, `type_enforce_input_schema`, `type_enforcement_union_map_storage`, `type_occurrence`, and `typed_array_bool`) pass together through the rebuilt GTest. `InterpWalker.ObjectMethodsUseTracedCowReceiver` and `InterpWalker.NestedDeclaredMapWritesUseFullContract` also pass. The fresh serialized gate completes **410/411** tests in 24m36s: all 20 walker tests, all three frame-plan tests, and all 385 differential rows pass; its only failure is the deliberately stale exclusion audit for `arraynum_transform_preserve.ls`, `compact_typed_arrays.ls`, `eq_total.ls`, and `fuzzy_crash_regression.ls`. The partition manifests remain intentionally untouched pending the clean full-corpus sweep.

### 3.0.30 Grouped and ordered comprehensions — 2026-08-20 (rev 39)

- **One-source grouping now follows the same two-stage materialization boundary as MIR.** T0 collects filtered rows and their scalar-or-tuple keys, calls `fn_group_by_keys_items`, then binds the detached `into` entry before evaluating the aggregate body. `AST_NODE_GROUP_KEY` is now its own allocation tag, so generic traversal cannot cast its smaller layout as `AstGroupClause`. Ordered streams retain a parallel key stream and use the shared stable sort before applying an ordered window. This preserves the group tuple semantics of **S14.1**, total ordering in **S6**, and the shared helper/rooting rule in **D5.3.3**.
- **Element pipes iterate content, not attributes.** Group Elements use attributes for the aliases and children for their member rows. Treating every Element as a map made `g |> ~` empty, so T0 now takes the existing list traversal path for Elements exactly as the lowering does; that preserves the innermost stream-context rule in **S10.1.3**.
- **Evidence.** `for_group_test.ls` is byte-identical to its golden under normal T0 and `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`; `InterpWalker.GroupedForMaterializesRowsBeforeAggregateBody` and `InterpWalker.OrderedForWindowsUseSharedSortKeys` pass. The committed partition files are deliberately unchanged pending the clean full-corpus sweep.

### 3.0.31 Joined comprehensions — 2026-08-20 (rev 40)

- **T0 now materializes the tuple stream used by chained joins.** The first source is seeded through `fn_join_seed_tuples`; each later source takes either the shared hash-join helper for an `on` clause or the shared cross-product helper. That admits inner and optional-left joins, multi-key conjunctions, probe/new-source index or key bindings, and mixed join/cross source lists while retaining the JIT's deterministic prior-then-new row order (**S14.1**). Joined grouping stays pinned because MIR itself selects the tuple pipeline before group lowering.
- **The forced-GC failure was a native ownership bug, not a walker workaround.** Join tuple Elements had been pool-allocated and native hash entries held raw bucket Arrays, both invisible to the GC tracer. Tuples are now heap-visible, helper inputs/results are rooted across every allocating call, and the hash map stores only indices into rooted bucket-owner arrays. This is the exact-root requirement of **D5.3.3**; it prevents collection from erasing bindings or matches without changing JIT semantics.
- **Evidence.** `for_join_test.ls` and `for_join_s3b_test.ls` are byte-identical to their goldens under normal T0 and `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`. `InterpWalker.JoinedForPreservesTupleAndLeftJoinBindings` passes. `make build -j8` and `make build-test -j8` pass; the serialized `make test-lambda-interp` gate completes **414/415** in 27m50s, with every walker, frame-plan, and 385-row differential test green. Its only failure is the nine-entry stale-exclusion audit named in this document header. The global partition files remain untouched until the required clean full-corpus sweep.

### 3.0.32 Pure-system named arguments — 2026-08-20 (rev 41)

- **T0 now follows MIR's positional lowering for named operands on pure system calls.** The system registry carries a function/arity ABI, not a formal-name layout: MIR evaluates each `NAMED_ARG::as` in source order and passes those values positionally. The walker now does the same for direct and pipe-injected system calls, while direct Lambda calls retain their existing formal-name resolver. Dynamic calls remain rejected under **S12.3.2** because they have no declaration layout, and procedural system calls remain pinned because a piped receiver has no direct binding through which T0 can publish the required COW replacement (**S9.1.2**, **D5.3.3**). This preserves the tier-equivalence boundary in **D8.1.1v2** without inventing an interpreter-only named ABI.
- **Evidence.** `typed_array_axis_reduce.ls` and `agg_null_absence.ls` now run with `executed=1 fallback=0` and byte-identical output under normal T0 and forced GC with freed-memory poisoning. `InterpWalker.NamedPureSystemArgsKeepTheirPositionalAbi` covers both direct `min(m, axis: 0)` and pipe-injected `m |> min(axis: 0)` / `max(axis: 1)`. The global partition files remain intentionally unchanged until the required clean full-corpus sweep classifies every affected script.

### 3.0.33 Full-sweep timeout reaping — 2026-08-20 (rev 42)

- **A timeout now reaps the invocation without draining descendants' inherited capture pipes.** Some renderer helpers detach into a different process session but retain `stdout`/`stderr`. After the direct Lambda process group is killed, calling `communicate()` can therefore block forever waiting for an unrelated helper to close its copy, leaving a zombie direct child and an incomplete corpus run. The sweep now waits only for the killed direct child and closes its local pipe ends; that preserves the timeout verdict while letting the next classification proceed. This protects the all-corpus evidence requirement of **D8.1.1v2** rather than weakening the classifier's retry policy.
- **Evidence.** With a deliberately one-second budget, the 25-chart corpus completes every timeout/retry classification and writes `temp/sweep_timeout_reap.tsv`; all 25 are reported as explicit inconclusive timeouts, with no stranded worker. The normal-budget full sweep is rerun from this repair before either committed partition manifest changes.

### 3.0.34 Deferred declaration and parameter error boundaries — 2026-08-20 (rev 44)

- **A deferred declared binding now skips before publication.** When the shared numeric, native-array, or structural-map boundary creates an error from a non-error initializer, T0 leaves the slot untouched and raises the already-reserved `ERROR_SKIP` payload. Content/list/array/for declaration sequencing stops immediately; the owning function or module turns that payload back into its original error result. This mirrors MIR's checked-boundary return-before-store behavior and implements the declaration-containment rule of **S7.7.1/S7.7.2** under the common tier contract in **D8.1.1v2**. An incoming ordinary `ItemError` is deliberately not relabelled as a deferred check failure, so expression-level error flow remains unchanged.
- **A freshly rejected direct parameter returns through its caller before body entry.** Parameter preparation distinguishes an error *created by its admission conversion* from an incoming ordinary `ItemError`. Only the fresh rejection preserves the E201 call-boundary exit: `must_not_enter(dynamic_decimal()) or 9` exits before body entry. An incoming error is returned as the value of the call expression, so its enclosing `or` or handler can consume it; error-admitting parameters still receive it as ordinary body data. This exactly separates **S7.7.1** interior error flow from **S7.7.3**'s call-site boundary and retains the root payload in a reserved slot across **D5.3.3** safepoints.
- **Evidence.** The completed rev43 full sweep reported 479 matches, 45 explicit fallbacks, 150 explicit renderer/PDF timeouts, and seven confirmed mismatches; no partition was refreshed. Three mismatches (`split_error`, functional `type_param_error_short_circuit`, and procedural `proc_type_param_error_short_circuit`) exposed the too-broad incoming-error exit and now match JIT under normal and forced-GC execution. The other four rows are closed in §3.0.35: nominal object/import behavior is now exact, and the generic-`u64` representation gap returns to an explicit whole-module fallback. `InterpWalker.DeferredDeclarationFailureSkipsItsDeclaringBlock`, `InterpWalker.DeferredParameterFailureReturnsAtTheCallBoundary`, and the named-system regression pass together; `make build -j8`, `make build-test -j8`, and `git diff --check` pass. A new clean full-corpus sweep is required before either partition manifest changes.

### 3.0.35 Nominal object shape alignment, imported method ownership, and generic `u64` pin — 2026-08-20 (rev 45)

- **Object construction now aligns source labels with the resolved nominal shape.** Both execution tiers walk `TypeObject::shape` in declaration order, use the corresponding supplied `KEY_EXPR` when present, then its `ShapeEntry::default_value`, otherwise `null`. Type inheritance now carries an inherited field's default into the child shape. This eliminates the former positional vararg fill in the JIT and the differing T0 literal count; omitted fields now obey the same declared object contract in both tiers. Nominal `match` continues to use `is`, as required by **S11.2.1/S11.3.1**, rather than comparing a type value for equality.
- **An interpreted object method now owns its declaring module, not the transient builder or the caller module.** `TypeMethod` records the retained `Script` that owns its AST, constants, and type list; the loader sets that owner before adopting the stack-local `Transpiler`. A bound closure for a method defined in an imported module therefore evaluates against that module's slab and type list. This preserves the Script-package ownership boundary of **D7.2.1** and the common-tier rule of **D8.1.1v2**. The match frame planner also reserves the type-pattern temporary above `eval_match`'s four context homes, so no type arm can borrow the signal slot across `fn_is` and a possible collection (**D5.3.3**).
- **Mixed generic `u64` reads are deliberately fail-closed.** A homogeneous or declared `u64[]` still uses `ELEM_UINT64`, whose indexed read returns a boxed `u64`. A mixed literal that must use generic storage is rejected by the pre-scan when a source item may be `u64`: `scalar_storage_read` intentionally canonicalizes a small borrowed `u64` to `int`, while MIR's immutable static-constant path retains `u64`. Re-tagging the result would invent a T0-only representation repair, violating **SI3v2**; a real generic lane-preserving carrier is required before this shape may execute.
- **Evidence.** `object_default.ls`, `object_pattern.ls`, and `import_pub_types.ls` now match JIT in normal T0 and under `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`; `object_pattern.ls` has no scratch-overflow diagnostic. `proc_uint64_array_set.ls` remains JIT-exact in normal and forced-GC T0 with `executed=1 fallback=0`, while `sized_numeric_collections.ls` reports `executed=0 fallback=1`. The focused 7-test GTest gate passes, including `NominalMatchPatternsStayInsideThePlannedWindow` and `GenericUint64ReadStaysPinnedToMir`; `make build-test -j8` reports 0 errors (42 warnings). The required clean full-corpus sweep remains next before either partition manifest changes.

### 3.0.36 AST-wrapper tail calls, object-method identity, and explicit contract boundaries — 2026-08-21 (rev 46)

- **Direct-parser wrappers now preserve self-tail position.** The frame planner and safety analyzer descend through the parser's `BLOCK` and `EXPR_STMT` wrappers and inspect only the final statement, so direct-parser procedures reuse the existing TCO iteration instead of recursing on the native stack. `phase1_multipage.ls` now completes under T0 with the same two-page PDF summary as JIT; the full PDF directory sweep is exact.
- **Direct object methods carry their AST identity into T0 dispatch.** The direct AST builder now records `TypeMethod::ast_def`, its declaring `Script`, and parameter arity. A method lookup therefore produces a callable interpreted closure rather than a name-only descriptor; `proc_object_counter.ls` is exact under T0.
- **Abstract numeric map fields preserve their self-describing carrier.** Reification of `integer`/`number` fields uses the 9-byte `TypedItem` lane. The map rebuild store now preserves decimal-backed integer values in that lane, protecting the storage/ownership invariant in **D3.2.2** and the value carrier rule in **D2.6.1**; `abstract_numeric_field.ls` is exact with `executed=1 fallback=0`.
- **Explicit non-numeric declaration contracts are checked at the same boundary as MIR.** T0 now routes unions and other explicit contracts through `lambda_type_check`; a fresh rejected assignment returns before publishing the binding, matching the checked-boundary placement in **S7.7.2** and procedure completion in **S12.1.2**. The open nullable `array?` contract has a narrow admission path for null or any array carrier because the generic validator currently interprets its metadata as an occurrence bound. `union_local_carrier.ls` and `proc_nullable_array_lane_function_boundary.ls` are exact under both tiers.
- **Evidence at that checkpoint.** The current procedural directory sweep reported **145 scripts: 140 matches, 5 counted fallbacks, 0 mismatches, 0 timeouts**; the PDF directory reported **81/81 matches, 0 fallbacks, 0 mismatches, 0 timeouts**. A subsequent 685-script all-corpus run reached 350 rows but was inconclusive: the runner hit a sandbox `PermissionError` while reaping `test/lambda/editor/editor_api_basic.ls`; no partition manifest was changed. `make test-lambda-baseline` rebuilt successfully and passed **3861/3862** tests; the sole unrelated failure was `test_js_gtest`'s `tune4_global_callable_binding`, which could not load the absent `modules/node-crypto/node-crypto.dylib`. Rev53 supersedes that partial checkpoint with a complete 687-row partition and 522-test interpreter gate.

### 3.0.37 Direct-reduction join rebinding, deferred pattern materialization, and root-slice parity — 2026-08-21 (rev 47)

- **Direct-parser join predicates now bind the new source name before key extraction.** The reduction builder can construct `c.id`/`r.id` before the later loop-binding reduction installs `c`/`r`; the AST therefore retained an unresolved identifier even though MIR's name-keyed lowering found it. The direct builder now reattaches only that unresolved object-side identifier after registering the loop entry, preserving the join-key contract in **S14.1** and the shared AST/binding ownership boundary in **D7.2.1/D8.1.1v2**.
- **Named patterns remain first-class even when a later multi-declaration island reaches T0 without a type-list index.** T0 lazily compiles the valid `TypePattern` AST at the declaration use site and publishes it through the module-local type list, preserving the pattern carrier required by **S11.4.1/S11.4.6** and **D7.2.1**. This closes the `second_word` parity gap without changing MIR's regex or matching helpers.
- **Evidence.** `for_join_test.ls`, `for_join_s3b_test.ls`, and `string_pattern_syntax.ls` are byte-identical under T0 and JIT; the join slice also passes forced-GC/freed-memory poisoning. The bounded functional root sweep now completes **299 scripts: 272 matches, 25 counted fallbacks, 0 mismatches, 2 explicit renderer timeouts** (`graph_transform_html.ls`, `graph_transform_input.ls`). The 25 fallbacks are named in `temp/sweep_lambda_root_postfix4.tsv`; they are unsupported bridges/shape lanes or scripts whose JIT itself exits/crashes, not T0-vs-JIT mismatches. The 685-script all-corpus gate and partition refresh remain outstanding because the renderer/editor tail still needs a clean harness run.

### 3.0.38 Direct grouped-comprehension scope completion — 2026-08-21 (rev 48)

- **Direct parser group reductions now carry the aggregate binding identity.** The `into` name is registered after the post-group scope is entered, so the builder now retains its `NameEntry` on `AstGroupClause`; T0 can bind each materialized group without searching the closed row scope. Group-key reductions also use their dedicated `AST_NODE_GROUP_KEY` tag, preventing the frame planner from casting a key as a clause and dereferencing a null `entry`. This preserves the grouping scope boundary in **S14.1** and the planned-slot/rooting contract in **D5.3.3/D8.1.1v2**.
- **Evidence.** `for_group_test.ls` is byte-identical to JIT under normal T0 and `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`, with `executed=1 fallback=0`. The refreshed direct functional root sweep reports **299 scripts: 277 matches, 20 counted fallbacks, 0 mismatches, 2 explicit renderer timeouts** (`graph_transform_html.ls`, `graph_transform_input.ls`); the named rows are recorded in `temp/sweep_lambda_root_postfix5.tsv`. The 685-script all-corpus gate and partition refresh are still outstanding because its editor/renderer tail has not yet completed cleanly.

### 3.0.39 Interpreter gate partition and pattern/comprehension promotion — 2026-08-21 (rev 49)

- **The direct parser fixes are now represented by the focused interpreted subset.** The partition promotes the exact direct rows for grouped and joined comprehensions, string/symbol patterns, graph-layout helpers, selected typed-array paths, and import/error fixtures. The unsupported `sized_numeric_ushr` and `uint64_storage_lifetime` rows remain counted fallbacks; they still require the generic/full-width storage work described in **D3.2.2**.
- **Inconclusive rows are no longer mislabeled as fallbacks.** Renderer rows whose JIT side exceeds the bounded sweep budget are recorded in `test/lambda/interp_inconclusive.txt`, separate from `interp_excluded.txt`; the refresher now preserves match/fallback/inconclusive as three verdicts. A no-summary `node:none` row is also treated as “not executed” by the fallback assertion, rather than being reported as a false T0 execution.
- **Evidence at that checkpoint.** `test_interp_gtest.exe` focused promotion/fallback coverage passed (**23 tests** after the manifest refresh); the stale `object_constraint.ls` golden was retained as an inconclusive `golden_drift` row. The direct functional root slice was **299 scripts: 277 matches, 20 counted fallbacks, 0 mismatches, 2 bounded renderer timeouts**. Rev53 supersedes this checkpoint by closing the joined/procedural/numeric slices and recording the complete partition and baseline.

### 3.0.40 N-D ArrayNum view indexing — 2026-08-21 (rev 50)

- **N-D reads now admit statically proven ArrayNum views.** The interpreter already calls `array_num_at_nd`; the pre-scan now follows direct numeric literals plus `reshape` and `transpose` calls whose source is itself a proven numeric array. Generic arrays, dynamic coordinates, and effectful producers remain on MIR. This keeps the carrier proof aligned with the runtime representation and the index totality rule in **S10.1.3**, while preserving the planned/rooted helper boundary in **D5.3.3**.
- **Evidence at that checkpoint.** `typed_array_ndim_index.ls` and `typed_array_transpose.ls` now match their goldens under T0 with `executed=1 fallback=0`; both remain JIT-identical. The two rows are promoted from `interp_excluded.txt` into the interpreted subset. Rev53 subsequently closes the full P1 gate and records the final partition.

### 3.0.41 Generic u64 lane boundary and explicit sized-numeric coverage — 2026-08-21 (rev 51)

- **The original fail-closed gate was replaced by a lane-preserving read carrier.** `interp_item_at` first uses the ordinary runtime accessor, then re-reads only generic `LMD_TYPE_ARRAY` storage and boxes a raw `u64` lane before it crosses the AST boundary. Native nullable/sized lanes are excluded from that raw-Item inspection because their words are not pointers; this protects the tagged-value invariant in **D3.2.2** and the safepoint/rooting boundary in **D5.3.3**.
- **Evidence.** `sized_numeric_annotation_edges.ls`, `sized_numeric_ushr.ls`, `uint64_storage_lifetime.ls`, and `sized_numeric_collections.ls` now match JIT byte-for-byte under normal T0; the full forced-GC nullable/native lane rows and the 522-test P1 gate are green. The earlier use-sensitive rejection text is superseded by rev53.

### 3.0.42 Procedural typed-array mutation and N-D write promotion — 2026-08-21 (rev 52)

- **The already-landed COW lanes are now gated by executable evidence.** Procedural `fill`, mask assignment, splice, element mutation, N-D scalar writes, nullable native pointer/float/int64/sized arrays, typed-bool recursive updates, and sized-array widening all execute through the interpreter's existing checked setter/publication paths. Their admission preserves the COW sharing rule in **S9.1.2**, typed-array contract boundary in **S7.7.2**, and N-D index totality in **S10.1.3**; no new unchecked mutation path is introduced.
- **Evidence.** The 23 newly promoted procedural rows match their checked-in goldens under normal T0 and under `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`; the focused interpreter GTest rows pass with zero fallback. Rev53 closes the remaining `PIPE_FILE_STAM`, deferred declaration-boundary, generic `u64`, joined-group, and procedural-window slices and records the full 522-test gate.

### 3.0.43 P1 closure — 2026-08-21 (rev 53)

- **Semantics and representation.** Joined grouping now materializes rooted tuples before binding the post-group `into` scope; procedural `limit`/`offset` execute the complete body stream and discard only the stream result, matching MIR's side-effect semantics (**S14.1.1–S14.1.3**, **S12.1.2**, **D5.3.3**). Generic full-width `u64` reads preserve their raw lane through the boxed bridge, while native nullable lanes are never interpreted as pointer-shaped `Item`s (**D3.2.2**, **S10.1.3**). `PIPE_FILE_STAM` has structural AST/evaluator support; no active production grammar currently constructs it.
- **Corpus policy.** The three manifest files now partition the complete 687-script discovery set: 487 exact T0/JIT matches, 186 counted P1.6 fallbacks, and 14 explicitly named inconclusive rows. The partition has no duplicates, no extras, and no missing discovered golden. This satisfies the no-silent-cap requirement in **D8.1.1v2** and R4.
- **Gate evidence.** `make -C build/premake test_interp_gtest -j8` builds successfully; all 21 newly classified rows pass focused differential checks; `make test-lambda-interp` passes **522/522** tests (28 walker, 3 fallback-accounting, 4 frame-plan, 487 subset). Forced-GC/freed-memory coverage for nullable/native arrays, N-D writes, COW mutation, and generic `u64` rows is exact. The broader `make test-lambda-baseline` is also green at **3,864/3,864** (2,104 input plus 1,760 Lambda runtime tests). `git diff --check` is clean after this document update.

Seven defects the differential caught while landing these, all fixed:

1. **`and`/`or` short-circuited on an error operand.** An error is *falsy* (`is_truthy` returns `BOOL_FALSE` for `LMD_TYPE_ERROR`), which is precisely what makes `int("x") or 7` yield `7`. Returning early on an error broke containment. The walker now short-circuits only where `fn_and`/`fn_or` would return the left operand anyway and delegates the rest to the helper — the division of labour AI3 asks for.
2. **A braced `for` body's scope was unreachable from the AST.** `build_for_expr` created a per-iteration `NameScope` and dropped the reference; `AstListNode::vars` exists for exactly this and `build_content` never filled it, so the frame plan could not see the body's bindings. Fixed at the source in `build_ast.cpp` — lowering resolves those names through its own hashmaps and reads that field nowhere, so the write is inert for the JIT.
3. **`lambda.exe run` never invoked the user's `pn main()`.** `interp_run_script` set `Context::run_main` but had no equivalent of the generated module entry's scan for a top-level `pn main` and its zero-argument call, so *every* `run`-mode script silently produced empty output. Only the full-corpus sweep could see this — the 279-script sweep never used `run`.
4. **A bare top-level definition was never bound.** A script whose top level is a single statement carries it directly under `AST_SCRIPT` rather than inside a content list, so `pn main(){…}` alone reached `eval_expr`, which built an anonymous closure and left the name unbound. Adding any second top-level item masked it. The top-level walk now hoists and binds definitions the way `build_content`'s pass 1 does.
5. **The import cone was invisible to T0.** `direct_imports` is populated inside `compile_script_as_mir_direct` — the function the interpreter skips — so the cone that drives module init order was always empty. The T0 load path now records it from the AST's import children before publishing the tier decision.
6. **`C_RET_RETITEM` sys funcs were called through the wrong prototype.** `input()`/`parse()` register the raw function returning a 16-byte `RetItem`, but the walker called them as `Item(*)(…)` — undefined behaviour that happens to read the right register on arm64 while **silently discarding `.err`**, so a failure would surface as a plausible value. Lowering avoids this with `_mir` wrappers; the walker now uses the correct prototype and the same `ri.err ? ItemError : ri.value` mapping.
7. **Content-block value classification diverged in two places.** A `for` reached as an expression always yields its stream (the discard decision belongs to the enclosing block, as `transpile_expr` defers it to `transpile_content`), and the block-expression shortcut must exclude a lone `for` so its spreadable result flattens through `list_push_spread` instead of nesting. `is_proc_flow_side_effect_node` was promoted to `ast.hpp` so both tiers make the call from one predicate.

### 3.0.44 Object-constraint golden refresh — 2026-08-21 (rev 54)

- **Current oracle behavior.** `object_constraint.ls` was rerun through the normal JIT and now emits ten `true` lines. This is consistent with **S11.4.6**'s shipped base-only rule for constrained types: the fixture's field/object predicates are not yet enforced by the current `is` path, so the previous semantic-intent golden was stale relative to the current JIT oracle. The fixture comment records that distinction; no evaluator workaround was added.
- **Tier parity.** The updated golden matches both normal JIT and `LAMBDA_TIER=interp` byte-for-byte; the interpreter reports `executed=1 fallback=0`. The row moved from `golden_drift` to the direct subset, changing the partition to 488 subset / 186 excluded / 13 inconclusive while preserving the 687-script union.
- **Gate evidence.** The focused `object_constraint` GTest passes, and the complete `make test-lambda-interp` gate passes **523/523** (28 walker, 3 fallback-accounting, 4 frame-plan, 488 subset). Predicate enforcement remains future work; this refresh only records current JIT/T0 behavior.

### 3.0.45 `any[]` boundary parity and stale-exclusion refresh — 2026-08-21 (rev 55)

- **Root cause fixed.** MIR widens a packed N-D `ArrayNum` value to a boxed generic `Array` when it crosses an open `any[]` declaration boundary. T0 previously retained the packed carrier, so a later scalar COW index write treated a row as a scalar and diverged. The interpreter now calls the shared `ensure_typed_array(..., LMD_TYPE_ANY)` boundary conversion and applies the corresponding non-N-D admission rule. This preserves the declaration contract in **S7.7.2**, the mutation/ownership rule in **S9.1.2**, and the storage distinction in **D3.2.2**.
- **Rows promoted after exact differential probes.** `proc/cow_ordering.ls`, `proc/radiant_dom_set.ls`, `radiant_custom_layout_bfc.ls`, `radiant_custom_layout_flow.ls`, `radiant_dom_read.ls`, `radiant_dom_mutate.ls`, `radiant_poc.ls`, `radiant_poc_uaf.ls`, `radiant_register_layout.ls`, and `radiant_vmap_projection.ls` now match JIT byte-for-byte under T0 with `executed=1 fallback=0`.
- **Live evidence.** The checked-in manifests partition all 687 discovered scripts as **598 subset / 87 excluded / 2 inconclusive** with no duplicates, missing rows, or divergent rows. `make test-lambda-interp` passes **633/633** (28 walker, 3 fallback-accounting, 4 frame-plan, 598 subset). The two remaining inconclusive rows are `graph_transform_html.ls` and `graph_transform_input.ls`, both bounded timeouts. The tiered-policy accounting remains the explicit **D8.1.1v2** contract.

### 3.0.46 Ordinary-`for` frame-plan safety and graph promotion — 2026-08-21 (rev 56)

- **Root cause fixed.** `eval_for` always reserves output and ordering-key homes, and each recursive loop level publishes a collection home. The frame-plan's ordinary `FOR_EXPR` floor accounted for only one of those fixed homes, so nested graph comprehensions could log `scratch overflow` at a GC-capable call boundary even when their output appeared correct. The planner now reserves the three fixed homes plus the widest child demand, preserving the static-window invariant in **D5.1.1**, **D5.3.3**, and **D8.1.1v2**.
- **Rows promoted after exact differential probes.** `graph/mermaid/canonical_ir.ls` and `graph/graphviz/annotations.ls` now match their JIT outputs byte-for-byte under T0 with `executed=1 fallback=0`; both were also checked for zero scratch-overflow diagnostics after the planner fix.
- **Live evidence.** The manifests now partition all 687 discovered scripts as **600 subset / 85 excluded / 2 inconclusive** with no duplicates, missing rows, or divergent rows. `make test-lambda-interp` passes **635/635** (28 walker, 3 fallback-accounting, 4 frame-plan, 600 subset). The two inconclusive rows remain the bounded `graph_transform_html.ls` and `graph_transform_input.ls` timeouts.

### 3.0.47 Mermaid graph promotion — 2026-08-22 (rev 57)

- **The ordinary-`for` planner fix clears the larger Mermaid graph cone.** The 17 Mermaid fixtures covering class, ER, state, style, metadata, labels, markers, source fidelity, and scene rendering now execute entirely under T0. Each was probed directly against the JIT with `executed=1 fallback=0`, byte-identical output, and no `scratch overflow` diagnostic; the existing goldens are unchanged.
- **Live evidence.** The manifests then partitioned all 687 scripts as **617 subset / 68 excluded / 2 inconclusive** with no duplicates, missing rows, or divergent rows. The full interpreter gate passed **652/652** (28 walker, 3 fallback-accounting, 4 frame-plan, 617 subset). This widens only the already-proven P1 comprehension/import path; it does not admit views, async tasks, or cross-language imports. The promotion follows **D5.1.1**, **D5.3.3**, and **D8.1.1v2**.

### 3.0.48 Graphviz and Structurizr promotion — 2026-08-22 (rev 58)

- **All remaining Graphviz and Structurizr golden rows are now T0-native.** Serial 60-second differential sweeps found 11 Graphviz and 15 Structurizr rows with JIT/T0 status `ok`, `executed=1 fallback=0`, and no mismatches or timeouts. The focused 61-test golden gate passed before the full gate; no fixture/golden changes were required.
- **Live evidence.** The manifests now partition all 687 scripts as **643 subset / 42 excluded / 2 inconclusive** with no duplicates, missing rows, or divergent rows. `make test-lambda-interp` (the 678-test interpreter gate) passes **678/678**. The remaining exclusions are the explicit async/task, view/edit registry, cross-language import, invalid/crashing object, and unsupported native-system policy boundaries under **D8.1.1v2** and **D5.3.3**.

### 3.0.49 Stateless view-template registry bridge — 2026-08-22 (rev 59)

- **Root cause fixed.** T0 previously rejected every `AST_NODE_VIEW` because the generated path registered an `apply()` template entry and invoked a MIR body function, while the interpreter had no equivalent registry publication or `~`-context body bridge. Stateless `view` declarations now publish interpreter-owned entries with the same type/tag specificity and definition-order matching rules as MIR; `apply()` evaluates the body in a fresh planned frame with the model rooted as `~`. Stateful, edit, and event-bearing views remain fail-closed because their state store and handler ABI are not yet interpreter-owned. This preserves the shared dispatch contract in **D8.1.1v2** and the root-before-call invariant in **D5.3.3**.
- **Rows promoted after exact differential probes.** `grammar_reduce5_scanner.ls` and `view_template.ls` now run with `executed=1 fallback=0` and byte-identical JIT/T0 output; their checked-in goldens are unchanged. The focused two-row golden gate passes.
- **Live evidence.** The manifests now partition all 687 scripts as **645 subset / 40 excluded / 2 inconclusive** with no duplicates, missing rows, or divergent rows. The full interpreter gate passes **680/680**. Remaining `AST_NODE_VIEW` exclusions are stateful/edit/event-bearing fixtures; cross-language imports, async/task procedures, invalid/crashing object forms, and unsupported native-system families remain explicit policy rows.

### 3.0.50 Stateful view and event-handler bridge — 2026-08-22 (rev 60)

- **Root cause fixed.** The stateless bridge still rejected view declarations whose body read state or whose event handlers were present: those names were not frame-plan bindings, and the event registry stored only erased MIR function pointers. T0 now overlays state and handler-parameter bindings on a rooted activation, initializes state with `tmpl_state_get_or_init`, writes through `tmpl_state_set` (including render-map dirtiness), and publishes AST handler entries that the Radiant event dispatcher invokes through an interpreter bridge. Edit templates remain excluded because their MarkEditor transaction ABI is distinct.
- **Rows promoted after exact differential probes.** `render_map.ls` and `view_state.ls` now run with `executed=1 fallback=0`, byte-identical output, and unchanged goldens. Both also pass with `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`; the focused four-row view gate passes.
- **Live evidence.** The manifests now partition all 687 scripts as **647 subset / 38 excluded / 2 inconclusive** with no duplicates, missing rows, or divergent rows. The full interpreter gate passes **682/682**. The sole remaining `AST_NODE_VIEW` exclusion is `edit_bridge.ls`; cross-language imports, async/task procedures, invalid/crashing object forms, and unsupported native-system families remain explicit policy rows.

### 3.0.51 Edit-template bridge and retained-event state guard — 2026-08-22 (rev 61)

- **Root cause fixed.** The interpreter registry previously published every T0 view as a normal render template and rejected `edit` declarations at the scanner boundary, so `apply(..., {mode: "edit"})` could never select an interpreter-owned edit body. The registry now preserves `AstViewNode::is_edit`; the same planned `~`/state/handler activation evaluates edit bodies, while editor-only native operations remain subject to the ordinary system-function admission checks. Retained Radiant events also create a bounded temporary `InterpState` from the document `EvalContext` when the script runner has already unwound, preserving the event bridge outside the initial script call. These changes protect the context ownership and root-before-call invariants in **D5.3.3** and **D8.1.1v2**.
- **Rows promoted after exact differential probes.** `edit_bridge.ls` now runs with `executed=1 fallback=0`, byte-identical output under JIT and T0, and exact output under forced GC plus freed-memory poisoning. The focused view/edit gate (including `grammar_reduce5_scanner.ls`, `render_map.ls`, `view_state.ls`, and `view_template.ls`) passes; the exclusion-accounting test remains green with the expected `object_update.ls` crash row.
- **Live evidence.** The manifests now partition all 687 scripts as **648 subset / 37 excluded / 2 inconclusive** with no duplicates, missing rows, or divergent rows. The remaining `AST_NODE_VIEW` exclusion is closed; the 37 explicit rows are cross-language imports, async/task procedures, invalid/crashing object forms, and unsupported native-system families. The full 648-row gate refresh is the next verification step.

### 3.0.52 Hosted-JavaScript import bridge — 2026-08-22 (rev 62)

- **Root cause fixed.** T0 already had an evaluated, rooted JavaScript module namespace, but the interpreter import planner treated every cross-language import as a Lambda slab dependency and rejected the whole cone at `AST_NODE_IMPORT`. Hosted JavaScript imports now resolve their synthetic export binding through `module_namespace_get()` and dispatch calls through the existing `js_call_export_N_into` boxed ABI. Lambda-to-Lambda imports still require one shared planned slab, and async/task imports remain fail-closed; this preserves the module ownership and tier boundary in **D7.2.1**, **D7.2.2**, and **D8.1.1v2**.
- **Rows promoted after exact differential probes.** `binary_js_bridge.ls`, `import_js.ls`, `import_js_naming.ls`, and `js_array_props_tail_bridge.ls` now run with `executed=1 fallback=0`, byte-identical output under JIT and T0, and no fixture or golden changes. `object_update.ls` also now runs with `executed=1 fallback=0`; the shared object spread path reads omitted fields from `*:source` before typed storage coercion instead of decoding a null numeric payload. All five rows pass with `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`; the focused import/object-update interpreter gates pass.
- **Remaining blockers are structural.** The 26 `AST_NODE_PROC` rows require a resumable `LambdaAsyncFrame`/scheduler poll continuation across `sleep`, `wait`, mailbox, cancellation, timeout, and `io.read`; the boxed T0 `InterpFrame` is deliberately not admitted as a synchronous substitute under **D5.1.3**, **D8.1.1v2**, and **S12.1.2**. The three remaining import rows all enter that async/promise cone. The three `node:none` fixtures remain compile-invalid (`int64`/procedure-method-in-function diagnostics); no fixture was altered to hide those diagnostics.
- **Live evidence.** The manifests now partition all 687 discovered scripts as **653 subset / 32 excluded / 2 inconclusive** with no duplicates, missing rows, or divergent rows. The remaining exclusions are 26 async/task procedure cases, three Lambda/async import cases, and three compile-invalid fixtures. The object-update crash is fixed at the shared typed-object spread boundary; the full 653-row gate refresh remains pending.

### 3.0.53 Async/task satellites and import-cone completion — 2026-08-22 (rev 63)

- **Root cause fixed.** T0's boxed `InterpFrame` cannot itself suspend through the scheduler, but the existing MIR lowering already has the resumable task state machine. Task-backed procedures that pass the satellite structural scan now publish their generated resumable entry as a boxed `Function` value; the module, synchronous code, handlers, cancellation, mailbox, timeout, and GC-sensitive values remain owned by T0. Satellite property reads use an append-only slice of the context-local sealed name image, and the owner binds its immutable const/type image before a satellite can execute. This preserves the suspension/rooting boundary in **D5.1.3**, **D5.3.3**, and **D8.1.1v2** rather than pretending an async body is synchronous.
- **Import cone fixed.** Function-local Lambda import views are rebound to their already-planned declaring-module slot before satellite admission (**D7.2.1**). Hosted JavaScript exports are registered through the namespace membrane before satellite MIR linking, then invoked through `js_call_export_N_into`; promise-returning exports therefore use the same async continuation path as direct JS calls (**D7.2.2**).
- **Rows promoted after exact probes.** All 26 former `AST_NODE_PROC` exclusions plus `conc/imported_call.ls`, `conc/js_promise_wait.ls`, and `conc/to_promise.ls` now report `executed=1 fallback=0`, exit successfully, and match the JIT output. The rows were rerun with `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`; existing goldens are unchanged, and the dependency-only `conc_worker_mod.ls` now has its required empty result golden. The focused `InterpWalker.*:InterpFallback.*:InterpFramePlan.*` gate passes **35/35**.
- **Current partition.** `test/lambda/interp_p0_subset.txt` contains **682** rows, `test/lambda/interp_excluded.txt` contains only the three compile-invalid `node:none` fixtures (`map_object_robustness.ls`, `object_direct_access.ls`, `typed_param_direct_access.ls`), and `interp_inconclusive.txt` retains the two renderer timeouts. The serialized 682-row gate was started but not accepted: it reached the known host/Jube and Radiant rows that emit empty interpreter output and was stopped before the slow graph/renderer tail, so the full gate remains pending; no invalid fixture was rewritten to hide its diagnostics.

### 3.0.54 Full P1 promotion — 2026-08-22 (rev 64)

- **Parser/interface root cause fixed.** The renderer-transform rows were not timeouts: the checked-in production AST enum was paired with a stale/mismatched generated Tree-sitter artifact, so `jube_compile_module_interface()` saw syntax errors in the Radiant declaration and rejected the `radiant` import cone. Rebuilding the production parser archive from the grammar revision that matches `lambda/runtime/ts-enum.h` restores the interface AST and keeps the formal Jube syntax check under **D7.2.1**, **D7.2.2**, and **D8.1.1v2**.
- **Fixture boundary repaired.** `map_object_robustness.ls`, `object_direct_access.ls`, and `typed_param_direct_access.ls` now exercise their `pn` mutation paths from `pn main()` with local `var` receivers; `int64` is corrected to the canonical `i64`, and reserved helper/member names are avoided. Their goldens are regenerated from the JIT run and compared by T0, preserving the no-golden-masking rule in **SI3v2**.
- **Rows promoted after exact differential probes.** Both graph-transform rows and all three formerly `node:none` rows report `executed=1 fallback=0`, exit successfully, and match JIT output. `map_object_robustness`, `object_direct_access`, and `typed_param_direct_access` also pass the forced-GC interpreter path; the focused subset gate covers the new `run` entries.
- **Current partition.** `interp_p0_subset.txt` contains **687** rows, while `interp_excluded.txt` and `interp_inconclusive.txt` retain only their explanatory headers. The 687-row subset/fallback gate passed **689/689** (687 subset rows plus the two fallback-accounting probes), and the complete walker/frame-plan/fallback suite passed **35/35**.

### 3.0.55 Invalid member access boundary — 2026-08-24 (rev 65)

- **The write boundary now mirrors the total-read ruling.** MIR Direct and T0 route computed array, map, element, and VMAP stores through the checked `member_set_cow`/`fn_index_set` boundary. Wrong key domains, fractional or negative positions, missing named fields, out-of-bounds positions, and invalid bases return the hard `ItemError`/`T^` channel; reads remain `null`-total under **S7.1.1v2**, **S7.1.3v2**, **S8.2.1v2**, and **S9.1.6**. Exact integral float/decimal keys are normalized without machine-index truncation, and the container kind is never promoted by a failed write.
- **The read boundary is quiet for non-indexable bases.** Numeric keys on maps/objects now return `null` before `item_at`, avoiding an unsupported-type diagnostic on an otherwise valid total read.
- **Typed and vector paths share the same admission.** Typed-array writes validate the boxed key before narrowing, while scalar, range, and mask assignment errors now propagate instead of being silently dropped. The change keeps the existing numeric-mask store and COW publication paths intact.
- **Evidence.** `proc_invalid_member_access.ls` covers invalid array/map/element reads, hard writes, typed-array string/exact-float/exact-decimal keys, symbol and empty-string map keys, element attributes, and unchanged containers. Its JIT golden, `AutoDiscovered/LambdaScriptTest`, `InterpSubsetTest`, and `InterpWalker.InvalidMemberAccessUsesNullReadsAndHardWriteErrors` all pass with `fallback=0`; the focused neighboring OOB/mask/element/VMap interpreter tests remain green.
- **Gate refresh.** Adding this fixture brings `interp_p0_subset.txt` to **688** rows; `interp_excluded.txt` and `interp_inconclusive.txt` remain empty. `make test-lambda-interp` passes **724/724** (30 walker, 4 frame-plan, 2 fallback-accounting, and 688 subset tests), with zero interpreter fallbacks.

### 3.0.56 Tail-call hotness and entry-equivalent handoff — 2026-08-25 (rev 67)

- **Root cause and policy.** TCO self-calls reuse the active `InterpFrame` and therefore bypass `lambda_dynamic_call`, so only the outer activation previously advanced the definition-site `call_count`. A validated self-tail iteration now increments both `call_count` and a separate saturated `tail_edge_count`; the shared default `LAMBDA_JIT_THRESHOLD` is **5**.
- **Tail handoff.** On the fifth direct tail edge, T0 retains the rooted source argument vector, compiles through the ordinary satellite publication path without incrementing the counter again, detaches the inactive interpreter frame from the active caller chain, and invokes the standard boxed entry. Failed admission or compilation leaves the already-coerced T0 parameter slots in place and TCO continues. No arbitrary local/PC materialization occurs; general loop backedges remain next-entry-only (**D8.1.1v5**).
- **Regression evidence.** `InterpPromotion.TailIterationsHandoffAtDefaultThreshold` proves that four tail edges do not compile and the fifth compiles during a single `loop(20, 0)` activation, whose remaining iterations run through the satellite and still produce `20`.
- **Release gate.** Fresh release `test_lambda_gtest` AUTO samples pass **758/758** at **61.26s, 52.41s, and 47.96s real** (median **52.41s**). The same release binary logs one `loop` satellite during a single `loop(20, 0)` activation. These samples were host-loadier than the earlier 37.96s AUTO median, so they are not an A/B profitability claim; the next comparison must rerun the interpreter and JIT controls in the same session.

### P1.1 Comprehensions, pipes, implicit contexts

- [x] `FOR_EXPR`/`FOR_STAM` with the full clause set (`AstForNode`: `where/group/order/limit/limit_from_end/offset/then`; `AstLoopNode` joins incl. `join_keys`/`key_filter`/`key_only`/`optional`), `LOOP`, `ORDER_SPEC`, `GROUP_CLAUSE`, `JOIN_KEY`; accumulators/group tables use the same container/sort helpers as lowering (S6 total order lives in the helpers). Ordered streams, one-source groups, tuple-stream inner/left joins, joined grouping, and procedural windows are covered by the 678-test gate.
- [x] `PIPE` (+ `where` filter form), pipe argument injection, and structural `PIPE_FILE_STAM` evaluation; active grammar paths that require unsupported native output remain explicit P1.6 fallbacks.
- [x] Implicit contexts as slot-backed stacks in `InterpState` (`~`, `~#`, `last`; innermost-wins per S10.1.3), mirroring the transpiler's build-time context (`last_index_object` behavior); `CURRENT_ITEM`, `CURRENT_INDEX`, `LAST_INDEX` read them. Unordered expression-form `FOR` windows use the full stream before `offset`/first/last selection.

### P1.2 Match, types, patterns

- [x] `MATCH_EXPR`/`MATCH_ARM`: the scrutinee is evaluated once (S11.2.1); literal arms use `fn_eq`, type arms use `fn_is`, and constrained arms enter the P3 `PREDICATE` subset after their base check. Generic `fn_is` and validator policy remain base-only under S11.4.6.
- [x] Type-expression nodes as values (`TYPE`, `*_TYPE`, `TYPE_STAM`, `CONSTRAINED_TYPE`, `OBJECT_TYPE`) resolve through the build-time `Type*` graph exactly as lowering does; the walker does not construct types at runtime.
- [x] String/symbol patterns (`STRING_PATTERN`, `SYMBOL_PATTERN`, `PATTERN_SEQ`, `PATTERN_CHAR_CLASS`, `PATTERN_ISLAND`) use the prepass pattern helpers and const-pool pattern values; satellite specialization remains P2 work.

### P1.3 Documents, paths, queries

- [x] `ELEMENT`/`CONTENT_TYPE`/`ELMT_TYPE`, `OBJECT_LITERAL`, `SPREAD`; object/update forms that require dynamic or renderer-only receivers are routed through the explicit P1.6 policy list.
- [x] Positional/named `DECOMPOSE`, including lexical list blocks. `NAMED_ARG` is complete for a statically resolved direct Lambda call and for pure system calls whose names are source labels lowered positionally; dynamic calls and procedural system calls remain excluded (S12.3.2, S9.1.2).
- [x] `PATH_EXPR`/`PATH_INDEX_EXPR`/`PARENT_EXPR`/`QUERY_EXPR` via the shared `path_*`/query helpers.

### P1.4 Procedural + error/fault channels

- [x] `EvalSignal` completion for `VAR_STAM`, `ASSIGN_STAM`, `INDEX_ASSIGN_STAM`/`MEMBER_ASSIGN_STAM`, `WHILE_STAM`/`DO_WHILE_STAM`, `BREAK_STAM`/`CONTINUE_STAM`, and explicit `RETURN_STAM` (`pn`-only set per S12.1.2), using the same COW/checked-store helpers as MIR (S9.1.2).
- [x] Error-as-value check placement matches `emit_return_if_item_error`/`emit_jump_if_item_error`; `RAISE_STAM`/`RAISE_EXPR`, `f(...)^`, `^`/`is error`, and `CURRENT_ERROR` are covered at their shared boundaries.
- [x] Declaration-boundary skip (S7.7.2) propagates `ERROR_SKIP` to the frame owning the declaring block; failed bindings are never published.
- [x] `HANDLER_EXPR`/`HANDLER_STAM` (`^{ }`) arm `LOCAL_FAULT` recovery frames; structural `PIPE_FILE_STAM` evaluation is present and the existing procedural pipe-file fixture remains exact.
- [x] **Self-tail-call iteration** (AIO1's committed v1 slice): tail-position self-calls rebind parameter slots and loop with the JIT's `LAMBDA_TCO_MAX_ITERATIONS` cap; general TCO remains a P2 optimization concern.

### P1.5 Modules

- [x] `IMPORT`/`PUB_STAM`; the tier-aware load path records the cone, initializes modules post-order, and places each module under a `TRANSACTION_BARRIER` recovery frame with `lambda_module_state_reset()` on abandonment (D7.2.2/S7.7.6).
- [x] Module slabs are per-module persistent-rooted Item arrays in `EvalContext::module_states` / `module_state_id`; cross-module identifier resolution honors `entry->import`, and const/type lookups use the declaring Script (AI6/AIO12).
- [ ] Built-in `math`/`io` imports need no work (they resolve to `SYS_FUNC` nodes at build time); Jube static-module imports: verify call-site resolution in interp mode, fall back if any lowering-time dependency surfaces (documented, counted).
- [x] Retained-module (L1 cache) interaction: interp-mode Scripts carry no `jit_context`; retain/invalidate paths tolerate NULL JIT state. Mixed-tier Lambda imports and hosted-JavaScript namespace imports use the shared module membrane; task-backed procedures delegate only their resumable bodies to satellites.

### P1.6 Exclusions wired explicitly

- [x] Unsupported generators and stateful/native-system forms remain governed by the explicit tier policy, while the former object/map source-invalid rows now have legal procedural entrypoints and execute under T0. Task-backed procedures use a resumable MIR satellite for the suspension body while the owning module and synchronous definitions stay under T0; direct Lambda imports and hosted-JavaScript promise exports use the same sealed module/import ABI. The three-way partition is explicit even when both `interp_excluded.txt` and `interp_inconclusive.txt` contain zero rows (no silent caps).

### P1 gate (G1)

| Check | Criterion |
|---|---|
| G1.1 full differential | `make test-lambda-interp` runs the committed 688-row T0 subset with zero fallback and exact JIT/golden output; the three-way 688-row partition has zero excluded and zero inconclusive rows (SI3/D3.3.1/D8.1.1v2) |
| G1.2 GC stress | Forced-GC/freed-memory differentials pass for the promoted object/map and renderer/import rows plus the prior async/task, typed-object-update, view/edit/state/handler, nullable/native, COW, N-D, sized, generic-`u64`, and comprehension frame-plan boundaries; a sanitizer-wide run remains a CI-strengthening task, not an unverified completion claim |
| G1.3 non-regression | Direct premake build, the 724-test interpreter gate, empty-exclusion accounting probe, and focused walker/frame-plan suite pass; default-tier behavior remains unchanged by the interpreter-only path |
| G1.4 **measurement report** | §6's historical performance report remains labeled to its 2026-08-15 corpus; rev64 adds the current 687-row correctness partition and exact parser/fixture evidence rather than silently rewriting old measurements |

### 3.1 P2 — opt-in satellite tier-up — **in progress 2026-08-19**

The implemented unit is one definition plus its generated `_b` wrapper. Its
`FnAnalysis::promotion` starts in `INTERP`, increments at the single dynamic
dispatcher, compiles synchronously at `LAMBDA_JIT_THRESHOLD` (default `5`),
then publishes `boxed_entry` before upgrading each observed `Function` value
to the ordinary context-owning boxed ABI. A compiler failure pins that
definition to T0; it never changes the current run to whole-module JIT
(D8.1.1v2 §5.1–§5.3, DI14).

Each satellite has unique module and metadata BSS names because `find_import`
is context-wide. Its global lowering map is reconstructed from the T0
frame-plan's `NameEntry::slot` values and its `LambdaModuleLayout::var_count`
is the complete interpreter slab size. Therefore scalar module reads and
cross-satellite calls use the same per-`EvalContext` state as T0; no eager
`_gvar_*` numbering is reused.

A satellite may also read a planned Lambda import without linking that
module's MIR image. The imported `NameEntry` already resolves at frame-plan
time to its T0 owner Script and slot, so satellite lowering embeds that
immutable `{module_id, slot}` pair and reads the per-context slab directly.
Imported function calls deliberately continue through the boxed dynamic
dispatcher, which observes the provider's current T0-or-promoted `Function`
entry; no imported direct wrapper symbol or mixed T0/MIR cone is created
(D7.2.1, D8.1.1v2, D5.3.3).

The focused P2 fixtures are now exact at threshold `3` (and the import fixture
at threshold `2`):

| Tier | Result | Evidence |
|---|---|---|
| `interp` | `[3, 4, 5, 6, 10, 5, 6, 7, 4, 2]` | T0 only, zero fallback |
| `auto` | `[3, 4, 5, 6, 10, 5, 6, 7, 4, 2]` | four scalar satellites; module reads are unboxed through the shared slab ABI |
| `jit` | `[3, 4, 5, 6, 10, 5, 6, 7, 4, 2]` | existing eager whole-module control |

`test/lambda/interp_auto_import.ls` adds the T0-import boundary: its provider
exports `offset` and `shift`, while the importing `hot` function reads both.
Under `auto` with `LAMBDA_JIT_THRESHOLD=2`, the provider `shift` and importing
`hot` each publish one satellite and all three tiers return
`[21, 22, 23, 24]`. The importer satellite reads the provider's slab Function
dynamically; it never resolves `mN._shift_b_*`. The scalar slab read is
unboxed before native MIR arithmetic, preserving the shared module contract.

Still pinned to T0: captures/nested definitions, generators, and object/view
mutation code, because their environment or replacement-channel contracts are
not yet satellite-safe. Rev63 adds a bounded named-property key-image slice and
hosted-JavaScript promise bridge for task-backed bodies; general P2 promotion
still requires the complete closure/call-site matrix. `LAMBDA_JIT_BACKEDGE` now defaults to
`1024`: a T0 `while` or comprehension continuation increments the active
definition's counter, and crossing it permits compilation only on a later
dynamic entry. Direct self-tail edges use their separate threshold-5
entry-equivalent handoff; arbitrary loop OSR remains unimplemented. `interp_auto_tier.ls`
forces `LAMBDA_JIT_THRESHOLD=100 LAMBDA_JIT_BACKEDGE=2`; its first
`loop_count(4)` remains T0 and the second entry compiles the sole satellite.
Full-AST call-site analysis persistence and the complete P2 matrix remain
open. P3's validator de-JIT and P4's persistent REPL state remain blocked on
their stated design gates. P5's selector flip is landed: `LAMBDA_TIER` unset
selects `auto`, and the release corpus passes 758/758 with no failure or
segfault. Batch mode deliberately disables cross-script MIR retention because
its per-script heap/slab reset invalidates retained module Items.

### 3.2 P3 — restricted `that` predicates + immediate-scalar CONST folding — **vertical slices landed 2026-08-19**

`InterpState` now carries `EvalMode::{RUNTIME, CONST, PREDICATE}` plus a
per-attempt fuel counter. The live slice enters `PREDICATE` only for non-object
`AstConstrainedTypeNode` checks reached by `expr is T` or a constrained match
arm. It binds `~`/`~#`/parent/root through the existing slot-backed context
stack, decrements `LAMBDA_PREDICATE_FUEL` at each evaluated AST node (default
`1024`), and rejects/exhausts as a failed predicate rather than publishing a
partial result. Nested constrained checks share the outer budget.

Admission is fail-closed: `interp_predicate_supported()` permits literals,
`~`, pure unary/binary/member/index shapes, conditionals, and a narrow
registered-sysfunc allow-list (including `len`). It rejects identifiers,
closures/procedures, arbitrary calls, containers, assignments, handlers,
pipes, loops, async, I/O, and mutation. `eval_call` repeats the allow-list
check at runtime so a future AST edge cannot silently weaken the boundary.
This is the AI17 mechanism only: generic `fn_is`, object constraints, and the
validator retain the S11.4.6 base-only interim; no validator/JIT ownership path
was changed.

The constrained type expression now resolves through its `type_list` index,
the same runtime Type* identity lowering emits. `plan_need` reserves the
context homes around a constrained predicate and match arms, so the new mode
does not trade predicate correctness for an unrooted Item (D5.3.3).

The CONST slice runs as a `const-fold` compiler pass after indexed AST
construction and after the canonical `EvalContext` is bound. It reuses
`eval_expr` in `EvalMode::CONST`, with `LAMBDA_CONST_FUEL` (default `1024`) and
an exact `LAMBDA_CONST_FOLD=0` off switch. Admission is deliberately narrower
than PREDICATE: literal null/bool/int/float syntax; `not`, unary sign; scalar
arithmetic, comparisons, `and`/`or`; and an all-literal `if`. Calls, bindings,
containers, reads, mutation, and pointer-backed results are rejected. Every
attempt runs in a throwaway side-stack frame and contains an unexpected native
fault; failed, rejected, exhausted, or non-immediate attempts leave no fact.

Facts store only tagged null/bool/int words in `AstIndex`, never a pointer or a
number-stack home. Lowering consumes a fact only at a known generic-`Item`
boundary (the generic array path and `transpile_box_item`); it emits the full
tagged word rather than a native arithmetic lane. This preserves D5.3.3's
rooting/representation boundary and DI14's cache-relocation rule. The slice is
therefore a safe P3 implementation increment, not a claim that all pure AST
shapes or all lowering boundaries fold yet (D8.1.1v2, AI16).

Verified locally:

| Check | Result |
|---|---|
| `test/lambda/constrained_type.ls` under `LAMBDA_TIER=interp` | exact existing golden; zero fallback |
| new `test/lambda/interp_predicate_mode.ls` under `jit`, `interp`, and `auto` | exact golden `[true, false, true, false, "negative"]`; covers alias `is`, `len(~)`, and constrained match arms |
| same fixture with `LAMBDA_PREDICATE_FUEL=1` | `[false, false, false, false, "other"]`; bounded checks fail closed without a runtime error |
| new `test/lambda/interp_const_fold.ls`, JIT fold on versus `LAMBDA_CONST_FOLD=0` | exact golden `[42, true, true, 13]` in both modes; fold-on MIR contains four canonical tagged immediates while fold-off retains arithmetic/control-flow lowering |
| `P0Subset/InterpSubsetTest.MatchesGoldenWithoutFallback/interp_const_fold` | PASS — exact golden under T0 with zero fallback |
| focused `test_interp_gtest` constrained cases | PASS — both moved subset entries, zero fallback |
| `make build` | PASS (2026-08-19) |

`constrained_type.ls` moved from `interp_excluded.txt` into the zero-fallback
differential subset, together with the P3 predicate and CONST fixtures. The
CONST pass now owns its indexed-AST/EvalContext seam; validator de-JIT remains
unimplemented because its `TypeObject::constraint` ownership and validator
entry routing have not yet been moved off JIT (D8.1.1v2, AI16/AI17).

### 3.3 P4 — persistent interpreter REPL — **vertical slice landed 2026-08-19**

`LAMBDA_TIER=interp` and `LAMBDA_TIER=auto` now create one empty T0 Script
when the REPL starts. A completed input is parsed as its own Tree-sitter tree,
then shifted into the session's append-only source before `build_repl_fragment`
builds it against the retained global `NameScope`. The session retains every
fragment tree for the Script lifetime, appends only the new indexed AST nodes,
assigns slots only to newly introduced bindings, and runs only the fragment's
top-level nodes. Existing definitions and module-slot values therefore stay
live without re-parsing, rebuilding, or re-executing the preceding history
(D8.1.1v2, D7.2.1).

The Script stores its top-level append cursor, so linking a new fragment does
not scan earlier history. The module slab uses geometric storage growth while
registering its one owned GC-root range before retiring the old range; its
logical `var_count` stays exact for interpreter and satellite slot checks
(D5.3.3). Each completed input snapshots the live slots before execution. An
AST/build/plan rejection or an error Item restores the name-scope tail,
const/type-list lengths, source length, AST index, and slab values, then drops
the fragment tree. `clear` releases the old module's exact root and Script
before constructing a clean session. Imports after the session begins are
deliberately rejected: the incremental REPL transaction has no cone
load/initialization-and-rollback protocol yet, even though a fully planned T0
module can now supply a P2 satellite binding (D7.2.2).

This is a REPL vertical slice, not P5: unset and `LAMBDA_TIER=jit` retain the
historical whole-history REPL, and no latency table is claimed until the
release-build C3 driver measures it. The normal interpreter differential gate
also remains an independent P1 status signal, not evidence for this stateful
interactive path.

Verified locally:

| Check | Result |
|---|---|
| persistent `let x = 40` → `x + 2` → `fn add(y) => x + y` → `add(3)` under `interp` | PASS — `42`, then `43`; four fragment executions and zero fallback |
| same session with forced collection (`LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`) | PASS — retained array binding and function return `42`, `43` |
| propagated runtime error in `let y = fail()^`, then `x`/`y` | PASS — `x` remains `40`; the failed binding is absent |
| `clear`, then read former `x` | PASS — former binding is absent; no invalid free or retained module root |
| `auto` session, threshold 2, two calls to `add` | PASS — one satellite compiles and returns `41`, `42`; `clear` then discards the session safely |
| `make build -j8` | PASS (2026-08-19) |

`make test-lambda-interp` was started after the P4 work. Its focused walker and
frame-plan groups passed, but later `make build` invocations replaced
`lambda.exe` while the long-running subprocess gate was still launching
cases. That run is invalid as an acceptance result and must be restarted from
a stable binary; it is not evidence of P4 or P2 coverage loss.

## 4. Walker ↔ helper boundary rules (normative for both phases)

1. Any Item held across a child eval or MAY_GC helper call sits in a frame slot first and is re-read after (D5.3.3; buffers move).
2. Frames are strictly LIFO; only recovery landings may abandon them (watermark restore does the cleanup).
3. Zero-before-publish on every window; slot writes are single 8-byte stores; non-Item raw words never enter root slots.
4. Wide scalars follow the shipping home conventions (§P0.3); results crossing a frame boundary are rehomed before watermark restore.
5. `EvalSignal` is the only non-local mechanism for language control flow; `longjmp` is fault-only (AI13/AI14).
6. The walker never mutates the AST, `Type*` graph, or `NameEntry` records at runtime — all runtime state lives in frames, slabs, and heap values (hot-reload and re-entrancy depend on this).

## 5. Test strategy

- **Unit** (`test/test_interp_gtest.cpp`): frame-plan slot/depth assertions; walker micro-tests per node family (build from source snippets, compare against expected Items); signal-propagation tests (nested loops + break/continue/return); depth-budget fault test.
- **Integration**: the existing `test/lambda/*.ls` + goldens, driven through the tier matrix. `make test-lambda-interp` = subset (P0) → full-minus-exclusions (P1). New `.ls` tests always ship with `.txt` goldens (rule 8).
- **Differential mechanics**: both tiers validate against the same goldens; additionally the P1 gate diffs raw formatted output of both runs for a sample to catch golden-blind divergence (e.g., float formatting).
- **Stress**: gc stress knobs + ASan job in CI for the interp matrix leg; a fuzz-ish generated-expression corpus (from `gen_bench.py`) run under stress.
- **Fallback accounting**: the runner prints `interp: executed=N fallback=M excluded=K`; gates pin M.

## 6. The measurement report — exit-gate deliverable

Published *in this document* (section replaces TBD cells; house style `[measured YYYY-MM-DD]` tags), produced by `make interp-bench` writing `temp/interp_bench.tsv`. Method: release build (rule 10), one warm-up + five measured runs, median (the U33 protocol); RSS from `ru_maxrss`; both tiers on the same binary, selected by `LAMBDA_TIER`.

### 6.1 Corpus

| Item | What it measures |
|---|---|
| C1 `test/lambda` suite (P0: subset; P1: full minus exclusions) | real-workload turnaround, suite wall-clock + per-script phases |
| C2 synthetic L1 scripts at 1k / 5k / 20k lines (`gen_bench.py`, deterministic, under `./temp/`) | compile-vs-walk scaling on run-once straight-line code |
| C3 REPL session at history 10 / 100 / 1000 lines (`repl_bench.py` driving stdin) | per-line latency vs history length (the O(n²) claim, quantified) |
| C4 `lambda.exe validate` on a schema fixture | baseline JIT cost carried by the validator today (P3 context) |
| C5 hot-loop kernel (tail-recursive + `for`-heavy scripts, P1) | the honest worst case; context for the P2 promotion threshold |

### 6.2 Report tables — **[measured 2026-08-15]**

Release build (`make release`, rule 10), macOS/arm64, one warm-up + five measured runs, median. Both tiers on the same binary, selected by `LAMBDA_TIER`. Peak RSS is the child process's own `ru_maxrss`, reported by the CLI under `LAMBDA_RSS_REPORT=1` — `RUSAGE_CHILDREN` is a running maximum over every reaped child and cannot attribute a peak to one run. Reproduce with `make interp-bench`; raw data in `temp/interp_bench.tsv` and `temp/interp_repl_bench.tsv`.

**What the `jit` column actually is.** The shipped default path is not uniformly native codegen. `transpile-mir.cpp` flips `MIR_link` from `MIR_set_gen_interface` to `MIR_set_interp_interface` once a module exceeds `MIR_LARGE_MODULE_INSN_THRESHOLD` (100 000 MIR instructions), independent of optimization level — the size-triggered escape hatch AI19 retires at P5. Two of the C2 rows cross that line, so their baseline is *full lowering + link, then MIR-interp execution*, not generated machine code. Every row below records which mode ran, and the C2 rows additionally carry a **forced-native** column measured with `LAMBDA_JS_LARGE_INTERP=0`, which is the `--jit-all` steady-state baseline. The 1 000-line row is the method's own control: it sits below the threshold, so its default and forced-native cells agree to within noise.

**C1 / C3 / C4 — baseline is native codegen throughout** (0 of the 81 subset scripts and none of the REPL histories tripped the hatch)

| # | Corpus | scripts | jit total ms | interp total ms | speed-up | jit peak RSS MB | interp peak RSS MB | RSS ratio | fallback |
|---|---|---|---|---|---|---|---|---|---|
| C1 | `test/lambda` P0 subset | 81 | 1439.5 | 850.0 | **1.69×** | 58.6 | 30.0 | 0.51× | 0 |
| C4 | `validate` on a schema fixture | 1 | 9.9 | 8.8 | 1.13× | 11.5 | 11.5 | 1.00× | 0 |

**C2 — synthetic scale, all three execution modes** `[re-measured 2026-08-15]`

| lines | MIR insns | default `jit` mode | default `jit` | forced native (`--jit-all` equivalent) | interp | vs default | vs native |
|---|---|---|---|---|---|---|---|
| 1 000 | ~29 k | native codegen | 304.5 ms / 229.8 MB | 301.9 ms / 229.1 MB | **25.5 ms / 29.4 MB** | 11.9× | **11.9×** |
| 5 000 | 145 045 | **MIR-interp** (over threshold) | 1 433.4 ms / 2 133.6 MB | 2 929.6 ms / 3 924.6 MB | **215.5 ms / 42.7 MB** | 6.7× | **13.6×** |
| 20 000 | 580 045 | **MIR-interp** (over threshold) | 32 618.9 ms / 5 376.4 MB | 63 511.3 ms / 4 228.8 MB † | **2 228.1 ms / 92.4 MB** | 14.6× | **28.5×** |

† Single run, not a median of five: at ~64 s per invocation the standard protocol exceeds a practical budget. Treat it as an order-of-magnitude figure, not a precise one. Its RSS being *below* the default row is not a typo — disabling the escape hatch also drops large modules to opt=0 (`transpile-mir.cpp`, "the expensive optimizer passes do not pay back on cold generated code"), so this cell measures unoptimized codegen, and MIR-interp holds the full IR resident throughout. The two RSS figures are therefore not a clean codegen-versus-no-codegen contrast; only the T0 column is directly comparable across all three.

**C3 — REPL per-line latency vs history length**

| history lines | jit ms | interp ms | ratio |
|---|---|---|---|
| 10 | 10.5 | 8.9 | 1.18× |
| 100 | 15.0 | 9.3 | 1.62× |
| 1 000 | 185.7 | 19.7 | **9.42×** |

Per-phase columns (`parse | ast | plan | mir | gen | exec`) are recorded per script by `LAMBDA_PROFILE=1` into `temp/phase_profile.txt` (TSV format 2, extended with `plan`, `interp_exec` and `peak_rss_mb` for this arc). The aggregate rows above are what the gate reads; the per-script phase file is the drill-down.

**Coverage accounting (no silent caps, R4)**

| | count |
|---|---|
| corpus scripts with goldens | 279 |
| interpreted end to end, output identical to JIT | **81** (`test/lambda/interp_p0_subset.txt`) |
| routed to the JIT fallback, counted and logged | **198** (`test/lambda/interp_excluded.txt`) |
| divergent under either tier | **0** |

Top fallback reasons: `FOR_EXPR` 32, does-not-compile-on-either-tier 23, `IMPORT` 22, N-D/sized `ARRAY` literal 21, `ELEMENT` 12, `TYPE_STAM` 11, annotated/`^err` `ASSIGN` 10, `PIPE` 9, `TYPE` 8, `RAISE_STAM` 8. Every one is P1 coverage work (§3), not a defect.

### 6.3 Gate reading — **[reviewed 2026-08-15]**

Required findings, against §6.3's criteria:

- **C2 and C3 must show net total-time wins for interp.** They do, decisively, and against *both* baselines: 6.7–14.6× versus the shipped default path, 11.9–28.5× versus forced native codegen, and 9.4× at REPL history 1 000. The gap widens with program size exactly as the compile-dominance argument predicts. The design's premise holds; P2 may proceed on this evidence.
- **C1 must show the compile share eliminated and its net direction documented.** Real-workload turnaround is **1.69× faster** on the 81-script subset, against a native-codegen baseline throughout. This is the conservative number: these scripts are small and helper-dominated, so the compile share is a smaller slice than in C2, and the walker still wins outright.
- **RSS deltas must separate AST/plan retention from MIR/code-page savings.** The AST + `ast_index` retention cost (AIO4) is visible as C1's floor — interp holds 30.0 MB where the JIT holds 58.6 MB, i.e. keeping the indexed AST alive is *cheaper* than the MIR module it replaces. On C2 the separation is stark and the mode column sharpens it: at 5 000 lines the MIR *IR alone* costs 2.13 GB (the default path never generated code), and adding codegen takes it to 3.92 GB, while T0 stays at 42.7 MB. At 20 000 lines the IR alone is 5.38 GB against T0's 92.4 MB — a **58× reduction**, and the same pressure `count_lambda_mir_volume` and the `mir_policy.hpp` thresholds exist to manage.
- **A caveat on the C2 default-path rows.** Because the escape hatch fired at 5 000 and 20 000 lines, the "default `jit`" speed-ups there (6.7× and 14.6×) compare T0 against the *cheaper* of the two JIT modes — one that had already skipped codegen. They understate the win against `--jit-all`: the forced-native column is roughly double at 5 000 lines and about twice again at 20 000 (28.5×). This was not noticed in the first pass of this report; the mode column exists so the next reader does not have to re-derive it, and `test/interp/run_bench.py` now reads the mode back out of the run log and prints it in every row.
- **C4** is measured but uninformative at P0 by construction: `validate` does not route its schema through the tier switch yet, so both tiers pay the same cost. De-JIT-ing the validator is `EvalMode::PREDICATE` work in P3 (AI17); this row is the before-baseline for that change.
- **C5 (hot-loop kernel)** is deliberately not in this table. §6.1 scopes it to P1, and the constructs that make an honest hot kernel — `for` comprehensions and tail recursion — are on the P0 exclusion list, so any number measured now would describe the JIT fallback rather than the walker. It is a P1 gate deliverable.

Against the JS-side priors: `MIR_link` being 50–82% of compile and short scripts never amortizing codegen both reproduce on the Lambda side. The Lambda numbers are stronger than the JS analogs (which compared JIT against MIR-interp, a cheaper-dispatch interpreter that still pays full lowering) because T0 skips MIR construction entirely.

## 7. Restrictions

- No changes to MIR lowering, emission, `MirEmitter`, or budgets in P0/P1 — satellites are P2; MT7 fixtures untouched (D8.6.1).
- Boxed-only walker, forever (AI3); no representation/lane logic outside `MirEmitter` (D5.3.4).
- No conservative stack scanning in any form (rule 15); precise slots only.
- No `std::` containers, no printf-debugging, `./temp/` only, no vendor edits, no `parser.c`/Lua edits (rules 2–7, 16).
- Never edit existing `.ls` goldens to make interp pass — divergence is a T0 bug by definition (SI3); the only sanctioned divergence is fault timing (S7.11.4).
- `LAMBDA_TIER` unset/`jit` stays byte-identical to today through both phases.

## 8. Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | Scratch-depth undercount → unrooted Item across GC | debug assert on every push; generated deep-expression fuzz under forced GC; the cost table is per-kind-reviewed at each slice |
| R2 | DAG double-evaluation via shared declaration nodes | structural-edge-only descent rule + walker code review checklist; `ast_index` parent info available for a debug validator |
| R3 | Helper-selection drift vs lowering (wrong helper for op/type) | shared table extraction where possible (rule 13); differential gate catches the rest; cross-reference comments at every mapping |
| R4 | Fallback masks missing coverage (suite "green" but half-JIT) | fallback counter pinned to 0 on gated corpora; counts printed and reported |
| R5 | Interp-mode module/L1-cache interactions (NULL `jit_context`) | P1.5 explicit task + cache-path audit; `runtime_free_script` already tolerates partial state |
| R6 | Number-stack growth in deep interp recursion (wide scalars per frame) | per-frame number watermark restore (already the design); C5 corpus includes an int64-heavy recursion; side-stack exhaustion guard is the backstop |
| R7 | Plan-pass build-time cost erodes the turnaround win | measured (G1.3, ≤5% of ast phase); pass is a single walk with integer bookkeeping |
| R8 | Baseline tests relying on TCO depth crash only under interp | P1.4 self-tail-call iteration before the G1 differential runs |

## 9. Open items (tracked, not blocking P0 start)

All map to DO25 / the design doc's AIO ledger: `ast_index` retention cost (AIO4 — measured in §6, decision at P2), CST retention vs materialized spans (AIO5), general TCO parity (AIO1 — only the self-tail slice lands here), counter/promotion design (P2, AIO3/AIO6/AIO9/AIO11), emission-budget treatment for satellites (AIO2). Anything discovered during P0/P1 that changes a design decision goes through the design doc's ledger first (AI# addendum), then lands here.
