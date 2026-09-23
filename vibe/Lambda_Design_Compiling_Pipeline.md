# Lambda Compile Pipeline — stage map, build-time budget, and implementation

> **Status**: **IMPLEMENTED (2026-09-22).** This is an implementation of the
> existing pipeline contract (**D8.2.4**, **D8.2.5v2**) under the shipped
> **AUTO** tier policy (**D8.1.1v13**); it introduces no semantic or design
> ruling.
> **Role**: the design home for the *cost* of the Lambda compile pipeline from
> source to a runnable unit — which stages exist, what each walks, where the
> time goes, and how the front end is brought to a linear budget. The
> *structure* of the pipeline stays owned by `Lambda_Design_Unified_AST.md`
> (U27–U32 → D8.2.4–D8.2.6) and the tier model by
> `Lambda_Design_Ast_Interpreter.md` (AI1–AI22 → D8.1.1).
> **Scope**: parse → build → bind → validate → index → (plan | const-fold →
> MIR plan/lower/finalize/link). Out of scope: execution speed of T0 or of
> generated code, the MIR cache tiers (D8.5), and the parser's grammar.
> **Formal anchors**: D8.1.1v13 (AUTO tier, T0 baseline), D8.1.2v3 (C parser
> is the shipped front end), D8.2.4 (one indexed compilation unit),
> D8.2.5v2 (typed pass manager; facts live on the AST), D8.5.1v7 (L1 script
> cache, AST-template prebuild), D8.6.1 (MT7 emission ratchet),
> D8.6.4v2 (fail-closed consolidation gates).
> **Ledger**: extends the compiling area's **LC#** series
> (`Lambda_Design_Compiling.md`, LC1–LC2): decisions **LC3.1–LC3.8**, open
> issues **LCO1–LCO6**. No new series.
> **Companion**: `doc/dev/lambda/LR_01_Compilation_Pipeline.md` (orchestration
> detail), `LR_02_Parsing_AST.md`, `LR_07_MIR_Transpiler_JIT.md`;
> measurement tool `utils/compile_phase_bench.sh`; synthetic repros under
> `temp/astscale/`.

---

## 1. The pipeline as it is

### 1.1 Stage map

`transpile_script` (`lambda/runtime/runner.cpp`) drives a
`CompilerPassManager` (`compiler_timing.hpp`) whose passes declare required
and produced facts (D8.2.5v2). In the AUTO/interp tiers the manager stops
after `index` and hands the unit to the frame planner; in the `jit` tier
`compile_script_as_mir_direct` (`transpile-mir.cpp`) appends the MIR passes
to the same manager.

```
source ──lex+parse──▶ reduction tape ──replay──▶ typed AST (+ provisional scopes)
                                                     │
                                                   bind   (clone scopes, rewrite edges, captures)
                                                     │
                                                 validate (colour, COW borrows, concurrency, …)
                                                     │
                                                   index  (flat node table, IDs, pointer hash)
                                                     │
                     AUTO / interp ◀─────────────────┴─────────────────▶ jit
                     scan + frame plan (T0)                 const-fold → mir-plan → mir-lower
                     satellites lowered later, per fn       → mir-finalize-load → mir-link-entry
```

