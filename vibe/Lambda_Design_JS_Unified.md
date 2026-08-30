# LambdaJS and Lambda Runtime Unification

**Date:** 2026-08-29

**Status:** ACTIVE — P0a/P0b gates, P1a–P1e core-layout migrations, P2a–P2c shared identity publication/lowering, P3a–P3j compile-unit migrations, P4a–P4l indexed function facts, P5 semantic-family MIR lowering, and P6 shared execution/runtime lifecycle are fully implemented and verified. The P5/P6 review base and candidate each governed 326,064 `lambda/runtime` + `lambda/js` lines; the post-P6 direct-frontend retirement reduces that scope to 318,980 (`-7,084`). Proposal-wide structural convergence and the historical 308,711-line project target remain open.

**Scope:** The Lambda and LambdaJS AST builders, binding/indexing, compiler pass process, MIR lowering, AST interpreters, and shared runtime substrate. This document does not change either language's semantics, does not extend C2MIR, and does not modify a vendored dependency.

**Formal authority:** **S1.6**, **S1.11**, and **S3.1–S3.3** in [`doc/Lambda_Formal_Semantics.md`](../doc/Lambda_Formal_Semantics.md); **D1.2–D1.6**, **D1.8–D1.10**, **D2.4.1–D2.4.3**, **D3.2.3**, **D3.3.1v2**, **D5.3.4**, **D8.1.1v5**, **D8.1.3v10**, **D8.2.1–D8.2.6**, **D8.4.1v2**, **D8.4.3v2**, and **D8.6.1–D8.6.4v2** in [`doc/Lambda_Formal_Design.md`](../doc/Lambda_Formal_Design.md). The formal specifications win on disagreement.

**Design lineage:** This document specializes the already-confirmed compiler-consolidation rulings **U27–U36** in [`Lambda_Design_Unified_AST.md`](Lambda_Design_Unified_AST.md). It does not create a new ruling series or replace that document's language-neutral catalog. It narrows the next implementation to Lambda and LambdaJS, records the live structural defects that must be removed, and adds the phase-local LOC conservation rule requested on 2026-08-28.

**Related designs:** [`Lambda_Design_JS_Interpreter.md`](Lambda_Design_JS_Interpreter.md), [`Lambda_Design_Ast_Interpreter.md`](Lambda_Design_Ast_Interpreter.md), [`Lambda_Design_Runtime_Error_Handling.md`](Lambda_Design_Runtime_Error_Handling.md), [`Lambda_Design_Stack_Frame_JS.md`](Lambda_Design_Stack_Frame_JS.md), and [`vibe/impl/Lambda_Impl_Tune_Ast (retired).md`](<impl/Lambda_Impl_Tune_Ast (retired).md>). The last file is historical partial implementation evidence; its unchecked closeout items are carried forward here and it is not an active plan.

---

## 1. Decision

Lambda and LambdaJS will share one structured compiler process and one runtime substrate while retaining separate language semantics.

The end state is:

```text
Lambda source -> first-party Lambda parser -> Lambda syntax builder --+
                                                                    |
JS source     -> first-party JS/TS C parser -> JS syntax builder ---+
                                                                    v
                                                        CompilationUnit
                                      build -> bind -> validate -> index
                                      -> capture/effect/type/rep analysis
                                      -> function plan -> backend selection
                                                    /              \
                                      profile T0 interpreter     shared MIR driver
                                                    \              /
                                                     Runtime/EvalContext
                                         Item + precise GC + modules + async host
```

The common process owns structure, identities, fact lifetime, pass ordering, representation flow, rooting, module activation, and execution entry/exit. Language profiles own semantic decisions. This division follows **D1.3**: guests reuse contracts below the semantic boundary and do not inherit another language's truthiness, coercion, or object model. It also follows **S1.11**: hosted LambdaJS follows ECMAScript even where Lambda deliberately differs.

This is a consolidation, not a parallel replacement. Every landed phase removes the old implementation in the same phase that introduces its replacement.

### 1.1 Hard outcome

The completed work must remove at least **2,000 additional net physical C/C++ lines** from the fixed **D8.6.4v2** scope:

```text
lambda/runtime/**.{c,cc,cpp,h,hpp}
lambda/js/**.{c,cc,cpp,h,hpp}
```

This proposal freezes a project-specific unification anchor at commit `44b98dcebd19a548a14bbb75785091b545445f00`, measured at 310,711 lines. The final candidate must therefore be at most **308,711 lines**. It should reach at most **308,311 lines**, leaving a 400-line buffer against late correctness or platform work.

The older formal anchor remains commit `e66e5b5c71bc7ee7fe2d1e2b2a9afe27dc6825a3`, measured at 319,606 lines. Its final cap remains 317,606 lines, checked by the existing authority:

```sh
./utils/check_ast_tune_loc.sh --base e66e5b5c71bc7ee7fe2d1e2b2a9afe27dc6825a3
```

At proposal time the formal checker reported 310,711 candidate lines, already 8,895 below that older anchor, and the worktree had no tracked change in the governed scope. That earlier reduction does **not** count toward this proposal. Both the new 308,711 project cap and the older 317,606 formal cap must pass.

### 1.2 Phase-local LOC conservation

The final LOC ratchet is supplemented by a stricter migration invariant:

> **Every mergeable phase and every review-sized subphase has a non-positive C/C++ LOC delta. New code is funded by deletion in that same phase. No phase borrows deletion from a future phase.**

Three counters enforce the rule:

1. **Governed-scope physical LOC:** the total physical C/C++ lines under `lambda/runtime` and `lambda/js` after the phase must be no greater than immediately before that phase.
2. **Changed first-party C/C++ LOC:** across every first-party C/C++ file changed by the phase, including tests and utilities, added physical lines must not exceed removed physical lines.
3. **Changed first-party source LOC:** across all hand-authored implementation, test, build, and check sources changed by the phase, added physical lines must not exceed removed physical lines. This includes C/C++ headers and sources, `.inc`/`.inl`, shell/Python/JavaScript/TypeScript/Lambda scripts, and hand-authored make/build sources. Documentation, data manifests, and expected-output/golden fixtures are reported separately; they neither consume this source-code counter nor earn deletion credit.

Therefore:

```text
phase_governed_delta <= 0
phase_changed_cpp_delta <= 0
phase_changed_source_delta <= 0
final_governed_delta_from_project_anchor <= -2000
final_governed_delta_from_formal_anchor <= -2000
final_credited_consolidation_delta <= -2000
```

This is deliberately stricter than a final-only budget. A common file, pass, helper, test harness, adapter, or runtime service may be created only when the old code it replaces is deleted in the same phase. New test source counts too and must be paid for by production or obsolete-test deletion in that phase.

An implementation may use temporary parallel code in an uncommitted working tree to establish equivalence. It may not land both paths. A feature flag, forwarding wrapper, compatibility alias, or dormant scaffold does not count as retirement while the old implementation remains compiled or callable.

Earlier surplus deletion may improve the final buffer, but it does not authorize a later positive phase. Each phase independently remains flat or negative. Deletion before the frozen project anchor is historical and earns no project credit.

### 1.3 What counts as retirement

The deletion ledger must name removed symbols, switches, tables, fields, files, or entry paths and identify the surviving authority. These count:

- deleting a private traversal after all callers use the common traversal;
- deleting a pointer index after all callers use stable IDs;
- deleting binding repair after lowering consumes `BindingId`;
- deleting duplicate compile/link/cleanup paths after one driver owns them;
- deleting old lowering cases after the shared handler is the sole implementation;
- deleting duplicated runtime lifecycle or root-range machinery after promotion to the runtime substrate.

These do not satisfy the gate:

- moving code outside `lambda/runtime` or `lambda/js`;
- deleting comments, blank lines, diagnostics, assertions, or required ownership explanations;
- compacting formatting or joining statements;
- generated-file churn;
- excluding a file from the counter;
- weakening tests, manifests, MIR budgets, or Test262 runner behavior;
- keeping the old implementation under a flag or alternate entry point.

Each ledger row records the physical lines removed with the retired implementation, the replacement lines added for that behavior, and the credited net. At final closeout the sum of those credited consolidation rows must be at most `-2,000`. Concurrent or unrelated deletion may improve the physical cap but earns no consolidation credit. Formatting-only hunks and code movement earn none.

The numeric LOC result and the quantified semantic deletion ledger must both pass. **D8.6.4v2** explicitly rejects formatting, comment stripping, and scope-escape as substitutes for consolidation.

---

## 2. Present State and Root Causes

The repository has already unified the value model and much of the physical compiler substrate. The remaining duplication is not caused by JavaScript needing a second runtime. It is caused by shared structures that are not yet authoritative across passes.

### 2.1 Already shared and retained

The following are correct foundations and remain:

- `Item` is the sole cross-language value currency (**D1.2**, **S1.6**).
- `Script` owns source, AST/index, module identity, interpreter state, and derived MIR artifacts; `JsScript : Script` adds JS semantic facts.
- `EvalContext` owns the heap, precise side stacks, active module state, module slabs, scheduler, and the JS semantic-state capsule.
- `AstNodeType`, most AST layouts, `FnAnalysis`, `AstIndex`, `MirValue`, and `MirEmitter` are physically shared.
- the module registry and module-state IDs are cross-language authorities.
- JS AST and MIR functions use the same `JsFunction::invoke` and `construct` capabilities and the same JS property/call/iterator/class helpers.
- Lambda and JS async work use the same libuv host loop and the runtime-owned `RuntimeAsyncDeque` storage primitive, while JS retains its required queue ordering.

This is the correct **D1.3** boundary. The proposal does not flatten `JsRuntimeState` into Lambda semantics or make Lambda maps behave like JavaScript objects.

### 2.2 Shared tags still have incompatible meanings or layouts

The common node catalog is only partially converged.

P1a repaired the iterator-loop example: JavaScript alone initially published C-style `AST_NODE_FOR_STAM`/`AstForStmtNode`, while Lambda iterator loops published `AST_NODE_FOR_EXPR`/`AstForNode`; the procedural spelling was a `discard_result` form flag. P1d subsequently replaced the condition-loop tag with `AST_NODE_LOOP`/`AstLoopControlNode`; the P1a record is retained as the historical intermediate, not as a current tag contract.

P1b repaired the declaration example: JavaScript alone publishes `AST_NODE_ASSIGN` as `AstAssignNode {left, right, op}`, while Lambda declarations now publish `AST_NODE_VARIABLE_DECLARATOR`/`AstDeclaratorNode {id, init, ts_type, name, entry, ...}`. A declaration owns a real identifier child and initializer, rather than borrowing the JavaScript assignment layout. P1c extends that physical contract to Lambda's procedural assignment spellings: `AST_NODE_ASSIGN_STAM`, `AST_NODE_INDEX_ASSIGN_STAM`, and `AST_NODE_MEMBER_ASSIGN_STAM` now use the same `AstAssignNode` storage as JavaScript assignment expressions; only the node tag and Lambda semantic metadata distinguish the statement forms.

`interp_visit_children()` now delegates all catalogued core edges to `ast_visit_core_children()` and retains only Lambda-extension layouts (including the still-extension-owned `AstListNode`). JavaScript retains its extension visitor until its remaining families are migrated, but the shared assignment tag no longer needs a language-dependent cast.

Before P1b, the language-dependent assignment interpretation violated **D8.2.2** and **D8.2.4**. Language is a compilation-unit property, but a core tag must still have one physical contract. A profile may contribute extension-node children; it may not reinterpret a core layout.

### 2.3 Binding is decided, rediscovered, and repaired

The JS builder predeclares bindings, looks up identifiers during CST construction, and writes `AstIdentNode::entry` and `AstNode::type`. The indexed post-build pass now publishes dense binding, scope, and class IDs; MIR lowering consumes those IDs instead of rebuilding compiler scope state. Builder lookup remains name-based only while the CST is being constructed.

Lambda likewise performs substantial binding and inference during direct AST reduction. Both lanes use `NameScope` and `NameEntry`, but neither exposes the complete stable identity graph required by **D8.2.4**.

This has four costs:

- lowering can disagree with the builder about shadowing;
- capture, call, and direct-call analyses rescan by pointer or spelling;
- builder-time inferred types become difficult to distinguish from source contracts, contrary to **D3.2.3**;
- interpreter and MIR backends can grow separate fixes for the same binding defect.

### 2.4 `AstIndex` is a scaffold, not yet the authority

The live `AstIndex` assigns dense node, function, scope, binding, and class IDs and records parent and owner-function links. Common AST scopes, JavaScript extension scopes, resolved `NameEntry` bindings, class nodes, and node-to-binding use edges are published through one table; binding definitions resolve through the same indexed identity. Function IDs are consumed by JS MIR; binding IDs are consumed by direct-call and Test262 assertion lowering.

JavaScript separately counts functions/classes, walks again to populate exact-sized metadata, builds a pointer-keyed function index, and stores another `FnAnalysis` inside `JsFuncCollected`. The comment in `transpile_js_mir_ast()` explicitly treats the shared function count as only an upper-bound hint.

This is the duplication that the index should delete, not coexist with.

### 2.5 Pass facts are only partly authoritative

`build_js_ast_indexed()` now runs a required validation pass before index publication. The pass manager marks `VALIDATED` only when `js_check_early_errors()` succeeds; validation failures retain the AST for the caller's existing error lane but do not publish an indexed unit.

This is the first truthful **D8.2.5** slice, not yet the production schedule. Only JavaScript validation/indexing is registered; build/bind, Lambda validation/indexing, and the large numbered analysis/lowering sequence remain manually ordered.

