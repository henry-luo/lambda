# AST Interpreter — Implementation Plan, Phases P0–P1

**Date:** 2026-08-15 (rev 2 — **P0 landed**)
**Status:** **P0 complete and gated (2026-08-15)**; **P1 partially landed** — see §3.0 for the slice-by-slice state. The §6 measurement report is published and reviewed. Exit gate of the P0→P1 arc is that report plus the full-baseline differential + GC-stress gates (§7); P0's own gate G0 is recorded in §2.7.
**Design authority:** `doc/Lambda_Formal_Design.md` **D8.1.1v2** (T0 default, tiered execution), D5.1.1/D5.1.2 (side-stack frames), D5.3.2/D5.3.3 (MAY_GC + native rooting contract), D6.2.1/D6.2.3/D6.2.4 (function values, snapshot captures, traced env), D7.2.1/D7.2.2 (module slabs, init transaction), DI14; `doc/Lambda_Formal_Semantics.md` S3.1, S7.7.1/S7.7.2, S7.11.4, S9.1, S11.2.1, SI3.
**Working design:** `vibe/Lambda_Design_Ast_Interpreter.md` (AI1–AI22 confirmed; AIO1–AIO12 = DO25). This plan implements §11's P0 and P1 exactly; P2 (tiering/satellites), P3 (CONST/PREDICATE modes), P4 (REPL persistent env), P5 (default flip) are **out of scope here** and get their own plan revisions.
**Scope rule:** P0/P1 never touch MIR lowering, emission, or the default execution path — `LAMBDA_TIER` unset or `jit` must remain byte-identical to today (MT7/D8.6.1 untouched by construction).

---

## 0. Summary

P0 builds the skeleton: the frame-plan pass, side-stack interpreter frames, a walker for the L1 core subset, `LAMBDA_TIER=interp` wiring, and the first measurement report. P1 completes construct coverage to the full Lambda baseline, adds the error/fault channels, module support, and self-tail-call iteration, and re-publishes the report over the full corpus. The arc ends when: (g-a) `make test-lambda-baseline` is green under both tiers with the committed exclusion list at zero silent fallbacks, (g-b) the GC-stress/ASan run is clean, and (g-c) the turnaround/memory report in §6 is published with all required cells filled and reviewed.

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

Layout-compat constraints on struct edits: `Function::type_id` at offset 0 and `closure_field_count` at offset 2 are poked by generated code (`transpile-mir.cpp:20095–20135`) — new fields go at the **end** only; `NameEntry`/`FnAnalysis` are pool-calloc'd, so zero-init is the correct "no plan" state.

## 2. P0 — skeleton + first evidence

### P0.1 Core types and entry points

- [ ] `interp.hpp` with the structures below; every log line prefixed `interp:` / `frame-plan:` (rule 9); `lib/` types only (rule 3).

