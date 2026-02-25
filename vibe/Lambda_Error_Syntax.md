# Lambda Parse Error Enhancement Proposal

## Problem

Parse errors currently take a **completely different code path** from semantic/runtime errors, producing raw `printf()` output with no source context:

```
PARSE ERROR: Syntax error at Ln 2, Col 5 - 2, Col 6: node_type='ERROR'
  Error node has 1 children
    Child 0: identifier
```

Meanwhile, semantic and runtime errors use the structured `LambdaError` system with source lines, carets, and help text:

```
test.ls:5:3: error[E201]: Undefined variable 'foo'
  |
5 | let x = foo + 1
  |         ^^^
  = help: Did you mean 'for'?
```

The infrastructure for rich error display already exists — parse errors just don't use it.

## Architecture

### Current Error Flow

```
Source Code
    │
    ▼
[parse.c] ts_parser_parse_string() → TSTree (CST)
    │
    ▼
[runner.cpp] transpile_script()
    ├─ ts_node_has_error()? ──YES──► find_errors() → printf("PARSE ERROR: ...")
    │                                                  (raw printf, no source context, RETURN)
    │
    ├─ NO errors → build_script() [build_ast.cpp]
    │   ├─ record_semantic_error() → LambdaError* with source span + source pointer
    │   └─ record_type_error()     → LambdaError* with line + source pointer
    │
    └─ tp->errors present? → err_print() each (with source context + carets)
```

### Proposed Error Flow

```
Source Code
    │
    ▼
[parse.c] ts_parser_parse_string() → TSTree (CST)
    │
    ▼
[runner.cpp] transpile_script()
    ├─ ts_node_has_error()? ──YES──► find_errors(node, source, file, errors)
    │                                  │
    │                                  ├─ Create LambdaError per error node
    │                                  ├─ Attach source text for context display
    │                                  ├─ Pattern-match for common mistakes
    │                                  └─ Add help text with corrections
    │                                  │
    │                                  ▼
    │                                err_print() each → rich output with carets + help
    │                                RETURN
    │
    ├─ NO errors → build_script() [build_ast.cpp]  (unchanged)
    ...
```

## Implementation

### 1. Change `find_errors()` Signature

```cpp
// Before (runner.cpp line 92):
void find_errors(TSNode node);

// After:
void find_errors(TSNode node, const char* source, const char* file, ArrayList* errors);
```

### 2. Replace `printf()` with `LambdaError` Creation

For each ERROR or MISSING node, create a structured error:

```cpp
void find_errors(TSNode node, const char* source, const char* file, ArrayList* errors) {
    if (ts_node_is_error(node) || strcmp(ts_node_type(node), "ERROR") == 0) {
        TSPoint start = ts_node_start_point(node);
        TSPoint end = ts_node_end_point(node);
        SourceLocation loc = src_loc_span(file,
            start.row + 1, start.column + 1,
            end.row + 1, end.column + 1);
        loc.source = source;

        // Extract the error text from source for pattern matching
        // Try to detect common mistake patterns and give specific messages
        const char* message = diagnose_parse_error(node, source);
        LambdaError* error = err_create(ERR_SYNTAX_ERROR, message, &loc);
        error->help = diagnose_help_text(node, source);
        arraylist_append(errors, error);
        return;  // don't recurse into ERROR node children
    }

    if (ts_node_is_missing(node)) {
        TSPoint start = ts_node_start_point(node);
        SourceLocation loc = src_loc(file, start.row + 1, start.column + 1);
        loc.source = source;
        
        char msg[256];
        snprintf(msg, sizeof(msg), "Expected '%s'", ts_node_type(node));
        LambdaError* error = err_create(ERR_SYNTAX_ERROR, msg, &loc);
        arraylist_append(errors, error);
    }

    // Recurse into children
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        find_errors(ts_node_child(node, i), source, file, errors);
    }
}
```

### 3. Update Call Site in `transpile_script()`

