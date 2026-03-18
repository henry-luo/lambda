# Proposal: Remove List Type and List Syntax

## Motivation

Lambda currently has two ordered-sequence types at the runtime level:

| | **List** (`LMD_TYPE_LIST`) | **Array** (`LMD_TYPE_ARRAY`) |
|---|---|---|
| Syntax | `(1, 2, 3)` | `[1, 2, 3]` |
| Null filtering | Yes — nulls silently skipped | No |
| String merging | Yes — consecutive strings concatenated | No |
| Auto-flatten | Yes — nested lists recursively spread | No |
| Spreadable flag | Supported | Supported |

The list type's three implicit normalizations (null filter, string merge, flatten) add hidden complexity:
- Users must reason about *which* sequence type they're in to predict behavior.
- Runtime and transpiler carry parallel code paths for list vs array construction.
- `list_end()` has special flattening: 0 items → `null`, 1 item → unwrapped scalar, 2+ → list. This "auto-unwrap" makes list behavior context-dependent and hard to predict.
- `(a, b, c)` syntax conflicts with the universal meaning of parentheses as grouping.

The normalizations are genuinely useful only for **element content** (markup), where filtering nulls, merging adjacent text nodes, and flattening nested fragments is the correct semantic.

## Proposal

### 1. Remove `LMD_TYPE_LIST` as a runtime type

Remove the `List` type entirely. The `List` struct and `list_push()` / `list_end()` family of functions are retired. Arrays become the sole ordered-sequence type.

### 2. Remove list syntax — `(expr)` is just grouping

No more list construction via `(a, b, ...)`. Parentheses become pure grouping, like in every other language:

```lambda
(expr)          // grouping — evaluates to expr
(1 + 2) * 3    // grouping — evaluates to 9
```

### 3. Arrays support `let` expressions

Arrays gain `let`-expression support, replacing the `(let a = b, expr)` list pattern:

```lambda
[let a = 1, let b = 2, a + b]       // [1, 2, 3]
[let x = get_data(), x.name, x.age] // ["Alice", 30]
```

### 4. Explicit spread with `*` operator

Array spreading uses an explicit `*` prefix instead of the implicit `is_spreadable` flag:

```lambda
let a = [1, 2]
let b = [3, 4]
[*a, *b, 5]        // [1, 2, 3, 4, 5]

// For-expressions still auto-spread in array context
[0, for (x in arr) x * 2, 99]  // [0, 2, 4, 6, 99]
```

### 5. Element content uses semicolons, retains full normalization

Element content items are separated by semicolons (not commas). The first `;` still separates attributes from content, subsequent `;` separate content items:

```lambda
<tag; a; b; c>                       // 3 content items
<p; "Hello, "; null; "world!">       // normalized to: "Hello, world!"
<div class: "x"; item1; item2>       // attrs + 2 content items
```

Element content is the **only** place that applies the 3 normalizations:
1. Auto-spread nested arrays
2. Null filtering
3. Consecutive string merging

Implementation: a dedicated `content_push()` function applies the normalizations, used only when building element content. Content is stored internally as an `Array` with `is_content` flag.

### 6. Destructuring — no brackets needed

Destructuring uses bare comma-separated names (unchanged from current syntax):

```lambda
let a, b = [1, 2]       // a = 1, b = 2
let x, y, z = expr      // destructure 3 values
```

### 7. Function multi-return uses arrays

Functions return multiple values via arrays:

```lambda
fn divide(a, b) => [a / b, a % b]
let quot, rem = divide(10, 3)   // quot = 3, rem = 1
```

### 8. Top-level script builds an array

A script's top-level statements accumulate into an array. No null filtering or string merging — the result is the raw array of statement values.

## Syntax Comparison

