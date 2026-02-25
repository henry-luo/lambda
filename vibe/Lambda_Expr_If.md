# Proposal: Unified `if` Expression

## Summary

Unify Lambda's two `if` forms (`if_expr` and `if_stam`) at the **AST level** so downstream code treats them identically, while keeping two grammar rules for parse disambiguation. Enhance both forms: `if_expr` gains block-else, and `if_stam` gains expression-else.

## Current Design

Lambda has two distinct `if` forms with different syntax and semantics:

| Form | Syntax | `else` | Value |
|------|--------|--------|-------|
| `if_expr` | `if (cond) expr else expr` | required | yes |
| `if_stam` | `if cond { stmts } else { stmts }` | optional | no |

This forces users to pick a form upfront and rewrite if requirements change (e.g., a one-liner branch grows into a multi-statement block).

## Implemented Design

Both grammar rules remain (for parser disambiguation) but map to the same `AST_NODE_IF_EXPR` node. The `else` branch of both forms is enhanced to accept either an expression or a `{ stam }` block:

```
if (cond) expr else expr                // expr form, expr else (original)
if (cond) expr else { stam }            // expr form, block else (NEW)
if (cond) expr else if ...              // expr form, chained (original)

if cond { stam }                        // block form, no else (original)
if cond { stam } else expr              // block form, expr else (NEW)
if cond { stam } else { stam }          // block form, block else (original)
if cond { stam } else if ...            // block form, chained (original)
```

**Condition syntax remains unchanged:**
- `if (cond)` — parentheses required for the expression-form. `else` is required.
- `if cond { ... }` — parentheses optional for the block-form. `else` is optional.

**Else branch is unified:** both forms accept `else expr`, `else { stam }`, or `else if ...`.

**Why `else` stays required in `if_expr`:** In Lambda, newlines terminate statements inside content blocks. If `else` were optional, `if (cond) expr\nelse ...` would be parsed as two separate statements (the `if` ending at the newline). The required `else` forces the parser to continue past the newline.

### Map/Object Literal Ambiguity

`{` is ambiguous — it begins both a `{ stam }` block and a map/object literal. For example:

```lambda
if (cond) {a: 1, b: 2}     // map literal or statement block?
```

**Resolution:** In the grammar, `{ stam }` takes higher priority. This is the same strategy JavaScript uses for the identical ambiguity (bare `{` at statement position is a block, not an object):

```lambda
if (cond) {a: 1, b: 2}           // parsed as block (stam)
if (cond) ({a: 1, b: 2})         // parenthesized → map literal (expr)
if (cond) ({a: 1, b: 2}) else 0  // map literal in then-branch
```

This also parallels how Lambda's arrow functions already work — `() => { ... }` would need `() => ({...})` to return a map.

## Rationale

### 1. Consistency with Kotlin, and other languages

Kotlin's `if` allows expression and block bodies interchangeably, which is the closest model to Lambda's syntax:

```kotlin
// Kotlin
val x = if (cond) 1 else 2                     // expr + expr
val y = if (cond) 1 else { compute(); fallback } // expr + block
val z = if (cond) {                              // block + expr
    val tmp = compute()
    tmp * 2
} else 42

if (cond) { doSomething() }                     // no else, statement
```

Lambda's proposed unified `if` follows the same pattern:

```lambda
// Lambda (proposed)
let x = if (cond) 1 else 2
let y = if (cond) 1 else { let f = compute(); f }
let z = if cond {
    let tmp = compute();
    tmp * 2
} else 42

if cond { doSomething() }
```

Kotlin does not have Lambda's `{` map ambiguity, but Lambda resolves it with the block-priority rule (same as JavaScript), so the practical experience is equivalent.

### 2. Precedent within Lambda: `match` already mixes forms

Lambda's `match` expression already allows expression and statement arms to be freely mixed:

```lambda
match value {
    case int: ~ * 2              // expression arm
    case string {                // statement arm
        let processed = trim(~);
        upper(processed)
    }
    default: null                // expression arm
}
```

Unifying `if` is the natural extension of this design — applying the same principle to `if` branches.

### 3. Ergonomics and Reduced Cognitive Load

- **One construct instead of two.** Users don't need to remember separate syntax rules for `if_expr` vs `if_stam`. The same `if` works everywhere.
- **Smooth refactoring.** Expanding `else 0` into `else { log("fallback"); 0 }` is a local edit — no need to restructure the entire `if`.
- **Expression/statement interchangeability.** This aligns with Lambda's broader philosophy of allowing `expr` and `{ stam }` in parallel positions (as already done in `match` arms and function bodies `fn f() => expr` vs `fn f() { stam }`).

## Grammar Changes

Two grammar rules are kept for disambiguation, but both map to `AST_NODE_IF_EXPR`:

```js
// Expression-form if: if (cond) expr else expr | else { stam }
// Condition always in parens. Else is REQUIRED (ternary-style).
// New: else can be a block { content } (preferred over map via prec.dynamic).
if_expr: $ => prec.right(seq(
  'if', '(', field('cond', $._expr), ')', field('then', $._expr),
  'else', choice(
    prec.dynamic(1, seq('{', field('else', $.content), '}')),
    field('else', $._expr),
  ),
)),

// Block-form if: if cond { stam } [else { stam } | else if_stam | else expr]
// Condition without required parens. Block body. Else can be expr (NEW).
if_stam: $ => prec.right(seq(
  'if', field('cond', $._expr),
  '{', field('then', $.content), '}',
  optional(seq('else', choice(
    prec.dynamic(1, seq('{', field('else', $.content), '}')),
    field('else', $.if_stam),
    field('else', $._expr),
  ))),
)),
```

Key changes from original:
- `if_expr` else can now be a `{ content }` block (`prec.dynamic(1)` prefers block over map)
- `if_stam` else can now be an `_expr` (expression else — NEW)
- Both grammar rules dispatch to the same `build_if_expr` function in `build_ast.cpp`
- A single `AST_NODE_IF_EXPR` enum is used (no more `AST_NODE_IF_STAM`)

### Parser disambiguation

The two rules are needed because `if (` is ambiguous in an LR parser:
- `if_expr`: `(` is a literal delimiter around the condition
- `if_stam`: `(` could start a parenthesized expression as the condition

Tree-sitter resolves this automatically via LR state merging. In `_expr` contexts (arrays, function args), only `if_expr` is available — no ambiguity. In statement contexts, both are active; `if_stam` fails when it doesn't find `{` after the condition, and `if_expr` continues.

## AST Impact

Both `SYM_IF_EXPR` and `SYM_IF_STAM` dispatch to `build_if_expr()` in `build_ast.cpp`, producing a single `AST_NODE_IF_EXPR` node. The `AST_NODE_IF_STAM` enum value has been removed. The `AstIfNode` struct is unchanged — it already supports both branch forms through `is_content` flags.

All downstream code (transpile.cpp, transpile-mir.cpp, safety_analyzer.cpp, print.cpp, runner.cpp) only handles `AST_NODE_IF_EXPR`.

## Migration

This is a **backward-compatible** change:

- All existing `if_expr` code continues to work (expression branches with required `else` still parse).
- All existing `if_stam` code continues to work (block branches with optional `else` still parse).
- New mixed forms become additionally valid: `if (cond) expr else { stam }` and `if cond { stam } else expr`.
