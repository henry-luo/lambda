# LambdaJS AST Interpreter — Tier-0 Execution and Mixed-Tier Design

**Date:** 2026-08-25

**Status:** PARTIALLY IMPLEMENTED — the explicit synchronous AST backend
landed on 2026-08-26 and now covers the implemented P2 core plus the P3
surface listed in §13, including dynamic `eval`, CommonJS, and the admitted
synchronous ES-module slice through the shared runtime bridge. The default
remains whole-script MIR; mixed T0/T1 environment cells, suspension, and AUTO
promotion remain planned.

**Scope:** LambdaJS only. This document specializes the interpreter direction established by **AI21** and the shared-AST rules **D8.2.1–D8.2.5**. It does not change the Lambda-language T0 semantics, does not extend C2MIR, and does not introduce a bytecode VM.

**Formal authority:** **D1.1–D1.7**, **D5.1.1**, **D5.3.2–D5.3.4**, **D5.4.1**, **D6.2.1–D6.2.4**, **D7.2.1**, **D8.1.1v5**, **D8.1.3v9**, **D8.2.1–D8.2.6**, **D8.4.1v2**, **D8.4.3v2**, and **D8.6.1–D8.6.4v2** in [`doc/Lambda_Formal_Design.md`](../doc/Lambda_Formal_Design.md). The formal specification wins on disagreement.

**Related designs:** [`Lambda_Design_Ast_Interpreter.md`](Lambda_Design_Ast_Interpreter.md) (AI1–AI22), [`Lambda_Design_Unified_AST.md`](Lambda_Design_Unified_AST.md) (U1–U36), [`Lambda_Design_Runtime_Error_Handling.md`](Lambda_Design_Runtime_Error_Handling.md), [`Lambda_Design_Stack_Frame_JS.md`](Lambda_Design_Stack_Frame_JS.md), [`doc/dev/js/JS_01_Compilation_Pipeline.md`](../doc/dev/js/JS_01_Compilation_Pipeline.md), [`JS_04_MIR_Lowering.md`](../doc/dev/js/JS_04_MIR_Lowering.md), [`JS_05_Functions_Closures.md`](../doc/dev/js/JS_05_Functions_Closures.md), [`JS_08_Iterators_Generators.md`](../doc/dev/js/JS_08_Iterators_Generators.md), and [`JS_09_Async_Modules.md`](../doc/dev/js/JS_09_Async_Modules.md).

---

## 1. Decision

LambdaJS will gain a boxed AST-walking Tier-0 executor over the existing shared `AstNode` graph. It will be a **JavaScript-semantic walker**, not another arm in Lambda's `eval_expr()` switch. It shares the runtime substrate below the language boundary:

- `Item` values and the Lambda heap;
- the canonical `EvalContext` and event loop;
- precise side-stack rooting and number ownership;
- `AstIndex`, pass scheduling, source spans, counters, and debugging hooks;
- the existing JavaScript runtime/property/call/iterator/promise helpers;
- the same module-state and name-pool contracts used by generated JavaScript.

It does **not** share Lambda truthiness, operator selection, arrays/maps, assignment semantics, closure capture, statement completion, or error-control policy. This division follows **D1.3**: guests reuse contracts below the semantic boundary and never inherit another language's coercion or object model.

The retained JavaScript compilation/runtime unit is named **`JsScript`**. A `JsScript` owns the source-facing artifacts and persistent semantic facts needed by both T0 and T1. MIR modules are derived caches attached to that owner, consistent with **D1.7**.

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

Putting both languages in one semantic switch would either branch on language at nearly every case or accidentally reuse the wrong contract. A sibling walker keeps the boundary explicit while still sharing the substrate that is genuinely common.

### 1.2 Goals

