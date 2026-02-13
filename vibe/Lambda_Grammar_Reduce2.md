# Lambda Parser Size Reduction Analysis

## Current State

After the pattern grammar unification (Feb 2026), the parser stands at:

- **361K lines / 11MB** (down from 860K / 23MB — already 58% reduced)
- `STATE_COUNT`: 6,811
- `LARGE_STATE_COUNT`: 2,586
- `SYMBOL_COUNT`: 255

## Parser Structure Breakdown

| Section | Lines | % of total |
|---------|------:|---:|
| **Large parse table** (dense) | 224K | **62%** |
| Small parse table (sparse) | 112K | 31% |
| Parse actions | 4.5K | 1.3% |
| Lex modes + rest | 21K | 5.8% |

The **large parse table** dominates — it's `LARGE_STATE_COUNT (2,586) × SYMBOL_COUNT (255)` = 659K entries. Reducing either dimension has outsized impact.

## Strategies Ranked by Measured Impact

### 1. Consolidate `built_in_types` into a Single Regex Token ✅ Highest Impact

**Measured: −23K lines (−6.3%)**

| Metric | Before | After | Change |
|--------|-------:|------:|-------:|
| `LARGE_STATE_COUNT` | 2,586 | **801** | −69% |
| `SYMBOL_COUNT` | 255 | **234** | −21 |
| Parser lines | 361K | **338K** | −6.3% |

The 23 type keywords (`any`, `error`, `bool`, `int`, `float`, `string`, `map`, etc.) each become separate terminal symbols in the grammar. Every parser state that can accept a `base_type` or `primary_type` must enumerate all 23 keywords as potential lookaheads/shifts. Replacing the `choice('any', 'error', 'bool', ...)` with a single `token(/any|error|bool|.../)` collapses 23 branches into 1 in every such state.

**Change:**

```javascript
// Before — 23 separate keyword terminals
function built_in_types(include_null) {
  let types = ['any', 'error', 'bool', 'int', 'int64', 'float', ...];
  return include_null ? choice('null', ...types) : choice(...types);
}

// After — single regex token
function built_in_types(include_null) {
  const type_keyword = /any|error|bool|int64|int|float|decimal|number|date|time|datetime|symbol|string|binary|range|list|array|map|element|entity|object|type|function/;
  return include_null ? token(choice('null', type_keyword)) : token(type_keyword);
}
```

> **Note:** `int64` must appear before `int` in the regex to ensure longest match.

**Trade-off:** The AST builder receives a single `base_type` token string (e.g., `"int"`) instead of individually named child nodes. `build_ast.cpp` would need to `get_text()` the token and string-match to determine which built-in type was parsed. This is a minor change since the text is already available.

### 2. Eliminate `expr_no_pipe` / `binary_expr_no_pipe` ⚠️ Requires Redesign

These duplicate ~22 binary operator rules (total ~44 extra grammar alternatives). `expr_no_pipe` exists to prevent `where` from being consumed as a binary pipe operator inside `for` loop headers.

**Measured: Cannot remove without conflict resolution redesign.**

Possible alternatives:
- Use `prec.dynamic()` to prefer `for_where_clause` over binary `where`
- Require parentheses around the `where` clause value in `for` loops
- Use a different keyword for `for` filtering (e.g., `when` instead of `where`)

### 3. Eliminate `attr_binary_expr` ⚠️ Requires Redesign

This variant excludes `<`, `<=`, `>=`, `>` to avoid conflicts with XML-style `<element ...>` tags. Removing it causes immediate conflicts between element start `<` and comparison `<`.

Possible alternatives:
- Use a different element syntax that doesn't use `<`/`>`
- Use external scanner to disambiguate (adds complexity)

### 4. Un-inline `_non_null_literal` ✅ Small Additional Win

**Measured: −922 lines** (marginal alone, −833 extra when combined with #1)

This rule expands 9 alternatives (integer, float, decimal, string, symbol, datetime, binary, true, false) at 3 reference sites. Un-inlining prevents the state duplication.

### 5. Reduce `primary_expr` Alternatives — Theoretical

`primary_expr` has ~23 choices. Every state where an expression can start must branch on all of them. Grouping some (e.g., all container literals under one intermediate rule) could reduce branching, but Tree-sitter is already good at merging overlapping states here.

### 6. Un-inline Other Rules — Negligible

Tested individually:

| Rule removed from inline | Impact |
|--------------------------|--------|
| `_statement` | +400 lines (worse!) |
| `_content_expr` | −162 lines |
| `_attr_expr` | −922 lines |
| `_expr_stam` | −0 lines |

Tree-sitter's state merging already handles these well. In the case of `_statement`, inlining actually *helps* because it lets Tree-sitter merge states that would otherwise be duplicated.

## Summary

| Strategy | Feasibility | Impact | Effort |
|----------|:-----------:|-------:|:------:|
| Single-token `built_in_types` | ✅ Safe | **−23K lines (−6.3%)** | Low |
| Un-inline `_non_null_literal` | ✅ Safe | −922 lines | Trivial |
| Remove `expr_no_pipe` | ⚠️ Needs redesign | Unknown (est. −10K+) | Medium |
| Remove `attr_binary_expr` | ⚠️ Needs redesign | Unknown (est. −10K+) | High |
| Un-inline other rules | ❌ Negligible | ≤ 162 lines | — |

**Recommended next step:** Implement Strategy #1 (single-token `built_in_types`). It's the clear winner — safe, low-effort, and delivers the biggest measured payoff. Combined with the pattern unification already done, total reduction would be **860K → 338K lines (61% total reduction)**.
