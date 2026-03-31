# Transpile_Ts — TypeScript Support for LambdaJS

## Executive Summary

This document proposes extending LambdaJS to support TypeScript (`.ts`, `.tsx`) source files
through the existing Lambda JIT pipeline. TypeScript is a strict superset of JavaScript; the
core strategy is **type erasure at the AST layer** — TS annotations are parsed and discarded,
and the remaining JS-compatible AST is handed to the existing MIR transpiler with only minimal
TS-specific additions (enums, namespaces, decorators, access modifiers).

**CLI target:**
```
./lambda.exe ts script.ts          # Run TypeScript via JIT
./lambda.exe ts --check script.ts  # Type-check only (future phase)
```

---

## 1. Grammar Strategy

### Option 1 — Extend the Existing JS Grammar (Recommended)

Add TypeScript syntax on top of `tree-sitter-javascript/grammar.js` using the same
`grammar(base, overrides)` pattern that the official `tree-sitter-typescript` package uses:

```js
// lambda/tree-sitter-typescript/grammar.js
const javascript = require('../tree-sitter-javascript/grammar')

module.exports = grammar(javascript, {
  name: 'typescript',

  // Extra externals for TS scanner (e.g. template literal disambiguation in tsx)
  externals: ($, prev) => prev.concat([$._tsx_start_opening_element_tag]),

  // TS adds these to the declaration supertype
  supertypes: ($, prev) => prev.concat([
    $.type_declaration,
    $.enum_declaration,
    $.interface_declaration,
    $.namespace_declaration,
    $.ambient_declaration,
  ]),

  rules: {
    // --- type annotations ---
    type_annotation:        $ => seq(':', $._type),
    _type:                  $ => choice($.predefined_type, $.type_identifier,
                                        $.generic_type, $.union_type,
                                        $.intersection_type, $.function_type,
                                        $.tuple_type, $.array_type,
                                        $.object_type, $.parenthesized_type,
                                        $.literal_type, $.conditional_type,
                                        $.infer_type, $.mapped_type_clause,
                                        $.template_literal_type),
    predefined_type:        _ => /any|unknown|never|void|object|string|number|boolean|symbol|bigint|null|undefined/,
    generic_type:           $ => seq($.type_identifier, $.type_arguments),
    type_arguments:         $ => seq('<', commaSep1($._type), '>'),
    union_type:             $ => seq($._type, '|', $._type),
    intersection_type:      $ => seq($._type, '&', $._type),
    tuple_type:             $ => seq('[', commaSep($._type), ']'),
    array_type:             $ => seq($._type, '[', ']'),
    function_type:          $ => seq(optional($.type_parameters), '(',
                                       optional($._parameter_list_with_types), ')',
                                       '=>', $._type),
    conditional_type:       $ => seq($._type, 'extends', $._type, '?', $._type, ':', $._type),
    infer_type:             $ => seq('infer', $.type_identifier),
    mapped_type_clause:     $ => seq('{', '[', $.type_identifier, 'in', $._type, ']',
                                       optional(seq(':', $._type)), '}'),
    template_literal_type:  $ => seq('`', repeat(choice($.template_chars, seq('${', $._type, '}'))), '`'),
    literal_type:           $ => choice($.number, $.string, $.true, $.false, '-', $.number),
    parenthesized_type:     $ => seq('(', $._type, ')'),
    object_type:            $ => seq('{', repeat($.object_type_member), '}'),
    object_type_member:     $ => seq(optional($.readonly), $._property_name,
                                       optional('?'), $.type_annotation),

    // --- type parameters on generics ---
    type_parameters:        $ => seq('<', commaSep1($.type_parameter), '>'),
    type_parameter:         $ => seq($.type_identifier,
                                       optional(seq('extends', $._type)),
                                       optional(seq('=', $._type))),
    type_identifier:        _ => /[A-Z_$][a-zA-Z0-9_$]*/,

    // --- declarations ---
    interface_declaration:  $ => seq('interface', $.type_identifier,
                                       optional($.type_parameters),
                                       optional(seq('extends', commaSep1($._type))),
                                       $.object_type),
    type_declaration:       $ => seq('type', $.type_identifier,
                                       optional($.type_parameters),
                                       '=', $._type),
    enum_declaration:       $ => seq(optional('const'), 'enum', $.identifier,
                                       '{', commaSep($.enum_member), '}'),
    enum_member:            $ => seq($._property_name, optional(seq('=', $.expression))),
    namespace_declaration:  $ => seq(choice('namespace', 'module'), $._module_name,
                                       $.statement_block),
    ambient_declaration:    $ => seq('declare', choice(
                                       $.function_declaration, $.class_declaration,
                                       $.type_declaration, $.interface_declaration,
                                       $.enum_declaration, $.namespace_declaration,
                                       seq('const', $.identifier, $.type_annotation))),

    // --- decorators ---
    decorator:              $ => seq('@', choice($.identifier,
                                       $.call_expression, $.member_expression)),

    // --- modifiers on class members ---
    accessibility_modifier: _ => /public|private|protected/,
    override_modifier:      _ => 'override',
    readonly:               _ => 'readonly',
    abstract:               _ => 'abstract',

    // --- TS extensions to existing JS rules ---
    // Override: add optional type annotation to arrow_function params and
    //           type_parameters to function_declaration / arrow_function
    function_declaration: ($, prev) => seq(
      optional('async'), 'function', optional('*'),
      optional($.identifier),
      optional($.type_parameters),
      $.formal_parameters,
      optional($.type_annotation),
      $.statement_block,
    ),
    arrow_function: ($, prev) => seq(
      optional('async'),
      optional($.type_parameters),
      choice(
        $.identifier,
        seq('(', optional($._parameter_list_with_types), ')'),
      ),
      optional($.type_annotation),
      '=>',
      choice($.expression, $.statement_block),
    ),
    // Override: TS class members may have modifiers + field type annotations
    method_definition: ($, prev) => seq(
      repeat(choice($.decorator, $.accessibility_modifier, $.override_modifier,
                    $.readonly, $.abstract, 'static', 'async')),
      optional(choice('get', 'set', '*')),
      $._property_name,
      optional('?'),
      optional($.type_parameters),
      $.formal_parameters,
      optional($.type_annotation),
      $.statement_block,
    ),
    public_field_definition: ($, prev) => seq(
      repeat(choice($.decorator, $.accessibility_modifier, $.override_modifier,
                    $.readonly, $.abstract, 'static', 'declare')),
      $._property_name,
      optional(choice('?', '!')),
      optional($.type_annotation),
      optional(seq('=', $.expression)),
    ),
    // Override: variable declarator adds optional type annotation
    variable_declarator: ($, prev) => seq($.pattern, optional($.type_annotation),
                                            optional(seq('=', $.expression))),
    // Override: formal_parameter gains type annotation + modifiers (constructor param props)
    _formal_parameter: ($, prev) => choice(
      prev,
      seq(repeat($.accessibility_modifier), $.pattern, optional($.type_annotation),
          optional(seq('=', $.expression))),
    ),
    // as expression
    as_expression:        $ => seq($.expression, 'as', choice($._type, 'const')),
    // non-null assertion
    non_null_expression:  $ => seq($.expression, '!'),
    // satisfies
    satisfies_expression: $ => seq($.expression, 'satisfies', $._type),
    // type predicate (in return type position)
    type_predicate:       $ => seq($.identifier, 'is', $._type),
  },
})
```

