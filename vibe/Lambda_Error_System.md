# Lambda Error Handling System Proposal

## Overview

This document proposes a structured error handling system for Lambda, adopting an **error-as-return-value** paradigm inspired by Go, Rust, and Zig. Lambda will **not** have try/throw/catch exception handling. Instead, errors are explicit values that flow through the type system, enabling compile-time verification of error handling paths.

## Design Goals

1. **Explicit Error Handling**: Errors must be explicitly handled or propagated
2. **Type Safety**: The type system enforces correct error handling at compile time
3. **Clear Separation**: Normal return paths and error paths are syntactically distinct
4. **Ergonomic Syntax**: Minimal boilerplate while maintaining clarity
5. **Functional Purity**: Errors as values align with Lambda's pure functional nature

---

## Syntax Design

### 1. Function Return Type Annotation

```lambda
// function that may return error of specific type
fn divide(a: int, b: int) int^division_error

// shorthand: function may return any error (unspecified error type)
fn parse(s: string) int^.

// function that never returns error - must succeed
fn add(a: int, b: int) int
```

**Semantics:**
- `T^E` — Returns `T` on success, error type `E` on failure
- `T^.` — Returns `T` on success, any error on failure (error type inferred)
- `T` — Always returns `T`, function body cannot raise errors

> **Note:** The `.` wildcard is used instead of bare `^` for the "any error" shorthand in return types to avoid grammar conflicts where `{...}` would be parsed as a `map_type`.

### 1b. Error Union Type in Parameters and Let Bindings

```lambda
// parameter that accepts T or error
fn process(input: int^) int { ... }

// let binding that may hold T or error  
let result: int^ = may_fail(x)
```

**Semantics:**
- `T^` — Type that is either `T` or `error` (shorthand for `T | error`)
- Used in parameters to accept potentially-errored values
- Used in let bindings to capture results that may be errors

> **Note:** In parameters and let bindings, bare `T^` works because it's followed by `,`, `)`, or `=` which cannot start a type expression. Return types require `T^.` because `{body}` would otherwise be parsed as a map type.

### 2. The `raise` Keyword

Lambda introduces the `raise` keyword to explicitly return an error from a function:

```lambda
fn divide(a: int, b: int) int^division_error =
  if b == 0 then
    raise error("division by zero")
  else
    a / b

fn safe_head(list: list[T]) T^empty_list_error =
  match list with
  | [] -> raise error("list is empty")
  | [x, ..] -> x
```

**Key Properties:**
- `raise` is the **only** way to return an error
- `return` (or implicit return) only returns normal values
- This separation enables static analysis to verify:
  - Normal paths never accidentally return errors
  - All error paths are explicitly marked

### 3. Error Handling at Call Site

**Destructuring syntax** — capture both value and error:

```lambda
let result^err = divide(10, x)
if err != null then
  log("error: " + err.message)
  0  // default value
else
  result * 2
```

**Propagation operator `?`** — auto-propagate errors to caller:

```lambda
fn compute(x: int) int^ =
  let a = parse(input)?      // propagate if error
  let b = divide(a, x)?      // propagate if error
  a + b                       // normal return
```

---

## The `error` Type

Lambda provides a built-in `error` type that integrates with the structured error code system defined in [Lambda_Error_Code.md](Lambda_Error_Code.md).

### Error Structure

The built-in `error` type has the following fields:

| Field | Type | Description |
|-------|------|-------------|
| `code` | `int` | Error code from Lambda's error code system (see below) |
| `message` | `string` | Human-readable error message |
| `file` | `string?` | Source file where error occurred (may be null for REPL) |
| `line` | `int` | 1-based line number |
| `column` | `int` | 1-based column number |
| `source` | `error?` | Wrapped/chained error (for error wrapping) |
| `stack` | `list[frame]?` | Stack trace frames (when enabled) |

### Error Code Categories

Errors are categorized by code ranges (see [Lambda_Error_Code.md](Lambda_Error_Code.md) for full details):

