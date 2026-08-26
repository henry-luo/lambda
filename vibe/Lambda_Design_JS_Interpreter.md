# LambdaJS AST Interpreter — Page-Integrated Tier-0 and Mixed-Tier Design

**Date:** 2026-08-26

**Status:** PROPOSED — architecture and staged implementation plan; no implementation is claimed by this document.

**Scope:** LambdaJS execution inside the shared Lambda runtime, including page-level coexistence with Lambda behavior/app code. This document specializes the interpreter direction established by **AI21**, the shared-AST rules **D8.2.1–D8.2.5**, and the accepted DOM-state decisions **ES10–ES13**. It does not change either language's observable semantics, does not extend C2MIR, and does not introduce a bytecode VM.

**Formal authority:** **D1.1–D1.10**, **D4.1.1v2**, **D4.1.4v4**, **D4.5.1v3**, **D4.6.1v2–D4.6.2v2**, **D5.1.1–D5.1.4**, **D5.3.2–D5.3.4**, **D5.4.1–D5.4.4**, **D6.2.1–D6.2.4**, **D6.3.1**, **D7.2.1**, **D8.1.1v5**, **D8.2.1–D8.2.6**, **D8.4.1v2**, **D8.4.3v2**, and **D8.6.1–D8.6.4v2** in [`doc/Lambda_Formal_Design.md`](../doc/Lambda_Formal_Design.md). The formal specification wins on disagreement.

**Related designs:** [`Lambda_Design_DOM_State.md`](Lambda_Design_DOM_State.md) (ES10–ES13, EO1–EO6), [`Lambda_Design_Runtime_Globals.md`](Lambda_Design_Runtime_Globals.md) (RG0–RG14), [`Lambda_Design_Ast_Interpreter.md`](Lambda_Design_Ast_Interpreter.md) (AI1–AI22), [`Lambda_Design_Unified_AST.md`](Lambda_Design_Unified_AST.md) (U1–U36), [`Lambda_Design_Runtime_Error_Handling.md`](Lambda_Design_Runtime_Error_Handling.md), [`Lambda_Design_Stack_Frame_JS.md`](Lambda_Design_Stack_Frame_JS.md), [`doc/dev/js/JS_01_Compilation_Pipeline.md`](../doc/dev/js/JS_01_Compilation_Pipeline.md), [`JS_04_MIR_Lowering.md`](../doc/dev/js/JS_04_MIR_Lowering.md), [`JS_05_Functions_Closures.md`](../doc/dev/js/JS_05_Functions_Closures.md), [`JS_08_Iterators_Generators.md`](../doc/dev/js/JS_08_Iterators_Generators.md), and [`JS_09_Async_Modules.md`](../doc/dev/js/JS_09_Async_Modules.md).

---

## 1. Decision

LambdaJS will gain a boxed AST-walking Tier-0 executor over the existing shared `AstNode` graph. It will be a **JavaScript-semantic walker**, not another arm in Lambda's `eval_expr()` switch. On a page it is one profile of the same integrated interpreter service as Lambda T0. The profiles share the runtime substrate below the language boundary:

- `Item` values and the Lambda heap;
- the page's `Runtime`, canonical `EvalContext`, module registry, and event-loop owner;
- precise side-stack rooting and number ownership;
- `AstIndex`, pass scheduling, source spans, counters, and debugging hooks;
- the existing JavaScript runtime/property/call/iterator/promise helpers;
- the same module-state and name-pool contracts used by generated JavaScript.

It does **not** share Lambda truthiness, operator selection, arrays/maps, assignment semantics, closure capture, statement completion, lexical/global environments, or internal control records. This division follows **D1.3**: guests reuse contracts below the semantic boundary and never inherit another language's coercion or object model.

The retained JavaScript compilation/runtime unit is named **`JsScript`** and is a real subtype of the existing **`Script`** owner. A `JsScript` reuses the base source, pool, profile, AST/index, module-state identity, T0 plan/slab, imports, cache, debug, and MIR-artifact fields; it adds only JavaScript parse-goal and persistent semantic facts needed by both T0 and T1. MIR modules remain derived caches attached through the inherited `Script` fields, consistent with **D1.7** and **D8.2.1**.

The shipped end state is:

```text
source
  -> parse + shared JS AST
  -> early errors + binding + indexed semantic plans
  -> JsScript
       -> T0: boxed JavaScript AST walker
       -> T1: per-definition boxed MIR satellite when hot/required
       -> eager whole-script MIR only when explicitly selected or fail-closed policy requires it
```

MIR-interp remains a backend diagnostic. It is not the JavaScript AST tier because it still pays complete AST-to-MIR lowering.

### 1.1 Why this is a separate walker

LambdaJS already aliases 53 shared core node kinds and retains only a small JavaScript-specific extension range. Structural convergence is therefore sufficient to share traversal, indexing, frame machinery, and pass products. It is not sufficient to share evaluation semantics.

Examples of same-shape/different-meaning nodes include:

| Shared shape | Lambda meaning | JavaScript meaning |
|---|---|---|
| `AST_NODE_BINARY` | Lambda numeric/collection helpers and Lambda truthiness | `ToPrimitive`, string-or-number `+`, JS equality, `ToInt32`, operand-returning logical operators |
| `AST_NODE_ARRAY` | Lambda list/array construction and COW rules | JavaScript Array exotic object, holes, spread, prototype, descriptors |
| `AST_NODE_MAP` | Lambda map/object literal | JavaScript object literal, computed keys, accessors, methods, spread, `__proto__` semantics |
| `AST_NODE_ASSIGN` | Lambda binding/container rules | Reference evaluation, one-time LHS evaluation, strict writes, TDZ/const, logical assignment |
| `AST_NODE_FUNC` | immutable Lambda snapshot captures under S9.1.4 | mutable by-reference lexical bindings and per-iteration cells |
| `AST_NODE_MEMBER_EXPR` | value lookup | Reference with base, receiver, optionality, `super`/private behavior and Get→Call `this` |

Putting both languages in one semantic switch would either branch on language at nearly every case or accidentally reuse the wrong contract. A profile-owned walker keeps the boundary explicit while still sharing one activation, rooting, heap, call, module, scheduling, and debugging substrate. “Separate walker” in this document never means a separate runtime, collector, side stack, event loop, or module registry.

### 1.2 Page-level integration decision

The page is one isolate. Its Lambda packages/templates and JavaScript realm run under the same `Runtime`, canonical `EvalContext`, heap generation, dynamic `NamePool`, module registry, module-state ID space, scheduler/event-loop owner, and root/number side-stack pair. This is required by **D1.2–D1.5**, **D5.3.3**, **D5.4.1–D5.4.4**, **D6.3.1**, and accepted DOM-state decisions **ES10–ES13**.

The selected boundary is:

| Facility | Decision | Reason |
|---|---|---|
| `Runtime` | one per document isolate | one lifecycle, script catalog, immutable module-definition registry, and derived-code cache owner |
| `EvalContext` | one canonical context | one TLS identity and one set of context capsules; no context switch inside an event/callback chain |
| module registry and module-state IDs | shared | cross-language imports, namespace identity, circular loading, and teardown need one authority |
| GC heap | shared | `Item` has no heap identity; callbacks, errors, namespaces, template state, and DOM wrappers cross language boundaries directly |
| root and number side stacks | shared | nested Lambda→JS→Lambda calls must remain one precisely rooted LIFO chain |
| native C stack | naturally shared | both walkers use native control frames; no VM value stack is added |
| interpreter service | one kernel, two semantic evaluators | entry/exit, roots, calls, tiers, tracing, counters, and faults unify; language semantics do not |
| AST/source pools | per `Script` / `JsScript` owner | compiler artifacts are non-GC content with independent source lifetimes |
| mutable runtime state | one `EvalContext` owner, split lazy capsules | common modules, roots, async work, page services, and activation state must not be hidden inside a JS megastruct |
| JS realm state | one optional `JsRealmState` capsule | `globalThis`, intrinsics, prototypes, JS environments, and observable realm caches remain ECMAScript-specific |
| lexical/global environments | profile-owned | Lambda immutable snapshot captures and JavaScript mutable binding cells are observably different |
| DOM/view storage | native Radiant arena/state store | **D4.5.1v3** remains the seam; scripts exchange generation-checked projections and `Item` values, not raw ownership |

```text
DomDocument  (native DOM/view arena + canonical interaction state)
└─ Runtime  (one document isolate)
   ├─ runtime script catalog
   │  ├─ Script                  [Lambda AST/facts/artifacts]
   │  └─ JsScript : Script       [same base owner + JS-only facts]
   ├─ ModuleRegistry  [one path/namespace/loading authority]
   └─ EvalContext  [one TLS identity]
      ├─ Heap + dynamic NamePool + module-state slabs
      ├─ root side stack + number side stack
      ├─ ContextRootRegistry + InterpreterState
      ├─ AsyncRuntimeState + ContextModuleState
      ├─ PageRuntimeState + template/Jube service capsules
      ├─ JsRealmState  [optional JS-only realm semantics]
      └─ integrated interpreter service
         ├─ Lambda evaluator → Lambda frame payload / EvalSignal
         └─ JS evaluator     → JS frame payload / JsCompletion/JsReference
```

This is tighter than two interpreters sharing only an API. A Lambda behavior handler can call a JS-exposed callable, which can synchronously call a Lambda callback, with every activation nested in the same context, heap, side-stack watermarks, module registry, and cross-language stack trace. It is deliberately not one language-neutral evaluator: same-shaped AST nodes still have different observable algorithms.

### 1.3 Goals

1. Skip MIR construction and link work for cold/run-once JavaScript.
2. Preserve JavaScript behavior by routing semantic operations through the same `js_*` runtime helpers as MIR.
3. Make the typed AST plus persistent semantic facts the executable source of truth.
4. Support safe T0↔T1 calls through the existing `JsFunction` call/construct capabilities.
5. Replace copied-closure/write-back behavior with real environment records shared by both tiers.
6. Provide a per-node execution surface for debugging, profiling, and future hot reload.
7. Fail closed before observable execution for unsupported unit shapes.
8. Establish exact differential gates against the current MIR implementation and Test262 baseline.
9. Execute page JS, Lambda app templates, and Lambda UA behavior in one document isolate without context, heap, or registry switching.
10. Make cross-language nesting visible as one activation chain for rooting, cleanup, diagnostics, and stack traces.

### 1.4 Non-goals

1. No bytecode or additional resident IR.
2. No unboxed interpreter lane.
3. No general on-stack replacement or arbitrary interpreter-PC transfer to MIR.
4. No inline caches or mutable per-AST-site property caches under **D8.4.1v2**.
5. No C2MIR work under **D1.6**.
6. No attempt to inherit Lambda language semantics for common-shaped nodes.
7. No promise of full ECMAScript coverage beyond the features admitted by LambdaJS's profile under **D1.1**.
8. No mid-execution fallback that repeats or skips already-observable side effects.
9. No merging of `globalThis` with Lambda package scope, or of JS environment records with Lambda closure slots.
10. No second page heap, JS-only root stack, or JS-only event loop hidden behind the shared `Runtime`.

---

## 2. Current Baseline and the Actual Gap

The required frontend representation already exists:

- `JsAstNodeType` is `AstNodeType` and `JsAstNode` is `AstNode`;
- core expressions, statements, functions, classes, patterns, and modules use shared node shapes;
- every node has a source span and can receive a stable `AstNodeId`;
- `NameEntry`, `Type*`, `FnAnalysis`, and the `Item` value model are shared;
- `js_ast_children.cpp` already holds a centralized, source-order child description;
- JavaScript runtime semantics are exposed through boxed C helpers.

The missing executor is not the only gap. The following lifetime, phase, and page-integration choices prevent a correct integrated tree walker:

1. **The AST owner is transient.** `JsTranspiler` destroys its `AstIndex`, parser tree, name pool, and AST pool after the current run. An interpreted function retained by a timer, DOM listener, Promise reaction, module namespace, or returned closure would then point into freed source/AST storage.
2. **Authoritative semantic facts are MIR-session-owned.** `JsFuncCollected`, class tables, `module_consts`, capture layouts, strict/direct-eval flags, and several TDZ/Annex-B decisions are computed inside `JsMirTranspiler`. `fn->analysis` currently points at `JsFuncCollected::analysis`, whose surrounding storage is destroyed with the lowering session.
3. **Static name discovery is tied to MIR emission.** Property-name specifications are accumulated as lowering asks for name indices, then sealed before realm construction. T0 needs the same complete static name image without emitting MIR.
4. **Function values know only native/MIR bodies.** `JsFunction` has `func_ptr`, native bodies, closure slot arrays, `invoke`, and `construct`, but no AST definition, retained script owner, or interpreted lexical environment.
5. **JavaScript has no retained `Script` subtype in the runtime catalog.** Lambda `Script` already owns the common source/pool/AST/index/profile/module-state/T0/T1 lifecycle, while the current JS frontend is a standalone lowering session that duplicates several of those fields. The missing owner is `JsScript : Script`, not a parallel record or catalog.
6. **There is no common interpreter activation chain.** Lambda T0 keeps `InterpState::top`; JS compiled code relies on JS-specific ambient call state. A JS walker added beside that unchanged would produce two backtrace, depth, cleanup, and reentrancy authorities even though both run on the same native and side stacks.
7. **JS root ranges mix durable realm state with activation-shaped state.** Intrinsics, queues, and cached namespace Items correctly require context-owned roots. `with`, `super`/`this`, module selection, and other execution-temporary stacks should instead be frame/environment state in T0. The design must not blindly clone every current fixed JS stack into a second interpreter stack.
8. **Page bootstrap and teardown still have more than one construction history.** `Runtime` already owns the retained heap, canonical `EvalContext`, module registry, module-state ID allocator, scheduler, and script list, while `EvalContext::js_state` is already context-owned. The integrated design makes that ownership mandatory for every page path rather than relying on hand-built partial runtimes or later reconciliation.
9. **`JsRuntimeState` is context-owned but still over-broad.** It currently combines ECMAScript realm identity, Node/DOM host wrappers, module instantiation, async queues/timers, compiler recovery and deferred MIR, test harness state, heap epochs/root registries, and activation-temporary `this`/`new.target`/`super`/arguments/call-depth fields. Moving a global into one context struct solved process-global isolation; it did not prove that every field has the same lifetime or semantic owner. A common Lambda+JS page needs those responsibilities split under `EvalContext`, not merely reached through it.

The current large-script policy does not solve these gaps. It chooses MIR's interpreter interface only after the source has been parsed, analyzed, and lowered to a complete MIR module. The new tier must decide before `jit_init()` and before any MIR module exists.

---

## 3. Normative Constraints

### JSI1 — JavaScript semantics stay in the JavaScript profile

The JavaScript walker selects JavaScript runtime helpers and JavaScript control rules. Shared node identity does not imply shared semantics (**D1.3**, **D8.2.1–D8.2.2**).

### JSI2 — `JsScript` is the retained owner

`JsScript` publicly extends `Script`. The inherited base is the only carrier for source ownership, `Input` pool/name/type storage, profile, canonical reference, AST/index, module-state ID, imports, cache state, T0 plan/slab, debug metadata, and MIR context. `JsScript` adds only JavaScript-specific parse-goal and persistent semantic facts. Any `JsFunction` whose body references that AST points back to its owning `JsScript`; the runtime retains the owner beyond every callback/function that can execute it (**D1.7**, **D6.2.1**, **D8.2.1**).

### JSI3 — One authoritative binding/analysis pipeline

Binding, early validation, strictness, hoisting, TDZ, Annex B, captures, class facts, module slots, and effect/suspension facts are computed before backend selection and stored on the `JsScript` or in ID-keyed side tables. MIR lowering consumes these facts and may not repair or independently reconstruct binding (**D8.2.4–D8.2.5**).

### JSI4 — T0 is boxed-only

Every interpreter-visible value is an `Item`. Wide scalar lifetime follows the same side-number ownership contract as Lambda T0 and MIR. T0 never grows a native/unboxed specialization lane.

### JSI5 — Precise rooting only

Every live `Item` across child evaluation, helper calls, callbacks, or other `MAY_GC` boundaries occupies a side-root slot or a traced heap object. No conservative C-stack scan is restored (**D1.5**, **D5.3.2–D5.3.3**).

### JSI6 — References are explicit

Identifier/member/private/`super` evaluation produces either a value or a `JsReference`. Assignment, delete, update, compound assignment, optional call, and method call consume that reference. A plain `Item` is not sufficient to represent an ECMAScript Reference Record.

### JSI7 — JavaScript completions are structured

The walker uses JavaScript completion kinds `NORMAL`, `RETURN`, `THROW`, `BREAK`, and `CONTINUE`, with rooted value payload and optional stable label identity. Lambda's `ERROR_SKIP` and Lambda-specific tail signals are not imported as JavaScript semantics.

### JSI8 — Throws remain explicit returned completions

A fallible helper's ERROR-tagged `Item` is converted immediately into a `THROW` completion in the current activation. Every crossed activation runs its own `finally`, IteratorClose, environment cleanup, and root/number epilogue before returning the same error identity (**D1.4v3**, **D8.4.3v2**).

### JSI9 — Calls retain the existing capability authority

Every dynamic call goes through `fn->invoke`; construction requires `fn->construct` and an explicit `newTarget` operand. AST execution is a function body kind behind those entries, not a parallel call dispatcher (**D6.2.2v2**).

### JSI10 — No inline caches

Named/indexed/private accesses call the shared JavaScript reference/property kernels. `AstIndex` may hold immutable analysis facts, counters, or source/debug state, but not mutable property-result caches (**D8.4.1v2**).

### JSI11 — Fallback is pre-execution and counted

The support scan runs after semantic planning and before declaration instantiation or any user-visible action. A rejected `JsScript` takes the existing whole-script MIR path. Once T0 starts executing a script, unsupported nodes cannot cause whole-script replay.

### JSI12 — Mixed-tier capture uses one environment ABI

T0 and T1 use the same environment records and binding cells for every binding that can escape, be captured, be observed by direct eval, or require per-iteration identity. Copied-slot adapters are not the mixed-tier ABI.

### JSI13 — Suspension stays compiled first; durable T0 continuations come later

Until heapified interpreter continuations land, generator/async/top-level-await shapes fail the T0 support scan or enter an already-prepared T1 satellite before observable execution. The final design extends the interpreter with durable continuations; it never retains a native frame across suspension.

### JSI14 — Function body kind is explicit

`JsFunction` declares `NATIVE`, `MIR`, or `AST`; null pointer inference never selects semantics (**D6.2.2v2**).

### JSI15 — MIR exists only after T1 selection

`jit_init()`, MIR lowering, linking, and code publication occur only inside the selected T1 path. T0 page execution does not create an empty MIR context as an ownership token.

### JSI16 — MIR-interp is a backend diagnostic

MIR-interp may execute an already-produced artifact for differential/backend work. It is not the AST tier and does not satisfy a zero-lowering T0 gate.

### JSI17 — Admission uses semantic facts

T0 support is decided from the complete indexed semantic plan before declaration instantiation. Node-kind-only admission is insufficient.

### JSI18 — Promotion occurs at function entry

No arbitrary interpreter program counter or active frame is transferred to MIR. Hotness publishes a boxed satellite for a later ordinary entry, aligned with **D8.1.1v5**.

### JSI19 — Static property names are planned once

`JsScript` discovers and seals the static property-name image before realm/global construction. T0 and T1 consume the same linked `NameId`s.

### JSI20 — Runtime ownership retains script generations

The runtime script catalog/module registry retains every `JsScript` generation that can still be reached by a function, callback, job, module namespace, or continuation.

### JSI21 — Differential and ownership gates are mandatory

T0/T1 semantic comparison, tier crossings, forced GC, page coexistence, Test262 partitioning, and release measurements are implementation gates, not follow-up polish (**D1.10**, **D8.6.3–D8.6.4v2**).

### JSI22 — No substitute machine or retired path

This work adds no bytecode, C2MIR behavior, vendor patch, conservative stack scan, or fifth allocation mechanism (**D1.5–D1.6**, **D4.1.4v4**).

### JSI23 — A page has one execution isolate

Lambda and JavaScript on one `DomDocument` use the same `Runtime` and canonical `EvalContext`. A nested event, callback, import, or behavior dispatch validates that identity; it never swaps to a language-owned context (**D5.4.1**, **ES10–ES13**).

### JSI24 — The GC heap is shared

All GC-managed Lambda and JavaScript values for the page allocate in `EvalContext::heap`. A collector cycle traces both languages' active frames and context-owned roots in one graph. There is no cross-heap `Item` state or clone-on-call membrane (**D1.2–D1.5**, **D6.3.1**).

### JSI25 — The side stacks are shared

Lambda and JavaScript activations reserve nested windows from the same `Context::side_root_*` and `side_number_*` regions. No JavaScript operand/value/root stack is added. Durable realm/job state remains in traced heap objects or registered context root ranges (**D5.1.1–D5.1.4**, **D5.3.3**).

### JSI26 — One interpreter kernel, profile-owned semantics

A common activation service owns frame entry/exit, root and number watermarks, limits, current-source metadata, counters, tier hooks, and activation chaining. Lambda and JavaScript retain separate node evaluators, frame payloads, references/environments, and internal completions. Profile selection occurs at activation entry, not as a language branch in every shared node case (**D1.3**, **D8.2.1**).

### JSI27 — Scheduling has one owner and ordered lanes

The `EvalContext` owns one event-loop/scheduler service. JavaScript jobs/microtasks retain JavaScript checkpoint semantics; Lambda task resumes retain their macrotask ordering under **D6.3.1**. “One event loop” does not mean one undifferentiated FIFO.

### JSI28 — Names share one context identity domain

Per-script static spelling tables may remain owner-local, but every runtime property/module name links into the page's dynamic `NamePool`/`NameId` domain. A JavaScript script cannot publish a competing property identity table (**D4.6.1v2–D4.6.2v2**).

### JSI29 — Script and module identity are runtime-wide

`JsScript : Script` is stored directly as a `Script*` in `Runtime::scripts` and `script_index`; no wrapper header, parallel JS catalog, or `void* language_owner` indirection is introduced. The inherited `profile`, `reference`, cache fields, and `module_state_id` remain the one script identity. `Runtime::next_module_state_id` remains the only allocator, the canonical module registry remains the descriptor authority, and `EvalContext::module_states` remains the only slab index space.

### JSI30 — Realms and lexical environments remain separate

Sharing a context and heap does not merge Lambda package scope with `globalThis`, Lambda snapshot captures with JS binding cells, or Lambda collection semantics with JS objects. Cross-language access goes through declared callable/namespace/VMap membranes using `Item` (**D1.2–D1.3**, **D6.2.2v2**).

### JSI31 — The Radiant memory seam remains explicit

DOM/view nodes and packed interaction state remain native arena/state-store allocations. Lambda and JavaScript heap values refer to them only through the existing pin, generation-check, and copy-as-value contracts (**D4.5.1v3**, **ES2**, **ES10–ES12**). A shared script heap does not make DOM arena pointers GC objects.

### JSI32 — `EvalContext` is the state owner, not a state megastruct

Mutable isolate state belongs to the canonical `EvalContext`, partitioned into lazy, non-moving capsules by lifetime and semantic responsibility. Unification does not mean copying every field into `EvalContext` or renaming `JsRuntimeState` to a generic megastruct. It means common facilities have one common capsule, while JavaScript-only realm semantics have one optional JS capsule (**D5.4.1–D5.4.2**, **RG0**, **RG2**, **RG12–RG13**).

### JSI33 — Activation state is not realm state

`this`, `new.target`, arguments handoff, private-home class, `super` initialization, active module selection, interpreter `with` scope, call depth, and current source are dynamic activation facts. They move to the shared activation chain with a JS payload, or to a compatibility generated-call activation during migration. T0 and T1 may not keep two authoritative copies.

### JSI34 — One TLS root remains the execution authority

`context` remains the sole sanctioned runtime TLS root under **D5.4.1**. The current `js_active_runtime_state` may remain only as a checked transitional derived cache; the endpoint hoists `context->js_realm` at JS entry or passes it explicitly through cold/native boundaries. It never becomes a second owner, rebinding mechanism, or independently mutable execution identity.

---

## 4. `JsScript`: Ownership and Persistent Facts

### 4.1 Required shape

The required relationship is inheritance, matching the existing `Transpiler : Script` pattern and **D8.2.1**:

```c
struct JsScript : Script {
    JsParseGoal parse_goal;       // classic script, ES module, or admitted TS profile
    JsScriptFacts* js_facts;      // strict/Annex-B/eval/binding/class/module/property facts
};
```

`JsScriptFacts` may internally contain the post-build scope lookup cache, ID-indexed `JsBindingPlan`, `JsFunctionPlan`, `JsClassPlan`, `JsModulePlan`, and the sealed `PropertyKeySpec` image. Those are JavaScript-only because they encode TDZ, Annex B, direct eval, live bindings, private names, class initialization, or ECMAScript property discovery. A fact already representable in shared `FnAnalysis`, `FnFramePlan`, `AstIndex`, or the module descriptor is stored there instead.

The following are explicitly inherited and must not be redeclared in `JsScript`: `reference`, `directory`, `source`, `profile`, `module_state_id`, `ast_root`, `ast_index`, `current_scope`, `pool`, `arena`, `name_pool`, `type_list`, `direct_imports`, `interp_plan`, `interp_slab`, all `interp_*` tier fields, `jit_context`, `main_func`, `debug_info`, `func_name_map`, and cache/lifecycle flags. If source length proves useful to every parser, it is promoted once to `Script`; otherwise it is a JS-only field inside `JsScriptFacts`, not a second source owner.

The inherited `profile` is always `&js_profile` for JavaScript units. Checked downcast helpers use that invariant:

```c
static inline JsScript* script_as_js(Script* script) {
    return script && script->profile == &js_profile ? (JsScript*)script : NULL;
}
```

No C++ virtual base is required. Cleanup and language-specific cold operations dispatch through the already-known `LangProfile`; if cleanup needs a hook, it is added once to `LangProfile` rather than by adding a wrapper record.

### 4.2 Builder/session relationship

`JsTranspiler` becomes the JavaScript analogue of `Transpiler : Script`:

```c
struct JsTranspiler : JsScript {
    Runtime* runtime;
    TSParser* parser;
    TSTree* tree;
    char* normalized_source;
    StrBuf* error_buf;
    struct hashmap* type_registry;
    // counters, current builder mode, and MIR-lowering-local state only
};
```

Construction writes persistent fields into the inherited `JsScript` prefix. A `js_script_adopt_transpiler()` operation transfers that prefix to the stable runtime owner exactly once, analogous to `script_adopt_transpiler()`. Parser/tree, normalized scratch source, diagnostic buffers, recursion/mode counters, and MIR register/symbol tables remain in the builder tail and are destroyed after adoption. The stable owner never depends on a leaked `JsTranspiler`.

This refactor deletes the present duplication of `ast_pool`, `name_pool`, `source`, `profile`, `ast_index`, `current_scope`, and `runtime` as independent ownership facts. `runtime` remains builder-local because `Script` is catalogued by a `Runtime` but does not own one. The inherited `current_scope` is the construction cursor and is restored to the root global/module scope after planning; no second `global_scope` owner is needed.

### 4.3 Runtime catalog and lifecycle

`Runtime::scripts` remains an `ArrayList` of `Script*`, and `script_index` remains canonical path → `Script*`. `runtime_register_script()` assigns the inherited `module_state_id` before list bookkeeping exactly once for either subtype. A `JsScript*` upcasts without allocation or an extra lookup.

`runtime_free_script()` becomes profile-aware at the cold cleanup boundary: it first invokes the optional profile cleanup hook for JS-only facts and artifacts, then runs the existing base cleanup exactly once. Base cleanup continues to own `reference`, `source`, directory, `AstIndex`, `Input` pool/type list, imports, T0 state, and inherited MIR context. The JS hook must not free an inherited field.

The canonical `ModuleDescriptor` is found through the module registry by the inherited canonical reference/module identity; it is not duplicated as a second script-owned identity. If a direct descriptor link is later shown necessary, add it once to base `Script` because Lambda and guest units need the same relation.

`JsScript::name_pool` (inherited from `Input`) retains compiler spellings and source names. Runtime-visible names are linked into `EvalContext::name_pool`; the static owner pool never establishes a second `NameId` domain.

### 4.4 Lifetime

A `JsScript` generation is owned by the page `Runtime` script catalog and, for current modules, referenced by the canonical module registry entry. Initial implementation retains every generation until the page heap and deferred event work are torn down. This is intentionally conservative but precise: it retains compiler arenas, not heap values, and avoids inventing GC finalization/refcount edges before a live need exists.

The following must keep the generation alive:

- ordinary and bound functions with AST bodies;
- getters, setters, methods, constructors, and class field initializers;
- DOM/event callbacks, timers, immediates, and Promise reactions;
- CommonJS/ES module exports and cached namespaces;
- deferred dynamic import and top-level-await continuations;
- hot-reload closures created against an earlier generation.

A raw `AstNode*` without the owner is invalid. A `JsFunction` may keep a raw `JsScript*` because the runtime catalog retains all generations until after the page heap can no longer contain a callable that references them; it must not attempt to trace an arena pointer as a GC object.

### 4.5 Parser artifacts

The AST and any source bytes addressed by spans are retained. The Tree-sitter parser and tree need not be retained after AST construction if no AST field points into CST-owned storage and all syntax-dependent facts have been copied. This boundary must be verified before shortening CST lifetime.

### 4.6 One complete child contract

`ast_visit_core_children` remains the core catalog traversal. `js_profile.visit_ext_children` is wired to an adapter over the JS extension-child table for template literals/elements, static blocks, labels, regex, `with`, tagged templates, and future admitted extension kinds.

The adapter supplies the correct parent to the shared `AstChildVisitor`; it does not install a second recursive walk. This closes the current gap where `build_js_ast_indexed()` requests a profile index but the JavaScript profile's extension hook is a no-op.

### 4.7 Persistent pass products

The pass manager for a `JsScript` becomes:

```text
parse
  -> AST build
  -> early errors
  -> complete index
  -> binding + declaration-instantiation plan
  -> strict/direct-eval/with analysis
  -> capture + environment plan
  -> class/module plan
  -> property-name image
  -> suspension/effect facts
  -> T0 support + frame plan
  -> select T0 or whole-script T1
  -> optional per-function T1 planning/promotion later
```

Source contracts remain on AST nodes. Backend-specific register/representation facts remain lowering-local. Facts used by both backends are stored by stable node/function/binding/class ID as required by **D8.2.4–D8.2.5**.

`JsFuncCollected` is split conceptually into:

- persistent semantic/function facts owned by `JsScript`;
- ephemeral MIR items, registers, forward symbols, and representation choices owned by `JsMirTranspiler`.

`AstFuncNode::analysis` may point only to the persistent `FnAnalysis` carrier.

### 4.8 Static property-name discovery

The current MIR name-index helper discovers spellings as lowering encounters them. T0 instead needs a pre-lowering pass that records every statically named property required by either backend, including synthetic runtime names introduced by admitted source forms.

The resulting `PropertyKeySpec` image is sealed before realm construction, exactly as the existing prelink contract requires. MIR lowering receives stable indices from the `JsScript` plan instead of growing its own semantic spelling list. Backend-private synthetic names, if any remain, must be registered during the declared lowering/finalization pass before the static root is activated; silent late growth is forbidden.

---

## 5. Environments and Binding Cells

### 5.1 Why Lambda frame slots are insufficient

Lambda T0 flattens lexical locals into one activation plan and snapshots immutable captures. JavaScript requires binding identity:

- two sibling closures observe each other's writes to one outer variable;
- every captured `for`/`for-of`/`for-in` lexical binding has a fresh cell per iteration;
- reading a lexical before initialization throws;
- writing a `const` after initialization throws;
- `with` inserts an object environment into name resolution;
- direct eval can observe and introduce bindings according to caller strictness and environment type;
- a named function expression has a private self-binding distinct from outer bindings;
- sloppy simple-parameter `arguments` may alias parameter bindings.

These are not optional interpreter details. They determine observable JavaScript behavior.

### 5.2 Environment record kinds

The interpreter uses a GC-managed `JsEnvRecord` chain with at least these semantic forms:

| Environment | Purpose |
|---|---|
| global | coordinates global object properties and global lexical bindings |
| module | stable import/export/live-binding cells, always strict |
| function | parameters, vars, function declarations, `this`, `new.target`, `arguments` |
| declarative/block | `let`, `const`, class, catch, and nested block bindings |
| object/with | dynamic object-backed lookup with unscopables policy through runtime helpers |
| private/class | class private names and class lexical self-binding where required |

An environment record contains a traced outer link plus its binding storage. A binding cell contains an `Item` lane and flags such as initialized, mutable, deletable, and binding kind. Wide scalars are re-homed into record-owned scalar storage where required by the existing number-lifetime contract. Every record and JavaScript object it reaches belongs to the page's one `EvalContext::heap`; “JS environment” is a semantic type, not a heap partition.

The exact representation should extend the existing `GC_TYPE_JS_ENV` machinery rather than create a duplicate heap family if the current tracer/layout can safely express outer links and binding metadata. That question is resolved by auditing the current env allocator and promoting it when possible.

### 5.3 Binding plan

Every source binding receives a stable binding identity and a storage policy:

```text
frame-local Item slot
environment cell
module-state slot
global object binding
global lexical cell
object-environment dynamic lookup
unresolvable reference
```

The planner may keep an uncaptured, non-eval-visible local in a rooted frame slot. It must allocate a cell when identity can escape or be observed dynamically. `NameEntry` remains the source binding connection; backend-neutral storage/classification facts belong in the indexed binding plan.

### 5.4 Declaration instantiation

Before executing a body, the walker performs the same planned instantiation the MIR `js_main`/function prologues perform today:

- create var/function bindings and initialize them to `undefined` or the hoisted function;
- create lexical/class bindings in TDZ;
- enforce global lexical/var declaration conflicts;
- apply Annex B block-function rules in sloppy scripts;
- create parameter bindings and evaluate defaults left-to-right;
- build the correct mapped or unmapped `arguments` object lazily;
- establish module import/export bindings and namespace identity.

The planner records the order and target cells. The walker executes it; MIR emits it from the same plan.

### 5.5 Closures

An interpreted closure stores a traced pointer to the lexical environment visible at its creation. It does not copy capture values into a private vector and does not require caller-side read-back. Arrow functions retain lexical `this`, `new.target`, and `arguments` through their environment chain.

This replaces the current copied-env/read-back workaround for the shared mixed-tier ABI. It also addresses the documented per-iteration staleness and fixed capture-count hazards in `JS_05_Functions_Closures.md`.

**D6.2.3 clarification required before landing:** its current unqualified snapshot-capture wording describes Lambda semantics through S9.1.4. Under **D1.3**, it cannot govern LambdaJS. The formal design should revise the ruling in place (for example, D6.2.3v2) to state that Lambda captures immutable values while each guest profile owns its closure semantics; LambdaJS captures lexical bindings by reference. The corresponding working design ledger must be updated in the same change.

### 5.6 Direct eval

Direct eval is admitted only after both tiers can materialize the same environment view:

- T0 passes its active lexical/variable environments directly;
- T1 functions with syntactic direct eval allocate/materialize every binding that eval may observe;
- eval binding creation targets the proper variable/global environment under strict/sloppy rules;
- compiled and interpreted callees read the same cells after eval mutates them.

Until this bridge is complete, a `JsScript` containing direct eval fails the T0 support scan. Indirect eval and `Function` construction may continue through a separate script compile/execute entry, subject to the same retained-owner rules.

---

## 6. Frames, Roots, and Interpreter State

### 6.1 State ownership rule

The target is **one state owner with several typed capsules**, not one flat “unified runtime state.” This follows the existing Runtime/EvalContext distinction in **RG0** and the opaque-capsule requirement in **D5.4.2/RG2**:

| Owner | Owns | Must not own |
|---|---|---|
| `Runtime` | retained `Script*` catalog, canonical module definitions, source/AST/facts, immutable dependency data, derived MIR/code caches, module-state ID allocator | heap `Item`s, globals, live namespaces, active calls, jobs, DOM wrapper identity |
| `EvalContext` | one heap/name domain, module instances, root registry, scheduling, page/template/Jube state, active execution chain, optional language realms | AST/compiler arenas, immutable code caches, a second language-owned runtime |
| `InterpActivation` / generated-call activation | dynamic call state and rooted temporaries for one invocation | realm-wide caches, timers, modules, persistent globals |
| `JsRealmState` | ECMAScript realm identity and JS-only semantic services | common scheduling substrate, common module slab authority, compiler recovery owners, page-native canonical state |