```cpp
enum class EvalMode : uint8_t { RUNTIME /* CONST, PREDICATE arrive P3 */ };
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

Coverage (the design's P0 subset, §4.9 families 1–3 plus construction): `AST_SCRIPT`, `CONTENT` (statement/body lists), `PRIMARY` (const-list resolution through `node->type` → `const_index` → owning `Script::const_list`), `IDENT` (slot/env/slab load via `entry->slot`), `UNARY`, `BINARY` (arith/compare/set ops through the boxed helpers; `and`/`or` short-circuit; truthiness by tag per S3.1), `IF_EXPR`, `LET_STAM`+`ASSIGN` declare lists (statically-checked lets only; deferred-check skip semantics land in P1.4), `CALL_EXPR` (uniform path: evaluate callee to a `Function` Item, root args in a span, enter `interp_call` / native invoke via `lambda_dynamic_call` — the static-callee fast path is a P2 option, AIO10), `FUNC`/`FUNC_EXPR` (closure creation: snapshot captures per D6.2.3 via `to_closure_interp` + `owned_item_slot_store`; `entry_abi = LAMBDA_INTERPRETED`, `ptr = NULL`, `def = node`), `MEMBER_EXPR`/`INDEX_EXPR` (read-only, `fn_member`/`fn_index` — null totality per S7.1.1 comes from the helpers), `ARRAY`/`LIST`/`MAP`/`KEY_EXPR` construction, `SYS_FUNC` (direct C call through `SysFuncInfo`), `RETURN_STAM` only as fn-implicit-last-expression (explicit `return` is `pn`, P1.4).

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

### 3.0.1 Why a mixed-tier import cone is refused

Both tiers keep module globals in the per-context slab (`EvalContext::module_states`), but they *number* it independently: the JIT assigns slots in `prepass_create_global_vars` during lowering, the frame-plan pass assigns its own. A module compiled by one tier and read by the other would therefore resolve the right slab and the wrong slot — a silent wrong-value read, not a crash. The pre-scan closes this by refusing an import whose target is not itself `interp_supported`, so a cone is interpretable whole or not at all.

This is visible in the corpus: `test/lambda/import.ls` falls back because its dependency `func.ls` needs `AST_NODE_TYPE` (P1.2). Of the 163 remaining `IMPORT` fallbacks, most are this transitive gate rather than anything missing in the module machinery itself — they should clear as P1.2/P1.3 land.

### 3.0 P1 progress — **partially landed 2026-08-15**

All five slices are in (three complete, two partial), verified over the **full baseline corpus** — every directory `test_lambda_gtest` discovers, functional and procedural: **653 scripts, 312 interpreted end to end with byte-identical output, 317 counted fallbacks, 0 divergences, 24 inconclusive**. GC stress over all 312 clean but for the pre-existing JIT `path.ls` rooting defect (the JIT differs stressed vs unstressed; T0 is stable both ways — AI18's executable-spec role). `test_interp_gtest` 284/284, default tier 720/720.

*Inconclusive* is its own verdict, not a silent absorption (R4): a script that exceeds the sweep's budget on both a parallel and a 3×-budget retry is named and excluded from the interpreted list rather than counted as either answer. The same confirm-before-accusing retry applies to a mismatch — every genuine T0 divergence found so far reproduces on a direct re-run, so a one-off under N-way load must not be reported as one.

Import-cone leverage was the dominant effect: `IMPORT` fell **203 → 9** once the handful of modules its cone depended on could interpret (`pdf/util.ls`, `graph/style.ls`, `graphviz/records.ls`, `chart/util.ls`) plus aliased imports opened. Each unblocking was a gate *removal* or a few lines mirroring an existing lowering, never new walker machinery:

| change | LOC | effect |
|---|---|---|
| `sysfunc_native_math_always_float` promoted to `sys_func_registry.h`; gate narrowed to the type-preserving family, whose result is re-narrowed by the call's static type (the same input `POST_PROCESS_UNBOX` uses) | ~20 | `SYS_FUNC` 42 → 12; unblocked `pdf/util.ls` (78 dependents) |
| `map()`/`map([k, v, …])` mirrored to `vmap_new`/`vmap_from_array`, as lowering emits | ~6 | unblocked `graphviz/records.ls` (52) |
| map-spread gate removed — `{*:base, k: v}` records the merge on the *shape entry*, so `eval_map`'s positional fill already hands `map_fill_items` what lowering does | −8 | unblocked `graph/style.ls` (60) |
| aliased-import gate removed — `push_qualified_name` entries carry the same `node` + `import` pair `plan_resolve_import` already matches | −4 | `IMPORT` 203 → 9 |
| declared-`float` binding coercion (S7.7.2); `int`/`bool` need none, the rest stay gated | ~10 | `ASSIGN` gate narrowed |

One real T0 defect surfaced, found only because the sweep compares `run` mode too: `run_main`'s scan could reach the same `pn main` through two root children (a top-level `pn` is linked both as a root statement and inside the content item list), and the `break` left only the inner loop — so the whole procedure ran twice.

> **Correction to the P0 gate record.** G0.1 and the first P1 verification were measured over `test/lambda` only (279 scripts), not the full baseline. Worse, every harness — the gtest subset, the GC-stress loop, and the sweep — invoked `lambda.exe <script>` directly, so **`run` mode was untested by construction** through all of P0 and most of P1. Widening the sweep to the whole corpus immediately found a `run`-mode defect (below). The subset list now records each script's invocation mode and the gtest honours it, plus a dedicated `ProceduralMainIsInvokedUnderRunMode` case so the path stays covered even if the subset's only procedural script ever leaves it.

| Slice | State | What landed |
|---|---|---|
| P1.4 procedural + errors | **partial** | `EvalSignal` plumbing on `InterpFrame` (payload in the reserved slot); `VAR_STAM`, `ASSIGN_STAM` to a named binding, `WHILE_STAM`/`DO_WHILE_STAM`, `BREAK`/`CONTINUE`, explicit `RETURN`; `RAISE_STAM`/`RAISE_EXPR`; `AstCallNode::propagate` (`f(...)^`); `INDEX_ASSIGN_STAM`/`MEMBER_ASSIGN_STAM` through `array_set_cow`/`map_set_cow` for a plain-binding root; `run`-mode `pn main()` invocation. **Not yet:** nested COW paths (`a.b.c = v`), declaration-boundary skip (S7.7.2), `HANDLER_EXPR`/`HANDLER_STAM`, `CURRENT_ERROR`, self-tail-call iteration |
| P1.1 comprehensions | **partial** | `FOR_EXPR`/`FOR_STAM` core: nested `AstLoopNode` chains, key/value and index binding, `key_filter`, `key_only`, the `let` clause, `where`, and the spreadable output stream. **Not yet:** `group`/`order`/`limit`/`offset` clauses and equi-joins (rejected by the pre-scan), `PIPE`, implicit contexts (`~`, `~#`, `last`) |
| P1.5 modules | **landed** | `IMPORT`/`PUB_STAM`; the import cone recorded on the T0 load path; post-order module init, each under its own `TRANSACTION_BARRIER` with `lambda_module_state_reset()` on abandonment (D7.2.2/S7.7.6); per-module slabs through `EvalContext::module_states`; cross-module resolution against the *declaring* Script via `NameEntry::import_owner`. **Deliberately excluded:** mixed-tier cones and cross-language (Jube) imports |
| P1.2 types | **partial** | Type expressions as values (`TYPE`, `BINARY_TYPE`, `UNARY_TYPE`, `CONTENT_TYPE`, `LIST_TYPE`, `ARRAY_TYPE`, `MAP_TYPE`, `ELMT_TYPE`, `FUNC_TYPE`), `TYPE_STAM` declarations, and type names used as values. Identity selection is shared with lowering through `lambda_type_node_singleton` (ast.hpp). **Not yet:** `MATCH_EXPR`/`MATCH_ARM`, string/symbol patterns, `CONSTRAINED_TYPE`, `OBJECT_TYPE` |
| P1.3 | not started | elements, paths & queries |

