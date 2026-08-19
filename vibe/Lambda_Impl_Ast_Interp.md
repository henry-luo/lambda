# AST Interpreter — Implementation Plan, Phases P0–P5

**Date:** 2026-08-19 (rev 21 — **P1 declared-numeric binding and error-value slice**)
**Status:** **P0 complete and gated (2026-08-15)**; **P1 remains partial** (the committed partition awaits a clean post-slice full sweep). T0 now constructs inferred homogeneous compact `NumSized` literals (`i8[]`, `u8[]`, `f16[]`, `f32[]`, and peers) in the same `ArrayNum` carrier as MIR; direct one-argument sized-numeric/`u64` type calls and every admitted declared scalar binding use the same coercion helpers as MIR. Full-width `u64[]` remains pinned because its generic index-read boundary needs MIR's raw-lane contract. A fresh procedural-only differential sweep reports 108 exact matches, 34 counted fallbacks, and 0 mismatches/timeouts; the global partition still awaits its one clean full-corpus sweep. `LAMBDA_TIER=auto` now runs T0 and promotes eligible definitions to unique, Script-owned MIR satellites at the shared dynamic-call boundary. The verified slice covers self recursion, several satellites in one MIR context, T0 module-slab reads, dynamic calls to another Lambda function, deferred promotion after a T0 loop backedge, and pre-planned Lambda-import slab reads/calls. Captures, cross-language/type imports, and named-property/key-table users remain pinned to T0. **P3 is in progress:** non-object constrained `is` checks and constrained match arms run their `that` AST under pure, fuel-bounded `PREDICATE`; the JIT pass manager folds admitted pure literal scalar subtrees to immediate `Item` facts at safe generic-value boundaries. **P4 has a persistent-REPL vertical slice:** `interp` and `auto` retain one Script, append and execute only a completed fragment, grow module slots/root storage geometrically, and roll failed fragments back. Imports after session start remain rejected. Validator de-JIT and P5's default-tier flip remain unimplemented.
**Design authority:** `doc/Lambda_Formal_Design.md` **D8.1.1v2** (T0 default, tiered execution), D5.1.1/D5.1.2 (side-stack frames), D5.3.2/D5.3.3 (MAY_GC + native rooting contract), D6.2.1/D6.2.3/D6.2.4 (function values, snapshot captures, traced env), D7.2.1/D7.2.2 (module slabs, init transaction), DI14; `doc/Lambda_Formal_Semantics.md` S3.1, S7.7.1/S7.7.2, S7.11.4, S9.1, S11.2.1, SI3.
**Working design:** `vibe/Lambda_Design_Ast_Interpreter.md` (AI1–AI22 confirmed; AIO1–AIO12 = DO25). This revision adds a narrow P1 declared-numeric binding and error-value assignment slice alongside numeric type-call coercion and compact-NumSized array construction, and continues §11's P2, P3's PREDICATE plus immediate-scalar CONST vertical slices, and P4's persistent-REPL vertical slice; validator integration and P5 (default flip) remain subsequent gates.
**Scope rule:** `LAMBDA_TIER` unset or `jit` remains the existing eager whole-module pipeline (MT7/D8.6.1). `auto` is opt-in T0 plus P2 promotion; it must pin an unsupported function to T0, never recompile the full module as a substitute.

---

## 0. Summary

P0 builds the skeleton: the frame-plan pass, side-stack interpreter frames, a walker for the L1 core subset, `LAMBDA_TIER=interp` wiring, and the first measurement report. P1 completes construct coverage to the full Lambda baseline, adds the error/fault channels, module support, and self-tail-call iteration, and re-publishes the report over the full corpus. P2 adds opt-in tier-up without changing `jit`: the definition-site cell counts dynamic entries, emits one function plus its boxed wrapper into a satellite MIR module, then upgrades pre-existing `Function` values in place at the shared dispatcher (D8.1.1v2 §5.1–§5.3). P3 now has restricted `that` predicate evaluation and immediate-scalar CONST folding. P4 retains one interpreter Script for a REPL session, parses/builds/indexes only appended fragments, executes only those nodes, and makes a failed completed input atomic; validator de-JIT remains gated by its separate `Type*`/validator ownership work, followed by P5.

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

