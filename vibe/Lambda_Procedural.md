# Lambda Procedural Enhancement Proposal

## Implementation Status

**Status: ✅ Core Features Implemented**

Implemented features:
- ✅ `while` loop statement
- ✅ `var` mutable variable declaration  
- ✅ Assignment statement (`x = expr`)
- ✅ `return` statement for early exit
- ✅ `output(source, url, format?)` system procedure - writes formatted data to file
- ⚠️ `break`/`continue` - grammar parsed, basic transpilation done, but requires `if` statement improvements for full support

All Lambda baseline tests pass (100/100 tests).

---

## Overview

This proposal outlines enhancements to Lambda Script's procedural programming support, focusing on:
1. Loop control statements (`break`, `continue`) for `for` and `while` statements
2. Early `return` statement for procedural functions
3. New `output()` system procedure for file output
4. `while` loop statement
5. Mutable variables with `var` keyword

> **Important**: `break`, `continue`, `return`, `while`, and `var` are **only available in procedural functions (`pn`)**, not in functional functions (`fn`). This maintains Lambda's pure functional semantics in `fn` while enabling imperative patterns in `pn`.

---

## 1. Loop Control: `break` and `continue`

### Motivation

Currently, Lambda's `for` statement iterates over all elements without the ability to exit early or skip iterations. This limits efficiency when:
- Searching for a specific element (exit on first match)
- Filtering with complex conditions mid-iteration
- Processing until a condition is met

### Syntax

```lambda
for item in collection {
    if (should_skip(item)) continue;
    if (should_stop(item)) break;
    process(item)
}
```

### Grammar Changes

Add to `_statement` in `grammar.js`:

```javascript
_statement: $ => choice(
    $.object_type,
    $.entity_type,
    $.if_stam,
    $.for_stam,
    $.fn_stam,
    $.break_stam,      // NEW
    $.continue_stam,   // NEW
    seq($._content_expr, choice(linebreak, ';')),
),

break_stam: $ => seq('break', optional(';')),

continue_stam: $ => seq('continue', optional(';')),
```

### Semantics

| Statement | Behavior |
|-----------|----------|
| `break` | Exit the innermost enclosing `for` or `while` loop immediately |
| `continue` | Skip remaining body, proceed to next iteration |

> **Note**: `break` and `continue` are only valid within procedural functions (`pn`). Using them in functional functions (`fn`) is a compile-time error.

### Implementation Notes

1. **AST Nodes**: Add `AST_BREAK` and `AST_CONTINUE` node types in `ast.hpp`
2. **Transpilation**: In MIR/C transpilation, implement via:
   - Stack of loop labels (for nested loops)
   - `break` → `goto loop_end_label`
   - `continue` → `goto loop_continue_label`
3. **Interpreter**: Track loop context in evaluation stack

### Examples

```lambda
// Find first even number
let result = null;
for n in [1, 3, 4, 7, 8] {
    if (n % 2 == 0) {
        let result = n;
        break
    }
}
// result = 4

// Sum only positive numbers, stop at first negative
let total = 0;
for n in [1, 2, -3, 4, 5] {
    if (n < 0) break;
    let total = total + n
}
// total = 3

// Skip processing of null items
for item in data {
    if (item == null) continue;
    process(item)
}
```

### Labeled Loops (Future Extension)

For nested loop control:

```lambda
outer: for x in [1, 2, 3] {
    for y in [4, 5, 6] {
        if (x * y > 10) break outer;  // Exit outer loop
    }
}
```

---

## 2. Procedural Function `return` Statement

### Motivation

Procedural functions (`pn`) currently return the value of the last expression. Explicit `return` enables:
- Early exit from functions
- Clearer control flow
- Guard clauses pattern

### Syntax

```lambda
pn find_user(id: int): User {
    if (id <= 0) return error("Invalid ID");
    
    let user = fetch_user(id);
    if (user == null) return error("User not found");
    
    return user
}
```

### Grammar Changes

Add to `_statement`:

```javascript
return_stam: $ => seq(
    'return',
    optional(field('value', $._expression)),
    optional(';')
),
```

### Semantics

| Syntax | Behavior |
|--------|----------|
| `return expr` | Exit function, return `expr` as result |
| `return` | Exit function, return `null` |

### Type Checking

- Return value type must match function's declared return type
- Multiple return paths must all return compatible types
- Functions without explicit `return` implicitly return last expression value

> **Note**: `return` is only valid within procedural functions (`pn`). Functional functions (`fn`) always return their expression body value and cannot use `return`.

### Implementation Notes

1. **AST**: Add `AST_RETURN` node with optional value field
2. **Transpilation**:
   - Create function epilogue label
   - `return expr` → store result, `goto epilogue`
   - Epilogue handles cleanup and actual return
3. **Scope Interaction**: `return` within `for` should properly exit the function, not just the loop

### Examples

