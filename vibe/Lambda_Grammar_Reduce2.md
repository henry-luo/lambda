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

### 2. Eliminate `expr_no_pipe` / `binary_expr_no_pipe` ✅ Done

**Measured: −29K lines (−8%)**

| Metric | Before | After | Change |
|--------|-------:|------:|--------|
| Parser lines | 359K | **330K** | −8% |

These rules duplicated ~22 binary operator rules (total ~44 extra grammar alternatives). Originally `expr_no_pipe` existed to prevent `where` from being consumed as a binary pipe operator inside `for` loop headers.

**Solution:** The `where` → `that` keyword change (see Appendix) eliminated the ambiguity. With `that` as the filter operator, there's no longer a conflict with `for`-loop's `where` clause. This allowed complete removal of `expr_no_pipe` and `binary_expr_no_pipe`.

**Changes made:**
- **grammar.js**: Removed `expr_no_pipe` and `binary_expr_no_pipe` rules; simplified `binary_expr($, in_attr)` to single parameter; `loop_expr`, `for_let_clause`, `order_spec` now use `$._expr`
- **build_ast.cpp**: Removed `SYM_EXPR_NO_PIPE` and `SYM_BINARY_EXPR_NO_PIPE` handling

**All 223 Lambda baseline tests pass.**

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
| Remove `expr_no_pipe` | ✅ Done | **−29K lines (−8%)** | Medium |
| Un-inline `_non_null_literal` | ✅ Safe | −922 lines | Trivial |
| Remove `attr_binary_expr` | ⚠️ Needs redesign | Unknown (est. −10K+) | High |
| Un-inline other rules | ❌ Negligible | ≤ 162 lines | — |

**Current status:** Strategy #2 (remove `expr_no_pipe`) is complete — **359K → 330K lines (−8%)**.

**Recommended next step:** Implement Strategy #1 (single-token `built_in_types`) for an additional −23K lines.

---

## Appendix: Binary `where` → `that` Keyword Change

### The Problem

The binary `where` operator (for filtering collections) conflicted with the `where` keyword in `for` loop clauses:

```lambda
// Binary 'where' - filter operator
[1, 2, 3, 4, 5] where (~ > 2)  // → [3, 4, 5]

// For-loop 'where' - iteration filter clause
for (x in items where x > 0) ...
```

This ambiguity required `expr_no_pipe` and `binary_expr_no_pipe` rules that duplicate ~44 grammar alternatives, contributing significantly to parser bloat.

### Alternatives Considered

| Syntax | Example | Verdict |
|--------|---------|---------|
| `?` | `items ? (~ > 0)` | ❌ Conflicts with error propagation `call()?` |
| `::` | `items :: (~ > 0)` | ❌ Commonly used for namespaces/scope in other languages |
| `??` | `items ?? (~ > 0)` | ❌ Null coalescing operator in C#, Swift, JS |
| `?:` | `items ?: (~ > 0)` | ❌ Elvis/ternary operator in C-family languages |
| `\|:` | `items \|: (~ > 0)` | ⚠️ Novel, visually related to pipe, but cryptic |
| `:>` | `items :> (~ > 0)` | ⚠️ Pattern matching in ML; could be confused |
| `having` | `items having (~ > 0)` | ⚠️ SQL-like but verbose; 6 chars vs 5 |
| `like` | `items like (~ > 0)` | ❌ Suggests pattern matching, not filtering |
| `[: :]` | `items [: ~ > 0 :]` | ❌ Verbose bracket syntax |
| **`that`** | `items that (~ > 0)` | ✅ **Chosen** |

### Survey of Other Languages

| Language | Filter Syntax | Notes |
|----------|---------------|-------|
| **jq** | `select(cond)` | Function-based |
| **SQL** | `WHERE cond` | Keyword in query clauses |
| **LINQ** | `.Where(x => cond)` | Method chain |
| **Python** | `[x for x in items if cond]` | Comprehension with `if` |
| **Haskell** | `filter pred list` | Function; guards use `\|` |
| **Scala** | `.filter(cond)` | Method chain |
| **Rust** | `.filter(\|x\| cond)` | Iterator method |
| **Elixir** | `Enum.filter(list, fn)` | Function-based |

Most languages use either a method/function (`filter`, `select`, `Where`) or integrate filtering into comprehension syntax (`if` in Python). Few use a dedicated infix keyword.

### Why `that` Won

1. **Natural English reading**: `items that (~ > 0)` reads as "items that are greater than zero" — intuitive and self-documenting.

2. **No conflicts**: `that` is not used elsewhere in Lambda's grammar and doesn't conflict with common programming constructs.

3. **Distinct from `where`**: Clearly separates the filter operator from `for`-loop clauses, eliminating the grammar ambiguity.

4. **Concise**: 4 characters, similar length to `where` (5 chars).

5. **Familiar pattern**: English uses "that" as a relative pronoun for restrictive clauses — exactly what filtering does semantically.

### Implementation

The change was minimal:
- **grammar.js**: Replace `['where', 'pipe']` → `['that', 'pipe']` in binary operators; update `constrained_type` rule
- **build_ast.cpp**: Add `that` as alias for `OPERATOR_WHERE`
- **Test files**: Update `where (` → `that (` in filter expressions

The `where` keyword is preserved exclusively for `for` loop filter clauses:
```lambda
for (x in items where x > 0) ...  // ✅ Still valid
```

### Result

```lambda
// New filter syntax with 'that'
[1, 2, 3, 4, 5] that (~ > 2)           // → [3, 4, 5]
items | ~ * 2 that (~ > 5)             // pipe then filter
type positive = int that (~ > 0)       // constrained type

// For-loop 'where' remains unchanged
for (x in items where x > 0) x * 2
```

### Important: Parentheses Required for Relational Operators at Document Level

At the document/statement level, Lambda uses `attr_binary_expr` which excludes relational operators (`<`, `>`, `<=`, `>=`) to avoid conflicts with XML-style element tags like `<div>`.

```lambda
// ❌ Won't parse at statement level — '>' is excluded in attr context
[1, 2, 3] that ~ > 0

// ✅ Works — parentheses create a nested expression context
[1, 2, 3] that (~ > 0)

// ✅ Also works inside functions, let bindings, etc.
let result = items that ~ > 0   // OK — not at document level
```

This is a pre-existing limitation of the grammar (not introduced by the `that` change) — the same restriction applied to the old `where` operator.

---

## Appendix B: `expr_no_pipe` Removal (Feb 2026)

The `where` → `that` keyword change enabled complete removal of `expr_no_pipe` and `binary_expr_no_pipe` rules.

### Before

```javascript
// grammar.js had two parallel expression hierarchies
expr_no_pipe: $ => choice(
  $.binary_expr_no_pipe,
  $.unary_expr,
  $.primary_expr,
  // ... duplicating all expression rules without pipe operators
),

binary_expr_no_pipe: $ => // ... 22 operators duplicated
```

These existed because `for (x in items where cond)` needed to prevent `where` from being parsed as a binary pipe operator.

### After

With `that` as the filter keyword (distinct from `for`'s `where` clause), there's no ambiguity:
- `for (x in items where cond)` — `where` is unambiguously a loop clause
- `items that (~ > 0)` — `that` is unambiguously a filter operator

**Result:** `expr_no_pipe` and `binary_expr_no_pipe` removed entirely. Parser reduced from 359K to 330K lines (−8%, −29K lines).

All 223 Lambda baseline tests pass.

