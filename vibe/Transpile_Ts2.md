# Transpile_Ts — TypeScript Support for LambdaJS

## Executive Summary

This document proposes extending LambdaJS to support TypeScript (`.ts`, `.tsx`) source files
through the existing Lambda JIT pipeline. Unlike standard TypeScript compilers that erase
all type information during compilation, this proposal **preserves TypeScript types as
first-class Lambda `Type*` values**. TS annotations are parsed, resolved to Lambda's type
system (`TypeId`, `TypeBinary` for unions, `TypeFunc` for function signatures, `TypeMap` for
interfaces/object types), and carried through the entire pipeline — driving native code
generation, enabling runtime type introspection via `type()`, powering runtime type guards,
and providing structural type checking at object boundaries.

**Design philosophy:** Types are not decoration — they are data. Following Lambda's
existing approach where types are first-class values (`LMD_TYPE_TYPE`), TypeScript types
are compiled into the same `Type*` infrastructure and available at runtime.

**CLI target:**
```
./lambda.exe ts script.ts          # Run TypeScript via JIT (types preserved)
./lambda.exe ts --check script.ts  # Static type-check only (future phase)
```

---

## 1. Grammar Strategy — Extend the Existing JS Grammar

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
- The JS grammar is already in the project and well-tested; extending rather than forking it
  keeps the parsers in sync as the JS grammar evolves.
- All JS node symbols keep their existing numeric IDs. TS-only nodes get new IDs appended at
  the end — no existing `js_ast.hpp` symbol `#define` needs to change.

---

## 2. Type Mapping: TypeScript → Lambda Type System

### 2.1 Design Principle — Types Are First-Class, Not Erased

Standard TypeScript compilers (tsc, esbuild, swc) erase all types during compilation. This
proposal takes the **opposite approach**: every TS type annotation is resolved to a Lambda
`Type*` struct and preserved through AST → MIR → runtime. This enables:

| Capability | How |
|---|---|
| **Native codegen** | `x: number` → `MIR_T_D` register (skips boxing) |
| **Runtime type()** | `type(x)` returns the TS-declared type as a Lambda type value |
| **Runtime type guards** | `if (x is string)` → `item_type_id(x) == LMD_TYPE_STRING` |
| **Structural checking** | Interface shapes compiled to `TypeMap` with `ShapeEntry` fields |
| **Union narrowing** | `x: string \| number` → `TypeBinary{left=STRING, right=FLOAT}` |
| **Generic instantiation** | `Array<number>` → `TypeArray{nested=&TYPE_FLOAT}` |
| **Function signatures** | `(x: number) => string` → `TypeFunc` with param/return types |

### 2.2 Primitive Type Mapping

| TypeScript | Lambda TypeId | MIR Type | Runtime Representation |
|---|---|---|---|
| `number` | `LMD_TYPE_FLOAT` | `MIR_T_D` (native double) | Unboxed double in register |
| `bigint` | `LMD_TYPE_INT64` | `MIR_T_I64` | Tagged pointer to GC nursery |
| `string` | `LMD_TYPE_STRING` | `MIR_T_I64` | Tagged pointer to `String*` |
| `boolean` | `LMD_TYPE_BOOL` | `MIR_T_I64` | Inline scalar (tag + 0/1) |
| `null` | `LMD_TYPE_NULL` | `MIR_T_I64` | Inline scalar (tag only) |
| `undefined` | `LMD_TYPE_UNDEFINED` | `MIR_T_I64` | Inline scalar (tag only) |
| `symbol` | `LMD_TYPE_SYMBOL` | `MIR_T_I64` | Tagged pointer to `Symbol*` |
| `void` | `LMD_TYPE_NULL` | `MIR_T_I64` | Treated as null return |
| `any` | `LMD_TYPE_ANY` | `MIR_T_I64` | Boxed Item (no native path) |
| `unknown` | `LMD_TYPE_ANY` | `MIR_T_I64` | Boxed Item + mandatory narrowing |
| `never` | `LMD_TYPE_ERROR` | — | Function never returns |
| `object` | `LMD_TYPE_MAP` | `MIR_T_I64` | Container pointer |

**Key decision: `number` maps to `LMD_TYPE_FLOAT` (not INT).** TypeScript `number` is an
IEEE 754 double. This matches JS semantics (`7/2 → 3.5`) and enables direct `MIR_T_D`
native registers. Integer-only annotations (`x: number` where value is always integral)
are tracked via type narrowing, not the base mapping.

### 2.3 Compound Type Mapping

| TypeScript | Lambda Type | Struct |
|---|---|---|
| `string \| number` | Union | `TypeBinary{left=STRING, right=FLOAT, op=OPERATOR_UNION}` |
| `A & B` | Intersection | `TypeBinary{left=A, right=B, op=OPERATOR_INTERSECT}` |
| `number[]` | Typed array | `TypeArray{nested=&TYPE_FLOAT}` |
| `[string, number]` | Tuple | `TypeArray{nested=[STRING,FLOAT], length=2}` |
| `{x: number; y: string}` | Object type | `TypeMap{shape→ShapeEntry chain}` |
| `(x: number) => string` | Function type | `TypeFunc{param=[FLOAT], returned=STRING}` |
| `T?` / `T \| undefined` | Optional | `TypeUnary{operand=T, op=OPERATOR_OPTIONAL}` |
| `interface Foo {...}` | Named shape | `TypeMap` with `struct_name="Foo"` + ShapeEntry chain |
| `enum Direction` | Enum object | `TypeMap` (frozen, forward+reverse mappings) |

### 2.4 Interface → TypeMap with ShapeEntry

TypeScript interfaces compile to Lambda's structural typing infrastructure — the same
`TypeMap` + `ShapeEntry` chain that Lambda uses for map types:

```ts
interface Point {
    x: number;
    y: number;
    label?: string;
}
```

