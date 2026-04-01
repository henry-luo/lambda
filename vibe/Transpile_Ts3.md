# Phase 3: Unified JS/TS Transpiler

> **Goal**: Unify JavaScript and TypeScript into one transpiler with one grammar,
> eliminating redundancy and enabling type-driven optimization for both languages.

---

## 1. Motivation

JavaScript is a subset of TypeScript. The current architecture maintains two separate
grammars, two parsers, and two transpiler entry points — tripling maintenance surface
for what is fundamentally one language with optional type annotations.

### Current Architecture (Problems)

```
JS path:  grammar.js (1,249 lines)  →  parser.c (83K lines, 391KB .a)
          build_js_ast.cpp (2,417 lines) → transpile_js_mir.cpp (17,198 lines)

TS path:  define-grammar.js (1,149 lines, extends JS grammar)
          → parser.c (282K lines, 1.4MB .a)
          build_ts_ast.cpp (1,421 lines) → transpile_ts_mir.cpp (918 lines)
          → delegates to transpile_js_mir.cpp anyway
```

**Waste**:
- Two parser.c files (365K combined lines, 1.8MB combined .a). The TS parser *already
  includes* all JS grammar rules — the JS parser is pure redundancy.
- `build_ts_ast.cpp` is 60% delegation to `build_js_ast.cpp` via cast.
- `transpile_ts_mir.cpp` is 90% delegation to `transpile_js_ast_to_mir()`.
- TsTranspiler struct is a layout-compatible copy of JsTranspiler with 3 extra fields.
- **Estimated savings**: removing the JS parser saves ~391KB from the binary. Merging
  AST builder / transpiler code reduces ~2,300 lines of duplication/delegation.

### The Critical Gap: Type Annotations Are Ignored During Codegen

The current TS transpiler resolves type annotations into `Type*` structs (Phase 2–3),
then **strips them entirely** before handing the AST to the JS MIR transpiler. The JS
MIR transpiler re-infers types using heuristics:

```
TS source:  function add(x: number, y: number): number { return x + y; }
                         ^^^^^^^^   ^^^^^^^^    ^^^^^^
                         explicitly typed — but IGNORED by MIR codegen

JS MIR:     jm_infer_param_types() scans body for arithmetic usage patterns
            → "x used in addition with y" → int_evidence++ → INT
            → return type inferred from `return x + y` → INT
```

This means **explicit TS type annotations provide zero optimization benefit** over
implicit JS — the MIR transpiler runs the same heuristic inference either way.
Type annotations should be the **primary** type source, with heuristic inference as
fallback for untyped JS code.

---

## 2. Design

### 2.1 One Grammar: Use tree-sitter-typescript for All JS/TS

The TS grammar already extends the JS grammar:
```js
// define-grammar.js line 1
const JavaScript = require('tree-sitter-javascript/grammar');
module.exports = function defineGrammar(dialect) {
  return grammar(JavaScript, { ... });
};
```

Every valid JS program is a valid TS program. We use the TS parser for both:

```
Before:   .js → tree_sitter_javascript()   →  JS CST
          .ts → tree_sitter_typescript()    →  TS CST

After:    .js → tree_sitter_typescript()    →  TS CST  (strict_js = true)
          .ts → tree_sitter_typescript()    →  TS CST  (strict_js = false)
```

**JS strict mode**: When `strict_js = true`, the AST builder rejects TS-specific CST
nodes with a syntax error:

```c
// in build_ast — reject TS nodes when parsing JS
if (tp->strict_js && is_ts_specific_node(sym)) {
    log_error("syntax error: TypeScript syntax not allowed in .js file: '%s'",
              ts_node_type(node));
    tp->has_errors = true;
    return NULL;
}
```

TS-specific CST node symbols to reject in JS mode:
- `type_annotation`, `type_alias_declaration`, `interface_declaration`
- `as_expression`, `satisfies_expression`, `non_null_expression`
- `enum_declaration`, `abstract_class_declaration`
- `type_assertion`, `type_arguments` (angle-bracket generics)
- `accessibility_modifier` (`public`, `private`, `protected`)
- `readonly`, `override`, `declare`, `namespace`

**Build system change**: Remove `tree-sitter-javascript` from `build_lambda_config.json`.
One parser library (`libtree-sitter-typescript.a`, 1.4MB) replaces two (1.8MB combined).

### 2.2 One Transpiler Struct

Merge `TsTranspiler` into `JsTranspiler` by adding 3 fields:

```c
// js_transpiler.hpp — unified
typedef struct JsTranspiler {
    // ... existing JS fields ...
    JsAstNode* (*expr_builder_override)(void* tp, TSNode node);
    Runtime* runtime;

    // Type-aware fields (used for both JS inference and TS annotations)
    struct hashmap* type_registry;   // name → Type* (populated from TS interfaces)
    bool strict_js;                  // true = reject TS syntax, false = allow TS
    bool emit_runtime_checks;        // emit ts_assert_type/ts_check_shape calls
} JsTranspiler;
```

The `TsTranspiler` typedef and `ts_transpiler.hpp` are removed. All code uses
`JsTranspiler*` directly. The `tsx_mode` flag moves to a local variable in the
transpiler entry point if needed later.

### 2.3 One AST Builder

Merge `build_ts_ast.cpp` into `build_js_ast.cpp`:

```c
// build_js_ast.cpp — handles both JS and TS nodes
JsAstNode* build_expression(JsTranspiler* tp, TSNode node) {
    TSSymbol sym = ts_node_symbol(node);

    // TS-specific node? Check strict_js mode
    if (is_ts_specific_node(sym)) {
        if (tp->strict_js) {
            // JS file encountered TS syntax — error
            report_syntax_error(tp, node, "TypeScript syntax not allowed in .js");
            return NULL;
        }
        // Handle TS expression nodes (as, satisfies, non_null, etc.)
        return build_ts_expression(tp, node);
    }

    // Standard JS expression handling (existing code)
    switch (sym) {
        case sym_number: ...
        case sym_identifier: ...
        ...
    }
}
```

The existing `ts_expr_override` hook is no longer needed — TS nodes are handled
inline in the unified builder. The ~1,421 lines of `build_ts_ast.cpp` merge into
`build_js_ast.cpp`, with delegation code removed.

### 2.4 One Transpiler Entry Point

```c
// transpile_mir.cpp — unified entry point

Item transpile_to_mir(Runtime* runtime, const char* source, size_t length,
                      const char* filename, bool strict_js) {
    JsTranspiler* tp = js_transpiler_create(runtime);
    tp->strict_js = strict_js;

    // Phase 1: Parse (always use TS parser)
    js_transpiler_parse(tp, source, length);  // tree_sitter_typescript()

    // Phase 2: Build AST (unified builder handles both JS and TS nodes)
    JsAstNode* ast = build_ast(tp);

    // Phase 3: Resolve types (annotations + inference)
    resolve_all_types(tp, ast);     // NEW: unified type resolution

    // Phase 4: Strip type-only nodes (interfaces, type aliases)
    strip_type_only_nodes(tp, ast);

    // Phase 5: MIR codegen (type-informed)
    return transpile_ast_to_mir(runtime, tp, ast, filename);
}
```

`main.cpp` routes:
```c
case "js":  transpile_to_mir(runtime, src, len, file, /* strict_js */ true);
case "ts":  transpile_to_mir(runtime, src, len, file, /* strict_js */ false);
```

---

## 3. Type-Driven Optimization: Bridging Annotations and Inference

This is the core contribution of Phase 3. The current JS transpiler has a capable
type inference engine (`jm_get_effective_type`, `jm_infer_param_types`,
`jm_infer_return_type`). The TS transpiler has explicit type annotations. Neither
uses the other. We unify them.

### 3.1 Current Type Inference (JS — What Works)

The JS MIR transpiler already infers types well for numeric code:

| Capability | How |
|-----------|-----|
| Variable types from init | `let x = 42` → `jm_get_effective_type(init)` → INT |
| Parameter types from body | `jm_infer_walk` accumulates evidence (arithmetic → INT/FLOAT) |
| Return types from returns | `jm_infer_return_type_walk` unifies all return expressions |
| Native codegen for typed fns | `has_native_version` → unboxed MIR ops |
| Type through binary ops | `INT + INT → INT`, `x / y → FLOAT`, comparison → BOOL |
| Special function returns | `Math.floor()` → INT, `arr.length` → INT |

### 3.2 What's Missing

| Gap | Impact |
|-----|--------|
| **TS annotations ignored** | `function f(x: number)` infers x from body, not annotation |
| **No call-site propagation** | `f(42)` doesn't inform that f's param is INT |
| **No union narrowing** | `if (typeof x === "number")` doesn't narrow x to INT in branch |
| **No object shape tracking** | `point.x` always returns ANY even if Point has `x: number` |
| **No generic specialization** | `identity<number>(42)` boxes/unboxes unnecessarily |
| **No string/bool native ops** | Only INT/FLOAT get native paths; string concatenation always boxes |

### 3.3 Unified Type Resolution: Annotations First, Inference as Fallback

