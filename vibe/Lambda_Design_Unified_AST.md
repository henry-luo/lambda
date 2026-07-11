# Unified AST & Compiler Design — Lambda + LambdaJS (and beyond)

**Date:** 2026-07-11 (rev 4 — CodeQL prior art, call-site inference common)
**Status:** Proposal — ledger U1–U18 in §9; U13, U15–U18 user-confirmed
**Related:** `vibe/Lambda_Semantics_Features.md` Part 1 (J1–J6, G1–G8), `vibe/Lambda_Design_Concurrency.md` (K17), `vibe/Lambda_Desing_Native_Module.md`, `vibe/Lambda_Code_Clean_Up.md` §6, `vibe/Lambda_GC_Root_Issue.md`, `vibe/Lambda_Expr_For_Clauses2.md` (FC1–FC9)

---

## 1. Goal and Scope

Unify the Lambda-script and LambdaJS compilers onto a **common AST**:

1. one AST tree that both `build_ast.cpp` (Lambda) and `build_js_ast.cpp` (JS) produce;
2. shared semantic analysis on that tree — symbol binding, capture analysis, evidence-based type inference;
3. one shared AST→MIR transpiler core, with per-language semantic handlers;
4. deeper unification of the compile-time/runtime substrate: name pool, shape pool, const pool, variable lookup, module registry;
5. a foundation such that Python, Ruby, Bash (and future front-ends) become *builders + language profiles* on the same spine instead of parallel ~300 KB transpilers.

The common AST is organized as a **leveled core-node catalog** (§2): each level defines the core nodes all languages map onto, targeting ≥80% of any guest language expressed in core nodes; language-unique nodes are kept to a minimum and per-language variance is carried as extra fields or clauses on the shared nodes, not as new node kinds.

### 1.1 Relation to prior decisions (J-ledger amendment)

- **J1 (no portable IR) — intact.** The common AST is a private, in-memory compiler structure, exactly like MIR. Nothing here creates a distributable format; scripts still ship as source text.
- **J2 (interop = Items/VMap + C ABI; "each front-end transpiles itself") — amended → J2-R.** The *interop contract* is unchanged: cross-language calls still cross at the Item/C-ABI seam, errors still cross as values (J3). What changes is an implementation statement: in-tree front-ends now share compiler infrastructure (AST, analysis, MIR lowering core). J6 (curated runtime, in-tree front-ends only) makes this safe — there is no third-party front-end contract to break.
- **J5 (guests are dialects) — reinforced.** A shared spine makes the "dialect" framing structural: a guest language is a grammar + builder + LangProfile, and what the profile doesn't cover simply isn't supported.
- **K17 doctrine ("extract the common core only after two working clients exist; protect what's green") — adopted as the migration method** (§8). We have two working clients today: the Lambda and JS pipelines, both green on their suites. This project *is* the extraction step, done deliberately.

### 1.2 Why now — the discovered convergence

The research pass found that far more is already unified than the code layout suggests. **This is not a merge of two foreign compilers; it is the completion of a convergence that was designed in from the start:**

| Layer | Status today |
|---|---|
| Base AST node layout | **Already binary-compatible.** `JsAstNode` = `{node_type, Type*, next, TSNode}` deliberately matches `AstNode` field-for-field (js_ast.hpp:239–240: "field order must match Lambda's AstNode… NameEntry.node points at JsAstNode memory") |
| Symbol-table entry | **Already shared** — `NameEntry` is one struct used by both |
| Type model | **Already shared** — JS nodes carry Lambda `Type*` (`&TYPE_FLOAT`, `&TYPE_ANY`, …); TS annotations resolve to Lambda `Type*` |
| Value model | **Already shared** — JS uses Lambda's self-tagged `Item` wholesale; only additions: `LMD_TYPE_UNDEFINED` + two non-double sentinels (js_runtime.h:27–40) |
| Object shapes | **Already shared** — JS objects are Lambda `Map`/`TypeMap`/`ShapeEntry` discriminated by `map_kind`; `TypeMap` already carries the JS shape-transition/descriptor fields |
| Name interning | **Already shared** — one `NamePool` for both |
| Module system | **Already shared** — one `module_registry` keyed by path with `source_lang` |
| MIR linkage | **Already shared** — one `import_resolver`, one `Context* rt` |
| Node-kind numbering precedent | **Already exists** — TS extends the JS enum from 1000 (`TS_AST_NODE_TYPE_ANNOTATION = 1000`, sentinel 1100); `build_js_ast` builds both JS and TS nodes; type-only nodes stripped before shared lowering |
| MIR emission primitives | Duplicated verbatim (`emit_call_N` ≡ `jm_call_N`, `new_reg` ≡ `jm_new_reg` — see Clean_Up §6.4) |
| Param-type inference | Duplicated in *concept* — both evidence-based (`InferCtx`/`INFER_*` bits, transpile-mir.cpp:11083 vs `JsParamEvidence`, js_mir_context.hpp:82), different resolution policies |
| Capture analysis | Duplicated in *concept* — both free-variable walks producing capture lists (`analyze_captures`/`CaptureInfo` vs `jm_analyze_captures`/`JsCaptureEntry`) |
| Compile-time var entry | Duplicated in *shape* — `MirVarEntry{reg, mir_type, type_id, env_offset,…}` ≅ `JsMirVarEntry{reg, mir_type, type_id, env_slot,…}` |
| AST node kinds/structs | Divergent enums and leaf structs — **this is the actual gap** |
| Scope/binding pass | Divergent: Lambda binds authoritatively at build time; JS has a coarse build-time `JsScope` + authoritative MIR-time side tables (`JsFuncCollected`) |

The unification surface is therefore: **(a) the node-kind space and leaf structs, (b) the analysis passes, (c) the MIR lowering core, (d) four substrate systems (shape pool, const pool, var lookup, module import binding).**

---

## 2. The Common AST — Leveled Core-Node Catalog

The core AST is defined in **levels**, each building on the ones below. Level 0 (the type system) is already in place and is the model the rest follows: one shared representation, per-language surface syntax resolving into it.

| Level | Layer | Contents |
|---|---|---|
| **L0** | Type | `Type*`/`TypeId` model, `TypeMap`/`ShapeEntry`, `NamePool`/`ShapePool` — **already unified** |
| **L1** | Expression | literal, ident, unary, binary, call, member, if-expr, for-expr, match/switch-expr, … |
| **L2** | Statement & variable | declarations, assignment, control flow, error statements — variables given particular care |
| **L3** | Binding & pattern | declarators, params, destructuring patterns (proposed fill for the level gap — binding targets cut across L2/L4/L5 and deserve their own layer) |
| **L4** | Function & closure | fn/pn, arrow/async/generator flags, captures, yield/await |
| **L5** | Class & interface | class members, methods/fields, interfaces resolving to `TypeObject` |
| **L6** | Module | import/export, module root, registry integration |

### 2.1 Base node and the three-tier variance mechanism

The existing base contract is correct and stays:

```c
struct AstNode {
    AstNodeType node_type;   // unified kind space, §2.2
    Type*       type;        // Lambda Type* (L0)
    AstNode*    next;        // intrusive sibling list
    TSNode      node;        // CST node of the *module's* grammar
};
```

Per-language variance on core nodes is expressed in exactly three ways, in order of preference:

1. **Fields/flags on the shared struct** — e.g. `FuncFlags{is_proc, is_arrow, is_async, is_generator, …}` covers fn/pn *and* JS function kinds in one node; `CatchNode.type_filter` is NULL for JS but carries Python's `except TypeError` / Ruby's `rescue TypeError`; `Declarator.error_name` carries Lambda's `let a^err`. Cheap, uniform, and shared passes see through it.
2. **Clause chains** — clause-bearing core nodes (for-expr, class, function) carry an optional `AstNode* clauses` list of *clause nodes*. Core clauses are shared (for-binding, where/if, let); language clauses attach to the same list (Lambda's `group by`/`order by`/`join on` per FC1–FC9; Python decorators; Lambda object-type constraints). Shared passes iterate the chain and hand unknown clause kinds to the profile.
3. **Language-range node kinds** — the last resort, for constructs with genuinely language-specific *structure* (JS `with`, Lambda element literals, Bash redirection). Kept to a minimum by design.

Allocation stays pool-based; deeper per-function analysis hangs off `FnAnalysis` (§3.3), not nodes.

### 2.2 One node-kind space

A single `AstNodeType` integer space. Core kinds are blocked by level so dispatch tables and debug dumps read cleanly; language ranges follow (generalizing the existing TS-at-1000 precedent):

| Range | Owner |
|---|---|
| 1–49 | L0 type surface (`AST_TYPE_REF`) |
| 50–149 | L1 expressions |
| 150–249 | L2 statements |
| 250–299 | L3 bindings & patterns |
| 300–349 | L4 functions |
| 350–399 | L5 classes |
| 400–449 | L6 modules |
| 450–499 | reserved core |
| 500–999 | Lambda-specific |
| 1000–1499 | JS-specific |
| 1500–1999 | TS (renumbered from 1000) |
| 2000+ | Python (2000), Ruby (2500), Bash (3000) |

Rules: a kind is core only if its *structure* (child slots) is language-independent — semantics may differ (JS `==` vs Lambda `==`); that is the lowering profile's problem (§4), never grounds for a second node kind. Promotion from language range to core is allowed later; demotion never.

### 2.3 The core-node catalog

Notation: each table lists the core node, its fields (beyond the base), and what maps onto it from Lambda (`AST_NODE_*`), JS (`JS_AST_NODE_*`), and — prospectively — Python/Ruby. *(var.)* marks fields that exist for one language's variance, per §2.1 tier 1.

#### L0 — Type (in place; the model)

The type *model* is already unified: `TypeId`, `Type`, `TypeConst` family, `TypeFunc`, `TypeMap`+`ShapeEntry`, `TypeArray`/`TypeList`, `TypeElmt`, `TypeObject`, `TypePattern`, the `LIT_*`/`TYPE_*` singletons, `NamePool`, `ShapePool`, and the Item runtime mapping. JS already annotates its nodes with Lambda `Type*`; TS resolves annotations to `Type*` then strips.

Core AST surface at L0 is deliberately tiny:

| Core node | Fields | Lambda | JS/TS | Py/Rb |
|---|---|---|---|---|
| `AST_TYPE_REF` | resolves to `Type*` in `node->type` | wraps the Lambda type-syntax nodes | TS annotation nodes | Py type hints (advisory) |

The rich type-*syntax* nodes stay per-language (Lambda's `LIST_TYPE/MAP_TYPE/FUNC_TYPE/BINARY_TYPE/…` in the Lambda range — its type syntax is a language feature; TS annotation nodes in the TS range). All of them must terminate in a `Type*`; everything above L0 consumes only the `Type*`.

#### L1 — Expression core

| Core node | Fields | Lambda maps | JS maps | Py/Rb maps |
|---|---|---|---|---|
| `AST_LITERAL` | `LiteralKind {null, undefined, bool, int, float, decimal, string, symbol}`; value union; `has_decimal`, `is_bigint` hints | `AST_NODE_PRIMARY` (literal) | `JS_AST_NODE_LITERAL` (+BigInt) | int/str/None/True… |
| `AST_IDENT` | `String* name; NameEntry* entry` | `AST_NODE_IDENT` | `IDENTIFIER` | names |
| `AST_ARRAY` | `items; length` | `AST_NODE_ARRAY` | `ARRAY_EXPRESSION` | list literal |
| `AST_MAP` | `entries` (list of `AST_MAP_ENTRY`) | `AST_NODE_MAP` | `OBJECT_EXPRESSION` | dict / hash |
| `AST_MAP_ENTRY` | `key; value; computed; shorthand` *(var.)* | `AST_NODE_KEY_EXPR` | `PROPERTY` (method/get/set forms become entries whose value is an L4 `AST_FUNC`) | dict entry |
| `AST_SEQ` | `items; SeqKind {list, tuple, comma}` *(var.)* | `AST_NODE_LIST` / `AST_NODE_CONTENT` | `SEQUENCE_EXPRESSION` | tuple |
| `AST_UNARY` | `Operator op; operand; prefix` | `AST_NODE_UNARY` | `UNARY_EXPRESSION` | unary |
| `AST_BINARY` | `Operator op; left; right` | `AST_NODE_BINARY` | `BINARY_EXPRESSION` (+logical) | binary/boolean ops |
| `AST_ASSIGN` | `Operator op (incl. compound); target; value; lhs_parenthesized` *(var.)* — usable in expr (JS, Py walrus) and stmt position (§L2) | `AST_NODE_ASSIGN`, `ASSIGN_STAM`, `INDEX_ASSIGN_STAM`, `MEMBER_ASSIGN_STAM` (target = ident/member/pattern) | `ASSIGNMENT_EXPRESSION` | assignment, `:=` |
| `AST_IF` | `cond; then; otherwise` — one node for if-expr and if-stmt; value-ness from context | `AST_NODE_IF_EXPR` | `IF_STATEMENT`, `CONDITIONAL_EXPRESSION` | if / ternary |
| `AST_CALL` | `callee; args; flags {optional, pipe_inject, propagate, can_raise}` *(var.)*; named args as `AST_NAMED_ARG` entries | `AST_NODE_CALL_EXPR`, `SYS_FUNC`, `NAMED_ARG` | `CALL_EXPRESSION` | call, kwargs |
| `AST_MEMBER` | `object; property; computed; optional` | `AST_NODE_MEMBER_EXPR`, `INDEX_EXPR` | `MEMBER_EXPRESSION` | attribute / subscript |
| `AST_FOR_EXPR` | clause chain (§2.1 tier 2) + `body/then` — the comprehension/query node | `AST_NODE_FOR_EXPR` with core clauses `CLAUSE_FOR` (binding, iterable, key/index flags), `CLAUSE_WHERE`, `CLAUSE_LET`; Lambda-range clauses `CLAUSE_GROUP`, `CLAUSE_ORDER`, `CLAUSE_JOIN`, limit/offset (FC1–FC9) | — (JS has no comprehension; never produced) | **Python comprehensions = exactly `CLAUSE_FOR` + `CLAUSE_WHERE` + body** |
| `AST_MATCH` | `scrutinee; arms` | `AST_NODE_MATCH_EXPR` | — | Py `match`, Rb `case/in` |
| `AST_MATCH_ARM` | `pattern; guard` *(var.)*`; body` | `AST_NODE_MATCH_ARM` | — | arm with guard |
| `AST_SWITCH` | `discriminant; cases; fallthrough semantics via profile` | — | `SWITCH_STATEMENT`+`SWITCH_CASE` | Bash `case` |
| `AST_INTERP_STR` | `quasis; exprs` | — today; **Lambda to adopt interpolation in some manner later** (node is ready) | `TEMPLATE_LITERAL`+`TEMPLATE_ELEMENT` | **f-strings, Ruby `#{}`, Bash strings** |
| `AST_RANGE` | `start; end; inclusive; step` *(var.)* | `to` operator | — | Py `range`/slice, Rb `..`/`...` |
| `AST_SPREAD` | `argument` | `AST_NODE_SPREAD` | `SPREAD_ELEMENT` | `*args` splat |
| `AST_NEW` | `callee; args` | — (never produced) | `NEW_EXPRESSION` (`new.target` stays a JS-range detail) | — (Py/Rb instantiate via plain `AST_CALL`; profile dispatches) |

Language-range L1 nodes (kept, minimal): **Lambda** — `PIPE`, `CURRENT_ITEM ~`/`CURRENT_INDEX`/`LAST_INDEX`, `QUERY_EXPR`, `PATH_EXPR`/`PATH_INDEX_EXPR`/`PARENT_EXPR`, `ELEMENT`/`CONTENT` (document model), string/symbol pattern nodes, `OBJECT_LITERAL`; **JS** — `REGEX`, `TAGGED_TEMPLATE`.

#### L2 — Statement core (variables get particular care)

| Core node | Fields | Lambda maps | JS maps | Py/Rb maps |
|---|---|---|---|---|
| `AST_SCRIPT` | `body; NameScope* globals; LangProfile* (via Script)` | `AST_SCRIPT` | `PROGRAM` | module body |
| `AST_BLOCK` | `statements; NameScope*; label?` | let-block / body | `BLOCK_STATEMENT` | suite |
| `AST_EXPR_STMT` | `expression` | expression content | `EXPRESSION_STATEMENT` | expr stmt |
| `AST_VAR_DECL` | `DeclKind {let, var, const, pub}` *(var.)*`; declarators` (L3) | `LET_STAM`, `VAR_STAM`, `PUB_STAM` | `VARIABLE_DECLARATION` (+`is_using`) | synthesized (below) |
| `AST_WHILE` / `AST_DO_WHILE` | `cond; body; NameScope*; label?` | `WHILE_STAM` | `WHILE`/`DO_WHILE` | while / until (flag) |
| `AST_FOR_C` | `init; test; update; body; label?` | — | `FOR_STATEMENT` | — |
| `AST_FOR_ITER` | `target (L3 pattern); iterable; body; IterKind {of, in, range, await}` *(var.)*`; label?` | `FOR_STAM`, `LOOP` clause reuse | `FOR_OF`/`FOR_IN` | Py/Rb `for`/`each` |
| `AST_BREAK` / `AST_CONTINUE` | `label?` | `BREAK_STAM`/`CONTINUE_STAM` | `BREAK`/`CONTINUE` | break/continue/next |
| `AST_RETURN` | `value` | `RETURN_STAM` | `RETURN_STATEMENT` | return |
| `AST_RAISE` | `value` | `RAISE_STAM`/`RAISE_EXPR` | `THROW_STATEMENT` | raise / raise |
| `AST_TRY` | `block; catches; finalizer` | — (Lambda uses `T^E`; never produced) | `TRY`+`CATCH`+`FINALLY` | try/except, begin/rescue |
| `AST_CATCH` | `param (L3 pattern); type_filter` *(var.— Py/Rb typed excepts)*`; body; next` | — | `CATCH_CLAUSE` | `except T as e` / `rescue T => e` |
| `AST_IF`, `AST_ASSIGN`, `AST_SWITCH`, `AST_MATCH` | shared with L1 — same node in statement position (`AST_SWITCH` also gains `label?`) | | | |

**Labels are a core *field*, not a node** (user decision): `String* label` lives on the labelable core statements — the loop family, `AST_BLOCK`, `AST_SWITCH` — and `break`/`continue` carry the target label. There is no `LABELED_STATEMENT` wrapper node; the JS builder folds `label: stmt` into the statement's field, and for the rare legal-but-exotic cases (labeled `if`, labeled expression statement) it normalizes by wrapping in a labeled `AST_BLOCK` — semantically equivalent, since `break label` continues after the labeled statement either way. Lambda doesn't have labels today but the core field is there if it ever wants them; Python/Ruby builders never set it.

Language-range L2 nodes: **JS** — `WITH_STATEMENT`; **Lambda** — `PIPE_FILE_STAM`, `TYPE_STAM` (type declarations); **Bash** — redirection/pipeline statements (expected thick).

**The variable story** (the care the level deserves) spans four layers, each defined once:

1. **Declaration surface:** one `AST_VAR_DECL` + `AST_DECLARATOR` core; `DeclKind` covers Lambda `let/var/pub` and JS `var/let/const`. Python/Ruby have no declaration syntax — their builders **synthesize declarators at first assignment during binding** (the profile's scoping rule), so downstream passes see a uniform declared world.
2. **Binding rules:** applied at build time by each builder on shared `NameScope`/`NameEntry` (§3.2): Lambda immutability + `var` mutability; JS `var` hoisting to function scope, `let/const` TDZ flags; Python function-scope + `global/nonlocal` (profile clause); Ruby local-on-assign.
3. **`NameEntry` flag superset:** `{is_mutable, is_var_param, has_type_annotation, type_widened}` (Lambda) ∪ `{var_kind, tdz, is_private}` (JS) ∪ `{is_global_decl, is_nonlocal}` (Py).
4. **Storage:** decided once in the shared emitter (§5.3): MIR register (native or boxed) / closure scope-env slot / module BSS slot — the unified `VarEntry` replaces `MirVarEntry` ≅ `JsMirVarEntry`.

#### L3 — Binding & pattern core (proposed level)

Binding targets recur in declarators, params, for-targets, catch params, and match arms — one shared vocabulary:

| Core node | Fields | Lambda maps | JS maps | Py/Rb maps |
|---|---|---|---|---|
| `AST_DECLARATOR` | `target (ident or pattern); AST_TYPE_REF* type_annot; init; error_name` *(var.— Lambda `let a^err`)* | assign/decompose parts of let/var | `VARIABLE_DECLARATOR` | synthesized |
| `AST_PARAM` | `target; type_annot; default_value; is_rest; is_mutable` *(var.— pn params)* | `AST_NODE_PARAM` | `PARAMETER`, `ASSIGNMENT_PATTERN` (default), `REST_ELEMENT` | params, `*args`/`**kwargs` |
| `AST_PATTERN_ARRAY` | `elements` (patterns, holes, rest) | `AST_NODE_DECOMPOSE` (positional) | `ARRAY_PATTERN` | tuple unpack |
| `AST_PATTERN_MAP` | `entries {key, value-pattern, default}` | `AST_NODE_DECOMPOSE` (named) | `OBJECT_PATTERN`+`REST_PROPERTY` | `**rest`, match mapping |
| `AST_PATTERN_DEFAULT` | `pattern; fallback` | — | `ASSIGNMENT_PATTERN` | default |
| `AST_PATTERN_REST` | `target` | — | `REST_ELEMENT/PROPERTY` | `*rest` |

Match-arm patterns reuse these plus literals and `AST_TYPE_REF` type-tests; Lambda's string/symbol pattern sub-language stays Lambda-range (it compiles to `TypePattern`/RE2 at L0).

Destructuring therefore goes **core from the start** (revising the earlier lean toward JS-range): 4 of 5 target languages destructure, and putting patterns at L3 is what makes `AST_VAR_DECL`, `AST_PARAM`, `AST_FOR_ITER`, `AST_CATCH`, and `AST_MATCH_ARM` uniformly pattern-carrying. One shared **destructuring lowering engine** in the driver then serves all five sites — today JS carries its own and Lambda's `DECOMPOSE` is separate.

**L3 also owns the use/def facts on bindings** (user decision): beyond declaring names, the binding layer records how each binding is *used* — including **call-site argument facts**: for every call to a known function, the argument expressions' evidence is attached to the callee's parameter bindings. This is what makes call-site type propagation (§3.4) a *common* analysis rather than a JS-side pass: it is binding-level bookkeeping, language-independent by construction, with only the evidence *resolution* staying per-profile.

#### L4 — Function & closure core

| Core node | Fields | Lambda maps | JS maps | Py/Rb maps |
|---|---|---|---|---|
| `AST_FUNC` | `String* name; params (L3); body; NameScope* vars; FnAnalysis* analysis; clauses` *(tier 2 — decorators, Lambda method constraints)*`; FuncFlags flags` | `AST_NODE_FUNC`, `FUNC_EXPR`, `PROC` | `FUNCTION_DECLARATION`, `FUNCTION_EXPRESSION`, `ARROW_FUNCTION`, method values | def / lambda / do-blocks |
| `FuncFlags` | `is_proc (pn); is_arrow; is_async; is_generator; is_anonymous; is_public; is_variadic; can_raise; strict; has_use_strict` | fn/pn, pub, `^E` | arrow/async/generator/strict | async def, generator (yield-detected) |
| `AST_YIELD` | `argument; delegate` | — (Lambda `start`/streams lower differently per K-plan) | `YIELD_EXPRESSION` | yield / yield from |
| `AST_AWAIT` | `argument` | — | `AWAIT_EXPRESSION` | await |

The **fn/pn one-bit effect system is a core flag**, not a Lambda quirk: Lambda enforces it (safety analyzer as profile hook); JS/Python/Ruby builders mark everything `is_proc = true` (all-effectful), so shared passes can still exploit purity where a language declares it. This keeps the "color contracts, infer mechanics" doctrine intact on the shared spine.

Captures/closures: `FnAnalysis` (§3.3) carries the unified capture list; the shared **scope-env** representation (heap `Item*` slot array — JS's current model, adopted by Lambda) is the runtime form; the resumable-function transform (K17 layer 1) consumes `yield/await` counts from `FnAnalysis` regardless of language. Calling conventions and resumption drivers stay per-language (K17 layers 3–4).

#### L5 — Class & interface core

| Core node | Fields | Lambda maps | JS maps | Py/Rb maps |
|---|---|---|---|---|
| `AST_CLASS` | `String* name; bases (list — single for JS, multiple for Py); members; clauses` *(tier 2 — decorators, Lambda constraints)* | `AST_NODE_OBJECT_TYPE` (with methods/base) | `CLASS_DECLARATION`/`CLASS_EXPRESSION` | class |
| `AST_METHOD` | `key; AST_FUNC* value; MethodKind {method, ctor, getter, setter}; is_static; is_private; computed` *(var.)* | `TypeMethod` surface | `METHOD_DEFINITION` | def in class |
| `AST_FIELD` | `key; value; is_static; is_private; computed` | object-type fields | `FIELD_DEFINITION` | class attrs |
| `AST_INTERFACE` | `name; members; bases` — resolves to `TypeObject`/`Type*` (an L0 producer, like `AST_TYPE_REF`) | object-type sans implementation | TS `interface` | `Protocol` (advisory) |

Language-range L5 nodes: **JS** — `STATIC_BLOCK`, private-name specifics beyond the flags; **Python** — class-body statements (arbitrary code in class scope), metaclass clause; **Lambda** — `OBJECT_LITERAL` (typed literal construction), constraint clauses (tier-2 on `AST_CLASS`). Instantiation: `AST_NEW` is core (L1 table) — JS produces it, Lambda never does, Python/Ruby instantiate via plain `AST_CALL` with the profile dispatching class-vs-function callees.

Semantic depth here is deliberately *shallow in the AST*: prototype chains, MRO, method dispatch are LangProfile lowering concerns (§4.3) over the shared member structure — the AST records *what was declared*, not *how dispatch works*.

#### L6 — Module core

| Core node | Fields | Lambda maps | JS maps | Py/Rb maps |
|---|---|---|---|---|
| `AST_IMPORT` | `String* source; specifiers; default_alias; namespace_alias; flags {relative, cross_lang}` | `AST_NODE_IMPORT` | `IMPORT_DECLARATION`+`IMPORT_SPECIFIER` | import / require |
| `AST_EXPORT` | `declaration?; specifiers; source?; is_default` | `pub` modifier — builder normalizes `pub` decls to `AST_EXPORT{declaration}` | `EXPORT_DECLARATION`+`EXPORT_SPECIFIER` | `__all__` (advisory) / module fns |
| `AST_SCRIPT` | (shared with L2) — the module root, bound to `ModuleDescriptor` in the registry | | | |

Module *resolution* (paths, node_modules, package.json vs Lambda relative imports vs Python site rules) is per-profile; module *representation* (descriptor, namespace Item map, circular-import guard, live bindings §6.5) is shared.

### 2.4 Coverage — how much of each language lands in core

Mapping the two existing enums against the catalog:

- **JS (~57 node kinds):** everything maps to core except `WITH`, `STATIC_BLOCK`, `TAGGED_TEMPLATE`, `REGEX` — **~93% of kinds, >95% of occurrences** in ordinary code (labels became a core field and `new` a core node per the rev-3 confirmations).
- **Lambda (~75 node kinds):** expressions/statements/functions/modules map to core; Lambda-range retains its document-and-query surface — type-syntax nodes (~11, L0 surface), pipe/query/path/current-item family (~9), element/content (~3), string/symbol patterns (~5), views (~3), for-clause extensions (~4). **~60% of kinds; occurrence-weighted coverage in typical scripts is much higher** (literals/idents/binary/call/if/for dominate). Lambda is the *host* language and semantic superset — its unique surface is the product, not accidental divergence; the 80% target is for *guests*.
- **Python (projected):** comprehensions→`AST_FOR_EXPR` core clauses, f-strings→`AST_INTERP_STR`, tuple-unpack→L3 patterns, match→`AST_MATCH`, decorators→tier-2 clauses. Expected **≥85% of kinds**; residue: `global/nonlocal` (profile clause), `with` (context managers — maps to Lambda's R1–R5 scoped-resource model, likely a small core-promotable node later), slicing (an `AST_RANGE`-in-`AST_MEMBER` composition), metaclass edges.
- **Ruby (projected):** blocks/procs→`AST_FUNC` values on calls, `case/in`→`AST_MATCH`, `case/when`→`AST_SWITCH`, interpolation→`AST_INTERP_STR`. Expected **~85%**; residue: mixins/refinements, method_missing-class dynamism (profile).
- **Bash (projected):** the outlier — word expansion, redirection, pipelines are structurally alien. Expect **~50–60%** core (control flow, functions, variables) with a thick Bash range; it still gains the shared emitter/driver regardless.

### 2.5 Language on the compilation unit

`Script` gains the authoritative language descriptor: `const LangProfile* lang` — resolved from `ModuleDescriptor.source_lang` at load. Language is a property of the module, never of individual nodes; every pass receives the `Script*` and knows the grammar its `TSNode`s belong to. Mixed-language *programs* (Lambda importing JS) exist; mixed-language *modules* do not.

### 2.6 Operator representation

One superset `Operator` enum in core (merging Lambda's `Operator` and `JsOperator`): shared arithmetic/comparison/logical/bitwise/compound-assign ops, plus JS-only (`===/!==`, `>>>`, `in`, `instanceof`, `typeof`, `void`, `delete`, `??`, `??=/&&=/||=`) and Lambda-only (`to`, `is`, occurrence, pipe) members that other builders simply never produce. **Operator semantics are not encoded in the enum** — `OP_EQ` from a JS module lowers via the JS profile (coercing), from a Lambda module via the Lambda profile (structural). One shape, profile-dispatched meaning — the same trick `map_kind` already plays for the object model.

### 2.7 Header layout

New **`lambda/ast-core.hpp`**: base node, leveled core enum, core leaf structs (L1–L6 tables above), superset `Operator`, shared `NameScope`/`NameEntry`, `FnAnalysis`, clause-node base. `ast.hpp` and `js_ast.hpp` include it and keep only their language-range structs.

---

## 3. Shared Semantic Analysis

### 3.1 Pass architecture

Per-language **builders** stay (surface syntax and static-semantics quirks are absorbed here), but they emit the common AST and drive shared machinery:

```
source ──tree-sitter(lang grammar)──► CST
      ──build_<lang>_ast (per-language)──► common AST + scopes bound
      ──shared passes (parameterized by LangProfile):
            capture analysis
            evidence-based type inference
            const collection
      ──per-language passes (LangProfile hooks):
            early errors (JS), strict-mode propagation (JS), TDZ marking (JS),
            purity/fn-pn checking (Lambda), safety analyzer (Lambda)
      ──► analyzed AST
      ──shared MIR transpiler core + per-language lowering handlers──► MIR → JIT
```

### 3.2 Scope and binding — converge on build-time binding over shared structures

Today: Lambda binds authoritatively during the build pass (`NameScope`/`NameEntry`, `push_name`/`lookup_name`); JS has a coarse build-time `JsScope` plus the authoritative MIR-time analysis in side tables. `NameEntry` is already shared; `NameScope` and `JsScope` are both intrusive linked lists with parent pointers — near-identical.

**Design:** one scope structure (`NameScope`, extended with `ScopeKind {GLOBAL, MODULE, FUNCTION, BLOCK}` and `strict` — superset of both), with binding done at build time by each builder according to its language's rules:

- Lambda builder: binds as today.
- JS builder: implements hoisting at bind time — `var` declarations walk up past BLOCK scopes to the nearest FUNCTION/GLOBAL scope (exactly what `js_scope_define` does today); `let/const` bind in the block scope with a TDZ flag on the entry. Function declarations hoist per-scope. This *promotes* the authoritative rules from MIR-time to build-time, eliminating the two-tier split.
- Python/Ruby builders (later): declare-on-first-assignment synthesis per §L2.

Per-language *validation* passes (early errors, duplicate checks, strict rules) run on the bound tree via LangProfile hooks.

### 3.3 Function analysis side structure — `FnAnalysis`

Both compilers accumulate per-function facts outside the node. Generalize JS's `JsFuncCollected` and Lambda's `CaptureInfo` into one structure referenced from the unified `AST_FUNC`:

```c
struct FnAnalysis {
    // captures (shared shape; superset of CaptureInfo + JsCaptureEntry)
    CaptureEntry* captures;        // name, entry, is_mutable/is_const, env slot,
                                   // scope_env_key, grandparent_slot, force_env
    int capture_count;
    bool has_scope_env;            // shared closure environment (JS model,
                                   // adopted by Lambda closures)
    // inference results
    ParamEvidence params[MAX];     // §3.4 — unified evidence record
    Type* resolved_params[MAX];
    Type* return_type;
    // language-specific facts
    bool is_strict;                // JS
    bool can_raise;                // Lambda T^E
    int yield_count, await_count;  // resumable-function transform (K17 layer 1)
    void* lang_ext;                // profile-owned extension (e.g. JS ctor shape cache ptr)
};
```

This also gives K17's layer-1 "neutral resumable-function utility" its natural input: the shared transform reads `yield/await` facts from `FnAnalysis` regardless of source language.

### 3.4 Unified evidence-based inference

The two inference engines are the same idea with different vocabularies:

| | Lambda `InferCtx` (transpile-mir.cpp:11083) | JS `JsParamEvidence` (js_mir_context.hpp:82) |
|---|---|---|
| evidence bits | `INFER_INT/FLOAT/STOP/NUMERIC_USE/FLOAT_CONTEXT/ARITH_USE` | `int_evidence, float_evidence, string_evidence, used_as_container, compared_with_non_numeric, param_reassigned` |
| alias tracking | `find_aliases` | P6 alias folding |
| resolution | `STOP→ANY; INT&&!FLOAT→INT; FLOAT→FLOAT` | container/compare/reassign→ANY; float→FLOAT; **int→FLOAT** (JS numbers are binary64) |

**Design:** one evidence collector walking the common AST (arithmetic/bitwise/index/comparison/reassignment evidence, alias folding), one `ParamEvidence` record that is the superset, and a **per-language resolution policy** in the LangProfile:

```c
Type* (*resolve_param_evidence)(const ParamEvidence*);
// Lambda: int evidence w/o float context → LMD_TYPE_INT (unboxed int fast path)
// JS:     int evidence → LMD_TYPE_FLOAT (Number is float64; per N1–N9)
// Python (later): int evidence → int64/BigInt policy per its number model
```

This preserves each language's number model (a semantic property) while sharing the walk, the alias machinery, and the caching (`infer_cache`).

**Call-site propagation is common** (user decision — prior Lambda deferral lifted): `jm_callsite_propagate` generalizes into the shared analysis, anchored at the L3 binding layer (§L3 — caller-argument evidence attached to callee parameter bindings). Body evidence and call-site evidence merge into one `ParamEvidence` record; the profile's resolution policy runs last. Lambda thereby gains caller-aware native-param typing (params with no body evidence can still go native when all callers pass a consistent type); safe in this architecture because intra-module call sites are fully known at JIT time and cross-module calls cross through boxed wrappers.

Both engines' hard-won fixes carry over once, not twice: the pn-param float-div rule (INFER_FLOAT_CONTEXT on division), the "comparisons aren't int evidence" rule, the container/reassignment demotions.

---

## 4. Handling Semantic / Language Differences — the `LangProfile`

Semantic divergence is handled by **one policy object per language**, not by branching inside shared code and not by duplicating pipelines. Divergences fall into five classes:

### 4.1 Surface syntax → absorbed by builders
Different grammars, different CST shapes. Each language keeps its tree-sitter grammar and its `build_<lang>_ast.cpp`. Cost stays per-language; this is irreducible and fine.

### 4.2 Static semantics → builder rules + profile validation hooks
Hoisting, TDZ, strict mode, duplicate rules, fn/pn purity, reserved words. Implemented in the builders (binding, §3.2) and profile hook passes (validation). Examples: `js_check_early_errors` becomes the JS profile's `validate` hook; Lambda's safety analyzer its own.

### 4.3 Dynamic semantics → profile-dispatched lowering of core nodes

The heart of the design. The shared transpiler lowers core nodes by delegating **semantic micro-decisions** to the profile:

```c
struct LangProfile {
    const char* name;                       // "lambda", "js", ...
    // operator lowering: given op + operand static types, either emit inline MIR
    // (via shared emitter) or return the runtime helper symbol to call
    LowerAction (*lower_binary)(MirEmitter*, Operator, AstNode* l, AstNode* r);
    LowerAction (*lower_unary)(MirEmitter*, Operator, AstNode*);
    // coercions & tests
    void (*emit_truthy_test)(MirEmitter*, Reg val, Label if_true, Label if_false);
    Reg  (*emit_to_number)(MirEmitter*, Reg val);
    // member/index access semantics
    Reg  (*emit_member_get)(MirEmitter*, Reg obj, ...);   // shape walk vs prototype chain + IC
    void (*emit_member_set)(MirEmitter*, ...);
    // calls & errors
    Reg  (*emit_call)(MirEmitter*, ...);                   // arity/this/spread policy
    void (*emit_error_check)(MirEmitter*, Reg result);     // T^E propagation vs JS throw-completion
    // inference policy (§3.4)
    Type* (*resolve_param_evidence)(const ParamEvidence*);
    // validation, clause & extension-node lowering
    int  (*validate)(Script*, AstNode* root);
    Reg  (*lower_clause)(MirEmitter*, AstNode* clause, ...); // language clauses on core nodes
    Reg  (*lower_ext_node)(MirEmitter*, AstNode*);           // language-range nodes
};
```

Concrete divergences and where they land:

| Divergence | Resolution |
|---|---|
| `==` (JS coercing vs Lambda structural), `+` (JS string-concat overload), truthiness (`""`/`0` falsy in JS; Lambda's own rules per Formal_Semantics) | `lower_binary` / `emit_truthy_test` per profile |
| Number models (Lambda int/int64/float/decimal vs JS all-float64 + BigInt→decimal per N1–N9) | inference resolution policy + `emit_to_number`; the Item encoding already carries both |
| `null` vs `undefined` | already solved in the value model (`LMD_TYPE_UNDEFINED`); Lambda code simply never produces it |
| Object access: Lambda map field vs JS prototype chain + descriptors + ICs | `emit_member_get/set`; JS profile keeps its IC machinery (`JsLoadIC`/`JsStoreIC`), Lambda profile keeps direct shape access. `map_kind` already discriminates at runtime |
| Errors: Lambda `T^E`/raise vs JS throw/try vs Py/Rb exceptions | All lower to **error-values in MIR** (J3 already forces this at ABI level; JS's completion machinery is already value-based internally). Profile `emit_error_check` decides propagation style: Lambda `?`-style early-return vs try-context dispatch. Cross-language: an error crossing modules is an error Item — no unwinding, per J3 |
| Mutability: Lambda immutable-by-default vs JS/Py/Rb mutable | DeclKind/fn-pn flags + profile; assignment lowering consults the profile |
| `this`, prototype, MRO, classes | L5 records declarations; dispatch semantics live in profile `emit_member_get`/`emit_call`; JS-range/Py-range nodes for the genuinely unique parts |
| Iteration protocols (JS Symbol.iterator vs Lambda ranges/collections vs Py `__iter__`) | `AST_FOR_ITER` core node, profile-dispatched iterator emission (`jm_emit_get_iterator` generalizes) |

### 4.4 Object model → already solved
`map_kind` (MapKind = ECMAScript exotics, engine-owned; VMap = host objects, module-owned — per the native-module decision log) already unifies the object model at the data layer. Nothing new needed.

### 4.5 What must NOT unify (K17 boundaries respected)
- **Calling conventions** (K17 layer 4): JS always-Promise async vs Lambda `value|suspended` zero-alloc fast path — stays per-language, as decided.
- **Resumption protocol drivers** (layer 3) — thin per-language.
- JS async stays on its state machines (K10); the shared *transform utility* (layer 1) and *resume-frame layout* (layer 2) become shared services of the new transpiler core, per K17's plan.

---

## 5. Shared MIR Transpiler

### 5.1 Layering

```
┌────────────────────────────────────────────────────────┐
│ per-language lowering handlers (ext nodes, clauses,    │
│ semantic ops)                                          │  ← Lambda / JS / Py …
├────────────────────────────────────────────────────────┤
│ shared lowering driver: dispatch on node_type,         │
│ core control flow (if/while/for/switch/match), calls,  │
│ closures & scope-env, destructuring engine (L3),       │
│ resumable-function transform (K17 L1),                 │
│ TCO, const materialization, error-value plumbing       │
├────────────────────────────────────────────────────────┤
│ MirEmitter: registers, labels, insns, call emission,   │
│ boxing/unboxing primitives, import cache,              │
│ GC root slots, BSS (consts/type_list) plumbing         │
├────────────────────────────────────────────────────────┤
│ MIR / JIT (unchanged)                                  │
└────────────────────────────────────────────────────────┘
```

### 5.2 `MirEmitter` (bottom layer — extract first)

Already identified in Clean_Up §6.4: `new_reg/emit_insn/emit_label/emit_call_N/ensure_import` are line-for-line duplicated between `transpile-mir.cpp:434–490` and `js_mir_internal.hpp:96–204`. Extract into one struct holding `MIR_context_t, current_func, reg counter, import cache, rt_reg/gc_reg, root-slot allocator, consts/type-list BSS regs`. Both existing transpilers adopt it **before** any AST change — it's independently a cleanup win (~300–500 LOC) and creates the single home for:

- **GC rooting (G1).** The honest-local-typing fix from `vibe/Lambda_GC_Root_Issue.md` must be implemented *in the emitter*, once, as part of this extraction. Rooting is currently the highest-priority foundation gap and re-plumbing register allocation without fixing it would entrench the blanket-`MIR_T_I64` hack in shared code. **Prerequisite, not afterthought.**
- **Double-boxing v2.** The inline-doubles proposal (`vibe/Lambda_Type_Double_Boxing.md`) changes boxing sequences; with one emitter, it lands in one place for both languages instead of two.

### 5.3 Unified variable model

Merge `MirVarEntry` and `JsMirVarEntry` (near-identical: `{MIR_reg_t reg; MIR_type_t mir_type; TypeId type_id; env slot/offset; flags}`) into one `VarEntry` owned by the emitter layer, with the superset of flags (`tdz_active/is_let_const/is_const` from JS; `is_state_var/num_type/elem_type` from Lambda). The `var_scopes[64]` hashmap-stack machinery is identical in both — one implementation.

Closure environments: adopt the JS **shared scope-env** model (`Item*` array, slots, `scope_env_key` identity, grandparent links) as the common representation — it is the more general mechanism (Lambda's per-closure `CaptureInfo` env is a subset). Lambda closures move onto it; behavior is observationally identical, and it prepares Lambda for K17 layer-2 resume frames (also heap slot arrays).

### 5.4 Driver + handlers

The shared driver owns the `switch(node_type)` over core nodes, delegating semantic decisions to the profile (§4.3), clause chains to `lower_clause`, and language-range nodes to `lower_ext_node`. The existing 10-file `js_mir_*` body becomes: JS profile handlers (classes' dispatch semantics, generators driver, eval, with, regex, ICs) + shared driver contributions (statements, expressions, destructuring, iterators, completion). Lambda's `transpile-mir.cpp` becomes: Lambda profile handlers (query/for-clause extensions, elements, pipes, patterns, views) + the same shared driver.

**Native specialization** (JS's boxed+native dual emission, `has_native_version`) generalizes: it's the same mechanism as Lambda's unboxed-param path; the shared driver emits native variants when the profile's inference policy typed the params natively.

### 5.5 C2MIR path (decision needed — U11)

`transpile.cpp` (C-text backend) is Lambda-only, Jube-build-only, kept for debugging/regression. Options:
- **(a) Freeze:** it keeps consuming Lambda AST nodes; the unified enum keeps Lambda node values stable (or the renumber is mechanically applied to its switches once). It never learns core-node semantics for other languages. Low cost, keeps the debug tool.
- **(b) Retire** after the unified MIR path is proven, per the J1 spirit ("one spec level").

**Recommendation: (a) freeze now, revisit retirement after Phase 4** — it's load-bearing for transpiler-diff debugging during exactly this migration.

---

## 6. Runtime / Substrate Unification

### 6.1 Name pool — done
Already one `NamePool` shared across Lambda, JS, and runtime. No work. (Guests must adopt it when they port — audit py/rb/bash for private interning then.)

### 6.2 Shape pool + JS hidden classes — one shape system

Today: Lambda's `ShapePool` interns whole shape chains by signature (find-or-create, arena-backed); JS builds shapes via `TypeMap` transition tables + per-ctor/class shape caches + ICs keyed on shape pointers. Both operate on the *same* `TypeMap`/`ShapeEntry` structures.

**Design:** ShapePool becomes the single interning/backing store; JS transitions become an *index* over pooled shapes:
- `shape_pool_get_*` (whole-signature) stays for literal maps/elements (Lambda + JS object literals with known shape).
- Add `shape_pool_transition(parent_shape, name, type) → TypeMap*` — find-or-create the successor shape, memoized in the parent's transition table. JS's incremental property-addition path routes through it, so structurally-identical objects built in different orders/modules converge on pooled shapes.
- ICs stay JS-profile-owned but now key on pooled (therefore more shareable) shape pointers — strictly better hit rates.
- One ref-count/lifecycle discipline (the `RefCountedPool` extraction from Clean_Up §1.7 folds in here).

### 6.3 Const pool — adopt the Lambda model, fold JS in

Today: Lambda has `const_list` (+ `const_index` on `TypeConst`, `consts_bss` per module, `rt->consts` at runtime, cross-module const access via the callee's BSS); JS bakes interned strings/numbers as immediate Items and keeps `module_consts` (module-level bindings) separately.

**Design:** one per-module const pool in the emitter layer:
- Immediate-encodable constants (ints, most doubles via self-tagging, interned strings as `s2it`) stay baked inline — both languages already do this; it's faster than pool loads.
- Pool residency for what needs it: decimals/BigInt (`mpd`), datetimes, regex-compiled objects (JS `JsRegexNode` compiles once → pool slot instead of per-eval), template-strings' cooked arrays, pattern objects (`TypePattern` re2). One `add_const/load_const` API on `MirEmitter`, one `consts_bss` discipline for both.
- `module_consts` (JS module-level *variables*) is a different thing (module state, not constants) — it merges with Lambda's `global_vars` BSS model in the unified variable layer (§5.3).

### 6.4 Variable lookup — compile-time only, unified
Covered by §5.3: no runtime name lookup exists in either language (registers/env slots/BSS resolved at compile time) — this stays; the unification is of the compile-time structures. The one runtime name-ish path — JS `eval`/`with` env frames — remains a JS-profile feature on its existing machinery.

### 6.5 Module registry — from namespace-map seam to AST-level import binding

Today the registry is already the cross-language seam, but Lambda→JS imports go through `create_js_import_script` (module_registry.cpp:202), which **synthesizes fake Lambda AST nodes** (with fabricated byte offsets ≥1,000,000) from a JS namespace map's shape — a bridge that exists only because the two ASTs were foreign.

**Design:** with a common AST, imports bind directly:
- The importer's builder resolves a cross-language import to the imported module's *actual* `AstScript` (common AST) and binds `pub`/`export` entries via the same `declare_module_import` path used intra-language. `create_js_import_script` and its synthetic nodes are **retired**.
- `ModuleDescriptor` gains `AstScript* ast` (compile-time; may be dropped post-JIT) alongside `namespace_obj` (runtime).
- **Live bindings** (the documented gap at module_registry.cpp:184 — `pub` vars skipped): with unified BSS/global-var handling (§6.3), an exported var binding can reference the owning module's BSS slot directly — implement once, works for Lambda `pub` vars and ES-module live bindings alike.
- Export naming (`module_to_mir_name` etc.) and circular-import handling (`loading` flag) stay as-is.
- Cross-language *calls* keep the J2 contract: the callee is invoked through its Item `Function` / C-ABI surface; the common AST improves *binding and arity/type visibility* at compile time (the importer can see the callee's `TypeFunc`), enabling checked cross-language calls and future inlining — without creating a new ABI.

---

## 7. Foundation for Python / Ruby / Bash

Current state (measured): each guest is a full parallel stack — own AST, own scope, own lowering, own runtime library:

| Guest | AST builder | MIR lowering | Runtime |
|---|---|---|---|
| Python | build_py_ast.cpp 103 KB | transpile_py_mir.cpp **349 KB** | ~200 KB |
| Ruby | build_rb_ast.cpp 87 KB | transpile_rb_mir.cpp **213 KB** | ~100 KB |
| Bash | build_bash_ast.cpp 191 KB | transpile_bash_mir.cpp **296 KB** | ~350 KB |

Under this design, a guest becomes: **grammar + builder (kept, per-language) + LangProfile (new, small) + runtime library (kept)** — the ~860 KB of parallel *lowering* code is the collapse target, since the shared driver + emitter absorb control flow, calls, closures, variables, boxing, consts, GC rooting. The §2.4 coverage projections (~85% core for Py/Rb) quantify the claim.

Guest-specific notes:
- **Python:** comprehensions are *exactly* core `AST_FOR_EXPR` clauses; f-strings→`AST_INTERP_STR`; tuple-unpack→L3 patterns; its number model gets its own evidence-resolution policy (int→int64/BigInt); reference semantics + in-place mutation get **documented projection rules in the profile** — turning G7 (reference-vs-value impedance, "currently emergent transpiler behavior") into explicit policy code; `with` maps toward the R1–R5 scoped-resource model.
- **Ruby:** blocks/procs map onto the shared closure/scope-env model; method dispatch is profile `emit_member_get`/`emit_call`; `case/in`→`AST_MATCH`, `case/when`→`AST_SWITCH`.
- **Bash:** most idiosyncratic (word expansion, redirection); expect a thicker profile and more language-range nodes; port last, or accept it staying on its own lowering indefinitely (it shares the emitter regardless).
- **TS:** already on the JS AST; it renumbers into its range and otherwise rides along unchanged.

Porting order: **Python first** (most complete, already wired into Lambda's importer via `load_py_module`), then Ruby, then decide on Bash. Each port is gated on that guest's existing test corpus.

---

## 8. Migration Plan

Method: K17's extract-after-convergence — never a big-bang rewrite; every phase keeps both pipelines green. Gates throughout: `make test-lambda-baseline` (100%), lambda gtest suite, editor Phase-A 1931 JS tests, UI-automation 5714, node-baseline (1492/3517, no regression), test262-linked early-error behavior.

**Phase 0 — substrate (no AST change; independently valuable)**
1. Extract `MirEmitter` from `transpile-mir.cpp` + `js_mir_internal.hpp`; both transpilers adopt it. (= Clean_Up §6.4)
2. Land the **G1 honest-local-typing GC-rooting fix inside the emitter** (from `vibe/Lambda_GC_Root_Issue.md`). Prerequisite for everything after.
3. Unify `VarEntry` + `var_scopes` machinery on the emitter layer.
4. Unify const-pool API on the emitter (`add_const/load_const`, per-module BSS).

**Phase 1 — AST base formalization (mechanical)**
5. Create `ast-core.hpp`: base node, leveled core enum (§2.2), superset `Operator`, shared `NameScope`/`NameEntry` extensions, clause-node base.
6. Renumber JS/TS node kinds into their ranges (mechanical switch updates; AST-dump equivalence tests before/after).
7. `Script.lang` → `LangProfile*`; profiles exist but are pass-throughs to current code.

**Phase 2 — leaf-struct unification, level by level**
8. Merge onto core structs following the level order: **L1 expressions first** (literal/ident → unary/binary → call/member → if/seq), then **L2 statements + L3 declarators/patterns together** (the variable story lands as one piece), then L4 `AST_FUNC`+`FnAnalysis` (initially wrapping each side's existing data), then L5/L6. After each level, both suites green.

**Phase 3 — shared analysis**
9. One capture analysis producing `FnAnalysis.captures`; Lambda closures move onto the shared scope-env representation.
10. One evidence-based inference walk + per-profile resolution (§3.4); port both sides' inference fixes into it; delete `InferCtx` and `JsParamEvidence`. Call-site propagation ships as part of this step for both languages (U17 — Lambda's prior deferral lifted), gated on the lambda baseline + AWFY benchmarks.
11. JS binding promoted to build time on shared scopes (§3.2); `JsFuncCollected`'s scope facts migrate into `FnAnalysis`; early errors become the JS profile validate hook.

**Phase 4 — shared lowering driver**
12. Stand up the driver on *core* nodes only, behind a per-module flag; Lambda first (simpler semantics), JS module-by-module (start with the node-baseline corpus).
13. Move statements → expressions → destructuring engine (L3) → iterators into the driver; per-language handlers shrink correspondingly.
14. Resumable-function transform extracted as the shared utility (K17 layer 1), consuming `FnAnalysis` — Lambda's `start`/concurrency Stage A work and JS generators/async converge on it (respecting layer-3/4 boundaries).

**Phase 5 — module system**
15. AST-level cross-language import binding; retire `create_js_import_script`; live bindings for exported vars.

**Phase 6 — first guest port (Python)**
16. Python builder retargets to common AST + PyProfile; delete `transpile_py_mir.cpp` incrementally. This is the proof that the design generalizes — treat it as the acceptance test for the whole effort, and the empirical check on the ≥80%-core target (§2.4).

Risk register:
- **G1 rooting** — addressed at Phase 0 by design; do not defer.
- **JS perf cliffs** (ICs, native specialization, shape caches): Phase 4 must carry them into the profile intact; benchmark AWFY + LambdaJS perf suite per step.
- **Double-boxing v2 interaction**: if S0–S3 of that plan proceeds concurrently, sequence it *after* Phase 0 so it lands in the shared emitter once.
- **Enum renumber blast radius** (Phase 1): purely mechanical but wide; one commit per language with AST-dump equivalence tests.
- **`transpile.cpp` (C2MIR)**: frozen per U11; only the mechanical enum update touches it.

---

## 9. Decision Ledger (proposed — awaiting confirmation)

| # | Decision | Status |
|---|---|---|
| **U1** | Common AST defined as a **leveled core-node catalog** — L0 type (in place, the model), L1 expr, L2 statement/variable, L3 binding/pattern, L4 fn/pn+closure, L5 class/interface, L6 module — with the concrete core node lists of §2.3; target ≥80% of any guest language in core nodes; language-unique nodes kept minimal | proposed |
| **U2** | Language is a property of the compilation unit (`Script.lang → LangProfile*`), never of individual nodes; base node stays `{node_type, Type*, next, TSNode}` | proposed |
| **U3** | Per-language variance in exactly three tiers, in order of preference: (1) fields/flags on shared structs, (2) clause chains on clause-bearing nodes, (3) language-range node kinds as last resort. Promotion to core allowed, demotion never | proposed |
| **U4** | One superset `Operator` enum; operator *semantics* dispatch through the LangProfile | proposed |
| **U5** | Scope/binding converges on build-time authoritative binding over shared `NameScope`/`NameEntry` (JS hoisting/TDZ at bind time; Py/Rb declare-on-first-assign synthesis; validation via profile hooks) | proposed |
| **U6** | Per-function analysis unifies in `FnAnalysis` (captures on the JS shared scope-env model, unified evidence records); evidence-based inference shared with per-language resolution policies | proposed |
| **U7** | Shared MIR transpiler = MirEmitter (bottom) + shared driver (core nodes, incl. one destructuring engine) + LangProfile handlers (semantics, clauses, ext nodes); K17 layer boundaries respected (calling conventions and resumption drivers stay per-language) | proposed |
| **U8** | Substrate: ShapePool becomes the single shape store with a new transition API absorbing JS hidden classes; const pool adopts the Lambda `const_list`/BSS model; `VarEntry`/scope stacks unified in the emitter | proposed |
| **U9** | Module registry: AST-level import binding; `create_js_import_script` synthetic bridge retired; live bindings implemented once for Lambda `pub` vars + ES live bindings; J2's Item/C-ABI call contract unchanged | proposed |
| **U10** | J-ledger amendment **J2-R**: "each front-end transpiles itself" → "each front-end *builds* itself (grammar + builder + profile); in-tree front-ends share AST, analysis, and lowering infrastructure." J1/J3/J5/J6 unchanged; common AST is in-memory only, never a distribution format | proposed |
| **U11** | C2MIR/`transpile.cpp` frozen as Lambda-only debug backend during migration; retirement decision deferred to post-Phase-4 | proposed |
| **U12** | Migration follows K17 extract-after-convergence with per-phase green gates; **G1 GC-rooting fix is a Phase-0 prerequisite baked into MirEmitter**; Python is the first guest port and the design's acceptance test | proposed |
| **U13** | **L3 = binding & pattern layer** fills the level gap: declarators/params/destructuring patterns are core from the start (4 of 5 languages destructure), giving `VAR_DECL`/`PARAM`/`FOR_ITER`/`CATCH`/`MATCH_ARM` one pattern vocabulary and one shared destructuring lowering engine | **confirmed** |
| **U14** | fn/pn purity is a **core flag** on `AST_FUNC` (guest builders mark all-effectful); the one-bit effect system rides the shared spine rather than staying a Lambda-range concept | proposed |
| **U15** | **Labels are core, as a field not a node**: `label?` on the loop family/`AST_BLOCK`/`AST_SWITCH` + on `break`/`continue`; no `LABELED_STATEMENT` wrapper; exotic labeled statements normalize to a labeled block. Lambda doesn't produce labels today but the field is core | **confirmed** |
| **U16** | Promoted/confirmed core on cross-language commonality: `AST_MAP` (+`AST_MAP_ENTRY` — Lambda map / JS object / Py dict / Rb hash), `AST_NEW` (JS produces, Lambda never, Py/Rb use `AST_CALL`), `AST_INTERP_STR` (Lambda to adopt interpolation in some manner later), `AST_SEQ` unifying Lambda list / Python tuple / JS comma-expression via `SeqKind` + profile lowering | **confirmed** |
| **U17** | **Call-site type propagation is common analysis, anchored at L3**: caller-argument evidence is binding-layer bookkeeping attached to callee parameter bindings; merges with body evidence into one `ParamEvidence`; profile resolves last. Lambda adopts it (prior deferral lifted), gated on lambda baseline + AWFY | **confirmed** |
| **U18** | **CodeQL adopted as prior art** (§11): its guest-onto-host precedents (TS on JS libraries, Kotlin on the Java schema, C on C++), shared parameterized analysis libraries (dataflow/SSA/CFG with per-language input signatures), and flags-over-subclasses modeling corroborate U1/U3/U7 and the K17 method; its QL class taxonomy serves as a completeness checklist for the core catalog; "interfaces/views over language-range nodes" noted as a possible fourth variance tier (optional, not committed) | **confirmed** |

## 10. Open Questions

Resolved by user confirmation: L3 as its own level (U13); labels core-as-field (U15); `AST_MAP`/`AST_NEW`/`AST_INTERP_STR`/`AST_SEQ` core (U16); call-site propagation common at L3 (U17); CodeQL as prior art (U18).

Still open — elaborated trade space and recommendations (pending decision):

### Q1 — Surface preservation vs desugaring

When one construct reduces to another (`for(;;)`→while, `do-while`→while+flag, `switch`→if-chains, `x+=e`→`x=x+e`, `unless`→negated if): keep both surface forms as core nodes, or normalize to a minimal kernel?

The kernel's promise (fewer cases per pass, optimizations written once) is weakened by three facts:
1. **Desugaring is where semantic bugs live, and it is not language-independent.** `a[f()] += 1` must evaluate `a[f()]` once; correct expansion needs temporaries whose interaction with each language's evaluation order/TDZ/coercion differs — every desugar rule needs a per-language soundness argument anyway, forfeiting the sharing. A rewrite sound for Lambda can be unsound for JS.
2. **Codegen prefers original shapes:** `switch`→jump table; `do-while` without synthetic flags; TCO/loop-idiom detection pattern-matches surface forms.
3. **Debugging/goldens:** `emit_sexpr` dumps, golden tests, and errors map 1:1 to source — the Phase-1/2 AST-dump-equivalence safety net depends on this.

Cost of preservation is bounded (~6–8 nodes saved by a kernel out of ~45); the per-pass case count is solved by category predicates (`is_loop()`) — CodeQL's abstract `LoopStmt` move.

**Recommendation:** preserve surface as default. Two sanctioned exceptions: (a) builder-level *mapping* when a guest construct is literally an instance of a core node (Python comprehension → `AST_FOR_EXPR` clauses); (b) driver-internal canonicalization (the driver may reuse its while-machinery for `for(;;)` without rewriting the tree). Any future desugar requires: provably identical semantics under every producing profile + measurable simplification of a shared pass.

### Q2 — `FnAnalysis.lang_ext`: opaque pointer vs tagged union

Where do profile-private per-function facts live (JS ctor shape-cache ptr, class links, eval/`arguments` presence; Lambda view/state context, method-owner links)?

- **Opaque `void*`:** full decoupling — core header never sees profile structs; guest ports touch nothing shared. Cost: casts, no generic dump, no checking.
- **Tagged union:** checked and dumpable, but inverts the dependency direction (core header includes every front-end's struct; each guest port edits it and rebuilds the world) and is sized to the largest member — decisive, because the JS ext is "what remains of `JsFuncCollected` after shared fields move to `FnAnalysis`," which is large (today: `func_entries[32768]` of a fat struct). A union makes every Lambda function carry JS-sized dead weight.

**Recommendation:** opaque pointer + discipline: a 2-byte `ext_kind` (lang id) beside the pointer, inline typed accessors asserting it in debug builds (`js_fn_ext(fa)` checks `ext_kind==LANG_JS`), a `dump_fn_ext` hook on `LangProfile` for complete AST dumps, allocation from the AST pool (lifetime moot). Side benefit: shared passes *cannot* peek at profile facts — the core/profile boundary stays honest.

### Q3 — Sequencing vs concurrency Stage A

Both concurrency (Stage A fork-join, Stage B M:N tasks) and Phase 4 here touch `transpile-mir.cpp` and claim the resumable-function transform (K17 layer 1). Three observations reshape either/or into an interleave:
1. **Phase 0 is needed by both, urgently:** Stage A runs JIT'd frames concurrently — building that on the blanket-`MIR_T_I64` rooting hack (G1) would stack a headline feature on the known foundation crack. Emitter + honest rooting land before either track. Stage A written against the emitter API survives the later Phase-4 migration, defusing the rework worry.
2. **Stage A likely doesn't need the transform** (fork-join can block; Stage B's M:N state machines need function-splitting). Remaining contention is file-level churn only, and Phases 1–3 live mostly outside the lowering file.
3. **K17's two-client rule exists for a reason:** extracting the "shared" transform with JS async as the only prior client (layers 3–4 forbid touching JS's drivers) risks a JS-shaped utility Lambda then fights. A Lambda transform built first (for Stage B) gives extraction two green clients.

**Recommendation — interleave:** Phase 0 (emitter + G1) → in parallel: concurrency Stage A ∥ unified-AST Phases 1–3 → Phase 4 extraction with Lambda's Stage-B transform as second client → Stage B ships on the shared transform. Honors K17, ships concurrency early, makes the shared prerequisite explicit.

### Q4 — The "views" fourth variance tier (minor)

From CodeQL (§11.3): let language-range nodes implement core *interfaces* (Lambda `PIPE` exposing a call-like view) so shared passes handle them without promotion. **Recommendation:** defer — adopt only when a real shared pass asks for it.

---

## 11. Prior Art — CodeQL

CodeQL is the closest large-scale precedent for "many languages, shared program representation and analysis" (user-directed reference). Its architecture: a per-language **extractor** parses source into a relational database over a per-language **schema** (dbscheme); per-language **QL standard libraries** define AST class hierarchies over the database; **queries and shared analysis libraries** run on top. Newer extractors (Ruby, QL-for-QL) are tree-sitter-based — the same parsing substrate we use.

### 11.1 Level-by-level mapping

| Our level | CodeQL analogue |
|---|---|
| L0 Type | per-language `Type` hierarchies (Java `Type`/`RefType`, C++ `Type`, TS type entities) — like us, types are a first-class layer the AST references |
| L1 Expr | the `Expr` class hierarchy: `Literal`, `BinaryExpr`, `UnaryExpr`, `Call`/`MethodAccess`, `FieldAccess`/`PropAccess`/`Subscript`, `ConditionalExpr`, template/f-string classes, Python comprehension classes |
| L2 Stmt | the `Stmt` hierarchy: `IfStmt`, `WhileStmt`/`ForStmt`/`ForEachStmt` (+ an abstract `LoopStmt` over them), `BreakStmt`/`ContinueStmt`, `ReturnStmt`, `ThrowStmt`, `TryStmt`/`CatchClause`, `SwitchStmt` |
| L3 Binding | `Variable`, `Parameter`, JS `BindingPattern` (ident/array/object destructuring as one pattern family — direct precedent for our L3); the shared **SSA library** is built over bindings, as our use/def + call-site facts are |
| L4 Fn | `Function`/`Callable`/`Method`; notably JS `Function` models generator/async as **boolean predicates on one class**, not separate node kinds — precedent for our `FuncFlags` tier-1 variance |
| L5 Class | `Class`/`ClassOrInterface`, member declarations, `Method`/`Field` |
| L6 Module | `Module`, `ImportDeclaration`/specifiers, `ExportDeclaration` |

### 11.2 Precedents that corroborate this design

- **Guest-onto-host AST:** TypeScript is analyzed with the JavaScript libraries (one AST family, two languages — exactly our TS-on-JS starting point); **Kotlin support was implemented on the Java schema/libraries**, with the Kotlin extractor lowering Kotlin constructs into Java-shaped ones — precisely our "builder normalizes into the shared AST" move; C and C++ share one schema. Multiple independent proofs that related languages can productively share one program representation, with impedance absorbed at extraction/build time.
- **Shared analysis parameterized per language:** CodeQL's dataflow, SSA, and control-flow-graph libraries are language-agnostic modules instantiated per language via input signatures — structurally our LangProfile + shared-passes design. Their history also matches K17's doctrine: the shared dataflow library was **extracted after** years of mature per-language implementations, not designed up front.
- **Flags over subclasses:** where CodeQL models variance as predicates/properties on one class (generator/async functions), it validates our tier-1 field/flag mechanism; where it subclasses, the subclass is usually structural — matching our core-if-structure-identical rule.

### 11.3 Ideas to borrow (and one contrast)

- **Taxonomy as completeness checklist:** CodeQL has exhaustively enumerated the AST surface of 10+ languages. When finalizing the core catalog per level (and again before each guest port), walk the corresponding QL class list (`Expr`/`Stmt`/`BindingPattern`/…) for that language as a coverage audit — cheap insurance against forgotten constructs.
- **Interfaces/views over concrete nodes:** QL leans on abstract classes (`LoopStmt`, `Callable`) and cross-cutting "concepts" that concrete nodes *implement*. For us this suggests an optional **fourth variance tier**: a language-range node implementing a core view (Lambda `PIPE` exposing call-like structure) so shared passes can process it without promoting it to core. Recorded as open question 4 — adopt only when a real pass needs it.
- **Contrast — why we go further:** CodeQL never unified its schemas across unrelated ecosystems; per-language fidelity and independent evolution mattered more for an analysis product covering code it doesn't control. We own every front-end (J6, curated in-tree) and need one *executable* lowering to MIR — which CodeQL, being analysis-only, has no analogue of. That is exactly why deeper unification is justified here, and why the LangProfile *lowering* surface (§4.3) is the novel part of this design with no CodeQL counterpart.
