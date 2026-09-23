# LambdaJS Compile Pipeline — stage map, alignment with Lambda, and tuning proposal

> **Status**: **IMPLEMENTED (2026-09-23).** LC4.1–LC4.12 are landed. The
> shared index now owns per-function free read/write facts, the cached
> profile-support bitmap, and direct-call evidence for both front ends. The
> 2026-09-22 measurements below remain the historical baseline; the
> implementation record is in §4.4. Companion to
> `Lambda_Design_Compile_Pipeline.md` (LC3.1–LC3.8, the Lambda front-end
> budget). Everything here is an implementation of the shared pipeline
> contract (**D8.2.4v2**, **D8.2.5v3**) and the revised default-backend
> clause of **D8.1.3v19**.
> **Role**: the design home for the *cost* of the LambdaJS compile pipeline
> and for the structural alignment of the two front ends: which stages exist,
> what each walks, where the time goes, which analyses the two languages
> duplicate, and which shared substrate replaces the duplicates. Pipeline
> *structure* stays owned by `Lambda_Design_Unified_AST.md` (U27–U32) and
> `Lambda_Design_JS_Unified.md`; the JS AST backend by
> `Lambda_Design_Ast_Interpreter.md` and **D8.1.3v19**.
> **Scope**: parse-build → bind → validate → index → analyze/plan → lower →
> finalize → prelink → link, in the default MIR lane and the AST/AUTO lane.
> Out of scope: execution speed of generated code, the MIR cache tiers
> (D8.5), the vendored MIR code generator (rule 16: never patched, only
> steered by policy).
> **Formal anchors**: D8.1.3v19 (JS front end, explicit boxed AST backend,
> AUTO default), D8.1.1v13 (Lambda AUTO tier), D8.2.2 (variance tiers),
> D8.2.4v2 (one indexed compilation unit; no new private core-child walks),
> D8.2.5v3 (typed pass manager; facts on the AST), D8.4.1v2 (no inline
> caches), D8.5.1v7 (L1 cache, AST-template prebuild), D8.6.1 (MT7 emission
> ratchet).
> **Ledger**: extends the compiling area's **LC#** series
> (`Lambda_Design_Compiling.md`): decisions **LC4.1–LC4.12**, open issues
> **LCO7–LCO12** (continuing LCO1–LCO6 of the Lambda doc). No new series.
> **Companion**: `doc/dev/js/JS_01_Compilation_Pipeline.md`,
> `JS_02_Parsing_AST.md`, `JS_04_MIR_Lowering.md`, `JS_15_Performance.md`;
> measurement tool `utils/js_compile_phase_bench.sh`.

---

## 1. The JS pipeline as it is

### 1.1 Stage map, side by side with Lambda

Both front ends run a `CompilerPassManager` (`compiler_timing.hpp`) with the
same fact bits. The JS manager lives in `js_transpiler_parse_c`
(`js_c_parser.cpp`) for the front end and in `transpile_js_mir_ast`
(`js_mir_module_batch_lowering.cpp`) for the MIR side.

| Stage | LambdaJS | Lambda (for comparison) | Shared today |
|---|---|---|---|
| parse-build (AST) | Reduction sink `js_c_reduce` allocates `JsAstNode`s directly; **no binding during construction**. Lexer/parser `lambda/js/parser/`. | Parser emits a reduction tape; replay builds nodes **and binds names inline**, with per-function analyses at `FUNCTION_END`. | `AstNode` header, core node kinds (D8.2.2), pool/name-pool |
| bind (BOUND) | `js_rebuild_direct_scope_graph` creates the scope graph, hoists declarations, resolves identifiers, and plans binding slots. | Lambda retains its language-specific scope rebuild. | `NameScope` owns the shared lazy pointer-identity lookup index and slot planner (`D8.2.4v2`). |
| validate (VALIDATED) | `js_check_early_errors` (`js_early_errors.cpp`): one walk with a strict/generator/async/label context. | `lambda_ast_finalize_script`: ≈9 full walks plus per-procedure fixed points (colour, COW borrows, concurrency). | — |
| index (INDEXED) | shared `ast_index_compiler_pass` with `js_profile` (`visit_ext_children`, `publish_ext_facts`). | same pass, `lambda_profile`. | `AstIndex` (D8.2.4) |
| AST lane | `js_interp_script_is_supported`; slots are planned at bind. `ast` is explicit; `auto` and an unset selector are AST-first with MIR fallback. AUTO promotes hot definitions to P2 satellites. | `interp_scan_supported` + `interp_plan_script`; AUTO is the default. | `FnPromotionCell`, satellite compile, `InputScriptCache`, `module_ast_prebuild` |
| analyze-plan (ANALYZED+PLANNED) | Collection, capture, environment-layout, inference, and forward-declare slices consume index ranges, reverse tables, and parent→children adjacency; each slice is separately timed. | language-specific planning consumes the common index where applicable. | `AstIndex` queries, `MirEmitter` context |
| lower (MIR_LOWERED) | `js_mir_lower`: literal-shape recipes are published during planning; array, call, capture, and suspension predicates consume ranges/reverse tables. | `prepass_define_functions` → `transpile_func_def`. | `MirEmitter` incl. `em_finalize_semantic_root_write_back` |
| finalize / prelink / link | `js_mir_finalize`, `js_mir_prelink`, runtime link → shared `mir_select_link_interface`. | Lambda MIR link pass uses the same policy. | `mir_policy.hpp` |

