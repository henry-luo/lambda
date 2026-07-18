# Python Runtime on the Unified AST — Implementation Plan

**Date:** 2026-07-18 (rev 1)
**Status:** Proposed — stage plan P0–P6 with decision points PY1–PY10 pending user confirmation
**Related:** `vibe/Lambda_Design_Unified_AST.md` (the governing design; esp. §2.3 catalog, §2.4 coverage, §7 guest foundation, §8 Phase 6), `doc/Lambda_Jube_Runtime.md`, `vibe/Lambda_Design_Concurrency.md` (K17 layers), `vibe/Lambda_GC_Root_Issue.md` (G1)

---

## 1. Goal and Scope

Retarget the Python guest runtime (`lambda/py/`) onto the unified AST and compiler spine, per Phase 6 of `Lambda_Design_Unified_AST.md`. Python is the **first guest port and the acceptance test of the whole unified-AST design** — the empirical check on the ≥80%-core-coverage target (§2.4) and on the claim that a guest language collapses to *grammar + builder + LangProfile + runtime library*.

What changes and what stays, per the design's §7 formula:

| Component | Today | Target |
|---|---|---|
| Grammar | `lambda/tree-sitter-python/` | **kept** as-is |
| AST | own `PyAstNodeType` enum + ~45 `Py*Node` structs (`py_ast.hpp`, 546 lines) | **replaced**: core nodes + a small Python range (2000–2499) in `ast-core.hpp` |
| Builder | `build_py_ast.cpp` (2,429 lines) → PyAst | **kept, retargeted** to emit common AST + bind on shared `NameScope` |
| Scope/binding | `py_scope.cpp` `PyScope` (own struct, shared `NameEntry`) | **replaced** by shared `NameScope` + `ScopeKind` extensions |
| Analysis | `PyFuncCollected` side tables inside the transpiler | **replaced** by shared `FnAnalysis` + `PyFnExt` |
| Lowering | `transpile_py_mir.cpp` (**7,642 lines / 349 KB**) | **collapse target**: shared driver + `PyProfile` handlers (~1,000–1,500 lines projected) |
| Runtime library | `py_runtime` / `py_builtins` / `py_stdlib` / `py_class` / `py_bigint` / `py_print` / `py_async` (~7,100 lines) | **kept** — the Item-level helpers *are* Python's dynamic semantics; profile-emitted code calls them |
| Tests | `test/py/` — 30 scripts + golden `.txt`, `test_py_gtest.cpp` harness | **kept**; the per-stage green gate (plus additions, §8) |

Non-goals: no change to the J2 interop contract (cross-language calls still cross at the Item/C-ABI seam); no change to Python surface semantics observable in the test corpus; no port of Ruby/Bash here (they follow the pattern this port proves).

---

## 2. Current State Audit (measured 2026-07-18)

### 2.1 Shared infrastructure already in place (what Python can target today)

