# LambdaJS and Lambda Runtime Unification

**Date:** 2026-08-28

**Status:** ACTIVE — P0a LOC gates, P1a–P1d core-layout migrations, and P2a–P2b shared function identity are verified; P0b, P1e, P2c–P6 remain proposed.

**Scope:** The Lambda and LambdaJS AST builders, binding/indexing, compiler pass process, MIR lowering, AST interpreters, and shared runtime substrate. This document does not change either language's semantics, does not extend C2MIR, and does not modify a vendored dependency.

**Formal authority:** **S1.6**, **S1.11**, and **S3.1–S3.3** in [`doc/Lambda_Formal_Semantics.md`](../doc/Lambda_Formal_Semantics.md); **D1.2–D1.6**, **D1.8–D1.10**, **D2.4.1–D2.4.3**, **D3.2.3**, **D3.3.1v2**, **D5.3.4**, **D8.1.1v5**, **D8.1.3v9**, **D8.2.1–D8.2.6**, **D8.4.1v2**, **D8.4.3v2**, and **D8.6.1–D8.6.4v2** in [`doc/Lambda_Formal_Design.md`](../doc/Lambda_Formal_Design.md). The formal specifications win on disagreement.

**Design lineage:** This document specializes the already-confirmed compiler-consolidation rulings **U27–U36** in [`Lambda_Design_Unified_AST.md`](Lambda_Design_Unified_AST.md). It does not create a new ruling series or replace that document's language-neutral catalog. It narrows the next implementation to Lambda and LambdaJS, records the live structural defects that must be removed, and adds the phase-local LOC conservation rule requested on 2026-08-28.

**Related designs:** [`Lambda_Design_JS_Interpreter.md`](Lambda_Design_JS_Interpreter.md), [`Lambda_Design_Ast_Interpreter.md`](Lambda_Design_Ast_Interpreter.md), [`Lambda_Design_Runtime_Error_Handling.md`](Lambda_Design_Runtime_Error_Handling.md), [`Lambda_Design_Stack_Frame_JS.md`](Lambda_Design_Stack_Frame_JS.md), and [`vibe/impl/Lambda_Impl_Tune_Ast (retired).md`](<impl/Lambda_Impl_Tune_Ast (retired).md>). The last file is historical partial implementation evidence; its unchecked closeout items are carried forward here and it is not an active plan.

---

## 1. Decision

Lambda and LambdaJS will share one structured compiler process and one runtime substrate while retaining separate language semantics.

The end state is:

