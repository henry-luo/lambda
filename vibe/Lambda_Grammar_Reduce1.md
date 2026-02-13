# Proposal: Unify String/Symbol Pattern Grammar with Type Pattern Grammar

## Motivation

Lambda's grammar currently has **two separate pattern sub-grammars** that share nearly identical structural operators:

| Concept | Type Pattern | String/Symbol Pattern |
|---------|-------------|----------------------|
| Union | `binary_type` with `\|` | `binary_pattern` with `\|` |
| Intersection | `binary_type` with `&` | `binary_pattern` with `&` |
| Exclusion | `binary_type` with `!` | *(not defined)* |
| Optional | `type_occurrence` with `?` | `pattern_occurrence` with `?` |
| One-or-more | `type_occurrence` with `+` | `pattern_occurrence` with `+` |
| Zero-or-more | `type_occurrence` with `*` | `pattern_occurrence` with `*` |
| Exact/range count | `occurrence_count` `[n]` `[n,m]` `[n+]` | `pattern_count` `[n]` `[n,m]` `[n+]` |
| Range | `range_type` (`1 to 10`) | `pattern_range` (`"a" to "z"`) |
| Grouping | `list_type` `(...)` | `primary_pattern` `(...)` |
| Negation | `!` in `binary_type` | `pattern_negation` `!` |

This duplication accounts for **~15 grammar rules and ~60 lines** in `grammar.js`, and contributes to
parse table bloat in the generated `parser.c` (currently **860K lines / 23 MB**). The two sub-grammars
produce different CST node types but are transformed into identical AST node types (`AstBinaryNode`,
`AstUnaryNode`) by the AST builder.

### Goal

Unify the grammar so that **one set of structural rules** covers both type patterns and string/symbol
patterns. The language syntax seen by users remains **unchanged**. Validation that was previously
implicit in the grammar (e.g., "only string literals are valid in a string pattern primary") moves to
the AST builder, where it can produce clearer error messages.

---

## Current Grammar Structure

### Type Pattern Rules (9 rules)

```
_type_expr          → primary_type | type_occurrence | binary_type | error_union_type | constrained_type
primary_type        → literal | base_type | identifier | range_type | list_type | array_type | map_type | element_type | fn_type
type_occurrence     → _type_expr  occurrence
binary_type         → _type_expr  ('|' | '&' | '!')  _type_expr
occurrence          → '?' | '+' | '*' | occurrence_count
occurrence_count    → '[' int ']' | '[' int ',' int ']' | '[' int '+' ']'
range_type          → literal 'to' literal
constrained_type    → (primary_type | type_occurrence)  'where' '(' _expr ')'
error_union_type    → _type_expr '^'
```

### String/Symbol Pattern Rules (12 rules)

```
_pattern_expr       → pattern_seq | binary_pattern
_pattern_term       → primary_pattern | pattern_occurrence | pattern_negation | pattern_range
pattern_seq         → _pattern_term+               (concatenation)
primary_pattern     → string | pattern_char_class | pattern_any | pattern_any_star | '(' _pattern_expr ')'
pattern_occurrence  → (primary_pattern | pattern_negation)  ('?' | '+' | '*' | pattern_count)
pattern_negation    → '!' primary_pattern
pattern_range       → string 'to' string
binary_pattern      → _pattern_term ('|' | '&') _pattern_expr
pattern_char_class  → '\d' | '\w' | '\s' | '\a'     (token)
pattern_any         → '\.'                           (token)
pattern_any_star    → '...'                          (token)
pattern_count       → '[' int ']' | '[' int '+' ']' | '[' int ',' int ']'
string_pattern      → 'string' identifier '=' _pattern_expr
symbol_pattern      → 'symbol' identifier '=' _pattern_expr
```

### Key Differences

1. **String patterns have concatenation** (`pattern_seq`): multiple terms placed side-by-side form a sequence. Type patterns do not have concatenation.
2. **String patterns have char-class atoms** (`\d`, `\w`, `\s`, `\a`, `\.`, `...`). These are
   meaningless in type context.