```cpp
// runner.cpp, inside transpile_script():
if (ts_node_has_error(root_node)) {
    log_error("Syntax tree has errors.");
    ArrayList* errors = arraylist_new(8);
    find_errors(root_node, tp->source, script_path, errors);
    fprintf(stderr, "\n");
    for (int i = 0; i < errors->length; i++) {
        err_print((LambdaError*)errors->data[i]);
        fprintf(stderr, "\n");
    }
    fprintf(stderr, "%d syntax error(s) found.\n", errors->length);
    tp->errors = errors;
    tp->error_count = errors->length;
    return;
}
```

### 4. Pattern Detection Engine: `diagnose_parse_error()`

Inspect the error node, its children, siblings, and surrounding source text to detect common mistake patterns:

```cpp
// Returns a specific error message, or a generic one if no pattern matched.
const char* diagnose_parse_error(TSNode error_node, const char* source);

// Returns help text (may be NULL if no suggestion available).
const char* diagnose_help_text(TSNode error_node, const char* source);
```

---

## Common Syntax Error Patterns

### Pattern 1: `...` Used as Spread Operator

**Trigger**: A `variadic` token (`...`) appears outside of a function parameter list.

Many languages use `...` for spread/rest. Lambda uses `*expr` for spread, and `...` is reserved for variadic parameters. In map construction, spread is implicit — just include the map name.

```
// User writes (from JavaScript/Python habit):
{...base, c: 3}

// Lambda equivalent — spread is implicit in map construction:
{c: 3, base}
```

**Detection**: The error node or its preceding sibling is a `variadic` token.

**Output**:
```
test.ls:2:2: error[E100]: Unexpected '...' — not a spread operator in Lambda
  |
1 | let a = 123
2 | {...a, b: 456}
  |  ^^^
  = help: Lambda uses '*expr' for spread. In map construction, spread is
          implicit: {b: 456, a} merges 'a' into the map.
```

### Pattern 2: ~~Mixing `if` Expression and `if` Statement Syntax~~ (REMOVED)

> **This pattern has been removed.** As of the unified `if` implementation, both
> `if_expr` and `if_stam` accept expression or block else-branches, so mixing is
> now valid syntax. See `vibe/Lambda_Expr_If.md` for details.

### Pattern 3: Missing Closing Delimiter

**Trigger**: A MISSING node for `)`, `}`, `]`, or `>`.

**Detection**: `ts_node_is_missing(node)` and the node type is one of the closing delimiters.

**Output**:
```
script.ls:3:25: error[E100]: Expected ')' — unclosed parenthesis
  |
2 | let result = add(1, 2
3 | let other = 5
  |                         ^
  = help: The opening '(' at line 2, column 18 is never closed.
```

**Enhancement**: Walk backward from the MISSING node to find the matching opener and report its location.

### Pattern 4: Using `=` Instead of `==` in Conditions

**Trigger**: An `assign_expr` or ERROR node inside an `if` condition position where `==` was likely intended.

```
// WRONG:
if (x = 5) "yes" else "no"

// CORRECT:
if (x == 5) "yes" else "no"
```

**Detection**: The `if` node's `cond` field contains an assignment or error near `=`.

**Output**:
```
script.ls:1:6: error[E100]: Assignment in condition — did you mean '=='?
  |
1 | if (x = 5) "yes" else "no"
  |      ^^^
  = help: Use '==' for comparison. '=' is for assignment (let x = 5).
```

### Pattern 5: Using `else if` Without Braces (Statement Form)

**Trigger**: `else if` used in if-statement form without braces on the first `if`.

```
// WRONG — statement form requires braces:
if x > 0 { "pos" } else if x < 0 { "neg" } else { "zero" }
//                       ^^ this is fine (else if_stam is valid)

// WRONG — but missing inner braces:
if x > 0 { "pos" } else if (x < 0) "neg" else "zero"
//                       ^^^^^^^^^^^^^^^^^^^^^^^^^ mixed forms
```

**Detection**: `else` branch of `if_stam` contains an `if_expr` (or vice versa).

**Output**:
```
script.ls:1:25: error[E100]: Cannot use if-expression after 'else' in if-statement
  |
1 | if x > 0 { "pos" } else if (x < 0) "neg" else "zero"
  |                           ^^
  = help: Use if-statement form consistently:
            if x > 0 { "pos" } else if x < 0 { "neg" } else { "zero" }
```

### Pattern 6: `fn` Without `=>`

**Trigger**: `fn name(params) expr` without `=>` or `{ }`.

