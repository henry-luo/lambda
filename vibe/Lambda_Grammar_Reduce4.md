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
| Combine 20 `base_type` keywords into single `_base_type_kw` token | **−20.2% (−2.1 MB)** | SYMBOL_COUNT −19, TOKEN_COUNT −20, LARGE_STATE_COUNT −570. Keywords only used via `base_type` merged into one token; 4 keywords used elsewhere (`error`, `type`, `string`, `symbol`) kept separate. All 445/451 non-MIR tests pass (same 5 pre-existing failures). **Kept.** |
| Merge `match_arm_expr`/`match_arm_stam` + `match_default_expr`/`match_default_stam` into `match_arm`/`match_default` (Proposal 8) | **−0.02% (−2 KB)** | SYMBOL_COUNT −2 (237→235). STATE_COUNT, LARGE_STATE_COUNT unchanged. Each merged rule uses `choice(: expr, { stmts })` body. Cleaner grammar, trivial size gain. All tests pass. **Kept.** |
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

## Proposal 7: Consolidate Access Expressions — PARTIALLY APPLIED ✓

**Original estimate: Low-Medium (2–4% reduction)**

`path_expr`, `member_expr`, `parent_expr`, `query_expr`, and `direct_query_expr` all handle access operations. They generate 4 conflicts with `$._expr`:

```
[$._expr, $.member_expr]
[$._expr, $.parent_expr]
[$._expr, $.query_expr]
[$._expr, $.direct_query_expr]
```

### Conservative Approach Applied

Merging all 5 into one `access_expr` was too risky — `member_expr` is referenced in `assign_expr` targets, and `path_expr`/`parent_expr` have fundamentally different structures. Instead, only `query_expr` and `direct_query_expr` were merged since they have identical structure except for the operator (`?` vs `.?`):

```js
query_expr: $ => seq(
  field('object', $.primary_expr),
  field('op', choice('?', '.?')),
  field('query', $.primary_type),
),
```

The AST builder determines direct vs recursive from the `op` field length:
```cpp
TSNode op_node = ts_node_child_by_field_id(child, FIELD_OP);
StrView op = ts_node_source(tp, op_node);
query_node->direct = (op.length == 2);  // ".?" = direct, "?" = recursive
```

Conflicts reduced from 4 to 3 (two query conflicts → one).

### Results

| Metric | Before | After | Delta |
|--------|--------|-------|-------|
| File size | 8,480,465 B | 8,399,363 B | **−81,102 B (−0.96%)** |
| STATE_COUNT | 6,264 | 6,241 | **−23** |
| LARGE_STATE_COUNT | 1,482 | 1,480 | **−2** |
| SYMBOL_COUNT | 235 | 234 | **−1** |
| TOKEN_COUNT | 104 | 104 | No change |
| Non-MIR tests | 450/451 | 450/451 | No change |

**Verdict: Kept.** Modest size reduction but cleaner grammar — two nearly-identical rules consolidated into one.

---

## Proposal 8: Merge Match Arm Forms — APPLIED ✓

**Original estimate: Low (1–2% reduction)**

`match_expr` had 4 arm types: `match_arm_expr`, `match_arm_stam`, `match_default_expr`, `match_default_stam`.

### Applied Change

Merged into 2 rules using `choice()` for the body form:

```js
match_arm: $ => seq('case', field('pattern', $._type_expr),
  choice(seq(':', field('body', $._expr)), seq('{', field('body', $.content), '}'))
),
match_default: $ => seq('default',
  choice(seq(':', field('body', $._expr)), seq('{', field('body', $.content), '}'))
),
```

### Results

| Metric | Before | After | Delta |
|--------|--------|-------|-------|
| File size | 8,482,463 B | 8,480,465 B | **−1,998 B (−0.02%)** |
| STATE_COUNT | 6,264 | 6,264 | No change |
| LARGE_STATE_COUNT | 1,482 | 1,482 | No change |
| SYMBOL_COUNT | 237 | 235 | **−2** |
| TOKEN_COUNT | 104 | 104 | No change |
| Non-MIR tests | 450/451 | 450/451 | No change |