### 2.6 `MirValue` exists around a bare-register core

`MirValue` already carries the physical register, full type contract, semantic type, representation, provenance, demand, rooting home, scalar home, and pending-completion lane. `MirEmitter` already owns common frame, root, representation-conversion, call-effect, and finalization machinery.

The principal Lambda and JS expression functions still return `MIR_reg_t`. Consumers consequently re-derive whether a register is boxed, native, rooted, scalar-backed, discarded, or branch-only. This is the unfinished **D2.4.1–D2.4.3** and **D8.2.6** migration.

### 2.7 Execution is more unified than compilation

The two AST interpreters correctly keep separate semantic frames:

- Lambda has slot-planned `InterpFrame` records and `EvalSignal`.
- JavaScript has `JsInterpFrame`, traced lexical environments, references, labels, `this`/`new.target`/home-class state, and JS completion kinds including throw, yield, and await.

**D8.1.3v10** explicitly requires separate semantic walkers and activation records while sharing the runtime, heap, event loop, module registry, module state, and call kernels. A single semantic interpreter switch would add language branches to nearly every node and would risk applying Lambda truthiness (**S3.1–S3.3**) to JavaScript.

The duplication worth removing here is the execution shell: context preparation, current-file and module-state guards, root setup, execution-turn lifecycle, result publication, event-loop ownership, and cleanup.

---

## 3. Target Architecture

### 3.1 Syntax adapters remain language-specific

Lambda retains its first-party lexer and recursive-descent/Pratt parser under **D8.1.1v5**. JavaScript and TypeScript use their first-party C lexer and recursive-descent/Pratt parser under **D8.1.3v10**; their vendored Tree-sitter grammars are unchanged and isolated to `lambda-cst`. No vendor source is modified.

The builders share only mechanics that are independent of grammar shape:

- pool-backed AST allocation and source spans;
- intrusive-list append and owned-child construction;
- diagnostic publication;
- declaration and pattern construction primitives;
- publication into the common compilation unit.

The builders stop performing optimization analysis. They may collect the syntax needed by binding, but final reference identity, inferred type, representation, captures, call graph, and function plan are post-build facts.

### 3.2 One truthful core-node catalog

Every core `AstNodeType` has one layout and one child contract.

The migration follows the already-confirmed **D8.2.2** / U19–U24 design:

- Lambda declarations use `AST_VAR_DECL + AST_DECLARATOR`; `AST_ASSIGN` is the shared target/value assignment node.
- condition loops use `AST_LOOP {LoopForm}`; JavaScript `for(;;)` is `LoopForm::FOR_C`.
- query/iterator `for` remains the separate `AST_FOR` family because its semantic color differs from condition loops.
- `AST_IF`, `AST_MATCH`, and `AST_ASSIGN` retain syntax variance in fields/forms, not tree rewriting.
- labels are fields on labelable core nodes rather than a second wrapper shape.
- JS-only structures remain in the JS extension range and are exposed solely by `visit_ext_children()`.

One catalog completeness test constructs every core form and verifies that the shared visitor reports every owned child exactly once and never reports a non-child field. The test extends an existing compiler/AST test executable; a new executable is not created unless an obsolete one is retired in the same phase.

### 3.3 One indexed compilation unit

The owner is conceptually:

```c
struct CompilationUnit {
    Script* script;
    const LangProfile* profile;
    AstNode* root;
    AstIndex index;
    CompilationFacts facts;
    CompilerDiagnostics diagnostics;
    LambdaCompilerTiming timing;
    CompilerArtifact artifact;
};
```

Names are illustrative; the implementation may extend `Script` rather than add a wrapper if that removes more code and preserves ownership.

The index assigns:

- `NodeId`
- `ScopeId`
- `BindingId`
- `FunctionId`
- `ClassId`

It records:

- parent and owned-child edges;
- owning scope and function;
- declaration/reference-to-binding links;
- use/definition lists;
- function/class direct indexes;
- nested-function, call, return, await/yield, and capture-input lists;
- compact call/effect graph adjacency.

Pointers remain valid arena-local conveniences. Cross-pass identity uses IDs. Lowering never searches a source spelling or repairs a binding.

### 3.4 Shared binding engine, language-owned binding policy

The engine provides scope creation, stable IDs, declaration insertion, reference linking, use/definition publication, and diagnostics plumbing.

The profiles decide:

| Question | Lambda policy | JavaScript policy |
|---|---|---|
| declaration kinds | `let`, `var`, `pub`, params, `fn`/`pn`, types/imports | `var`, `let`, `const`, function/class/import/catch bindings |
| scope selection | Lambda lexical/procedural/module rules | function/global/module/block/class/catch scopes |
| predeclaration | Lambda forward declarations and imports | hoisting, lexical predeclaration, Annex B |
| read-before-init | Lambda contract | TDZ and unresolvable-reference rules |
| dynamic lookup | none unless explicitly designed | direct eval and `with` environment boundaries |
| mutability | Lambda declaration and purity rules | const/immutable import and ordinary mutable binding rules |

The shared engine executes one bind schedule; the profile supplies answers. Builder-local lookup caches and MIR-time repair disappear after migration.

### 3.5 One fact graph and pass process

Facts have one authority:

| Fact | Authority |
|---|---|
| source form and declared `Type*` | immutable AST |
| resolved node/scope/binding/function/class identity | `AstIndex` |
| inferred/effective type | ID-keyed node/binding facts |
| captures, effects, call graph | ID-keyed function facts |
| representation and entry plan | lowering/function-plan facts |
| constant result | erasable const-analysis fact |
| requested result demand | lowering input, never semantic truth |

The pass order is exactly the **D8.2.5** order:

```text
build
  -> bind
  -> validate
  -> index/call graph
  -> capture/effect analysis
  -> type/representation inference
  -> function planning
  -> MIR lowering
  -> emitter finalization
  -> module finalization/link
```

Each pass declares required, produced, and invalidated facts and returns an explicit status. `PASS_NOT_APPLICABLE` is recorded; a no-op hook does not silently claim a produced fact. Timing is owned by the pass manager rather than hand-placed entry-point timestamps.

JavaScript early errors remain JavaScript rules but run as the profile's validation pass. Lambda safety/type validation remains Lambda policy but occupies the same stage.

### 3.6 One function-analysis authority

`FunctionId` indexes the common `FnAnalysis` and `FnVariantAnalysis`. A profile extension pointer carries only language-specific facts.

JavaScript retains facts such as:

- strictness and dynamic-scope use;
- `arguments`, `this`, and `new.target` observation;
- class/home-object/private-name ownership;
- JS lexical-environment layout;
- generator/async resume requirements;
- constructor field planning where it remains a valid optimization.

The following are retired:

- `func_index_nodes` and `func_index_ids`;
- duplicate `JsFuncCollected::analysis` storage;
- count-then-fill function identity walks;
- repeated pointer/name lookup for direct calls;
- per-walker scans for calls, returns, captures, `arguments`, and suspension points where the index already supplies the set.

Graph propagation uses adjacency lists and worklists rather than repeated whole-tree scans.

### 3.7 Shared structural MIR driver, profile semantic handlers

The common driver owns structural evaluation order and demands for core nodes. It does not own language meaning.

Shared responsibilities include:

- sequencing and child evaluation order declared by the core node contract;
- destination and representation demand propagation;
- common call argument/root plumbing;
- binding and module-slot access by stable ID;
- common control-flow block/label construction;
- representation conversion through `em_require_rep()`;
- call effects, explicit completion checks, root publication, final stores, and emitter finalization.

Profile handlers retain:

- operator meaning and semantic coercion;
- truthiness and conditional conversion;
- member/index/reference semantics;
- call and construct semantics;
- Lambda error routing versus JavaScript throw/try/finally routing;
- JavaScript classes, private fields, `super`, `eval`, `with`, generators, and async semantics;
- Lambda query clauses, elements, patterns, pipes, views, and other true extensions.

Every core expression returns the full `MirValue` required by **D2.4.1–D2.4.3** and **D8.2.6**. Demands are `DISCARD`, `ANY`, `REQUIRED_REP`, `DEST_REG`, and `BRANCH`. Unsupported demands fall back to boxed `ANY`; they never change semantics. Root and final-store ownership remains exclusive to `MirEmitter` under **D5.3.4**.

Per **D8.4.1v2**, neither language gains inline caches. JavaScript property operations continue through the shared JS reference/property kernels; TypeMap shape metadata remains ordinary lookup data.

### 3.8 Shared execution shell, separate interpreters

One execution service owns:

- canonical `EvalContext` preparation and TLS binding;
- current-file and active-module-state restoration;
- module namespace activation;
- root-frame and result-home setup;
- execution-turn nesting and outermost-turn detection;
- event-loop/scheduler start, drain, and teardown policy;
- result/error publication and common fault containment;
- tracing, counters, and backend diagnostics.

Lambda and JavaScript retain their own frame payloads, lexical environments, references, and completion enums. A shared completion carrier may be extracted only if its payload is always precisely rooted and each profile keeps its own semantic kind. No shared carrier may reintroduce a pending-exception side channel; **D1.4v3** and **D8.4.3v2** require explicit returned completion through every frame.

### 3.9 Runtime services promoted only below the semantic boundary

The following are appropriate runtime promotions when they immediately retire two or more live implementations:

- promote the registration/epoch/reset contract of `JsRootRange` into a context-owned `RuntimeRootRange` only when the same slice replaces the manual registered-range lifecycle in `runtime-state.cpp`; if that second client is not structurally equivalent, `JsRootRange` stays JS-owned and no rename/move lands;
- keep `RuntimeAsyncDeque` as common storage while profiles own queue ordering;
- use one module activation and module-state growth API for both languages;
- use one execution-turn guard and current-file/module-state restoration service;
- retain one module registry with profile-owned extension state and precise trace/destroy hooks.

The following remain JavaScript-owned:

- realm/global/intrinsic/prototype identity;
- property descriptors, prototype traversal, proxies, private brands, and JS references;
- lexical/object environment records and TDZ;
- Promise, nextTick, microtask, and JavaScript job ordering;
- `this`, `super`, `new.target`, construct, generator, and async-function semantics;
- Node/Web compatibility caches and host APIs.

`JsRuntimeState` remains a context-owned semantic capsule. It may be split into coherent JS modules, but its fields are not moved into generic `EvalContext` merely to reduce the visible size of a JS header.

---

## 4. Atomic Migration Phases

Every phase may be divided into smaller review slices. Every slice inherits all phase gates, especially all three non-positive LOC checks. A slice that introduces a replacement without deleting its predecessor does not land.

The cumulative numbers below are measured from the project-specific anchor, not the older formal anchor. They are planning targets, not permission to grow in another phase. The hard rules remain phase delta `<= 0` and final project delta `<= -2,000`.

| Phase | Consolidation | Same-phase retirement | Planned cumulative governed delta |
|---|---|---|---:|
| P0 | P0a counters and P0b catalog/timing gates complete | stale assertions/helpers, historical row-count literals, and superseded catalog test code | `0` |
| P1 | P1a iterator-for, P1b declarator, P1c assignment/declaration-wrapper, P1d condition-loop layouts, and P1e shared child ownership complete | private loop-layout casts, Lambda declaration-as-assignment casts, duplicate assignment/wrapper structs, old condition-loop tags, duplicated JS core-child rows, and migrated core-child cases | `-110` |
| P2 | P2a/P2b source and synthetic `FunctionId` authority plus P2c binding/scope/class identity, use/definition edges, and extension-scope publication landed | lowering name repair, pointer indexes, builder lookup cache, repeated identity walks | `<= -500` |
| P3 | one compilation unit, typed pass schedule, and one compile lifecycle | duplicated parse/build/validate/index/link/cleanup orchestration and false fact publication | `<= -800` |
| P4 | P4a–P4b collection-owned function facts; then `FunctionId`-owned analysis and graph worklists | duplicate strictness/direct-eval scans, duplicate `JsFuncCollected` analysis, count/fill scans, per-pass AST caches and propagation loops | `<= -1,200` |
| P5 | full-contract `MirValue` core and shared structural MIR lowering | corresponding Lambda/JS bare-register cases, duplicate structural statement/expression/call plumbing | `<= -2,100` |
| P6 | shared execution shell and runtime root/module activation services | duplicate AST/MIR context guards, JS-only generic root-range machinery, obsolete aliases and cleanup paths | `<= -2,400` |

### 4.1 P0 — Counter and test ratchets without scaffolding

P0 does not add a parallel compiler abstraction or reset the project anchor.

It:

1. verifies the frozen project anchor and records the first review-slice base plus all three LOC counters;
2. extends the existing LOC checker rather than creating a second tool, including self-contained creation of its `./temp/ast_tune/` working directory and separate project-anchor, formal-anchor, and phase-base reports;
3. adds core-catalog completeness assertions to an existing AST/compiler test harness;
4. records exact current Lambda and JS compiler timing manifests under the existing **D8.6.4v2** protocol;
5. deletes enough obsolete assertion/helper/checker code in the same slice to keep all three counters non-positive.

If the catalog test requires more code than can honestly be retired in P0, the test lands with the first P1 node-family migration that deletes the old traversal cases. A test-only positive phase is forbidden.

#### P0a implementation record — 2026-08-28