Seven defects the differential caught while landing these, all fixed:

1. **`and`/`or` short-circuited on an error operand.** An error is *falsy* (`is_truthy` returns `BOOL_FALSE` for `LMD_TYPE_ERROR`), which is precisely what makes `int("x") or 7` yield `7`. Returning early on an error broke containment. The walker now short-circuits only where `fn_and`/`fn_or` would return the left operand anyway and delegates the rest to the helper — the division of labour AI3 asks for.
2. **A braced `for` body's scope was unreachable from the AST.** `build_for_expr` created a per-iteration `NameScope` and dropped the reference; `AstListNode::vars` exists for exactly this and `build_content` never filled it, so the frame plan could not see the body's bindings. Fixed at the source in `build_ast.cpp` — lowering resolves those names through its own hashmaps and reads that field nowhere, so the write is inert for the JIT.
3. **`lambda.exe run` never invoked the user's `pn main()`.** `interp_run_script` set `Context::run_main` but had no equivalent of the generated module entry's scan for a top-level `pn main` and its zero-argument call, so *every* `run`-mode script silently produced empty output. Only the full-corpus sweep could see this — the 279-script sweep never used `run`.
4. **A bare top-level definition was never bound.** A script whose top level is a single statement carries it directly under `AST_SCRIPT` rather than inside a content list, so `pn main(){…}` alone reached `eval_expr`, which built an anonymous closure and left the name unbound. Adding any second top-level item masked it. The top-level walk now hoists and binds definitions the way `build_content`'s pass 1 does.
5. **The import cone was invisible to T0.** `direct_imports` is populated inside `compile_script_as_mir_direct` — the function the interpreter skips — so the cone that drives module init order was always empty. The T0 load path now records it from the AST's import children before publishing the tier decision.
6. **`C_RET_RETITEM` sys funcs were called through the wrong prototype.** `input()`/`parse()` register the raw function returning a 16-byte `RetItem`, but the walker called them as `Item(*)(…)` — undefined behaviour that happens to read the right register on arm64 while **silently discarding `.err`**, so a failure would surface as a plausible value. Lowering avoids this with `_mir` wrappers; the walker now uses the correct prototype and the same `ri.err ? ItemError : ri.value` mapping.
7. **Content-block value classification diverged in two places.** A `for` reached as an expression always yields its stream (the discard decision belongs to the enclosing block, as `transpile_expr` defers it to `transpile_content`), and the block-expression shortcut must exclude a lone `for` so its spreadable result flattens through `list_push_spread` instead of nesting. `is_proc_flow_side_effect_node` was promoted to `ast.hpp` so both tiers make the call from one predicate.

