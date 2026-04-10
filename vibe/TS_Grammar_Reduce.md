# TS Grammar Reduction: External Type Parser Architecture

**Goal**: Reduce TypeScript parser from ~1.3 MB to ~500–600 KB while preserving 100% TypeScript support.

## Problem

The tree-sitter TS grammar extends JS with 113 rules. Of these, **41 rules (36%) are recursive type expression rules** (union, intersection, conditional, function, generic, array, tuple, object, mapped, etc.) that create massive parse table bloat:

| Component | JS Parser | TS Parser | TS Type Overhead |
|-----------|-----------|-----------|------------------|
| States | 1,722 | 5,601 | **+3,879 (3.3×)** |
| Large states | 326 | 994 | +668 (3.0×) |
| Symbols | 259 | 369 | +110 |
| Conflicts | 16 | 44 | **+28 type-related** |
| `__TEXT,__const` | 325 KB | 1,244 KB | **+919 KB** |
| `.a` lib size | 391 KB | 1,304 KB | +913 KB |

The type expression rules multiply states because they are deeply recursive and create ambiguities with JS expression rules (28 extra conflicts). Each conflict forces the GLR parser to fork, generating parallel state machines.

**Key insight**: Lambda's TS transpiler preserves types (builds `TsTypeNode*` trees for type checking and code generation). But type tree construction uses text-based dispatch (`strcmp` on node type strings) followed by child traversal — it doesn't depend on the grammar's internal type rule structure. This means we can replace grammar-level type parsing with a C++ recursive descent parser without changing the transpiler.

## Architecture Overview

```
Source: let x: string | number = 42;

Layer 1 — Tree-sitter grammar (JS + thin TS shell):
  Parses: [let] [x] [:] [_ts_type] [=] [42] [;]
  _ts_type is an OPAQUE external token covering "string | number"

Layer 2 — External scanner (C, ~300 lines):
  Scans _ts_type by bracket-balancing: ()  <>  []  {}
  Terminates at depth-0: , ) ] ; = || && + - * ...
  Returns byte range of type text as a single token

Layer 3 — C++ type parser (~800–1200 lines):
  Called by AST builder when it encounters _ts_type node
  Input: raw text "string | number"
  Output: TsTypeNode* tree (union of predefined string + number)
  Same TsTypeNode structs as current — downstream transpiler unchanged
```

### What's Removed from Grammar (~41 rules, ~28 conflicts)

All type expression rules:

| Category | Rules Removed |
|----------|--------------|
| Supertypes | `type`, `primary_type` |
| Binary types | `union_type`, `intersection_type` |
| Complex types | `conditional_type`, `function_type`, `constructor_type` |
| Container types | `tuple_type`, `object_type`, `array_type` |
| Named types | `generic_type`, `nested_type_identifier` |
| Special types | `mapped_type`, `mapped_type_clause`, `type_query`, `index_type_query`, `lookup_type`, `infer_type` |
| Literal types | `literal_type`, `template_literal_type`, `template_type`, `predefined_type` |
| Utility types | `parenthesized_type`, `readonly_type`, `existential_type`, `flow_maybe_type`, `optional_type`, `rest_type` |
| Type predicates | `type_predicate`, `asserts` |
| Tuple internals | `_tuple_type_member`, `tuple_parameter`, `optional_tuple_parameter` |
| Type query variants | `_type_query_expression`, `_type_query_import_expression` |
| Object type members | `property_signature`, `call_signature`, `construct_signature`, `index_signature`, `method_signature` |

Plus ~28 type-related conflicts and ~15 type-related precedence entries.

### What Stays in Grammar (~72 rules)