**Verdict: Kept.** Minimal size impact but cleaner grammar — 4 near-identical rules reduced to 2.

---

## Proposal 9: Reduce `base_type` Keywords — APPLIED ✓ (via token consolidation)

**Original estimate: Low (1–2% reduction)**
**Actual result: −20.2% (−2.1 MB) — the largest single improvement**

The original proposal suggested removing 5 rarely-used keywords from `base_type`. Instead, a more effective approach was found: consolidating 20 of the 24 `base_type` keywords into a single `token()` rule.

### Analysis

`base_type` had 24 keyword alternatives. Each keyword created its own anonymous terminal symbol (`anon_sym_null`, `anon_sym_int`, etc.), adding 24 columns to every large state row.

4 keywords are also used as standalone string literals elsewhere in the grammar:
- `'error'` — in `error_type_pattern`
- `'type'` — in `type_stam`, `entity_type`, `object_type`, `pub_stam`
- `'string'` — in `string_pattern`
- `'symbol'` — in `symbol_pattern`

These 4 must remain separate. The other 20 (`null`, `any`, `bool`, `int64`, `int`, `float`, `decimal`, `number`, `datetime`, `date`, `time`, `binary`, `range`, `list`, `array`, `map`, `element`, `entity`, `object`, `function`) are ONLY referenced through `base_type`.

### Applied Change

```js
// New: single token for 20 keywords only used via base_type
_base_type_kw: _ => token(prec(1, choice(
  'null', 'any', 'bool', 'int64', 'int', 'float', 'decimal', 'number',
  'datetime', 'date', 'time', 'binary', 'range',
  'list', 'array', 'map', 'element', 'entity', 'object', 'function'
))),

// base_type now uses the combined token + 4 standalone keywords
base_type: $ => prec(1, choice(
  $._base_type_kw,
  'error', 'type', 'string', 'symbol'
)),
```

No changes needed in `build_ast.cpp` — the AST builder reads the text content of the `base_type` node via `ts_node_source()`, which returns the same keyword string regardless of whether it came from `_base_type_kw` or individual anonymous symbols.

### Results

| Metric | Before | After | Delta |
|--------|--------|-------|-------|
| File size | 10,631,709 B | 8,482,463 B | **−2,149,246 B (−20.2%)** |
| STATE_COUNT | 6,265 | 6,264 | −1 |
| LARGE_STATE_COUNT | 2,052 | 1,482 | **−570** |
| SYMBOL_COUNT | 256 | 237 | **−19** |
| TOKEN_COUNT | 124 | 104 | **−20** |
| Non-MIR tests | 445/451 | 445/451 | No change (same 5 pre-existing) |

**Verdict: Kept.** The 19 fewer symbol columns removed from each of the 2,052 large state rows dramatically shrinks the dense `ts_parse_table`. 570 states dropped below the large-state threshold and moved to the compact `ts_small_parse_table` representation.

### Key Insight

Token consolidation is the most effective parser reduction technique found. Each symbol ID eliminated removes one column from every large state row — a multiplicative effect. Combining N keywords that share a single grammar role into one `token()` rule removes N−1 symbols at no cost to parsing correctness.

---

## Proposal 10: Merge `string_pattern`/`symbol_pattern` into `type_stam` — APPLIED ✓

**Impact: Low (−0.28%)**

`string_pattern` and `symbol_pattern` were standalone grammar rules with identical structure to `type_stam` (via `type_assign`):

```js
// Before: 3 separate rules
type_stam:      'type'   identifier = _type_expr
string_pattern: 'string' identifier = _type_expr
symbol_pattern: 'symbol' identifier = _type_expr
```

### Applied Change

