# Unified AST & Compiler Design — Lambda + LambdaJS (and beyond)

**Date:** 2026-07-11
**Status:** Proposal — decision ledger U1–U12 in §9 awaits confirmation
**Related:** `vibe/Lambda_Semantics_Features.md` Part 1 (J1–J6, G1–G8), `vibe/Lambda_Design_Concurrency.md` (K17), `vibe/Lambda_Desing_Native_Module.md`, `vibe/Lambda_Code_Clean_Up.md` §6, `vibe/Lambda_GC_Root_Issue.md`

---

## 1. Goal and Scope

Unify the Lambda-script and LambdaJS compilers onto a **common AST**:

1. one AST tree that both `build_ast.cpp` (Lambda) and `build_js_ast.cpp` (JS) produce;
2. shared semantic analysis on that tree — symbol binding, capture analysis, evidence-based type inference;
3. one shared AST→MIR transpiler core, with per-language semantic handlers;
4. deeper unification of the compile-time/runtime substrate: name pool, shape pool, const pool, variable lookup, module registry;
5. a foundation such that Python, Ruby, Bash (and future front-ends) become *builders + language profiles* on the same spine instead of parallel ~300 KB transpilers.

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

## 2. The Common AST

### 2.1 Base node — keep, formalize

The existing base contract is correct and stays:

```c
struct AstNode {
    AstNodeType node_type;   // unified kind space, §2.2
    Type*       type;        // Lambda Type* (shared type model)
    AstNode*    next;        // intrusive sibling list
    TSNode      node;        // CST node of the *module's* grammar
};
```

- `TSNode` stays as the source-location carrier. It is grammar-specific, but **language is a property of the compilation unit, not the node** (§2.4), so consumers always know which grammar a module's TSNodes belong to. No per-node lang tag, no fattening.
- `Type*` stays the single type carrier on nodes. Deeper analysis hangs off the function-level side structure (§3.3), not the node.
- Allocation stays pool-based (`alloc_ast_node` pattern), whole-pool lifetime.

The header split: extract the base node + core kinds + shared leaf structs into a new **`lambda/ast-core.hpp`**, included by both `ast.hpp` and `js_ast.hpp`. Lambda- and JS-specific leaf structs remain in their own headers.

### 2.2 One node-kind space: core + language ranges (generalize the TS precedent)

A single `AstNodeType` integer space, partitioned:

| Range | Owner | Contents |
|---|---|---|
| 0–499 | **Core** | Constructs with shared *shape* across languages: literal/primary, identifier, binary, unary, assignment, call, member, index, array, map/object, list/sequence, if, while, do-while, for (C-style), for-in/of/loop, block, return, break/continue, function, param, spread, conditional, template pieces, import/export/module, try/throw-shaped error nodes, match/switch |
| 500–999 | **Lambda** | Lambda-only constructs: pipe, current-item `~`, query-expr, for-clauses (group/order/join), element/content, path-expr, string/symbol patterns, view/state/event-handler, type-stam & type nodes, raise/`T^E` nodes |
| 1000–1499 | **JS** | JS-only constructs: with, labeled statement, tagged template, regex literal, class/method/field/static-block, getter/setter property, yield/await, new-expression, optional chaining forms, destructuring patterns (until/unless promoted to core) |
| 1500–1999 | **TS** | existing TS annotation nodes (renumbered from 1000 → 1500 block) |
| 2000+ | future guests | Python (2000), Ruby (2500), Bash (3000) reserved |

Rules:

- **A node kind goes in core only if its *structure* (child slots) is language-independent.** Semantics may differ (JS `==` vs Lambda `==`) — that is the lowering profile's problem (§4), not the node's. Structure-identical + semantics-divergent ⇒ core node + profile-dispatched lowering.
- **A node kind stays in a language range if its structure is language-specific** (Lambda's group-by clause, JS's class body). Language-range nodes are handled by that language's lowering handler; the shared driver just dispatches.
- Promotion is allowed later (e.g., destructuring patterns start JS-range; when Python arrives with near-identical structure, promote to core). Demotion is not — ranges only widen.
- The `switch` dispatchers in both transpilers already dispatch on `node_type` integers, so range partitioning costs nothing at runtime.

### 2.3 Leaf structs — unify where shape-identical

Merge the struct pairs that are already isomorphic into shared definitions in `ast-core.hpp`:

| Shared struct | Replaces | Notes |
|---|---|---|
| `AstIdentNode {String* name; NameEntry* entry}` | `AstIdentNode` ≡ `JsIdentifierNode` | identical today |
| `AstBinaryNode {op; left; right}` | `AstBinaryNode` ≡ `JsBinaryNode` | operator enum: §2.5 |
| `AstUnaryNode {op; operand; prefix}` | both | Lambda gains the (unused-for-Lambda) `prefix` bit |
| `AstCallNode {callee; args; flags}` | `AstCallNode` ≅ `JsCallNode` | flags union: `pipe_inject/propagate/can_raise` (Lambda) + `optional` (JS) |
| `AstFieldNode {object; property; computed; optional}` | `AstFieldNode` ≅ `JsMemberNode` | Lambda member/index unify onto `computed` |
| `AstIfNode`, `AstWhileNode`, `AstReturnNode`, `AstBlockNode`, `AstConditional`, `AstArrayNode`, `AstProgram/Script` | near-identical pairs | mechanical |
| `AstFuncNode` | `AstFuncNode` ≅ `JsFunctionNode` | unified: `{name; params; body; NameScope* vars; FnAnalysis* analysis; FuncFlags flags}` where flags = `{is_proc, is_arrow, is_async, is_generator, is_anonymous, is_public, strict}` — the fn/pn distinction and JS function kinds are the same field |
| `AstLiteralNode` | `AstPrimaryNode` ≅ `JsLiteralNode` | carries value union + `has_decimal/is_bigint` hints; Lambda literals keep pointing at `LIT_*` type singletons via `type` |

Language-specific structs (`AstForNode` with group/order/join clauses, `JsClassNode`, `AstViewNode`, patterns, …) stay in their language headers, extending the shared base.

### 2.4 Language on the compilation unit

`Script` (which both pipelines already flow through via `module_registry`) gains the authoritative language descriptor:

```c
struct Script {
    ...
    const LangProfile* lang;   // §4 — replaces string-typed source_lang as the working handle
};
```

`ModuleDescriptor.source_lang` remains the registry's string tag; the `LangProfile*` is resolved from it at load. Every analysis and lowering pass receives the `Script*` and therefore knows the language without per-node tagging. Mixed-language *programs* exist (Lambda importing JS), but each *module* is single-language — this matches today's reality and keeps the design simple.

### 2.5 Operator representation

One superset `Operator` enum in core. Lambda's `Operator` (lambda-data.hpp:518) and `JsOperator` (js_ast.hpp:161) overlap heavily (add/sub/mul/div/mod/pow, comparisons, logical, unary). JS adds: `===/!==`, `>>>`, `in`, `instanceof`, `typeof`, `void`, `delete`, `??`, compound assignments incl. `??=/&&=/||=`. Lambda adds: `to` (range), `is`, occurrence operators, pipe.

- Merge into one enum; operators that exist in only one language are simply never produced by the other builder.
- **Operator semantics are not encoded in the enum** — `OP_EQ` from a JS module lowers via the JS profile (coercing `==`), from a Lambda module via the Lambda profile. Where behavior differs the *shape* is still one binary node. This mirrors how `map_kind` already lets one Map type carry two object models.

---

## 3. Shared Semantic Analysis

### 3.1 Pass architecture

Per-language **builders** stay (this is essential — surface syntax and static-semantics quirks are absorbed here), but they emit the common AST and drive shared machinery:

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
- `NameEntry` gains the union of flags both sides need: `{is_mutable, is_var_param, has_type_annotation, type_widened}` (Lambda) + `{var_kind: var|let|const, tdz, is_private}` (JS).

Per-language *validation* passes (early errors, duplicate checks, strict rules) run on the bound tree via LangProfile hooks.

### 3.3 Function analysis side structure — `FnAnalysis`

Both compilers accumulate per-function facts outside the node. Generalize JS's `JsFuncCollected` and Lambda's `CaptureInfo` into one structure referenced from the unified `AstFuncNode`:

```c
struct FnAnalysis {
    // captures (shared shape; superset of CaptureInfo + JsCaptureEntry)
    CaptureEntry* captures;        // name, entry, is_mutable/is_const, env slot,
                                   // scope_env_key, grandparent_slot, force_env
    int capture_count;
    bool has_scope_env;            // shared closure environment (JS model,
                                   // adoptable by Lambda closures)
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

This preserves each language's number model (a semantic property) while sharing the walk, the alias machinery, the caching (`infer_cache`), and the call-site propagation pass (`jm_callsite_propagate` generalizes to both — Lambda currently *lacks* call-site-aware inference and was deferred; it comes for free here).

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
    // validation & extension-node lowering
    int  (*validate)(Script*, AstNode* root);
    Reg  (*lower_ext_node)(MirEmitter*, AstNode*);         // language-range nodes
};
```

Concrete divergences and where they land:

| Divergence | Resolution |
|---|---|
| `==` (JS coercing vs Lambda structural), `+` (JS string-concat overload), truthiness (`""`/`0` falsy in JS; Lambda's own rules per Formal_Semantics) | `lower_binary` / `emit_truthy_test` per profile |
| Number models (Lambda int/int64/float/decimal vs JS all-float64 + BigInt→decimal per N1–N9) | inference resolution policy + `emit_to_number`; the Item encoding already carries both |
| `null` vs `undefined` | already solved in the value model (`LMD_TYPE_UNDEFINED`); Lambda code simply never produces it |
| Object access: Lambda map field vs JS prototype chain + descriptors + ICs | `emit_member_get/set`; JS profile keeps its IC machinery (`JsLoadIC`/`JsStoreIC`), Lambda profile keeps direct shape access. `map_kind` already discriminates at runtime |
| Errors: Lambda `T^E`/raise vs JS throw/try | Both lower to **error-values in MIR** (J3 already forces this at ABI level; JS's completion machinery is already value-based internally). Profile `emit_error_check` decides propagation style: Lambda `?`-style early-return vs JS try-context dispatch. Cross-language: an error crossing modules is an error Item — no unwinding, per J3 |
| Mutability: Lambda immutable-by-default vs JS mutable | fn/pn flags + profile; assignment lowering consults the profile |
| `this`, prototype, classes | JS-range nodes lowered by JS `lower_ext_node`; Lambda never sees them |
| Iteration protocols (JS Symbol.iterator vs Lambda ranges/collections) | for-of core node, profile-dispatched iterator emission (`jm_emit_get_iterator` generalizes) |

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
│ per-language lowering handlers (ext nodes + semantics) │  ← Lambda / JS / Py …
├────────────────────────────────────────────────────────┤
│ shared lowering driver: dispatch on node_type,         │
│ core control flow (if/while/for/switch), calls,        │
│ closures & scope-env, destructuring engine,            │
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

The shared driver owns the `switch(node_type)` over core nodes, delegating semantic decisions to the profile (§4.3) and unknown/language-range nodes to `lower_ext_node`. The existing 10-file `js_mir_*` body becomes: JS profile handlers (classes, generators driver, eval, with, regex, ICs) + shared driver contributions (statements, expressions, destructuring, iterators, completion). Lambda's `transpile-mir.cpp` becomes: Lambda profile handlers (query/for-clauses, elements, pipes, patterns, views) + the same shared driver.

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

Under this design, a guest becomes: **grammar + builder (kept, per-language) + LangProfile (new, small) + runtime library (kept)** — the ~860 KB of parallel *lowering* code is the collapse target, since the shared driver + emitter absorb control flow, calls, closures, variables, boxing, consts, GC rooting.

Guest-specific notes:
- **Python:** its comprehensions map naturally onto Lambda's for-expr clause family (for/where/order — Lambda-range nodes could be *shared* with the Python builder rather than core-promoted); its number model gets its own evidence-resolution policy (int→int64/BigInt); reference semantics + in-place mutation get **documented projection rules in the profile** — turning G7 (reference-vs-value impedance, "currently emergent transpiler behavior") into explicit policy code.
- **Ruby:** blocks/procs map onto the shared closure/scope-env model; method dispatch is profile `emit_member_get`/`emit_call`.
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
5. Create `ast-core.hpp`: base node, unified `AstNodeType` ranges, superset `Operator` enum, shared `NameScope`/`NameEntry` extensions.
6. Renumber JS/TS node kinds into their ranges (mechanical switch updates; `.bak`-style diff tests: AST dumps before/after must match modulo numbers).
7. `Script.lang` → `LangProfile*`; profiles exist but are pass-throughs to current code.

**Phase 2 — leaf-struct unification (incremental, node-family at a time)**
8. Merge shape-identical struct pairs (§2.3), family by family: literals+idents → binary/unary → control flow → calls/members → functions. After each family, both suites green.
9. Unify `AstFuncNode` + introduce `FnAnalysis` (initially just wrapping each side's existing data).

**Phase 3 — shared analysis**
10. One capture analysis producing `FnAnalysis.captures`; Lambda closures move onto the shared scope-env representation.
11. One evidence-based inference walk + per-profile resolution (§3.4); port both sides' inference fixes into it; delete `InferCtx` and `JsParamEvidence`. Call-site propagation becomes available to Lambda (previously deferred — revisit then).
12. JS binding promoted to build time on shared scopes (§3.2); `JsFuncCollected`'s scope facts migrate into `FnAnalysis`; early errors become the JS profile validate hook.

**Phase 4 — shared lowering driver**
13. Stand up the driver on *core* nodes only, behind a per-module flag; Lambda first (simpler semantics), JS module-by-module (start with the node-baseline corpus).
14. Move statements → expressions → destructuring → iterators into the driver; per-language handlers shrink correspondingly.
15. Resumable-function transform extracted as the shared utility (K17 layer 1), consuming `FnAnalysis` — Lambda's `start`/concurrency Stage A work and JS generators/async converge on it (respecting layer-3/4 boundaries).

**Phase 5 — module system**
16. AST-level cross-language import binding; retire `create_js_import_script`; live bindings for exported vars.

**Phase 6 — first guest port (Python)**
17. Python builder retargets to common AST + PyProfile; delete `transpile_py_mir.cpp` incrementally. This is the proof that the design generalizes — treat it as the acceptance test for the whole effort.

Risk register:
- **G1 rooting** — addressed at Phase 0 by design; do not defer.
- **JS perf cliffs** (ICs, native specialization, shape caches): Phase 4 must carry them into the profile intact; benchmark AWFY + LambdaJS perf suite per step.
- **Double-boxing v2 interaction**: if S0–S3 of that plan proceeds concurrently, sequence it *after* Phase 0 so it lands in the shared emitter once.
- **Enum renumber blast radius** (Phase 1): purely mechanical but wide; do it in one commit per language with AST-dump equivalence tests.
- **`transpile.cpp` (C2MIR)**: frozen per U11; only the mechanical enum update touches it.

---

## 9. Decision Ledger (proposed — awaiting confirmation)

| # | Decision | Status |
|---|---|---|
| **U1** | One AST node-kind space: core (0–499) + per-language ranges; generalizes the TS-at-1000 precedent. Structure-identical ⇒ core node; semantics divergence handled at lowering, not by node duplication | proposed |
| **U2** | Language is a property of the compilation unit (`Script.lang → LangProfile*`), never of individual nodes; base node stays `{node_type, Type*, next, TSNode}` | proposed |
| **U3** | Shape-identical leaf structs unify in `ast-core.hpp`; language-specific leaves stay in language headers; promotion to core allowed, demotion never | proposed |
| **U4** | One superset `Operator` enum; operator *semantics* dispatch through the LangProfile | proposed |
| **U5** | Scope/binding converges on build-time authoritative binding over shared `NameScope`/`NameEntry` (JS hoisting/TDZ implemented at bind time; validation via profile hooks) | proposed |
| **U6** | Per-function analysis unifies in `FnAnalysis` (captures on the JS shared scope-env model, unified evidence records); evidence-based inference shared with per-language resolution policies | proposed |
| **U7** | Shared MIR transpiler = MirEmitter (bottom) + shared driver (core nodes) + LangProfile handlers (semantics + ext nodes); K17 layer boundaries respected (calling conventions and resumption drivers stay per-language) | proposed |
| **U8** | Substrate: ShapePool becomes the single shape store with a new transition API absorbing JS hidden classes; const pool adopts the Lambda `const_list`/BSS model; `VarEntry`/scope stacks unified in the emitter | proposed |
| **U9** | Module registry: AST-level import binding; `create_js_import_script` synthetic bridge retired; live bindings implemented once for Lambda `pub` vars + ES live bindings; J2's Item/C-ABI call contract unchanged | proposed |
| **U10** | J-ledger amendment **J2-R**: "each front-end transpiles itself" → "each front-end *builds* itself (grammar + builder + profile); in-tree front-ends share AST, analysis, and lowering infrastructure." J1/J3/J5/J6 unchanged; common AST is in-memory only, never a distribution format | proposed |
| **U11** | C2MIR/`transpile.cpp` frozen as Lambda-only debug backend during migration; retirement decision deferred to post-Phase-4 | proposed |
| **U12** | Migration follows K17 extract-after-convergence with per-phase green gates; **G1 GC-rooting fix is a Phase-0 prerequisite baked into MirEmitter**; Python is the first guest port and the design's acceptance test | proposed |

## 10. Open Questions (need input)

1. **How far to normalize in builders vs preserve surface forms?** E.g. JS `for(;;)` and Lambda `for … in` could both desugar toward fewer core loop nodes, or stay distinct nodes. Recommendation: preserve surface forms as distinct core/range nodes initially (easier debugging, dump fidelity); desugar later only where the driver visibly benefits.
2. **Destructuring: core or JS-range?** Lambda has `AST_NODE_DECOMPOSE`; JS has full patterns; Python will need them too. Recommendation: start JS-range, promote to core during the Python port (U3 allows this).
3. **Should Lambda adopt any JS analysis strengths early** — specifically call-site type propagation (`jm_callsite_propagate`), previously deferred for Lambda? Phase 3 makes it nearly free; needs the earlier deferral revisited.
4. **`FnAnalysis.lang_ext` vs typed unions** for profile-owned function facts (ctor shape caches, view state) — opaque pointer (loose, simple) or tagged union (checked, rigid)?
5. **Timing vs concurrency work:** K-plan Stage A (fork-join) and the resumable-function transform both touch `transpile-mir.cpp`. Sequence the K17 layer-1 extraction inside Phase 4 here, or land Stage A first on the old code and extract after? (K17's own advice — "build the Lambda transform as an independent second implementation, extract after two clients" — argues Stage A first.)