P0a replaced the formal-anchor-only behavior in `utils/check_ast_tune_loc.sh`; it did not add a second checker. The surviving checker now creates `./temp/ast_tune/` itself, retains the fixed 319,606-line formal-anchor assertion, accepts a project cap through `--cap`, and reports/enforces changed C/C++ and hand-source deltas through `--phase-base`. A phase report refuses to run while an untracked source file exists, so a new file cannot silently evade the source counter.

The retired code was the one-anchor, fixed-cap checker path. The phase report recorded `added=31`, `removed=34`, `delta=-3` source lines; C/C++ was `added=0`, `removed=0`, `delta=0`. The governed scope remained 310,711 lines, exactly equal to the frozen project anchor. Thus P0a is non-positive on all three counters, as required by **D8.6.4v2**.

Verified commands and results:

```sh
./utils/check_ast_tune_loc.sh --base e66e5b5c71bc7ee7fe2d1e2b2a9afe27dc6825a3
# AST_TUNE_LOC baseline=319606 candidate=310711 delta=-8895 threshold=317606

./utils/check_ast_tune_loc.sh \
  --base 44b98dcebd19a548a14bbb75785091b545445f00 \
  --cap 310711 --phase-base 44b98dcebd19a548a14bbb75785091b545445f00
# AST_TUNE_LOC baseline=310711 candidate=310711 delta=0 threshold=310711
# AST_TUNE_PHASE_CPP ... added=0 removed=0 delta=0
# AST_TUNE_PHASE_SOURCE ... added=31 removed=34 delta=-3
```

The final project cap of 308,711 is intentionally not claimed: the current candidate is 312,216 after P4a (312,260 after P1e). P0b now records a complete release-host capture for the current baseline lane; the formal compiler-time ratchets remain open until an unchanged-tree formal baseline is captured for comparison. The catalog-completeness test was funded by the P1e node-family deletion, where its assertion landed with the retired child rows.

#### P0b implementation record — release timing manifests and live-corpus ratchet, 2026-08-29

The timing capture utility had two stale historical assumptions: fixed 698/324
row counts, and an unqualified JS run that included the three extended library
fixtures excluded by the production `--baseline` lane. P0b retires those
assumptions. Each capture now derives the required row count from the exact
filtered GTest manifest, while JS captures pass `--baseline`; Lambda retains its
full corpus. This keeps completeness fail-closed as the repository corpus grows
and does not weaken the existing failed-record checks. The utility diff is
`+5/-5` source lines (`0` net), so all three D8.6.4v2 LOC counters remain
non-positive.

Release-mode test targets were built after the release host:

```text
make build-release-compile
make -C build/premake config=release_native test_lambda_gtest test_js_gtest -j8 CC=clang CXX=clang++
```

The complete one-warm-up/five-run capture is retained under
`./temp/ast_tune/p1e_release/` (the tree was dirty only because of the tracked
P1e/P0b edits). Every measured record was cold, status `0`, complete, and had a
matching sorted sample manifest across all five runs:

| suite | samples | run totals (us) | median (us) | run-0 MIR instructions |
|---|---:|---|---:|---:|
| Lambda baseline | 767 | 5,308,963 / 5,252,807 / 5,291,808 / 5,189,950 / 5,406,437 | 5,291,808 | n/a |
| JS baseline (`--baseline`) | 340 | 196,595,270 / 206,217,202 / 206,229,646 / 211,457,713 / 185,232,717 | 206,217,202 | 5,964,634 (large-library cohort 4,846,800) |

These are current-candidate manifests, not a claimed performance win: the
required formal-anchor comparison still needs a clean historical release
capture. `make test-lambda-baseline` remains the semantic gate and is recorded
below; no test harness or fixture was changed to admit the excluded
`lib_tom_select` case.

### 4.2 P1 — Repair the core AST contract one family at a time

#### P1a implementation record — iterator `for`, 2026-08-28

**D8.2.2** requires syntax variance to live in fields/flags rather than reinterpret a core tag. Lambda's iterator `for` previously allocated `AstForNode` under `AST_NODE_FOR_STAM`, even though that tag's sole common child contract is the JavaScript C-style `AstForStmtNode {init, test, update, body}`. This made every Lambda private traversal either cast a JS tag as `AstForNode` or avoid the common walk.

P1a made `AST_NODE_FOR_STAM` exclusively the JS C-style loop tag. The Lambda builder then published iterator loops as `AST_NODE_FOR_EXPR`/`AstForNode`, with procedural stream discard as a form flag. P1d retired that intermediate C-style tag in favor of the shared condition-loop contract. The Lambda builder, MIR lowering/analysis, T0 interpreter/planner, safety analysis, and Lambda AST dumper retired their old `AST_NODE_FOR_STAM`/`AstForNode` cases as part of the two-step migration. This is a structural migration, not a compatibility alias or a language-semantic change.

The independently measured P1a diff, excluding completed P0a checker work, is `C/C++ +41/-62 = -21` and all hand source `+42/-63 = -21`; its governed scope is 310,690 lines (`-21` from the project anchor). The post-migration combined checker result is C/C++ `+41/-62 = -21` and all source `+73/-97 = -24`, including P0a's separately completed `-3`. P1a does not borrow that earlier deletion credit.

Focused evidence:

```text
Lambda JIT:    ./lambda.exe run test/lambda/proc/proc_for_window.ls --no-log
Lambda T0:     LAMBDA_TIER=interp ./lambda.exe run test/lambda/proc/proc_for_window.ls --no-log
AST dump:      AST_NODE_FOR_EXPR (and no AST_NODE_FOR_STAM) for proc_for_window.ls
JS C-style:    ./lambda.exe js -e 'let sum = 0; for (let i = 0; i < 4; i++) sum += i; console.log(sum);'
Focused GTest: proc_for_range, proc_for_window, proc_for_expr_content_proc, interp_for_window — 4/4 passed
```

The first aggregate run exceeded the available execution window at 712/766 scripts. After the import-boundary correction below, `make test-lambda-baseline` passed the complete input and runtime sets: 2,104/2,104 input tests, 1,874/1,874 Lambda runtime tests, 3,978/3,978 combined.

#### P1b implementation record — declarations and core-child delegation, 2026-08-28

**D8.2.2** requires one physical layout per core tag, and **D8.2.4** requires core child ownership to be published once. Lambda declarations previously allocated `AST_NODE_ASSIGN` as `AstNamedNode {name, as, entry, declared_type}`, while JavaScript assignments allocate that tag as `AstAssignNode {op, left, right}`. P1b makes `AST_NODE_ASSIGN` exclusively the JavaScript target/value assignment contract. Lambda ordinary declarations, decomposition temporaries, direct type aliases, `for let`/`group` bindings, and module-registry synthetic public declarations now allocate `AST_NODE_VARIABLE_DECLARATOR` with an `AST_NODE_IDENT` `id` and `init` child.

The declaration metadata moves with the shared declarator layout: `name`, binding `entry`, type-definition flag, and declared type live on `AstDeclaratorNode`; `AstNamedNode` remains only for its genuine named/key/parameter roles. Builder registration, AST type lookup, safety analysis, T0 evaluation/planning, MIR lowering/prepasses, type-pattern parsing, AST dump, and module publication were migrated atomically. `ast_visit_core_children()` now owns the newly truthful declarator edges and the retired core rows; the Lambda planner delegates its core traversal through that API and keeps only extension layouts locally. This is not a layout-compatible cast or a compatibility tag.

The first baseline run exposed one missed consumer at the import boundary: `declare_module_import()` still cast public declarators to `AstNamedNode`, producing blank names and error-valued imported constants. The same P1b slice switched that path to the shared binding-name helper and retired its obsolete cast-only comments; the full baseline then passed.

The independent P1b delta is C/C++ `+381/-381 = 0` and all hand source `+381/-381 = 0`; it does not spend the P1a deletion credit. The combined governed result is C/C++ `+422/-443 = -21` and all hand source `+454/-478 = -24`, with candidate 310,690 at the 310,690 cap. Thus every completed slice remains non-positive while replacement and retirement occur together.

Focused evidence:

```text
make build-release-compile                          # Errors: 0
Lambda JIT/T0 proc_for_window                       # [1, 2, 3, 4], T0 executed=1 fallback=0
Lambda JIT/T0 type_syntax_edges                     # identical expected output, T0 executed=1 fallback=0
Lambda JIT/T0 proc_global_typed_array_index         # [2, 7], T0 executed=1 fallback=0
JS C-style assignment loop                          # console.log(sum) => 6
AST_NODE_ASSIGN runtime census                      # only shared visitor, enum, and dump-name rows
make test-lambda-baseline                            # 3978/3978 passed
./utils/check_ast_tune_loc.sh ... --cap 310690      # C/C++ -21; all source -24
git diff --check                                    # clean
```

#### P1c implementation record — assignment/declaration-wrapper storage and stale declarator consumer, 2026-08-28

**D8.2.2** requires one physical layout per shared tag, and **D8.2.4** requires child ownership to be published once. Lambda procedural assignment nodes previously extended `AstAssignNode` with a separate `AstAssignStamNode` or `AstCompoundAssignNode` layout: the former carried `target`/`target_node`/`target_entry`/`value`, while the latter carried `object`/`key`/`value`. P1c folds those optional fields into one `AstAssignNode`; `right` and Lambda's `value` spelling share one union slot, and the old type names are aliases rather than layouts. The builder allocates the shared size for identifier and compound assignments. `ast_visit_core_children()` now walks all four assignment tags through `left/right`, so a compound target's existing field node owns its object/key edges exactly once.

The focused migration also repaired the stale P1b consumer found by the differential gate: both MIR `for`-`let` emitters were still blind-casting `AST_NODE_VARIABLE_DECLARATOR` to `AstNamedNode`, which read the initializer through the wrong field contract and produced `inf`/`9` for grouped queries under explicit `LAMBDA_TIER=jit`. They now consume `AstDeclaratorNode::init`; no compatibility cast remains on that path. This is a root-cause correction, not a test relaxation (**D8.2.2**, **D8.2.4**).

The same rule now covers declaration wrappers. `AstVarDeclNode` is the sole wrapper layout; its `declarations` and Lambda's `declare` spellings share one union slot, and `AstLetNode` is an alias. `let`, `var`, `pub`, and `type` child ownership therefore runs through one `AstVarDeclNode` row in the common visitor, while JavaScript's `kind`/`using` flags remain available on the same record.

The independent P1c source delta is C/C++ `+46/-51 = -5`; it does not spend P1a/P1b deletion credit. The combined governed result from the project anchor is C/C++ `+460/-486 = -26` and all hand source `+492/-521 = -29`, with candidate 310,685 under the 310,690 cap. Every replacement and the stale layout retirement landed in the same slice.

Focused evidence:

```text
make build-release-compile                          # Errors: 0
InterpWalker assignment/for differential controls   # 14/14 passed
LAMBDA_TIER=jit grouped_for                         # key 1/0, total 3/6
LAMBDA_TIER=interp grouped_for                      # identical key 1/0, total 3/6
make test-lambda-baseline                            # 3978/3978 passed
./utils/check_ast_tune_loc.sh ... --cap 310690      # C/C++ -26; all source -29
git diff --check                                    # clean
```

#### P1d implementation record — condition loops, 2026-08-28