3. **Type patterns have compound primaries** (`list_type`, `array_type`, `map_type`, `element_type`,
   `fn_type`, `base_type`). These are meaningless in string pattern context.
4. **Type patterns have `constrained_type`** (`where` clause) and `error_union_type` (`^`). Not
   applicable to string patterns.
5. **Structural operators are identical**: `|`, `&`, `!`, `?`, `+`, `*`, `[n]`, `[n,m]`, `[n+]`,
   `to` range — same semantics, same precedence intent.

---

## Proposed Unified Grammar

### Design Principle

Keep one `_type_expr` rule family. Extend `primary_type` to accept string-pattern atoms. Re-use
`type_occurrence`, `binary_type`, `occurrence`, and `occurrence_count` for both purposes. Add
`type_seq` for concatenation (only semantically valid in string/symbol pattern context — validated
in AST builder). Rename/alias nothing in the visible CST — the node names stay as type-pattern names.

### Unified Rules

```js
// ── Atoms ──────────────────────────────────────────────────
// Extend primary_type to include string-pattern atoms
primary_type: $ => choice(
  $._non_null_literal,      // existing: int/float/string/symbol/bool/datetime/binary literals
  $.base_type,              // existing: int, string, float, ...
  $.identifier,             // existing: type reference / pattern reference
  $.range_type,             // existing: 1 to 10, "a" to "z"  (unified range)
  $.list_type,              // existing: (T, U)
  $.array_type,             // existing: [T]
  $.map_type,               // existing: {name: T}
  $.element_type,           // existing: <Tag ...>
  $.fn_type,                // existing: fn(x: T) U
  // NEW — string-pattern atoms (moved here):
  $.pattern_char_class,     // \d, \w, \s, \a
  $.pattern_any,            // \.
  $.pattern_any_star,       // ...
),

// ── Range (already unified) ──────────────────────────────
// range_type already uses _non_null_literal on both sides.
// "a" to "z" and 1 to 10 are both valid — no change needed.
range_type: $ => prec.left('range_to', seq(
  field('start', $._non_null_literal), 'to', field('end', $._non_null_literal),
)),

// ── Occurrence (unchanged) ───────────────────────────────
occurrence: $ => choice('?', '+', '*', $.occurrence_count),

occurrence_count: $ => choice(
  seq('[', $.integer, ']'),
  seq('[', $.integer, ',', $.integer, ']'),
  seq('[', $.integer, '+', ']'),
),

type_occurrence: $ => prec.right(seq(
  field('operand', $._type_expr),
  field('operator', $.occurrence),
)),

// ── Binary operators (unchanged) ─────────────────────────
binary_type: $ => choice(
  ...type_pattern($._type_expr),   // |, &, !
),

// ── Concatenation (NEW) ──────────────────────────────────
// Sequence of type terms placed side-by-side (string pattern concatenation).
// The AST builder validates this is only used inside string/symbol pattern definitions.
type_seq: $ => prec.left('pattern_concat', seq(
  $._type_term, repeat1($._type_term),
)),

// A type term is anything except binary_type (union/intersect/exclude)
// This mirrors _pattern_term — things that can be concatenated.
_type_term: $ => choice(
  $.primary_type,
  $.type_occurrence,
  $.type_negation,
),

// ── Negation (NEW, replaces pattern_negation) ────────────
// !T — negation / exclusion prefix. In type context treated as unary exclusion.
// In string-pattern context treated as character negation.
// Existing binary_type '!' handles binary exclusion (T ! U).
// This handles prefix negation (!T).
type_negation: $ => prec.right(seq(
  '!', field('operand', $.primary_type),
)),

// ── Top-level type expression ────────────────────────────
_type_expr: $ => choice(
  $.primary_type,
  $.type_occurrence,
  $.binary_type,
  $.type_seq,              // NEW — concatenation
  $.type_negation,         // NEW — prefix negation
  $.error_union_type,
  $.constrained_type,
),

// ── String/Symbol pattern definition (simplified) ────────
// Pattern body is now just _type_expr — same structural grammar.
string_pattern: $ => seq(
  'string', field('name', $.identifier), '=', field('pattern', $._type_expr),
),

symbol_pattern: $ => seq(
  'symbol', field('name', $.identifier), '=', field('pattern', $._type_expr),
),
```