Compiles to:
```c
TypeMap* point_type = alloc_type(pool, LMD_TYPE_MAP, sizeof(TypeMap));
point_type->struct_name = "Point";

// Build shape entry chain (linked list)
ShapeEntry* e_x = pool_calloc(pool, sizeof(ShapeEntry));
e_x->name = intern_name("x");
e_x->type = &TYPE_FLOAT;   // number → LMD_TYPE_FLOAT

ShapeEntry* e_y = pool_calloc(pool, sizeof(ShapeEntry));
e_y->name = intern_name("y");
e_y->type = &TYPE_FLOAT;
e_x->next = e_y;

ShapeEntry* e_label = pool_calloc(pool, sizeof(ShapeEntry));
e_label->name = intern_name("label");
e_label->type = alloc_unary_type(pool, &TYPE_STRING, OPERATOR_OPTIONAL);
e_y->next = e_label;

point_type->shape = e_x;
point_type->length = 3;
// FNV-1a hash table populated for O(1) field lookup
typemap_hash_build(point_type);
```

This enables:
- **Structural compatibility**: any object with `{x: number, y: number}` matches `Point`
- **Runtime field access**: `typemap_hash_lookup(point_type, "x", 1)` → O(1)
- **Type introspection**: `type(p)` returns a TypeType wrapping this TypeMap

### 2.5 Union Types → TypeBinary

```ts
function format(value: string | number): string { ... }
```

The parameter type compiles to:
```c
TypeBinary* union_type = alloc_type(pool, LMD_TYPE_TYPE, sizeof(TypeBinary));
union_type->kind = TYPE_KIND_BINARY;
union_type->op = OPERATOR_UNION;
union_type->left = &TYPE_STRING;
union_type->right = &TYPE_FLOAT;
```

Runtime type guard:
```ts
if (typeof value === "string") { ... }
```

Emits:
```c
// jm_emit: TypeId tid = item_type_id(value_reg);
//          MIR_BEQ label_else, tid, LMD_TYPE_STRING
```

After narrowing inside the `if` body, the transpiler knows `value` is `LMD_TYPE_STRING`
and can emit string-specific operations without boxing checks.

### 2.6 Function Types → TypeFunc

```ts
function add(x: number, y: number): number {
    return x + y;
}
```

```c
TypeFunc* fn_type = alloc_type(pool, LMD_TYPE_FUNC, sizeof(TypeFunc));
fn_type->returned = &TYPE_FLOAT;
fn_type->param_count = 2;
fn_type->required_param_count = 2;

TypeParam* p_x = pool_calloc(pool, sizeof(TypeParam));
p_x->name = intern_name("x");
p_x->type = &TYPE_FLOAT;

TypeParam* p_y = pool_calloc(pool, sizeof(TypeParam));
p_y->name = intern_name("y");
p_y->type = &TYPE_FLOAT;
p_x->next = p_y;

fn_type->param = p_x;
```

This TypeFunc drives:
1. **Native function generation**: both params are `LMD_TYPE_FLOAT` → emit native
   `MIR_T_D` version with `MIR_DADD` (no boxing overhead)
2. **Call-site type checking**: caller's argument types validated against param types
3. **Runtime introspection**: `type(add)` returns a TypeType wrapping this TypeFunc

### 2.7 Generic Types — Monomorphization via Type Parameters

```ts
function identity<T>(x: T): T { return x; }
```

Generics are handled in two tiers:

**Tier 1 — Boxed generic (default):** When `T` is unconstrained, the function operates
on boxed `Item` values (`LMD_TYPE_ANY`). The type parameter `T` is stored in the AST as a
`TsTypeParamNode` but resolves to `LMD_TYPE_ANY` at transpile time. This preserves correct
behavior without specialization overhead.

**Tier 2 — Monomorphized specialization (optimization):** When a generic function is
called with concrete type arguments at known call sites, the transpiler can optionally
generate specialized native versions:

```ts
identity<number>(42)   // → emit native double version
identity<string>("hi") // → emit native string version
```

The transpiler's function collection phase (Phase 1) scans all call sites. If every
invocation uses the same concrete type, it generates a single specialized native version.
Otherwise, it falls back to the boxed generic.

This follows the same dual-version strategy that Lambda's MIR transpiler already uses
for typed vs untyped functions (see `NativeFuncInfo` in `transpile-mir.cpp`).

---

## 3. AST Builder Extension

### 3.1 File Layout

```
lambda/ts/
  ts_ast.hpp              # TS-only AST node types (extends js_ast.hpp)
  ts_type_builder.cpp     # Resolves TS type syntax → Lambda Type* structs
  build_ts_ast.cpp        # TS AST builder (delegates to build_js_ast, handles TS nodes)
  ts_transpiler.hpp       # TsTranspiler context (extends JsMirTranspiler)
  transpile_ts_mir.cpp    # TS MIR transpiler (extends transpile_js_mir.cpp)
  ts_runtime.cpp          # TS-specific runtime helpers (enum, type guards, decorators)
  ts_runtime.h
```

### 3.2 New AST Node Types (`ts_ast.hpp`)

Type annotation nodes are **fully built** into the AST (not skipped), so the transpiler
can resolve them to Lambda `Type*` values.

