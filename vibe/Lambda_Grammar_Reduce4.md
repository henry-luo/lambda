# Lambda Grammar Parser Size Reduction Proposals

## Current Parser Stats

| Metric | Value |
|--------|-------|
| File size | 10.6 MB |
| Line count | 349,123 |
| STATE_COUNT | 6,179 |
| LARGE_STATE_COUNT | 2,132 |
| SYMBOL_COUNT | 262 |
| TOKEN_COUNT | 124 |
| FIELD_COUNT | 52 |
| Conflicts | 15 |

### Size Breakdown by Section

| Section | Lines | Share |
|---------|------:|------:|
| `ts_parse_table` (large states) | 186,400 | 53.4% |
| `ts_small_parse_table` | 135,924 | 38.9% |
| `ts_lex_modes` | 6,182 | 1.8% |
| `ts_primary_state_ids` | 6,182 | 1.8% |
| `ts_parse_actions` | 5,046 | 1.4% |
| `ts_small_parse_table_map` | 4,050 | 1.2% |
| `ts_lex` | 1,925 | 0.6% |
| `ts_lex_keywords` | 731 | 0.2% |

Parse tables account for **92.3%** of the file. Each of the 2,132 large states is a dense row of 262 columns (one per symbol). The two reduction levers are: **fewer states** and **fewer symbols**.

---

## Original Proposals (Preserved for Reference)

## Proposal 1: Merge `binary_expr` and `attr_binary_expr`

**Impact: High (10–15% reduction)**

The `binary_expr()` helper is instantiated twice — once with all 18 operators and once without the 4 relational operators (`<`, `<=`, `>=`, `>`). This generates two independent sets of binary-precedence states. The `_attr_expr` rule exists only to route to `attr_binary_expr`.

**Current grammar:**
```js
attr_binary_expr: $ => choice(...binary_expr($, true)),
_attr_expr: $ => choice($.primary_expr, $.unary_expr, alias($.attr_binary_expr, $.binary_expr), $.if_expr, $.for_expr),
```

`_attr_expr` is used in `attr` and `element` to avoid `<`/`>` ambiguity with element tags.

**Proposed change:**
- Remove `attr_binary_expr` and `_attr_expr` entirely.
- Use `$._expr` everywhere in attr/element context.
- Validate in `build_ast.cpp` that bare relational operators don't appear in attribute value position.

**Removes:**
- 2 conflicts: `[$._attr_expr, $._expr]` and `[$.attr_binary_expr, $._expr]`
- 2 symbol IDs (`attr_binary_expr`, `_attr_expr`)
- The duplicate binary operator state fan-out

**Risk:** Low — the AST builder validation is a simple check on `binary_expr` operator field content within an `attr` parent.
### Experimental Results

All proposals below were implemented and tested. **None produced a net parser size reduction.** This section documents the findings.
#### Why Grammar-Level Restructuring Cannot Reduce This Parser

The Lambda grammar has 15 GLR conflicts representing intrinsic ambiguities:
- **Element tag ambiguity:** `<` and `>` are both element delimiters AND relational operators
- **Postfix ambiguity:** `.`, `..`, `?`, `.?` after expressions
- **Block ambiguity:** `else { ... }` could be block content or map literal
- **Type occurrence:** `T[n]` could be occurrence or end-of-type + array
- **Range vs literal:** A literal could be standalone or start of `literal to literal`

These ambiguities are **language-level** — they exist because the same tokens serve multiple purposes. Grammar restructuring can only **move** conflicts between rules, never eliminate them. Every experiment confirmed this:

| Experiment | Result | Mechanism |
|-----------|--------|-----------|
| Remove `attr_binary_expr`, use `$._expr` in elements | **+10% (11.6 MB)** | 3 new GLR conflicts for `<`/`>` ambiguity create more states than the duplicate binary operator approach |
| Add `_literal_expr`/`_container_expr` grouping in `primary_expr` | **Build error** | New intermediate symbols conflict with `_content_expr`'s `repeat1(string,...)` |
| Group `primary_expr` with `_value_expr`/`_access_expr` (Proposal 5, retry) | **+0.83% (+88 KB)** | Hidden rules still add symbol IDs to parse table (+2 to SYMBOL_COUNT), widening every large state row. No state reduction. Reverted. |
| Merge `match_arm_expr`/`match_arm_stam` into single rule | **+0.7% (10.7 MB)** | Internal `choice(: expr, { stam })` adds branching states |
| Inline `_content_expr` and `_expr_stam` | **+0.7% (10.7 MB)** | Inlining expands alternatives at use sites, adding states |
| Move `range_type` from `primary_type` to `_compound_type` | **Same conflict** | Shift-reduce conflict moves from `[range_type, primary_type]` to `[range_type, unary_type]` |
| Flatten Type Hierarchy (Proposal 2): remove `unary_type`, `_quantified_type`, `_compound_type`; fold into 3-layer `primary_type → _unary_type → _type_expr` | **~0% (-1 KB)** | LARGE_STATE_COUNT −89, SYMBOL_COUNT −6, but STATE_COUNT +73. Net file size unchanged. All 600/605 baseline tests pass. **Kept** — cleaner grammar, marginal improvement in large states. |
#### Key Insight

The `attr_binary_expr` + `_attr_expr` design is the **optimal grammar-level solution** for the `<`/`>` element ambiguity. It statically excludes relational operators from element context without GLR overhead. The cost is 14 duplicate binary operator states — but the alternative (GLR conflicts) costs 330+ additional large states.

Tree-sitter's LALR/GLR state count is determined by **token-level lookahead**, not rule-level structure. Restructuring rules (merging, inlining, flattening) changes the rule graph but not the set of tokens valid at each parse position, so state count stays the same or increases.
#### Viable Paths Forward (Require Language/Architecture Changes)

1. **External scanner for element `>`** — A C scanner tracking element nesting could tokenize `>` differently inside elements, eliminating `attr_binary_expr`. Requires new `scanner.c` infrastructure.

2. **Element syntax change** — Using explicit close tags (`<div>...</div>`) or different delimiters would eliminate the `<`/`>` ambiguity entirely.

3. **Parser compression** — Post-process `parser.c` to compress the parse tables (run-length encoding, shared row deduplication). This is a build-tool change, not a grammar change.

4. **Tree-sitter version upgrade** — Newer tree-sitter versions may have better state merging or table compression.

---

## Proposal 2: Flatten Type Expression Hierarchy — APPLIED ✓

**Original estimate: High (10–15% reduction)**

The type system originally had 7 structural layers:

```
primary_type → unary_type → _quantified_type → occurrence_type / concat_type / constrained_type
    → _compound_type → binary_type → _type_expr (+ error_union_type)
```

**Applied change:** Flattened to 3 layers:

```
primary_type → unary_type (occurrence) → _type_expr (binary/concat/constrained/error)
```

Specific changes made:
- **Removed `unary_type` named rule** — folded `negation_type` into `primary_type` directly; renamed hidden `_unary_type` to visible `unary_type` as `choice(occurrence_type, primary_type)`
- **Removed `_quantified_type`** — `occurrence_type` and `primary_type` now grouped under `unary_type`
- **Removed `_compound_type`** — `constrained_type` and `concat_type` moved directly into `_type_expr`
- Updated `occurrence_type` operand from `$.unary_type` to `$.primary_type`
- Updated `constrained_type` base from `$._quantified_type` to `$.unary_type`
- Updated `concat_type` terms from `$._quantified_type` to `$.unary_type`
- Updated 3 conflicts and precedence declarations
- Removed dead `sym_unary_type` case from `build_ast.cpp`, added pass-through handler for new visible `unary_type` node

### Results

| Metric | Before | After | Delta |
|--------|--------|-------|-------|
| File size | 10,608,627 B | 10,607,520 B | **−1,107 B (~0%)** |
| STATE_COUNT | 6,179 | 6,252 | +73 |
| LARGE_STATE_COUNT | 2,132 | 2,043 | **−89** |
| SYMBOL_COUNT | 262 | 256 | **−6** |
| Baseline tests | 600/605 | 600/605 | No change |

**Verdict: Kept.** The net file size is essentially unchanged, but the grammar is cleaner with 6 fewer symbols, 89 fewer large states, and 4 fewer intermediate rules. The original 10–15% estimate was wrong — the type hierarchy layers were not the main driver of state count. The improvement comes from fewer symbols (smaller dense rows in large states) offsetting the slightly higher total state count.