### Rules Removed (12 → 0)

| Removed Rule | Replaced By |
|-------------|-------------|
| `_pattern_expr` | `_type_expr` |
| `_pattern_term` | `_type_term` |
| `pattern_seq` | `type_seq` |
| `primary_pattern` | `primary_type` (extended) |
| `pattern_occurrence` | `type_occurrence` |
| `pattern_negation` | `type_negation` |
| `pattern_range` | `range_type` (already covers `"a" to "z"`) |
| `binary_pattern` | `binary_type` |
| `pattern_count` | `occurrence_count` (already exists) |
| `string_pattern` body | uses `_type_expr` directly |
| `symbol_pattern` body | uses `_type_expr` directly |

### Rules Added (2)

| New Rule | Purpose |
|----------|---------|
| `type_seq` | Concatenation of type terms (for string patterns) |
| `type_negation` | Prefix `!` negation (for string patterns, also useful for types) |

### Rules Modified (1)

| Modified Rule | Change |
|---------------|--------|
| `primary_type` | Add `pattern_char_class`, `pattern_any`, `pattern_any_star` as choices |

### Token Rules Kept (unchanged, 3)

These remain as leaf token rules since they have unique lexical forms:

| Token | Syntax | Rationale |
|-------|--------|-----------|
| `pattern_char_class` | `\d`, `\w`, `\s`, `\a` | Unique lexical tokens, no type equivalent |
| `pattern_any` | `\.` | Unique lexical token |
| `pattern_any_star` | `...` | Unique lexical token (sugar for `\.*`) |

### Net Rule Count Change

- **Removed**: 10 visible rules (`_pattern_expr`, `_pattern_term`, `pattern_seq`, `primary_pattern`, `pattern_occurrence`, `pattern_negation`, `pattern_range`, `binary_pattern`, `pattern_count`, plus the 2 inline `_pattern_*` hidden rules)
- **Added**: 2 visible rules (`type_seq`, `type_negation`) + 1 hidden (`_type_term`)
- **Modified**: 1 rule (`primary_type`)
- **Net**: **−7 visible rules**, estimated 5-10% reduction in parse table size

---

## Precedence Changes

### Current Precedence Chains

**Type patterns:**
```
primary_type > type_occurrence > binary_type (set_intersect > set_exclude > set_union)
```

**String patterns:**
```
primary_pattern > pattern_occurrence, pattern_negation > pattern_concat > pattern_range > pattern_intersect > pattern_union
```

### Unified Precedence

Add `type_negation` and `type_seq` into the existing type precedence chain.
Add `pattern_concat` precedence level between occurrence and set operators.

```js
precedences: $ => [[
  // ... (expression precedences unchanged) ...
],
[
  $.fn_type,
  $.constrained_type,
  $.type_occurrence,
  $.type_negation,        // NEW — between occurrence and primary
  $.primary_type,
  $.binary_type,
],
// Pattern precedences (REMOVED — merged into type precedences above)
// [
//   $.primary_pattern,
//   $.pattern_occurrence,
//   $.pattern_negation,
//   'pattern_concat',
//   'pattern_range',
//   'pattern_intersect',
//   'pattern_union',
// ]
]
```

The `'pattern_concat'` named precedence is used in `type_seq` and sits between individual
terms and binary operators, exactly as before.

---

## AST Builder Changes

The AST builder (`build_ast.cpp`) currently has separate dispatch paths:

- `build_pattern_expr()` dispatches `SYM_PATTERN_*` nodes
- `build_primary_type()`, `build_binary_type()`, `build_occurrence_type()` dispatch `SYM_*_TYPE` nodes

After unification, the pattern-specific dispatch in `build_pattern_expr()` largely disappears.
Instead:

### 1. `build_string_pattern()` — simplified