**Why this approach is correct:**
- TypeScript IS a superset of JavaScript. The grammar relationship mirrors the language relationship.
- The official `tree-sitter-typescript` works exactly this way and is battle-tested.
- The JS grammar is already in the project and well-tested; extending rather than forking it keeps the parsers in sync as the JS grammar evolves.
- All JS node symbols keep their existing numeric IDs. TS-only nodes get new IDs appended at the end — no existing `js_ast.hpp` symbol `#define` needs to change.

### Option 2 — Standalone TS Grammar

Write a completely independent Tree-sitter grammar covering all of JavaScript AND TypeScript
from scratch.

**Drawbacks:**
- ~5× more grammar code to maintain (TypeScript grammar is ~2000 lines if self-contained).
- JS correctness regressions in the TS parser are invisible to JS regression tests.
- Two grammars evolve independently, inevitably diverging over time.
- The official Tree-sitter ecosystem explicitly uses the extension pattern for TypeScript; a
  standalone grammar would conflict with community tooling.

**Verdict: Option 1 is clearly better.** It is the standard approach, produces a smaller
grammar codebase, shares regression tests with the JS grammar, and keeps the two parsers
permanently synchronized.

---

## 2. AST Builder Extension

### 2.1 File Layout

```
lambda/ts/
  ts_ast.hpp              # TS-only AST node types (extends js_ast.hpp)
  build_ts_ast.cpp        # TS AST builder (delegates to build_js_ast, handles TS nodes)
  ts_transpiler.hpp       # TsTranspiler context (extends JsMirTranspiler)
  transpile_ts_mir.cpp    # TS MIR transpiler (extends transpile_js_mir.cpp)
  ts_runtime.cpp          # TS-specific runtime helpers (enum boxing, decorator apply)
  ts_runtime.h
```