| # | Pass (fact) | Owner | What it does | Tree walks |
|---|---|---|---|---|
| 1 | `parse` (PARSED) | `parser/lambda_lexer.c`, `parser/lambda_parser.c`, sink `direct_tape_reduce` in `build_ast.cpp` | Lex + recursive-descent/Pratt parse. The parser never builds nodes; it emits a **reduction tape** (§1.2). | source ×1 |
| 2 | `build` (AST) | `lambda_rd_build_reductions`, `direct_ast_reduce` | Replays the tape bottom-up into typed `AstNode`s and predeclares top-level `fn`/`pn` names from parser `FUNCTION_HEADER` reductions. Scope entry/exit, `push_name`, `lookup_name`, type inference and source-ordered validators run **inline** in the reducer. | tape ×1, plus required per-body analysis |
| 3 | `bind` (BOUND) | `lambda_ast_rebind_direct_scope_graph` | Clones every `NameScope`/`NameEntry` reachable from the tree into fresh pool objects, rewrites AST edges by bind epoch, remaps entry links and existing captures, and collects the one function list. | ×1 full |
| 4 | `validate` (VALIDATED) | `lambda_ast_finalize_script` | Proc-method-as-value reject walk; call-colour walk (per function); enforcing-call and cross-frame checks; COW and concurrency consume bind's function list. One concurrency body visitor gathers facts before its call-graph fixed point. | bounded full + per fn |
| 5 | `index` (INDEXED) | bind and `ast_index_compiler_pass` (`ast-core.cpp`) | Allocation reserves dense node IDs. Bind publishes graph columns for JIT; AUTO/interp defer JIT-only graph columns until an index consumer needs them (D8.2.4). | no T0 walk; one deferred JIT walk when needed |
| 6 | plan (AUTO/interp) | `interp_scan_supported`, `interp_plan_script` (`interp_plan.cpp`) | Kind-support scan; frame-slot assignment; tail-call marks. Reject → whole-module `jit` fallback. | ×2 full |
| 7 | `const-fold` (ANALYZED, jit) | `lambda_const_materialize_script`, `interp_const_fold_script` | Pools literal containers, folds constants over the **index**, not the tree. | index ×2 |
| 8 | `mir-plan` (PLANNED) | `transpile_mir_ast_begin` | Pattern prepass, global-var BSS, **call-site collection: up to 6 rounds** of a whole-tree walk each running `infer_param_types_batched` per callee, then `prepass_forward_declare` with `infer_return_type` (itself a body walk) called 1–3× per function. | ≤7 full |
| 9 | `mir-lower` (MIR_LOWERED) | `transpile_mir_ast_lower` → `transpile_func_def` | Per function: parameter/return inference again, variant analysis, then demand-driven codegen with ~20 ad-hoc body or loop-body scans (`walk_lambda_ast` sites in `transpile-mir.cpp`). | per fn, per loop |
| 10 | `mir-finalize-load`, `mir-link-entry` | `transpile_mir_ast_finalize`, `MIR_link` | `MIR_finish_func/module`; **GC-root write-back liveness** per function (`em_finalize_semantic_root_write_back`, `mir_emitter_shared.hpp`); MIR link = eager native codegen. | MIR insns |

Three structural facts matter for cost:

- **Facts have one owner where possible.** Captures are analysed at build
  (`FUNCTION_END`) and remapped during bind; COW and concurrency reuse the
  bind function list. Context-invariant MIR defect-origin inference is
  epoch-cached for a call-site round; return/defer inference remains local to
  its active function analysis.
- **T0 does not materialize JIT-only index columns.** It retains stable IDs
  but reads the AST directly; an on-demand JIT consumer materializes the
  complete graph.
- **Profile phases record own time.** Import compilation and prebuild-worker
  waits are subtracted from the importer so its row represents its own work.

### 1.2 The reduction tape

`LambdaReductionTape` (`build_ast.cpp`) is a flat array of
`LambdaReductionRecord`: reduction kind/form, span, a copied child-index
array and copied name tokens. Children are one-based indices of earlier
records, so the tape is a post-order serialization of the parse tree. The
build pass replays it forward with a `values[]` array of node pointers. The
split keeps the parser a pure recognizer with no `Transpiler` knowledge and
lets a comparison sink verify the C parser against the reference grammar
(D8.1.2v3). Record payloads come from one tape-owned arena, so destruction is
one arena release rather than per-record frees.

### 1.3 Where the time goes (measured)

All numbers: debug build, `--no-log --dry-run`, `LAMBDA_PROFILE=1`, min of 3.
The debug build only adds `-g`; logging is excluded because `log_debug`
alone triples wall time on the largest script (0.75 s vs 0.25 s).

**The largest script in tree** — `test/lambda/editor/oracle_poc.ls`
(225 KB, 3812 lines, 1950 module-level `let`s, 7 functions, 6 imports):

| Phase | interp tier | jit tier |
|---|---|---|
| parse | 7.5 ms | 6.7 ms |
| build | ≈150 ms | (731 ms incl. nested import compiles) |
| bind | 4.6 ms | |
| validate + index | ≈7 ms | |
| plan | 1.1 ms | — |
| MIR lower + finalize | — | 1715 ms |
| MIR link (codegen) | — | ≈450 ms of the above sample |

The dozen post-build walks (bind, validate, index, plan) cost ≈13 ms
together. **Build is ≈95% of the front end, and build is not walking a
tree: it is doing linear name lookups.** Synthetic modules of N chained
top-level `let`s (`temp/astscale/lets_N.ls`) show parse linear and build
quadratic:

| N lets | parse | build+bind+validate+index |
|---|---|---|
| 1000 | 1.2 ms | 7.5 ms |
| 2000 | 2.4 ms | 32.8 ms |
| 4000 | 4.7 ms | 171.6 ms |
| 8000 | 10.0 ms | 745.9 ms |

Sampling the 8000 case (`sample`, 1 ms) puts 95% of build self-time in
`str_eq`, `strview_eq`, `lookup_name`, `push_name_with_spelling` and
`memcmp`. Both `lookup_name` (`build_ast.cpp`, scope-chain walk with a
per-entry `strview_eq`) and the duplicate check in `push_name_with_spelling`
(`lookup_name_in_current_scope`) walk the `NameScope` singly-linked list.
N top-level functions (`fns_N.ls`) show the same curve: 250→3.8 ms,
500→10.3, 1000→33.1, 2000→117.