```
Type Source Priority:
  1. Explicit TS annotation    →  function add(x: number): number
  2. TS interface/alias        →  interface Point { x: number; y: number }
  3. Inferred from initializer →  let x = 42  (INT)
  4. Body-scan evidence        →  x used in x + y, x << 2  (INT evidence)
  5. Fallback                  →  LMD_TYPE_ANY (boxed)
```

#### Phase 3a: Feed TS annotations into `JsFuncCollected`

Currently, `jm_infer_param_types` runs body-scan evidence for every function.
For TS functions with annotations, skip inference and use the declared types:

```c
// In jm_infer_param_types (transpile_js_mir.cpp)

void jm_infer_param_types(JsMirTranspiler* mt, JsFuncCollected* fc) {
    JsFunctionNode* fn = fc->node;

    // Check for TS type annotations first
    bool has_annotations = false;
    JsAstNode* param = fn->params;
    for (int i = 0; i < fc->param_count && param; i++, param = param->next) {
        Type* ann_type = jm_get_annotation_type(param);
        if (ann_type) {
            fc->param_types[i] = ann_type->type_id;
            has_annotations = true;
        }
    }

    // Check return type annotation
    Type* ret_ann = jm_get_return_annotation(fn);
    if (ret_ann) {
        fc->return_type = ret_ann->type_id;
        has_annotations = true;
    }

    // For unannotated params, fall back to body-scan inference
    if (!has_annotations) {
        jm_infer_param_types_from_body(mt, fc);  // existing evidence scan
    } else {
        // Fill in any unannotated params via body inference
        jm_infer_remaining_params(mt, fc);
    }
}
```

`jm_get_annotation_type` extracts the `Type*` from the `TsTypeAnnotationNode`
attached to the parameter node (set during AST building, preserved through lowering).

#### Phase 3b: Variable type annotations

```ts
let count: number = 0;     // annotation → LMD_TYPE_FLOAT
let name: string = "hi";   // annotation → LMD_TYPE_STRING
```

In `jm_transpile_variable_declaration`, check for annotation before inferring from init:

```c
TypeId var_type;
Type* ann = jm_get_annotation_type(declarator);
if (ann) {
    var_type = ann->type_id;
} else {
    var_type = jm_get_effective_type(mt, declarator->init);
}
```

#### Phase 3c: Interface-aware member access

```ts
interface Point { x: number; y: number }
function length(p: Point): number {
    return Math.sqrt(p.x * p.x + p.y * p.y);
}
```

When accessing `p.x`, if `p`'s type is a `TypeMap` with a shape, look up `x` in the
shape's `ShapeEntry` chain to find `x: number`. This lets `p.x * p.x` generate
native `MIR_DMUL` instead of boxed multiplication.

```c
// in jm_get_effective_type, case JS_AST_NODE_MEMBER_EXPRESSION:
TypeId obj_type = jm_get_effective_type(mt, member->object);
if (obj_type == LMD_TYPE_MAP || obj_type == LMD_TYPE_OBJECT) {
    // Check if we have a TypeMap with shape info for this variable
    Type* obj_full_type = jm_get_full_type(mt, member->object);
    if (obj_full_type && obj_full_type->type_id == LMD_TYPE_MAP) {
        TypeMap* tm = (TypeMap*)obj_full_type;
        ShapeEntry* field = tm_lookup_field(tm, prop_name);
        if (field && field->type) {
            return field->type->type_id;  // e.g., LMD_TYPE_FLOAT for x: number
        }
    }
}
```

#### Phase 3d: Call-site type propagation (JS enhancement)

When a function has no type annotations and body-scan is ambiguous, check call sites:

```js
function double(x) { return x * 2; }  // x: int_evidence from body
double(42);                             // call-site confirms: x is INT
double("hello");                        // call-site contradicts: x must be ANY
```

This is a second-pass inference:
1. **Phase 1.5** (existing): `jm_infer_param_types` from body → provisional types
2. **Phase 1.75** (new): Scan all call expressions. For each call to a known function,
   check if argument types match provisional param types. If any call passes a
   conflicting type, widen the parameter to ANY.

This primarily helps JS code where no annotations exist.

### 3.4 Enhanced Native Codegen

With unified type information, more functions qualify for native (unboxed) codegen:

**Currently** (Phase 4 / `has_native_version`):
- Only INT and FLOAT params/returns generate native versions
- Functions must have no captures and ≤16 params

**Enhanced**:
- TS annotations unlock native codegen for functions where body-scan couldn't infer types
- Interface shapes enable native field access on typed objects
- Known return types from annotations skip the conservative ANY fallback