The following was the pre-LC4 structural inventory; the implemented state is
recorded in §4.4.

Three structural differences mattered for cost and for alignment:

- **JS binds after build; Lambda binds during build.** JS therefore has no
  scope clone, no rebind, and no duplicated capture pass. Lambda's bind
  exists to undo construction-time binding. The clean shape is the JS one.
- **JS consumes the index everywhere; Lambda barely consumes it.** JS
  analyses were index-driven (D8.2.4 as intended) but had no subtree or
  per-owner ranges, so every "facts about this function or this subtree"
  question scanned all N nodes. LC4.2 supplies the shared range/query layer.
- **JS defaults to MIR; Lambda defaults to AUTO.** For a page script the
  difference is the whole compile budget (§1.3, §2).

### 1.2 Walk inventory

| Site | Walks / scans | Note |
|---|---|---|
| `js_c_reduce` sink | source ×1 | nodes allocated in the sink |
| `js_rebuild_direct_scope_graph` | full ×1; `direct_predeclare_vars` prescan per function body (stops at nested functions) | linear overall |
| `js_check_early_errors` | full ×1 | |
| `ast_index_compiler_pass` | full ×1; **plus a loop over all functions seen so far per new node** to recover a span owner (`ast_index_visit`, `ast-core.cpp`) | O(N·F) worst case; shared with Lambda |
| `js_interp_script_is_supported` | full ×1 | AST/AUTO lane only |
| 1.1 module consts | `jm_collect_indexed_body_locals` per IIFE (index subtree visit with a stack malloc sized to the whole index) | |
| 1.5 `jm_analyze_captures` | `jm_collect_indexed_body_refs`: **index ×1 per function** | O(F·N) |
| 1.6 transitive captures | fixed point over functions | |
| 1.7–1.7d env layouts | `for fi` × `for ci` over functions | O(F²) |
| 1.75 `jm_infer_param_types` | `jm_infer_indexed`, `jm_infer_boxed_return_scalar_class`, `jm_prescan_float_widening` ×2: **index ×1 each per function** | O(F·N) ×4 |
| 1.76 `jm_callsite_propagate` | index ×1 | fine |
| lower: `jm_literal_shape_for_object` | linear scan of `literal_shape_plans` + `jm_literal_shape_has_static_field_use` **index ×1 per object literal**, whose loop body calls `jm_direct_call_literal_return` / `jm_direct_literal_argument_for_parameter`, each **another index scan** | O(L·N²) worst case; the measured hot spot |
| lower: `jm_is_array_literal_candidate` | index ×1 per array literal | |
| lower: `jm_can_suspend` / `jm_count_yields` / `jm_count_awaits` | `jm_count_indexed_suspensions`: index ×1 per query with a parent-chain hash walk per node; 36 call sites, generator/async bodies only | O(Q·N·depth) |
| link | `MIR_link` native codegen of every function | vendored; superlinear in function size |

### 1.3 Where the time goes (measured)

Debug build (`-g` only), `--no-log`, `JS_TRANSPILE_TIMING=1`, single run
unless noted, machine shared with other sessions' benchmarks (ratios are
the reliable figures). The timer's `parse_ms` covers parse-build, bind,
validate **and** index (§1.1); `ast_ms`/`early_ms` always read 0.

| Script | KB | fns | MIR insns | front end | MIR analyze+lower | link (codegen) | AST lane front end |
|---|---|---|---|---|---|---|---|
| Octane `richards.js` | 15 | 79 | 15.8k | 4.0 ms | 34 ms | 331 ms | 1.9 ms |
| Octane `deltablue.js` | 25 | 153 | 32.0k | 3.6 | 92 | 474 | 2.9 |
| Octane `raytrace.js` | 15 | 120 | 35.2k | 2.1 | 49 | 829 | 2.1 |
| Octane `crypto.js` | 47 | 281 | 170.8k | 8.1 | 470 | 3013 | 9.0 |
| Octane `earley-boyer.js` | 191 | 834 | 423.8k | 102 | 1800 | **57,678** | — |
| `test/js/alpine.min.js` | 45 | 1310 | 218.6k | 29 | **3746–7009** | 2007–2485 | 27–30 |
| `test/js/htmx.min.js` | 50 | | | 18 | 2440 | 1059 | 21 |
| `test/js/bootstrap.min.js` | 59 | | | 21 | 1268 | 5487 | 28 |
| `test/js/lib_moment.js` | 73 | | | 26 | 7824 | 37,049 | 34 |
| `test/js/lib_zod.js` | 68 | | | 28 | 17,061 | 15,179 | 26 |
| `test/js/hljs_highlight.js` | 122 | | | 34 | 14,122 | 1634 | 33 |
| `test/js/lib_yup.js` | 155 | | | 75 | 19,423 | 6670 | 52 |
| `test/editable-editors/build/prosemirror.js` | 467 | 2557 | 1,027k | 282 | **97,608** | 12,622 | — |
| `test/js/lib_lodash.js`, `lib_ramda.js`, `lib_rxjs.js`, `moment_src.js`, `ramda_src*.js`, Octane `box2d.js` | 65–228 | | | 21–126 | did not finish in 60 s | | 21–126 |