“Same runtime state” therefore means Lambda and JavaScript address the same `EvalContext` capsules for facilities whose contracts are common. It does not mean Lambda must pay to initialize JavaScript intrinsics, or that JavaScript realm algorithms become generic runtime behavior.

### 6.2 Target `EvalContext` capsule graph

The endpoint is conceptually:

```c
struct EvalContext : Context {
    // existing frozen Context prefix and common scalar owners
    Heap* heap;
    NamePool* name_pool;
    Runtime* runtime;

    ContextRootRegistry* roots;       // all persistent native root ranges
    InterpreterState* interpreter;    // alternating T0 activation chain
    AsyncRuntimeState* async;         // one event-loop/liveness owner, ordered lanes
    ContextModuleState* modules;      // all per-context module instances/slabs
    PageRuntimeState* page;           // native document/UI bindings and page services

    TemplateRegistry* template_registry;
    void* template_state_store;
    void* render_map_state;
    void* jube_node_session;

    JsRealmState* js_realm;           // NULL until ECMAScript realm semantics are needed
    TestRuntimeState* test_state;      // NULL outside harness/batch execution
};
```

These names describe ownership; they do not require one flag-day layout edit. Fields already correctly common—`heap`, `name_pool`, `module_states`, scheduler, template state, Jube session—are promoted behind the indicated capsule API as their callers migrate. The JIT-visible `Context` prefix stays unchanged, capsule objects are stable after construction, and root registration remains explicit as required by **D5.4.2**.

The common capsules are lazy. A Lambda-only CLI run need not allocate `JsRealmState` or page state. A JS-only Test262 run does not allocate template/render state. A mixed page allocates both but still has one heap, root registry, module instance space, and scheduling owner.

### 6.3 `JsRuntimeState` decomposition

The current `JsRuntimeState` fields are reassigned by semantic lifetime, not by filename:

| Current responsibility/examples | Target owner | Rationale |
|---|---|---|
| `global_bindings`, `constructors`, `intrinsics`, `well_known`, prototypes, `globalThis`, JS namespace/wrapper identity, RegExp last-match state | `JsRealmState` | observable ECMAScript realm identity; meaningless to a Lambda-only context |
| Promise reaction records, generator/async continuations, eval environment journals, JS module live-binding/TLA extensions | JS subcapsules reached from `JsRealmState` or the relevant common instance | language semantics remain JS-specific even when their storage/scheduling substrate is common |
| `event_loop` queues, timers, immediate/rAF task ownership, page liveness and drain state | `AsyncRuntimeState` with JS lanes | **D1.3/D5.4.4** require one event loop per context; JS microtask/`nextTick` ordering remains a lane policy |
| `module_states`, module slab table/capacity, instantiated namespace/loading/error state | `ContextModuleState` | both languages instantiate modules in the same context and module-state ID space |
| active JS module selector | current activation, with a temporary compatibility field in `ContextModuleState` for generated ABI | it changes across nested calls and is not realm identity |
| `root_range_registry`, heap/root epochs | `ContextRootRegistry` / common heap generation | GC ownership is common; individual realm/page capsules still own and register their fixed ranges |
| `current_this`, `new_target`, private home class/index, `super` stack, pending arguments/source, strict-call flag, call depth/limit, interpreter `with` stack | `JsActivationPayload` on the common activation chain | these are dynamic call facts; nesting requires a stack, not singleton realm registers |
| native document/UI binding, canonical active document, DOM generation/pins shared with Radiant | `PageRuntimeState` | ES10–ES13 make page state common; native DOM remains outside GC under **D4.5.1v3** |
| JS DOM wrapper/collection identity, listener callback Items, observer/XHR/fetch JS objects | JS DOM subcapsule under `JsRealmState`, registered through common roots and pointing to `PageRuntimeState` | wrapper identity is realm-observable; native page authority is common |
| Node/Jube native service session | existing `EvalContext::jube_node_session` and service capsules | native services are context facilities, not ECMAScript global-object identity |
| Node globals and module namespace wrapper Items (`process`, `console`, Buffer/stream/http constructors) | JS host-realm subcapsule | the visible objects belong to the JS realm even when the service behind them is context-owned |
| `deferred_mir`, MIR timeout/recovery owners, source buffers, compiler nesting, immutable dynamic-Function code cache | `Runtime` compile-cache owner or ephemeral `JsTranspiler`/`JsMirTranspiler` | **D1.7/RG12** place derived code and compile state outside mutable realm state |
| dynamic-Function/eval wrapper objects built from cached code | `JsRealmState` | the code cache is runtime-owned; heap object identity is realm-owned |
| Test262 agents, preamble/batch module IDs, harness isolation counters | `TestRuntimeState` | test lifecycle must not enlarge or reset production realm semantics |
| `strict_mode` and `input` ambient fields | `JsScript` facts plus current activation/source record | strictness and source owner are unit/function facts, not mutable realm globals |

This classification deliberately splits some current structs. For example, a Promise is a JS heap object and its reaction ordering is ECMAScript semantics, but the queue node that keeps the page alive and the turn owner that drains it belong to `AsyncRuntimeState`. Similarly, a DOM wrapper is JS-realm identity while the native element/document and canonical form state belong to the page capsule.

After these moves, the remainder is renamed `JsRealmState`. Keeping the old name indefinitely would imply that common runtime state is JavaScript-owned; moving every field out would erase legitimate realm semantics. The rename happens only after compatibility aliases no longer make the old aggregate the authoritative address.

### 6.4 Module registry and module instances

The same-module-registry goal requires a sharper definition/instance split than the current `ModuleDescriptor`, which presently mixes runtime definition facts with context-owned `Item` namespaces and JS TLA evaluation state:

```text
Runtime::ModuleRegistry
  path/profile/source language/dependencies/namespace ops
  Script* owner + immutable compiled artifact reference
  no heap Item and no per-evaluation loading state

EvalContext::ContextModuleState[module_state_id]
  common slab + namespace Item + loading/initialized/error transaction state
  optional JS instance extension: live bindings, TLA parents/order/resume
  optional Lambda instance extension: package initialization transaction facts
```

The inherited `Script::module_state_id` is the join key. Cross-language imports consult the one runtime definition registry, then instantiate or retrieve the one context entry. This preserves language-specific namespace/live-binding algorithms without storing heap values in a runtime that may cache code across contexts (**D5.4.3**, **D7.2.1–D7.2.3**, **RG12–RG13**).

### 6.5 Activation state and T0/T1 convergence

The common activation header owns language-neutral facts:

```text
profile + Script* + definition identity
caller link + depth/limit accounting
source node/span
root/number side-stack checkpoints
generated/native/interpreted boundary metadata
```

A `JsActivationPayload` owns ECMAScript dynamic facts:

```text
rooted this + this-initialization state
rooted new.target + callee/arguments span
lexical/variable/private environments
home class and super-construction state
active module instance
strictness and direct-eval/with context
pending completion/iterator cleanup state
```

T0 stores this payload directly in `JsInterpFrame`. Generated T1 initially enters a lightweight compatibility activation whose accessors are backed by the migrated fields. The migration is complete only when `js_current_this()`, `js_current_new_target()`, module selection, arguments creation, private access, and stack tracing all read the current activation abstraction. At that point the singleton call fields and fixed `super` arrays are deleted from realm state.

This is the answer to “same stack”: both languages share the native C stack, one common activation chain, and one root/number side-stack pair; each activation has a language payload. They do not share one operand/value stack because neither interpreter needs one, and they do not share JavaScript completion records with Lambda `EvalSignal`.

### 6.6 Migration sequence for runtime state

1. Add capsule accessors and an inventory assertion without changing storage; every accessor requires the canonical `EvalContext`.
2. Introduce the common activation header and route Lambda T0 plus JS native/MIR entry boundaries through it.
3. Move activation-temporary JS fields first; this prevents the new AST walker from creating a second call-state authority.
4. Split runtime module definitions from context module instances and replace `active_js_module_state` with activation selection.
5. Promote the root registry/heap generation and async queue owner to common context capsules; keep JS ordering policy in JS lane callbacks.
6. Split native page/service state from JS wrapper identity, preserving ES10–ES13 and teardown ordering.
7. Move compiler recovery/deferred MIR and test harness owners out of production realm state.
8. Rename the remaining semantic capsule to `JsRealmState`, pivot compatibility macros, and retire `js_active_runtime_state` after measurements confirm boundary-hoisted access has no regression.

Each step must leave one authoritative writer. A transitional accessor may project old storage, but dual-write reconciliation is rejected: nested Lambda→JS→Lambda execution makes stale mirrors a semantic bug, not cleanup debt.

### 6.7 Integrated interpreter service

`EvalContext` gains or promotes one lazily allocated interpreter-service capsule. Its active state is control-only:

```text
top interpreted activation
combined depth/limit accounting
current source/node for diagnostics
per-profile and page-total counters
tier/promotion hooks
debug/trace observer hooks
```

Each profile may retain a stricter language-local recursion/fuel budget, but the service also counts every interpreted activation against one hard page/native-stack limit. Alternating Lambda→JS→Lambda recursion cannot evade overflow protection by resetting a per-language counter.

Each synchronous T0 entry pushes a common `InterpActivation` header on the native C stack:

```text
language/profile
Script* + definition identity
current AstNodeId/source span
caller activation
entry root/number watermarks
root-window bounds
completion-adapter kind
```

The header contains no unrooted `Item`. `LambdaInterpFrame` and `JsInterpFrame` embed or reference it and then carry their profile-specific payloads. The caller link may therefore alternate Lambda → JS → Lambda without changing `EvalContext` or consulting two unrelated `top` pointers. Generated/native entries keep their existing ABI; the stack-trace assembler stitches their existing debug records to the common interpreted activation chain at the callable boundary.

The reusable part of Lambda's current `InterpFrameGuard` becomes the common activation/rooted-window owner that:

1. snapshots the number/root side stacks;
2. opens a checked `LambdaRootFrame` of a planned size;
3. exposes rooted `Item` slots;
4. publishes/removes the activation on the one interpreter chain;
5. restores both watermarks exactly once on normal completion;
6. is safely abandoned only by an eligible native-fault recovery landing.

Profile selection happens once when the activation is entered. The kernel then calls `lambda_eval_*` or `js_eval_*`; it does not place a language branch in every core-node case. Their semantic fields and completion records remain separate. This preserves **D1.3** and **D5.1.1**: one interpreter service and one stack substrate do not mean one semantic switch.

### 6.8 One side-stack pair and one heap

All interpreted frames reserve nested windows from the active `Context::side_root_*` and `side_number_*` regions. A JS helper calling Lambda, or a Lambda behavior handler invoking JS, simply opens another LIFO window above its caller. Collection scans the one side-root interval plus the context's registered persistent roots.

JavaScript does not add an operand stack, shadow root stack, or private number stack. Existing `JsRootRange`s are classified during implementation:

- realm-persistent values—intrinsics, global/wrapper identity, Promise/generator state—remain `JsRealmState` roots or traced heap owners;
- queued callbacks/timers and module namespaces are owned by the common async/module capsules, using the same root registry;
- page listener/observer wrapper Items remain in the JS DOM realm subcapsule while native document pins remain in `PageRuntimeState`;
- activation-temporary `this`, `new.target`, arguments, `super`, iterator, `with`, and operand state move to `JsInterpFrame` root slots or `JsEnvRecord`s;
- state surviving a yield moves to a traced heap continuation under **D5.1.3**.

During migration, ambient fields still required by generated T1 are exposed only through the activation accessors described in §6.5; T0 does not create or reconcile a second copy. A compatibility generated-call activation may project old storage until each T1 boundary is migrated, then the old fields are deleted.

The heap is likewise singular. Lambda containers/functions/errors and JavaScript objects/functions/environments/promises are distinct traced object kinds in the same `Heap::gc`. Same heap does not authorize representation punning: a JS object is not a Lambda map, and access still selects the language's vtable/runtime kernels through `get_type_id(Item)` and declared membranes.

AST pools, source pools, and static name pools remain outside GC under **D4.1.1v2**. Radiant DOM/view arenas remain outside GC under **D4.5.1v3**. The common collector sees only the heap wrappers/handles and rooted `Item`s that intentionally cross those seams.