```lambda
// Guard clause pattern
pn process_data(data: any): Result {
    if (data == null) return error("Data is null");
    if (data is not map) return error("Expected map");
    
    let processed = transform(data);
    return {status: 'ok, data: processed}
}

// Early return in loop
pn find_first(predicate: fn, items: [any]): any {
    for item in items {
        if (predicate(item)) return item
    }
    return null
}
```

---

## 3. `output()` System Procedure

### Motivation

Currently, Lambda has `format()` for converting data to string representations, and `print()` for console output. A dedicated `output()` procedure enables:
- Direct file writing
- Format conversion on save
- Batch processing workflows

### Signature

```lambda
output(source: any, url: string, format?: symbol): null | error
```

### Parameters

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `source` | `any` | Yes | Data to output |
| `url` | `string` | Yes | Output file URL (currently local files only) |
| `format` | `symbol` | No | Output format; defaults to `'mark` |

### Supported Formats

| Format     | Extension       | Description                         |
| ---------- | --------------- | ----------------------------------- |
| `mark`     | `.mark`, `.ls`  | Lambda/Mark native format (default) |
| `json`     | `.json`         | JSON format                         |
| `yaml`     | `.yaml`, `.yml` | YAML format                         |
| `xml`      | `.xml`          | XML format                          |
| `html`     | `.html`         | HTML format                         |
| `markdown` | `.md`           | Markdown format                     |
| `csv`      | `.csv`          | CSV format (for arrays of maps)     |
| `text`     | `.txt`          | Plain text (string conversion)      |

### Behavior

1. Format `source` using the specified format
2. Write formatted string to file at `url`
3. Return `null` on success, `error` on failure

### Auto-Detection

When `format` is omitted:
1. Check file extension for format hint
2. Fall back to `'mark` if unknown

### Examples

```lambda
// Basic usage
let data = {name: "Alice", age: 30};
output(data, "./user.json", 'json)

// Auto-detect format from extension
output(data, "./user.yaml")  // Outputs as YAML

// HTML document output
let page = <html <body <h1 "Hello">>>;
output(page, "./index.html", 'html)

// Batch processing
pn process_files(input_dir: string, output_dir: string) {
    let files = list_files(input_dir);
    for file in files {
        let data = input(file);
        let processed = transform(data);
        let out_path = output_dir + "/" + basename(file);
        output(processed, out_path, 'json)
    }
}
```

### Error Handling

```lambda
let result = output(data, "/readonly/file.json", 'json);
if (result is error) {
    print("Failed to write:", result)
}
```

### Implementation Notes

1. **Location**: Add to `lambda/lambda-proc.cpp` alongside `pn_print`
2. **Dependencies**: Utilize existing formatters in `lambda/format/`
3. **File Writing**: Use lib/file utilities for cross-platform file I/O
4. **URL Support**: Initially support `file://` and relative paths; future: remote URLs

---

## 4. `while` Statement

### Motivation

Add traditional while loop for condition-based iteration where the number of iterations is not known in advance.

### Syntax

```lambda
pn countdown(n: int) {
    var count = n;
    while (count > 0) {
        print(count);
        count = count - 1
    }
    print("Done!")
}
```

### Grammar Changes

Add to `_statement` in `grammar.js`:

```javascript
while_stam: $ => seq(
    'while', '(', field('cond', $._expression), ')',
    '{', field('body', $.content), '}'
),
```

### Semantics

| Construct | Behavior |
|-----------|----------|
| `while (cond) { body }` | Evaluate `cond`; if truthy, execute `body` and repeat; if falsy, exit loop |

> **Note**: `while` is only valid within procedural functions (`pn`). For iteration in functional code, use `for` expressions.

### Examples

```lambda
// Read until EOF
pn read_all(stream: Stream): string {
    var result = "";
    while (not eof(stream)) {
        result = result + read_line(stream)
    }
    return result
}

// Retry with backoff
pn fetch_with_retry(url: string, max_retries: int): Response {
    var attempts = 0;
    while (attempts < max_retries) {
        let response = fetch(url);
        if (response.ok) return response;
        attempts = attempts + 1;
        sleep(attempts * 1000)
    }
    return error("Max retries exceeded")
}

// While with break
pn find_root(n: int): int {
    var x = n;
    while (true) {
        if (x * x <= n) break;
        x = x - 1
    }
    return x
}
```

---

## 5. Mutable Variables with `var`

### Motivation

Procedural code often requires mutable state. The `var` keyword provides explicit mutable variable declarations, distinct from immutable `let` bindings.

### Syntax

```lambda
pn accumulate(items: [int]): int {
    var total = 0;              // Mutable binding
    for item in items {
        total = total + item    // Reassignment allowed
    }
    return total
}
```

### Grammar Changes

```javascript
var_stam: $ => seq(
    'var', field('declare', $.assign_expr), repeat(seq(',', field('declare', $.assign_expr)))
),

assign_stam: $ => seq(
    field('target', $.identifier), '=', field('value', $._expression)
),
```

### Semantics

| Construct | Behavior |
|-----------|----------|
| `var x = expr` | Declare mutable variable `x`, initialize with `expr` |
| `x = expr` | Reassign existing mutable variable `x` |

