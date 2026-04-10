# Proposal: Reduce TypeScript Parser Size

**Goal**: Reduce `libtree-sitter-typescript.a` from 1.4 MB while maintaining 100% TypeScript language support.

## Current State

| Metric | Value |
|--------|-------|
| `parser.c` | 282,437 lines (8.3 MB source) |
| `libtree-sitter-typescript.a` | 1.4 MB |
| `STATE_COUNT` | 5,870 |
| `LARGE_STATE_COUNT` | 1,193 |
| `SYMBOL_COUNT` | 376 |
| `TOKEN_COUNT` | 166 |
| Conflict sets | 48 (16 from JS + 32 from TS) → 46 after Phase 1 |
| Total grammar rules | 229 |
| Type-related rules | 48 |
| `_type_query_*` variant rules | 6 |

### Size Breakdown (parser.c)

| Section | Lines | % |
|---------|-------|---|
| Large parse table (dense) | 119,292 | 42.2% |
| Small parse table (sparse) | 134,870 | 47.8% |
| Small table map | 4,680 | 1.7% |
| Parse actions | 4,797 | 1.7% |
| Lex modes | 5,873 | 2.1% |
| Primary state IDs | 5,888 | 2.1% |
| Lexer + keyword lexer | 2,563 | 0.9% |
| Symbol metadata | 4,474 | 1.6% |

**Parse tables = 90% of file size.** All optimization must target STATE_COUNT and SYMBOL_COUNT reduction.

The large parse table is a dense 2D array: `uint16_t[LARGE_STATE_COUNT][SYMBOL_COUNT]` = 1,193 × 376 = 448K entries. Every symbol removed saves 1,193 entries; every large state eliminated saves 376 entries.

---

## Strategy 1: `predefined_type` as Token

### Current

```js
predefined_type: _ => choice(
  'any', 'number', 'boolean', 'string', 'symbol',
  alias(seq('unique', 'symbol'), 'unique symbol'),
  'void', 'unknown', 'string', 'never', 'object',
),
```

This creates **10 separate keyword tokens** (`any`, `number`, `boolean`, `string`, `symbol`, `void`, `unknown`, `never`, `object`, plus the `unique symbol` seq). Each keyword is a distinct terminal symbol in the parse table, and the parser must track transitions for each one independently across all 5,870 states.

Note: `string` appears twice (bug in upstream grammar) — minor but worth fixing.

### Proposed

```js
predefined_type: _ => token(choice(
  'any', 'number', 'boolean', 'string', 'symbol',
  seq('unique', /\s+/, 'symbol'),
  'void', 'unknown', 'never', 'object',
)),
```

Wrapping in `token()` collapses the choice into a **single terminal symbol** in the parse table. The lexer handles the keyword discrimination internally (a trie/switch, already efficient), but the parser sees only one `predefined_type` token.

### Impact

- **SYMBOL_COUNT**: −9 tokens (from 10 individual keywords consolidated into 1)
- **Large table savings**: 9 × 1,193 = 10,737 fewer entries (direct), ~21 KB
- **Indirect savings**: Fewer distinct tokens in ambiguous positions → fewer parser states → cascading reductions in both parse tables
- **Conflict reduction**: The 2 conflicts involving `predefined_type` (`[primary_expression, predefined_type]` and `[primary_expression, predefined_type, rest_pattern]`) may be eliminable since the parser no longer needs to distinguish individual keywords at the grammar level

### AST Builder Change

The AST builder already reads predefined_type via text extraction:
```cpp
// build_js_ast.cpp:2803
static TsTypeNode* build_ts_predefined_type_u(JsTranspiler* tp, TSNode node) {
    int len;
    const char* text = ts_node_text_util(tp, node, &len);
    pn->predefined_id = ts_predefined_name_to_type_id(text, len);
    return (TsTypeNode*)pn;
}
```
**No AST builder changes needed** — it already resolves by text, not by which grammar alternative matched.

### Caveats