### 2.2 New AST Node Types (`ts_ast.hpp`)

Type annotations are parsed but never emitted to MIR — they exist only so the builder can
skip them cleanly. Runtime-meaningful TS constructs (enums, namespaces) do generate MIR.

```cpp
// ts_ast.hpp  — extends JsAstNodeType
typedef enum TsAstNodeType {
    // --- type-level nodes (all erased at transpile time) ---
    TS_AST_NODE_TYPE_ANNOTATION = JS_AST_NODE__MAX,  // : SomeType
    TS_AST_NODE_TYPE_ALIAS,          // type Foo = ...
    TS_AST_NODE_INTERFACE,           // interface Foo { ... }
    TS_AST_NODE_TYPE_PARAMETERS,     // <T extends U = V>
    TS_AST_NODE_TYPE_PARAMETER,      // single T extends U = V
    TS_AST_NODE_PREDEFINED_TYPE,     // string, number, any, ...
    TS_AST_NODE_GENERIC_TYPE,        // Array<T>
    TS_AST_NODE_UNION_TYPE,          // A | B
    TS_AST_NODE_INTERSECTION_TYPE,   // A & B
    TS_AST_NODE_TUPLE_TYPE,          // [A, B, C]
    TS_AST_NODE_ARRAY_TYPE,          // T[]
    TS_AST_NODE_FUNCTION_TYPE,       // (a: T) => R
    TS_AST_NODE_OBJECT_TYPE,         // { a: T; b: U }
    TS_AST_NODE_CONDITIONAL_TYPE,    // T extends U ? X : Y
    TS_AST_NODE_INFER_TYPE,          // infer T
    TS_AST_NODE_MAPPED_TYPE,         // { [K in T]: V }
    TS_AST_NODE_TEMPLATE_LITERAL_TYPE,

    // --- erasable expression wrappers ---
    TS_AST_NODE_AS_EXPRESSION,       // expr as T   → just expr at runtime
    TS_AST_NODE_NON_NULL_EXPRESSION, // expr!        → just expr at runtime
    TS_AST_NODE_SATISFIES_EXPRESSION,// expr satisfies T → just expr at runtime
    TS_AST_NODE_AMBIENT_DECLARATION, // declare ...  → skipped entirely

    // --- runtime-meaningful TS constructs ---
    TS_AST_NODE_ENUM_DECLARATION,    // enum Direction { Up = 0, Down }
    TS_AST_NODE_ENUM_MEMBER,         // Up = 0
    TS_AST_NODE_NAMESPACE_DECLARATION,// namespace Foo { ... }
    TS_AST_NODE_DECORATOR,           // @decorator

    TS_AST_NODE__MAX,
} TsAstNodeType;

// Enum declaration node
typedef struct {
    JsAstNode base;
    bool is_const;       // `const enum`
    Str name;
    JsAstNode** members; // TsAstNodeType == TS_AST_NODE_ENUM_MEMBER
    int member_count;
} TsEnumDeclarationNode;

// Single enum member
typedef struct {
    JsAstNode base;
    Str name;
    JsAstNode* initializer; // nullable; expression if explicit, null for auto-increment
    int auto_value;          // resolved numeric value (filled during analysis phase)
} TsEnumMemberNode;

// namespace Foo { statements* }
typedef struct {
    JsAstNode base;
    Str name;
    JsAstNode** body;
    int body_count;
} TsNamespaceDeclarationNode;

// @decorator   (applied to class / method / field)
typedef struct {
    JsAstNode base;
    JsAstNode* expression; // call or identifier
} TsDecoratorNode;

// as / non-null / satisfies  — all single-child wrappers
typedef struct {
    JsAstNode base;
    JsAstNode* inner;
    // type_node left NULL after erasure (we don't store types in these nodes)
} TsErasureNode;
```

### 2.3 AST Builder Logic (`build_ts_ast.cpp`)

The builder wraps the existing JS builder using a thin dispatch layer:

```
build_ts_ast(TsTranspiler* tp, TSNode root)
    │
    ├── if node is a known JS symbol → call build_js_ast_node() (existing logic)
    │
    └── if node is a TS-only symbol → call build_ts_ast_node() (new)
            ├── type_annotation       → TS_AST_NODE_TYPE_ANNOTATION (leaf, no children stored)
            ├── interface_declaration → TS_AST_NODE_INTERFACE       (leaf, erased)
            ├── type_declaration      → TS_AST_NODE_TYPE_ALIAS      (leaf, erased)
            ├── enum_declaration      → TsEnumDeclarationNode
            ├── namespace_declaration → TsNamespaceDeclarationNode
            ├── decorator             → TsDecoratorNode
            ├── as_expression         → TsErasureNode wrapping inner expression
            ├── non_null_expression   → TsErasureNode wrapping inner expression
            ├── satisfies_expression  → TsErasureNode wrapping inner expression
            └── ambient_declaration   → TS_AST_NODE_AMBIENT_DECLARATION (leaf, not compiled)
```

**Key rules for the builder:**

1. **Type annotations are ignored entirely.** When building a `variable_declarator`,
   `function_declaration`, `arrow_function`, `method_definition`, or `formal_parameter`,
   the builder iterates Tree-sitter children and skips any child whose symbol is
   `type_annotation`, `type_parameters`, or any `_type`-family symbol.

2. **Erasure wrappers are transparent.** When the expression layer encounters
   `as_expression`, `non_null_expression`, or `satisfies_expression`, it builds a
   `TsErasureNode` whose `inner` field is the real sub-expression. The transpiler
   then simply recurses into `inner`.

3. **`declare` blocks produce no AST children.** Any node under `ambient_declaration`
   is skipped completely — ambient declarations are only meaningful to a type checker.

4. **Access modifiers (`public`/`private`/`protected`) are stripped.** They are
   parsed (to avoid grammar errors) but not stored in the AST; JS semantics apply.

5. **`readonly` on fields becomes a plain field.** Mutability is not enforced at runtime.

6. **Constructor parameter properties** (e.g. `constructor(private x: number)`) are
   desugared during AST construction into a field declaration + assignment in the body:
   ```ts
   // TS source
   constructor(private x: number) {}
   // AST equivalent built
   // field: x (no initializer)
   // body prepend: this.x = x;
   ```

7. **Enum members are resolved** to their numeric (or string) values during the AST pass,
   filling `TsEnumMemberNode.auto_value` with the sequential integer counter.

---

## 3. TypeScript Transpiler

### 3.1 Design Principle: Thin Wrapper Over JS Transpiler

TypeScript transpiles to exactly the same MIR as its erased JavaScript equivalent. The TS
transpiler reuses `transpile_js_mir.cpp` for 95% of the code path and adds only:

| TS construct | Transpiler action |
|---|---|
| `as` / `!` / `satisfies` | Recurse into inner expression, discard wrapper |
| `interface` / `type alias` / `ambient` | Emit nothing |
| `enum` (numeric, non-const) | Emit JS-object construction code |
| `const enum` | Inline member values as integer literals |
| `namespace` | Emit an IIFE assigning to a JS object |
| `@decorator` | Emit post-class decorator application call |

### 3.2 Entry Point

```c
// transpile_ts_mir.cpp

Item transpile_ts_to_mir(Runtime* runtime, const char* ts_source, const char* filename) {
    TsTranspiler tt;
    ts_transpiler_init(&tt, runtime, ts_source, filename);

    // 1. Parse with the TS grammar (tree-sitter-typescript)
    TSParser* parser = ts_parser_new();
    const TSLanguage* lang = tree_sitter_typescript();
    ts_parser_set_language(parser, lang);
    TSTree* tree = ts_parser_parse_string(parser, NULL, ts_source, strlen(ts_source));

    // 2. Build typed AST  (build_ts_ast delegates to build_js_ast for JS nodes)
    JsAstNode* root = build_ts_ast(&tt, ts_tree_root_node(tree));

    // 3. Transpile to MIR  (jm_transpile_statement / jm_transpile_expression
    //    handle TS nodes; everything else delegates to JS equivalents)
    return transpile_ts_mir_ast(&tt, root);
}
```

The `TsTranspiler` struct embeds `JsMirTranspiler` as its first member so all
`jm_*` helpers work without casts:

```c
typedef struct TsTranspiler {
    JsMirTranspiler base;       // MUST be first — enables safe upcast
    bool tsx_mode;              // true for .tsx files
    bool strict_mode;           // always true for .ts (TS implies strict)
} TsTranspiler;
```

### 3.3 Statement Transpilation Extensions

`jm_transpile_statement` is extended with a TS-specific prefix check:

```c
// transpile_ts_mir.cpp — extends jm_transpile_statement

static void jm_transpile_ts_statement(JsMirTranspiler* mt, JsAstNode* stmt) {
    switch (stmt->type) {

    // --- erased: emit nothing ---
    case TS_AST_NODE_INTERFACE:
    case TS_AST_NODE_TYPE_ALIAS:
    case TS_AST_NODE_AMBIENT_DECLARATION:
        return;

    // --- enum → object literal ---
    case TS_AST_NODE_ENUM_DECLARATION:
        jm_transpile_ts_enum(mt, (TsEnumDeclarationNode*)stmt);
        return;

    // --- namespace → IIFE ---
    case TS_AST_NODE_NAMESPACE_DECLARATION:
        jm_transpile_ts_namespace(mt, (TsNamespaceDeclarationNode*)stmt);
        return;

    default:
        // fall through to the existing JS statement transpiler
        jm_transpile_statement(mt, stmt);
        break;
    }
}
```

### 3.4 Expression Transpilation Extensions

```c
static MIR_reg_t jm_transpile_ts_expression(JsMirTranspiler* mt, JsAstNode* expr) {
    switch (expr->type) {

    // erasure wrappers — just recurse into inner expression
    case TS_AST_NODE_AS_EXPRESSION:
    case TS_AST_NODE_NON_NULL_EXPRESSION:
    case TS_AST_NODE_SATISFIES_EXPRESSION:
        return jm_transpile_ts_expression(mt, ((TsErasureNode*)expr)->inner);

    default:
        return jm_transpile_expression(mt, expr);
    }
}
```

### 3.5 Enum Transpilation

A numeric `enum` is lowered to a frozen JS object:

```ts
// TypeScript source
enum Direction { Up, Down, Left = 10, Right }
```

Transpiled as if the programmer had written:

```js
const Direction = Object.freeze({
    Up: 0, Down: 1, Left: 10, Right: 11,
    0: "Up", 1: "Down", 10: "Left", 11: "Right",
})
```

```c
static void jm_transpile_ts_enum(JsMirTranspiler* mt, TsEnumDeclarationNode* node) {
    if (node->is_const) {
        // const enum — all usages are inlined as integer literals during the AST pass.
        // No runtime object is emitted. The symbol is registered so the expression
        // transpiler can substitute literals at reference sites.
        jm_register_const_enum(mt, node);
        return;
    }
    // Emit: const Direction = js_create_object(runtime);
    MIR_reg_t obj = jm_call_1(mt, "js_create_enum_object", jm_runtime_reg(mt));
    for (int i = 0; i < node->member_count; i++) {
        TsEnumMemberNode* m = (TsEnumMemberNode*)node->members[i];
        MIR_reg_t val;
        if (m->initializer) {
            val = jm_transpile_ts_expression(mt, m->initializer);
        } else {
            val = jm_box_int_const(mt, m->auto_value);
        }
        // Forward mapping: Direction.Up = 0
        jm_call_4(mt, "js_set_prop_str", jm_runtime_reg(mt), obj,
                  jm_box_string(mt, m->name.data), val);
        // Reverse mapping: Direction[0] = "Up"  (only for numeric values)
        jm_call_4(mt, "js_set_prop_item", jm_runtime_reg(mt), obj,
                  val, jm_box_string(mt, m->name.data));
    }
    // Object.freeze(Direction) — prevent runtime mutation
    jm_call_2(mt, "js_object_freeze", jm_runtime_reg(mt), obj);
    jm_define_var(mt, node->name, obj, JS_VAR_CONST);
}
```

`const enum` member references are resolved during AST building. When `build_ts_ast` sees
`EnumName.MemberName` and `EnumName` is a registered const enum, it replaces the `member_expression`
node with a literal integer node in-place. No runtime object is ever created.

### 3.6 Namespace Transpilation

A `namespace` block is lowered to an IIFE that receives the namespace object (creating it
if absent), matching the TypeScript compiler's standard output:

```ts
// TypeScript source
namespace Geometry {
    export function area(r: number): number { return Math.PI * r * r }
}
```

Transpiled as:

```js
var Geometry;
(function(Geometry) {
    Geometry.area = function(r) { return Math.PI * r * r }
})(Geometry || (Geometry = {}))
```

The AST builder performs this desugaring directly, constructing a `JsVariableDeclarationNode`
(for `var Geometry`) and a `JsCallNode` (IIFE pattern) from the `TsNamespaceDeclarationNode`
source, so `transpile_js_mir.cpp` handles them without modification.