**The JIT side has a worse pathology, off the T0 critical path.** In the
`oracle_poc` jit-tier sample, about half of the 2.3 s lowering is
`em_finalize_semantic_root_write_back` and `em_root_compute_block_live_out`:
the liveness fixed point resolves each branch target with
`em_root_find_label_block`, a linear scan over all labels, on every block on
every iteration (blocks × labels × iterations), and the bitsets are
re-swept in full each iteration. Chained module lets explode it:

| N lets, jit tier | MIR lowering |
|---|---|
| 500 | 2.5 s |
| 1000 | 73 s |
| 2000 | killed after 5 min |

Secondary linear scans in the same file: `find_var_by_binding` and
`find_global_var_by_binding` iterate whole hashmaps per lookup, and
`lambda_after_may_gc_call` iterates every variable scope after every
collecting call.

**Import accounting.** For `test/lambda/editor/input_intent_basic.ls`
(23 KB, 8 imports) the main-thread profile shows `ast = 49.5 ms`, of which
≈40 ms are nested compiles of `mod_input_intent` (35.8, itself including
`mod_commands` 26.3), `mod_step`, `mod_transaction`, `mod_decorations` and
`mod_history`; the script's own build is ≈9 ms. The L1 cache summary for
that run reads `ast_hits=10 ast_misses=18`: modules that the prebuild
workers had already built (`mod_step`, `mod_paste`, `mod_edit_registry`
appear once on a worker thread and again on the main thread) were rebuilt
on the critical path. Cause not yet found (LCO2).

**One parser anomaly** (out of scope, recorded): `complex_iot_report_html.ls`
parses at 1.0 ms/KB against 0.02 ms/KB for every other corpus script; it is
linear in size (×2 source → ×2 time), so a per-construct constant, not a
quadratic. LCO1.

---

## 2. Why tune the front end now

1. **AUTO is the shipped policy (D8.1.1v13): every unit runs in T0 first.**
   Nothing reaches MIR until a definition proves hot, and satellites lower
   one function at a time off the initial path. So the time from source to
   first result is exactly parse + build + bind + validate + index + plan,
   plus the same for every import that missed its template. MIR lowering
   dominates only the explicit `jit` tier and benchmark drivers.
2. **Initial page load is a front-end budget.** A Radiant page that embeds or
   imports Lambda scripts pays this budget before the first paint of any
   template it drives; the AST-interpreter design already argued the cold-
   start case against eager codegen (`Lambda_Design_Ast_Interpreter.md` §1,
   motivation 1). Having won that argument, the front end is now the whole
   cost, and a 20× parse-to-build ratio on a 225 KB script (§1.3) means the
   build, not the parser or the interpreter, decides the load time.
3. **The cost is superlinear in exactly the dimension that grows with real
   programs.** Module scope size is the number of top-level declarations:
   generated or data-heavy modules (`oracle_poc` is a generated oracle,
   `mod_commands` is a command table) are the ones that grow past a thousand.
   A quadratic build turns a linear-looking edit into a visible stall.
4. **Prebuild only helps if templates are reused.** D8.5.1v7 made module
   ASTs a cache producer; the measured 18 misses in one page-sized closure
   (§1.3) mean the critical path still rebuilds modules a worker already
   built, serially, inside the importer's build pass.
5. **The REPL and `validate`/`convert` paths inherit the same budget**
   (`LR_01` §6: whole-history replay per line), so every millisecond saved
   in build is saved per keystroke as well.

Target, used as the acceptance bar for §4: **build ≤ 3 × parse on every
corpus script, and build linear in module-scope size** (`lets_8000` ≤ 4 ×
`lets_2000`). The healthy rows of the corpus already sit at 3–4× (§4.1), so
the bar states that the pathological rows join them, not that the reducer
gets faster than the lexer.

---

## 3. Implemented tuning

Ordered by measured payoff on the T0 critical path. LC3.1–LC3.3 are the
initial-load items; LC3.4–LC3.6 are accounting and hygiene that make the
gain measurable and durable; LC3.7–LC3.8 are JIT-side and developer-loop
items recorded here because the same investigation found them.

### LC3.1 — Scope lookups are indexed; the entry list stays the order

**Decision.** `NameScope` gains a lazily built open-addressing index keyed
by the interned `String*` (pointer identity, since every binding name is
pooled by `name_pool_create_strview`). The index is built the first time a
scope exceeds a small threshold (8 entries) and maintained by `push_name`.
`lookup_name_in_current_scope` becomes one probe; `lookup_name` interns the
`StrView` once (`name_pool` lookup, itself hashed) and probes each scope up
the chain. The `first/last/next` linked list is kept unchanged: it is the
declaration order that bind, the frame planner and `module_build_lambda_
namespace` iterate, and the bind pass clones scopes by walking it.