1. Skip MIR construction and link work for cold/run-once JavaScript.
2. Preserve JavaScript behavior by routing semantic operations through the same `js_*` runtime helpers as MIR.
3. Make the typed AST plus persistent semantic facts the executable source of truth.
4. Support safe T0↔T1 calls through the existing `JsFunction` call/construct capabilities.
5. Replace copied-closure/write-back behavior with real environment records shared by both tiers.
6. Provide a per-node execution surface for debugging, profiling, and future hot reload.
7. Fail closed before observable execution for unsupported unit shapes.
8. Establish exact differential gates against the current MIR implementation and Test262 baseline.

### 1.3 Non-goals

1. No bytecode or additional resident IR.
2. No unboxed interpreter lane.
3. No general on-stack replacement or arbitrary interpreter-PC transfer to MIR.
4. No inline caches or mutable per-AST-site property caches under **D8.4.1v2**.
5. No C2MIR work under **D1.6**.
6. No attempt to inherit Lambda language semantics for common-shaped nodes.
7. No promise of full ECMAScript coverage beyond the features admitted by LambdaJS's profile under **D1.1**.
8. No mid-execution fallback that repeats or skips already-observable side effects.

---

## 2. Current Baseline and the Actual Gap

The required frontend representation already exists:

- `JsAstNodeType` is `AstNodeType` and `JsAstNode` is `AstNode`;
- core expressions, statements, functions, classes, patterns, and modules use shared node shapes;
- every node has a source span and can receive a stable `AstNodeId`;
- `NameEntry`, `Type*`, `FnAnalysis`, and the `Item` value model are shared;
- `js_ast_children.cpp` already holds a centralized, source-order child description;
- JavaScript runtime semantics are exposed through boxed C helpers.

The missing executor is not the only gap. Four current lifetime/phase choices prevent a correct tree walker:

1. **The AST owner is transient.** `JsTranspiler` destroys its `AstIndex`, parser tree, name pool, and AST pool after the current run. An interpreted function retained by a timer, DOM listener, Promise reaction, module namespace, or returned closure would then point into freed source/AST storage.
2. **Authoritative semantic facts are MIR-session-owned.** `JsFuncCollected`, class tables, `module_consts`, capture layouts, strict/direct-eval flags, and several TDZ/Annex-B decisions are computed inside `JsMirTranspiler`. `fn->analysis` currently points at `JsFuncCollected::analysis`, whose surrounding storage is destroyed with the lowering session.
3. **Static name discovery is tied to MIR emission.** Property-name specifications are accumulated as lowering asks for name indices, then sealed before realm construction. T0 needs the same complete static name image without emitting MIR.
4. **Function values know only native/MIR bodies.** `JsFunction` has `func_ptr`, native bodies, closure slot arrays, `invoke`, and `construct`, but no AST definition, retained script owner, or interpreted lexical environment.

The current large-script policy does not solve these gaps. It chooses MIR's interpreter interface only after the source has been parsed, analyzed, and lowered to a complete MIR module. The new tier must decide before `jit_init()` and before any MIR module exists.

---

## 3. Normative Constraints

### JSI1 — JavaScript semantics stay in the JavaScript profile

The JavaScript walker selects JavaScript runtime helpers and JavaScript control rules. Shared node identity does not imply shared semantics (**D1.3**, **D8.2.1–D8.2.2**).

### JSI2 — `JsScript` is the retained owner

`JsScript` owns source, AST, scopes, indexes, semantic plans, static property-name specifications, module identity, and all derived T1 artifacts. Any `JsFunction` whose body references that AST points back to its owning `JsScript`. The owner outlives every callback/function that can execute it (**D1.7**, **D6.2.1**).

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

---

## 4. `JsScript`: Ownership and Persistent Facts

### 4.1 Required shape

The exact field layout is implementation-owned, but the semantic ownership must be equivalent to:

```c
struct JsScript {
    Runtime* runtime;
    const char* reference;
    const char* source;
    size_t source_length;
    uint32_t generation;
    uint32_t module_state_id;
    bool is_module;
    bool is_strict;

    Pool* ast_pool;
    NamePool* name_pool;
    AstNode* ast_root;
    AstIndex ast_index;
    NameScope* global_scope;

    JsBindingPlan* bindings;
    JsFunctionPlan* functions;
    JsClassPlan* classes;
    PropertyKeySpec* property_specs;

    JsInterpPlan interp_plan;
    MIR_context_t jit_context;
    ArrayList* satellites;
};
```