Merged all three into `type_stam` by parameterizing the leading keyword:

```js
type_stam: $ => seq(
  field('kind', choice('type', 'string', 'symbol')),
  field('declare', alias($.type_assign, $.assign_expr)),
  repeat(seq(',', field('declare', alias($.type_assign, $.assign_expr))))
),
```

Removed `string_pattern` and `symbol_pattern` rules and their references in `_expr_stam`.

**AST builder changes:**
- `build_let_and_type_stam` now reads the `kind` field text via `ts_node_source()`. If `"string"` or `"symbol"`, routes to `build_string_pattern` instead of `build_assign_expr`.
- `build_string_pattern` updated to read `FIELD_AS` (from `type_assign`) with fallback to `FIELD_PATTERN`.
- Removed `SYM_STRING_PATTERN` and `SYM_SYMBOL_PATTERN` macros from `ast.hpp` and their `case` branches from `build_expr`.

### Results

| Metric | Before | After | Delta |
|--------|--------|-------|-------|
| File size | 8,399,363 B | 8,375,586 B | **−23,777 B (−0.28%)** |
| STATE_COUNT | 6,241 | 6,226 | −15 |
| LARGE_STATE_COUNT | 1,480 | 1,480 | No change |
| SYMBOL_COUNT | 234 | 232 | **−2** |
| TOKEN_COUNT | 104 | 104 | No change |
| Tests | 450/451 | **605/605** | All pass |

**Verdict: Kept.** Modest size reduction (−24 KB, −2 symbols). Cleaner grammar — three near-identical rules consolidated into one. Also eliminates `'string'` and `'symbol'` as standalone anonymous keywords, which is a prerequisite for potentially merging them into `_base_type_kw` in the future.

---

## Proposal 11: Merge `true`/`false`/`inf`/`nan` into `named_value` Token — APPLIED ✓

**Impact: Low-Medium (−1.71%)**

Four simple keyword rules existed as separate grammar rules:

```js
// Before: 4 separate rules
true:  _ => 'true',
false: _ => 'false',
inf:   _ => 'inf',
nan:   _ => 'nan',
```

Each created its own symbol ID. They were used in `_non_null_literal` (`true`, `false`) and `primary_expr` (all four).

### Applied Change

Merged all four into a single `named_value` token rule:

```js
named_value: _ => token(choice('true', 'false', 'inf', 'nan')),
```

Replaced `$.true`, `$.false`, `$.inf`, `$.nan` with `$.named_value` in `_non_null_literal` and `primary_expr`.

**AST builder changes:**
- Replaced `SYM_TRUE`/`SYM_FALSE`/`sym_inf`/`sym_nan` with single `SYM_NAMED_VALUE` macro.
- Both `build_primary_expr` and `build_expr` check the source text via `strview_equal()` to distinguish `"true"`/`"false"` (bool) from `"inf"`/`"nan"` (float).

**Design note:** Initially implemented as a 2-rule pattern (`_named_value` hidden token + `named_value` visible wrapper), but merging into a single visible token rule eliminated one symbol and produced a much larger reduction (−143 KB vs −35 KB).

### Results

| Metric | Before | After | Delta |
|--------|--------|-------|-------|
| File size | 8,375,586 B | 8,232,241 B | **−143,345 B (−1.71%)** |
| STATE_COUNT | 6,226 | 6,226 | No change |
| LARGE_STATE_COUNT | 1,480 | 1,479 | **−1** |
| SYMBOL_COUNT | 232 | 229 | **−3** |
| TOKEN_COUNT | 104 | 101 | **−3** |
| Tests | 605/605 | 605/605 | All pass |

**Verdict: Kept.** Applying the same token consolidation pattern as `_base_type_kw` (Proposal 9) to value keywords. The larger-than-expected reduction comes from eliminating 3 symbol columns from large state rows.

---