**Why this shape.** A per-scope index keeps the scope object self-contained,
so `lambda_ast_rebind_direct_scope_graph`'s clone stays a struct copy plus a
rebuilt index, and the T0 planner's slot assignment (which reads the list)
is untouched. A single global symbol table keyed by (scope id, name) was
considered and rejected: scopes are pool-allocated and cloned in bind, so a
global table would need rekeying at the clone and a second lifetime to
manage. Small scopes (parameters, `for` clauses, blocks) stay list-only, so
the common case pays nothing.

**Design-time target.** `oracle_poc` build 150 → ~15 ms (the residual is the
reducer proper, which the corpus shows at ≈3× parse); `lets_8000` 746 →
~15 ms; the duplicate-definition check in `push_name_with_spelling` becomes
O(1). Constraint: the pointer-identity fast path already present in
`lookup_name_in_current_scope` must remain the *only* comparison — the
index never falls back to `memcmp`.

### LC3.2 — Import templates are consumed, never rebuilt, on the importer's thread

**Decision.** An IMPORT reduction that finds a prebuilt AST template
(D8.5.1v7) adopts it; a miss for a module that a prebuild worker owns
*waits* for that worker rather than rebuilding inline. Rebuilding is allowed
only when no worker owns the key (single-import scripts, `jit` tier, or
prebuild disabled). The cache summary must report zero `ast_misses` for
modules that also appear as worker rows in the phase profile.

**Why.** `input_intent_basic` spends ≈80% of its `ast` phase inside nested
compiles that a worker had already produced (§1.3). Until the miss cause is
known (LCO2) the design ruling is the invariant, not the fix.

### LC3.3 — One function list, one capture pass, fused post-build walks

**Decision.**
- Build assigns the index ID at node allocation (`alloc_ast_node_from_span`
  appends to `AstIndex`); parent/owner/binding columns are filled by the
  bind walk that already visits every edge. The separate `index` pass and its
  full walk disappear; `ast_index_append_profile` remains for REPL/fragment
  appends. This is the D8.2.4 index produced where the nodes are born.
- The bind pass's visited hashmap is replaced by an epoch mark on `AstNode`
  (one `uint32_t`, compared against `Transpiler::bind_epoch`), removing one
  hash insert per node.
- The function list is collected once, in bind, and handed to the COW and
  concurrency passes (`lambda_ast_decide_cow_borrows`,
  `analyze_lambda_concurrency`) instead of each re-walking the tree.
- Captures are computed once. Build-time `analyze_captures` at
  `FUNCTION_END` is retained because construction diagnostics
  (`ERR_IMMUTABLE_ASSIGNMENT`) depend on it; the bind-time recomputation is
  replaced by an entry-remap of the existing `FnCapture` list, since bind
  only changes *which* `NameEntry` object a capture points at, not the set.
- The five concurrency body scans per procedure (`scan_may_await_node`,
  `scan_task_context_node`, `count_await_point_node`,
  `assign_async_handler_state`, plus the validation walk) become one visitor
  carrying all bits; the two fixed-point loops share one worklist over the
  call graph instead of re-walking every body per round.
- In the AUTO/interp tiers, `const-fold` is not run and the index columns
  the JIT needs (`node_bindings`, `first_children`) are filled lazily on the
  first satellite lowering. T0 never reads them.

**Why.** Measured at ≈13 ms of 165 ms on `oracle_poc`, this is the smaller
half of the front end after LC3.1 lands, but it is the half that scales with
node count on *every* script, and it is where the D8.2.4 prohibition on new
private core-child walks is being eroded (the COW and concurrency passes each
grew their own collectors). Design-time target: front-end minus build ≈13 → ≈6
ms on `oracle_poc`; a constant-factor gain on the corpus.

### LC3.4 — Build-time inline analyses run once per body

**Decision.** The analyses that run at `FUNCTION_END` inside the reducer
(`analyze_captures`, `validate_cross_frame_binding_reads`,
`lambda_ast_note_param_cow_effects`, `lambda_ast_flush_place_copy_
diagnostics`) are audited against the top-level pass that runs the same
analysis again (`validate_top_level_cross_frame_binding_reads`,
`lambda_ast_decide_cow_borrows`). Each analysis has exactly one home: the
reducer when its diagnostic must be in source order, the validate pass
otherwise. Nested functions are visited by their own `FUNCTION_END` only;
an outer body's analysis does not descend into inner bodies
(`walk_lambda_ast(..., descend_functions=false)` is the rule, and the two
`descend=true` sites in `lambda_ast_note_fixed_array_lengths` and
`direct_bind_collect_function` are justified in a comment or fixed).