Both tiers keep module globals in the per-context slab (`EvalContext::module_states`), but they *number* it independently: the JIT assigns slots in `prepass_create_global_vars` during lowering, the frame-plan pass assigns its own. A module compiled by one tier and read by the other would therefore resolve the right slab and the wrong slot — a silent wrong-value read, not a crash. The pre-scan refuses an import whose target is not itself `interp_supported`, so a cone is interpretable whole or not at all. If a parent later falls back to MIR after a child was tentatively planned for T0, the loader now demotes that loaded child cone in post-order before linking the parent; MIR therefore never imports a T0 module with no generated symbol context.

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
| P1.4 procedural + errors | **partial** | `EvalSignal` plumbing on `InterpFrame` (payload in the reserved slot); `VAR_STAM`, `ASSIGN_STAM` to a named binding, `WHILE_STAM`/`DO_WHILE_STAM`, `BREAK`/`CONTINUE`, explicit `RETURN`; `RAISE_STAM`/`RAISE_EXPR`; `AstCallNode::propagate` (`f(...)^`); root and nested `INDEX_ASSIGN_STAM`/`MEMBER_ASSIGN_STAM` through COW helpers, including direct typed-map replacement validation and fixed-arity direct `var`-parameter write-back for a mutable binding; direct `push`/`splice` replacement publication; expression/statement handlers with `CURRENT_ERROR`; local native-fault recovery; self-tail-call iteration; variadic Lambda `varg()` scope; compact scalar declaration witnesses, canonical declared sized-numeric/`u64` binding stores, and direct sized-numeric/`u64` coercion calls; inferred compact `NumSized` array literals; and run-mode `pn main()`. **Not yet:** deferred declaration-boundary skip (S7.7.2), `PIPE_FILE_STAM`, N-D row writes, full-width `u64[]` literal/index lanes, and the remaining procedural AST families. |
| P1.1 comprehensions | **partial** | `FOR_EXPR`/`FOR_STAM` core: nested `AstLoopNode` chains, key/value and index binding, `key_filter`, `key_only`, the single-binding `let` clause, `where`, the spreadable output stream, `PIPE`/`where`, and slot-backed `~`/`~#` contexts. `LAST_INDEX` is admitted for an ordinary subscript and uses the innermost rooted owner (S10.1.3); expression-form unordered `limit`, `limit last`, and `offset` are full-stream post-selections. **Not yet:** `group`/`order`, window clauses on a procedural `FOR_STAM`, and equi-joins (rejected by the pre-scan). |
| P1.5 modules | **landed** | `IMPORT`/`PUB_STAM`; the import cone recorded on the T0 load path; post-order module init, each under its own `TRANSACTION_BARRIER` with `lambda_module_state_reset()` on abandonment (D7.2.2/S7.7.6); per-module slabs through `EvalContext::module_states`; cross-module resolution against the *declaring* Script via `NameEntry::import_owner`. **Deliberately excluded:** mixed-tier cones and cross-language (Jube) imports |
| P1.2 types | **partial** | Type expressions as values (`TYPE`, `BINARY_TYPE`, `UNARY_TYPE`, `CONTENT_TYPE`, `LIST_TYPE`, `ARRAY_TYPE`, `MAP_TYPE`, `ELMT_TYPE`, `FUNC_TYPE`), `TYPE_STAM` declarations, and type names used as values. Identity selection is shared with lowering through `lambda_type_node_singleton` (ast.hpp). **Not yet:** `MATCH_EXPR`/`MATCH_ARM`, string/symbol patterns, `CONSTRAINED_TYPE`, `OBJECT_TYPE` |
| P1.3 documents, paths, queries | **partial** | Elements, paths and queries already execute in the admitted subset. Positional/named `DECOMPOSE` is admitted; object-literal/type forms remain excluded. A named argument is admitted only for a statically resolved Lambda definition; system, dynamic and pipe-injected calls remain positional-only. |

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
- [x] Implicit contexts as slot-backed stacks in `InterpState` (`~`, `~#`, `last`; innermost-wins per S10.1.3), mirroring the transpiler's build-time context (`last_index_object` behavior); `CURRENT_ITEM`, `CURRENT_INDEX`, `LAST_INDEX` read them. Unordered expression-form `FOR` windows use the full stream before `offset`/first/last selection.