Three conclusions:

1. **The JS front end is already linear and cheap: 0.1–0.7 ms/KB**, with
   minified bundles at the high end. It is also exactly the AST-lane cost:
   `alpine.min.js` reaches its first result in ≈30 ms under
   `JS_EXECUTION_BACKEND=ast` and in 6–9.5 s under the default.
2. **MIR analysis+lowering is superlinear and dominated by whole-index
   scans.** Sampling `prosemirror.js` puts **87% of lowering self-time in
   `jm_literal_shape_for_object`** (`js_mir_expression_lowering.cpp`): a
   linear search of the plan list, then `jm_literal_shape_has_static_field_use`
   scanning every indexed node per object literal, with two more index scans
   inside that loop. The `alpine.min.js` sample shows the same frame at 60%
   of lowering. Per-KB lowering cost ranges from 2 ms (richards) to 200 ms
   (prosemirror); the corpus rows that did not finish are the same pathology
   at larger N.
3. **Link is eager native codegen for every function and is superlinear in
   function size.** The `earley-boyer.js` link sample is entirely MIR's
   generator (`update_call_mem_live`, `make_live_from_mem`, `gvn_modify`,
   `calculate_func_cfg_live_info`): 424k instructions take 58 s where
   crypto's 171k take 3 s. `mir_policy.hpp` defines the interp-interface
   thresholds (`MIR_LARGE_MODULE_INSN_THRESHOLD` 100k, document 20k) but
   `js_mir_link_main` no longer applies them, and Lambda's `jit` tier never
   did. The vendored generator is off limits (rule 16); the lever is policy
   and function size.

The shared root write-back finalizer (`em_finalize_semantic_root_write_back`)
that dominates Lambda's lowering (Lambda doc §1.3) is used by JS too but did
not surface in the JS samples; the literal-shape scans hide it.

---

## 2. Why tune, and why align

1. **Page scripts are JS, and JS pays the whole MIR budget on first load.**
   A Radiant page that pulls `alpine.min.js`, `htmx.min.js` and
   `bootstrap.min.js` compiles for ≈15 s in the default lane and ≈70 ms in
   the AST lane on this build (§1.3). The document safety gate
   (`MIR_RADIANT_AST_NODE_THRESHOLD`) already routes very large bundles to
   the AST executor because MIR was measurably too slow for them
   (JS_01 §6, JS_15); the numbers above say the gate is set far above where
   the pathology starts.
2. **Lambda already made this decision.** D8.1.1v13 ships AUTO: every
   Lambda definition starts in T0 and promotes when hot. LambdaJS has the
   same machinery behind an env var (P2 satellites, `js_interp_promote_
   function_if_hot`, prebuilt AST templates under D8.5.1v7). Two languages
   in one runtime with opposite defaults is a policy inconsistency, and the
   Lambda doc's initial-load argument (§2 there) applies to JS verbatim.