### LC3.5 — Compile-time profile records own time, and the breakdown is reachable

**Decision.** `PhaseProfile` and `LambdaCompilerTiming` record a script's
**own** phase time: a nested `load_script` for an import subtracts its
elapsed time from the enclosing pass's accumulator (a thread-local nesting
stack, as the recovery frames already keep). The `COMPILER_TIMING` printer
(`main.cpp`) is available on the ordinary script path behind
`LAMBDA_COMPILER_TIMING=1`, not only in `test-batch`. `build` gains two
sub-timers: reducer proper and inline analyses. `utils/compile_phase_bench.sh`
is the reference driver.

**Why.** Without this the corpus (§4) cannot distinguish an importer's cost
from its closure's, and the `jit` tier's `ast` column reads 731 ms for a
150 ms build (§1.3).

### LC3.6 — Tape records are arena-allocated; predeclaration comes from the parser

**Decision.** `LambdaReductionTape` allocates child-index arrays and name
tokens from one bump arena freed with the tape, replacing two `mem_alloc`
per record. The parser emits a `FUNCTION_HEADER` reduction as soon as it has
consumed `fn|pn name`, and the build pass predeclares from that record
instead of re-lexing the source in `direct_predeclare_top_level_functions`.

**Why.** Measured small (parse is 7 ms on the largest script) and last in
priority; recorded so the second lex is not mistaken for a necessary
design element. Constraint: the tape's fail-closed `default:` arm (§1.2)
must keep rejecting unknown forms.

### LC3.7 — JIT lowering: label→block map, worklist liveness, binding-keyed tables, cached inference

**Decision.**
- `em_finalize_semantic_root_write_back`: build a label→block array once
  (index labels by ordinal, or store the block index in the label insn's
  `data`), iterate blocks in reverse postorder with a worklist, and skip the
  pass for functions with no collecting call.
- `find_var_by_binding` / `find_global_var_by_binding`: a second map keyed by
  `NameEntry*`; `lambda_after_may_gc_call` walks only variables that own a
  typed-array cache (a per-frame list, not the scope maps).
- `function_body_may_originate_defect` is memoized in `FnAnalysis` and
  invalidated when a call-site round changes that function's resolved
  parameter types. `infer_return_type` and `function_return_may_defer` are
  contextual (not shared) because recursive native-lane analysis changes their
  active proof state.
- A predicted native lane is only an admission to a typed-array fast path;
  lowering must observe the produced `MirValue` carrier before it writes raw
  storage. A public-ABI Item produced in a satellite falls back to the checked
  setter (D2.4.1–D2.4.3, D8.2.6).
- Every Lambda MIR entry uses the shared native-stack probe against
  `Context::stack_limit` and shares its recovery-backed exit with side-stack
  exhaustion. The finalizer inserts the probe only after root coloring, so its
  scalar temporaries cannot perturb the body liveness plan. Native promotion
  therefore retains stack-fault containment; fault timing may differ from the
  interpreter (S7.11.1v2, S7.11.4).
- The process-wide MIR import catalog is initialized once before any P2 worker
  creates a private MIR context, so each request receives the fully published
  immutable resolver table (D8.2.6).
- A reusable AST map-literal `TypeMap` is an immutable construction recipe.
  Its null fields retain the dynamic Item lane, so a write in one evaluation
  cannot retag the packed layout used by another (S1.6, S9.1.2).
- Execution-pool map/element rebuilds and search-match shapes stay attached to
  their containers, with no compiler type-list index. Registering those shapes
  in a cached module left dangling entries after batch teardown and raced
  satellite shape inference (D4.2.3, D8.5.1v7).
- Satellite property-key indices are image-local until the receiving evaluator
  assigns their key-table suffix at publication. Generated entries read the
  sealed image relocation; workers never query evaluator TLS for a key base.
  Otherwise every worker sees base zero and later images silently reuse the
  first image's unrelated property names (D4.6.1v2, D8.5.1v7).
- Shared name/shape pool reference counts are atomic. Satellite cancellation
  waits for rejected-image cleanup before reporting the queue idle, so parent
  pools cannot disappear while workers still release or consult them
  (D4.2.4, D8.5.1v7).

**Why.** Not on the T0 critical path, but it is the whole `jit` tier budget
(§1.3) and it bounds satellite promotion latency in AUTO: a satellite for a
1000-binding `main` would pay the same liveness cost. The analysis-order
items are governed by the MT7 emission ratchet (D8.6.1): their emitted MIR
must be byte-identical before and after. The separate native stack guard is a
required S7.11.1v2 recovery edge, so its reviewed instruction growth is
recorded in the same-commit MT7 re-baseline.