### P1.1 Comprehensions, pipes, implicit contexts

- [ ] `FOR_EXPR`/`FOR_STAM` with the full clause set (`AstForNode`: `where/group/order/limit/limit_from_end/offset/then`; `AstLoopNode` joins incl. `join_keys`/`key_filter`/`key_only`/`optional`), `LOOP`, `ORDER_SPEC`, `GROUP_CLAUSE`, `JOIN_KEY`; accumulators/group tables through the same container/sort helpers as lowering (S6 total order lives in the helpers).
- [ ] `PIPE` (+ `where` filter form), pipe argument injection.
- [ ] Implicit contexts as slot-backed stacks in `InterpState` (`~`, `~#`, `last`; innermost-wins per S10.1.3), mirroring the transpiler's build-time context (`last_index_object` behavior); `CURRENT_ITEM`, `CURRENT_INDEX`, `LAST_INDEX` read them.

### P1.2 Match, types, patterns

- [ ] `MATCH_EXPR`/`MATCH_ARM`: scrutinee evaluated exactly once (S11.2.1); literal arms via `fn_eq`, type arms via `fn_is`, constrained arms base-only (current shipped behavior — S11.4.6 unchanged; direct predicate eval is P3/AI17).
- [ ] Type-expression nodes as values (`TYPE`, `*_TYPE`, `TYPE_STAM`, `CONSTRAINED_TYPE`, `OBJECT_TYPE`) — resolve through the build-time `Type*` graph exactly as lowering does; no interpreter-side type construction.
- [ ] String/symbol patterns (`STRING_PATTERN`, `SYMBOL_PATTERN`, `PATTERN_SEQ`, `PATTERN_CHAR_CLASS`, `PATTERN_ISLAND`) via the pattern helpers over prepass-compiled pattern constants (in interp-only mode these come from the const pool at build time; the satellite story is AIO12/P2).

### P1.3 Documents, paths, queries

- [ ] `ELEMENT`/`CONTENT_TYPE`/`ELMT_TYPE`, `OBJECT_LITERAL`, `DECOMPOSE`, `SPREAD`, `NAMED_ARG`.
- [ ] `PATH_EXPR`/`PATH_INDEX_EXPR`/`PARENT_EXPR`/`QUERY_EXPR` via the `path_*`/query helpers.

### P1.4 Procedural + error/fault channels