### Rules

1. **`var` is procedural-only**: Only valid within `pn` functions
2. **`let` remains immutable**: `let` bindings cannot be reassigned
3. **Reassignment requires `var`**: Attempting to reassign a `let` variable is a compile-time error
4. **Shadowing allowed**: Both `let` and `var` can shadow outer bindings

> **Note**: `var` is only valid within procedural functions (`pn`). Functional functions (`fn`) use only immutable `let` bindings.

### Examples

```lambda
// Counter with var
pn count_matches(items: [any], predicate: fn): int {
    var count = 0;
    for item in items {
        if (predicate(item)) {
            count = count + 1
        }
    }
    return count
}

// Swap values
pn swap(a: int, b: int): (int, int) {
    var temp = a;
    var x = a;
    var y = b;
    x = y;
    y = temp;
    return (x, y)
}

// Multiple mutable variables
pn fibonacci(n: int): int {
    var a = 0, b = 1;
    var i = 0;
    while (i < n) {
        var temp = a + b;
        a = b;
        b = temp;
        i = i + 1
    }
    return a
}
```

### Implementation Notes

1. **AST**: Add `AST_VAR_DECL` and `AST_ASSIGN` nodes
2. **Symbol Table**: Track mutability flag for each binding
3. **Transpilation**: `var` compiles to standard C variables; `let` can use `const` where applicable
4. **Error Checking**: Validate reassignment targets are `var` bindings

---

## 6. Future Enhancements

The following features are deferred for future consideration:

### 6.1 `assert` Statement

Debug assertion for procedural code:

```lambda
pn process(data: map) {
    assert data.id is int, "ID must be integer";
    assert data.name != "", "Name cannot be empty";
    // ...
}
```

### 6.2 `try`/`catch` Error Handling

Structured exception handling:

```lambda
pn safe_process(data: any): Result {
    try {
        let result = risky_operation(data);
        return {ok: true, value: result}
    } catch (e) {
        log_error(e);
        return {ok: false, error: e}
    }
}
```

### 6.3 `defer` Statement

Cleanup actions executed on scope exit:

```lambda
pn process_file(path: string) {
    let handle = open_file(path);
    defer close_file(handle);  // Always executed on exit
    
    if (not valid(handle)) return error("Invalid file");
    
    // Process file...
    // close_file(handle) called automatically
}
```

### 6.4 Multiple Return Values

Tuple unpacking for function returns:

```lambda
pn divide(a: int, b: int): (int, int) {
    return (a _/ b, a % b)  // quotient and remainder
}

let (q, r) = divide(17, 5);  // q=3, r=2
```

---

## 7. Implementation Priority

| Priority | Feature | Complexity | Impact |
|----------|---------|------------|--------|
| P0 | `break` / `continue` | Medium | High |
| P0 | `return` statement | Medium | High |
| P0 | `output()` procedure | Low | High |
| P0 | `while` statement | Medium | High |
| P0 | `var` mutable variables | Medium | High |
| P2 | `assert` statement | Low | Medium |
| P2 | `try`/`catch` | High | Medium |
| P3 | `defer` statement | Medium | Low |
| P3 | Multiple returns | Medium | Low |

---

## 8. Grammar Summary

Complete additions to `grammar.js`:

```javascript
// In _statement choice:
$.break_stam,
$.continue_stam,
$.return_stam,
$.while_stam,
$.var_stam,
$.assign_stam,
$.assert_stam,     // future

// New rules:
break_stam: $ => seq('break', optional(';')),

continue_stam: $ => seq('continue', optional(';')),

return_stam: $ => seq(
    'return',
    optional(field('value', $._expression)),
    optional(';')
),

while_stam: $ => seq(
    'while', '(', field('cond', $._expression), ')',
    '{', field('body', $.content), '}'
),

var_stam: $ => seq(
    'var', field('declare', $.assign_expr),
    repeat(seq(',', field('declare', $.assign_expr)))
),

assign_stam: $ => seq(
    field('target', $.identifier), '=', field('value', $._expression),
    optional(';')
),

assert_stam: $ => seq(    // future
    'assert',
    field('cond', $._expression),
    optional(seq(',', field('message', $._expression))),
    optional(';')
),
```

---

## 9. Backward Compatibility

All proposed changes are **additive**:
- New keywords (`break`, `continue`, `return`, `while`, `var`) are reserved within `pn` functions only
- Existing functional code (`fn`) continues to work unchanged
- Procedural features are strictly scoped to `pn` function declarations
- Using procedural-only constructs in `fn` functions results in compile-time errors

---

## 10. References

- [Lambda_Reference.md](Lambda_Reference.md) - Language reference
- [Lambda_Sys_Func_Reference.md](Lambda_Sys_Func_Reference.md) - System functions
- [grammar.js](../lambda/tree-sitter-lambda/grammar.js) - Current grammar
- [lambda-proc.cpp](../lambda/lambda-proc.cpp) - Procedural function implementations