### LC3.8 — Hot-path `log_debug` calls are removed from name lookup and registration

**Decision.** `lookup_name`, `push_name_with_spelling` and the reducer's
per-node arms emit no per-call `log_debug`. Diagnostics that matter stay at
`log_notice`/`log_error`.

**Why.** Debug builds write ≈67k log lines per compile of `oracle_poc`
and run 3× slower than `--no-log`; every developer iteration on the front
end pays it, and the timing numbers it produces are misleading.

### 3.9 Non-goals

- No bytecode, no second IR, no change to the tape/AST split (D8.1.1v13,
  AI22).
- No change to fact placement: inferred types stay on `AstNode.type`,
  declared annotations on the declaring node (D8.2.5v2). LC3.3's index
  columns are the D8.2.4 identities, not a fact table.
- No semantic effect: every item is verified by golden identity across
  tiers (§4.3), and the reducer's diagnostics keep their source order.

### 3.10 Priority and measured effect

| Item | Path | `oracle_poc` build+bind+validate+index | Corpus effect |
|---|---|---|---|
| LC3.1 scope index | T0 critical | ≈165 → ≈30 ms | rows with >100 top-level decls; linear scaling |
| LC3.2 template reuse | T0 critical | — | import-heavy pages: nested rebuilds removed |
| LC3.3 fused walks | T0 critical | ≈30 → ≈22 ms | constant factor, all rows |
| LC3.4 single-home analyses | T0 critical | small | correctness of the walk budget |
| LC3.5 accounting | measurement | — | makes §4 honest |
| LC3.6 tape arena | T0 critical | ≈1–2 ms | all rows, small |
| LC3.7 JIT liveness/tables | jit + satellites | — | `lets_1000` 73 s → sub-second; jit tier −40–50% |
| LC3.8 log gating | dev loop | — | debug-build compile 3× faster |

### 3.11 Implementation record (2026-09-22)

- `NameScope` now keeps declaration order in its existing list and adds a
  lazily grown pointer-identity table for construction-time lookup. Imported
  spellings are re-interned in the importing `name_pool` before they reach
  that table, preserving the D8.2.4 identity invariant across template
  owners.
- The direct parser writes a tape-owned `FUNCTION_HEADER` record; top-level
  predeclaration consumes those records, and tape payload memory is released
  with its single arena.
- Binding uses an epoch mark, remaps the reducer-built capture list in place,
  and returns one function list to validation. Allocation reserves node IDs;
  bind publishes graph columns, with the JIT-only columns deferred for T0 as
  permitted by D8.2.4 and D8.2.5v2.
- Cache templates keep a cache-only dependency list of immutable template
  owners. Execution keeps its distinct Runtime-local `direct_imports` shell
  graph, so both AST and MIR cache publication avoid retaining stale module
  state (D8.5.1v7).
- The call validator resolves source offsets to line numbers only when it
  emits a diagnostic. Successful calls previously scanned the source prefix
  solely to prepare an unused line number, producing a second quadratic
  build cost after scope indexing; this does not alter diagnostics or their
  source order (D8.2.5v2).
- Release scaling, interleaving 12 runs per size, measured median
  `ast_build_us` of 1,135 for `fns_500` and 3,997 for `fns_2000` (3.52×),
  and 2,443 for `lets_2000` and 9,478 for `lets_8000` (3.88×). The focused
  cache suite, compiler-pass suite, and interp/JIT output parity checks are
  recorded with the implementation handoff.

---

## 4. Verification

### 4.1 Corpus and baseline

Twenty scripts, the largest compilable units under `test/lambda` and
`test/benchmark`, chosen to cover both regimes: import-free single modules
(benchmarks, reports, `type_pattern`) and import-heavy page-style scripts
(`editor/*`, `structurizr`). Three candidates were dropped because they no
longer compile (`wip/scientific_computing_research.ls`,
`wip/supply_chain_optimization.ls`: E209 duplicate definition;
`jetstream/crypto_rsa.ls`: E312). The list is the default corpus of
`utils/compile_phase_bench.sh`.

Baseline, interp tier, debug build, `--no-log --dry-run`, min of 3
(2026-09-22, commit `0ccf73a4a` + working tree). `ast` = build + bind +
validate + index. Rows marked † are historical nested totals; current
profiles subtract nested compile and prebuild-wait time from the importer's
own phase under LC3.5.