- The `_reserved_identifier` rule also lists 6 of these keywords (`any`, `number`, `boolean`, `string`, `symbol`, `object`). After tokenization, tree-sitter's keyword scanner won't auto-promote these to `predefined_type` — need to verify the keyword lexer interaction. May need to keep these in `_reserved_identifier` and handle in AST builder.
- `type_predicate` uses `alias($.predefined_type, $.identifier)` — after tokenizing, the alias still works since `predefined_type` remains a named node.
- `_type_query_subscript_expression` uses `predefined_type` as an index type — the token approach still works fine here.

### Risk: Low

This is the safest change. The lexer already knows how to match these keywords; we're just preventing them from bloating the parser table.

---

## Strategy 2: Unify `_type_query_*` Variant Rules

### Current

There are **6 `_type_query_*` rules** that duplicate expression grammar structure with slight variations:

| Rule | Purpose | # Choices for object/function |
|------|---------|------|
| `_type_query_member_expression` | `typeof a.b` | 5 (identifier, this, subscript, member, call) |
| `_type_query_subscript_expression` | `typeof a[k]` | 5 |
| `_type_query_call_expression` | `typeof a()` | 4 (import, identifier, member, subscript) |
| `_type_query_instantiation_expression` | `typeof a<T>` | 4 |
| `_type_query_member_expression_in_type_annotation` | `import('x').y` in type position | 3 (import, member, call) |
| `_type_query_call_expression_in_type_annotation` | `import('x')()` in type position | 2 |

These exist because type query expressions (`typeof expr`) accept a restricted subset of expressions — not arbitrary expressions. The grammar duplicates member/subscript/call rules specifically for this context.

### Problem

Each `_type_query_*` rule creates its own **set of parser states** that parallel the regular expression states. They also introduce:
- 6 additional entries in the precedence table
- 2 additional conflict sets
- Recursive alias chains (each variant references others via `alias`)

### Proposed: Merge into 2 Rules

```js
// Unified type query expression — covers member, subscript, call, instantiation
_type_query_expression: $ => choice(
  $.identifier,
  $.this,
  seq(  // member access
    field('object', $._type_query_expression),
    choice('.', '?.'),
    field('property', choice(
      $.private_property_identifier,
      alias($.identifier, $.property_identifier),
    )),
  ),
  seq(  // subscript access
    field('object', $._type_query_expression),
    optional('?.'),
    '[', field('index', choice($.predefined_type, $.string, $.number)), ']',
  ),
  seq(  // call expression
    field('function', choice($.import, $._type_query_expression)),
    field('arguments', $.arguments),
  ),
  seq(  // instantiation
    field('function', choice($.import, $._type_query_expression)),
    field('type_arguments', $.type_arguments),
  ),
),

// Import type annotation variant (simpler, only from import)
_type_query_import_expression: $ => choice(
  seq(
    field('object', choice($.import, $._type_query_import_expression)),
    '.', field('property', choice($.private_property_identifier, alias($.identifier, $.property_identifier))),
  ),
  seq(
    field('function', choice($.import, $._type_query_import_expression)),
    field('arguments', $.arguments),
  ),
),
```

The AST builder then classifies the unified `_type_query_expression` into member/subscript/call/instantiation based on structure (already trivial — check last child).

### Impact

- **Rules reduced**: 6 → 2
- **Estimated state reduction**: ~200–500 states (these rules create parallel state machines)
- **Conflicts reduced**: Remove ~4 `_type_query_*` precedence/conflict entries
- **Parse table impact**: Fewer states × fewer symbols = significant reduction in both tables

### AST Builder Change

Add a classifier in `build_js_ast.cpp` that inspects the children of the unified node:
```cpp
// If last child is '.property' -> member_expression
// If last child is '[index]' -> subscript_expression
// If last child is '(args)' -> call_expression  
// If last child is '<type_args>' -> instantiation_expression
```

### Risk: Medium