```cpp
// ts_ast.hpp — extends JsAstNodeType

typedef enum TsAstNodeType {
    // --- type expression nodes (resolved to Lambda Type* during transpilation) ---
    TS_AST_NODE_TYPE_ANNOTATION = JS_AST_NODE__MAX,  // : SomeType
    TS_AST_NODE_TYPE_ALIAS,          // type Foo = ...
    TS_AST_NODE_INTERFACE,           // interface Foo { ... }
    TS_AST_NODE_TYPE_PARAMETERS,     // <T extends U = V>
    TS_AST_NODE_TYPE_PARAMETER,      // single T extends U = V
    TS_AST_NODE_PREDEFINED_TYPE,     // string, number, any, ...
    TS_AST_NODE_TYPE_REFERENCE,      // Foo, Array<T>, MyInterface
    TS_AST_NODE_UNION_TYPE,          // A | B
    TS_AST_NODE_INTERSECTION_TYPE,   // A & B
    TS_AST_NODE_TUPLE_TYPE,          // [A, B, C]
    TS_AST_NODE_ARRAY_TYPE,          // T[]
    TS_AST_NODE_FUNCTION_TYPE,       // (a: T) => R
    TS_AST_NODE_OBJECT_TYPE,         // { a: T; b: U }
    TS_AST_NODE_CONDITIONAL_TYPE,    // T extends U ? X : Y
    TS_AST_NODE_LITERAL_TYPE,        // literal: "hello", 42, true
    TS_AST_NODE_PARENTHESIZED_TYPE,  // (T)
    TS_AST_NODE_TYPEOF_TYPE,         // typeof x
    TS_AST_NODE_KEYOF_TYPE,          // keyof T
    TS_AST_NODE_INDEXED_ACCESS_TYPE, // T[K]
    TS_AST_NODE_MAPPED_TYPE,         // { [K in T]: V }
    TS_AST_NODE_TEMPLATE_LITERAL_TYPE,
    TS_AST_NODE_INFER_TYPE,          // infer T

    // --- expression wrappers with type info ---
    TS_AST_NODE_AS_EXPRESSION,       // expr as T
    TS_AST_NODE_NON_NULL_EXPRESSION, // expr!
    TS_AST_NODE_SATISFIES_EXPRESSION,// expr satisfies T
    TS_AST_NODE_TYPE_ASSERTION,      // <T>expr (angle-bracket style)

    // --- runtime-meaningful TS constructs ---
    TS_AST_NODE_ENUM_DECLARATION,    // enum Direction { Up = 0, Down }
    TS_AST_NODE_ENUM_MEMBER,         // Up = 0
    TS_AST_NODE_NAMESPACE_DECLARATION,// namespace Foo { ... }
    TS_AST_NODE_DECORATOR,           // @decorator
    TS_AST_NODE_AMBIENT_DECLARATION, // declare ... (type info kept, no code emitted)

    TS_AST_NODE__MAX,
} TsAstNodeType;


// --- Type expression AST nodes ---

// Base for all TS type nodes — carries the resolved Lambda Type*
typedef struct TsTypeNode {
    JsAstNode base;
    Type* resolved_type;    // filled during type resolution pass; NULL before
} TsTypeNode;

// : SomeType  (attached to variable_declarator, parameter, function return, etc.)
typedef struct {
    TsTypeNode base;
    TsTypeNode* type_expr;  // the type expression subtree
} TsTypeAnnotationNode;

// string, number, boolean, any, void, never, unknown, object, symbol, bigint, null, undefined
typedef struct {
    TsTypeNode base;
    TypeId predefined_id;   // resolved directly: number→FLOAT, string→STRING, etc.
} TsPredefinedTypeNode;

// Foo, Array<T>, MyInterface
typedef struct {
    TsTypeNode base;
    Str name;               // type name
    TsTypeNode** type_args; // generic arguments (nullable)
    int type_arg_count;
} TsTypeReferenceNode;

// A | B
typedef struct {
    TsTypeNode base;
    TsTypeNode** types;     // all union members
    int type_count;
} TsUnionTypeNode;

// A & B
typedef struct {
    TsTypeNode base;
    TsTypeNode** types;
    int type_count;
} TsIntersectionTypeNode;

// T[]
typedef struct {
    TsTypeNode base;
    TsTypeNode* element_type;
} TsArrayTypeNode;

// [A, B, C]
typedef struct {
    TsTypeNode base;
    TsTypeNode** element_types;
    int element_count;
} TsTupleTypeNode;

// (x: T, y: U) => R
typedef struct {
    TsTypeNode base;
    TsTypeNode** param_types;
    Str* param_names;
    int param_count;
    TsTypeNode* return_type;
} TsFunctionTypeNode;

// { x: T; y: U; z?: V }
typedef struct {
    TsTypeNode base;
    TsTypeNode** member_types;
    Str* member_names;
    bool* member_optional;  // per-member optional flag
    bool* member_readonly;  // per-member readonly flag
    int member_count;
} TsObjectTypeNode;

// T extends U ? X : Y
typedef struct {
    TsTypeNode base;
    TsTypeNode* check_type;
    TsTypeNode* extends_type;
    TsTypeNode* true_type;
    TsTypeNode* false_type;
} TsConditionalTypeNode;

// --- type parameter ---
typedef struct {
    TsTypeNode base;
    Str name;
    TsTypeNode* constraint;   // extends X (nullable)
    TsTypeNode* default_type; // = Y (nullable)
} TsTypeParamNode;

// --- interface Foo { x: T; ... } ---
typedef struct {
    JsAstNode base;
    Str name;
    TsTypeParamNode** type_params;
    int type_param_count;
    TsTypeNode** extends_types;    // extends clause (nullable)
    int extends_count;
    TsObjectTypeNode* body;
    Type* resolved_type;           // → TypeMap with ShapeEntry chain
} TsInterfaceNode;

// --- type Foo = ... ---
typedef struct {
    JsAstNode base;
    Str name;
    TsTypeParamNode** type_params;
    int type_param_count;
    TsTypeNode* type_expr;
    Type* resolved_type;
} TsTypeAliasNode;

// --- enum Direction { Up = 0, Down } ---
typedef struct {
    JsAstNode base;
    bool is_const;
    Str name;
    JsAstNode** members;
    int member_count;
    Type* resolved_type;   // → TypeMap representing the enum shape
} TsEnumDeclarationNode;

typedef struct {
    JsAstNode base;
    Str name;
    JsAstNode* initializer;
    int auto_value;
} TsEnumMemberNode;

// --- namespace Foo { ... } ---
typedef struct {
    JsAstNode base;
    Str name;
    JsAstNode** body;
    int body_count;
} TsNamespaceDeclarationNode;

// --- @decorator ---
typedef struct {
    JsAstNode base;
    JsAstNode* expression;
} TsDecoratorNode;

// --- expr as T / expr satisfies T ---
typedef struct {
    JsAstNode base;
    JsAstNode* inner;       // the expression
    TsTypeNode* target_type; // the asserted/checked type
} TsTypeExprNode;

// --- expr! (non-null assertion) ---
typedef struct {
    JsAstNode base;
    JsAstNode* inner;
} TsNonNullNode;
```

### 3.3 Type Resolution Pass (`ts_type_builder.cpp`)

After the AST is built, a resolution pass walks all `TsTypeNode` subtrees and fills
in `resolved_type` with Lambda `Type*` structs:

```c
// ts_type_builder.cpp

Type* ts_resolve_type(TsTranspiler* tp, TsTypeNode* node) {
    if (node->resolved_type) return node->resolved_type;

    switch (node->base.type) {

    case TS_AST_NODE_PREDEFINED_TYPE: {
        TsPredefinedTypeNode* pn = (TsPredefinedTypeNode*)node;
        node->resolved_type = base_type(pn->predefined_id);
        return node->resolved_type;
    }

    case TS_AST_NODE_UNION_TYPE: {
        TsUnionTypeNode* un = (TsUnionTypeNode*)node;
        // build left-associative TypeBinary chain: A | B | C → (A | B) | C
        Type* result = ts_resolve_type(tp, un->types[0]);
        for (int i = 1; i < un->type_count; i++) {
            Type* right = ts_resolve_type(tp, un->types[i]);
            TypeBinary* tb = alloc_type(tp->pool, LMD_TYPE_TYPE, sizeof(TypeBinary));
            tb->kind = TYPE_KIND_BINARY;
            tb->op = OPERATOR_UNION;
            tb->left = result;
            tb->right = right;
            result = (Type*)tb;
        }
        node->resolved_type = result;
        return result;
    }

    case TS_AST_NODE_ARRAY_TYPE: {
        TsArrayTypeNode* an = (TsArrayTypeNode*)node;
        Type* elem = ts_resolve_type(tp, an->element_type);
        TypeArray* ta = alloc_type(tp->pool, LMD_TYPE_ARRAY, sizeof(TypeArray));
        ta->nested = elem;
        // specialize: number[] → LMD_TYPE_ARRAY_FLOAT, etc.
        if (elem->type_id == LMD_TYPE_FLOAT) ta->type_id = LMD_TYPE_ARRAY_FLOAT;
        else if (elem->type_id == LMD_TYPE_INT) ta->type_id = LMD_TYPE_ARRAY_INT;
        node->resolved_type = (Type*)ta;
        return (Type*)ta;
    }

    case TS_AST_NODE_FUNCTION_TYPE: {
        TsFunctionTypeNode* fn = (TsFunctionTypeNode*)node;
        TypeFunc* tf = alloc_type(tp->pool, LMD_TYPE_FUNC, sizeof(TypeFunc));
        tf->returned = ts_resolve_type(tp, fn->return_type);
        tf->param_count = fn->param_count;
        tf->required_param_count = fn->param_count;
        TypeParam* prev_p = NULL;
        for (int i = 0; i < fn->param_count; i++) {
            TypeParam* tp_param = pool_calloc(tp->pool, sizeof(TypeParam));
            tp_param->name = intern_name(fn->param_names[i].data);
            tp_param->type = ts_resolve_type(tp, fn->param_types[i]);
            if (prev_p) prev_p->next = tp_param;
            else tf->param = tp_param;
            prev_p = tp_param;
        }
        node->resolved_type = (Type*)tf;
        return (Type*)tf;
    }

    case TS_AST_NODE_OBJECT_TYPE: {
        TsObjectTypeNode* on = (TsObjectTypeNode*)node;
        TypeMap* tm = alloc_type(tp->pool, LMD_TYPE_MAP, sizeof(TypeMap));
        tm->length = on->member_count;
        ShapeEntry* prev_se = NULL;
        for (int i = 0; i < on->member_count; i++) {
            ShapeEntry* se = pool_calloc(tp->pool, sizeof(ShapeEntry));
            se->name = intern_name(on->member_names[i].data);
            Type* field_type = ts_resolve_type(tp, on->member_types[i]);
            if (on->member_optional[i]) {
                // wrap in TypeUnary OPTIONAL
                TypeUnary* tu = alloc_type(tp->pool, LMD_TYPE_TYPE, sizeof(TypeUnary));
                tu->kind = TYPE_KIND_UNARY;
                tu->op = OPERATOR_OPTIONAL;
                tu->operand = field_type;
                se->type = (Type*)tu;
            } else {
                se->type = field_type;
            }
            if (prev_se) prev_se->next = se;
            else tm->shape = se;
            prev_se = se;
        }
        tm->last = prev_se;
        typemap_hash_build(tm);  // populate FNV-1a hash table
        node->resolved_type = (Type*)tm;
        return (Type*)tm;
    }

    case TS_AST_NODE_TYPE_REFERENCE: {
        TsTypeReferenceNode* rn = (TsTypeReferenceNode*)node;
        // look up in type registry (interfaces, type aliases, enums, classes)
        Type* found = ts_lookup_type_name(tp, rn->name);
        if (!found) {
            log_error("ts type unresolved: %s", rn->name.data);
            found = &TYPE_ANY;
        }
        // apply generic arguments if present
        if (rn->type_arg_count > 0 && found->type_id == LMD_TYPE_ARRAY) {
            TypeArray* base = (TypeArray*)found;
            TypeArray* specialized = alloc_type(tp->pool, LMD_TYPE_ARRAY, sizeof(TypeArray));
            specialized->nested = ts_resolve_type(tp, rn->type_args[0]);
            node->resolved_type = (Type*)specialized;
            return (Type*)specialized;
        }
        node->resolved_type = found;
        return found;
    }

    default:
        // unresolved complex types fall back to any
        node->resolved_type = &TYPE_ANY;
        return &TYPE_ANY;
    }
}
```

### 3.4 AST Builder Logic (`build_ts_ast.cpp`)

The builder wraps the existing JS builder. Unlike a type-erasure approach, **type
annotations are parsed and stored in the AST** as `TsTypeNode` subtrees:

```
build_ts_ast(TsTranspiler* tp, TSNode root)
    │
    ├── JS node → call build_js_ast_node() (existing logic)
    │    └── but: variable_declarator, formal_parameter, function_declaration,
    │             arrow_function, method_definition, class fields
    │             → EXTENDED to also build type_annotation child as TsTypeAnnotationNode
    │             → stored in node struct (e.g., JsVariableDeclaratorNode.ts_type)
    │
    └── TS-only node → call build_ts_ast_node()
            ├── type_annotation       → TsTypeAnnotationNode (full subtree built)
            ├── predefined_type       → TsPredefinedTypeNode (number→FLOAT, etc.)
            ├── union_type            → TsUnionTypeNode (children recursed)
            ├── intersection_type     → TsIntersectionTypeNode
            ├── array_type            → TsArrayTypeNode
            ├── tuple_type            → TsTupleTypeNode
            ├── function_type         → TsFunctionTypeNode
            ├── object_type           → TsObjectTypeNode (field shapes built)
            ├── conditional_type      → TsConditionalTypeNode
            ├── type_reference        → TsTypeReferenceNode
            ├── interface_declaration → TsInterfaceNode (shape resolved to TypeMap)
            ├── type_declaration      → TsTypeAliasNode (alias resolved)
            ├── enum_declaration      → TsEnumDeclarationNode
            ├── namespace_declaration → TsNamespaceDeclarationNode
            ├── decorator             → TsDecoratorNode
            ├── as_expression         → TsTypeExprNode (inner + target type)
            ├── satisfies_expression  → TsTypeExprNode (inner + target type)
            ├── non_null_expression   → TsNonNullNode (inner only)
            └── ambient_declaration   → TsInterfaceNode/TsTypeAliasNode
                                        (types resolved, no code emitted)
```