- [ ] `EvalSignal` completion for `VAR_STAM`, `ASSIGN_STAM`, `INDEX_ASSIGN_STAM`/`MEMBER_ASSIGN_STAM` (mutation through `cow_prepare_write`-family helpers — S9.1.2 sharing unobservable; out-of-bounds writes raise per SI11), `WHILE_STAM`/`DO_WHILE_STAM`, `BREAK_STAM`/`CONTINUE_STAM`, explicit `RETURN_STAM` (`pn`-only set per S12.1.2).
- [ ] Error-as-value check-placement parity: insert tag checks exactly where lowering emits `emit_return_if_item_error`/`emit_jump_if_item_error` (that placement is the spec); `RAISE_STAM`/`RAISE_EXPR`; `f(...)^` propagate flag suppresses the check; `^`/`is error` inline; `CURRENT_ERROR` in handler scopes.
- [ ] Declaration-boundary skip (S7.7.2): failed deferred checks propagate `ERROR_SKIP` to the frame owning the declaring block; the block yields the error; the binding is never established. This retro-covers the annotated-let cases P0 excluded.
- [ ] `HANDLER_EXPR`/`HANDLER_STAM` (`^{ }`): arm `LOCAL_FAULT` recovery frames via `lambda_recovery_frame_begin_for` + native `sigsetjmp` (interp C code arms directly — no imported-primitive dance needed); `PIPE_FILE_STAM`.
- [ ] **Self-tail-call iteration** (AIO1's committed v1 slice): the frame-plan pass marks tail-position self-calls; the walker rebinds param slots and loops instead of recursing, with the same `LAMBDA_TCO_MAX_ITERATIONS` cap as the JIT for parity. Required for baseline parity — deep tail-recursive tests must not overflow only under interp. General TCO stays open (AIO1).

### P1.5 Modules

- [ ] `IMPORT`/`PUB_STAM`; import cone loaded through the tier-aware load path (dependencies also build-only in interp mode); module init interpreted in post-order, each under a `TRANSACTION_BARRIER` recovery frame with `lambda_module_state_reset()` on abandonment (mirroring `run_script_mir:25697` — D7.2.2/S7.7.6).
- [ ] Module slabs: per-module persistent-rooted Item array sized by the frame-plan pass, hung off the per-context module-state machinery (`EvalContext::module_states` / `module_state_id`) so P2 satellites can import the same storage without rework (AI6/AIO12); cross-module identifier resolution honors `entry->import` — const/type lookups go against the *declaring* Script.
- [ ] Built-in `math`/`io` imports need no work (they resolve to `SYS_FUNC` nodes at build time); Jube static-module imports: verify call-site resolution in interp mode, fall back if any lowering-time dependency surfaces (documented, counted).
- [ ] Retained-module (L1 cache) interaction: interp-mode Scripts carry no `jit_context`; verify cache retain/invalidate paths tolerate NULL JIT state.

### P1.6 Exclusions wired explicitly

- [ ] Scripts with `START`, generator/async definitions (`FnAnalysis` `may_await`/`is_generator`/`needs_task_context`), or `VIEW`/`STATE_ENTRY`/`EVENT_HANDLER` fall back to whole-module JIT — per AI11/AI12 these bypass T0 until P2's first-call satellite compile exists. The list of affected baseline scripts is committed as `test/lambda/interp_excluded.txt`; the differential gate runs them in jit mode under the matrix and their count appears in the report (no silent caps).

### P1 gate (G1)

| Check | Criterion |
|---|---|
| G1.1 full differential | `make test-lambda-baseline` green under `LAMBDA_TIER=jit` **and** `LAMBDA_TIER=interp` (goldens are the shared oracle — SI3/D3.3.1's third leg); interp fallback count = 0 outside `interp_excluded.txt` |
| G1.2 GC stress | Full interp baseline clean under stress knobs + ASan build; no `AutoAssertNoGC` aborts; forced-GC run confirms every helper call is treated as a safepoint |
| G1.3 non-regression | Default mode still byte-identical (no lowering diffs); `make test` green; plan-pass cost ≤ 5% of the `ast` phase on the suite (reported) |
| G1.4 **measurement report** | §6 re-published over the full corpus; every corpus item where interp mode is net-slower end-to-end is explained (exec-dominated) with its projected P2 outcome |

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