```ts
// Currently: body-scan infers x, y as ANY (arithmetic with member access)
function distance(p1: Point, p2: Point): number {
    const dx = p1.x - p2.x;  // p1.x → ANY (member access = ANY)
    const dy = p1.y - p2.y;  // → boxed arithmetic
    return Math.sqrt(dx * dx + dy * dy);
}

// With TS3: annotation says p1: Point, shape says x: number
// → p1.x resolves to FLOAT → dx is FLOAT → native MIR_DSUB, MIR_DMUL
```

---

## 4. Implementation Phases

### Phase 3.1 — Grammar Unification

- [ ] Switch JS entry point to use `tree_sitter_typescript()` parser.
- [ ] Add `strict_js` field to `JsTranspiler`.
- [ ] Add `is_ts_specific_node()` helper — checks CST symbol against a set of
      TS-only node names (`type_annotation`, `interface_declaration`, etc.).
- [ ] In `build_js_expression` / `build_js_statement`, reject TS nodes when `strict_js`.
- [ ] Remove `tree-sitter-javascript` from `build_lambda_config.json`.
- [ ] Verify: all JS test scripts parse correctly through TS parser.
- [ ] Verify: JS test scripts with TS syntax produce syntax errors.

### Phase 3.2 — Transpiler Struct Unification

- [ ] Move `type_registry`, `strict_js`, `emit_runtime_checks` into `JsTranspiler`.
- [ ] Remove `TsTranspiler` typedef and `ts_transpiler.hpp`.
- [ ] Remove `ts_transpiler_create` / `ts_transpiler_destroy` — use `js_transpiler_create`.
- [ ] Delete `(JsTranspiler*)tp` casts everywhere (no longer needed).
- [ ] Merge `transpile_ts_to_mir` into `transpile_js_to_mir` (add `strict_js` param).
- [ ] Update `main.cpp` / `runner.cpp` to call unified entry point.

### Phase 3.3 — AST Builder Merge

- [ ] Merge `build_ts_ast.cpp` into `build_js_ast.cpp`.
- [ ] Remove `expr_builder_override` hook (inline TS expression handling).
- [ ] Keep `ts_expr_override` logic as direct switch cases in `build_expression`.
- [ ] Merge TS statement builders (interface, type alias, enum, namespace) into
      `build_js_statement`.
- [ ] Remove `build_ts_ast.cpp`.

### Phase 3.4 — Type Resolution Unification

- [ ] Move `ts_resolve_type()` and `ts_type_builder.cpp` into the unified transpiler.
- [ ] In `jm_infer_param_types`: check for TS annotation → use it.
- [ ] In `jm_infer_return_type`: check for TS annotation → use it.
- [ ] In `jm_transpile_variable_declaration`: check for annotation → use it.
- [ ] Feed `TypeMap` shapes into `jm_get_effective_type` for member access.
- [ ] Add `jm_get_full_type(mt, expr)` → returns `Type*` (not just TypeId), checking both
      annotations and variable scope for rich type info.
- [ ] Implement the `Type*` carry in `JsMirVarEntry` (add `Type* full_type` field).

### Phase 3.5 — Type Inference Enhancements (JS benefit)

- [ ] Call-site type propagation: scan call expressions to reinforce/contradict
      body-scan parameter types.
- [ ] Union type narrowing: after `typeof x === "number"` guard, narrow x to
      LMD_TYPE_FLOAT in that branch's scope.
- [ ] Track return type from call targets: if `let x = knownFunc(42)`, set x's type
      to `knownFunc.return_type`.
- [ ] Conditional expression type: `cond ? intExpr : intExpr` → INT (not ANY).

### Phase 3.6 — Cleanup and Testing

- [ ] Delete `lambda/ts/ts_transpiler.hpp`.
- [ ] Delete `lambda/ts/build_ts_ast.cpp` (merged).
- [ ] Delete `lambda/ts/transpile_ts_mir.cpp` (merged).
- [ ] Keep `lambda/ts/ts_type_builder.cpp` (type resolution logic) — or move to
      `lambda/type_builder.cpp`.
- [ ] Keep `lambda/ts/ts_runtime.cpp` / `ts_runtime.h` (runtime helpers).
- [ ] Run all 754+ baseline tests — must pass 100%.
- [ ] Run all 18 TS tests — must pass 100%.
- [ ] Performance benchmark: typed TS functions generate identical native code to
      manually-optimized JS.

---

## 5. File Map (After Unification)