### 6.9 JavaScript frame

A JavaScript activation needs at least:

```text
JsScript / function definition
lexical environment
variable environment
private/class environment
rooted callee and argument span
rooted this binding and this-initialization state
rooted new.target
lazy arguments metadata/object
completion kind + rooted completion value + label identity
rooted scratch window
active iterator-cleanup chain
source/debug current node
common caller activation
```

Control-only pointers may live in the C++ frame while active. Every `Item` they designate must reside in the root window or a traced heap object. Iterators held across child evaluation are rooted; no array/object payload pointer is cached across a `MAY_GC` helper.

### 6.10 `this`, `new.target`, and call state

The shared activation abstraction is the target dynamic-extent authority for sloppy `this`, `newTarget`, home global/class, module state, private home class, and restoration. During migration, the existing JavaScript call kernel populates a compatibility `JsActivationPayload`; when it enters an AST body, `js_interp_call_body()` materializes the same payload directly in rooted frame slots and constructs the function environment.

Frame-aware accessors read the current activation regardless of whether its body is AST, MIR, or native. Arrow functions resolve the lexical binding through their captured environment, not the call receiver. After every compiled/native entry creates the compatibility activation, direct fallback reads from singleton realm fields are removed.

This staged bridge avoids duplicating OrdinaryCallBindThis or constructor setup in the walker while permitting later removal of avoidable ambient state.

### 6.11 Arguments

The call boundary remains dynamically sized `Item* + argc` under **D6.2.2v2**. The caller roots the span; the callee initializes parameter cells and retains any rest/arguments object it creates.

The frame stores enough metadata to lazily construct:

- a mapped arguments object for sloppy functions with a simple parameter list;
- an unmapped object for strict or non-simple parameter lists;
- `arguments.callee` behavior according to the existing runtime policy;
- lexical lookup for arrows through the outer environment.

No fixed 16-argument interpreter storage is introduced.

---

## 7. Evaluation Model

### 7.1 Values and references

The walker exposes two internal operations:

```text
eval_value(node) -> JsCompletion(value)
eval_reference(node, JsReference* out) -> JsCompletion
```

For `eval_reference()`, a normal completion means that `out` contains the reference; an abrupt completion leaves it unusable. A reference is never boxed into the `Item` payload of `JsCompletion`.

`JsReference` is interpreter control data whose `Item` fields are rooted while live. It records the reference kind and the operands needed by existing runtime kernels:

```text
binding identity / environment cell
base object or environment
property key / private name
receiver (distinct for super)
strict flag
optional-chain state
unresolvable/global classification
```

`GetValue`, `PutValue`, and delete are the only consumers that turn this record into observable runtime operations. This ensures compound/update assignments evaluate the LHS once and method calls retain the original base as `this`.

### 7.2 Structured completion

The conceptual completion carrier is:

```c
enum JsCompletionKind : uint8_t {
    JS_COMPLETION_NORMAL,
    JS_COMPLETION_RETURN,
    JS_COMPLETION_THROW,
    JS_COMPLETION_BREAK,
    JS_COMPLETION_CONTINUE,
};

struct JsCompletion {
    JsCompletionKind kind;
    Item value;
    JsLabelId label;
    bool has_value;
};
```

The actual implementation keeps `value` in the frame's reserved root slot rather than trusting a C++ aggregate across allocation. `JsLabelId` is a stable, analysis-assigned, non-GC identity; no transient source pointer is required.

JavaScript's `empty` completion is distinct from the value `undefined`. `has_value` preserves that distinction so blocks, loops, switch, eval, and script completion can update the last non-empty value correctly.

### 7.3 Helper calls and throws

Each helper call follows one template:

1. publish every live operand into rooted slots;
2. invoke the same `js_*` helper imported by MIR;
3. re-read moved values from roots where necessary;
4. if the result is ERROR-tagged, convert it to `JS_COMPLETION_THROW` immediately;
5. otherwise continue with the success value.

No pending-exception poll or recovery-frame jump implements a JavaScript throw. Promise rejection remains a durable async value handled by the Promise machinery; when observed as a synchronous throw at an await/resume point, it enters the same explicit completion path.

### 7.4 Literals and operators

The walker uses JavaScript constructors/coercion helpers:

- number literals preserve JS double, `-0`, NaN, Infinity, and safe integer behavior;
- BigInt literals use the existing BigInt path;
- strings/booleans/null/undefined use JavaScript carriers;
- regex literals use `js_create_regex_literal`;
- arithmetic, equality, relational, bitwise, `typeof`, `void`, delete, `in`, and `instanceof` select JavaScript helpers;
- `&&`, `||`, and `??` short-circuit and return operand values;
- conditional and loop tests use `js_is_truthy`, never Lambda `is_truthy`.

Constant folding remains an analysis optimization and must be differentially identical to execution. T0 does not consult MIR's inferred native representation.

### 7.5 Calls, optional chains, and construction

Call evaluation is reference-aware:

1. evaluate the callee once;
2. if it is a property/super reference, retain its receiver;
3. honor optional-chain nullish short-circuit before evaluating arguments;
4. evaluate arguments left-to-right, expanding spread through iterator semantics;
5. call `js_call_function_prerooted_args_into` or the equivalent common entry;
6. convert ERROR to `THROW`.

`new` evaluates the constructor and arguments, then calls the existing construct entry with explicit `newTarget`. Class constructors, bound constructors, proxies, derived constructors, and builtin constructors continue through the current construct authority.

### 7.6 Arrays and objects

Array literals create JavaScript arrays, preserving elisions/holes, spread order, length semantics, and abrupt iterator completion. Object literals use JavaScript property-definition helpers for:

- data properties;
- computed names;
- shorthand;
- getters/setters;
- methods and home-object/class metadata;
- spread via own-enumerable property copy;
- the special literal `__proto__` form.

They never call Lambda collection/COW builders. **D4.4.2** keeps JS mutation out of Lambda's COW protocol.

### 7.7 Assignment and destructuring

Assignment uses `eval_reference(node, &ref)` and preserves:

- one LHS evaluation;
- right-before-write ordering;
- strict/sloppy unresolvable writes;
- const and TDZ errors;
- accessor/proxy/private/super behavior;
- postfix versus prefix update result;
- logical-assignment short circuit;
- mapped-arguments aliases.

Array destructuring drives `js_get_iterator_lazy`, `js_iterator_step`, and `js_iterator_close`; object destructuring uses ordinary property access and ordered exclusion for rest. Abrupt target/default evaluation closes an open iterator before propagating the completion.

### 7.8 Loops, labels, and IteratorClose

Each loop installs a control target with optional stable label and optional active iterator. `break(label)` and `continue(label)` propagate until the matching target. Every crossed `for-of` target performs IteratorClose for abrupt completions that require it.

Lexical loop heads allocate fresh per-iteration binding cells before the body and update expressions. Closures therefore retain the cell for their own iteration without copied-env reset/read-back bookkeeping.

### 7.9 `try`, `catch`, and `finally`

The algorithm is explicit:

1. execute the try block and save its completion in rooted frame state;
2. if it is `THROW` and a catch exists, unwrap the JavaScript thrown payload and execute catch in a new declarative environment;
3. execute `finally` for every prior completion;
4. if `finally` completes abruptly, it replaces the saved completion;
5. otherwise restore the saved completion unchanged.

Iterator/environment cleanups nested inside these constructs execute in their own crossed frames, satisfying **D1.4v3**.

### 7.10 `with`

`with` is admitted only in sloppy code. It evaluates the object, creates an object environment record, and executes the body with that record at the head of the lexical chain. Binding resolution uses the existing with/unscopables helpers.

The current captured `with_env` array remains only as a transition aid for compiled functions. The shared mixed-tier endpoint is an environment record retained by closures.

### 7.11 Classes

Classes require persistent `JsClassPlan` facts before T0 admission:

- strict class body;
- superclass evaluation and constructor validation;
- private-name environment;
- computed-key evaluation exactly once and in source order;
- method/accessor function creation with home object/class;
- instance and static field initializers;
- static blocks;
- base/derived constructor behavior, `super()`, `super.x`, and this-before-super TDZ;
- class lexical self-binding and outer declaration TDZ.

The walker calls the existing class/property/construct helpers. It does not reproduce prototype or private-brand algorithms in interpreter-only code.

Classes are outside the first restricted T0 slice because current class facts and synthetic initializer functions are deeply tied to MIR collection. The pre-execution support scan routes them to T1 until `JsClassPlan` is complete.

### 7.12 Modules

Each module is a `JsScript : Script` whose inherited `module_state_id`, `reference`, and `profile` join it to the canonical runtime module definition and context module instance described in §6.4. The JavaScript facts retain the import/export plan and dependency semantics; the context instance retains the namespace placeholder and live evaluation state; module loading retains the one runtime registry and resolution policy.

T0 module evaluation must preserve:

- placeholder namespace registration before circular traversal;
- dependency ordering and self-import handling;
- live binding cells rather than value snapshots where supported;
- active module-state switching during calls;
- cross-language `.ls` namespace imports;
- event-loop and async-parent drain ordering.

The registry unifies identity, loading state, namespace publication, and cross-language enumeration. It does not flatten module semantics: JavaScript live bindings/top-level await and Lambda transactional immutable package instantiation remain language-owned operations behind the descriptor's profile/namespace hooks.

Initial T0 may reject ES modules as a whole before execution. CommonJS classic scripts may enter earlier if their injected bindings and `require` calls use ordinary planned global/module cells.

---

## 8. `JsFunction` and the Call Boundary

### 8.1 Explicit body kind

Preserve the ABI-fixed prefix containing `type_id`, `layout_magic`, and `func_ptr`. Add explicit fields after the existing stable portion or in an owned extension:

```text
JsFunctionBodyKind body_kind = NATIVE | MIR | AST
JsScript* script
const AstFuncNode* ast_def
JsEnvRecord* lexical_env
```

Promotion state is definition-site state and therefore belongs to the persistent function plan/FnAnalysis in `JsScript`, not to one closure instance. Multiple closures of one definition may have different environment pointers while sharing one eventual compiled body.

`func_ptr == NULL` is not a body-kind discriminator. Native targets intentionally may have no MIR pointer, and an AST body must be recognized deliberately.

### 8.2 Function factories

Add AST-target factories parallel to, but not duplicating, MIR/native factories:

- ordinary function/closure;
- method/accessor;
- class constructor/field initializer when admitted;
- arrow/async/generator metadata publication.

Shared initialization, metadata properties, home realm/class, with environment, source/stack metadata, and capability finalization remain centralized in `js_runtime_function.cpp`.

### 8.3 Call and construct capabilities

`js_function_finalize_capabilities()` continues to be the only publisher of executable capabilities. For an AST ordinary function:

- `invoke` is the existing generic/bound call entry;
- `construct` is the ordinary construct entry when syntax permits it;
- arrows, methods, generators, async functions, and typed-array methods remain non-constructable;
- class constructors remain construct-only according to the existing class protocol.

The common call kernel changes its executable-body check from “MIR pointer or native body” to “valid declared body kind”, then dispatches the body:

```text
NATIVE -> native_call
MIR    -> existing MIR invocation wrapper
AST    -> js_interp_call_body
```

All setup/restoration around that body remains one implementation. This is essential for T1→T0 callbacks and for native builtins invoking interpreted functions.

### 8.4 GC tracing

`JsFunction` traces its lexical environment record and all existing `Item` edges. `JsScript*`/`AstNode*` are arena-owner pointers governed by runtime-catalog lifetime and are not passed to `gc_mark_object_ptr` unless `JsScript` is later made a GC allocation by a separate ruling.

Environment record compaction/rebasing follows the current JS env tracer and scalar-tail contract. Any new raw env pointer in a function, generator, async context, Promise reaction, or module record must have an explicit trace/compact owner.

### 8.5 Cross-language calls

Lambda `Function` and `JsFunction` remain distinct heap object kinds. Unifying their full layouts would import JavaScript's `[[Construct]]`, `this`, proxy/bound-function, and realm rules into Lambda, or erase Lambda's signature/effect contracts. Integration occurs at the declared callable membrane:

1. the caller roots the callee and dynamically sized `Item` argument span on the shared side-root stack;
2. the namespace/callable adapter validates the target language and capability;
3. it enters the target profile under the same `EvalContext`, heap, module registry, and interpreter activation chain;
4. the callee returns through the explicit completion ABI;
5. the boundary converts only the completion *classification* required by the declared import/callback contract, preserving the underlying heap error/value identity;
6. every crossed frame performs its own cleanup before the caller consumes the result (**D1.4v3**, **D8.4.3v2**).

No deep copy, heap switch, context rebinding, or raw function-layout cast occurs. Cross-language imports continue to publish language-owned namespaces through `ModuleNamespaceOps`; direct same-language calls retain their existing fast path.

---

## 9. Tier Selection and Promotion

### 9.1 Selector

Introduce a JavaScript tier selector independent of the MIR backend selector:

```text
JS_TIER=interp   pin admitted scripts/functions to AST T0
JS_TIER=auto     start admitted code in T0 and promote eligible hot definitions
JS_TIER=jit      retain eager whole-script MIR behavior
```

Exact CLI spelling may be normalized with the existing command-line configuration, but the three semantic modes and differential-testability are required. `JS_MIR_INTERP` continues to choose how an already-produced MIR artifact executes.

Unset policy stays unchanged until the staged gates pass. The final default flip requires its own formal revision and measured release evidence.

### 9.2 Support scan

Every `JsScript` records:

```text
interp_planned
interp_supported
first_reject_kind/reason
executed_node_count
fallback_count
promotion_count
```

The support scan examines semantic facts, not only node kinds. A syntactically ordinary function may still be rejected for direct eval, an unsupported class dependency, module/suspension behavior, or a missing binding plan.

Rejected AUTO scripts enter the existing eager/MIR-interp policy before declaration instantiation. Forced `interp` reports a deterministic unsupported-tier error rather than silently compiling, except where the test harness explicitly requests counted comparison fallback.

### 9.3 Promotion point

Promotion occurs at a function-entry boundary only. The call count belongs to the static AST definition. A threshold of five is the initial parity default with Lambda **D8.1.1v5**, but the JS profitability threshold remains tunable only through measured release data.

No active interpreter locals or arbitrary PC are transferred. A hot loop marks its definition for the next entry; it does not OSR. Direct validated self-tail handoff may be considered only after the ordinary entry satellite is correct and separately gated.

### 9.4 Satellite contents

A JavaScript satellite contains the selected boxed function body and required public wrapper/metadata references. It consumes `JsScript`-owned:

- stable function/binding/class IDs;
- environment layout;
- module slots;
- property-name indices;
- strict/effect/direct-eval facts;
- source/debug identity.

It does not rerun whole-script semantic discovery. Dependencies that cannot be referenced through stable runtime helpers or existing satellites make the definition ineligible and pin it to T0 or trigger a pre-execution whole-unit policy decision.

Compiled artifacts remain immutable under **D8.4.1v2**. Publication writes the definition's promotion cell/body entry, not generated instructions.

### 9.5 Shared environment prerequisite

True T0↔T1 closure interoperability starts only after MIR capture lowering reads/writes the shared environment cells. The transition is:

1. introduce environment records for T0 and test their JS semantics;
2. adapt MIR capturable bindings to the same records while leaving noncaptured locals in registers;
3. remove copied-env/read-back as an authority;
4. enable mixed-tier function promotion;
5. retain old helpers only as audited compatibility adapters until no caller remains.

Promoting first and copying cells into the existing dense env ABI is rejected: direct eval, sibling mutation, per-iteration identity, and errors during callbacks make correct write-back a deoptimization system, not a small adapter.

---

## 10. Suspension and Durable Continuations

### 10.1 Initial policy

Generators, async functions, async generators, and modules with top-level await remain on the compiled state-machine path during the first T0 releases. The support scan makes that decision before script execution, or the planner prebuilds a required satellite before any closure exposing it is published.

Compiling “at first call” is permitted only when compilation cannot fail after prior observable script effects in a way that requires whole-script replay. A compilation failure returns an ordinary explicit error completion; it never restarts the script.

### 10.2 Final interpreter continuation

Interpreter-native suspension heapifies the durable portion of `JsInterpFrame`:

- AST resume node/state;
- lexical/variable/private environments;
- rooted operand/scratch values live across suspension;
- current completion and pending `finally` chain;
- active iterator and delegation state;
- `this`, `new.target`, and arguments state;
- generator/async input and resume kind.

The continuation is a traced heap object. No native C++ frame, root-window pointer, `jmp_buf`, or borrowed argument span survives the return to the event loop.

`js_generator_next` and `js_async_drive` gain a declared state-body kind parallel to `JsFunction`: MIR state function or AST continuation. Existing Promise scheduling, reentrancy checks, `yield*`, return/throw injection, and async resolution remain runtime authorities.

This later work can remove the current fixed MIR resume-label cap for interpreted bodies, but lifting a compiled-path cap is a separate fix and must not be silently claimed by T0.

---

## 11. Page Runtime, Entry Pipeline, and Lifecycle

### 11.1 Page isolate construction

A `DomDocument` that needs either language creates or adopts exactly one fully initialized `Runtime`. That owner creates the canonical `EvalContext`, heap, dynamic `NamePool`, side-stack regions, async service, module registry, script catalog, root registry, and module-state allocator. The current `EvalContext::js_state` is decomposed per §6.3; its semantic remainder becomes the lazy `JsRealmState` inside that owner, not a second evaluator.

```text
document setup / post-render evaluator creation
  -> runtime_init(Runtime) exactly once
  -> create canonical EvalContext
  -> bind heap + NamePool + side stacks + scheduler
  -> initialize module registry/script catalog/module-state space
  -> initialize template registry/state when Lambda code needs it
  -> initialize JsRealmState when ECMAScript realm semantics are needed
  -> register Script and JsScript : Script owners in the same Runtime catalog
```

A JS-bearing HTML page that later loads the Lambda dom package reuses its existing runtime/context. A Lambda page that later establishes page JS ensures `JsRealmState` on that same context; during migration `js_runtime_state_init()` remains the compatibility entry that performs this work. Neither path allocates a competing heap, async service, root registry, or module registry. This is the interpreter-side realization of **ES12** and **EO6**.

An iframe/subdocument is a separate document isolate under **EO5v2**: it owns a different `Runtime`, `EvalContext`, heap, and registry. The host thread may hand off between document contexts only at a quiescent document/event boundary with no live activation or side-root frame, as required by **D5.4.1**. It never switches context in a nested callback.

Standalone CLI/Test262 execution uses the same ownership graph without a `DomDocument`; “one page isolate” becomes “one invocation/test isolate.”

### 11.2 JavaScript script entry

The JavaScript entry driver is split into frontend, planning, and backend execution:

```text
1. acquire the owning Runtime and its canonical EvalContext
2. own/copy source bytes
3. create `JsScript : Script`; register the inherited `Script*` and allocate its module-state ID
4. parse and build AST
5. run early errors
6. index and run persistent semantic plans
7. discover the static property-name image and run the T0 support plan
8. prepare static Input/NamePool data and the context module-state slab
9. prelink and seal the property-name image into the context NameId domain
10. ensure the page's `JsRealmState` once; instantiate this script/module environment in `ContextModuleState`
11. load/plan imports through the runtime's canonical module registry
12. execute T0 or build/link/execute T1
13. perform the required job/microtask checkpoint while all owners remain alive
14. publish result and release only backend-temporary state
15. retain JsScript generations in the runtime catalog until runtime teardown removes all possible heap referrers
```

`jit_init()` moves wholly inside the T1 branch. This is the point of the feature: a successful T0 run creates no MIR context and performs no MIR lowering/link. Runtime/context/heap initialization is not T1 work and is shared with Lambda.

### 11.3 One page event turn

The accepted DOM-state order becomes a single mixed-profile activation tree:

```text
native event entry  [canonical EvalContext already bound]
  -> JS capture / target / bubble listeners
  -> Lambda app-template handler, when present
  -> defaultPrevented check
  -> Lambda dom-package behavior handler (UA default action)
  -> native state settle / restyle scheduling
  -> JavaScript microtask checkpoint
  -> return to the page scheduler for later Lambda/host macrotasks
```

Every synchronous callback in that turn uses the same heap and side-stack tops. JS listeners may call Lambda exports; Lambda app/behavior code may call declared JS exports; either may re-enter native DOM helpers that call back into the other language. Such nesting pushes common interpreter activations and profile-specific frames. It does not rebind TLS, drain a second loop, or copy the values.

The event pipeline, not interpreter profile, owns cancellation and default-action ordering. JavaScript's `defaultPrevented` and Lambda behavior verdicts continue to meet at the native event boundary defined by **ES5/ES15**.

### 11.4 One scheduler with semantic lanes

The context has one scheduling owner and liveness decision. It may contain multiple semantic lanes:

- JavaScript `nextTick` and Promise microtasks at their defined checkpoints;
- JavaScript timers, immediates, animation-frame callbacks, and async module work;
- Lambda task resumes as FIFO macrotasks under **D6.3.1**;
- native DOM/event work and deferred layout/paint callbacks.

Unification means one owner decides whether the page has pending work, performs teardown, and roots queued callbacks in one heap. It does not reorder everything into a generic FIFO. A Lambda resume never interrupts a JS job or its microtask checkpoint, and a JS callback never runs concurrently with a Lambda handler on the page thread.

### 11.5 Preamble and batch mode

The Test262/browser preamble becomes a retained `JsScript` plus its persistent declaration/property/module plan. Consumer scripts inherit the planned prefix exactly as current module property IDs require. They do not inherit transient MIR registers or `JsMirTranspiler` maps.

Batch reset must release all per-test environments, callbacks, module states, registry namespaces, and `JsScript` generations not belonging to the retained harness before replacing the one heap generation. A stale function must never retain a dead AST or point at a reused module-state slot. Lambda package scripts retained by the same test runtime participate in the same reset transaction.

### 11.6 Modules and nested compilation

Nested `require`, dynamic import, eval, `Function` construction, and Lambda package imports create or retrieve owners through the same runtime script catalog and canonical module registry. They reserve IDs from the same allocator and instantiate entries in the same `ContextModuleState` capsule (initially projecting the existing `EvalContext::module_states` table). The active compiler/transpiler TLS state must distinguish a retained `Script`/`JsScript` owner from an ephemeral MIR-lowering session so recovery cleanup cannot destroy the former twice.

### 11.7 Teardown order

Page teardown is one ordered transaction:

1. stop admitting new events/tasks and settle or cancel queued work according to each lane's semantics;
2. remove DOM listeners/pins and template/render-state roots while both the document and heap are live;
3. release Promise/timer/module/Jube callbacks, namespaces, context-module instances, root ranges, and JS intrinsic caches while the heap is current;
4. destroy `JsRealmState`, page/async/module/interpreter capsules, and finally the common root registry after every registered range is removed;
5. finalize and destroy the single GC heap;
6. destroy the runtime module-definition registry, then release `Script`/`JsScript` AST/source pools and derived MIR contexts whose code can no longer be referenced by a heap callable;
7. release side-stack mappings, the canonical `EvalContext`, and the `Runtime` owner.

Implementation may combine adjacent steps where current ownership already does so, but it may not free a script owner while a heap function can still point at its AST, destroy the heap before unregistering roots, or let Lambda and JS each tear down the shared owner.

---

## 12. Implementation Map

Names below describe ownership boundaries; final placement should reuse existing helpers and follow rule 13 before adding files.

### 12.1 New or promoted components

| Component | Responsibility |
|---|---|
| `JsScript : Script` and profile-aware base lifecycle | reuse the existing catalog/identity/tier/artifact owner and add only persistent JavaScript facts |
| integrated interpreter activation service | one `EvalContext` activation chain, root/number-window guard, combined depth/debug/counter/tier hooks |
| common `EvalContext` capsules | root registry, async lanes, module instances, page state, and interpreter activation ownership shared by Lambda and JS |
| `JsRealmState` | narrowed JavaScript realm/intrinsic/global/wrapper semantic owner after `JsRuntimeState` decomposition |
| `lambda/js/js_script.*` | retained `JsScript` JavaScript-only facts and profile cleanup/downcast helpers over inherited `Script` storage |
| `lambda/js/js_analysis.*` | persistent binding/function/class/module/property/suspension plans extracted from MIR phases |
| `lambda/js/js_environment.*` | GC-managed environment records and binding-cell access |
| `lambda/js/js_interp.hpp` | JavaScript frame, completion, reference, tier and stats contracts |
| `lambda/js/js_interp.cpp` | JavaScript expression/statement walker and function entry |
| `lambda/js/js_interp_plan.cpp` | frame/support planning over complete indexed AST |