| Range | Category | Description |
|-------|----------|-------------|
| **1xx** | Syntax Errors | Lexical and grammatical errors during parsing |
| **2xx** | Semantic Errors | Type checking, compilation errors |
| **3xx** | Runtime Errors | Execution-time failures |
| **4xx** | I/O Errors | File, network, external resource errors |
| **5xx** | Internal Errors | Unexpected internal states, bugs |

Common error codes include:

| Code | Name | Description |
|------|------|-------------|
| 301 | `null_reference` | Null dereference |
| 302 | `index_out_of_bounds` | Array/list index out of range |
| 303 | `key_not_found` | Map key not found |
| 304 | `division_by_zero` | Division or modulo by zero |
| 318 | `user_error` | User-defined error via `error()` |
| 401 | `file_not_found` | File does not exist |
| 407 | `parse_error` | Error parsing input format |

### Error Constructor Functions

Lambda provides the `error()` function to construct errors:

```lambda
// 1. simple error with message (code defaults to 318 = user_error)
error("something went wrong")

// 2. error wrapping another error (for error chaining)
error("failed to load config", inner_err)

// 3. error with parameter map (full control)
error({
  code: 304,
  message: "division by zero",
  file: "math.ls",
  line: 42,
  column: 15
})
```

**Constructor signatures:**

| Signature                                     | Description                            |
| --------------------------------------------- | -------------------------------------- |
| `error(message: string) error`                | Creates user error with message        |
| `error(message: string, source: error) error` | Creates error wrapping another error   |
| `error(params: map) error`                    | Creates error with explicit parameters |

### Accessing Error Fields

```lambda
let result ^ err = divide(10, 0)
if err != null then
  print("code: " + str(err.code))       // 304
  print("message: " + err.message)       // "division by zero"
  print("location: " + err.file + ":" + str(err.line))
  if err.source != null then
    print("caused by: " + err.source.message)
```

### Custom Error Types

Custom error types extend the base `error` type with additional fields:

```lambda
// define custom error type (lowercase naming)
type parse_error : error = {
  token: string,
  expected: string
}

type network_error : error = {
  status_code: int,
  url: string
}

type config_error : error = {
  config_path: string,
  key: string?
}
```

Creating custom errors:

```lambda
fn parse_int(s: string) int^parse_error =
  if not is_numeric(s) then
    raise parse_error({
      message: "invalid integer format",
      token: s,
      expected: "numeric string"
    })
  else
    to_int(s)
```

### Stack Traces

When stack traces are enabled (via CLI flag or runtime setting), the `stack` field contains the call chain:

```lambda
// stack frame structure
type frame = {
  function: string,    // function name or "<main>"
  file: string,
  line: int,
  column: int
}
```

Example stack trace access:

```lambda
let result^err = process_data(input)
if err != null then
  print("error: " + err.message)
  if err.stack != null then
    for frame in err.stack do
      print("  at " + frame.function + " (" + frame.file + ":" + str(frame.line) + ")")
```

Output:
```
error: index out of bounds
  at process_item (data.ls:78)
  at process_list (data.ls:52)
  at process_data (data.ls:35)
  at <main> (script.ls:12)
```

---

## Comparison with Go, Rust, and Zig

| Aspect | Go | Rust | Zig | Lambda (Proposed) |
|--------|-----|------|-----|-------------------|
| **Error Type** | `(T, error)` tuple | `Result<T, E>` enum | `T!E` error union | `T^E` |
| **Propagation** | Manual `if err != nil` | `?` operator | `try` keyword | `?` operator |
| **Raise/Return** | `return nil, err` | `return Err(e)` | `return error.X` | `raise e` |
| **Unwrap** | Direct access + nil check | `.unwrap()`, `match` | `orelse`, `catch` | Destructuring `^` |
| **Type Safety** | Weak (error can be ignored) | Strong (must handle) | Strong | Strong |

### Strengths of Lambda's Approach

1. **Clearer Intent than Go**
   - Go: `return 0, fmt.Errorf("failed")` — ambiguous, is `0` meaningful?
   - Lambda: `raise error("failed")` — unambiguous error path

2. **Lighter Syntax than Rust**
   - Rust: `Result<T, E>`, `Ok(v)`, `Err(e)` — verbose wrapper types
   - Lambda: `T^E`, direct value or `raise` — less syntactic overhead