```cpp
// Before: field('pattern') → build_pattern_expr(tp, pattern_node)
// After:  field('pattern') → build_expr(tp, pattern_node)
//         followed by: validate_pattern_ast(tp, ast_node->as, is_symbol)
```

The new `validate_pattern_ast()` walks the built AST tree and:
- Verifies only valid nodes appear (string literals, char classes, identifiers, sequences, occurrences, binary `|`/`&`, negation, ranges with string endpoints)
- Rejects type-only constructs: `base_type`, `map_type`, `array_type`, `element_type`, `fn_type`, `constrained_type`, `error_union_type`
- Reports clear error messages: `"map type {...} is not valid inside a string pattern"`

### 2. Dispatch table changes

```cpp
// These cases are REMOVED from build_expr:
case SYM_PATTERN_CHAR_CLASS:    // now handled as primary_type child
case SYM_PATTERN_ANY:           // now handled as primary_type child
case SYM_PATTERN_ANY_STAR:      // now handled as primary_type child
case SYM_PATTERN_OCCURRENCE:    // now type_occurrence
case SYM_PATTERN_NEGATION:      // now type_negation
case SYM_PATTERN_RANGE:         // now range_type
case SYM_BINARY_PATTERN:        // now binary_type
case SYM_PATTERN_SEQ:           // now type_seq
case SYM_PRIMARY_PATTERN:       // now primary_type
```

`build_primary_type()` gains three new child cases:

```cpp
case SYM_PATTERN_CHAR_CLASS:
    return build_pattern_char_class(tp, child);
case SYM_PATTERN_ANY:
    // build PATTERN_ANY char class node
case SYM_PATTERN_ANY_STAR:
    // build PATTERN_ANY with * occurrence (sugar)
```

### 3. New: `build_type_seq()`

```cpp
// Handles type_seq nodes — concatenation of type terms
// Used only in string/symbol pattern context (validated by caller)
AstNode* build_type_seq(Transpiler* tp, TSNode node) {
    AstPatternSeqNode* seq_node = (AstPatternSeqNode*)
        alloc_ast_node(tp, AST_NODE_PATTERN_SEQ, node, sizeof(AstPatternSeqNode));
    // ... iterate named children, build each via build_expr()
}
```

### 4. New: `build_type_negation()`

```cpp
// Handles type_negation nodes — prefix ! operator
AstNode* build_type_negation(Transpiler* tp, TSNode node) {
    AstUnaryNode* ast_node = (AstUnaryNode*)
        alloc_ast_node(tp, AST_NODE_UNARY, node, sizeof(AstUnaryNode));
    ast_node->operand = build_expr(tp, ts_node_child_by_field_id(node, FIELD_OPERAND));
    ast_node->op = OPERATOR_NOT;
    // In pattern context: type = TYPE_KIND_PATTERN
    // In type context: type = TYPE_KIND_BINARY (exclusion)
    // Determined by context in parent builder
}
```

### 5. Validation function (new)

```cpp
// Validate that an AST subtree contains only pattern-valid nodes
bool validate_pattern_ast(Transpiler* tp, AstNode* node, bool is_symbol) {
    if (!node) return true;
    switch (node->node_type) {
    case AST_NODE_PRIMARY:    // string literal, char class
    case AST_NODE_PATTERN_CHAR_CLASS:
    case AST_NODE_PATTERN_RANGE:
    case AST_NODE_PATTERN_SEQ:
    case AST_NODE_UNARY:      // occurrence (?, +, *) or negation (!)
    case AST_NODE_BINARY:     // union (|), intersection (&)
    case AST_NODE_IDENT:      // pattern reference
        return true;  // + recurse into children

    case AST_NODE_TYPE:       // base_type like 'int', 'float' — invalid
    case AST_NODE_LIST_TYPE:
    case AST_NODE_ARRAY_TYPE:
    case AST_NODE_MAP_TYPE:
    case AST_NODE_CONSTRAINED_TYPE:
        log_error("Error: %s is not valid inside a %s pattern definition",
                  ast_node_type_name(node), is_symbol ? "symbol" : "string");
        return false;
    default:
        return false;
    }
}
```