**D8.2.2**/**U19** require semantically unified loop forms to share one core
kind while retaining surface syntax as a form field; **D8.2.4** requires one
child-ownership contract. P1d therefore promotes `AST_NODE_LOOP` to the shared
`AstLoopControlNode {form, init, test/cond, update, body, vars}` contract. The
three forms are `LOOP_FORM_WHILE`, `LOOP_FORM_DO_WHILE`, and
`LOOP_FORM_FOR_C`. JavaScript builders now publish all three through that tag;
Lambda `while` uses the same record. The former `AST_NODE_FOR_STAM`,
`AST_NODE_WHILE_STAM`, and `AST_NODE_DO_WHILE_STAM` tags and their duplicate
consumer cases were deleted. Lambda iterator/query clauses moved to the new
`AST_NODE_FOR_CLAUSE` extension tag, so `AST_NODE_LOOP` is not overloaded.

The core visitor owns condition-loop children in source order (including the
body-before-test order of `do...while`); the JS child table uses the same row.
JS early-error, AST-interpreter, MIR-lowering, module-lexical, capture, type,
and return analyses now dispatch on `form` instead of maintaining three node
switches. The JS and Lambda semantic execution frames remain separate as
required by **D8.1.3v10**; only structural storage and traversal were unified.

The independent P1d source delta, measured after the already-landed P1c
changes, is C/C++ `+273/-369 = -96`; all hand source is also `+273/-369 = -96`.
The final governed result after the source-order correction is C/C++
`+738/-861 = -123` and all hand source `+770/-896 = -126`, with candidate
310,601 (delta `-110`) under the 310,690 cap. No previous deletion credit is
borrowed. The correction keeps C-style header checks in initializer, test,
update order; it changes no node layout or runtime semantics.

Focused evidence:

```text
make build                                           # Errors: 0, Warnings: 10
Lambda while JIT/T0                                  # while_swap output identical
JS MIR while/do/for                                   # 6, 6, 6
JS AST backend while                                  # 6
JS AST interpreter do                                 # 4
./utils/check_ast_tune_loc.sh ... --cap 310690        # C/C++ -123; source -126; candidate -110
make test-lambda-baseline                             # 3978/3978 passed
make build-release-compile                            # Errors: 0, Warnings: 34
git diff --check                                      # clean
```

Recommended order:

1. catalog-completeness assertions over the migrated core forms;
2. P4's strictness and owning-function/class relations.

For each family, the same slice updates both producers and all consumers, switches the shared visitor, deletes the matching cases from `interp_visit_children()` and `js_ast_children.cpp`, and proves catalog completeness. No compatibility struct or second tag interpretation remains.

P1 exits only when core ownership is described once. Language semantic walkers may still switch on a core node to evaluate it, but they delegate child ownership/enumeration to the common contract and do not carry another structural catalog.

#### P1e implementation record — shared JS core-child ownership, 2026-08-29

**D8.2.4** requires one common child-enumeration contract and forbids private
core-child walks. The JavaScript child table now contains only the five
extension layouts (`static_block`, `labeled`, `with`, template literal, and
tagged template). Block, match/switch, try/catch/finally, labels' core body
edges, loops, declarations, calls, patterns, classes, and the remaining shared
forms route through `ast_visit_core_children()`.

The JS adapter preserves list-valued `next` chains, suppresses the core
visitor's parent-sibling callback, and retains the source-order `do...while`
special case. `js_ast_any_child()` uses the same adapter, so predicate walks do
not grow a second structural catalog. The former 40-plus JS rows and their
duplicate field accessors are retired in the same slice; extension-only rows
remain owned by `js_ast_children.cpp`.

The independent P1e diff against the immediately preceding `master` tree is
C/C++ `+54/-62 = -8`; all governed hand source is also `+54/-62 = -8`.
The pre-slice current tree is 312,272 governed lines and the post-slice tree is
312,260, so this slice is non-positive even though later unrelated `master`
changes have moved the historical 310,711-line project anchor. The formal
319,606-line anchor remains at `-7,346`; the final `-2,000` project ratchet is
not claimed until the remaining phases retire their duplicate implementations.

Focused evidence:

```text
make build-test                                      # Errors: 0, Warnings: 40
./test/test_js_opt_gtest.exe                         # 19/19 passed
./test/test_js_script_gtest.exe --gtest_filter='JsScriptOwnership.*:JsInterpreter.*'
                                                       # 73/73 passed
./test/test_js_gtest.exe                             # 356/357; the pre-existing lib_tom_select fixture remains excluded by the baseline
./utils/check_ast_tune_loc.sh --base HEAD --cap 312272 --phase-base HEAD
                                                       # candidate 312,260; phase C/C++ -8; source -8
git diff --check                                     # clean
```

### 4.3 P2 — Bind once and publish stable identity

Migrate declarations and references by construct family. For each migrated family:

1. the builder records syntax and declarations;
2. the shared binder allocates IDs and invokes profile policy;
3. the index publishes the resolved binding and use/definition edges;
4. interpreter and MIR consumers switch to the ID;
5. name-based lookup and repair for that family are deleted in the same slice.

The direct-call shadow workaround is an early target because its comment records the stale-scope root cause. The JS scope lookup cache is removed only after its last builder/binder consumer is migrated; it is not copied into `CompilationFacts`.

P2 exits with a static check that lowering code cannot call builder scope lookup. Dynamic JavaScript environment lookup remains an explicit runtime operation; it is not confused with compiler name resolution.

#### P2a implementation record — shared function identity, 2026-08-28

**D8.2.4** requires one stable identity authority across compiler passes. The
JS MIR lane previously built `func_index_nodes`/`func_index_ids`, a private
pointer-hash table over the same source function nodes already indexed by the
shared `AstIndex`. P2a deletes that table and publishes each source
`JsFuncCollected` record through `AstFunctionId` and the pool-owned
`func_by_id` table. `jm_find_collected_func()` now resolves the AST node through
`ast_index_find()` and returns the collected entry by the shared ID. The
collector's post-order array remains intact because parent/capture propagation
still uses its semantic order; the ID table is the bridge, not a second identity
scheme.

Class-field initializer functions are synthesized after source indexing. P2a
therefore retained a bounded linear fallback and explicitly left their IDs to
P2b; no false claim of complete FunctionId coverage was made. No source
semantics or ABI changed.

The independent P2a delta is C/C++ `+33/-35 = -2`; all hand source is also
`+33/-35 = -2`. The governed candidate is 310,599 lines, two below the P1d
candidate and below the 310,690 cap. The retired implementation is the private
pointer-hash allocation and lookup; the surviving authority is
`JsTranspiler::ast_index` plus `JsMirTranspiler::func_by_id`.

Focused evidence:

```text
make build                                           # Errors: 0, Warnings: 12
./test/test_js_gtest.exe                             # 357/357 passed
./test/test_js_opt_gtest.exe                         # 19/19 passed
make test-lambda-baseline                            # 3978/3978 passed
./utils/check_ast_tune_loc.sh ... --cap 310690      # C/C++ -125; source -128; candidate -112
git diff --check                                     # clean
```

#### P2b implementation record — synthetic function identity, 2026-08-28

**D8.2.4** requires synthesized callables to use the same stable identity
authority as source functions. P2b appends each class-field initializer function
to `AstIndex` at construction time, so the existing `func_by_id` table covers
both source and synthesized entries. The linear fallback in
`jm_find_collected_func()` is deleted. The append uses the field as the owning
AST edge and preserves the existing collector order and capture semantics.
Verification also exposed missing core edges for `await`/`yield` operands and
destructuring assignment/array/map/rest patterns; those edges now enter the
same index, so nested functions receive IDs instead of falling through to a
repair scan.

The independent P2b delta is C/C++ `+24/-24 = 0`; all hand source is also
`+24/-24 = 0`. The governed candidate is 310,599 lines, unchanged from P2a and
below the 310,690 cap. The retired implementation is the synthetic linear lookup;
the surviving authority is `AstIndex`/`AstFunctionId`/`func_by_id`.

Focused evidence:

```text
make build                                           # Errors: 0, Warnings: 10
class-field JS fixtures                               # 2/2 passed
promise/GC/pattern identity fixtures                  # 3/3 passed
./test/test_js_gtest.exe                             # 354/354 passed
./test/test_js_opt_gtest.exe                         # 19/19 passed
make test-lambda-baseline                            # 3978/3978 passed
make build-release-compile                            # Errors: 0, Warnings: 37; smoke: 6
./utils/check_ast_tune_loc.sh ... --cap 310690      # C/C++ -125; source -128; candidate -112
git diff --check                                     # clean
```

P2c now owns dense identity publication, use/definition edges, JavaScript
extension scopes, and the lowering binding handoff; no callable identity
fallback or compiler-time scope re-resolution remains in normal indexed
compilation.

#### P2c implementation record — binding handoff and identity publication, 2026-08-28

**D8.2.4** requires lowering to consume resolved binding identity rather than
rebuild compiler scope state. P2c makes the AST builder's `AstIdentNode::entry`
the sole binding input for JS MIR direct-call resolution and Test262 assert
classification. The MIR lane no longer calls `js_scope_lookup()` after the
indexed unit is sealed, and the stale-scope parameter/capture repair scan is
deleted. Runtime dynamic lookup remains in the JS runtime; this change only
removes compiler-time name re-resolution.

The independent P2c identity slice is C/C++ `+128/-130 = -2`; all hand source
is also `+128/-130 = -2`. The governed candidate is 310,530 lines, 181 below
the project anchor and 9,076 below the formal anchor. `AstIndex` now owns the
dense `ScopeId`/`BindingId`/`ClassId` tables for common scope nodes, resolved
name entries, and class nodes; migrated MIR call paths dereference binding IDs.
The retired implementation is `JsScopeLookupCacheEntry`, its hash/compare and
enable/clear/free paths, plus the duplicate `jm_current_param_pattern_declares`
pattern walk. The surviving builder owner is direct `js_scope_lookup`; no
post-build consumer uses a name cache.
The shared graph is now represented by `AstIndex::node_bindings` plus the
definition lookup from each indexed `NameEntry`; extension-owned scopes enter
the same table through the JS profile fact publisher.

Focused evidence:

```text
make build                                           # Errors: 0; no warnings in changed files
./test/test_js_gtest.exe                             # 357/357 passed
./test/test_js_opt_gtest.exe                         # 19/19 passed
P2c shadow/closure/direct-call fixtures              # all passed in JS suite
! rg 'js_scope_lookup\(' lambda/js/js_mir*          # no MIR compiler lookup
./utils/check_ast_tune_loc.sh ... --cap 310532      # C/C++ -2; source -2; candidate -2
git diff --check                                     # clean
```

#### P2c completion — shared use/definition graph and JS extension scopes, 2026-08-28

The final P2c slice completes the **D8.2.4** identity boundary. `AstIndex`
publishes a dense binding edge for every indexed identifier/declaration node;
definition lookup follows the binding's declaration node through the same
pointer-to-ID table, so forward declarations remain valid without a second
definition cache. The JS `LangProfile` publishes branch, catch, switch,
loop-head, and named-class-expression scopes before extension children are
walked. Direct-call lowering now uses the shared resolver, retiring its
duplicate declaration/const-function scan and the obsolete `NameEntry`-stored
binding ID.

This independent completion slice is C/C++ `+176/-179 = -3`; all hand source
is also `+176/-179 = -3`. The governed candidate is 310,527 lines, 184 below
the project anchor and 9,079 below the formal anchor. The retired
implementation is the inline direct-call definition scan, the unused binding
ID field, duplicate child-row walking, and unconsumed index convenience
lookups; the surviving authority is `AstIndex` plus the JS profile publisher.

Focused evidence:

```text
make build                                           # Errors: 0; Warnings: 12
./lambda.exe js test/js/functions_basic.js           # exit 0; 7 / Hello / 13
./test/test_js_script_gtest.exe --gtest_filter='JsScriptOwnership.*:JsInterpreter.*'
                                                       # 51/51 passed
make test-lambda-baseline                            # 3978/3978 passed
./utils/check_ast_tune_loc.sh --base 44b98dcebd19a548a14bbb75785091b545445f00 --cap 310711 --phase-base 3c01d18e9089b3ea6739cb065f65850f1b912a98
                                                       # candidate 310,527; phase C/C++ -3; source -3
./utils/check_ast_tune_loc.sh --base e66e5b5c71bc7ee7fe2d1e2b2a9afe27dc6825a3 --phase-base 3c01d18e9089b3ea6739cb065f65850f1b912a98
                                                       # candidate 310,527; delta -9,079
git diff --check                                     # clean
```

### 4.4 P3 — Make the pass manager and compile driver real

P3 converts one entry path at a time to the common compilation-unit driver and deletes that path's old orchestration immediately.

Recommended order:

1. ordinary JS source;
2. pre-built JS AST;
3. JS module;
4. direct eval/new Function;
5. batch/preamble mode;
6. Lambda ordinary file/module path.

Mode-specific policy is data on the unit: parse goal, module/eval/preamble flags, backend selection, execution policy, and artifact-retention policy. It is not a copy of the driver.

The JS validation pass now produces `VALIDATED` before index publication; repeated caller-side early-error checks are retired. P3b moves MIR context creation, error-handler installation, transpiler ownership, name-base setup, and module creation into one script-unit opener used by ordinary source and pre-built-AST entry points. P3c extends that opener to ES modules while retaining the module's private zero-based name image and registry/TLA policy. P3d extends it to direct eval and `new Function` with their compact storage and optimize-level policy. P3e routes eval-preamble publication and batch/preamble declaration snapshots through one owned map-to-array helper. P3f routes finalized MIR volume accounting for Lambda and LambdaJS through one shared runtime walk. Manual phase timing and cleanup labels remain for the later driver slices.

P3g makes pass contexts explicit and places the JS analysis/lower/finalize
boundary under the manager. P3i extends that authority through Lambda MIR
lower/finalize/link and routes JavaScript execution through one recovery-aware
entry helper; cleanup remains the final runtime-owned handoff after template
and module publication.

#### P3a implementation record — truthful JavaScript validation pass, 2026-08-28

**D8.2.5** requires a pass to declare the facts it produces and to run only
after its prerequisites are present. P3a removes the pre-seeded `VALIDATED`
fact from `build_js_ast_indexed()`, registers `js_check_early_errors()` as a
required validation pass, and makes index publication depend on that fact.
Validation failures leave the original AST available to the caller's existing
syntax-error lane, but no `INDEXED` fact is published. Source, module,
eval/new-Function, and AST-interpreter paths now consume the one validation
result instead of rerunning the checker and its cleanup branch.

The independent P3a slice is C/C++ `+22/-25 = -3`; all hand source is also
`+22/-25 = -3`. The phase base is commit `068301268`; the governed candidate
is 310,524 lines, a non-positive delta. The retired implementation is the
five entry-point early-error call blocks and the false initial fact; the
surviving authority is the `validate`/`index` pair in `build_js_ast_indexed()`.

Focused evidence:

```text
make build-test                                      # Errors: 0; Warnings: 40
./test/test_js_gtest.exe                             # 357/357 passed
./test/test_js_opt_gtest.exe                         # 19/19 passed
./test/test_js_script_gtest.exe --gtest_filter='JsScriptOwnership.*:JsInterpreter.*'
                                                       # 51/51 passed
./utils/check_ast_tune_loc.sh --base 44b98dcebd19a548a14bbb75785091b545445f00 --cap 310711 --phase-base 068301268
                                                       # candidate 310,524; phase C/C++ -3; source -3
git diff --check                                     # clean
```

#### P3c implementation record — module compile-unit opener, 2026-08-28

**D8.2.5** requires compilation-unit setup to be shared without erasing
language- or mode-specific policy. `js_mir_open_compile_unit` now accepts the
module bit and module name, so the ES-module entry uses the same MIR context,
transpiler ownership, active-owner tracking, and module-artifact creation as
ordinary source. Its zero-based private property-name image, prelink ordering,
registry placeholder, top-level-await bookkeeping, and deferred MIR retention
remain in the module caller. The previous module-local context/transpiler/
module setup is retired in the same change.

The independent P3c slice is C/C++ `+17/-24 = -7`; all hand source is also
`+17/-24 = -7`. The phase base is commit `91e89a0c4`; the governed candidate is
310,517 lines, seven below P3b and 194 below the 310,711-line project anchor.

Focused evidence:

```text
make build-test                                      # Errors: 0; Warnings: 40
./test/test_js_gtest.exe --gtest_filter='JavaScriptRegression.Module*'
                                                       # 2/2 passed
./test/test_js_script_gtest.exe --gtest_filter='JsInterpreter.LinksEsModulesWithLiveRegistryBindings:JsInterpreter.SupportsModuleMetadataAndDynamicImports:JsInterpreter.PreservesLiveBindingsThroughNamedReexports:JsInterpreter.PreservesLiveBindingsThroughStarReexports:JsInterpreter.ExportsNamespaceObjectsAndAnonymousDefaultFunctions:JsInterpreter.InstantiatesHoistedExportsBeforeCircularDependencies:JsInterpreter.RejectsAmbiguousStarExportsBeforeModuleBodyExecution:JsInterpreter.ImportsLambdaModulesThroughTheSharedRegistry'
                                                       # 8/8 passed
./utils/check_ast_tune_loc.sh --base 44b98dcebd19a548a14bbb75785091b545445f00 --cap 310711 --phase-base 91e89a0c4
                                                       # candidate 310,517; phase C/C++ -7; source -7
git diff --check                                     # clean
```

#### P3b implementation record — shared script-unit opener, 2026-08-28

**D8.2.5** requires compilation-unit policy and ownership to be explicit rather
than copied across entry points. The ordinary source and pre-built-AST paths
now call `js_mir_open_script_unit`, which creates the MIR context, installs the
mode-selected batch error handler, creates and tracks `JsMirTranspiler`,
publishes the preamble name base, and opens the script module. Their duplicated
context/transpiler/module setup is retired immediately; the pre-built-AST
success path also clears its active-transpile owner before destruction. The
remaining preamble, import, link, execution, and retention policy stays in the
caller until its own driver slice.

The independent P3b slice is C/C++ `+33/-33 = 0`; all hand source is also
`+33/-33 = 0`. The phase base is commit `79a1f1684`; the governed candidate is
310,524 lines, unchanged from P3a and below the 310,711-line cap. This is a
zero-growth migration: every new helper line retires an equivalent duplicated
setup line in the two clients.

Focused evidence:

```text
make build-test                                      # Errors: 0; Warnings: 36
./test/test_js_gtest.exe                             # 357/357 passed
./test/test_js_script_gtest.exe --gtest_filter='JsScriptOwnership.*:JsInterpreter.*'
                                                       # 51/51 passed
./test/test_js_opt_gtest.exe                         # 19/19 passed
./utils/check_ast_tune_loc.sh --base 44b98dcebd19a548a14bbb75785091b545445f00 --cap 310711 --phase-base 79a1f1684
                                                       # candidate 310,524; phase C/C++ 0; source 0
git diff --check                                     # clean
```

#### P3d implementation record — direct-eval compile-unit opener, 2026-08-28

**D8.2.5** requires the same lifecycle owner across source forms while keeping
mode policy explicit. Direct eval and `new Function` now call
`js_mir_open_compile_unit` with optimize level 0, compact 16/8/8 collection
capacities, their caller-selected name base, and their dynamic module name.
The existing lexical/global preamble rules, eval module-scope isolation,
`new.target` handling, cache behavior, and deferred MIR lifetime remain in
their callers. Their duplicated MIR context, error-handler, transpiler, and
module setup is retired; successful deferred paths also clear the active owner
before destroying the transpiler.

The independent P3d slice is C/C++ `+31/-51 = -20`; all hand source is also
`+31/-51 = -20`. The phase base is commit `a234a0838`; the governed candidate
is 310,497 lines, 20 below P3c and 214 below the 310,711-line project anchor.

Focused evidence:

```text
make build-test                                      # Errors: 0; Warnings: 40
./test/test_js_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/eval*'
                                                       # 2/2 passed
./test/test_js_opt_gtest.exe --gtest_filter='JsOpt.DynamicFunction*'
                                                       # 2/2 passed
./test/test_js_script_gtest.exe --gtest_filter='JsInterpreter.DirectEvalSharesInterpretedFunctionEnvironment'
                                                       # 1/1 passed
./utils/check_ast_tune_loc.sh --base 44b98dcebd19a548a14bbb75785091b545445f00 --cap 310711 --phase-base a234a0838
                                                       # candidate 310,497; phase C/C++ -20; source -20
git diff --check                                     # clean
```

#### P3e implementation record — batch/preamble declaration snapshot, 2026-08-28

**D8.2.5** requires compile-unit metadata publication to have one ownership
path. `js_preamble_entries_from_module_consts` now performs the single
map-to-owned-array copy, including partial-copy unwind. Eval-preamble
publication and batch/preamble snapshots call it; the old duplicate allocation
and iteration walks are retired while each caller retains its own replacement
and module-variable policy.

The independent P3e slice is C/C++ `+30/-30 = 0`; all hand source is also
`+30/-30 = 0`. The phase base is commit `1d5f058a2`; the governed candidate is
310,497 lines, unchanged from P3d and 214 below the 310,711-line project
anchor. This is a zero-growth migration: the shared helper is funded entirely
by the two retired map walks.

Focused evidence:

```text
make build-test                                      # Errors: 0; Warnings: 40
./test/test_js_script_gtest.exe --gtest_filter='JsScriptOwnership.*'
                                                       # 1/1 passed
./test/test_js_gtest.exe --gtest_filter='JavaScriptRegression.ModuleCompileCacheHonorsPermissionWriteGrants:JavaScriptRegression.ModuleEntryPrelinksOwnAndImportNameTables'
                                                       # 2/2 passed
./utils/check_ast_tune_loc.sh --base 44b98dcebd19a548a14bbb75785091b545445f00 --cap 310711 --phase-base 1d5f058a2
                                                       # candidate 310,497; phase C/C++ 0; source 0
git diff --check                                     # clean
```

#### P3f implementation record — shared finalized MIR volume accounting, 2026-08-28

**D8.2.5** requires one compile-unit artifact contract across language
profiles. Lambda and LambdaJS had separate module/function/instruction walks
for the same finalized-MIR volume definition, including the same exclusion of
labels. `mir_count_module_volume` in the existing runtime shared utility is now
the sole walk; both front ends publish their profile-specific counters through
it, and the private copies are retired.

The independent P3f slice is C/C++ `+34/-40 = -6`; all hand source is also
`+34/-40 = -6`. The phase base is commit `6f7b0ebaa`; the governed candidate is
310,491 lines, six below P3e and 220 below the 310,711-line project anchor.
The helper is funded by deleting both duplicate walks in the same slice.

Focused evidence:

```text
make build-test                                      # Errors: 0
LAMBDA_TEST_MAX_CONCURRENT=1 make test-lambda-baseline
                                                       # 3978/3978 passed
./utils/check_ast_tune_loc.sh --base 44b98dcebd19a548a14bbb75785091b545445f00 --cap 310711 --phase-base 6f7b0ebaa
                                                       # candidate 310,491; phase C/C++ -6; source -6
git diff --check                                     # clean
```

#### P3g implementation record — pass-owned contexts and authoritative MIR driver, 2026-08-29

**D8.2.5** requires pass declarations to carry the context they consume and
requires the compile driver, rather than each caller, to publish phase facts.
`AstIndexPassContext` and `ast_index_compiler_pass()` now own the indexed-AST
operation for both Lambda and JavaScript; their private callbacks are deleted.
`CompilerPassSpec` carries an optional pass context, so validation and index
passes cannot accidentally receive the other pass's state. The public
`transpile_js_mir_ast()` entry is now the only JS analysis/lower/finalize
driver: it seeds the indexed prerequisites, runs one manager-owned composite
pass, and publishes `ANALYZED`, `PLANNED`, `MIR_LOWERED`, and `FINALIZED` only
after the complete existing sequence succeeds. All JS source, module, eval,
batch, and pre-built-AST callers already enter through this boundary.

This is a deletion-funded **D8.2.5** slice. Path-isolated against commit
`7a7ca002c`, the P3g hand source is `+47/-49 = -2`; the combined working tree
also contains P4e and is `313,938 → 313,931` governed lines with
`+131/-138 = -7` across changed first-party C/C++ and source. MIR link,
execution/cleanup policy, and Lambda's later analysis/lowering remain explicit
residue for the next driver slices; P3g does not claim the complete cross-
language schedule or the final **D8.6.4v2** ratchets.

Focused verification after rebuilding the affected targets:

```text
make -C build/premake config=debug_native test_compiler_pass_gtest test_js_script_gtest test_js_mir_emission_gtest test_js_opt_gtest -j8 CC=clang CXX=clang++
                                                       # build passed
./test/test_compiler_pass_gtest.exe --gtest_color=no   # 2/2 passed
./test/test_js_opt_gtest.exe --gtest_color=no          # 19/19 passed
./test/test_js_script_gtest.exe --gtest_color=no --gtest_filter='JsScriptOwnership.*:JsInterpreter.*'
                                                       # 103/103 passed
./test/test_js_mir_emission_gtest.exe --gtest_color=no # 21/21 passed
LAMBDA_TEST_MAX_CONCURRENT=1 make test-lambda-baseline
                                                       # 4035/4035 passed
./utils/check_ast_tune_loc.sh --base HEAD --cap 313947 --phase-base HEAD
                                                       # candidate 313,931; phase C/C++ -7; source -7
git diff --check                                     # clean
```

#### P3h implementation record — shared JavaScript link/entry publication, 2026-08-29

The source, pre-built-AST, and ES-module JavaScript entry paths now call one
`js_mir_link_main()` helper for the MIR interface selection, `MIR_link`, and
`js_main` lookup. The shared `JsMirMainFunc` contract also removes three local
function-pointer typedefs. This retires link/entry spelling drift while leaving
the mode-specific execution and retention policy explicit for the remaining
**D8.2.5** lifecycle slices.

This is a deletion-funded **D8.2.5** slice. The helper and shared typedef are
paid for by the three duplicate link/find blocks and local typedefs; an
additional dead class-method scan was removed in the same slice. Against the
current external HEAD, the working candidate is 314,675 lines (`-139`), and the
changed first-party C/C++ and source counters are `+98/-238 = -140`. No prior
slice's deletion credit is reused.

Focused verification:

```text
make -C build/premake config=debug_native test_js_script_gtest test_js_mir_emission_gtest test_js_opt_gtest -j8 CC=clang CXX=clang++
                                                       # build passed
./test/test_js_script_gtest.exe --gtest_color=no       # 104/104 passed
./test/test_js_mir_emission_gtest.exe --gtest_color=no # 21/21 passed
./test/test_js_opt_gtest.exe --gtest_color=no          # 19/19 passed
./utils/check_ast_tune_loc.sh --base HEAD --cap 314814 --phase-base HEAD
                                                       # candidate 314,675; phase C/C++ -140; source -140
git diff --check                                     # clean
```

P3i and P3j subsequently move Lambda link/entry and the conditional indexed
prerequisite into the authoritative driver; mode-specific cleanup and the
final **D8.6.4v2** ratchets remain explicit closeout work.

#### P3i implementation record — manager-owned Lambda MIR link and shared execution entry, 2026-08-29

**D8.2.5** requires the compile driver to publish phase facts only after the
declared work succeeds. The Lambda MIR Direct path now runs const-fold,
lower/finalize, and link/`main` publication as one ordered pass-manager
schedule. The link pass owns the large-module interpreter policy, lazy/native
interface choice, optimization downgrade, and entry lookup; the caller retains
only post-link BSS/template publication and final ownership transfer. The JS
source, cached-AST, eval/`new Function`, and ES-module execution paths use the
same recovery-aware entry helper, including deferred module-body execution.

This is a deletion-funded **D8.2.5** slice. The former Lambda lower/count,
policy, link, and entry blocks are retired when their pass callbacks are
installed. The current governed candidate is 314,506 lines against the
314,814-line HEAD baseline; the cumulative changed C/C++ and source counters
are `+989/-1,297 = -308`. No prior deletion credit is reused.

Focused verification:

```text
make build                                             # Errors: 0
./test/test_js_script_gtest.exe --gtest_color=no       # 104/104 passed
./test/test_js_mir_emission_gtest.exe --gtest_color=no # 21/21 passed
./test/test_js_opt_gtest.exe --gtest_color=no          # 19/19 passed
./utils/check_ast_tune_loc.sh --base HEAD --cap 314814 --phase-base HEAD
                                                       # candidate 314,506; phase C/C++ -308; source -308
git diff --check                                       # clean
```

The final baseline gate is run after the remaining P4 fact/index slices; no
semantic ruling or AST layout changed.

#### P3j implementation record — conditional indexed prerequisite in the Lambda driver, 2026-08-29

The Lambda MIR Direct compile driver now treats the indexed AST as a required
prerequisite while remaining safe for retained pre-indexed AST callers. The
driver seeds the pass manager with the facts already present, schedules the
shared `ast_index_compiler_pass` only when the unit has no published index, and
then runs const-fold, MIR lower/finalize, and link/entry through the same
ordered manager. This makes the compile driver authoritative for the complete
build-to-entry schedule without rebuilding an index that the shared builder
already published.

This is a deletion-funded **D8.2.5** slice: the old caller-side prerequisite
branch is retired when the conditional `CompilerPassSpec` is installed. No
semantic ruling or AST layout changed, and the phase remains non-positive under
the governed and changed-source counters.

Focused verification:

```text
make build                                             # Errors: 0
./test/test_compiler_pass_gtest.exe --gtest_color=no   # 2/2 passed
./test/test_js_script_gtest.exe --gtest_color=no       # 104/104 passed
./test/test_js_mir_emission_gtest.exe --gtest_color=no # 21/21 passed
./test/test_js_opt_gtest.exe --gtest_color=no          # 19/19 passed
./utils/check_ast_tune_loc.sh --base HEAD --cap 314814 --phase-base HEAD
                                                       # candidate 314,192; phase C/C++ -622; source -622
git diff --check                                       # clean
```

The full baseline and final LOC ratchets remain the P1–P4 acceptance gate.

### 4.5 P4 — Replace collection with indexed analysis

P4 makes `AstIndex.functions` and the new class index authoritative. Each analysis changes from recursive discovery to an indexed or worklist pass, and its former walk/cache is deleted immediately.

Recommended order:

1. strictness and owning-function/class relations;
2. direct-eval, `arguments`, `this`, `new.target`, return, and suspension facts;
3. captures and environment layout;
4. effect propagation and call-site evidence;
5. parameter/return inference and function planning;
6. class method/field/static-block planning.

Synthetic functions, such as class field initializers, receive real `FunctionId` values when created; they do not justify a second identity table.

P4 deletes the `JsFuncCollected` fields that have moved to common or JS extension facts. The record may survive temporarily only for backend MIR items that have not yet moved to the artifact table, and its shrinking must be visible in the deletion ledger every slice.

#### P4a implementation record — collection-owned strictness, 2026-08-29

The first P4 slice moves strictness facts to the function collection boundary,
where the owning function, class method, synthetic field initializer, and
class-heritage function ranges are already known. Ordinary functions now derive
their own strictness from the compilation-unit policy, class syntax, and direct
`use strict` directive; strict parents mark every collected descendant in their
range. Class methods retain both `is_class_method` and strict facts at creation,
synthetic field initializers mark their descendants, and functions collected
from `extends` expressions are marked strict before class metadata is published.
This preserves the **D8.2.4** indexed ownership boundary and removes the
post-collection AST/class propagation walk from `js_mir_module_batch_lowering.cpp`.

The retired block was 55 lines; the collection changes are `+15/-4` (`-44`
net C/C++ and source lines for this slice). No AST layout or runtime semantic
contract changed. The current combined checker remains non-positive:

```text
./utils/check_ast_tune_loc.sh --base HEAD --cap 312272 --phase-base HEAD
# AST_TUNE_LOC baseline=312272 candidate=312216 delta=-56
# AST_TUNE_PHASE_CPP base=HEAD added=69 removed=121 delta=-52
# AST_TUNE_PHASE_SOURCE base=HEAD added=74 removed=126 delta=-52
```

Focused verification passed after rebuilding the affected targets with
`CC=clang CXX=clang++`:

```text
./test/test_js_script_gtest.exe --gtest_color=no   # release_native: 74/74 passed
./test/test_js_opt_gtest.exe --gtest_color=no      # debug_native: 19/19 passed
git diff --check                                 # clean
```

After rebuilding the JS script runner and its Node core DSO in one release
configuration, the serialized baseline aggregate passed:

```text
LAMBDA_TEST_MAX_CONCURRENT=1 node test/test_run.js --target=lambda --category=baseline \
  --exclude-test=test_node_prelim_gtest --exclude-test=test_lambda_concurrency_gtest \
  --parallel --input-results=test_output/input_baseline_results.json
# Input parsers 2104/2104; Lambda runtime 1901/1901; combined 4005/4005
```

This slice does not claim the final **D8.6.4v2** timing or `-2,000`-line
target.

#### P4b implementation record — shared direct-eval facts, 2026-08-29

The second P4 slice gives syntactic direct-`eval` detection one AST-support
owner. The recursive scanner now uses the shared core/extension child contract,
explicitly stops at nested functions/classes/methods, and keeps the optional-call
rule (`eval?.()` is indirect). `js_ast_function_has_direct_eval` applies that
same node fact to parameters and the body, so MIR collection and the AST
interpreter agree on default-parameter evaluation as well as body evaluation.
The collector's private 96-line scanner and the interpreter's duplicate
function wrapper are retired; class-field initializer expressions continue to
use the node-level helper because they are not function nodes.

This is a deletion-funded **D8.2.4** ownership slice with no semantic ruling or
AST layout change. Relative to the pre-P4b candidate, the governed tree falls
from 312,216 to 312,159 lines (a `-57` slice delta); the cumulative phase remains
non-positive:

```text
./utils/check_ast_tune_loc.sh --base HEAD --cap 312272 --phase-base HEAD
# AST_TUNE_LOC baseline=312272 candidate=312159 delta=-113
# AST_TUNE_PHASE_CPP base=HEAD added=114 removed=223 delta=-109
# AST_TUNE_PHASE_SOURCE base=HEAD added=119 removed=228 delta=-109
```

Focused verification passed after the release-native JS script and debug-native
optimization targets were rebuilt with `CC=clang CXX=clang++`:

```text
./test/test_js_script_gtest.exe --gtest_color=no   # release_native: 74/74 passed
./test/test_js_mir_emission_gtest.exe --gtest_color=no  # release_native: 21/21 passed
./test/test_js_opt_gtest.exe --gtest_color=no      # debug_native: 19/19 passed
git diff --check                                 # clean
```

The serialized baseline aggregate also passed with the same release-native
script runner and a single worker:

```text
LAMBDA_TEST_MAX_CONCURRENT=1 node test/test_run.js --target=lambda --category=baseline \
  --exclude-test=test_node_prelim_gtest --exclude-test=test_lambda_concurrency_gtest \
  --parallel --input-results=test_output/input_baseline_results.json
# Input parsers 2104/2104; Lambda runtime 1901/1901; combined 4005/4005
```

The final **D8.6.4v2** timing and `-2,000`-line project target remain open.

#### P4c implementation record — shared `arguments` observation fact, 2026-08-29

The next P4 slice gives `arguments` observation one AST-support owner. The
shared function fact scans parameters and the body through the common
core/extension child contract, keeps lexical arrows in the enclosing scan, and
stops at nested ordinary functions, methods, and classes that own their own
binding facts. MIR collection and the AST interpreter now consume the same
classification; the interpreter's private 29-line scan is retired while MIR's
reference set remains responsible for capture edges.

This is a deletion-funded **D8.2.4** ownership slice with no semantic ruling or
AST layout change. The merge-base governed tree and candidate are both 313,883
lines (`0` phase delta); changed first-party C/C++ and source counters are each
`+30/-30` (`0` delta). The earlier P4a/P4b reductions remain recorded against
their pre-merge bases and are not double-counted here.

Focused verification after rebuilding the release-native JS script target:

```text
./test/test_js_script_gtest.exe --gtest_color=no
# 104/104 passed
```

This slice does not claim the final **D8.6.4v2** timing or `-2,000`-line
target.

#### P4d implementation record — shared tail-reuse eligibility fact, 2026-08-29

The next P4 slice moves AST tail-reuse eligibility into the shared AST-support
owner. Direct-eval, `arguments`, and tail-reuse scanners now use one explicit
nested-function/method/class boundary predicate; tail reuse still rejects
`eval`, `with`, `try`, and any nested function, while `arguments` remains a
shared function fact. The interpreter's private 24-line structural scanner is
retired, and the call path consumes the shared fact without changing the
activation or completion contract.

This is a deletion-funded **D8.2.4** ownership slice with no semantic ruling or
AST layout change. The merge-base governed tree falls from 313,947 to 313,938
lines (`-9`); changed first-party C/C++ and source counters are `+32/-41`
(`-9` delta). The prior P4 records retain their original merge-base accounting.

Focused verification after rebuilding the affected targets:

```text
./test/test_js_script_gtest.exe --gtest_color=no       # 104/104 passed
./test/test_js_mir_emission_gtest.exe --gtest_color=no # 21/21 passed
./test/test_js_opt_gtest.exe --gtest_color=no          # 19/19 passed
```

This slice does not claim the final **D8.6.4v2** timing or `-2,000`-line
target.

#### P4e implementation record — shared lexical-observation fact, 2026-08-29

The next P4 slice gives `arguments`, `this`, and `new.target` observation one
AST-support owner. One observation mask now handles default-parameter and body
references, lexical arrows, `super`-based `this`, and ordinary
function/method/class boundaries. MIR capture analysis consumes the mask for
function facts and arrow environment slots; the temporary arrow pseudo-reference
walk and MIR-local `with` scan are retired. `with` ownership remains a separate
AST fact because lexical arrows do not transfer that dynamic environment fact to
their parent.

This is a deletion-funded **D8.2.4** ownership slice with no semantic ruling or
AST layout change. The merge-base governed tree falls from 313,938 to 313,933
lines (`-5`); changed first-party C/C++ and source counters are `+84/-89`
(`-5` delta). The P4d accounting remains against its own merge base.

Focused verification after rebuilding the affected targets:

```text
./test/test_js_script_gtest.exe --gtest_color=no       # 104/104 passed
./test/test_js_mir_emission_gtest.exe --gtest_color=no # 21/21 passed
./test/test_js_opt_gtest.exe --gtest_color=no          # 19/19 passed
```

This slice does not claim the final **D8.6.4v2** timing or `-2,000`-line
target.

#### P4f implementation record — AST-owned function analysis, 2026-08-29

The next P4 slice retires the duplicate `FnAnalysis` record embedded in
`JsFuncCollected`. Function analysis now lives on the owning AST function node,
the same identity consumed by Lambda planning/interpreter/runtime code;
collection metadata reaches it through one small accessor. Parameter facts,
capture facts, variants, and cleanup therefore use the common AST-owned record,
while MIR-only artifacts remain on `JsFuncCollected` until their indexed-table
slice. The old embedded record and its per-function publication assignment are
deleted in the same change. The capture analyzer also shares one initializer
for self and lexical pseudo-captures, retiring four duplicate record layouts.

This is a deletion-funded **D8.2.4** slice. Against the current external merge
HEAD, changed first-party C/C++ is `+79/-103 = -24`; the governed candidate is
314,790 lines versus the 314,814 baseline. The prior P4a–P4e accounting remains
against each slice's own merge base and is not reused here.

Focused verification after rebuilding the affected targets:

```text
make -C build/premake config=debug_native test_js_script_gtest test_js_mir_emission_gtest test_js_opt_gtest test_compiler_pass_gtest -j8 CC=clang CXX=clang++
./test/test_js_script_gtest.exe --gtest_color=no       # 104/104 passed
./test/test_js_mir_emission_gtest.exe --gtest_color=no # 21/21 passed
./test/test_js_opt_gtest.exe --gtest_color=no          # 19/19 passed
./test/test_compiler_pass_gtest.exe --gtest_color=no   # 2/2 passed
./utils/check_ast_tune_loc.sh --base HEAD --cap 314814 --phase-base HEAD
                                                       # candidate 314,790; phase C/C++ -24; source -24
git diff --check                                     # clean
```

The full Lambda baseline remains the next gate after the subsequent P3/P4
driver slices; this record does not claim the final **D8.6.4v2** timing or
`-2,000`-line project target.

#### P4g implementation record — indexed enclosing lexical facts, 2026-08-29

The capture pass no longer rescans the AST subtree to rediscover enclosing
lexical declarations for every function. The builder's `NameScope` parent chain
is the authoritative bound scope graph; P4g projects its lexical entries into
the existing capture-analysis fact set, preserving source ranges, binding
identity, TDZ kind, and Annex-B function-declaration markers. The recursive
collector and its private child-dispatch context are retired immediately, so
capture analysis now consumes the same scope identity that binding/indexing
published under **D8.2.4**.

This is a deletion-funded **D8.2.4** slice. Relative to the P4f candidate,
changed first-party C/C++ is `+24/-133 = -109`; the cumulative candidate is
314,681 lines against the current 314,814-line HEAD baseline. No prior slice's
deletion credit is reused.

Focused verification after rebuilding the affected targets:

```text
make -C build/premake config=debug_native test_js_script_gtest test_js_mir_emission_gtest test_js_opt_gtest -j8 CC=clang CXX=clang++
./test/test_js_script_gtest.exe --gtest_color=no       # 104/104 passed
./test/test_js_mir_emission_gtest.exe --gtest_color=no # 21/21 passed
./test/test_js_opt_gtest.exe --gtest_color=no          # 19/19 passed
LAMBDA_TEST_MAX_CONCURRENT=1 make test-lambda-baseline  # pending final run
./utils/check_ast_tune_loc.sh --base HEAD --cap 314814 --phase-base HEAD
                                                       # candidate 314,681; phase C/C++ -133; source -133
git diff --check                                     # clean
```

P4j subsequently consumes indexed call-site evidence and completes the
capture/inference worklists; the final **D8.6.4v2** timing and project target
remain acceptance gates.

#### P4h implementation record — common capture-capacity ownership, 2026-08-29

Capture-array capacity and teardown now live with the AST-owned `FnAnalysis`
record. `JsFuncCollected` no longer carries a second capacity counter; the
collection entry remains only a temporary backend view of the shared array while
the indexed capture/environment slices are completed. Reallocation copies the
shared analysis storage, and cleanup releases it through that owner, preventing
the previous mirror from becoming an independent lifetime.

This is a deletion-funded **D8.2.4** slice. One JS collection field and its
separate teardown path were retired while the common analysis record gained the
single capacity fact. Against the current external HEAD, the candidate is
314,675 lines; the cumulative changed C/C++ and source counters are
`+113/-252 = -139` (the same working candidate includes P3h). No prior
deletion credit is reused.

Focused verification after rebuilding the affected targets:

```text
make -C build/premake config=debug_native test_js_script_gtest test_js_mir_emission_gtest test_js_opt_gtest -j8 CC=clang CXX=clang++
                                                       # build passed
./test/test_js_script_gtest.exe --gtest_color=no       # 104/104 passed
./test/test_js_mir_emission_gtest.exe --gtest_color=no # 21/21 passed
./test/test_js_opt_gtest.exe --gtest_color=no          # 19/19 passed
./utils/check_ast_tune_loc.sh --base HEAD --cap 314814 --phase-base HEAD
                                                       # candidate 314,675; phase C/C++ -139; source -139
git diff --check                                     # clean
```

The JS pointer/count view is still consumed by backend code and is the next
capture migration target; this slice does not claim sole `FunctionId` capture
identity or the final **D8.6.4v2** project ratchet.

#### P4i implementation record — AST-owned capture identity, 2026-08-29

The JavaScript MIR capture readers and writers now use the AST function's
`FnAnalysis` array and count directly. `JsFuncCollected` no longer carries a
second pointer/count view, so capture propagation, scope-environment layout,
closure emission, and cleanup all address the same analysis record published by
the indexed function identity. Class-method collection no longer touches that
record before the collection pass allocates it; the AST-owned zero state is
initialized once at publication. This closes the pointer/count mirror left by
P4h without changing Lambda's linked-list capture contract.

This is a deletion-funded **D8.2.4** slice. The two collection fields and their
cleanup/assignment paths were retired; two shared access macros are the only
new structural surface. Against the current external HEAD, the governed
candidate is 314,684 lines and the changed first-party C/C++ and source
counters are `+431/-561 = -130`. No prior deletion credit is reused.

Focused verification after rebuilding the affected targets:

```text
make build                                             # Errors: 0
./test/test_js_script_gtest.exe --gtest_color=no       # 104/104 passed
./test/test_js_mir_emission_gtest.exe --gtest_color=no # 21/21 passed
./test/test_js_opt_gtest.exe --gtest_color=no          # 19/19 passed
./utils/check_ast_tune_loc.sh --base HEAD --cap 314814 --phase-base HEAD
                                                       # candidate 314,684; phase C/C++ -130; source -130
git diff --check                                       # clean
```

P4j subsequently completes the capture fixed point, call-site evidence, and
function-local inference caches; the final **D8.6.4v2** ratchets remain open.

#### P4j implementation record — shared JavaScript profile facts and indexed inference, 2026-08-29

**D8.2.4** requires one function-owned analysis record for facts consumed by
all post-index passes. JavaScript direct-eval, `arguments`, rest/non-simple
parameter, `this`, `new.target`, `with`, reassignment, formal length, return
type, boxed-return class, parameter count, parameter types, and capture storage
now live on the AST function's `FnAnalysis`; `JsFuncCollected` retains only
collection/backend state. Parameter and return inference consume the indexed
node table filtered by `AstFunctionId`, so nested function bodies cannot leak
evidence into an enclosing function. The old recursive parameter/return walks
and their public declarations are deleted in the same slice.

The indexed capture fixed-point is a queued worklist rather than a bounded
ten-round loop; call-site evidence is read directly from `AstIndex.nodes`, and
the function-local declaration caches are owned and freed by `FnAnalysis`.
This closes the remaining pointer/count/fact-table mirrors without changing
the JS ABI or Lambda's linked-list capture contract.

This is a deletion-funded **D8.2.4** slice. Against the 314,814-line HEAD
baseline the governed candidate is 314,506 lines, with changed first-party
C/C++ and source counters `+989/-1,297 = -308`. No prior deletion credit is
reused.

Focused verification:

```text
make build                                             # Errors: 0
./test/test_js_script_gtest.exe --gtest_color=no       # 104/104 passed
./test/test_js_mir_emission_gtest.exe --gtest_color=no # 21/21 passed
./test/test_js_opt_gtest.exe --gtest_color=no          # 19/19 passed
./utils/check_ast_tune_loc.sh --base HEAD --cap 314814 --phase-base HEAD
                                                       # candidate 314,506; phase C/C++ -308; source -308
git diff --check                                       # clean
```

The full Lambda baseline is the final P1–P4 acceptance gate; P5 lowering
families and the historical project-wide 308,711-line target remain outside
this closeout.

#### P4k implementation record — indexed suspension, assignment, return, local, and constructor facts, 2026-08-29

The remaining function-shape scans now consume the shared indexed function
identity. Suspension counts, assignment-name collection, boxed-return checks,
function-body locals, constructor-property discovery, and related body-reference
queries use the indexed owner-filtered dispatch; the former recursive walks and
their duplicate public declarations are retired in the same slice. Synthetic
class-field and block-function fragments therefore use the same AST ownership
and scope projection as ordinary functions. Shared scope writeback also ignores
unused parent-environment holes, which prevents a sparse indexed environment
from being treated as a binding.

This closes the P4 fact/index family under **D8.2.4** without changing JS
semantics or the Lambda linked-list contract. The slice is deletion-funded and
its governed, changed-C/C++, and changed-source LOC deltas are all
non-positive; the aggregate current-tree checker reports candidate 314,192
against the 314,814 HEAD baseline (`+1,492/-2,114 = -622`), and no historical
anchor credit is reused.

Focused verification:

```text
make build                                             # Errors: 0
./lambda.exe js test/js/hljs_highlight.js --no-log     # exit 0
./lambda.exe js test/js/lib_ajv.js --no-log            # exit 0
./lambda.exe js test/js/lib_handlebars.js --no-log     # exit 0
./lambda.exe js test/js/lib_marked.js --no-log         # exit 0
./lambda.exe js test/js/lib_ramda.js --no-log          # exit 0
./lambda.exe js test/js/lib_yup.js --no-log            # exit 0
./lambda.exe js test/js/lib_zod.js --no-log            # exit 0
./lambda.exe js test/js/tune4_closure_scalar_ownership.js --no-log # exit 0
./lambda.exe js test/js/underscore_lib.js --no-log     # exit 0
LAMBDA_TEST_IDLE_TIMEOUT=900 make test-lambda-baseline # 4045/4045 passed
./utils/check_ast_tune_loc.sh --base HEAD --cap 314814 --phase-base HEAD
                                                       # candidate 314,192; phase C/C++ -622; source -622
```

The complete baseline and phase-local LOC checker pass above satisfy the
P1–P4 implementation gate. The historical project-anchor ratchet and the
semantic-family lowering work remain P5 scope.

#### P4l implementation record — deletion-funded indexed-walk retirement, 2026-08-29

The next P4 cleanup slice retires the duplicate recursive lexical-key walk and
its target-containment helpers. Enclosing lexical identity now comes from the
indexed `JsScope`/`NameEntry` chain, while module-block and `for`-initializer
collection reuse the shared `js_ast_visit_children()` child contract. The same
slice removes the dead pattern-kind wrapper, always-true closure/parent-scope
guards, duplicate capture-key branch, and hand-written frame teardown in favor
of the existing `em_frame_dispose()` owner. `jm_collect_let_const_names()` is
also reused for switch lexical collection, so no new AST walk or compatibility
wrapper is introduced.

This is an implementation-only **D8.2.4**/**D8.2.5** slice: indexed identity,
one child-enumeration contract, and pass-owned analysis remain authoritative;
the Lambda/JavaScript semantic boundary and linked-list storage are unchanged.
The generic rewrite of `jm_count_lexical_binding_name_for_slot()` was rejected
after it double-counted sibling lists and regressed Highlight.js; that focused
manual count remains an explicit P4 residual until it can consume identity
keys without changing Annex B behavior.