3. **The duplicated analyses are where the cost lives.** Captures, call-site
   evidence, param/return inference, function collection, support scans and
   slot planning exist twice, once per language, and each copy has its own
   scaling defect: Lambda's are tree walks repeated per pass (Lambda doc
   §1.1), JS's are whole-index scans repeated per function or per literal
   (§1.2). One shared, range-indexed query layer fixes both and removes the
   second copy, which is the D8.2.4 intent ("core passes may not grow private
   core-child walks") applied to analyses rather than only to walkers.
4. **Link policy is shared code that neither side uses.** Both front ends
   generate native code for every function of every module regardless of
   size. The vendored generator cannot be patched, so the only durable fix
   is a shared policy that both call.

Historical target, proposed as the acceptance bar for §4: **under the shipped default,
no corpus script's compile (front end + analyze + lower + link) exceeds
20 × its front-end time**, and **lowering is linear in node count**
(`ms per KB` within 3× of the corpus median for every row).

---

## 3. Proposal

Part A aligns the two pipelines and shares their analyses (LC4.1–LC4.7).
Part B removes the JS-specific pathologies (LC4.8–LC4.11) and asks the
default-backend question (LC4.12). Ordered by payoff within each part.

### Part A — one pipeline shape, shared analyses

#### LC4.1 (implemented 2026-09-23) — Both managers run the same named passes, each timed

**Decision.** The JS composite `analyze-plan` pass is split into the same
named passes Lambda runs: `index` → `collect` (functions/classes from the
index) → `captures` → `env-layout` (JS) / `cow-borrows` (Lambda) →
`infer` → `forward-declare` → `mir-lower` → `mir-finalize-load` →
`prelink` → `link`. Fact bits are shared (`CompilerFactBits`); a language
that has no work for a stage registers a no-op. Every pass records its
elapsed time into one `LambdaCompilerTiming` record; the JS `parse_ms`
timer that today covers four passes is retired in favour of the per-pass
fields, and `LAMBDA_PROFILE=1` writes JS rows to `temp/phase_profile.txt`
exactly as it writes Lambda rows.

**Why.** D8.2.5v3 names this schedule. Without per-pass timing the
JS front end cannot be split (§1.3) and the O(F·N) analysis phases cannot be
told apart from lowering. This is the precondition for §4.

#### LC4.2 (implemented 2026-09-23) — The index gains subtree ranges and reverse tables; whole-index scans are retired

**Decision.** `AstIndex` grows: (a) `subtree_end[id]` — IDs are assigned in
pre-order by `ast_index_walk_root`, so a subtree is the contiguous range
`[id, subtree_end)`, descendant tests become a compare, and subtree scans
touch only the subtree; (b) per-function node ranges (`functions[f].first,
.end`) derived from (a); (c) reverse tables built in the same pass:
uses-by-binding (`AstBindingId` → node IDs), calls-by-callee-binding,
member-uses-by-object-node. `ast_index_visit_subtree` drops its
whole-index stack allocation and iterates the range. Shared fragments that
break pre-order (class-field initializers revisited under a second owner)
are recorded as an explicit overlay list rather than by breaking the range
invariant.

Every site in Appendix B then becomes a range scan or a table lookup:
`jm_collect_indexed_body_refs`, `jm_infer_indexed`,
`jm_infer_boxed_return_scalar_class`, `jm_prescan_float_widening`,
`jm_count_indexed_suspensions`, `jm_collect_indexed_func_assignments`,
`jm_is_array_literal_candidate`, `jm_direct_call_literal_return`,
`jm_function_for_parameter`, `jm_direct_literal_argument_for_parameter`,
`jm_literal_shape_has_static_field_use`. The span-owner recovery loop in
`ast_index_visit` runs only for nodes without a structural parent.

**Why.** This turns the JS analysis and lowering phases from O(F·N) and
O(L·N²) into O(N + F + L) without changing a single semantic rule, and it
gives Lambda the same query layer so LC3.3's fused walks can become index
range scans instead of tree walks. It is the D8.2.4 index doing what it was
built for.

#### LC4.3 (implemented 2026-09-23) — One free-variable core feeds both capture analyses

**Decision.** One shared pass over the index (owner-grouped by LC4.2)
produces per function: identifier references with their resolved
`AstBindingId`, own declarations, and the set of referenced bindings owned
by an enclosing function. Lambda's `analyze_captures` and JS's
`jm_analyze_captures` consume this record to build `FnAnalysis::captures`
under their own rules (Lambda: mutability and `var`-param semantics; JS:
`this`/`new.target`/`with`/eval observations, self-reference, class owner).
Lambda drops its build-time and bind-time body walks (Lambda doc LC3.3); the
retained `jm_collect_indexed_body_refs` name is now only a language-specific
filter over that CSR fact, not a body scan. The transitive-capture fixed point
(JS 1.6) runs once, shared, over the function parent table.

#### LC4.4 (implemented 2026-09-23) — The hashed scope index moves into `NameScope`

**Decision.** `JsScopeBindingIndex` (`js_scope.cpp`) is promoted to
`ast-core`: `NameScope` owns a lazily built index keyed by the interned
`String*`, maintained by the shared `push_name`/`js_scope_define_in_scope`
paths, consulted by `lookup_name`, `lookup_name_in_current_scope` and
`js_scope_lookup`. This *is* Lambda doc LC3.1, realized by reuse rather than
by a second implementation. JS keeps its hoisting and Annex-B rules in
`js_direct_scope.cpp`; only the lookup structure is shared.

#### LC4.5 (implemented 2026-09-23) — Function lists, slot planning and support scans come from the index and from bind

**Decision.** (a) Lambda's three function collectors are replaced by
`ast_index.functions`, as JS already does (`jm_collect_indexed_functions`).
(b) Lambda plans interpreter slots at bind as JS does
(`js_scope_plan_binding_slots` becomes the shared `NameScope` planner), so
`interp_plan_script` reduces to scratch-depth computation. (c) Both
kind-support scans (`interp_scan_supported`, `js_interp_script_is_supported`)
become one loop over `index->nodes` against a per-profile supported-kind
bitmap, with the few structural checks (JS named-arg pipes, view bodies)
kept as profile callbacks. Kind support is then a property of the unit
computed once and cached on `Script`.

#### LC4.6 (implemented 2026-09-23) — One call-site table feeds both parameter-evidence inferences

**Decision.** The calls-by-callee-binding table (LC4.2) replaces Lambda's
direct-call portion of the six-round `prepass_collect_call_sites` walk and
JS's per-function `jm_infer_indexed` scans. Lambda enumerates the table by
callee binding on every inference round; its retained prepass walk carries
only distinct escape and return-shape facts. The evidence rules stay per
language
(`infer_param_types_batched` for Lambda, `jm_infer_param_types` for JS).
`infer_return_type` and its body predicates are memoized in `FnAnalysis`
(Lambda doc LC3.7 third bullet) so a callee is inferred once per round.

#### LC4.7 (implemented 2026-09-23) — One link policy, applied by both front ends

**Decision.** `mir_policy.hpp` gains
`mir_select_link_interface(total_insns, largest_function_insns,
document_attached, optimize_level)` returning native / lazy-native /
interp-interface, and both `js_mir_link_main` and Lambda's
`lambda_mir_link_compiler_pass` call it. Defaults: interp interface above
`MIR_LARGE_MODULE_INSN_THRESHOLD` or when a document is attached above
`MIR_RADIANT_INTERP_INSN_THRESHOLD` (the values already defined);
optimize-level downgrade to O1 for any single function above a size
threshold; lazy generation where the owner lifetime permits (the
`JS_LAZY_MIR` caveat in `js_mir_entrypoints_require.cpp` is the constraint
to resolve first). Module top level (`js_main`, Lambda `main`) is lowered as
a chain of bounded-size body functions so no single function carries a
whole bundle's straight-line code. The vendored generator is not touched.

**Why.** 58 s of link on a 191 KB file (§1.3) is not a Lambda-side bug and
cannot be fixed on the Lambda side except by not asking the generator to do
it. The policy exists, is shared, and is unused.

### Part B — JS-specific fixes and the default question

#### LC4.8 (implemented 2026-09-23) — Literal-shape planning is an analysis pass, not a lowering query

**Decision.** `jm_literal_shape_has_static_field_use` and
`jm_literal_shape_for_object` are replaced by a `literal-shapes` step of
the `infer` pass: one walk over the member-uses-by-object table (LC4.2)
counts static field uses per object literal, `jm_literal_shape_build` runs
once per qualifying literal, and the plan is stored on the literal node's
index slot (or in a hashmap keyed by node) so lowering's four call sites do
a lookup. Expected effect: `prosemirror.js` lowering 98 s → seconds;
`alpine.min.js` 3.7–7 s → well under 1 s; the rows that did not finish in
60 s complete.

#### LC4.9 (implemented 2026-09-23) — Suspension counts are subtree-range scans

**Decision.** `jm_count_indexed_suspensions` iterates `[root_id,
subtree_end)` and tests ownership with the range compare; the parent-chain
walk survives only for the array-pattern multiplier and the computed-key
exception. With LC4.2 this is a change of loop bounds, not of rules.

#### LC4.10 (implemented 2026-09-23) — Env-layout phases index children by parent

**Decision.** Phases 1.7–1.7d iterate each function's children through a
parent → children list built once from `functions[].parent`, replacing the
nested `for ci < func_count` searches. O(F²) → O(F).

#### LC4.11 (implemented 2026-09-23) — Index construction does not scan functions per node

**Decision.** In `ast_index_visit`, the span-owner recovery loop over all
functions runs only when the child has no structural parent edge (the
malformed or shared-list case it was written for), never on the ordinary
path. Shared with Lambda.

#### LC4.12 (implemented 2026-09-23; **D8.1.3v19**) — LambdaJS defaults to AUTO like Lambda

**Decision to ratify.** The unset `JS_EXECUTION_BACKEND` selects AUTO: an
interpreter-supported unit and its prebuilt static closure execute from the
retained AST, hot definitions promote to P2 satellites, unsupported units
fall back to whole-module MIR exactly as today. `mir` remains selectable.
This aligns the JS default with D8.1.1v13 and makes the numbers in the
"AST lane front end" column of §1.3 the default first-result cost for page
scripts.

**Ruling record.** D8.1.3v18's MIR default was revised by **D8.1.3v19** and
the formal-design semver bump. The Test262 AUTO/default gate and focused
module/dynamic-import coverage in §4.4 are the acceptance evidence.

### 3.1 Non-goals

- No change to JavaScript semantics, early-error rules, or the AST backend's
  admitted surface (D8.1.3v18 body).
- No inline caches or per-site mutable dispatch state (D8.4.1v2); reverse
  tables are compile-time facts of the unit.
- No patch to vendored MIR (rule 16); LC4.7 is policy only.
- No second IR; the index is the only shared query substrate (D8.2.4).

### 3.2 Priority and expected effect

| Item | Effect | Corpus rows affected |
|---|---|---|
| LC4.8 literal-shape pass | lowering O(L·N²) → O(N); 60–87% of lowering on library bundles | every `test/js` library row; the 60 s time-outs |
| LC4.2 index ranges + tables | O(F·N)/O(Q·N) analyses → linear; enables LC4.3, 4.6, 4.9 | all rows; large-F rows most |
| LC4.7 link policy | link superlinear tail bounded; `earley-boyer` 58 s → interp interface or O1 | rows with > 100k insns or > 20k in a document |
| LC4.12 AUTO default | first result at front-end cost (30 ms vs 6–9 s for alpine) | every page script; needs ratification |
| LC4.1 named timed passes | accounting; front-end split | measurement |
| LC4.3/4.4/4.5/4.6 shared analyses | second copies removed; Lambda inherits hashed lookup, index-based function list, bind-time slots, call-site table | both corpora |
| LC4.9/4.10/4.11 | generator/async bodies, F² phases, index build | large-F rows |

---

## 4. Verification

### 4.1 Corpus and baseline

Twenty scripts: eight Octane programs (the shared benchmark set) and twelve
`test/js` libraries that a page would load, chosen so the rows span 11 KB to
191 KB, 44 to 1310 functions, and the healthy-to-pathological range of §1.3.
The list is the default corpus of `utils/js_compile_phase_bench.sh`.
`prosemirror.js` (467 KB) and the six scripts that do not finish in 60 s are
tracked separately as pathology witnesses, not corpus rows, until LC4.8
lands.

Baseline (2026-09-22, debug build, `--no-log`, min of the runs taken;
`front end` = the timer's `parse_ms`, which spans parse-build, bind,
validate and index until LC4.1):

| Script | KB | MIR default: front end / analyze+lower / link (ms) | AST lane: front end (ms) |
|---|---|---|---|
| `ref/JetStream/Octane/richards.js` | 15 | 4.0 / 34 / 331 | 1.9 |
| `ref/JetStream/Octane/deltablue.js` | 25 | 3.6 / 92 / 474 | 2.9 |
| `ref/JetStream/Octane/raytrace.js` | 15 | 2.1 / 49 / 829 | 2.1 |
| `ref/JetStream/Octane/crypto.js` | 47 | 8.1 / 470 / 3013 | 9.0 |
| `ref/JetStream/Octane/navier-stokes.js` | 13 | 1.5 / 25 / 209 | 1.5 |
| `ref/JetStream/Octane/splay.js` | 11 | 0.9 / 6 / 38 | 0.9 |
| `ref/JetStream/Octane/earley-boyer.js` | 191 | 102 / 1800 / 57,678 | — |
| `ref/JetStream/Octane/code-first-load.js` | 113 | 3.4 / 5 / 27 | — |
| `test/js/lib_mustache.js` | 20 | 2.9 / 34 / 231 | 3.1 |
| `test/js/lib_immer.js` | 15 | 4.7 / 83 / 319 | 5.0 |
| `test/js/floating-ui.min.js` | 22 | 8.6 / 688 / 562 | 8.4 |
| `test/js/lib_popper.js` | 31 | 9.4 / 516 / 712 | 9.7 |
| `test/js/lib_fast_diff.js` | 60 | 5.6 / 132 / 1794 | 5.2 |
| `test/js/alpine.min.js` | 45 | 28.8 / 3746 / 2007 | 30.3 |
| `test/js/htmx.min.js` | 50 | 17.8 / 2440 / 1059 | 21.0 |
| `test/js/bootstrap.min.js` | 59 | 21.4 / 1268 / 5487 | 27.8 |
| `test/js/lib_marked.js` | 99 | 9.8 / 383 / 3756 | 10.0 |
| `test/js/lib_moment.js` | 73 | 26.5 / 7824 / 37,049 | 33.9 |
| `test/js/hljs_highlight.js` | 122 | 33.5 / 14,122 / 1634 | 33.3 |
| `test/js/lib_zod.js` | 68 | 28.2 / 17,061 / 15,179 | 25.9 |

Pathology witnesses: `test/editable-editors/build/prosemirror.js` 282 /
97,608 / 12,622; `test/js/lib_yup.js` 75 / 19,423 / 6670;
`test/js/moment_src_min.js` 36 / 5830 / 48,771; no result within 60 s:
`test/js/lib_lodash.js`, `lib_ramda.js`, `lib_rxjs.js`, `moment_src.js`,
`ramda_src.js`, `ramda_src_clean.js`, `ramda_src_min.js`,
`ramda_src_nocomments.js`, `ref/JetStream/Octane/box2d.js`.

### 4.2 Method

```bash
# default lane (the shipped default), 3 reps, min; TIMEOUT in seconds
TIMEOUT=300 utils/js_compile_phase_bench.sh mir 3 > temp/js_compile_phase_after_mir.tsv

# AST/AUTO lanes (front end only reaches the timer)
utils/js_compile_phase_bench.sh ast  3 > temp/js_compile_phase_after_ast.tsv
utils/js_compile_phase_bench.sh auto 3 > temp/js_compile_phase_after_auto.tsv

# pathology witnesses, one at a time
printf "test/editable-editors/build/prosemirror.js\n" > temp/js_witness.txt
TIMEOUT=600 utils/js_compile_phase_bench.sh mir 1 temp/js_witness.txt
```

Rules as in the Lambda doc: same binary type before and after; `--no-log`;
one compile at a time; min of 3; keep the TSVs under `temp/` and quote them
in the implementation record. The current schema also exposes the named
parse-build, bind, validate, and index fields.

### 4.3 Historical acceptance criteria

1. **Budget**: under the MIR default, every corpus row's total compile
   (front end + analyze + lower + link) ≤ 20 × its front end; every
   pathology witness completes, `prosemirror.js` in ≤ 10 s.
2. **Scaling**: analyze+lower `ms/KB` within 3× of the corpus median on every
   row (today 2–200 ms/KB); no row's link exceeds 3 s once LC4.7 applies the
   interp interface above the threshold.
3. **Identity**: `test_js_test262_gtest` baseline unchanged under the
   default and under `JS_EXECUTION_BACKEND=auto` (rule 18: never masked);
   `make test` green; the 20 corpus scripts produce byte-identical stdout
   before and after under each backend.
4. **Emission**: MT7 (D8.6.1) at 0% slack via `test_js_mir_emission_gtest`
   and `test_mir_ratchet_gtest`: LC4.2–LC4.11 reorder analysis, not emission,
   so the finalized MIR of every corpus script is identical before and after.
   LC4.7's interp/O1 selections are exempt by construction (no MIR change,
   different link interface).
