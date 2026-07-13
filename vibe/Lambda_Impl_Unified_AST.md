# Unified AST — Implementation Plan (Lambda + LambdaJS)

**Date:** 2026-07-11
**Design authority:** `vibe/Lambda_Design_Unified_AST.md` rev 6 (ledger U1–U25). This plan implements that design for **Lambda-script and LambdaJS only**.
**Out of scope (deferred to future plans):** Python/Ruby/Bash ports (their transpilers stay untouched and keep working as-is); **TypeScript active integration** — TS rides along passively (see §1.3); the ShapePool transition API for JS hidden classes (design §6.2) is split into an optional parallel workstream (§8).

---

## Implementation Progress

| Phase | Step | Status |
|---|---|---|
| **0** | P0.1 — extract `MirEmitter` | 🟡 **in progress** — Lambda side ✅ done; JS side + shared `em_call_*`/`em_ensure_import` (import-cache unify) pending |
| 0 | P0.2 — G1 honest GC-rooting | ⬜ not started |
| 0 | P0.3 — unified `VarEntry` + scopes | ⬜ not started |
| 0 | P0.4 — const-pool API | ⬜ not started |
| 1–5 | — | ⬜ not started |

**Latest (2026-07-13):** `struct MirEmitter` + inline `em_new_reg`/`em_new_label`/`em_emit_insn`/`em_emit_label` primitives added to `lambda/mir_emitter_shared.hpp` (building on the already-shared *stateless* helpers there). `MirTranspiler` now embeds a `MirEmitter em`, with its per-function emit cursor / counters / import-cache fields removed and all accesses routed through `mt->em.*`; the `new_reg`/`new_label`/`emit_insn`/`emit_label` wrappers delegate to the `em_*` functions. Build clean (0/0); every individual Lambda test passes (baseline gate confirmed modulo known parallel-load timeout flakiness on heavy tests). Change staged, uncommitted.

---

## 1. Scope and Ground Rules

### 1.1 What this plan delivers

1. **Phase 0** — shared `MirEmitter` substrate with the G1 GC-rooting fix, unified `VarEntry`/scopes, unified const-pool API.
2. **Phase 1** — `ast-core.hpp`: unified base node, leveled node-kind space, superset `Operator`, shared scopes; `LangProfile` skeleton.
3. **Phase 2** — leaf-struct unification level by level (L1 → L2+L3 → L4 → L5/L6), including the U19/U22/U23 form-field mergers.
4. **Phase 3** — shared analysis: one capture analysis (Lambda onto scope-env), one evidence inference with call-site propagation (U17), JS binding promoted to build time.
5. **Phase 4** — shared lowering driver over core nodes; Lambda and JS profiles carry the semantics; ICs/native specialization preserved.
6. **Phase 5** — module system: AST-level cross-language import binding; `create_js_import_script` retired; live bindings.

### 1.2 Ground rules (from the design + K17)

- **Never a big-bang rewrite.** Every phase — and every step inside Phase 2/4 — lands with both pipelines green. Old and new paths coexist behind flags until the new path is proven, then the old path is deleted in the same phase (no permanent dual paths).
- **Test gates on every merge:** `make test-lambda-baseline` (100%), lambda gtest (`make build-test` suite), editor Phase-A **1931** JS tests (`make editor-4c-js`), UI-automation **5714**, node-baseline (`make node-baseline`, ≥1492/3517 — no regression), plus AWFY + LambdaJS perf benchmarks on codegen-touching phases (release build only, per project rules).
- **Concurrency-track interleaving (U21):** after Phase 0, the concurrency plan's Stage A may proceed in parallel with Phases 1–3 (it codes against the emitter API). Phase 4's resumable-function-transform extraction is a *coordination point* with that track (§7).
- **C2MIR (`transpile.cpp`) is frozen (U11):** it receives only the mechanical Phase-1 enum update; it is never taught core-node semantics beyond what Lambda already exercises.

### 1.3 TS deferral — what "passive" means concretely

TS today: `TsTranspiler` *is* `JsTranspiler`; the default fast path strips TS annotations to JS **source text** and feeds the normal JS pipeline (`ts_preprocess_source` → `transpile_js_to_mir`); the type-aware path builds TS nodes (enum ≥1000) on the JS AST.