| Category | Rules | Purpose |
|----------|-------|---------|
| **JS base** | ~100 rules | Full JavaScript grammar (unchanged) |
| **TS declarations** | `type_alias_declaration`, `interface_declaration`, `ambient_declaration` | Declaration shells with `_ts_type` for body |
| **TS expressions** | `as_expression`, `satisfies_expression`, `non_null_expression`, `type_assertion`, `instantiation_expression` | Expression extensions, use `_ts_type` for target type |
| **TS modifiers** | `accessibility_modifier`, `override_modifier`, `abstract`, `readonly`, `declare` | Class/member modifiers (keywords only) |
| **Enum** | `enum_declaration`, `enum_body`, `enum_member`, `enum_assignment` | Full grammar — generates runtime code |
| **Namespace** | `module`, `internal_module` | Contains statements |
| **Class overrides** | Modified `class_body`, `method_definition`, `field_definition` | Add TS modifiers, use `_ts_type` for annotations |
| **Param overrides** | Modified `required_parameter`, `optional_parameter` | Add modifiers + type annotations |
| **Function overrides** | Modified `_call_signature`, `arrow_function`, function declarations | Add `_ts_type_parameters` and return type |
| **Import/export** | Modified `import_statement`, `export_statement` | Add `type` keyword |
| **Type hooks** | `type_annotation` (`: _ts_type`) | Thin wrapper, content is external |

---

## 1. Grammar Design

**Approach**: Extend from JS grammar (same as current TS grammar), but replace all type expression internals with external tokens.

### External Tokens

```js
externals: ($, previous) => previous.concat([
  $._ts_type,             // opaque type expression body
  $._ts_type_arguments,   // <Type1, Type2> in expression context
  $._ts_type_parameters,  // <T extends U, V = W> in declaration context
  $._function_signature_automatic_semicolon,
  $.__error_recovery,
]),
```

Three new external tokens:

| Token | Where Used | Scanner Behavior |
|-------|-----------|-----------------|
| `_ts_type` | After `:` in annotations, after `=` in type aliases, after `as`/`satisfies`, `extends`/`implements` types | Bracket-balance `()` `<>` `[]` `{}`; terminate at depth-0 `,` `)` `]` `;` `=` and expression operators |
| `_ts_type_arguments` | `foo<T>(x)` — generic calls, JSX generic | Scan `<...>` with lookahead disambiguation vs comparison |
| `_ts_type_parameters` | `function f<T>`, `class C<T>`, `interface I<T>` | Simple `<...>` bracket balance (unambiguous in declaration context) |

### Key Grammar Rules

```js
// Type annotation — just colon + opaque type
type_annotation: $ => seq(':', $._ts_type),

// Type alias — shell only, type body is opaque
type_alias_declaration: $ => seq(
  'type', $._type_identifier, optional($._ts_type_parameters),
  '=', $._ts_type, $._semicolon,
),

// Interface — structural shell, extends types are opaque
interface_declaration: $ => seq(
  'interface', $._type_identifier, optional($._ts_type_parameters),
  optional($.extends_type_clause),
  $.interface_body,
),

// Interface body stays in grammar (not externalized) because member
// signatures share structure with JS class members. Type parts use _ts_type.
interface_body: $ => seq('{', repeat(choice(
  $._interface_member, ';', ','
)), '}'),

_interface_member: $ => seq(
  repeat(choice('readonly', $.accessibility_modifier)),
  choice(
    // Property/method: name (params): type
    seq($._property_name, optional('?'),
        optional($._ts_type_parameters),
        optional(field('parameters', $.formal_parameters)),
        optional($.type_annotation)),
    // Index signature: [key: type]: type
    seq('[', $.identifier, ':', $._ts_type, ']', $.type_annotation),
    // Call signature: (params): type
    seq(optional($._ts_type_parameters),
        field('parameters', $.formal_parameters),
        optional($.type_annotation)),
    // Construct signature: new (params): type
    seq('new', optional($._ts_type_parameters),
        field('parameters', $.formal_parameters),
        optional($.type_annotation)),
  ),
),

// Expression extensions
as_expression: $ => prec.left('binary_relation', seq(
  $.expression, 'as', choice('const', $._ts_type),
)),

satisfies_expression: $ => prec.left('binary_relation', seq(
  $.expression, 'satisfies', $._ts_type,
)),

// Enum stays fully in grammar (generates runtime code)
enum_declaration: $ => seq(
  optional('const'), 'enum', $.identifier, $.enum_body,
),

// Call expression with optional type arguments
call_expression: ($, previous) => choice(
  prec('call', seq(
    field('function', choice($.expression, $.import)),
    optional($._ts_type_arguments),
    field('arguments', $.arguments),
  )),
  // ... template call, optional chain call
),

// Function signature with optional type parameters and return type
_call_signature: $ => seq(
  optional($._ts_type_parameters),
  field('parameters', $.formal_parameters),
  optional(choice($.type_annotation, $.type_predicate_annotation)),
),
```