## Proposal 12: Merge `time`/`datetime`/`_datetime` into Single `datetime` Token — APPLIED ✓

**Impact: Medium (−3.11%)**

Datetime literals used 3 grammar rules with a complex token structure:

```js
// Before: 3 rules with complex regex patterns
time: _ => token.immediate(time()),  // hh:mm:ss.sss with timezone
datetime: _ => token.immediate(      // yyyy-mm-dd + optional time
  seq(optional('-'), digit, digit, digit, digit, ...)),
_datetime: $ => seq("t'", /\s*/, choice($.datetime, $.time), /\s*/, "'"),
```

The `time()` helper function was a complex multi-part regex (hours, minutes, seconds, milliseconds, timezone) inlined into both `time` and `datetime` rules, expanding parse tables significantly. The `_datetime` hidden rule was inlined, making `datetime` and `time` visible child nodes inside `primary_expr`.

### Applied Change

Replaced all three rules with a single simple `datetime` token that captures the entire `t'...'` literal:

```js
datetime: _ => token(seq(
  "t'",
  repeat(choice(/[0-9]/, /[:\-+.tTzZ ]/)),
  "'",
)),
```

Actual datetime parsing is deferred to the AST builder's existing `datetime_parse()` function, which already handled all date/time formats.

**Grammar changes:**
- Removed `time` and `_datetime` rules.
- Removed `$._datetime` from the `inline` list.
- Changed `$._datetime` references to `$.datetime` in `_non_null_literal` and `primary_expr`.

**AST builder changes:**
- Removed `SYM_TIME` macro from `ast.hpp`; merged `case SYM_TIME` into `case SYM_DATETIME`.
- Updated `build_lit_datetime` to skip the `t'` prefix (2 chars) and trailing `'` (1 char), plus trim inner whitespace, before calling `datetime_parse()`.
- Fixed `build_expr` switch to call `build_lit_datetime` directly instead of routing through `build_lit_node` → `build_lit_string` (which was a pre-existing bug for datetime).

### Results

| Metric | Before | After | Delta |
|--------|--------|-------|-------|
| File size | 8,232,241 B | 7,976,424 B | **−255,817 B (−3.11%)** |
| STATE_COUNT | 6,226 | 5,841 | **−385** |
| LARGE_STATE_COUNT | 1,479 | 1,469 | **−10** |
| SYMBOL_COUNT | 229 | 227 | **−2** |
| TOKEN_COUNT | 101 | 99 | **−2** |
| Tests | 605/605 | 605/605 | All pass |

**Verdict: Kept.** The second-largest single improvement after Proposal 9. The large STATE_COUNT drop (−385) comes from eliminating the complex `time()` regex expansion from parse tables. Moving datetime validation from grammar to AST builder is a clean separation — the grammar just captures the `t'...'` envelope, the builder does semantic parsing.

---

## Proposal 13: Merge `hex_binary`/`base64_binary` into Single `binary` Token — APPLIED ✓

### Rationale

The `binary` rule had a complex structure with two child token rules:

```js
// OLD: Three rules, internal structure exposed to parser
binary: $ => seq("b'", /\s*/, choice($.hex_binary, $.base64_binary), /\s*/, "'"),
hex_binary: _ => /[0-9a-fA-F\s]*/,
base64_binary: _ => /[a-zA-Z0-9+\/=\s]*/,
```

The parser had to manage the choice between `hex_binary` and `base64_binary` as separate symbols, adding states and symbols to the parse tables. Since the AST builder already needs to inspect the content to determine the encoding format anyway, the grammar can be simplified to a single envelope token.

### Changes

**Grammar (`grammar.js`)**:
```js
// NEW: Single token, content parsing deferred to AST builder
binary: _ => token(seq("b'", repeat(/[^']/), "'")),
```

Removed `hex_binary` and `base64_binary` rules entirely. The `binary` token simply captures everything between `b'` and `'`.