### P1.2 Match, types, patterns

- [ ] `MATCH_EXPR`/`MATCH_ARM`: scrutinee evaluated exactly once (S11.2.1); literal arms via `fn_eq`, type arms via `fn_is`, constrained arms run the P3 `PREDICATE` subset after their base check. Generic `fn_is` and validator policy remain base-only under S11.4.6.
- [ ] Type-expression nodes as values (`TYPE`, `*_TYPE`, `TYPE_STAM`, `CONSTRAINED_TYPE`, `OBJECT_TYPE`) — resolve through the build-time `Type*` graph exactly as lowering does; no interpreter-side type construction.
- [ ] String/symbol patterns (`STRING_PATTERN`, `SYMBOL_PATTERN`, `PATTERN_SEQ`, `PATTERN_CHAR_CLASS`, `PATTERN_ISLAND`) via the pattern helpers over prepass-compiled pattern constants (in interp-only mode these come from the const pool at build time; the satellite story is AIO12/P2).

### P1.3 Documents, paths, queries

- [ ] `ELEMENT`/`CONTENT_TYPE`/`ELMT_TYPE`, `OBJECT_LITERAL`, `SPREAD`. Object forms remain excluded.
- [x] Positional/named `DECOMPOSE`, including lexical list blocks. `NAMED_ARG` is complete only for a statically resolved direct Lambda call (rev 6), not positional system/dynamic/pipe calls.
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

### 3.1 P2 — opt-in satellite tier-up — **in progress 2026-08-19**

The implemented unit is one definition plus its generated `_b` wrapper. Its
`FnAnalysis::promotion` starts in `INTERP`, increments at the single dynamic
dispatcher, compiles synchronously at `LAMBDA_JIT_THRESHOLD` (default `3`),
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

Verified with `test/lambda/interp_auto_tier.ls` and threshold `3`:

| Tier | Result | Evidence |
|---|---|---|
| `interp` | `[3, 4, 5, 6, 10, 5, 6, 7, 4, 2]` | T0 only, zero fallback |
| `auto` | same | four satellite images: self-recursive `count_down`/`sum_down`, plus `bridge` reading module `offset` and dynamically invoking `shifted` |
| `jit` | same | existing eager whole-module control |

`test/lambda/interp_auto_import.ls` adds the T0-import boundary: its provider
exports `offset` and `shift`, while the importing `hot` function reads both.
Under `auto` with `LAMBDA_JIT_THRESHOLD=2`, the provider `shift` and importing
`hot` each publish one satellite, returning `[21, 22, 23, 24]` exactly under
`jit`, `interp`, `auto`, and forced-GC `auto`. The importer satellite reads
the provider's slab Function dynamically; it never resolves `mN._shift_b_*`.

Still pinned to T0: captures/nested definitions, cross-language or type-only
imports, and named-property or object/view code, because their environment or
key-table artifacts are not yet Script-scoped satellite inputs. `LAMBDA_JIT_BACKEDGE` now defaults to
`1024`: a T0 `while` or comprehension continuation increments the active
definition's counter, and crossing it permits compilation only on a later
dynamic entry — never through on-stack replacement. `interp_auto_tier.ls`
forces `LAMBDA_JIT_THRESHOLD=100 LAMBDA_JIT_BACKEDGE=2`; its first
`loop_count(4)` remains T0 and the second entry compiles the sole satellite.
Full-AST call-site analysis persistence and the complete P2 matrix remain
open. P3's validator de-JIT, P4's persistent REPL state, and P5's default flip
remain blocked on their stated design gates; `LAMBDA_TIER`
unset remains `jit`.

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