### Conflicts

With type expression rules removed, all ~28 type-expression conflicts are eliminated. Remaining conflicts:

- 16 JS-inherited conflicts (unchanged)
- ~3–5 TS structural conflicts:
  - `[$.accessibility_modifier, $.primary_expression]`
  - `[$.override_modifier, $.primary_expression]`
  - `[$.construct_signature, $._property_name]`
  - `[$.extends_clause, $.primary_expression]`

**Total: ~19–21 conflicts** vs current 44.

---

## 2. External Scanner Design

The scanner extends the existing JS scanner (`scanner.h`). New token types are added to the `TokenType` enum.

### `_ts_type` Scanner

Triggered when `valid_symbols[TS_TYPE]` is true. The grammar ensures this only happens in type positions.

```
Algorithm:
  depth = 0
  skip whitespace
  loop:
    ch = lookahead
    if ch in ( < [ {  → depth++, advance, continue
    if ch in ) > ] }  → if depth == 0: stop; else depth--, advance
                          if ch was } and depth == 0: mark_end, check continuation
    if depth == 0:
      if ch in , ; =    → stop (structural delimiter)
      if ch is = and next is >  → include => (function type arrow after close-paren)
      if ch in || && ?? + - * / % ** == === != !== instanceof in
                         → stop (expression-only operator)
      if ch is | or &    → include (union/intersection type operator)
      if ch is [ and next is ]  → include [] (array type suffix)
    advance, mark_end
  return TS_TYPE
```

**Key rules at depth 0**:
- `|`, `&` → **continue** (union/intersection — these ONLY appear as type operators in type context)
- `=>` → **continue only after `)` preceded scan** (function type return arrow)
- `{` → **increase depth** (object type — always valid in type position)
- After `}` returns to depth 0 → stop unless next is `|`, `&`, `[` (type continuation)
- `||`, `&&`, `+`, `-`, `*`, `/` → **stop** (expression-only operators)
- `,`, `)`, `]`, `;`, `=` → **stop** (structural delimiters)
- `>` at depth 0 → **stop** (not inside `<>`, must be comparison)

### `_ts_type_arguments` Scanner — `<>` Disambiguation

The hardest part. Triggered in expression context where `<` could be comparison or type arguments.