The `alias()` calls in the current grammar ensure these nodes appear as `member_expression`, `subscript_expression`, etc. in the CST. After merging, the AST builder must reconstruct this classification. Need thorough testing with type query expressions.

---

## Strategy 3: Simplify Type Grammar — Delegate Complex Parsing to AST Builder

### 3a. Flatten `primary_type` / `type` Hierarchy

Currently, `type` is a 7-way choice, and `primary_type` is a **20-way choice**. Several `primary_type` alternatives are recursive through `type`:

```
primary_type → union_type → type → primary_type (cycle)
primary_type → intersection_type → type → primary_type (cycle)  
primary_type → conditional_type → type → primary_type (cycle)
primary_type → lookup_type → type → primary_type (cycle)
```

These cycles multiply parser states. Flatten by moving `union_type`, `intersection_type`, `conditional_type` out of `primary_type` and into `type` only:

```js
// Simplified primary_type — no recursive type references
primary_type: $ => choice(
  $.parenthesized_type,
  $.predefined_type,          // now a single token
  $._type_identifier,
  $.nested_type_identifier,
  $.generic_type,
  $.object_type,
  $.array_type,
  $.tuple_type,
  $.flow_maybe_type,
  $.type_query,
  $.index_type_query,
  alias($.this, $.this_type),
  $.existential_type,
  $.literal_type,
  $.template_literal_type,
  'const',
),

// type gains the binary/ternary type operators
type: $ => choice(
  $.primary_type,
  $.function_type,
  $.readonly_type,
  $.constructor_type,
  $.infer_type,
  $.union_type,             // moved from primary_type
  $.intersection_type,      // moved from primary_type
  $.conditional_type,       // moved from primary_type
  $.lookup_type,            // moved from primary_type
  prec(-1, alias($._type_query_member_expression_in_type_annotation, $.member_expression)),
  prec(-1, alias($._type_query_call_expression_in_type_annotation, $.call_expression)),
),
```

**Impact**: Breaks the recursive cycle through `primary_type`, reducing states. The `array_type` rule (`primary_type '[' ']'`) and similar rules that need `primary_type` still work correctly since `union_type` etc. should be parenthesized for `array_type` anyway.

### 3b. Simplify `object_type` Internal Parsing

Currently `object_type` fully parses its members as `property_signature | call_signature | construct_signature | index_signature | method_signature`. Each of these has complex rules with optional modifiers.

Proposed: Parse `object_type` members more loosely as sequences of tokens delimited by `;`/`,`/`}`, then let the AST builder classify:

```js
// Simpler object_type — delegates member classification to AST builder
object_type: $ => seq(
  choice('{', '{|'),
  optional(seq(
    optional(choice(',', ';')),
    sepBy1(
      choice(',', $._semicolon),
      $._object_type_member,
    ),
    optional(choice(',', $._semicolon)),
  )),
  choice('}', '|}'),
),

_object_type_member: $ => seq(
  repeat(choice(
    $.accessibility_modifier,
    'static', 'readonly', 'abstract', 'override',
    choice('get', 'set', '*'),
    'new', 'async',
    optional('?'),
  )),
  choice(
    // property/method: name + optional params + optional type
    seq(field('name', $._property_name), optional('?'),
        optional($._call_signature),
        optional($.type_annotation)),
    // index signature: [key: type]: type
    seq('[', $.identifier, ':', $.type, ']', $.type_annotation),
    // call signature: (params) => type
    $._call_signature,
  ),
),
```

**Impact**: Eliminates 5 separate member rules (`property_signature`, `call_signature`, `construct_signature`, `index_signature`, `method_signature`) from the parser's perspective, significantly reducing states in the `{...}` context.

**Risk: High** — Changing the CST shape affects all code that reads object type members. Requires careful AST builder changes.

### 3c. Reduce Conflicts via Common Prefix Extraction