- **`lambda/ast-core.hpp` exists** (739 lines): one `AstNodeType` space with core kinds blocked by level (L1 at 50+, L2/L3 at 150+, L4 at 300+, L5/L6 at 350/400+), Lambda range at 500+, JS-specific at 1000+ (`js_ast.hpp:126–133`), and `JS_AST_NODE_TS_EXTENSION_SENTINEL = 2000` (`js_ast.hpp:134`). JS is fully aliased onto it (`typedef AstNode JsAstNode`, `js_ast.hpp:195`; `typedef AstNodeType JsAstNodeType`, `js_ast.hpp:71`) — main-plan Phase 1 and much of Phase 2 are **done** for Lambda+JS.
- **Superset `Operator` enum** in ast-core (Lambda ops + `OPERATOR_JS_*` members); `typedef Operator JsOperator` with `JS_OP_* = OPERATOR_*` aliases.
- **Shared `NameEntry`/`NameScope`** with `ScopeKind {GLOBAL, MODULE, FUNCTION, BLOCK}` and the Lambda∪JS flag superset (`is_mutable`, `tdz_active`, `is_lexical`, …).
- **Shared core leaf structs**: `AstLiteralNode`, `AstIdentNode`, `AstUnaryNode`/`AstBinaryNode`, `AstCallNode`, `AstFieldNode`, `AstIfNode`, `AstMatchNode`/`AstMatchArm` (JS `switch` already maps onto MATCH per U19: `js_ast.hpp:118–119`), `AstTryNode`/`AstCatchNode` (single-arm), `AstFuncNode`/`AstMethodNode`, `AstClassNode`, L3 pattern nodes (`ASSIGN/ARRAY/MAP_PATTERN`, `REST_*`), `AstYieldNode`/`AstAwaitNode`, module nodes, `FnAnalysis`/`FnCapture`/`FnParamEvidence`, and a **dormant `LangProfile`** (validate/analyze/lower stubs; `lambda_profile`, `js_profile`; `Script.profile` and `ModuleDescriptor.profile` wired but no-op).
- **`lambda/mir_emitter_shared.hpp` exists** (698 lines): `MirEmitter` (ctx, func cursor, reg/label counters, import cache, const-pool fields `const_list`/`consts_reg`/`consts_bss`), unified `VarEntry` (typedef'd to both `MirVarEntry` and `JsMirVarEntry`), `em_var_scope_*` hashmap machinery, `em_new_reg`/`em_emit_insn`/`em_call_0..5`/`em_call_void_*`, scalar-return re-homing — main-plan **Phase 0 is done** for Lambda+JS.

### 2.2 Python's position relative to that

Already aligned (head start):

- `PyAstNode` base is **layout-identical** to `AstNode` by design (`py_ast.hpp:156` — "Field order must match AstNode layout") but uses a `base` member (composition), not inheritance.
- `py_scope.cpp` already stores **shared `NameEntry`** in its scopes.
- `PyMirTranspiler` **embeds `MirEmitter em`** (`transpile_py_mir.cpp:139`) and every `pm_*` primitive is already a thin wrapper over `em_*` (`pm_new_reg`→`em_new_reg` at `transpile_py_mir.cpp:244`, `pm_call_N`→`em_call_N` at 264–309, import cache via `em_import_cache_new` at 7375) — Phase-0 emitter adoption is **already done for Python**.
- Python modules already register in the shared **`module_registry`** with `source_lang = "python"` (`module_register_loading(py_path, "python")` at `transpile_py_mir.cpp:7573`; `module_get` caching at 7472), and Lambda→Python imports go through `load_py_module` (`build_ast.cpp:9494,9593`).

Still fully parallel (the actual work):

| Parallel piece | Where | Unified replacement |
|---|---|---|
| `PyAstNodeType` (~60 kinds), `PyOperator` (~44 ops), `PyLiteralType` | `py_ast.hpp:16–153` | core kinds + Py range 2000–2499; superset `Operator` |
| ~45 `Py*Node` structs with `base` member | `py_ast.hpp:156–546` | core leaf structs (inheritance) + Py-range residue structs |
| `PyScope`/`PyScopeType`/`PyVarKind` | `py_transpiler.hpp:14–39`, `py_scope.cpp` | shared `NameScope` + `ScopeKind` (needs CLASS/COMPREHENSION, §5) |
| `PyMirVarEntry` + `py_var_scope` hashmaps + `var_scopes[64]` | `transpile_py_mir.cpp:75–82, 145–146, 213–217` | shared `VarEntry` + `em_var_scope_*` |
| `PyFuncCollected` (captures, nonlocal/global lists, *args/**kwargs, generator/async flags, class info) + `PyLambdaCollected` | `transpile_py_mir.cpp:89–134` | `FnAnalysis` + `FnCapture` + `PyFnExt` via `FnExt` union |
| `PyModuleConstEntry` `module_consts` | `transpile_py_mir.cpp:225–231` | emitter const-pool API + unified module-var BSS |
| The entire lowering walk: expressions (710–2923), statements (4311–5236), functions/closures (2923–3618), classes (3671–4037), match (4037–4081), imports (4081–4311), comprehensions (2522–2708), TCO (5236–5478), generators/async state machines (5478–7288) | `transpile_py_mir.cpp` | shared driver + `PyProfile` hooks; generators later ride the shared resumable-function transform (K17 layer 1) |
| `lang_profile_for_name` knows only "lambda"/"js" — Python modules currently fall back to `lambda_profile` | `ast-core.hpp:733–739` | add `py_profile`, resolve `"python"` |

### 2.3 Entry points and build wiring

- Standalone: `lambda-jube.exe py script.py` → `transpile_py_to_mir(runtime, source, filename)` (`transpile_py_mir.cpp:7288`) — parse → `build_py_ast` → `pm_transpile_ast` → `MIR_link` → run `py_main`.
- Module: `load_py_module` (`transpile_py_mir.cpp:7468`) — used by Lambda importing `.py` (`build_ast.cpp:9494`), by Python↔Python imports (`transpile_py_mir.cpp:4123,4142,4285`), and for packages (`__init__.py`).
- Build: Python compiles **only into the jube variant** (`build_lambda_config.json` target `jube` → `lambda-jube.exe`, links `tree-sitter-python`); the main `lambda.exe` has no Python. This bounds blast radius: Python-side churn cannot break `make build`/`make test-lambda-baseline` except through shared-header edits.
- Tests: `test_py_gtest.cpp` discovers `test/py/test_py_*.py` with golden `.txt` (currently ~30 pairs), runs each and diffs output; one skip (`test_py_import`, filesystem-import Bus error, "Phase E not yet implemented").

---

## 3. Target Architecture

```
source.py ──tree-sitter-python──► CST
        ──build_py_ast (kept, retargeted)──► common AST (core + Py-range nodes)
                                             + scopes bound on shared NameScope
                                               (declare-on-first-assignment synthesis,
                                                global/nonlocal flags, CLASS-scope rule)
        ──shared passes:  capture analysis → FnAnalysis
                          evidence inference → ParamEvidence
                          (+ const folding, U26, when it lands)
        ──PyProfile hooks: validate (py static rules), resolve_param_evidence (int→int64/BigInt)
        ──shared lowering driver (core control flow, calls, closures/scope-env,
                          destructuring engine, TCO, resumable transform)
          + PyProfile lowering handlers (operator/truthiness/member/call/error semantics
                          → py_* runtime helpers; Py-range ext nodes; comprehension clauses)
        ──MirEmitter──► MIR ──► JIT (unchanged)
```

The `PyProfile` (the design's §4.3 vtable) is the port's centerpiece. Its handlers mostly *emit calls into the existing runtime library* — which is why the runtime stays and the transpiler collapses:

| Hook | Python policy | Runtime surface it emits |
|---|---|---|
| `lower_binary` / `lower_unary` | native `int64`/`float` fast path when both operand types are proven (preserving today's `pm_transpile_as_native_int/float`, `transpile_py_mir.cpp:575,658`); else helper call | `py_add/py_subtract/…/py_floor_divide/py_power` (`py_runtime.h:24–58`), bigint promotion inside the helpers |
| `emit_truthy_test` | Python truthiness (empty containers falsy, `0`/`""`/`None` falsy) | `py_to_bool` / inline tag tests |
| `emit_to_number` | Python numeric coercion | `py_to_int`/`py_to_float` |
| `emit_member_get/set` | attribute protocol incl. descriptors, bound methods | `py_getattr`/`py_setattr`/`py_delattr` (`py_runtime.h:63–67`) |
| `emit_call` | positional/default/`*args`/`**kwargs` arity policy; class-vs-function callee dispatch (Py instantiates via plain `AST_CALL`, §2.3 L1 of the design) | direct MIR call, `py_call_*` shims, `py_class` construction path |
| `emit_error_check` | try-context dispatch to nearest handler label; error-values per J3 | raise/except helpers |
| `resolve_param_evidence` | int evidence → `LMD_TYPE_INT64` (Python ints are arbitrary precision; int64 fast path + runtime overflow promotion to bigint); float → FLOAT; container/string/reassigned → ANY | — |
| `lower_clause` | comprehension `CLAUSE_FOR`/`CLAUSE_WHERE`; decorator clauses on FUNC/CLASS | closure + iterator emission |
| `lower_ext_node` | Py-range residue: slice, with, del, assert, chained compare, class patterns, global/nonlocal (no-op at lowering — binding already consumed them) | `py_slice_*`, context-manager `__enter__/__exit__`, `py_match_*` |
| `validate` | Python static rules: `return` outside function, `nonlocal` with no binding, duplicate params, `await` outside async | — |

Cross-language imports bind at AST level once main-plan Phase 5 lands (§6, Stage P6): the importer sees the Python module's `AstScript` + `TypeFunc`s instead of a namespace-map shape walk.

---

## 4. Node and Operator Mapping Catalog

The complete disposition of every `PyAstNodeType`. "Core" = existing ast-core kind; "core+" = core kind needing a field/struct amendment (§5); "clause" = tier-2 clause node; "Py-range" = new kind in 2000–2499; "builder-norm" = normalized away by the builder (sanctioned mapping, not tree rewriting of semantics).

### 4.1 Expressions (L1)

| Python node | Disposition | Target + notes |
|---|---|---|
| `IDENTIFIER` | core | `AST_NODE_IDENT` (`AstIdentNode` — already field-identical) |
| `LITERAL` (int/float/str/bool/None) | core+ | `AST_NODE_LITERAL`; **needs `int64_t` member + int/float `LiteralKind` distinction** in `AstLiteralNode` (today: `double number_value` only; Python must not round-trip int64 through double). `is_bigint`/`bigint_str` already present for large ints |
| `FSTRING`, `FSTRING_EXPR` | core+ | **promote** JS `TEMPLATE_LITERAL`/`TEMPLATE_ELEMENT` (js-range 1002/1003) to core `AST_NODE_INTERP_STR` per U16 (two clients now exist); Python `format_spec` (`.2f`, `>10`) rides as a *(var.)* field on the element node |
| `BINARY_OP` | core | `AST_NODE_BINARY` + superset `Operator` (§4.5) |
| `UNARY_OP`, `NOT` | core | `AST_NODE_UNARY` (`not` → `OPERATOR_NOT`; Python truthiness is the profile's `emit_truthy_test`) |
| `BOOLEAN_OP` (and/or) | core | `AST_NODE_BINARY` with `OPERATOR_AND/OR`; short-circuit + value-propagation ("return the operand, not a bool") is profile lowering |
| `COMPARE` (chained `a < b < c`) | split | single comparison → plain `AST_NODE_BINARY` (builder-norm); **chains stay Py-range `PY_AST_NODE_COMPARE_CHAIN`** — desugaring would double-evaluate middle operands, exactly the U19 hazard class |
| `CALL` | core | `AST_NODE_CALL_EXPR` (`AstCallNode`); kwargs → `AST_NODE_NAMED_ARG` **promoted** from Lambda range 521 to core (Lambda + Python both produce it) |
| `ATTRIBUTE` | core | `AST_NODE_MEMBER_EXPR` (`AstFieldNode`, `computed=false`) |
| `SUBSCRIPT` | core | `AST_NODE_INDEX_EXPR` (`AstFieldNode`, `computed=true`) |
| `SLICE` | core+ | new core `AST_NODE_RANGE` `{start; end; step; inclusive}` per design L1 (Lambda `to` is the second client; slice = RANGE inside INDEX_EXPR composition per §2.4) |
| `LIST` | core | `AST_NODE_ARRAY` |
| `TUPLE` | core+ | `AST_NODE_SEQ` + **add `SeqKind {list, tuple, comma}`** field per U16 (struct today has no kind field) |
| `SET` | Py-range | `PY_AST_NODE_SET` — no core set literal; lowering calls `py_set_new` |
| `DICT`, `PAIR` | core | `AST_NODE_MAP` + `AST_NODE_PROPERTY` (`computed` for non-literal keys) |
| `LIST/DICT/SET_COMPREHENSION`, `GENERATOR_EXPRESSION` | core+ | `AST_NODE_FOR_EXPR` **promoted** from Lambda range 511 to core, with core clause nodes `CLAUSE_FOR`/`CLAUSE_WHERE` (the design's marquee mapping: "Python comprehensions = exactly CLAUSE_FOR + CLAUSE_WHERE + body"); result-kind *(var.)* field `{list, dict, set, generator}`; dict-comp body is a PROPERTY node; generator expressions builder-norm to an immediately-referenced `AST_FUNC{is_generator}` closure (CPython does the same) |
| `CONDITIONAL_EXPR` | core | `AST_NODE_IF_EXPR` (`AstIfNode` — value-position `x if c else y`) |
| `LAMBDA` | core | `AST_NODE_FUNC_EXPR` (`AstFuncNode`, anonymous) |
| `STARRED` | core | `AST_NODE_SPREAD` (call-site splat) / `AST_NODE_REST_ELEMENT` (binding-site `*rest`) |
| `KEYWORD_ARGUMENT` | core | `AST_NODE_NAMED_ARG` (promoted, above); `**splat` → `AST_NODE_SPREAD` with `is_map_splat` *(var.)* flag |
| walrus `:=` | core | `AST_NODE_ASSIGN` in expression position (design: "usable in expr and stmt position") |

### 4.2 Statements (L2) and bindings (L3)

| Python node | Disposition | Target + notes |
|---|---|---|
| `MODULE` | core | `AST_SCRIPT` (root; bound to `ModuleDescriptor`) |
| `BLOCK` | core | `AST_NODE_BLOCK` (no `NameScope` of its own — Python has no block scope; `vars=NULL`) |
| `EXPRESSION_STATEMENT` | core | `AST_NODE_EXPR_STMT` |
| `ASSIGNMENT` (incl. `a = b = expr`, tuple targets, annotated) | core+ | `AST_NODE_ASSIGN`; tuple/star targets → L3 `AST_NODE_ARRAY_PATTERN`/`REST_ELEMENT` (one shared destructuring engine, U13); multi-target chains carry a *(var.)* `next_target` list — value evaluated once, assigned left-to-right, never desugared to repeated assigns |
| `AUGMENTED_ASSIGNMENT` | core | `AST_NODE_ASSIGN` with compound `op` (U19 — `a[f()] += 1` single-evaluation kept in one node's lowering) |
| first assignment to a name | builder-norm | **synthesized declarator** at bind time (design §L2 rule 1): builder creates the `NameEntry` (and a synthetic `AST_NODE_VARIABLE_DECLARATOR` where the driver needs a decl site) so downstream passes see a uniformly declared world |
| `IF`/`ELIF`/`ELSE` | core | `AST_NODE_IF_EXPR` statement-position; elif chains nest in `otherwise` (builder already flattens CST) |
| `WHILE` (+ `else`) | core+ | `AST_NODE_WHILE_STAM`; **add `else_body` *(var.)* field** (runs on normal exit, skipped on break) |
| `FOR` (+ `else`) | core+ | `AST_NODE_FOR_OF_STAM` (= design `AST_FOR`, U22) with L3 pattern target, `IterKind`, `else_body` *(var.)*; iterator protocol is profile-dispatched (`__iter__`/`__next__` via `py_iter_*`) |
| `BREAK`/`CONTINUE` | core | `AST_NODE_BREAK_STAM`/`CONTINUE_STAM` (no labels in Python — label field stays NULL) |
| `RETURN` | core | `AST_NODE_RETURN_STAM` |
| `RAISE` | core | `AST_NODE_RAISE_STAM` (bare re-raise = NULL value; `raise X from Y` cause as *(var.)* field) |
| `TRY`/`EXCEPT`/`FINALLY` | core+ | **`AST_NODE_TRY_STAM` upgraded to the U23 multi-arm shape**: `{block; catch-arm list; else_body; finalizer}`; `PyExceptNode` → `AST_NODE_CATCH_CLAUSE` gaining `type_filter` (the `except TypeError` expr) + existing param/body; JS becomes the single-arm case of the same struct |
| `ASSERT` | Py-range | `PY_AST_NODE_ASSERT` (test + message; lowers to conditional raise) |
| `PASS` | builder-norm | dropped (empty statement) |
| `DEL` | Py-range | `PY_AST_NODE_DEL` (targets list; attr/index/name deletion semantics are profile lowering) |
| `GLOBAL`/`NONLOCAL` | binding-only | consumed at bind time into `NameEntry.{is_global_decl, is_nonlocal}` (new flags, §5); kept as Py-range `PY_AST_NODE_SCOPE_DECL` marker node for dump fidelity; no lowering |
| `WITH` | Py-range | `PY_AST_NODE_WITH` (context managers; maps toward the R1–R5 scoped-resource model later per §2.4 — "likely a small core-promotable node"; keep Py-range until R-plan lands) |
| `IMPORT`/`IMPORT_FROM` | core | `AST_NODE_IMPORT` + `AST_NODE_IMPORT_SPECIFIER` (`AstImportNode` covers module/alias/specifiers/is_relative/is_cross_lang); Python path/package *resolution* stays profile policy |
| `MATCH`/`CASE` | core+ | `AST_NODE_MATCH_EXPR`/`AST_NODE_MATCH_ARM`; **add `MatchForm` + per-arm `guard`** fields (U19 — JS switch already shares the node; Python arms have guards and never fall through) |
| `YIELD` (+ `yield from`) | core | `AST_NODE_YIELD` (`AstYieldNode.delegate` = `yield from`) |
| `AWAIT` | core | `AST_NODE_AWAIT` |

### 4.3 Patterns (match/case → L3)

`PyPatternNode` (`py_ast.hpp:493–530`) redistributes onto the shared pattern vocabulary:

| `PyPatternKind` | Disposition | Target |
|---|---|---|
| `PY_PAT_LITERAL` | core | literal node as pattern (match-arm patterns reuse L1 literals per design §L3) |
| `PY_PAT_CAPTURE` | core | ident binding target |
| `PY_PAT_WILDCARD` | core | anonymous hole (ident `_` with no binding) |
| `PY_PAT_SEQUENCE` (+ `*rest`) | core | `AST_NODE_ARRAY_PATTERN` + `AST_NODE_REST_ELEMENT` (`rest_pos` preserved) |
| `PY_PAT_MAPPING` (+ `**rest`) | core | `AST_NODE_MAP_PATTERN` + `AST_NODE_REST_PROPERTY` |
| `PY_PAT_OR` | Py-range | `PY_AST_NODE_PATTERN_OR` — alternatives with shared bindings; genuinely Python-structural today (promote if Ruby `in`-patterns want it too) |
| `PY_PAT_CLASS` | Py-range | `PY_AST_NODE_PATTERN_CLASS` — isinstance + positional `__match_args__` + keyword sub-patterns |
| `PY_PAT_AS` | Py-range | `PY_AST_NODE_PATTERN_AS` |
| `PY_PAT_VALUE` (dotted const) | core | `AST_NODE_MEMBER_EXPR` used in pattern position |

Runtime support (`py_match_is_sequence`/`py_match_is_mapping`/`py_match_mapping_rest`, `py_runtime.h:56–58`) is unchanged; the shared destructuring engine handles SEQUENCE/MAPPING arms, the profile's `lower_ext_node` handles OR/CLASS/AS.

### 4.4 Functions, classes, modules (L4–L6)

| Python node | Disposition | Target + notes |
|---|---|---|
| `FUNCTION_DEF` | core | `AST_NODE_FUNC` (`AstFuncNode`): `is_async`, `is_generator` (yield-detected) flags exist; `is_proc = true` always (U14 — guests are all-effectful); return annotation resolves to advisory `Type*` or is dropped |
| `PARAMETER` family (typed/default/`*args`/`**kwargs`) | core+ | `AST_NODE_PARAM` (`AstNamedNode`) + `default_value`; `*args` → `is_rest`; `**kwargs` → **new *(var.)* `is_kw_rest` flag** (no JS/Lambda equivalent); keyword-only/positional-only markers as Py *(var.)* flags |
| `DECORATOR` | clause | tier-2 clause chain on `AST_FUNC`/`AST_CLASS` (design §2.1); lowering wraps the defined value in decorator calls innermost-first |
| `LAMBDA` | core | `AST_NODE_FUNC_EXPR` (already in §4.1) |
| `CLASS_DEF` | core+ | `AST_NODE_CLASS` (`AstClassNode`): **`superclass` generalizes to `bases` list** (design L5: "single for JS, multiple for Py"); `metaclass` as Py clause; methods → `AST_NODE_METHOD` (U25 — a method *is* a function node); class-body arbitrary statements stay Py-range residue (design §L5) |
| `IMPORT`/`IMPORT_FROM` | core | see §4.2; `__init__.py` package walk stays profile resolution (`transpile_py_mir.cpp:4285` path today) |

### 4.5 Operators

`PyOperator` (`py_ast.hpp:97–144`) folds into the superset `Operator`:

- Direct: ADD SUB MUL DIV MOD POW → `OPERATOR_ADD/SUB/MUL/DIV/MOD/POW`; `//` → `OPERATOR_IDIV`; comparisons → `OPERATOR_EQ/NE/LT/LE/GT/GE`; `and/or/not` → `OPERATOR_AND/OR/NOT`; `in` → `OPERATOR_IN`; `is` → `OPERATOR_IS` (identity semantics via profile — same enum member, profile-dispatched meaning per U4).
- Bitwise + shifts: reuse the existing `OPERATOR_JS_BIT_AND/OR/XOR/NOT`, `OPERATOR_JS_LSHIFT/RSHIFT` members. **Recommended cleanup (PY7):** rename to neutral `OPERATOR_BIT_*`/`OPERATOR_SHL/SHR` with JS aliases kept, now that a second non-JS client uses them.
- Compound assigns: map onto the existing `OPERATOR_JS_*_ASSIGN` members (same neutral-rename note).
- New members needed: `OPERATOR_PY_MATMUL` (`@`), `OPERATOR_PY_MATMUL_ASSIGN` (`@=`).
- Builder-normalized: `not in` → `UNARY{NOT}` over `BINARY{IN}`; `is not` → `UNARY{NOT}` over `BINARY{IS}` (safe: negation of the *result*, no operand re-evaluation).

### 4.6 Scope model mapping

| Python rule | Where it lands |
|---|---|
| function scope only, no block scope | builder creates `NameScope` per FUNCTION/MODULE only; block statements bind into the enclosing function scope |
| declare on first assignment | builder synthesis at bind time (§4.2) |
| `global` | `NameEntry.is_global_decl` (new flag) — lookups bind to the MODULE scope entry; lowering targets the module BSS slot |
| `nonlocal` | `NameEntry.is_nonlocal` (new flag) — binds to nearest enclosing FUNCTION entry; forces shared scope-env capture (today's `capture_is_nonlocal`/`shared_env_names`, `transpile_py_mir.cpp:98–105`) |
| class body is a scope, but **not** visible to nested functions by bare name | `ScopeKind` gains `SCOPE_KIND_CLASS` (new); shared `lookup_name` skips CLASS scopes when the lookup originates below a FUNCTION boundary — the one Python-specific lookup rule, implemented once in the shared walk behind a profile predicate |
| comprehensions get their own scope (target vars don't leak) | `ScopeKind` gains `SCOPE_KIND_COMPREHENSION` (FUNCTION-like for capture analysis; matches today's `PY_SCOPE_COMPREHENSION`) |

---

## 5. Core-Catalog Amendments Python Drives

Each is small, additive, and justified by a second (or third) client — per U3's promotion rule. All land in `ast-core.hpp` (+ the JS/Lambda code that must honor the widened shape). Grouped by risk:

**Enum/range housekeeping (mechanical):**
1. Move `JS_AST_NODE_TS_EXTENSION_SENTINEL` 2000 → 1500 (`js_ast.hpp:134`; declared but unused elsewhere — one line) so Python owns 2000–2499 per the design's range table.
2. Add the Python block `PY_AST_NODE_* = 2000…` for the §4 residue (~12 kinds: SET, COMPARE_CHAIN, ASSERT, DEL, WITH, SCOPE_DECL, PATTERN_OR/CLASS/AS, and reserves).
3. Fix the pre-existing collision `AST_NODE_START = 541` / `AST_NODE_EVENT_HANDLER = 541` (`ast-core.hpp:131–132` — two distinct kinds sharing a value; flagged separately as a bug).
4. `Operator`: add `OPERATOR_PY_MATMUL`, `OPERATOR_PY_MATMUL_ASSIGN`; optional neutral renames of `OPERATOR_JS_BIT_*` (PY7).

**Struct field additions (additive; JS/Lambda unaffected or trivially updated):**
5. `AstLiteralNode`: `int64_t int_value` in the value union + a `LiteralKind` that distinguishes int/float (design L1 catalog lists `{…int, float…}`; today only `AST_LITERAL_NUMBER`+double).
6. `AstWhileNode` / `AstForOfNode`: `AstNode* else_body` *(var.)*.
7. `AstMatchNode`: `MatchForm form`; `AstMatchArm`: `AstNode* guard`, `bool fallthrough` (JS switch sets `fallthrough`; Python sets `guard`).
8. `AstSeqNode`/`AstArrayNode` used as SEQ: `SeqKind kind` per U16.
9. `AstCallNode`/`AstSpreadNode`: `is_kw_rest`/map-splat flag for `**kwargs`.
10. `AstAssignNode`: `next_target` multi-target chain *(var.)*.
11. `AstClassNode`: `superclass` → `bases` list (JS builder produces a 1-element list; touch JS class lowering accordingly).
12. `NameEntry`: `is_global_decl`, `is_nonlocal` (design §L2 flag superset). `ScopeKind`: `SCOPE_KIND_CLASS`, `SCOPE_KIND_COMPREHENSION`.
13. `FnExt` union (`ast-core.hpp:697`): add `py` member; per U20 upgrade members to typed forward-declared pointers (`PyFnExt*` etc.) — contents stay in the profile.

**Structural upgrades (the two real ones — sequenced early in P2 because JS shares the structs):**
14. **Multi-arm `AST_NODE_TRY_STAM`** (U23): `AstTryNode` gains an arm list + `else_body`; `AstCatchNode` gains `type_filter` (+ optional `guard`). JS try lowering iterates a 1-arm list. This is the plan's largest shared-code touch; it is also *the* U23 deliverable, done once for JS+Py(+Rb later).
15. **Core `AST_NODE_INTERP_STR`** (U16): promote JS `TEMPLATE_LITERAL`/`TEMPLATE_ELEMENT` (1002/1003) into a core kind with quasis/exprs + per-element `format_spec` *(var.)*; JS tagged templates stay JS-range.
16. **Core `AST_NODE_FOR_EXPR` + clause nodes**: promote 511 out of the Lambda range; define `CLAUSE_FOR`/`CLAUSE_WHERE`/`CLAUSE_LET` as core clause kinds (Lambda's `GROUP/ORDER/JOIN` clauses stay Lambda-range). Lambda's builder/transpiler take the mechanical renumber; Python becomes the second producer.
17. **Core `AST_NODE_RANGE`**: `{start; end; step; inclusive}` — Lambda `to` and Python slices/`range()` per design L1.
18. `lang_profile_for_name`: add `py_profile`, resolve `"python"` (`ast-core.hpp:733`) — today Python modules silently get `lambda_profile`.

---

## 6. Migration Stages

Method: K17 extract-after-convergence, Python edition — **the `test/py` corpus stays green after every stage**; shared-header stages additionally gate on `make test-lambda-baseline` (100%) + the JS suites, since ast-core/emitter edits rebuild everyone. No big-bang: `transpile_py_mir.cpp` shrinks family-by-family, never wholesale.

### Stage P0 — substrate completion (no AST change; start immediately)

Emitter primitives are already shared (§2.2). Finish the remaining substrate:

1. Replace `PyMirVarEntry` + `py_var_scope` hashmaps + `var_scopes[64]` (`transpile_py_mir.cpp:75–82,145,213–217`) with shared `VarEntry` + `em_var_scope_new` — `PyMirVarEntry`'s fields (`reg, mir_type, from_env, env_slot, env_reg, type_hint`) are a strict subset of `VarEntry`.
2. Move `module_consts` (`PyModuleConstEntry`, 225–231) onto the emitter const-pool fields (`const_list`/`consts_bss`) so bigint literals and future pooled constants use the one `add_const/load_const` discipline (design §6.3); immediate-encodable ints/floats/strings stay baked inline as today.
3. Unify Python's module-var globals (`global_var_names/indices`, 156–161) with the Lambda `global_vars` BSS model — prerequisite for live bindings in P6.
4. Set `em.note_mir_call` if Python wants the same call telemetry as JS.

Deletes ~250–400 lines; zero behavior change. **Gate:** `make build-jube` + py corpus green; byte-diff `temp/py_mir_dump.txt` across the corpus before/after (expect identical or trivially-renamed registers).

### Stage P1 — kind-space entry (mechanical)

1. Land §5 items 1–4 (ranges, Py block, operator members).
2. `py_ast.hpp`: `typedef AstNodeType PyAstNodeType` with `PY_AST_NODE_X = AST_NODE_Y` aliases for every core-covered kind (the exact `js_ast.hpp:71–124` precedent) and real 2000-range values for the residue; `typedef Operator PyOperator` with `PY_OP_* = OPERATOR_*` aliases.
3. Convert `Py*Node` structs from `base`-member composition to `: AstNode` inheritance (mechanical: `n->base.node_type` → `n->node_type` across `build_py_ast.cpp`/`transpile_py_mir.cpp`); add `static_assert`s pinning layout.
4. Add a Python AST-dump golden: extend `print_py_ast_node` to a file dump, snapshot the corpus before the change, byte-compare after (kind *names* printed, so renumbering is invisible to the golden).

No struct-shape or semantics change. **Gate:** py corpus + dump goldens; lambda/js suites (shared enum touched).

### Stage P2 — leaf-struct adoption, level by level (mirrors main Phase 2)

Order follows the design: L1 first, then L2+L3 together (the variable story as one piece), then L4, L5/L6. For each family: land the §5 core amendment (if any) → retarget the builder to construct the core struct → point the existing `pm_*` lowering at the core fields → delete the Py struct.

1. **L1a** literal/ident/unary/binary/boolean (needs §5.5): `PyLiteralNode`→`AstLiteralNode`, `PyIdentifierNode`→`AstIdentNode`, `PyBinaryNode`/`PyUnaryNode`/`PyBooleanNode`→`AstBinaryNode`/`AstUnaryNode`. Single-op `PyCompareNode` → `AstBinaryNode`; chains → `PY_AST_NODE_COMPARE_CHAIN`.
2. **L1b** call/member/collections: `PyCallNode`→`AstCallNode` (+NAMED_ARG promotion), `PyAttributeNode`/`PySubscriptNode`→`AstFieldNode`, list/tuple/dict/pair → ARRAY/SEQ/MAP/PROPERTY (+`SeqKind`), conditional→`AstIfNode`, starred→SPREAD/REST.
3. **L1c** f-strings (§5.15) and slices (§5.17 RANGE).
4. **L2+L3** statements & the variable story: assignment family (incl. tuple-unpack → L3 patterns, aug-assign, multi-target, walrus), if/while/for (+else fields §5.6), break/continue/return/raise, **try multi-arm (§5.14)**, match (+§5.7), import/export, yield/await, block/expr-stmt; declarator synthesis begins here (builder-side only — binding still on PyScope until P3).
5. **L1d** comprehensions (§5.16 FOR_EXPR promotion) — after L2/L3 because clause bindings are L3 patterns.
6. **L4** functions: `PyFunctionDefNode`/`PyLambdaNode`/params → `AstFuncNode`/`AST_NODE_PARAM` (+`is_kw_rest` §5.9); decorators → clause chain.
7. **L5/L6** classes (+`bases` §5.11, metaclass clause) and imports on `AstImportNode`.

Each sub-step is one commit; `py_ast.hpp` shrinks to the Py-range residue (~150 lines). **Gate per sub-step:** py corpus; lambda/js suites when the shared struct changed (steps 1, 3, 4, 5, 7).

### Stage P3 — binding on shared scopes

1. Replace `PyScope`/`py_scope.cpp` with shared `NameScope` (+`SCOPE_KIND_CLASS`/`COMPREHENSION`, §5.12); `PyVarKind` facts move into `NameEntry` flags (`is_global_decl`, `is_nonlocal`).
2. Implement Python's binding rules at build time on the shared structures (design §3.2): declare-on-first-assignment synthesis; `global`/`nonlocal` consumption; the CLASS-scope skip rule in lookup; comprehension-scope isolation.
3. `validate` profile hook takes over Python's static errors (currently scattered `py_error` calls in the builder).
4. Add builder-level unit tests for the scope rules (see §8 — these are the subtle cases the corpus under-covers).

**Gate:** py corpus + new scope tests.

### Stage P4 — analysis on `FnAnalysis`

1. `PyFuncCollected`/`PyLambdaCollected` (`transpile_py_mir.cpp:89–134`) split: capture facts → shared `FnAnalysis.captures`/`FnCapture` (nonlocal ⇒ `force_env_capture` + shared scope-env, matching today's `shared_env_names` machinery); generator/async facts → `FnAnalysis.{yield/await counts, may_await}`; Python-only facts (`*args`/`**kwargs` names, class-method info, generator frame-local table) → `PyFnExt` via `FnExt.py` (§5.13).
2. Adopt the shared capture analysis when main Phase 3 lands; until then Python's own collection pass *populates* the shared structures — data model unified first, pass second (same two-step JS is taking).
3. Implement `resolve_param_evidence` for Python: int evidence → `LMD_TYPE_INT64` native path (overflow promotion stays in `py_add`/friends at runtime — inference only selects representation, never changes semantics); float → FLOAT; container/compare-with-non-numeric/reassigned → ANY. Wire Python into call-site propagation (U17) when the shared collector lands.

**Gate:** py corpus; verify the native-int fast paths still fire (MIR-dump spot checks on `test_py_tco.py`, `fib`-style scripts).

### Stage P5 — `PyProfile` + shared-driver adoption (syncs with main Phase 4)

Define `PyProfile` with the §3 hook table and move lowering family-by-family out of `transpile_py_mir.cpp` into the shared driver + hooks, in dependency order:

1. Statements skeleton (if/while/for/block/assign/return/break/continue) — driver owns control flow, profile owns truthiness + iterator protocol.
2. Expressions (literal/ident/binary/unary/member/index) — profile `lower_binary` keeps the native int64/float inline paths; helper-call fallback.
3. Destructuring engine (tuple unpack, star targets, match sequence/mapping arms) — the shared L3 engine replaces `pm_assign_comp_target` (`transpile_py_mir.cpp:2527`) and per-site copies.
4. Calls (`transpile_py_mir.cpp:1266–2384`, the 1,100-line family): defaults/`*args`/`**kwargs` arity policy in `emit_call`; builtin dispatch stays a Python table; TCO moves onto the driver's shared TCO.
5. Functions/closures onto the shared scope-env emission; module top-level onto unified globals.
6. Classes, match, with/del/assert/chained-compare via `lower_ext_node`.
7. Imports last within P5 (they interlock with P6).

**Generators/async are explicitly deferred within P5**: `pm_compile_generator` + async state machines (`transpile_py_mir.cpp:5478–7288`, ~1,800 lines) keep working against the new AST until the shared resumable-function transform (K17 layer 1, main Phase 4 item 14) is extracted with its two green clients (JS async/generators + Lambda Stage B). Python then becomes its third client: `FnAnalysis` yield/await counts feed the shared transform; frame layout maps onto K17 layer-2 resume frames; Python keeps its own resumption driver + calling convention (`py_async`, layers 3–4 stay per-language by design §4.5).

**Gate per family:** py corpus green; MIR-dump diff reviewed for the migrated family; no lambda/js regression (driver is shared). Escape hatch: families cut over by commit (git-revertible), no runtime flag — the jube build is not shipped in `lambda.exe`, so risk is contained (PY9 if a flag is wanted anyway).

### Stage P6 — module system & cross-language binding (syncs with main Phase 5)

1. Python modules gain `ModuleDescriptor.ast` (their `AstScript`); Lambda/JS importing Python binds `pub`/exported names through the same `declare_module_import` path — the namespace-map shape walk in `load_py_module` consumers retires alongside `create_js_import_script`.
2. Python→Python imports bind at AST level too; module top-level state moves to the unified BSS/global-var model, enabling live bindings (module_registry.cpp:184 gap — implemented once for Lambda `pub` vars, ES live bindings, and Python module attributes).
3. Fix the skipped `test_py_import` (filesystem-import Bus error) as part of this stage's package-resolution hardening; un-skip it.
4. Python's import *resolution* rules (relative, packages, `__init__.py`, stdlib shims) remain profile policy — unchanged behavior, relocated behind the profile seam.

**Gate:** py corpus incl. un-skipped import tests; cross-language import tests (Lambda→Py, Py→Py, Py→Lambda if supported today) green.

---

## 7. What Stays Python-Owned (explicit non-targets)

- **Runtime library** (~7,100 lines): `py_runtime.cpp` (Item-level operator/attr/container semantics incl. descriptor protocol), `py_builtins.cpp`, `py_stdlib.cpp` (math/json/os/re/…), `py_class.cpp` (MRO/instance model), `py_bigint.cpp` (int64→bigint promotion), `py_print.cpp` (repr/str), `py_async.cpp` (event-loop shim + resumption driver). The profile *emits calls into* this library; it is Python's dynamic-semantics contract per J5/J6.
- **Number-model policy**: int→int64 with runtime bigint promotion (the profile's `resolve_param_evidence` + the helpers), per design §3.4.
- **Reference semantics / in-place mutation (G7)**: documented projection rules live in the `PyProfile` as explicit policy code + a doc section — turning today's "emergent transpiler behavior" into stated rules (design §7 note). Concretely: which Items alias vs copy at assignment, argument passing, container insertion; `is` identity on the Item encoding.
- **Import resolution** rules and stdlib module registry.
- **Resumption drivers + calling conventions** for generators/async (K17 layers 3–4).
- **Grammar** and the CST→AST builder file (retargeted, never shared).

---

## 8. Testing and Gates

- **Primary gate:** `test_py_gtest` over `test/py/` — 100% minus the explicit skip list at every stage; skip list may only shrink.
- **Shared-code gates:** any stage touching `ast-core.hpp`/`mir_emitter_shared.hpp`/shared driver runs `make test-lambda-baseline` (100%) + the JS suites (editor Phase-A, node-baseline no-regression) — same gates as the main plan §8.
- **AST-dump goldens** (new, P1): corpus-wide Python AST dumps by kind-name; byte-stable across renumbering, diff-reviewed across struct adoption.
- **MIR-dump diffing:** `temp/py_mir_dump.txt` per script; P0 expects near-identical output; P5 families get a one-time reviewed diff.
- **Corpus additions before the stage that touches them** (test-first; current 30 scripts under-cover):
  - walrus in comprehensions/conditions; chained comparison with side-effecting middle operand (proves single-evaluation);
  - `while`/`for` `else` with and without `break`;
  - scope subtleties: class-var shadowing in methods, `nonlocal` through two levels, comprehension target non-leakage, `global` at module level, del of a local;
  - multi-target assignment `a = b = f()` (f called once); aug-assign on subscript with side-effecting index;
  - or-patterns/class-patterns/guards in `match`; star patterns at each position;
  - kwargs edge cases (`**` merge order, keyword-only params).
- **≥80%-core acceptance metric** (the design's empirical check): add a debug counter to the builder (`--ast-stats`) tallying nodes by kind over the corpus; report %-of-occurrences on core kinds at P2 exit. Target ≥85% per §2.4's Python projection.
- **Performance:** add 2–3 micro-benches (fib/TCO, nbody-style float loop, string-build) *before* P5; the native int64/float inline paths and TCO must not regress when lowering moves to the driver. Release build (`make release-jube`) for timing per CLAUDE.md rule 10.

---

## 9. Sequencing Against the Main Track

| Python stage | Depends on (main track) | Can start |
|---|---|---|
| P0 substrate | nothing (Phase 0 already landed) | **now** |
| P1 kind space | nothing (additive to Phase-1 state) | **now**, after P0 |
| P2 leaf structs | nothing hard; §5.14–17 amendments coordinate with whoever owns ast-core, and JS try/class lowering updates ride along | now, level by level |
| P3 binding | nothing (shared scopes exist) | after P2's L2/L3 |
| P4 analysis | data model: now; shared *passes*: main Phase 3 | split accordingly |
| P5 driver adoption | **main Phase 4 driver existing for each family** — Python should trail the driver's Lambda/JS bring-up by one family, becoming its third client per family | as Phase 4 progresses |
| P5-generators | resumable-function transform extracted (Phase 4 item 14, two green clients) | after that lands |
| P6 modules | main Phase 5 (AST-level import binding, live bindings) | with/after Phase 5 |

Two scheduling consequences worth stating: (a) P0–P4 are pure wins available immediately and de-risk Phase 6 regardless of when the driver lands — they are also exactly the state the design wants guests in ("builders + profiles on the same spine"); (b) if the main track stalls before Phase 4, Python still ends P4 with a unified AST/binding/analysis and its *own* lowering intact — a stable, shippable intermediate state.

---

## 10. Risk Register

| Risk | Mitigation |
|---|---|
| Shared-header churn breaks Lambda/JS (P2 amendments §5.14/15/16 touch their structs) | additive fields with defaults; `static_assert` layout pins; one amendment per commit; full three-suite gate on each |
| Python scope subtleties regress silently (CLASS-skip rule, comprehension isolation) | test-first corpus additions (§8) before P3; builder unit tests, not just end-to-end goldens |
| Chained-compare / multi-target desugar hazards | never desugared — Py-range node + *(var.)* target chain (U19 discipline) |
| Native int fast path lost in P5 → perf cliff | micro-benches before P5; `lower_binary` contract requires inline emission when operand types proven; MIR-dump review per family |
| int64 vs bigint drift if inference over-promises | inference selects representation only; overflow promotion stays inside `py_*` helpers (unchanged); native path only where today's guards exist |
| Generator frame layout vs K17 layer-2 resume frames | defer generators to the shared transform; map `gen_local_frame_slots` onto shared frame slots then; keep Python driver/calling convention per §4.5 |
| Import side-effect ordering / circular imports during P6 | registry `loading` flag semantics preserved; corpus import tests + un-skipped `test_py_import` |
| 349 KB file migration stalls half-done | family-by-family commits, each green; the file stays compilable throughout; measurable shrink per family (§11 accounting) |
| jube build drift vs main build (Python-only breakage invisible to `make test`) | add `make test-py` (build-jube + gtest) to the CI/default extended-test path if not already there |

---

## 11. Size Accounting (targets)

| File | Today | After P0–P4 | After P5–P6 |
|---|---|---|---|
| `transpile_py_mir.cpp` | 7,642 lines | ~6,800 (substrate + side tables gone) | **~0 — deleted**; replaced by `py_profile.cpp` ≈ 1,000–1,500 lines (hooks, ext-node lowering, builtin/call policy) + Python's share of the shared driver (owned by the driver, not Python) |
| `py_ast.hpp` | 546 lines | ~150 (Py-range residue structs + aliases) | same |
| `build_py_ast.cpp` | 2,429 lines | ~2,400 (retargeted in place; ~35–40% of lines edited) | same |
| `py_scope.cpp` | 252 lines | **deleted** (P3) | — |
| runtime library | ~7,100 lines | unchanged | unchanged |

Net: the ~349 KB parallel-lowering stack collapses to a ~40–60 KB profile — the design's §7 economic claim, made checkable per stage.

---

## 12. Decision Points (pending user confirmation)

| # | Decision | Recommendation |
|---|---|---|
| **PY1** | Python kind range 2000–2499 with TS sentinel moved to 1500 (design §2.2) vs leaving TS at 2000 and putting Python at 2500 | move TS (one unused line today) — keep the design's table true |
| **PY2** | Chained comparison as Py-range node (single-op case builder-normalized to BINARY) | as stated — desugar is a U19 hazard |
| **PY3** | Multi-arm `AST_TRY` upgrade (U23) done in this port, with JS becoming the 1-arm case | yes — Python is the second client that makes U23 concrete |
| **PY4** | `AST_INTERP_STR` + `AST_FOR_EXPR`/clauses + `AST_RANGE` + `NAMED_ARG` promotions to core in P2 | yes — each has ≥2 producers after this port |
| **PY5** | Generator expressions builder-normalized to `AST_FUNC{is_generator}` closures | yes — matches CPython and reuses the generator path wholesale |
| **PY6** | Generators/async wait for the shared resumable transform (Python = third client) rather than porting the state-machine compiler onto core nodes first | yes — avoids migrating 1,800 lines twice; K17 two-client rule honored |
| **PY7** | Neutral renames `OPERATOR_JS_BIT_*` → `OPERATOR_BIT_*` (aliases kept) now that Python uses them | yes, mechanical, in P1 |
| **PY8** | `with` stays Py-range until the R1–R5 scoped-resource model defines the core node | yes — promote later per §2.4 note |
| **PY9** | Family cutover in P5 without a runtime fallback flag (git-revert as the escape hatch; jube-only blast radius) | yes — a flag doubles test surface for a non-shipped binary |
| **PY10** | Add `make test-py` to the default extended test path so Python greenness is CI-visible during the port | yes |