```
Algorithm:
  if lookahead != '<': return false
  save position
  advance  // consume <
  depth = 1
  scan_type_content:
    bracket-balance < > ( ) [ ] { } handling >> as double-close
    if depth reaches 0 (closing >):
      look at next non-whitespace char
      if next in ( ` . ?. , ) ] ; } = : → commit as type arguments
      else → restore position, return false (it's comparison)
    if hit ; or invalid char at depth > 0 → restore, return false
  return TS_TYPE_ARGUMENTS
```

This follows the TypeScript compiler's approach: speculatively scan `<...>`, and commit based on what follows.

### `_ts_type_parameters` Scanner

Simple — only appears in declaration context (after `function name`, `class name`, `interface name`) where `<` is unambiguous.

```
Algorithm:
  if lookahead != '<': return false
  advance, depth = 1
  bracket-balance until depth 0
  mark_end, return TS_TYPE_PARAMETERS
```

### Scanner Size Estimate

~300–400 lines of C. The bracket-balancing core is ~100 lines; each token handler is ~50–80 lines. String/template literal handling adds ~50 lines (skip over quoted content inside types).

---

## 3. C++ Type Parser

A recursive descent parser called by the AST builder when it encounters `_ts_type`, `_ts_type_arguments`, or `_ts_type_parameters` tokens.

### Input/Output

```cpp
// Entry points — called from AST builder
TsTypeNode* parse_ts_type(JsTranspiler* tp, const char* text, int len);
TsTypeAnnotationNode* parse_ts_type_annotation(JsTranspiler* tp, const char* text, int len);
TsTypeParamsNode* parse_ts_type_parameters(JsTranspiler* tp, const char* text, int len);
TsTypeArgsNode* parse_ts_type_arguments(JsTranspiler* tp, const char* text, int len);
```

### Type Expression Grammar (Recursive Descent)

```
type            = union_type
union_type      = intersection_type ('|' intersection_type)*
intersection_type = primary_type ('&' primary_type)*
primary_type    = atom_type postfix*
postfix         = '[]' | '[' type ']'    (array / lookup)

atom_type       = predefined_type        (string, number, boolean, any, void, ...)
                | IDENTIFIER             (type reference)
                | IDENTIFIER '.' IDENTIFIER ('.' IDENTIFIER)*  (nested type)
                | atom_type '<' type (',' type)* '>'           (generic)
                | '(' param_list ')' '=>' type                 (function type)
                | '[' type (',' type)* ']'                     (tuple)
                | '{' member (';'|',')* '}'                    (object type)
                | '(' type ')'                                 (parenthesized)
                | 'typeof' expr                                (type query)
                | 'keyof' type                                 (index type query)
                | 'infer' IDENTIFIER                           (infer type)
                | 'readonly' type                              (readonly)
                | type 'extends' type '?' type ':' type        (conditional)
                | literal                                      (literal type: "foo", 42, true, false, null)
                | template_literal                             (template literal type)
                | 'unique' 'symbol'
                | 'const'
                | 'new' '(' param_list ')' '=>' type           (constructor type)
                | '{' '[' IDENT 'in' type ']' ... '}'          (mapped type)
```

### TsTypeNode Compatibility

The C++ parser produces the **exact same `TsTypeNode*` structures** as the current CST-walking code:

```cpp
// Current approach (CST walking — build_js_ast.cpp:3004):
static TsTypeNode* build_ts_type_expr_u(JsTranspiler* tp, TSNode node) {
    const char* type_str = ts_node_type(node);
    if (strcmp(type_str, "union_type") == 0) return build_ts_union_type_u(tp, node);
    if (strcmp(type_str, "array_type") == 0) return build_ts_array_type_u(tp, node);
    // ... 12 more dispatches
}

// New approach (C++ recursive descent):
TsTypeNode* parse_ts_type(JsTranspiler* tp, const char* text, int len) {
    TsTypeParser parser(tp, text, len);
    return parser.parse_union_type();  // same TsTypeNode* output
}
```

Memory allocation uses Lambda's `pool_calloc()` (same as current). All downstream transpiler code (type resolution, code generation, MIR emission) remains unchanged.

### AST Builder Integration

Minimal changes — replace CST-walking type dispatch with text-based parse:

```cpp
// In build_ts_type_annotation_u():
// BEFORE: walk CST child nodes to get type
an->type_expr = build_ts_type_expr_u(tp, ts_node_named_child(node, 0));

// AFTER: extract text from opaque _ts_type token, call C++ parser
int len;
const char* text = ts_node_text_util(tp, node, &len);
an->type_expr = parse_ts_type(tp, text, len);
```

The same pattern applies for `as_expression`, `satisfies_expression`, type parameters, and type arguments.

### Size Estimate

~800–1200 lines of C++. Compiles to ~15–25 KB object code (no tables — pure recursive descent).

---

## 4. Implementation Plan

### Phase A: Grammar + Scanner — Validate Size Hypothesis

1. Create new grammar `define-grammar-v2.js` extending JS, with external tokens
2. Implement external scanner `scanner_v2.h` (bracket-balancing + `<>` disambiguation)
3. Generate parser, measure size
4. **Goal**: Confirm parser lib ≤ 600 KB before investing in type parser

### Phase B: C++ Type Parser

5. Implement recursive descent type parser producing `TsTypeNode*` trees
6. Integrate with AST builder (swap CST-walking for text-based parsing)
7. Run full TS test suite
8. **Goal**: All 567 baseline tests pass

### Phase C: Edge Cases + Hardening

9. Test with complex TS patterns: conditional types, mapped types, template literal types, typeof queries, infer
10. Error recovery — graceful degradation on malformed types
11. Performance validation — type re-parsing overhead should be negligible

---

## Risk Analysis

| Risk | Severity | Mitigation |
|------|----------|------------|
| Scanner termination edge cases (`=>` after `)`, `{` as object type vs block) | Medium | `{` always starts object type in type position; `=>` continues only after `)` |
| `<>` disambiguation false positives/negatives | Medium | Follow TypeScript compiler's lookahead strategy; test with complex generic chains |
| Interface body member parsing complexity | Low | Keep member structure in grammar; only type annotations are external |
| C++ type parser doesn't cover all TS type forms | Medium | Start with the 12 types the current AST builder handles; add incrementally |
| Double parsing overhead (scanner + type parser) | Low | Scanner is O(n) bracket counting; type parser is O(n) recursive descent; negligible vs I/O |
| `extends` type clause — `{` could be object type or interface body | Low | Interface `extends` only permits named types; `{` always terminates the extends clause |

---

## Size Estimates

| Component | Current | Estimated | Savings |
|-----------|---------|-----------|---------|
| Parse table states | 5,601 | ~2,000–2,400 | −57% |
| Symbols | 369 | ~280 | −24% |
| Conflicts | 44 | ~20 | −55% |
| `parser.c` const data | 1,244 KB | ~400–500 KB | −60% |
| **`libtree-sitter-typescript.a`** | **1,304 KB** | **~500–600 KB** | **−54–62%** |
| C++ type parser (new) | 0 | ~20 KB | — |
| **Total TS parsing** | **1,304 KB** | **~520–620 KB** | **−52–60%** |

### Estimate Methodology

- JS baseline: 1,722 states / 259 symbols / 391 KB lib
- TS structural overhead: ~300–700 states for enum, interface shells, class modifiers, parameter variants, import/export type, expression extensions, type annotation hooks
- Total: ~2,000–2,400 states / ~280 symbols
- Large state count ≈ 20% of total (JS ratio: 326/1722 = 19%) → ~400–480
- Dense table: 460 × 280 × 2 bytes ≈ 258 KB
- With sparse table + metadata: ~400–500 KB total const data
- Lib overhead: ~50–100 KB (code, scanner, symbols) → **~500–600 KB**

---

## Decision: Extend from JS Grammar

**Recommendation**: Create a new grammar file extending the JS grammar, rather than modifying the existing TS grammar.

**Reasons**:
1. **Clean baseline** — Start from the known-good 391 KB JS parser; measure each TS addition's impact
2. **No vestigial rules** — Modifying the existing 1149-line grammar risks leaving behind dead type rules or unused conflicts
3. **Incremental validation** — Can build and test after each major rule addition, catching regressions early
4. **Safe migration** — Current grammar remains as working fallback during development

The TS structural overrides (class modifications, parameter types, import/export) are ~600 lines — manageable to re-implement from the existing code. The 41 type expression rules (~400 lines) are simply not written.

### File Layout

```
lambda/tree-sitter-typescript/
  define-grammar.js      ← current grammar (kept as backup)
  define-grammar-v2.js   ← new grammar (extends JS + external tokens)
  scanner.h              ← current scanner (kept)
  scanner_v2.h           ← new scanner (extends JS scanner + type scanning)
  src/scanner.c          ← updated to delegate to scanner_v2.h
  src/parser.c           ← regenerated from v2 grammar

lambda/ts/
  ts_type_parser.cpp     ← C++ recursive descent type parser (~700 lines)
  ts_type_parser.hpp     ← header

lambda/js/
  build_js_ast.cpp       ← modified to call ts_type_parser for _ts_type nodes
```

---

## Background: Phase 1+2 Results (Incremental Optimization)

Before this proposal, we reduced the parser via grammar tweaks (details in `TS_Grammar_Reduce_Phase1_2.md`):

| Metric | Original | After Phase 1+2 | Change |
|--------|----------|-----------------|--------|
| States | 5,870 | 5,601 | −4.6% |
| Large states | 1,193 | 994 | −16.7% |
| Symbols | 376 | 369 | −7 |
| Lib size | 1,456 KB | 1,304 KB | −8.4% |

**Phase 1**: Tokenized `predefined_type` with `token()`, removed 2 conflicts/precedences. (−6.1% lib)
**Phase 2**: Unified 6 `_type_query_*` rules into 2. (−2.4% lib)

---

## Phase A Results: Grammar + Scanner Validation ✅

Phase A implemented and validated. All type expression rules externalized into 3 opaque scanner tokens.

### Size Results

| Metric | Original | Phase 1+2 | **Phase A (v2)** | Total Change |
|--------|----------|-----------|------------------|-------------|
| States | 5,870 | 5,601 | **4,084** | **−30%** |
| Large states | 1,193 | 994 | **723** | **−39%** |
| Symbols | 376 | 369 | **330** | **−12%** |
| `__TEXT,__const` | — | 1,244 KB | **792 KB** | **−36%** |
| **Lib size** | **1,456 KB** | **1,304 KB** | **856 KB** | **−41%** |

The 856 KB is above the 500–600 KB estimate. The delta from JS baseline (391 KB) is 465 KB — TS structural rules (class modifiers, parameter variants, interface body, enum, import/export extensions, namespace) still add significant state count (2,362 states above JS). This is expected: these rules contain many optional modifiers and `choice()` variants that generate combinatorial states.

### Test Results

- **565/567 baseline tests pass (99.6%)**
- **19/19 TypeScript tests pass** (100%)
- 2 pre-existing JS test failures (template_literals, phase3_polyfills — unrelated to v2 changes)

### Implementation Files

- `define-grammar-v2.js` — v2 TS grammar extending JS with 3 external tokens (~650 lines)
- `scanner_v2.h` — v2 scanner with bracket-balancing type scanning (~600 lines)
- `grammar.js` — updated to require `define-grammar-v2`
- `src/scanner.c` — updated to include `scanner_v2.h`

### Key Scanner Design Decisions

1. **`brace_starts_type` flag**: Prevents `{` at depth 0 from being consumed as object type when it's actually a function body. `true` at type start and after type operators (`|`, `&`, `=>`, `?`); `false` after identifiers, numbers, closing brackets.
2. **`_ts_type` aliased to `$.type`**: All grammar usages wrap with `alias($._ts_type, $.type)` so the token appears as a named child in the CST, enabling the AST builder to find it via `ts_node_named_child()`.
3. **Opaque type fallback**: `build_ts_type_expr_u()` receives "type" as node type, hits fallback case returning `LMD_TYPE_ANY`. This is acceptable for Phase A — types are erased at runtime. Phase B (C++ type parser) will produce proper `TsTypeNode*` trees.

---

## Phase B Results: C++ Type Parser ✅

Phase B implemented a full recursive descent C++ type parser that converts opaque type text tokens into proper `TsTypeNode*` AST trees, restoring full type information for the transpiler.

### Implementation

- **`lambda/ts/ts_type_parser.cpp`** (~700 lines) — Recursive descent parser
- **`lambda/ts/ts_type_parser.hpp`** — Header declaring `ts_parse_type_text()`
- **`lambda/js/build_js_ast.cpp`** — Modified: `build_ts_type_expr_u()` dispatches "type" nodes to `ts_parse_type_text()`

### Parser Grammar

```
type → conditional_type
conditional_type → union_type ['extends' type '?' type ':' type]
union_type → intersection_type ('|' intersection_type)*
intersection_type → unary_type ('&' unary_type)*
unary_type → ['keyof' | 'typeof' | 'readonly' | 'infer' | 'unique'] postfix_type
postfix_type → primary_type ('[]' | '[' type ']')*
primary_type → predefined | type_reference | function_type | tuple | object |
               parenthesized | literal | template_literal
```

### Type Constructs Supported

| Category | Constructs |
|----------|-----------|
| Predefined | `string`, `number`, `boolean`, `void`, `any`, `never`, `null`, `undefined`, `unknown`, `object`, `symbol`, `bigint` |
| References | `MyType`, `A.B.C`, `Map<K, V>`, `Array<Set<Map<number, string>>>` |
| Union/Intersection | `A \| B \| C`, `A & B` |
| Arrays | `T[]`, `T[][]` |
| Indexed access | `T[K]`, `T[keyof T]` |
| Tuples | `[A, B, C]`, `[string, ...number[]]` |
| Object types | `{ x: T; y?: U }`, `{ readonly [K in keyof T]: V }` |
| Function types | `(x: T) => R`, `((x: T) => R)` (parenthesized) |
| Conditional | `T extends U ? X : Y`, nested conditionals (4+ levels) |
| Literal types | `"hello"`, `42`, `true`, `false`, `null` |
| Template literals | `` `on${string}Changed` `` |
| Unary | `keyof T`, `typeof x`, `readonly T[]`, `infer R`, `unique symbol` |

### Scanner Fixes During Phase B

1. **`conditional_depth` counter**: Fixed `:` inside conditional types (`T extends U ? X : Y`). The `:` between true/false branches was incorrectly terminating the type scan. `?` now increments `conditional_depth`; `:` checks it before breaking.

2. **`>` inside brackets**: Fixed `>` at `depth > 0` (inside `()` `[]` `{}`) incorrectly ending the type scan. This broke parenthesized function types like `((a: string) => boolean)` where `=>` inside outer parens had the `>` terminate the scan.

### Test Results

- **565/567 baseline tests pass** (same as Phase A — no regressions)
- **19/19 TypeScript tests pass** (100%)
- 2 pre-existing JS test failures (template_literals, phase3_polyfills — unrelated)
- Stress tested: nested generics (4 levels deep), nested conditionals (5 levels), complex mapped types, intersection of function+object types, union of tuples, template literal types, `infer` patterns, `keyof`/`typeof` in types

### Known Pre-Existing Limitations (Not Phase B Issues)

- Parenthesized ternary `(cond ? a : b)` fails at tree-sitter parse level (JS grammar issue)
- `as` expression inside ternary consequent crashes due to null type in ternary handler (AST builder issue, no null check on `cond->consequent->type`)

### Architecture Summary

```
Source: type Result<T> = T extends string ? "str" : T extends number ? "num" : "other";

Tree-sitter → external scanner → opaque "_ts_type" token text:
  "T extends string ? \"str\" : T extends number ? \"num\" : \"other\""

ts_type_parser.cpp → recursive descent → TsTypeNode* tree:
  TsConditionalTypeNode {
    check: TsTypeParamNode("T")
    extends_type: TsPredefinedTypeNode(STRING)
    consequence: TsLiteralTypeNode("str")
    alternate: TsConditionalTypeNode {
      check: TsTypeParamNode("T")
      extends_type: TsPredefinedTypeNode(NUMBER)
      consequence: TsLiteralTypeNode("num")
      alternate: TsLiteralTypeNode("other")
    }
  }
```
