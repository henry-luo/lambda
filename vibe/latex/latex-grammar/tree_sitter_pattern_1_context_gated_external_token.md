# Pattern 1: Context-Gated External Token (Tree-sitter)

## Purpose

Pattern 1, commonly referred to as a **context-gated external token**, is a Tree-sitter grammar design technique where an external scanner emits a token **only when the parser explicitly allows it in the current parse state**.

This pattern is the most reliable and idiomatic way to achieve *effective token priority* in Tree-sitter without relying on fragile precedence tricks or grammar conflicts.

---

## Core Concept

Tree-sitter external scanners are invoked with a `valid_symbols` array that indicates which **external tokens are acceptable at the current position in the parse**.

A context-gated scanner follows this rule:

> **If the parser does not expect this token here, the scanner must not emit it.**

In practice, this means:

1. Check `valid_symbols[TOKEN]` immediately.
2. If it is `false`, return `false` without consuming input.
3. Only attempt recognition when it is `true`.

This ensures the token exists *only* in the syntactic contexts where it belongs.

---

## Why This Works (Effective Priority)

Tree-sitter does not support explicit lexer priority ordering. However, context gating achieves the same effect indirectly:

- Grammar-defined tokens (regexes, literals) are always available.
- External tokens **only exist conditionally**, based on parser state.

As a result:

- In valid contexts, the external token is the *only* candidate and therefore wins.
- Outside those contexts, the external token does not compete at all, and grammar tokens naturally take over.

This eliminates ambiguity rather than attempting to resolve it afterward.

---

## Minimal Example: Contextual Keyword

### Use Case

A word such as `where` should behave as:

- a keyword in a specific syntactic construct
- a normal identifier everywhere else

---

### Grammar Definition (JavaScript)

```js
module.exports = grammar({
  name: "demo",

  externals: $ => [
    $.kw_where,
  ],

  rules: {
    source_file: $ => repeat($.stmt),

    stmt: $ => choice(
      $.select_stmt,
      $.expr_stmt
    ),

    select_stmt: $ => seq(
      "select",
      $.identifier,
      optional(seq($.kw_where, $.expr)),
      ";"
    ),

    expr_stmt: $ => seq($.expr, ";"),

    expr: $ => choice($.identifier, $.number),

    identifier: _ => /[a-zA-Z_]\w*/,
    number: _ => /\d+/,
  }
});
```

---

### External Scanner Implementation (C)

```c
#include <tree_sitter/parser.h>
#include <wctype.h>

enum TokenType {
  KW_WHERE,
};

static void skip_ws(TSLexer *lexer) {
  while (iswspace(lexer->lookahead)) {
    lexer->advance(lexer, true);
  }
}

bool tree_sitter_demo_external_scanner_scan(
  void *payload,
  TSLexer *lexer,
  const bool *valid_symbols
) {
  (void)payload;

  // Context gate: only attempt if the parser allows this token here
  if (!valid_symbols[KW_WHERE]) return false;

  skip_ws(lexer);

  const char *kw = "where";
  for (int i = 0; kw[i]; i++) {
    if (lexer->lookahead != kw[i]) return false;
    lexer->advance(lexer, false);
  }

  // Enforce word boundary
  if (iswalnum(lexer->lookahead) || lexer->lookahead == '_') return false;

  lexer->result_symbol = KW_WHERE;
  return true;
}
```

---

## Key Design Rules

### 1. Gate Early

Always check `valid_symbols[TOKEN]` **before consuming input**. This prevents accidental competition with grammar tokens and avoids corrupting the lexer state.

---

### 2. Never Consume Unless You Will Emit

External scanners cannot rewind input. Partial consumption followed by failure will break parsing. Only call `advance()` when you are confident the token will be produced.

---

### 3. Treat Whitespace Deliberately

- Some tokens require skipping whitespace (e.g. contextual keywords).
- Others use whitespace as semantic input (e.g. indentation-based grammars).

Whitespace handling must be explicit and intentional.

---

### 4. Keep Overlapping Validity Rare

If multiple external tokens can be valid in the same state:

- Order your checks carefully.
- Prefer grammar refactoring to reduce overlap.

A clean grammar minimizes situations where multiple external tokens are simultaneously valid.

---

## Typical Applications

Pattern 1 is ideal for:

- Contextual keywords
- Indentation / dedentation
- Heredocs and raw strings
- Nested or mode-switching comments
- Ambiguous punctuation resolved by syntax

---

## What This Pattern Does *Not* Do

- It does not introduce a global token priority system
- It does not override grammar tokens unconditionally
- It does not replace precedence rules

Instead, it **prevents ambiguity from arising in the first place**.

---

## Summary

A context-gated external token:

- Appears only when syntactically valid
- Avoids conflicts with grammar tokens
- Scales well to complex, real-world languages
- Is the canonical Tree-sitter approach for context-sensitive lexing

This pattern is used extensively in mature Tree-sitter grammars (e.g. Rust, Python, Bash) and should be your default choice whenever token meaning depends on parse context.