| Before (with lists) | After (no lists) |
|---|---|
| `(1, 2, 3)` — list | `[1, 2, 3]` — array |
| `(expr)` — ambiguous: grouping or 1-list? | `(expr)` — always grouping |
| `(let a = 1, a + 2)` — let in list | `[let a = 1, a + 2]` — let in array |
| `(a, b)` — multi-return | `[a, b]` — multi-return |
| `<tag; a, b, c>` — comma-separated content | `<tag; a; b; c>` — semicolon-separated content |
| Implicit spreading via `is_spreadable` | Explicit `*expr` spread |
| `let a, b = expr` | `let a, b = expr` (unchanged) |

## Impact Analysis

### What Changes

| Area | Current | Proposed |
|---|---|---|
| **Runtime type enum** | `LMD_TYPE_LIST = 14` | Remove entirely |
| **`(a,b,c)` syntax** | Produces `List` | Syntax error (no commas in parens) |
| **`(expr)` syntax** | Ambiguous grouping/list | Pure grouping |
| **`[let a=b, c]`** | Not supported | Array with let-expressions |
| **Multi-return** | `(a, b)` list | `[a, b]` array |
| **Spreading** | Implicit `is_spreadable` | Explicit `*expr` |
| **Element content** | `<tag; a, b, c>` with commas | `<tag; a; b; c>` with semicolons |
| **Top-level accumulation** | `list_push_spread()` / `list_end()` | `array_push_spread()` / `array_finalize()` |
| **For-expression over list** | Produces `List` | N/A — always produces `Array` |
| **Pipe over list** | Preserves `List` type | N/A — always `Array` |
| **Element content normalization** | `list_push()` with 3 normalizations | `content_push()` with 3 normalizations |
| **Type system** | `list` is a distinct type | `list` removed |

### What Stays the Same

- Array syntax `[1, 2, 3]` — unchanged (now the only sequence syntax).
- Map syntax `{k: v}` — unchanged.
- All sequence operations (`len`, indexing, slicing, `head`, `tail`, `++`, `sort`, etc.) — already unified via `seq_*` helpers.
- For-expression auto-spread in array context — unchanged.
- `ITEM_NULL_SPREADABLE` — unchanged.
- Specialized array types (`ArrayInt`, `ArrayInt64`, `ArrayFloat`) — unchanged.
- Destructuring syntax `let a, b = expr` — unchanged.

### Grammar Changes (`grammar.js`)

1. **Remove `list_expression`** rule (the `(a, b, ...)` production).
2. **`parenthesized_expression`** — keep as `(expr)`, no commas allowed.
3. **`array_expression`** — add `let_expression` as a valid item.
4. **`spread_expression`** — add `*expr` prefix operator for array spreading.
5. **Element content** — change separator from `,` to `;`.

### Code Areas to Modify

1. **`lambda/tree-sitter-lambda/grammar.js`** — Remove list production, add let-in-array, add spread `*`, change element content separator to `;`. Then `make generate-grammar`.

2. **`lambda/build_ast.cpp`** — Remove `AST_NODE_LIST` construction. Add `AST_NODE_SPREAD` for `*expr`. Handle let-in-array.

3. **`lambda/ast.hpp`** — Remove `AST_NODE_LIST` (or repurpose). Add `AST_NODE_SPREAD`.

4. **`lambda/lambda-data-runtime.cpp`** — Remove `list()`, `list_push()`, `list_push_spread()`, `list_end()`. Add `content_push()` for element content normalization.

5. **`lambda/lambda-data.cpp`** — Remove `list_push()` normalization logic. Move string-merge and null-filter logic to `content_push()`.

6. **`lambda/transpile-mir.cpp`** — Remove `AST_NODE_LIST` transpilation. Add `AST_NODE_SPREAD` transpilation (inline spread into enclosing array). Handle let-in-array.

7. **`lambda/transpile.cpp`** — Same changes for C transpiler path.

8. **`lambda/lambda-eval.cpp`** — Remove list evaluation. Add spread and let-in-array evaluation.

9. **`lambda/runner.cpp`** — Top-level accumulation switches to `array_push_spread()` / `array_finalize()`.

10. **`lambda/format/format-html.cpp`** — Remove LIST-specific handling.