If new translation units are added, update `build_lambda_config.json` and regenerate build files through `make`; never edit generated Lua files.

### 12.2 Existing components requiring change

| Existing area | Required change |
|---|---|
| `runtime/transpiler.hpp`, `runtime/runner.cpp`, `runtime/ast.hpp` | make full `Runtime` initialization mandatory; admit `JsScript : Script`, preserve the existing `Script*` catalog, and make cold cleanup profile-aware |
| `runtime/runtime-state.*`, `runtime/interp.*` | add common root/async/module/page/interpreter capsules and the interpreted/generated activation header without changing Lambda semantics |
| `runtime/module_registry.*` | split immutable runtime module definitions from context module instances; retain one path/dependency authority and one runtime owner |
| `runtime/side_stack.*`, GC root inventory | prove alternating Lambda/JS frame windows and one collector root graph; no new stack mechanism |
| `runtime/ast-core.*` | publish/wire the JavaScript profile extension-child contract; preserve one core traversal |
| `js/js_ast_children.cpp` | provide profile adapter without duplicating recursion |
| `js/js_transpiler.*`, `js/js_scope.cpp` | make `JsTranspiler : JsScript`, remove duplicated base fields, and separate builder-tail cleanup from retained prefix ownership |
| `js/js_mir_context.hpp` and MIR analysis files | split persistent semantic facts from lowering-local MIR state |
| `js/js_mir_module_batch_lowering.cpp` | consume persistent plans; stop owning authoritative binding/capture/class facts |
| `js/js_mir_expression_lowering.cpp` | share reference/environment plans and later shared environment-cell ABI |
| `js/js_mir_statement_lowering.cpp` | emit declaration/completion/iterator behavior from shared plans |
| `js/js_function.hpp` | add explicit AST body/owner/environment fields while preserving ABI prefix |
| `js/js_runtime_function.cpp` | AST factories, capability finalization, environment tracing |
| `js/js_runtime.cpp` | AST body dispatch inside common call/construct authority; enter the shared activation chain without context rebinding |
| `js/js_runtime_state.*` | decompose by §6.3, rename the semantic remainder `JsRealmState`, move activation fields to common frames, and retire the derived JS TLS cache |
| `js/js_mir_entrypoints_require.cpp` | frontend/backend split, pre-MIR tier choice, canonical runtime/context reuse, retained lifecycle |
| Radiant script runner/document lifecycle | use the document's one initialized `Runtime`/`EvalContext`; register both language owners and perform one teardown transaction |
| module/runtime state files | own `JsScript` generations and stable module/env roots in the shared ID/slab space |

Vendor sources, C2MIR, generated `parser.c`, generated Lua, and `log.conf` remain untouched.

---

## 13. Staged Implementation

### P0 — Design/spec and differential harness

1. Adopt this proposal's ledger.
2. Revise the stale Stage 2 text in `Lambda_Design_Ast_Interpreter.md`:
   - `EvalSignal` does not transfer unchanged; JS uses `JsCompletion`.
   - per-AST-node inline caches are removed under **D8.4.1v2**.
3. Add the formal JavaScript tier ruling (recommended new **D8.1.3**, rather than overloading Lambda-specific D8.1.1), clarify D6.2.3's Lambda-only snapshot rule, and formalize **ES12** as one document isolate = one `Runtime`/`EvalContext`/heap/registry/side-stack set.
4. Add `JS_TIER` plumbing, stats, and a differential runner that initially reports every script unsupported.
5. Add assertions/counters proving that a mixed Lambda/JS page has one runtime, context, heap, module registry, module-state allocator, and side-stack base.

**Gate:** selector and accounting tests prove no silent fallback; no runtime behavior changes under default policy.

### P1 — Shared page substrate, `JsScript`, and persistent planning

1. Make full `runtime_init()` ownership mandatory on every page path; remove partial/competing Runtime construction.
2. Define `JsScript : Script` and `JsTranspiler : JsScript`; register the inherited `Script*` in the existing catalog and remove duplicated common fields.
3. Make `runtime_free_script()` profile-aware while retaining exactly one base cleanup; move only JavaScript-specific fact cleanup to the profile hook.
4. Introduce common root, module-instance, async, page, and interpreter capsules under the canonical `EvalContext`; initially project current storage through accessors.
5. Extract the common interpreter activation/root-window guard from Lambda T0 and route JS entry boundaries through it without changing either language's semantics.
6. Move JS activation fields and active module selection out of realm state; split immutable module definitions from context instances.
7. Wire complete profile child enumeration.
8. Extract function collection, strict/direct-eval/with facts, bindings, module slots, capture inputs, class facts, and property-name discovery from MIR lifetime.
9. Make MIR consume the new persistent facts with unchanged output, then rename the narrowed JS capsule to `JsRealmState`.

**Gate:** eager MIR and MIR-interp baselines remain identical; a page with page JS plus the Lambda dom package proves one owner for runtime/context/heap/registry/module IDs and tears down once; `fn->analysis` and every source/name pointer remain valid through event-loop drain and batch reuse; **D8.6.4v2** timing/LOC diagnostics are recorded.

### P2 — Restricted synchronous vertical slice

Add the explicit `AST` function body kind, AST-target factories, and common call/construct body dispatch required to execute interpreted ordinary functions. Promotion remains disabled in this phase.

Implement a full vertical path for classic scripts containing:

- literals and identifiers;
- unary/binary/logical/conditional/sequence expressions;
- var/let/const and blocks;
- ordinary functions, arrows, closures, calls, return;
- arrays/objects/property access;
- assignment/update;
- if/while/do/for;
- throw/try/catch/finally;
- synchronous native/builtin callbacks.

Reject classes, modules, `with`, direct eval, generators, async, top-level await, and any unplanned extension before execution.

**Gate:** admitted fixtures match eager MIR exactly; forced `interp` executes them with zero MIR contexts and zero fallbacks; a DOM event runs JS listener → Lambda app handler → Lambda behavior handler through one activation service/side-stack pair, and a nested cross-call fixture proves a Lambda→JS→Lambda activation chain; forced GC/ASan lifecycle probes pass.

### P3 — Synchronous breadth

Add:

- destructuring/default/rest/spread;
- for-in/for-of and IteratorClose;
- switch and labels;
- optional chaining and logical assignment;
- regex, templates, tagged templates;
- `with` and richer global semantics;
- classes/private/super/static blocks;
- CommonJS and ES modules without suspension;
- direct eval after the materialization bridge.

**Gate:** the committed `test_js_gtest` corpus partition is complete—every discovered row is an exact T0 match or an explicit pre-execution exclusion, with no unclassified or silent fallback rows.

### P4 — Common environment ABI and promotion

1. Convert MIR captured/eval-visible bindings to shared environment cells.
2. Validate every T0/T1/native call and construct crossing over the shared environment ABI.
3. Implement per-definition boxed satellites and promotion publication.
4. Add AUTO counters and hot-function tests.
5. Demote size-based MIR-interp selection to a backend diagnostic for admitted scripts.

**Gate:** full JS T0/T1/native and Lambda↔JS call matrix passes under forced GC; closure mutation, per-iteration cells, direct eval, class constructors, bound/proxy calls, and error identity remain exact across tier and language crossings.

### P5 — Suspension

1. Add heapified generator continuations.
2. Add async/await and async-generator continuation driving.
3. Integrate top-level await/module async-parent ordering.
4. Integrate JS jobs and Lambda resumes under the one context scheduling owner without merging their ordering lanes.
5. Differentially test return/throw into suspended `finally` and active iterators.

**Gate:** no native frame or side-stack slot survives suspension; existing async/module/Test262 baselines do not regress; continuation stress passes forced GC and batch teardown.

### P6 — Default policy and performance

1. Run release-only cold-start, Test262, Radiant document, large-library, and memory measurements.
2. Tune promotion thresholds from measured total turnaround, not microbenchmarks alone.
3. Flip unset JavaScript policy to AUTO only after the formal ruling and complete gates.

**Gate:** correctness gates are green, fallback policy is auditable, compiler-time and LOC ratchets under **D8.6.4v2** are satisfied or explicitly accounted, and no supported workload regresses beyond an approved measured budget.

---

## 14. Validation and Enforcement

### 14.1 Semantic differential

For each admitted script, run isolated T0 and eager T1 realms and compare:

- returned/script/eval completion;
- stdout/stderr-visible language output;
- thrown payload identity, type, name, message, and observable properties;
- global/module mutations;
- property descriptors/prototypes where relevant;
- callback, microtask, timer, and module ordering;
- exit status and process hooks.

A comparison that crashes, times out, loses output, or cannot establish the same clean initial realm is inconclusive, not a pass.

### 14.2 Focused semantic matrix

Required cases include:

- TDZ, const, var/function hoisting, Annex B, global lexical conflicts;
- sloppy/strict `this`, arrows, methods, bound functions, proxies;
- call versus construct and explicit `newTarget`;
- member-call receiver preservation and optional-chain argument suppression;
- getters/setters/proxies/private/super assignments;
- sibling mutable closures, deep captures, more than 16 captures;
- per-iteration `let`/`const` closures;
- mapped/unmapped arguments;
- return/throw/break/continue through nested `finally`;
- IteratorClose through destructuring and labeled loop exits;
- direct/indirect eval and mixed T0/T1 visibility;
- class base/derived constructors and this-before-super;
- circular modules and live binding behavior;
- callbacks after top-level return and after hot reload.

### 14.3 Tier-crossing matrix

Every combination is exercised:

```text
T0 caller -> T0 callee
T0 caller -> T1 callee
T1 caller -> T0 callee
native builtin -> T0 callback
T0 callback -> native builtin -> T0/T1 callback
call entry versus construct entry
ordinary versus arrow/method/class/bound/proxy function
success versus throw/rejection
```

Add cross-language rows in both directions:

```text
Lambda T0/T1 -> JS T0/T1/native export
JS T0/T1/native -> Lambda T0/T1 export or callback
JS listener -> Lambda app handler -> Lambda behavior handler
Lambda handler -> JS callback -> Lambda callback
success/error/throw through every crossing
```

### 14.4 Page coexistence matrix

Exercise these document shapes with pointer/identity assertions and observable event-order checks:

- HTML page JS first, then Lambda dom-package load;
- Lambda app page first, then a real JS DOM realm;
- script-less interactive HTML that creates the evaluator for Lambda UA behavior;
- page JS plus Lambda behavior plus CommonJS/ES/Lambda package imports;
- hot reload retaining old JS and Lambda callbacks;
- parent document and iframe as distinct isolates switched only at quiescent boundaries;
- headless/event-sim and windowed paths using the same ownership construction.

Within one document, assert identical `Runtime*`, `EvalContext*`, `Heap*`, module-registry pointer, module-state allocator, side-root base, and side-number base at every language boundary. Across iframe documents, assert all of those isolate identities differ and no heap `Item` crosses without the declared DOM/message membrane.

### 14.5 GC and ownership

Run forced collection at every eligible helper/call boundary and verify:

- alternating Lambda/JS interpreter frames occupy one nested side-root interval;
- frame scratch operands remain live;
- environment cells and scalar tails move/rebase correctly;
- `JsFunction` traces the interpreted environment;
- JS jobs, Lambda tasks, module namespaces, template state, and DOM wrapper callbacks are roots in the same heap graph;
- no `Item` managed by one page heap is published into another document isolate;
- old `JsScript` generations survive while callbacks reference them;
- batch reset releases dead generations without stale code/AST/module pointers;
- no persistent root or deferred MIR/AST owner grows per completed test.

The dynamic forced-GC/self-baselining method under **D8.6.3** remains the liveness oracle; do not add a divergent static shadow analysis.

### 14.6 Corpus gates

1. focused interpreter unit tests;
2. admitted `test/js` differential subset;
3. full `test_js_gtest` partition;
4. Radiant/UI/browser preamble scripts;
5. current Test262 baseline and partial sets;
6. modules/CommonJS/Node compatibility suites;
7. forced-GC and sanitizer runs;
8. release performance runs.

The Test262 runner is never changed to hide an engine failure, crash, timeout, or unsupported feature. Rule 18 remains absolute.

### 14.7 Performance measurements