The deletion-funded ledger is measured against the immediately preceding live
candidate, not a historical anchor:

```text
governed candidate before this slice                        314,192
governed candidate after this slice                         313,692
net governed/source C/C++ change                               -500
current HEAD baseline                                        314,814
current aggregate delta                                      -1,122
phase-local C/C++ and source delta                           -1,122
phase-local additions/deletions                         +1,702/-2,824
```

No blank lines or formatting-only churn was removed. Validation for this
slice was:

```text
make build                                             # errors 0
focused JS library/closure/property matrix             # all exit 0
LAMBDA_TEST_IDLE_TIMEOUT=900 make test-lambda-baseline # 4045/4045 passed
./utils/check_ast_tune_loc.sh --base HEAD --cap 314814 --phase-base HEAD
                                                       # candidate 313,692; phase -1,122
git diff --check                                       # clean
```

No formal-spec ruling or semver changed; P5 lowering-family consolidation and
the historical project-wide target remain open.

### 4.6 P5 — Consolidate lowering by semantic family

P5 is the largest deletion phase. It migrates one semantic family at a time:

1. literals, identifiers, discard, destination, and branch demands;
2. sequencing, blocks, conditions, and loop skeletons;
3. binding/module-slot loads and stores;
4. call argument/root/result plumbing;
5. return and explicit completion routing;
6. common pattern/destructuring structure;
7. function and closure publication;
8. module finalization and link plumbing.