**AST Builder (`build_ast.cpp`)**:
- Updated `build_lit_string` binary handling to extract content between `b'` and `'` directly (skip 2 prefix chars, 1 suffix char, trim whitespace) instead of accessing child nodes `SYM_HEX_BINARY`/`SYM_BASE64_BINARY`.
- The existing `decode_hex()`/`decode_base64()` functions still handle the actual decoding — only the source text extraction changed.

### Results

| Metric | Before | After | Delta |
|--------|--------|-------|-------|
| File size | 7,976,424 B | 7,558,156 B | **−418,268 B (−5.24%)** |
| STATE_COUNT | 5,841 | 5,741 | **−100** |
| LARGE_STATE_COUNT | 1,469 | 598 | **−871** |
| SYMBOL_COUNT | 227 | 222 | **−5** |
| TOKEN_COUNT | 99 | 95 | **−4** |
| Tests | 605/605 | 605/605 | All pass |

**Verdict: Kept.** The largest single improvement by file size (−5.24%) and a massive LARGE_STATE_COUNT reduction (−871, from 1,469 to 598). This is the same envelope-token pattern used in Proposals 11 and 12 — the grammar captures just the delimiters, and the AST builder handles semantic parsing of the content. The dramatic LARGE_STATE_COUNT drop suggests the `hex_binary`/`base64_binary` choice was creating many large parse states due to overlapping character classes.

---

## Proposal 14: Merge `pattern_any` into `pattern_char_class` — APPLIED ✓

**Impact: Low (−0.43%)**

`pattern_any` was a separate grammar rule for the `\.` (any character) pattern atom, while `pattern_char_class` handled `\d`, `\w`, `\s`, `\a`. Both produce the same AST node type (`AST_NODE_PATTERN_CHAR_CLASS`) — the only difference is the `char_class` enum value.

### Applied Change

Added `\.` as a 5th choice in the `pattern_char_class` token:

```js
// Before: two separate rules
pattern_char_class: _ => token(choice('\\d', '\\w', '\\s', '\\a')),
pattern_any: _ => '\\.',

// After: single rule
pattern_char_class: _ => token(choice('\\d', '\\w', '\\s', '\\a', '\\.')),
```

Removed `pattern_any` rule and its reference in `primary_type`.

**AST builder changes:**
- Removed two duplicate `case SYM_PATTERN_ANY` blocks (in `build_primary_type` and `build_expr`) that constructed `AstPatternCharClassNode` with `PATTERN_ANY`.
- The existing `build_pattern_char_class` function already had a `default` case in its switch that maps to `PATTERN_ANY`, so `\.` is handled correctly without any new logic.
- Removed `SYM_PATTERN_ANY` macro from `ast.hpp`.

### Results

| Metric | Before | After | Delta |
|--------|--------|-------|-------|
| File size | 7,558,156 B | 7,525,508 B | **−32,648 B (−0.43%)** |
| STATE_COUNT | 5,741 | 5,741 | No change |
| LARGE_STATE_COUNT | 598 | 593 | **−5** |
| SYMBOL_COUNT | 222 | 221 | **−1** |
| TOKEN_COUNT | 95 | 94 | **−1** |
| Tests | 605/605 | 605/605 | All pass |

**Verdict: Kept.** Modest but clean reduction. A straightforward application of the token consolidation pattern — merging a rule that produces the same AST node type into the existing token. Eliminates one symbol and 12 lines of duplicate AST builder code.

---

## Proposal 15: Reuse `raise_expr` inside `raise_stam` — APPLIED ✓

**Impact: Negligible (−0.07%)**

`raise_expr` and `raise_stam` had nearly identical structure, differing only by an optional `;`:

```js
// Before: two rules with duplicated 'raise' + expr structure
raise_expr: $ => prec.right(seq('raise', field('value', $._expr))),
raise_stam: $ => prec.right(seq('raise', field('value', $._expr), optional(';'))),
```