### 3.7 Decorator Transpilation

Decorators are applied post-class-definition using the TC39 stage-3 semantics (same as
`tsc --experimentalDecorators=false`):

```ts
@Component({ selector: 'app-root' })
class AppRoot { }
```

Lowered to:

```js
let AppRoot = class AppRoot { }
AppRoot = Component({ selector: 'app-root' })(AppRoot) ?? AppRoot
```

The AST builder desugars this into a `let` declarator + assignment expression so the
existing JS MIR transpiler handles it naturally.

---

## 4. Implementation Phases

### Phase 1 — Grammar and Parser (v1)

- [ ] Create `lambda/tree-sitter-typescript/grammar.js` using the extension pattern.
- [ ] Generate `src/parser.c` via `tree-sitter generate`.
- [ ] Add `tree_sitter_typescript()` to `build_lambda_config.json`.
- [ ] Smoke-test: parse a `.ts` file, dump CST, verify all node types are recognized.

### Phase 2 — AST Builder (v1)

- [ ] Create `lambda/ts/ts_ast.hpp` with `TsAstNodeType` enum and concrete node structs.
- [ ] Create `lambda/ts/build_ts_ast.cpp` with TS dispatch layer + type annotation skipping.
- [ ] Implement constructor parameter property desugaring.
- [ ] Implement const enum value resolution (auto-increment counter, initializer eval for
      compile-time literals).
- [ ] Unit test: parse 20+ representative `.ts` snippets, assert correct AST shapes.

### Phase 3 — Transpiler Core (v1)

- [ ] Create `lambda/ts/ts_transpiler.hpp` (`TsTranspiler` embedding `JsMirTranspiler`).
- [ ] Create `lambda/ts/transpile_ts_mir.cpp` with erasure + enum + namespace handlers.
- [ ] Wire `transpile_ts_to_mir()` into `lambda/runner.cpp` for the `ts` CLI subcommand.
- [ ] Add `./lambda.exe ts` entry in `main.cpp` alongside existing `js` handler.
- [ ] Integration test: transpile and run 50+ TypeScript programs covering all erasure cases.

### Phase 4 — TSX Support (v2)

- [ ] Create `lambda/tree-sitter-tsx/grammar.js` extending the TS grammar (same pattern;
      tree-sitter-typescript ships separate `typescript` and `tsx` parsers).
- [ ] Adjust JSX element building in `build_js_ast.cpp` to accept TSX node symbols.
- [ ] Integration: `.tsx` files routed through `transpile_ts_to_mir()` with `tsx_mode = true`.

### Phase 5 — Type Checker Integration (v3, optional)

- [ ] Integrate `tsc --noEmit` as an optional pre-pass for `./lambda.exe ts --check`.
- [ ] Alternatively: build a lightweight structural checker on the TS AST for the most
      common error classes (undefined variables, wrong argument count, basic type mismatch).
- [ ] This phase is explicitly out of scope for v1/v2 — the runtime does not require types.

---

## 5. Test Plan

```
test/ts/
  basic/           # variable declarations, functions, classes
  types/           # type annotations erased correctly
  enums/           # numeric enum, string enum, const enum inlining
  namespaces/      # namespace IIFE lowering
  generics/        # generic functions/classes — types erased, logic runs correctly
  decorators/      # class and method decorators
  async/           # async/await (reuses JS async infrastructure)
  modules/         # import/export type, import type stripping
  tsx/             # TSX element expressions
```

Each test file: `test/ts/foo.ts` paired with `test/ts/foo.txt` (expected stdout), following
the Lambda convention for integration test scripts.

Run via:
```bash
make test-ts-baseline
```

---

## 6. File Map

| File | Role |
|---|---|
| `lambda/tree-sitter-typescript/grammar.js` | TS grammar extending JS grammar |
| `lambda/tree-sitter-typescript/src/parser.c` | Auto-generated; never edit manually |
| `lambda/ts/ts_ast.hpp` | TS AST node types and concrete structs |
| `lambda/ts/build_ts_ast.cpp` | TS AST builder |
| `lambda/ts/ts_transpiler.hpp` | `TsTranspiler` context struct |
| `lambda/ts/transpile_ts_mir.cpp` | TS MIR transpiler |
| `lambda/ts/ts_runtime.cpp` | `js_create_enum_object`, decorator helpers |
| `lambda/ts/ts_runtime.h` | Public API for TS runtime helpers |
| `test/ts/**/*.ts` + `*.txt` | Integration test scripts + expected outputs |