For every family:

- add the shared structural handler and profile semantic callbacks;
- switch both Lambda and JS callers;
- delete both old structural implementations in the same slice;
- retain a generic boxed fallback;
- run boxed-demand differential and forced-GC gates before landing.

This phase does not move all of `transpile-mir.cpp` or `js_mir_*` into one file. File layout follows ownership. Lambda-specific query/element/pattern lowering and JS-specific object/class/eval/generator lowering remain in their language modules.

### 4.7 P6 — Consolidate execution and runtime lifecycle

P6 extracts the common execution shell from proven Lambda and JS clients. Each extracted guard or lifecycle operation replaces and deletes both clients' old copies in the same slice.

Recommended order:

1. current-file and active-module-state restoration;
2. context preparation and runtime/name-pool activation;
3. root/result-home execution boundary;
4. outermost-turn and event-loop ownership;
5. module namespace activation;
6. root-range registration/reset only with the two-client proof and same-slice `runtime-state.cpp` retirement required by §3.9;
7. cleanup and artifact-retention decisions.

The two interpreter frames and completion enums remain. Shared lifecycle code must not introduce a second runtime, event loop, heap, module registry, or stack owner, satisfying **D8.1.3v10**.

P6 also removes stale compatibility aliases only after build/cache versioning proves no retained artifact can import them. Deleting an ABI symbol without that proof is not an acceptable LOC shortcut.