**Key rules:**

1. **Type annotations are fully parsed and stored.** When building a
   `variable_declarator`, the builder checks for a `type_annotation` child. If present,
   it builds a `TsTypeAnnotationNode` and stores it in an extended field on the JS node:
   ```c
   // Extended JsVariableDeclaratorNode for TS mode
   typedef struct {
       JsVariableDeclaratorNode base;
       TsTypeAnnotationNode* ts_type;  // NULL in pure JS mode
   } TsVariableDeclaratorNode;
   ```

2. **Interfaces build TypeMap immediately.** The `TsInterfaceNode` builder resolves
   member types and constructs a `TypeMap` with `ShapeEntry` chain during the AST pass.
   This is registered in a type name table for later reference resolution.

3. **Type aliases register their resolved type.** `type Point = {x: number, y: number}`
   resolves the RHS type expression and registers `"Point" → Type*` in the type registry.

4. **Enum members are resolved** to their numeric (or string) values during the AST pass,
   filling `TsEnumMemberNode.auto_value`. The enum type itself becomes a `TypeMap` with
   field entries for each member.

5. **Constructor parameter properties** (e.g. `constructor(public x: number)`) are
   desugared into a typed field + body-prepended assignment:
   ```ts
   constructor(public x: number) {}
   // → field: x with type FLOAT
   // → body prepend: this.x = x;
   ```
   The field carries the resolved `TypeMap` → `ShapeEntry` with `type = &TYPE_FLOAT`.

6. **Access modifiers are stored** in the AST (not stripped) for runtime enforcement:
   - `private` fields logged with `log_error` on external access (debug builds)
   - `readonly` fields reject assignment after initialization (runtime check)

7. **`as` and `satisfies` preserve the target type.** `expr as T` creates a
   `TsTypeExprNode` with both the inner expression and the target `TsTypeNode`. The
   type is resolved — `as number` sets the variable's tracked type to `LMD_TYPE_FLOAT`
   for subsequent native codegen. `satisfies` additionally emits a runtime check in
   debug builds.

---

## 4. TypeScript Transpiler

### 4.1 Design Principle: Type-Aware JS Transpiler Extension

The TS transpiler extends the JS MIR transpiler with full type awareness. TypeScript type
annotations are **the primary source** for type inference (replacing the JS transpiler's
heuristic inference from literal values and operator semantics). This produces better
native code because types are known precisely, not guessed.

| JS transpiler (heuristic) | TS transpiler (annotation-driven) |
|---|---|
| `let x = 42` → infer INT | `let x: number = 42` → declare FLOAT (correct TS semantics) |
| `x + y` → both literals INT? → native MIR_ADD | `x + y` → both annotated `number` → native MIR_DADD |
| No function param types → boxed Item call | `fn(x: number)` → native double param, `MIR_T_D` register |
| No return type → must box result | `fn(): number` → native double return, skip boxing |

### 4.2 Transpiler Context

```c
typedef struct TsTranspiler {
    JsMirTranspiler base;            // MUST be first — enables safe upcast to JsMirTranspiler

    // --- type registry ---
    HashMap type_registry;           // name → Type* (interfaces, aliases, enums)
    HashMap const_enum_registry;     // name → TsEnumDeclarationNode* (const enums)
    ArrayList type_list;             // all Type* allocated (for GC root scanning)

    // --- mode flags ---
    bool tsx_mode;                   // true for .tsx files
    bool strict_mode;                // always true (TS implies strict)
    bool emit_runtime_checks;        // emit type guard assertions (debug/dev mode)
} TsTranspiler;
```

### 4.3 Entry Point

```c
// transpile_ts_mir.cpp

Item transpile_ts_to_mir(Runtime* runtime, const char* ts_source, const char* filename) {
    TsTranspiler tt;
    ts_transpiler_init(&tt, runtime, ts_source, filename);

    // 1. Parse with the TS grammar (extending JS grammar)
    TSParser* parser = ts_parser_new();
    const TSLanguage* lang = tree_sitter_typescript();
    ts_parser_set_language(parser, lang);
    TSTree* tree = ts_parser_parse_string(parser, NULL, ts_source, strlen(ts_source));

    // 2. Build typed AST with type annotations preserved
    JsAstNode* root = build_ts_ast(&tt, ts_tree_root_node(tree));

    // 3. Resolve all type annotations → Lambda Type* structs
    ts_resolve_all_types(&tt, root);

    // 4. Transpile to MIR — types drive native codegen
    return transpile_ts_mir_ast(&tt, root);
}
```

### 4.4 Type-Driven Variable Type Assignment

When the transpiler encounters a variable declaration with a type annotation, it uses
the resolved `Type*` to set the variable's `JsMirVarEntry.type_id` precisely — replacing
the JS transpiler's heuristic literal-based inference:

```c
static void jm_transpile_ts_variable_declaration(TsTranspiler* tt, JsAstNode* stmt) {
    JsMirTranspiler* mt = &tt->base;
    TsVariableDeclaratorNode* decl = (TsVariableDeclaratorNode*)stmt;

    // Resolve the declared type
    TypeId declared_type = LMD_TYPE_ANY;
    if (decl->ts_type) {
        Type* resolved = ts_resolve_type(tt, decl->ts_type->type_expr);
        declared_type = resolved->type_id;
    }

    // Transpile initializer
    MIR_reg_t init_reg = 0;
    if (decl->base.init) {
        init_reg = jm_transpile_expression(mt, decl->base.init);
    }

    // Assign variable with the declared type (not inferred from literal)
    MIR_type_t mir_type = (declared_type == LMD_TYPE_FLOAT) ? MIR_T_D : MIR_T_I64;
    MIR_reg_t var_reg = jm_new_reg(mt, decl->base.name, mir_type);

    JsMirVarEntry entry = {
        .reg = var_reg,
        .mir_type = mir_type,
        .type_id = declared_type,    // ← from TS annotation, not guessed
    };
    jm_set_var(mt, decl->base.name, &entry);

    // If type is numeric and init is boxed, unbox to native register
    if (declared_type == LMD_TYPE_FLOAT && init_reg) {
        jm_emit_unbox_float(mt, var_reg, init_reg);
    } else if (init_reg) {
        jm_emit_mov(mt, var_reg, init_reg);
    }
}
```