5. **Alignment**: after LC4.3–LC4.6 the Lambda corpus of
   `Lambda_Design_Compile_Pipeline.md` §4.1 meets that document's bar with
   the shared code, and `grep` finds no remaining `for (... < index->count)`
   inside a per-function or per-node helper in `lambda/js/js_mir_*.cpp`
   (Appendix B empties).
6. **Default** (LC4.12): the web-template suite and the
   Test262 corpus pass under AUTO; page-load compile for the
   alpine + htmx + bootstrap trio is measured at the AST-lane front-end
   figure.

### 4.4 Implementation record (2026-09-23)

The implemented subset is governed by **D8.2.4v2**, **D8.2.5v3**, and
**D8.1.3v19**. `AstIndex` now publishes preorder ranges, reverse tables,
function-child adjacency, and a per-function overlay CSR slice for synthetic
class-field initializers that logically own source nodes outside their
structural range. JS capture collection consumes both the dense range and the
overlay. This preserves the range invariant and repairs explicit-MIR class
field captures without a special lowering path.

The JS pass manager separately publishes/times collect, captures,
environment-layout, inference, and forward declaration; `NameScope` owns the
shared pointer-identity lookup/slot planner; literal-shape planning consumes
member-use facts before lowering; and both front ends select MIR linking via
`mir_select_link_interface`. The unset JS selector is AUTO; AST-inadmissible
units retain the MIR fallback. Direct AST ES-module admission now owns and
drains its outer microtask turn, while nested dynamic imports retain their
existing suppression boundary.