```text
Lambda source -> first-party Lambda parser -> Lambda syntax builder --+
                                                                    |
JS source     -> Tree-sitter JavaScript -> JS syntax builder -------+
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

The JS builder predeclares bindings, looks up identifiers during CST construction, and writes `AstIdentNode::entry` and `AstNode::type`. MIR lowering later calls `js_scope_lookup()` again. It contains explicit shadow checks because the AST builder's current scope is stale during lowering.

Lambda likewise performs substantial binding and inference during direct AST reduction. Both lanes use `NameScope` and `NameEntry`, but neither exposes the complete stable identity graph required by **D8.2.4**.

This has four costs:

- lowering can disagree with the builder about shadowing;
- capture, call, and direct-call analyses rescan by pointer or spelling;
- builder-time inferred types become difficult to distinguish from source contracts, contrary to **D3.2.3**;
- interpreter and MIR backends can grow separate fixes for the same binding defect.

### 2.4 `AstIndex` is a scaffold, not yet the authority

The live `AstIndex` assigns dense node and function IDs and records parent and owner-function links. It does not yet assign scope, binding, or class IDs; function IDs have no production consumers outside index construction.

JavaScript separately counts functions/classes, walks again to populate exact-sized metadata, builds a pointer-keyed function index, and stores another `FnAnalysis` inside `JsFuncCollected`. The comment in `transpile_js_mir_ast()` explicitly treats the shared function count as only an upper-bound hint.

This is the duplication that the index should delete, not coexist with.

### 2.5 Pass facts currently overstate reality

`build_js_ast_indexed()` initializes its pass manager with `AST | BOUND | VALIDATED` and then builds the index. The actual `js_check_early_errors()` validation runs afterward in the entry points.

The current pass manager is a useful prerequisite checker, but it is not yet the **D8.2.5** production schedule. Only a few operations are registered with it; the large numbered JS phase sequence and Lambda's later analyses remain manually ordered.

### 2.6 `MirValue` exists around a bare-register core

`MirValue` already carries the physical register, full type contract, semantic type, representation, provenance, demand, rooting home, scalar home, and pending-completion lane. `MirEmitter` already owns common frame, root, representation-conversion, call-effect, and finalization machinery.

The principal Lambda and JS expression functions still return `MIR_reg_t`. Consumers consequently re-derive whether a register is boxed, native, rooted, scalar-backed, discarded, or branch-only. This is the unfinished **D2.4.1–D2.4.3** and **D8.2.6** migration.

### 2.7 Execution is more unified than compilation

The two AST interpreters correctly keep separate semantic frames:

- Lambda has slot-planned `InterpFrame` records and `EvalSignal`.
- JavaScript has `JsInterpFrame`, traced lexical environments, references, labels, `this`/`new.target`/home-class state, and JS completion kinds including throw, yield, and await.

**D8.1.3v9** explicitly requires separate semantic walkers and activation records while sharing the runtime, heap, event loop, module registry, module state, and call kernels. A single semantic interpreter switch would add language branches to nearly every node and would risk applying Lambda truthiness (**S3.1–S3.3**) to JavaScript.

The duplication worth removing here is the execution shell: context preparation, current-file and module-state guards, root setup, execution-turn lifecycle, result publication, event-loop ownership, and cleanup.

---

## 3. Target Architecture

### 3.1 Syntax adapters remain language-specific

Lambda retains its first-party lexer and recursive-descent/Pratt parser under **D8.1.1v5**. JavaScript retains the vendored Tree-sitter JavaScript grammar and its Lambda-side builder. No vendor source is modified.

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
| P0 | P0a counters complete; P0b catalog assertions and timing manifests pending | stale assertions/helpers and any superseded catalog test code | `0` |
| P1 | P1a iterator-for, P1b declarator, P1c assignment/declaration-wrapper, and P1d condition-loop layouts complete | private loop-layout casts, Lambda declaration-as-assignment casts, duplicate assignment/wrapper structs, old condition-loop tags, and migrated core-child cases | `-110` |
| P2 | P2a/P2b source and synthetic `FunctionId` authority landed; remaining scope/binding/class IDs and one binding/index graph | lowering name repair, pointer indexes, lookup caches made obsolete, repeated identity walks | `<= -500` |
| P3 | one compilation unit, typed pass schedule, and one compile lifecycle | duplicated parse/build/validate/index/link/cleanup orchestration and false fact publication | `<= -800` |
| P4 | `FunctionId`-owned analysis and graph worklists | duplicate `JsFuncCollected` analysis, count/fill scans, per-pass AST caches and propagation loops | `<= -1,200` |
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

The final project cap of 308,711 is intentionally not claimed: the current candidate is still 310,711. P0b must record complete release-host timing manifests. The catalog-completeness test is deferred to its deletion-funded P1 node-family slice, because P0a retired no traversal implementation that could honestly fund new test source.

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
required by **D8.1.3v9**; only structural storage and traversal were unified.

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

1. labels and remaining block/match/try child edges;
2. extension-only JS child visitor;
3. catalog-completeness assertions over the migrated core forms.

For each family, the same slice updates both producers and all consumers, switches the shared visitor, deletes the matching cases from `interp_visit_children()` and `js_ast_children.cpp`, and proves catalog completeness. No compatibility struct or second tag interpretation remains.

P1 exits only when core ownership is described once. Language semantic walkers may still switch on a core node to evaluate it, but they delegate child ownership/enumeration to the common contract and do not carry another structural catalog.

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

P2c now owns scope/binding/class IDs and the shared binding graph; no callable
identity fallback remains in normal indexed compilation.

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

The JS index pass no longer claims `VALIDATED` before early errors. Manual phase timing and repeated cleanup labels disappear as their paths migrate.

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

The two interpreter frames and completion enums remain. Shared lifecycle code must not introduce a second runtime, event loop, heap, module registry, or stack owner, satisfying **D8.1.3v9**.

P6 also removes stale compatibility aliases only after build/cache versioning proves no retained artifact can import them. Deleting an ABI symbol without that proof is not an acceptable LOC shortcut.

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

Rejected. **D8.1.3v9** requires separate semantic walkers and activation records. A language branch in nearly every expression and statement case would increase LOC, obscure semantics, and invite cross-language mistakes.

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
- [ ] Lambda and JS retain separate semantic interpreters but share one execution shell and runtime substrate under **D8.1.3v9**.
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
| `doc/Lambda_Formal_Design.md` | Spec 1.38.2 Appendix A now records partial `AstIndex`, traversal, pass-fact, and `MirValue` scaffolding; it names the incompatible core layouts, false validation publication, bare-register residue, current 310,711-line formal LOC result, and still-open combined closeout. The D8.2 rulings were not changed; older IC examples elsewhere were editorially reconciled with **D8.4.1v2**. |
| `vibe/impl/Lambda_Impl_Tune_Ast (retired).md` | Renamed from `(done)` to `(retired)` and marked as a historical partial record whose G1/G2/G3 closeout was not accepted. Open work points here. |
| `doc/dev/js/JS_01_Compilation_Pipeline.md` | Reverified against the 2026-08-28 tree: function/class/member arrays are exact-sized after count/fill collection, control stacks are dynamic, duplicate pointer identity and orchestration residue are explicit, and **D8.4.1v2** no-inline-cache terminology is restored. |
| `Lambda_Design_Unified_AST.md` | Current continuation links now point here; the retired plan is historical only; `JsLoadIC`/`JsStoreIC` wording was removed and replaced by the formal **D8.4.1v2** boundary. |
| `Lambda_Design_Ast_Interpreter.md` | The retired-plan cross-reference and header were synchronized with landed **D8.1.1v5**; its historical JavaScript Stage-2 section now defers to **D8.1.3v9**, keeps separate completion/frame semantics, reflects dynamic MIR stacks, and rejects per-node ICs under **D8.4.1v2**. |

Future implementation phases update status prose as they land, but do not revise an S#/D# ruling unless the user changes the design. Documentation edits do not earn code-deletion credit.