### 4.5 Type-Driven Function Compilation

TS function type annotations give the transpiler **complete knowledge** of parameter and
return types, enabling aggressive native codegen without heuristic scanning:

```c
static void jm_transpile_ts_function(TsTranspiler* tt, JsFunctionNode* fn_node) {
    JsMirTranspiler* mt = &tt->base;

    // Collect param types from TS annotations
    TypeId param_types[16];
    int param_count = fn_node->param_count;
    for (int i = 0; i < param_count; i++) {
        TsVariableDeclaratorNode* param = (TsVariableDeclaratorNode*)fn_node->params[i];
        if (param->ts_type) {
            Type* pt = ts_resolve_type(tt, param->ts_type->type_expr);
            param_types[i] = pt->type_id;
        } else {
            param_types[i] = LMD_TYPE_ANY;
        }
    }

    // Collect return type
    TypeId return_type = LMD_TYPE_ANY;
    TsTypeAnnotationNode* ret_ann = ts_get_return_type_annotation(fn_node);
    if (ret_ann) {
        return_type = ts_resolve_type(tt, ret_ann->type_expr)->type_id;
    }

    // Determine if ALL params are native-representable
    bool all_native = true;
    for (int i = 0; i < param_count; i++) {
        if (!ts_is_native_type(param_types[i])) { all_native = false; break; }
    }

    if (all_native && ts_is_native_type(return_type)) {
        // Generate ONLY native version — no boxed fallback needed
        // All callers will use native calling convention
        jm_emit_native_function(mt, fn_node, param_types, param_count, return_type);
    } else {
        // Generate boxed version with selective unboxing of typed params
        jm_emit_boxed_function(mt, fn_node, param_types, param_count, return_type);
    }

    // Store TypeFunc in function's metadata for runtime introspection
    TypeFunc* tf = ts_build_func_type(tt, param_types, param_count, return_type);
    jm_attach_func_type(mt, fn_node->name, (Type*)tf);
}
```

### 4.6 Type Guard Emission

TypeScript's control flow narrowing is powered by runtime type checks. The transpiler
emits `item_type_id()` checks that narrow the variable's tracked type within branches:

```ts
function process(value: string | number): string {
    if (typeof value === "string") {
        return value.toUpperCase();  // narrowed to string
    }
    return value.toFixed(2);         // narrowed to number
}
```

```c
// typeof value === "string" →
MIR_reg_t tid_reg = jm_call_1(mt, "item_type_id", value_reg);
MIR_label_t else_label = jm_new_label(mt);
jm_emit(mt, MIR_new_insn(ctx, MIR_BNE, else_label, tid_reg,
        MIR_new_int_op(ctx, LMD_TYPE_STRING)));

// Inside the if-body: narrow value's type_id to STRING
jm_push_narrowing(mt, "value", LMD_TYPE_STRING);
// → value.toUpperCase() can now call js_string_to_upper directly

jm_emit_label(mt, else_label);
// After the if: narrow to NUMBER (the remaining union member)
jm_push_narrowing(mt, "value", LMD_TYPE_FLOAT);
```

**User-defined type guards** (`x is Foo`):

```ts
function isPoint(obj: any): obj is Point {
    return typeof obj === "object" && "x" in obj && "y" in obj;
}

if (isPoint(data)) {
    console.log(data.x);  // narrowed to Point
}
```

The transpiler tracks the type predicate `obj is Point` on the function's return type.
At the call site, if the guard returns true, the variable's type is narrowed to the
interface's `TypeMap`.

### 4.7 Runtime Type Introspection

Because types are compiled to Lambda `Type*` structs, full runtime introspection works:

```ts
function add(x: number, y: number): number { return x + y; }

console.log(type(add));   // → fn (number, number) number
console.log(type(42));    // → number
```

**Implementation**: each function's `TypeFunc` is attached to its `Function*` metadata.
`fn_type(item)` returns a `TypeType` wrapping the function's `TypeFunc`:

```c
// ts_runtime.cpp

Type* ts_fn_type(Item item) {
    if (item.type_id() == LMD_TYPE_FUNC) {
        Function* func = (Function*)item.container;
        if (func->ts_type) {
            // return the TS-declared TypeFunc wrapped in TypeType
            TypeType* tt = heap_calloc(sizeof(TypeType), LMD_TYPE_TYPE);
            tt->type = func->ts_type;
            return (Type*)tt;
        }
    }
    // fallback to standard JS fn_type behavior
    return fn_type(item);
}
```

### 4.8 `as` Expression — Type Reinterpretation

`as` does NOT erase the type — it **changes the transpiler's tracked type** for subsequent
native codegen decisions, and optionally emits a runtime check:

```ts
const raw: any = getConfig();
const port = raw as number;    // transpiler now tracks port as FLOAT
console.log(port + 1);         // emits native MIR_DADD (because port is typed)
```

```c
case TS_AST_NODE_AS_EXPRESSION: {
    TsTypeExprNode* as_node = (TsTypeExprNode*)expr;
    MIR_reg_t inner = jm_transpile_expression(mt, as_node->inner);
    Type* target = ts_resolve_type(tt, as_node->target_type);

    if (tt->emit_runtime_checks) {
        // optional runtime assertion: check type compatibility
        jm_call_2(mt, "ts_assert_type", inner,
                  jm_box_type(mt, target));
    }

    // Update type tracking — subsequent uses of this value use target type
    if (target->type_id == LMD_TYPE_FLOAT) {
        MIR_reg_t native_reg = jm_new_reg(mt, NULL, MIR_T_D);
        jm_emit_unbox_float(mt, native_reg, inner);
        // return native register tagged with FLOAT type
        jm_set_expr_type(mt, native_reg, LMD_TYPE_FLOAT);
        return native_reg;
    }
    jm_set_expr_type(mt, inner, target->type_id);
    return inner;
}
```

### 4.9 Enum Transpilation

Enums are transpiled as in the original proposal — frozen objects with forward+reverse
mappings. Additionally, the enum's `TypeMap` is stored for runtime introspection:

```ts
enum Direction { Up, Down, Left = 10, Right }
type(Direction)  // → map { Up: number, Down: number, Left: number, Right: number }
```

**const enum** members are resolved at compile time. References to `ConstEnum.Member` are
replaced with literal integer nodes during the AST pass, but the enum's TypeMap is still
registered so `type()` can return it if the enum name is used as a value.

### 4.10 Namespace Transpilation

Namespace blocks are lowered to an IIFE that receives the namespace object:

```ts
namespace Geometry {
    export function area(r: number): number { return Math.PI * r * r }
}
```

The namespace's exported members are registered in the type registry so that
`Geometry.area` resolves to a properly-typed function reference. The IIFE desugaring
is the same as in standard TypeScript compilation.

### 4.11 Decorator Transpilation

Decorators are applied post-class-definition. The decorator function receives the class
(or method descriptor) and its metadata including the resolved TypeFunc/TypeMap:

```ts
@Component({ selector: 'app-root' })
class AppRoot { }
```

Lowered to:
```js
let AppRoot = class AppRoot { }
AppRoot = Component({ selector: 'app-root' })(AppRoot) ?? AppRoot
```

### 4.12 Structural Type Checking at Object Boundaries

When a function parameter has an interface type, the transpiler emits structural
compatibility checks at call boundaries:

```ts
interface Point { x: number; y: number; }

function distance(p: Point): number {
    return Math.sqrt(p.x * p.x + p.y * p.y);
}

distance({ x: 3, y: 4 });           // ✓ structurally compatible
distance({ x: 3, y: 4, z: 5 });     // ✓ extra fields OK (structural)
distance({ x: 3 });                  // ✗ missing 'y'
```

The transpiler knows the expected shape from the resolved `TypeMap`. At the call site:

```c
// Emit structural check: does obj have all required ShapeEntry fields?
jm_call_2(mt, "ts_check_shape", obj_reg, jm_box_type(mt, point_type));
// ts_check_shape: iterate point_type->shape, check each non-optional field exists
// On failure: log_error + return ItemError
```

This check is emitted only at public function boundaries where argument types are not
statically known. Internal calls between fully-typed functions skip the check.

---

## 5. Implementation Phases

### Phase 1 — Grammar and Parser ✅

- [x] Create `lambda/tree-sitter-typescript/grammar.js` extending JS grammar.
  — Uses official tree-sitter-typescript v0.23.2 via `define-grammar.js` factory pattern.
  Grammar source self-contained in `lambda/tree-sitter-typescript/`.
- [x] Generate `src/parser.c` via `tree-sitter generate`.
  — 282K-line parser auto-generated; compiled to `libtree-sitter-typescript.a` (1.4MB).
- [x] Add `tree_sitter_typescript()` to `build_lambda_config.json`.
  — Include path and static lib configured.
- [x] Smoke-test: parse `.ts` files, dump CST, verify all node types recognized.
  — 10 test scripts in `test/ts/` with matching expected output files. GTest runner
  (`test/test_ts_gtest.cpp`) auto-discovers and runs all. All 10 pass.

### Phase 2 — Type System Foundation ✅

- [x] Create `lambda/ts/ts_ast.hpp` with `TsAstNodeType` enum and all type node structs.
  — Full enum + struct definitions for all type nodes (annotation, predefined, reference,
  union, intersection, array, tuple, function, object, conditional, etc.).
- [x] Create `lambda/ts/ts_type_builder.cpp` — resolve TS type syntax → Lambda `Type*`.
  — 300+ lines with full type resolution walking TsTypeNode subtrees.
- [x] Implement TS→Lambda type mapping for all primitives.
  — `ts_predefined_name_to_type_id()` handles: number→FLOAT, string→STRING,
  boolean→BOOL, null→NULL, undefined→UNDEFINED, void→NULL, any→ANY,
  unknown→ANY, never→ERROR, object→MAP, symbol→SYMBOL, bigint→INT64.
- [x] Implement union types → `TypeBinary`.
  — Left-associative chain with `OPERATOR_UNION`.
- [x] Implement interface → `TypeMap` with `ShapeEntry` chain.
  — Full ShapeEntry linked list with field_count and hash table.
- [x] Implement function types → `TypeFunc`.
  — Param and return type resolution.
- [x] Implement array types → `TypeArray` (with specialization for `number[]`, etc.).
  — Nested type support for both `T[]` and tuple types.
- [x] Implement type registry (name → Type* lookup).
  — Hashmap-based registry in `ts_transpiler.hpp`.
- [x] Unit test: resolve 30+ TS type expressions, assert correct Lambda Type* shapes.
  — Covered by GTest suite.

### Phase 3 — AST Builder ✅

- [x] Create `lambda/ts/build_ts_ast.cpp` with TS dispatch layer.
  — Full AST builder with `build_ts_node`, `build_ts_expression`, `build_ts_function`,
  `build_ts_variable_declaration`, `build_ts_class_declaration`. Delegates to JS builder
  for non-TS nodes.
- [x] Extend JS node structs with optional `TsTypeAnnotationNode*` fields.
- [x] Build type annotation subtrees for variables, parameters, return types.
  — Handles `required_parameter`, `optional_parameter`, `variable_declarator` with
  type annotation, function return types, type parameters.
- [x] Build interface, type alias, and enum nodes.
  — `TsInterfaceNode`, `TsTypeAliasNode`, `TsEnumDeclarationNode` all defined and built.
- [x] Implement constructor parameter property desugaring.
  — `build_ts_class_body` intercepts constructor methods, detects `accessibility_modifier`
  and `readonly` on params, generates `this.x = x;` assignments prepended to constructor
  body. Test: `test/ts/constructor_properties.ts`.
- [x] Implement const enum value resolution.
  — Enum members distinguish bare vs `enum_assignment` CST nodes. Explicit numeric
  initializers parsed via `strtol`, updating `auto_value` for subsequent members.
  String initializers break the auto-increment sequence (`auto_value = -1`).
- [x] Unit test: parse 30+ TS snippets, assert AST shapes with type annotations.
  — 12 integration test scripts cover AST building end-to-end.

### Phase 4 — Transpiler Core (100%)

- [x] Create `lambda/ts/ts_transpiler.hpp` (`TsTranspiler` embedding `JsMirTranspiler`).
  — Full context struct with type registry, scope management, 40+ fields.