---

## Proposal 3: Unify Expression/Statement Duals

**Impact: Medium-High (5–8% reduction)**

Six conflicts arise from expression-vs-statement ambiguity inside `else { ... }` blocks:

| Expr form | Stam form | Conflict |
|-----------|-----------|----------|
| `let_expr` | `let_stam` | `[$.let_expr, $.let_stam]` |
| `raise_expr` | `raise_stam` | `[$.raise_expr, $.raise_stam]` |
| — | — | `[$._statement, $._expr]` |
| — | — | `[$._attr_expr, $._expr]` |

The only difference between `let_expr` and `let_stam` is that `let_stam` allows trailing `, declare` for multi-assignment. Similarly, `raise_expr` and `raise_stam` differ only in the optional `;`.

**Proposed change:**
- Unify `let_expr` / `let_stam` into a single `let_expr` that optionally takes trailing `, assign_expr`. The AST builder distinguishes single-let (expression) from multi-let (statement) by child count.
- Unify `raise_expr` / `raise_stam` into a single `raise_expr`. Handle `;` as optional at the statement boundary (already done for `break_stam`, `continue_stam`, `return_stam`).

**Removes:** 2 conflicts and the GLR fork path duplication for every `else {` block.

**Risk:** Low — the AST builder already processes both and can distinguish by child structure.

---

## Proposal 4: Strategic `inline` Additions

**Impact: Medium (3–5% reduction)**

Currently 5 rules are inlined: `_non_null_literal`, `_parenthesized_expr`, `_arguments`, `_number`, `_datetime`. Adding more intermediate rules to `inline` prevents them from creating their own symbol IDs, reducing SYMBOL_COUNT and its multiplicative effect on large state rows.

**Candidates (used in 1–2 places):**

| Rule | Used in | Benefit |
|------|---------|---------|
| `_content_expr` | `content`, `_statement` | Remove 1 symbol |
| `_expr_stam` | `_content_expr` only | Remove 1 symbol |
| `_compound_type` | `_type_expr` only | Remove 1 symbol (if Proposal 2 not applied) |
| `_quantified_type` | `_compound_type` only | Remove 1 symbol (if Proposal 2 not applied) |

**Caution:** Over-inlining rules used in many parents can *increase* states. Apply only to rules used in ≤2 contexts.

**Risk:** Low — these are hidden rules; inlining is invisible to consumers.

---

## Proposal 5: Group `primary_expr` Alternatives

**Impact: Medium (5–10% reduction)**

`primary_expr` has **27 alternatives**. Every token that could begin a primary expression forces the parser to consider all 27 in every expression-expecting state. This directly drives the large state count (2,132 large states).

**Proposed change:** Introduce intermediate hidden grouping rules:

```js
_literal_expr: $ => choice(
  $._number, $._datetime, $.string, $.symbol, $.binary,
  $.true, $.false, $.inf, $.nan,
),

_container_expr: $ => choice(
  $.list, $.array, $.map, $.element, $.object_literal,
),

primary_expr: $ => prec(50, choice(
  $._literal_expr,
  $._container_expr,
  $.base_type,
  $.identifier,
  $.index_expr,
  $.path_expr,
  $.member_expr,
  $.parent_expr,
  $.call_expr,
  $.query_expr,
  $.direct_query_expr,
  $._parenthesized_expr,
  $.fn_expr,
  $.current_item,
  $.current_index,
  $.variadic,
)),
```

Do **not** inline these grouping rules — they intentionally reduce fan-out by collapsing related tokens into shared state transitions.

**Risk:** Low — no semantic change. Verify with `make test-lambda-baseline`.

---

## Proposal 6: Merge For-Loop Clause Rules

**Impact: Low-Medium (2–3% reduction)**

The `for` comprehension uses 9 symbols: `for_where_clause`, `for_order_clause`, `for_group_clause`, `for_limit_clause`, `for_offset_clause`, `for_clauses`, `order_spec`, `loop_expr`, `for_let_clause`.

**Proposed change:** Merge the 5 clause rules into `for_clauses` directly:

```js
for_clauses: $ => repeat1(choice(
  seq('where', field('cond', $._expr)),
  seq('group', 'by', field('key', $._expr), repeat(seq(',', field('key', $._expr))), 'as', field('name', $.identifier)),
  seq('order', 'by', field('spec', seq($._expr, optional(choice('asc', 'desc')))),
      repeat(seq(',', field('spec', seq($._expr, optional(choice('asc', 'desc'))))))),
  seq('limit', field('count', $._expr)),
  seq('offset', field('count', $._expr)),
)),
```

The AST builder can distinguish clause kinds by their keyword.

**Removes:** 5 symbol IDs, ~4 named rule reductions.

**Risk:** Low — only affects for-loop internals. The AST builder already walks children.

---

## Proposal 7: Consolidate Access Expressions

**Impact: Low-Medium (2–4% reduction)**

`path_expr`, `member_expr`, and `parent_expr` all handle `.`-based access and generate 3 conflicts with `$._expr`:

```
[$._expr, $.member_expr]
[$._expr, $.parent_expr]
[$._expr, $.query_expr]
[$._expr, $.direct_query_expr]
```

**Proposed change:** Unify into a single `access_expr`:

```js
access_expr: $ => choice(
  // member: expr.field
  prec.dynamic(1, seq(field('object', $.primary_expr), '.', field('field', choice(...)))),
  // parent: expr..
  seq(field('object', $.primary_expr), '..', repeat(seq('_', '..'))),
  // path: /, ., .. with optional segment
  prec.right(seq(choice('/', '.', '..'), optional(field('field', choice(...))))),
),
```

The AST builder reconstructs `member_expr`, `parent_expr`, `path_expr` node types from the structure.

**Removes:** 2 symbol IDs, reduces conflicts from 4 to ~2.

**Risk:** Medium — the AST builder needs to classify access kind by structure. Test carefully.

---

## Proposal 8: Merge Match Arm Forms

**Impact: Low (1–2% reduction)**

`match_expr` has 4 arm types: `match_arm_expr`, `match_arm_stam`, `match_default_expr`, `match_default_stam`.

**Proposed change:** Merge into 2 rules:

```js
match_arm: $ => seq('case', field('pattern', $._type_expr),
  choice(seq(':', field('body', $._expr)), seq('{', field('body', $.content), '}'))
),
match_default: $ => seq('default',
  choice(seq(':', field('body', $._expr)), seq('{', field('body', $.content), '}'))
),
```

**Removes:** 2 symbol IDs.

**Risk:** Low — trivial AST builder update.

---

## Proposal 9: Reduce `base_type` Keywords

**Impact: Low (1–2% reduction)**

`base_type` has 22 keyword alternatives. Some keywords (`entity`, `object`, `type`, `function`) only appear in specific grammar positions (e.g., `entity_type`, `object_type`, `type_stam`). They could be parsed as identifiers and distinguished in the AST builder, removing them from `base_type`.

**Candidates for removal from `base_type`:** `entity`, `object`, `type`, `function`, `range`.

**Removes:** ~5 keyword tokens from `ts_lex_keywords`, slightly reduces state fan-out.

**Risk:** Medium — must verify no ambiguity when these are parsed as identifiers in expression position.

---

## Recommended Implementation Order

| Priority | Proposal | Risk | Impact |
|----------|----------|------|--------|
| 1st | #1 — Merge binary/attr_binary | Low | High |
| 2nd | #5 — Group primary_expr | Low | Medium |
| 3rd | #3 — Unify expr/stam duals | Low | Medium-High |
| 4th | #2 — Flatten type hierarchy | Medium | High |
| 5th | #8 — Merge match arms | Low | Low |
| 6th | #6 — Merge for-loop clauses | Low | Low-Medium |
| 7th | #4 — Strategic inlining | Low | Medium |
| 8th | #7 — Consolidate access exprs | Medium | Low-Medium |
| 9th | #9 — Reduce base_type keywords | Medium | Low |

After each change: run `make generate-grammar && make test-lambda-baseline` to verify correctness.

**Combined realistic estimate:** applying proposals 1–5 could reduce the parser from ~10.6 MB to roughly **5–7 MB** (35–50% reduction).