#### P5–P6 implementation record — semantic MIR profiles and shared runtime lifecycle, 2026-08-30

P5 now routes the structural lowering boundaries through demand-carrying `MirValue` profile calls: discard, branch and destination demands; linked sequences and conditions; module-slot loads/stores; call argument/root/result completion; return/completion routing; function publication; and module finalization/link plumbing. Lambda and JS retain semantic callbacks for their truthiness, coercion, language extensions, and boxed fallback, while the shared emitter owns the structural contract. This realizes **D2.4.1–D2.4.3**, **D5.2.1v3**, **D5.3.4**, and **D8.2.6** without collapsing language-specific lowering modules. Binding-aware hoisted declaration writeback preserves source-keyed lexical cells for recursive nested closures; the HLJS fixture covers that regression.

P6 centralizes retained/fresh context-owner binding, result-root publication, current-file, execution-turn, and module-state scopes, plus generic active-module name and slot access, in `runtime-state`. The superseded JS-specific runtime-state aliases and duplicated restoration paths are deleted. Semantic interpreters, frames, completions, and language-owned execution policies remain separate, as required by **D7.2.1** and **D8.1.3v10**.

The final P5/P6 delta is 326,064 to 326,064 governed lines (`0`); changed C/C++ is `+1246/-1253 = -7` and all first-party source is `+1255/-1257 = -2`. Verification passed: coherent debug-profile and release builds; Lambda MIR emission `63/63`; JS MIR emission `21/21`; MIR GC stress `93/93`; JS optimization `19/19`; JS script ownership `104/104`; Lambda/Input baseline `4,061/4,061`; Test262 baseline `40,261/40,261`; the HLJS MIR fixture; `git diff --check`; and the LOC gates. The matched P4 release capture has identical manifests and reports Lambda compiler median `7,745,692 → 5,956,751 us` (`0.769041`) and JS MIR-direct median `327,798,863 → 126,591,876 us` (`0.386188`); JS complete/library MIR volume changes are `+24/+31` instructions with no zero-MIR JS record. This is an implementation-status record, not a change to a formal ruling.

#### Post-P6 implementation record — direct JavaScript frontend retirement, 2026-08-30

Production JavaScript and TypeScript parsing now have one authority: the first-party C parser publishes the indexed AST consumed directly by the interpreter and MIR entry points. The obsolete Tree-sitter AST builder, its production-disabled parser-comparison façade, TypeScript source-preprocess fallback, backend-selection state, and repeated AST fallback calls are retired together. The remaining direct-parser facts—node allocation, operator decoding, and parser errors—are centralized in `js_c_ast_helpers`/`js_scope`; script-owned import/export recording stays with the scope owner. This keeps the single runtime/compilation boundary required by **D8.1.3v10** while preserving semantic profiles and AST ownership under **D2.4.1–D2.4.3**.