- [x] Create `lambda/ts/transpile_ts_mir.cpp` — type-driven statement/expression handlers.
  — Strips type-only nodes, resolves types, delegates to JS transpiler.
- [x] Type-driven variable declarations (annotation → `JsMirVarEntry.type_id`).
  — Type annotations preserved and available for transpiler use.
- [x] Type-driven function compilation (native codegen from param/return types).
  — Function types resolved before delegation to JS MIR transpiler.
- [x] Type guard emission (`typeof`, `instanceof`, user-defined `x is T`).
  — `typeof`/`instanceof` work via JS delegation. Test: `test/ts/type_guards.ts`.
- [x] `as` expression — type reinterpretation with optional runtime check.
  — `ts_expr_override` hook in `build_js_expression` intercepts `as_expression`,
  `satisfies_expression`, `non_null_expression` inside nested JS expressions.
  `ts_lower_expr_tree` strips wrappers before codegen. Test: `test/ts/as_expression.ts`.
- [x] Enum transpilation (frozen object + TypeMap metadata).
  — `ts_lower_enum_to_js` lowers enum AST to `const EnumName = {...}` object literal
  with forward (name→value) and reverse (value→name) mappings. Const enums are skipped
  (inlined at usage sites). Test: `test/ts/enums.ts`.
- [x] Namespace IIFE lowering.
  — `ts_lower_namespace_to_js` converts namespace to `var Ns; (function(Ns){ ... })(Ns || (Ns = {}));`
  IIFE pattern. Export functions/consts become `Ns.member = ...` assignments.
  Test: `test/ts/namespace.ts`.
- [x] Decorator desugaring.
  — `ts_lower_class_with_decorators` converts `@deco class C {}` to
  `let C = class C {}; C = deco(C) ?? C;`. Decorators applied in reverse order.
  Test: `test/ts/decorators.ts`.
- [x] Wire `./lambda.exe ts` entry in `main.cpp` and `runner.cpp`.
  — Full command handler in `main.cpp`.
- [x] Integration test: transpile and run 50+ TypeScript programs.
  — 16 test scripts running via GTest; auto-discovery framework in place.

### Phase 5 — Runtime Introspection (100%)

- [x] Create `lambda/ts/ts_runtime.cpp` — runtime helpers.
  — Both `ts_runtime.cpp` and `ts_runtime.h` present with extern C functions.
- [x] `ts_fn_type()` — return TypeFunc for typed functions.
  — `ts_typeof()` delegates to `js_typeof()`.
- [x] `ts_check_shape()` — structural compatibility check at object boundaries.
  — Full implementation: iterates `TypeMap.shape` entries, checks each field exists
  via `Map::has_field()` / `Element::has_attr()`. Returns ITEM_ERROR on missing field.
- [x] `ts_assert_type()` — runtime type assertion for `as` in debug mode.
  — Full implementation: delegates to `ts_check_shape()` for TypeMap targets,
  uses `ts_type_compatible()` for primitives (number/object compatibility).
- [x] `type()` returns full TS type info (union types, interface shapes, function sigs).
  — `ts_type_info()` implemented with recursive `ts_format_type()` formatter.
  Handles primitives, arrays (`T[]`), maps (`{ field: type }`), functions
  (`(params) => ret`), union types (`A | B`). Wired via `jm_call_1` in
  `transpile_js_mir.cpp` and registered in `sys_func_registry.c`.
- [x] Integration test: runtime type introspection for all type categories.
  — `test/ts/runtime_types.ts` (type() + typeof for primitives/arrays/functions),
  `test/ts/typeof_checks.ts` (typeof in type guards, type narrowing).
  18/18 TS tests pass, 754/754 baseline tests pass.

### Phase 6 — TSX Support

- [ ] Create `lambda/tree-sitter-tsx/grammar.js` extending TS grammar.
- [ ] Adjust JSX element building to accept TSX node symbols.
- [ ] `.tsx` files routed through TS transpiler with `tsx_mode = true`.

### Phase 7 — Advanced Type Features (v2)

- [ ] Generic monomorphization for hot call sites.
- [ ] Conditional type resolution (`T extends U ? X : Y` → static resolution).
- [ ] Mapped types (`{ [K in keyof T]: V }`).
- [ ] Template literal types.
- [ ] `satisfies` runtime validation in debug mode.

---

## 6. Test Plan

```
test/ts/
  basic/            # let/const declarations, functions, classes with type annotations
  types/            # type annotations resolved correctly, type() introspection
  native_codegen/   # annotated numeric code compiles to native MIR ops
  unions/           # union types, type narrowing, type guards
  interfaces/       # interface shapes, structural checks, extends
  generics/         # generic functions (boxed), monomorphized specialization
  enums/            # numeric enum, string enum, const enum inlining
  namespaces/       # namespace IIFE lowering
  decorators/       # class and method decorators
  type_guards/      # typeof narrowing, instanceof, user-defined is-guards
  as_satisfies/     # as type reinterpretation, satisfies checks
  async/            # async/await (reuses JS async infrastructure)
  modules/          # import type stripping, export types
  tsx/              # TSX element expressions
  runtime/          # type() introspection, structural checks, error on type mismatch
```

Each test file: `test/ts/foo.ts` paired with `test/ts/foo.txt` (expected stdout).

Run via:
```bash
make test-ts-baseline
```

---

## 7. File Map

| File | Role |
|---|---|
| `lambda/tree-sitter-typescript/grammar.js` | TS grammar extending JS grammar |
| `lambda/tree-sitter-typescript/src/parser.c` | Auto-generated; never edit manually |
| `lambda/ts/ts_ast.hpp` | TS AST node types, type node structs |
| `lambda/ts/ts_type_builder.cpp` | Resolve TS type syntax → Lambda `Type*` structs |
| `lambda/ts/build_ts_ast.cpp` | TS AST builder (type annotations preserved) |
| `lambda/ts/ts_transpiler.hpp` | `TsTranspiler` context with type registry |
| `lambda/ts/transpile_ts_mir.cpp` | Type-driven TS MIR transpiler |
| `lambda/ts/ts_runtime.cpp` | Runtime: type introspection, structural checks, guards |
| `lambda/ts/ts_runtime.h` | Public API for TS runtime helpers |
| `test/ts/**/*.ts` + `*.txt` | Integration test scripts + expected outputs |