Many conflicts arise from shared syntax between expressions and types:
- `[x, y]` could be array literal or tuple type
- `{a: b}` could be object literal or object type
- `string` could be identifier or predefined_type
- `(x) => y` could be arrow function or function type

Current approach: 48 conflict sets tell the parser to fork and try both. Each conflict multiplies states.

Proposed: where possible, parse the common prefix as a single rule, then use a follow-set discriminator:

```js
// Example: array/tuple ambiguity — parse as generic bracketed_list,
// then AST builder distinguishes based on contents
_bracketed_list: $ => seq('[', commaSep(choice(
  $._tuple_type_member,
  $.expression,
  $.spread_element,
)), optional(','), ']'),
```

Then remove conflicts:
```
[$.array, $.tuple_type]
[$.array, $.array_pattern, $.tuple_type]
[$.array_pattern, $.tuple_type]
```

**Impact**: Each conflict removed can eliminate hundreds of states. The 48→~35 conflict reduction could save 10–20% of states.

**Risk: High** — Changes node structure in CST. All consumers must be updated.

---

## Implementation Plan

### Phase 1: Low Risk, Quick Win — COMPLETED

1. ✅ **Tokenize `predefined_type`** — wrapped in `token(choice(...))`
2. ✅ **Fix duplicate `'string'`** in `predefined_type`
3. ✅ **Remove 2 conflicts** — `[$.primary_expression, $.predefined_type, $.rest_pattern]` and `[$.primary_expression, $.predefined_type]` no longer needed after tokenization
4. ✅ **Remove 2 precedences** — `[$.predefined_type, $.unary_expression]` and `[$.predefined_type, $.pattern]` no longer needed
5. ❌ **Tokenize `accessibility_modifier`** — REJECTED, see lessons learned below
6. ✅ All 567 baseline tests pass (including 19/19 TypeScript tests)

#### Phase 1 Results

| Metric | Before | After | Change |
|--------|--------|-------|--------|
| `STATE_COUNT` | 5,870 | 5,827 | −43 (−0.7%) |
| `LARGE_STATE_COUNT` | 1,193 | 1,011 | −182 (−15.3%) |
| `SYMBOL_COUNT` | 376 | 373 | −3 |
| `TOKEN_COUNT` | 166 | 164 | −2 |
| `parser.c` bytes | 8,745,894 | 8,572,838 | −173 KB (−2.0%) |
| **`libtree-sitter-typescript.a`** | **1,456,192** | **1,366,872** | **−89 KB (−6.1%)** |

The LARGE_STATE_COUNT reduction (−15.3%) is the biggest win — the dense parse table `uint16_t[LARGE_STATE_COUNT][SYMBOL_COUNT]` shrank significantly because fewer keyword tokens appear in ambiguous positions.

#### Lessons Learned

1. **`accessibility_modifier` cannot be tokenized.** `public`, `private`, `protected` must remain as individual keyword strings (not wrapped in `token()`). When wrapped in `token()`, the lexer always matches the combined token, preventing these keywords from being recognized as identifiers in expression contexts (e.g., `constructor(public x: number)` where `public` is a parameter property modifier). Tested both full tokenization and a hybrid approach (`choice('public', token(choice('private', 'protected')))`) — both break constructor parameter properties.

2. **`token()` only works for keywords in unambiguous contexts.** `predefined_type` keywords (`any`, `number`, `boolean`, etc.) only appear after `:` in type annotations, so they never need to be identifiers in the same parse context. Keywords that serve dual roles (keyword + identifier) cannot be tokenized.

3. **Build system caveat.** `make build` does not relink `lambda.exe` when `.a` library files change — Premake doesn't track static library dependencies. Must `touch` a source file to force relinking after regenerating the parser.

### Phase 2: Medium Risk — COMPLETED (partial)

5. ✅ **Unify `_type_query_*` rules** — merged 6 rules into 2 (`_type_query_expression` + `_type_query_import_expression`)
6. ⏭️ **Flatten `primary_type`/`type` hierarchy** — SKIPPED, too risky (see below)
7. ✅ No AST builder changes needed — types are stripped during transpilation, AST builder doesn't consume type_query internals
8. ✅ All 567 baseline tests pass (including 19/19 TypeScript tests)