The deleted implementation files are `lambda/js/build_js_ast.cpp`, `lambda/js/js_parser_compare.cpp`, and `lambda/ts/ts_preprocess.cpp`; `build_lambda_config.json` no longer compiles the removed comparison façade. The independent C-parser versus grammar differential remains a test-only validator (`418/418` accepted sources) and is not a second production frontend. This is genuine retirement under **D8.6.4v2**: no flag, stub, alternate runtime entry, blank-line deletion, or formatting-only reduction remains.

The post-P6 governed LOC result is `326,064 → 318,980` (`-7,084`), including the direct frontend retirement and its shared-helper consolidation. Validation passed: debug and release builds; C parser `16/16`; JS library `354/354`; JS script ownership `104/104`; JS MIR emission `21/21`; TypeScript `19/19`; parser differential `418/418`; sequential Lambda/Input baseline `4,061/4,061`; and Test262 `40,261/40,261` with zero regressions. The ordinary parallel baseline still exposes its pre-existing nested-process pressure failure in the recursive-stack-fault fixture; its isolated and sequential canonical runs pass, so this record does not weaken or alter the test runner. No formal-spec ruling or semver changed.

---

## 5. Phase Exit Gates

Every phase and subphase must pass all applicable gates before merge.

### 5.1 LOC gates

- governed-scope physical LOC delta from the immediately preceding phase is `<= 0`;
- changed first-party C/C++ added lines minus removed lines is `<= 0`;
- changed first-party source added lines minus removed lines is `<= 0`;
- cumulative governed-scope delta is recorded against both the project and formal anchors;
- deletion ledger names every material retired implementation and its surviving authority;
- no code was moved out of scope or hidden in generated/vendor files;
- `git diff --check` passes.

The LOC report is captured before formatting so formatting cannot obscure implementation movement. The final report also runs the existing formal-anchor checker.

### 5.2 Structural and compiler gates

- core catalog completeness and child-once assertions pass;
- every declared pass fact has actually been produced;
- lowering contains no compiler scope/name re-resolution for migrated families;
- stable node/function/class/binding dumps are deterministic;
- boxed-demand differential mode produces identical results;
- finalized MIR shape/count diagnostics are recorded and material growth is attributed;
- the zero-slack MIR budget passes under **D8.6.1**.

### 5.3 Semantic gates

- focused Lambda and JS fixtures for the migrated family pass;
- Lambda T0 and explicit JIT results match over the affected corpus;
- JS AST backend and default MIR results match over the admitted affected corpus;
- `test_mir_emission_gtest`, `test_js_mir_emission_gtest`, `test_mir_ratchet_gtest`, and `test_mir_gc_stress_gtest` pass where lowering/rooting changed;
- `test_js_opt_gtest` passes where an optimization path changed;
- `make test-lambda-baseline` passes completely;
- `make test262-baseline` reports 40,261/40,261 fully passing, zero failures, zero non-fully-passing tests, zero retries, and no crashed or killed batches.

`test_js_test262_gtest` is never modified to mask a failure. A red test remains red until its JS runtime/compiler root cause is fixed.

### 5.4 Performance gates

Performance is measured only with a release build, never a debug build.

The final consolidation must retain the complete **D8.6.4v2** protocol:

- identical complete Lambda and JS manifests;
- one warm-up and five measured runs;
- median of complete internal parse-through-link compiler time;
- execution, process, cleanup, scheduler, cache hits, retries, and missing samples excluded;
- Lambda compiler time at least 10% lower than the formal baseline;
- JS compiler time at least 20% lower than the formal baseline;
- finalized MIR volume reported for the complete JS corpus and frozen large-library cohort.

The existing capture tool is used:

```sh
./utils/capture_ast_tune_timing.sh --suite lambda --label <label>
./utils/capture_ast_tune_timing.sh --suite js --label <label>
```

Prior measurements showed AST construction was a small share of large-JS compilation while MIR lowering/link dominated. Therefore P1/P2 are correctness and deletion prerequisites; P4/P5 must deliver the compiler-time improvement. No AST micro-optimization is accepted as a substitute for the measured gates.

---

## 6. Deletion Ledger Template

Every phase records this table in its implementation plan or change description:

| Field | Required content |
|---|---|
| phase base | exact commit before the phase |
| governed LOC before/after/delta | physical C/C++ lines in `lambda/runtime` + `lambda/js` |
| changed C/C++ additions/deletions/net | all changed first-party C/C++ files, tests included |
| changed source additions/deletions/net | all changed hand-authored code sources; docs/data/goldens reported but excluded |
| new authority | exact file and symbol now owning the behavior |
| retired implementation | exact deleted symbols/tables/fields/files |
| credited consolidation lines | retired physical lines, replacement physical lines, and net for the named behavior |
| callers migrated | all production callers, including eval/module/batch/interpreter paths |
| residual duplication | explicit list; empty for the migrated family |
| semantic gates | commands and final counts |
| GC/MIR gates | commands and final counts |
| performance evidence | required when the phase changes measured compilation work |

If residual duplication is non-empty because another caller still depends on the old code, the replacement and deletion are not atomic and the slice does not land.

---

## 7. Rejected Directions

### 7.1 One semantic interpreter

Rejected. **D8.1.3v10** requires separate semantic walkers and activation records. A language branch in nearly every expression and statement case would increase LOC, obscure semantics, and invite cross-language mistakes.

### 7.2 One parser or CST

Rejected. **D8.1.1v5** makes the first-party Lambda parser authoritative for ordinary Lambda compilation. JavaScript retains its grammar. Parser/CST decoding is not the expensive duplicated semantic process targeted here.

### 7.3 Tree rewriting/desugaring as unification

Rejected by **D8.2.2**. Form fields preserve single evaluation, source fidelity, and lowering choices without duplicating node kinds or expanding expressions.

### 7.4 A scaffold-first common compiler

Rejected by the phase-local LOC rule. A new common layer above two intact pipelines would initially create three pipelines and make deletion optional. Each common component must replace live code immediately.

### 7.5 Compatibility wrappers between old and new internals

Rejected unless an external/cache ABI proof requires the symbol temporarily. Internal callers are updated in the same phase and the old symbol is deleted. A forwarding function is still code and still counts.

### 7.6 Moving code out of the governed directory

Rejected. **D8.6.4v2** explicitly makes this invalid. A helper moved from `lambda/js` to an uncounted directory is not consolidation.

### 7.7 Unifying JavaScript semantics with Lambda helpers

Rejected by **D1.3** and **S1.11**. JavaScript equality, coercion, property references, arrays, prototypes, descriptors, exceptions, jobs, and environments remain ECMAScript semantics. Only representation-neutral runtime mechanisms are shared.

### 7.8 Inline caches as part of unification

Rejected by **D8.4.1v2**. Shared property/reference kernels and ordinary TypeMap metadata are the allowed design.

### 7.9 Extending C2MIR

Rejected by **D1.6**. `transpile.cpp` remains frozen. A shared AST layout change may receive only the minimum mechanical compatibility edit required to keep the legacy path building; no new behavior is added there.

---

## 8. Completion Definition

This proposal is complete only when all statements are true:

- [ ] Every core node tag has one physical layout and one authoritative child contract.
- [ ] Lambda declarations and loops use the common catalog forms required by **D8.2.2**.
- [ ] `AstIndex` owns stable node, scope, binding, function, and class identities.
- [ ] Lowering never repairs binding or re-resolves a compiler name by spelling.
- [ ] One pass manager runs the complete **D8.2.5** schedule for Lambda and JavaScript.
- [ ] Source contracts and inferred/effective facts are separate under **D3.2.3** and **D3.3.1v2**.
- [ ] `FunctionId` is the sole function-analysis identity; duplicate JS collection indexes and analysis records are gone.
- [ ] Core expression boundaries return full `MirValue` and accept explicit demands under **D8.2.6**.
- [ ] `MirEmitter` is the sole owner of representation conversion, root/final-store policy, and common finalization under **D5.3.4**.
- [ ] Lambda and JS retain separate semantic interpreters but share one execution shell and runtime substrate under **D8.1.3v10**.
- [ ] Every phase and subphase has non-positive governed, changed-C/C++, and changed-source LOC deltas.
- [ ] The deletion ledger proves that replacements and retirements landed atomically.
- [ ] Final governed C/C++ LOC is at most 308,711: at least 2,000 below the project anchor, with a target of at most 308,311.
- [ ] Final governed C/C++ LOC also remains at least 2,000 lines below the older formal anchor.
- [ ] Quantified ledger rows independently credit at least 2,000 net lines to the named compiler/runtime consolidation, excluding unrelated deletion and code movement.
- [ ] The Lambda and JS compiler-time ratchets of **D8.6.4v2** pass under the prescribed release protocol.
- [ ] MIR budgets, forced-GC oracles, Lambda baseline, and complete Test262 baseline pass without weakened gates, masked tests, failures, or retries.

The project is not complete merely because shared structs exist, a common helper was added, or tests remain green. Completion requires deleting the duplicate process and proving the codebase is materially smaller.

---

## Appendix A — Initial Live-Code Map

This appendix is a starting map, not a substitute for re-resolving symbols before each phase.

| Area | Current authority / duplication to inspect |
|---|---|
| shared AST catalog/index | `lambda/runtime/ast-core.hpp`, `lambda/runtime/ast-core.cpp` |
| Lambda AST builder/binding | `lambda/runtime/build_ast.cpp`, `lambda/runtime/ast_build.hpp` |
| JS AST builder/binding | `lambda/js/build_js_ast.cpp`, `lambda/js/js_scope.cpp`, `lambda/js/js_early_errors.cpp` |
| duplicate child descriptions | `lambda/runtime/interp_plan.cpp::interp_visit_children`, `lambda/js/js_ast_children.cpp` |
| pass scaffold | `lambda/runtime/compiler_timing.hpp`, `lambda/runtime/compiler_pass.cpp`, `lambda/js/js_transpiler.hpp::build_js_ast_indexed` |
| Lambda MIR lowering | `lambda/runtime/transpile-mir.cpp` |
| JS MIR phase driver | `lambda/js/js_mir_module_batch_lowering.cpp::transpile_js_mir_ast` |
| JS function/class facts | `lambda/js/js_mir_context.hpp::JsFuncCollected`, `JsClassEntry` |
| JS expression/statement lowering | `lambda/js/js_mir_expression_lowering.cpp`, `lambda/js/js_mir_statement_lowering.cpp` |
| shared MIR emitter | `lambda/runtime/mir_emitter_shared.hpp` (header-owned implementation) |
| Lambda T0 | `lambda/runtime/interp.hpp`, `lambda/runtime/interp.cpp`, `lambda/runtime/interp_plan.cpp` |
| JS T0 | `lambda/js/js_interp.cpp`, `lambda/js/js_interp_env.h` |
| shared runtime/module state | `lambda/lambda-data.hpp::EvalContext`, `lambda/runtime/runtime-state.cpp`, `lambda/runtime/module_registry.cpp` |
| JS semantic state | `lambda/js/js_runtime_state.hpp`, `lambda/js/js_runtime_state.cpp` |
| JS call/property kernels | `lambda/js/js_runtime.cpp`, `lambda/js/js_props.cpp`, `lambda/js/js_runtime_function.cpp` |
| compile/execute entry paths | `lambda/js/js_mir_entrypoints_require.cpp`, module/eval/batch entry points |

## Appendix B — Documentation Reconciliation Record

The initial stale-document set was reconciled on 2026-08-28:

| Document | Reconciliation |
|---|---|
| `doc/Lambda_Formal_Design.md` | Spec 1.38.4 Appendix A now records the P2c `AstIndex` identity publication, P3a truthful JS validation/index pass, P3j conditional indexed prerequisite in the Lambda driver, and P4k indexed function-shape facts alongside the partial traversal, pass-fact, and `MirValue` scaffolding; it names the incompatible core layouts, remaining P5 manual/lowering residue, bare-register residue, current 310,711-line formal LOC result, and still-open proposal-wide closeout. The D8.2 rulings were not changed; older IC examples elsewhere were editorially reconciled with **D8.4.1v2**. |
| `vibe/impl/Lambda_Impl_Tune_Ast (retired).md` | Renamed from `(done)` to `(retired)` and marked as a historical partial record whose G1/G2/G3 closeout was not accepted. Open work points here. |
| `doc/dev/js/JS_01_Compilation_Pipeline.md` | Reverified against the 2026-08-28 tree: function/class/member arrays are exact-sized after count/fill collection, control stacks are dynamic, duplicate pointer identity and orchestration residue are explicit, and **D8.4.1v2** no-inline-cache terminology is restored. |
| `Lambda_Design_Unified_AST.md` | Current continuation links now point here; the retired plan is historical only; `JsLoadIC`/`JsStoreIC` wording was removed and replaced by the formal **D8.4.1v2** boundary. |
| `Lambda_Design_Ast_Interpreter.md` | The retired-plan cross-reference and header were synchronized with landed **D8.1.1v5**; its historical JavaScript Stage-2 section now defers to **D8.1.3v10**, keeps separate completion/frame semantics, reflects dynamic MIR stacks, and rejects per-node ICs under **D8.4.1v2**. |

Future implementation phases update status prose as they land, but do not revise an S#/D# ruling unless the user changes the design. Documentation edits do not earn code-deletion credit.