- The **fast path keeps working automatically** through every phase — it enters the pipeline as plain JS source.
- The **type-aware path** gets mechanical updates only: TS node kinds renumber to the 1500–1999 range in Phase 1; `ts_ast.hpp`/`ts_type_parser` compile against `ast-core.hpp`. No TS-specific driver work, no TS profile. If the type-aware path becomes costly to keep compiling mid-phase, it may be temporarily gated to the fast path (documented, reversible) — decide at the moment, don't pre-commit.
- Full TS integration (TS profile, annotation nodes as `AST_TYPE_REF` producers) is a future plan.

### 1.4 Current-state anchors (what we are reshaping)

| Asset | Size | Fate |
|---|---|---|
| `lambda/ast.hpp` | node structs + `AstNodeType` (~70 kinds) | splits: core → `ast-core.hpp`, Lambda-range stays |
| `lambda/js/js_ast.hpp` | 649 lines, ~57 kinds, base already layout-compatible | same split; JS-range stays |
| `lambda/build_ast.cpp` | 9,754 lines | stays (Lambda builder); retargets to core nodes |
| `lambda/js/build_js_ast.cpp` | 4,560 lines | stays (JS builder); retargets; gains bind-time hoisting/TDZ (Phase 3) |
| `lambda/transpile-mir.cpp` | 14,479 lines | Phase 0 extracts emitter; Phase 4 shrinks to Lambda profile handlers + driver contributions |
| `lambda/js/js_mir_*` (10 files) | ~2.3 MB | Phase 0 extracts `jm_*` emitter twins; Phases 3–4 dissolve analysis + statements/expressions into shared code; JS profile keeps classes/generators/eval/with/regex/ICs |
| `lambda/js/js_scope.cpp` | 1,244 lines | Phase 3 replaces with shared `NameScope` binding |
| `lambda/js/js_early_errors.cpp` | 1,287 lines | becomes JS profile `validate` hook (mostly unchanged internally) |
| `lambda/module_registry.cpp` | `create_js_import_script` synthetic bridge | Phase 5 retires the bridge |
| `lambda/transpile.cpp` (C2MIR) | 8,485 lines | frozen; Phase-1 mechanical enum update only |

---

## 2. Phase 0 — Emitter Substrate (no AST change)

**Deliverable:** `lambda/mir_emitter.hpp` + `mir_emitter.cpp`; both transpilers run on it; G1 rooting fixed; all suites green. Independently valuable (Clean_Up §6.4) even if later phases pause.

### Steps

**P0.1 — Extract `MirEmitter`.** 🟡 *in progress (Lambda side landed 2026-07-13)*
```c
struct MirEmitter {                        // ✅ landed in lambda/mir_emitter_shared.hpp
    MIR_context_t ctx;                     // ✅ (cached; MIR_module_t module kept on owner for now)
    MIR_item_t func_item;  MIR_func_t func;// ✅ per-function emit cursor
    int reg_counter;  int label_counter;   // ✅
    hashmap* import_cache;                 // ✅ ensure_import memo (handle held; shared em_ensure_import still TODO)
    // MIR_reg_t rt_reg, gc_reg;           // ⬜ deferred — not needed by the shared primitives yet
    // GC root slots (P0.2), consts/type_list BSS regs (P0.4)
};
// primitives: em_new_reg ✅, em_new_label ✅, em_emit_insn ✅, em_emit_label ✅,
//   em_call_0..6 (+void variants) ⬜, em_null_item ⬜, em_box_*/em_unbox_* bridge ⬜, em_ensure_import ⬜
```
Sources to unify: `transpile-mir.cpp:434–490` (`new_reg/emit_insn/emit_label/emit_call_N/ensure_import`) ≡ `js_mir_internal.hpp:96–204` + `js_mir_calls_boxing_types.cpp:168+` (`jm_*`).
**Mechanics:** `MirTranspiler` and `JsMirTranspiler` *embed* a `MirEmitter`; the old function names become thin inline wrappers (`#define`/inline forwarding) so the initial diff is small and reviewable; wrappers are deleted at the end of the phase (or mechanically inlined). The JS-side `js_exec_profile_note_mir_call` hook becomes an optional emitter callback.