Measure release builds only. Required phase counters:

```text
parse
AST build
early errors
persistent analysis
property-name planning
T0 execution
satellite compile/link
T1 execution
event-loop drain
cleanup
peak RSS
page heap count
page EvalContext count
maximum mixed-profile activation depth
nodes executed
functions promoted
fallback/reject reason
MIR functions/instructions produced
```

The principal metric is total cold turnaround for run-once work. Hot arithmetic kernels are expected to be slower in T0 and are promotion targets. Test262 and Radiant measurements use identical manifests, one warm-up, and median-of-five release samples where **D8.6.4v2** applies.

---

## 15. Considered and Rejected

### 15.1 Add JavaScript cases directly to Lambda `eval_expr()`

Rejected. Common node shapes do not share coercion, containers, references, closures, truthiness, or completions. Repeated language branches would obscure **D1.3** and make accidental semantic reuse likely.

### 15.2 Keep MIR-interp as the only non-native tier

Rejected as the product solution. It remains valuable for backend diagnostics but still pays whole-module MIR analysis, emission, and link—the cold cost T0 is intended to remove.

### 15.3 Introduce bytecode

Rejected under AI22. It adds another compile phase, executable format, verifier, and resident representation without solving environment/reference semantics.

### 15.4 Reuse Lambda snapshot closures

Rejected. JavaScript captures binding identity and mutation by reference. Snapshot capture violates observable semantics and **D1.3**.

### 15.5 Bridge promotion with copied env plus read-back

Rejected as the mixed-tier ABI. It reproduces the current per-iteration and capture-count hazards and becomes a deoptimization/materialization system for eval, callbacks, and abrupt completions.

### 15.6 Infer AST body from null `func_ptr`

Rejected. `func_ptr` is a MIR target field, not executable capability authority; native function shapes may also leave it null. `body_kind` must be explicit under **D6.2.2v2**.

### 15.7 Add per-node property inline caches

Rejected by **D8.4.1v2**. The current Stage 2 paragraph proposing them is superseded by this design and must be corrected.

### 15.8 Fall back when an unsupported node is reached

Rejected. Replaying through MIR can duplicate prior effects; continuing with a guessed value corrupts semantics. Support is decided before execution under JSI11.

### 15.9 Keep AST pools alive by leaking `JsTranspiler`

Rejected. Compiler-session state contains transient MIR/register/error/recovery ownership and is not a coherent runtime owner. `JsScript` explicitly retains only the source and persistent semantic products.

### 15.10 Restore conservative native-stack scanning

Rejected by **D1.5/D5.3.3** and repository rule 15. Interpreter temporaries use planned side-root slots.

### 15.11 Give Lambda and JavaScript separate page runtimes/contexts

Rejected by **D5.4.1** and **ES12**. Event callbacks, template state, module namespaces, and DOM behavior would need context switching or copying inside one synchronous event turn. TLS identity and precise root-stack ownership make that both unsafe and semantically unnecessary.

### 15.12 Give JavaScript a separate GC heap

Rejected by **D1.2–D1.5**. `Item` carries no heap identifier, `gc_is_managed()` is relative to the active heap, and page graphs already contain cross-language edges through callbacks, namespaces, errors, template state, promises, and DOM wrappers. A second heap would require a new handle/clone/write-barrier/finalization system and would defeat direct `Item` exchange.

### 15.13 Give the JS interpreter a private root/value stack

Rejected by **D1.3** and **D5.1.1–D5.3.3**. Nested Lambda↔JS calls are one LIFO lifetime chain. Two root stacks would require every collection and recovery boundary to discover, scan, checkpoint, and restore both, while a private value stack would become the forbidden fourth stack mechanism.

### 15.14 Build two independent interpreter services

Rejected as insufficient integration. The node evaluators remain distinct, but duplicating activation ownership, depth limits, source tracking, counters, tier hooks, frame guards, and backtrace heads would create two authorities over the same stack and heap. The selected design has one service with profile-owned evaluators.

### 15.15 Merge Lambda scope with the JavaScript realm

Rejected by **D1.3**. One isolate is an ownership decision, not a global-environment decision. Lambda package bindings, JavaScript `globalThis`/lexical environments, and their closure rules stay separate and meet through declared imports/callables/DOM projections.

### 15.16 Keep `JsRuntimeState` intact and merely share its parent context

Rejected as incomplete integration. Context ownership fixes cross-isolate globals, but the current aggregate still combines compile, realm, activation, module, async, page, host, root, and test lifetimes. A JavaScript AST walker layered on top would either duplicate activation/module state or make Lambda scheduling, rooting, and page teardown subordinate to a JS-named owner. Section 6 keeps the isolation win and decomposes the remaining semantic boundaries.

### 15.17 Inline all mutable state directly into `EvalContext`

Rejected by **D5.4.2**. A flat super-context would force Lambda-only runs to carry JS/DOM/Node state, destabilize the JIT-visible layout, obscure teardown order, and recreate the same lifetime ambiguity at a larger scale. Stable lazy capsule pointers provide one context identity without one megastruct.

---

## 16. Open Implementation Questions

These questions do not reopen the decisions above:

1. **Existing env promotion.** Can current `GC_TYPE_JS_ENV` be extended in place to represent outer-linked binding cells and metadata, or should a versioned JS environment layout be introduced? The audit must preserve every current tracer/compactor owner.
2. **Profile cleanup surface.** Is one `LangProfile::destroy_script(Script*)` hook sufficient, or do retained JS artifacts require separate cold invalidation and final-destroy hooks? Either answer must keep base cleanup exactly once and must not add a wrapper record.
3. **Fact layout.** Which `JsFuncCollected` fields become `FnAnalysis`, which require a JS function-plan extension, and which remain MIR-only? The rule is “shared semantic answer once; backend mechanics local.”
4. **Property-name synthetic inventory.** Which names are semantic products of source forms and which are backend-private? The former belong in `JsScript` planning; the latter must still be sealed before dynamic NamePool activation.
5. **Restricted-slice admission.** The exact first supported fixture manifest is established by the P2 differential survey, not by weakening semantics for an inconvenient node.
6. **Promotion threshold.** Five is the initial parity value; release corpus measurements decide whether JS retains it.
7. **Direct self-tail handoff.** KIV until ordinary entry promotion and JS call/construct/error semantics are green.
8. **Continuation representation.** Reuse/extend current generator and async state owners after the synchronous frame/rooting model is stable.
9. **JS root-range inventory.** Which current fixed ranges are truly realm-persistent, page-wrapper-owned, async-lane-owned, or activation-shaped? The target owners are fixed by §6.3; the audit determines each field's row and destruction callback.
10. **Scheduling-lane surface.** What is the narrow lane API by which Promise jobs/`nextTick`/timers preserve JS ordering while the common async owner performs liveness, task rooting, cancellation, and teardown?
11. **Module instance extension.** Which current `ModuleDescriptor` TLA fields form the JS extension of `ContextModuleInstance`, and which dependency facts are immutable runtime-definition data?
12. **Mixed stack traces.** Define the minimal boundary record needed to stitch generated MIR/native frames to alternating interpreted activations without adding a second semantic call stack.

---

## 17. Decision Ledger

| ID | Decision | Status |
|---|---|---|
| **JSI1** | JavaScript gets a profile-owned semantic walker inside the integrated interpreter service | proposed |
| **JSI2** | `JsScript : Script` reuses every common owner field and adds only JavaScript-specific facts | proposed |
| **JSI3** | Binding/strict/TDZ/capture/class/module facts are computed once before backend selection | proposed |
| **JSI4** | T0 is boxed-only | proposed |
| **JSI5** | Lambda and JS frames use the same precise root/number side stacks only | proposed |
| **JSI6** | ECMAScript references are explicit interpreter records | proposed |
| **JSI7** | JavaScript uses structured NORMAL/RETURN/THROW/BREAK/CONTINUE completions | proposed |
| **JSI8** | ERROR-tagged helper returns become explicit THROW completions in the immediate frame | proposed |
| **JSI9** | `fn->invoke`/`fn->construct` remain the sole call/construct authorities | proposed |
| **JSI10** | No JavaScript AST/property inline caches | confirmed by D8.4.1v2 |
| **JSI11** | Unsupported scripts fall back only before execution and are counted | proposed |
| **JSI12** | T0 and T1 capturable bindings share environment cells | proposed |
| **JSI13** | Suspension remains compiled until heapified interpreter continuations land | proposed |
| **JSI14** | `func_ptr == NULL` never selects AST semantics; function body kind is explicit | proposed |
| **JSI15** | `jit_init()` and MIR lowering occur only inside the selected T1 path | proposed |
| **JSI16** | MIR-interp remains a backend diagnostic, not the AST tier | proposed / AI19 alignment |
| **JSI17** | T0 support is semantic-fact-aware and decided before declaration instantiation | proposed |
| **JSI18** | Promotion happens only at function entry; no general OSR | proposed / D8.1.1v5 alignment |
| **JSI19** | Static property names are discovered and sealed from `JsScript` plans before realm work | proposed |
| **JSI20** | Runtime-catalog/module ownership retains old `JsScript` generations while callbacks can execute them | proposed |
| **JSI21** | Differential, tier/language-crossing, page-coexistence, forced-GC, Test262, and release-performance gates are mandatory | proposed |
| **JSI22** | No bytecode, C2MIR work, vendor edits, or conservative stack scanning | confirmed |
| **JSI23** | One page document isolate owns one `Runtime` and canonical `EvalContext` | proposed / ES12 alignment |
| **JSI24** | Lambda and JavaScript values occupy one page GC heap and one traced object graph | confirmed by D1.3; page enforcement proposed |
| **JSI25** | Lambda and JavaScript activations nest on one root/number side-stack pair | confirmed by D1.3/D5.1; interpreter enforcement proposed |
| **JSI26** | One interpreter kernel owns activations; semantic evaluators/frame payloads remain profile-specific | proposed |
| **JSI27** | One context owns scheduling, with ordered JS microtask/nextTick and Lambda macrotask lanes | confirmed by D6.3.1; consolidation proposed |
| **JSI28** | Static owner pools link runtime-visible names into one context `NameId` domain | proposed / D4.6 alignment |
| **JSI29** | `JsScript : Script` is catalogued directly as `Script*`; no parallel script record/catalog exists | proposed |
| **JSI30** | JS realms/environments and Lambda scopes/closures remain semantically distinct | confirmed by D1.3 |
| **JSI31** | The shared script heap does not absorb Radiant DOM/view arena ownership | confirmed by D4.5.1v3 / ES10 |
| **JSI32** | `EvalContext` owns split lazy common capsules; no unified state megastruct | proposed / D5.4.2 alignment |
| **JSI33** | JS call/module/`this`/`super`/arguments state belongs to the common activation chain, not realm state | proposed |
| **JSI34** | `context` remains the only TLS execution root; the derived JS-state TLS cache is retired | proposed / D5.4.1 alignment |

---

## 18. Adoption Requirements

This proposal becomes implementation authority only when the following documentation changes land together:

1. add the JavaScript T0/T1 ruling to `doc/Lambda_Formal_Design.md`, recommended as **D8.1.3**;
2. revise D6.2.3 in place to explicitly distinguish Lambda snapshot captures from guest-profile closure semantics;
3. add a page/document-isolate ownership ruling to `doc/Lambda_Formal_Design.md` (recommended **D5.4.5**) stating one document `Runtime`/`EvalContext`/heap/module registry/side-stack set, with subdocuments as separate isolates handed off only at quiescent boundaries;
4. revise `Lambda_Design_Ast_Interpreter.md` §9 to point here and remove its stale `EvalSignal`/inline-cache claims;
5. update `Lambda_Design_DOM_State.md` §3.11–§3.12 to cite the integrated interpreter/heap/stack contract here and reconcile the EO6 implementation-status text with the current landed path;
6. update `JS_01`, `JS_04`, `JS_05`, `JS_08`, `JS_09`, and `JS_16` as each implementation phase actually lands;
7. create an implementation status record under `vibe/impl/` before code work claims a completed phase.

Until then, landed page paths already share substantial `Runtime`/`EvalContext`/heap infrastructure under ES12, but JavaScript execution remains parse → shared AST → whole-module MIR → MIR JIT/interpreter and there is no common T0 activation service or retained `JsScript`. This document is a proposal rather than a statement that the integrated interpreter is shipped.