This created a GLR conflict `[$.raise_expr, $.raise_stam]` because the parser couldn't decide which rule to use when `raise` appeared in an `else { ... }` block.

### Applied Change

Rewrote `raise_stam` to wrap `raise_expr` instead of duplicating its structure:

```js
// After: raise_stam wraps raise_expr
raise_stam: $ => prec.right(seq($.raise_expr, optional(';'))),
```

The GLR conflict changed from `[$.raise_expr, $.raise_stam]` to `[$._expr, $.raise_stam]` — the total conflict count stayed at 14.

**AST builder changes:**
- Updated `build_raise_stam` to find the `raise_expr` child node (first child) and read the `value` field from it, instead of reading `value` directly from the `raise_stam` node.

### Results

| Metric | Before | After | Delta |
|--------|--------|-------|-------|
| File size | 7,525,508 B | 7,520,133 B | **−5,375 B (−0.07%)** |
| STATE_COUNT | 5,741 | 5,738 | **−3** |
| LARGE_STATE_COUNT | 593 | 591 | **−2** |
| SYMBOL_COUNT | 221 | 221 | No change |
| TOKEN_COUNT | 94 | 94 | No change |
| Tests | 605/605 | 605/605 | All pass |

**Verdict: Kept.** Negligible size impact but cleaner grammar — eliminates duplicated `'raise' + expr` structure. The conflict wasn't removed, only changed form.

---

## Proposal 16: Merge Path Prefix Tokens into `_path_prefix` — APPLIED ✓

**Impact: Medium (−2.76%)**

`path_expr` used three separate visible rules for its prefix:

```js
// Before: 3 separate rules
path_root: _ => '/',     // absolute file path
path_self: _ => '.',     // relative path
path_parent: _ => '..',  // parent path

path_expr: $ => prec.right(seq(
  choice($.path_root, $.path_self, $.path_parent),
  optional(field('field', ...))
)),
```

Each created its own symbol ID, adding columns to every large state row. `path_root` and `path_self` were only used in `path_expr`, while `path_parent` was also used in `parent_expr`.

### Applied Change

Replaced the three-rule choice with a single hidden `_path_prefix` token:

```js
// After: single hidden token for path_expr prefix
_path_prefix: _ => token(choice('/', '.', '..')),

path_expr: $ => prec.right(seq(
  $._path_prefix,
  optional(field('field', ...))
)),

// path_parent kept separately for parent_expr
path_parent: _ => '..',
```

Removed `path_root` and `path_self` rules entirely. `path_parent` remains for `parent_expr`.

**AST builder changes:**
- Removed `SYM_PATH_ROOT` and `SYM_PATH_SELF` macros from `ast.hpp`.
- Updated `collect_path_segments_if_path` to determine path scheme from the `path_expr` source text (first characters) instead of checking the first child's symbol:
  - `'/'` → `PATH_SCHEME_FILE`
  - `'..'` → `PATH_SCHEME_PARENT`
  - `'.'` → `PATH_SCHEME_REL`

### Results

| Metric | Before | After | Delta |
|--------|--------|-------|-------|
| File size | 7,520,133 B | 7,312,649 B | **−207,484 B (−2.76%)** |
| STATE_COUNT | 5,738 | 5,720 | **−18** |
| LARGE_STATE_COUNT | 591 | 528 | **−63** |
| SYMBOL_COUNT | 221 | 220 | **−1** |
| TOKEN_COUNT | 94 | 95 | +1 |
| Tests | 605/605 | 605/605 | All pass |

**Verdict: Kept.** Unexpectedly large reduction (−2.76%) from removing just 2 visible symbols. The LARGE_STATE_COUNT drop (−63) shows these path symbols were contributing significantly to large state row widths. TOKEN_COUNT increased by 1 because the new `_path_prefix` hidden token was added while `path_root` and `path_self` were removed as symbols but their underlying anonymous tokens (`'/'`, `'.'`) still exist.