| Before | After | Notes |
|--------|-------|-------|
| `lambda/js/js_transpiler.hpp` | `lambda/js/js_transpiler.hpp` | + type_registry, strict_js, emit_runtime_checks |
| `lambda/ts/ts_transpiler.hpp` | *(deleted)* | Fields merged into JsTranspiler |
| `lambda/js/build_js_ast.cpp` | `lambda/js/build_js_ast.cpp` | + TS node handling |
| `lambda/ts/build_ts_ast.cpp` | *(deleted)* | Merged into build_js_ast.cpp |
| `lambda/js/transpile_js_mir.cpp` | `lambda/js/transpile_js_mir.cpp` | + annotation-aware inference |
| `lambda/ts/transpile_ts_mir.cpp` | *(deleted)* | Lowering logic merged |
| `lambda/ts/ts_type_builder.cpp` | `lambda/ts/ts_type_builder.cpp` | Kept (type resolution) |
| `lambda/ts/ts_runtime.cpp` | `lambda/ts/ts_runtime.cpp` | Kept (runtime helpers) |
| `lambda/ts/ts_runtime.h` | `lambda/ts/ts_runtime.h` | Kept |
| `lambda/ts/ts_ast.hpp` | `lambda/ts/ts_ast.hpp` | Kept (TS AST node types) |
| `tree-sitter-javascript/` | *(removed from build)* | Parser no longer linked |
| `tree-sitter-typescript/` | `tree-sitter-typescript/` | Sole parser for JS + TS |

---

## 6. Size Impact

### Binary Size Reduction

| Component | Current | After | Savings |
|-----------|---------|-------|---------|
| JS parser (`libtree-sitter-javascript.a`) | 391 KB | 0 KB | −391 KB |
| TS parser (`libtree-sitter-typescript.a`) | 1.4 MB | 1.4 MB | 0 |
| `build_ts_ast.cpp` (~1,421 lines) | compiled | 0 | ~−20 KB |
| `transpile_ts_mir.cpp` (~918 lines) | compiled | 0 | ~−15 KB |
| `ts_transpiler.hpp` | compiled | 0 | minimal |
| **Total estimated savings** | | | **~425 KB** |

Current debug binary is 19 MB. Savings of ~425 KB is ~2.2%.

### Code Reduction

| Source File | Lines Before | Lines After | Change |
|-------------|-------------|-------------|--------|
| `build_js_ast.cpp` | 2,417 | ~3,200 | +783 (merged TS) |
| `build_ts_ast.cpp` | 1,421 | 0 | −1,421 |
| `transpile_js_mir.cpp` | 17,198 | ~17,500 | +302 (merged lowering) |
| `transpile_ts_mir.cpp` | 918 | 0 | −918 |
| `ts_transpiler.hpp` | 70 | 0 | −70 |
| `js_transpiler.hpp` | 75 | ~85 | +10 |
| **Net** | 22,099 | ~20,785 | **−1,314 lines** |

---

## 7. Risk Assessment

| Risk | Mitigation |
|------|-----------|
| TS parser slower than JS parser for plain JS | Unlikely — tree-sitter parsers are O(n); the additional grammar rules add only to table size, not per-character work. Benchmark to verify. |
| TS parser produces different CST for JS code | The TS grammar extends JS — JS syntax produces identical CST node types. Verify with diff on CST dumps. |
| Some JS edge cases parse differently in TS | TS grammar may treat `<expr>` as type assertion instead of comparison in some contexts. Test thoroughly with JS baseline suite. |
| `strict_js` checking misses some TS nodes | Enumerate all TS-specific CST symbols from `ts_node_type()` calls. Use an exhaustive allow/deny list. |
| Type annotation inference changes JS behavior | Annotations should only affect optimization (native vs boxed), never semantics. Boxing/unboxing must be transparent. |

---

## 8. Migration Path

The unification can be done incrementally:

1. **Phase 3.1** (grammar) is safe to do first — the TS parser handles JS fine.
   All JS tests should pass unchanged. This is the biggest win (binary size).

2. **Phase 3.2** (struct merge) is a refactoring change. TsTranspiler becomes a
   typedef for JsTranspiler, then the typedef is removed.

3. **Phase 3.3** (AST merge) is the largest code change. Do it file-by-file:
   move TS node handlers into the JS builder, test after each move.

4. **Phase 3.4** (type unification) is the feature work. This is where the
   optimization benefit comes from. Can be done behind a flag initially.

5. **Phase 3.5** (inference enhancements) benefits JS-only users too. Pure
   additive — no existing behavior changes.

Each phase delivers value independently and all JS/TS tests must pass after each.