| Script | KB | top-level decls | parse ms | ast ms | plan ms | ast/parse |
|---|---|---|---|---|---|---|
| `test/lambda/editor/oracle_poc.ls` † | 220 | 1956 | 7.50 | 160.9 | 1.10 | 21× |
| `test/lambda/complex_iot_report_html.ls` | 47 | 19 | 57.85 | 3.0 | 0.16 | 0.05× (LCO1) |
| `test/lambda/editor/commands_basic.ls` † | 41 | 170 | 1.19 | 58.2 | 0.24 | 49× |
| `test/lambda/wip/complex_iot_report.ls` | 28 | 12 | 0.60 | 2.1 | 0.10 | 3.4× |
| `test/lambda/type_pattern.ls` | 27 | 149 | 0.65 | 2.1 | 0.10 | 3.2× |
| `test/lambda/editor/input_intent_basic.ls` † | 22 | 111 | 0.73 | 60.6 (own ≈9) | 0.13 | 83× |
| `test/lambda/wip/healthcare_analytics.ls` † | 22 | 6 | 0.49 | 1.8 | 0.08 | 3.6× |
| `test/lambda/ui/todo2.ls` | 21 | 1 | 2.10 | 1.6 | — (JIT fallback, 79.7 ms transpile; LCO3) | 0.7× |
| `test/lambda/graph/structurizr/reference/structurizr_json_adapter.ls` † | 17 | 53 | 0.65 | 90.9 | 0.11 | 140× |
| `test/lambda/editor/paste_basic.ls` † | 16 | 64 | 0.44 | 51.5 | 0.09 | 117× |
| `test/benchmark/awfy/json2.ls` | 41 | 55 | 0.67 | 2.6 | 0.09 | 3.9× |
| `test/benchmark/awfy/json.ls` | 40 | 48 | 0.63 | 2.4 | 0.09 | 3.8× |
| `test/benchmark/awfy/cd.ls` | 17 | 43 | 0.67 | 2.6 | 0.10 | 3.9× |
| `test/benchmark/awfy/havlak.ls` | 22 | 57 | 0.79 | 3.2 | 0.12 | 4.0× |
| `test/benchmark/awfy/deltablue.ls` | 26 | 49 | 1.11 | 4.2 | 0.14 | 3.8× |
| `test/benchmark/awfy/cd2_orig.ls` | 32 | 61 | 1.55 | 5.7 | 0.19 | 3.7× |
| `test/benchmark/awfy/deltablue2.ls` | 27 | 69 | 1.36 | 5.2 | 0.18 | 3.8× |
| `test/benchmark/awfy/havlak2.ls` | 25 | 73 | 1.01 | 4.2 | 0.16 | 4.1× |
| `test/benchmark/awfy/cd2.ls` | 18 | 53 | 0.83 | 3.2 | 0.12 | 3.8× |
| `test/benchmark/text/prettier_ast2.ls` | 17 | 49 | 0.67 | 3.0 | 0.12 | 4.5× |

Reading: import-free scripts sit at a steady **3–4× parse**; that ratio is
the reducer's natural cost and the bar LC3.1 must bring the pathological
rows down to. The two synthetic series (`temp/astscale/lets_N.ls`,
`fns_N.ls`, N = 250…8000) are the scaling witnesses and should be run with
the corpus.

### 4.2 Method

```bash
# baseline / after, interp tier (the T0 critical path), 3 reps, min
utils/compile_phase_bench.sh interp 3 > temp/compile_phase_after_interp.tsv

# jit tier for LC3.7
utils/compile_phase_bench.sh jit 3 > temp/compile_phase_after_jit.tsv

# scaling witnesses
for N in 1000 2000 4000 8000; do
  LAMBDA_COMPILER_TIMING=1 LAMBDA_TIER=interp ./lambda.exe temp/astscale/lets_$N.ls --no-log
done
```

Rules: same binary type before and after (both debug or both release —
`make release` is the number that ships, the debug build is the number a
developer sees); `--no-log` always; one benchmark process at a time; report
min of 3 per phase; keep both TSVs under `temp/` and quote them in the impl
record. LC3.5 profiles import-heavy rows on own time and retain their worker
rows separately in `temp/phase_profile.txt`.

### 4.3 Acceptance

1. **Budget**: `ast ≤ 3 × parse` on every corpus row except the two
   recorded anomalies (LCO1 parse constant, LCO3 JIT fallback), measured on
   own time after LC3.5; `oracle_poc` build+bind+validate+index ≤ 25 ms.
2. **Scaling**: `lets_8000` ≤ 4 × `lets_2000` and `fns_2000` ≤ 4 ×
   `fns_500` (linear, not quadratic).
3. **Identity**: `make test-lambda-baseline` 100% under `LAMBDA_TIER=interp`
   and `LAMBDA_TIER=jit` (SI3v2: tiers evaluate identically);
   `make test-radiant-baseline` 100%; the 20 corpus scripts produce
   byte-identical output before and after.