Verification on the debug build:

- `make build` completed successfully.
- `test_js_script_gtest` passed all 190 tests, including explicit-MIR
  class-field capture-overlay coverage and default-AUTO dynamic-import
  coverage.
- `test_js_test262_gtest` completed with 35,047/35,047 fully passing baseline
  cases and zero regressions under the unset (AUTO) selector.
- `utils/js_compile_phase_bench.sh mir 1` and `auto 1` produced timing rows
  for all 20 checked-in corpus sources with no compiler timeout/error row.
  The TSV now retains a separate exit-status column because several raw
  benchmark files intentionally require CommonJS/browser hosts at execution
  time. Explicit MIR and AUTO produce identical stdout for `lib_marked.js`,
  the synthetic-field regression corpus member.

The reproducible corpus is the eight checked-in Are-We-Fast-Yet JavaScript
benchmarks plus the twelve listed `test/js` libraries. Historical Octane paths
in §4.1 describe the 2026-09-22 measurement only; they are not harness input.

The final shared consumers landed after the first implementation record:

- LC4.3 adds `AstFunctionReference` CSR slices keyed by `AstFunctionId`.
  Each entry records its indexed node, resolved `AstBindingId`, read/write
  role, and whether its binding is free of the owning function. Lambda builds
  captures directly from that fact and propagates direct-child captures;
  JavaScript filters the same fact for its lexical environment rules.