- ✅ **Done (Lambda):** `MirEmitter` struct + `em_new_reg`/`em_new_label`/`em_emit_insn`/`em_emit_label` in `mir_emitter_shared.hpp`; `MirTranspiler` embeds `MirEmitter em`, its cursor/counter/import-cache fields removed and accesses migrated to `mt->em.*`; the four primitive wrappers delegate to `em_*`. (Watch-out found: greedy field rename also hits the unrelated `current_func_can_raise` field — keep that on `MirTranspiler`.)
- ⬜ **TODO (JS + dedup payoff):** `JsMirTranspiler` embeds `MirEmitter`; promote `emit_call_*`/`ensure_import` ⇄ `jm_call_*`/`jm_ensure_import` into shared `em_call_*`/`em_ensure_import`, then delete both copies. Import-cache unification is feasible — Lambda `ImportCacheEntry` and JS `JsImportCacheEntry` share an identical layout (`char name[128]; {MIR_item_t proto; MIR_item_t import;}`).

**P0.2 — G1 honest-rooting fix, in the emitter.** Implement the honest-local-typing design from `vibe/Lambda_GC_Root_Issue.md`: root slots are allocated **only** for locals whose `VarEntry.type_id` is actually a GC-managed `Item`/pointer type; native ints/doubles and non-GC pointers are never blanket-rooted as `MIR_T_I64` Items. This is the single most delicate step of Phase 0:
- Known tripwires (from prior sessions): deltablue holds pointer locals in int-typed registers (why blanket rooting was "load-bearing"); the StackOverflow test hangs under naive blanket rooting; BUG-001 (`array_end`→GC use-after-free) reproduces deterministically via havlak+push.
- **Acceptance evidence:** lambda baseline + gtest 100%; StackOverflow test terminates; havlak+push BUG-001 repro passes; JS suites green; AWFY within noise on release build.

**P0.3 — Unified `VarEntry` + scope stacks.** Merge `MirVarEntry` (transpile-mir.cpp:106–116) and `JsMirVarEntry` (js_mir_context.hpp:145) into one emitter-owned `VarEntry` with the flag superset (`tdz_active/is_let_const/is_const` ∪ `is_state_var/num_type/elem_type` ∪ env slot fields); one `var_scopes[64]` hashmap-stack implementation.