#### Phase 2 Results

| Metric | Phase 1 | Phase 2 | Change | Cumulative from Original |
|--------|---------|---------|--------|--------------------------|
| `STATE_COUNT` | 5,827 | 5,601 | −226 (−3.9%) | −269 (−4.6%) |
| `LARGE_STATE_COUNT` | 1,011 | 994 | −17 (−1.7%) | −199 (−16.7%) |
| `SYMBOL_COUNT` | 373 | 369 | −4 | −7 |
| `TOKEN_COUNT` | 164 | 164 | 0 | −2 |
| `parser.c` bytes | 8,572,838 | 8,397,205 | −176 KB (−2.0%) | −349 KB (−4.0%) |
| **`libtree-sitter-typescript.a`** | **1,366,872** | **1,333,912** | **−33 KB (−2.4%)** | **−122 KB (−8.4%)** |

#### What was done

1. **Merged 4 `typeof`-context rules** (`_type_query_member_expression`, `_type_query_subscript_expression`, `_type_query_call_expression`, `_type_query_instantiation_expression`) into 1 unified `_type_query_expression` rule with 4 suffix branches (member, subscript, call, instantiation).

2. **Merged 2 import-type-annotation rules** (`_type_query_member_expression_in_type_annotation`, `_type_query_call_expression_in_type_annotation`) into 1 unified `_type_query_import_expression` rule with 2 branches (member access, call).

3. **Reduced precedence entries** from 10 `_type_query_*` entries to 7 unified entries.

4. **Updated `type_query` rule** to reference the unified `$._type_query_expression` (hidden, inlined into `type_query` node).

5. **Updated `type` rule** — collapsed 2 alternatives into 1: `prec(-1, alias($._type_query_import_expression, $.member_expression))`.

#### Why `primary_type`/`type` flattening was skipped

Moving `union_type`, `intersection_type`, `conditional_type`, `lookup_type` out of `primary_type` would break valid TypeScript patterns:
- `${string | number}` — template literal types rely on `union_type` being in `primary_type` (via `template_type → primary_type`)
- `A[B][]` — chained lookups require `lookup_type` in `primary_type` (via `array_type → primary_type`)
- `A[B][C]` — nested lookups require `lookup_type` to be a valid `primary_type` for the base of another `lookup_type`

The `primary_type` / `type` separation encodes operator precedence (array type binds tighter than union/intersection). Flattening would require alternative precedence mechanisms that risk subtle breakage.

#### Lessons Learned

4. **Recursive alias correctness.** When merging rules that use different aliases for recursive self-references, the unified rule can only have ONE alias per reference point. The alias may be "wrong" for some matches (e.g., a subscript expression aliased as `member_expression`). This is acceptable when the AST builder doesn't consume the aliased nodes.

5. **Type rule is a supertype.** The `type` rule requires each alternative to produce a single visible child node. Hidden rules (`_` prefix) used directly in `type` cause a "Supertype symbols must always have a single visible child" error. Must alias hidden rules to visible names.

### Phase 3: High Risk, High Reward (Estimated: additional −15–30%)

9. **Simplify `object_type` internal parsing**
10. **Common prefix extraction** for array/tuple and object/object_type ambiguities
11. **Reduce conflict sets** from 44 toward ~30
12. Extensive AST builder rework and testing

---

## Measurement Plan

After each phase, measure:
- `libtree-sitter-typescript.a` size
- `parser.c` line count
- `STATE_COUNT`, `LARGE_STATE_COUNT`, `SYMBOL_COUNT`
- Full TS test suite pass (100% compliance)

Target: reduce `libtree-sitter-typescript.a` from 1.4 MB to < 1.0 MB (Phase 1+2), ideally < 0.8 MB (Phase 3).