- LC4.5 adds `ast_index_scan_profile_support`: one indexed node loop invokes
  the frontend callback, stores its accept/reject/subtree-skip bitmap, and
  caches the result on the Script-owned `AstIndex`. Lambda and JavaScript
  support admission both consume it; Lambda's task satellite boundary is an
  explicit subtree skip rather than a recursive scan.
- LC4.6 enumerates `ast_index_callee_calls` by binding before Lambda's
  inference round. The general prepass no longer records direct calls while
  collecting: it remains only for forward declarations plus non-call-edge
  escape and return-shape sources.

The new `JsScriptOwnership.PublishesFreeReadWriteFactsAndCachesProfileSupport`
regression covers JS outer reads/writes and verifies that a second same-profile
query reuses the cached bitmap. It also caught and fixed the zero-function
CSR case. Targeted Lambda closure/shadowing/call-site inference and JS
MIR/AUTO parity checks pass with this record. These changes implement the
shared-unit requirements of **D8.2.4v2** and the fact ownership/scheduling
requirements of **D8.2.5v3**; they do not alter JavaScript or Lambda semantics.

---

## Appendix A — What is already shared, and what is duplicated

| Concern | Shared today | Duplicated today | Proposal |
|---|---|---|---|
| Pass manager, fact bits | `compiler_pass.cpp` | JS analyze/plan is one composite pass | LC4.1 |
| AST header, core kinds, child enumeration | `AstNode`, `ast_visit_core_children` + profiles | Lambda `walk_lambda_ast` (hand-written, private edges); JS `js_ast_visit_children` (table-driven) | fold Lambda's private edges into `lambda_profile.visit_ext_children`; one shared walker |
| Index | `AstIndex` | Lambda barely consumes it; JS scans it whole | LC4.2 |
| Scopes | `NameScope`/`NameEntry` | JS hashed lookup; Lambda linear | LC4.4 |
| Function list | `ast_index.functions` | Lambda collects ×3 by walking | LC4.5 |
| Captures | `FnAnalysis`, `FnCapture` | `analyze_captures` vs `jm_analyze_captures` | LC4.3 |
| Call-site evidence | — | `prepass_collect_call_sites` vs `jm_infer_indexed`/`jm_callsite_propagate` | LC4.6 |
| Interp support scan, slot planning | `FnPromotionCell`, satellites | two scanners; Lambda plans slots in a later pass | LC4.5 |
| Emitter, root write-back, ABI | `MirEmitter`, `mir_emitter_shared.hpp` | — | Lambda doc LC3.7 benefits both |
| Link policy | `mir_policy.hpp` thresholds | neither side applies them | LC4.7 |
| Caches, prebuild | `InputScriptCache`, `module_ast_prebuild` | — | — |
| Default execution tier | — | Lambda AUTO, JS MIR | LC4.12 |