**P0.4 — Const-pool API.** `em_add_const/em_load_const` + per-module `consts_bss` discipline on the emitter (Lambda's existing model). Lambda switches to the API (mechanical); JS *adopts the API* for its future pool residents (regex objects, template cooked arrays) but actual migration of those happens with Phase 4 lowering — Phase 0 only establishes the single home.

**Exit gate:** all suites green, benchmarks flat, `jm_call_*`/`emit_call_*` duplicates gone. **Concurrency Stage A is unblocked from here (U21).**

---

## 3. Phase 1 — `ast-core.hpp` + One Node-Kind Space (mechanical, wide)

**Deliverable:** both ASTs numbered in one space; shared base declarations; profiles as pass-throughs. Zero behavior change, proven by dump equivalence.

### Steps

**P1.1 — Create `lambda/ast-core.hpp`:**
- `AstNode` base (moved from `ast.hpp`; `js_ast.hpp`'s duplicate base deleted — it is already layout-identical by design).
- `enum AstNodeType : uint16_t` with the **core kinds only**, blocked by level (§2.2 of the design: 1–49 L0, 50–149 L1, 150–249 L2, 250–299 L3, 300–349 L4, 350–399 L5, 400–449 L6). Language ranges are *reserved*; language headers define their constants inside their range (`constexpr AstNodeType AST_LMD_PIPE = AstNodeType(500);`).
- Superset `Operator` enum (merge Lambda `Operator`, lambda-data.hpp:518, + `JsOperator`, js_ast.hpp:161; U4).
- `NameScope` extended with `ScopeKind {GLOBAL, MODULE, FUNCTION, BLOCK}` + `strict`; `NameEntry` flag superset (design §L2 item 3). No behavior change yet — new fields dormant.
- `FnAnalysis` skeleton + `union FnExt` with forward typedefs (U20) — dormant until Phase 3.
- Clause-node base (tier-2 variance) — dormant until Phase 2.

**P1.2 — Renumber Lambda kinds.** Map existing `AST_NODE_*` onto core values where they are core (ident, binary, if, call, …) and into 500–999 where Lambda-range (pipe, query, element, patterns, views, type-syntax). Update every switch/comparison: `build_ast.cpp`, `transpile-mir.cpp`, `transpile.cpp` (frozen C2MIR — mechanical only), `emit_sexpr.cpp`, `safety_analyzer.cpp`, `module_registry.cpp` (synthetic nodes), `runner.cpp`, validator/editor touchpoints. **One commit, mechanical.**

**P1.3 — Renumber JS/TS kinds.** JS kinds take the core values for 1:1-mapped constructs and 1000–1499 for JS-range; TS extension kinds move 1000→1500 block (`ts_ast.hpp`, `ts_type_parser/builder`, `ts_preprocess` untouched on the fast path). At this stage JS nodes keep their **own struct definitions** — only the numbers unify; structs are Phase 2.

**P1.4 — Dump-equivalence harness (tooling, load-bearing).** Extend `emit_sexpr` to (a) run on JS ASTs (a minimal JS dumper is required anyway for Phase-2 verification and is the long-term debug tool), (b) support a "canonical kind names" mode so before/after renumber dumps diff clean. Corpus: all `test/lambda/*.ls` + the node-baseline JS corpus. **The renumber commits must produce byte-identical canonical dumps.**

**P1.5 — `Script.lang → LangProfile*`.** Introduce `LangProfile` struct (design §4.3 signature) with `lambda_profile` and `js_profile` instances whose hooks are pass-throughs/no-ops calling today's code paths. `ModuleDescriptor.source_lang` string → profile resolution at load.

**Exit gate:** canonical dump equivalence on both corpora; all suites green; C2MIR build (`make` Jube config) compiles and passes its regression diff.

---

## 4. Phase 2 — Leaf-Struct Unification (level by level)

**Deliverable:** one struct set for core nodes in `ast-core.hpp`; both builders produce them; both existing lowerings consume them. Each family is its own PR with green gates. Order per design Phase-2: **L1 → L2+L3 together → L4 → L5/L6**.

### P2.1 — L1 expressions

| Family | Merge work | Watch out for |
|---|---|---|
| `AST_LITERAL` | `AstPrimaryNode` (value lives in `Type*`/`TypeConst.const_index`) + `JsLiteralNode` (value union on node) → unified node carrying the value union **and** keeping the `Type*`/const-index mechanism (it feeds the const pool). JS builder fills both; Lambda builder unchanged semantics | the two value-carrying conventions must not drift — the const-index path is authoritative for pooled literals |
| `AST_IDENT` | trivial — structs already identical (`name`, `entry`) | — |
| `AST_UNARY`/`AST_BINARY` | superset `Operator` adoption; Lambda keeps `op_str` (StrView) as a core field (harmless for JS) | JS logical ops (`&&`,`||`,`??`) short-circuit — stay `AST_BINARY` with op; lowering unchanged this phase |
| `AST_ASSIGN` | merge Lambda `ASSIGN_STAM`/`INDEX_ASSIGN_STAM`/`MEMBER_ASSIGN_STAM` + JS `ASSIGNMENT_EXPRESSION` → one node, `target` = ident/member/pattern | Lambda's three statement forms collapse via target shape; keep `target_entry` resolution |
| `AST_CALL`/`AST_MEMBER` | field-union per catalog (`optional`+`pipe_inject`+`propagate`+`can_raise`; `computed` covers Lambda index-vs-member) | `SYS_FUNC` stays a Lambda call flavor (flag or Lambda-range subtype — decide in-PR by diff size) |
| `AST_IF` | + `IfForm {if, unless, ternary}`; absorbs JS `CONDITIONAL_EXPRESSION` | — |
| `AST_ARRAY`/`AST_MAP`/`AST_MAP_ENTRY`/`AST_SEQ` | per catalog (U16); `SeqKind {list, tuple, comma}` | Lambda `CONTENT` list-flavor maps to `AST_SEQ`/Lambda-range — decide in-PR |
| `AST_INTERP_STR`, `AST_RANGE`, `AST_SPREAD`, `AST_NEW`, `AST_MATCH`(+arms) | JS structs adopt core shape; `AST_MATCH{MatchForm}` absorbs JS switch (U19) — switch cases become arms with `fallthrough` | switch fallthrough lowering stays byte-equivalent (old lowering reads the new shape) |

### P2.2 — L2 + L3 together (the variable story lands as one piece)

- `AST_VAR_DECL{DeclKind}` + `AST_DECLARATOR{target, type_annot, init, error_name}` — merges Lambda `LET/VAR/PUB_STAM` + JS `VARIABLE_DECLARATION/DECLARATOR`. `pub` becomes DeclKind + Phase-5's export normalization (until then, `pub` keeps its current handling behind DeclKind).
- L3 patterns: `AST_PARAM`, `AST_PATTERN_ARRAY/MAP/DEFAULT/REST` — JS patterns adopt core structs; Lambda `DECOMPOSE` re-expressed as core patterns (its builder emits them; its existing lowering updated to read them).
- `AST_LOOP{LoopForm}` (U19) absorbing Lambda `WHILE_STAM` + JS `WHILE/DO_WHILE/FOR_STATEMENT`; `AST_FOR` (U22) for `FOR_STAM` + JS `FOR_OF/FOR_IN` with `IterKind`; labels as fields (U15) — JS `LABELED_STATEMENT` deleted, builder folds labels in (exotic cases wrap in labeled `AST_BLOCK`).
- `AST_TRY` multi-arm + `AST_CATCH_ARM` (U23) — JS try restructured to the one-arm case of the multi-arm node.
- `AST_YIELD`/`AST_AWAIT` at L2 numbering (U24) — struct merge trivial.
- `AST_BREAK/CONTINUE/RETURN/RAISE/BLOCK/EXPR_STMT/SCRIPT` — near-trivial merges.

### P2.3 — L4 functions

- Unified `AST_FUNC` (`FuncFlags` covering fn/pn + arrow/async/generator/strict…; U14) merging `AstFuncNode` + `JsFunctionNode`; `FnAnalysis*` field added but initially wrapping each side's existing data (`CaptureInfo` list / `JsFuncCollected` entry) via the `FnExt` union — real unification is Phase 3.
- `AST_METHOD : AST_FUNC` (U25) — JS `METHOD_DEFINITION` flattened (no wrapper around an inner function node); Lambda `TypeMethod` surface maps on.

### P2.4 — L5/L6

- `AST_CLASS` (members = `AST_METHOD`/`AST_FIELD`), `AST_FIELD`, `AST_INTERFACE` (maps Lambda `OBJECT_TYPE` where it is interface-like; the full `OBJECT_TYPE` semantics — constraints, literal construction — stay Lambda-range/clauses).
- `AST_IMPORT`/`AST_EXPORT` struct merge (behavioral import changes are Phase 5; `pub`→`AST_EXPORT` normalization lands here as a builder change with unchanged downstream handling).

**Per-PR verification:** canonical dump equivalence *modulo the documented struct change*, plus full gates. **Exit gate:** `js_ast.hpp` and `ast.hpp` contain only language-range structs; every core construct has exactly one struct.

---

## 5. Phase 3 — Shared Analysis

**P3.1 — One capture analysis.** Generalize `jm_analyze_captures` (js_mir_analysis.cpp:1779) + Lambda `analyze_captures` (build_ast.cpp:1389) into a shared pass writing `FnAnalysis.captures` (superset entry: `scope_env_key`, `grandparent_slot`, `force_env`, `is_mutable`). **Lambda closures move onto the JS scope-env runtime representation** (`Item*` slot arrays) — this changes Lambda's closure codegen and env access paths; verify with closure-heavy lambda tests + baseline. Delete `CaptureInfo` and the JS capture side-tables.

**P3.2 — One evidence inference + call-site propagation (U17).** Shared walk producing superset `ParamEvidence`; profile resolution policies (`Lambda: INT w/o float-context → LMD_TYPE_INT; JS: int→FLOAT per N1–N9`); **port the hard-won rules once**: INFER_FLOAT_CONTEXT-on-division, comparisons-are-not-int-evidence, container/compared-non-numeric/reassignment demotions, alias folding. Call-site propagation generalizes `jm_callsite_propagate` at the L3 binding layer — **now on for Lambda too** (deferral lifted). Delete `InferCtx` (transpile-mir) and `JsParamEvidence`. Gates: lambda baseline + **AWFY release-build benchmarks** (call-site inference changes Lambda native-param decisions — expect wins, verify no regressions), node-baseline.

**P3.3 — JS binding to build time.** `build_js_ast.cpp` binds authoritatively on shared `NameScope` (var-hoisting past BLOCK scopes, let/const TDZ flags at bind time — promoting what `js_scope_define` half-does today); `js_early_errors` becomes the JS profile `validate` hook; the MIR-time scope reconstruction in `js_mir_*` reads bound entries instead of re-deriving. `js_scope.cpp`'s parallel machinery deleted.

**Exit gate:** `InferCtx`, `JsParamEvidence`, `CaptureInfo`, duplicate scope machinery all deleted; suites + benchmarks green.

---

## 6. Phase 4 — Shared Lowering Driver

**P4.1 — Driver skeleton + profile hooks.** `lower_node(Driver*, AstNode*) → Reg` dispatching core kinds; `LangProfile` lowering hooks go live (`lower_binary/unary`, `emit_truthy_test`, `emit_to_number`, `emit_member_get/set`, `emit_call`, `emit_error_check`, `lower_clause`, `lower_ext_node`). A **per-module A/B flag** (env var / CLI) selects old vs new lowering for diffing — MIR dump comparison (`temp/mir_dump.txt`) is the equivalence instrument.

**P4.2 — Lambda first**, node family by family (statements → expressions → calls/closures → for-expr clause driving with `lower_clause` for group/order/join). Lambda-range nodes route through `lower_ext_node` to the existing code, which shrinks in place.

**P4.3 — JS module-by-module**, starting on the node-baseline corpus. **Non-negotiable carry-overs into the JS profile:** inline caches (`JsLoadIC/JsStoreIC`), native specialization (dual boxed+native emission — generalized in the driver since it's the same mechanism as Lambda's unboxed path), ctor shape caches, TDZ checks, completion/finally semantics. Perf gate per batch: LambdaJS perf suite + AWFY-JS.

**P4.4 — Shared engines:** one destructuring lowering engine (serves declarators/params/for-targets/catch/match arms); shared iterator emission (`jm_emit_get_iterator` generalized, profile-dispatched); completion/abrupt-cleanup logic.

**P4.5 — Resumable transform (coordination point, U21):** if concurrency Stage B has produced the Lambda-side transform by now, extract the shared utility here with two clients; if not, Phase 4 completes without it and the extraction attaches to Stage B's schedule. **Do not extract single-client.**

**Exit gate:** shared driver lowers all core nodes for both languages; `js_mir_expression_lowering`/`statement_lowering` reduced to profile handlers; old per-language lowering for core nodes deleted; all suites + perf gates green.

---

## 7. Phase 5 — Module System

- Cross-language import binding at AST level: importer's builder binds against the imported module's real `AstScript` via the existing `declare_module_import` path; **retire `create_js_import_script`** (module_registry.cpp:202) and its synthetic ≥1,000,000-offset nodes.
- `ModuleDescriptor` gains `AstScript* ast` (compile-time; droppable post-JIT).
- **Live bindings:** exported vars reference the owning module's BSS slot (closes the documented `pub`-vars gap at module_registry.cpp:184; same mechanism serves ES live bindings).
- Both directions tested: Lambda→JS import corpus + JS→Lambda (`module_build_lambda_namespace` path) + circular-import cases.

**Exit gate:** synthetic bridge deleted; cross-language import tests green; live-binding tests added (new `.ls` + `.txt` goldens per project rule).

---

## 8. Parallel / Optional Workstream — ShapePool transitions (design §6.2)

Independent of Phases 1–5 (touches runtime shape machinery, not the AST): `shape_pool_transition(parent, name, type)` API; JS incremental property-addition routes through the pool; ICs key on pooled shapes. Schedulable any time after Phase 0; **not a dependency of any phase above**. Kept in this plan as opt-in scope.

---

## 9. Sequencing, Risks, Rollback

### Dependency graph
```
Phase 0 ──► Phase 1 ──► Phase 2 ──► Phase 3 ──► Phase 4 ──► Phase 5
   │                                                ▲
   └──► (concurrency Stage A, parallel) ──► Stage B ┘  (P4.5 two-client extraction)
   └──► §8 ShapePool workstream (any time)
```

### Rough sizing

| Phase | Size | Character |
|---|---|---|
| 0 | **M–L** | mechanical extraction + one delicate fix (G1); highest *care* density |
| 1 | **M** | wide but mechanical; tooling (dump harness) is the real work |
| 2 | **L** | many small PRs; steady grind; each family independently green |
| 3 | **L** | the deepest semantic work (closures re-based, inference merged) |
| 4 | **XL** | the long pole; module-by-module JS migration with perf gates |
| 5 | **M** | focused; well-bounded by the existing registry seam |

### Top risks and mitigations

| Risk | Mitigation |
|---|---|
| G1 fix destabilizes (blanket rooting is load-bearing today) | P0.2 acceptance list (StackOverflow, deltablue, havlak+push BUG-001 repro) before anything builds on it; land alone, soak on full suites |
| JS perf cliffs in Phase 4 (ICs, native specialization, shape caches) | explicit carry-over checklist (P4.3); per-batch perf gates; A/B flag allows instant fallback per module |
| Enum-renumber blast radius (Phase 1) | one commit per language; canonical-dump byte-equivalence harness (P1.4) as the proof instrument |
| Lambda closures on scope-env (P3.1) change observable perf | closure micro-benchmarks before/after; representation is strictly more general, but verify allocation counts |
| Two tracks editing `transpile-mir.cpp` (Stage A ∥ Phases 1–3) | Phase 0 lands first so Stage A codes against the emitter API; file-section ownership agreed per PR |
| Double-boxing v2 lands concurrently | sequenced *after* Phase 0 so it lands once, in the emitter (design risk register) |
| Dual old/new paths linger | rule from §1.2: the phase that introduces a flag deletes the old path before the phase exits |

### Rollback strategy
Every phase's flag (`use_mir_direct`-style) allows reverting a module or the whole pipeline to the previous path until the phase's exit gate; exit gates delete the old path, after which rollback = git revert of the phase branch. Phases are merge-gated, so `master` is always releasable.

---

## 10. Definition of Done (whole plan)

1. `ast-core.hpp` exists; `ast.hpp`/`js_ast.hpp` contain only language-range structs; one node-kind space with core + Lambda + JS(+TS parked) ranges.
2. One `MirEmitter`, one `VarEntry`/scope stack, one const-pool API — `jm_call_*`/`emit_call_*` and both old var machineries deleted.
3. G1 honest rooting shipped and soaked; BUG-001 repro green.
4. One capture analysis, one evidence inference with call-site propagation (on for both languages), JS bound at build time; `InferCtx`/`JsParamEvidence`/`CaptureInfo`/`js_scope.cpp` machinery deleted.
5. Shared driver lowers all core nodes for both languages; JS ICs/native specialization intact (perf suites ≥ pre-migration); per-language handlers only for semantics + language-range nodes.
6. `create_js_import_script` deleted; cross-language imports bind at AST level; live bindings work for Lambda `pub` vars and ES modules.
7. All gates green throughout: lambda baseline 100%, lambda gtest, editor 1931, UI-automation 5714, node-baseline ≥1492, AWFY/LambdaJS perf within noise or better.
8. TS fast path verified working at every phase exit; TS type-aware path compiling (or explicitly gated with a tracking note).
9. Measured outcome recorded: LOC deleted (target: the duplicated analysis/lowering layers — several thousand lines), and a short retro noting anything the Python port (future plan) should know.