3. **More Explicit than Zig**
   - Zig: `!T` prefix notation can be visually lost
   - Lambda: `T^E` suffix keeps error type visible at the end

4. **Functional Alignment**
   - The `raise` keyword semantically aligns with "raising" an exceptional condition
   - Destructuring `let a^b = ...` fits Lambda's pattern matching philosophy

### Potential Weaknesses to Address

1. **Error Type Inference**: Need clear rules for when `T^` infers error types
2. **Nested Errors**: Define how `T^E1^E2` or error composition works
3. **Error Context**: Consider error chaining/wrapping mechanism

---

## Static Analysis Requirements

The compiler must enforce:

1. **Functions with `T` return type cannot contain `raise`**
   ```lambda
   fn pure_add(a: int, b: int) int =
     raise error("oops")  // ❌ compile error: function does not declare error return
   ```

2. **Error-returning functions must be handled**
   ```lambda
   fn main() =
     divide(10, 0)  // ❌ compile error: unhandled error return
   ```

3. **Normal return cannot be error type**
   ```lambda
   fn broken() int^error =
     return error("oops")  // ❌ compile error: use 'raise' for errors
   ```

4. **`?` only valid in error-returning functions**
   ```lambda
   fn pure() int =
     parse("42")?  // ❌ compile error: cannot propagate error from non-error function
   ```

---

## Examples

### File Processing

```lambda
fn process_file(path: string) processed_data^file_error =
  let content = read_file(path)?
  let lines = split(content, "\n")
  let parsed = map(lines, parse_line)?
  aggregate(parsed)

fn parse_line(line: string) line_data^parse_error =
  let parts = split(line, ",")
  if length(parts) < 3 then
    raise error("insufficient fields in line")
  else
    line_data {
      id: parts[0],
      name: parts[1],
      value: parse_int(parts[2])?
    }
```

### HTTP Client

```lambda
fn fetch_user(id: int) user^network_error =
  let response = http_get("/users/" + str(id))?
  if response.status != 200 then
    raise network_error({
      message: "user not found",
      status_code: response.status,
      url: "/users/" + str(id)
    })
  else
    parse_json(response.body)?
```

### Config Loading with Error Wrapping

```lambda
fn load_config(path: string) config^config_error =
  let content^file_err = read_file(path)
  if file_err != null then
    raise error("failed to read config file", file_err)
  
  let parsed^parse_err = parse_json(content)
  if parse_err != null then
    raise error("invalid JSON in config", parse_err)
  
  validate_config(parsed)?
```

---

## Summary

Lambda's proposed error handling system combines:

- **Go's simplicity**: Errors as values, explicit handling
- **Rust's safety**: Type system enforces handling, `?` propagation
- **Zig's clarity**: Distinct error types, no hidden control flow

With Lambda-specific innovations:

- **`raise` keyword**: Unambiguous error return, enables static verification
- **`T^E` syntax**: Readable, semantic, pairs with destructuring
- **Structured error codes**: Integration with Lambda's error code system (1xx-5xx)
- **Error wrapping**: `error(msg, source)` for error chaining

This design maintains Lambda's pure functional character while providing robust, type-safe error handling with minimal syntactic overhead.

---

## Open Questions

1. Should `T^` (unspecified error) be allowed, or require explicit error types?
2. How to handle errors in async/concurrent contexts?
3. Should there be a `panic!` equivalent for unrecoverable errors?
4. Error interop with C/FFI functions?

---

## Appendix A: Grammar Changes

```
// return type with error
return_type
  : type
  | type '^' type        // T^error_type
  | type '^'             // T^ (any error)

// raise statement
raise_stmt
  : 'raise' expression

// error destructuring
let_pattern
  : identifier '^' identifier '=' expression

// error propagation
postfix_expr
  : primary_expr '?'
```

---

## Appendix B: Syntax Alternatives Analysis

Several syntax options were considered for error return types:

| Syntax | Example | Pros | Cons |
|--------|---------|------|------|
| `T^err` | `fn() int^parse_error` | Visually distinct, "raises" connotation | `^` used elsewhere? (exponent) |
| `T#err` | `fn() int#parse_error` | Clear separator | `#` often means comment |
| `T%err` | `fn() int%parse_error` | Unused in most languages | Resembles modulo operator |
| `!T` | `fn() !int` | Zig-familiar, concise | Prefix loses error type, `!` means "not" |

### Decision: `T^E` and `T@`

**`T^E` was chosen** for Lambda for explicit error types because:

1. **Semantic Meaning**: `^` visually suggests "raising" or "lifting" — matching the `raise` keyword
2. **Readability**: Suffix notation keeps the primary return type prominent: `int^error` reads as "int that may raise error"
3. **Distinctiveness**: Unlike `!` (negation), `#` (comments), or `%` (modulo), `^` has no conflicting meaning in Lambda's domain
4. **Consistency**: Pairs naturally with destructuring `let a^b = ...`
5. **Scanability**: Error type at the end mirrors how we read "returns X, or fails with Y"

**`T@` was chosen** for "any error" shorthand because:

1. **Grammar Compatibility**: Bare `T^` followed by `{` creates ambiguity with the `^` exponentiation operator, where the parser interprets `{...}` as a map operand
2. **Visual Distinction**: `@` is visually distinct and unambiguous
3. **Mnemonic**: `@` can be read as "at risk" — the function is "at risk" of returning an error
4. **Unused Symbol**: `@` has no other meaning in Lambda's syntax

**Summary:**
- `T^E` — explicit error type (e.g., `int^division_error`)
- `T@` — any error type, inferred (e.g., `int@`)
- `T` — no error possible

---

## Appendix C: Error Recovery Operators (Future)

These operators are deferred for future consideration:

### Default on Error (`??`)

Like Rust's `unwrap_or`, provides a default value when error occurs:

```lambda
let value = risky_fn()? ?? default_value
```

### Map Error (`?^`)

Transform error to a different type:

```lambda
let value = risky_fn()? ?^ |e| transform_error(e)
```

### Recover with Fallback (`?:`)

Call a fallback function on error:

```lambda
let value = risky_fn()? ?: fallback_fn
```

### Combined Example

```lambda
fn get_config_value(key: string) string =
  // try primary source, fall back to secondary, then default
  read_config(key)? ?? read_env(key)? ?? "default"
```

---

## Appendix D: Alternative "Any Error" Syntax Considered

During the design of the error return type syntax, several alternatives were considered for the "any error" shorthand (when the specific error type is not specified).

### The Problem

The intuitive syntax `T^` (bare caret) cannot be used because Tree-sitter greedily parses `{ body }` as a `map_type` for the error field:

```lambda
fn test() int^ { 42 }  // FAILS: { 42 } parsed as error type!
```

### Alternatives Considered

| Syntax | Pros                                     | Cons                             | Decision     |
| ------ | ---------------------------------------- | -------------------------------- | ------------ |
| `T^`   | Most intuitive                           | Grammar conflict with `map_type` | ❌ Rejected   |
| `T@`   | Clear terminator, "at risk" mnemonic     | Mixes character families         | ❌ Rejected   |
| `T^*`  | Universal wildcard semantics             | Visually prominent               | ❌ Rejected   |
| `T^^`  | Consistent character                     | No semantic meaning              | ❌ Rejected   |
| `T^.`  | Minimal, consistent with string patterns | Subtle                           | ✅ **Chosen** |
| `T^_`  | Minimal, "don't care" convention         | Visually prominent               | ❌ Rejected   |

### Rationale for `T^.`

1. **Consistency**: The `.` already means "any character" in Lambda's string pattern syntax
2. **Minimalism**: Single character, doesn't clutter the type signature
3. **No ambiguity**: `.` cannot start a type expression, so no grammar conflicts
4. **Semantic fit**: Wildcard meaning is established in pattern matching contexts

### Examples

```lambda
// Explicit error type
fn divide(a, b) int^division_error { ... }

// Any error (wildcard)
fn parse(s) int^. { ... }

// No error possible
fn add(a, b) int { ... }
```