---

## Proposal 17: Remove `import` Rule, Use String Literal — APPLIED ✓

**Impact: Low (−0.55%)**

`import` was defined as a standalone named rule:

```js
// Before: named rule wrapping a token
import: _ => token('import'),
```

It was only used in one place — `call_expr` — to allow `import(...)` as a function call syntax. Since `'import'` already exists as an anonymous keyword in `_import_stam`, the named rule was redundant.

### Applied Change

Removed the `import` rule and used the `'import'` string literal directly:

```js
// Before
import: _ => token('import'),
call_expr: $ => prec.right(100, seq(
  field('function', choice($.primary_expr, $.import)),
  ...
)),

// After
call_expr: $ => prec.right(100, seq(
  field('function', choice($.primary_expr, 'import')),
  ...
)),
```

**AST builder changes:** None needed. The builder reads the function name via `ts_node_source()` and doesn't check for `sym_import` anywhere.

### Results

| Metric | Before | After | Delta |
|--------|--------|-------|-------|
| File size | 7,312,649 B | 7,272,412 B | **−40,237 B (−0.55%)** |
| STATE_COUNT | 5,720 | 5,719 | −1 |
| LARGE_STATE_COUNT | 528 | 528 | No change |
| SYMBOL_COUNT | 220 | 219 | **−1** |
| TOKEN_COUNT | 95 | 95 | No change |
| Tests | 605/605 | 605/605 | All pass |

**Verdict: Kept.** A simple cleanup — the named `import` rule added a symbol ID for no benefit. Removing it eliminates one column from every large state row. No AST builder changes required.

---

## Proposal 18: Merge `path_wildcard_recursive` into `path_wildcard` Token — APPLIED ✓

**Impact: Low (−0.31%)**

Path wildcards used two separate grammar rules:

```js
// Before: two rules
path_wildcard: _ => token('*'),               // single wildcard: match one segment
path_wildcard_recursive: _ => token('**'),    // recursive wildcard: match zero or more segments
```

Both were used in `path_expr` and `member_expr` field choices. Each created its own symbol ID.

### Applied Change

Merged into a single `path_wildcard` token:

```js
// After: single token
path_wildcard: _ => token(choice('**', '*')),
```

`'**'` is listed first in the choice to ensure it takes priority over `'*'` during tokenization (longest match). Removed `path_wildcard_recursive` from `path_expr` and `member_expr` field choices.

**AST builder changes:**
- Removed `SYM_PATH_WILDCARD_RECURSIVE` macro from `ast.hpp`.
- Both `SYM_PATH_WILDCARD` cases in `collect_path_segments_if_path` now check source text length: `length == 2` → `LPATH_SEG_WILDCARD_REC`, otherwise `LPATH_SEG_WILDCARD`.
- Transpilers (`transpile.cpp`, `transpile-mir.cpp`) unchanged — they reference the AST segment type enum, not grammar symbols.

### Results

| Metric | Before | After | Delta |
|--------|--------|-------|-------|
| File size | 7,272,412 B | 7,250,003 B | **−22,409 B (−0.31%)** |
| STATE_COUNT | 5,719 | 5,701 | **−18** |
| LARGE_STATE_COUNT | 528 | 524 | **−4** |
| SYMBOL_COUNT | 219 | 218 | **−1** |
| TOKEN_COUNT | 95 | 96 | +1 |
| Tests | 605/605 | 605/605 | All pass |

**Verdict: Kept.** Same token consolidation pattern. The two wildcards share a single grammar role (path field segment), so merging into one token and deferring `*` vs `**` distinction to the AST builder is a clean separation.

---

## Recommended Implementation Order