## Appendix B — Whole-index scans inside per-function or per-node helpers (2026-09-22 tree)

`js_mir_analysis.cpp`: `jm_count_indexed_suspensions`,
`jm_collect_indexed_func_assignments`, `jm_collect_indexed_body_refs`.
`js_mir_expression_lowering.cpp`: `jm_is_array_literal_candidate`,
`jm_direct_call_literal_return`, `jm_function_for_parameter`,
`jm_direct_literal_argument_for_parameter`,
`jm_literal_shape_has_static_field_use`.
`js_mir_function_collection_class_inference.cpp`: `jm_infer_indexed`,
`jm_infer_boxed_return_scalar_class`, `jm_prescan_expression_has_float_hint`,
`jm_prescan_expression_has_float_array_access`, `jm_prescan_float_widening`
(two loops). Once per unit and therefore fine: `jm_callsite_propagate`.
Shared: `ast_index_visit` span-owner loop (`ast-core.cpp`).

## Appendix C — Open issues

- **LCO7** JS front-end split (parse-build vs bind vs validate vs index) is
  unmeasured because one timer spans all four; LC4.1 supplies it. The
  `ast_index_visit` O(N·F) loop may be a visible share on minified bundles.
- **LCO8** `js_mir_link_main` ignores `mir_policy.hpp`; whether the
  D8.1.3v11 "always native" comment was meant to retire the thresholds or
  only the pure-interpreter mode needs confirming against the design record
  before LC4.7.
- **LCO9** The interp-interface path's own cost on 400k-instruction modules
  is unmeasured here; JS_15 measured 4–6× on lodash. Re-measure with LC4.7.
- **LCO10** Sampler attribution for the alpine compile captured only ~1.2 s
  of a 9.5 s run; the per-pass timers of LC4.1 replace sampling for the
  budget table.
- **LCO11** Document AST gate (`MIR_RADIANT_AST_NODE_THRESHOLD`, 25,000
  nodes in `mir_policy.hpp`, 50,000 in JS_01 §6) disagree; unify with LC4.7.
- **LCO12** Minified bundles cost 3–5× more front end per KB than source
  (`alpine.min.js` 0.65 ms/KB vs `lib_marked.js` 0.1 ms/KB); whether that is
  token density or identifier interning is unknown.

## Appendix D — Measurement notes

- `JS_TRANSPILE_TIMING=1` prints one `JS_TRANSPILE_TIMING` and one
  `JS_MIR_VOLUME` line per script after execution (`main.cpp`); a script that
  is killed by `timeout` prints nothing. `LAMBDA_COMPILER_TIMING=1` adds the
  `COMPILER_TIMING` line on the same path.
- The AST lane is selected with `JS_EXECUTION_BACKEND=ast`; `auto` selects
  the AST lane plus P2 promotion; unset selects AUTO under **D8.1.3v19**.
- macOS `sample <pid>` by PID immediately after launch; the JS compile runs
  on the main thread (unlike Lambda's large-stack thread). Attribution
  scripts and raw samples are under `temp/sample_js_*.txt`.
- Other sessions were running benchmarks during these captures; repeated
  runs of `alpine.min.js` varied 3.7–7.0 s in the analyze+lower column.
  Ratios and orders of magnitude are the figures this document relies on.
