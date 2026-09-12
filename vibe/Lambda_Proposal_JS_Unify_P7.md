# P7 Proposal — Closing the Lambda/LambdaJS Unification

**Date:** 2026-09-11
**Status:** PARTIALLY IMPLEMENTED (2026-09-11). **U-B landed.** **U-A withdrawn** — disproven against the code (§2.1a). U-C landed in reduced scope. **U-D reversed** — it proposed the opposite of the correct direction; now respecified against **D8.2.5v2**/**U30v2** and ready to build. U-E unstarted. Folds into [`Lambda_Design_JS_Unified.md`](Lambda_Design_JS_Unified.md) §4 as phase P7 once accepted.

**Scope:** The four unchecked items in that document's §8 completion definition, restated against the code as it stands on `master` at `9e24208d6`. No semantic change to either language; no vendored dependency touched; no C2MIR extension.

**Formal authority:** **D8.1.3v10**, **D8.2.1–D8.2.4**, **D8.2.5v2**, **D8.2.6**, **D8.6.4v2** in [`doc/Lambda_Formal_Design.md`](../doc/Lambda_Formal_Design.md); **D3.2.3**, **D3.3.1v2**, **D5.3.4**, **D2.4.1–D2.4.3**. **S1.6** and **S3.1–S3.3** in [`doc/Lambda_Formal_Semantics.md`](../doc/Lambda_Formal_Semantics.md). The formal specifications win on disagreement.

---

## 1. Measured state

Counts are physical lines over the **D8.6.4v2** anchored scope (`lambda/runtime` + `lambda/js`, tracked `.c/.cc/.cpp/.h/.hpp`), taken with `utils/check_ast_tune_loc.sh`'s own counting rule.

| Quantity | Value |
|---|---|
| Governed LOC | **298,520** (205 files) — `lambda/js` 175,029, `lambda/runtime` 123,491 |
| D8.6.4v2 cap / target | 308,711 / 308,311 — **gate passes** |
| Lambda MIR lowering | 44,895 (`transpile-mir.cpp` 34,548 + shared emitter/driver) |
| JS MIR lowering | 30,702 (`js_mir_*.cpp`, `transpile_js_mir.cpp`) |
| Shared emitter helpers | 150 `em_*` in `mir_emitter_shared.hpp` |
| `em_*` call sites | Lambda 663, JS 327 |
| Core tags lowered by **both** lanes independently | **25** |
| Core tags interpreted by **both** lanes independently | **23** |

**Correction (2026-09-11).** F3 below first reported "294 name-keyed lookups".
That figure counted `->name->chars` field reads alongside actual lookup calls.
The lookup-call count over `js_mir_*.cpp` is **138**: 81 `strcmp`, 34
`hashmap_get`, 20 `jm_var_scope_*`. Of the 81 `strcmp`, most are module-path
and file-extension tests (`.js`, `.mjs`, `.cjs`, `.json`, `.ls`) that have
nothing to do with binding; the binding-relevant core is 56 comparisons against
three compiler-synthesized pseudo-binding spellings. F3's D8.2.4 point stands,
but its size was overstated by roughly 2x.

The §8 status line in the design doc records governed LOC as 287,618. It is now 298,520. The ratchet still passes against its cap, but the recorded figure is stale and should be refreshed when P7 lands.

### 1.1 What is genuinely shared — keep it

`Item` is the sole value currency (**D1.2**, **S1.6**). Above that, the substrate is real, not nominal:

- `typedef AstNode JsAstNode` and `typedef AstNodeType JsAstNodeType` — one node struct, one 141-entry tag catalog, JS extensions confined to 8 tags at ordinal ≥ 1002.
- `typedef NameScope JsScope` — one scope/`NameEntry` type.
- `struct JsScript : Script` and `struct JsTranspiler : JsScript`, mirroring `struct Transpiler : Script`.
- `AstIndex` — dense node/scope/binding/function/class IDs, parent and child adjacency, owner-function links.
- `MirValue` / `MirEmitter` — `JsMirFunctionEmitter` embeds `MirEmitter em` directly; 990 combined call sites into the 150 shared helpers.
- `CompilerPassManager`, the module registry, `EvalContext` + the context-capsule directory, `RuntimeExecutionScope` / `RuntimeModuleStateScope`, the libuv host loop, the GC heap, and the precise side stacks.

This is the correct **D1.3** boundary and P7 does not disturb it.

### 1.2 What is out of scope — and why

Three large bodies of JS code are *not* unification debt, and should stop being counted as such:

- `js_runtime.cpp` (36,982) and `js_globals.cpp` (18,061) are ECMAScript builtin semantics. §7.7 of the design doc already rejects fusing these with Lambda helpers; **S3.1–S3.3** truthiness alone makes it unsafe.
- The Node compatibility stack — `js_http`/`js_net`/`js_tls`/`js_fs`/`js_stream`/`js_child_process`/`js_buffer`/`js_https`/`js_readline`/`js_node_uv`/`js_event_loop` (37,944 combined) — is Node surface area, not Lambda substrate. Its own consolidation axis is the Jube hosted-Node migration (`lambda/module/node_*` behind `js_fs_service.h` and friends), tracked separately in [`Lambda_Design_Jube_Node_Hosting.md`](Lambda_Design_Jube_Node_Hosting.md). Do not fold it into P7.
- RegExp (`js_bt_regex`, `js_regexp_compile`, `js_regex_wrapper`, generated property tables) is JS-specified matching; Lambda's `re2_wrapper.cpp` is a different engine with different semantics.

---

## 2. Four findings

### F1 — Two structural walkers over one catalog *(withdrawn — see §2.1a)*

Twenty-five core tags are lowered independently by both lanes:

```
ARRAY ARROW_FUNC BINARY BLOCK BREAK_STAM CALL_EXPR CONTINUE_STAM FUNC FUNC_EXPR
IDENT IF_EXPR IMPORT LITERAL LOOP MAP MATCH_EXPR MEMBER_EXPR NULL RAISE_STAM
RETURN_STAM SEQ SPREAD UNARY VARIABLE_DECLARATOR VAR_STAM
```

For each one, both lanes hand-write the same *structure* — child evaluation order, block and frame bracketing, root/final-store sequencing, branch emission, demand propagation — and differ only at the semantic leaf. The sharing that exists is leaf-level (`em_require_rep`, `em_apply_value_demand`, …), not driver-level.

`MirLoweringProfile` is the vehicle **D8.2.6** specifies for exactly this, and it is used at **6 sites in Lambda and 4 in JS**. It is a seam, not a driver.

### 2.1a F1 is wrong: the shared tags do not share structure

F1 inferred structural duplication from tag overlap without checking that the
overlapping dispatch arms share structure. Three families were examined before
attempting any extraction:

- **`AST_NODE_SEQ`** — the tag is shared but the *layout* is not. Lambda uses
  `AstListNode {item}`; JS uses `JsSequenceNode {expressions}`. Extraction
  would first require a P1-style layout repair, not a driver.
- **`AST_NODE_IF_EXPR`** — layouts *are* converged (`JsIfNode : AstIfNode`), but
  the shared skeleton is ~8 lines (lower condition, `BF`, arm, `JMP`, labels)
  wrapped in ~50 lines of per-lane semantics on each side: Lambda carries
  `MirFlowScope`, tail-position tracking, carrier-type join analysis, the
  boxing decision and `proc_discard`; JS carries closure checkpointing, typeof
  narrowing, eval-completion reset, error-lane routing and state bridging, TDZ
  block init, and Annex-B function binding. Sharing 8 lines would cost five
  callbacks — precisely the scaffold §7.4 rejects, and LOC-positive.
- **`AST_NODE_ARRAY`** — both dispatch arms are already single-line delegations
  (`emit_array_storage` vs `jm_emit_array_value`) to builders that construct
  different runtime objects. There is nothing left to extract.

The 25-tag overlap measures **catalog convergence**, which D8.2.1 asks for and
which has already been achieved. It is not a duplication metric. U-A is
therefore withdrawn rather than deferred: the structural sharing that pays has
already been extracted as the 150 `em_*` helpers, and the residue is semantics.

The deferral criterion U-A itself used — thin structure, thick semantics — was
simply applied to the wrong set. IF, LOOP, SEQ, ARRAY belong with BINARY,
UNARY, MEMBER and CALL, not ahead of them.

### F2 — `LangProfile` is a dead vehicle

**D8.2.1** requires that "operator semantics dispatch through the profile". They do not. `LangProfile` has five hooks (`ast-core.hpp:1434`):

| Hook | `lambda_profile` | `js_profile` | Call sites |
|---|---|---|---|
| `validate` | no-op | no-op | **0** |
| `analyze` | no-op | no-op | **0** |
| `lower` | no-op | no-op | **0** |
| `publish_ext_facts` | NULL | wired at `js_scope.cpp:414` | 3 |
| `visit_ext_children` | NULL | wired at `js_scope.cpp:413` | 4 |

Three of five fields are never assigned and never called — dead struct members carried by every profile. The two live hooks serve indexing only. The profile mechanism that F1's fix requires does not yet exist in usable form.

### F3 — Binding identity is published but not consumed

`AstIndex` publishes dense `AstBindingId`s and `node_bindings` edges. JS MIR lowering still performs **294** name-keyed lookups (`hashmap_get` / `strcmp` / `jm_var_scope_*` / `jm_name_set_*`), concentrated in `js_mir_module_batch_lowering.cpp` (42), `js_mir_hashmap_scope_utils.cpp` (22), `js_mir_function_class_lowering.cpp` (17), and `js_mir_expression_lowering.cpp` (15).

Some are keyed on the *source spelling* of a variable — `JsMirTranspiler::widen_to_float` is documented as a "set of variable names". **D8.2.4** is explicit: "lowering consumes resolved identities and never repairs binding." Name-keyed compiler state is how a lowering pass comes to disagree with the builder about shadowing.

### F4 — `AstNodeFacts` is the wrong mechanism *(restated 2026-09-11)*

**F4 originally had the polarity backwards** and is restated here; the full
argument is `Lambda_Design_Unified_AST.md` §13, which drives **D8.2.5v2** and
**U30v2**.

The original F4 claimed inference was corrupting a source-contract slot by
writing into `node->type`, and proposed moving it out. That misreads the
design. `AstNode.type` **is** the effective/inferred type — most expression
nodes carry no source annotation, so there is nothing else it could hold. The
code says so at `NameEntry::declared_type`: "`node->type` is the **effective
compiler type** and historically lost this distinction during declaration
construction". Declared annotations already have their own homes on the
declaring nodes — `NameEntry::declared_type`, `AstNamedNode::declared_type`
(params), `AstDeclaratorNode::declared_type` (let/var) — with ~164 live read
sites.

What is actually wrong is `AstIndex::facts`:

- `declared_contract` duplicates a fact that already lives on the declaring
  node, and is **written once and never read**.
- `inferred_type` duplicates `node->type`, and is **written once to `NULL` and
  never touched**; `representation` likewise (`VALUE_REP_NONE`).
- Only `flags`/`folded_item` are live, serving two sites: the const-fold pass
  in `interp.cpp` writes them, `transpile-mir.cpp` reads them back. That reader
  holds a facts pointer and still reaches past it to `node->type` for its type
  guard.

Classifying inferred type and const results as *erasable* is what justified the
side table. They are not erasable — the runtime operates on them. An effective
type selects the carrier a value materializes in; a folded constant *is* the
value.

**Correction to the earlier count.** F4 first reported "17 `AstNodeFacts`
references". That grep pattern (`->facts`) also matched `pass_manager->facts`
(a compiler-fact bitmask), `ledger->facts` (early-error ledger), and
`scope->facts` (`MirFlowFact`). Real `AstNodeFacts` uses: allocation, init, and
the two const-fold sites. The mechanism is emptier than reported, not fuller.

### F5 — Execution shell sequencing (smaller)

Both lanes already use the shared scoped primitives — `runtime_context_bind_retained`, `RuntimeExecutionScope`, `RuntimeModuleStateScope`, `lambda_module_state_*`. What is duplicated is the *sequence*: `js_mir_entrypoints_require.cpp:1307` and `runner.cpp:1430–1560` each hand-write prepare → bind → activate module state → link → execute → finish-turn → drain, with per-lane hooks interleaved. **D8.1.3v10** correctly keeps two semantic walkers; it does not ask for two shells.

---

## 3. Proposed work

Five items. **U-B** and **U-C** are enablers, **U-A** carries the payoff, **U-D** and **U-E** are independent. Each is separately landable and each must satisfy **D8.6.4v2**'s non-positive phase LOC delta on its own.

### U-B — Make `LangProfile` carry semantics (enabler, small) — **LANDED**

**As built**, simpler than proposed. `MirEmitter` already carries `call_owner`
(the lane transpiler), so the two lowering hooks belong on the emitter itself
and `MirLoweringProfile` disappears rather than being folded into `LangProfile`:

- `MirEmitter` gains `lower_value` and `emit_condition`, wired once per lane at
  emitter init (`transpile-mir.cpp`, `js_mir_hashmap_scope_utils.cpp`).
- `struct MirLoweringProfile` and its 10 ad-hoc construction sites are deleted.
- `em_lower_profile_value(&profile, n, d)` → `em_lower_value(em, n, d)`;
  `em_lower_profile_condition(&profile, n)` → `em_lower_condition(em, n)`.
  The latter takes an optional `variant` hook, because Lambda's loop test
  selects a native comparison lane (`mir_profile_emit_loop_condition`) that the
  general truthiness emitter does not.
- `LangProfile` loses `validate`, `analyze`, and `lower` — all three had zero
  assignments and zero call sites in both profiles — and retains only the two
  live extension-node hooks.

Net **−18 LOC**. Structural helpers can now recurse into language lowering from
anywhere, which was U-A's prerequisite; with U-A withdrawn it remains the right
shape for `em_*` helpers that already need a callback.

### U-B (as originally proposed — superseded)

Delete the three dead hooks. Replace them with the typed semantic questions a shared structural driver must ask, per **D8.2.1**:

```c
typedef struct LangProfile {
    const char* name;
    // structural driver callbacks (D8.2.1: operator semantics dispatch here)
    MirValue (*lower_leaf)(void* owner, AstNode* node, uint32_t demand);
    MirValue (*emit_truth_test)(void* owner, MirValue v);   // S3.1–S3.3 stay lane-owned
    MirValue (*emit_binary)(void* owner, AstNode* n, MirValue l, MirValue r);
    MirValue (*emit_member_get)(void* owner, AstNode* n, MirValue obj, MirValue key);
    // indexing (already live)
    bool (*publish_ext_facts)(AstNode*, struct AstIndex*);
    void (*visit_ext_children)(AstNode*, AstChildVisitor, void*);
} LangProfile;
```

Truthiness must stay behind `emit_truth_test`. Nothing in the shared driver may ever apply Lambda truthiness to a JavaScript value.

**Exit:** both profiles fully populated; zero unassigned hooks; `MirLoweringProfile` folded into `LangProfile` or documented as its per-function instantiation.

### U-A — Promote the structural driver, family by family — **WITHDRAWN (§2.1a)**

Retained below as the original text, so the withdrawal can be audited against
what was actually proposed. It rests on a premise §2.1a disproves.

For each shared family, move the *structure* into one `em_lower_*` in `mir_emitter_shared.hpp` that owns child order, bracketing, rooting, and demand propagation, and calls the U-B hooks for semantics. **D8.2.3**'s extract-after-convergence rule applies: extract only where two working clients already agree.

Order by semantic distance, cheapest first:

1. `BLOCK`, `SEQ`, `SPREAD`, `ARRAY` — pure sequencing.
2. `BREAK_STAM`, `CONTINUE_STAM`, `RETURN_STAM` — completion/label plumbing; JS `finally` interaction stays lane-owned (see the AST-interp/MIR gap ledger).
3. `LOOP`, `IF_EXPR` — structure shared, condition through `emit_truth_test`.
4. `VARIABLE_DECLARATOR`, `VAR_STAM` — already one physical layout after P1b/P1c.

Defer `BINARY`, `UNARY`, `MEMBER_EXPR`, `CALL_EXPR`, `MATCH_EXPR`, `FUNC`, `ARROW_FUNC`, `IMPORT` to a later phase — their structure is thin and their semantics thick, so the extraction would not pay.

**Exit:** each migrated family has one structural implementation; per-family LOC delta non-positive; Lambda baseline, Test262, and forced-GC oracles green with no weakened gate.

### U-C — Retire name-keyed lookup in JS MIR lowering — **LANDED (reduced)**

Scoped down after the F3 correction. What landed is the rule-13 extraction: the
three-way pseudo-binding test was duplicated verbatim in
`js_mir_module_batch_lowering.cpp` beside the existing
`jm_capture_is_lexical_meta_binding` helper, with a two-way variant open-coded
twice more in `js_mir_expression_lowering.cpp`. Both predicates now live in
`js_mir_hashmap_scope_utils.cpp` behind `js_mir_internal.hpp`, sharing one
`_js_` prefix test that rejects an ordinary source name before any full
comparison.

The remaining sentinel comparisons are if/else-if chains that dispatch on
*which* pseudo-binding a capture is. Converting those to a classifier enum was
built and then backed out: it read no better, cost ~20 lines for four call
sites, and rewriting dense capture-emission control flow carries regression
risk disproportionate to a compile-time micro-gain. Interning the three names
and comparing pointers was also rejected — an un-pooled name would silently
compare unequal, turning a slow path into a wrong-answer bug.

What is left of F3 for a future phase is the genuine D8.2.4 item: the 20
`jm_var_scope_*` name-keyed hashmap lookups and the 34 `hashmap_get` sites that
key compiler state on source spelling (`widen_to_float` above all).

### U-C (as originally proposed — superseded)

Convert the 294 sites to `AstBindingId`-keyed dense arrays sourced from `AstIndex`. `widen_to_float`, `module_consts`, `jm_var_scope_*`, and the `jm_name_set_*` family become ID-indexed. `js_mir_hashmap_scope_utils.cpp` (1,202 lines) should shrink substantially or disappear.

This closes **D8.2.4** for the JS lane and removes per-name hashing and interning from the compile path — which is where the **D8.6.4v2** "≥20% lower `test_js_gtest` parse-through-link time" ratchet is most likely to be earned.

**Exit:** zero `strcmp`/spelling-keyed compiler state in `lambda/js/js_mir_*`; `node_bindings` is the only binding authority consulted by lowering.

### U-D — Retire `AstIndex::facts` *(SUPERSEDED 2026-09-11)*

**Superseded by [`Lambda_Design_Runtime_Const.md`](Lambda_Design_Runtime_Const.md).**
U-D kept the existing const-fold and only relocated its result. The fold itself
is the defect: eligibility admits four node shapes and no calls or identifier
reads (so **D6.1.2**'s purity gate never engages); results are restricted by a
type-ID whitelist that groups self-tagged floats with pointer-backed values;
floats are evaluated and then discarded; the fact is read at two boxing sites
only, so `let a = 1 + 2` computes at runtime; and the interpreter that produces
the fact never reads it. Retiring `AstIndex::facts` survives there as **RC12**,
reached as a consequence rather than as the goal.

The original text follows.

Supersedes the original U-D, which proposed the opposite and is recorded as
withdrawn in `Lambda_Design_Unified_AST.md` §13.6. Governed by **D8.2.5v2** and
**U30v2**.

- `node->type` keeps holding the effective/inferred type. Unchanged,
  authoritative. No sweep of its 349 read sites.
- Declared annotations stay on the declaring node/binding, where they are.
- Const-fold results move onto the node's own type: a folded node gets a freshly
  allocated literal `TypeConst` subclass (`TypeFloat`, `TypeInt64`,
  `TypeNumSized`, …) carrying the value and `const_index`, with the existing
  `is_const`/`is_literal` bits. This is already the builder's representation for
  source literals (`build_ast.cpp:3540`); folding just produces one later.
- `AstIndex::facts` and `struct AstNodeFacts` are deleted, along with their
  allocation, growth `memcpy`, and init block.
- Genuinely erasable optimization facts, if any survive, hang off a lazily
  allocated record behind a pointer in `Type`.

**Design constraint.** `Type*` is not uniformly per-node: `set_type_any()`
returns `&TYPE_ANY`, a process-global singleton, as do `TYPE_ANY_NO_ERROR` and
`LIT_TYPE_ANY`. The lazy side record may therefore hold only facts true of *the
type*, never of *the node that happens to have it*. Const-folding satisfies this
because it allocates a fresh `TypeConst` anyway.

**Exit:** `AstNodeFacts` gone; const-fold round-trips through the node's literal
type with Lambda baseline and `test_js_gtest` unchanged; no consumer reads a
per-node fact table. Expected LOC delta strongly negative.

### U-E — One execution shell

Extract `script_execution_run(Script*, const LangProfile*, ...)` owning the prepare → bind → activate → link → execute → finish-turn → drain sequence, with lane hooks for realm init (`js_runtime_state_init`), loop init (`js_event_loop_init`), and turn completion (`js_mir_finish_script_turn`). Both interpreters stay exactly as they are — **D8.1.3v10** is not being renegotiated.

**Exit:** §8 item "share one execution shell and runtime substrate under **D8.1.3v10**" checked; one sequence, two profiles.

---

## 4. Gates

Unchanged from the design doc §5, restated for P7:

- **LOC:** every item's governed, changed-C/C++, and changed-source deltas non-positive (`utils/check_ast_tune_loc.sh --base <anchor> --phase-base HEAD`). Comment stripping, reformatting, and moving code out of scope cannot satisfy it (**D8.6.4v2**, and CLAUDE.md rule 19).
- **Compiler time:** median of five release-mode runs after one warm-up; U-C and U-A carry the ≥10% Lambda / ≥20% JS parse-through-link ratchets.
- **MIR volume:** per-test machine-readable record on identical manifests; growth attributed, not waived.
- **Semantics:** `make test-lambda-baseline` 100%, complete Test262 baseline, forced-GC oracles (`LAMBDA_GC_FORCE_EVERY=1` + `POISON_FREED=1`), no masked or skipped test. Per CLAUDE.md rule 18, a failing or hanging Test262 row is investigated, never silenced.

## 5. Explicitly rejected

- Merging the two AST interpreters (**D8.1.3v10**, §7.1).
- One parser or CST (§7.2); JS keeps its first-party C lexer/parser.
- Tree rewriting or desugaring as a unification device (**D8.2.2**, §7.3).
- Unifying ECMAScript builtins with Lambda helpers (§7.7).
- Any scaffold-first common compiler that adds a layer before it removes one (§7.4) — F2 is what that produces.

## 5a. Implementation record (2026-09-11)

Base `9e24208d6`, governed LOC 298,520 → **298,512** (**−8**, non-positive gate
satisfied). Files touched: `mir_emitter_shared.hpp`, `ast-core.hpp`,
`transpile-mir.cpp`, `js_mir_expression_lowering.cpp`,
`js_mir_hashmap_scope_utils.cpp`, `js_mir_module_batch_lowering.cpp`,
`js_mir_internal.hpp`.

Verification, both before and after, on the same build:

- `make test-lambda-baseline` — 5,345/5,351. The 6 failures
  (`tune21_index_mul_hoist`, `tune25_loop_calls`, `shared_loop_effects`,
  `lambda_tune4_typed_array_guard`, `hljs_highlight`,
  `proc_interp_var_writeback`) were confirmed failing on a clean stashed
  baseline with freshly built test binaries. Pre-existing, unchanged set.
- `test_js_gtest` — 504/505, the one failure being the same pre-existing
  `hljs_highlight`.
- Build warnings unchanged; the one warning in a touched file
  (`transpile-mir.cpp:18124`, unused `container_tid`) sits outside every diff
  hunk and is pre-existing.

Compiler-time and MIR-volume ratchets under **D8.6.4v2** were **not** measured:
both require release-mode medians over five runs, and neither U-B nor the
reduced U-C is expected to move them. They remain owed if this phase is
credited against those ratchets.

## 6. Open questions for the user

1. **U-A.** Withdrawn per §2.1a. Confirm withdrawal, or name a specific family
   where the structure is thick enough to pay.
2. **U-D sequencing.** The `node->type` sweep touches Lambda far more than JS and overlaps the TIG1 work already tracked in the type-inference ledger. Should U-D land inside P7, or be handed to that ledger and only *referenced* here?
3. **Stale §8 figure.** The design doc records 287,618; actual is now 298,512.
   Refresh it as part of crediting this phase, or as a standalone correction?
4. **Remaining scope.** U-D is now specified against **D8.2.5v2** and ready to
   build. U-E remains unstarted and unverified — F5 was measured by grep only,
   and both §2.1a and §13.6 of the Unified-AST record are cautions against
   trusting that. Probe it against the code before committing to it.