This is a responsibility sketch, not permission to duplicate facts already carried by `AstIndex`, `FnAnalysis`, the module registry, or another shared owner. Rule 13 still applies: existing carriers are promoted or reused before a new parallel table is introduced.

### 4.2 Lifetime

A `JsScript` generation is owned by the current JavaScript realm/context and, for modules, by the module registry entry. Initial implementation retains every generation until that realm's heap and deferred event work are torn down. This is intentionally conservative but precise: it retains compiler arenas, not heap values, and avoids inventing GC finalization/refcount edges before a live need exists.

The following must keep the generation alive:

- ordinary and bound functions with AST bodies;
- getters, setters, methods, constructors, and class field initializers;
- DOM/event callbacks, timers, immediates, and Promise reactions;
- CommonJS/ES module exports and cached namespaces;
- deferred dynamic import and top-level-await continuations;
- hot-reload closures created against an earlier generation.

A raw `AstNode*` without the owner is invalid. A `JsFunction` may keep a raw `JsScript*` because the realm registry owns the `JsScript` for at least the heap lifetime; it must not attempt to trace an arena pointer as a GC object.

### 4.3 Parser artifacts

The AST and any source bytes addressed by spans are retained. The Tree-sitter parser and tree need not be retained after AST construction if no AST field points into CST-owned storage and all syntax-dependent facts have been copied. This boundary must be verified before shortening CST lifetime.

### 4.4 One complete child contract

`ast_visit_core_children` remains the core catalog traversal. `js_profile.visit_ext_children` is wired to an adapter over the JS extension-child table for template literals/elements, static blocks, labels, regex, `with`, tagged templates, and future admitted extension kinds.

The adapter supplies the correct parent to the shared `AstChildVisitor`; it does not install a second recursive walk. This closes the current gap where `build_js_ast_indexed()` requests a profile index but the JavaScript profile's extension hook is a no-op.

### 4.5 Persistent pass products

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

### 4.6 Static property-name discovery

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

An environment record contains a traced outer link plus its binding storage. A binding cell contains an `Item` lane and flags such as initialized, mutable, deletable, and binding kind. Wide scalars are re-homed into record-owned scalar storage where required by the existing number-lifetime contract.

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
- materialize the correct mapped or unmapped `arguments` object before
  parameter defaults and retain it in the function environment;
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
- a class-private environment contributes declared source-name/identity-key
  pairs to the existing eval-private frame, from outer to inner class;
- compiled and interpreted callees read the same cells after eval mutates them.

Until this bridge is complete, a `JsScript` containing direct eval fails the T0 support scan. Indirect eval and `Function` construction may continue through a separate script compile/execute entry, subject to the same retained-owner rules.

---

## 6. Frames, Roots, and Interpreter State

### 6.1 Shared frame substrate

The reusable part of Lambda's `InterpFrameGuard` is extracted into a language-neutral rooted-window owner that:

1. snapshots the number/root side stacks;
2. opens a checked `LambdaRootFrame` of a planned size;
3. exposes rooted Item slots;
4. restores both watermarks exactly once on normal completion;
5. is safely abandoned only by an eligible native-fault recovery landing.

Lambda `InterpFrame` and JavaScript `JsInterpFrame` wrap that substrate. Their semantic fields remain separate. This preserves **D5.1.1**: native control frames plus the existing root and number side stacks are the only mechanisms; no fourth value stack is introduced.