4. **Emission**: for LC3.7's analysis reorder, MT7 (D8.6.1) at 0% slack —
   the finalized MIR dump of every corpus script is identical before and
   after. The native stack guard is separately reviewed and re-baselined in
   that same zero-slack gate because it implements the S7.11.1v2 recovery
   edge rather than an analysis reorder.
5. **Templates**: for LC3.2, the L1 summary for `input_intent_basic` and
   `commands_basic` reports `ast_misses = 0` for modules that a worker row
   built, and no module appears twice in the phase profile.
6. **Dev loop**: for LC3.8, `oracle_poc` compile in the debug build with
   logging on is within 1.3× of `--no-log`.

---

## Appendix A — Walk inventory (anchors, 2026-09-22 tree)

| Site | Walks | Note |
|---|---|---|
| `direct_predeclare_top_level_functions` (`build_ast.cpp`) | tape ×1 | parser-owned `FUNCTION_HEADER` records; no second lex |
| `lambda_rd_build_reductions` | tape ×1 | `values[]` replay |
| `FUNCTION_END` arm of `direct_ast_reduce` | per fn: `analyze_captures` (params + body), `validate_cross_frame_binding_reads`, `lambda_ast_note_param_cow_effects` | inline analyses |
| `lambda_ast_rebind_direct_scope_graph` | full ×1 | epoch mark, capture-entry remap, and one returned function list |
| `lambda_ast_finalize_script` | `reject_proc_method_value` ×1, `colour_walk_visit` ×1 (+ per fn), per top-level item ×2 | |
| `lambda_ast_decide_cow_borrows` | returned function list; per-function facts only | no independent function collector |
| `analyze_lambda_concurrency` | returned function list; one combined body scan and shared call-graph worklist | consolidated per-procedure facts |
| `ast_index_compiler_pass` | no T0 full walk | allocation reserves IDs; bind publishes core graph columns; JIT-only columns are lazy |
| `interp_scan_supported`, `interp_plan_script` | full ×2 | T0 only |
| `lambda_const_materialize_script`, `interp_const_fold_script` | index ×2 | jit only |
| `prepass_collect_call_sites` | full × ≤6 rounds | `infer_param_types_batched` per callee per round |
| `prepass_forward_declare` | full ×1 | `infer_return_type` 1–3× per fn, each a body walk |
| `prepass_define_functions` → `transpile_func_def` | per fn | 21 `walk_lambda_ast` sites in `transpile-mir.cpp`, several per loop body |
| `em_finalize_semantic_root_write_back` | MIR insns, blocks × labels × iterations | §1.3 |

## Appendix B — Open issues

- **LCO1** `complex_iot_report_html.ls` parses at 50× the corpus rate per
  KB, linear in size. No backtracking found in `lambda_parser.c` by
  keyword search; needs a sample of a longer instance.
- **LCO2 (resolved)** Cached template dependency validation had normalized
  execution-local imports in place. Template-owner dependencies now live in
  `cache_direct_imports`, so prebuilt AST and MIR graphs do not reuse an
  earlier Runtime's module shells (D8.5.1v7).
- **LCO3** `ui/todo2.ls` rejects T0 (view template without body) and falls
  back to whole-module JIT (79.7 ms). Either T0 support or a satellite-only
  path for the rejected kind.
- **LCO4** `fns_1000.ls` lowered in 114 ms in the jit tier while `fns_500`
  took 259 ms; some threshold changes the path. Identify before LC3.7 is
  measured.
- **LCO5** In the jit tier, imports compile nested inside the importer's
  build pass on one thread with no prebuild; whether the AUTO-tier prebuild
  should also serve `jit` is a policy question for `Lambda_Design_Compiling.md`.
- **LCO6** `AstIndex::facts`/`AstNodeFacts` are still allocated per unit
  although D8.2.5v2 retired the fact table; retiring them belongs to
  `Lambda_Proposal_JS_Unify_P7.md` U-D but shortens the index pass here.

## Appendix C — Measurement notes

- Tooling: `LAMBDA_PROFILE=1` writes `temp/phase_profile.txt` per process
  (`runner.cpp` `profile_dump_to_file`); rows are appended in completion
  order, so nested import rows precede their importer. `LAMBDA_COMPILER_
  TIMING=1` also prints the stable schema-1 record for ordinary script paths
  (`main.cpp`), including parse/build/bind/validate/index and analysis time.
- Sampling: macOS `sample <pid> 2 1` launched right after the process, on a
  longer synthetic input; `-wait` attaches too late for a 150 ms compile.
  The compile runs on the large-stack thread, not the main thread.
- The `took` lines in `log.txt` (`print_elapsed_time`) are cumulative from
  the start of `transpile_script` and only exist with logging on.
- Machine load: other benchmark processes were running during the baseline;
  min-of-3 was used to damp it, and the relative ratios (not absolute ms)
  are the figures this document relies on.