| Priority | Proposal | Risk | Impact | Status |
|----------|----------|------|--------|--------|
| 1st | #1 — Merge binary/attr_binary | Low | High | ❌ Failed (+10%) |
| 2nd | #5 — Group primary_expr | Low | Medium | ❌ Failed (+0.83%) |
| 3rd | #3 — Unify expr/stam duals | Low | Medium-High | Not tried |
| 4th | #2 — Flatten type hierarchy | Medium | High | ✅ Applied (~0%, −89 LARGE) |
| 5th | #8 — Merge match arms | Low | Low | ✅ Applied (−0.02%, −2 SYM) |
| 6th | #6 — Merge for-loop clauses | Low | Low-Medium | Not tried |
| 7th | #4 — Strategic inlining | Low | Medium | ❌ Failed (+0.7%) |
| 8th | #7 — Consolidate access exprs | Medium | Low-Medium | ✅ Partial (−0.96%, −1 SYM) |
| 9th | #9 — Reduce base_type keywords | Medium | Low | ✅ Applied (−20.2%, −19 SYM) |
| 10th | #10 — Merge patterns into type_stam | Low | Low | ✅ Applied (−0.28%, −2 SYM) |
| 11th | #11 — Merge named values token | Low | Low-Medium | ✅ Applied (−1.71%, −3 SYM) |
| 12th | #12 — Merge datetime token | Low | Medium | ✅ Applied (−3.11%, −2 SYM) |
| 13th | #13 — Merge binary token | Low | Medium-High | ✅ Applied (−5.24%, −5 SYM) |
| 14th | #14 — Merge pattern_any token | Low | Low | ✅ Applied (−0.43%, −1 SYM) |
| 15th | #15 — Reuse raise_expr in raise_stam | Low | Negligible | ✅ Applied (−0.07%) |
| 16th | #16 — Merge path prefix tokens | Low | Medium | ✅ Applied (−2.76%, −1 SYM) |
| 17th | #17 — Remove import rule | Low | Low | ✅ Applied (−0.55%, −1 SYM) |
| 18th | #18 — Merge path wildcards | Low | Low | ✅ Applied (−0.31%, −1 SYM) |

### Cumulative Results

| Change | File Size | STATE | LARGE | SYMBOL | TOKEN |
|--------|-----------|-------|-------|--------|-------|
| Original | 10,631,709 | 6,265 | 2,132 | 262 | 124 |
| + Proposal 2 (flatten types) | 10,631,709 | 6,265 | 2,043 | 256 | 124 |
| + Proposal 9 (_base_type_kw) | 8,482,463 | 6,264 | 1,482 | 237 | 104 |
| + Proposal 8 (match arms) | 8,480,465 | 6,264 | 1,482 | 235 | 104 |
| + Proposal 7 (query merge) | 8,399,363 | 6,241 | 1,480 | 234 | 104 |
| + Proposal 10 (pattern merge) | 8,375,586 | 6,226 | 1,480 | 232 | 104 |
| + Proposal 11 (named_value token) | 8,232,241 | 6,226 | 1,479 | 229 | 101 |
| + Proposal 12 (datetime token) | 7,976,424 | 5,841 | 1,469 | 227 | 99 |
| + Proposal 13 (binary token) | 7,558,156 | 5,741 | 598 | 222 | 95 |
| + Proposal 14 (pattern_any merge) | 7,525,508 | 5,741 | 593 | 221 | 94 |
| + Proposal 15 (raise_stam reuse) | 7,520,133 | 5,738 | 591 | 221 | 94 |
| + Proposal 16 (path prefix merge) | 7,312,649 | 5,720 | 528 | 220 | 95 |
| + Proposal 17 (remove import rule) | 7,272,412 | 5,719 | 528 | 219 | 95 |
| + Proposal 18 (path wildcard merge) | 7,250,003 | 5,701 | 524 | 218 | 96 |
| **Total reduction** | **−3,381,706 (−31.8%)** | **−564** | **−1,608** | **−44** | **−28** |

After each change: run `make generate-grammar && make test-lambda-baseline` to verify correctness.