### 6.2 JavaScript frame

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
caller interpreter frame
```

Control-only pointers may live in the C++ frame while active. Every `Item` they designate must reside in the root window or a traced heap object. Iterators held across child evaluation are rooted; no array/object payload pointer is cached across a `MAY_GC` helper.

### 6.3 `this`, `new.target`, and call state

The existing common JavaScript call kernel remains the dynamic-extent authority for sloppy `this`, `newTarget`, home global/class, module state, private home class, and restoration. When it enters an AST body, `js_interp_call_body()` copies the active values into rooted frame slots and constructs the function environment.

Frame-aware accessors then read from the active interpreter frame when one exists and from the existing compiled-call state otherwise. Arrow functions resolve the lexical binding through their captured environment, not the call receiver.

This staged bridge avoids duplicating OrdinaryCallBindThis or constructor setup in the walker while permitting later removal of avoidable ambient state.

### 6.4 Arguments

The call boundary remains dynamically sized `Item* + argc` under **D6.2.2v2**. The caller roots the span; the callee initializes parameter cells and retains any rest/arguments object it creates.

The function environment stores the materialized runtime object and the
metadata required to preserve:

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

### 7.10.1 Direct and indirect `eval`

An AST call is direct only when its identifier reference resolves to the
intrinsic `eval` function; an alias/property call follows ordinary indirect
eval. The direct call first evaluates the callee and all arguments exactly
once, then uses the existing dynamic-code compiler as the narrow source
boundary. It does not rerun or fall back from the enclosing AST script.

For an interpreted function, the caller projects its visible cells through
the canonical `EvalContext` bridge, invokes dynamic eval, writes mutable cells
back, and retains eval-created `var` names in that activation's shared journal.
Subsequent interpreted reads and writes consult that journal. At script global
scope, dynamic eval uses the realm-global path and the walker synchronizes the
realm lexical table back to the script module slab. This retains one runtime,
one EvalContext, one global object, and one module registry as required by
**D8.1.3v9** and **D5.4.1**.

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

The admitted P3 class subset retains its AST owner and delegates public and
private capabilities to the existing runtime metadata. Class evaluation owns a
traced private-home environment; member closures and nested functions retain
that exact class identity, while private fields/method brands and private
accesses use the common runtime kernels.

### 7.12 Modules

Each module is a `JsScript` with a stable module-state ID, namespace placeholder, import/export binding plan, and dependency links. Module loading retains the existing registry and resolution policy.

T0 module evaluation must preserve:

- placeholder namespace registration before circular traversal;
- dependency ordering and self-import handling;
- live binding cells rather than value snapshots where supported;
- active module-state switching during calls;
- cross-language `.ls` namespace imports;
- event-loop and async-parent drain ordering.

The current T0 admits synchronous CommonJS: its wrapper owns a private
module slab, resolves literal `require` targets relative to the retained
`JsScript`, and delegates cache/registry identity to the existing CJS
runtime. Nested execution restores the caller's active module slab. ES
modules use the same Runtime module registry, not a JS-private loader. An ES
`JsScript` registers its namespace placeholder, initializes its hoisted
function declarations in the module slab, then loads static dependencies and
executes its body. That ordering lets a circular dependency observe a function
export without creating a second function identity. Import plans are consulted
on every read and export writes propagate through the registry, so the
admitted named/default bindings stay live through named and star re-exports.

The implemented synchronous ES surface is:

- default, named, and namespace imports; default, named, namespace
  (`export * as name`), and non-ambiguous star exports;
- named and star re-exports, `import.meta.url`, and dynamic `import()` as a
  Promise over the same resolver;
- strict module execution with undefined top-level `this`, read-only imports,
  and a private module slab that never leaks bindings into the page global;
- `.ls` imports through the same registry descriptor. Lambda public functions
  keep their `TypeFunc` metadata and are invoked through the Lambda boxed-call
  ABI at the common call boundary; no adapter heap, second stack, or second
  interpreter is introduced.

This is deliberately a synchronous module tier. Top-level await, async module
evaluation, generators/async functions, and ambiguous star-export resolution
remain excluded before observable module execution. This matches **D8.1.3v9**.

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

`JsFunction` traces its lexical environment record and all existing Item edges. `JsScript*`/`AstNode*` are arena-owner pointers governed by realm lifetime and are not passed to `gc_mark_object_ptr` unless `JsScript` is later made a GC allocation by a separate ruling.

Environment record compaction/rebasing follows the current JS env tracer and scalar-tail contract. Any new raw env pointer in a function, generator, async context, Promise reaction, or module record must have an explicit trace/compact owner.

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

## 11. Entry Pipeline and Lifecycle

### 11.1 Script entry

The JavaScript entry driver is split into frontend, planning, and backend execution:

```text
1. own/copy source bytes
2. create JsScript + builder context
3. parse and build AST
4. run early errors
5. index and run persistent semantic plans
6. discover the static property-name image and run the T0 support plan
7. prepare canonical EvalContext/Input/static NamePool/module state
8. prelink and seal the property-name image
9. activate the runtime NamePool child and construct the realm/global object
10. load/plan imports in the required module-graph order
11. execute T0 or build/link/execute T1
12. drain microtasks/event loop while all body owners remain alive
13. publish result and release only backend-temporary state
14. retain JsScript generations required by the realm/module registry
```

`jit_init()` moves wholly inside the T1 branch. This is the point of the feature: a successful T0 run creates no MIR context and performs no MIR lowering/link.

### 11.2 Preamble and batch mode

The Test262/browser preamble becomes a retained `JsScript` plus its persistent declaration/property/module plan. Consumer scripts inherit the planned prefix exactly as current module property IDs require. They do not inherit transient MIR registers or `JsMirTranspiler` maps.

Batch reset must release all per-test environments, callbacks, module states, and `JsScript` generations not belonging to the retained harness. A stale function must never retain a dead AST or point at a reused module-state slot.

### 11.3 Event loop

The existing end-of-script drain order remains authoritative. T0 changes only the executable body. Timers, Promise jobs, DOM events, dynamic imports, before-exit/exit hooks, and trace flushing execute while their `JsScript` owners, source metadata, environment records, and optional satellites are alive.

### 11.4 Modules and nested compilation

Nested `require`, dynamic import, eval, and `Function` construction create or retrieve their own `JsScript` owners. The active compiler/transpiler TLS state must distinguish a retained script owner from an ephemeral MIR-lowering session so recovery cleanup cannot destroy the former twice.

---

## 12. Implementation Map

Names below describe ownership boundaries; final placement should reuse existing helpers and follow rule 13 before adding files.

### 12.1 New or promoted components

| Component | Responsibility |
|---|---|
| `lambda/js/js_script.*` | retained `JsScript` lifecycle, generation/module ownership, backend artifacts |
| `lambda/js/js_analysis.*` | persistent binding/function/class/module/property/suspension plans extracted from MIR phases |
| `lambda/js/js_environment.*` | GC-managed environment records and binding-cell access |
| `lambda/js/js_interp.hpp` | JavaScript frame, completion, reference, tier and stats contracts |
| `lambda/js/js_interp.cpp` | JavaScript expression/statement walker and function entry |
| `lambda/js/js_interp_plan.cpp` | frame/support planning over complete indexed AST |
| shared interpreter frame helper | root-window/number-watermark RAII reused by Lambda and JS |

If new translation units are added, update `build_lambda_config.json` and regenerate build files through `make`; never edit generated Lua files.

### 12.2 Existing components requiring change

| Existing area | Required change |
|---|---|
| `runtime/ast-core.*` | publish/wire the JavaScript profile extension-child contract; preserve one core traversal |
| `js/js_ast_children.cpp` | provide profile adapter without duplicating recursion |
| `js/js_transpiler.*`, `js/js_scope.cpp` | separate builder session cleanup from retained `JsScript` ownership |
| `js/js_mir_context.hpp` and MIR analysis files | split persistent semantic facts from lowering-local MIR state |
| `js/js_mir_module_batch_lowering.cpp` | consume persistent plans; stop owning authoritative binding/capture/class facts |
| `js/js_mir_expression_lowering.cpp` | share reference/environment plans and later shared environment-cell ABI |
| `js/js_mir_statement_lowering.cpp` | emit declaration/completion/iterator behavior from shared plans |
| `js/js_function.hpp` | add explicit AST body/owner/environment fields while preserving ABI prefix |
| `js/js_runtime_function.cpp` | AST factories, capability finalization, environment tracing |
| `js/js_runtime.cpp` | AST body dispatch inside common call/construct authority |
| `js/js_mir_entrypoints_require.cpp` | frontend/backend split, pre-MIR tier choice, retained lifecycle |
| module/runtime state files | own `JsScript` generations and stable module/env roots |

Vendor sources, C2MIR, generated `parser.c`, generated Lua, and `log.conf` remain untouched.

---

## 13. Staged Implementation

### P0 — Design/spec and differential harness

1. Adopt this proposal's ledger.
2. Revise the stale Stage 2 text in `Lambda_Design_Ast_Interpreter.md`:
   - `EvalSignal` does not transfer unchanged; JS uses `JsCompletion`.
   - per-AST-node inline caches are removed under **D8.4.1v2**.
3. Add the formal JavaScript tier ruling (recommended new **D8.1.3**, rather than overloading Lambda-specific D8.1.1) and clarify D6.2.3's Lambda-only snapshot rule.
4. Add `JS_TIER` plumbing, stats, and a differential runner that initially reports every script unsupported.

**Gate:** selector and accounting tests prove no silent fallback; no runtime behavior changes under default policy.

### P1 — `JsScript` and persistent planning

1. Introduce retained ownership and move AST/name/index cleanup to it.
2. Wire complete profile child enumeration.
3. Extract function collection, strict/direct-eval/with facts, bindings, module slots, capture inputs, class facts, and property-name discovery from MIR lifetime.
4. Make MIR consume the new persistent facts with unchanged output.

**Gate:** eager MIR and MIR-interp baselines remain identical; `fn->analysis` and every source/name pointer remain valid through event-loop drain and batch reuse; **D8.6.4v2** timing/LOC diagnostics are recorded.

### P2 — Restricted synchronous vertical slice — implemented 2026-08-26

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

**Implemented boundary:** `JS_EXECUTION_BACKEND=ast` selects the retained
`JsScript` path. It supports the listed P2 forms with simple identifier
bindings/parameters, ordinary functions/arrows/constructors, native
callbacks, structured completions, and precisely traced mutable lexical
environments. Classes, modules, direct eval, `with`, destructuring/default/
rest/spread, optional chaining, object methods/accessors, generators, async,
and top-level await were rejected before declaration instantiation. P3 adds
the admitted forms below. A forced AST request returns the JavaScript rejection
error rather than silently replaying in MIR; with no selector, existing MIR
remains the policy. This is the explicit-backend contract of **D8.1.3v9**,
preserving **D8.4.3v2** error transport and **D1.3** guest semantics.

**Gate:** focused ownership, module-state, call/construct, native-callback,
completion, closure/per-iteration, forced-GC, and pre-execution-rejection
tests pass. Broader eager-MIR differential and sanitizer corpus gates remain
P3/P4 work.

### P3 — Synchronous breadth — partially implemented 2026-08-26

Implemented:

- destructuring/default/rest/spread, including parameters, catch, assignment,
  and iteration heads;
- synchronous `for-in`/`for-of`, per-iteration lexical cells, and
  `IteratorClose` on abrupt completions;
- switch and labeled break/continue completions;
- optional chaining, logical assignment, and delete references;
- regex, templates, and tagged templates through the common call runtime;
- `with` through the existing object-environment stack, including escaped AST
  closures;
- classes: public/private methods and accessors, public/private
  instance/static fields, private `in`, static blocks, and implicit derived
  construction, all through the existing class function/property kernels.
- direct/indirect `eval`: direct calls bridge interpreted function cells and
  eval-local vars through the shared EvalContext; global lexical mutations
  synchronize back to the retained script slab.
- `new.target`, including lexical preservation when an AST arrow escapes its
  constructing function.
- public `super`: explicit derived constructors, `this` TDZ-to-bound
  transition, post-super public fields, property reads/writes/calls, static
  methods, and arrows/object methods retaining their lexical home object.
- `arguments`: runtime-object materialization before defaults, mapped sloppy
  simple parameters, unmapped strict/non-simple parameters, `callee`, and
  escaped arrow lookup through the captured function environment.
- synchronous ES modules: registry-owned namespace placeholders, declaration
  instantiation before dependency traversal, default/named/namespace imports,
  default/named/namespace/non-ambiguous-star exports, named/star re-exports,
  `import.meta.url`, dynamic `import()`, live bindings, circular function
  imports, and `.ls` imports through the shared registry and boxed-call ABI.

Still excluded: generators, async functions/module evaluation, top-level
await, and ambiguous star exports. These forms fail before observable
execution under the forced AST selector.

**Gate:** the committed `test_js_gtest` corpus partition is complete—every discovered row is an exact T0 match or an explicit pre-execution exclusion, with no unclassified or silent fallback rows.

### P4 — Common environment ABI and promotion

1. Convert MIR captured/eval-visible bindings to shared environment cells.
2. Validate every T0/T1/native call and construct crossing over the shared environment ABI.
3. Implement per-definition boxed satellites and promotion publication.
4. Add AUTO counters and hot-function tests.
5. Demote size-based MIR-interp selection to a backend diagnostic for admitted scripts.

**Gate:** full T0/T1/native call matrix passes under forced GC; closure mutation, per-iteration cells, direct eval, class constructors, bound/proxy calls, and error identity remain exact across tier changes.

### P5 — Suspension

1. Add heapified generator continuations.
2. Add async/await and async-generator continuation driving.
3. Integrate top-level await/module async-parent ordering.
4. Differentially test return/throw into suspended `finally` and active iterators.

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

### 14.4 GC and ownership

Run forced collection at every eligible helper/call boundary and verify:

- frame scratch operands remain live;
- environment cells and scalar tails move/rebase correctly;
- `JsFunction` traces the interpreted environment;
- old `JsScript` generations survive while callbacks reference them;
- batch reset releases dead generations without stale code/AST/module pointers;
- no persistent root or deferred MIR/AST owner grows per completed test.

The dynamic forced-GC/self-baselining method under **D8.6.3** remains the liveness oracle; do not add a divergent static shadow analysis.

### 14.5 Corpus gates

1. focused interpreter unit tests;
2. admitted `test/js` differential subset;
3. full `test_js_gtest` partition;
4. Radiant/UI/browser preamble scripts;
5. current Test262 baseline and partial sets;
6. modules/CommonJS/Node compatibility suites;
7. forced-GC and sanitizer runs;
8. release performance runs.

The Test262 runner is never changed to hide an engine failure, crash, timeout, or unsupported feature. Rule 18 remains absolute.

### 14.6 Performance measurements

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

---

## 16. Open Implementation Questions

These questions do not reopen the decisions above:

1. **Existing env promotion.** Can current `GC_TYPE_JS_ENV` be extended in place to represent outer-linked binding cells and metadata, or should a versioned JS environment layout be introduced? The audit must preserve every current tracer/compactor owner.
2. **`JsScript` storage location.** The first implementation may use a realm-owned registry entry or promote a compatible existing module/runtime record. It must not duplicate module identity or use a function-instance refcount as the initial lifetime authority.
3. **Fact layout.** Which `JsFuncCollected` fields become `FnAnalysis`, which require a JS function-plan extension, and which remain MIR-only? The rule is “shared semantic answer once; backend mechanics local.”
4. **Property-name synthetic inventory.** Which names are semantic products of source forms and which are backend-private? The former belong in `JsScript` planning; the latter must still be sealed before dynamic NamePool activation.
5. **Restricted-slice admission.** The exact first supported fixture manifest is established by the P2 differential survey, not by weakening semantics for an inconvenient node.
6. **Promotion threshold.** Five is the initial parity value; release corpus measurements decide whether JS retains it.
7. **Direct self-tail handoff.** KIV until ordinary entry promotion and JS call/construct/error semantics are green.
8. **Continuation representation.** Reuse/extend current generator and async state owners after the synchronous frame/rooting model is stable.

---

## 17. Decision Ledger

| ID | Decision | Status |
|---|---|---|
| **JSI1** | JavaScript gets a separate semantic walker over the shared AST/runtime substrate | implemented for P2 |
| **JSI2** | `JsScript` is the retained AST/source/fact/T1-artifact owner | implemented for the AST owner and retained ES import/export plans; MIR-fact migration pending |
| **JSI3** | Binding/strict/TDZ/capture/class/module facts are computed once before backend selection | partially implemented for AST module binding plans; broader MIR fact migration pending |
| **JSI4** | T0 is boxed-only | implemented for P2 |
| **JSI5** | Frames use existing precise root/number side stacks only | implemented for P2 |
| **JSI6** | ECMAScript references are explicit interpreter records | implemented for identifier/member, update, delete, optional, and `with` P3 forms |
| **JSI7** | JavaScript uses structured NORMAL/RETURN/THROW/BREAK/CONTINUE completions | implemented for P2 plus labels/IteratorClose in P3 |
| **JSI8** | ERROR-tagged helper returns become explicit THROW completions in the immediate frame | implemented for P2 |
| **JSI9** | `fn->invoke`/`fn->construct` remain the sole JS call/construct authorities | implemented; the shared kernel also recognizes published Lambda boxed-call values |
| **JSI10** | No JavaScript AST/property inline caches | confirmed by D8.4.1v2 |
| **JSI11** | Unsupported scripts fall back only before execution and are counted | implemented as forced-backend rejection; AUTO fallback pending |
| **JSI12** | T0 and T1 capturable bindings share environment cells | proposed |
| **JSI13** | Suspension remains compiled until heapified interpreter continuations land | proposed |
| **JSI14** | `func_ptr == NULL` never selects AST semantics; function body kind is explicit | proposed |
| **JSI15** | `jit_init()` and MIR lowering occur only inside the selected T1 path | proposed |
| **JSI16** | MIR-interp remains a backend diagnostic, not the AST tier | proposed / AI19 alignment |
| **JSI17** | T0 support is semantic-fact-aware and decided before declaration instantiation | proposed |
| **JSI18** | Promotion happens only at function entry; no general OSR | proposed / D8.1.1v5 alignment |
| **JSI19** | Static property names are discovered and sealed from `JsScript` plans before realm work | proposed |
| **JSI20** | Realm/module ownership retains old `JsScript` generations while callbacks can execute them | proposed |
| **JSI21** | Differential, tier-crossing, forced-GC, Test262, and release-performance gates are mandatory | proposed |
| **JSI22** | No bytecode, C2MIR work, vendor edits, or conservative stack scanning | confirmed |

---

## 18. Adoption Requirements

This proposal's P2 implementation authority is now recorded by
**D6.2.3v2**, **D8.1.3v9**, and
[`vibe/impl/Lambda_Impl_JS_Interpreter.md`](impl/Lambda_Impl_JS_Interpreter.md).
The following requirements remain for later phases:

1. migrate authoritative MIR-session binding/property/function facts to `JsScript` before T0/T1 mixing;
2. revise `Lambda_Design_Ast_Interpreter.md` §9 to point here and remove its stale `EvalSignal`/inline-cache claims;
3. update `JS_01`, `JS_04`, `JS_05`, `JS_08`, `JS_09`, and `JS_16` as each implementation phase actually lands;
4. complete the P3–P6 gates before changing the unset backend policy.

Until those later gates land, the default JavaScript pipeline remains parse →
shared AST → whole-module MIR → MIR JIT/interpreter. The explicit AST backend
is a shipped restricted synchronous tier, not a full-coverage replacement.