---

## What Stays the Same (User-Facing Syntax)

| Syntax | Before | After | Change? |
|--------|--------|-------|---------|
| `string phone = \d[3] "-" \d[3] "-" \d[4]` | ✓ | ✓ | No |
| `symbol Keyword = 'if' \| 'else' \| 'for'` | ✓ | ✓ | No |
| `string id = ("a" to "z" \| "_") \w*` | ✓ | ✓ | No |
| `type T = int \| string` | ✓ | ✓ | No |
| `type T = int?` | ✓ | ✓ | No |
| `type T = string[2, 5]` | ✓ | ✓ | No |
| `type T = int where (~ > 0)` | ✓ | ✓ | No |
| `"hello" is phone` | ✓ | ✓ | No |

The CST node names change (e.g., `pattern_occurrence` → `type_occurrence`), but this is internal
to the parser — the AST builder maps these to the same AST node types as before.

---

## Impact Summary

| Aspect | Before | After |
|--------|--------|-------|
| Grammar rules (visible) | ~15 pattern + ~9 type = **~24** | ~11 unified = **~11** |
| Precedence chains | 2 separate | 1 merged |
| `parser.c` estimated size | 860K lines | ~770-800K lines (est. −7-10%) |
| AST node types | unchanged | unchanged |
| AST builder functions | ~8 pattern builders | ~3 pattern builders + 1 validator |
| User syntax | unchanged | **unchanged** |
| Error messages | grammar-level rejection | AST-level validation (more descriptive) |

---

## Migration Steps

1. **Modify `grammar.js`**:
   - Add `pattern_char_class`, `pattern_any`, `pattern_any_star` to `primary_type`
   - Add `type_seq` and `type_negation` rules
   - Add `_type_term` hidden rule
   - Change `string_pattern` and `symbol_pattern` to use `_type_expr`
   - Remove all `_pattern_*`, `pattern_*` rules (except the 3 token rules)
   - Update precedences and conflicts
   - Remove `_pattern_term`, `_pattern_expr` from inline list

2. **Regenerate parser**: `make generate-grammar`

3. **Update `ast.hpp`**:
   - Remove `SYM_PATTERN_*` defines that no longer exist (keep `SYM_PATTERN_CHAR_CLASS`, `SYM_PATTERN_ANY`, `SYM_PATTERN_ANY_STAR`)
   - Add `SYM_TYPE_SEQ`, `SYM_TYPE_NEGATION`

4. **Update `build_ast.cpp`**:
   - Add `pattern_char_class`/`pattern_any`/`pattern_any_star` handling in `build_primary_type()`
   - Add `build_type_seq()` and `build_type_negation()`
   - Simplify `build_string_pattern()` to use `build_expr()` + `validate_pattern_ast()`
   - Remove `build_pattern_occurrence()`, `build_pattern_negation()`, `build_binary_pattern()`, `build_primary_pattern()`, `build_pattern_range()`
   - Remove `build_pattern_expr()` dispatch function

5. **Run tests**: `make test-lambda-baseline` — all existing pattern tests must pass unchanged.

---

## Risks & Mitigations

| Risk | Mitigation |
|------|-----------|
| Grammar conflicts from merging | `_type_term` and precedence levels isolate concatenation from binary ops. Run `tree-sitter generate` and check for new conflicts. |
| Over-permissive parsing (e.g., `string x = int?`) | `validate_pattern_ast()` in AST builder catches and reports these with clear messages. |
| `pattern_any_star` (`...`) conflicts with expression ellipsis | `...` is already a token; it just becomes a valid `primary_type` child. Only valid in pattern context (validated in AST builder). |
| Concatenation ambiguity in type context | `type_seq` requires 2+ terms. In `type T = int string`, this would parse as a `type_seq` and be rejected by the type checker. The grammar already prevents this in `type_stam` because the `=` RHS is `_type_expr`, and `type_seq` would only match if two primaries are adjacent — which is unusual and would be caught. |