```
// WRONG:
fn add(a, b) a + b

// CORRECT:
fn add(a, b) => a + b
fn add(a, b) { a + b }
```

**Detection**: A `fn` keyword followed by identifier and parameter list, but no `=>` or `{`.

**Output**:
```
script.ls:1:14: error[E100]: Function body requires '=>' or '{...}'
  |
1 | fn add(a, b) a + b
  |              ^
  = help: Use '=>' for expression body:  fn add(a, b) => a + b
          Use '{...}' for statement body: fn add(a, b) { a + b }
```

### Pattern 7: Comma After Last Item (Trailing Comma)

**Trigger**: Trailing comma before closing `]`, `)`, or `}`.

```
// WRONG:
[1, 2, 3,]

// CORRECT:
[1, 2, 3]
```

**Detection**: ERROR or MISSING node immediately after a comma and before a closing delimiter.

**Output**:
```
script.ls:1:10: error[E100]: Trailing comma not allowed
  |
1 | [1, 2, 3,]
  |          ^
  = help: Remove the trailing comma: [1, 2, 3]
```

### Pattern 8: `let` Without `=`

**Trigger**: `let name` without `= expr`.

```
// WRONG:
let x

// CORRECT:
let x = 0
```

**Detection**: `let` keyword followed by identifier but no `=` before end of line.

**Output**:
```
script.ls:1:6: error[E100]: 'let' binding requires an initial value
  |
1 | let x
  |     ^
  = help: Lambda is purely functional — all bindings must have a value:
            let x = 0
          For mutable variables in procedural code, use:
            var x = 0
```

### Pattern 9: Semicolon-Separated Statements

**Trigger**: Multiple expressions separated by `;` where `;` is not expected.

Lambda uses linebreaks as statement separators. `;` is only needed for multiple statements on one line, but users from C/JavaScript may overuse it.

```
// WRONG — semicolons at end of every line:
let x = 1;
let y = 2;

// ok — Lambda uses linebreaks, semicolons optional at end of line.
// but a cleaner style lets linebreaks do the job:
let x = 1
let y = 2
```

This is actually valid Lambda (`;` works as separator), so no error — but worth noting that it's unnecessary noise. No action needed for this pattern.

### Pattern 10: `return` in Functional Context

**Trigger**: `return expr` used inside an `fn ... => ...` (expression function).

```
// WRONG — fn-expr doesn't use return:
fn add(a, b) => return a + b

// CORRECT:
fn add(a, b) => a + b
```

**Detection**: `return` keyword found inside `fn_expr_stam` body.

**Output**:
```
script.ls:1:17: error[E100]: 'return' not allowed in expression function
  |
1 | fn add(a, b) => return a + b
  |                  ^^^^^^
  = help: Expression functions (=>) implicitly return their body.
          Remove 'return':  fn add(a, b) => a + b
          Or use a statement function:  fn add(a, b) { return a + b }
```

---

## Scope

- **One file to modify**: `lambda/runner.cpp` — rewrite `find_errors()` (~50 lines → ~150 lines) and update its call site (~5 lines)
- **One new function**: `diagnose_parse_error()` + `diagnose_help_text()` — pattern matcher (~200 lines), can live in `runner.cpp` or a new `lambda/parse_diag.cpp`
- **Zero new infrastructure** — reuses existing `LambdaError`, `SourceLocation`, `err_create()`, `err_print()`, `err_format_with_context()`
- **Backward compatible** — same errors detected, same exit codes, just better formatted output

## Priority

| Priority | Pattern | Frequency |
|----------|---------|-----------|
| P0 | Unified `LambdaError` output (source context + carets for ALL parse errors) | Every parse error |
| P0 | `...` spread operator confusion | Very common (JS/Python users) |
| ~~P0~~ | ~~Mixed if-expr / if-stam syntax~~ | Removed — now valid syntax |
| P1 | Missing closing delimiter with opener location | Common |
| P1 | `fn` without `=>` or `{}` | Common for new users |
| P2 | `=` vs `==` in condition | Occasional |
| P2 | Trailing comma | Occasional |
| P2 | `let` without `=` | Occasional |
| P3 | `return` in expression function | Rare |