11. **`lambda/lambda.h`** — Remove `LMD_TYPE_LIST` enum value, remove `List` struct.

12. **Input parsers (`lambda/input/`)** — Element content building uses `content_push()` instead of `list_push()`.

13. **`lambda/mark_builder.hpp` / `mark_builder.cpp`** — Update content construction.

14. **Tests (`test/`)** — Update all tests using list syntax `(a, b, ...)` to use array syntax `[a, b, ...]`. Update element content tests for `;` separator. Update expected outputs.

## Edge Cases & Considerations

### 1. `list_end()` auto-unwrap behavior

Currently `list_end()` returns:
- `null` for empty lists
- The single item for length-1 lists
- The list for length-2+ lists

This is used pervasively for expression evaluation (e.g., `let` blocks, `if` branches). Options:
- **(A) Keep the unwrap in `array_finalize()`** — minimal change, preserves existing semantics for top-level and let-block results.
- **(B) Drop the unwrap** — always return an array, even for 0/1 items. Cleaner but requires auditing all call sites.

**Recommendation:** (A) initially — keep the unwrap in `array_finalize()` for backward compatibility.

### 2. For-expression spread in arrays

For-expressions in array context still auto-spread (no explicit `*` needed):
```lambda
[0, for (x in [1,2,3]) x * 2, 99]  // [0, 2, 4, 6, 99]
```
The `*` operator is for explicitly spreading a named array:
```lambda
let evens = [2, 4, 6]
[1, *evens, 7]  // [1, 2, 4, 6, 7]
```

### 3. Backward compatibility

This is a **breaking change** to syntax. All Lambda scripts using `(a, b, ...)` list expressions must be updated to `[a, b, ...]`. Element content using commas must switch to semicolons. The `list` type name is removed.

### 4. `*` spread operator precedence

The `*` prefix should bind tightly — `*expr` spreads the result of `expr`. Needs careful grammar design to avoid ambiguity with multiplication:
```lambda
[*a, *b]            // spread a and b
[*(f(x)), 1]        // spread result of f(x)
[a * b]             // multiplication (not spread)
```

### 5. Performance

Removing a runtime type and its code paths simplifies the engine. Explicit `*` spread is cheaper to implement than implicit `is_spreadable` checking on every push.

## Migration Plan

### Phase 1: Grammar and AST changes
1. Modify `grammar.js`: remove list production, add let-in-array, add `*` spread, change element content separator.
2. `make generate-grammar`
3. Update `build_ast.cpp` and `ast.hpp`.

### Phase 2: Runtime changes
1. Introduce `content_push()` for element content normalization.
2. Route element content building through `content_push()`.
3. Switch top-level accumulation to array-based.
4. Implement `*` spread in transpiler and evaluator.

### Phase 3: Remove list type
1. Remove `LMD_TYPE_LIST`, `List` struct, and all list functions.
2. Remove `list` from type system.
3. Clean up enum values.

### Phase 4: Tests and docs
1. Update all test scripts: `(a,b,c)` → `[a,b,c]`, content commas → semicolons.
2. Update expected outputs.
3. Update documentation.

## Summary

| Before | After |
|---|---|
| Two sequence types (list + array) | One sequence type (array) |
| `(a, b)` — list, `[a, b]` — array | `[a, b]` — array only |
| `(expr)` — ambiguous grouping | `(expr)` — always grouping |
| `(let a = 1, a + 2)` — let in list | `[let a = 1, a + 2]` — let in array |
| Implicit spreading via `is_spreadable` | Explicit `*expr` spread |
| `<tag; a, b, c>` — commas in content | `<tag; a; b; c>` — semicolons in content |
| Null filtering + string merging in lists | Only in element content (via `content_push()`) |
| `list` type in type system | Removed |
| Dual code paths in transpiler/evaluator | Single array code path |

The net result: **simpler syntax** (parentheses are just grouping), **one sequence type** (array), **explicit spreading** (`*`), **predictable behavior** (no hidden normalizations except in element content), and **less runtime/transpiler code**.
